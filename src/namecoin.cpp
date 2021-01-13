#include <vector>
#include "namecoin.h"
#include "coincontrol.h"
#include "script.h"
#include "wallet.h"
#include "denariusrpc.h"

extern CWallet* pwalletMain;
extern std::map<uint256, CTransaction> mapTransactions;

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/xpressive/xpressive_dynamic.hpp>
#include <fstream>

using namespace std;
using namespace json_spirit;

template<typename T> void ConvertTo(Value& value, bool fAllowNull=false);

//map<vector<unsigned char>, uint256> mapMyNames;
//map<vector<unsigned char>, set<uint256> > mapNamePending; // for pending tx
map<CNameVal, set<uint256> > mapNamePending; // for pending tx

extern uint256 SignatureHash(CScript scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType);

// forward decls
extern bool Solver(const CKeyStore& keystore, const CScript& scriptPubKey, uint256 hash, int nHashType, CScript& scriptSigRet, txnouttype& whichTypeRet);
extern bool VerifyScript(const CScript& scriptSig, const CScript& scriptPubKey, const CTransaction& txTo, unsigned int nIn, unsigned int flags, int nHashType);
extern std::string _(const char* psz);

class CNamecoinHooks : public CHooks
{
public:
	virtual bool IsNameFeeEnough(const CTransaction& tx, const CAmount& txFee);
    virtual bool CheckInputs(const CTransaction& tx, const CBlockIndex* pindexBlock, vector<nameTempProxy> &vName, const CDiskTxPos& pos, const CAmount& txFee);
    virtual bool DisconnectInputs(const CTransaction& tx);
    virtual bool ConnectBlock(CBlockIndex* pindex, const vector<nameTempProxy> &vName);
    virtual bool ExtractAddress(const CScript& script, string& address);
    virtual void AddToPendingNames(const CTransaction& tx);
    virtual bool IsMine(const CTxOut& txout);
    virtual bool IsNameTx(int nVersion);
    virtual bool RemoveNameScriptPrefix(const CScript& scriptIn, CScript& scriptOut);
    virtual bool IsNameScript(CScript scr);
    virtual bool deletePendingName(const CTransaction& tx);
    virtual bool getNameValue(const string& name, string& value);
    virtual bool DumpToTextFile();
};

bool CTransaction::ReadFromTDisk(const CDiskTxPos& postx)
{
    // if (!fTxIndex)
    //     return false;

    CAutoFile file(OpenBlockFile(postx.nFile, postx.nBlockPos, "rb"), SER_DISK, CLIENT_VERSION); //0
    // CBlockHeader header;
    try {
        // file >> header;
        fseek(file, postx.nTxPos, SEEK_CUR);
        file >> *this;
    } catch (std::exception& e) {
        return error("%s() : deserialize or I/O error\n%s", __PRETTY_FUNCTION__, e.what());
    }
    return true;
}

CNameVal nameValFromValue(const Value& value) {
    string strName = value.get_str();
    unsigned char *strbeg = (unsigned char*)strName.c_str();
    return CNameVal(strbeg, strbeg + strName.size());
}

CNameVal nameValFromString(const string& str) {
    unsigned char *strbeg = (unsigned char*)str.c_str();
    return CNameVal(strbeg, strbeg + str.size());
}

string stringFromNameVal(const CNameVal& nameVal) {
    string res;
    CNameVal::const_iterator vi = nameVal.begin();
    while (vi != nameVal.end()) {
        res += (char)(*vi);
        vi++;
    }
    return res;
}

string limitString(const string& inp, unsigned int size, string message = "")
{
    if (size == -1)
        return inp;

    string ret = inp;
    if (inp.size() > size)
    {
        ret.resize(size);
        ret += message;
    }

    return ret;
}

// Calculate at which block will expire.
bool CalculateExpiresAt(CNameRecord& nameRec)
{
    if (nameRec.deleted())
    {
        nameRec.nExpiresAt = 0;
        return true;
    }

    int64_t sum = 0;
    for(unsigned int i = nameRec.nLastActiveChainIndex; i < nameRec.vtxPos.size(); i++)
    {
        CTransaction tx;
        // if (!tx.ReadFromTDisk(nameRec.vtxPos[i].txPos)) //ReadFromTDisk
        //     return error("CalculateExpiresAt() : could not read tx from disk");
        printf('found name');

        NameTxInfo nti;
        if (!DecodeNameTx(tx, nti))
            return error("CalculateExpiresAt() : %s is not namecoin tx, this should never happen", tx.GetHash().GetHex().c_str());

        sum += nti.nRentalDays * 2880; //days to blocks. 2880 is average number of blocks per day roughly (prev 175)
    }

    //limit to INT_MAX value
    sum += nameRec.vtxPos[nameRec.nLastActiveChainIndex].nHeight;
    nameRec.nExpiresAt = sum > INT_MAX ? INT_MAX : sum;

    return true;
}

// Tests if name is active. You can optionaly specify at which height it is/was active.
bool NameActive(CNameDB& dbName, const CNameVal& name, int currentBlockHeight = -1)
{
    CNameRecord nameRec;
    if (!dbName.ReadName(name, nameRec))
        return false;

    if (currentBlockHeight < 0)
        currentBlockHeight = pindexBest->nHeight;

    if (nameRec.deleted()) // last name op was name_delete
        return false;

    return currentBlockHeight <= nameRec.nExpiresAt;
}

bool NameActive(const CNameVal& name, int currentBlockHeight = -1)
{
    CNameDB dbName("r");
    return NameActive(dbName, name, currentBlockHeight);
}

// Returns minimum name operation fee rounded down to cents. Should be used during|before transaction creation.
// If you wish to calculate if fee is enough - use IsNameFeeEnough() function.
// Generaly:  GetNameOpFee() > IsNameFeeEnough().
int64_t GetNameOpFee(const CBlockIndex* pindexBlock, const int nRentalDays, int op, const CNameVal& name, const CNameVal& value)
{
    if (op == OP_NAME_DELETE)
        return MIN_NAME_FEE;

    const CBlockIndex* lastPoW = GetLastBlockIndex(pindexBlock, false);

    int64_t txMinFee = nRentalDays * lastPoW->nMint / (365 * 100); // 1% PoW per 365 days

    if (op == OP_NAME_NEW)
        txMinFee += lastPoW->nMint; // +10% PoW per operation itself

    txMinFee = sqrt(txMinFee / CENT) * CENT; // square root is taken of the number of cents.
    txMinFee += (int)((name.size() + value.size()) / 128) * CENT; // 1 cent per 128 bytes

    // Round up to CENT
    txMinFee += CENT - 1;
    txMinFee = (txMinFee / CENT) * CENT;

    // Fee should be at least MIN_TX_FEE
    txMinFee = max(txMinFee, MIN_NAME_FEE); //100000000 = 1.0 D

    return txMinFee;
}

CAmount GetNameOpFee2(const CBlockIndex* pindexBlock, const int nRentalDays, int op, const CNameVal& name, const CNameVal& value)
{
    if (op == OP_NAME_DELETE)
        return MIN_NAME_FEE;

    const CBlockIndex* lastPoW = GetLastBlockIndex(pindexBlock, false);

    CAmount txMinFee = nRentalDays * lastPoW->nMint / (365 * 100); // 1% PoW per 365 days

    if (op == OP_NAME_NEW)
        txMinFee += lastPoW->nMint; // +1% PoW per operation itself

    txMinFee = sqrt(txMinFee / CENT) * CENT; // square root is taken of the number of cents.
    txMinFee += (int)((name.size() + value.size()) / 128) * CENT; // 1 cent per 128 bytes

    // Round up to CENT
    txMinFee += CENT - 1;
    txMinFee = (txMinFee / CENT) * CENT;

    // Fee should be at least MIN_NAME_FEE
    txMinFee = max(txMinFee, MIN_NAME_FEE);

    return txMinFee;
}

// namecoin stuff

bool checkNameValues(NameTxInfo& ret)
{
    ret.err_msg = "";
    if (ret.name.size() > MAX_NAME_LENGTH)
        ret.err_msg.append("name is too long.\n");

    if (ret.value.size() > MAX_VALUE_LENGTH)
        ret.err_msg.append("value is too long.\n");

    if (ret.op == OP_NAME_NEW && ret.nRentalDays < 1)
        ret.err_msg.append("rental days must be greater than 0.\n");

    if (ret.op == OP_NAME_UPDATE && ret.nRentalDays < 0)
        ret.err_msg.append("rental days must be greater or equal 0.\n");

    if (ret.nRentalDays > MAX_RENTAL_DAYS)
        ret.err_msg.append("rental days value is too large.\n");

    if (ret.err_msg != "")
        return false;
    return true;
}

