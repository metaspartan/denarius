// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2017-2021 The Denarius developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "alert.h"
#include "checkpoints.h"
#include "db.h"
#include "txdb.h"
#include "net.h"
#include "init.h"
#include "ui_interface.h"
#include "kernel.h"
#include "fortuna.h"
#include "fortunastake.h"
#include "spork.h"
#include "smessage.h"
#include "namecoin.h"
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#if BOOST_VERSION >= 107300
#include <boost/bind/bind.hpp>
using boost::placeholders::_1;
using boost::placeholders::_2;
#else
#include <boost/bind.hpp>
#endif

using namespace std;
using namespace boost;

//
// Global state
//

CCriticalSection cs_setpwalletRegistered;
set<CWallet*> setpwalletRegistered;

CCriticalSection cs_main;

CTxMemPool mempool;
//unsigned int nTransactionsUpdated = 0;

map<uint256, CBlockIndex*> mapBlockIndex;
set<pair<COutPoint, unsigned int> > setStakeSeen;

CBigNum bnProofOfWorkLimit(~uint256(0) >> 20);      // "standard" scrypt target limit for proof of work, results with 0,000244140625 proof-of-work difficulty
CBigNum bnProofOfStakeLimit(~uint256(0) >> 20);
CBigNum bnProofOfWorkLimitTestNet(~uint256(0) >> 16);

/** Fees smaller than this (in denarii) are considered zero fee (for relaying and mining) */
// CFeeRate minRelayTxFee = CFeeRate(SUBCENT);

// Block Variables

unsigned int nTargetSpacing     = 30;               // 30 seconds, FAST
unsigned int nStakeMinAge       = 8 * 60 * 60;      // 8 hour min stake age
unsigned int nStakeMaxAge       = -1;               // unlimited
unsigned int nModifierInterval  = 10 * 60;          // time to elapse before new modifier is computed
int64_t nLastCoinStakeSearchTime = GetAdjustedTime();
int nCoinbaseMaturity = 20; //30 on Mainnet D e n a r i u s, 30 for testnet
CBlockIndex* pindexGenesisBlock = NULL;
int nBestHeight = -1;
bool FortunaReorgBlock = true;
uint256 nBestChainTrust = 0;
uint256 nBestInvalidTrust = 0;

uint256 hashBestChain = 0;
CBlockIndex* pindexBest = NULL;
int64_t nTimeBestReceived = 0;

bool fImporting = false;
bool fReindex = false;
bool fAddrIndex = false;

CMedianFilter<int> cPeerBlockCounts(5, 0); // Amount of blocks that other nodes claim to have

std::map<int64_t, CAnonOutputCount> mapAnonOutputStats;
//map<int64_t, CAnonOutputCount> mapAnonOutputStats; // display only, not 100% accurate, height could become inaccurate due to undos
map<uint256, CBlock*> mapOrphanBlocks;
multimap<uint256, CBlock*> mapOrphanBlocksByPrev;
set<pair<COutPoint, unsigned int> > setStakeSeenOrphan;

map<uint256, CTransaction> mapOrphanTransactions;
map<uint256, set<uint256> > mapOrphanTransactionsByPrev;

// Constant stuff for coinbase transactions we create:
CScript COINBASE_FLAGS;

const string strMessageMagic = "Denarius Signed Message:\n";

// Settings
int64_t nTransactionFee = MIN_TX_FEE;
int64_t nReserveBalance = 0;
int64_t nMinimumInputValue = 0;

unsigned int nCoinCacheSize = 5000;

extern enum Checkpoints::CPMode CheckpointsMode;

std::set<uint256> setValidatedTx;

CHooks* hooks; // This adds Denarius Name DB hooks which allow splicing of code inside standard Denarius functions.

//////////////////////////////////////////////////////////////////////////////
//
// dispatching functions
//

// These functions dispatch to one or all registered wallets

namespace {
struct CMainSignals {
    // Notifies listeners of updated transaction data (passing hash, transaction, and optionally the block it is found in.
    boost::signals2::signal<void (const CTransaction &, const CBlock *, bool)> SyncTransaction;
    // Notifies listeners of an erased transaction (currently disabled, requires transaction replacement).
    boost::signals2::signal<void (const uint256 &)> EraseTransaction;
    // Notifies listeners of an updated transaction without new data (for now: a coinbase potentially becoming visible).
    boost::signals2::signal<void (const uint256 &)> UpdatedTransaction;
    // Notifies listeners of a new active block chain.
    boost::signals2::signal<void (const CBlockLocator &)> SetBestChain;
    // Notifies listeners about an inventory item being seen on the network.
    boost::signals2::signal<void (const uint256 &)> Inventory;
    // Tells listeners to broadcast their data.
    boost::signals2::signal<void (bool)> Broadcast;

} g_signals;
}

void RegisterWallet(CWallet* pwalletIn) {
    g_signals.EraseTransaction.connect(boost::bind(&CWallet::EraseFromWallet, pwalletIn, _1));
    g_signals.UpdatedTransaction.connect(boost::bind(&CWallet::UpdatedTransaction, pwalletIn, _1));
    g_signals.SetBestChain.connect(boost::bind(&CWallet::SetBestChain, pwalletIn, _1));
    g_signals.Inventory.connect(boost::bind(&CWallet::Inventory, pwalletIn, _1));
    g_signals.Broadcast.connect(boost::bind(&CWallet::ResendWalletTransactions, pwalletIn, _1));
    {
            LOCK(cs_setpwalletRegistered);
            setpwalletRegistered.insert(pwalletIn);
    }
}

void UnregisterWallet(CWallet* pwalletIn) {
    g_signals.Broadcast.disconnect(boost::bind(&CWallet::ResendWalletTransactions, pwalletIn, _1));
    g_signals.Inventory.disconnect(boost::bind(&CWallet::Inventory, pwalletIn, _1));
    g_signals.SetBestChain.disconnect(boost::bind(&CWallet::SetBestChain, pwalletIn, _1));
    g_signals.UpdatedTransaction.disconnect(boost::bind(&CWallet::UpdatedTransaction, pwalletIn, _1));
    g_signals.EraseTransaction.disconnect(boost::bind(&CWallet::EraseFromWallet, pwalletIn, _1));
    {
            LOCK(cs_setpwalletRegistered);
            setpwalletRegistered.erase(pwalletIn);
    }
}


// check whether the passed transaction is from us
bool static IsFromMe(CTransaction& tx)
{
    for (CWallet* pwallet : setpwalletRegistered)
        if (pwallet->IsFromMe(tx))
            return true;
    return false;
}


// get the wallet transaction with the given hash (if it exists)
bool static GetTransaction(const uint256& hashTx, CWalletTx& wtx)
{
    for (CWallet* pwallet : setpwalletRegistered)
        if (pwallet->GetTransaction(hashTx,wtx))
            return true;
    return false;
}

// erases transaction with the given hash from all wallets
void static EraseFromWallets(uint256 hash)
{
    for (CWallet* pwallet : setpwalletRegistered)
        pwallet->EraseFromWallet(hash);
}

// make sure all wallets know about the given transaction, in the given block
void SyncWithWallets(const CTransaction& tx, const CBlock* pblock, bool fUpdate, bool fConnect)
{
    if (!fConnect)
    {
        // ppcoin: wallets need to refund inputs when disconnecting coinstake
        if (tx.IsCoinStake())
        {
            for (CWallet* pwallet : setpwalletRegistered)
            {
                if (pwallet->IsFromMe(tx))
                    pwallet->DisableTransaction(tx);
            };
        };

        if (tx.nVersion == ANON_TXN_VERSION)
        {
            for (CWallet* pwallet : setpwalletRegistered)
                pwallet->UndoAnonTransaction(tx);
        };
        return;
    };

    //uint256 hash = tx.GetHash();
    for (CWallet* pwallet : setpwalletRegistered)
        pwallet->AddToWalletIfInvolvingMe(tx, pblock, fUpdate);
}

// notify wallets about a new best chain
void static SetBestChain(const CBlockLocator& loc)
{
    for (CWallet* pwallet : setpwalletRegistered)
        pwallet->SetBestChain(loc);
}

// notify wallets about an updated transaction
void static UpdatedTransaction(const uint256& hashTx)
{
    for (CWallet* pwallet : setpwalletRegistered)
        pwallet->UpdatedTransaction(hashTx);
}
/*
// dump all wallets
void static PrintWallets(const CBlock& block)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        pwallet->PrintWallet(block);
} */

// notify wallets about an incoming inventory (for request counts)
void static Inventory(const uint256& hash)
{
    for (CWallet* pwallet : setpwalletRegistered)
        pwallet->Inventory(hash);
}

// ask wallets to resend their transactions
void ResendWalletTransactions(bool fForce)
{
    for (CWallet* pwallet : setpwalletRegistered)
        pwallet->ResendWalletTransactions(fForce);
}

bool Finalise()
{
    printf("Finalise()");

    LOCK(cs_main);

    SecureMsgShutdown();
    //nTransactionsUpdated++;
    mempool.AddTransactionsUpdated(1);
    bitdb.Flush(false);
    StopNode();
    bitdb.Flush(true);
    boost::filesystem::remove(GetPidFile());
    UnregisterWallet(pwalletMain);
    delete pwalletMain;

    finaliseRingSigs();

    CTxDB().Close();


    return true;
}

bool AbortNode(const std::string &strMessage, const std::string &userMessage) {
    strMiscWarning = strMessage;
    printf("*** %s\n", strMessage.c_str());
	/*
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occured, see debug.log for details") : userMessage,
        "", CClientUIInterface::MSG_ERROR);
		*/
    StartShutdown();
    return false;
}

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats)
{
    // TODO:
    return false;
}

//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

bool AddOrphanTx(const CTransaction& tx)
{
    uint256 hash = tx.GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:

    size_t nSize = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);

    if (nSize > 5000)
    {
        printf("ignoring large orphan tx (size: %" PRIszu", hash: %s)\n", nSize, hash.ToString().substr(0,10).c_str());
        return false;
    };

    mapOrphanTransactions[hash] = tx;
    for (const CTxIn& txin : tx.vin)
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    printf("stored orphan tx %s (mapsz %" PRIszu")\n", hash.ToString().substr(0,10).c_str(),
        mapOrphanTransactions.size());
    return true;
}

void static EraseOrphanTx(uint256 hash)
{
    if (!mapOrphanTransactions.count(hash))
        return;
    const CTransaction& tx = mapOrphanTransactions[hash];
    for (const CTxIn& txin : tx.vin)
    {
        mapOrphanTransactionsByPrev[txin.prevout.hash].erase(hash);
        if (mapOrphanTransactionsByPrev[txin.prevout.hash].empty())
            mapOrphanTransactionsByPrev.erase(txin.prevout.hash);
    }
    mapOrphanTransactions.erase(hash);
}

unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans)
{
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        map<uint256, CTransaction>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}







//////////////////////////////////////////////////////////////////////////////
//
// CTransaction and CTxIndex
//

// CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::CURRENT_VERSION), nTime(GetAdjustedTime()), nLockTime(0) {}
// CMutableTransaction::CMutableTransaction(const CTransaction& tx) : nVersion(tx.nVersion), nTime(tx.nTime), vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime) {}

// uint256 CMutableTransaction::GetHash() const
// {
//     return SerializeHash(*this);
// }

// void CTransaction::UpdateHash() const
// {
//     *const_cast<uint256*>(&hash) = SerializeHash(*this);
// }

// CTransaction::CTransaction() : hash(0), nVersion(CTransaction::CURRENT_VERSION), nTime(GetAdjustedTime()), vin(), vout(), nLockTime(0) { }

// CTransaction::CTransaction(const CMutableTransaction &tx) : nVersion(tx.nVersion), nTime(tx.nTime), vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime) {
//     UpdateHash();
// }



bool CTransaction::ReadFromDisk(CTxDB& txdb, COutPoint prevout, CTxIndex& txindexRet)
{
    SetNull();
    if (!txdb.ReadTxIndex(prevout.hash, txindexRet))
        return false;
    if (!ReadFromDisk(txindexRet.pos))
        return false;
    if (prevout.n >= vout.size())
    {
        SetNull();
        return false;
    }
    return true;
}

bool CTransaction::ReadFromDisk(CTxDB& txdb, COutPoint prevout)
{
    CTxIndex txindex;
    return ReadFromDisk(txdb, prevout, txindex);
}

bool CTransaction::ReadFromDisk(COutPoint prevout)
{
    CTxDB txdb("r");
    CTxIndex txindex;
    return ReadFromDisk(txdb, prevout, txindex);
}

// bool CTransaction::IsStandard() const
// {
//     if (nVersion > CTransaction::CURRENT_VERSION)
//         return false;

//     BOOST_FOREACH(const CTxIn& txin, vin)
//     {
//         // Biggest 'standard' txin is a 3-signature 3-of-3 CHECKMULTISIG
//         // pay-to-script-hash, which is 3 ~80-byte signatures, 3
//         // ~65-byte public keys, plus a few script ops.
//         if (txin.scriptSig.size() > 500)
//             return false;
//         if (!txin.scriptSig.IsPushOnly())
//             return false;
//         if (fEnforceCanonical && !txin.scriptSig.HasCanonicalPushes()) {
//             return false;
//         }
//     }

//     unsigned int nDataOut = 0;
//     unsigned int nTxnOut = 0;

//     txnouttype whichType;
//     BOOST_FOREACH(const CTxOut& txout, vout) {
//         if (!::IsStandard(txout.scriptPubKey, whichType))
//             return false;
//         if (whichType == TX_NULL_DATA)
//         {
//             nDataOut++;
//         } else
//         {
//             if (txout.nValue == 0)
//                 return false;
//             nTxnOut++;
//         }
//         if (fEnforceCanonical && !txout.scriptPubKey.HasCanonicalPushes()) {
//             return false;
//         }
//     }

//     // only one OP_RETURN txout per txn out is permitted
//     if (nDataOut > nTxnOut) {
//         return false;
//     }

//     return true;
// }

bool IsStandardTx(const CTransaction& tx, string& reason)
{
    if (tx.nVersion > CTransaction::CURRENT_VERSION && tx.nVersion != ANON_TXN_VERSION && tx.nVersion != NAMECOIN_TX_VERSION) { //WIP
        reason = "version";
        return false;
    }

    // Treat non-final transactions as non-standard to prevent a specific type
    // of double-spend attack, as well as DoS attacks. (if the transaction
    // can't be mined, the attacker isn't expending resources broadcasting it)
    // Basically we don't want to propagate transactions that can't be included in
    // the next block.
    //
    // However, IsFinalTx() is confusing... Without arguments, it uses
    // chainActive.Height() to evaluate nLockTime; when a block is accepted, chainActive.Height()
    // is set to the value of nHeight in the block. However, when IsFinalTx()
    // is called within CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a transaction can
    // be part of the *next* block, we need to call IsFinalTx() with one more
    // than chainActive.Height().
    //
    // Timestamps on the other hand don't get any special treatment, because we
    // can't know what timestamp the next block will have, and there aren't
    // timestamp applications where it matters.
    //if (!IsFinalTx(tx, nBestHeight + 1)) {
	  if (!tx.IsFinal(nBestHeight + 1)) {
        reason = "non-final";
        return false;
    }
    // nTime has different purpose from nLockTime but can be used in similar attacks
    if (tx.nTime > FutureDrift(GetAdjustedTime())) {
        reason = "time-too-new";
        return false;
    }

    // Extremely large transactions with lots of inputs can cost the network
    // almost as much to process as they cost the sender in fees, because
    // computing signature hashes is O(ninputs*txsize). Limiting transactions
    // to MAX_STANDARD_TX_SIZE mitigates CPU exhaustion attacks.
    unsigned int sz = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    if (sz >= MAX_STANDARD_TX_SIZE) {
        reason = "tx-size";
        return false;
    }

    for (const CTxIn& txin : tx.vin)
    {
        if (txin.IsAnonInput())
        {
            int nRingSize = txin.ExtractRingSize();

            if (tx.nVersion != ANON_TXN_VERSION
                || nRingSize < (int)MIN_RING_SIZE
                || nRingSize > (int)MAX_RING_SIZE
                || txin.scriptSig.size() > sizeof(COutPoint) + 2 + (33 + 32 + 32) * nRingSize)
            {
                printf("IsStandard() anon txin failed.\n");
                return false;
            };
            continue;
        };
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys. (remember the 520 byte limit on redeemScript size) That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard)
        if (txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
        if (!txin.scriptSig.HasCanonicalPushes()) {
            reason = "scriptsig-non-canonical-push";
            return false;
        }
    }

    unsigned int nDataOut = 0;
    unsigned int nTxnOut = 0;

    txnouttype whichType;
    for (const CTxOut& txout : tx.vout) {
        if (txout.IsAnonOutput())
        {
            if (tx.nVersion != ANON_TXN_VERSION
                || txout.nValue < 1
                || txout.scriptPubKey.size() > MIN_ANON_OUT_SIZE + MAX_ANON_NARRATION_SIZE)
            {
                printf("IsStandard() anon txout failed.\n");
                return false;
            }
            //nTxnOut++; anon outputs don't count (narrations are embedded in scriptPubKey)
            continue;
        };

         if (!::IsStandard(txout.scriptPubKey, whichType)) {
             reason = "scriptpubkey";
             return false;
         }
         if (whichType == TX_NULL_DATA)
         {
             nDataOut++;
         } else
         {
             if (txout.nValue == 0)
                 return false;
             nTxnOut++;
         }
         if (fEnforceCanonical && !txout.scriptPubKey.HasCanonicalPushes()) {
             reason = "scriptpubkey-non-canonical-push";
             return false;
         }
    }

    // only one OP_RETURN txout per txn out is permitted
    if (nDataOut > nTxnOut) {
        reason = "multi-op-return";
        return false;
    }

    return true;
}

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    AssertLockHeld(cs_main);
    // Time based nLockTime implemented in 0.1.6
    if (tx.nLockTime == 0)
        return true;
    if (nBlockHeight == 0)
        nBlockHeight = nBestHeight;
    if (nBlockTime == 0)
        nBlockTime = GetAdjustedTime();
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const CTxIn& txin : tx.vin)
        if (!txin.IsFinal())
            return false;
    return true;
}

//
// Check transaction inputs, and make sure any
// pay-to-script-hash transactions are evaluating IsStandard scripts
//
// Why bother? To avoid denial-of-service attacks; an attacker
// can submit a standard HASH... OP_EQUAL transaction,
// which will get accepted into blocks. The redemption
// script can be anything; an attacker could use a very
// expensive-to-check-upon-redemption script like:
//   DUP CHECKSIG DROP ... repeated 100 times... OP_1
//
bool AreInputsStandard(const CTransaction& tx, const MapPrevTx& mapInputs)
{
    if (tx.IsCoinBase())
        return true; // Coinbases don't use vin normally

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        if (tx.nVersion == ANON_TXN_VERSION
            && tx.vin[i].IsAnonInput())
            continue;

        const CTxOut& prev = tx.GetOutputFor(tx.vin[i], mapInputs);

        vector<vector<unsigned char> > vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions))
            return false;
        int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
        if (nArgsExpected < 0)
            return false;

        // Transactions with extra stuff in their scriptSigs are
        // non-standard. Note that this EvalScript() call will
        // be quick, because if there are any operations
        // beside "push data" in the scriptSig
        // IsStandard() will have already returned false
        // and this method isn't called.
        vector<vector<unsigned char> > stack;
        if (!EvalScript(stack, tx.vin[i].scriptSig, tx, i, SCRIPT_VERIFY_NONE, 0))
            return false;

        if (whichType == TX_SCRIPTHASH)
        {
            if (stack.empty())
                return false;
            CScript subscript(stack.back().begin(), stack.back().end());
            vector<vector<unsigned char> > vSolutions2;
            txnouttype whichType2;
            if (Solver(subscript, whichType2, vSolutions2))
            {
                int tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);
                if (tmpExpected < 0)
                    return false;
                nArgsExpected += tmpExpected;
            }
            else
            {
                // Any other Script with less than 15 sigops OK:
                unsigned int sigops = subscript.GetSigOpCount(true);
                // ... extra data left on the stack after execution is OK, too:
                return (sigops <= MAX_P2SH_SIGOPS);
            }
        }

        if (stack.size() != (unsigned int)nArgsExpected)
            return false;
    }

    return true;
}

bool CTransaction::HasStealthOutput() const
{
    // -- todo: scan without using GetOp

    std::vector<uint8_t> vchEphemPK;
    opcodetype opCode;

    for (vector<CTxOut>::const_iterator it = vout.begin(); it != vout.end(); ++it)
    {
        if (nVersion == ANON_TXN_VERSION
            && it->IsAnonOutput())
            continue;

        CScript::const_iterator itScript = it->scriptPubKey.begin();

        if (!it->scriptPubKey.GetOp(itScript, opCode, vchEphemPK)
            || opCode != OP_RETURN
            || !it->scriptPubKey.GetOp(itScript, opCode, vchEphemPK) // rule out np narrations
            || vchEphemPK.size() != ec_compressed_size)
            continue;

        return true;
    };

    return false;
};

unsigned int CTransaction::GetLegacySigOpCount() const
{
    unsigned int nSigOps = 0;
    for (const CTxIn& txin : vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    };
    for (const CTxOut& txout : vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    };
    return nSigOps;
}


int CMerkleTx::SetMerkleBranch(const CBlock* pblock)
{
    AssertLockHeld(cs_main);

    CBlock blockTmp;
    if (pblock == NULL)
    {
        // Load the block this tx is in
        CTxIndex txindex;
        if (!CTxDB("r").ReadTxIndex(GetHash(), txindex))
            return 0;
        if (!blockTmp.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos))
            return 0;
        pblock = &blockTmp;
    }

    // Update the tx's hashBlock
    hashBlock = pblock->GetHash();

    // Locate the transaction
    for (nIndex = 0; nIndex < (int)pblock->vtx.size(); nIndex++)
        if (pblock->vtx[nIndex] == *(CTransaction*)this)
            break;
    if (nIndex == (int)pblock->vtx.size())
    {
        vMerkleBranch.clear();
        nIndex = -1;
        printf("ERROR: SetMerkleBranch() : couldn't find tx in block\n");
        return 0;
    }

    // Fill in merkle branch
    vMerkleBranch = pblock->GetMerkleBranch(nIndex);

    // Is the tx in a block that's in the main chain
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;

    return pindexBest->nHeight - pindex->nHeight + 1;
}







