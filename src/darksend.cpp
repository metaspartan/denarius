// Copyright (c) 2017 The Denarius developers
// Copyright (c) 2009-2012 The Darkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "darksend.h"
#include "main.h"
#include "init.h"
#include "util.h"
#include "masternode.h"
#include "ui_interface.h"

#include <openssl/rand.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <boost/assign/list_of.hpp>

using namespace std;
using namespace boost;

CCriticalSection cs_darksend;

/** The main object for accessing darksend */
CDarkSendPool darkSendPool;
/** A helper object for signing messages from masternodes */
CDarkSendSigner darkSendSigner;
/** The current darksends in progress on the network */
std::vector<CDarksendQueue> vecDarksendQueue;
/** Keep track of the used masternodes */
std::vector<CTxIn> vecMasternodesUsed;
// keep track of the scanning errors I've seen
map<uint256, CDarksendBroadcastTx> mapDarksendBroadcastTxes;
//
CActiveMasternode activeMasternode;

// count peers we've requested the list from
int RequestedMasterNodeList = 0;

/* *** BEGIN DARKSEND MAGIC  **********
    Copyright 2014, Darkcoin Developers
        eduffield - evan@darkcoin.io
*/

int randomizeList (int i) { return std::rand()%i;}

// Recursively determine the rounds of a given input (How deep is the darksend chain for a given input)
int GetInputDarksendRounds(CTxIn in, int rounds)
{
    if(rounds >= 17) return rounds;

    std::string padding = "";
    padding.insert(0, ((rounds+1)*5)+3, ' ');

    CWalletTx tx;
    if(pwalletMain->GetTransaction(in.prevout.hash,tx))
    {
        // bounds check
        if(in.prevout.n >= tx.vout.size()) return -4;

        if(tx.vout[in.prevout.n].nValue == DARKSEND_FEE) return -3;

        //make sure the final output is non-denominate
        if(rounds == 0 && !pwalletMain->IsDenominatedAmount(tx.vout[in.prevout.n].nValue)) return -2; //NOT DENOM

        bool found = false;
        BOOST_FOREACH(CTxOut out, tx.vout)
        {
            found = pwalletMain->IsDenominatedAmount(out.nValue);
            if(found) break; // no need to loop more
        }
        if(!found) return rounds - 1; //NOT FOUND, "-1" because of the pre-mixing creation of denominated amounts

        // find my vin and look that up
        BOOST_FOREACH(CTxIn in2, tx.vin)
        {
            if(pwalletMain->IsMine(in2))
            {
                //printf("rounds :: %s %s %d NEXT\n", padding.c_str(), in.ToString().c_str(), rounds);
                int n = GetInputDarksendRounds(in2, rounds+1);
                if(n != -3) return n;
            }
        }
    }

    return rounds-1;
}

void CDarkSendPool::Reset(){
    cachedLastSuccess = 0;
    vecMasternodesUsed.clear();
    UnlockCoins();
    SetNull();
}

void CDarkSendPool::SetNull(bool clearEverything){
    finalTransaction.vin.clear();
    finalTransaction.vout.clear();

    entries.clear();

    state = POOL_STATUS_ACCEPTING_ENTRIES;

    lastTimeChanged = GetTimeMillis();

    entriesCount = 0;
    lastEntryAccepted = 0;
    countEntriesAccepted = 0;
    lastNewBlock = 0;

    sessionUsers = 0;
    sessionDenom = 0;
    sessionFoundMasternode = false;
    vecSessionCollateral.clear();
    txCollateral = CTransaction();

    if(clearEverything){
        myEntries.clear();

        if(fMasterNode){
            sessionID = 1 + (rand() % 999999);
        } else {
            sessionID = 0;
        }
    }

    // -- seed random number generator (used for ordering output lists)
    unsigned int seed = 0;
    GetRandBytes((unsigned char*)&seed, sizeof(seed));
    std::srand(seed);
}

bool CDarkSendPool::SetCollateralAddress(std::string strAddress){
    CBitcoinAddress address;
    if (!address.SetString(strAddress))
    {
        printf("CDarkSendPool::SetCollateralAddress - Invalid DarkSend collateral address\n");
        return false;
    }
    collateralPubKey= GetScriptForDestination(address.Get());
    return true;
}

//
// Unlock coins after Darksend fails or succeeds
//
void CDarkSendPool::UnlockCoins(){
    BOOST_FOREACH(CTxIn v, lockedCoins)
        pwalletMain->UnlockCoin(v.prevout);

    lockedCoins.clear();
}