// read name script and extract name, value and rentalDays
// returns true/false is script is correct/incorrect
bool DecodeNameScript(const CScript& script, NameTxInfo& ret, CScript::const_iterator& pc)
{
    // script structure:
    // (name_new | name_update) << OP_DROP << name << days << OP_2DROP << val1 << val2 << .. << valn << OP_DROP2 << OP_DROP2 << ..<< (OP_DROP2 | OP_DROP) << paytoscripthash
    // or
    // name_delete << OP_DROP << name << OP_DROP << paytoscripthash

    // NOTE: script structure is strict - it must not contain anything else in the midle of it to be a valid name script. It can, however, contain anything else after the correct structure have been read.

    // read op
    ret.err_msg = "failed to read op";
    opcodetype opcode;
    if (!script.GetOp(pc, opcode))
        return false;
    if (opcode < OP_1 || opcode > OP_16)
        return false;
    ret.op = opcode - OP_1 + 1;

    if (ret.op != OP_NAME_NEW && ret.op != OP_NAME_UPDATE && ret.op != OP_NAME_DELETE)
        return false;

    ret.err_msg = "failed to read OP_DROP after op_type";
    if (!script.GetOp(pc, opcode))
        return false;
    if (opcode != OP_DROP)
        return false;

    vector<unsigned char> vch;

    // read name
    ret.err_msg = "failed to read name";
    if (!script.GetOp(pc, opcode, vch))
        return false;
    if ((opcode == OP_DROP || opcode == OP_2DROP) || !(opcode >= 0 && opcode <= OP_PUSHDATA4))
        return false;
    ret.name = vch;

    // if name_delete - read OP_DROP after name and exit.
    if (ret.op == OP_NAME_DELETE)
    {
        ret.err_msg = "failed to read OP2_DROP in name_delete";
        if (!script.GetOp(pc, opcode))
            return false;
        if (opcode != OP_DROP)
            return false;
        ret.err_msg = "";

        return true;
    }

    // read rental days
    ret.err_msg = "failed to read rental days";
    if (!script.GetOp(pc, opcode, vch))
        return false;
    if ((opcode == OP_DROP || opcode == OP_2DROP) || !(opcode >= 0 && opcode <= OP_PUSHDATA4))
        return false;
    ret.nRentalDays = CScriptNum(vch, false).getint();

    // read OP_2DROP after name and rentalDays
    ret.err_msg = "failed to read delimeter d in: name << rental << d << value";
    if (!script.GetOp(pc, opcode))
        return false;
    if (opcode != OP_2DROP)
        return false;

    // read value
    ret.err_msg = "failed to read value";
    int valueSize = 0;
    for (;;)
    {
        if (!script.GetOp(pc, opcode, vch))
            return false;
        if (opcode == OP_DROP || opcode == OP_2DROP)
            break;
        if (!(opcode >= 0 && opcode <= OP_PUSHDATA4))
            return false;
        ret.value.insert(ret.value.end(), vch.begin(), vch.end());
        valueSize++;
    }
    pc--;

    // read next delimiter and move the pc after it
    ret.err_msg = "failed to read correct number of DROP operations after value"; //sucess! we have read name script structure
    int delimiterSize = 0;
    while (opcode == OP_DROP || opcode == OP_2DROP)
    {
        if (!script.GetOp(pc, opcode))
            break;
        if (opcode == OP_2DROP)
            delimiterSize += 2;
        if (opcode == OP_DROP)
            delimiterSize += 1;
    }
    pc--;

    if (valueSize != delimiterSize)
        return false;


    ret.err_msg = "";     //sucess! we have read name script structure without errors!
    if (!checkNameValues(ret))
        return false;

    return true;
}

bool DecodeNameScript(const CScript& script, NameTxInfo& ret)
{
    CScript::const_iterator pc = script.begin();
    return DecodeNameScript(script, ret, pc);
}

bool RemoveNameScriptPrefix(const CScript& scriptIn, CScript& scriptOut)
{
    NameTxInfo nti;
    CScript::const_iterator pc = scriptIn.begin();

    if (!DecodeNameScript(scriptIn, nti, pc))
        return false;

    scriptOut = CScript(pc, scriptIn.end());
    return true;
}

// bool SignNameSignature(const CTransaction& txFrom, CTransaction& txTo, unsigned int nIn, int nHashType, CScript scriptPrereq=CScript())
// {
//     assert(nIn < txTo.vin.size());
//     CTxIn& txin = txTo.vin[nIn];
//     assert(txin.prevout.n < txFrom.vout.size());
//     const CTxOut& txout = txFrom.vout[txin.prevout.n];

//     // Leave out the signature from the hash, since a signature can't sign itself.
//     // The checksig op will also drop the signatures from its hash.

//     CScript scriptPubKey;
//     if (!RemoveNameScriptPrefix(txout.scriptPubKey, scriptPubKey))
//         return error("SignNameSignature(): failed to remove name script prefix");

//     uint256 hash = SignatureHash(scriptPrereq + txout.scriptPubKey, txTo, nIn, nHashType);

//     txnouttype whichType;
//     if (!Solver(*pwalletMain, scriptPubKey, hash, nHashType, txin.scriptSig, whichType))
//         return false;

//     txin.scriptSig = scriptPrereq + txin.scriptSig;

//     // Test solution
//     if (scriptPrereq.empty())
//         if (!VerifyScript(txin.scriptSig, txout.scriptPubKey, txTo, nIn, STANDARD_SCRIPT_VERIFY_FLAGS, 0))
//             return false;

//     return true;
// }

bool SignNameSignatureD(const CKeyStore& keystore, const CTransaction& txFrom, CTransaction& txTo, unsigned int nIn, int nHashType)
{
    assert(nIn < txTo.vin.size());
    CTxIn& txin = txTo.vin[nIn];
    assert(txin.prevout.n < txFrom.vout.size());
    const CTxOut& txout = txFrom.vout[txin.prevout.n];

    // Leave out the signature from the hash, since a signature can't sign itself.
    // The checksig op will also drop the signatures from its hash.

    uint256 hash = SignatureHash(txout.scriptPubKey, txTo, nIn, nHashType);

    CScript scriptPubKey;
    if (!RemoveNameScriptPrefix(txout.scriptPubKey, scriptPubKey))
        return error("SignNameSignatureD(): failed to remove name script prefix");

    txnouttype whichType;
    if (!Solver(keystore, scriptPubKey, hash, nHashType, txin.scriptSig, whichType))
        return false;

    // Test solution
    // return VerifyScript(txin.scriptSig, txout.scriptPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, MutableTransactionSignatureChecker(&txTo, nIn), NULL, txTo.nVersion == NAMECOIN_TX_VERSION);
    //!VerifyScript(txin.scriptSig, txout.scriptPubKey, txTo, nIn, STANDARD_SCRIPT_VERIFY_FLAGS, 0))
    return VerifyScript(txin.scriptSig, txout.scriptPubKey, txTo, nIn, STANDARD_SCRIPT_VERIFY_FLAGS, 0);
}


// Just like CreateTransaction, but with addition of having 1 input already addded.
bool CreateTransactionWithInputTx(const vector<pair<CScript, int64_t> >& vecSend, CWalletTx& wtxIn, int nTxOut, CWalletTx& wtxNew, CReserveKey& reservekey, int64_t& nFeeRet, string& strFailReason, int32_t& nChangePos, const CCoinControl* coinControl)
{
    int64_t nValue = 0;
    BOOST_FOREACH (const PAIRTYPE(CScript, int64_t)& s, vecSend)
    {
        if (nValue < 0)
        {
            strFailReason = _("Transaction amounts must be positive");
            return false;
        }
        nValue += s.second;
    }
    if (vecSend.empty() || nValue < 0)
    {
        strFailReason = _("Transaction amounts must be positive");
        return false;
    }

    wtxNew.BindWallet(pwalletMain);

    {
        CTxDB txdb("r");
        LOCK2(cs_main, pwalletMain->cs_wallet);
        {
            nFeeRet = nTransactionFee;
            while (true)
            {
                // wtxNew.vin.clear();
                // wtxNew.vout.clear();
                // wtxNew.fFromMe = true;

                // int64_t nTotalValue = nValue + nFeeRet;
                // double dPriority = 0;

                // // vouts to the payees
                // BOOST_FOREACH(const PAIRTYPE(CScript, int64)& s, vecSend)
                //     wtxNew.vout.push_back(CTxOut(s.second, s.first));
                wtxNew.vin.clear();
                wtxNew.vout.clear();
                wtxNew.fFromMe = true;

                int64_t nTotalValue = nValue + nFeeRet;
                double dPriority = 0;

                int64_t nWtxinCredit = wtxIn.vout[nTxOut].nValue;   

                // vouts to the payees with UTXO splitter - D E N A R I U S
                if(coinControl && !coinControl->fSplitBlock)
                {
                    BOOST_FOREACH (const PAIRTYPE(CScript, int64_t)& s, vecSend)
                    {
                        wtxNew.vout.push_back(CTxOut(s.second, s.first));
                    }
                }
                else //UTXO Splitter Transaction
                {
                    int nSplitBlock;
                    if(coinControl)
                        nSplitBlock = coinControl->nSplitBlock;
                    else
                        nSplitBlock = 1;

                    BOOST_FOREACH (const PAIRTYPE(CScript, int64_t)& s, vecSend)
                    {
                        for(int i = 0; i < nSplitBlock; i++)
                        {
                            if(i == nSplitBlock - 1)
                            {
                                uint64_t nRemainder = s.second % nSplitBlock;
                                wtxNew.vout.push_back(CTxOut((s.second / nSplitBlock) + nRemainder, s.first));
                            }
                            else
                                wtxNew.vout.push_back(CTxOut(s.second / nSplitBlock, s.first));
                        }
                    }
                }

                // Choose coins to use
                set<pair<const CWalletTx*,unsigned int> > setCoins;
                
                int64_t nValueIn = 0;

                if (nTotalValue - nWtxinCredit > 0)
                    if (!pwalletMain->SelectCoins2(nTotalValue - nWtxinCredit, wtxNew.nTime, setCoins, nValueIn, coinControl))
                        return false;
                
                vector<pair<const CWalletTx*, unsigned int> > vecCoins(setCoins.begin(), setCoins.end());

                BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins)
                {
                    //Fix priority calculation in CreateTransaction
                    //Make this projection of priority in 1 block match the
                    //calculation in the low priority reject code.
                    int64_t nCredit = pcoin.first->vout[pcoin.second].nValue;
                    //But mempool inputs might still be in the mempool, so their age stays 0
                    int age = pcoin.first->GetDepthInMainChain();
                    if (age != 0)
                        age += 1;
                    //dPriority += (double)nCredit * pcoin.first->GetDepthInMainChain();
                    dPriority += (double)nCredit * age;
                }

                // Input tx always at first position
                vecCoins.insert(vecCoins.begin(), make_pair(&wtxIn, nTxOut));

                nValueIn += nWtxinCredit;
                dPriority += (double)nWtxinCredit * wtxIn.GetDepthInMainChain();

                int64_t nChange = nValueIn - nValue - nFeeRet;
                // if sub-cent change is required, the fee must be raised to at least MIN_TX_FEE
                // or until nChange becomes zero
                // NOTE: this depends on the exact behaviour of GetMinFee
                if (nFeeRet < MIN_TX_FEE && nChange > 0 && nChange < CENT)
                {
                    int64_t nMoveToFee = min(nChange, MIN_TX_FEE - nFeeRet);
                    nChange -= nMoveToFee;
                    nFeeRet += nMoveToFee;
                }

                if (nChange > 0)
                {
                    // Fill a vout to ourself
                    // TODO: pass in scriptChange instead of reservekey so
                    // change transaction isn't always pay-to-bitcoin-address
                    CScript scriptChange;

                    // coin control: send change to custom address
                    if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange))
                        scriptChange.SetDestination(coinControl->destChange);

                    // no coin control: send change to newly generated address
                    else
                    {
                        // Note: We use a new key here to keep it from being obvious which side is the change.
                        //  The drawback is that by not reusing a previous key, the change may be lost if a
                        //  backup is restored, if the backup doesn't have the new private key for the change.
                        //  If we reused the old key, it would be possible to add code to look for and
                        //  rediscover unknown transactions that were written with keys of ours to recover
                        //  post-backup change.

                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        assert(reservekey.GetReservedKey(vchPubKey)); // should never fail, as we just unlocked

                        scriptChange.SetDestination(vchPubKey.GetID());
                    }

                    // Insert change txn at random position:
                    vector<CTxOut>::iterator position = wtxNew.vout.begin()+GetRandInt(wtxNew.vout.size() + 1);

                    // -- don't put change output between value and narration outputs
                    if (position > wtxNew.vout.begin() && position < wtxNew.vout.end())
                    {
                        while (position > wtxNew.vout.begin())
                        {
                            if (position->nValue != 0)
                                break;
                            position--;
                        };
                    };
                    wtxNew.vout.insert(position, CTxOut(nChange, scriptChange));
                    nChangePos = std::distance(wtxNew.vout.begin(), position);
                }
                else
                    reservekey.ReturnKey();

                // Fill vin
                BOOST_FOREACH(PAIRTYPE(const CWalletTx*,unsigned int)& coin, vecCoins)
                    wtxNew.vin.push_back(CTxIn(coin.first->GetHash(),coin.second));

                // Sign
                int nIn = 0;
                BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int)& coin, vecCoins)
                {
                    if (coin.first == &wtxIn && coin.second == nTxOut)
                    {
                        if (!SignNameSignatureD(*pwalletMain, *coin.first, wtxNew, nIn++))
                        {
                            strFailReason = _("Signing name input failed");
                            return false;
                        }
                    } else {
                        if (!SignSignature(*pwalletMain, *coin.first, wtxNew, nIn++))
                        {
                            strFailReason = _("Signing transaction failed");
                            return false;
                        }
                    }
                }

                // Limit size
                unsigned int nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew, SER_NETWORK, PROTOCOL_VERSION);
                if (nBytes >= MAX_BLOCK_SIZE_GEN/5)
                {
                    strFailReason = _("Transaction too large");
                    return false;
                }
                dPriority /= nBytes;

                // Check that enough fee is included
                int64_t nPayFee = nTransactionFee * (1 + (int64_t)nBytes / 1000);
                int64_t nMinFee = wtxNew.GetMinFee(1, GMF_SEND, nBytes);

                if (nFeeRet < max(nPayFee, nMinFee))
                {
                    nFeeRet = max(nPayFee, nMinFee);
                    printf("CreateTransactionWithInputTx: re-iterating (nFreeRet = %s)\n", FormatMoney(nFeeRet).c_str());
                    continue;
                }

                // Fill vtxPrev by copying from previous transactions vtxPrev
                wtxNew.AddSupportingTransactions(txdb);
                wtxNew.fTimeReceivedIsTxTime = true;

                break;
            }
        }
    }
    return true;
}

