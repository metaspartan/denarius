// Copyright (c) 2017 The Denarius developers
// Copyright (c) 2009-2012 The Darkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fortuna.h"
#include "main.h"
#include "init.h"
#include "util.h"
#include "fortunastake.h"
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

CCriticalSection cs_fortuna;

/** The main object for accessing fortuna */
CForTunaPool forTunaPool;
/** A helper object for signing messages from fortunastakes */
CForTunaSigner forTunaSigner;
/** The current fortunas in progress on the network */
std::vector<CFortunaQueue> vecFortunaQueue;
/** Keep track of the used fortunastakes */
std::vector<CTxIn> vecFortunastakesUsed;
// keep track of the scanning errors I've seen
map<uint256, CFortunaBroadcastTx> mapFortunaBroadcastTxes;
//
CActiveFortunastake activeFortunastake;
// count peers we've requested the list from
int RequestedFortunaStakeList = 0;

//MIN_MN_PROTO_VERSION
int MIN_MN_PROTO_VERSION = 31000;

/* *** BEGIN FORTUNA MAGIC  **********
    Copyright 2014, Darkcoin Developers
        eduffield - evan@darkcoin.io
    Copyright 2018, Denarius Developers
        carsenk - admin@denarius.io
        enkayz - enkayz@denarius.io
*/

int randomizeList (int i) { return std::rand()%i;}

// Recursively determine the rounds of a given input (How deep is the fortuna chain for a given input)
int GetInputFortunaRounds(CTxIn in, int rounds)
{
    if(rounds >= 17) return rounds;

    std::string padding = "";
    padding.insert(0, ((rounds+1)*5)+3, ' ');

    CWalletTx tx;
    if(pwalletMain->GetTransaction(in.prevout.hash,tx))
    {
        // bounds check
        if(in.prevout.n >= tx.vout.size()) return -4;

        if(tx.vout[in.prevout.n].nValue == FORTUNA_FEE) return -3;

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
                int n = GetInputFortunaRounds(in2, rounds+1);
                if(n != -3) return n;
            }
        }
    }

    return rounds-1;
}

void CForTunaPool::Reset(){
    cachedLastSuccess = 0;
    vecFortunastakesUsed.clear();
    UnlockCoins();
    SetNull();
}