bool CTransaction::CheckTransaction() const
{
    // Basic checks that don't depend on any context
    if (vin.empty())
        return DoS(10, error("CTransaction::CheckTransaction() : vin empty"));
    if (vout.empty())
        return DoS(10, error("CTransaction::CheckTransaction() : vout empty"));
    // Size limits
    if (::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return DoS(100, error("CTransaction::CheckTransaction() : size limits failed"));

    // Check for negative or overflow output values
    int64_t nValueOut = 0;
    for (unsigned int i = 0; i < vout.size(); i++)
    {
        const CTxOut& txout = vout[i];
        if (txout.IsEmpty() && !IsCoinBase() && !IsCoinStake())
            return DoS(100, error("CTransaction::CheckTransaction() : txout empty for user transaction"));
        if (txout.nValue < 0)
            return DoS(100, error("CTransaction::CheckTransaction() : txout.nValue negative"));
        if (txout.nValue > MAX_MONEY)
            return DoS(100, error("CTransaction::CheckTransaction() : txout.nValue too high"));
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return DoS(100, error("CTransaction::CheckTransaction() : txout total out of range"));
    }

    // Check for duplicate inputs
    set<COutPoint> vInOutPoints;
    for (const CTxIn& txin : vin)
    {
        if (nVersion == ANON_TXN_VERSION
            && txin.IsAnonInput())
        {
            // -- blank the upper 3 bytes of n to prevent the same keyimage passing with different ring sizes
            COutPoint opTest = txin.prevout;
            opTest.n &= 0xFF;
            if (vInOutPoints.count(opTest))
            {
                if (fDebugRingSig)
                    printf("CheckTransaction() failed - found duplicate keyimage in txn %s\n", GetHash().ToString().c_str());
                return false;
            };
            vInOutPoints.insert(opTest);
            continue;
        };

        if (vInOutPoints.count(txin.prevout))
            return false;
        vInOutPoints.insert(txin.prevout);
    };

    if (nVersion == ANON_TXN_VERSION)
    {
        // -- Check for duplicate anon outputs
        // NOTE: is this necessary, duplicate coins would not be spendable anyway?
        set<CPubKey> vAnonOutPubkeys;
        CPubKey pkTest;
        for (const CTxOut& txout : vout)
        {
            if (!txout.IsAnonOutput())
                continue;

            const CScript &s = txout.scriptPubKey;
            pkTest = CPubKey(&s[2+1], 33);
            if (vAnonOutPubkeys.count(pkTest))
                return false;
            vAnonOutPubkeys.insert(pkTest);
        };
    };

    if (IsCoinBase())
    {
        if (vin[0].scriptSig.size() < 2 || vin[0].scriptSig.size() > 100)
            return DoS(100, error("CTransaction::CheckTransaction() : coinbase script size is invalid"));
    }
    else
    {
        for (const CTxIn& txin : vin)
            if (txin.prevout.IsNull())
                return DoS(10, error("CTransaction::CheckTransaction() : prevout is null"));
    } //New ban code for hybrid fortunastakes and FMPS - Not for prime time yet, may or may not be used
	/*
	else
	{
		BOOST_FOREACH(const CTxIn& txin, vin)
			if (txin.prevout.IsBanned()){ // new function that checks if the txin.prevout matches an address
				txin.prevout.SetNull(); // this should set the UTXO to null
				return DoS(10, error("CheckTransaction(): You have been caught trying to cheat. Kthxbai"));
			}
	}
	*/
    //return hooks->CheckTransaction(*this);
    return true;
}

int64_t CTransaction::GetMinFee(unsigned int nBlockSize, enum GetMinFee_mode mode, unsigned int nBytes) const
{
    // Base fee is either MIN_TX_FEE or MIN_RELAY_TX_FEE for standard txns, and MIN_TX_FEE_ANON for anon txns

    // -- force GMF_ANON if anon txn
    if (nVersion == ANON_TXN_VERSION)
        mode = GMF_ANON;

    int64_t nBaseFee;
    switch (mode)
    {
        case GMF_RELAY: nBaseFee = MIN_RELAY_TX_FEE; break;
        case GMF_ANON:  nBaseFee = MIN_TX_FEE_ANON;  break;
        default:        nBaseFee = MIN_TX_FEE;       break;
    };

    unsigned int nNewBlockSize = nBlockSize + nBytes;
    int64_t nMinFee = (1 + (int64_t)nBytes / 1000) * nBaseFee;

    // To limit dust spam, require MIN_TX_FEE/MIN_RELAY_TX_FEE if any output is less than 0.01
    if (nMinFee < nBaseFee)
    {
        for (const CTxOut& txout : vout)
            if (txout.nValue < CENT)
                nMinFee = nBaseFee;
    };

    // Raise the price as the block approaches full
    if (mode != GMF_ANON && nBlockSize != 1 && nNewBlockSize >= MAX_BLOCK_SIZE_GEN/2)
    {
        if (nNewBlockSize >= MAX_BLOCK_SIZE_GEN)
            return MAX_MONEY;
        nMinFee *= MAX_BLOCK_SIZE_GEN / (MAX_BLOCK_SIZE_GEN - nNewBlockSize);
    };

    if (!MoneyRange(nMinFee))
        nMinFee = MAX_MONEY;
    return nMinFee;
}

bool CTxMemPool::accept(CTxDB& txdb, CTransaction &tx, bool fCheckInputs,
                        bool* pfMissingInputs, bool fOnlyCheckWithoutAdding)
{
    AssertLockHeld(cs_main);
    printf("CTxMemPool::accept, fCheckInputs = %d, fOnlyCheckWithoutAdding = %d\n", fCheckInputs, fOnlyCheckWithoutAdding);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    if (!tx.CheckTransaction())
        return error("CTxMemPool::accept() : CheckTransaction failed");

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return tx.DoS(100, error("CTxMemPool::accept() : coinbase as individual tx"));

    // ppcoin: coinstake is also only valid in a block, not as a loose transaction
    if (tx.IsCoinStake())
        return tx.DoS(100, error("CTxMemPool::accept() : coinstake as individual tx"));
    
    //bool isNameTx = hooks->IsNameFeeEnough(txdb, tx); //accept name tx with correct fee.
    bool isNameTx = tx.nVersion == NAMECOIN_TX_VERSION;
    // Rather not work on nonstandard transactions (unless -testnet)
    string reason;
    if (!fTestNet && !IsStandardTx(tx, reason) && !isNameTx) //!IsStandardTx(tx, reason)
        return error("CTxMemPool::accept() : nonstandard transaction type");

    // Do we already have it?
    uint256 hash = tx.GetHash();
    {
        LOCK(cs);
        if (mapTx.count(hash))
            return false;
    }

    if (txdb.ContainsTx(hash))
        return false;

    // Check for conflicts with in-memory transactions
    CTransaction* ptxOld = NULL;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        COutPoint outpoint = tx.vin[i].prevout;
        if (mapNextTx.count(outpoint))
        {
            // Disable replacement feature for now
            return false;

            // Allow replacing with a newer version of the same transaction
            if (i != 0)
                return false;
            ptxOld = mapNextTx[outpoint].ptx;
            if (ptxOld->IsFinal())
                return false;
            if (!tx.IsNewerThan(*ptxOld))
                return false;
            for (unsigned int i = 0; i < tx.vin.size(); i++)
            {
                COutPoint outpoint = tx.vin[i].prevout;
                if (!mapNextTx.count(outpoint) || mapNextTx[outpoint].ptx != ptxOld)
                    return false;
            }
            break;
        }
    }

    {
        MapPrevTx mapInputs;
        //map<uint256, CTxIndex> mapUnused;
		std::map<uint256, CTxIndex> mapUnused;
        bool fInvalid = false;
        int64_t nFees;
        if (!tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
        {
            if (fInvalid)
            {
                if (fDebug)
                    return error("CTxMemPool::accept() : FetchInputs found invalid tx %s", hash.ToString().substr(0,10).c_str());
                else return false;
            }

            if (pfMissingInputs)
                *pfMissingInputs = true;
            return false;
        }
            // Check for non-standard pay-to-script-hash in inputs
            if (!AreInputsStandard(tx, mapInputs) && !fTestNet && !isNameTx)
                return error("CTxMemPool::accept() : nonstandard transaction input");

            nFees = tx.GetValueIn(mapInputs) - tx.GetValueOut();

            GetMinFee_mode feeMode = GMF_RELAY;

            if (tx.nVersion == ANON_TXN_VERSION)
            {
                int64_t nSumAnon;
                if (!tx.CheckAnonInputs(txdb, nSumAnon, fInvalid, true))
                {
                    if (fInvalid)
                        return error("CTxMemPool::accept() : CheckAnonInputs found invalid tx %s", hash.ToString().substr(0,10).c_str());
                    if (pfMissingInputs)
                        *pfMissingInputs = true;
                    return false;
                };

                nFees += nSumAnon;

                feeMode = GMF_ANON;
            };
            // Note: if you modify this code to accept non-standard transactions, then
            // you should add code here to check that the transaction does a
            // reasonable number of ECDSA signature verifications.

            unsigned int nSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            // Don't accept it if it can't get into a block

            int64_t txMinFee = tx.GetMinFee(1000, feeMode, nSize);

            if (nFees < txMinFee && !isNameTx)
            {
                return error("CTxMemPool::accept() : not enough fees %s, %" PRId64" < %" PRId64,
                             hash.ToString().c_str(),
                             nFees, txMinFee);
            };

            // Continuously rate-limit free transactions
            // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
            // be annoying or make others' transactions take longer to confirm.
            if (nFees < MIN_RELAY_TX_FEE)
            {
                static CCriticalSection cs;
                static double dFreeCount;
                static int64_t nLastTime;
                int64_t nNow = GetTime();

                {
                    LOCK(cs);
                    // Use an exponentially decaying ~10-minute window:
                    dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
                    nLastTime = nNow;
                    // -limitfreerelay unit is thousand-bytes-per-minute
                    // At default rate it would take over a month to fill 1GB
                    if (dFreeCount > GetArg("-limitfreerelay", 15)*10*1000 && !IsFromMe(tx))
                        return error("CTxMemPool::accept() : free transaction rejected by rate limiter");
                    if (fDebug)
                        printf("Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
                    dFreeCount += nSize;
                }
            };

            // Check against previous transactions
            // This is done last to help prevent CPU exhaustion denial-of-service attacks.
            if (!tx.ConnectInputs(txdb, mapInputs, mapUnused, CDiskTxPos(1,1,1), pindexBest, false, false))
            {
                return error("CTxMemPool::accept() : ConnectInputs failed %s", hash.ToString().substr(0,10).c_str());
            };
        };

    // Do not write to memory if read only mode.
    if(!fOnlyCheckWithoutAdding)
    {
        // Store transaction in memory
        {
            LOCK(cs);
            if (ptxOld) {
                printf("CTxMemPool::accept() : replacing tx %s with new version\n", ptxOld->GetHash().ToString().c_str());
                remove(*ptxOld);
            }
            addUnchecked(hash, tx);
            //Add the TX to our Pending Names in Name DB
            hooks->AddToPendingNames(tx);
        }

        ///// are we sure this is ok when loading transactions or restoring block txes
        // If updated, erase old tx from wallet
        if (ptxOld)
            EraseFromWallets(ptxOld->GetHash());

        printf("CTxMemPool::accept() : accepted %s (poolsz %" PRIszu")\n", hash.ToString().substr(0,10).c_str(), mapTx.size());
    }
    return true;
}

bool CTransaction::AcceptToMemoryPool(CTxDB& txdb,  bool fCheckInputs, bool* pfMissingInputs, bool fOnlyCheckWithoutAdding)
{
    return mempool.accept(txdb, *this, fCheckInputs, pfMissingInputs, fOnlyCheckWithoutAdding);
}

bool AcceptableInputs(CTxMemPool& pool, const CTransaction &txo, bool fLimitFree,
                        bool* pfMissingInputs)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    CTransaction tx(txo);

    if (!tx.CheckTransaction())
        return error("AcceptableInputs : CheckTransaction failed");

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return tx.DoS(100, error("AcceptableInputs : coinbase as individual tx"));

    // ppcoin: coinstake is also only valid in a block, not as a loose transaction
    if (tx.IsCoinStake())
        return tx.DoS(100, error("AcceptableInputs : coinstake as individual tx"));

    // Rather not work on nonstandard transactions (unless -testnet)
    string reason;
    if (false && !fTestNet && !IsStandardTx(tx, reason))
        return error("AcceptableInputs : nonstandard transaction");

    // is it already in the memory pool?
    uint256 hash = tx.GetHash();
    if (pool.exists(hash))
        return false;

    // Check for conflicts with in-memory transactions
    {
    LOCK(pool.cs); // protect pool.mapNextTx
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        COutPoint outpoint = tx.vin[i].prevout;
        if (pool.mapNextTx.count(outpoint))
        {
            // Disable replacement feature for now
            return false;
        }
    }
    }

    {
        CTxDB txdb("r");

        // do we already have it?
        if (txdb.ContainsTx(hash))
            return false;

        MapPrevTx mapInputs;
        map<uint256, CTxIndex> mapUnused;
        bool fInvalid = false;
        if (!tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
        {
            if (fInvalid)
                if (fDebugNet) return error("AcceptableInputs : FetchInputs found invalid tx %s", hash.ToString().substr(0,10).c_str());
                return false;
            if (pfMissingInputs)
                *pfMissingInputs = true;
            return false;
        }

        // Check for non-standard pay-to-script-hash in inputs
        //if (!fTestNet() && !tx.AreInputsStandard(mapInputs))
          //  return error("AcceptToMemoryPool : nonstandard transaction input");

	    // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
	    unsigned int nSigOps = tx.GetLegacySigOpCount();
	    nSigOps += tx.GetP2SHSigOpCount(mapInputs);
        if (nSigOps > MAX_TX_SIGOPS)
            return tx.DoS(0,
                          error("AcceptToMemoryPool : too many sigops %s, %d > %d",
                                hash.ToString().c_str(), nSigOps, MAX_TX_SIGOPS));

        int64_t nFees = tx.GetValueIn(mapInputs)-tx.GetValueOut();
        unsigned int nSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

        // Don't accept it if it can't get into a block
        int64_t txMinFee = tx.GetMinFee(1000, GMF_RELAY, nSize);
        if ((fLimitFree && nFees < txMinFee) || (!fLimitFree && nFees < MIN_TX_FEE))
            return error("AcceptableInputs : not enough fees %s, %ld < %ld",
                         hash.ToString().c_str(),
                         nFees, txMinFee);

        // Continuously rate-limit free transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (fLimitFree && nFees < MIN_RELAY_TX_FEE)
        {
            static CCriticalSection csFreeLimiter;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            LOCK(csFreeLimiter);

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
            nLastTime = nNow;
            // -limitfreerelay unit is thousand-bytes-per-minute
            // At default rate it would take over a month to fill 1GB
            if (dFreeCount > GetArg("-limitfreerelay", 15)*10*1000)
                return error("AcceptableInputs : free transaction rejected by rate limiter");
            printf("mempool: Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
            dFreeCount += nSize;
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        if (!tx.ConnectInputs(txdb, mapInputs, mapUnused, CDiskTxPos(1,1,1), pindexBest, true, false, STANDARD_SCRIPT_VERIFY_FLAGS, false))
        {
            return error("AcceptableInputs : ConnectInputs failed %s", hash.ToString().c_str());
        }
    }

	//Minimize debug spam
    if (fDebug) {
        printf("mempool: AcceptableInputs : accepted %s (poolsz %lu)\n",
               hash.ToString().substr(0,10).c_str(),
               pool.mapTx.size());
    }
    return true;
}

int GetInputAge(CTxIn& vin, CBlockIndex* pindex)
{
    const uint256& prevHash = vin.prevout.hash;
    CTransaction tx;
    uint256 hashBlock;
    bool fFound = GetTransaction(prevHash, tx, hashBlock);
    if(fFound)
    {
    if(mapBlockIndex.find(hashBlock) != mapBlockIndex.end())
    {
        return pindex->nHeight - mapBlockIndex[hashBlock]->nHeight;
    }
    else
        return 0;
    }
    else
        return 0;
}

unsigned int CTxMemPool::GetTransactionsUpdated() const
{
    LOCK(cs);
    return nTransactionsUpdated;
}

void CTxMemPool::AddTransactionsUpdated(unsigned int n)
{
    LOCK(cs);
    nTransactionsUpdated += n;
}

bool CTxMemPool::addUnchecked(const uint256& hash, CTransaction &tx)
{
    // Add to memory pool without checking anything.  Don't call this directly,
    // call CTxMemPool::accept to properly check the transaction first.
    {
        mapTx[hash] = tx;
        for (unsigned int i = 0; i < tx.vin.size(); i++)
            mapNextTx[tx.vin[i].prevout] = CInPoint(&mapTx[hash], i);
        nTransactionsUpdated++;
    }
    return true;
}


bool CTxMemPool::remove(const CTransaction &tx, bool fRecursive)
{
    // Remove transaction from memory pool
    {
        LOCK(cs);
        uint256 hash = tx.GetHash();
        if (mapTx.count(hash))
        {
            if (fRecursive)
            {
                for (unsigned int i = 0; i < tx.vout.size(); i++)
                {
                    std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
                    if (it != mapNextTx.end())
                        remove(*it->second.ptx, true);
                };
            };
            for (const CTxIn& txin : tx.vin)
                mapNextTx.erase(txin.prevout);
            mapTx.erase(hash);

            if (tx.nVersion == ANON_TXN_VERSION)
            {
                // -- remove key images
                for (unsigned int i = 0; i < tx.vin.size(); ++i)
                {
                    const CTxIn& txin = tx.vin[i];

                    if (!txin.IsAnonInput())
                        continue;

                    ec_point vchImage;
                    txin.ExtractKeyImage(vchImage);

                    mapKeyImage.erase(vchImage);
                };
            };

            nTransactionsUpdated++;
        };
    }
    return true;
}

bool CTxMemPool::removeConflicts(const CTransaction &tx)
{
    // Remove transactions which depend on inputs of tx, recursively
    LOCK(cs);
    for (const CTxIn &txin : tx.vin) {
        std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end()) {
            const CTransaction &txConflict = *it->second.ptx;
            if (txConflict != tx)
                remove(txConflict, true);
        }
    }
    return true;
}

void CTxMemPool::clear()
{
    LOCK(cs);
    mapTx.clear();
    mapNextTx.clear();
    mapKeyImage.clear();
    ++nTransactionsUpdated;
}

void CTxMemPool::queryHashes(std::vector<uint256>& vtxid)
{
    vtxid.clear();

    LOCK(cs);
    vtxid.reserve(mapTx.size());
    for (map<uint256, CTransaction>::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi)
        vtxid.push_back((*mi).first);
}

int CMerkleTx::GetDepthInMainChainINTERNAL(CBlockIndex* &pindexRet) const
{
    if (hashBlock == 0 || nIndex == -1)
        return 0;
    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;

    // Make sure the merkle branch connects to this block
    if (!fMerkleVerified)
    {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
            return 0;
        fMerkleVerified = true;
    }

    pindexRet = pindex;
    return pindexBest->nHeight - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChain(CBlockIndex* &pindexRet) const
{
    AssertLockHeld(cs_main);
    int nResult = GetDepthInMainChainINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(GetHash()))
        return -1; // Not in chain, not in mempool

    return nResult;
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!(IsCoinBase() || IsCoinStake()))
        return 0;
    return max(0, (nCoinbaseMaturity+10) - GetDepthInMainChain());
}


bool CMerkleTx::AcceptToMemoryPool(CTxDB& txdb)
{
    return CTransaction::AcceptToMemoryPool(txdb);
}

bool CMerkleTx::AcceptToMemoryPool()
{
    CTxDB txdb("r");
    return AcceptToMemoryPool(txdb);
}

bool CWalletTx::AcceptWalletTransaction(CTxDB& txdb)
{

    {
        // Add previous supporting transactions first
        for (CMerkleTx& tx : vtxPrev)
        {
            if (!(tx.IsCoinBase() || tx.IsCoinStake()))
            {
                uint256 hash = tx.GetHash();
                if (!mempool.exists(hash) && !txdb.ContainsTx(hash))
                    tx.AcceptToMemoryPool(txdb);
            }
        }
        return AcceptToMemoryPool(txdb);
    }
    return false;
}

bool CWalletTx::AcceptWalletTransaction()
{
    CTxDB txdb("r");
    return AcceptWalletTransaction(txdb);
}

int CTxIndex::GetDepthInMainChain() const
{
    // Read block header
    CBlock block;
    if (!block.ReadFromDisk(pos.nFile, pos.nBlockPos, false))
        return 0;
    // Find the block in the index
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(block.GetHash());
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;
    return 1 + nBestHeight - pindex->nHeight;
}

// Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock
bool GetTransaction(const uint256 &hash, CTransaction &tx, uint256 &hashBlock, bool s)
{
    {
        if(s)
        {
          LOCK(cs_main);
          {
            if (mempool.lookup(hash, tx))
            {
                return true;
            }
          }
        }
        CTxDB txdb("r");
        CTxIndex txindex;
        if (tx.ReadFromDisk(txdb, COutPoint(hash, 0), txindex))
        {
            CBlock block;
            if (block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
                hashBlock = block.GetHash();
            return true;
        }
    }
    return false;
}

bool GetKeyImage(CTxDB* ptxdb, ec_point& keyImage, CKeyImageSpent& keyImageSpent, bool& fInMempool)
{
    AssertLockHeld(cs_main);


    // -- check txdb first
    fInMempool = false;
    if (ptxdb->ReadKeyImage(keyImage, keyImageSpent))
        return true;

    if (mempool.lookupKeyImage(keyImage, keyImageSpent))
    {
        fInMempool = true;
        return true;
    };

    return false;
};

bool TxnHashInSystem(CTxDB* ptxdb, uint256& txnHash)
{
    // -- is the transaction hash known in the system

    AssertLockHeld(cs_main);

    // TODO: thin mode

    if (mempool.exists(txnHash))
        return true;

    CTxIndex txnIndex;
    if (ptxdb->ReadTxIndex(txnHash, txnIndex))
    {
        if (txnIndex.GetDepthInMainChain() > 0)
            return true;
    };

    return false;
};

//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

// bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos)
// {
//     block.SetNull();

//     // Open history file to read
//     CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
//     if (filein.IsNull())
//         return error("ReadBlockFromDisk : OpenBlockFile failed");

//     // Read block
//     try {
//         filein >> block;
//     }
//     catch (std::exception &e) {
//         return error("%s : Deserialize or I/O error - %s", __func__, e.what());
//     }

//     // Check the header
//     if (block.IsProofOfWork() && !CheckProofOfWork(block.GetHash(), block.nBits))
//         return error("ReadBlockFromDisk : Errors in block header");

//     return true;
// }

// bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex)
// {
//     if (!ReadBlockFromDisk(block, pindex->nBlockPos))
//         return false;
//     if (block.GetHash() != pindex->GetBlockHash())
//         return error("ReadBlockFromDisk(CBlock&, CBlockIndex*) : GetHash() doesn't match index");
//     return true;
// }

static CBlockIndex* pblockindexFBBHLast;
CBlockIndex* FindBlockByHeight(int nHeight)
{
    CBlockIndex *pblockindex;
    if (nHeight < nBestHeight / 2)
        pblockindex = pindexGenesisBlock;
    else
        pblockindex = pindexBest;
    if (pblockindexFBBHLast && abs(nHeight - pblockindex->nHeight) > abs(nHeight - pblockindexFBBHLast->nHeight))
        pblockindex = pblockindexFBBHLast;
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;
    while (pblockindex->nHeight < nHeight)
        pblockindex = pblockindex->pnext;
    pblockindexFBBHLast = pblockindex;
    return pblockindex;
}

bool CBlock::ReadFromDisk(const CBlockIndex* pindex, bool fReadTransactions)
{
    if (!fReadTransactions)
    {
        *this = pindex->GetBlockHeader();
        return true;
    }
    if (!ReadFromDisk(pindex->nFile, pindex->nBlockPos, fReadTransactions))
        return false;
    if (GetHash() != pindex->GetBlockHash())
        return error("CBlock::ReadFromDisk() : GetHash() doesn't match index");
    return true;
}

uint256 static GetOrphanRoot(const CBlock* pblock)
{
    // Work back to the first block in the orphan chain
    while (mapOrphanBlocks.count(pblock->hashPrevBlock))
        pblock = mapOrphanBlocks[pblock->hashPrevBlock];
    return pblock->GetHash();
}

// ppcoin: find block wanted by given orphan block
uint256 WantedByOrphan(const CBlock* pblockOrphan)
{
    // Work back to the first block in the orphan chain
    while (mapOrphanBlocks.count(pblockOrphan->hashPrevBlock))
        pblockOrphan = mapOrphanBlocks[pblockOrphan->hashPrevBlock];
    return pblockOrphan->hashPrevBlock;
}

// Remove a random orphan block (which does not have any dependent orphans).
void static PruneOrphanBlocks()
{
    if (mapOrphanBlocksByPrev.size() <= (size_t)std::max((int64_t)0, GetArg("-maxorphanblocks", DEFAULT_MAX_ORPHAN_BLOCKS)))
        return;

    // Pick a random orphan block.
    int pos = insecure_rand() % mapOrphanBlocksByPrev.size();
    std::multimap<uint256, CBlock*>::iterator it = mapOrphanBlocksByPrev.begin();
    while (pos--) it++;

    // As long as this block has other orphans depending on it, move to one of those successors.
    do {
        std::multimap<uint256, CBlock*>::iterator it2 = mapOrphanBlocksByPrev.find(it->second->GetHash());
        if (it2 == mapOrphanBlocksByPrev.end())
            break;
        it = it2;
    } while(1);

    uint256 hash = it->second->GetHash();
    delete it->second;
    mapOrphanBlocksByPrev.erase(it);
    mapOrphanBlocks.erase(hash);
}

// Proof of Work miner's coin base reward
int64_t GetProofOfWorkReward(int nHeight, int64_t nFees)
{
	int64_t nSubsidy = 1 * COIN;

    if (fTestNet) {
        if (pindexBest->nHeight == 1)
            nSubsidy = 3000000 * COIN;  // 3m D Premine for Testnet for testing and fund distribution
        else if (pindexBest->nHeight <= FAIR_LAUNCH_BLOCK) // Block 210, Instamine prevention
            nSubsidy = 1 * COIN/2;
        else if (pindexBest->nHeight <= 4000)
            nSubsidy = 3 * COIN; //300000000
        else if (pindexBest->nHeight > 4000) // Block 4000 Testnet PoW Rewards End
            nSubsidy = 0; // PoW Reward 0.0001 
        else if (pindexBest->nHeight > 30000)
            nSubsidy = 10000; // PoW Reward 0.0001 

        if (fDebug && GetBoolArg("-printcreation"))
            printf("GetProofOfWorkReward() : create=%s nSubsidy=%" PRId64"\n", FormatMoney(nSubsidy).c_str(), nSubsidy);

        return nSubsidy + nFees;
    } else {
        if (pindexBest->nHeight == 1)
            nSubsidy = 1000000 * COIN;  // 10% Premine
        else if (pindexBest->nHeight <= FAIR_LAUNCH_BLOCK) // Block 210, Instamine prevention
            nSubsidy = 1 * COIN/2;
        else if (pindexBest->nHeight <= 1000000) // Block 1m ~ 3m D (33% will go to hybrid fortunastakes)
            nSubsidy = 3 * COIN;
        else if (pindexBest->nHeight <= 2000000) // Block 2m ~ 4m D
            nSubsidy = 4 * COIN;
        else if (pindexBest->nHeight <= 3000000) // Block 3m ~ 3m D
            nSubsidy = 3 * COIN;
        else if (pindexBest->nHeight > ZERO_POW_BLOCK && pindexBest->nHeight < 4693333) // Block 3m
            nSubsidy = 0; // PoW Reward 
        else if (pindexBest->nHeight >= 4693333) // Block 4693333, Start PoW Rewards again as 0.0001 D per block, Less than ~100 D per year to prevent unspendable UTXOs
            nSubsidy = 10000; // PoW Reward 0.0001 D

        if (fDebug && GetBoolArg("-printcreation"))
            printf("GetProofOfWorkReward() : create=%s nSubsidy=%" PRId64"\n", FormatMoney(nSubsidy).c_str(), nSubsidy);

        return nSubsidy + nFees;
    }
}

const int YEARLY_BLOCKCOUNT = 1051896; // Amount of Blocks per year

// Proof of Stake miner's coin stake reward based on coin age spent (coin-days)
int64_t GetProofOfStakeReward(int64_t nCoinAge, int64_t nFees)
{
	if (pindexBest->nHeight > (YEARLY_BLOCKCOUNT*9000)) // It's Over 9000 Years!
        return nFees;

    int64_t nRewardCoinYear;
    nRewardCoinYear = COIN_YEAR_REWARD; // 0.06 6%

    int64_t nSubsidy;
    nSubsidy = nCoinAge * nRewardCoinYear / 365; //Updated back on D Block 640k

    //PoS Fixed on Block 640k v2.0+ DeNaRiUs
    if (pindexBest->nHeight >= MAINNET_POSFIX || fTestNet)
        nSubsidy = nCoinAge * nRewardCoinYear / 365;

    if (fDebug && GetBoolArg("-printcreation"))
        printf("GetProofOfStakeReward(): create=%s nCoinAge=%" PRId64"\n", FormatMoney(nSubsidy).c_str(), nCoinAge);

    return nSubsidy + nFees;
}

static const int64_t nTargetTimespan = 60;

//
// maximum nBits value could possible be required nTime after
//
unsigned int ComputeMaxBits(CBigNum bnTargetLimit, unsigned int nBase, int64_t nTime)
{
    CBigNum bnResult;
    bnResult.SetCompact(nBase);
    bnResult *= 2;
    while (nTime > 0 && bnResult < bnTargetLimit)
    {
        // Maximum 200% adjustment per day...
        bnResult *= 2;
        nTime -= 24 * 60 * 60;
    }
    if (bnResult > bnTargetLimit)
        bnResult = bnTargetLimit;
    return bnResult.GetCompact();
}

//
// minimum amount of work that could possibly be required nTime after
// minimum proof-of-work required was nBase
//
unsigned int ComputeMinWork(unsigned int nBase, int64_t nTime)
{
    return ComputeMaxBits(bnProofOfWorkLimit, nBase, nTime);
}

//
// minimum amount of stake that could possibly be required nTime after
// minimum proof-of-stake required was nBase
//
unsigned int ComputeMinStake(unsigned int nBase, int64_t nTime, unsigned int nBlockTime)
{
    return ComputeMaxBits(bnProofOfStakeLimit, nBase, nTime);
}


// ppcoin: find last block index up to pindex
const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake)
{
    while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
        pindex = pindex->pprev;
    return pindex;
}

unsigned int GetNextTargetRequired(const CBlockIndex* pindexLast, bool fProofOfStake)
{
    CBigNum bnTargetLimit = fProofOfStake ? bnProofOfStakeLimit : bnProofOfWorkLimit;

    if (pindexLast == NULL)
        return bnTargetLimit.GetCompact(); // genesis block

    const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast, fProofOfStake);
    if (pindexPrev->pprev == NULL)
        return bnTargetLimit.GetCompact(); // first block
    const CBlockIndex* pindexPrevPrev = GetLastBlockIndex(pindexPrev->pprev, fProofOfStake);
    if (pindexPrevPrev->pprev == NULL)
        return bnTargetLimit.GetCompact(); // second block

    int64_t nActualSpacing = pindexPrev->GetBlockTime() - pindexPrevPrev->GetBlockTime();
    if (nActualSpacing < 0)
        nActualSpacing = nTargetSpacing;

    // ppcoin: target change every block
    // ppcoin: retarget with exponential moving toward target spacing
    CBigNum bnNew;
    bnNew.SetCompact(pindexPrev->nBits);
    int64_t nInterval = nTargetTimespan / nTargetSpacing;
    bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
    bnNew /= ((nInterval + 1) * nTargetSpacing);

    if (bnNew <= 0 || bnNew > bnTargetLimit)
        bnNew = bnTargetLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits)
{
    CBigNum bnTarget;
    bnTarget.SetCompact(nBits);

    // Check range
    if (bnTarget <= 0 || bnTarget > bnProofOfWorkLimit)
        return error("CheckProofOfWork() : nBits below minimum work");

    // Check proof of work matches claimed amount
    if (hash > bnTarget.getuint256())
        return error("CheckProofOfWork() : hash doesn't match nBits");

    return true;
}

