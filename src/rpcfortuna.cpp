// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Darkcoin developers
// Copyright (c) 2017 The Denarius developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "db.h"
#include "init.h"
#include "fortunastake.h"
#include "activefortunastake.h"
#include "fortunastakeconfig.h"
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
    obj.push_back(Pair("current_fortunastake",        GetCurrentFortunaStake()));
    obj.push_back(Pair("state",        forTunaPool.GetState()));
    obj.push_back(Pair("entries",      forTunaPool.GetEntriesCount()));
    obj.push_back(Pair("entries_accepted",      forTunaPool.GetCountEntriesAccepted()));
    return obj;
}

Value fortunastake(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp  ||
        (strCommand != "start" && strCommand != "start-alias" && strCommand != "start-many" && strCommand != "stop" && strCommand != "stop-alias" && strCommand != "stop-many" && strCommand != "list" && strCommand != "list-conf" && strCommand != "count"  && strCommand != "enforce"
            && strCommand != "debug" && strCommand != "current" && strCommand != "winners" && strCommand != "genkey" && strCommand != "connect" && strCommand != "outputs" && strCommand != "status"))
		throw runtime_error(
			"fortunastake \"command\"... ( \"passphrase\" )\n"
			"Set of commands to execute fortunastake related actions\n"
			"\nArguments:\n"
			"1. \"command\"        (string or set of strings, required) The command to execute\n"
			"2. \"passphrase\"     (string, optional) The wallet passphrase\n"
			"\nAvailable commands:\n"
			"  count        - Print number of all known fortunastakes (optional: 'enabled', 'both')\n"
			"  current      - Print info on current fortunastake winner\n"
			"  debug        - Print fortunastake status\n"
			"  genkey       - Generate new fortunastakeprivkey\n"
			"  enforce      - Enforce fortunastake payments\n"
			"  outputs      - Print fortunastake compatible outputs\n"
            "  status       - Current fortunastake status\n"
			"  start        - Start fortunastake configured in denarius.conf\n"
			"  start-alias  - Start single fortunastake by assigned alias configured in fortunastake.conf\n"
			"  start-many   - Start all fortunastakes configured in fortunastake.conf\n"
			"  stop         - Stop fortunastake configured in denarius.conf\n"
			"  stop-alias   - Stop single fortunastake by assigned alias configured in fortunastake.conf\n"
			"  stop-many    - Stop all fortunastakes configured in fortunastake.conf\n"
			"  list         - Print list of all known fortunastakes (see fortunastakelist for more info)\n"
			"  list-conf    - Print fortunastake.conf in JSON format\n"
			"  winners      - Print list of fortunastake winners\n"
			//"  vote-many    - Vote on a Denarius initiative\n"
			//"  vote         - Vote on a Denarius initiative\n"
            );
    if (strCommand == "stop")
    {
        if(!fFortunaStake) return "You must set fortunastake=1 in the configuration";

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
        if(!activeFortunastake.StopFortunaStake(errorMessage)) {
        	return "Stop Failed: " + errorMessage;
        }
        pwalletMain->Lock();

        if(activeFortunastake.status == FORTUNASTAKE_STOPPED) return "Successfully Stopped Fortunastake";
        if(activeFortunastake.status == FORTUNASTAKE_NOT_CAPABLE) return "Not a capable Fortunastake";

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

    	BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
    		if(mne.getAlias() == alias) {
    			found = true;
    			std::string errorMessage;
    			bool result = activeFortunastake.StopFortunaStake(mne.getIp(), mne.getPrivKey(), errorMessage);

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

		BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
			total++;

			std::string errorMessage;
			bool result = activeFortunastake.StopFortunaStake(mne.getIp(), mne.getPrivKey(), errorMessage);

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
		returnObj.push_back(Pair("overall", "Successfully stopped " + boost::lexical_cast<std::string>(successful) + " fortunastakes, failed to stop " +
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

        if (strCommand != "active" && strCommand != "txid" && strCommand != "pubkey" && strCommand != "lastseen" && strCommand != "lastpaid" && strCommand != "activeseconds" && strCommand != "rank" && strCommand != "n" && strCommand != "full" && strCommand != "protocol" && strCommand != "roundpayments" && strCommand != "roundearnings" && strCommand != "dailyrate"){
            throw runtime_error(
                "list supports 'active', 'txid', 'pubkey', 'lastseen', 'lastpaid', 'activeseconds', 'rank', 'n', 'protocol', 'roundpayments', 'roundearnings', 'dailyrate', full'\n");
        }

        Object obj;
        BOOST_FOREACH(CFortunaStake mn, vecFortunastakes) {
            mn.Check();

            if(strCommand == "active"){
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int)mn.IsActive()));
            } else if (strCommand == "txid") {
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
            } else if (strCommand == "n") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int64_t)mn.vin.prevout.n));
            } else if (strCommand == "lastpaid") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       mn.nBlockLastPaid));
            } else if (strCommand == "lastseen") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int64_t)mn.lastTimeSeen));
            } else if (strCommand == "activeseconds") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int64_t)(mn.lastTimeSeen - mn.now)));
            } else if (strCommand == "rank") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int)(GetFortunastakeRank(mn, pindexBest))));
            } else if (strCommand == "roundpayments") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       mn.payCount));
            } else if (strCommand == "roundearnings") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       mn.payRate));
            } else if (strCommand == "dailyrate") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       mn.payValue));
            }
			else if (strCommand == "full") {
                Object list;
                list.push_back(Pair("active",        (int)mn.IsActive()));
                list.push_back(Pair("txid",           mn.vin.prevout.hash.ToString().c_str()));
                list.push_back(Pair("n",       (int64_t)mn.vin.prevout.n));

                CScript pubkey;
                pubkey =GetScriptForDestination(mn.pubkey.GetID());
                CTxDestination address1;
                ExtractDestination(pubkey, address1);
                CBitcoinAddress address2(address1);

                list.push_back(Pair("pubkey",         address2.ToString().c_str()));
                list.push_back(Pair("protocolversion",       (int64_t)mn.protocolVersion));
                list.push_back(Pair("lastseen",       (int64_t)mn.lastTimeSeen));
                list.push_back(Pair("activeseconds",  (int64_t)(mn.lastTimeSeen - mn.now)));
                list.push_back(Pair("rank",           (int)(GetFortunastakeRank(mn, pindexBest))));
                list.push_back(Pair("lastpaid",       mn.nBlockLastPaid));
                list.push_back(Pair("roundpayments",       mn.payCount));
                list.push_back(Pair("roundearnings",       mn.payValue));
                list.push_back(Pair("dailyrate",       mn.payRate));
                obj.push_back(Pair(mn.addr.ToString().c_str(), list));
            }
        }
        return obj;
    }
    if (strCommand == "count") return (int)vecFortunastakes.size();

    if (strCommand == "start")
    {
        if(!fFortunaStake) return "you must set fortunastake=1 in the configuration";

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

        if(activeFortunastake.status != FORTUNASTAKE_REMOTELY_ENABLED && activeFortunastake.status != FORTUNASTAKE_IS_CAPABLE){
            activeFortunastake.status = FORTUNASTAKE_NOT_PROCESSED; // TODO: consider better way
            std::string errorMessage;
            activeFortunastake.ManageStatus();
            pwalletMain->Lock();
        }

        if(activeFortunastake.status == FORTUNASTAKE_REMOTELY_ENABLED) return "fortunastake started remotely";
        if(activeFortunastake.status == FORTUNASTAKE_INPUT_TOO_NEW) return "fortunastake input must have at least 15 confirmations";
        if(activeFortunastake.status == FORTUNASTAKE_STOPPED) return "fortunastake is stopped";
        if(activeFortunastake.status == FORTUNASTAKE_IS_CAPABLE) return "successfully started fortunastake";
        if(activeFortunastake.status == FORTUNASTAKE_NOT_CAPABLE) return "not capable fortunastake: " + activeFortunastake.notCapableReason;
        if(activeFortunastake.status == FORTUNASTAKE_SYNC_IN_PROCESS) return "sync in process. Must wait until client is synced to start.";

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

    	BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
    		if(mne.getAlias() == alias) {
    			found = true;
    			std::string errorMessage;
    			bool result = activeFortunastake.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage);

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

		std::vector<CFortunastakeConfig::CFortunastakeEntry> mnEntries;
		mnEntries = fortunastakeConfig.getEntries();

		int total = 0;
		int successful = 0;
		int fail = 0;

		Object resultsObj;

		BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
			total++;

			std::string errorMessage;
			bool result = activeFortunastake.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage);

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
		returnObj.push_back(Pair("overall", "Successfully started " + boost::lexical_cast<std::string>(successful) + " fortunastakes, failed to start " +
				boost::lexical_cast<std::string>(fail) + ", total " + boost::lexical_cast<std::string>(total)));
		returnObj.push_back(Pair("detail", resultsObj));

		return returnObj;
    }

    if (strCommand == "debug")
    {
        if(activeFortunastake.status == FORTUNASTAKE_REMOTELY_ENABLED) return "fortunastake started remotely";
        if(activeFortunastake.status == FORTUNASTAKE_INPUT_TOO_NEW) return "fortunastake input must have at least 15 confirmations";
        if(activeFortunastake.status == FORTUNASTAKE_IS_CAPABLE) return "successfully started fortunastake";
        if(activeFortunastake.status == FORTUNASTAKE_STOPPED) return "fortunastake is stopped";
        if(activeFortunastake.status == FORTUNASTAKE_NOT_CAPABLE) return "not capable fortunastake: " + activeFortunastake.notCapableReason;
        if(activeFortunastake.status == FORTUNASTAKE_SYNC_IN_PROCESS) return "sync in process. Must wait until client is synced to start.";

        CTxIn vin = CTxIn();
        CPubKey pubkey = CScript();
        CKey key;
        bool found = activeFortunastake.GetFortunaStakeVin(vin, pubkey, key);
        if(!found){
            return "Missing fortunastake input, please look at the documentation for instructions on fortunastake creation";
        } else {
            return "No problems were found";
        }
    }

    if (strCommand == "create")
    {

        return "Not implemented yet, please look at the documentation for instructions on fortunastake creation";
    }

    if (strCommand == "current")
    {
        int winner = GetCurrentFortunaStake(1);
        if(winner >= 0) {
            return vecFortunastakes[winner].addr.ToString().c_str();
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
            if(fortunastakePayments.GetBlockPayee(nHeight, payee)){
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
        return (uint64_t)enforceFortunastakePaymentsTime;
    }

    if(strCommand == "connect")
    {
        std::string strAddress = "";
        if (params.size() == 2){
            strAddress = params[1].get_str().c_str();
        } else {
            throw runtime_error(
                "Fortunastake address required\n");
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
    	std::vector<CFortunastakeConfig::CFortunastakeEntry> mnEntries;
    	mnEntries = fortunastakeConfig.getEntries();

        Object resultObj;

        BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
    		Object mnObj;
    		mnObj.push_back(Pair("alias", mne.getAlias()));
    		mnObj.push_back(Pair("address", mne.getIp()));
    		mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
    		mnObj.push_back(Pair("txHash", mne.getTxHash()));
    		mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
    		resultObj.push_back(Pair("fortunastake", mnObj));
    	}

    	return resultObj;
    }

    if (strCommand == "outputs"){
        // Find possible candidates
        vector<COutput> possibleCoins = activeFortunastake.SelectCoinsFortunastake();

        Object obj;
        BOOST_FOREACH(COutput& out, possibleCoins) {
            obj.push_back(Pair(out.tx->GetHash().ToString().c_str(), boost::lexical_cast<std::string>(out.i)));
        }

        return obj;

    }

    if(strCommand == "status")
    {
        std::vector<CFortunastakeConfig::CFortunastakeEntry> mnEntries;
        mnEntries = fortunastakeConfig.getEntries();
        Object mnObj;

            CScript pubkey;
            pubkey = GetScriptForDestination(activeFortunastake.pubKeyFortunastake.GetID());
            CTxDestination address1;
            ExtractDestination(pubkey, address1);
            CBitcoinAddress address2(address1);
            if (activeFortunastake.pubKeyFortunastake.IsFullyValid()) {
                CScript pubkey;
                CTxDestination address1;
                std::string address = "";
                bool found = false;
                Object localObj;
                localObj.push_back(Pair("vin", activeFortunastake.vin.ToString().c_str()));
                localObj.push_back(Pair("service", activeFortunastake.service.ToString().c_str()));
                LOCK(cs_fortunastakes);
                BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes) {
                    if (mn.vin == activeFortunastake.vin) {
                        //int mnRank = GetFortunastakeRank(mn, pindexBest);
                        pubkey = GetScriptForDestination(mn.pubkey.GetID());
                        ExtractDestination(pubkey, address1);
                        CBitcoinAddress address2(address1);
                        address = address2.ToString();
                        localObj.push_back(Pair("payment_address", address));
                        //localObj.push_back(Pair("rank", GetFortunastakeRank(mn, pindexBest)));
                        localObj.push_back(Pair("network_status", mn.IsActive() ? "active" : "registered"));
                        if (mn.IsActive()) {
                          localObj.push_back(Pair("activetime",(mn.lastTimeSeen - mn.now)));

                        }
                        localObj.push_back(Pair("earnings", mn.payValue));
                        found = true;
                        break;
                    }
                }
                string reason;
                if(activeFortunastake.status == FORTUNASTAKE_REMOTELY_ENABLED) reason = "fortunastake started remotely";
                if(activeFortunastake.status == FORTUNASTAKE_INPUT_TOO_NEW) reason = "fortunastake input must have at least 15 confirmations";
                if(activeFortunastake.status == FORTUNASTAKE_IS_CAPABLE) reason = "successfully started fortunastake";
                if(activeFortunastake.status == FORTUNASTAKE_STOPPED) reason = "fortunastake is stopped";
                if(activeFortunastake.status == FORTUNASTAKE_NOT_CAPABLE) reason = "not capable fortunastake: " + activeFortunastake.notCapableReason;
                if(activeFortunastake.status == FORTUNASTAKE_SYNC_IN_PROCESS) reason = "sync in process. Must wait until client is synced to start.";

                if (!found) {
                    localObj.push_back(Pair("network_status", "unregistered"));
                    if (activeFortunastake.status != 9 && activeFortunastake.status != 7)
                    {
                        localObj.push_back(Pair("notCapableReason", reason));
                    }
                } else {
                    localObj.push_back(Pair("local_status", reason));
                }


                //localObj.push_back(Pair("address", address2.ToString().c_str()));

                mnObj.push_back(Pair("local",localObj));
            } else {
                Object localObj;
                localObj.push_back(Pair("status", "unconfigured"));
                mnObj.push_back(Pair("local",localObj));
            }

            BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry& mne, fortunastakeConfig.getEntries()) {
                Object remoteObj;
                std::string address = mne.getIp();

                CTxIn vin;
                CTxDestination address1;
                CActiveFortunastake amn;
                CPubKey pubKeyCollateralAddress;
                CKey keyCollateralAddress;
                CPubKey pubKeyFortunastake;
                CKey keyFortunastake;
                std::string errorMessage;
                std::string forTunaError;
                std::string vinError;

                if(!forTunaSigner.SetKey(mne.getPrivKey(), forTunaError, keyFortunastake, pubKeyFortunastake))
                {
                    errorMessage = forTunaError;
                }

                if (!amn.GetFortunaStakeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, mne.getTxHash(), mne.getOutputIndex(), vinError))
                {
                    errorMessage = vinError;
                }

                CScript pubkey = GetScriptForDestination(pubKeyCollateralAddress.GetID());
                ExtractDestination(pubkey, address1);
                CBitcoinAddress address2(address1);

                remoteObj.push_back(Pair("alias", mne.getAlias()));
                remoteObj.push_back(Pair("ipaddr", address));
				
				if(pwalletMain->IsLocked() || fWalletUnlockStakingOnly) {
					remoteObj.push_back(Pair("collateral", "Wallet is Locked"));
				} else {
					remoteObj.push_back(Pair("collateral", address2.ToString()));
				}
                //remoteObj.push_back(Pair("collateral", address2.ToString()));
				//remoteObj.push_back(Pair("collateral", CBitcoinAddress(mn->pubKeyCollateralAddress.GetID()).ToString()));

                bool mnfound = false;
                BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes)
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