// nTxOut is the output from wtxIn that we should grab
// requires cs_main lock
// string SendMoneyWithInputTx(CScript scriptPubKey, int64_t nValue, int64_t nNetFee, CWalletTx& wtxIn, CWalletTx& wtxNew)
// {
//     if (fWalletUnlockStakingOnly)
//     {
//         string strError = _("Error: Wallet unlocked for block minting only, unable to create transaction.");
//         printf("SendMoneyWithInputTx(): %s", strError.c_str());
//         return strError;
//     }

//     int nTxOut = IndexOfNameOutput(wtxIn);
//     CReserveKey reservekey(pwalletMain);
//     int64_t nFeeRequired;
//     vector< pair<CScript, int64_t> > vecSend;
//     vecSend.push_back(make_pair(scriptPubKey, nValue));

//     if (nNetFee)
//     {
//         CScript scriptFee;
//         scriptFee << OP_RETURN;
//         vecSend.push_back(make_pair(scriptFee, nNetFee));
//     }

//     string failReason;
//     int nChangePos;
//     if (!CreateTransactionWithInputTx(vecSend, wtxIn, nTxOut, wtxNew, reservekey, nFeeRequired, failReason, nChangePos, nullptr))
//     {
//         string strError;
//         if (nValue + nFeeRequired > pwalletMain->GetBalance())
//             strError = strprintf(_("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds "), FormatMoney(nFeeRequired).c_str());
//         else
//             strError = _("Error: Transaction creation failed  ");
//         printf("SendMoneyWithInputTx(): %s", strError.c_str());
//         return strError;
//     }

//     if (!pwalletMain->CommitTransaction(wtxNew, reservekey))
//         return _("SendMoneyWithInputTx(): The transaction was rejected.  This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

//     return "";
// }

// scans denariusnames.dat and return names with their last CNameIndex
bool CNameDB::ScanNames(const CNameVal& name, unsigned int nMax,
        vector<
            pair<
                CNameVal,
                pair<CNameIndex, int>
            >
        > &nameScan)
{
    Dbc* pcursor = GetCursor();
    if (!pcursor)
        return false;

    unsigned int fFlags = DB_SET_RANGE;
    while (true)
    {
        // Read next record
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        if (fFlags == DB_SET_RANGE)
            ssKey << make_pair(string("namei"), name);
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        int ret = ReadAtCursor(pcursor, ssKey, ssValue, fFlags);
        fFlags = DB_NEXT;
        if (ret == DB_NOTFOUND)
            break;
        else if (ret != 0)
            return false;

        // Unserialize
        string strType;
        ssKey >> strType;
        if (strType == "namei")
        {
            CNameVal name2;
            ssKey >> name2;
            CNameRecord val;
            ssValue >> val;
            if (val.deleted() || val.vtxPos.empty())
                continue;
            nameScan.push_back(make_pair(name2, make_pair(val.vtxPos.back(), val.nExpiresAt)));
        }

        if (nameScan.size() >= nMax)
            break;
    }
    pcursor->close();
    return true;
}

bool CNameDB::ReadName(const CNameVal& name, CNameRecord& rec)
{
    bool ret = Read(make_pair(std::string("namei"), name), rec);
    int s = rec.vtxPos.size();

     // check if array index is out of array bounds
    if (s > 0 && rec.nLastActiveChainIndex >= s) //&& rec.nLastActiveChainIndex >= s
    {
        // delete nameindex and kill the application. nameindex should be recreated on next start
        boost::system::error_code err;
        boost::filesystem::remove(GetDataDir() / this->strFile, err);
        LogPrintf("denariusnames.dat is corrupt! It will be recreated on next start.");
        assert(rec.nLastActiveChainIndex < s);
    }
    return ret;
}

CHooks* InitHook()
{
    return new CNamecoinHooks();
}

// version for connectInputs. Used when accepting blocks.
bool IsNameFeeEnough(const CTransaction& tx, const NameTxInfo& nti, const CBlockIndex* pindexBlock, const CAmount& txFee)
{
    // scan last 10 PoW block for tx fee that matches the one specified in tx
    const CBlockIndex* lastPoW = GetLastBlockIndex(pindexBlock, false);
    //LogPrintf("IsNameFeeEnough(): pindexBlock->nHeight = %d, op = %s, nameSize = %lu, valueSize = %lu, nRentalDays = %d, txFee = %"PRI64d"\n",
    //       lastPoW->nHeight, nameFromOp(nti.op), nti.name.size(), nti.value.size(), nti.nRentalDays, txFee);
    bool txFeePass = false;
    for (int i = 1; i <= 10000; i++) //Original 10, changed to 10,000 PoW Blocks
    {
        CAmount netFee = GetNameOpFee(lastPoW, nti.nRentalDays, nti.op, nti.name, nti.value);
        //LogPrintf("                 : netFee = %"PRI64d", lastPoW->nHeight = %d\n", netFee, lastPoW->nHeight);
        if (txFee >= netFee)
        {
            txFeePass = true;
            break;
        }
        lastPoW = GetLastBlockIndex(lastPoW->pprev, false);
    }
    return txFeePass;
}

bool CNamecoinHooks::IsNameFeeEnough(const CTransaction& tx, const CAmount& txFee)
{
    if (tx.nVersion != NAMECOIN_TX_VERSION)
        return false;

    NameTxInfo nti;
    if (!DecodeNameTx(tx, nti))
        return false;

    return ::IsNameFeeEnough(tx, nti, pindexBest, txFee);
}

// bool checkNameValues(NameTxInfo& ret)
// {
//     ret.err_msg = "";
//     if (ret.name.size() > MAX_NAME_LENGTH)
//         ret.err_msg.append("name is too long.\n");

//     if (ret.value.size() > MAX_VALUE_LENGTH)
//         ret.err_msg.append("value is too long.\n");

//     if (ret.op == OP_NAME_NEW && ret.nRentalDays < 1)
//         ret.err_msg.append("rental days must be greater than 0.\n");

//     if (ret.op == OP_NAME_UPDATE && ret.nRentalDays < 0)
//         ret.err_msg.append("rental days must be greater or equal 0.\n");

//     if (ret.nRentalDays > MAX_RENTAL_DAYS)
//         ret.err_msg.append("rental days value is too large.\n");

//     if (ret.err_msg != "")
//         return false;
//     return true;
// }

//returns first name operation. I.e. name_new from chain like name_new->name_update->name_update->...->name_update
bool GetFirstTxOfName(CNameDB& dbName, const CNameVal& name, CTransaction& tx)
{
    CNameRecord nameRec;
    if (!dbName.ReadName(name, nameRec) || nameRec.vtxPos.empty())
        return false;
    CNameIndex& txPos = nameRec.vtxPos[nameRec.nLastActiveChainIndex];

    if (!tx.ReadFromTDisk(txPos.txPos))
        return error("GetFirstTxOfName() : could not read tx from disk");
    return true;
}

