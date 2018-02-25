// Copyright (c) 2015 The Sling developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef RICHLISTDATA_H
#define RICHLISTDATA_H

#include "util.h"


class CRichListData;
class CRichListDataItem;

// Returns true if data found, false if rich list not in db yet
bool LoadRichList(CRichListData& richList);
bool UpdateRichList(); // recomputes the rich list in the database

class CRichListDataItem
{
public:
    int nVersion;
    std::string dAddress;
    int64_t nBalance;

    CRichListDataItem()
    {
	nVersion = 0;
	nBalance = 0;
    }

    IMPLEMENT_SERIALIZE(
	READWRITE(nVersion);
	READWRITE(dAddress);
	READWRITE(nBalance);
    )

    bool operator < (const CRichListDataItem &rhs) const
    {
        return nBalance < rhs.nBalance;
    }

    bool operator > (const CRichListDataItem &rhs) const
    {
        return nBalance > rhs.nBalance;
    }
};

class CRichListData
{
public:
    int nVersion;
    std::map<std::string, CRichListDataItem> mapRichList;
    int nLastHeight;

    CRichListData()
    {
	nVersion = 0;
    }
    
    IMPLEMENT_SERIALIZE(
        READWRITE(nVersion);
	READWRITE(nLastHeight);
	READWRITE(mapRichList);
    )
};



#endif // RICHLISTDATA_H
