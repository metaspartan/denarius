// Copyright (c) 2017 The Denarius developers
// Copyright (c) 2009-2012 The Darkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef FORTUNA_H
#define FORTUNA_H

#include "main.h"
#include "fortunastake.h"
#include "activefortunastake.h"

class CTxIn;
class CForTunaPool;
class CForTunaSigner;
class CFortunaStakeVote;
class CBitcoinAddress;
class CFortunaQueue;
class CFortunaBroadcastTx;
class CActiveFortunastake;

#define POOL_MAX_TRANSACTIONS                  3 // wait for X transactions to merge and publish
#define POOL_STATUS_UNKNOWN                    0 // waiting for update
#define POOL_STATUS_IDLE                       1 // waiting for update
#define POOL_STATUS_QUEUE                      2 // waiting in a queue
#define POOL_STATUS_ACCEPTING_ENTRIES          3 // accepting entries
#define POOL_STATUS_FINALIZE_TRANSACTION       4 // master node will broadcast what it accepted
#define POOL_STATUS_SIGNING                    5 // check inputs/outputs, sign final tx
#define POOL_STATUS_TRANSMISSION               6 // transmit transaction
#define POOL_STATUS_ERROR                      7 // error
#define POOL_STATUS_SUCCESS                    8 // success

// status update message constants
#define FORTUNASTAKE_ACCEPTED                    1
#define FORTUNASTAKE_REJECTED                    0
#define FORTUNASTAKE_RESET                       -1

#define FORTUNA_QUEUE_TIMEOUT                 120
#define FORTUNA_SIGNING_TIMEOUT               30

extern CForTunaPool forTunaPool;
extern CForTunaSigner forTunaSigner;
extern std::vector<CFortunaQueue> vecFortunaQueue;
extern std::string strFortunaStakePrivKey;
extern map<uint256, CFortunaBroadcastTx> mapFortunaBroadcastTxes;
extern CActiveFortunastake activeFortunastake;

//specific messages for the Fortuna protocol
void ProcessMessageFortuna(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

// get the fortuna chain depth for a given input
int GetInputFortunaRounds(CTxIn in, int rounds=0);


// An input in the fortuna pool
class CForTunaEntryVin
{
public:
    bool isSigSet;
    CTxIn vin;

    CForTunaEntryVin()
    {
        isSigSet = false;
        vin = CTxIn();
    }
};

// A clients transaction in the fortuna pool
class CForTunaEntry
{
public:
    bool isSet;
    std::vector<CForTunaEntryVin> sev;
    int64_t amount;
    CTransaction collateral;
    std::vector<CTxOut> vout;
    CTransaction txSupporting;
    int64_t addedTime;

    CForTunaEntry()
    {
        isSet = false;
        collateral = CTransaction();
        amount = 0;
    }

    bool Add(const std::vector<CTxIn> vinIn, int64_t amountIn, const CTransaction collateralIn, const std::vector<CTxOut> voutIn)
    {
        if(isSet){return false;}

        for (const CTxIn v : vinIn) {
            CForTunaEntryVin s = CForTunaEntryVin();
            s.vin = v;
            sev.push_back(s);
        }
        vout = voutIn;
        amount = amountIn;
        collateral = collateralIn;
        isSet = true;
        addedTime = GetTime();

        return true;
    }

    bool AddSig(const CTxIn& vin)
    {
        for (CForTunaEntryVin& s : sev) {
            if(s.vin.prevout == vin.prevout && s.vin.nSequence == vin.nSequence){
                if(s.isSigSet){return false;}
                s.vin.scriptSig = vin.scriptSig;
                s.vin.prevPubKey = vin.prevPubKey;
                s.isSigSet = true;

                return true;
            }
        }

        return false;
    }

    bool IsExpired()
    {
        return (GetTime() - addedTime) > FORTUNA_QUEUE_TIMEOUT;// 120 seconds
    }
};

//
// A currently inprogress fortuna merge and denomination information
//
class CFortunaQueue
{
public:
    CTxIn vin;
    int64_t time;
    int nDenom;
    bool ready; //ready for submit
    std::vector<unsigned char> vchSig;

    CFortunaQueue()
    {
        nDenom = 0;
        vin = CTxIn();
        time = 0;
        vchSig.clear();
        ready = false;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion){
	unsigned int nSerSize = 0;
        READWRITE(nDenom);
        READWRITE(vin);
        READWRITE(time);
        READWRITE(ready);
        READWRITE(vchSig);
    }

    bool GetAddress(CService &addr)
    {
        for (CFortunaStake mn : vecFortunastakes) {
            if(mn.vin == vin){
                addr = mn.addr;
                return true;
            }
        }
        return false;
    }

    bool GetProtocolVersion(int &protocolVersion)
    {
        for (CFortunaStake mn : vecFortunastakes) {
            if(mn.vin == vin){
                protocolVersion = mn.protocolVersion;
                return true;
            }
        }
        return false;
    }

    bool Sign();
    bool Relay();

    bool IsExpired()
    {
        return (GetTime() - time) > FORTUNA_QUEUE_TIMEOUT;// 120 seconds
    }

    bool CheckSignature();

};

// store fortuna tx signature information
class CFortunaBroadcastTx
{
public:
    CTransaction tx;
    CTxIn vin;
    vector<unsigned char> vchSig;
    int64_t sigTime;
};

//
// Helper object for signing and checking signatures
//
class CForTunaSigner
{
public:
    bool IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey);
    bool SetKey(std::string strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey);
    bool SignMessage(std::string strMessage, std::string& errorMessage, std::vector<unsigned char>& vchSig, CKey key);
    bool VerifyMessage(CPubKey pubkey, std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage);
};

