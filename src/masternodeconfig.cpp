// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Darkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "net.h"
#include "masternodeconfig.h"
#include "util.h"

CMasternodeConfig masternodeConfig;

void CMasternodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex) {
    CMasternodeEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

void CMasternodeConfig::purge(CMasternodeEntry cme) {
    std::string line;
    std::string errMsg;
    boost::filesystem::path confPath(GetMasternodeConfigFile());
    boost::filesystem::path tempPath(GetMasternodeConfigFile().replace_extension(".temp"));
    boost::filesystem::ifstream fin(confPath);
    boost::filesystem::ofstream temp(tempPath);

    while (getline(fin, line)) {
        std::istringstream iss(line);
        std::string alias, ip, privKey, txHash, outputIndex;
        iss.str(line);
        iss.clear();
        iss >> alias >> ip >> privKey >> txHash >> outputIndex;
        if (alias != cme.getAlias())
        {
            temp << line << std::endl;
        }
    }

    temp.close();
    fin.close();

    boost::filesystem::remove(confPath);
    boost::filesystem::rename(tempPath,confPath);

    // clear the entries and re-read the config file
    entries.clear();
    read(errMsg);
}

bool CMasternodeConfig::read(std::string& strErr) {
    boost::filesystem::ifstream streamConfig(GetMasternodeConfigFile());
    if (!streamConfig.good()) {
        return true; // No masternode.conf file is OK
    }

    for(std::string line; std::getline(streamConfig, line); )
    {
        if(line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        std::string alias, ip, privKey, txHash, outputIndex;
        iss.str(line);
        iss.clear();
        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            //strErr = "Could not parse masternode.conf line: " + line;
            printf("Could not parse masternode.conf line: %s\n", line.c_str());
            streamConfig.close();
            return false;
        }

        if(CService(ip).GetPort() != 19999 && CService(ip).GetPort() != 9999)  {
            strErr = "Invalid port (must be 9999 for mainnet or 19999 for testnet) detected in masternode.conf: " + line;
            streamConfig.close();
            return false;
        }

        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}