// Return maximum amount of blocks that other nodes claim to have
int GetNumBlocksOfPeers()
{
    return std::max(cPeerBlockCounts.median(), Checkpoints::GetTotalBlocksEstimate());
}

bool IsSynchronized() {
  static bool rc = false;
  if(rc == false) rc = !IsInitialBlockDownload();
  return rc;
}

bool IsInitialBlockDownload()
{
    if (pindexBest == NULL || nBestHeight < Checkpoints::GetTotalBlocksEstimate() || nBestHeight < (GetNumBlocksOfPeers() - nCoinbaseMaturity*2))
        return true;
    static int64_t nLastUpdate;
    static CBlockIndex* pindexLastBest;
    static bool lockIBDState = false;
        if (lockIBDState)
            return false;
    if (pindexBest != pindexLastBest)
    {
        pindexLastBest = pindexBest;
        nLastUpdate = GetTime();
    }

    bool state = (GetTime() - nLastUpdate < 5 &&
            pindexBest->GetBlockTime() < (GetTime() - 300)); // last block is more than 5 minutes old

    if (state)
    {
        lockIBDState = true;
        // do stuff required at end of sync
        GetFortunastakeRanks(pindexBest);
    }
    return state;

}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (pindexNew->nChainTrust > nBestInvalidTrust)
    {
        nBestInvalidTrust = pindexNew->nChainTrust;
        CTxDB().WriteBestInvalidTrust(CBigNum(nBestInvalidTrust));
        uiInterface.NotifyBlocksChanged(pindexBest->nHeight, GetNumBlocksOfPeers());
    }

    uint256 nBestInvalidBlockTrust = pindexNew->nChainTrust - pindexNew->pprev->nChainTrust;
    uint256 nBestBlockTrust = pindexBest->nHeight != 0 ? (pindexBest->nChainTrust - pindexBest->pprev->nChainTrust) : pindexBest->nChainTrust;

    printf("InvalidChainFound: invalid block=%s  height=%d  trust=%s  blocktrust=%" PRId64"  date=%s\n",
      pindexNew->GetBlockHash().ToString().substr(0,20).c_str(), pindexNew->nHeight,
      CBigNum(pindexNew->nChainTrust).ToString().c_str(), nBestInvalidBlockTrust.Get64(),
      DateTimeStrFormat("%x %H:%M:%S", pindexNew->GetBlockTime()).c_str());
    printf("InvalidChainFound:  current best=%s  height=%d  trust=%s  blocktrust=%" PRId64"  date=%s\n",
      hashBestChain.ToString().substr(0,20).c_str(), nBestHeight,
      CBigNum(pindexBest->nChainTrust).ToString().c_str(),
      nBestBlockTrust.Get64(),
      DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime()).c_str());
}


void CBlock::UpdateTime(const CBlockIndex* pindexPrev)
{
    nTime = max(GetBlockTime(), GetAdjustedTime());
}





// Requires cs_main.
void Misbehaving(NodeId pnode, int howmuch)
{
    if (howmuch == 0)
        return;

    LOCK(cs_vNodes);
    for (CNode* pn : vNodes)
    {
        if(pn->GetId() == pnode)
        {
            pn->nMisbehavior += howmuch;
            int banscore = GetArg("-banscore", 100);
            if (pn->nMisbehavior >= banscore)
            {
                printf("Misbehaving: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", pn->addrName.c_str(), pn->nMisbehavior-howmuch, pn->nMisbehavior);
                pn->fDisconnect = true;
            }
            else
                printf("Misbehaving: %s (%d -> %d)\n", pn->addrName.c_str(), pn->nMisbehavior-howmuch, pn->nMisbehavior);

            break;
        }
    }
}





bool CTransaction::DisconnectInputs(CTxDB& txdb)
{
    //hooks->DisconnectInputs(*this); //Disconnect Name DB Inputs

    // Relinquish previous transactions' spent pointers
    if (!IsCoinBase())
    {
        for (const CTxIn& txin : vin)
        {
            COutPoint prevout = txin.prevout;

            // Get prev txindex from disk
            CTxIndex txindex;
            if (!txdb.ReadTxIndex(prevout.hash, txindex))
                return error("DisconnectInputs() : ReadTxIndex failed");

            if (prevout.n >= txindex.vSpent.size())
                return error("DisconnectInputs() : prevout.n out of range");

            // Mark outpoint as not spent
            txindex.vSpent[prevout.n].SetNull();

            // Write back
            if (!txdb.UpdateTxIndex(prevout.hash, txindex))
                return error("DisconnectInputs() : UpdateTxIndex failed");
        }
    }

    // Remove transaction from index
    // This can fail if a duplicate of this transaction was in a chain that got
    // reorganized away. This is only possible if this transaction was completely
    // spent, so erasing it would be a no-op anyway.
    txdb.EraseTxIndex(*this);

    return true;
}


bool CTransaction::FetchInputs(CTxDB& txdb, const map<uint256, CTxIndex>& mapTestPool,
                               bool fBlock, bool fMiner, MapPrevTx& inputsRet, bool& fInvalid)
{
    // FetchInputs can return false either because we just haven't seen some inputs
    // (in which case the transaction should be stored as an orphan)
    // or because the transaction is malformed (in which case the transaction should
    // be dropped).  If tx is definitely invalid, fInvalid will be set to true.
    fInvalid = false;

    if (IsCoinBase())
        return true; // Coinbase transactions have no inputs to fetch.

    for (unsigned int i = 0; i < vin.size(); i++)
    {
        if (nVersion == ANON_TXN_VERSION
            && vin[i].IsAnonInput())
            continue;

        COutPoint prevout = vin[i].prevout;
        if (inputsRet.count(prevout.hash))
            continue; // Got it already

        // Read txindex
        CTxIndex& txindex = inputsRet[prevout.hash].first;
        bool fFound = true;
        if ((fBlock || fMiner) && mapTestPool.count(prevout.hash))
        {
            // Get txindex from current proposed changes
            txindex = mapTestPool.find(prevout.hash)->second;
        }
        else
        {
            // Read txindex from txdb
            fFound = txdb.ReadTxIndex(prevout.hash, txindex);
        }
        if (!fFound && (fBlock || fMiner))
            return fMiner ? false : error("FetchInputs() : %s prev tx %s index entry not found", GetHash().ToString().substr(0,10).c_str(),  prevout.hash.ToString().substr(0,10).c_str());

        // Read txPrev
        CTransaction& txPrev = inputsRet[prevout.hash].second;
        if (!fFound || txindex.pos == CDiskTxPos(1,1,1))
        {
            // Get prev tx from single transactions in memory
            if (!mempool.lookup(prevout.hash, txPrev))
                return error("FetchInputs() : %s mempool Tx prev not found %s", GetHash().ToString().substr(0,10).c_str(),  prevout.hash.ToString().substr(0,10).c_str());
            if (!fFound)
                txindex.vSpent.resize(txPrev.vout.size());
        }
        else
        {
            // Get prev tx from disk
            if (!txPrev.ReadFromDisk(txindex.pos))
                return error("FetchInputs() : %s ReadFromDisk prev tx %s failed", GetHash().ToString().substr(0,10).c_str(),  prevout.hash.ToString().substr(0,10).c_str());
        }
    }

    // Make sure all prevout.n indexes are valid:
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        if (nVersion == ANON_TXN_VERSION
            && vin[i].IsAnonInput())
            continue;

        const COutPoint prevout = vin[i].prevout;
        assert(inputsRet.count(prevout.hash) != 0);
        const CTxIndex& txindex = inputsRet[prevout.hash].first;
        const CTransaction& txPrev = inputsRet[prevout.hash].second;
        if (prevout.n >= txPrev.vout.size() || prevout.n >= txindex.vSpent.size())
        {
            // Revisit this if/when transaction replacement is implemented and allows
            // adding inputs:
            fInvalid = true;
            if (fDebugNet)
                return DoS(100, error("FetchInputs() : %s prevout.n out of range %d %" PRIszu" %" PRIszu" prev tx %s\n%s", GetHash().ToString().substr(0,10).c_str(), prevout.n, txPrev.vout.size(), txindex.vSpent.size(), prevout.hash.ToString().substr(0,10).c_str(), txPrev.ToString().c_str()));
            return DoS(100, false);

        }
    }

    return true;
}

// Ring Signatures - D e n a r i u s
static bool CheckAnonInputAB(CTxDB &txdb, const CTxIn &txin, int i, int nRingSize, std::vector<uint8_t> &vchImage, uint256 &preimage, int64_t &nCoinValue)
{
    const CScript &s = txin.scriptSig;

    CPubKey pkRingCoin;
    CAnonOutput ao;
    CTxIndex txindex;

    ec_point pSigC;
    pSigC.resize(ec_secret_size);
    memcpy(&pSigC[0], &s[2], ec_secret_size);
    const unsigned char *pSigS    = &s[2 + ec_secret_size];
    const unsigned char *pPubkeys = &s[2 + ec_secret_size + ec_secret_size * nRingSize];
    for (int ri = 0; ri < nRingSize; ++ri)
    {
        pkRingCoin = CPubKey(&pPubkeys[ri * ec_compressed_size], ec_compressed_size);
        if (!txdb.ReadAnonOutput(pkRingCoin, ao))
        {
            printf("CheckAnonInputsAB(): Error input %d, element %d AnonOutput %s not found.\n", i, ri);
            return false;
        };

        if (nCoinValue == -1)
        {
            nCoinValue = ao.nValue;
        } else
        if (nCoinValue != ao.nValue)
        {
            printf("CheckAnonInputsAB(): Error input %d, element %d ring amount mismatch %d, %d.\n", i, ri, nCoinValue, ao.nValue);
            return false;
        };

        if (ao.nBlockHeight == 0
            || nBestHeight - ao.nBlockHeight < MIN_ANON_SPEND_DEPTH)
        {
            printf("CheckAnonInputsAB(): Error input %d, element %d depth < MIN_ANON_SPEND_DEPTH.\n", i, ri);
            return false;
        };
    };

    if (verifyRingSignatureAB(vchImage, preimage, nRingSize, pPubkeys, pSigC, pSigS) != 0)
    {
        printf("CheckAnonInputsAB(): Error input %d verifyRingSignatureAB() failed.\n", i);
        return false;
    };

    return true;
};

bool CTransaction::CheckAnonInputs(CTxDB& txdb, int64_t& nSumValue, bool& fInvalid, bool fCheckExists)
{
    AssertLockHeld(cs_main);
    // - fCheckExists should only run for anonInputs entering this node

    fInvalid = false; // TODO: is it acceptable to not find ring members?

    nSumValue = 0;

    uint256 preimage;
    if (pwalletMain->GetTxnPreImage(*this, preimage) != 0)
    {
        printf("CheckAnonInputs(): Error GetTxnPreImage() failed.\n");
        fInvalid = true; return false;
    };

    uint256 txnHash = GetHash();

    for (uint32_t i = 0; i < vin.size(); i++)
    {
        const CTxIn &txin = vin[i];

        if (!txin.IsAnonInput())
            continue;

        const CScript &s = txin.scriptSig;

        std::vector<uint8_t> vchImage;
        txin.ExtractKeyImage(vchImage);

        CKeyImageSpent spentKeyImage;
        bool fInMemPool;
        if (GetKeyImage(&txdb, vchImage, spentKeyImage, fInMemPool))
        {
            // -- this can happen for transactions created by the local node
            if (spentKeyImage.txnHash == txnHash)
            {
                if (fDebugRingSig)
                    printf("Input %d keyimage %s matches txn %s.\n", i, HexStr(vchImage).c_str(), txnHash.ToString().c_str());
            } else
            {
                if (fCheckExists
                    && !TxnHashInSystem(&txdb, spentKeyImage.txnHash))
                {
                    if (fDebugRingSig)
                        printf("Input %d keyimage %s matches unknown txn %s, continuing.\n", i, HexStr(vchImage).c_str(), spentKeyImage.txnHash.ToString().c_str());

                    // -- spentKeyImage is invalid as points to unknown txnHash
                    //    continue
                } else
                {
                    printf("CheckAnonInputs(): Error input %d keyimage %s already spent.\n", i, HexStr(vchImage).c_str());
                    fInvalid = true; return false;
                };
            };
        };

        int64_t nCoinValue = -1;
        int nRingSize = txin.ExtractRingSize();
        if (nRingSize < 1
          ||nRingSize > (pindexBest->nHeight ? (int)MAX_RING_SIZE : (int)MAX_RING_SIZE_OLD))
        {
            printf("CheckAnonInputs(): Error input %d ringsize %d not in range [%d, %d].\n", i, nRingSize, MIN_RING_SIZE, MAX_RING_SIZE);
            fInvalid = true; return false;
        };


        if (nRingSize > 1 && s.size() == 2 + ec_secret_size + (ec_secret_size + ec_compressed_size) * nRingSize)
        {
            // ringsig AB
            if (!CheckAnonInputAB(txdb, txin, i, nRingSize, vchImage, preimage, nCoinValue))
            {
                fInvalid = true; return false;
            };

            nSumValue += nCoinValue;
            continue;
        };

        if (s.size() < 2 + (ec_compressed_size + ec_secret_size + ec_secret_size) * nRingSize)
        {
            printf("CheckAnonInputs(): Error input %d scriptSig too small.\n", i);
            fInvalid = true; return false;
        };


        CPubKey pkRingCoin;
        CAnonOutput ao;
        CTxIndex txindex;
        const unsigned char* pPubkeys = &s[2];
        const unsigned char* pSigc    = &s[2 + ec_compressed_size * nRingSize];
        const unsigned char* pSigr    = &s[2 + (ec_compressed_size + ec_secret_size) * nRingSize];
        for (int ri = 0; ri < nRingSize; ++ri)
        {
            pkRingCoin = CPubKey(&pPubkeys[ri * ec_compressed_size], ec_compressed_size);
            if (!txdb.ReadAnonOutput(pkRingCoin, ao))
            {
                printf("CheckAnonInputs(): Error input %d, element %d AnonOutput %s not found.\n", i, ri);
                fInvalid = true; return false;
            };

            if (nCoinValue == -1)
            {
                nCoinValue = ao.nValue;
            } else
            if (nCoinValue != ao.nValue)
            {
                printf("CheckAnonInputs(): Error input %d, element %d ring amount mismatch %d, %d.\n", i, ri, nCoinValue, ao.nValue);
                fInvalid = true; return false;
            };

            if (ao.nBlockHeight == 0
                || nBestHeight - ao.nBlockHeight < MIN_ANON_SPEND_DEPTH)
            {
                printf("CheckAnonInputs(): Error input %d, element %d depth < MIN_ANON_SPEND_DEPTH.\n", i, ri);
                fInvalid = true; return false;
            };
        };

        if (verifyRingSignature(vchImage, preimage, nRingSize, pPubkeys, pSigc, pSigr) != 0)
        {
            printf("CheckAnonInputs(): Error input %d verifyRingSignature() failed.\n", i);
            fInvalid = true; return false;
        };

        nSumValue += nCoinValue;
    };

    return true;
};

const CTxOut& CTransaction::GetOutputFor(const CTxIn& input, const MapPrevTx& inputs) const
{
    MapPrevTx::const_iterator mi = inputs.find(input.prevout.hash);
    if (mi == inputs.end())
        throw std::runtime_error("CTransaction::GetOutputFor() : prevout.hash not found");

    const CTransaction& txPrev = (mi->second).second;
    if (input.prevout.n >= txPrev.vout.size())
        throw std::runtime_error("CTransaction::GetOutputFor() : prevout.n out of range");

    return txPrev.vout[input.prevout.n];
}

int64_t CTransaction::GetValueIn(const MapPrevTx& inputs) const
{
    if (IsCoinBase())
        return 0;

    int64_t nResult = 0;
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        if (nVersion == ANON_TXN_VERSION
            && vin[i].IsAnonInput())
        {
            continue;
        };
        nResult += GetOutputFor(vin[i], inputs).nValue;
    };

    return nResult;
}

unsigned int CTransaction::GetP2SHSigOpCount(const MapPrevTx& inputs) const
{
    if (IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        if (nVersion == ANON_TXN_VERSION
            && vin[i].IsAnonInput())
            continue;
        const CTxOut& prevout = GetOutputFor(vin[i], inputs);
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(vin[i].scriptSig);
    };

    return nSigOps;
}

