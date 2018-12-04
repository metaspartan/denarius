// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The DarkCoin developers
// Copyright (c) 2018 The Denarius developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "fortunastake.h"
#include "activefortunastake.h"
#include "fortuna.h"
#include "txdb.h"
#include "main.h"
#include "util.h"
#include "addrman.h"
#include "sync.h"
#include "core.h"
#include <boost/lexical_cast.hpp>

int CFortunaStake::minProtoVersion = MIN_MN_PROTO_VERSION;

CCriticalSection cs_fortunastakes;

/** The list of active fortunastakes */
std::vector<CFortunaStake> vecFortunastakes;
std::vector<pair<int, CFortunaStake*> > vecFortunastakeScores;
std::vector<pair<int, CFortunaStake> > vecFortunastakeRanks;
/** Object for who's going to get paid on which blocks */
CFortunastakePayments fortunastakePayments;
// keep track of fortunastake votes I've seen
map<uint256, CFortunastakePaymentWinner> mapSeenFortunastakeVotes;
// keep track of the scanning errors I've seen
map<uint256, int> mapSeenFortunastakeScanningErrors;
// who's asked for the fortunastake list and the last time
std::map<CNetAddr, int64_t> askedForFortunastakeList;
// which fortunastakes we've asked for
std::map<COutPoint, int64_t> askedForFortunastakeListEntry;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;
CMedianFilter<unsigned int> mnMedianCount(10, 0);
unsigned int mnCount = 0;

// manage the fortunastake connections
void ProcessFortunastakeConnections(){
    LOCK(cs_vNodes);

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        //if it's our fortunastake, let it be
        if(forTunaPool.submittedToFortunastake == pnode->addr) continue;

        if( pnode->fForTunaMaster ||
            (pnode->addr.GetPort() == 9999 && pnode->nStartingHeight > (nBestHeight - 120)) // disconnect fortunastakes that were in sync when they connected recently
                )
        {
            printf("Closing fortunastake connection %s \n", pnode->addr.ToString().c_str());
            pnode->CloseSocketDisconnect();
        }
    }
}

