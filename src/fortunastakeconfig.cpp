// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Darkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "net.h"
#include "fortunastakeconfig.h"
#include "util.h"

CFortunastakeConfig fortunastakeConfig;

void CFortunastakeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex) {
    CFortunastakeEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

void CFortunastakeConfig::purge(CFortunastakeEntry cme) {
    std::string line;
    std::string errMsg;
    boost::filesystem::path confPath(GetFortunastakeConfigFile());
    boost::filesystem::path tempPath(GetFortunastakeConfigFile().replace_extension(".temp"));
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

bool CFortunastakeConfig::read(std::string& strErr) {
    boost::filesystem::ifstream streamConfig(GetFortunastakeConfigFile());
    if (!streamConfig.good()) {
        return true; // No fortunastake.conf file is OK
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
            //strErr = "Could not parse fortunastake.conf line: " + line;
            printf("Could not parse fortunastake.conf line: %s\n", line.c_str());
            streamConfig.close();
            return false;
        }

        if(CService(ip).GetPort() != 19969 && CService(ip).GetPort() != 9969)  {
            strErr = "Invalid port (must be 9969 for mainnet or 19969 for testnet) detected in fortunastake.conf: " + line;
            streamConfig.close();
            return false;
        }

        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}