bool CTransaction::ConnectInputs(CTxDB& txdb, MapPrevTx inputs, map<uint256, CTxIndex>& mapTestPool, const CDiskTxPos& posThisTx,
    const CBlockIndex* pindexBlock, bool fBlock, bool fMiner, unsigned int flags, bool fValidateSig)
{
    // Take over previous transactions' spent pointers
    // fBlock is true when this is called from AcceptBlock when a new best-block is added to the blockchain
    // fMiner is true when called from the internal bitcoin miner
    // ... both are false when called from CTransaction::AcceptToMemoryPool
    if (!IsCoinBase())
    {
        // vector<CTransaction> vTxPrev;
        // vector<CTxIndex> vTxindex;
        int64_t nValueIn = 0;
        int64_t nFees = 0;
        for (unsigned int i = 0; i < vin.size(); i++)
        {
            if (nVersion == ANON_TXN_VERSION && vin[i].IsAnonInput())
                continue;
            COutPoint prevout = vin[i].prevout;
            assert(inputs.count(prevout.hash) > 0);
            CTxIndex& txindex = inputs[prevout.hash].first;
            CTransaction& txPrev = inputs[prevout.hash].second;

            if (prevout.n >= txPrev.vout.size() || prevout.n >= txindex.vSpent.size())
                return DoS(100, error("ConnectInputs() : %s prevout.n out of range %d %" PRIszu" %" PRIszu" prev tx %s\n%s", GetHash().ToString().substr(0,10).c_str(), prevout.n, txPrev.vout.size(), txindex.vSpent.size(), prevout.hash.ToString().substr(0,10).c_str(), txPrev.ToString().c_str()));

            // If prev is coinbase or coinstake, check that it's matured
            if (txPrev.IsCoinBase() || txPrev.IsCoinStake())
                for (const CBlockIndex* pindex = pindexBlock; pindex && pindexBlock->nHeight - pindex->nHeight < nCoinbaseMaturity; pindex = pindex->pprev)
                    if (pindex->nBlockPos == txindex.pos.nBlockPos && pindex->nFile == txindex.pos.nFile)
                        return error("ConnectInputs() : tried to spend %s at depth %d", txPrev.IsCoinBase() ? "coinbase" : "coinstake", pindexBlock->nHeight - pindex->nHeight);

            // ppcoin: check transaction timestamp
            if (txPrev.nTime > nTime)
                return DoS(100, error("ConnectInputs() : transaction timestamp earlier than input transaction"));

            // Check for negative or overflow input values
            nValueIn += txPrev.vout[prevout.n].nValue;
            if (!MoneyRange(txPrev.vout[prevout.n].nValue) || !MoneyRange(nValueIn))
                return DoS(100, error("ConnectInputs() : txin values out of range"));

        }
        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.
        for (unsigned int i = 0; i < vin.size(); i++)
        {
            if (nVersion == ANON_TXN_VERSION
                && vin[i].IsAnonInput())
                continue;
            COutPoint prevout = vin[i].prevout;
            assert(inputs.count(prevout.hash) > 0);
            CTxIndex& txindex = inputs[prevout.hash].first;
            CTransaction& txPrev = inputs[prevout.hash].second;

            // Check for conflicts (double-spend)
            // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
            // for an attacker to attempt to split the network.
            if (!txindex.vSpent[prevout.n].IsNull())
                return fMiner ? false : error("ConnectInputs() : %s prev tx already used at %s", GetHash().ToString().substr(0,10).c_str(), txindex.vSpent[prevout.n].ToString().c_str());

    	if(fValidateSig)
	    {
            // Skip ECDSA signature verification when connecting blocks (fBlock=true)
            // before the last blockchain checkpoint. This is safe because block merkle hashes are
            // still computed and checked, and any change will be caught at the next checkpoint.
            if (!(fBlock && (nBestHeight < Checkpoints::GetTotalBlocksEstimate())))
            {
                // Verify signature
                if (!VerifySignature(txPrev, *this, i, flags, 0))
                {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                    // Check whether the failure was caused by a
                    // non-mandatory script verification check, such as
                    // non-null dummy arguments;
                    // if so, don't trigger DoS protection to
                    // avoid splitting the network between upgraded and
                    // non-upgraded nodes.
                    if (VerifySignature(txPrev, *this, i, flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, 0))
                        return error("ConnectInputs() : %s non-mandatory VerifySignature failed", GetHash().ToString().c_str());
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after a soft-fork
                    // super-majority vote has passed.
                    return DoS(100,error("ConnectInputs() : %s VerifySignature failed", GetHash().ToString().substr(0,10).c_str()));
                }
            }
        }
            // Mark outpoints as spent
            txindex.vSpent[prevout.n] = posThisTx;

            // Write back
            if (fBlock || fMiner)
            {
                mapTestPool[prevout.hash] = txindex;
            }
            //Push txPrev and txindex to vTxPrev and VTxIndex
            // vTxPrev.push_back(txPrev);
            // vTxindex.push_back(txindex);
        }
        //vector<nameTempProxy>& vName
        //If it can't connect inputs return false to the Name DB
        // if (!hooks->ConnectInputs(txdb, mapTestPool, *this, posThisTx, pindexBlock, fBlock, fMiner, flags, vName)) {
        //     return false;
        // }

        if (nVersion == ANON_TXN_VERSION)
        {
            int64_t nSumAnon;
            bool fInvalid;
            if (!CheckAnonInputs(txdb, nSumAnon, fInvalid, true))
            {
                //if (fInvalid)
                DoS(100, error("ConnectInputs() : CheckAnonInputs found invalid tx %s", GetHash().ToString().substr(0,10).c_str()));
            };

            nValueIn += nSumAnon;
        };

        if (!IsCoinStake())
        {
            if (nValueIn < GetValueOut())
                return DoS(100, error("ConnectInputs() : %s value in < value out", GetHash().ToString().substr(0,10).c_str()));

            // Tally transaction fees
            int64_t nTxFee = nValueIn - GetValueOut();
            if (nTxFee < 0)
                return DoS(100, error("ConnectInputs() : %s nTxFee < 0", GetHash().ToString().substr(0,10).c_str()));

            // enforce transaction fees for every block
            if (nTxFee < GetMinFee())
                return fBlock? DoS(100, error("ConnectInputs() : %s not paying required fee=%s, paid=%s", GetHash().ToString().substr(0,10).c_str(), FormatMoney(GetMinFee()).c_str(), FormatMoney(nTxFee).c_str())) : false;

            nFees += nTxFee;
            if (!MoneyRange(nFees))
                return DoS(100, error("ConnectInputs() : nFees out of range"));
        }
    }

    return true;
}

bool CBlock::DisconnectBlock(CTxDB& txdb, CBlockIndex* pindex, bool fWriteNames)
{
    // Disconnect in reverse order
    for (int i = vtx.size()-1; i >= 0; i--)
        if (!vtx[i].DisconnectInputs(txdb))
            return false;

    // Update block index on disk without changing it in memory.
    // The memory index structure will be changed after the db commits.
    if (pindex->pprev)
    {
        CDiskBlockIndex blockindexPrev(pindex->pprev);
        blockindexPrev.hashNext = 0;
        if (!txdb.WriteBlockIndex(blockindexPrev))
            return error("DisconnectBlock() : WriteBlockIndex failed");
    }

    // denarius: undo name transactions in reverse order
    for (int i = vtx.size() - 1; i >= 0; i--)
        hooks->DisconnectInputs(vtx[i]);

    // ppcoin: clean up wallet after disconnecting coinstake
    for (CTransaction& tx : vtx)
        SyncWithWallets(tx, this, false, false);

    return true;
}

bool static BuildAddrIndex(const CScript &script, std::vector<uint160>& addrIds)
{
    CScript::const_iterator pc = script.begin();
    CScript::const_iterator pend = script.end();
    std::vector<unsigned char> data;
    opcodetype opcode;
    bool fHaveData = false;
    while (pc < pend) {
        script.GetOp(pc, opcode, data);
        if (0 <= opcode && opcode <= OP_PUSHDATA4 && data.size() >= 8) { // data element
            uint160 addrid = 0;
            if (data.size() <= 20) {
                memcpy(&addrid, &data[0], data.size());
            } else {
                addrid = Hash160(data);
            }
            addrIds.push_back(addrid);
            fHaveData = true;
        }
    }
    if (!fHaveData) {
        uint160 addrid = Hash160(script);
	addrIds.push_back(addrid);
        return true;
    }
    else
    {
	if(addrIds.size() > 0)
	    return true;
	else
  	    return false;
    }
}

bool FindTransactionsByDestination(const CTxDestination &dest, std::vector<uint256> &vtxhash) {
    uint160 addrid = 0;
    const CKeyID *pkeyid = boost::get<CKeyID>(&dest);
    if (pkeyid)
        addrid = static_cast<uint160>(*pkeyid);
    if (!addrid) {
        const CScriptID *pscriptid = boost::get<CScriptID>(&dest);
        if (pscriptid)
            addrid = static_cast<uint160>(*pscriptid);
    }
    if (!addrid)
    {
        printf("FindTransactionsByDestination(): Couldn't parse dest into addrid\n");
        return false;
    }

    LOCK(cs_main);
    CTxDB txdb("r");
    if(!txdb.ReadAddrIndex(addrid, vtxhash))
    {
	printf("FindTransactionsByDestination(): txdb.ReadAddrIndex failed\n");
	return false;
    }
    return true;
}

void CBlock::RebuildAddressIndex(CTxDB& txdb)
{
    for (CTransaction& tx : vtx)
    {
        uint256 hashTx = tx.GetHash();
	// inputs
	if(!tx.IsCoinBase())
	{
            MapPrevTx mapInputs;
	    map<uint256, CTxIndex> mapQueuedChangesT;
	    bool fInvalid;
            if (!tx.FetchInputs(txdb, mapQueuedChangesT, true, false, mapInputs, fInvalid))
                return;

	    MapPrevTx::const_iterator mi;
	    for(MapPrevTx::const_iterator mi = mapInputs.begin(); mi != mapInputs.end(); ++mi)
	    {
		    for (const CTxOut &atxout : (*mi).second.second.vout)
		    {
			std::vector<uint160> addrIds;
			if(BuildAddrIndex(atxout.scriptPubKey, addrIds))
			{
                    for (uint160 addrId : addrIds)
		            {
			            if(!txdb.WriteAddrIndex(addrId, hashTx))
				            printf("RebuildAddressIndex(): txins WriteAddrIndex failed addrId: %s txhash: %s\n", addrId.ToString().c_str(), hashTx.ToString().c_str());
                    }
			}
		    }
	    }

        }
	// outputs
	for (const CTxOut &atxout : tx.vout) {
	    std::vector<uint160> addrIds;
        if(BuildAddrIndex(atxout.scriptPubKey, addrIds))
	    {
		for (uint160 addrId : addrIds)
		{
		    if(!txdb.WriteAddrIndex(addrId, hashTx))
		        printf("RebuildAddressIndex(): txouts WriteAddrIndex failed addrId: %s txhash: %s\n", addrId.ToString().c_str(), hashTx.ToString().c_str());
        }
	    }
	}
    }
}

static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;