//
// Check for various timeouts (queue objects, darksend, etc)
//
void CDarkSendPool::CheckTimeout(){
    if(!fMasterNode) return;

    // catching hanging sessions
    if(!fMasterNode) {
        if(state == POOL_STATUS_TRANSMISSION) {
            if(fDebug) printf("CDarkSendPool::CheckTimeout() -- Session complete -- Running Check()\n");
            // Check();
        }
    }

    // check darksend queue objects for timeouts
    int c = 0;
    vector<CDarksendQueue>::iterator it;
    for(it=vecDarksendQueue.begin();it<vecDarksendQueue.end();it++){
        if((*it).IsExpired()){
            if(fDebug) printf("CDarkSendPool::CheckTimeout() : Removing expired queue entry - %d\n", c);
            vecDarksendQueue.erase(it);
            break;
        }
        c++;
    }

    /* Check to see if we're ready for submissions from clients */
    if(state == POOL_STATUS_QUEUE && sessionUsers == GetMaxPoolTransactions()) {
        CDarksendQueue dsq;
        dsq.nDenom = sessionDenom;
        dsq.vin = activeMasternode.vin;
        dsq.time = GetTime();
        dsq.ready = true;
        dsq.Sign();
        dsq.Relay();

        UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);
    }

    int addLagTime = 0;
    if(!fMasterNode) addLagTime = 10000; //if we're the client, give the server a few extra seconds before resetting.

    if(state == POOL_STATUS_ACCEPTING_ENTRIES || state == POOL_STATUS_QUEUE){
        c = 0;

        // if it's a masternode, the entries are stored in "entries", otherwise they're stored in myEntries
        std::vector<CDarkSendEntry> *vec = &myEntries;
        if(fMasterNode) vec = &entries;

        // check for a timeout and reset if needed
        vector<CDarkSendEntry>::iterator it2;
        for(it2=vec->begin();it2<vec->end();it2++){
            if((*it2).IsExpired()){
                if(fDebug) printf("CDarkSendPool::CheckTimeout() : Removing expired entry - %d\n", c);
                vec->erase(it2);
                if(entries.size() == 0 && myEntries.size() == 0){
                    SetNull(true);
                    UnlockCoins();
                }
                if(fMasterNode){
                    RelayDarkSendStatus(darkSendPool.sessionID, darkSendPool.GetState(), darkSendPool.GetEntriesCount(), MASTERNODE_RESET);
                }
                break;
            }
            c++;
        }

        if(GetTimeMillis()-lastTimeChanged >= (DARKSEND_QUEUE_TIMEOUT*1000)+addLagTime){
            lastTimeChanged = GetTimeMillis();

            // reset session information for the queue query stage (before entering a masternode, clients will send a queue request to make sure they're compatible denomination wise)
            sessionUsers = 0;
            sessionDenom = 0;
            sessionFoundMasternode = false;
            vecSessionCollateral.clear();

            UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);
        }
    } else if(GetTimeMillis()-lastTimeChanged >= (DARKSEND_QUEUE_TIMEOUT*1000)+addLagTime){
        if(fDebug) printf("CDarkSendPool::CheckTimeout() -- Session timed out (30s) -- resetting\n");
        SetNull();
        UnlockCoins();

        UpdateState(POOL_STATUS_ERROR);
        lastMessage = _("Session timed out (30 seconds), please resubmit.");
    }

    if(state == POOL_STATUS_SIGNING && GetTimeMillis()-lastTimeChanged >= (DARKSEND_SIGNING_TIMEOUT*1000)+addLagTime ) {
        if(fDebug) printf("CDarkSendPool::CheckTimeout() -- Session timed out -- restting\n");
        SetNull();
        UnlockCoins();
        //add my transactions to the new session

        UpdateState(POOL_STATUS_ERROR);
        lastMessage = _("Signing timed out, please resubmit.");
    }
}