bool GetLastTxOfName(CNameDB& dbName,  const CNameVal& name, CTransaction& tx, CNameRecord &nameRec)
{
    if (!dbName.ReadName(name, nameRec))
        return false;
    if (nameRec.deleted() || nameRec.vtxPos.empty())
        return false;

    CNameIndex& txPos = nameRec.vtxPos.back();

    if (!tx.ReadFromTDisk(txPos.txPos))
        return error("GetLastTxOfName() : could not read tx from disk");
    return true;
}

bool GetLastTxOfName(CNameDB& dbName,  const CNameVal& name, CTransaction& tx)
{
    CNameRecord nameRec;
    return GetLastTxOfName(dbName, name, tx, nameRec);
}


Value sendtoname(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
            "sendtoname <name> <amount> [comment] [comment-to]\n"
            "<amount> is a real and is rounded to the nearest 0.01"
            + HelpRequiringPassphrase());

    if (!IsSynchronized())
        throw runtime_error("Blockchain is still downloading - wait until it is done.");

    CNameVal name = nameValFromValue(params[0]);
    int64_t nAmount = AmountFromValue(params[1]);

    // Wallet comments
    CWalletTx wtx;
    if (params.size() > 2 && params[2].type() != null_type && !params[2].get_str().empty())
        wtx.mapValue["comment"] = params[2].get_str();
    if (params.size() > 3 && params[3].type() != null_type && !params[3].get_str().empty())
        wtx.mapValue["to"]      = params[3].get_str();

    string error;
    CBitcoinAddress address;
    if (!GetNameCurrentAddress(name, address, error))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);

	std::string sNarr;
    string strError = pwalletMain->SendMoneyToDestination(address.Get(), nAmount, sNarr, wtx);
    if (strError != "")
        throw JSONRPCError(RPC_WALLET_ERROR, strError);

    Object res;
    res.push_back(Pair("SENTTO", address.ToString()));
    res.push_back(Pair("TX", wtx.GetHash().GetHex()));
    return res;
}

bool GetNameCurrentAddress(const CNameVal& name, CBitcoinAddress& address, string& error)
{
    CNameDB dbName("r");
    if (!dbName.ExistsName(name))
    {
        error = "Name not found";
        return false;
    }

    CTransaction tx;
    NameTxInfo nti;
    if (!(GetLastTxOfName(dbName, name, tx) && DecodeNameTx(tx, nti, true)))
    {
        error = "Failed to read/decode last name transaction";
        return false;
    }

    address.SetString(nti.strAddress);
    if (!address.IsValid())
    {
        error = "Name contains invalid address"; // this error should never happen, and if it does - this probably means that client blockchain database is corrupted
        return false;
    }

    if (!NameActive(dbName, name))
    {
        stringstream ss;
        ss << "This name have expired. If you still wish to send money to it's last owner you can use this command:\n"
           << "sendtoaddress " << address.ToString() << " <your_amount> ";
        error = ss.str();
        return false;
    }

    return true;
}

bool CNamecoinHooks::RemoveNameScriptPrefix(const CScript& scriptIn, CScript& scriptOut)
{
    return ::RemoveNameScriptPrefix(scriptIn, scriptOut);
}

bool CNamecoinHooks::IsMine(const CTxOut& txout)
{
    CScript scriptPubKey;
    if (!RemoveNameScriptPrefix(txout.scriptPubKey, scriptPubKey))
        return false;

    return ::IsMine(*pwalletMain, scriptPubKey);
}

Value name_list(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
                "name_list [<name>]\n"
                "list my own names"
                );

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Denarius is downloading blocks...");

    CNameVal nameUniq;
    if (params.size() == 1)
        nameUniq = nameValFromValue(params[0]);

    map<CNameVal, NameTxInfo> mapNames, mapPending;
    GetNameList(nameUniq, mapNames, mapPending);

    Array oRes;
    BOOST_FOREACH(const PAIRTYPE(CNameVal, NameTxInfo)& item, mapNames)
    {
        Object oName;
        oName.push_back(Pair("name", stringFromNameVal(item.second.name)));
        oName.push_back(Pair("value", stringFromNameVal(item.second.value)));
        if (item.second.fIsMine == false)
            oName.push_back(Pair("transferred", true));
        oName.push_back(Pair("address", item.second.strAddress));
        oName.push_back(Pair("expires_in", item.second.nExpiresAt - pindexBest->nHeight));
        if (item.second.nExpiresAt - pindexBest->nHeight <= 0)
            oName.push_back(Pair("expired", true));

        oRes.push_back(oName);
    }
    return oRes;
}

// read wallet name txs and extract: name, value, rentalDays, nOut and nExpiresAt
void GetNameList(const CNameVal& nameUniq, std::map<CNameVal, NameTxInfo> &mapNames, std::map<CNameVal, NameTxInfo> &mapPending)
{
    CNameDB dbName("r");
    LOCK2(cs_main, pwalletMain->cs_wallet);

    // add all names from wallet tx that are in blockchain
    BOOST_FOREACH(const PAIRTYPE(uint256, CWalletTx) &item, pwalletMain->mapWallet)
    {
        NameTxInfo ntiWalllet;
        if (!DecodeNameTx(item.second, ntiWalllet))
            continue;

        if (mapNames.count(ntiWalllet.name)) // already added info about this name
            continue;

        CTransaction tx;
        CNameRecord nameRec;
        if (!GetLastTxOfName(dbName, ntiWalllet.name, tx, nameRec))
            continue;

        NameTxInfo nti;
        if (!DecodeNameTx(tx, nti, true))
            continue;

        if (nameUniq.size() > 0 && nameUniq != nti.name)
            continue;

        if (!dbName.ExistsName(nti.name))
            continue;

        nti.nExpiresAt = nameRec.nExpiresAt;
        mapNames[nti.name] = nti;
    }

    // add all pending names
    BOOST_FOREACH(const PAIRTYPE(CNameVal, set<uint256>) &item, mapNamePending)
    {
        if (!item.second.size())
            continue;

        // if there is a set of pending op on a single name - select last one, by nTime
        CTransaction tx;
        uint32_t nTime = 0;
        bool found = false;
        BOOST_FOREACH(const uint256& hash, item.second)
        {
            if (!mempool.exists(hash))
                continue;
            if (mempool.mapTx[hash].nTime > nTime)
            {
                tx = mempool.mapTx[hash];
                nTime = tx.nTime;
                found = true;
            }
        }

        if (!found)
            continue;

        NameTxInfo nti;
        if (!DecodeNameTx(tx, nti, true))
            continue;

        if (nameUniq.size() > 0 && nameUniq != nti.name)
            continue;

        mapPending[nti.name] = nti;
    }
}

Value name_debug(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1)
        throw runtime_error(
            "name_debug\n"
            "Dump pending transactions id in the debug file.\n");

    LogPrintf("Pending:\n----------------------------\n");

    {
        LOCK(cs_main);
        BOOST_FOREACH(const PAIRTYPE(CNameVal, set<uint256>) &pairPending, mapNamePending)
        {
            string name = stringFromNameVal(pairPending.first);
            LogPrintf("%s :\n", name);
            uint256 hash;
            BOOST_FOREACH(hash, pairPending.second)
            {
                LogPrintf("    ");
                if (!pwalletMain->mapWallet.count(hash))
                    LogPrintf("foreign ");
                LogPrintf("    %s\n", hash.GetHex());
            }
        }
    }
    LogPrintf("----------------------------\n");
    return true;
}

Value name_show(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "name_show <name> [filepath]\n"
            "Show values of a name.\n"
            "If filepath is specified name value will be saved in that file in binary format (file will be overwritten!).\n"
            );

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Denarius is downloading blocks...");

    Object oName;
    CNameVal name = nameValFromValue(params[0]);
    string sName = stringFromNameVal(name);
    NameTxInfo nti;
    {
        LOCK(cs_main);
        CNameRecord nameRec;
        CNameDB dbName("r");
        if (!dbName.ReadName(name, nameRec))
            throw JSONRPCError(RPC_WALLET_ERROR, "failed to read from name DB");

        if (nameRec.vtxPos.size() < 1)
            throw JSONRPCError(RPC_WALLET_ERROR, "no result returned");

        CTransaction tx;
        if (!tx.ReadFromTDisk(nameRec.vtxPos.back().txPos))
            throw JSONRPCError(RPC_WALLET_ERROR, "failed to read from from disk");

        if (!DecodeNameTx(tx, nti, true))
            throw JSONRPCError(RPC_WALLET_ERROR, "failed to decode name");

        oName.push_back(Pair("name", sName));
        string value = stringFromNameVal(nti.value);
        oName.push_back(Pair("value", value));
        oName.push_back(Pair("txid", tx.GetHash().GetHex()));
        oName.push_back(Pair("address", nti.strAddress));
        oName.push_back(Pair("expires_in", nameRec.nExpiresAt - pindexBest->nHeight));
        oName.push_back(Pair("expires_at", nameRec.nExpiresAt));
        oName.push_back(Pair("time", (boost::int64_t)tx.nTime));
        if (nameRec.deleted())
            oName.push_back(Pair("deleted", true));
        else
            if (nameRec.nExpiresAt - pindexBest->nHeight <= 0)
                oName.push_back(Pair("expired", true));
    }

    if (params.size() > 1)
    {
        string filepath = params[1].get_str();
        ofstream file;
        file.open(filepath.c_str(), ios::out | ios::binary | ios::trunc);
        if (!file.is_open())
            throw JSONRPCError(RPC_PARSE_ERROR, "Failed to open file. Check if you have permission to open it.");

        file.write((const char*)&nti.value[0], nti.value.size());
        file.close();
    }

    return oName;
}