class CFortunaSession
{

};

//
// Used to keep track of current status of fortuna pool
//
class CForTunaPool
{
public:
    static const int PROTOCOL_VERSION = 33933; //33933, D v3.3.9.8

    // clients entries
    std::vector<CForTunaEntry> myEntries;
    // fortunastake entries
    std::vector<CForTunaEntry> entries;
    // the finalized transaction ready for signing
    CTransaction finalTransaction;

    int64_t lastTimeChanged;
    int64_t lastAutoDenomination;

    unsigned int state;
    unsigned int entriesCount;
    unsigned int lastEntryAccepted;
    unsigned int countEntriesAccepted;

    // where collateral should be made out to
    CScript collateralPubKey;

    std::vector<CTxIn> lockedCoins;

    uint256 masterNodeBlockHash;

    std::string lastMessage;
    bool completedTransaction;
    bool unitTest;
    CService submittedToFortunastake;

    int sessionID;
    int sessionDenom; //Users must submit an denom matching this
    int sessionUsers; //N Users have said they'll join
    bool sessionFoundFortunastake; //If we've found a compatible fortunastake
    int64_t sessionTotalValue; //used for autoDenom
    std::vector<CTransaction> vecSessionCollateral;

    int cachedLastSuccess;
    int cachedNumBlocks; //used for the overview screen
    int minBlockSpacing; //required blocks between mixes
    CTransaction txCollateral;

    int64_t lastNewBlock;

    //debugging data
    std::string strAutoDenomResult;

    //incremented whenever a DSQ comes through
    int64_t nDsqCount;

    CForTunaPool()
    {
        /* ForTuna uses collateral addresses to trust parties entering the pool
            to behave themselves. If they don't it takes their money. */

        cachedLastSuccess = 0;
        cachedNumBlocks = 0;
        unitTest = false;
        txCollateral = CTransaction();
        minBlockSpacing = 1;
        nDsqCount = 0;
        lastNewBlock = 0;

        SetNull();
    }

    void InitCollateralAddress(){
        std::string strAddress = "";
            strAddress = "DQjK69r9hjKgJo3foVsqdErBzL1Rdmsswr";
        SetCollateralAddress(strAddress);
    }

    void SetMinBlockSpacing(int minBlockSpacingIn){
        minBlockSpacing = minBlockSpacingIn;
    }

    bool SetCollateralAddress(std::string strAddress);
    void Reset();
    void SetNull(bool clearEverything=false);

    void UnlockCoins();

    bool IsNull() const
    {
        return (state == POOL_STATUS_ACCEPTING_ENTRIES && entries.empty() && myEntries.empty());
    }

