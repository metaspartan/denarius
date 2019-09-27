#include "db.h"
#include "txdb-leveldb.h"
#include "denariusrpc.h"
#include "base58.h"

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
static const unsigned int NAMEINDEX_CHAIN_SIZE = 100;

static const int RELEASE_HEIGHT = 1<<16;

class CNameIndex
{
public:
    CDiskTxPos txPos;
    int nHeight;
    int op;
    std::vector<unsigned char> vchValue;

    CNameIndex() : nHeight(0), op(0) {}

    CNameIndex(CDiskTxPos txPos, int nHeight, std::vector<unsigned char> vchValue) :
        txPos(txPos), nHeight(nHeight), vchValue(vchValue) {}

    IMPLEMENT_SERIALIZE
    (
        READWRITE(txPos);
        READWRITE(nHeight);
        READWRITE(op);
        READWRITE(vchValue);
    )
};

// CNameRecord is all the data that is saved (in nameindex.dat) with associated name
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
    CNameDB(const char* pszMode="r+") : CDB("namedb.dat", pszMode) {}

    bool WriteName(const std::vector<unsigned char>& name, const CNameRecord &rec)
    {
        return Write(make_pair(std::string("namei"), name), rec);
    }

    bool ReadName(const std::vector<unsigned char>& name, CNameRecord &rec)
    {
        bool ret = Read(make_pair(std::string("namei"), name), rec);
        int s = rec.vtxPos.size();
        if (s > 0)
            assert(s > rec.nLastActiveChainIndex);
        return ret;
    }

    bool ExistsName(const std::vector<unsigned char>& name)
    {
        return Exists(make_pair(std::string("namei"), name));
    }

    bool EraseName(const std::vector<unsigned char>& name)
    {
        return Erase(make_pair(std::string("namei"), name));
    }

    bool ScanNames(
            const std::vector<unsigned char>& vchName,
            unsigned int nMax,
            std::vector<
                std::pair<
                    std::vector<unsigned char>,
                    std::pair<CNameIndex, int>
                >
            >& nameScan
            );
    bool DumpToTextFile();
};

extern std::map<std::vector<unsigned char>, uint256> mapMyNames;
extern std::map<std::vector<unsigned char>, std::set<uint256> > mapNamePending;

int IndexOfNameOutput(const CTransaction& tx);
bool GetNameCurrentAddress(const std::vector<unsigned char> &vchName, CBitcoinAddress &address, std::string &error);
std::string stringFromVch(const std::vector<unsigned char> &vch);
std::vector<unsigned char> vchFromString(const std::string &str);
std::string nameFromOp(int op);

int64_t GetNameOpFee(const CBlockIndex* pindexBlock, const int nRentalDays, int op, const std::vector<unsigned char> &vchName, const std::vector<unsigned char> &vchValue);

struct NameTxInfo
{
    std::vector<unsigned char> vchName;
    std::vector<unsigned char> vchValue;
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
    NameTxInfo(std::vector<unsigned char> vchName1, std::vector<unsigned char> vchValue1, int nRentalDays1, int op1, int nOut1, std::string err_msg1):
        vchName(vchName1), vchValue(vchValue1), nRentalDays(nRentalDays1), op(op1), nOut(nOut1), err_msg(err_msg1), fIsMine(false), nExpiresAt(-1) {}
};

bool DecodeNameScript(const CScript& script, NameTxInfo& ret, bool checkValuesCorrectness = true, bool checkAddressAndIfIsMine = false);
bool DecodeNameScript(const CScript& script, NameTxInfo& ret, CScript::const_iterator& pc, bool checkValuesCorrectness = true, bool checkAddressAndIfIsMine = false);
bool DecodeNameTx(const CTransaction& tx, NameTxInfo& nti, bool checkValuesCorrectness = true, bool checkAddressAndIfIsMine = false);
void GetNameList(const std::vector<unsigned char> &vchNameUniq, std::map<std::vector<unsigned char>, NameTxInfo> &mapNames, std::map<std::vector<unsigned char>, NameTxInfo> &mapPending);
bool GetNameValue(const std::vector<unsigned char> &vchName, std::vector<unsigned char> &vchValue, bool checkPending);

struct NameTxReturn
{
     bool ok;
     std::string err_msg;
     RPCErrorCode err_code;
     std::string address;
     uint256 hex;   // Transaction hash in hex
};
NameTxReturn name_new(const std::vector<unsigned char> &vchName,
              const std::vector<unsigned char> &vchValue,
              const int nRentalDays, std::string strAddress);
NameTxReturn name_update(const std::vector<unsigned char> &vchName,
              const std::vector<unsigned char> &vchValue,
              const int nRentalDays, std::string strAddress = "");
NameTxReturn name_delete(const std::vector<unsigned char> &vchName);


struct nameTempProxy
{
    unsigned int nTime;
    std::vector<unsigned char> vchName;
    int op;
    uint256 hash;
    CNameIndex ind;
};