Value name_history (const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw std::runtime_error (
            "name_history \"name\" ( fullhistory )\n"
            "\nLook up the current and all past data for the given name.\n"
            "\nArguments:\n"
            "1. \"name\"           (string, required) the name to query for\n"
            "2. \"fullhistory\"    (boolean, optional) shows full history, even if name is not active\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\": \"xxxx\",            (string) transaction id"
            "    \"time\": xxxxx,               (numeric) transaction time"
            "    \"height\": xxxxx,             (numeric) height of block with this transaction"
            "    \"address\": \"xxxx\",         (string) address to which transaction was sent"
            "    \"address_is_mine\": \"xxxx\", (string) shows \"true\" if this is your address, otherwise not visible"
            "    \"operation\": \"xxxx\",       (string) name operation that was performed in this transaction"
            "    \"days_added\": xxxx,          (numeric) days added (1 day = 2880 blocks) to name expiration time, not visible if 0"
            "    \"value\": xxxx,               (numeric) name value in this transaction; not visible when name_delete was used"
            "  }\n"
            "]\n"
        );

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Denarius is downloading blocks...");

    CNameVal name = nameValFromValue(params[0]);
    bool fFullHistory = false;
    if (params.size() > 1)
        fFullHistory = params[1].get_bool();

    CNameRecord nameRec;
    {
        LOCK(cs_main);
        CNameDB dbName("r");
        if (!dbName.ReadName(name, nameRec))
            throw JSONRPCError(RPC_DATABASE_ERROR, "failed to read from name DB");
    }

    if (nameRec.vtxPos.empty())
        throw JSONRPCError(RPC_DATABASE_ERROR, "record for this name exists, but transaction list is empty");

    if (!fFullHistory && !NameActive(name))
        throw JSONRPCError(RPC_MISC_ERROR, "record for this name exists, but this name is not active");

    Array res;
    for (unsigned int i = fFullHistory ? 0 : nameRec.nLastActiveChainIndex; i < nameRec.vtxPos.size(); i++)
    {
        CTransaction tx;
        if (!tx.ReadFromTDisk(nameRec.vtxPos[i].txPos))
            throw JSONRPCError(RPC_DATABASE_ERROR, "could not read transaction from disk");

        NameTxInfo nti;
        if (!DecodeNameTx(tx, nti, true))
            throw JSONRPCError(RPC_DATABASE_ERROR, "failed to decode name transaction");

        Object obj;
        obj.push_back(Pair("txid",             tx.GetHash().ToString()));
        obj.push_back(Pair("time",             (boost::int64_t)tx.nTime));
        obj.push_back(Pair("height",           nameRec.vtxPos[i].nHeight));
        obj.push_back(Pair("address",          nti.strAddress));
      if (nti.fIsMine)
        obj.push_back(Pair("address_is_mine",  "true"));
        obj.push_back(Pair("operation",        stringFromOp(nti.op)));
      if (nti.op == OP_NAME_UPDATE || nti.op == OP_NAME_NEW)
        obj.push_back(Pair("days_added",       nti.nRentalDays));
      if (nti.op == OP_NAME_UPDATE || nti.op == OP_NAME_NEW)
        obj.push_back(Pair("value",            stringFromNameVal(nti.value)));

        res.push_back(obj);
    }

    return res;
}

Value name_mempool (const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw std::runtime_error (
            "name_mempool\n"
            "\nList pending name transactions in mempool.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"name\": \"xxxx\",            (string) name"
            "    \"txid\": \"xxxx\",            (string) transaction id"
            "    \"time\": xxxxx,               (numeric) transaction time"
            "    \"address\": \"xxxx\",         (string) address to which transaction was sent"
            "    \"address_is_mine\": \"xxxx\", (string) shows \"true\" if this is your address, otherwise not visible"
            "    \"operation\": \"xxxx\",       (string) name operation that was performed in this transaction"
            "    \"days_added\": xxxx,          (numeric) days added (1 day = 2880 blocks) to name expiration time, not visible if 0"
            "    \"value\": xxxx,               (numeric) name value in this transaction; not visible when name_delete was used"
            "  }\n"
            "]\n"
        );

    Array res;
    BOOST_FOREACH(const PAIRTYPE(CNameVal, set<uint256>) &pairPending, mapNamePending)
    {
        string sName = stringFromNameVal(pairPending.first);
        BOOST_FOREACH(const uint256& hash, pairPending.second)
        {
            if (!mempool.exists(hash))
                continue;

            CTransaction tx = mempool.mapTx[hash];
            NameTxInfo nti;
            if (!DecodeNameTx(tx, nti, true))
                throw JSONRPCError(RPC_DATABASE_ERROR, "failed to decode name transaction");

            Object obj;
            obj.push_back(Pair("name",             sName));
            obj.push_back(Pair("txid",             hash.ToString()));
            obj.push_back(Pair("time",             (boost::int64_t)tx.nTime));
            obj.push_back(Pair("address",          nti.strAddress));
          if (nti.fIsMine)
            obj.push_back(Pair("address_is_mine",  "true"));
            obj.push_back(Pair("operation",        stringFromOp(nti.op)));
          if (nti.op == OP_NAME_UPDATE || nti.op == OP_NAME_NEW)
            obj.push_back(Pair("days_added",       nti.nRentalDays));
          if (nti.op == OP_NAME_UPDATE || nti.op == OP_NAME_NEW)
            obj.push_back(Pair("value",            stringFromNameVal(nti.value)));

            res.push_back(obj);
        }
    }
    return res;
}

// used for sorting in name_filter by nHeight
bool mycompare2 (const Object &lhs, const Object &rhs)
{
    int pos = 2; //this should exactly match field name position in name_filter

    return lhs[pos].value_.get_int() < rhs[pos].value_.get_int();
}
Value name_filter(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 5)
        throw runtime_error(
                "name_filter [[[[[regexp] maxage=0] from=0] nb=0] stat]\n"
                "scan and filter names\n"
                "[regexp] : apply [regexp] on names, empty means all names\n"
                "[maxage] : look in last [maxage] blocks\n"
                "[from] : show results from number [from]\n"
                "[nb] : show [nb] results, 0 means all\n"
                "[stats] : show some stats instead of results\n"
                "name_filter \"\" 5 # list names updated in last 5 blocks\n"
                "name_filter \"^id/\" # list all names from the \"id\" namespace\n"
                "name_filter \"^id/\" 0 0 0 stat # display stats (number of names) on active names from the \"id\" namespace\n"
                );

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Denarius is downloading blocks...");

    string strRegexp;
    int nFrom = 0;
    int nNb = 0;
    int nMaxAge = 0;
    bool fStat = false;
    int nCountFrom = 0;
    int nCountNb = 0;


    if (params.size() > 0)
        strRegexp = params[0].get_str();

    if (params.size() > 1)
        nMaxAge = params[1].get_int();

    if (params.size() > 2)
        nFrom = params[2].get_int();

    if (params.size() > 3)
        nNb = params[3].get_int();

    if (params.size() > 4)
        fStat = (params[4].get_str() == "stat" ? true : false);


    CNameDB dbName("r");
    vector<Object> oRes;

    CNameVal name;
    vector<pair<CNameVal, pair<CNameIndex,int> > > nameScan;
    if (!dbName.ScanNames(name, 100000000, nameScan))
        throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

    // compile regex once
    using namespace boost::xpressive;
    smatch nameparts;
    sregex cregex = sregex::compile(strRegexp);

    pair<CNameVal, pair<CNameIndex,int> > pairScan;
    BOOST_FOREACH(pairScan, nameScan)
    {
        string name = stringFromNameVal(pairScan.first);

        // regexp
        if(strRegexp != "" && !regex_search(name, nameparts, cregex))
            continue;

        CNameIndex txName = pairScan.second.first;

        CNameRecord nameRec;
        if (!dbName.ReadName(pairScan.first, nameRec))
            continue;

        // max age
        int nHeight = nameRec.vtxPos[nameRec.nLastActiveChainIndex].nHeight;
        if(nMaxAge != 0 && pindexBest->nHeight - nHeight >= nMaxAge)
            continue;

        // from limits
        nCountFrom++;
        if(nCountFrom < nFrom + 1)
            continue;

        Object oName;
        if (!fStat) {
            oName.push_back(Pair("name", name));

            string value = stringFromNameVal(txName.value);
            oName.push_back(Pair("value", limitString(value, 300, "\n...(value too large - use name_show to see full value)")));

            oName.push_back(Pair("registered_at", nHeight)); // pos = 2 in comparison function (above name_filter)

            int nExpiresIn = nameRec.nExpiresAt - pindexBest->nHeight;
            oName.push_back(Pair("expires_in", nExpiresIn));
            if (nExpiresIn <= 0)
                oName.push_back(Pair("expired", true));
        }
        oRes.push_back(oName);

        nCountNb++;
        // nb limits
        if(nNb > 0 && nCountNb >= nNb)
            break;
    }

    Array oRes2;
    if (!fStat)
    {
        std::sort(oRes.begin(), oRes.end(), mycompare2); //sort by nHeight
        BOOST_FOREACH(const Object& res, oRes)
            oRes2.push_back(res);
    }
    else
    {
        Object oStat;
        oStat.push_back(Pair("blocks",    (int)nBestHeight));
        oStat.push_back(Pair("count",     (int)oRes2.size()));
        //oStat.push_back(Pair("sha256sum", SHA256(oRes), true));
        return oStat;
    }

    return oRes2;
}

Value name_scan(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
                "name_scan [start-name] [max-returned] [max-value-length=-1]\n"
                "Scan all names, starting at start-name and returning a maximum number of entries (default 500)\n"
                "You can also control the length of shown value (0 = full value)\n"
                );

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Denarius is downloading blocks...");

    CNameVal name;
    int nMax = 500;
    int mMaxShownValue = 0;
    if (params.size() > 0)
        name = nameValFromValue(params[0]);

    if (params.size() > 1)
        nMax = params[1].get_int();

    if (params.size() > 2)
        mMaxShownValue = params[2].get_int();

    CNameDB dbName("r");
    Array oRes;

    vector<pair<CNameVal, pair<CNameIndex,int> > > nameScan;
    if (!dbName.ScanNames(name, nMax, nameScan))
        throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

    pair<CNameVal, pair<CNameIndex,int> > pairScan;
    BOOST_FOREACH(pairScan, nameScan)
    {
        Object oName;
        string name = stringFromNameVal(pairScan.first);
        oName.push_back(Pair("name", name));

        CNameIndex txName = pairScan.second.first;
        int nExpiresAt    = pairScan.second.second;
        CNameVal value = txName.value;
        string sValue = stringFromNameVal(value);

        oName.push_back(Pair("value", limitString(sValue, mMaxShownValue, "\n...(value too large - use name_show to see full value)")));
        oName.push_back(Pair("expires_in", nExpiresAt - pindexBest->nHeight));
        if (nExpiresAt - pindexBest->nHeight <= 0)
            oName.push_back(Pair("expired", true));

        oRes.push_back(oName);
    }

    return oRes;
}