void CForTunaPool::SetNull(bool clearEverything){
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
    sessionFoundFortunastake = false;
    vecSessionCollateral.clear();
    txCollateral = CTransaction();

    if(clearEverything){
        myEntries.clear();

        if(fFortunaStake){
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

bool CForTunaPool::SetCollateralAddress(std::string strAddress){
    CBitcoinAddress address;
    if (!address.SetString(strAddress))
    {
        printf("CForTunaPool::SetCollateralAddress - Invalid ForTuna collateral address\n");
        return false;
    }
    collateralPubKey= GetScriptForDestination(address.Get());
    return true;
}

//
// Unlock coins after Fortuna fails or succeeds
//
void CForTunaPool::UnlockCoins(){
    BOOST_FOREACH(CTxIn v, lockedCoins)
        pwalletMain->UnlockCoin(v.prevout);

    lockedCoins.clear();
}

//
// Check for various timeouts (queue objects, fortuna, etc)
//
void CForTunaPool::CheckTimeout(){
    if(!fFortunaStake) return;

    // catching hanging sessions
    if(!fFortunaStake) {
        if(state == POOL_STATUS_TRANSMISSION) {
            if(fDebug) printf("CForTunaPool::CheckTimeout() -- Session complete -- Running Check()\n");
            // Check();
        }
    }

    // check fortuna queue objects for timeouts
    int c = 0;
    vector<CFortunaQueue>::iterator it;
    for(it=vecFortunaQueue.begin();it<vecFortunaQueue.end();it++){
        if((*it).IsExpired()){
            if(fDebug) printf("CForTunaPool::CheckTimeout() : Removing expired queue entry - %d\n", c);
            vecFortunaQueue.erase(it);
            break;
        }
        c++;
    }

    /* Check to see if we're ready for submissions from clients */
    if(state == POOL_STATUS_QUEUE && sessionUsers == GetMaxPoolTransactions()) {
        CFortunaQueue dsq;
        dsq.nDenom = sessionDenom;
        dsq.vin = activeFortunastake.vin;
        dsq.time = GetTime();
        dsq.ready = true;
        dsq.Sign();
        dsq.Relay();

        UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);
    }

    int addLagTime = 0;
    if(!fFortunaStake) addLagTime = 10000; //if we're the client, give the server a few extra seconds before resetting.

    if(state == POOL_STATUS_ACCEPTING_ENTRIES || state == POOL_STATUS_QUEUE){
        c = 0;

        // if it's a fortunastake, the entries are stored in "entries", otherwise they're stored in myEntries
        std::vector<CForTunaEntry> *vec = &myEntries;
        if(fFortunaStake) vec = &entries;

        // check for a timeout and reset if needed
        vector<CForTunaEntry>::iterator it2;
        for(it2=vec->begin();it2<vec->end();it2++){
            if((*it2).IsExpired()){
                if(fDebug) printf("CForTunaPool::CheckTimeout() : Removing expired entry - %d\n", c);
                vec->erase(it2);
                if(entries.size() == 0 && myEntries.size() == 0){
                    SetNull(true);
                    UnlockCoins();
                }
                if(fFortunaStake){
                    RelayForTunaStatus(forTunaPool.sessionID, forTunaPool.GetState(), forTunaPool.GetEntriesCount(), FORTUNASTAKE_RESET);
                }
                break;
            }
            c++;
        }

        if(GetTimeMillis()-lastTimeChanged >= (FORTUNA_QUEUE_TIMEOUT*1000)+addLagTime){
            lastTimeChanged = GetTimeMillis();

            // reset session information for the queue query stage (before entering a fortunastake, clients will send a queue request to make sure they're compatible denomination wise)
            sessionUsers = 0;
            sessionDenom = 0;
            sessionFoundFortunastake = false;
            vecSessionCollateral.clear();

            UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);
        }
    } else if(GetTimeMillis()-lastTimeChanged >= (FORTUNA_QUEUE_TIMEOUT*1000)+addLagTime){
        if(fDebug) printf("CForTunaPool::CheckTimeout() -- Session timed out (30s) -- resetting\n");
        SetNull();
        UnlockCoins();

        UpdateState(POOL_STATUS_ERROR);
        lastMessage = _("Session timed out (30 seconds), please resubmit.");
    }

    if(state == POOL_STATUS_SIGNING && GetTimeMillis()-lastTimeChanged >= (FORTUNA_SIGNING_TIMEOUT*1000)+addLagTime ) {
        if(fDebug) printf("CForTunaPool::CheckTimeout() -- Session timed out -- restting\n");
        SetNull();
        UnlockCoins();
        //add my transactions to the new session

        UpdateState(POOL_STATUS_ERROR);
        lastMessage = _("Signing timed out, please resubmit.");
    }
}

// check to see if the signature is valid
bool CForTunaPool::SignatureValid(const CScript& newSig, const CTxIn& newVin){
    CTransaction txNew;
    txNew.vin.clear();
    txNew.vout.clear();

    int found = -1;
    CScript sigPubKey = CScript();
    unsigned int i = 0;

    BOOST_FOREACH(CForTunaEntry e, entries) {
        BOOST_FOREACH(const CTxOut out, e.vout)
            txNew.vout.push_back(out);

        BOOST_FOREACH(const CForTunaEntryVin s, e.sev){
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
        if(fDebug) printf("CForTunaPool::SignatureValid() - Sign with sig %s\n", newSig.ToString().substr(0,24).c_str());
        if (!VerifyScript(txNew.vin[n].scriptSig, sigPubKey, txNew, i, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, 0)){
            if(fDebug) printf("CForTunaPool::SignatureValid() - Signing - Error signing input %u\n", n);
            return false;
        }
    }

    if(fDebug) printf("CForTunaPool::SignatureValid() - Signing - Succesfully signed input\n");
    return true;
}

// check to make sure the collateral provided by the client is valid
bool CForTunaPool::IsCollateralValid(const CTransaction& txCollateral){
    if(txCollateral.vout.size() < 1) return false;
    if(txCollateral.nLockTime != 0) return false;

    int64_t nValueIn = 0;
    int64_t nValueOut = 0;
    bool missingTx = false;

    BOOST_FOREACH(const CTxOut o, txCollateral.vout){
        nValueOut += o.nValue;

        if(!o.scriptPubKey.IsNormalPaymentScript()){
            printf("CForTunaPool::IsCollateralValid - Invalid Script %s\n", txCollateral.ToString().c_str());
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
        if(fDebug) printf("CForTunaPool::IsCollateralValid - Unknown inputs in collateral transaction - %s\n", txCollateral.ToString().c_str());
        return false;
    }

    //collateral transactions are required to pay out FORTUNA_COLLATERAL as a fee to the miners
    if(nValueIn-nValueOut < FORTUNA_COLLATERAL) {
        if(fDebug) printf("CForTunaPool::IsCollateralValid - did not include enough fees in transaction %lu\n%s\n", nValueOut-nValueIn, txCollateral.ToString().c_str());
        return false;
    }

    if(fDebug) printf("CForTunaPool::IsCollateralValid %s\n", txCollateral.ToString().c_str());

    CValidationState state;
    //if(!AcceptableInputs(mempool, state, txCollateral)){
    bool* pfMissingInputs;
    if(!AcceptableInputs(mempool, txCollateral, false, pfMissingInputs)){
        if(fDebug) printf("CForTunaPool::IsCollateralValid - didn't pass IsAcceptable\n");
        return false;
    }

    return true;
}


//
// Add a clients transaction to the pool
//
bool CForTunaPool::AddEntry(const std::vector<CTxIn>& newInput, const int64_t& nAmount, const CTransaction& txCollateral, const std::vector<CTxOut>& newOutput, std::string& error){
    if (!fFortunaStake) return false;

    BOOST_FOREACH(CTxIn in, newInput) {
        if (in.prevout.IsNull() || nAmount < 0) {
            if(fDebug) printf("CForTunaPool::AddEntry - input not valid!\n");
            error = _("Input is not valid.");
            sessionUsers--;
            return false;
        }
    }

    if (!IsCollateralValid(txCollateral)){
        if(fDebug) printf("CForTunaPool::AddEntry - collateral not valid!\n");
        error = _("Collateral is not valid.");
        sessionUsers--;
        return false;
    }

    if((int)entries.size() >= GetMaxPoolTransactions()){
        if(fDebug) printf("CForTunaPool::AddEntry - entries is full!\n");
        error = _("Entries are full.");
        sessionUsers--;
        return false;
    }

    BOOST_FOREACH(CTxIn in, newInput) {
        if(fDebug) printf("looking for vin -- %s\n", in.ToString().c_str());
        BOOST_FOREACH(const CForTunaEntry v, entries) {
            BOOST_FOREACH(const CForTunaEntryVin s, v.sev){
                if(s.vin == in) {
                    if(fDebug) printf("CForTunaPool::AddEntry - found in vin\n");
                    error = _("Already have that input.");
                    sessionUsers--;
                    return false;
                }
            }
        }
    }

    if(state == POOL_STATUS_ACCEPTING_ENTRIES) {
        CForTunaEntry v;
        v.Add(newInput, nAmount, txCollateral, newOutput);
        entries.push_back(v);

        if(fDebug) printf("CForTunaPool::AddEntry -- adding %s\n", newInput[0].ToString().c_str());
        error = "";

        return true;
    }

    if(fDebug) printf("CForTunaPool::AddEntry - can't accept new entry, wrong state!\n");
    error = _("Wrong state.");
    sessionUsers--;
    return false;
}

bool CForTunaPool::AddScriptSig(const CTxIn newVin){
    if(fDebug) printf("CForTunaPool::AddScriptSig -- new sig  %s\n", newVin.scriptSig.ToString().substr(0,24).c_str());

    BOOST_FOREACH(const CForTunaEntry v, entries) {
        BOOST_FOREACH(const CForTunaEntryVin s, v.sev){
            if(s.vin.scriptSig == newVin.scriptSig) {
                printf("CForTunaPool::AddScriptSig - already exists \n");
                return false;
            }
        }
    }

    if(!SignatureValid(newVin.scriptSig, newVin)){
        if(fDebug) printf("CForTunaPool::AddScriptSig - Invalid Sig\n");
        return false;
    }

    if(fDebug) printf("CForTunaPool::AddScriptSig -- sig %s\n", newVin.ToString().c_str());

    if(state == POOL_STATUS_SIGNING) {
        BOOST_FOREACH(CTxIn& vin, finalTransaction.vin){
            if(newVin.prevout == vin.prevout && vin.nSequence == newVin.nSequence){
                vin.scriptSig = newVin.scriptSig;
                vin.prevPubKey = newVin.prevPubKey;
                if(fDebug) printf("CForTunaPool::AddScriptSig -- adding to finalTransaction  %s\n", newVin.scriptSig.ToString().substr(0,24).c_str());
            }
        }
        for(unsigned int i = 0; i < entries.size(); i++){
            if(entries[i].AddSig(newVin)){
                if(fDebug) printf("CForTunaPool::AddScriptSig -- adding  %s\n", newVin.scriptSig.ToString().substr(0,24).c_str());
                return true;
            }
        }
    }

    printf("CForTunaPool::AddScriptSig -- Couldn't set sig!\n" );
    return false;
}

// check to make sure everything is signed
bool CForTunaPool::SignaturesComplete(){
    BOOST_FOREACH(const CForTunaEntry v, entries) {
        BOOST_FOREACH(const CForTunaEntryVin s, v.sev){
            if(!s.isSigSet) return false;
        }
    }
    return true;
}

// Incoming message from fortunastake updating the progress of fortuna
//    newAccepted:  -1 mean's it'n not a "transaction accepted/not accepted" message, just a standard update
//                  0 means transaction was not accepted
//                  1 means transaction was accepted

bool CForTunaPool::StatusUpdate(int newState, int newEntriesCount, int newAccepted, std::string& error, int newSessionID){
    if(fFortunaStake) return false;
    if(state == POOL_STATUS_ERROR || state == POOL_STATUS_SUCCESS) return false;

    UpdateState(newState);
    entriesCount = newEntriesCount;

    if(error.size() > 0) strAutoDenomResult = _("Fortunastake:") + " " + error;

    if(newAccepted != -1) {
        lastEntryAccepted = newAccepted;
        countEntriesAccepted += newAccepted;
        if(newAccepted == 0){
            UpdateState(POOL_STATUS_ERROR);
            lastMessage = error;
        }

        if(newAccepted == 1) {
            sessionID = newSessionID;
            printf("CForTunaPool::StatusUpdate - set sessionID to %d\n", sessionID);
            sessionFoundFortunastake = true;
        }
    }

    if(newState == POOL_STATUS_ACCEPTING_ENTRIES){
        if(newAccepted == 1){
            printf("CForTunaPool::StatusUpdate - entry accepted! \n");
            sessionFoundFortunastake = true;
            //wait for other users. Fortunastake will report when ready
            UpdateState(POOL_STATUS_QUEUE);
        } else if (newAccepted == 0 && sessionID == 0 && !sessionFoundFortunastake) {
            printf("CForTunaPool::StatusUpdate - entry not accepted by fortunastake \n");
            UnlockCoins();
            UpdateState(POOL_STATUS_ACCEPTING_ENTRIES);
        }
        if(sessionFoundFortunastake) return true;
    }

    return true;
}

//
// After we receive the finalized transaction from the fortunastake, we must
// check it to make sure it's what we want, then sign it if we agree.
// If we refuse to sign, it's possible we'll be charged collateral
//
bool CForTunaPool::SignFinalTransaction(CTransaction& finalTransactionNew, CNode* node){
    if(fDebug) printf("CForTunaPool::AddFinalTransaction - Got Finalized Transaction\n");

    if(!finalTransaction.vin.empty()){
        printf("CForTunaPool::AddFinalTransaction - Rejected Final Transaction!\n");
        return false;
    }

    finalTransaction = finalTransactionNew;
    printf("CForTunaPool::SignFinalTransaction %s\n", finalTransaction.ToString().c_str());

    vector<CTxIn> sigs;

    //make sure my inputs/outputs are present, otherwise refuse to sign
    BOOST_FOREACH(const CForTunaEntry e, myEntries) {
        BOOST_FOREACH(const CForTunaEntryVin s, e.sev) {
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
                    printf("CForTunaPool::Sign - My entries are not correct! Refusing to sign. %d entries %d target. \n", foundOutputs, targetOuputs);
                    return false;
                }

                if(fDebug) printf("CForTunaPool::Sign - Signing my input %i\n", mine);
                if(!SignSignature(*pwalletMain, prevPubKey, finalTransaction, mine, int(SIGHASH_ALL|SIGHASH_ANYONECANPAY))) { // changes scriptSig
                    if(fDebug) printf("CForTunaPool::Sign - Unable to sign my own transaction! \n");
                    // not sure what to do here, it will timeout...?
                }

                sigs.push_back(finalTransaction.vin[mine]);
                if(fDebug) printf(" -- dss %d %d %s\n", mine, (int)sigs.size(), finalTransaction.vin[mine].scriptSig.ToString().c_str());
            }

        }

        if(fDebug) printf("CForTunaPool::Sign - txNew:\n%s", finalTransaction.ToString().c_str());
    }

    // push all of our signatures to the fortunastake
    if(sigs.size() > 0 && node != NULL)
        node->PushMessage("dss", sigs);

    return true;
}

void CForTunaPool::NewBlock()
{
    if(fDebug) printf("CForTunaPool::NewBlock \n");

    //we we're processing lots of blocks, we'll just leave
    if(GetTime() - lastNewBlock < 10) return;
    lastNewBlock = GetTime();

    forTunaPool.CheckTimeout();

    if(!fFortunaStake){
        //denominate all non-denominated inputs every 50 blocks (25 minutes)
        if(pindexBest->nHeight % 50 == 0)
            UnlockCoins();
        // free up fortunastake connections every 30 blocks unless we are syncing
        if(pindexBest->nHeight % 60 == 0 && !IsInitialBlockDownload())
            ProcessFortunastakeConnections();
    }
}

void CForTunaPool::ClearLastMessage()
{
    lastMessage = "";
}

bool CForTunaPool::IsCompatibleWithSession(int64_t nDenom, CTransaction txCollateral, std::string& strReason)
{
    printf("CForTunaPool::IsCompatibleWithSession - sessionDenom %d sessionUsers %d\n", sessionDenom, sessionUsers);

    if (!unitTest && !IsCollateralValid(txCollateral)){
        if(fDebug) printf("CForTunaPool::IsCompatibleWithSession - collateral not valid!\n");
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
            CFortunaQueue dsq;
            dsq.nDenom = nDenom;
            dsq.vin = activeFortunastake.vin;
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
        if(sessionUsers >= GetMaxPoolTransactions()) strReason = _("Fortunastake queue is full.");
        printf("CForTunaPool::IsCompatibleWithSession - incompatible mode, return false %d %d\n", state != POOL_STATUS_ACCEPTING_ENTRIES, sessionUsers >= GetMaxPoolTransactions());
        return false;
    }

    if(nDenom != sessionDenom) {
        strReason = _("No matching denominations found for mixing.");
        return false;
    }

    printf("CForTunaPool::IsCompatibleWithSession - compatible\n");

    sessionUsers++;
    lastTimeChanged = GetTimeMillis();
    vecSessionCollateral.push_back(txCollateral);

    return true;
}

//create a nice string to show the denominations
void CForTunaPool::GetDenominationsToString(int nDenom, std::string& strDenom){
    // Function returns as follows:
    //
    // bit 0 - 100D+1 ( bit on if present )
    // bit 1 - 10D+1
    // bit 2 - 1D+1
    // bit 3 - .1D+1
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
int CForTunaPool::GetDenominations(const std::vector<CTxOut>& vout){
    std::vector<pair<int64_t, int> > denomUsed;

    // make a list of denominations, with zero uses
    BOOST_FOREACH(int64_t d, forTunaDenominations)
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
    // bit 0 - 100D+1 ( bit on if present )
    // bit 1 - 10D+1
    // bit 2 - 1D+1
    // bit 3 - .1D+1

    return denom;
}


int CForTunaPool::GetDenominationsByAmounts(std::vector<int64_t>& vecAmount){
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

int CForTunaPool::GetDenominationsByAmount(int64_t nAmount, int nDenomTarget){
    CScript e = CScript();
    int64_t nValueLeft = nAmount;

    std::vector<CTxOut> vout1;

    // Make outputs by looping through denominations, from small to large
    BOOST_REVERSE_FOREACH(int64_t v, forTunaDenominations){
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

bool CForTunaSigner::IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey){
	bool fIsInitialDownload = IsInitialBlockDownload();
    if(fIsInitialDownload) return nullptr; // Needs to return a value, so returns null pointer v3.3.9.1 - macOS
	
    CScript payee2;
    payee2= GetScriptForDestination(pubkey.GetID());

    CTransaction txVin;
    uint256 hash;
	
    if(GetTransaction(vin.prevout.hash, txVin, hash)){
        CTxOut out = txVin.vout[vin.prevout.n];
		if ((out.nValue == GetMNCollateral()*COIN) && (out.scriptPubKey == payee2))
		{
			return true;
		}
    } else {
		if (fDebug) {
			printf("IsVinAssociatedWithPubKey:: GetTransaction failed for %s\n",vin.prevout.hash.ToString().c_str());
		}
	}

    CTxDestination address1;
    ExtractDestination(payee2, address1);
    CBitcoinAddress address2(address1);
	if (fDebug) {
		printf("IsVinAssociatedWithPubKey:: vin %s is not associated with pubkey %s for address %s\n",
			   vin.ToString().c_str(), pubkey.GetHash().ToString().c_str(), address2.ToString().c_str());
	}
    return false;
}

bool CForTunaSigner::SetKey(std::string strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey){
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

bool CForTunaSigner::SignMessage(std::string strMessage, std::string& errorMessage, vector<unsigned char>& vchSig, CKey key)
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

bool CForTunaSigner::VerifyMessage(CPubKey pubkey, vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage)
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
        printf("CForTunaSigner::VerifyMessage -- keys don't match: %s %s", pubkey2.GetID().ToString().c_str(), pubkey.GetID().ToString().c_str());

    return (pubkey2.GetID() == pubkey.GetID());
}

bool CFortunaQueue::Sign()
{
    if(!fFortunaStake) return false;

    std::string strMessage = vin.ToString() + boost::lexical_cast<std::string>(nDenom) + boost::lexical_cast<std::string>(time) + boost::lexical_cast<std::string>(ready);

    CKey key2;
    CPubKey pubkey2;
    std::string errorMessage = "";

    if(!forTunaSigner.SetKey(strFortunaStakePrivKey, errorMessage, key2, pubkey2))
    {
        printf("CFortunaQueue():Relay - ERROR: Invalid fortunastakeprivkey: '%s'\n", errorMessage.c_str());
        return false;
    }

    if(!forTunaSigner.SignMessage(strMessage, errorMessage, vchSig, key2)) {
        printf("CFortunaQueue():Relay - Sign message failed");
        return false;
    }

    if(!forTunaSigner.VerifyMessage(pubkey2, vchSig, strMessage, errorMessage)) {
        printf("CFortunaQueue():Relay - Verify message failed");
        return false;
    }

    return true;
}

bool CFortunaQueue::Relay()
{

    //LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes){
        // always relay to everyone
        pnode->PushMessage("dsq", (*this));
    }

    return true;
}

bool CFortunaQueue::CheckSignature()
{
    BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes) {

        if(mn.vin == vin) {
            std::string strMessage = vin.ToString() + boost::lexical_cast<std::string>(nDenom) + boost::lexical_cast<std::string>(time) + boost::lexical_cast<std::string>(ready);

            std::string errorMessage = "";
            if(!forTunaSigner.VerifyMessage(mn.pubkey2, vchSig, strMessage, errorMessage)){
                return error("CFortunaQueue::CheckSignature() - Got bad fortunastake address signature %s \n", vin.ToString().c_str());
            }

            return true;
        }
    }

    return false;
}


//TODO: Rename/move to core
void ThreadCheckForTunaPool(void* parg)
{
    // Make this thread recognisable as the wallet flushing thread
    RenameThread("denarius-mn");

    unsigned int c = 0;
    std::string errorMessage;

    while (true && !fShutdown)
    {
        c++;

        MilliSleep(1000);
        //printf("ThreadCheckForTunaPool::check timeout\n");
        //forTunaPool.CheckTimeout();

        int mnTimeout = 150; //2.5 minutes

        if(c % mnTimeout == 0){
            LOCK(cs_main);
            /*
                cs_main is required for doing fortunastake.Check because something
                is modifying the coins view without a mempool lock. It causes
                segfaults from this code without the cs_main lock.
            */
        {

        LOCK(cs_fortunastakes);
            vector<CFortunaStake>::iterator it = vecFortunastakes.begin();
            //check them separately
            while(it != vecFortunastakes.end()){
                (*it).Check();
                ++it;
            }

            //remove inactive
            it = vecFortunastakes.begin();
            while(it != vecFortunastakes.end()){
                if((*it).enabled == 4 || (*it).enabled == 3){
                    printf("Removing inactive fortunastake %s\n", (*it).addr.ToString().c_str());
                    it = vecFortunastakes.erase(it);
                    mnCount = mnCount-1;
                } else {
                    ++it;
                }
            }

        }
            fortunastakePayments.CleanPaymentList();
        }

        int mnRefresh = 30;

        //try to sync the fortunastake list and payment list every 30 seconds from at least 2 nodes until we have them all
        if(vNodes.size() > 1 && c % mnRefresh == 0 && (mnCount == 0 || vecFortunastakes.size() < mnCount)) {
            bool fIsInitialDownload = IsInitialBlockDownload();
            if(!fIsInitialDownload) {
                LOCK(cs_vNodes);
                BOOST_FOREACH(CNode* pnode, vNodes)
                {
                    if (pnode->nVersion >= forTunaPool.PROTOCOL_VERSION) {

                        // re-request from each node every 120 seconds
                        if(GetTime() - pnode->nLastDseg < 120)
                        {
                            continue;
                        } else {
                            printf("Asking for Fortunastake list from %s\n",pnode->addr.ToStringIPPort().c_str());
                            pnode->PushMessage("dseg", CTxIn()); //request full mn list
                            pnode->nLastDseg = GetTime();
                            pnode->PushMessage("getsporks"); //get current network sporks
                            RequestedFortunaStakeList++;
                        }
                    }
                }
            }
        }

        if(c % FORTUNASTAKE_PING_SECONDS == 0){
            activeFortunastake.ManageStatus();
        }

        //if(c % (60*5) == 0){
        if(c % 60 == 0 && vecFortunastakes.size()) {
            //let's connect to a random fortunastake every minute!
            int fs = rand() % vecFortunastakes.size();
            CService addr = vecFortunastakes[fs].addr;
            AddOneShot(addr.ToStringIPPort());
            if (fDebug) printf("added fortunastake at %s to connection attempts\n",addr.ToStringIPPort().c_str());


            //if we're low on peers, let's connect to some random ipv4 fortunastakes. ipv6 probably won't route anyway
            if (GetArg("-maxconnections", 125) > 16 && vNodes.size() < min(25, (int)GetArg("-maxconnections", 125)) && vecFortunastakes.size() > 25) {
                int x = 25 - vNodes.size();
                for (int i = x; i-- > 0; ) {
                    int fs = rand() % vecFortunastakes.size();
                    CService addr = vecFortunastakes[fs].addr;
                    if (addr.IsIPv4() && !addr.IsLocal()) {
                        AddOneShot(addr.ToStringIPPort());
                    }
                }

            }
            //if we've used 1/5 of the fortunastake list, then clear the list.
            if((int)vecFortunastakesUsed.size() > (int)vecFortunastakes.size() / 5)
                vecFortunastakesUsed.clear();
        }
    }
}
