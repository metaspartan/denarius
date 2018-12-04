// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The DarkCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef ACTIVEMASTERNODE_H
#define ACTIVEMASTERNODE_H

#include "uint256.h"
#include "sync.h"
#include "net.h"
#include "key.h"
#include "main.h"
#include "init.h"
#include "wallet.h"
#include "fortuna.h"

// Responsible for activating the fortunastake and pinging the network
class CActiveFortunastake
{
public:
	// Initialized by init.cpp
	// Keys for the main fortunastake
	CPubKey pubKeyFortunastake;

	// Initialized while registering fortunastake
	CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveFortunastake()
    {        
        status = MASTERNODE_NOT_PROCESSED;
    }

    void ManageStatus(); // manage status of main fortunastake

    bool Dseep(std::string& errorMessage); // ping for main fortunastake
    bool Dseep(CTxIn vin, CService service, CKey key, CPubKey pubKey, std::string &retErrorMessage, bool stop); // ping for any fortunastake

    bool StopFortunaStake(std::string& errorMessage); // stop main fortunastake
    bool StopFortunaStake(std::string strService, std::string strKeyFortunastake, std::string& errorMessage); // stop remote fortunastake
    bool StopFortunaStake(CTxIn vin, CService service, CKey key, CPubKey pubKey, std::string& errorMessage); // stop any fortunastake

    bool Register(std::string strService, std::string strKey, std::string txHash, std::string strOutputIndex, std::string& errorMessage); // register remote fortunastake
    bool Register(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyFortunastake, CPubKey pubKeyFortunastake, std::string &retErrorMessage); // register any fortunastake
    bool RegisterByPubKey(std::string strService, std::string strKeyFortunastake, std::string collateralAddress, std::string& errorMessage); // register for a specific collateral address

    // get 5000D input that can be used for the fortunastake
    bool GetFortunaStakeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    bool GetFortunaStakeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetFortunaStakeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage);

    bool GetFortunaStakeVinForPubKey(std::string collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    bool GetFortunaStakeVinForPubKey(std::string collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    vector<COutput> SelectCoinsFortunastake(bool fSelectUnlocked=true);
    vector<COutput> SelectCoinsFortunastakeForPubKey(std::string collateralAddress);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    //bool SelectCoinsFortunastake(CTxIn& vin, int64& nValueIn, CScript& pubScript, std::string strTxHash, std::string strOutputIndex);

    // enable hot wallet mode (run a fortunastake with no funds)
    bool EnableHotColdFortunaStake(CTxIn& vin, CService& addr);
};

#endif