bool createNameScript(CScript& nameScript, const CNameVal& name, const CNameVal& value, int nRentalDays, int op, string& err_msg)
{
    if (op == OP_NAME_DELETE)
    {
        nameScript << op << OP_DROP << name << OP_DROP;
        return true;
    }

    NameTxInfo nti(name, value, nRentalDays, op, -1, err_msg);
    if (!checkNameValues(nti))
    {
        err_msg = nti.err_msg;
        return false;
    }

    vector<unsigned char> vchRentalDays = CScriptNum(nRentalDays).getvch();

    //add name and rental days
    nameScript << op << OP_DROP << name << vchRentalDays << OP_2DROP;

    // split value in 520 bytes chunks and add it to script
    {
        unsigned int nChunks = ceil(value.size() / 520.0);

        for (unsigned int i = 0; i < nChunks; i++)
        {   // insert data
            vector<unsigned char>::const_iterator sliceBegin = value.begin() + i*520;
            vector<unsigned char>::const_iterator sliceEnd = min(value.begin() + (i+1)*520, value.end());
            vector<unsigned char> vchSubValue(sliceBegin, sliceEnd);
            nameScript << vchSubValue;
        }

            //insert end markers
        for (unsigned int i = 0; i < nChunks / 2; i++)
            nameScript << OP_2DROP;
        if (nChunks % 2 != 0)
            nameScript << OP_DROP;
    }
    return true;
}

bool IsWalletLocked(NameTxReturn& ret)
{
    if (pwalletMain->IsLocked())
    {
        ret.err_code = RPC_WALLET_UNLOCK_NEEDED;
        ret.err_msg = "Error: Please enter the wallet passphrase with walletpassphrase first.";
        return true;
    }
    if (fWalletUnlockStakingOnly)
    {
        ret.err_code = RPC_WALLET_UNLOCK_NEEDED;
        ret.err_msg = "Error: Wallet unlocked for block staking only, unable to create transaction.";
        return true;
    }
    return false;
}

Value name_new(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 5)
        throw runtime_error(
                "name_new <name> <value> <days> [toaddress] [valueAsFilepath]\n"
                "Creates new key->value pair which expires after specified number of days.\n"
                "Cost is 0.9 D To TX Fees (Miners/Stakers) and 0.1 D To Name Registration."
                "If [valueAsFilepath] is non-zero it will interpret <value> as a filepath and try to write file contents in binary format.\n"
                + HelpRequiringPassphrase());

    CNameVal name = nameValFromValue(params[0]);
    CNameVal value = nameValFromValue(params[1]);
    int nRentalDays = params[2].get_int();
    string strAddress = "";
    if (params.size() == 4)
        strAddress = params[3].get_str();

    bool fValueAsFilepath = false;
    if (params.size() > 4)
        fValueAsFilepath = (params[4].get_int() != 0);

    NameTxReturn ret = name_operation(OP_NAME_NEW, name, value, nRentalDays, strAddress, fValueAsFilepath);
    if (!ret.ok)
        throw JSONRPCError(ret.err_code, ret.err_msg);
    return ret.hex.GetHex();
}

Value name_update(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 5)
        throw runtime_error(
                "name_update <name> <value> <days> [toaddress] [valueAsFilepath]\n"
                "Update name value, add days to expiration time and possibly transfer a name to diffrent address.\n"
                "If [valueAsFilepath] is non-zero it will interpret <value> as a filepath and try to write file contents in binary format.\n"
                + HelpRequiringPassphrase());

    CNameVal name = nameValFromValue(params[0]);
    CNameVal value = nameValFromValue(params[1]);
    int nRentalDays = params[2].get_int();
    string strAddress = "";
    if (params.size() == 4)
        strAddress = params[3].get_str();

    bool fValueAsFilepath = false;
    if (params.size() > 4)
        fValueAsFilepath = (params[4].get_int() != 0);

    NameTxReturn ret = name_operation(OP_NAME_UPDATE, name, value, nRentalDays, strAddress, fValueAsFilepath);
    if (!ret.ok)
        throw JSONRPCError(ret.err_code, ret.err_msg);
    return ret.hex.GetHex();
}

Value name_delete(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "name_delete <name>\nDelete a name if you own it. Others may do name_new after this command."
                + HelpRequiringPassphrase());

    if (!IsSynchronized())
        throw runtime_error("Denarius is still downloading - wait until it is done.");

    CNameVal name = nameValFromValue(params[0]);

    NameTxReturn ret = name_operation(OP_NAME_DELETE, name, CNameVal(), 0, "");
    if (!ret.ok)
        throw JSONRPCError(ret.err_code, ret.err_msg);
    return ret.hex.GetHex();

}

NameTxReturn name_operation(const int op, const CNameVal& name, CNameVal value, const int nRentalDays, const string strAddress, bool fValueAsFilepath)
{
    NameTxReturn ret;
    ret.err_code = RPC_INTERNAL_ERROR; // default value in case of abnormal exit
    ret.err_msg = "unkown error";
    ret.ok = false;

    if ((op == OP_NAME_NEW || op == OP_NAME_UPDATE) && value.empty())
    {
        ret.err_msg = "value must not be empty";
        return ret;
    }

    // currently supports only new, update and delete operations.
    if (op != OP_NAME_NEW && op != OP_NAME_UPDATE && op != OP_NAME_DELETE)
    {
        ret.err_msg = "illegal name op";
        return ret;
    }

    if (fValueAsFilepath)
    {
        string filepath = stringFromNameVal(value);
        std::ifstream ifs;
        ifs.open(filepath.c_str(), std::ios::binary | std::ios::ate);
        if (!ifs)
        {
            ret.err_msg = "failed to open file";
            return ret;
        }
        std::streampos fileSize = ifs.tellg();
        if (fileSize > MAX_VALUE_LENGTH)
        {
            ret.err_msg = "file is larger than maximum allowed size";
            return ret;
        }

        ifs.clear();
        ifs.seekg(0, std::ios::beg);

        value.resize(fileSize);
        if (!ifs.read(reinterpret_cast<char*>(&value[0]), fileSize))
        {
            ret.err_msg = "failed to read file";
            return ret;
        }
    }

    if (IsInitialBlockDownload())
    {
        ret.err_code = RPC_CLIENT_IN_INITIAL_DOWNLOAD;
        ret.err_msg = "Denarius is downloading blocks...";
        return ret;
    }

    if (IsWalletLocked(ret))
        return ret;

    CWalletTx wtx;
    wtx.nVersion = NAMECOIN_TX_VERSION;

    stringstream ss;
    CScript scriptPubKey;

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        // wait until other name operation on this name are completed
        if (mapNamePending.count(name) && mapNamePending[name].size())
        {
            ss << "there are " << mapNamePending[name].size() <<
                  " pending operations on that name, including " << mapNamePending[name].begin()->GetHex();
            ret.err_msg = ss.str();
            return ret;
        }

        // check if op can be aplied to name remaining time
        if (NameActive(name))
        {
            if (op == OP_NAME_NEW)
            {
                ret.err_msg = "name_new on an unexpired name";
                return ret;
            }
        }
        else
        {
            if (op == OP_NAME_UPDATE || op == OP_NAME_DELETE)
            {
                ret.err_msg = stringFromOp(op) + " on an unexpired name";
                return ret;
            }
        }

    // grab last tx in name chain and check if it can be spent by us
        CWalletTx wtxIn = CWalletTx();
        if (op == OP_NAME_UPDATE || op == OP_NAME_DELETE)
        {
            CNameDB dbName("r");
            CTransaction prevTx;
            if (!GetLastTxOfName(dbName, name, prevTx))
            {
                ret.err_msg = "could not find tx with this name";
                return ret;
            }

            uint256 wtxInHash = prevTx.GetHash();
            if (!pwalletMain->mapWallet.count(wtxInHash))
            {
                ret.err_msg = "this name tx is not in your wallet: " + wtxInHash.GetHex();
                return ret;
            }

            wtxIn = pwalletMain->mapWallet[wtxInHash];
            int nTxOut = IndexOfNameOutput(wtxIn);

            // if (::IsMine(*pwalletMain, wtxIn.vout[nTxOut].scriptPubKey) != MINE_SPENDABLE)
            // {
            //     ret.err_msg = "this name tx is not yours or is not spendable: " + wtxInHash.GetHex();
            //     return ret;
            // }
        }

    // create namescript
        CScript nameScript;
        string prevMsg = ret.err_msg;
        if (!createNameScript(nameScript, name, value, nRentalDays, op, ret.err_msg))
        {
            if (prevMsg == ret.err_msg)  // in case error message not changed, but error still occurred
                ret.err_msg = "failed to create name script";
            return ret;
        }

    // add destination to namescript
        if ((op == OP_NAME_UPDATE || op == OP_NAME_NEW) && strAddress != "")
        {
            CBitcoinAddress address(strAddress);
            if (!address.IsValid())
            {
                ret.err_code = RPC_INVALID_ADDRESS_OR_KEY;
                ret.err_msg = "Denarius address is invalid";
                return ret;
            }
            scriptPubKey = GetScriptForDestination(address.Get());
        }
        else
        {
            CPubKey vchPubKey;
            if(!pwalletMain->GetKeyFromPool(vchPubKey))
            {
                ret.err_msg = "failed to get key from pool";
                return ret;
            }
            scriptPubKey = GetScriptForDestination(vchPubKey.GetID());
        }
        nameScript += scriptPubKey;

    // verify namescript
        NameTxInfo nti;
        if (!DecodeNameScript(nameScript, nti))
        {
            ret.err_msg = nti.err_msg;
            return ret;
        }

    // set fee and send!
        CAmount nameFee = GetNameOpFee(pindexBest, nRentalDays, op, name, value);
        SendName(nameScript, MIN_TXOUT_AMOUNT, wtx, wtxIn, nameFee);
    }

    //success! collect info and return
    CTxDestination address;
    if (ExtractDestination(scriptPubKey, address))
    {
        ret.address = CBitcoinAddress(address).ToString();
    }
    ret.hex = wtx.GetHash();
    ret.ok = true;
    return ret;
}