// check to see if the signature is valid
bool CDarkSendPool::SignatureValid(const CScript& newSig, const CTxIn& newVin){
    CTransaction txNew;
    txNew.vin.clear();
    txNew.vout.clear();

    int found = -1;
    CScript sigPubKey = CScript();
    unsigned int i = 0;

    BOOST_FOREACH(CDarkSendEntry e, entries) {
        BOOST_FOREACH(const CTxOut out, e.vout)
            txNew.vout.push_back(out);

        BOOST_FOREACH(const CDarkSendEntryVin s, e.sev){
            txNew.vin.push_back(s.vin);

            if(s.vin == newVin){
                found = i;
                sigPubKey = s.vin.prevPubKey;
            }
            i++;
        }
    }

    if(found >= 0){ //might have to do this one input at a time?
        int n = found;
        txNew.vin[n].scriptSig = newSig;
        if(fDebug) printf("CDarkSendPool::SignatureValid() - Sign with sig %s\n", newSig.ToString().substr(0,24).c_str());
        if (!VerifyScript(txNew.vin[n].scriptSig, sigPubKey, txNew, i, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, 0)){
            if(fDebug) printf("CDarkSendPool::SignatureValid() - Signing - Error signing input %u\n", n);
            return false;
        }
    }

    if(fDebug) printf("CDarkSendPool::SignatureValid() - Signing - Succesfully signed input\n");
    return true;
}

// check to make sure the collateral provided by the client is valid
bool CDarkSendPool::IsCollateralValid(const CTransaction& txCollateral){
    if(txCollateral.vout.size() < 1) return false;
    if(txCollateral.nLockTime != 0) return false;

    int64_t nValueIn = 0;
    int64_t nValueOut = 0;
    bool missingTx = false;

    BOOST_FOREACH(const CTxOut o, txCollateral.vout){
        nValueOut += o.nValue;

        if(!o.scriptPubKey.IsNormalPaymentScript()){
            printf("CDarkSendPool::IsCollateralValid - Invalid Script %s\n", txCollateral.ToString().c_str());
            return false;
        }
    }

    BOOST_FOREACH(const CTxIn i, txCollateral.vin){
        CTransaction tx2;
        uint256 hash;
        //if(GetTransaction(i.prevout.hash, tx2, hash, true)){
    if(GetTransaction(i.prevout.hash, tx2, hash)){
            if(tx2.vout.size() > i.prevout.n) {
                nValueIn += tx2.vout[i.prevout.n].nValue;
            }
        } else{
            missingTx = true;
        }
    }

    if(missingTx){
        if(fDebug) printf("CDarkSendPool::IsCollateralValid - Unknown inputs in collateral transaction - %s\n", txCollateral.ToString().c_str());
        return false;
    }

    //collateral transactions are required to pay out DARKSEND_COLLATERAL as a fee to the miners
    if(nValueIn-nValueOut < DARKSEND_COLLATERAL) {
        if(fDebug) printf("CDarkSendPool::IsCollateralValid - did not include enough fees in transaction %lu\n%s\n", nValueOut-nValueIn, txCollateral.ToString().c_str());
        return false;
    }

    if(fDebug) printf("CDarkSendPool::IsCollateralValid %s\n", txCollateral.ToString().c_str());

    CValidationState state;
    //if(!AcceptableInputs(mempool, state, txCollateral)){
    bool* pfMissingInputs;
    if(!AcceptableInputs(mempool, txCollateral, false, pfMissingInputs)){
        if(fDebug) printf("CDarkSendPool::IsCollateralValid - didn't pass IsAcceptable\n");
        return false;
    }

    return true;
}


//
// Add a clients transaction to the pool
//
bool CDarkSendPool::AddEntry(const std::vector<CTxIn>& newInput, const int64_t& nAmount, const CTransaction& txCollateral, const std::vector<CTxOut>& newOutput, std::string& error){
    if (!fMasterNode) return false;

    BOOST_FOREACH(CTxIn in, newInput) {
        if (in.prevout.IsNull() || nAmount < 0) {
            if(fDebug) printf("CDarkSendPool::AddEntry - input not valid!\n");
            error = _("Input is not valid.");
            sessionUsers--;
            return false;
        }
    }

    if (!IsCollateralValid(txCollateral)){
        if(fDebug) printf("CDarkSendPool::AddEntry - collateral not valid!\n");
        error = _("Collateral is not valid.");
        sessionUsers--;
        return false;
    }

    if((int)entries.size() >= GetMaxPoolTransactions()){
        if(fDebug) printf("CDarkSendPool::AddEntry - entries is full!\n");
        error = _("Entries are full.");
        sessionUsers--;
        return false;
    }

    BOOST_FOREACH(CTxIn in, newInput) {
        if(fDebug) printf("looking for vin -- %s\n", in.ToString().c_str());
        BOOST_FOREACH(const CDarkSendEntry v, entries) {
            BOOST_FOREACH(const CDarkSendEntryVin s, v.sev){
                if(s.vin == in) {
                    if(fDebug) printf("CDarkSendPool::AddEntry - found in vin\n");
                    error = _("Already have that input.");
                    sessionUsers--;
                    return false;
                }
            }
        }
    }

    if(state == POOL_STATUS_ACCEPTING_ENTRIES) {
        CDarkSendEntry v;
        v.Add(newInput, nAmount, txCollateral, newOutput);
        entries.push_back(v);

        if(fDebug) printf("CDarkSendPool::AddEntry -- adding %s\n", newInput[0].ToString().c_str());
        error = "";

        return true;
    }

    if(fDebug) printf("CDarkSendPool::AddEntry - can't accept new entry, wrong state!\n");
    error = _("Wrong state.");
    sessionUsers--;
    return false;
}

