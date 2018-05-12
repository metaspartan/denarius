// Copyright (c) 2017-2018 The Denarius developers
// Copyright (c) 2009-2012 The Darkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODE_H
#define MASTERNODE_H

#include "uint256.h"
#include "uint256.h"
#include "sync.h"
#include "net.h"
#include "key.h"
#include "util.h"
#include "base58.h"
#include "hashblock.h"
#include "main.h"
#include "script.h"

class CMasterNode;
class CMasternodePayments;
class uint256;

#define MASTERNODE_NOT_PROCESSED               0 // initial state
#define MASTERNODE_IS_CAPABLE                  1
#define MASTERNODE_NOT_CAPABLE                 2
#define MASTERNODE_STOPPED                     3
#define MASTERNODE_INPUT_TOO_NEW               4
#define MASTERNODE_PORT_NOT_OPEN               6
#define MASTERNODE_PORT_OPEN                   7
#define MASTERNODE_SYNC_IN_PROCESS             8
#define MASTERNODE_REMOTELY_ENABLED            9

#define MASTERNODE_MIN_CONFIRMATIONS           15
#define MASTERNODE_MIN_DSEEP_SECONDS           (10*60)
#define MASTERNODE_MIN_DSEE_SECONDS            (5*60)
#define MASTERNODE_PING_SECONDS                (1*60)
#define MASTERNODE_EXPIRATION_SECONDS          (120*60)
#define MASTERNODE_REMOVAL_SECONDS             (130*60)
#define MASTERNODE_CHECK_SECONDS               10

using namespace std;

class CMasternodePaymentWinner;

extern CCriticalSection cs_masternodes;
extern std::vector<CMasterNode> vecMasternodes;
extern std::vector<pair<int, CMasterNode*> > vecMasternodeScores;
extern std::vector<pair<int, CMasterNode> > vecMasternodeRanks;
extern CMasternodePayments masternodePayments;
extern std::vector<CTxIn> vecMasternodeAskedFor;
extern map<uint256, CMasternodePaymentWinner> mapSeenMasternodeVotes;
extern map<int64_t, uint256> mapCacheBlockHashes;
extern int mnCount;



// manage the masternode connections
void ProcessMasternodeConnections();
int CountMasternodesAboveProtocol(int protocolVersion);


void ProcessMessageMasternode(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool CheckMasternodeVin(CTxIn& vin, std::string& errorMessage);

//
// The Masternode Class. For managing the darksend process. It contains the input of the 5000 DNR, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CMasterNode
{
public:
	static int minProtoVersion;
    CService addr;
    CTxIn vin;
    int64_t lastTimeSeen;
    CPubKey pubkey;
    CPubKey pubkey2;
    std::vector<unsigned char> sig;
    int64_t now; //dsee message times
    int64_t lastDseep;
    int cacheInputAge;
    int cacheInputAgeBlock;
    int enabled;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int64_t lastTimeChecked;
    int nBlockLastPaid;
    int64_t nTimeLastChecked;
    int64_t nTimeLastPaid;


    //the dsq count from the last dsq broadcast of this node
    int64_t nLastDsq;
    CMasterNode(CService newAddr, CTxIn newVin, CPubKey newPubkey, std::vector<unsigned char> newSig, int64_t newNow, CPubKey newPubkey2, int protocolVersionIn)
    {
        addr = newAddr;
        vin = newVin;
        pubkey = newPubkey;
        pubkey2 = newPubkey2;
        sig = newSig;
        now = newNow;
        enabled = 1;
        lastTimeSeen = 0;
        unitTest = false;
        cacheInputAge = 0;
        cacheInputAgeBlock = 0;
        nLastDsq = 0;
        lastDseep = 0;
        allowFreeTx = true;
        protocolVersion = protocolVersionIn;
        lastTimeChecked = 0;
        nBlockLastPaid = 0;
        nTimeLastChecked = 0;
        nTimeLastPaid = 0;
    }

    uint256 CalculateScore(int mod=1, int64_t nBlockHeight=0);

    void UpdateLastPaidBlock(const CBlockIndex *pindex, int nMaxBlocksToScanBack);

    void UpdateLastSeen(int64_t override=0)
    {
        if(override == 0){
            lastTimeSeen = GetAdjustedTime();
        } else {
            lastTimeSeen = override;
        }
    }

    inline uint64_t SliceHash(uint256& hash, int slice)
    {
        uint64_t n = 0;
        memcpy(&n, &hash+slice*64, 64);
        return n;
    }

    void Check(bool forceCheck=false);

    bool UpdatedWithin(int seconds)
    {
        // printf("UpdatedWithin %d, %d --  %d \n", GetAdjustedTime() , lastTimeSeen, (GetAdjustedTime() - lastTimeSeen) < seconds);

        return (GetAdjustedTime() - lastTimeSeen) < seconds;
    }

    void Disable()
    {
        lastTimeSeen = 0;
    }

    bool IsEnabled()
    {
        return enabled == 1;
    }

    int GetMasternodeInputAge()
    {
        if(pindexBest == NULL) return 0;

        if(cacheInputAge == 0){
            cacheInputAge = GetInputAge(vin);
            cacheInputAgeBlock = pindexBest->nHeight;
        }

        return cacheInputAge+(pindexBest->nHeight-cacheInputAgeBlock);
    }
};



// Get the current winner for this block
int GetCurrentMasterNode(int mod=1, int64_t nBlockHeight=0, int minProtocol=CMasterNode::minProtoVersion);

int GetMasternodeByVin(CTxIn& vin);
int GetMasternodeRank(CMasterNode& tmn, int64_t nBlockHeight=0, int minProtocol=CMasterNode::minProtoVersion);
int GetMasternodeByRank(int findRank, int64_t nBlockHeight=0, int minProtocol=CMasterNode::minProtoVersion);
bool GetMasternodeRanks();

// for storing the winning payments
class CMasternodePaymentWinner
{
public:
    int nBlockHeight;
    CTxIn vin;
    CScript payee;
    std::vector<unsigned char> vchSig;
    uint64_t score;

    CMasternodePaymentWinner() {
        nBlockHeight = 0;
        score = 0;
        vin = CTxIn();
        payee = CScript();
    }

    uint256 GetHash(){
        uint256 n2 = Tribus(BEGIN(nBlockHeight), END(nBlockHeight));
        uint256 n3 = vin.prevout.hash > n2 ? (vin.prevout.hash - n2) : (n2 - vin.prevout.hash);

        return n3;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion){
	unsigned int nSerSize = 0;
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vin);
        READWRITE(score);
        READWRITE(vchSig);
     }
};

