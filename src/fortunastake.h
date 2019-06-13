// Copyright (c) 2017-2018 The Denarius developers
// Copyright (c) 2009-2012 The Darkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef FORTUNASTAKE_H
#define FORTUNASTAKE_H

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

class CFortunaStake;
class CFortunastakePayments;
class uint256;

#define FORTUNASTAKE_NOT_PROCESSED               0 // initial state
#define FORTUNASTAKE_IS_CAPABLE                  1
#define FORTUNASTAKE_NOT_CAPABLE                 2
#define FORTUNASTAKE_STOPPED                     3
#define FORTUNASTAKE_INPUT_TOO_NEW               4
#define FORTUNASTAKE_PORT_NOT_OPEN               6
#define FORTUNASTAKE_PORT_OPEN                   7
#define FORTUNASTAKE_SYNC_IN_PROCESS             8
#define FORTUNASTAKE_REMOTELY_ENABLED            9

#define FORTUNASTAKE_MIN_CONFIRMATIONS           15
#define FORTUNASTAKE_MIN_CONFIRMATIONS_NOPAY     500
#define FORTUNASTAKE_MIN_DSEEP_SECONDS           (10*60)
#define FORTUNASTAKE_MIN_DSEE_SECONDS            (5*60)
#define FORTUNASTAKE_PING_SECONDS                (1*60)
#define FORTUNASTAKE_EXPIRATION_SECONDS          (120*60)
#define FORTUNASTAKE_REMOVAL_SECONDS             (130*60)
#define FORTUNASTAKE_CHECK_SECONDS               10

#define FORTUNASTAKE_FAIR_PAYMENT_MINIMUM         200
#define FORTUNASTAKE_FAIR_PAYMENT_ROUNDS          3

#define FORTUNASTAKE_ACTIVETIME_FIXHEIGHT         2200000 // Still WIP

using namespace std;

class CFortunastakePaymentWinner;

extern CCriticalSection cs_fortunastakes;
extern std::vector<CFortunaStake> vecFortunastakes;
extern std::vector<pair<int, CFortunaStake*> > vecFortunastakeScores;
extern std::vector<pair<int, CFortunaStake> > vecFortunastakeRanks;
extern CFortunastakePayments fortunastakePayments;
extern std::vector<CTxIn> vecFortunastakeAskedFor;
extern map<uint256, CFortunastakePaymentWinner> mapSeenFortunastakeVotes;
extern map<int64_t, uint256> mapCacheBlockHashes;
extern unsigned int mnCount;


// manage the fortunastake connections
void ProcessFortunastakeConnections();
int CountFortunastakesAboveProtocol(int protocolVersion);


