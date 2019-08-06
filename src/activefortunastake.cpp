// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The DarkCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "protocol.h"
#include "activefortunastake.h"
#include "fortunastakeconfig.h"
#include <boost/lexical_cast.hpp>
#include "clientversion.h"

//
// Bootup the fortunastake, look for a 5000 D input and register on the network
//
void CActiveFortunastake::ManageStatus()
{
    std::string errorMessage;

    if(!fFortunaStake) return;

    if (fDebug) printf("CActiveFortunastake::ManageStatus() - Begin\n");

    //need correct adjusted time to send ping
    bool fIsInitialDownload = IsInitialBlockDownload();
    if(fIsInitialDownload) {
        status = FORTUNASTAKE_SYNC_IN_PROCESS;
        printf("CActiveFortunastake::ManageStatus() - Sync in progress. Must wait until sync is complete to start fortunastake.\n");
        return;
    }

    if(status == FORTUNASTAKE_INPUT_TOO_NEW || status == FORTUNASTAKE_NOT_CAPABLE || status == FORTUNASTAKE_SYNC_IN_PROCESS){
        status = FORTUNASTAKE_NOT_PROCESSED;
    }

    if(status == FORTUNASTAKE_NOT_PROCESSED) {
        if(strFortunaStakeAddr.empty()) {
            if(!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the fortunastakeaddr configuration option.";
                status = FORTUNASTAKE_NOT_CAPABLE;
                printf("CActiveFortunastake::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
                return;
            }
        } else {
            service = CService(strFortunaStakeAddr);
        }

        printf("CActiveFortunastake::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString().c_str());

            if(!ConnectNode((CAddress)service, service.ToString().c_str())){
                notCapableReason = "Could not connect to " + service.ToString();
                status = FORTUNASTAKE_NOT_CAPABLE;
                printf("CActiveFortunastake::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
                return;
            }

        if(pwalletMain->IsLocked()){
            notCapableReason = "Wallet is locked.";
            status = FORTUNASTAKE_NOT_CAPABLE;
            printf("CActiveFortunastake::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
            return;
        }

        // Set defaults
        status = FORTUNASTAKE_NOT_CAPABLE;
        notCapableReason = "Unknown. Check debug.log for more information.\n";

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if(GetFortunaStakeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {

            //if(GetInputAge(vin, pindexBest) < (nBestHeight > BLOCK_START_FORTUNASTAKE_DELAYPAY ? FORTUNASTAKE_MIN_CONFIRMATIONS_NOPAY : FORTUNASTAKE_MIN_CONFIRMATIONS)){
            //    printf("CActiveFortunastake::ManageStatus() - Input must have least %d confirmations - %d confirmations\n", (nBestHeight > BLOCK_START_FORTUNASTAKE_DELAYPAY ? FORTUNASTAKE_MIN_CONFIRMATIONS_NOPAY : FORTUNASTAKE_MIN_CONFIRMATIONS), GetInputAge(vin, pindexBest));
            //    status = FORTUNASTAKE_INPUT_TOO_NEW;
            //    return;
            //}

            printf("CActiveFortunastake::ManageStatus() - Is capable master node!\n");

            status = FORTUNASTAKE_IS_CAPABLE;
            notCapableReason = "";

            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyFortunastake;
            CKey keyFortunastake;

            if(!forTunaSigner.SetKey(strFortunaStakePrivKey, errorMessage, keyFortunastake, pubKeyFortunastake))
            {
                printf("Register::ManageStatus() - Error upon calling SetKey: %s\n", errorMessage.c_str());
                return;
            }

            if(!Register(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyFortunastake, pubKeyFortunastake, errorMessage)) {
                printf("CActiveFortunastake::ManageStatus() - Error on Register: %s\n", errorMessage.c_str());
            }

            return;
        } else {
            printf("CActiveFortunastake::ManageStatus() - Could not find suitable coins!\n");
        }
    }

    //send to all peers
    if(!Dseep(errorMessage)) {
        printf("CActiveFortunastake::ManageStatus() - Error on Ping: %s", errorMessage.c_str());
    }
}

// Send stop dseep to network for remote fortunastake
bool CActiveFortunastake::StopFortunaStake(std::string strService, std::string strKeyFortunastake, std::string& errorMessage) {
    CTxIn vin;
    CKey keyFortunastake;
    CPubKey pubKeyFortunastake;

    if(!forTunaSigner.SetKey(strKeyFortunastake, errorMessage, keyFortunastake, pubKeyFortunastake)) {
        printf("CActiveFortunastake::StopFortunaStake() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return StopFortunaStake(vin, CService(strService), keyFortunastake, pubKeyFortunastake, errorMessage);
}

// Send stop dseep to network for main fortunastake
bool CActiveFortunastake::StopFortunaStake(std::string& errorMessage) {
    if(status != FORTUNASTAKE_IS_CAPABLE && status != FORTUNASTAKE_REMOTELY_ENABLED) {
        errorMessage = "fortunastake is not in a running status";
        printf("CActiveFortunastake::StopFortunaStake() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    status = FORTUNASTAKE_STOPPED;

    CPubKey pubKeyFortunastake;
    CKey keyFortunastake;

    if(!forTunaSigner.SetKey(strFortunaStakePrivKey, errorMessage, keyFortunastake, pubKeyFortunastake))
    {
        printf("Register::ManageStatus() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    return StopFortunaStake(vin, service, keyFortunastake, pubKeyFortunastake, errorMessage);
}

// Send stop dseep to network for any fortunastake
bool CActiveFortunastake::StopFortunaStake(CTxIn vin, CService service, CKey keyFortunastake, CPubKey pubKeyFortunastake, std::string& errorMessage) {
       pwalletMain->UnlockCoin(vin.prevout);
    return Dseep(vin, service, keyFortunastake, pubKeyFortunastake, errorMessage, true);
}

bool CActiveFortunastake::Dseep(std::string& errorMessage) {
    if(status != FORTUNASTAKE_IS_CAPABLE && status != FORTUNASTAKE_REMOTELY_ENABLED) {
        errorMessage = "fortunastake is not in a running status";
        printf("CActiveFortunastake::Dseep() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    CPubKey pubKeyFortunastake;
    CKey keyFortunastake;

    if(!forTunaSigner.SetKey(strFortunaStakePrivKey, errorMessage, keyFortunastake, pubKeyFortunastake))
    {
        printf("Register::ManageStatus() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    return Dseep(vin, service, keyFortunastake, pubKeyFortunastake, errorMessage, false);
}

bool CActiveFortunastake::Dseep(CTxIn vin, CService service, CKey keyFortunastake, CPubKey pubKeyFortunastake, std::string &retErrorMessage, bool stop) {
    std::string errorMessage;
    std::vector<unsigned char> vchFortunaStakeSignature;
    std::string strFortunaStakeSignMessage;
    int64_t masterNodeSignatureTime = GetAdjustedTime();

    std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(masterNodeSignatureTime) + boost::lexical_cast<std::string>(stop);

    if(!forTunaSigner.SignMessage(strMessage, errorMessage, vchFortunaStakeSignature, keyFortunastake)) {
        retErrorMessage = "sign message failed: " + errorMessage;
        printf("CActiveFortunastake::Dseep() - Error: %s\n", retErrorMessage.c_str());
        return false;
    }

    if(!forTunaSigner.VerifyMessage(pubKeyFortunastake, vchFortunaStakeSignature, strMessage, errorMessage)) {
        retErrorMessage = "Verify message failed: " + errorMessage;
        printf("CActiveFortunastake::Dseep() - Error: %s\n", retErrorMessage.c_str());
        return false;
    }

    // Update Last Seen timestamp in fortunastake list
    bool found = false;
    BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes) {
        //printf(" -- %s\n", mn.vin.ToString().c_str());
        if(mn.vin == vin) {
            found = true;
            mn.UpdateLastSeen();
        }
    }

    if(!found){
        // Seems like we are trying to send a ping while the fortunastake is not registered in the network
        retErrorMessage = "Fortuna Fortunastake List doesn't include our fortunastake, Shutting down fortunastake pinging service! " + vin.ToString();
        printf("CActiveFortunastake::Dseep() - Error: %s\n", retErrorMessage.c_str());
        status = FORTUNASTAKE_NOT_CAPABLE;
        notCapableReason = retErrorMessage;
        return false;
    }

    //send to all peers
    printf("CActiveFortunastake::Dseep() - SendForTunaElectionEntryPing vin = %s\n", vin.ToString().c_str());
    SendForTunaElectionEntryPing(vin, vchFortunaStakeSignature, masterNodeSignatureTime, stop);

    return true;
}

bool CActiveFortunastake::RegisterByPubKey(std::string strService, std::string strKeyFortunastake, std::string collateralAddress, std::string& errorMessage) {
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyFortunastake;
    CKey keyFortunastake;

    if(!forTunaSigner.SetKey(strKeyFortunastake, errorMessage, keyFortunastake, pubKeyFortunastake))
    {
        printf("CActiveFortunastake::RegisterByPubKey() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    if(!GetFortunaStakeVinForPubKey(collateralAddress, vin, pubKeyCollateralAddress, keyCollateralAddress)) {
        errorMessage = "could not allocate vin for collateralAddress";
        printf("Register::Register() - Error: %s\n", errorMessage.c_str());
        return false;
    }
    return Register(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyFortunastake, pubKeyFortunastake, errorMessage);
}

bool CActiveFortunastake::Register(std::string strService, std::string strKeyFortunastake, std::string txHash, std::string strOutputIndex, std::string& errorMessage) {
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyFortunastake;
    CKey keyFortunastake;

    if(!forTunaSigner.SetKey(strKeyFortunastake, errorMessage, keyFortunastake, pubKeyFortunastake))
    {
        printf("CActiveFortunastake::Register() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    if(!GetFortunaStakeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, txHash, strOutputIndex, errorMessage)) {
        //errorMessage = "could not allocate vin";
        printf("Register::Register() - Error: %s\n", errorMessage.c_str());
        return false;
    }
    return Register(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyFortunastake, pubKeyFortunastake, errorMessage);
}

bool CActiveFortunastake::Register(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyFortunastake, CPubKey pubKeyFortunastake, std::string &retErrorMessage) {
    std::string errorMessage;
    std::vector<unsigned char> vchFortunaStakeSignature;
    std::string strFortunaStakeSignMessage;
    int64_t masterNodeSignatureTime = GetAdjustedTime();

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyFortunastake.begin(), pubKeyFortunastake.end());

    std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(masterNodeSignatureTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(PROTOCOL_VERSION);
    if(!forTunaSigner.SignMessage(strMessage, errorMessage, vchFortunaStakeSignature, keyCollateralAddress)) {
        retErrorMessage = "sign message failed: " + errorMessage;
        printf("CActiveFortunastake::Register() - Error: %s\n", retErrorMessage.c_str());
        return false;
    }
    if(!forTunaSigner.VerifyMessage(pubKeyCollateralAddress, vchFortunaStakeSignature, strMessage, errorMessage)) {
        retErrorMessage = "Verify message failed: " + errorMessage;
        printf("CActiveFortunastake::Register() - Error: %s\n", retErrorMessage.c_str());
        return false;
    }

    bool found = false;
    LOCK(cs_fortunastakes);
    BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes)
        if(mn.vin == vin)
            found = true;

    if(!found) {
        printf("CActiveFortunastake::Register() - Adding to fortunastake list service: %s - vin: %s\n", service.ToString().c_str(), vin.ToString().c_str());
        CFortunaStake mn(service, vin, pubKeyCollateralAddress, vchFortunaStakeSignature, masterNodeSignatureTime, pubKeyFortunastake, PROTOCOL_VERSION);
        mn.UpdateLastSeen(masterNodeSignatureTime);
        vecFortunastakes.push_back(mn);
    }

    //send to all peers
    printf("CActiveFortunastake::Register() - SendForTunaElectionEntry vin = %s\n", vin.ToString().c_str());
    SendForTunaElectionEntry(vin, service, vchFortunaStakeSignature, masterNodeSignatureTime, pubKeyCollateralAddress, pubKeyFortunastake, -1, -1, masterNodeSignatureTime, PROTOCOL_VERSION);

    return true;
}

bool CActiveFortunastake::GetFortunaStakeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
    return GetFortunaStakeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveFortunastake::GetFortunaStakeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex) {
    CScript pubScript;
    // Find possible candidates
    vector<COutput> possibleCoins = SelectCoinsFortunastake();
    COutput *selectedOutput;

    // Find the vin
    if(!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex = boost::lexical_cast<int>(strOutputIndex);
        bool found = false;
        BOOST_FOREACH(COutput& out, possibleCoins) {
            if(out.tx->GetHash() == txHash && out.i == outputIndex)
            {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if(!found) {
            printf("CActiveFortunastake::GetFortunaStakeVin - Could not locate valid vin\n");
            return false;
        }
        if (selectedOutput->nDepth < FORTUNASTAKE_MIN_CONFIRMATIONS_NOPAY) {
            CScript mn;
            mn = GetScriptForDestination(pubkey.GetID());
            CTxDestination address1;
            ExtractDestination(mn, address1);
            CBitcoinAddress address2(address1);
            int remain = FORTUNASTAKE_MIN_CONFIRMATIONS_NOPAY - selectedOutput->nDepth;
            printf("CActiveFortunastake::GetFortunaStakeVin - Transaction for MN %s is too young (%d more confirms required)", address2.ToString().c_str(), remain);
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if(possibleCoins.size() > 0) {
            // May cause problems with multiple transactions.
            selectedOutput = &possibleCoins[0];
        } else {
            printf("CActiveFortunastake::GetFortunaStakeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}

bool CActiveFortunastake::GetFortunaStakeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage) {

    // Find possible candidates
    vector<COutput> possibleCoins = SelectCoinsFortunastake(false);
    COutput *selectedOutput;

    // Find the vin
    if(!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex = boost::lexical_cast<int>(strOutputIndex);
        bool found = false;
        BOOST_FOREACH(COutput& out, possibleCoins) {
            if(out.tx->GetHash() == txHash && out.i == outputIndex)
            {
                if (out.tx->IsSpent(outputIndex))
                {
                        errorMessage = "vin was spent";
                        return false;
                }
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if(!found) {
            errorMessage = "Could not locate valid vin";
            return false;
        }
        if (selectedOutput->nDepth < FORTUNASTAKE_MIN_CONFIRMATIONS_NOPAY) {
            int remain = FORTUNASTAKE_MIN_CONFIRMATIONS_NOPAY - selectedOutput->nDepth;
            errorMessage = strprintf("%d more confirms required", remain);
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if(possibleCoins.size() > 0) {
            // May cause problems with multiple transactions.
            selectedOutput = &possibleCoins[0];
        } else {
            errorMessage = "Could not locate specified vin from coins in wallet";
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    if (!GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey))
    {
        errorMessage = "could not allocate vin";
        return false;
    }
    return true;
}

bool CActiveFortunastake::GetFortunaStakeVinForPubKey(std::string collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
    return GetFortunaStakeVinForPubKey(collateralAddress, vin, pubkey, secretKey, "", "");
}

bool CActiveFortunastake::GetFortunaStakeVinForPubKey(std::string collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex) {
    CScript pubScript;

    // Find possible candidates
    vector<COutput> possibleCoins = SelectCoinsFortunastakeForPubKey(collateralAddress);
    COutput *selectedOutput;

    // Find the vin
    if(!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex = boost::lexical_cast<int>(strOutputIndex);
        bool found = false;
        BOOST_FOREACH(COutput& out, possibleCoins) {
            if(out.tx->GetHash() == txHash && out.i == outputIndex)
            {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if(!found) {
            printf("CActiveFortunastake::GetFortunaStakeVinForPubKey - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if(possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            printf("CActiveFortunastake::GetFortunaStakeVinForPubKey - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract fortunastake vin information from output
bool CActiveFortunastake::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {

    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(),out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        printf("CActiveFortunastake::GetFortunaStakeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        printf("CActiveFortunastake::GetFortunaStakeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running fortunastake
vector<COutput> CActiveFortunastake::SelectCoinsFortunastake(bool fSelectUnlocked)
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from fortunastake.conf
    if(fSelectUnlocked && GetBoolArg("-fsconflock", true)) {
        uint256 mnTxHash;
        BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());
            COutPoint outpoint = COutPoint(mnTxHash, boost::lexical_cast<unsigned int>(mne.getOutputIndex()));
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoinsMN(vCoins, true, fSelectUnlocked);

    // Lock MN coins from fortunastake.conf back if they where temporary unlocked
    if(!confLockedCoins.empty()) {
        BOOST_FOREACH(COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    BOOST_FOREACH(const COutput& out, vCoins)
    {
        if(out.tx->vout[out.i].nValue == GetMNCollateral()*COIN) { //exactly 5,000 D
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// get all possible outputs for running fortunastake for a specific pubkey
vector<COutput> CActiveFortunastake::SelectCoinsFortunastakeForPubKey(std::string collateralAddress)
{
    CBitcoinAddress address(collateralAddress);
    CScript scriptPubKey;
    scriptPubKey.SetDestination(address.Get());
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Filter
    BOOST_FOREACH(const COutput& out, vCoins)
    {
        if(out.tx->vout[out.i].scriptPubKey == scriptPubKey && out.tx->vout[out.i].nValue == GetMNCollateral()*COIN) { //exactly 5,000 D
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a fortunastake, this can enable to run as a hot wallet with no funds
bool CActiveFortunastake::EnableHotColdFortunaStake(CTxIn& newVin, CService& newService)
{
    if(!fFortunaStake) return false;

    status = FORTUNASTAKE_REMOTELY_ENABLED;

    //The values below are needed for signing dseep messages going forward
    this->vin = newVin;
    this->service = newService;

    printf("CActiveFortunastake::EnableHotColdFortunaStake() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