bool CBlock::ConnectBlock(CTxDB& txdb, CBlockIndex* pindex, bool fJustCheck, bool fWriteNames)
{
    // Check it again in case a previous version let a bad block in, but skip BlockSig checking
    if (!CheckBlock(!fJustCheck, !fJustCheck, false))
        return false;

    unsigned int flags = SCRIPT_VERIFY_NOCACHE;

    /* // Currently don't need
    if(V3(nTime))
    {
      flags |= SCRIPT_VERIFY_NULLDUMMY |
               SCRIPT_VERIFY_STRICTENC |
               SCRIPT_VERIFY_ALLOW_EMPTY_SIG |
               SCRIPT_VERIFY_FIX_HASHTYPE |
               SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }
    */

    //// issue here: it doesn't know the version
    unsigned int nTxPos;
    if (fJustCheck)
        // FetchInputs treats CDiskTxPos(1,1,1) as a special "refer to memorypool" indicator
        // Since we're just checking the block and not actually connecting it, it might not (and probably shouldn't) be on the disk to get the transaction from
        nTxPos = 1;
    else
        nTxPos = pindex->nBlockPos + ::GetSerializeSize(CBlock(), SER_DISK, CLIENT_VERSION) - (2 * GetSizeOfCompactSize(0)) + GetSizeOfCompactSize(vtx.size());

    int64_t nTimeStart = GetTimeMicros();
    map<uint256, CTxIndex> mapQueuedChanges;
    int64_t nFees = 0;
    int64_t nValueIn = 0;
    int64_t nValueOut = 0;
    int64_t nAmountBurned = 0;
    int64_t nStakeReward = 0;
    unsigned int nSigOps = 0;

    //DiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(vtx.size()));
    CDiskTxPos pos(pindex->nFile, pindex->nBlockPos, nTxPos);
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(vtx.size());

    std::vector<CAmount> vFees (vtx.size(), 0);
    
    for (CTransaction& tx : vtx)
    {
        //const CTransaction &tx = vtx[i];
        uint256 hashTx = tx.GetHash();

        // Do not allow blocks that contain transactions which 'overwrite' older transactions,
        // unless those are already completely spent.
        // If such overwrites are allowed, coinbases and transactions depending upon those
        // can be duplicated to remove the ability to spend the first instance -- even after
        // being sent to another address.
        // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
        // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
        // already refuses previously-known transaction ids entirely.
        // This rule was originally applied all blocks whose timestamp was after March 15, 2012, 0:00 UTC.
        // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
        // two in the chain that violate it. This prevents exploiting the issue against nodes in their
        // initial block download.
        CTxIndex txindexOld;
        if (txdb.ReadTxIndex(hashTx, txindexOld)) {
            for (CDiskTxPos &pos : txindexOld.vSpent)
                if (pos.IsNull())
                    return false;
        }

        nSigOps += tx.GetLegacySigOpCount();
        if (nSigOps > MAX_BLOCK_SIGOPS)
            return DoS(100, error("ConnectBlock() : too many sigops"));

        CDiskTxPos posThisTx(pindex->nFile, pindex->nBlockPos, nTxPos);
        if (!fJustCheck)
            nTxPos += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);

        MapPrevTx mapInputs;
        if (tx.IsCoinBase())
            nValueOut += tx.GetValueOut();
        else
        {
            bool fInvalid;
            if (!tx.FetchInputs(txdb, mapQueuedChanges, true, false, mapInputs, fInvalid))
                return false;

            // Add in sigops done by pay-to-script-hash inputs;
            // this is to prevent a "rogue miner" from creating
            // an incredibly-expensive-to-validate block.
            nSigOps += tx.GetP2SHSigOpCount(mapInputs);
            if (nSigOps > MAX_BLOCK_SIGOPS)
                return DoS(100, error("ConnectBlock() : too many sigops"));

            int64_t nTxValueIn = tx.GetValueIn(mapInputs);
            int64_t nTxValueOut = tx.GetValueOut();

            if (tx.nVersion == ANON_TXN_VERSION)
            {
                int64_t nSumAnon;
                if (!tx.CheckAnonInputs(txdb, nSumAnon, fInvalid, true))
                {
                    if (fInvalid)
                        return error("ConnectBlock() : CheckAnonInputs found invalid tx %s", tx.GetHash().ToString().substr(0,10).c_str());
                    return false;
                };

                nTxValueIn += nSumAnon;
            };

            nValueIn += nTxValueIn;
            nValueOut += nTxValueOut;
            for (const CTxOut& out : tx.vout) {
              if(out.scriptPubKey.IsUnspendable())
                nAmountBurned += out.nValue;
            }
            if (!tx.IsCoinStake()) {
                for (unsigned int i = 0; i < vtx.size(); i++)
                {
                    vFees[i] = nTxValueIn - nTxValueOut;
                }
                nFees += nTxValueIn - nTxValueOut;
            }
            if (tx.IsCoinStake())
                nStakeReward = nTxValueOut - nTxValueIn;

            if (!tx.ConnectInputs(txdb, mapInputs, mapQueuedChanges, posThisTx, pindex, true, false, flags))
                return false;
        }

        mapQueuedChanges[hashTx] = CTxIndex(posThisTx, tx.vout.size());

        vPos.push_back(std::make_pair(tx.GetHash(), pos));
        // pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
        pos.nTxPos += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }

    //int64_t nTime1 = GetTimeMicros(); nTimeConnect += nTime1 - nTimeStart;
    //LogPrint("bench", "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", (unsigned)vtx.size(), 0.001 * (nTime1 - nTimeStart), 0.001 * (nTime1 - nTimeStart) / vtx.size(), nInputs <= 1 ? 0 : 0.001 * (nTime1 - nTimeStart) / (nInputs-1), nTimeConnect * 0.000001);

    // if (!control.Wait())
    //     return state.DoS(100, false);
    // int64_t nTime2 = GetTimeMicros(); nTimeVerify += nTime2 - nTimeStart;
    // LogPrint("bench", "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1, 0.001 * (nTime2 - nTimeStart), nInputs <= 1 ? 0 : 0.001 * (nTime2 - nTimeStart) / (nInputs-1), nTimeVerify * 0.000001);


    if (IsProofOfWork())
    {
        int64_t nReward = GetProofOfWorkReward(pindex->nHeight, nFees);
        // Check coinbase reward
        if (vtx[0].GetValueOut() > nReward)
            return DoS(50, error("ConnectBlock() : coinbase reward exceeded (actual=%" PRId64" vs calculated=%" PRId64")",
                   vtx[0].GetValueOut(),
                   nReward));
    }
    if (IsProofOfStake())
    {
        // ppcoin: coin stake tx earns reward instead of paying fee
        uint64_t nCoinAge;
        if (!vtx[1].GetCoinAge(txdb, nCoinAge))
            return error("ConnectBlock() : %s unable to get coin age for coinstake", vtx[1].GetHash().ToString().substr(0,10).c_str());

        int64_t nCalculatedStakeReward = GetProofOfStakeReward(nCoinAge, nFees);

        if (nStakeReward > nCalculatedStakeReward)
            return DoS(100, error("ConnectBlock() : coinstake pays too much(actual=%" PRId64" vs calculated=%" PRId64")", nStakeReward, nCalculatedStakeReward));
    }

    // ----------- fortunastake payments -----------
    // Once upon a time, People were really interested in D.
    // So much so, People wanted to bring D to the moon. Even Mars, Sooner than the roadster...
    // The Discord was active, People discussed how they would reach that goal.
    // There was one person, named Thi3rryzz watching all this from a save distance.
    // Then, the word FORTUNASTAKES came to the table.
    // People wanted fortunastakes... Really Bad. But King Carsen was already busy with the rest of D
    // So Thi3rryzz decided to jump in..
    // After a lot of: "How much for MN" and "When MN?"
    // We hope to proudly present you:
    // ----------- hybrid fortunastake payments -----------


    // ... after a long time, it came to be known in the lands that the fortunastakes were indeed high.
    // many of thy were so invested in their stakes they pushed it, to get all the D they could. the streets
    // were dark and the days were long. people wanted a fair hand. they wanted to know they could rely
    // on the D to bring them joy and happiness, and not worry for when they might next taste the D

    // and oh ye of little faith, feast your eyes upon the broth of thine calling. the hybrid stakes are no more.
    // gone are the days of not knowing when to expect the sweet caress of the glorious D to be gracing the silver linings
    // of your wallet. forever more you shall know the D, and the D shall know you, and ye shall be fairly judged
    // for all of eternity


    // ----- Denarius fortuna stakes, the fair payment edition  -----
    // proudly presented by enkayz

    bool FortunastakePayments = false;
    bool fIsInitialDownload = IsInitialBlockDownload();

    if (fTestNet) {
        if (pindex->nHeight > BLOCK_START_FORTUNASTAKE_PAYMENTS_TESTNET){ // Block 301 Testnet
            FortunastakePayments = true;
            if(fDebug) { printf("CheckBlock() : Fortunastake payments enabled\n"); }
        }else{
            FortunastakePayments = false;
            if(fDebug) { printf("CheckBlock() : Fortunastake payments disabled\n"); }
        }
    } else {
        if (pindex->nHeight > BLOCK_START_FORTUNASTAKE_PAYMENTS){ //Block 645k Mainnet
            FortunastakePayments = true;
            if(fDebug) { printf("CheckBlock() : Fortunastake payments enabled\n"); }
        }else{
            FortunastakePayments = false;
            if(fDebug) { printf("CheckBlock() : Fortunastake payments disabled\n"); }
        }
    }

    if(!fJustCheck && pindex->GetBlockTime() > GetTime() - 20*nCoinbaseMaturity && (pindex->nHeight < pindexBest->nHeight+5) && !IsInitialBlockDownload() && FortunastakePayments == true)
    {
        LOCK2(cs_main, mempool.cs);

        CScript burnPayee;
        CBitcoinAddress burnDestination;
        burnDestination.SetString("DNRXXXXXXXXXXXXXXXXXXXXXXXXXZeeDTw");
        burnPayee = GetScriptForDestination(burnDestination.Get());

        if(IsProofOfStake() && pindexBest != NULL){
            if(pindexBest->GetBlockHash() == hashPrevBlock){

                // make sure the ranks are updated to prev block
                GetFortunastakeRanks(pindexBest);
                // Calculate Coin Age for Fortunastake Reward Calculation
                uint64_t nCoinAge;
                if (!vtx[1].GetCoinAge(txdb, nCoinAge))
                    return error("CheckBlock-POS : %s unable to get coin age for coinstake, Can't Calculate Fortunastake Reward\n", vtx[1].GetHash().ToString().substr(0,10).c_str());
                int64_t nCalculatedStakeReward = GetProofOfStakeReward(nCoinAge, nFees);

                // Calculate expected fortunastakePaymentAmmount
                int64_t fortunastakePaymentAmount = GetFortunastakePayment(pindex->nHeight, nCalculatedStakeReward);

                // If we don't already have its previous block, skip fortunastake payment step
                if (pindex != NULL)
                {
                    bool foundPaymentAmount = false;
                    bool foundPayee = false;
                    bool paymentOK = false;

                    CScript payee;
                    if(fDebug) { printf("CheckBlock-POS() : Using fortunastake payments for block %ld\n", pindex->nHeight); }

                    // Check transaction for payee and if contains fortunastake reward payment
                    if(fDebug) { printf("CheckBlock-POS(): Transaction 1 Size : %i\n", vtx[1].vout.size()); }
                    if(fDebug) { printf("CheckBlock-POS() : Expected Fortunastake reward of: %ld\n", fortunastakePaymentAmount); }
                    for (unsigned int i = 0; i < vtx[1].vout.size(); i++) {
                        if(fDebug) { printf("CheckBlock-POS() : Payment vout number: %i , Amount: %ld\n",i, vtx[1].vout[i].nValue); }
                        if(vtx[1].vout[i].nValue == fortunastakePaymentAmount )
                        {
                            foundPaymentAmount = true;
                            payee = vtx[1].vout[i].scriptPubKey;
                            CScript pubScript;

                            if (pubScript == payee) {
                                printf("CheckBlock-POS() : Found fortunastake payment: %s D to anonymous payee.\n", FormatMoney(vtx[1].vout[i].nValue).c_str());
                                foundPayee = true;
                            } else if (payee == burnPayee) {
                                printf("CheckBlock-POS() : Found fortunastake payment: %s D to burn address.\n", FormatMoney(vtx[1].vout[i].nValue).c_str());
                                foundPayee = true;
                            } else {
                                CTxDestination mnDest;
                                ExtractDestination(vtx[1].vout[i].scriptPubKey, mnDest);
                                CBitcoinAddress mnAddress(mnDest);
                                if (fDebug) printf("CheckBlock-POS() : Found fortunastake payment: %s D to %s.\n",FormatMoney(vtx[1].vout[i].nValue).c_str(), mnAddress.ToString().c_str());
                                for (CFortunaStake& mn : vecFortunastakes)
                                {
                                    pubScript = GetScriptForDestination(mn.pubkey.GetID());
                                    CTxDestination address1;
                                    ExtractDestination(pubScript, address1);
                                    CBitcoinAddress address2(address1);

                                    if (vtx[1].vout[i].scriptPubKey == pubScript)
                                    {
                                        int64_t value = vtx[1].vout[i].nValue;
                                        if (fDebug) printf("CheckBlock-POS() : Fortunastake PoS payee found at block %d: %s who got paid %s D rate:%" PRId64" rank:%d lastpaid:%d\n", pindex->nHeight, address2.ToString().c_str(), FormatMoney(value).c_str(), mn.payRate, mn.nRank, mn.nBlockLastPaid);

                                        if (!fIsInitialDownload) {
                                            if (!CheckPoSFSPayment(pindex, vtx[1].vout[i].nValue, mn)) // CheckPoSFSPayment()
                                            {
                                                if (pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT || pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT_TESTNET) { //Update PoS FS Payments to not go out of sync
													//printf("CheckBlock-POS() : Out-of-cycle fortunastake payment detected, rejecting block.");
                                                    printf("CheckBlock-POS() : Out-of-cycle FortunaStake payment detected, rejecting block. rank:%d value:%s avg:%s payRate:%s payCount:%d\n",mn.nRank,FormatMoney(mn.payValue).c_str(),FormatMoney(nAverageFSIncome).c_str(),FormatMoney(mn.payRate).c_str(), mn.payCount);
                                                } else {
                                                    printf("CheckBlock-POS(): This fortunastake payment is too aggressive and will be accepted after block %d\n", MN_ENFORCEMENT_ACTIVE_HEIGHT);
                                                }
                                                //break;
                                            } else {
                                                if (fDebug) printf("CheckBlock-POS() : Payment meets rate requirement: payee has earnt %s against average %s\n",FormatMoney(mn.payValue).c_str(),FormatMoney(nAverageFSIncome).c_str());
                                            }
                                        } else {
                                            if (fDebug) printf("CheckBlock-POS() : Wallet currently in startup mode, ignoring rate requirements.");
                                        }
                                        // add mn payment data
                                        mn.nBlockLastPaid = pindex->nHeight;
                                        CFortunaPayData data;
                                        data.height = pindex->nHeight;
                                        data.amount = value;
                                        data.hash = pindex->GetBlockHash();
                                        mn.payData.push_back(data);
                                        mn.SetPayRate(pindex->nHeight);
                                        foundPayee = true;
                                        paymentOK = true;
                                        break;
                                    }
                                }
                                // if payee not found in mn list, check if the pubkey holds a 5K transaction
                                if (!foundPayee) {
                                    if (FindFSPayment(payee, pindex)) {
                                        if (fDebug) printf("CheckBlock-POS() : WARNING: Payee was not found in MN list, but confirmed to hold collateral.\n");
                                        foundPayee = true;
                                    }
                                }
                            }
                        }
                    }



                    if (!foundPayee) {
                        if (pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT || pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT_TESTNET) {
                                    LOCK(cs_vNodes);
                                    for (CNode* pnode : vNodes)
                                    {
                                        if (pnode->nVersion >= forTunaPool.PROTOCOL_VERSION) {
                                                printf("Asking for Fortunastake list from %s\n",pnode->addr.ToStringIPPort().c_str());
                                                pnode->PushMessage("dseg", CTxIn()); //request full mn list
                                                pnode->nLastDseg = GetTime();
                                        }
                                    }
                            return error("CheckBlock-POS() : Did not find this payee in the fortunastake list. Requesting list update and rejecting block.");
                        } else {
                            if (fDebug) printf("WARNING: Did not find this payee in the fortunastake list, this block will not be accepted after block %d\n", MN_ENFORCEMENT_ACTIVE_HEIGHT);
                            foundPayee = true;
                        }
                    } else if (paymentOK) {
                        if (pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT || pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT_TESTNET) {
                            if (fDebug) printf("CheckBlock-POS() : This payment has been determined as legitimate, and will be allowed.\n");
                        } else {
                            if (fDebug) printf("CheckBlock-POS() : This payment has been determined as legitimate, and will be allowed after block %d.\n", MN_ENFORCEMENT_ACTIVE_HEIGHT);
                        }
                    }

                    if(!(foundPaymentAmount && foundPayee)) {
                        CTxDestination address1;
                        ExtractDestination(payee, address1);
                        CBitcoinAddress address2(address1);
                        if(fDebug) { printf("CheckBlock-POS() : Couldn't find fortunastake payment(%d|%ld) or payee(%d|%s) nHeight %ld. \n", foundPaymentAmount, fortunastakePaymentAmount, foundPayee, address2.ToString().c_str(), pindexBest->nHeight+1); }
                        return DoS(100, error("CheckBlock-POS() : Couldn't find fortunastake payment or payee"));
                    } else {
                        if(fDebug) { printf("CheckBlock-POS() : Found fortunastake payment %d\n", pindexBest->nHeight+1); }
                    }
                } else {
                    if(fDebug) { printf("CheckBlock-POS() : Is initial download, skipping fortunastake payment check %ld\n", pindexBest->nHeight+1); }
                }
            } else {
                if(fDebug) { printf("CheckBlock-POS() : Skipping fortunastake payment check - nHeight %ld Hash %s\n", pindex->nHeight, GetHash().ToString().c_str()); }
            }
        }else if(IsProofOfWork() && pindexBest != NULL){
            if(pindexBest->GetBlockHash() == hashPrevBlock){

                // make sure the ranks are updated
                GetFortunastakeRanks(pindexBest);

                int64_t fortunastakePaymentAmount = GetFortunastakePayment(pindex->nHeight, vtx[0].GetValueOut());

                // If we don't already have its previous block, skip fortunastake payment step
                if (pindex != NULL)
                {
                    bool foundPaymentAmount = false;
                    bool foundPayee = false;
                    bool paymentOK = true;
                    CScript payee;

                    if(fDebug) { printf("CheckBlock-POW() : Using non-specific fortunastake payments %ld\n", pindex->nHeight); }

                    // Check transaction for payee and if contains fortunastake reward payment
                    if (fDebug) { printf("CheckBlock-POW(): Transaction 0 Size : %i\n", vtx[0].vout.size()); }
                    if (fDebug) { printf("CheckBlock-POW() : Expected Fortunastake reward of: %ld\n", fortunastakePaymentAmount); }
                    for (unsigned int i = 0; i < vtx[0].vout.size(); i++) {
                        if(fDebug) { printf("CheckBlock-POW() : Payment vout number: %i , Amount: %lld\n",i, vtx[0].vout[i].nValue); }
                        if(vtx[0].vout[i].nValue == fortunastakePaymentAmount )
                        {
                            CTxDestination mnDest;
                            payee = vtx[0].vout[i].scriptPubKey;
                            ExtractDestination(payee, mnDest);
                            CBitcoinAddress mnAddress(mnDest);
                            if (fDebug) printf("CheckBlock-POW() : Found fortunastake payment: %s D to %s.\n",FormatMoney(vtx[0].vout[i].nValue).c_str(), mnAddress.ToString().c_str());

                            foundPaymentAmount = true;

                            CScript pubScript;

                            for (CFortunaStake& mn : vecFortunastakes)
                            {
                                pubScript = GetScriptForDestination(mn.pubkey.GetID());
                                CTxDestination address1;
                                ExtractDestination(pubScript, address1);
                                CBitcoinAddress address2(address1);

                                if (payee == pubScript)
                                {
                                    if (fDebug) printf("CheckBlock-POW() : Fortunastake PoW payee found at block %d: %s who got paid %s D rate:%" PRId64" rank:%d lastpaid:%d\n", pindex->nHeight, address2.ToString().c_str(), FormatMoney(vtx[0].vout[i].nValue).c_str(), FormatMoney(mn.payRate).c_str(), mn.nRank, mn.nBlockLastPaid);
                                    if (!fIsInitialDownload) {
                                        if (!CheckFSPayment(pindex, vtx[0].vout[i].nValue, mn)) // if MN is being paid and it's bottom 50% ranked, don't let it be paid.
                                        {
                                            if (pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT || pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT_TESTNET)
                                            {
                                                printf("CheckBlock-POW() : Fortunastake overpayment detected, rejecting block. rank:%d value:%s avg:%s payRate:%s payCount:%d\n",mn.nRank,FormatMoney(mn.payValue).c_str(),FormatMoney(nAverageFSIncome).c_str(),FormatMoney(mn.payRate).c_str(), mn.payCount);
                                            } else {
                                                printf("WARNING: This fortunastake payment is too aggressive and will not be accepted after block %d\n", MN_ENFORCEMENT_ACTIVE_HEIGHT);
                                            }
                                            //break;
                                        } else {
                                            if (fDebug) printf("CheckBlock-POW() : Payment meets rate requirement: payee has earnt %s against average %s\n",FormatMoney(mn.payValue).c_str(),FormatMoney(nAverageFSIncome).c_str());
                                        }
                                    } else {
                                        if (fDebug) printf("CheckBlock-POW() : Wallet currently in startup mode, ignoring rate requirements.");
                                    }

                                    mn.nBlockLastPaid = pindex->nHeight;
                                    CFortunaPayData data;
                                    data.height = pindex->nHeight;
                                    data.amount = vtx[0].vout[i].nValue;
                                    data.hash = pindex->GetBlockHash();
                                    mn.payData.push_back(data);
                                    mn.SetPayRate(pindex->nHeight);
                                    foundPayee = true;
                                    paymentOK = true;
                                    break;
                                } else if (payee == burnPayee) {
                                    printf("CheckBlock-POW() : Found fortunastake payment: %s D to burn address.\n", FormatMoney(vtx[0].vout[i].nValue).c_str());
                                    foundPayee = true;
                                }
                            }

                            // if payee not found in mn list, check if the pubkey holds a 5K transaction
                            if (!foundPayee) {
                                if (FindFSPayment(payee, pindex)) {
                                    if (fDebug) printf("CheckBlock-POW() : WARNING: Payee was not found in MN list, but confirmed to hold collateral.\n");
                                    foundPayee = true;
                                }
                            }
                        }
                    }

                    if (!foundPayee) {
                        if (pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT || pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT_TESTNET) {
                                LOCK(cs_vNodes);
                                for (CNode* pnode : vNodes)
                                {
                                    if (pnode->nVersion >= forTunaPool.PROTOCOL_VERSION) {
                                            printf("Asking for Fortunastake list from %s\n",pnode->addr.ToStringIPPort().c_str());
                                            pnode->PushMessage("dseg", CTxIn()); //request full mn list
                                            pnode->nLastDseg = GetTime();
                                    }
                                }
                                return error("CheckBlock-POW() : Did not find this payee in the fortunastake list, rejecting block.");
                        } else {
                            if (fDebug) printf("WARNING: Did not find this payee in  the fortunastake list, this block will not be accepted after block %d\n", MN_ENFORCEMENT_ACTIVE_HEIGHT);
                            foundPayee = true;
                        }
                    } else if (paymentOK) {
                        if (pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT || pindexBest->nHeight >= MN_ENFORCEMENT_ACTIVE_HEIGHT_TESTNET) {
                            if (fDebug) printf("CheckBlock-POW() : This payment has been determined as legitimate, and will be allowed.\n");
                        } else {
                            if (fDebug) printf("CheckBlock-POW() : This payment has been determined as legitimate, and will be allowed after block %d.\n", MN_ENFORCEMENT_ACTIVE_HEIGHT);
                        }
                    }

                    if(fDebug) {printf("CheckBlock-POW(): foundPaymentAmount= %i ; foundPayee = %i\n", foundPaymentAmount, foundPayee); }
                    if(!(foundPaymentAmount && foundPayee)) {
                        CScript payee;
                        CTxDestination address1;
                        ExtractDestination(payee, address1);
                        CBitcoinAddress address2(address1);
                        if(fDebug) { printf("CheckBlock-POW() : Couldn't find fortunastake payment(%d|%ld) or payee(%d|%s) nHeight %d. \n", foundPaymentAmount, fortunastakePaymentAmount, foundPayee, address2.ToString().c_str(), pindexBest->nHeight+1); }
                        return DoS(100, error("CheckBlock-POW() : Couldn't find fortunastake payment or payee"));
                    } else {
                        if(fDebug) { printf("CheckBlock-POW() : Found fortunastake payment %ld\n", pindexBest->nHeight+1); }
                    }
                } else {
                    if(fDebug) { printf("CheckBlock-POW() : Is initial download, skipping fortunastake payment check %ld\n", pindexBest->nHeight+1); }
                }
            } else {
                if(fDebug) { printf("CheckBlock-POW() : Skipping fortunastake payment check - nHeight %ld Hash %s\n", pindexBest->nHeight+1, GetHash().ToString().c_str()); }
            }
        }

         else {
            if(fDebug) { printf("CheckBlock() : pindex is null, skipping fortunastake payment check\n"); }
        }
    } else {
        if(fDebug) {
                printf("CheckBlock() : skipping fortunastake payment checks\n");
        }
    }

    // ppcoin: track money supply and mint amount info
    pindex->nMint = nValueOut - nValueIn + nFees;
    pindex->nMoneySupply = (pindex->pprev? pindex->pprev->nMoneySupply : 0) + nValueOut - nValueIn;
    pindex->nMoneySupply -= nAmountBurned;
    assert(pindex->nMoneySupply >= 0);

    // denarius: collect valid name tx
    // NOTE: tx.UpdateCoins should not affect this loop, probably...
    // vector<nameTempProxy> vName;
    // for (unsigned int i=0; i<vtx.size(); i++)
    // {
    //     if (fDebug) printf("ConnectBlock() for Name Index\n");
    //     const CTransaction &tx = vtx[i];
    //     //if (!tx.IsCoinBase()) //|| !tx.IsCoinStake()
    //     //hooks->CheckInputs(tx, pindex, vName, vPos[i].second, vFees[i]); // collect valid name tx to vName
    //     // hooks->CheckInputs(txdb, mapTestPool, tx, vPos[i].second, pindexBlock)
    // }

    if (!txdb.WriteBlockIndex(CDiskBlockIndex(pindex)))
        return error("Connect() : WriteBlockIndex for pindex failed");

    if (fJustCheck)
        return true;

    // Write queued txindex changes
    for (map<uint256, CTxIndex>::iterator mi = mapQueuedChanges.begin(); mi != mapQueuedChanges.end(); ++mi)
    {
        if (!txdb.UpdateTxIndex((*mi).first, (*mi).second))
            return error("ConnectBlock() : UpdateTxIndex failed");
    }
    if(GetBoolArg("-addrindex", false))
    {
        // Write Address Index
        for (CTransaction& tx : vtx)
        {
            uint256 hashTx = tx.GetHash();
        // inputs
        if(!tx.IsCoinBase())
        {
                MapPrevTx mapInputs;
            map<uint256, CTxIndex> mapQueuedChangesT;
            bool fInvalid;
                if (!tx.FetchInputs(txdb, mapQueuedChangesT, true, false, mapInputs, fInvalid))
                    return false;

            MapPrevTx::const_iterator mi;
            for(MapPrevTx::const_iterator mi = mapInputs.begin(); mi != mapInputs.end(); ++mi)
            {
                for (const CTxOut &atxout : (*mi).second.second.vout)
                {
                std::vector<uint160> addrIds;
                if(BuildAddrIndex(atxout.scriptPubKey, addrIds))
                {
                        for (uint160 addrId : addrIds)
                        {
                        if(!txdb.WriteAddrIndex(addrId, hashTx))
                            printf("ConnectBlock(): txins WriteAddrIndex failed addrId: %s txhash: %s\n", addrId.ToString().c_str(), hashTx.ToString().c_str());
                        }
                }
                }
            }

            }

        // outputs
        for (const CTxOut &atxout : tx.vout) {
            std::vector<uint160> addrIds;
                if(BuildAddrIndex(atxout.scriptPubKey, addrIds))
            {
            for (uint160 addrId : addrIds)
            {
                if(!txdb.WriteAddrIndex(addrId, hashTx))
                    printf("ConnectBlock(): txouts WriteAddrIndex failed addrId: %s txhash: %s\n", addrId.ToString().c_str(), hashTx.ToString().c_str());
                    }
            }
        }
        }
    }

    // Update block index on disk without changing it in memory.
    // The memory index structure will be changed after the db commits.
    if (pindex->pprev)
    {
        CDiskBlockIndex blockindexPrev(pindex->pprev);
        blockindexPrev.hashNext = pindex->GetBlockHash();
        if (!txdb.WriteBlockIndex(blockindexPrev))
            return error("ConnectBlock() : WriteBlockIndex failed");
    }

    // Check Name Release Height to Connect Blocks
    if (pindex->nHeight >= RELEASE_HEIGHT) {
        // add names to denariusnamesindex.dat
        hooks->ConnectBlock(txdb, pindex);
    }

    // Watch for transactions paying to me
    for (CTransaction& tx : vtx)
        SyncWithWallets(tx, this, true);

    // update the UI about the new block
    uiInterface.NotifyRanksUpdated();

    return true;
}

bool static Reorganize(CTxDB& txdb, CBlockIndex* pindexNew)
{
    printf("REORGANIZE\n");

    // Find the fork
    CBlockIndex* pfork = pindexBest;
    CBlockIndex* plonger = pindexNew;
    while (pfork != plonger)
    {
        while (plonger->nHeight > pfork->nHeight)
            if (!(plonger = plonger->pprev))
                return error("Reorganize() : plonger->pprev is null");
        if (pfork == plonger)
            break;
        if (!(pfork = pfork->pprev))
            return error("Reorganize() : pfork->pprev is null");
    }

    // List of what to disconnect
    vector<CBlockIndex*> vDisconnect;
    for (CBlockIndex* pindex = pindexBest; pindex != pfork; pindex = pindex->pprev)
        vDisconnect.push_back(pindex);

    // List of what to connect
    vector<CBlockIndex*> vConnect;
    for (CBlockIndex* pindex = pindexNew; pindex != pfork; pindex = pindex->pprev)
        vConnect.push_back(pindex);
    reverse(vConnect.begin(), vConnect.end());

    printf("REORGANIZE: Disconnect %" PRIszu" blocks; %s..%s\n", vDisconnect.size(), pfork->GetBlockHash().ToString().substr(0,20).c_str(), pindexBest->GetBlockHash().ToString().substr(0,20).c_str());
    printf("REORGANIZE: Connect %" PRIszu" blocks; %s..%s\n", vConnect.size(), pfork->GetBlockHash().ToString().substr(0,20).c_str(), pindexNew->GetBlockHash().ToString().substr(0,20).c_str());



    // Disconnect shorter branch
    list<CTransaction> vResurrect;
    for (CBlockIndex* pindex : vDisconnect)
    {
        CBlock block;
        if (!block.ReadFromDisk(pindex))
            return error("Reorganize() : ReadFromDisk for disconnect failed");
        if (!block.DisconnectBlock(txdb, pindex))
            return error("Reorganize() : DisconnectBlock %s failed", pindex->GetBlockHash().ToString().substr(0,20).c_str());

        // Queue memory transactions to resurrect.
        // We only do this for blocks after the last checkpoint (reorganisation before that
        // point should only happen with -reindex/-loadblock, or a misbehaving peer.
        BOOST_REVERSE_FOREACH(const CTransaction& tx, block.vtx)
            if (!(tx.IsCoinBase() || tx.IsCoinStake()) && pindex->nHeight > Checkpoints::GetTotalBlocksEstimate())
                vResurrect.push_front(tx);
    }

    // Connect longer branch
    vector<CTransaction> vDelete;
    for (unsigned int i = 0; i < vConnect.size(); i++)
    {
        CBlockIndex* pindex = vConnect[i];
        CBlock block;
        if (!block.ReadFromDisk(pindex))
            return error("Reorganize() : ReadFromDisk for connect failed");

        if (!IsInitialBlockDownload()) GetFortunastakeRanks(pindex); // recalculate ranks for the this block hash if required

        if (!block.ConnectBlock(txdb, pindex))
        {
            // Invalid block
            return error("Reorganize() : ConnectBlock %s failed", pindex->GetBlockHash().ToString().substr(0,20).c_str());
        }

        // Queue memory transactions to delete
        for (const CTransaction& tx : block.vtx)
            vDelete.push_back(tx);
    }
    if (!txdb.WriteHashBestChain(pindexNew->GetBlockHash()))
        return error("Reorganize() : WriteHashBestChain failed");

    // Make sure it's successfully written to disk before changing memory structure
    if (!txdb.TxnCommit())
        return error("Reorganize() : TxnCommit failed");

    // Disconnect shorter branch
    for (CBlockIndex* pindex : vDisconnect)
        if (pindex->pprev)
            pindex->pprev->pnext = NULL;

    // Connect longer branch
    for (CBlockIndex* pindex : vConnect)
        if (pindex->pprev)
            pindex->pprev->pnext = pindex;

    // Resurrect memory transactions that were in the disconnected branch
    for (CTransaction& tx : vResurrect)
        tx.AcceptToMemoryPool(txdb);

    // Delete redundant memory transactions that are in the connected branch
    for (CTransaction& tx : vDelete) {
        mempool.remove(tx);
        mempool.removeConflicts(tx);
    }

    FortunaReorgBlock = true;
    printf("REORGANIZE: done\n");

    return true;
}


// Called from inside SetBestChain: attaches a block to the new best chain being built
bool CBlock::SetBestChainInner(CTxDB& txdb, CBlockIndex *pindexNew)
{
    uint256 hash = GetHash();

    // Adding to current best branch
    if (!ConnectBlock(txdb, pindexNew) || !txdb.WriteHashBestChain(hash))
    {
        txdb.TxnAbort();
        InvalidChainFound(pindexNew);
        return false;
    }
    if (!txdb.TxnCommit())
        return error("SetBestChain() : TxnCommit failed");

    // Add to current best branch
    pindexNew->pprev->pnext = pindexNew;

    // Delete redundant memory transactions
    for (CTransaction& tx : vtx)
        mempool.remove(tx);

    return true;
}

bool CBlock::SetBestChain(CTxDB& txdb, CBlockIndex* pindexNew)
{
    uint256 hash = GetHash();
    if (!txdb.TxnBegin())
        return error("SetBestChain() : TxnBegin failed");

    if (pindexGenesisBlock == NULL && hash == (!fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet))
    {
        txdb.WriteHashBestChain(hash);
        if (!txdb.TxnCommit())
            return error("SetBestChain() : TxnCommit failed");
        pindexGenesisBlock = pindexNew;
    }
    else if (hashPrevBlock == hashBestChain)
    {
        if (!SetBestChainInner(txdb, pindexNew))
            return error("SetBestChain() : SetBestChainInner failed");
    }
    else
    {
        // the first block in the new chain that will cause it to become the new best chain
        CBlockIndex *pindexIntermediate = pindexNew;

        // list of blocks that need to be connected afterwards
        std::vector<CBlockIndex*> vpindexSecondary;

        // Reorganize is costly in terms of db load, as it works in a single db transaction.
        // Try to limit how much needs to be done inside
        while (pindexIntermediate->pprev && pindexIntermediate->pprev->nChainTrust > pindexBest->nChainTrust)
        {
            vpindexSecondary.push_back(pindexIntermediate);
            pindexIntermediate = pindexIntermediate->pprev;
        }

        if (!vpindexSecondary.empty())
            printf("Postponing %" PRIszu" reconnects\n", vpindexSecondary.size());

        // Switch to new best branch
        if (!Reorganize(txdb, pindexIntermediate))
        {
            txdb.TxnAbort();
            InvalidChainFound(pindexNew);
            return error("SetBestChain() : Reorganize failed");
        }

        // Connect further blocks
        BOOST_REVERSE_FOREACH(CBlockIndex *pindex, vpindexSecondary)
        {
            CBlock block;
            if (!block.ReadFromDisk(pindex))
            {
                printf("SetBestChain() : ReadFromDisk failed\n");
                break;
            }
            if (!txdb.TxnBegin()) {
                printf("SetBestChain() : TxnBegin 2 failed\n");
                break;
            }
            // errors now are not fatal, we still did a reorganisation to a new chain in a valid way
            if (!block.SetBestChainInner(txdb, pindex))
                break;
        }


    }

    // Update best block in wallet (so we can detect restored wallets)
    bool fIsInitialDownload = IsInitialBlockDownload();
    if (!fIsInitialDownload)
    {
        const CBlockLocator locator(pindexNew);
        ::SetBestChain(locator);
    }

    // New best block
    hashBestChain = hash;
    pindexBest = pindexNew;
    pblockindexFBBHLast = NULL;
    nBestHeight = pindexBest->nHeight;
    nBestChainTrust = pindexNew->nChainTrust;
    nTimeBestReceived = GetTime();
    mempool.AddTransactionsUpdated(1);

    uint256 nBestBlockTrust = pindexBest->nHeight != 0 ? (pindexBest->nChainTrust - pindexBest->pprev->nChainTrust) : pindexBest->nChainTrust;

    printf("SetBestChain: new best=%s  height=%d  trust=%s  blocktrust=%" PRId64"  date=%s\n",
      hashBestChain.ToString().substr(0,20).c_str(), nBestHeight,
      CBigNum(nBestChainTrust).ToString().c_str(),
      nBestBlockTrust.Get64(),
      DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime()).c_str());

    // Check the version of the last 100 blocks to see if we need to upgrade:
    if (!fIsInitialDownload)
    {
        int nUpgraded = 0;
        const CBlockIndex* pindex = pindexBest;
        for (int i = 0; i < 100 && pindex != NULL; i++)
        {
            if (pindex->nVersion > CBlock::CURRENT_VERSION)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            printf("SetBestChain: %d of last 100 blocks above version %d\n", nUpgraded, CBlock::CURRENT_VERSION);
        if (nUpgraded > 100/2)
            // strMiscWarning is read by GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            strMiscWarning = _("Warning: This version is obsolete, upgrade required!");
    }

    std::string strCmd = GetArg("-blocknotify", "");

    if (!fIsInitialDownload && !strCmd.empty())
    {
        boost::replace_all(strCmd, "%s", hashBestChain.GetHex());
        boost::thread t(runCommand, strCmd); // thread runs free
    }

    return true;
}