void ProcessMessageFortunastake(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool CheckFortunastakeVin(CTxIn& vin, std::string& errorMessage, CBlockIndex *pindex);

// For storing payData
class CFortunaPayData
{
public:
    int height;
    uint256 hash;
    int64_t amount;

    CFortunaPayData() {
        height = 0;
        hash = 0;
        amount = 0;
    }

};

class CFortunaCollateral
{
public:
    CTxIn vin;
    CScript scriptPubKey;
    int height;
    uint256 blockHash;

    CFortunaCollateral() {
        height = 0;
        blockHash = 0;
    }
};

// For storing payData
class CFortunaPayments
{
public:
    std::vector<CFortunaStake> vStakes; // this array should be sorted
    std::vector<CFortunaPayData> vPayments; // this array just contains our scanned data
    //std::vector<CTxIn> vCollaterals;
    std::vector<CFortunaCollateral> vCollaterals;
    std::vector<CScript> vScripts;

    CFortunaPayments() {
        // fill vStakes array with pointers to MN's from vecFortunastakes
    }

    bool add(CFortunaStake* mn)
    {
        // add address of pointer into the payments array
        return true;
    }

    bool remove(CFortunaStake* mn)
    {
        // remove address of pointer from the payments array
        return true;
    }

    void update(const CBlockIndex *pindex, bool force = false);

    bool initialize(const CBlockIndex* pindex);
};

//
// The Fortunastake Class. For managing the fortuna process. It contains the input of the 5000 D, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CFortunaStake
{
public:
	static int minProtoVersion;
    CService addr;
    CTxIn vin;
    int64_t lastTimeSeen;
    CPubKey pubkey;
    CPubKey pubkey2;
    std::vector<unsigned char> sig;
    std::vector<CFortunaPayData> payData;
    pair<int, int64_t> payInfo;
    int64_t payRate;
    int payCount;
    int64_t payValue;
    int64_t now; //dsee message times
    int64_t lastDseep;
    int cacheInputAge;
    int cacheInputAgeBlock;
    int enabled;
    string status;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int64_t lastTimeChecked;
    int nBlockLastPaid;
    int64_t nTimeLastChecked;
    int64_t nTimeRegistered;
    int nRank;


    //the dsq count from the last dsq broadcast of this node
    int64_t nLastDsq;
    CFortunaStake(CService newAddr, CTxIn newVin, CPubKey newPubkey, std::vector<unsigned char> newSig, int64_t newNow, CPubKey newPubkey2, int protocolVersionIn)
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
        nTimeRegistered = newNow;
        nRank = 0;
        payValue = 0;
        payRate = 0;
        payCount = 0;
    }

    uint256 CalculateScore(int mod=1, int64_t nBlockHeight=0);

    int SetPayRate(int nHeight);
    bool GetPaymentInfo(const CBlockIndex *pindex, int64_t &totalValue, double &actualRate);
    float GetPaymentRate(const CBlockIndex *pindex);
    int GetPaymentAmount(const CBlockIndex *pindex, int nMaxBlocksToScanBack, int64_t &totalValue);
    int UpdateLastPaidAmounts(const CBlockIndex *pindex, int nMaxBlocksToScanBack, int &value);
    void UpdateLastPaidBlock(const CBlockIndex *pindex, int nMaxBlocksToScanBack);

    void UpdateLastSeen(int64_t override=0)
    {
        if (override == 0) {
            lastTimeSeen = GetAdjustedTime();
        } else {
            lastTimeSeen = override;
        }
    }

    bool IsActive() {
        if (lastTimeSeen - now > (max(FORTUNASTAKE_FAIR_PAYMENT_MINIMUM, (int)mnCount) * 30))
        { // dsee broadcast is more than a round old, let's consider it active
                return true;
        }
        return false;
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

    int GetFortunastakeInputAge(CBlockIndex* pindex=pindexBest)
    {
        if(pindex == NULL) return 0;

        if(cacheInputAge == 0){
            cacheInputAge = GetInputAge(vin, pindex);
            cacheInputAgeBlock = pindex->nHeight;
        }

        return cacheInputAge+(pindex->nHeight-cacheInputAgeBlock);
    }
};



// Get the current winner for this block
int GetCurrentFortunaStake(int mod=1, int64_t nBlockHeight=0, int minProtocol=CFortunaStake::minProtoVersion);
bool CheckFSPayment(CBlockIndex* pindex, int64_t value, CFortunaStake &mn);
int64_t avg2(std::vector<CFortunaStake> const& v);
int GetFortunastakeByVin(CTxIn& vin);
int GetFortunastakeRank(CFortunaStake& tmn, CBlockIndex* pindex, int minProtocol=CFortunaStake::minProtoVersion);
int GetFortunastakeByRank(int findRank, int64_t nBlockHeight=0, int minProtocol=CFortunaStake::minProtoVersion);
bool GetFortunastakeRanks(CBlockIndex* pindex=pindexBest);
extern int64_t nAverageFSIncome;
bool FindFSPayment(CScript& payee, CBlockIndex* pindex=pindexBest);

// for storing the winning payments
class CFortunastakePaymentWinner
{
public:
    int nBlockHeight;
    CTxIn vin;
    CScript payee;
    std::vector<unsigned char> vchSig;
    uint64_t score;

    CFortunastakePaymentWinner() {
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

inline bool operator==(const CFortunaStake& a, const CFortunaStake& b)
{
    return a.vin == b.vin;
}
inline bool operator!=(const CFortunaStake& a, const CFortunaStake& b)
{
    return !(a.vin == b.vin);
}
inline bool operator<(const CFortunaStake& a, const CFortunaStake& b)
{
    return (a.nBlockLastPaid < b.nBlockLastPaid);
}
inline bool operator>(const CFortunaStake& a, const CFortunaStake& b)
{
    return (a.nBlockLastPaid > b.nBlockLastPaid);
}

//
// Fortunastake Payments Class
// Keeps track of who should get paid for which blocks
//

class CFortunastakePayments
{
private:
    std::vector<CFortunastakePaymentWinner> vWinning;
    int nSyncedFromPeer;
    std::string strMasterPrivKey;
    std::string strTestPubKey;
    std::string strMainPubKey;
    bool enabled;

public:

    CFortunastakePayments() {
        strMainPubKey = "04af2b6c63d5e5937266a4ce630ab6ced73a0f6a5ff5611ef9b5cfc4f9e264e4a8a4840ab4da4d3ded243ef9f80f114d335dad9a87a50431004b35c01b2c68ea49";
        strTestPubKey = "0406d6c9580d20c4daaacbade0f5bbe4448c511c5860f6dc27a1bf2a8c043b2ad27f3831f8e24750488f0c715100cc5a5811ffd578029f3af62633d9e1c51be384";
        enabled = false;
    }

    bool SetPrivKey(std::string strPrivKey);
    bool CheckSignature(CFortunastakePaymentWinner& winner);
    bool Sign(CFortunastakePaymentWinner& winner);

    // Deterministically calculate a given "score" for a fortunastake depending on how close it's hash is
    // to the blockHeight. The further away they are the better, the furthest will win the election
    // and get paid this block
    //

    uint256 vecFortunastakeRanksLastUpdated;
    uint64_t CalculateScore(uint256 blockHash, CTxIn& vin);
    bool GetWinningFortunastake(int nBlockHeight, CTxIn& vinOut);
    bool AddWinningFortunastake(CFortunastakePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);
    void Relay(CFortunastakePaymentWinner& winner);
    void Sync(CNode* node);
    void CleanPaymentList();
    int LastPayment(CFortunaStake& mn);

    //slow
    bool GetBlockPayee(int nBlockHeight, CScript& payee);
};



#endif
