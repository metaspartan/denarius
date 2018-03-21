// Copyright (c) 2015 The Sling developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef RICHLISTDB_H
#define RICHLISTDB_H

#include "richlistdata.h"
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include "db.h"

extern CCriticalSection cs_richlistdb; // for locking leveldb operations

bool WriteRichList(CRichListData richListData);
bool ReadRichList(CRichListData& richListData);

bool TryOpenRichListDB();

class RichListDB
{
public:
    RichListDB()
    {
        activeBatch = NULL;
    };

    ~RichListDB()
    {
        if (activeBatch)
            delete activeBatch;
    };

    bool Open(const char* pszMode="r+");

    bool ScanBatch(const CDataStream& key, std::string* value, bool* deleted) const;

    bool TxnBegin();
    bool TxnCommit();
    bool TxnAbort();

    bool WriteRichList(CRichListData richListData);
    bool ReadRichList(CRichListData& richListData);

    leveldb::DB *pdb;       // points to the global instance
    leveldb::WriteBatch *activeBatch;

};

#endif // RICHLISTDB_H