// ppcoin: total coin age spent in transaction, in the unit of coin-days.
// Only those coins meeting minimum age requirement counts. As those
// transactions not in main chain are not currently indexed so we
// might not find out about their coin age. Older transactions are
// guaranteed to be in main chain by sync-checkpoint. This rule is
// introduced to help nodes establish a consistent view of the coin
// age (trust score) of competing branches.
bool CTransaction::GetCoinAge(CTxDB& txdb, uint64_t& nCoinAge) const
{
    CBigNum bnCentSecond = 0;  // coin age in the unit of cent-seconds
    nCoinAge = 0;

    if (IsCoinBase())
        return true;

    for (const CTxIn& txin : vin)
    {
        // First try finding the previous transaction in database
        CTransaction txPrev;
        CTxIndex txindex;
        if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
            continue;  // previous transaction not in main chain
        if (nTime < txPrev.nTime)
            return false;  // Transaction timestamp violation

        // Read block header
        CBlock block;
        if (!block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
            return false; // unable to read block of previous transaction
        if (block.GetBlockTime() + nStakeMinAge > nTime)
            continue; // only count coins meeting min age requirement

        int64_t nValueIn = txPrev.vout[txin.prevout.n].nValue;
        bnCentSecond += CBigNum(nValueIn) * (nTime-txPrev.nTime) / CENT;

        if (fDebug && GetBoolArg("-printcoinage"))
            printf("coin age nValueIn=%" PRId64" nTimeDiff=%d bnCentSecond=%s\n", nValueIn, nTime - txPrev.nTime, bnCentSecond.ToString().c_str());
    }

    CBigNum bnCoinDay = bnCentSecond * CENT / COIN / (24 * 60 * 60);
    if (fDebug && GetBoolArg("-printcoinage"))
        printf("coin age bnCoinDay=%s\n", bnCoinDay.ToString().c_str());
    nCoinAge = bnCoinDay.getuint64();
    return true;
}

// ppcoin: total coin age spent in block, in the unit of coin-days.
bool CBlock::GetCoinAge(uint64_t& nCoinAge) const
{
    nCoinAge = 0;

    CTxDB txdb("r");
    for (const CTransaction& tx : vtx)
    {
        uint64_t nTxCoinAge;
        if (tx.GetCoinAge(txdb, nTxCoinAge))
            nCoinAge += nTxCoinAge;
        else
            return false;
    }

    if (nCoinAge == 0) // block coin age minimum 1 coin-day
        nCoinAge = 1;
    if (fDebug && GetBoolArg("-printcoinage"))
        printf("block coin age total nCoinDays=%" PRId64"\n", nCoinAge);
    return true;
}

bool CBlock::AddToBlockIndex(unsigned int nFile, unsigned int nBlockPos, const uint256& hashProof)
{
    // Check for duplicate
    uint256 hash = GetHash();
    if (mapBlockIndex.count(hash))
        return error("AddToBlockIndex() : %s already exists", hash.ToString().substr(0,20).c_str());

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(nFile, nBlockPos, *this);
    if (!pindexNew)
        return error("AddToBlockIndex() : new CBlockIndex failed");
    pindexNew->phashBlock = &hash;
    map<uint256, CBlockIndex*>::iterator miPrev = mapBlockIndex.find(hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
    }

    // ppcoin: compute chain trust score
    pindexNew->nChainTrust = (pindexNew->pprev ? pindexNew->pprev->nChainTrust : 0) + pindexNew->GetBlockTrust();

    // ppcoin: compute stake entropy bit for stake modifier
    if (!pindexNew->SetStakeEntropyBit(GetStakeEntropyBit()))
        return error("AddToBlockIndex() : SetStakeEntropyBit() failed");

    // Record proof hash value
    pindexNew->hashProof = hashProof;

    // ppcoin: compute stake modifier
    uint64_t nStakeModifier = 0;
    bool fGeneratedStakeModifier = false;
    if (!ComputeNextStakeModifier(pindexNew->pprev, nStakeModifier, fGeneratedStakeModifier))
        return error("AddToBlockIndex() : ComputeNextStakeModifier() failed");
    pindexNew->SetStakeModifier(nStakeModifier, fGeneratedStakeModifier);
    pindexNew->nStakeModifierChecksum = GetStakeModifierChecksum(pindexNew);
    if (!CheckStakeModifierCheckpoints(pindexNew->nHeight, pindexNew->nStakeModifierChecksum))
        return error("AddToBlockIndex() : Rejected by stake modifier checkpoint height=%d, modifier=0x%016" PRIx64, pindexNew->nHeight, nStakeModifier);

    // Add to mapBlockIndex
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    if (pindexNew->IsProofOfStake())
        setStakeSeen.insert(make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));
    pindexNew->phashBlock = &((*mi).first);

    // Write to disk block index
    CTxDB txdb;
    if (!txdb.TxnBegin())
        return false;
    txdb.WriteBlockIndex(CDiskBlockIndex(pindexNew));
    if (!txdb.TxnCommit())
        return false;

    LOCK(cs_main);

    // New best
    if (pindexNew->nChainTrust > nBestChainTrust)
        if (!SetBestChain(txdb, pindexNew))
            return false;

    if (pindexNew == pindexBest)
    {
        // Notify UI to display prev block's coinbase if it was ours
        static uint256 hashPrevBestCoinBase;
        UpdatedTransaction(hashPrevBestCoinBase);
        hashPrevBestCoinBase = vtx[0].GetHash();
    }

    uiInterface.NotifyBlocksChanged(pindexNew->nHeight, GetNumBlocksOfPeers());
    return true;
}




bool CBlock::CheckBlock(bool fCheckPOW, bool fCheckMerkleRoot, bool fCheckSig) const
{
    // These are checks that are independent of context
    // that can be verified before saving an orphan block.

    // Size limits
    if (vtx.empty() || vtx.size() > MAX_BLOCK_SIZE || ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return DoS(100, error("CheckBlock() : size limits failed"));

    // Check proof of work matches claimed amount
    if (fCheckPOW && IsProofOfWork() && !CheckProofOfWork(GetPoWHash(), nBits))
        return DoS(50, error("CheckBlock() : proof of work failed"));

    // Check timestamp
    if (GetBlockTime() > FutureDrift(GetAdjustedTime()))
        return error("CheckBlock() : block timestamp too far in the future");

    // First transaction must be coinbase, the rest must not be
    if (vtx.empty() || !vtx[0].IsCoinBase())
        return DoS(100, error("CheckBlock() : first tx is not coinbase"));
    for (unsigned int i = 1; i < vtx.size(); i++)
        if (vtx[i].IsCoinBase())
            return DoS(100, error("CheckBlock() : more than one coinbase"));

    // Check coinbase timestamp
    if (GetBlockTime() > FutureDrift((int64_t)vtx[0].nTime))
        return DoS(50, error("CheckBlock() : coinbase timestamp is too early"));

    if (IsProofOfStake())
    {
        // Coinbase output should be empty if proof-of-stake block
        if (vtx[0].vout.size() != 1 || !vtx[0].vout[0].IsEmpty())
            return DoS(100, error("CheckBlock() : coinbase output not empty for proof-of-stake block"));

        // Second transaction must be coinstake, the rest must not be
        if (vtx.empty() || !vtx[1].IsCoinStake())
            return DoS(100, error("CheckBlock() : second tx is not coinstake"));
        for (unsigned int i = 2; i < vtx.size(); i++)
            if (vtx[i].IsCoinStake())
                return DoS(100, error("CheckBlock() : more than one coinstake"));

		// Check coinstake timestamp
		if (!CheckCoinStakeTimestamp(GetBlockTime(), (int64_t)vtx[1].nTime))
			return DoS(50, error("CheckBlock() : coinstake timestamp violation nTimeBlock=%" PRId64" nTimeTx=%u", GetBlockTime(), vtx[1].nTime));

		// Check proof-of-stake block signature
		if (fCheckSig && !CheckBlockSignature())
            return DoS(100, error("CheckBlock() : bad proof-of-stake block signature"));
	}

    // Check transactions
    for (const CTransaction& tx : vtx)
    {
        if (!tx.CheckTransaction())
            return DoS(tx.nDoS, error("CheckBlock() : CheckTransaction failed"));

        // ppcoin: check transaction timestamp
        if (GetBlockTime() < (int64_t)tx.nTime)
            return DoS(50, error("CheckBlock() : block timestamp earlier than transaction timestamp"));
    }

    // Check for duplicate txids. This is caught by ConnectInputs(),
    // but catching it earlier avoids a potential DoS attack:
    set<uint256> uniqueTx;
    for (const CTransaction& tx : vtx)
    {
        uniqueTx.insert(tx.GetHash());
    }
    if (uniqueTx.size() != vtx.size())
        return DoS(100, error("CheckBlock() : duplicate transaction"));

    unsigned int nSigOps = 0;
    for (const CTransaction& tx : vtx)
    {
        nSigOps += tx.GetLegacySigOpCount();
    }
    if (nSigOps > MAX_BLOCK_SIGOPS)
        return DoS(100, error("CheckBlock() : out-of-bounds SigOpCount"));

    // Check merkle root
    if (fCheckMerkleRoot && hashMerkleRoot != BuildMerkleTree())
        return DoS(100, error("CheckBlock() : hashMerkleRoot mismatch"));


    return true;
}

bool CBlock::AcceptBlock()
{
    AssertLockHeld(cs_main);

    if (nVersion > CURRENT_VERSION)
        return DoS(100, error("AcceptBlock() : reject unknown block version %d", nVersion));

    // Check for duplicate
    uint256 hash = GetHash();
    if (mapBlockIndex.count(hash))
        return error("AcceptBlock() : block already in mapBlockIndex");

    // Get prev block index
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashPrevBlock);
    if (mi == mapBlockIndex.end())
        return DoS(10, error("AcceptBlock() : prev block not found"));
    CBlockIndex* pindexPrev = (*mi).second;
    int nHeight = pindexPrev->nHeight+1;

    // Check proof-of-work or proof-of-stake
    if (nBits != GetNextTargetRequired(pindexPrev, IsProofOfStake()))
        return DoS(100, error("AcceptBlock() : incorrect %s", IsProofOfWork() ? "proof-of-work" : "proof-of-stake"));

    // Check timestamp against prev
    if (GetBlockTime() <= pindexPrev->GetPastTimeLimit() || FutureDrift(GetBlockTime()) < pindexPrev->GetBlockTime())
        return error("AcceptBlock() : block's timestamp is too early");

    // Check that all transactions are finalized
    for (const CTransaction& tx : vtx)
        //if (!tx.IsFinal(nHeight, GetBlockTime()))
		  if (!tx.IsFinal(nHeight, GetBlockTime()))
            return DoS(10, error("AcceptBlock() : contains a non-final transaction"));

    // Check that the block chain matches the known block chain up to a checkpoint
    if (!Checkpoints::CheckHardened(nHeight, hash))
        return DoS(100, error("AcceptBlock() : rejected by hardened checkpoint lock-in at %d", nHeight));

    uint256 hashProof;
    // Verify hash target and signature of coinstake tx
    if (IsProofOfStake())
    {
        uint256 targetProofOfStake;
        //if (!CheckProofOfStake(pindexPrev, vtx[1], nBits, hashProof, targetProofOfStake))
		if (!CheckProofOfStake(vtx[1], nBits, hashProof, targetProofOfStake))
        {
			printf("WARNING: AcceptBlock(): check proof-of-stake failed for block %s\n", hash.ToString().c_str());
            //return error("AcceptBlock() : check proof-of-stake failed for block %s", hash.ToString().c_str());
			return false; // do not error here as we expect this during initial block download
        }
    }
    // PoW is checked in CheckBlock()
    if (IsProofOfWork())
    {
        hashProof = GetPoWHash();
    }

    bool cpSatisfies = Checkpoints::CheckSync(hash, pindexPrev);

    // Check that the block satisfies synchronized checkpoint
    if (CheckpointsMode == Checkpoints::STRICT && !cpSatisfies)
        return error("AcceptBlock() : rejected by synchronized checkpoint");

    if (CheckpointsMode == Checkpoints::ADVISORY && !cpSatisfies)
        strMiscWarning = _("WARNING: syncronized checkpoint violation detected, but skipped!");

    // Enforce rule that the coinbase starts with serialized block height
    CScript expect = CScript() << nHeight;
    if (vtx[0].vin[0].scriptSig.size() < expect.size() ||
        !std::equal(expect.begin(), expect.end(), vtx[0].vin[0].scriptSig.begin()))
        return DoS(100, error("AcceptBlock() : block height mismatch in coinbase"));

    // Write block to history file
    if (!CheckDiskSpace(::GetSerializeSize(*this, SER_DISK, CLIENT_VERSION)))
        return error("AcceptBlock() : out of disk space");
    unsigned int nFile = -1;
    unsigned int nBlockPos = 0;
    if (!WriteToDisk(nFile, nBlockPos))
        return error("AcceptBlock() : WriteToDisk failed");
    if (!AddToBlockIndex(nFile, nBlockPos, hashProof))
        return error("AcceptBlock() : AddToBlockIndex failed");

    // Relay inventory, but don't relay old inventory during initial block download
    int nBlockEstimate = Checkpoints::GetTotalBlocksEstimate();
    if (hashBestChain == hash)
    {
        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes)
            if (nBestHeight > (pnode->nChainHeight != -1 ? pnode->nChainHeight - 2000 : nBlockEstimate))
                pnode->PushInventory(CInv(MSG_BLOCK, hash));
    }

    // ppcoin: check pending sync-checkpoint
    Checkpoints::AcceptPendingSyncCheckpoint();

    return true;
}

uint256 CBlockIndex::GetBlockTrust() const
{
    CBigNum bnTarget;
    bnTarget.SetCompact(nBits);

    if (bnTarget <= 0)
        return 0;

    return ((CBigNum(1)<<256) / (bnTarget+1)).getuint256();
}

bool CBlockIndex::IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned int nRequired, unsigned int nToCheck)
{
    unsigned int nFound = 0;
    for (unsigned int i = 0; i < nToCheck && nFound < nRequired && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }
    return (nFound >= nRequired);
}

bool ProcessBlock(CNode* pfrom, CBlock* pblock)
{
    AssertLockHeld(cs_main);

    int64_t nStartTime = GetTimeMillis();
    // Check for duplicate
    uint256 hash = pblock->GetHash();
    if (mapBlockIndex.count(hash))
        return error("ProcessBlock() : already have block %d %s", mapBlockIndex[hash]->nHeight, hash.ToString().substr(0,20).c_str());
    if (mapOrphanBlocks.count(hash))
        return error("ProcessBlock() : already have block (orphan) %s", hash.ToString().substr(0,20).c_str());

    // ppcoin: check proof-of-stake
    // Limited duplicity on stake: prevents block flood attack
    // Duplicate stake allowed only when there is orphan child block
    if (pblock->IsProofOfStake() && setStakeSeen.count(pblock->GetProofOfStake()) && !mapOrphanBlocksByPrev.count(hash) && !Checkpoints::WantedByPendingSyncCheckpoint(hash))
        return error("ProcessBlock() : duplicate proof-of-stake (%s, %d) for block %s", pblock->GetProofOfStake().first.ToString().c_str(), pblock->GetProofOfStake().second, hash.ToString().c_str());

    // Preliminary checks
    if (!pblock->CheckBlock())
        return error("ProcessBlock() : CheckBlock FAILED");

    CBlockIndex* pcheckpoint = Checkpoints::GetLastSyncCheckpoint();
    if (pcheckpoint && pblock->hashPrevBlock != hashBestChain && !Checkpoints::WantedByPendingSyncCheckpoint(hash))
    {
        // Extra checks to prevent "fill up memory by spamming with bogus blocks"
        int64_t deltaTime = pblock->GetBlockTime() - pcheckpoint->nTime;
        CBigNum bnNewBlock;
        bnNewBlock.SetCompact(pblock->nBits);
        CBigNum bnRequired;

        if (pblock->IsProofOfStake())
            bnRequired.SetCompact(ComputeMinStake(GetLastBlockIndex(pcheckpoint, true)->nBits, deltaTime, pblock->nTime));
        else
            bnRequired.SetCompact(ComputeMinWork(GetLastBlockIndex(pcheckpoint, false)->nBits, deltaTime));

        if (bnNewBlock > bnRequired)
        {
            if (pfrom)
                pfrom->Misbehaving(100);
            return error("ProcessBlock() : block with too little %s", pblock->IsProofOfStake()? "proof-of-stake" : "proof-of-work");
        }
    }

    // Denarius: ask for pending sync-checkpoint if any
    if (!IsInitialBlockDownload()){

        Checkpoints::AskForPendingSyncCheckpoint(pfrom);

        CScript payee;

        if (!fImporting && !fReindex && pindexBest->nHeight > Checkpoints::GetTotalBlocksEstimate()){
            if(fortunastakePayments.GetBlockPayee(pindexBest->nHeight, payee)){
                // MAYBE NEEDS TO BE REWORKED
                //UPDATE FORTUNASTAKE LAST PAID TIME
                // CFortunastake* pmn = mnodeman.Find(vin);
                // if(pmn != NULL) {
                //     pmn->nLastPaid = GetAdjustedTime();
                // }

                printf("ProcessBlock() : Got BlockPayee for block : - %d\n", pindexBest->nHeight);
            }

            forTunaPool.CheckTimeout();
            forTunaPool.NewBlock();
            fortunastakePayments.ProcessBlock((pindexBest->nHeight)+10);

        }

    }

    // If don't already have its previous block, shunt it off to holding area until we get it
    if (!mapBlockIndex.count(pblock->hashPrevBlock)) //pblock->hashPrevBlock != 0 &&
    {
        if (fDebug)
            printf("ProcessBlock: ORPHAN BLOCK, prev=%s\n", pblock->hashPrevBlock.ToString().substr(0,20).c_str());
            //LogPrintf("ProcessBlock: ORPHAN BLOCK %lu, prev=%s\n", (unsigned long)mapOrphanBlocks.size(), pblock->hashPrevBlock.ToString());

        //if (pfrom)
            //PruneOrphanBlocks();

        // ppcoin: check proof-of-stake
        if (pblock->IsProofOfStake())
        {
            // Limited duplicity on stake: prevents block flood attack
            // Duplicate stake allowed only when there is orphan child block
            if (setStakeSeenOrphan.count(pblock->GetProofOfStake()) && !mapOrphanBlocksByPrev.count(hash) && !Checkpoints::WantedByPendingSyncCheckpoint(hash))
                return error("ProcessBlock() : duplicate proof-of-stake (%s, %d) for orphan block %s", pblock->GetProofOfStake().first.ToString().c_str(), pblock->GetProofOfStake().second, hash.ToString().c_str());
            else
                setStakeSeenOrphan.insert(pblock->GetProofOfStake());
        }
        CBlock* pblock2 = new CBlock(*pblock);
        mapOrphanBlocks.insert(make_pair(hash, pblock2));
        mapOrphanBlocksByPrev.insert(make_pair(pblock2->hashPrevBlock, pblock2));

        // Ask this guy to fill in what we're missing
        if (pfrom)
        {
            pfrom->PushGetBlocks(pindexBest, GetOrphanRoot(pblock2));
			//PushGetBlocks(pfrom, pindexBest, GetOrphanRoot(pblock2));
            // ppcoin: getblocks may not obtain the ancestor block rejected
            // earlier by duplicate-stake check so we ask for it again directly
            if (!IsInitialBlockDownload())
                pfrom->AskFor(CInv(MSG_BLOCK, WantedByOrphan(pblock2)));
        }
        return true;
    }

    // Store to disk
    if (!pblock->AcceptBlock())
        return error("ProcessBlock() : AcceptBlock FAILED");

    // Recursively process any orphan blocks that depended on this one
    vector<uint256> vWorkQueue;
    vWorkQueue.push_back(hash);
    for (unsigned int i = 0; i < vWorkQueue.size(); i++)
    {
        uint256 hashPrev = vWorkQueue[i];
        for (multimap<uint256, CBlock*>::iterator mi = mapOrphanBlocksByPrev.lower_bound(hashPrev);
             mi != mapOrphanBlocksByPrev.upper_bound(hashPrev);
             ++mi)
        {
            CBlock* pblockOrphan = (*mi).second;
            if (pblockOrphan->AcceptBlock())
                vWorkQueue.push_back(pblockOrphan->GetHash());
            mapOrphanBlocks.erase(pblockOrphan->GetHash());
            setStakeSeenOrphan.erase(pblockOrphan->GetProofOfStake());
            delete pblockOrphan;
        }
        mapOrphanBlocksByPrev.erase(hashPrev);
    }

    if (fDebug && GetBoolArg("-showtimers", false)) {
        printf("ProcessBlock: ACCEPTED (%" PRId64"ms)\n", GetTimeMillis() - nStartTime);
    } else {
        if (fDebug) printf("ProcessBlock: ACCEPTED\n");
    }

    //After block 1.5m, The Minimum FortunaStake Protocol Version is 31005
    if(nBestHeight >= 1500000 || fTestNet) {
        MIN_MN_PROTO_VERSION = 33933;
    }

    // ppcoin: if responsible for sync-checkpoint send it
    if (pfrom && !CSyncCheckpoint::strMasterPrivKey.empty())
        Checkpoints::SendSyncCheckpoint(Checkpoints::AutoSelectSyncCheckpoint()->GetBlockHash());

    return true;
}

// novacoin: attempt to generate suitable proof-of-stake
bool CBlock::SignBlock(CWallet& wallet, int64_t nFees)
{
    // if we are trying to sign
    //    something except proof-of-stake block template
    if (!vtx[0].vout[0].IsEmpty())
        return false;

    // if we are trying to sign
    //    a complete proof-of-stake block
    if (IsProofOfStake())
        return true;

    // nLastCoinStakeSearchTime = GetAdjustedTime(); // startup timestamp
    // nLastCoinStakeSearchTime = pindexBest->GetBlockTime(); // time of the last block in our index

    CKey key;
    CTransaction txCoinStake; // make a new transaction.
    int64_t nSearchTime = txCoinStake.nTime; // search to current time

    if (fDebug && GetBoolArg("-printcoinstake")) printf ("searchtime %ld to %ld \n",nSearchTime,nLastCoinStakeSearchTime);
    if (nSearchTime > nLastCoinStakeSearchTime)
    {
        if (fDebug && GetBoolArg("-printcoinstake")) printf ("nSearchTime %ld > nLastCoinStakeSearchTime %ld\n",nSearchTime,nLastCoinStakeSearchTime);
        if (wallet.CreateCoinStake(wallet, nBits, nSearchTime-nLastCoinStakeSearchTime, nFees, txCoinStake, key))
        {
            if (fDebug && GetBoolArg("-printcoinstake")) printf ("CreateCoinStake succeeded \n");
            if (txCoinStake.nTime >= max(pindexBest->GetPastTimeLimit()+1, PastDrift(pindexBest->GetBlockTime())))
            {
                if (fDebug && GetBoolArg("-printcoinstake")) printf ("txCoinStake.nTime >= max(pindexBest->GetPastTimeLimit()+1, PastDrift(pindexBest->GetBlockTime()))");
                // make sure coinstake would meet timestamp protocol
                //    as it would be the same as the block timestamp
                vtx[0].nTime = nTime = txCoinStake.nTime;
                nTime = max(pindexBest->GetPastTimeLimit()+1, GetMaxTransactionTime());
                nTime = max(GetBlockTime(), PastDrift(pindexBest->GetBlockTime()));

                // we have to make sure that we have no future timestamps in
                //    our transactions set
                for (vector<CTransaction>::iterator it = vtx.begin(); it != vtx.end();)
                    if (it->nTime > nTime) { it = vtx.erase(it); } else { ++it; }

                vtx.insert(vtx.begin() + 1, txCoinStake);
                hashMerkleRoot = BuildMerkleTree();

                // append a signature to our block
                return key.Sign(GetHash(), vchBlockSig);
            }
        }
        nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
        if (fDebug && GetBoolArg("-printcoinstake")) printf ("CreateCoinStake failed at %ld. Try again in %ld\n",nLastCoinStakeSearchTime,nLastCoinStakeSearchInterval);
    }

    return false;
}

