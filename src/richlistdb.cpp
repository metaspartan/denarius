// Copyright (c) 2015 The Sling developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include "richlistdata.h"
#include "richlistdb.h"
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <leveldb/slice.h>
#include "db.h"
#include "util.h"

CCriticalSection cs_richlistdb;

leveldb::DB *richListDB = NULL;

namespace fs = boost::filesystem;

bool RichListDB::Open(const char* pszMode)
{
    if (richListDB)
    {
        pdb = richListDB;
        return true;
    };

    bool fCreate = strchr(pszMode, 'c');

    fs::path fullpath = GetDataDir() / "richlist";

    if (!fCreate
        && (!fs::exists(fullpath)
            || !fs::is_directory(fullpath)))
    {
        printf("RichListDB::open() - DB does not exist.");
        return false;
    };

    leveldb::Options options;
    options.create_if_missing = fCreate;
    leveldb::Status s = leveldb::DB::Open(options, fullpath.string(), &richListDB);

    if (!s.ok())
    {
        printf("RichListDB::open() - Error opening db: %s.", s.ToString().c_str());
        return false;
    };

    pdb = richListDB;

    return true;
};

class RichListDbBatchScanner : public leveldb::WriteBatch::Handler
{
public:
    std::string needle;
    bool* deleted;
    std::string* foundValue;
    bool foundEntry;

    RichListDbBatchScanner() : foundEntry(false) {}

    virtual void Put(const leveldb::Slice& key, const leveldb::Slice& value)
    {
        if (key.ToString() == needle)
        {
            foundEntry = true;
            *deleted = false;
            *foundValue = value.ToString();
        };
    };

    virtual void Delete(const leveldb::Slice& key)
    {
        if (key.ToString() == needle)
        {
            foundEntry = true;
            *deleted = true;
        };
    };
};

bool RichListDB::ScanBatch(const CDataStream& key, std::string* value, bool* deleted) const
{
    if (!activeBatch)
        return false;

    *deleted = false;
    RichListDbBatchScanner scanner;
    scanner.needle = key.str();
    scanner.deleted = deleted;
    scanner.foundValue = value;
    leveldb::Status s = activeBatch->Iterate(&scanner);
    if (!s.ok())
    {
        printf("RichListDB ScanBatch error: %s", s.ToString().c_str());
        return false;
    };

    return scanner.foundEntry;
}

bool RichListDB::TxnBegin()
{
    if (activeBatch)
        return true;
    activeBatch = new leveldb::WriteBatch();
    return true;
};

bool RichListDB::TxnCommit()
{
    if (!activeBatch)
        return false;

    leveldb::WriteOptions writeOptions;
    writeOptions.sync = true;
    leveldb::Status status = pdb->Write(writeOptions, activeBatch);
    delete activeBatch;
    activeBatch = NULL;

    if (!status.ok())
    {
        printf("RichListDB batch commit failure: %s", status.ToString().c_str());
        return false;
    };

    return true;
};

bool RichListDB::TxnAbort()
{
    delete activeBatch;
    activeBatch = NULL;
    return true;
};

bool RichListDB::ReadRichList(CRichListData& richListData)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'r'; // rich
    ssKey << 'l'; // list

    std::string strValue;

    bool readFromDb = true;
    if (activeBatch)
    {
        // -- check activeBatch first
        bool deleted = false;
        readFromDb = ScanBatch(ssKey, &strValue, &deleted) == false;
        if (deleted)
            return false;
    };

    if (readFromDb)
    {
        leveldb::Status s = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &strValue);
        if (!s.ok())
        {
            if (s.IsNotFound())
                return false;
            printf("RichListDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> richListData;
    } catch (std::exception& e) {
        printf("RichListDB::ReadRichList() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool RichListDB::WriteRichList(CRichListData richListData)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'r'; // rich
    ssKey << 'l'; // list

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(richListData));
    ssValue << richListData;

    if (activeBatch)
    {
        activeBatch->Put(ssKey.str(), ssValue.str());
        return true;
    };

    leveldb::WriteOptions writeOptions;
    writeOptions.sync = true;
    leveldb::Status s = pdb->Put(writeOptions, ssKey.str(), ssValue.str());
    if (!s.ok())
    {
        printf("RichListDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};

bool WriteRichList(CRichListData richListData)
{
    LOCK(cs_richlistdb);
    RichListDB ldb;
    if (!ldb.Open("cw")
        || !ldb.TxnBegin())
        return false;

    if(ldb.WriteRichList(richListData))
    {
        ldb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadRichList(CRichListData& richListData)
{
    RichListDB ldb;
    if(!ldb.Open("r"))
        return false;

    if(ldb.ReadRichList(richListData))
        return true;
    else
        return false;
}