// void createNameIndexFile()
// {
//     printf("Scanning Denarius's chain for names to create a fast index...\n");

//     // create empty denariusnames.dat
//     CNameDB dbName("cr+");

//     // scan blockchain to fill denariusnames.dat
//     CTxDB txdb("r");

//     vector<nameTempProxy> vName;

//     CBlockIndex* pindex = pindexGenesisBlock;
//     {
//         LOCK(cs_main);
//         while (pindex)
//         {
//             hooks->ConnectBlock(pindex, vName);
//             pindex = pindex->pnext;
//         }
//     }
//     printf("Scanned Denarius for names successfully!\n");
// }

bool createNameIndexFile()
{
    LogPrintf("Scanning Denarius blockchain for names to create a fast index...\n");
    CNameDB dbName("cr+");

    // if (!fTxIndex)
    //     return error("createNameIndexFile() : transaction index not available");

    const CBlockIndex *BlockReading = pindexBest;
    int blocksFound = 0;
    int nHeight = 0;	

    //int maxHeight = pindexBest->nHeight;
    //for (int nHeight=0; nHeight<=maxHeight; nHeight++)
    for (int b = 0; BlockReading && BlockReading->nHeight > RELEASE_HEIGHT; b++) //1 originally scan all, now scan from name release height
    {
        //CBlockIndex* pindex = nHeight;    
        printf("createNameIndex() %d\n", b);
        CBlock block;

        nHeight = BlockReading->nHeight;
            
        if(!block.ReadFromDisk(BlockReading, true)) {
            continue;
        }

        // collect name tx from block
        vector<nameTempProxy> vName;
        //CDiskTxPos pos(pindex->nFile, pindex->nBlockPos, nTxPos);
        CDiskTxPos pos(BlockReading->nFile, BlockReading->nBlockPos, GetSizeOfCompactSize(block.vtx.size()));
        //CDiskTxPos pos(pindex->nBlockPos, GetSizeOfCompactSize(block.vtx.size())); // start position
        for (unsigned int i=0; i<block.vtx.size(); i++)
        {
            const CTransaction& tx = block.vtx[i];
            if (tx.IsCoinStake() || tx.IsCoinBase())
            {
                pos.nTxPos += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);  // set next tx position
                continue;
            }

            // calculate tx fee
            CAmount input = 0;
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
            {
                CTransaction txPrev;
                uint256 hashBlock = 0;
                if (!GetTransaction(txin.prevout.hash, txPrev, hashBlock))
                    return error("createNameIndexFile() : prev transaction not found");

                input += txPrev.vout[txin.prevout.n].nValue;
            }
            CAmount fee = input - tx.GetValueOut();

            hooks->CheckInputs(tx, BlockReading, vName, pos, fee);                    // collect valid name tx to vName
            pos.nTxPos += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);  // set next tx position
        }

        // execute name operations, if any
        hooks->ConnectBlock(BlockReading, vName);

        if (BlockReading->pprev == NULL) { 
            assert(BlockReading); break; 
        }

        BlockReading = BlockReading->pprev;

        printf("nameindex finished at %d\n", nHeight);
    }
    return true;
}

// Check that the last entry in name history matches the given tx pos
bool CheckNameTxPos(const vector<CNameIndex> &vtxPos, const CDiskTxPos& txPos)
{
    if (vtxPos.empty())
        return false;

    return vtxPos.back().txPos == txPos;
}

// read name tx and extract: name, value and rentalDays
// optionaly it can extract destination address and check if tx is mine (note: it does not check if address is valid)
bool DecodeNameTx(const CTransaction& tx, NameTxInfo& nti, bool checkAddressAndIfIsMine /* = false */)
{
    if (tx.nVersion != NAMECOIN_TX_VERSION)
        return false;

    bool found = false;
    CScript::const_iterator pc;
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& out = tx.vout[i];
        NameTxInfo ntiTmp;
        pc = out.scriptPubKey.begin();
        if (DecodeNameScript(out.scriptPubKey, ntiTmp, pc))
        {
            // If more than one name op, fail
            if (found)
                return false;

            nti = ntiTmp;
            nti.nOut = i;

            if (checkAddressAndIfIsMine)
            {
                //read address
                CTxDestination address;
                CScript scriptPubKey(pc, out.scriptPubKey.end());
                if (!ExtractDestination(scriptPubKey, address))
                    nti.strAddress = "";
                nti.strAddress = CBitcoinAddress(address).ToString();

                // check if this is mine destination
                nti.fIsMine = IsMine(*pwalletMain, address) == ISMINE_SPENDABLE;
            }

            found = true;
        }
    }

    if (found) nti.err_msg = "";
    return found;
}

int IndexOfNameOutput(const CTransaction& tx)
{
    NameTxInfo nti;
    bool good = DecodeNameTx(tx, nti);

    if (!good)
        throw runtime_error("IndexOfNameOutput() : name output not found");
    return nti.nOut;
}

void CNamecoinHooks::AddToPendingNames(const CTransaction& tx)
{
    if (tx.nVersion != NAMECOIN_TX_VERSION)
        return;

    // CCoins coins;
    // if (pcoinsTip->GetCoins(tx.GetHash(), coins)) // try to ignore coins that are in blockchain
    //     return;

    if (tx.vout.size() < 1)
    {
        error("AddToPendingNames() : no output in tx %s\n", tx.ToString().c_str());
        return;
    }

    NameTxInfo nti;
    if (!DecodeNameTx(tx, nti))
    {
        error("AddToPendingNames() : could not decode name script in tx %s", tx.ToString().c_str());
        return;
    }

    //LOCK(cs_main);
    mapNamePending[nti.name].insert(tx.GetHash());
    printf("AddToPendingNames(): added %s %s from tx %s", stringFromOp(nti.op).c_str(), stringFromNameVal(nti.name).c_str(), tx.ToString().c_str());
}

// Checks name tx and save name data to vName if valid
// returns true if: (tx is valid name tx) OR (tx is not a name tx)
// returns false if tx is invalid name tx
bool CNamecoinHooks::CheckInputs(const CTransaction& tx, const CBlockIndex* pindexBlock, vector<nameTempProxy> &vName, const CDiskTxPos& pos, const CAmount& txFee)
{
    if (tx.nVersion != NAMECOIN_TX_VERSION)
        return true;

    //read name tx
    NameTxInfo nti;
    if (!DecodeNameTx(tx, nti))
    {
        if (pindexBlock->nHeight > RELEASE_HEIGHT)
            return error("CheckInputsHook() : could not decode namecoin tx %s in block %d", tx.GetHash().GetHex().c_str(), pindexBlock->nHeight);
        return false;
    }

    CNameVal name = nti.name;
    string sName = stringFromNameVal(name);
    string info = "name=" + sName + ", value=" + stringFromNameVal(nti.value) + ", tx=" + tx.GetHash().GetHex() +
        ", block=" + boost::lexical_cast<string>(pindexBlock->nHeight) + " \n";

    //check if last known tx on this name matches any of inputs of this tx
    CNameDB dbName("r");
    CNameRecord nameRec;
    if (dbName.ExistsName(name) && !dbName.ReadName(name, nameRec))
        return error("CheckInputsHook() : failed to read from name DB for %s", info.c_str());

    bool found = false;
    NameTxInfo prev_nti;
    if (!nameRec.vtxPos.empty() && !nameRec.deleted())
    {
        CTransaction lastKnownNameTx;
        if (!lastKnownNameTx.ReadFromTDisk(nameRec.vtxPos.back().txPos))
            return error("CheckInputsHook() : failed to read from name DB for %s", info.c_str());
        uint256 lasthash = lastKnownNameTx.GetHash();
        if (!DecodeNameTx(lastKnownNameTx, prev_nti))
            return error("CheckInputsHook() : Failed to decode existing previous name tx for %s. Your blockchain or denariusnames.dat may be corrupt.", info.c_str());

        for (unsigned int i = 0; i < tx.vin.size(); i++) //this scans all scripts of tx.vin
        {
            if (tx.vin[i].prevout.hash != lasthash)
                continue;
            found = true;
            break;
        }
    }

    switch (nti.op)
    {
        case OP_NAME_NEW:
        {
            //scan last 10 PoW block for tx fee that matches the one specified in tx
            if (!::IsNameFeeEnough(tx, nti, pindexBlock, txFee))
            {
                if (pindexBlock->nHeight > RELEASE_HEIGHT)
                    return error("CheckInputsHook() : rejected name_new because not enough fee for %s", info.c_str());
                return false;
            }

            if (NameActive(dbName, name, pindexBlock->nHeight))
            {
                if (pindexBlock->nHeight > RELEASE_HEIGHT)
                    return error("CheckInputsHook() : name_new on an unexpired name for %s", info.c_str());
                return false;
            }
            break;
        }
        case OP_NAME_UPDATE:
        {
            //scan last 10 PoW block for tx fee that matches the one specified in tx
            if (!::IsNameFeeEnough(tx, nti, pindexBlock, txFee))
            {
                if (pindexBlock->nHeight > RELEASE_HEIGHT)
                    return error("CheckInputsHook() : rejected name_update because not enough fee for %s", info.c_str());
                return false;
            }

            if (!found || (prev_nti.op != OP_NAME_NEW && prev_nti.op != OP_NAME_UPDATE))
                return error("name_update without previous new or update tx for %s", info.c_str());

            if (prev_nti.name != name)
                return error("CheckInputsHook() : name_update name mismatch for %s", info.c_str());

            if (!NameActive(dbName, name, pindexBlock->nHeight))
                return error("CheckInputsHook() : name_update on an expired name for %s", info.c_str());
            break;
        }
        case OP_NAME_DELETE:
        {
            if (!found || (prev_nti.op != OP_NAME_NEW && prev_nti.op != OP_NAME_UPDATE))
                return error("name_delete without previous new or update tx, for %s", info.c_str());

            if (prev_nti.name != name)
                return error("CheckInputsHook() : name_delete name mismatch for %s", info.c_str());

            if (!NameActive(dbName, name, pindexBlock->nHeight))
                return error("CheckInputsHook() : name_delete on expired name for %s", info.c_str());
            break;
        }
        default:
            return error("CheckInputsHook() : unknown name operation for %s", info.c_str());
    }

    // all checks passed - record tx information to vName. It will be sorted by nTime and writen to denariusnames.dat at the end of ConnectBlock
    CNameIndex txPos2;
    txPos2.nHeight = pindexBlock->nHeight;
    txPos2.value = nti.value;
    txPos2.txPos = pos;

    nameTempProxy tmp;
    tmp.nTime = tx.nTime;
    tmp.name = name;
    tmp.op = nti.op;
    tmp.hash = tx.GetHash();
    tmp.ind = txPos2;

    vName.push_back(tmp);
    return true;
}