inline bool operator==(const CMasterNode& a, const CMasterNode& b)
{
    return a.vin == b.vin;
}
inline bool operator!=(const CMasterNode& a, const CMasterNode& b)
{
    return !(a.vin == b.vin);
}
inline bool operator<(const CMasterNode& a, const CMasterNode& b)
{
    return (a.nBlockLastPaid < b.nBlockLastPaid);
}
inline bool operator>(const CMasterNode& a, const CMasterNode& b)
{
    return (a.nBlockLastPaid > b.nBlockLastPaid);
}

//
// Masternode Payments Class
// Keeps track of who should get paid for which blocks
//

class CMasternodePayments
{
private:
    std::vector<CMasternodePaymentWinner> vWinning;
    int nSyncedFromPeer;
    std::string strMasterPrivKey;
    std::string strTestPubKey;
    std::string strMainPubKey;
    bool enabled;

public:

    CMasternodePayments() {
        strMainPubKey = "04af2b6c63d5e5937266a4ce630ab6ced73a0f6a5ff5611ef9b5cfc4f9e264e4a8a4840ab4da4d3ded243ef9f80f114d335dad9a87a50431004b35c01b2c68ea49";
        strTestPubKey = "0406d6c9580d20c4daaacbade0f5bbe4448c511c5860f6dc27a1bf2a8c043b2ad27f3831f8e24750488f0c715100cc5a5811ffd578029f3af62633d9e1c51be384";
        enabled = false;
    }

    bool SetPrivKey(std::string strPrivKey);
    bool CheckSignature(CMasternodePaymentWinner& winner);
    bool Sign(CMasternodePaymentWinner& winner);

    // Deterministically calculate a given "score" for a masternode depending on how close it's hash is
    // to the blockHeight. The further away they are the better, the furthest will win the election
    // and get paid this block
    //

    int vecMasternodeRanksLastUpdated;
    uint64_t CalculateScore(uint256 blockHash, CTxIn& vin);
    bool GetWinningMasternode(int nBlockHeight, CTxIn& vinOut);
    bool AddWinningMasternode(CMasternodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);
    void Relay(CMasternodePaymentWinner& winner);
    void Sync(CNode* node);
    void CleanPaymentList();
    int LastPayment(CMasterNode& mn);

    //slow
    bool GetBlockPayee(int nBlockHeight, CScript& payee);
};



#endif