bool CDarkSendPool::AddScriptSig(const CTxIn newVin){
    if(fDebug) printf("CDarkSendPool::AddScriptSig -- new sig  %s\n", newVin.scriptSig.ToString().substr(0,24).c_str());

    BOOST_FOREACH(const CDarkSendEntry v, entries) {
        BOOST_FOREACH(const CDarkSendEntryVin s, v.sev){
            if(s.vin.scriptSig == newVin.scriptSig) {
                printf("CDarkSendPool::AddScriptSig - already exists \n");
                return false;
            }
        }
    }

    if(!SignatureValid(newVin.scriptSig, newVin)){
        if(fDebug) printf("CDarkSendPool::AddScriptSig - Invalid Sig\n");
        return false;
    }

    if(fDebug) printf("CDarkSendPool::AddScriptSig -- sig %s\n", newVin.ToString().c_str());

    if(state == POOL_STATUS_SIGNING) {
        BOOST_FOREACH(CTxIn& vin, finalTransaction.vin){
            if(newVin.prevout == vin.prevout && vin.nSequence == newVin.nSequence){
                vin.scriptSig = newVin.scriptSig;
                vin.prevPubKey = newVin.prevPubKey;
                if(fDebug) printf("CDarkSendPool::AddScriptSig -- adding to finalTransaction  %s\n", newVin.scriptSig.ToString().substr(0,24).c_str());
            }
        }
        for(unsigned int i = 0; i < entries.size(); i++){
            if(entries[i].AddSig(newVin)){
                if(fDebug) printf("CDarkSendPool::AddScriptSig -- adding  %s\n", newVin.scriptSig.ToString().substr(0,24).c_str());
                return true;
            }
        }
    }

    printf("CDarkSendPool::AddScriptSig -- Couldn't set sig!\n" );
    return false;
}

// check to make sure everything is signed
bool CDarkSendPool::SignaturesComplete(){
    BOOST_FOREACH(const CDarkSendEntry v, entries) {
        BOOST_FOREACH(const CDarkSendEntryVin s, v.sev){
            if(!s.isSigSet) return false;
        }
    }
    return true;
}

// Incoming message from masternode updating the progress of darksend
//    newAccepted:  -1 mean's it'n not a "transaction accepted/not accepted" message, just a standard update
//                  0 means transaction was not accepted
//                  1 means transaction was accepted

bool CDarkSendPool::StatusUpdate(int newState, int newEntriesCount, int newAccepted, std::string& error, int newSessionID){
    if(fMasterNode) return false;
    if(state == POOL_STATUS_ERROR || state == POOL_STATUS_SUCCESS) return false;

    UpdateState(newState);
    entriesCount = newEntriesCount;

    if(error.size() > 0) strAutoDenomResult = _("Masternode:") + " " + error;

    if(newAccepted != -1) {
        lastEntryAccepted = newAccepted;
        countEntriesAccepted += newAccepted;
        if(newAccepted == 0){
            UpdateState(POOL_STATUS_ERROR);
            lastMessage = error;
        }

        if(newAccepted == 1) {
            sessionID = newSessionID;
            printf("CDarkSendPool::StatusUpdate - set sessionID to %d\n", sessionID);
            sessionFoundMasternode = true;
        }
    }

    if(newState == POOL_STATUS_ACCEPTING_ENTRIES){
        if(newAccepted == 1){
            printf("CDarkSendPool::StatusUpdate - entry accepted! \n");
            sessionFoundMasternode = true;
            //wait for other users. Masternode will report when ready
            UpdateState(POOL_STATUS_QUEUE);
        } else if (newAccepted == 0 && sessionID == 0 && !sessionFoundMasternode) {
            printf("CDarkSendPool::StatusUpdate - entry not accepted by masternode \n");
            UnlockCoins();
            UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);
        }
        if(sessionFoundMasternode) return true;
    }

    return true;
}