bool CBlock::CheckBlockSignature() const
{
    if (IsProofOfWork())
        return vchBlockSig.empty();

    vector<valtype> vSolutions;
    txnouttype whichType;

    const CTxOut& txout = vtx[1].vout[1];

    if (!Solver(txout.scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_PUBKEY)
    {
        valtype& vchPubKey = vSolutions[0];
        return CPubKey(vchPubKey).Verify(GetHash(), vchBlockSig);
    }

    return false;
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = filesystem::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
    {
        fShutdown = true;
        string strMessage = _("Warning: Disk space is low!");
        strMiscWarning = strMessage;
        printf("*** %s\n", strMessage.c_str());
        uiInterface.ThreadSafeMessageBox(strMessage, "Denarius", CClientUIInterface::OK | CClientUIInterface::ICON_EXCLAMATION | CClientUIInterface::MODAL);
        StartShutdown();
        return false;
    }
    return true;
}

static unsigned int nCurrentBlockFile = 1;

static filesystem::path BlockFilePath(unsigned int nFile)
{
    string strBlockFn = strprintf("blk%04u.dat", nFile);
    return GetDataDir() / strBlockFn;
}

FILE* OpenBlockFile(unsigned int nFile, unsigned int nBlockPos, const char* pszMode)
{
    if ((nFile < 1) || (nFile == (unsigned int) -1))
        return NULL;
    FILE* file = fopen(BlockFilePath(nFile).string().c_str(), pszMode);
    if (!file)
        return NULL;
    if (nBlockPos != 0 && !strchr(pszMode, 'a') && !strchr(pszMode, 'w'))
    {
        if (fseek(file, nBlockPos, SEEK_SET) != 0)
        {
            fclose(file);
            return NULL;
        }
    }
    return file;
}

FILE* AppendBlockFile(unsigned int& nFileRet)
{
    nFileRet = 0;
    while (true)
    {
        FILE* file = OpenBlockFile(nCurrentBlockFile, 0, "ab");
        if (!file)
            return NULL;
        if (fseek(file, 0, SEEK_END) != 0)
            return NULL;
        // FAT32 file size max 4GB, fseek and ftell max 2GB, so we must stay under 2GB
        if (ftell(file) < (long)(0x7F000000 - MAX_SIZE))
        {
            nFileRet = nCurrentBlockFile;
            return file;
        }
        fclose(file);
        nCurrentBlockFile++;
    }
}

bool LoadBlockIndex(bool fAllowNew)
{
    LOCK(cs_main);

    if (fTestNet)
    {
        pchMessageStart[0] = 0x07;
        pchMessageStart[1] = 0x11;
        pchMessageStart[2] = 0x05;
        pchMessageStart[3] = 0x0b;

        bnProofOfWorkLimit = bnProofOfWorkLimitTestNet; // 16 bits PoW target limit for testnet
        nStakeMinAge = 8 * 60 * 60; // test net min age is 8 hours like mainnet
        nCoinbaseMaturity = 20; // test maturity is 20 blocks
    };

    //
    // Load block index
    //
    CTxDB txdb("cr+");
    if (!txdb.LoadBlockIndex())
        return false;
    if (!pwalletMain->CacheAnonStats())
        printf("CacheAnonStats() failed.\n");

    //
    // Init with genesis block
    //
    if (mapBlockIndex.empty())
    {
        if (!fAllowNew)
            return false;

        if(fTestNet)
        {
            const char* pszTimestampTestNet = "https://blockforums.org/topic/417-denarius-cryptocurrency-marketing-images-pdf-svg-jpg-png/";
            CTransaction txNewTestNet;

            txNewTestNet.nTime = 1606593576;
            txNewTestNet.vin.resize(1);
            txNewTestNet.vout.resize(1);
            txNewTestNet.vin[0].scriptSig = CScript() << 0 << CBigNum(42) << vector<unsigned char>((const unsigned char*)pszTimestampTestNet, (const unsigned char*)pszTimestampTestNet + strlen(pszTimestampTestNet));
            txNewTestNet.vout[0].SetEmpty();

            CBlock blocktest;
            blocktest.vtx.push_back(txNewTestNet);
            blocktest.hashPrevBlock = 0;
            blocktest.hashMerkleRoot = blocktest.BuildMerkleTree();
            blocktest.nTime    = 1606593576; //1497476511 - New D Testnet on 11/27/2020
            blocktest.nVersion = 1;
            blocktest.nBits    = bnProofOfWorkLimit.GetCompact();
            blocktest.nNonce   = 4950; //13278

            if (false && (blocktest.GetHash() != hashGenesisBlockTestNet)) {
            // This will figure out a valid hash and Nonce if you're
            // creating a different genesis block:
                uint256 hashTarget = CBigNum().SetCompact(blocktest.nBits).getuint256();
                while (blocktest.GetHash() > hashTarget)
                {
                    ++blocktest.nNonce;
                    if (blocktest.nNonce == 0)
                    {
                        printf("NONCE WRAPPED, incrementing time");
                        ++blocktest.nTime;
                    }
                }
            }
            blocktest.print();
            printf("TestNet blocktest.GetHash() == %s\n", blocktest.GetHash().ToString().c_str());
            printf("TestNet blocktest.hashMerkleRoot == %s\n", blocktest.hashMerkleRoot.ToString().c_str());
            printf("TestNet blocktest.nTime = %u \n", blocktest.nTime);
            printf("TestNet blocktest.nNonce = %u \n", blocktest.nNonce);


            //// debug print
            assert(blocktest.hashMerkleRoot == uint256("0x4de1a91e22664438d714ca7a01eab02ead6eb8c6fe63eab508849ffb4cb0e5dc"));
            blocktest.print();
            assert(blocktest.GetHash() == hashGenesisBlockTestNet);
            assert(blocktest.CheckBlock());

            // -- debug print
            if (fDebugChain)
            {
                printf("Initialised Denarius TestNet genesis block:\n");
                blocktest.print();
            };

            // Start new block file
            unsigned int nFile;
            unsigned int nBlockPos;
            if (!blocktest.WriteToDisk(nFile, nBlockPos))
                return error("TestNetLoadBlockIndex() : writing genesis block to disk failed");
            if (!blocktest.AddToBlockIndex(nFile, nBlockPos, hashGenesisBlockTestNet))
                return error("TestNetLoadBlockIndex() : Testnet genesis block not accepted");

            // ppcoin: initialize synchronized checkpoint
            if (!Checkpoints::WriteSyncCheckpoint(hashGenesisBlockTestNet))
                return error("TestNetLoadBlockIndex() : failed to init sync checkpoint");

        } else {

            const char* pszTimestamp = "http://www.coindesk.com/bitcoin-scaling-give-everyone-control/";
            CTransaction txNew;
            txNew.nTime = 1497476511;
            txNew.vin.resize(1);
            txNew.vout.resize(1);
            txNew.vin[0].scriptSig = CScript() << 0 << CBigNum(42) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
            txNew.vout[0].SetEmpty();

            CBlock block;
            block.vtx.push_back(txNew);
            block.hashPrevBlock = 0;
            block.hashMerkleRoot = block.BuildMerkleTree();
            block.nTime    = 1497476511;
            block.nVersion = 1;
            block.nBits    = bnProofOfWorkLimit.GetCompact();
            block.nNonce   = 41660;

            if (false && (block.GetHash() != hashGenesisBlock)) {
            // This will figure out a valid hash and Nonce if you're
            // creating a different genesis block:
                uint256 hashTarget = CBigNum().SetCompact(block.nBits).getuint256();
                while (block.GetHash() > hashTarget)
                {
                    ++block.nNonce;
                    if (block.nNonce == 0)
                    {
                        printf("NONCE WRAPPED, incrementing time");
                        ++block.nTime;
                    }
                }
            }
            block.print();
            printf("block.GetHash() == %s\n", block.GetHash().ToString().c_str());
            printf("block.hashMerkleRoot == %s\n", block.hashMerkleRoot.ToString().c_str());
            printf("block.nTime = %u \n", block.nTime);
            printf("block.nNonce = %u \n", block.nNonce);


            //// debug print
            assert(block.hashMerkleRoot == uint256("0xc6d8e8f56c25cac33567e571a3497bfc97f715140fcfe16d971333b38e4ee0f2"));
            block.print();
            assert(block.GetHash() == hashGenesisBlock);
            assert(block.CheckBlock());

            // -- debug print
            if (fDebugChain)
            {
                printf("Initialised genesis block:\n");
                block.print();
            };

            // Start new block file
            unsigned int nFile;
            unsigned int nBlockPos;
            if (!block.WriteToDisk(nFile, nBlockPos))
                return error("LoadBlockIndex() : writing genesis block to disk failed");
            if (!block.AddToBlockIndex(nFile, nBlockPos, hashGenesisBlock))
                return error("LoadBlockIndex() : genesis block not accepted");

            // ppcoin: initialize synchronized checkpoint
            if (!Checkpoints::WriteSyncCheckpoint(hashGenesisBlock))
                return error("LoadBlockIndex() : failed to init sync checkpoint");
        }
    }

    string strPubKey = "";

    // if checkpoint master key changed must reset sync-checkpoint
    if (!txdb.ReadCheckpointPubKey(strPubKey) || strPubKey != CSyncCheckpoint::strMasterPubKey)
    {
        // write checkpoint master key to db
        txdb.TxnBegin();
        if (!txdb.WriteCheckpointPubKey(CSyncCheckpoint::strMasterPubKey))
            return error("LoadBlockIndex() : failed to write new checkpoint master key to db");
        if (!txdb.TxnCommit())
            return error("LoadBlockIndex() : failed to commit new checkpoint master key to db");
        if ((!fTestNet) && !Checkpoints::ResetSyncCheckpoint())
            return error("LoadBlockIndex() : failed to reset sync-checkpoint");
    }

    return true;
}



void PrintBlockTree()
{
    AssertLockHeld(cs_main);
    // pre-compute tree structure
    map<CBlockIndex*, vector<CBlockIndex*> > mapNext;
    for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
    {
        CBlockIndex* pindex = (*mi).second;
        mapNext[pindex->pprev].push_back(pindex);
        // test
        //while (rand() % 3 == 0)
        //    mapNext[pindex->pprev].push_back(pindex);
    }

    vector<pair<int, CBlockIndex*> > vStack;
    vStack.push_back(make_pair(0, pindexGenesisBlock));

    int nPrevCol = 0;
    while (!vStack.empty())
    {
        int nCol = vStack.back().first;
        CBlockIndex* pindex = vStack.back().second;
        vStack.pop_back();

        // print split or gap
        if (nCol > nPrevCol)
        {
            for (int i = 0; i < nCol-1; i++)
                printf("| ");
            printf("|\\\n");
        }
        else if (nCol < nPrevCol)
        {
            for (int i = 0; i < nCol; i++)
                printf("| ");
            printf("|\n");
       }
        nPrevCol = nCol;

        // print columns
        for (int i = 0; i < nCol; i++)
            printf("| ");

        // print item
        CBlock block;
        block.ReadFromDisk(pindex);
        printf("%d (%u,%u) %s  %08x  %s  mint %7s  tx %" PRIszu"",
            pindex->nHeight,
            pindex->nFile,
            pindex->nBlockPos,
            block.GetHash().ToString().c_str(),
            block.nBits,
            DateTimeStrFormat("%x %H:%M:%S", block.GetBlockTime()).c_str(),
            FormatMoney(pindex->nMint).c_str(),
            block.vtx.size());

        //PrintWallets(block);

        // put the main time-chain first
        vector<CBlockIndex*>& vNext = mapNext[pindex];
        for (unsigned int i = 0; i < vNext.size(); i++)
        {
            if (vNext[i]->pnext)
            {
                swap(vNext[0], vNext[i]);
                break;
            }
        }

        // iterate children
        for (unsigned int i = 0; i < vNext.size(); i++)
            vStack.push_back(make_pair(nCol+i, vNext[i]));
    }
}

bool LoadExternalBlockFile(FILE* fileIn)
{
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    {
        LOCK(cs_main);
        try {
            CAutoFile blkdat(fileIn, SER_DISK, CLIENT_VERSION);
            unsigned int nPos = 0;
            while (nPos != (unsigned int)-1 && blkdat.good() && !fRequestShutdown)
            {
                unsigned char pchData[65536];
                do {
                    fseek(blkdat, nPos, SEEK_SET);
                    int nRead = fread(pchData, 1, sizeof(pchData), blkdat);
                    if (nRead <= 8)
                    {
                        nPos = (unsigned int)-1;
                        break;
                    }
                    void* nFind = memchr(pchData, pchMessageStart[0], nRead+1-sizeof(pchMessageStart));
                    if (nFind)
                    {
                        if (memcmp(nFind, pchMessageStart, sizeof(pchMessageStart))==0)
                        {
                            nPos += ((unsigned char*)nFind - pchData) + sizeof(pchMessageStart);
                            break;
                        }
                        nPos += ((unsigned char*)nFind - pchData) + 1;
                    }
                    else
                        nPos += sizeof(pchData) - sizeof(pchMessageStart) + 1;
                } while(!fRequestShutdown);
                if (nPos == (unsigned int)-1)
                    break;
                fseek(blkdat, nPos, SEEK_SET);
                unsigned int nSize;
                blkdat >> nSize;
                if (nSize > 0 && nSize <= MAX_BLOCK_SIZE)
                {
                    CBlock block;
                    blkdat >> block;
                    if (ProcessBlock(NULL,&block))
                    {
                        nLoaded++;
                        nPos += 4 + nSize;
                    }
                }
            }
        }
        catch (std::exception &e) {
            printf("%s() : Deserialize or I/O error caught during load\n",
                   __PRETTY_FUNCTION__);
        }
    }
    printf("Loaded %i blocks from external file in %" PRId64"ms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//

extern map<uint256, CAlert> mapAlerts;
extern CCriticalSection cs_mapAlerts;

string GetWarnings(string strFor)
{
    int nPriority = 0;
    string strStatusBar;
    string strRPC;

    if (GetBoolArg("-testsafemode"))
        strRPC = "test";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "")
    {
        nPriority = 1000;
        strStatusBar = strMiscWarning;
    }

    // if detected invalid checkpoint enter safe mode
    if (Checkpoints::hashInvalidCheckpoint != 0)
    {
        nPriority = 3000;
        strStatusBar = strRPC = _("WARNING: Invalid checkpoint found! Displayed transactions may not be correct! You may need to upgrade, or notify developers.");
    }

    // Alerts
    {
        LOCK(cs_mapAlerts);
        for (PAIRTYPE(const uint256, CAlert)& item : mapAlerts)
        {
            const CAlert& alert = item.second;
            if (alert.AppliesToMe() && alert.nPriority > nPriority)
            {
                nPriority = alert.nPriority;
                strStatusBar = alert.strStatusBar;
                if (nPriority > 1000)
                    strRPC = strStatusBar;
            }
        }
    }

    if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings() : invalid parameter");
    return "error";
}








//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(CTxDB& txdb, const CInv& inv)
{
    switch (inv.type)
    {
    case MSG_TX:
        {
        bool txInMap = false;
        txInMap = mempool.exists(inv.hash);
        return txInMap ||
               mapOrphanTransactions.count(inv.hash) ||
               txdb.ContainsTx(inv.hash);
        }

    case MSG_BLOCK:
        return mapBlockIndex.count(inv.hash) ||
               mapOrphanBlocks.count(inv.hash);
    case MSG_SPORK:
        return mapSporks.count(inv.hash);
    case MSG_FORTUNASTAKE_WINNER:
        return mapSeenFortunastakeVotes.count(inv.hash);
    }
    // Don't know what it is, just say we already got one
    return true;
}

void static ProcessGetData(CNode* pfrom)
{
    if (fDebugNet)
      printf("ProcessGetData\n");

    std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();

    vector<CInv> vNotFound;

    LOCK(cs_main);

    while (it != pfrom->vRecvGetData.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        if (fShutdown)
            return;

        const CInv &inv = *it;
        {
            boost::this_thread::interruption_point();
            it++;

            if (inv.type == MSG_BLOCK)
            {
                bool send = false;
                // Send block from disk
                map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(inv.hash);
                if (mi != mapBlockIndex.end())
                {
                    send = true;
                    CBlock block;
                    block.ReadFromDisk((*mi).second);
                    pfrom->PushMessage("block", block);

                    // Trigger them to send a getblocks request for the next batch of inventory
                    if (inv.hash == pfrom->hashContinue)
                    {
                        // Bypass PushInventory, this must send even if redundant,
                        // and we want it right after the last block so they don't
                        // wait for other stuff first.
                        vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, hashBestChain));
                        pfrom->PushMessage("inv", vInv);
                        pfrom->hashContinue = 0;
                    }
                }
                // disconnect node in case we have reached the outbound limit for serving historical blocks
                static const int nOneWeek = 7 * 24 * 60 * 60; // assume > 1 week = historical
                if (send && CNode::OutboundTargetReached(true) && 
                ( 
                    ((pindexBest != NULL) && 
                    (pindexBest->GetBlockTime() - mi->second->GetBlockTime() > nOneWeek)) || 
                    inv.type == MSG_BLOCK
                    ) && !pfrom->fWhitelisted)                
                {
                    printf("net historical block serving limit reached, disconnected peer=%d\n", pfrom->GetId());

                    //disconnect node
                    pfrom->fDisconnect = true;
                    send = false;
                }
            }
            else if (inv.IsKnownType())
            {
                // Send stream from relay memory
                bool pushed = false;
                /*{
                    LOCK(cs_mapRelay);
                    map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
                    if (mi != mapRelay.end()) {
                        pfrom->PushMessage(inv.GetCommand(), (*mi).second);
                        pushed = true;
                    }
                }*/
                if (!pushed && inv.type == MSG_TX) {
                    if(mapFortunaBroadcastTxes.count(inv.hash)){
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss <<
                            mapFortunaBroadcastTxes[inv.hash].tx <<
                            mapFortunaBroadcastTxes[inv.hash].vin <<
                            mapFortunaBroadcastTxes[inv.hash].vchSig <<
                            mapFortunaBroadcastTxes[inv.hash].sigTime;

                        pfrom->PushMessage("dstx", ss);
                        pushed = true;
                    } else {
                        CTransaction tx;
                        if (mempool.lookup(inv.hash, tx)) {
                            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                            ss.reserve(1000);
                            ss << tx;
                            pfrom->PushMessage("tx", ss);
                            pushed = true;
                        }
                    }
                }
                if (!pushed && inv.type == MSG_SPORK) {
                    if(mapSporks.count(inv.hash)){
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << mapSporks[inv.hash];
                        pfrom->PushMessage("spork", ss);
                        pushed = true;
                    }
                }
                if (!pushed && inv.type == MSG_FORTUNASTAKE_WINNER) {
                    if(mapSeenFortunastakeVotes.count(inv.hash)){
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        int a = 0;
                        ss.reserve(1000);
                        ss << mapSeenFortunastakeVotes[inv.hash] << a;
                        pfrom->PushMessage("mnw", ss);
                        pushed = true;
                    }
                }
                if (!pushed) {
                    vNotFound.push_back(inv);
                }
            }

            // Track requests for our stuff.
            g_signals.Inventory(inv.hash);

            if (inv.type == MSG_BLOCK /* || inv.type == MSG_FILTERED_BLOCK */)
                break;
        }
    }

    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage("notfound", vNotFound);
    }
}

// The message start string is designed to be unlikely to occur in normal data.
// The characters are rarely used upper ASCII, not valid as UTF-8, and produce
// a large 4-byte int at any alignment.
unsigned char pchMessageStart[4] = { 0xfa, 0xf2, 0xef, 0xb4 };

