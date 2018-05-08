// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Darkcoin developers
// Copyright (c) 2017 The Denarius developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "db.h"
#include "init.h"
#include "masternode.h"
#include "activemasternode.h"
#include "masternodeconfig.h"
#include "bitcoinrpc.h"
#include <boost/lexical_cast.hpp>
#include "util.h"
#include "base58.h"

#include <fstream>
using namespace json_spirit;
using namespace std;

Value getpoolinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpoolinfo\n"
            "Returns an object containing anonymous pool-related information.");

    Object obj;
    obj.push_back(Pair("current_masternode",        GetCurrentMasterNode()));
    obj.push_back(Pair("state",        darkSendPool.GetState()));
    obj.push_back(Pair("entries",      darkSendPool.GetEntriesCount()));
    obj.push_back(Pair("entries_accepted",      darkSendPool.GetCountEntriesAccepted()));
    return obj;
}

Value masternode(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp  ||
        (strCommand != "start" && strCommand != "start-alias" && strCommand != "start-many" && strCommand != "stop" && strCommand != "stop-alias" && strCommand != "stop-many" && strCommand != "list" && strCommand != "list-conf" && strCommand != "count"  && strCommand != "enforce"
            && strCommand != "debug" && strCommand != "current" && strCommand != "winners" && strCommand != "genkey" && strCommand != "connect" && strCommand != "outputs" && strCommand != "status"))
		throw runtime_error(
			"masternode \"command\"... ( \"passphrase\" )\n"
			"Set of commands to execute masternode related actions\n"
			"\nArguments:\n"
			"1. \"command\"        (string or set of strings, required) The command to execute\n"
			"2. \"passphrase\"     (string, optional) The wallet passphrase\n"
			"\nAvailable commands:\n"
			"  count        - Print number of all known masternodes (optional: 'enabled', 'both')\n"
			"  current      - Print info on current masternode winner\n"
			"  debug        - Print masternode status\n"
			"  genkey       - Generate new masternodeprivkey\n"
			"  enforce      - Enforce masternode payments\n"
			"  outputs      - Print masternode compatible outputs\n"
            "  status       - Current masternode status\n"
			"  start        - Start masternode configured in denarius.conf\n"
			"  start-alias  - Start single masternode by assigned alias configured in masternode.conf\n"
			"  start-many   - Start all masternodes configured in masternode.conf\n"
			"  stop         - Stop masternode configured in denarius.conf\n"
			"  stop-alias   - Stop single masternode by assigned alias configured in masternode.conf\n"
			"  stop-many    - Stop all masternodes configured in masternode.conf\n"
			"  list         - Print list of all known masternodes (see masternodelist for more info)\n"
			"  list-conf    - Print masternode.conf in JSON format\n"
			"  winners      - Print list of masternode winners\n"
			//"  vote-many    - Vote on a Denarius initiative\n"
			//"  vote         - Vote on a Denarius initiative\n"
            );
    if (strCommand == "stop")
    {
        if(!fMasterNode) return "You must set masternode=1 in the configuration";

        if(pwalletMain->IsLocked()) {
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if (params.size() == 2){
                strWalletPass = params[1].get_str().c_str();
            } else {
                throw runtime_error(
                    "Your wallet is locked, passphrase is required\n");
            }

            if(!pwalletMain->Unlock(strWalletPass)){
                return "Incorrect passphrase";
            }
        }

        std::string errorMessage;
        if(!activeMasternode.StopMasterNode(errorMessage)) {
        	return "Stop Failed: " + errorMessage;
        }
        pwalletMain->Lock();

        if(activeMasternode.status == MASTERNODE_STOPPED) return "Successfully Stopped Masternode";
        if(activeMasternode.status == MASTERNODE_NOT_CAPABLE) return "Not a capable Masternode";

        return "unknown";
    }

    if (strCommand == "stop-alias")
    {
	    if (params.size() < 2){
			throw runtime_error(
			"command needs at least 2 parameters\n");
	    }

	    std::string alias = params[1].get_str().c_str();

    	if(pwalletMain->IsLocked()) {
    		SecureString strWalletPass;
    	    strWalletPass.reserve(100);

			if (params.size() == 3){
				strWalletPass = params[2].get_str().c_str();
			} else {
				throw runtime_error(
				"Your wallet is locked, passphrase is required\n");
			}

			if(!pwalletMain->Unlock(strWalletPass)){
				return "Incorrect passphrase";
			}
        }

    	bool found = false;

		Object statusObj;
		statusObj.push_back(Pair("alias", alias));

    	BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
    		if(mne.getAlias() == alias) {
    			found = true;
    			std::string errorMessage;
    			bool result = activeMasternode.StopMasterNode(mne.getIp(), mne.getPrivKey(), errorMessage);

				statusObj.push_back(Pair("result", result ? "successful" : "failed"));
    			if(!result) {
   					statusObj.push_back(Pair("errorMessage", errorMessage));
   				}
    			break;
    		}
    	}

    	if(!found) {
    		statusObj.push_back(Pair("result", "failed"));
    		statusObj.push_back(Pair("errorMessage", "could not find alias in config. Verify with list-conf."));
    	}

    	pwalletMain->Lock();
    	return statusObj;
    }

    if (strCommand == "stop-many")
    {
    	if(pwalletMain->IsLocked()) {
			SecureString strWalletPass;
			strWalletPass.reserve(100);

			if (params.size() == 2){
				strWalletPass = params[1].get_str().c_str();
			} else {
				throw runtime_error(
				"Your wallet is locked, passphrase is required\n");
			}

			if(!pwalletMain->Unlock(strWalletPass)){
				return "incorrect passphrase";
			}
		}

		int total = 0;
		int successful = 0;
		int fail = 0;


		Object resultsObj;

		BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
			total++;

			std::string errorMessage;
			bool result = activeMasternode.StopMasterNode(mne.getIp(), mne.getPrivKey(), errorMessage);

			Object statusObj;
			statusObj.push_back(Pair("alias", mne.getAlias()));
			statusObj.push_back(Pair("result", result ? "successful" : "failed"));

			if(result) {
				successful++;
			} else {
				fail++;
				statusObj.push_back(Pair("errorMessage", errorMessage));
			}

			resultsObj.push_back(Pair("status", statusObj));
		}
		pwalletMain->Lock();

		Object returnObj;
		returnObj.push_back(Pair("overall", "Successfully stopped " + boost::lexical_cast<std::string>(successful) + " masternodes, failed to stop " +
				boost::lexical_cast<std::string>(fail) + ", total " + boost::lexical_cast<std::string>(total)));
		returnObj.push_back(Pair("detail", resultsObj));

		return returnObj;

    }

    if (strCommand == "list")
    {
        std::string strCommand = "active";

        if (params.size() == 2){
            strCommand = params[1].get_str().c_str();
        }

        if (strCommand != "active" && strCommand != "vin" && strCommand != "pubkey" && strCommand != "lastseen" && strCommand != "activeseconds" && strCommand != "rank" && strCommand != "protocol"){
            throw runtime_error(
                "list supports 'active', 'vin', 'pubkey', 'lastseen', 'activeseconds', 'rank', 'protocol'\n");
        }

        Object obj;
        BOOST_FOREACH(CMasterNode mn, vecMasternodes) {
            mn.Check();

            if(strCommand == "active"){
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int)mn.IsEnabled()));
            } else if (strCommand == "vin") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       mn.vin.prevout.hash.ToString().c_str()));
            } else if (strCommand == "pubkey") {
                CScript pubkey;
                pubkey =GetScriptForDestination(mn.pubkey.GetID());
                CTxDestination address1;
                ExtractDestination(pubkey, address1);
                CBitcoinAddress address2(address1);

                obj.push_back(Pair(mn.addr.ToString().c_str(),       address2.ToString().c_str()));
            } else if (strCommand == "protocol") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int64_t)mn.protocolVersion));
            } else if (strCommand == "lastseen") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int64_t)mn.lastTimeSeen));
            } else if (strCommand == "activeseconds") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int64_t)(mn.lastTimeSeen - mn.now)));
            } else if (strCommand == "rank") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int)(GetMasternodeRank(mn, pindexBest->nHeight))));
            }
        }
        return obj;
    }
    if (strCommand == "count") return (int)vecMasternodes.size();

    if (strCommand == "start")
    {
        if(!fMasterNode) return "you must set masternode=1 in the configuration";

        if(pwalletMain->IsLocked()) {
            SecureString strWalletPass;
            strWalletPass.reserve(100);

            if (params.size() == 2){
                strWalletPass = params[1].get_str().c_str();
            } else {
                throw runtime_error(
                    "Your wallet is locked, passphrase is required\n");
            }

            if(!pwalletMain->Unlock(strWalletPass)){
                return "incorrect passphrase";
            }
        }

        if(activeMasternode.status != MASTERNODE_REMOTELY_ENABLED && activeMasternode.status != MASTERNODE_IS_CAPABLE){
            activeMasternode.status = MASTERNODE_NOT_PROCESSED; // TODO: consider better way
            std::string errorMessage;
            activeMasternode.ManageStatus();
            pwalletMain->Lock();
        }

        if(activeMasternode.status == MASTERNODE_REMOTELY_ENABLED) return "masternode started remotely";
        if(activeMasternode.status == MASTERNODE_INPUT_TOO_NEW) return "masternode input must have at least 15 confirmations";
        if(activeMasternode.status == MASTERNODE_STOPPED) return "masternode is stopped";
        if(activeMasternode.status == MASTERNODE_IS_CAPABLE) return "successfully started masternode";
        if(activeMasternode.status == MASTERNODE_NOT_CAPABLE) return "not capable masternode: " + activeMasternode.notCapableReason;
        if(activeMasternode.status == MASTERNODE_SYNC_IN_PROCESS) return "sync in process. Must wait until client is synced to start.";

        return "unknown";
    }

    if (strCommand == "start-alias")
    {
	    if (params.size() < 2){
			throw runtime_error(
			"command needs at least 2 parameters\n");
	    }

	    std::string alias = params[1].get_str().c_str();

    	if(pwalletMain->IsLocked()) {
    		SecureString strWalletPass;
    	    strWalletPass.reserve(100);

			if (params.size() == 3){
				strWalletPass = params[2].get_str().c_str();
			} else {
				throw runtime_error(
				"Your wallet is locked, passphrase is required\n");
			}

			if(!pwalletMain->Unlock(strWalletPass)){
				return "incorrect passphrase";
			}
        }

    	bool found = false;

		Object statusObj;
		statusObj.push_back(Pair("alias", alias));

    	BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
    		if(mne.getAlias() == alias) {
    			found = true;
    			std::string errorMessage;
    			bool result = activeMasternode.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage);

    			statusObj.push_back(Pair("result", result ? "successful" : "failed"));
    			if(!result) {
					statusObj.push_back(Pair("errorMessage", errorMessage));
				}
    			break;
    		}
    	}

    	if(!found) {
    		statusObj.push_back(Pair("result", "failed"));
    		statusObj.push_back(Pair("errorMessage", "could not find alias in config. Verify with list-conf."));
    	}

    	pwalletMain->Lock();
    	return statusObj;

    }

    if (strCommand == "start-many")
    {
    	if(pwalletMain->IsLocked()) {
			SecureString strWalletPass;
			strWalletPass.reserve(100);

			if (params.size() == 2){
				strWalletPass = params[1].get_str().c_str();
			} else {
				throw runtime_error(
				"Your wallet is locked, passphrase is required\n");
			}

			if(!pwalletMain->Unlock(strWalletPass)){
				return "incorrect passphrase";
			}
		}

		std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
		mnEntries = masternodeConfig.getEntries();

		int total = 0;
		int successful = 0;
		int fail = 0;

		Object resultsObj;

		BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
			total++;

			std::string errorMessage;
			bool result = activeMasternode.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage);

			Object statusObj;
			statusObj.push_back(Pair("alias", mne.getAlias()));
			statusObj.push_back(Pair("result", result ? "succesful" : "failed"));

			if(result) {
				successful++;
			} else {
				fail++;
				statusObj.push_back(Pair("errorMessage", errorMessage));
			}

			resultsObj.push_back(Pair("status", statusObj));
		}
		pwalletMain->Lock();

		Object returnObj;
		returnObj.push_back(Pair("overall", "Successfully started " + boost::lexical_cast<std::string>(successful) + " masternodes, failed to start " +
				boost::lexical_cast<std::string>(fail) + ", total " + boost::lexical_cast<std::string>(total)));
		returnObj.push_back(Pair("detail", resultsObj));

		return returnObj;
    }

    if (strCommand == "debug")
    {
        if(activeMasternode.status == MASTERNODE_REMOTELY_ENABLED) return "masternode started remotely";
        if(activeMasternode.status == MASTERNODE_INPUT_TOO_NEW) return "masternode input must have at least 15 confirmations";
        if(activeMasternode.status == MASTERNODE_IS_CAPABLE) return "successfully started masternode";
        if(activeMasternode.status == MASTERNODE_STOPPED) return "masternode is stopped";
        if(activeMasternode.status == MASTERNODE_NOT_CAPABLE) return "not capable masternode: " + activeMasternode.notCapableReason;
        if(activeMasternode.status == MASTERNODE_SYNC_IN_PROCESS) return "sync in process. Must wait until client is synced to start.";

        CTxIn vin = CTxIn();
        CPubKey pubkey = CScript();
        CKey key;
        bool found = activeMasternode.GetMasterNodeVin(vin, pubkey, key);
        if(!found){
            return "Missing masternode input, please look at the documentation for instructions on masternode creation";
        } else {
            return "No problems were found";
        }
    }

    if (strCommand == "create")
    {

        return "Not implemented yet, please look at the documentation for instructions on masternode creation";
    }

    if (strCommand == "current")
    {
        int winner = GetCurrentMasterNode(1);
        if(winner >= 0) {
            return vecMasternodes[winner].addr.ToString().c_str();
        }

        return "unknown";
    }

    if (strCommand == "genkey")
    {
		CKey secret;
		secret.MakeNewKey(false);
		return CBitcoinSecret(secret).ToString();
    }

    if (strCommand == "winners")
    {
        Object obj;

        for(int nHeight = pindexBest->nHeight-10; nHeight < pindexBest->nHeight+20; nHeight++)
        {
            CScript payee;
            if(masternodePayments.GetBlockPayee(nHeight, payee)){
                CTxDestination address1;
                ExtractDestination(payee, address1);
                CBitcoinAddress address2(address1);
                obj.push_back(Pair(boost::lexical_cast<std::string>(nHeight),       address2.ToString().c_str()));
            } else {
                obj.push_back(Pair(boost::lexical_cast<std::string>(nHeight),       ""));
            }
        }

        return obj;
    }

    if(strCommand == "enforce")
    {
        return (uint64_t)enforceMasternodePaymentsTime;
    }

    if(strCommand == "connect")
    {
        std::string strAddress = "";
        if (params.size() == 2){
            strAddress = params[1].get_str().c_str();
        } else {
            throw runtime_error(
                "Masternode address required\n");
        }

        CService addr = CService(strAddress);

        if(ConnectNode((CAddress)addr, NULL, true)){
            return "successfully connected";
        } else {
            return "error connecting";
        }
    }

    if(strCommand == "list-conf")
    {
    	std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
    	mnEntries = masternodeConfig.getEntries();

        Object resultObj;

        BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
    		Object mnObj;
    		mnObj.push_back(Pair("alias", mne.getAlias()));
    		mnObj.push_back(Pair("address", mne.getIp()));
    		mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
    		mnObj.push_back(Pair("txHash", mne.getTxHash()));
    		mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
    		resultObj.push_back(Pair("masternode", mnObj));
    	}

    	return resultObj;
    }

    if (strCommand == "outputs"){
        // Find possible candidates
        vector<COutput> possibleCoins = activeMasternode.SelectCoinsMasternode();

        Object obj;
        BOOST_FOREACH(COutput& out, possibleCoins) {
            obj.push_back(Pair(out.tx->GetHash().ToString().c_str(), boost::lexical_cast<std::string>(out.i)));
        }

        return obj;

    }

    if(strCommand == "status")
    {
        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();
        Object mnObj;

            CScript pubkey;
            pubkey = GetScriptForDestination(activeMasternode.pubKeyMasternode.GetID());
            CTxDestination address1;
            ExtractDestination(pubkey, address1);
            CBitcoinAddress address2(address1);
            if (activeMasternode.pubKeyMasternode.IsFullyValid()) {
                CScript pubkey;
                pubkey = GetScriptForDestination(activeMasternode.pubKeyMasternode.GetID());
                CTxDestination address1;
                ExtractDestination(pubkey, address1);
                if (pubkey.IsPayToScriptHash())
                CBitcoinAddress address2(address1);

                Object localObj;
                localObj.push_back(Pair("vin", activeMasternode.vin.ToString().c_str()));
                localObj.push_back(Pair("service", activeMasternode.service.ToString().c_str()));
                localObj.push_back(Pair("status", activeMasternode.status));
                localObj.push_back(Pair("address", address2.ToString().c_str()));
                localObj.push_back(Pair("notCapableReason", activeMasternode.notCapableReason.c_str()));
                mnObj.push_back(Pair("local",localObj));
            } else {
                Object localObj;
                localObj.push_back(Pair("status", "unconfigured"));
                mnObj.push_back(Pair("local",localObj));
            }

            BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry& mne, masternodeConfig.getEntries()) {
                Object remoteObj;
                std::string address = mne.getIp();

                CTxIn vin;
                CTxDestination address1;
                CActiveMasternode amn;
                CPubKey pubKeyCollateralAddress;
                CKey keyCollateralAddress;
                CPubKey pubKeyMasternode;
                CKey keyMasternode;
                std::string errorMessage;
                std::string darkSendError;
                std::string vinError;

                if(!darkSendSigner.SetKey(mne.getPrivKey(), darkSendError, keyMasternode, pubKeyMasternode))
                {
                    errorMessage = darkSendError;
                }

                if (!amn.GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, mne.getTxHash(), mne.getOutputIndex(), vinError))
                {
                    errorMessage = vinError;
                }

                CScript pubkey = GetScriptForDestination(pubKeyCollateralAddress.GetID());
                ExtractDestination(pubkey, address1);
                CBitcoinAddress address2(address1);

                remoteObj.push_back(Pair("alias", mne.getAlias()));
                remoteObj.push_back(Pair("ipaddr", address));
                remoteObj.push_back(Pair("collateral", address2.ToString()));

                bool mnfound = false;
                BOOST_FOREACH(CMasterNode& mn, vecMasternodes)
                {
                    if (mn.addr.ToString() == mne.getIp()) {
                        remoteObj.push_back(Pair("status", "online"));
                        remoteObj.push_back(Pair("lastpaidblock",mn.nBlockLastPaid));
                        remoteObj.push_back(Pair("version",mn.protocolVersion));
                        mnfound = true;
                        break;
                    }
                }
                if (!mnfound)
                {
                    if (!errorMessage.empty()) {
                        remoteObj.push_back(Pair("status", "error"));
                        remoteObj.push_back(Pair("error", errorMessage));
                    } else {
                        remoteObj.push_back(Pair("status", "notfound"));
                    }
                }
                mnObj.push_back(Pair(mne.getAlias(),remoteObj));
            }

            return mnObj;
    }


    return Value::null;
}
