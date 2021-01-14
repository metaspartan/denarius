// Copyright (c) 2021 Denarius Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef DENARIUS_HOOKS_H
#define DENARIUS_HOOKS_H

class CWalletTx;
class CScript;
class CTransaction;
class CTxDB;
class uint256;
class CTxIndex;
class CBlockIndex;
class CDiskTxPos;
class CBlock;
class CTxOut;

#include <map>
#include <vector>
#include <string>
using namespace std;

typedef int64_t CAmount;
typedef std::shared_ptr<const CTransaction> CTransactionRef;

struct nameTempProxy;

class CHooks
{
public:
    virtual bool IsNameFeeEnough(const CTransaction& tx, const CAmount& txFee) = 0;
    virtual bool CheckInputs(const CTransaction& tx, const CBlockIndex* pindexBlock, std::vector<nameTempProxy> &vName, const CDiskTxPos& pos, const CAmount& txFee) = 0;
    virtual bool DisconnectInputs(const CTransaction& tx) = 0;
    virtual bool ConnectBlock(CBlockIndex* pindex, const std::vector<nameTempProxy> &vName) = 0;
    virtual bool ExtractAddress(const CScript& script, std::string& address) = 0;
    virtual void AddToPendingNames(const CTransaction& tx) = 0;
    virtual bool IsMine(const CTxOut& txout) = 0;
    virtual bool IsNameTx(int nVersion) = 0;
    virtual bool IsNameScript(CScript scr) = 0;
    virtual bool deletePendingName(const CTransaction& tx) = 0;
    virtual bool getNameValue(const string& name, string& value) = 0;
    virtual bool DumpToTextFile() = 0;
};

extern CHooks* InitHook();
extern std::string GetDefaultDataDirSuffix();
extern CHooks* hooks;

#endif