//
// After we receive the finalized transaction from the masternode, we must
// check it to make sure it's what we want, then sign it if we agree.
// If we refuse to sign, it's possible we'll be charged collateral
//
bool CDarkSendPool::SignFinalTransaction(CTransaction& finalTransactionNew, CNode* node){
    if(fDebug) printf("CDarkSendPool::AddFinalTransaction - Got Finalized Transaction\n");

    if(!finalTransaction.vin.empty()){
        printf("CDarkSendPool::AddFinalTransaction - Rejected Final Transaction!\n");
        return false;
    }

    finalTransaction = finalTransactionNew;
    printf("CDarkSendPool::SignFinalTransaction %s\n", finalTransaction.ToString().c_str());

    vector<CTxIn> sigs;

    //make sure my inputs/outputs are present, otherwise refuse to sign
    BOOST_FOREACH(const CDarkSendEntry e, myEntries) {
        BOOST_FOREACH(const CDarkSendEntryVin s, e.sev) {
            /* Sign my transaction and all outputs */
            int mine = -1;
            CScript prevPubKey = CScript();
            CTxIn vin = CTxIn();

            for(unsigned int i = 0; i < finalTransaction.vin.size(); i++){
                if(finalTransaction.vin[i] == s.vin){
                    mine = i;
                    prevPubKey = s.vin.prevPubKey;
                    vin = s.vin;
                }
            }

            if(mine >= 0){ //might have to do this one input at a time?
                int foundOutputs = 0;
                int64_t nValue1 = 0;
                int64_t nValue2 = 0;

                for(unsigned int i = 0; i < finalTransaction.vout.size(); i++){
                    BOOST_FOREACH(const CTxOut o, e.vout) {
                        if(finalTransaction.vout[i] == o){
                            foundOutputs++;
                            nValue1 += finalTransaction.vout[i].nValue;
                        }
                    }
                }

                BOOST_FOREACH(const CTxOut o, e.vout)
                    nValue2 += o.nValue;

                int targetOuputs = e.vout.size();
                if(foundOutputs < targetOuputs || nValue1 != nValue2) {
                    // in this case, something went wrong and we'll refuse to sign. It's possible we'll be charged collateral. But that's
                    // better then signing if the transaction doesn't look like what we wanted.
                    printf("CDarkSendPool::Sign - My entries are not correct! Refusing to sign. %d entries %d target. \n", foundOutputs, targetOuputs);
                    return false;
                }

                if(fDebug) printf("CDarkSendPool::Sign - Signing my input %i\n", mine);
                if(!SignSignature(*pwalletMain, prevPubKey, finalTransaction, mine, int(SIGHASH_ALL|SIGHASH_ANYONECANPAY))) { // changes scriptSig
                    if(fDebug) printf("CDarkSendPool::Sign - Unable to sign my own transaction! \n");
                    // not sure what to do here, it will timeout...?
                }

                sigs.push_back(finalTransaction.vin[mine]);
                if(fDebug) printf(" -- dss %d %d %s\n", mine, (int)sigs.size(), finalTransaction.vin[mine].scriptSig.ToString().c_str());
            }

        }

        if(fDebug) printf("CDarkSendPool::Sign - txNew:\n%s", finalTransaction.ToString().c_str());
    }

    // push all of our signatures to the masternode
    if(sigs.size() > 0 && node != NULL)
        node->PushMessage("dss", sigs);

    return true;
}

void CDarkSendPool::NewBlock()
{
    if(fDebug) printf("CDarkSendPool::NewBlock \n");

    //we we're processing lots of blocks, we'll just leave
    if(GetTime() - lastNewBlock < 10) return;
    lastNewBlock = GetTime();

    darkSendPool.CheckTimeout();

    if(!fMasterNode){
        //denominate all non-denominated inputs every 50 blocks (25 minutes)
        if(pindexBest->nHeight % 50 == 0)
            UnlockCoins();
        // free up masternode connections every 120 blocks (1 hour) unless we are syncing
        if(pindexBest->nHeight % 120 == 0 && !IsInitialBlockDownload())
            ProcessMasternodeConnections();
    }
}

void CDarkSendPool::ClearLastMessage()
{
    lastMessage = "";
}