    int GetState() const
    {
        return state;
    }

    int GetEntriesCount() const
    {
        if(fFortunaStake){
            return entries.size();
        } else {
            return entriesCount;
        }
    }

    int GetLastEntryAccepted() const
    {
        return lastEntryAccepted;
    }

    int GetCountEntriesAccepted() const
    {
        return countEntriesAccepted;
    }

    int GetMyTransactionCount() const
    {
        return myEntries.size();
    }

    void UpdateState(unsigned int newState)
    {
        if (fFortunaStake && (newState == POOL_STATUS_ERROR || newState == POOL_STATUS_SUCCESS)){
            printf("CForTunaPool::UpdateState() - Can't set state to ERROR or SUCCESS as a fortunastake. \n");
            return;
        }

        printf("CForTunaPool::UpdateState() == %d | %d \n", state, newState);
        if(state != newState){
            lastTimeChanged = GetTimeMillis();
            if(fFortunaStake) {
                RelayForTunaStatus(forTunaPool.sessionID, forTunaPool.GetState(), forTunaPool.GetEntriesCount(), FORTUNASTAKE_RESET);
            }
        }
        state = newState;
    }

    int GetMaxPoolTransactions()
    {

        //use the production amount
        return POOL_MAX_TRANSACTIONS;
    }

    //Do we have enough users to take entries?
    bool IsSessionReady(){
        return sessionUsers >= GetMaxPoolTransactions();
    }

    // Are these outputs compatible with other client in the pool?
    bool IsCompatibleWithEntries(std::vector<CTxOut> vout);
    // Is this amount compatible with other client in the pool?
    bool IsCompatibleWithSession(int64_t nAmount, CTransaction txCollateral, std::string& strReason);

    // Passively run Fortuna in the background according to the configuration in settings (only for QT)
    bool DoAutomaticDenominating(bool fDryRun=false, bool ready=false);
    bool PrepareFortunaDenominate();


    // check for process in Fortuna
    void Check();
    // charge fees to bad actors
    void ChargeFees();
    // rarely charge fees to pay miners
    void ChargeRandomFees();
    void CheckTimeout();
    // check to make sure a signature matches an input in the pool
    bool SignatureValid(const CScript& newSig, const CTxIn& newVin);
    // if the collateral is valid given by a client
    bool IsCollateralValid(const CTransaction& txCollateral);
    // add a clients entry to the pool
    bool AddEntry(const std::vector<CTxIn>& newInput, const int64_t& nAmount, const CTransaction& txCollateral, const std::vector<CTxOut>& newOutput, std::string& error);
    // add signature to a vin
    bool AddScriptSig(const CTxIn newVin);
    // are all inputs signed?
    bool SignaturesComplete();
    // as a client, send a transaction to a fortunastake to start the denomination process
    void SendFortunaDenominate(std::vector<CTxIn>& vin, std::vector<CTxOut>& vout, int64_t amount);
    // get fortunastake updates about the progress of fortuna
    bool StatusUpdate(int newState, int newEntriesCount, int newAccepted, std::string& error, int newSessionID=0);

    // as a client, check and sign the final transaction
    bool SignFinalTransaction(CTransaction& finalTransactionNew, CNode* node);

    // get the last valid block hash for a given modulus
    bool GetLastValidBlockHash(uint256& hash, int mod=1, int nBlockHeight=0);
    // process a new block
    void NewBlock();
    void CompletedTransaction(bool error, std::string lastMessageNew);
    void ClearLastMessage();

    // split up large inputs or make fee sized inputs
    bool MakeCollateralAmounts();

	std::string Denominate();

    bool CreateDenominated(int64_t nTotalValue);
    // get the denominations for a list of outputs (returns a bitshifted integer)
    int GetDenominations(const std::vector<CTxOut>& vout);
    void GetDenominationsToString(int nDenom, std::string& strDenom);
    // get the denominations for a specific amount of denarius.
    int GetDenominationsByAmount(int64_t nAmount, int nDenomTarget=0);

    int GetDenominationsByAmounts(std::vector<int64_t>& vecAmount);
};


void ConnectToForTunaFortunaStakeWinner();

void ThreadCheckForTunaPool(void* parg);

#endif
