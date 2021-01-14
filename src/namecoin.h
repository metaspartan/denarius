#include "db.h"
#include "txdb-leveldb.h"
#include "denariusrpc.h"
#include "base58.h"
#include "main.h"
#include "hooks.h"

class CBitcoinAddress;
class CKeyStore;
struct NameIndexStats;

static const int NAMECOIN_TX_VERSION = 0x0333; //0x0333 is initial version
static const unsigned int MAX_NAME_LENGTH = 512;
static const unsigned int MAX_VALUE_LENGTH = 20*1024;
static const int MAX_RENTAL_DAYS = 100*365; //100 years
static const int OP_NAME_NEW = 0x01;
static const int OP_NAME_UPDATE = 0x02;
static const int OP_NAME_DELETE = 0x03;
static const unsigned int NAMEINDEX_CHAIN_SIZE = 1000;

static const int RELEASE_HEIGHT = 4146600; //Old Height: 65536
typedef std::vector<unsigned char> CNameVal;

class CNameIndex
{
public:
    CDiskTxPos txPos;
    int nHeight;
    int op;
    CNameVal value;

    CNameIndex() : nHeight(0), op(0) {}

    CNameIndex(CDiskTxPos txPos, int nHeight, CNameVal value) :
        txPos(txPos), nHeight(nHeight), value(value) {}

    IMPLEMENT_SERIALIZE
    (
        READWRITE(txPos);
        READWRITE(nHeight);
        READWRITE(op);
        READWRITE(value);
    )
};

// CNameRecord is all the data that is saved (in denariusnames.dat) with associated name
class CNameRecord
{
public:
    std::vector<CNameIndex> vtxPos;
    int nExpiresAt;
    int nLastActiveChainIndex;  // position in vtxPos of first tx in last active chain of name_new -> name_update -> name_update -> ....

    CNameRecord() : nExpiresAt(0), nLastActiveChainIndex(0) {}
    bool deleted()
    {
        if (!vtxPos.empty())
            return vtxPos.back().op == OP_NAME_DELETE;
        else return true;
    }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(vtxPos);
        READWRITE(nExpiresAt);
        READWRITE(nLastActiveChainIndex);
    )
};

class CNameDB : public CDB
{
public:
    CNameDB(const char* pszMode="r+") : CDB("denariusnames.dat", pszMode) {}

    bool WriteName(const CNameVal& name, const CNameRecord& rec)
    {
        return Write(make_pair(std::string("namei"), name), rec);
    }

    bool ReadName(const CNameVal& name, CNameRecord& rec);

    // bool ReadName(const CNameVal& name, CNameRecord &rec)
    // {
    //     bool ret = Read(make_pair(std::string("namei"), name), rec);
    //     int s = rec.vtxPos.size();
    //     if (s > 0)
    //         assert(s > rec.nLastActiveChainIndex);
    //     return ret;
    // }

    bool ExistsName(const CNameVal& name)
    {
        return Exists(make_pair(std::string("namei"), name));
    }

    bool EraseName(const CNameVal& name)
    {
        return Erase(make_pair(std::string("namei"), name));
    }

    bool ScanNames(
            const CNameVal& name,
            unsigned int nMax,
            std::vector<
                std::pair<
                    CNameVal,
                    std::pair<CNameIndex, int>
                >
            >& nameScan
            );
    bool DumpToTextFile();
};

// extern std::map<std::vector<unsigned char>, uint256> mapMyNames;
extern std::map<CNameVal, std::set<uint256> > mapNamePending;

int IndexOfNameOutput(const CTransaction& tx);
bool GetNameCurrentAddress(const CNameVal& name, CBitcoinAddress &address, std::string &error);

std::string stringFromNameVal(const CNameVal& nameVal);
CNameVal nameValFromString(const std::string& str);
std::string stringFromOp(int op);

int64_t GetNameOpFee(const CBlockIndex* pindexBlock, const int nRentalDays, int op, const CNameVal& name, const CNameVal& value);
CAmount GetNameOpFee2(const CBlockIndex* pindexBlock, const int nRentalDays, int op, const CNameVal& name, const CNameVal& value);

struct NameTxInfo
{
    CNameVal name;
    CNameVal value;
    int nRentalDays;
    int op;
    int nOut;
    std::string err_msg; //in case function that takes this as argument have something to say about it

    //used only by DecodeNameScript()
    std::string strAddress;
    bool fIsMine;

    //used only by GetNameList()
    int nExpiresAt;

    NameTxInfo(): nRentalDays(-1), op(-1), nOut(-1), fIsMine(false), nExpiresAt(-1) {}
    NameTxInfo(CNameVal name, CNameVal value, int nRentalDays, int op, int nOut, std::string err_msg):
        name(name), value(value), nRentalDays(nRentalDays), op(op), nOut(nOut), err_msg(err_msg), fIsMine(false), nExpiresAt(-1) {}
};

bool DecodeNameTx(const CTransaction& tx, NameTxInfo& nti, bool checkAddressAndIfIsMine = false);
void GetNameList(const CNameVal& nameUniq, std::map<CNameVal, NameTxInfo> &mapNames, std::map<CNameVal, NameTxInfo> &mapPending);
bool GetNameValue(const CNameVal& name, CNameVal& value);

bool checkNameValues(NameTxInfo& ret);
bool DecodeNameScript(const CScript& script, NameTxInfo& ret, CScript::const_iterator& pc);
bool DecodeNameScript(const CScript& script, NameTxInfo& ret);
bool RemoveNameScriptPrefix(const CScript& scriptIn, CScript& scriptOut);

bool SignNameSignatureD(const CKeyStore& keystore, const CTransaction& txFrom, CTransaction& txTo, unsigned int nIn, int nHashType=SIGHASH_ALL);
struct NameTxReturn
{
     bool ok;
     std::string err_msg;
     RPCErrorCode err_code;
     std::string address;
     uint256 hex;   // Transaction hash in hex
};

NameTxReturn name_operation(const int op, const CNameVal& name, CNameVal value, const int nRentalDays, const string strAddress, bool fValueAsFilepath = false);

// NameTxReturn name_new(const std::vector<unsigned char> &vchName,
//               const std::vector<unsigned char> &vchValue,
//               const int nRentalDays, std::string strAddress);
// NameTxReturn name_update(const std::vector<unsigned char> &vchName,
//               const std::vector<unsigned char> &vchValue,
//               const int nRentalDays, std::string strAddress = "");
// NameTxReturn name_delete(const std::vector<unsigned char> &vchName);


struct nameTempProxy
{
    unsigned int nTime;
    CNameVal name;
    int op;
    uint256 hash;
    CNameIndex ind;
};