bool CDarkSendPool::IsCompatibleWithSession(int64_t nDenom, CTransaction txCollateral, std::string& strReason)
{
    printf("CDarkSendPool::IsCompatibleWithSession - sessionDenom %d sessionUsers %d\n", sessionDenom, sessionUsers);

    if (!unitTest && !IsCollateralValid(txCollateral)){
        if(fDebug) printf("CDarkSendPool::IsCompatibleWithSession - collateral not valid!\n");
        strReason = _("Collateral not valid.");
        return false;
    }

    if(sessionUsers < 0) sessionUsers = 0;

    if(sessionUsers == 0) {
        sessionDenom = nDenom;
        sessionUsers++;
        lastTimeChanged = GetTimeMillis();
        entries.clear();

        if(!unitTest){
            //broadcast that I'm accepting entries, only if it's the first entry though
            CDarksendQueue dsq;
            dsq.nDenom = nDenom;
            dsq.vin = activeMasternode.vin;
            dsq.time = GetTime();
            dsq.Sign();
            dsq.Relay();
        }

        UpdateState(POOL_STATUS_QUEUE);
        vecSessionCollateral.push_back(txCollateral);
        return true;
    }

    if((state != POOL_STATUS_ACCEPTING_ENTRIES && state != POOL_STATUS_QUEUE) || sessionUsers >= GetMaxPoolTransactions()){
        if((state != POOL_STATUS_ACCEPTING_ENTRIES && state != POOL_STATUS_QUEUE)) strReason = _("Incompatible mode.");
        if(sessionUsers >= GetMaxPoolTransactions()) strReason = _("Masternode queue is full.");
        printf("CDarkSendPool::IsCompatibleWithSession - incompatible mode, return false %d %d\n", state != POOL_STATUS_ACCEPTING_ENTRIES, sessionUsers >= GetMaxPoolTransactions());
        return false;
    }

    if(nDenom != sessionDenom) {
        strReason = _("No matching denominations found for mixing.");
        return false;
    }

    printf("CDarkSendPool::IsCompatibleWithSession - compatible\n");

    sessionUsers++;
    lastTimeChanged = GetTimeMillis();
    vecSessionCollateral.push_back(txCollateral);

    return true;
}

//create a nice string to show the denominations
void CDarkSendPool::GetDenominationsToString(int nDenom, std::string& strDenom){
    // Function returns as follows:
    //
    // bit 0 - 100DNR+1 ( bit on if present )
    // bit 1 - 10DNR+1
    // bit 2 - 1DNR+1
    // bit 3 - .1DNR+1
    // bit 3 - non-denom


    strDenom = "";

    if(nDenom & (1 << 0)) {
        if(strDenom.size() > 0) strDenom += "+";
        strDenom += "100";
    }

    if(nDenom & (1 << 1)) {
        if(strDenom.size() > 0) strDenom += "+";
        strDenom += "10";
    }

    if(nDenom & (1 << 2)) {
        if(strDenom.size() > 0) strDenom += "+";
        strDenom += "1";
    }

    if(nDenom & (1 << 3)) {
        if(strDenom.size() > 0) strDenom += "+";
        strDenom += "0.1";
    }
}

// return a bitshifted integer representing the denominations in this list
int CDarkSendPool::GetDenominations(const std::vector<CTxOut>& vout){
    std::vector<pair<int64_t, int> > denomUsed;

    // make a list of denominations, with zero uses
    BOOST_FOREACH(int64_t d, darkSendDenominations)
        denomUsed.push_back(make_pair(d, 0));

    // look for denominations and update uses to 1
    BOOST_FOREACH(CTxOut out, vout){
        bool found = false;
        BOOST_FOREACH (PAIRTYPE(int64_t, int)& s, denomUsed){
            if (out.nValue == s.first){
                s.second = 1;
                found = true;
            }
        }
        if(!found) return 0;
    }

    int denom = 0;
    int c = 0;
    // if the denomination is used, shift the bit on.
    // then move to the next
    BOOST_FOREACH (PAIRTYPE(int64_t, int)& s, denomUsed)
        denom |= s.second << c++;

    // Function returns as follows:
    //
    // bit 0 - 100DNR+1 ( bit on if present )
    // bit 1 - 10DNR+1
    // bit 2 - 1DNR+1
    // bit 3 - .1DNR+1

    return denom;
}


int CDarkSendPool::GetDenominationsByAmounts(std::vector<int64_t>& vecAmount){
    CScript e = CScript();
    std::vector<CTxOut> vout1;

    // Make outputs by looping through denominations, from small to large
    BOOST_REVERSE_FOREACH(int64_t v, vecAmount){
        int nOutputs = 0;

        CTxOut o(v, e);
        vout1.push_back(o);
        nOutputs++;
    }

    return GetDenominations(vout1);
}