Value masternode(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp  ||
        (strCommand != "start" && strCommand != "start-alias" && strCommand != "start-many" && strCommand != "stop" && strCommand != "stop-alias" && strCommand != "stop-many" && strCommand != "list" && strCommand != "list-conf" && strCommand != "count"  && strCommand != "enforce"
            && strCommand != "debug" && strCommand != "current" && strCommand != "winners" && strCommand != "genkey" && strCommand != "connect" && strCommand != "outputs" && strCommand != "status"))
		throw runtime_error(
			"fortunastake \"command\"... ( \"passphrase\" )\n"
			"Set of commands to execute fortunastake related actions\n"
			"\nArguments:\n"
			"1. \"command\"        (string or set of strings, required) The command to execute\n"
			"2. \"passphrase\"     (string, optional) The wallet passphrase\n"
			"\nAvailable commands:\n"
			"  count        - Print number of all known fortunastakes (optional: 'enabled', 'both')\n"
			"  current      - Print info on current fortunastake winner\n"
			"  debug        - Print fortunastake status\n"
			"  genkey       - Generate new fortunastakeprivkey\n"
			"  enforce      - Enforce fortunastake payments\n"
			"  outputs      - Print fortunastake compatible outputs\n"
            "  status       - Current fortunastake status\n"
			"  start        - Start fortunastake configured in denarius.conf\n"
			"  start-alias  - Start single fortunastake by assigned alias configured in fortunastake.conf\n"
			"  start-many   - Start all fortunastakes configured in fortunastake.conf\n"
			"  stop         - Stop fortunastake configured in denarius.conf\n"
			"  stop-alias   - Stop single fortunastake by assigned alias configured in fortunastake.conf\n"
			"  stop-many    - Stop all fortunastakes configured in fortunastake.conf\n"
			"  list         - Print list of all known fortunastakes (see fortunastakelist for more info)\n"
			"  list-conf    - Print fortunastake.conf in JSON format\n"
			"  winners      - Print list of fortunastake winners\n"
			//"  vote-many    - Vote on a Denarius initiative\n"
			//"  vote         - Vote on a Denarius initiative\n"
            );
    if (strCommand == "stop")
    {
        if(!fFortunaStake) return "You must set fortunastake=1 in the configuration";

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
        if(!activeFortunastake.StopFortunaStake(errorMessage)) {
        	return "Stop Failed: " + errorMessage;
        }
        pwalletMain->Lock();

        if(activeFortunastake.status == FORTUNASTAKE_STOPPED) return "Successfully Stopped Fortunastake";
        if(activeFortunastake.status == FORTUNASTAKE_NOT_CAPABLE) return "Not a capable Fortunastake";

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

    	BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
    		if(mne.getAlias() == alias) {
    			found = true;
    			std::string errorMessage;
    			bool result = activeFortunastake.StopFortunaStake(mne.getIp(), mne.getPrivKey(), errorMessage);

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

		BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
			total++;

			std::string errorMessage;
			bool result = activeFortunastake.StopFortunaStake(mne.getIp(), mne.getPrivKey(), errorMessage);

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
		returnObj.push_back(Pair("overall", "Successfully stopped " + boost::lexical_cast<std::string>(successful) + " fortunastakes, failed to stop " +
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

        if (strCommand != "active" && strCommand != "txid" && strCommand != "pubkey" && strCommand != "lastseen" && strCommand != "lastpaid" && strCommand != "activeseconds" && strCommand != "rank" && strCommand != "n" && strCommand != "full" && strCommand != "protocol"){
            throw runtime_error(
                "list supports 'active', 'txid', 'pubkey', 'lastseen', 'lastpaid', 'activeseconds', 'rank', 'n', 'protocol', 'full'\n");
        }

        Object obj;
        BOOST_FOREACH(CFortunaStake mn, vecFortunastakes) {
            mn.Check();

            if(strCommand == "active"){
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int)mn.IsEnabled()));
            } else if (strCommand == "txid") {
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
            } else if (strCommand == "n") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int64_t)mn.vin.prevout.n));
            } else if (strCommand == "lastpaid") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       mn.nBlockLastPaid));
            } else if (strCommand == "lastseen") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int64_t)mn.lastTimeSeen));
            } else if (strCommand == "activeseconds") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int64_t)(mn.lastTimeSeen - mn.now)));
            } else if (strCommand == "rank") {
                obj.push_back(Pair(mn.addr.ToString().c_str(),       (int)(GetFortunastakeRank(mn, pindexBest))));
            }
			else if (strCommand == "full") {
                Object list;
                list.push_back(Pair("active",        (int)mn.IsEnabled()));
                list.push_back(Pair("txid",           mn.vin.prevout.hash.ToString().c_str()));
                list.push_back(Pair("n",       (int64_t)mn.vin.prevout.n));

                CScript pubkey;
                pubkey =GetScriptForDestination(mn.pubkey.GetID());
                CTxDestination address1;
                ExtractDestination(pubkey, address1);
                CBitcoinAddress address2(address1);

                list.push_back(Pair("pubkey",         address2.ToString().c_str()));
                list.push_back(Pair("protocolversion",       (int64_t)mn.protocolVersion));
                list.push_back(Pair("lastseen",       (int64_t)mn.lastTimeSeen));
                list.push_back(Pair("activeseconds",  (int64_t)(mn.lastTimeSeen - mn.now)));
                list.push_back(Pair("rank",           (int)(GetFortunastakeRank(mn, pindexBest))));
                list.push_back(Pair("lastpaid",       mn.nBlockLastPaid));
                obj.push_back(Pair(mn.addr.ToString().c_str(), list));
            }
        }
        return obj;
    }
    if (strCommand == "count") return (int)vecFortunastakes.size();

    if (strCommand == "start")
    {
        if(!fFortunaStake) return "you must set fortunastake=1 in the configuration";

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

        if(activeFortunastake.status != FORTUNASTAKE_REMOTELY_ENABLED && activeFortunastake.status != FORTUNASTAKE_IS_CAPABLE){
            activeFortunastake.status = FORTUNASTAKE_NOT_PROCESSED; // TODO: consider better way
            std::string errorMessage;
            activeFortunastake.ManageStatus();
            pwalletMain->Lock();
        }

        if(activeFortunastake.status == FORTUNASTAKE_REMOTELY_ENABLED) return "fortunastake started remotely";
        if(activeFortunastake.status == FORTUNASTAKE_INPUT_TOO_NEW) return "fortunastake input must have at least 15 confirmations";
        if(activeFortunastake.status == FORTUNASTAKE_STOPPED) return "fortunastake is stopped";
        if(activeFortunastake.status == FORTUNASTAKE_IS_CAPABLE) return "successfully started fortunastake";
        if(activeFortunastake.status == FORTUNASTAKE_NOT_CAPABLE) return "not capable fortunastake: " + activeFortunastake.notCapableReason;
        if(activeFortunastake.status == FORTUNASTAKE_SYNC_IN_PROCESS) return "sync in process. Must wait until client is synced to start.";

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

    	BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
    		if(mne.getAlias() == alias) {
    			found = true;
    			std::string errorMessage;
    			bool result = activeFortunastake.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage);

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

		std::vector<CFortunastakeConfig::CFortunastakeEntry> mnEntries;
		mnEntries = fortunastakeConfig.getEntries();

		int total = 0;
		int successful = 0;
		int fail = 0;

		Object resultsObj;

		BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
			total++;

			std::string errorMessage;
			bool result = activeFortunastake.Register(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage);

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
		returnObj.push_back(Pair("overall", "Successfully started " + boost::lexical_cast<std::string>(successful) + " fortunastakes, failed to start " +
				boost::lexical_cast<std::string>(fail) + ", total " + boost::lexical_cast<std::string>(total)));
		returnObj.push_back(Pair("detail", resultsObj));

		return returnObj;
    }

    if (strCommand == "debug")
    {
        if(activeFortunastake.status == FORTUNASTAKE_REMOTELY_ENABLED) return "fortunastake started remotely";
        if(activeFortunastake.status == FORTUNASTAKE_INPUT_TOO_NEW) return "fortunastake input must have at least 15 confirmations";
        if(activeFortunastake.status == FORTUNASTAKE_IS_CAPABLE) return "successfully started fortunastake";
        if(activeFortunastake.status == FORTUNASTAKE_STOPPED) return "fortunastake is stopped";
        if(activeFortunastake.status == FORTUNASTAKE_NOT_CAPABLE) return "not capable fortunastake: " + activeFortunastake.notCapableReason;
        if(activeFortunastake.status == FORTUNASTAKE_SYNC_IN_PROCESS) return "sync in process. Must wait until client is synced to start.";

        CTxIn vin = CTxIn();
        CPubKey pubkey = CScript();
        CKey key;
        bool found = activeFortunastake.GetFortunaStakeVin(vin, pubkey, key);
        if(!found){
            return "Missing fortunastake input, please look at the documentation for instructions on fortunastake creation";
        } else {
            return "No problems were found";
        }
    }

    if (strCommand == "create")
    {

        return "Not implemented yet, please look at the documentation for instructions on fortunastake creation";
    }

    if (strCommand == "current")
    {
        int winner = GetCurrentFortunaStake(1);
        if(winner >= 0) {
            return vecFortunastakes[winner].addr.ToString().c_str();
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
            if(fortunastakePayments.GetBlockPayee(nHeight, payee)){
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
        return (uint64_t)enforceFortunastakePaymentsTime;
    }

    if(strCommand == "connect")
    {
        std::string strAddress = "";
        if (params.size() == 2){
            strAddress = params[1].get_str().c_str();
        } else {
            throw runtime_error(
                "Fortunastake address required\n");
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
    	std::vector<CFortunastakeConfig::CFortunastakeEntry> mnEntries;
    	mnEntries = fortunastakeConfig.getEntries();

        Object resultObj;

        BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry mne, fortunastakeConfig.getEntries()) {
    		Object mnObj;
    		mnObj.push_back(Pair("alias", mne.getAlias()));
    		mnObj.push_back(Pair("address", mne.getIp()));
    		mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
    		mnObj.push_back(Pair("txHash", mne.getTxHash()));
    		mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
    		resultObj.push_back(Pair("fortunastake", mnObj));
    	}

    	return resultObj;
    }

    if (strCommand == "outputs"){
        // Find possible candidates
        vector<COutput> possibleCoins = activeFortunastake.SelectCoinsFortunastake();

        Object obj;
        BOOST_FOREACH(COutput& out, possibleCoins) {
            obj.push_back(Pair(out.tx->GetHash().ToString().c_str(), boost::lexical_cast<std::string>(out.i)));
        }

        return obj;

    }

    if(strCommand == "status")
    {
        std::vector<CFortunastakeConfig::CFortunastakeEntry> mnEntries;
        mnEntries = fortunastakeConfig.getEntries();
        Object mnObj;

            CScript pubkey;
            pubkey = GetScriptForDestination(activeFortunastake.pubKeyFortunastake.GetID());
            CTxDestination address1;
            ExtractDestination(pubkey, address1);
            CBitcoinAddress address2(address1);
            if (activeFortunastake.pubKeyFortunastake.IsFullyValid()) {
                CScript pubkey;
                pubkey = GetScriptForDestination(activeFortunastake.pubKeyFortunastake.GetID());
                CTxDestination address1;
                ExtractDestination(pubkey, address1);
                if (pubkey.IsPayToScriptHash())
                CBitcoinAddress address2(address1);

                Object localObj;
                localObj.push_back(Pair("vin", activeFortunastake.vin.ToString().c_str()));
                localObj.push_back(Pair("service", activeFortunastake.service.ToString().c_str()));
                localObj.push_back(Pair("status", activeFortunastake.status));
                localObj.push_back(Pair("address", address2.ToString().c_str()));
                localObj.push_back(Pair("notCapableReason", activeFortunastake.notCapableReason.c_str()));
                mnObj.push_back(Pair("local",localObj));
            } else {
                Object localObj;
                localObj.push_back(Pair("status", "unconfigured"));
                mnObj.push_back(Pair("local",localObj));
            }

            BOOST_FOREACH(CFortunastakeConfig::CFortunastakeEntry& mne, fortunastakeConfig.getEntries()) {
                Object remoteObj;
                std::string address = mne.getIp();

                CTxIn vin;
                CTxDestination address1;
                CActiveFortunastake amn;
                CPubKey pubKeyCollateralAddress;
                CKey keyCollateralAddress;
                CPubKey pubKeyFortunastake;
                CKey keyFortunastake;
                std::string errorMessage;
                std::string forTunaError;
                std::string vinError;

                if(!forTunaSigner.SetKey(mne.getPrivKey(), forTunaError, keyFortunastake, pubKeyFortunastake))
                {
                    errorMessage = forTunaError;
                }

                if (!amn.GetFortunaStakeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, mne.getTxHash(), mne.getOutputIndex(), vinError))
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
                BOOST_FOREACH(CFortunaStake& mn, vecFortunastakes)
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
