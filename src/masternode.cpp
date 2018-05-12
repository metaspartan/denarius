// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The DarkCoin developers
// Copyright (c) 2018 The Denarius developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "masternode.h"
#include "activemasternode.h"
#include "darksend.h"
#include "txdb.h"
#include "main.h"
#include "util.h"
#include "addrman.h"
#include "sync.h"
#include "core.h"
#include <boost/lexical_cast.hpp>

int CMasterNode::minProtoVersion = MIN_MN_PROTO_VERSION;

CCriticalSection cs_masternodes;

/** The list of active masternodes */
std::vector<CMasterNode> vecMasternodes;
std::vector<pair<int, CMasterNode*> > vecMasternodeScores;
std::vector<pair<int, CMasterNode> > vecMasternodeRanks;
/** Object for who's going to get paid on which blocks */
CMasternodePayments masternodePayments;
// keep track of masternode votes I've seen
map<uint256, CMasternodePaymentWinner> mapSeenMasternodeVotes;
// keep track of the scanning errors I've seen
map<uint256, int> mapSeenMasternodeScanningErrors;
// who's asked for the masternode list and the last time
std::map<CNetAddr, int64_t> askedForMasternodeList;
// which masternodes we've asked for
std::map<COutPoint, int64_t> askedForMasternodeListEntry;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;
CMedianFilter<int> mnMedianCount(10, 0);
int mnCount;

// manage the masternode connections
void ProcessMasternodeConnections(){
    return;
    LOCK(cs_vNodes);

    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        //if it's our masternode, let it be
        if(darkSendPool.submittedToMasternode == pnode->addr) continue;

        if(pnode->fDarkSendMaster){
            printf("Closing masternode connection %s \n", pnode->addr.ToString().c_str());
            pnode->CloseSocketDisconnect();
        }
    }
}