void ProcessMessageFortunastake(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

    if (strCommand == "dsee") { //ForTuna Election Entry

        bool fIsInitialDownload = IsInitialBlockDownload();
        //if(fIsInitialDownload) return;

        CTxIn vin;
        CService addr;
        CPubKey pubkey;
        CPubKey pubkey2;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        int count;
        int current;
        int64_t lastUpdated;
        int protocolVersion;
        bool isLocal;
        std::string strMessage;

        // 70047 and greater
        vRecv >> vin >> addr >> vchSig >> sigTime >> pubkey >> pubkey2 >> count >> current >> lastUpdated >> protocolVersion;

        // make sure signature isn't in the future (past is OK)
        if (sigTime > GetAdjustedTime() + 60 * 60) {
            printf("dsee - Signature rejected, too far into the future %s\n", vin.ToString().c_str());
            return;
        }

        isLocal = addr.IsRFC1918() || addr.IsLocal();
        //if(Params().MineBlocksOnDemand()) isLocal = false;

        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

        strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

        if(protocolVersion < MIN_MN_PROTO_VERSION) {
            printf("dsee - ignoring outdated fortunastake %s protocol version %d\n", vin.ToString().c_str(), protocolVersion);
            return;
        }

        CScript pubkeyScript;
        pubkeyScript = GetScriptForDestination(pubkey.GetID());

        if(pubkeyScript.size() != 25) {
            printf("dsee - pubkey the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        CScript pubkeyScript2;
        pubkeyScript2 =GetScriptForDestination(pubkey2.GetID());

        if(pubkeyScript2.size() != 25) {
            printf("dsee - pubkey2 the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        std::string errorMessage = "";
        if(!forTunaSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage)){
            printf("dsee - Got bad fortunastake address signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if((fTestNet && addr.GetPort() != 19999) || (!fTestNet && addr.GetPort() != 9999)) return;

        //search existing fortunastake list, this is where we update existing fortunastakes with new dsee broadcasts
	      LOCK(cs_fortunastakes);
        BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes) {
            if(mn.vin.prevout == vin.prevout) {
                // count == -1 when it's a new entry
                //   e.g. We don't want the entry relayed/time updated when we're syncing the list
                // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
                //   after that they just need to match
                if(count == -1 && mn.pubkey == pubkey && !mn.UpdatedWithin(MASTERNODE_MIN_DSEE_SECONDS)){
                    mn.UpdateLastSeen();

                    if(mn.now < sigTime){ //take the newest entry
                        printf("dsee - Got updated entry for %s\n", addr.ToString().c_str());
                        mn.pubkey2 = pubkey2;
                        mn.now = sigTime;
                        mn.sig = vchSig;
                        mn.protocolVersion = protocolVersion;
                        mn.addr = addr;

                        RelayForTunaElectionEntry(vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion);
                    }
                }

                return;
            }
        }

        if (count > 0) {
            mnMedianCount.input(count);
            mnCount = mnMedianCount.median();
        }

        // make sure the vout that was signed is related to the transaction that spawned the fortunastake
        //  - this is expensive, so it's only done once per fortunastake
        if(!forTunaSigner.IsVinAssociatedWithPubkey(vin, pubkey)) {
            printf("dsee - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if(fDebug) printf("dsee - Got NEW fortunastake entry %s\n", addr.ToString().c_str());

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckForTunaPool()
        std::string vinError;
        if(CheckFortunastakeVin(vin,vinError)){
            if (fDebugNet) printf("dsee - Accepted input for fortunastake entry %i %i\n", count, current);

            if(GetInputAge(vin) < (nBestHeight > BLOCK_START_MASTERNODE_DELAYPAY ? MASTERNODE_MIN_CONFIRMATIONS_NOPAY : MASTERNODE_MIN_CONFIRMATIONS)){
                if (fDebugNet) printf("dsee - Input must have least %d confirmations\n", (nBestHeight > BLOCK_START_MASTERNODE_DELAYPAY ? MASTERNODE_MIN_CONFIRMATIONS_NOPAY : MASTERNODE_MIN_CONFIRMATIONS));
                Misbehaving(pfrom->GetId(), 20);
                return;
            }

            // use this as a peer
            addrman.Add(CAddress(addr), pfrom->addr, 2*60*60);

            // add our fortunastake
            CFortunaStake mn(addr, vin, pubkey, vchSig, sigTime, pubkey2, protocolVersion);
            mn.UpdateLastSeen(lastUpdated);
            CBlockIndex* pindex = pindexBest;

            // if it matches our fortunastakeprivkey, then we've been remotely activated
            if(pubkey2 == activeFortunastake.pubKeyFortunastake && protocolVersion == PROTOCOL_VERSION){
                activeFortunastake.EnableHotColdFortunaStake(vin, addr);
            }

            if(count == -1 && !isLocal)
                RelayForTunaElectionEntry(vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion);

            // mn.UpdateLastPaidBlock(pindex, 1000); // do a search back 1000 blocks when receiving a new fortunastake to find their last payment
            int value;
            int payments = mn.UpdateLastPaidAmounts(pindex, 1000, value); // do a search back 1000 blocks when receiving a new fortunastake to find their last payment, payments = number of payments received, value = amount
            if (payments > 0) {
                printf("Registered new fortunastake %s(%i/%i) - paid %d times for %f D\n", addr.ToString().c_str(), count, current, payments, value);
            }
            vecFortunastakes.push_back(mn);

        } else {
            printf("dsee - Rejected fortunastake entry %s: %s\n", addr.ToString().c_str(),vinError.c_str());
        }
    }

    else if (strCommand == "dseep") { //ForTuna Election Entry Ping
        bool fIsInitialDownload = IsInitialBlockDownload();
        //if(fIsInitialDownload) return;

        CTxIn vin;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        bool stop;
        vRecv >> vin >> vchSig >> sigTime >> stop;

        if (fDebug) printf("dseep - Received: vin: %s sigTime: %lld stop: %s\n", vin.ToString().c_str(), sigTime, stop ? "true" : "false");
        if (sigTime > GetAdjustedTime() + (60 * 60)*2) {
            printf("dseep - Signature rejected, too far into the future %s, sig %d local %d \n", vin.ToString().c_str(), sigTime, GetAdjustedTime());
            return;
        }

        if (sigTime <= GetAdjustedTime() - (60 * 60)*2) {
            printf("dseep - Signature rejected, too far into the past %s - sig %d local %d \n", vin.ToString().c_str(), sigTime, GetAdjustedTime());
            return;
        }

        // see if we have this fortunastake
	      LOCK(cs_fortunastakes);
        BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes) {
            if(mn.vin.prevout == vin.prevout) {
            	// printf("dseep - Found corresponding mn for vin: %s\n", vin.ToString().c_str());
            	// take this only if it's newer
                if(mn.lastDseep < sigTime){
                    std::string strMessage = mn.addr.ToString() + boost::lexical_cast<std::string>(sigTime) + boost::lexical_cast<std::string>(stop);

                    std::string errorMessage = "";
                    if(!forTunaSigner.VerifyMessage(mn.pubkey2, vchSig, strMessage, errorMessage)){
                        printf("dseep - Got bad fortunastake address signature %s \n", vin.ToString().c_str());
                        //Misbehaving(pfrom->GetId(), 100);
                        return;
                    }

                    mn.lastDseep = sigTime;

                    if(!mn.UpdatedWithin(MASTERNODE_MIN_DSEEP_SECONDS)){
                        mn.UpdateLastSeen();
                        if(stop) {
                            mn.Disable();
                            mn.Check(true);
                        }
                        RelayForTunaElectionEntryPing(vin, vchSig, sigTime, stop);
                    }
                }
                return;
            }
        }

        if (fDebugNet) printf("dseep - Couldn't find fortunastake entry %s\n", vin.ToString().c_str());

        std::map<COutPoint, int64_t>::iterator i = askedForFortunastakeListEntry.find(vin.prevout);
        if (i != askedForFortunastakeListEntry.end()){
            int64_t t = (*i).second;
            if (GetTime() < t) {
                // we've asked recently
                return;
            }
        }

        // ask for the dsee info once from the node that sent dseep

        if (fDebugNet) printf("dseep - Asking source node for missing entry %s\n", vin.ToString().c_str());
        pfrom->PushMessage("dseg", vin);
        int64_t askAgain = GetTime()+(60*1); // only ask for each dsee once per minute
        askedForFortunastakeListEntry[vin.prevout] = askAgain;

    } else if (strCommand == "dseg") { //Get fortunastake list or specific entry
        bool fIsInitialDownload = IsInitialBlockDownload();
        //if(fIsInitialDownload) return;

        CTxIn vin;
        vRecv >> vin;

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            //Note tor peers show up as local proxied addrs
            //if(!pfrom->addr.IsRFC1918())//&& !Params().MineBlocksOnDemand())
            //{
              if(!pfrom->addr.IsRFC1918())
              {
                std::map<CNetAddr, int64_t>::iterator i = askedForFortunastakeList.find(pfrom->addr);
                if (i != askedForFortunastakeList.end())
                {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        //Misbehaving(pfrom->GetId(), 34);
                        //printf("dseg - peer already asked me for the list\n");
                        //return;
                        //Misbehaving(pfrom->GetId(), 34);
                        printf("dseg - peer already asked me for the list\n");
                        return;
                    }
                }

                int64_t askAgain = GetTime()+(60*1); // only allow nodes to do a dseg all once per minute
                askedForFortunastakeList[pfrom->addr] = askAgain;
            //}
              }
        } //else, asking for a specific node which is ok

	      LOCK(cs_fortunastakes);
        int count = vecFortunastakes.size();
        int i = 0;

        BOOST_FOREACH(CFortunaStake mn, vecFortunastakes) {

            if(mn.addr.IsRFC1918()) continue; //local network

            if(vin == CTxIn()){
                mn.Check(true);
                if(mn.IsEnabled()) {
                    if(fDebug) printf("dseg - Sending fortunastake entry - %s \n", mn.addr.ToString().c_str());
                    pfrom->PushMessage("dsee", mn.vin, mn.addr, mn.sig, mn.now, mn.pubkey, mn.pubkey2, count, i, mn.lastTimeSeen, mn.protocolVersion);
                }
            } else if (vin == mn.vin) {
                if(fDebug) printf("dseg - Sending fortunastake entry - %s \n", mn.addr.ToString().c_str());
                pfrom->PushMessage("dsee", mn.vin, mn.addr, mn.sig, mn.now, mn.pubkey, mn.pubkey2, count, i, mn.lastTimeSeen, mn.protocolVersion);
                printf("dseg - Sent 1 fortunastake entries to %s\n", pfrom->addr.ToString().c_str());
                return;
            }
            i++;
        }

        printf("dseg - Sent %d fortunastake entries to %s\n", count, pfrom->addr.ToString().c_str());
    }

    else if (strCommand == "mnget") { //Fortunastake Payments Request Sync
        bool fIsInitialDownload = IsInitialBlockDownload();
        if(fIsInitialDownload) return;

        if(pfrom->HasFulfilledRequest("mnget")) {
            printf("mnget - peer already asked me for the list\n");
            Misbehaving(pfrom->GetId(), 20);
            return;
        }

        pfrom->FulfilledRequest("mnget");
        fortunastakePayments.Sync(pfrom);
        printf("mnget - Sent fortunastake winners to %s\n", pfrom->addr.ToString().c_str());
    }
    else if (strCommand == "mnw") { //Fortunastake Payments Declare Winner
        bool fIsInitialDownload = IsInitialBlockDownload();
        if(fIsInitialDownload) return;

        CFortunastakePaymentWinner winner;
        int a = 0;
        vRecv >> winner >> a;

        if(pindexBest == NULL) return;

        uint256 hash = winner.GetHash();
        if(mapSeenFortunastakeVotes.count(hash)) {
            if(fDebug) printf("mnw - seen vote %s Height %d bestHeight %d\n", hash.ToString().c_str(), winner.nBlockHeight, pindexBest->nHeight);
            return;
        }

        if(winner.nBlockHeight < pindexBest->nHeight - 10 || winner.nBlockHeight > pindexBest->nHeight+20){
            printf("mnw - winner out of range %s Height %d bestHeight %d\n", winner.vin.ToString().c_str(), winner.nBlockHeight, pindexBest->nHeight);
            return;
        }

        if(winner.vin.nSequence != std::numeric_limits<unsigned int>::max()){
            printf("mnw - invalid nSequence\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        printf("mnw - winning vote  %s Height %d bestHeight %d\n", winner.vin.ToString().c_str(), winner.nBlockHeight, pindexBest->nHeight);

        if(!fortunastakePayments.CheckSignature(winner)){
            printf("mnw - invalid signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        mapSeenFortunastakeVotes.insert(make_pair(hash, winner));

        if(fortunastakePayments.AddWinningFortunastake(winner)){
            fortunastakePayments.Relay(winner);
        }
    }
}

struct CompareValueOnly
{
    bool operator()(const pair<int64_t, CTxIn>& t1,
                    const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareLastPaidBlock
{
    bool operator()(const pair<int, CFortunaStake*>& t1,
                    const pair<int, CFortunaStake*>& t2) const
    {
        return (t1.first != t2.first ? t1.first > t2.first : t1.second->CalculateScore(1, pindexBest->nHeight) > t1.second->CalculateScore(1, pindexBest->nHeight));
    }
};

struct CompareLastPayRate
{
    bool operator()(const pair<int, CFortunaStake*>& t1,
                    const pair<int, CFortunaStake*>& t2) const
    {
        return (t1.second->payRate == t2.second->payRate ? t1.first > t2.first : t1.second->payRate > t2.second->payRate);
    }
};

struct CompareLastPayValue
{
    bool operator()(const pair<int, CFortunaStake*>& t1,
                    const pair<int, CFortunaStake*>& t2) const
    {
        return (t1.second->payValue == t2.second->payValue ? t1.first > t2.first : t1.second->payValue > t2.second->payValue);
    }
};

struct CompareValueOnly2
{
    bool operator()(const pair<int64_t, int>& t1,
                    const pair<int64_t, int>& t2) const
    {
        return t1.first < t2.first;
    }
};

int CountFortunastakesAboveProtocol(int protocolVersion)
{
    int i = 0;
    LOCK(cs_fortunastakes);
    BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes) {
        if(mn.protocolVersion < protocolVersion) continue;
        i++;
    }

    return i;
}


int GetFortunastakeByVin(CTxIn& vin)
{
    int i = 0;
    LOCK(cs_fortunastakes);
    BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes) {
        if (mn.vin == vin) return i;
        i++;
    }

    return -1;
}

int GetCurrentFortunaStake(int mod, int64_t nBlockHeight, int minProtocol)
{
    int i = 0;
    unsigned int score = 0;
    int winner = -1;
    LOCK(cs_fortunastakes);
    // scan for winner
    BOOST_FOREACH(CFortunaStake mn, vecFortunastakes) {
        mn.Check();
        if(mn.protocolVersion < minProtocol) continue;
        if(!mn.IsEnabled()) {
            i++;
            continue;
        }

        // calculate the score for each fortunastake
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        unsigned int n2 = 0;
        memcpy(&n2, &n, sizeof(n2));

        // determine the winner
        if(n2 > score){
            score = n2;
            winner = i;
        }
        i++;
    }

    return winner;
}
bool GetFortunastakeRanks()
{
    if (fortunastakePayments.vecFortunastakeRanksLastUpdated == pindexBest->nHeight)
        return true;

    // std::vector<pair<int, CFortunaStake*> > vecFortunastakeScores;

    vecFortunastakeScores.clear();

    BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes) {

        mn.Check();
        if(mn.protocolVersion < MIN_MN_PROTO_VERSION) continue;

        int value = -1;
        CBlockIndex* pindex = pindexBest;
        int payments = mn.UpdateLastPaidAmounts(pindex, max(MASTERNODE_FAIR_PAYMENT_MINIMUM, (int)mnCount) * MASTERNODE_FAIR_PAYMENT_ROUNDS, value); // do a search back 1000 blocks when receiving a new fortunastake to find their last payment, payments = number of payments received, value = amount

        vecFortunastakeScores.push_back(make_pair(value, &mn));
    }

    sort(vecFortunastakeScores.rbegin(), vecFortunastakeScores.rend(), CompareLastPayRate());

    fortunastakePayments.vecFortunastakeRanksLastUpdated = pindexBest->nHeight;
    return true;
}

int GetFortunastakeRank(CFortunaStake &tmn, int64_t nBlockHeight, int minProtocol)
{
    LOCK(cs_fortunastakes);
    GetFortunastakeRanks();
    unsigned int i = 0;

    BOOST_FOREACH(PAIRTYPE(int, CFortunaStake*)& s, vecFortunastakeScores)
    {
        i++;
        if (s.second->vin == tmn.vin)
            return i;
    }
}

int GetFortunastakeByRank(int findRank, int64_t nBlockHeight, int minProtocol)
{
    LOCK(cs_fortunastakes);
    GetFortunastakeRanks();
    unsigned int i = 0;

    BOOST_FOREACH(PAIRTYPE(int, CFortunaStake*)& s, vecFortunastakeScores)
    {
        i++;
        if (i == findRank)
            return s.first;
    }
}

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (pindexBest == NULL) return false;

    if(nBlockHeight == 0)
        nBlockHeight = pindexBest->nHeight;

    if(mapCacheBlockHashes.count(nBlockHeight)){
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex *BlockLastSolved = pindexBest;
    const CBlockIndex *BlockReading = pindexBest;

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || pindexBest->nHeight+1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if(nBlockHeight > 0) nBlocksAgo = (pindexBest->nHeight+1)-nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if(n >= nBlocksAgo){
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

bool CFortunaStake::GetPaymentInfo(const CBlockIndex *pindex, int64_t &totalValue, double &actualRate)
{
    int scanBack = max(MASTERNODE_FAIR_PAYMENT_MINIMUM, (int)mnCount) * MASTERNODE_FAIR_PAYMENT_ROUNDS;
    double requiredRate = scanBack / (int)mnCount;
    int actualPayments = GetPaymentAmount(pindex, scanBack, totalValue);
    actualRate = actualPayments / requiredRate;
    // TODO: stop payment if fortunastake vin age is under mnCount*30 old
    if (actualRate > 0) return true;
}

float CFortunaStake::GetPaymentRate(const CBlockIndex *pindex)
{
    int scanBack = max(MASTERNODE_FAIR_PAYMENT_MINIMUM, (int)mnCount) * MASTERNODE_FAIR_PAYMENT_ROUNDS;
    double requiredRate = scanBack / (int)mnCount;
    int64_t totalValue;
    int actualPayments = GetPaymentAmount(pindex, scanBack, totalValue);
    float actualRate = actualPayments/requiredRate;
    return actualRate;
}

int CFortunaStake::SetPayRate(int nHeight)
{
     int scanBack = max(MASTERNODE_FAIR_PAYMENT_MINIMUM, (int)mnCount) * MASTERNODE_FAIR_PAYMENT_ROUNDS;
     if (nHeight > pindexBest->nHeight) {
         scanBack += nHeight - pindexBest->nHeight;
     } // if going past current height, add to scan back height to account for how far it is - e.g. 200 in front will get 200 more blocks to smooth it out

     if (payData.size()>0) {
         // printf("Using fortunastake cached payments data for pay rate");
         // printf(" (payInfo:%d@%f)...", payCount, payRate);
         int64_t amount = 0;
         int matches = 0;
         BOOST_FOREACH(PAIRTYPE(int, int64_t) &item, payData)
         {
             if (item.first > nHeight - scanBack) { // find payments in last scanrange
                amount += item.second;
                matches++;
             }
         }
         if (matches > 0) {
             payCount = matches;
             payValue = amount;
             // set the node's current 'reward rate' - pay value divided by rounds (3)
             payRate = ((double)(payValue / COIN) / MASTERNODE_FAIR_PAYMENT_ROUNDS)*100;
             // printf("%d found with %s value %.2f rate\n", matches, FormatMoney(amount).c_str(), payRate);
             return matches;
         }
     }
}

int CFortunaStake::GetPaymentAmount(const CBlockIndex *pindex, int nMaxBlocksToScanBack, int64_t &totalValue)
{
    if(!pindex) return 0;
    CScript mnpayee = GetScriptForDestination(pubkey.GetID());
    CTxDestination address1;
    ExtractDestination(mnpayee, address1);
    CBitcoinAddress address2(address1);
    totalValue = 0;
    if (payData.size()>0) {
        //printf("Using fortunastake cached payments data");
        //printf("(payInfo:%d@%f)...", payCount, payRate);
        int64_t amount = 0;
        int matches = 0;
        BOOST_FOREACH(PAIRTYPE(int, int64_t) &item, payData)
        {
            amount += item.second;
            matches++;
        }
        //printf("done checking for matches: %d found with %s value\n", matches, FormatMoney(amount).c_str());
        if (matches > 0) {
            totalValue = amount;
            return totalValue / COIN;
        }
    }
    const CBlockIndex *BlockReading = pindex;

    int blocksFound = 0;
    totalValue = 0;
    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
            CBlock block;
            if(!block.ReadFromDisk(BlockReading, true)) // shouldn't really happen
                continue;

            if (block.IsProofOfWork())
            {
                BOOST_FOREACH(CTxOut txout, block.vtx[0].vout)
                    if(mnpayee == txout.scriptPubKey) {
                        blocksFound++;
                        totalValue += txout.nValue / COIN;
                    }
            } else if (block.IsProofOfStake())
            {
                BOOST_FOREACH(CTxOut txout, block.vtx[1].vout)
                    if(mnpayee == txout.scriptPubKey) {
                        blocksFound++;
                        totalValue += txout.nValue / COIN;
                    }
            }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }

        BlockReading = BlockReading->pprev;
    }
    return totalValue;

}

int CFortunaStake::UpdateLastPaidAmounts(const CBlockIndex *pindex, int nMaxBlocksToScanBack, int &value)
{
    if(!pindex) return 0;

    const CBlockIndex *BlockReading = pindex;
    int scanBack = max(MASTERNODE_FAIR_PAYMENT_MINIMUM, (int)mnCount) * MASTERNODE_FAIR_PAYMENT_ROUNDS;

    CScript mnpayee = GetScriptForDestination(pubkey.GetID());
    CTxDestination address1;
    ExtractDestination(mnpayee, address1);
    CBitcoinAddress address2(address1);
    int rewardCount = 0;
    int64_t rewardValue = 0;
    int64_t val = 0;
    value = 0;

    // reset counts
    payCount = 0;
    payValue = 0;
    payData.clear();

    LOCK(cs_fortunastakes);
    for (int i = 0; i < scanBack; i++) {
            val = 0;
            CBlock block;
            if(!block.ReadFromDisk(BlockReading, true)) // shouldn't really happen
                continue;

            // if it's a legit block, then count it against this node and record it in a vector
            if (block.IsProofOfWork() || block.IsProofOfStake())
            {
                BOOST_FOREACH(CTxOut txout, block.vtx[block.IsProofOfWork() ? 0 : 1].vout)
                {
                    if(mnpayee == txout.scriptPubKey) {
                        int height = BlockReading->nHeight;
                        if (rewardCount == 0) {
                            nBlockLastPaid = height;
                            nTimeLastPaid = BlockReading->nTime;
                        }
                        // values
                        val = txout.nValue;

                        // add this profit & the reward itself
                        rewardValue += val;
                        rewardCount++;

                        // make a note in the node for later lookup
                        payData.push_back(make_pair(height, val));
                    }
                }
            }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }

        BlockReading = BlockReading->pprev;
    }

    if (rewardCount > 0)
    {
        // return the count and value
        value = rewardValue / COIN;
        payCount = rewardCount;
        payValue = rewardValue;

        // set the node's current 'reward rate' - pay value divided by rounds (3)
        payRate = ((double)(payValue / COIN) / MASTERNODE_FAIR_PAYMENT_ROUNDS)*100;

        if (fDebug) printf("CFortunastake::UpdateLastPaidAmounts -- MN %s in last %d blocks was paid %d times for %s D, rate:%.2f count:%d val:%s\n", address2.ToString().c_str(), scanBack, rewardCount, FormatMoney(rewardValue).c_str(), payRate, payCount, FormatMoney(payValue).c_str());

        return rewardCount;
    } else {
        payCount = rewardCount;
        payValue = rewardValue;
        payRate = 0;
        value = 0;
        return 0;
    }

}

void CFortunaStake::UpdateLastPaidBlock(const CBlockIndex *pindex, int nMaxBlocksToScanBack)
{
    if(!pindex) return;

    const CBlockIndex *BlockReading = pindex;

    CScript mnpayee = GetScriptForDestination(pubkey.GetID());
    CTxDestination address1;
    ExtractDestination(mnpayee, address1);
    CBitcoinAddress address2(address1);
    uint64_t nCoinAge;

    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
            CBlock block;
            if(!block.ReadFromDisk(BlockReading, true)) // shouldn't really happen
                continue;

    /*  no amount checking for now
     * TODO: Scan block for payments to calculate reward
            // Calculate Coin Age for Fortunastake Reward Calculation
            if (!block.vtx[1].GetCoinAge(txdb, nCoinAge))
                return error("CheckBlock-POS : %s unable to get coin age for coinstake, Can't Calculate Fortunastake Reward\n", block.vtx[1].GetHash().ToString().substr(0,10).c_str());
            int64_t nCalculatedStakeReward = GetProofOfStakeReward(nCoinAge, nFees);
            int64_t nFortunastakePayment = GetFortunastakePayment(BlockReading->nHeight, block.IsProofOfStake() ? nCalculatedStakeReward : block.vtx[0].GetValueOut());
    */
            if (block.IsProofOfWork())
            {
                // TODO HERE: Scan the block for fortunastake payment amount
                BOOST_FOREACH(CTxOut txout, block.vtx[0].vout)
                    if(mnpayee == txout.scriptPubKey) {
                        nBlockLastPaid = BlockReading->nHeight;
                        nTimeLastPaid = BlockReading->nTime;
                        int lastPay = pindexBest->nHeight - nBlockLastPaid;
                        int value = txout.nValue;
                        // TODO HERE: Check the nValue for the fortunastake payment amount
                        if (fDebug) printf("CFortunaStake::UpdateLastPaidBlock -- searching for block with payment to %s -- found pow %d (%d blocks ago)\n", address2.ToString().c_str(), nBlockLastPaid, lastPay);
                        return;
                    }
            } else if (block.IsProofOfStake())
            {
                // TODO HERE: Scan the block for fortunastake payment amount
                BOOST_FOREACH(CTxOut txout, block.vtx[1].vout)
                    if(mnpayee == txout.scriptPubKey) {
                        nBlockLastPaid = BlockReading->nHeight;
                        nTimeLastPaid = BlockReading->nTime;
                        int lastPay = pindexBest->nHeight - nBlockLastPaid;
                        int value = txout.nValue;
                        // TODO HERE: Check the nValue for the fortunastake payment amount
                        if (fDebug) printf("CFortunaStake::UpdateLastPaidBlock -- searching for block with payment to %s -- found pos %d (%d blocks ago)\n", address2.ToString().c_str(), nBlockLastPaid, lastPay);
                        return;
                    }
            }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }

        BlockReading = BlockReading->pprev;
    }
    if (!nBlockLastPaid)
    {
        if (fDebug) printf("CFortunastake::UpdateLastPaidBlock -- searching for block with payment to %s e.g. %s -- NOT FOUND\n", vin.prevout.ToString().c_str(),address2.ToString().c_str());
        nBlockLastPaid = 1;
    }
}

//
// Deterministically calculate a given "score" for a fortunastake depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CFortunaStake::CalculateScore(int mod, int64_t nBlockHeight)
{
    if(pindexBest == NULL) return 0;

    uint256 hash = 0;
    uint256 aux = vin.prevout.hash + vin.prevout.n;

    if(!GetBlockHash(hash, nBlockHeight)) return 0;

    uint256 hash2 = Tribus(BEGIN(hash), END(hash)); //Tribus Algo Integrated, WIP
    uint256 hash3 = Tribus(BEGIN(hash), END(aux));

    uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

    return r;
}

void CFortunaStake::Check(bool forceCheck)
{
    if(!forceCheck && (GetTime() - lastTimeChecked < MASTERNODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();

    //once spent, stop doing the checks
    if(enabled==3) return;


    if(!UpdatedWithin(MASTERNODE_REMOVAL_SECONDS)){
        enabled = 4;
        return;
    }

    if(!UpdatedWithin(MASTERNODE_EXPIRATION_SECONDS)){
        enabled = 2;
        return;
    }

    if(!unitTest){
        std::string vinError;
        if(!CheckFortunastakeVin(vin,vinError)) {
                enabled = 3; //MN input was spent, disable checks for this MN
                if (fDebug) printf("error checking fortunastake %s: %s\n", vin.prevout.ToString().c_str(), vinError.c_str());
                return;
            }
        }

    enabled = 1; // OK
}

bool CheckFortunastakeVin(CTxIn& vin, std::string& errorMessage) {
    CTxDB txdb("r");
    CTxIndex txindex;
    CTransaction ctx;
    uint256 hashBlock;

    if (!GetTransaction(vin.prevout.hash,ctx,hashBlock))
    {
        errorMessage = "could not find transaction";
        return false;
    }

    CTxOut vout = ctx.vout[vin.prevout.n];
    if (vout.nValue != GetMNCollateral()*COIN)
    {
        errorMessage = "specified vin was not a fortunastake capable transaction";
        return false;
    }

    if (txdb.ReadTxIndex(vin.prevout.hash, txindex))
    {
        if (txindex.vSpent[vin.prevout.n].nTxPos != 0) {
            errorMessage = "vin was spent";
            return false;
        }
        return true;
    } else {
        errorMessage = "specified vin transaction was not found in the txindex\n";
    }
    return false;
}

bool CFortunastakePayments::CheckSignature(CFortunastakePaymentWinner& winner)
{
    //note: need to investigate why this is failing
    std::string strMessage = winner.vin.ToString().c_str() + boost::lexical_cast<std::string>(winner.nBlockHeight) + winner.payee.ToString();
    std::string strPubKey = fTestNet? strTestPubKey : strMainPubKey;
    CPubKey pubkey(ParseHex(strPubKey));

    std::string errorMessage = "";
    if(!forTunaSigner.VerifyMessage(pubkey, winner.vchSig, strMessage, errorMessage)){
        return false;
    }

    return true;
}

bool CFortunastakePayments::Sign(CFortunastakePaymentWinner& winner)
{
    std::string strMessage = winner.vin.ToString().c_str() + boost::lexical_cast<std::string>(winner.nBlockHeight) + winner.payee.ToString();

    CKey key2;
    CPubKey pubkey2;
    std::string errorMessage = "";

    if(!forTunaSigner.SetKey(strMasterPrivKey, errorMessage, key2, pubkey2))
    {
        printf("CFortunastakePayments::Sign - ERROR: Invalid fortunastakeprivkey: '%s'\n", errorMessage.c_str());
        return false;
    }

    if(!forTunaSigner.SignMessage(strMessage, errorMessage, winner.vchSig, key2)) {
        printf("CFortunastakePayments::Sign - Sign message failed");
        return false;
    }

    if(!forTunaSigner.VerifyMessage(pubkey2, winner.vchSig, strMessage, errorMessage)) {
        printf("CFortunastakePayments::Sign - Verify message failed");
        return false;
    }

    return true;
}

uint64_t CFortunastakePayments::CalculateScore(uint256 blockHash, CTxIn& vin)
{
    uint256 n1 = blockHash;
    uint256 n2 = Tribus(BEGIN(n1), END(n1));
    uint256 n3 = Tribus(BEGIN(vin.prevout.hash), END(vin.prevout.hash));
    uint256 n4 = n3 > n2 ? (n3 - n2) : (n2 - n3);

    //printf(" -- CFortunastakePayments CalculateScore() n2 = %d \n", n2.Get64());
    //printf(" -- CFortunastakePayments CalculateScore() n3 = %d \n", n3.Get64());
    //printf(" -- CFortunastakePayments CalculateScore() n4 = %d \n", n4.Get64());

    return n4.Get64();
}

bool CFortunastakePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    BOOST_FOREACH(CFortunastakePaymentWinner& winner, vWinning){
        if(winner.nBlockHeight == nBlockHeight) {
            CTransaction tx;
            uint256 hash;
            if(GetTransaction(winner.vin.prevout.hash, tx, hash)){
                BOOST_FOREACH(CTxOut out, tx.vout){
                    if(out.nValue == GetMNCollateral()*COIN){
                        payee = out.scriptPubKey;
                        return true;
                    }
                }
            }
            return false;
        }
    }
    return false;
}

bool CFortunastakePayments::GetWinningFortunastake(int nBlockHeight, CTxIn& vinOut)
{
    BOOST_FOREACH(CFortunastakePaymentWinner& winner, vWinning){
        if(winner.nBlockHeight == nBlockHeight) {
            vinOut = winner.vin;
            return true;
        }
    }

    return false;
}

bool CFortunastakePayments::AddWinningFortunastake(CFortunastakePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if(!GetBlockHash(blockHash, winnerIn.nBlockHeight-576)) {
        return false;
    }

    winnerIn.score = CalculateScore(blockHash, winnerIn.vin);

    bool foundBlock = false;
    BOOST_FOREACH(CFortunastakePaymentWinner& winner, vWinning){
        if(winner.nBlockHeight == winnerIn.nBlockHeight) {
            foundBlock = true;
            if(winner.score < winnerIn.score){
                winner.score = winnerIn.score;
                winner.vin = winnerIn.vin;
                winner.payee = winnerIn.payee;
                winner.vchSig = winnerIn.vchSig;

                return true;
            }
        }
    }

    // if it's not in the vector
    if(!foundBlock){
        vWinning.push_back(winnerIn);
        mapSeenFortunastakeVotes.insert(make_pair(winnerIn.GetHash(), winnerIn));

        return true;
    }

    return false;
}

void CFortunastakePayments::CleanPaymentList()
{
    LOCK(cs_fortunastakes);
    if(pindexBest == NULL) return;

    int nLimit = std::max(((int)vecFortunastakes.size())*2, 5000);

    vector<CFortunastakePaymentWinner>::iterator it;
    for(it=vWinning.begin();it<vWinning.end();it++){
        if(pindexBest->nHeight - (*it).nBlockHeight > nLimit){
            if(fDebug) printf("CFortunastakePayments::CleanPaymentList - Removing old fortunastake payment - block %d\n", (*it).nBlockHeight);
            vWinning.erase(it);
            break;
        }
    }
}

int CFortunastakePayments::LastPayment(CFortunaStake& mn)
{
    if(pindexBest == NULL) return 0;

    int ret = mn.GetFortunastakeInputAge();

    BOOST_FOREACH(CFortunastakePaymentWinner& winner, vWinning){
        if(winner.vin == mn.vin && pindexBest->nHeight - winner.nBlockHeight < ret)
            ret = pindexBest->nHeight - winner.nBlockHeight;
    }

    return ret;
}

bool CFortunastakePayments::ProcessBlock(int nBlockHeight)
{
    LOCK(cs_fortunastakes);
    if(!enabled) return false; // don't process blocks for fortunastake winners if we aren't signing a winner list
    CFortunastakePaymentWinner winner;

    std::vector<CTxIn> vecLastPayments;
    int c = 0;
    BOOST_REVERSE_FOREACH(CFortunastakePaymentWinner& winner, vWinning){
        vecLastPayments.push_back(winner.vin);
        //if we have one full payment cycle, break
        if(++c > (int)vecFortunastakes.size()) break;
    }

    std::random_shuffle ( vecFortunastakes.begin(), vecFortunastakes.end() );
    BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes) {
        bool found = false;
        BOOST_FOREACH(CTxIn& vin, vecLastPayments)
            if(mn.vin == vin) found = true;

        if(found) continue;

        mn.Check();
        if(!mn.IsEnabled()) {
            continue;
        }

        winner.score = 0;
        winner.nBlockHeight = nBlockHeight;
        winner.vin = mn.vin;
        winner.payee =GetScriptForDestination(mn.pubkey.GetID());

        break;
    }

    //if we can't find someone to get paid, pick randomly
    if(winner.nBlockHeight == 0 && vecFortunastakes.size() > 0) {
        winner.score = 0;
        winner.nBlockHeight = nBlockHeight;
        winner.vin = vecFortunastakes[0].vin;
        winner.payee =GetScriptForDestination(vecFortunastakes[0].pubkey.GetID());
    }


    if(CFortunastakePayments::enabled && Sign(winner)){
        if(AddWinningFortunastake(winner)){
            Relay(winner);
            return true;
        }
    }

    return false;
}

void CFortunastakePayments::Relay(CFortunastakePaymentWinner& winner)
{
    CInv inv(MSG_MASTERNODE_WINNER, winner.GetHash());

    vector<CInv> vInv;
    vInv.push_back(inv);
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes){
        pnode->PushMessage("inv", vInv);
    }
}

void CFortunastakePayments::Sync(CNode* node)
{
    int a = 0;
    BOOST_FOREACH(CFortunastakePaymentWinner& winner, vWinning)
        if(winner.nBlockHeight >= pindexBest->nHeight-10 && winner.nBlockHeight <= pindexBest->nHeight + 20)
            node->PushMessage("mnw", winner, a);
}


bool CFortunastakePayments::SetPrivKey(std::string strPrivKey)
{
    CFortunastakePaymentWinner winner;

    // Test signing successful, proceed
    strMasterPrivKey = strPrivKey;

    Sign(winner);

    if(CheckSignature(winner)){
        printf("CFortunastakePayments::SetPrivKey - Successfully initialized as fortunastake payments master\n");
        enabled = true;
        return true;
    } else {
        return false;
    }
}