bool static ProcessMessage(CNode* pfrom, string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{
    static map<CService, CPubKey> mapReuseKey;
    RandAddSeedPerfmon();
    if (fDebugNet)
        printf("received: %s (%" PRIszu" bytes)\n", strCommand.c_str(), vRecv.size());
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        printf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (strCommand == "version")
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->Misbehaving(1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;

        // Old Node Versioning with Block Height Code
        bool oldVersion = false;

        if (pfrom->nVersion < MIN_PEER_PROTO_VERSION)
            oldVersion = true;

        /*
        if (pfrom->nVersion < PROTO_VERSION)
        {
            // disconnect from peers older than this proto version
            printf("partner %s using obsolete version %i; disconnecting\n", pfrom->addr.ToString().c_str(), pfrom->nVersion);
            pfrom->fDisconnect = true;
            return false;
        }*/

        if (pfrom->nVersion == 10300)
            pfrom->nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty())
            vRecv >> pfrom->strSubVer;
        if (!vRecv.empty())
            vRecv >> pfrom->nChainHeight;

        // Disconnect if the peer's subversion is < /Denarii:3.3.9.14/
        // Leaving this out for now until new update is out for a bit
        // if (pfrom->strSubVer != "/Denarii:3.3.9.14/")
        //     oldVersion = true;

        // print the current pfrom->strSubVer
        printf("ProcessMessage(): peer=%s using SubVer=%s, oldVersion=%s\n", pfrom->addr.ToString().c_str(), pfrom->strSubVer.c_str(), oldVersion ? "true" : "false");

        if (oldVersion == true)
        {
          printf("Partner %s using obsolete version %i; DISCONNECTING\n", pfrom->addr.ToString().c_str(), pfrom->nVersion);
          pfrom->fDisconnect = true;
          if (pfrom->fForTunaMaster)
              printf("Masternode hosting node version was obsolete. This masternode should be removed from the list\n");
          return false;
        }
        
        // if (pfrom->nSendBytes >= 1000000) // New arg flag per peer 1MB 1000000 bytes
        // {
        //     printf("data sent by peer = %i, disconnecting\n", pfrom->nSendBytes);
        //     printf("disconnecting node from max outbound per peer target: %s\n", pfrom->addr.ToString().c_str());
        //     pfrom->fDisconnect = true;
        //     return false;
        // }    

        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            pfrom->addrLocal = addrMe;
            SeenLocal(addrMe);
        }

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            printf("connected to self at %s, disconnecting\n", pfrom->addr.ToString().c_str());
            pfrom->fDisconnect = true;
            return true;
        }

        // record my external IP reported by peer
        if (addrFrom.IsRoutable() && addrMe.IsRoutable())
            addrSeenByPeer = addrMe;

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        if (GetBoolArg("-synctime", true))
            AddTimeData(pfrom->addr, nTime);

        // Change version
        pfrom->PushMessage("verack");
        pfrom->ssSend.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (!fNoListen && !IsInitialBlockDownload())
            {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                if (addr.IsRoutable())
                    pfrom->PushAddress(addr);
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
            {
                pfrom->PushMessage("getaddr");
                pfrom->fGetAddr = true;
            }
            addrman.Good(pfrom->addr);
        } else {
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        // Ask every node for the fortunastake list straight away
        pfrom->PushMessage("dseg", CTxIn());

        // Ask the first connected node for block updates
        static int nAskedForBlocks = 0;
        if (!pfrom->fClient && !pfrom->fOneShot &&
            (pfrom->nChainHeight > (nBestHeight - 144)) &&
            (pfrom->nVersion < NOBLKS_VERSION_START ||
             pfrom->nVersion >= NOBLKS_VERSION_END) &&
             (nAskedForBlocks < 1 || vNodes.size() <= 1))
        {
            nAskedForBlocks++;
            pfrom->PushGetBlocks(pindexBest, uint256(0));
			//PushGetBlocks(pfrom, pindexBest, uint256(0));
        }

        // Relay alerts
        {
            LOCK(cs_mapAlerts);
            for (PAIRTYPE(const uint256, CAlert)& item : mapAlerts)
                item.second.RelayTo(pfrom);
        }

        // Relay sync-checkpoint
        {
            LOCK(Checkpoints::cs_hashSyncCheckpoint);
            if (!Checkpoints::checkpointMessage.IsNull())
                Checkpoints::checkpointMessage.RelayTo(pfrom);
        }

        pfrom->fSuccessfullyConnected = true;
        pfrom->fRelayTxes = true;

        printf("receive version message: version %d, blocks=%d, us=%s, them=%s, peer=%s\n", pfrom->nVersion, pfrom->nChainHeight, addrMe.ToString().c_str(), addrFrom.ToString().c_str(), pfrom->addr.ToString().c_str());

        cPeerBlockCounts.input(pfrom->nChainHeight);

        // ppcoin: ask for pending sync-checkpoint if any
        if (!IsInitialBlockDownload())
            Checkpoints::AskForPendingSyncCheckpoint(pfrom);
    }


    else if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else, as it is sent as soon as the socket opens
        pfrom->Misbehaving(1);
        if (fDebug) printf("net: received an out-of-sequence %s from peer at %s\n", strCommand.c_str(), pfrom->addr.ToString().c_str());
        if (pfrom->nMisbehavior > 10 || pfrom->nTimeConnected < GetTime() - 10)
            pfrom->fDisconnect = true; // Disconnect them so we can reconnect and try for another version message
        return false;
    }


    else if (strCommand == "verack")
    {
        pfrom->SetRecvVersion(min(pfrom->nVersion, PROTOCOL_VERSION));
        printf("net: received verack from peer version %d (recvVersion: %d) at %s\n", pfrom->nVersion, pfrom->nRecvVersion, pfrom->addr.ToString().c_str());
    }


    else if (strCommand == "addr")
    {
        vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            pfrom->Misbehaving(20);
            return error("message addr size() = %" PRIszu"", vAddr.size());
        }

        // Store the new addresses
        vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        for (CAddress& addr : vAddr)
        {
            if (fShutdown)
                return true;
            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the setAddrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt == 0)
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = hashSalt ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    multimap<uint256, CNode*> mapMix;
                    for (CNode* pnode : vNodes)
                    {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = hashRand ^ nPointer;
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
    }

    else if (strCommand == "inv")
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            pfrom->Misbehaving(20);
            return error("message inv size() = %" PRIszu"", vInv.size());
        }

        // find last block in inv vector
        unsigned int nLastBlock = (unsigned int)(-1);
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++) {
            if (vInv[vInv.size() - 1 - nInv].type == MSG_BLOCK) {
                nLastBlock = vInv.size() - 1 - nInv;
                break;
            }
        }

        LOCK(cs_main);
        CTxDB txdb("r");

        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            const CInv &inv = vInv[nInv];

            if (fShutdown)
                return true;

            boost::this_thread::interruption_point();
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(txdb, inv);
            if (fDebugNet)
                printf("  got inventory: %s  %s\n", inv.ToString().c_str(), fAlreadyHave ? "have" : "new");

            if (!fAlreadyHave)
                pfrom->AskFor(inv);                
            else if (inv.type == MSG_BLOCK && mapOrphanBlocks.count(inv.hash)) {
                pfrom->PushGetBlocks(pindexBest, GetOrphanRoot(mapOrphanBlocks[inv.hash]));
				//PushGetBlocks(pfrom, pindexBest, GetOrphanRoot(mapOrphanBlocks[inv.hash]));
            } else if (nInv == nLastBlock) {
                // In case we are on a very long side-chain, it is possible that we already have
                // the last block in an inv bundle sent in response to getblocks. Try to detect
                // this situation and push another getblocks to continue.
                pfrom->PushGetBlocks(mapBlockIndex[inv.hash], uint256(0));
				//PushGetBlocks(pfrom, mapBlockIndex[inv.hash], uint256(0));
                if (fDebugNet)
                    printf("force request: %s\n", inv.ToString().c_str());
            }

            // Don't bother if send buffer is too full to respond anyway
            if (pfrom->nSendSize >= SendBufferSize()) {
                pfrom->Misbehaving(50);
                return error("send buffer size() = %" PRIszu"", pfrom->nSendSize);
            }

            // Track requests for our stuff
            g_signals.Inventory(inv.hash);
        }
    }


    else if (strCommand == "getdata")
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        printf("received getdata (%" PRIszu" invsz)\n", vInv.size());
        if (vInv.size() > MAX_INV_SZ)
        {
            pfrom->Misbehaving(20);
            return error("message getdata size() = %" PRIszu"", vInv.size());
        }

        if (fDebugNet || (vInv.size() != 1))
            printf("received getdata (%" PRIszu" invsz)\n", vInv.size());

        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
        ProcessGetData(pfrom);
    }


    else if (strCommand == "getblocks")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        CBlockIndex* pindex = locator.GetBlockIndex();

        // Send the rest of the chain
        if (pindex)
            pindex = pindex->pnext;
        int nLimit = 1000;
        if (fDebugNet) printf("getblocks %d to %s limit %d\n", (pindex ? pindex->nHeight : -1), hashStop.ToString().substr(0,20).c_str(), nLimit);
        for (; pindex; pindex = pindex->pnext)
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                if (fDebugNet) printf("  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString().substr(0,20).c_str());
                // ppcoin: tell downloading node about the latest block if it's
                // without risk being rejected due to stake connection check
                if (hashStop != hashBestChain && pindex->GetBlockTime() + nStakeMinAge > pindexBest->GetBlockTime())
                    pfrom->PushInventory(CInv(MSG_BLOCK, hashBestChain));
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll make them
                // getblocks the next batch of inventory.
                if (fDebugNet) printf("  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString().substr(0,20).c_str());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }
    else if (strCommand == "checkpoint")
    {
        CSyncCheckpoint checkpoint;
        vRecv >> checkpoint;

        if (checkpoint.ProcessSyncCheckpoint(pfrom))
        {
            // Relay
            pfrom->hashCheckpointKnown = checkpoint.hashCheckpoint;
            LOCK(cs_vNodes);
            for (CNode* pnode : vNodes)
                checkpoint.RelayTo(pnode);
        }
    }

    else if (strCommand == "getheaders")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        if (IsInitialBlockDownload())
            return true;

        CBlockIndex* pindex = NULL;
        if (locator.IsNull())
        {
            // If locator is null, return the hashStop block
            map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashStop);
            if (mi == mapBlockIndex.end())
                return true;
            pindex = (*mi).second;
        }
        else
        {
            // Find the last block the caller has in the main chain
            pindex = locator.GetBlockIndex();
            if (pindex)
                pindex = pindex->pnext;
        }

        vector<CBlock> vHeaders;
        int nLimit = 2000;
        if (fDebugNet) printf("getheaders %d to %s\n", (pindex ? pindex->nHeight : -1), hashStop.ToString().substr(0,20).c_str());
        for (; pindex; pindex = pindex->pnext)
        {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        pfrom->PushMessage("headers", vHeaders);
    }


    else if (strCommand == "tx")
    {
        vector<uint256> vWorkQueue;
        vector<uint256> vEraseQueue;
        CTxDB txdb("r");
        CTransaction tx;
        vRecv >> tx;

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        bool fMissingInputs = false;
        if (tx.AcceptToMemoryPool(txdb, &fMissingInputs))
        {
            SyncWithWallets(tx, NULL, true);
            RelayTransaction(tx, inv.hash);
            mapAlreadyAskedFor.erase(inv);
            vWorkQueue.push_back(inv.hash);
            vEraseQueue.push_back(inv.hash);

            // Recursively process any orphan transactions that depended on this one
            for (unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                uint256 hashPrev = vWorkQueue[i];
                for (set<uint256>::iterator mi = mapOrphanTransactionsByPrev[hashPrev].begin();
                     mi != mapOrphanTransactionsByPrev[hashPrev].end();
                     ++mi)
                {
                    const uint256& orphanTxHash = *mi;
                    CTransaction& orphanTx = mapOrphanTransactions[orphanTxHash];
                    bool fMissingInputs2 = false;

                    if (orphanTx.AcceptToMemoryPool(txdb, &fMissingInputs2))
                    {
                        printf("   accepted orphan tx %s\n", orphanTxHash.ToString().substr(0,10).c_str());
                        SyncWithWallets(tx, NULL, true);
                        RelayTransaction(orphanTx, orphanTxHash);
                        mapAlreadyAskedFor.erase(CInv(MSG_TX, orphanTxHash));
                        vWorkQueue.push_back(orphanTxHash);
                        vEraseQueue.push_back(orphanTxHash);
                    }
                    else if (!fMissingInputs2)
                    {
                        // invalid orphan
                        vEraseQueue.push_back(orphanTxHash);
                        printf("   removed invalid orphan tx %s\n", orphanTxHash.ToString().substr(0,10).c_str());
                    }
                }
            }

            for (uint256 hash : vEraseQueue)
                EraseOrphanTx(hash);
        }
        else if (fMissingInputs)
        {
            AddOrphanTx(tx);

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            //unsigned int nEvicted = LimitOrphanTxSize(MAX_ORPHAN_TRANSACTIONS);
            unsigned int nMaxOrphanTx = (unsigned int)std::max((int64_t)0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
            unsigned int nEvicted = LimitOrphanTxSize(nMaxOrphanTx);

            if (nEvicted > 0)
                printf("mapOrphan overflow, removed %u tx\n", nEvicted);
        }
        if (tx.nDoS) pfrom->Misbehaving(tx.nDoS);
    }


    else if (strCommand == "block")
    {
        CBlock block;
        vRecv >> block;
        uint256 hashBlock = block.GetHash();

        if (fDebugNet) printf("received block %s\n", hashBlock.ToString().substr(0,20).c_str());
        // block.print();

        CInv inv(MSG_BLOCK, hashBlock);
        pfrom->AddInventoryKnown(inv);

        LOCK(cs_main);
        if (ProcessBlock(pfrom, &block))
            mapAlreadyAskedFor.erase(inv);

        if (block.nDoS)
            pfrom->Misbehaving(block.nDoS);

        if (fSecMsgEnabled)
            SecureMsgScanBlock(block);
    }


    else if (strCommand == "getaddr")
    {
        // Don't return addresses older than nCutOff timestamp
        int64_t nCutOff = GetTime() - (nNodeLifespan * 24 * 60 * 60);
        pfrom->vAddrToSend.clear();
        vector<CAddress> vAddr = addrman.GetAddr();
        for (const CAddress &addr : vAddr)
            if(addr.nTime > nCutOff)
                pfrom->PushAddress(addr);
    }


    else if (strCommand == "mempool")
    {
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        vector<CInv> vInv;
        for (unsigned int i = 0; i < vtxid.size(); i++) {
            CInv inv(MSG_TX, vtxid[i]);
            vInv.push_back(inv);
            if (i == (MAX_INV_SZ - 1))
                    break;
        }
        if (vInv.size() > 0)
            pfrom->PushMessage("inv", vInv);
    }


    else if (strCommand == "checkorder")
    {
        uint256 hashReply;
        vRecv >> hashReply;

        if (!GetBoolArg("-allowreceivebyip"))
        {
            pfrom->PushMessage("reply", hashReply, (int)2, string(""));
            return true;
        }

        CWalletTx order;
        vRecv >> order;

        /// we have a chance to check the order here

        // Keep giving the same key to the same ip until they use it
        if (!mapReuseKey.count(pfrom->addr))
            pwalletMain->GetKeyFromPool(mapReuseKey[pfrom->addr], true);

        // Send back approval of order and pubkey to use
        CScript scriptPubKey;
        scriptPubKey << mapReuseKey[pfrom->addr] << OP_CHECKSIG;
        pfrom->PushMessage("reply", hashReply, (int)0, scriptPubKey);
    }


    else if (strCommand == "reply")
    {
        uint256 hashReply;
        vRecv >> hashReply;

        CRequestTracker tracker;
        {
            LOCK(pfrom->cs_mapRequests);
            map<uint256, CRequestTracker>::iterator mi = pfrom->mapRequests.find(hashReply);
            if (mi != pfrom->mapRequests.end())
            {
                tracker = (*mi).second;
                pfrom->mapRequests.erase(mi);
            }
        }
        if (!tracker.IsNull())
            tracker.fn(tracker.param1, vRecv);
    }


    else if (strCommand == "ping")
    {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            pfrom->PushMessage("pong", nonce);
        }
    }


    else if (strCommand == "pong")
    {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce)) {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0) {
                if (nonce == pfrom->nPingNonceSent) {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0) {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        if (fDebug) { printf("Ping time for peer %s: %d msec\n", pfrom->addr.ToString().c_str(), (((double)pfrom->nPingUsecTime) / 1e6)); }
                    } else {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                } else {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0) {
                        // This is most likely a bug in another implementation somewhere, cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            } else {
                sProblem = "Unsolicited pong without ping";
            }
        } else {
            // This is most likely a bug in another implementation somewhere, cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty())) {
            printf("pong %s %s: %s, %" PRIx64" expected, %" PRIx64" received, %zu bytes\n"
                , pfrom->addr.ToString().c_str()
                , pfrom->strSubVer.c_str()
                , sProblem.c_str()
                , pfrom->nPingNonceSent
                , nonce
                , nAvail);
        }
        if (bPingFinished) {
            pfrom->nPingNonceSent = 0;
        }
    }


    else if (strCommand == "alert")
    {
        CAlert alert;
        vRecv >> alert;

        uint256 alertHash = alert.GetHash();
        if (pfrom->setKnown.count(alertHash) == 0)
        {
            if (alert.ProcessAlert())
            {
                // Relay
                pfrom->setKnown.insert(alertHash);
                {
                    LOCK(cs_vNodes);
                    for (CNode* pnode : vNodes)
                        alert.RelayTo(pnode);
                }
            }
            else {
                // Small DoS penalty so peers that send us lots of
                // duplicate/expired/invalid-signature/whatever alerts
                // eventually get banned.
                // This isn't a Misbehaving(100) (immediate ban) because the
                // peer might be an older or different implementation with
                // a different signature key, etc.
                pfrom->Misbehaving(10);
            }
        }
    }


    else
    {
        if (fSecMsgEnabled)
            SecureMsgReceiveData(pfrom, strCommand, vRecv);

        //ProcessMessageFortuna(pfrom, strCommand, vRecv);
        ProcessMessageFortunastake(pfrom, strCommand, vRecv);
        //ProcessSpork(pfrom, strCommand, vRecv);

        // Ignore unknown commands for extensibility
    }


    // Update the last seen time for this node's address
    if (pfrom->fNetworkNode)
        if (strCommand == "version" || strCommand == "addr" || strCommand == "inv" || strCommand == "getdata" || strCommand == "ping")
            AddressCurrentlyConnected(pfrom->addr);


    return true;
}

// requires LOCK(cs_vRecvMsg)
bool ProcessMessages(CNode* pfrom)
{
    //if (fDebug)
    //    printf("ProcessMessages(%zu messages)\n", pfrom->vRecvMsg.size());

    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fOk = true;

    if (!pfrom->vRecvGetData.empty())
        ProcessGetData(pfrom);

    // this maintains the order of responses
    if (!pfrom->vRecvGetData.empty()) return fOk;

    std::deque<CNetMessage>::iterator it = pfrom->vRecvMsg.begin();
    while (!pfrom->fDisconnect && it != pfrom->vRecvMsg.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        // get next message
        CNetMessage& msg = *it;

        //if (fDebug)
        //    printf("ProcessMessages(message %u msgsz, %zu bytes, complete:%s)\n",
        //            msg.hdr.nMessageSize, msg.vRecv.size(),
        //            msg.complete() ? "Y" : "N");

        // end, if an incomplete message is found
        if (!msg.complete())
            break;

        // at this point, any failure means we can delete the current message
        it++;

        // Scan for message start
        if (memcmp(msg.hdr.pchMessageStart, pchMessageStart, sizeof(pchMessageStart)) != 0) {
            printf("\n\nPROCESSMESSAGE: INVALID MESSAGESTART\n\n");
            fOk = false;
            break;
        }

        // Read header
        CMessageHeader& hdr = msg.hdr;
        if (!hdr.IsValid())
        {
            printf("\n\nPROCESSMESSAGE: ERRORS IN HEADER %s\n\n\n", hdr.GetCommand().c_str());
            continue;
        }
        string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;

        // Checksum
        CDataStream& vRecv = msg.vRecv;
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = 0;
        memcpy(&nChecksum, &hash, sizeof(nChecksum));
        if (nChecksum != hdr.nChecksum)
        {
            printf("ProcessMessages(%s, %u bytes) : CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n",
               strCommand.c_str(), nMessageSize, nChecksum, hdr.nChecksum);
            continue;
        }

        // Process message
        bool fRet = false;
        try
        {
            fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nTime);
            boost::this_thread::interruption_point();
        }
        catch (std::ios_base::failure& e)
        {
            if (strstr(e.what(), "end of data"))
            {
				if(fDebug)
					// Allow exceptions from under-length message on vRecv
					printf("ProcessMessages(%s, %u bytes) : Exception '%s' caught, normally caused by a message being shorter than its stated length\n", strCommand.c_str(), nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
				if(fDebug)
					// Allow exceptions from over-long size
					printf("ProcessMessages(%s, %u bytes) : Exception '%s' caught\n", strCommand.c_str(), nMessageSize, e.what());
            }
            else
            {
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
        }
        catch (boost::thread_interrupted) {
            throw;
        }
        catch (std::exception& e) {
            PrintExceptionContinue(&e, "ProcessMessages()");
        } catch (...) {
            PrintExceptionContinue(NULL, "ProcessMessages()");
        }

        if (!fRet)
            printf("ProcessMessage(%s, %u bytes) FAILED\n", strCommand.c_str(), nMessageSize);

        break;
    }

    // In case the connection got shut down, its receive buffer was wiped
    if (!pfrom->fDisconnect)
        pfrom->vRecvMsg.erase(pfrom->vRecvMsg.begin(), it);

    return fOk;
}


bool SendMessages(CNode* pto, bool fSendTrickle)
{
    TRY_LOCK(cs_main, lockMain);
    if (lockMain) {
        // Don't send anything until we get their version message
        if (pto->nVersion == 0)
            return true;

        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued) {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend) {
            uint64_t nonce = 0;
            while (nonce == 0) {
                RAND_bytes((unsigned char*)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION) {
                pto->nPingNonceSent = nonce;
                pto->PushMessage("ping", nonce);
            } else {
                // Peer is too old to support ping command with nonce, pong will never arrive.
                pto->nPingNonceSent = 0;
                pto->PushMessage("ping");
            }
        }


		// Start block sync
        if (pto->fStartSync && !fImporting && !fReindex) {
            pto->fStartSync = false;
            pto->PushGetBlocks(pindexBest, uint256(0));
        }


        // Resend wallet transactions that haven't gotten in a block yet
        // Except during reindex, importing and IBD, when old wallet
        // transactions become unconfirmed and spams other nodes.
        if (!fReindex && !IsInitialBlockDownload())
        {
            ResendWalletTransactions();
        }

        // Address refresh broadcast
        static int64_t nLastRebroadcast;
        if (!IsInitialBlockDownload() && (GetTime() - nLastRebroadcast > 24 * 60 * 60))
        {
            {
                LOCK(cs_vNodes);
                for (CNode* pnode : vNodes)
                {
                    // Periodically clear setAddrKnown to allow refresh broadcasts
                    if (nLastRebroadcast)
                        pnode->setAddrKnown.clear();

                    // Rebroadcast our address
                    if (!fNoListen)
                    {
                        CAddress addr = GetLocalAddress(&pnode->addr);
                        if (addr.IsRoutable())
                            pnode->PushAddress(addr);
                    }
                }
            }
            nLastRebroadcast = GetTime();
        }

        //
        // Message: addr
        //
        if (fSendTrickle)
        {
            vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            for (const CAddress& addr : pto->vAddrToSend)
            {
                // returns true if wasn't already contained in the set
                if (pto->setAddrKnown.insert(addr).second)
                {
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        pto->PushMessage("addr", vAddr);
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                pto->PushMessage("addr", vAddr);
        }

        //
        // Message: getblocks
        //

        int n = pto->getBlocksIndex.size();
        for (int i = 0; i < n; i++)
        {
            if (fDebugNet) printf("Pushing getblocks %s to %s\n\n",pto->getBlocksIndex[i]->ToString().c_str(),pto->getBlocksHash[i].ToString().c_str());
            pto->PushMessage("getblocks", CBlockLocator(pto->getBlocksIndex[i]), pto->getBlocksHash[i]);
        }
        pto->getBlocksIndex.clear();
        pto->getBlocksHash.clear();

        //
        // Message: inventory
        //
        vector<CInv> vInv;
        vector<CInv> vInvWait;
        {
            LOCK(pto->cs_inventory);
            vInv.reserve(pto->vInventoryToSend.size());
            vInvWait.reserve(pto->vInventoryToSend.size());
            for (const CInv& inv : pto->vInventoryToSend)
            {
                if (pto->setInventoryKnown.count(inv))
                    continue;

                // trickle out tx inv to protect privacy
                if (inv.type == MSG_TX && !fSendTrickle)
                {
                    // 1/4 of tx invs blast to all immediately
                    static uint256 hashSalt;
                    if (hashSalt == 0)
                        hashSalt = GetRandHash();
                    uint256 hashRand = inv.hash ^ hashSalt;
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((hashRand & 3) != 0);

                    // always trickle our own transactions
                    if (!fTrickleWait)
                    {
                        CWalletTx wtx;
                        if (GetTransaction(inv.hash, wtx))
                            if (wtx.fFromMe)
                                fTrickleWait = true;
                    }

                    if (fTrickleWait)
                    {
                        vInvWait.push_back(inv);
                        continue;
                    }
                }

                // returns true if wasn't already contained in the set
                if (pto->setInventoryKnown.insert(inv).second)
                {
                    vInv.push_back(inv);
                    if (vInv.size() >= 1000)
                    {
                        pto->PushMessage("inv", vInv);
                        vInv.clear();
                    }
                }
            }
            pto->vInventoryToSend = vInvWait;
        }
        if (!vInv.empty())
            pto->PushMessage("inv", vInv);


        //
        // Message: getdata
        //
        vector<CInv> vGetData;
        int64_t nNow = GetTime() * 1000000;
        CTxDB txdb("r");
        while (!pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
        {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            if (!AlreadyHave(txdb, inv))
            {
                if (fDebugNet)
                    printf("sending getdata: %s\n", inv.ToString().c_str());
                vGetData.push_back(inv);
                if (vGetData.size() >= 1000)
                {
                    pto->PushMessage("getdata", vGetData);
                    vGetData.clear();
                }
                mapAlreadyAskedFor[inv] = nNow;
            }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        }
        if (!vGetData.empty())
            pto->PushMessage("getdata", vGetData);

        if (fSecMsgEnabled)
            SecureMsgSendData(pto, fSendTrickle);
    }



    return true;
}

int64_t GetFortunastakePayment(int nHeight, int64_t blockValue)
{
    //int64_t ret = blockValue * 1/3; //33%
	int64_t ret = static_cast<int64_t>(blockValue * 1/3); //33%

    return ret;
}