bool CNamecoinHooks::DisconnectInputs(const CTransaction& tx)
{
    if (tx.nVersion != NAMECOIN_TX_VERSION)
        return true;

    NameTxInfo nti;
    if (!DecodeNameTx(tx, nti))
        return error("DisconnectInputsHook() : could not decode Denarius name tx");

    {
        CNameDB dbName("cr+");
        dbName.TxnBegin();

        CNameRecord nameRec;
        if (!dbName.ReadName(nti.name, nameRec))
            return error("DisconnectInputsHook() : failed to read from name DB");

        // vtxPos might be empty if we pruned expired transactions.  However, it should normally still not
        // be empty, since a reorg cannot go that far back.  Be safe anyway and do not try to pop if empty.
        if (nameRec.vtxPos.size() > 0)
        {
            // check if tx matches last tx in denariusnames.dat
            CTransaction lastTx;
            lastTx.ReadFromTDisk(nameRec.vtxPos.back().txPos);
            try {
                assert(lastTx.GetHash() == tx.GetHash());
            } catch (std::exception &e) {
                return error("Cannot assert lastTx.GetHash() DisconnectInputs() Name");
            }

            // remove tx
            nameRec.vtxPos.pop_back();

            if (nameRec.vtxPos.size() == 0) // delete empty record
                return dbName.EraseName(nti.name);

            // if we have deleted name_new - recalculate Last Active Chain Index
            if (nti.op == OP_NAME_NEW)
                for (int i = nameRec.vtxPos.size() - 1; i >= 0; i--)
                    if (nameRec.vtxPos[i].op == OP_NAME_NEW)
                    {
                        nameRec.nLastActiveChainIndex = i;
                        break;
                    }
        }
        else
            return dbName.EraseName(nti.name); // delete empty record

        if (!CalculateExpiresAt(nameRec))
            return error("DisconnectInputsHook() : failed to calculate expiration time before writing to name DB");
        if (!dbName.WriteName(nti.name, nameRec))
            return error("DisconnectInputsHook() : failed to write to name DB");

        dbName.TxnCommit();
    }

    return true;
}

string nameFromOp(int op)
{
    switch (op)
    {
        case OP_NAME_UPDATE:
            return "name_update";
        case OP_NAME_NEW:
            return "name_new";
        case OP_NAME_DELETE:
            return "name_delete";
        default:
            return "<unknown name op>";
    }
}

string stringFromOp(int op)
{
    switch (op)
    {
        case OP_NAME_UPDATE:
            return "name_update";
        case OP_NAME_NEW:
            return "name_new";
        case OP_NAME_DELETE:
            return "name_delete";
        default:
            return "<unknown name op>";
    }
}

bool CNamecoinHooks::ExtractAddress(const CScript& script, string& address)
{
    NameTxInfo nti;
    if (!DecodeNameScript(script, nti))
        return false;

    string strOp = stringFromOp(nti.op);
    address = strOp + ": " + stringFromNameVal(nti.name);
    return true;
}

// Executes name operations in vName and writes result to denariusnames.dat.
// NOTE: the block should already be written to blockchain by now - otherwise this may fail.
bool CNamecoinHooks::ConnectBlock(CBlockIndex* pindex, const vector<nameTempProxy> &vName)
{
    if (vName.empty())
        return true;

    // All of these name ops should succed. If there is an error - denariusnames.dat is probably corrupt.
    CNameDB dbName("r+");
    set<CNameVal> sNameNew;

    BOOST_FOREACH(const nameTempProxy& i, vName)
    {
        CNameRecord nameRec;
        if (dbName.ExistsName(i.name) && !dbName.ReadName(i.name, nameRec))
            return error("ConnectBlockHook() : failed to read from name DB");

        dbName.TxnBegin();

        // only first name_new for same name in same block will get written
        if  (i.op == OP_NAME_NEW && sNameNew.count(i.name))
            continue;

        nameRec.vtxPos.push_back(i.ind); // add

        // if starting new chain - save position of where it starts
        if (i.op == OP_NAME_NEW)
            nameRec.nLastActiveChainIndex = nameRec.vtxPos.size()-1;

        // limit to 1000 tx per name or a full single chain - whichever is larger
        static size_t maxSize = 0;
        if(maxSize == 0)
            GetArg("-nameindexchainsize", NAMEINDEX_CHAIN_SIZE);

        if (nameRec.vtxPos.size() > maxSize &&
            nameRec.vtxPos.size() - nameRec.nLastActiveChainIndex + 1 <= maxSize)
        {
            int d = nameRec.vtxPos.size() - maxSize; // number of elements to delete
            nameRec.vtxPos.erase(nameRec.vtxPos.begin(), nameRec.vtxPos.begin() + d);
            nameRec.nLastActiveChainIndex -= d; // move last index backwards by d elements
            assert(nameRec.nLastActiveChainIndex >= 0);
        }

        // save name op
        nameRec.vtxPos.back().op = i.op;

        if (!CalculateExpiresAt(nameRec))
            return error("ConnectBlockHook() : failed to calculate expiration time before writing to name DB for %s", i.hash.GetHex());
        if (!dbName.WriteName(i.name, nameRec))
            return error("ConnectBlockHook() : failed to write to name DB");
        if  (i.op == OP_NAME_NEW)
            sNameNew.insert(i.name);
        LogPrintf("ConnectBlockHook(): writing %s %s in block %d to denariusnames.dat\n", stringFromOp(i.op), stringFromNameVal(i.name), pindex->nHeight);

        {
            // remove from pending names list
            LOCK(cs_main);
            map<CNameVal, set<uint256> >::iterator mi = mapNamePending.find(i.name);
            if (mi != mapNamePending.end())
            {
                mi->second.erase(i.hash);
                if (mi->second.empty())
                    mapNamePending.erase(i.name);
            }
        }
        if (!dbName.TxnCommit())
            return error("ConnectBlockHook(): failed to write %s to name DB", stringFromNameVal(i.name));
    }

    return true;
}

bool CNamecoinHooks::IsNameTx(int nVersion)
{
    return nVersion == NAMECOIN_TX_VERSION;
}

bool CNamecoinHooks::IsNameScript(CScript scr)
{
    NameTxInfo nti;
    return DecodeNameScript(scr, nti);
}

bool CNamecoinHooks::deletePendingName(const CTransaction& tx)
{
    NameTxInfo nti;
    if (DecodeNameTx(tx, nti, true) && mapNamePending.count(nti.name))
    {
        mapNamePending[nti.name].erase(tx.GetHash());
        if (mapNamePending[nti.name].empty())
            mapNamePending.erase(nti.name);
        return true;
    }
    else
    {
        return false;
    }
}

bool CNamecoinHooks::getNameValue(const string& sName, string& sValue)
{
    CNameVal name = nameValFromString(sName);
    CNameDB dbName("r");
    if (!dbName.ExistsName(name))
        return false;

    CTransaction tx;
    NameTxInfo nti;
    if (!(GetLastTxOfName(dbName, name, tx) && DecodeNameTx(tx, nti, true)))
        return false;

    if (!NameActive(dbName, name))
        return false;

    sValue = stringFromNameVal(nti.value);

    return true;
}

bool GetNameValue(const CNameVal& name, CNameVal& value)
{
    CNameDB dbName("r");
    CNameRecord nameRec;

    if (!NameActive(name))
        return false;
    if (!dbName.ReadName(name, nameRec))
        return false;
    if (nameRec.vtxPos.empty())
        return false;

    value = nameRec.vtxPos.back().value;
    return true;
}

bool CNamecoinHooks::DumpToTextFile()
{
    CNameDB dbName("r");
    return dbName.DumpToTextFile();
}


bool CNameDB::DumpToTextFile()
{
    ofstream myfile ("name_dump.txt");
    if (!myfile.is_open())
        return false;

    Dbc* pcursor = GetCursor();
    if (!pcursor)
        return false;

    CNameVal name;
    unsigned int fFlags = DB_SET_RANGE;
    while (true)
    {
        // Read next record
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        if (fFlags == DB_SET_RANGE)
            ssKey << make_pair(string("namei"), name);
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        int ret = ReadAtCursor(pcursor, ssKey, ssValue, fFlags);
        fFlags = DB_NEXT;
        if (ret == DB_NOTFOUND)
            break;
        else if (ret != 0)
            return false;

        // Unserialize
        string strType;
        ssKey >> strType;
        if (strType == "namei")
        {
            CNameVal name2;
            ssKey >> name2;
            CNameRecord val;
            ssValue >> val;
            if (val.vtxPos.empty())
                continue;

            myfile << "name =  " << stringFromNameVal(name2) << "\n";
            myfile << "nExpiresAt " << val.nExpiresAt << "\n";
            myfile << "nLastActiveChainIndex " << val.nLastActiveChainIndex << "\n";
            myfile << "vtxPos:\n";
            for (unsigned int i = 0; i < val.vtxPos.size(); i++)
            {
                myfile << "    nHeight = " << val.vtxPos[i].nHeight << "\n";
                myfile << "    op = " << val.vtxPos[i].op << "\n";
                myfile << "    value = " << stringFromNameVal(val.vtxPos[i].value) << "\n";
            }
            myfile << "\n\n";
        }
    }
    pcursor->close();
    myfile.close();
    return true;
}