void ProcessMessageMasternode(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

    if (strCommand == "dsee") { //DarkSend Election Entry

        bool fIsInitialDownload = IsInitialBlockDownload();
        if(fIsInitialDownload) return;

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
        std::string strMessage;

        // 70047 and greater
        vRecv >> vin >> addr >> vchSig >> sigTime >> pubkey >> pubkey2 >> count >> current >> lastUpdated >> protocolVersion;

        // make sure signature isn't in the future (past is OK)
        if (sigTime > GetAdjustedTime() + 60 * 60) {
            printf("dsee - Signature rejected, too far into the future %s\n", vin.ToString().c_str());
            return;
        }

        bool isLocal = addr.IsRFC1918() || addr.IsLocal();
        //if(Params().MineBlocksOnDemand()) isLocal = false;

        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

        strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

        if(protocolVersion < MIN_MN_PROTO_VERSION) {
            printf("dsee - ignoring outdated masternode %s protocol version %d\n", vin.ToString().c_str(), protocolVersion);
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
        if(!darkSendSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage)){
            printf("dsee - Got bad masternode address signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if((fTestNet && addr.GetPort() != 19999) || (!fTestNet && addr.GetPort() != 9999)) return;

        //search existing masternode list, this is where we update existing masternodes with new dsee broadcasts
	      LOCK(cs_masternodes);
        BOOST_FOREACH(CMasterNode& mn, vecMasternodes) {
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

                        RelayDarkSendElectionEntry(vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion);
                    }
                }

                return;
            }
        }

        // make sure the vout that was signed is related to the transaction that spawned the masternode
        //  - this is expensive, so it's only done once per masternode
        if(!darkSendSigner.IsVinAssociatedWithPubkey(vin, pubkey)) {
            printf("dsee - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if(fDebug) printf("dsee - Got NEW masternode entry %s\n", addr.ToString().c_str());

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckDarkSendPool()
        std::string vinError;
        if(CheckMasternodeVin(vin,vinError)){
            if (fDebugNet) printf("dsee - Accepted input for masternode entry %i %i\n", count, current);

            if(GetInputAge(vin) < MASTERNODE_MIN_CONFIRMATIONS){
                if (fDebugNet) printf("dsee - Input must have least %d confirmations\n", MASTERNODE_MIN_CONFIRMATIONS);
                Misbehaving(pfrom->GetId(), 20);
                return;
            }

            // use this as a peer
            addrman.Add(CAddress(addr), pfrom->addr, 2*60*60);

            // add our masternode
            CMasterNode mn(addr, vin, pubkey, vchSig, sigTime, pubkey2, protocolVersion);
            mn.UpdateLastSeen(lastUpdated);
            CBlockIndex* pindex = pindexBest;
            mn.UpdateLastPaidBlock(pindex, 1000); // do a search back 1000 blocks when receiving a new masternode to find their last payment
            vecMasternodes.push_back(mn);

            // if it matches our masternodeprivkey, then we've been remotely activated
            if(pubkey2 == activeMasternode.pubKeyMasternode && protocolVersion == PROTOCOL_VERSION){
                activeMasternode.EnableHotColdMasterNode(vin, addr);
            }

            if(count == -1 && !isLocal)
                RelayDarkSendElectionEntry(vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion);
            if (count > 0) {
                mnMedianCount.input(count);
                mnCount = mnMedianCount.median();
            }

        } else {
            printf("dsee - Rejected masternode entry %s: %s\n", addr.ToString().c_str(),vinError.c_str());
        }
    }

    else if (strCommand == "dseep") { //DarkSend Election Entry Ping
        bool fIsInitialDownload = IsInitialBlockDownload();
        if(fIsInitialDownload) return;

        CTxIn vin;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        bool stop;
        vRecv >> vin >> vchSig >> sigTime >> stop;

        if (fDebug) printf("dseep - Received: vin: %s sigTime: %lld stop: %s\n", vin.ToString().c_str(), sigTime, stop ? "true" : "false");

        if (sigTime > GetAdjustedTime() + 60 * 60) {
            printf("dseep - Signature rejected, too far into the future %s\n", vin.ToString().c_str());
            return;
        }

        if (sigTime <= GetAdjustedTime() - 60 * 60) {
            printf("dseep - Signature rejected, too far into the past %s - %d %d \n", vin.ToString().c_str(), sigTime, GetAdjustedTime());
            return;
        }

        // see if we have this masternode
	      LOCK(cs_masternodes);
        BOOST_FOREACH(CMasterNode& mn, vecMasternodes) {
            if(mn.vin.prevout == vin.prevout) {
            	// printf("dseep - Found corresponding mn for vin: %s\n", vin.ToString().c_str());
            	// take this only if it's newer
                if(mn.lastDseep < sigTime){
                    std::string strMessage = mn.addr.ToString() + boost::lexical_cast<std::string>(sigTime) + boost::lexical_cast<std::string>(stop);

                    std::string errorMessage = "";
                    if(!darkSendSigner.VerifyMessage(mn.pubkey2, vchSig, strMessage, errorMessage)){
                        printf("dseep - Got bad masternode address signature %s \n", vin.ToString().c_str());
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
                        RelayDarkSendElectionEntryPing(vin, vchSig, sigTime, stop);
                    }
                }
                return;
            }
        }

        if (fDebugNet) printf("dseep - Couldn't find masternode entry %s\n", vin.ToString().c_str());

        std::map<COutPoint, int64_t>::iterator i = askedForMasternodeListEntry.find(vin.prevout);
        if (i != askedForMasternodeListEntry.end()){
            int64_t t = (*i).second;
            if (GetTime() < t) {
                // we've asked recently
                return;
            }
        }

        // ask for the dsee info once from the node that sent dseep

        if (fDebugNet) printf("dseep - Asking source node for missing entry %s\n", vin.ToString().c_str());
        pfrom->PushMessage("dseg", vin);
        int64_t askAgain = GetTime()+(60*60*24);
        askedForMasternodeListEntry[vin.prevout] = askAgain;

    } else if (strCommand == "dseg") { //Get masternode list or specific entry
        bool fIsInitialDownload = IsInitialBlockDownload();
        if(fIsInitialDownload) return;

        CTxIn vin;
        vRecv >> vin;

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            //Note tor peers show up as local proxied addrs
            //if(!pfrom->addr.IsRFC1918())//&& !Params().MineBlocksOnDemand())
            //{
              if(!pfrom->addr.IsRFC1918())
              {
                std::map<CNetAddr, int64_t>::iterator i = askedForMasternodeList.find(pfrom->addr);
                if (i != askedForMasternodeList.end())
                {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        //Misbehaving(pfrom->GetId(), 34);
                        //printf("dseg - peer already asked me for the list\n");
                        //return;
                        Misbehaving(pfrom->GetId(), 34);
                        printf("dseg - peer already asked me for the list\n");
                        return;
                    }
                }

                int64_t askAgain = GetTime()+(60*60*3);
                askedForMasternodeList[pfrom->addr] = askAgain;
            //}
              }
        } //else, asking for a specific node which is ok

	      LOCK(cs_masternodes);
        int count = vecMasternodes.size();
        int i = 0;

        BOOST_FOREACH(CMasterNode mn, vecMasternodes) {

            if(mn.addr.IsRFC1918()) continue; //local network

            if(vin == CTxIn()){
                mn.Check(true);
                if(mn.IsEnabled()) {
                    if(fDebug) printf("dseg - Sending masternode entry - %s \n", mn.addr.ToString().c_str());
                    pfrom->PushMessage("dsee", mn.vin, mn.addr, mn.sig, mn.now, mn.pubkey, mn.pubkey2, count, i, mn.lastTimeSeen, mn.protocolVersion);
                }
            } else if (vin == mn.vin) {
                if(fDebug) printf("dseg - Sending masternode entry - %s \n", mn.addr.ToString().c_str());
                pfrom->PushMessage("dsee", mn.vin, mn.addr, mn.sig, mn.now, mn.pubkey, mn.pubkey2, count, i, mn.lastTimeSeen, mn.protocolVersion);
                printf("dseg - Sent 1 masternode entries to %s\n", pfrom->addr.ToString().c_str());
                return;
            }
            i++;
        }

        printf("dseg - Sent %d masternode entries to %s\n", count, pfrom->addr.ToString().c_str());
    }

    else if (strCommand == "mnget") { //Masternode Payments Request Sync
        bool fIsInitialDownload = IsInitialBlockDownload();
        if(fIsInitialDownload) return;

        if(pfrom->HasFulfilledRequest("mnget")) {
            printf("mnget - peer already asked me for the list\n");
            Misbehaving(pfrom->GetId(), 20);
            return;
        }

        pfrom->FulfilledRequest("mnget");
        masternodePayments.Sync(pfrom);
        printf("mnget - Sent masternode winners to %s\n", pfrom->addr.ToString().c_str());
    }
    else if (strCommand == "mnw") { //Masternode Payments Declare Winner
        bool fIsInitialDownload = IsInitialBlockDownload();
        if(fIsInitialDownload) return;

        CMasternodePaymentWinner winner;
        int a = 0;
        vRecv >> winner >> a;

        if(pindexBest == NULL) return;

        uint256 hash = winner.GetHash();
        if(mapSeenMasternodeVotes.count(hash)) {
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

        if(!masternodePayments.CheckSignature(winner)){
            printf("mnw - invalid signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        mapSeenMasternodeVotes.insert(make_pair(hash, winner));

        if(masternodePayments.AddWinningMasternode(winner)){
            masternodePayments.Relay(winner);
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
    bool operator()(const pair<int, CMasterNode*>& t1,
                    const pair<int, CMasterNode*>& t2) const
    {
        return (t1.first != t2.first ? t1.first > t2.first : t1.second->CalculateScore(1, pindexBest->nHeight) > t1.second->CalculateScore(1, pindexBest->nHeight));
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

int CountMasternodesAboveProtocol(int protocolVersion)
{
    int i = 0;
    LOCK(cs_masternodes);
    BOOST_FOREACH(CMasterNode& mn, vecMasternodes) {
        if(mn.protocolVersion < protocolVersion) continue;
        i++;
    }

    return i;
}


int GetMasternodeByVin(CTxIn& vin)
{
    int i = 0;
    LOCK(cs_masternodes);
    BOOST_FOREACH(CMasterNode& mn, vecMasternodes) {
        if (mn.vin == vin) return i;
        i++;
    }

    return -1;
}

int GetCurrentMasterNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int i = 0;
    unsigned int score = 0;
    int winner = -1;
    LOCK(cs_masternodes);
    // scan for winner
    BOOST_FOREACH(CMasterNode mn, vecMasternodes) {
        mn.Check();
        if(mn.protocolVersion < minProtocol) continue;
        if(!mn.IsEnabled()) {
            i++;
            continue;
        }

        // calculate the score for each masternode
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
bool GetMasternodeRanks()
{
    if (masternodePayments.vecMasternodeRanksLastUpdated == pindexBest->nHeight)
        return true;

    // std::vector<pair<int, CMasterNode*> > vecMasternodeScores;

    vecMasternodeScores.clear();

    BOOST_FOREACH(CMasterNode& mn, vecMasternodes) {

        mn.Check();
        if(mn.protocolVersion < MIN_MN_PROTO_VERSION) continue;

        if (!mn.nBlockLastPaid || mn.nBlockLastPaid == 0)
        {
            CBlockIndex* pindex = pindexBest;
            mn.UpdateLastPaidBlock(pindex, 2880); // search back 1 day
        }
        vecMasternodeScores.push_back(make_pair(mn.nBlockLastPaid, &mn));
    }

    sort(vecMasternodeScores.rbegin(), vecMasternodeScores.rend(), CompareLastPaidBlock());

    masternodePayments.vecMasternodeRanksLastUpdated = pindexBest->nHeight;
    return true;
}

int GetMasternodeRank(CMasterNode &tmn, int64_t nBlockHeight, int minProtocol)
{
    LOCK(cs_masternodes);
    GetMasternodeRanks();
    unsigned int i = 0;

    BOOST_FOREACH(PAIRTYPE(int, CMasterNode*)& s, vecMasternodeScores)
    {
        i++;
        if (s.second->vin == tmn.vin)
            return i;
    }
}

int GetMasternodeByRank(int findRank, int64_t nBlockHeight, int minProtocol)
{
    LOCK(cs_masternodes);
    GetMasternodeRanks();
    unsigned int i = 0;

    BOOST_FOREACH(PAIRTYPE(int, CMasterNode*)& s, vecMasternodeScores)
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

void CMasterNode::UpdateLastPaidBlock(const CBlockIndex *pindex, int nMaxBlocksToScanBack)
{
    if(!pindex) return;

    const CBlockIndex *BlockReading = pindex;

    CScript mnpayee = GetScriptForDestination(pubkey.GetID());
    CTxDestination address1;
    ExtractDestination(mnpayee, address1);
    CBitcoinAddress address2(address1);

    // LogPrint("masternode", "CMasternode::UpdateLastPaidBlock -- searching for block with payment to %s\n", vin.prevout.ToStringShort());
    uint64_t nCoinAge;

    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
            CBlock block;
            if(!block.ReadFromDisk(BlockReading, true)) // shouldn't really happen
                continue;

    /*  no amount checking for now
     * TODO: Scan block for payments to calculate reward
            // Calculate Coin Age for Masternode Reward Calculation
            if (!block.vtx[1].GetCoinAge(txdb, nCoinAge))
                return error("CheckBlock-POS : %s unable to get coin age for coinstake, Can't Calculate Masternode Reward\n", block.vtx[1].GetHash().ToString().substr(0,10).c_str());
            int64_t nCalculatedStakeReward = GetProofOfStakeReward(nCoinAge, nFees);
            int64_t nMasternodePayment = GetMasternodePayment(BlockReading->nHeight, block.IsProofOfStake() ? nCalculatedStakeReward : block.vtx[0].GetValueOut());
    */
            if (block.IsProofOfWork())
            {
                // TODO HERE: Scan the block for masternode payment amount
                BOOST_FOREACH(CTxOut txout, block.vtx[0].vout)
                    if(mnpayee == txout.scriptPubKey) {
                        nBlockLastPaid = BlockReading->nHeight;
                        nTimeLastPaid = BlockReading->nTime;
                        int lastPay = pindexBest->nHeight - nBlockLastPaid;
                        // TODO HERE: Check the nValue for the masternode payment amount
                        printf("CMasterNode::UpdateLastPaidBlock -- searching for block with payment to %s -- found pow %d (%d blocks ago)\n", address2.ToString().c_str(), nBlockLastPaid, lastPay);
                        return;
                    }
            } else if (block.IsProofOfStake())
            {
                // TODO HERE: Scan the block for masternode payment amount
                BOOST_FOREACH(CTxOut txout, block.vtx[1].vout)
                    if(mnpayee == txout.scriptPubKey) {
                        nBlockLastPaid = BlockReading->nHeight;
                        nTimeLastPaid = BlockReading->nTime;
                        int lastPay = pindexBest->nHeight - nBlockLastPaid;
                        // TODO HERE: Check the nValue for the masternode payment amount
                        printf("CMasterNode::UpdateLastPaidBlock -- searching for block with payment to %s -- found pos %d (%d blocks ago)\n", address2.ToString().c_str(), nBlockLastPaid, lastPay);
                        return;
                    }
            }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }

        BlockReading = BlockReading->pprev;
    }
    if (!nBlockLastPaid)
    {
        printf("CMasternode::UpdateLastPaidBlock -- searching for block with payment to %s e.g. %s -- NOT FOUND\n", vin.prevout.ToString().c_str(),address2.ToString().c_str());
        nBlockLastPaid = 1;
    }
}

//
// Deterministically calculate a given "score" for a masternode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CMasterNode::CalculateScore(int mod, int64_t nBlockHeight)
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

void CMasterNode::Check(bool forceCheck)
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
        if(!CheckMasternodeVin(vin,vinError)) {
                enabled = 3; //MN input was spent, disable checks for this MN
                if (fDebug) printf("error checking masternode %s: %s\n", vin.prevout.ToString().c_str(), vinError.c_str());
                return;
            }
        }

    enabled = 1; // OK
}

bool CheckMasternodeVin(CTxIn& vin, std::string& errorMessage) {
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
        errorMessage = "specified vin was not a masternode capable transaction";
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

bool CMasternodePayments::CheckSignature(CMasternodePaymentWinner& winner)
{
    //note: need to investigate why this is failing
    std::string strMessage = winner.vin.ToString().c_str() + boost::lexical_cast<std::string>(winner.nBlockHeight) + winner.payee.ToString();
    std::string strPubKey = fTestNet? strTestPubKey : strMainPubKey;
    CPubKey pubkey(ParseHex(strPubKey));

    std::string errorMessage = "";
    if(!darkSendSigner.VerifyMessage(pubkey, winner.vchSig, strMessage, errorMessage)){
        return false;
    }

    return true;
}

bool CMasternodePayments::Sign(CMasternodePaymentWinner& winner)
{
    std::string strMessage = winner.vin.ToString().c_str() + boost::lexical_cast<std::string>(winner.nBlockHeight) + winner.payee.ToString();

    CKey key2;
    CPubKey pubkey2;
    std::string errorMessage = "";

    if(!darkSendSigner.SetKey(strMasterPrivKey, errorMessage, key2, pubkey2))
    {
        printf("CMasternodePayments::Sign - ERROR: Invalid masternodeprivkey: '%s'\n", errorMessage.c_str());
        return false;
    }

    if(!darkSendSigner.SignMessage(strMessage, errorMessage, winner.vchSig, key2)) {
        printf("CMasternodePayments::Sign - Sign message failed");
        return false;
    }

    if(!darkSendSigner.VerifyMessage(pubkey2, winner.vchSig, strMessage, errorMessage)) {
        printf("CMasternodePayments::Sign - Verify message failed");
        return false;
    }

    return true;
}

uint64_t CMasternodePayments::CalculateScore(uint256 blockHash, CTxIn& vin)
{
    uint256 n1 = blockHash;
    uint256 n2 = Tribus(BEGIN(n1), END(n1));
    uint256 n3 = Tribus(BEGIN(vin.prevout.hash), END(vin.prevout.hash));
    uint256 n4 = n3 > n2 ? (n3 - n2) : (n2 - n3);

    //printf(" -- CMasternodePayments CalculateScore() n2 = %d \n", n2.Get64());
    //printf(" -- CMasternodePayments CalculateScore() n3 = %d \n", n3.Get64());
    //printf(" -- CMasternodePayments CalculateScore() n4 = %d \n", n4.Get64());

    return n4.Get64();
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    BOOST_FOREACH(CMasternodePaymentWinner& winner, vWinning){
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

bool CMasternodePayments::GetWinningMasternode(int nBlockHeight, CTxIn& vinOut)
{
    BOOST_FOREACH(CMasternodePaymentWinner& winner, vWinning){
        if(winner.nBlockHeight == nBlockHeight) {
            vinOut = winner.vin;
            return true;
        }
    }

    return false;
}

bool CMasternodePayments::AddWinningMasternode(CMasternodePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if(!GetBlockHash(blockHash, winnerIn.nBlockHeight-576)) {
        return false;
    }

    winnerIn.score = CalculateScore(blockHash, winnerIn.vin);

    bool foundBlock = false;
    BOOST_FOREACH(CMasternodePaymentWinner& winner, vWinning){
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
        mapSeenMasternodeVotes.insert(make_pair(winnerIn.GetHash(), winnerIn));

        return true;
    }

    return false;
}

void CMasternodePayments::CleanPaymentList()
{
    LOCK(cs_masternodes);
    if(pindexBest == NULL) return;

    int nLimit = std::max(((int)vecMasternodes.size())*2, 5000);

    vector<CMasternodePaymentWinner>::iterator it;
    for(it=vWinning.begin();it<vWinning.end();it++){
        if(pindexBest->nHeight - (*it).nBlockHeight > nLimit){
            if(fDebug) printf("CMasternodePayments::CleanPaymentList - Removing old masternode payment - block %d\n", (*it).nBlockHeight);
            vWinning.erase(it);
            break;
        }
    }
}

int CMasternodePayments::LastPayment(CMasterNode& mn)
{
    if(pindexBest == NULL) return 0;

    int ret = mn.GetMasternodeInputAge();

    BOOST_FOREACH(CMasternodePaymentWinner& winner, vWinning){
        if(winner.vin == mn.vin && pindexBest->nHeight - winner.nBlockHeight < ret)
            ret = pindexBest->nHeight - winner.nBlockHeight;
    }

    return ret;
}

bool CMasternodePayments::ProcessBlock(int nBlockHeight)
{
    LOCK(cs_masternodes);
    if(!enabled) return false; // don't process blocks for masternode winners if we aren't signing a winner list
    CMasternodePaymentWinner winner;

    std::vector<CTxIn> vecLastPayments;
    int c = 0;
    BOOST_REVERSE_FOREACH(CMasternodePaymentWinner& winner, vWinning){
        vecLastPayments.push_back(winner.vin);
        //if we have one full payment cycle, break
        if(++c > (int)vecMasternodes.size()) break;
    }

    std::random_shuffle ( vecMasternodes.begin(), vecMasternodes.end() );
    BOOST_FOREACH(CMasterNode& mn, vecMasternodes) {
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
    if(winner.nBlockHeight == 0 && vecMasternodes.size() > 0) {
        winner.score = 0;
        winner.nBlockHeight = nBlockHeight;
        winner.vin = vecMasternodes[0].vin;
        winner.payee =GetScriptForDestination(vecMasternodes[0].pubkey.GetID());
    }


    if(CMasternodePayments::enabled && Sign(winner)){
        if(AddWinningMasternode(winner)){
            Relay(winner);
            return true;
        }
    }

    return false;
}

void CMasternodePayments::Relay(CMasternodePaymentWinner& winner)
{
    CInv inv(MSG_MASTERNODE_WINNER, winner.GetHash());

    vector<CInv> vInv;
    vInv.push_back(inv);
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes){
        pnode->PushMessage("inv", vInv);
    }
}

void CMasternodePayments::Sync(CNode* node)
{
    int a = 0;
    BOOST_FOREACH(CMasternodePaymentWinner& winner, vWinning)
        if(winner.nBlockHeight >= pindexBest->nHeight-10 && winner.nBlockHeight <= pindexBest->nHeight + 20)
            node->PushMessage("mnw", winner, a);
}


bool CMasternodePayments::SetPrivKey(std::string strPrivKey)
{
    CMasternodePaymentWinner winner;

    // Test signing successful, proceed
    strMasterPrivKey = strPrivKey;

    Sign(winner);

    if(CheckSignature(winner)){
        printf("CMasternodePayments::SetPrivKey - Successfully initialized as masternode payments master\n");
        enabled = true;
        return true;
    } else {
        return false;
    }
}