int CDarkSendPool::GetDenominationsByAmount(int64_t nAmount, int nDenomTarget){
    CScript e = CScript();
    int64_t nValueLeft = nAmount;

    std::vector<CTxOut> vout1;

    // Make outputs by looping through denominations, from small to large
    BOOST_REVERSE_FOREACH(int64_t v, darkSendDenominations){
        if(nDenomTarget != 0){
            bool fAccepted = false;
            if((nDenomTarget & (1 << 0)) &&      v == ((100000*COIN)+100000000)) {fAccepted = true;}
            else if((nDenomTarget & (1 << 1)) && v == ((10000*COIN) +10000000)) {fAccepted = true;}
            else if((nDenomTarget & (1 << 2)) && v == ((1000*COIN)  +1000000)) {fAccepted = true;}
            else if((nDenomTarget & (1 << 3)) && v == ((100*COIN)   +100000)) {fAccepted = true;}
            else if((nDenomTarget & (1 << 4)) && v == ((10*COIN)    +10000)) {fAccepted = true;}
            else if((nDenomTarget & (1 << 5)) && v == ((1*COIN)     +1000)) {fAccepted = true;}
            else if((nDenomTarget & (1 << 6)) && v == ((.1*COIN)    +100)) {fAccepted = true;}
            if(!fAccepted) continue;
        }

        int nOutputs = 0;

        // add each output up to 10 times until it can't be added again
        while(nValueLeft - v >= 0 && nOutputs <= 10) {
            CTxOut o(v, e);
            vout1.push_back(o);
            nValueLeft -= v;
            nOutputs++;
        }
        printf("GetDenominationsByAmount --- %lu nOutputs %d\n", v, nOutputs);
    }

    //add non-denom left overs as change
    if(nValueLeft > 0){
        CTxOut o(nValueLeft, e);
        vout1.push_back(o);
    }

    return GetDenominations(vout1);
}

bool CDarkSendSigner::IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey){
    CScript payee2;
    payee2= GetScriptForDestination(pubkey.GetID());

    CTransaction txVin;
    uint256 hash;
    //if(GetTransaction(vin.prevout.hash, txVin, hash, true)){
    if(GetTransaction(vin.prevout.hash, txVin, hash)){
        BOOST_FOREACH(CTxOut out, txVin.vout){
            if(out.nValue == GetMNCollateral()*COIN){
                if(out.scriptPubKey == payee2) return true;
            }
        }
    } else {
        printf("IsVinAssociatedWithPubKey:: GetTransaction failed for %s\n",vin.prevout.hash.ToString().c_str());
    }

    CTxDestination address1;
    ExtractDestination(payee2, address1);
    CBitcoinAddress address2(address1);
    printf("IsVinAssociatedWithPubKey:: vin %s is not associated with pubkey %s for address %s\n",
           vin.ToString().c_str(), pubkey.GetHash().ToString().c_str(), address2.ToString().c_str());
    return false;
}

bool CDarkSendSigner::SetKey(std::string strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey){
    CBitcoinSecret vchSecret;
    bool fGood = vchSecret.SetString(strSecret);

    if (!fGood) {
        errorMessage = _("Invalid private key.");
        return false;
    }

    key = vchSecret.GetKey();
    pubkey = key.GetPubKey();

    return true;
}

bool CDarkSendSigner::SignMessage(std::string strMessage, std::string& errorMessage, vector<unsigned char>& vchSig, CKey key)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        errorMessage = _("Signing failed.");
        return false;
    }

    return true;
}

bool CDarkSendSigner::VerifyMessage(CPubKey pubkey, vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey2;
    if (!pubkey2.RecoverCompact(ss.GetHash(), vchSig)) {
        errorMessage = _("Error recovering public key.");
        return false;
    }

    if (fDebug && pubkey2.GetID() != pubkey.GetID())
        printf("CDarkSendSigner::VerifyMessage -- keys don't match: %s %s", pubkey2.GetID().ToString().c_str(), pubkey.GetID().ToString().c_str());

    return (pubkey2.GetID() == pubkey.GetID());
}

bool CDarksendQueue::Sign()
{
    if(!fMasterNode) return false;

    std::string strMessage = vin.ToString() + boost::lexical_cast<std::string>(nDenom) + boost::lexical_cast<std::string>(time) + boost::lexical_cast<std::string>(ready);

    CKey key2;
    CPubKey pubkey2;
    std::string errorMessage = "";

    if(!darkSendSigner.SetKey(strMasterNodePrivKey, errorMessage, key2, pubkey2))
    {
        printf("CDarksendQueue():Relay - ERROR: Invalid masternodeprivkey: '%s'\n", errorMessage.c_str());
        return false;
    }

    if(!darkSendSigner.SignMessage(strMessage, errorMessage, vchSig, key2)) {
        printf("CDarksendQueue():Relay - Sign message failed");
        return false;
    }

    if(!darkSendSigner.VerifyMessage(pubkey2, vchSig, strMessage, errorMessage)) {
        printf("CDarksendQueue():Relay - Verify message failed");
        return false;
    }

    return true;
}

bool CDarksendQueue::Relay()
{

    //LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes){
        // always relay to everyone
        pnode->PushMessage("dsq", (*this));
    }

    return true;
}

bool CDarksendQueue::CheckSignature()
{
    BOOST_FOREACH(CMasterNode& mn, vecMasternodes) {

        if(mn.vin == vin) {
            std::string strMessage = vin.ToString() + boost::lexical_cast<std::string>(nDenom) + boost::lexical_cast<std::string>(time) + boost::lexical_cast<std::string>(ready);

            std::string errorMessage = "";
            if(!darkSendSigner.VerifyMessage(mn.pubkey2, vchSig, strMessage, errorMessage)){
                return error("CDarksendQueue::CheckSignature() - Got bad masternode address signature %s \n", vin.ToString().c_str());
            }

            return true;
        }
    }

    return false;
}


//TODO: Rename/move to core
void ThreadCheckDarkSendPool(void* parg)
{
    // Make this thread recognisable as the wallet flushing thread
    RenameThread("denarius-mn");

    unsigned int c = 0;
    std::string errorMessage;

    while (true && !fShutdown)
    {
        c++;

        MilliSleep(1000);
        //printf("ThreadCheckDarkSendPool::check timeout\n");
        //darkSendPool.CheckTimeout();

        int mnTimeout = 150; //2.5 minutes

        if(c % mnTimeout == 0){
            LOCK(cs_main);
            /*
                cs_main is required for doing masternode.Check because something
                is modifying the coins view without a mempool lock. It causes
                segfaults from this code without the cs_main lock.
            */
        {

        LOCK(cs_masternodes);
            vector<CMasterNode>::iterator it = vecMasternodes.begin();
            //check them separately
            while(it != vecMasternodes.end()){
                (*it).Check();
                ++it;
            }

            //remove inactive
            it = vecMasternodes.begin();
            while(it != vecMasternodes.end()){
                if((*it).enabled == 4 || (*it).enabled == 3){
                    printf("Removing inactive masternode %s\n", (*it).addr.ToString().c_str());
                    it = vecMasternodes.erase(it);
                } else {
                    ++it;
                }
            }

        }
            masternodePayments.CleanPaymentList();
        }

        int mnRefresh = 10; //(3*5)

        //try to sync the masternode list and payment list every 90 seconds from at least 3 nodes until we have them all
        if(vNodes.size() > 2 && c % mnRefresh == 0 && RequestedMasterNodeList < 10 && (mnCount == 0 || vecMasternodes.size() < mnCount)) {
            bool fIsInitialDownload = IsInitialBlockDownload();
            if(!fIsInitialDownload) {
                LOCK(cs_vNodes);
                BOOST_FOREACH(CNode* pnode, vNodes)
                {
                    if (pnode->nVersion >= darkSendPool.PROTOCOL_VERSION) {

                        //keep track of who we've asked for the list
                        if(pnode->HasFulfilledRequest("mnsync")) continue;
                        pnode->FulfilledRequest("mnsync");

                        printf("Asking for Masternode list from %s\n",pnode->addr.ToStringIPPort().c_str());

                        pnode->PushMessage("dseg", CTxIn()); //request full mn list
                        pnode->PushMessage("mnget"); //sync payees
                        pnode->PushMessage("getsporks"); //get current network sporks
                        RequestedMasterNodeList++;
                    }
                }
            }
        }

        if(c % MASTERNODE_PING_SECONDS == 0){
            activeMasternode.ManageStatus();
        }

        //if(c % (60*5) == 0){
        if(c % 60 == 0){
            //if we've used 1/5 of the masternode list, then clear the list.
            if((int)vecMasternodesUsed.size() > (int)vecMasternodes.size() / 5)
                vecMasternodesUsed.clear();
        }
    }
}
