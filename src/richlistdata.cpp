// Copyright (c) 2015 The Sling developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "richlistdata.h"
#include "richlistdb.h"
#include "util.h"
#include "main.h"
#include "txdb.h"
#include "key.h"
#include "base58.h"
#include "init.h"
#include "wallet.h"

bool LoadRichList(CRichListData& richList)
{
    return ReadRichList(richList);
}

bool UpdateRichList()
{
    CRichListData richListData;
    int nLastHeight = 0;
    // First read out existing rich list
    bool fExists = ReadRichList(richListData);
    if(fExists)
	nLastHeight = richListData.nLastHeight;

    int nLastHeightProcessed = 0;
    LOCK(cs_main);
    CTxDB txdb("r");
    CBlockIndex* pindex = pindexBest;

    // find the first block in the main chain
    while(pindex->pprev)
    {
	pindex = pindex->pprev;
    }

    // iterate forward from first block to end
    while(pindex)
    {
	if(pindex->nHeight > nLastHeight)
	{
	    nLastHeightProcessed = pindex->nHeight;

	    // read the transactions in this block and update the rich list balances
	    CBlock block;
	    block.ReadFromDisk(pindex, true);
	    // iterate all of the transactions in the block
	    BOOST_FOREACH(CTransaction& tx, block.vtx)
	    {
		uint256 hashTx = tx.GetHash();
		// inputs
		if(!tx.IsCoinBase()) 
		{
            	MapPrevTx mapInputs;
	    	    std::map<uint256, CTxIndex> mapQueuedChanges;
	    	    bool fInvalid;
            	    if (tx.FetchInputs(txdb, mapQueuedChanges, true, false, mapInputs, fInvalid))
                    {
		      if(!fInvalid)
		      {
		        MapPrevTx::const_iterator mi;
		        for(MapPrevTx::const_iterator mi = mapInputs.begin(); mi != mapInputs.end(); ++mi)
 	 	        {
 		            BOOST_FOREACH(const CTxOut &atxout, (*mi).second.second.vout)
			    { 
			        // get the address
			        CTxDestination dest;
			        if(ExtractDestination(atxout.scriptPubKey, dest))
			        {
			   
			    	    std::string dnrAddress = CBitcoinAddress(dest).ToString();
				        CRichListDataItem richListDataItem;
				        richListDataItem.dAddress = dnrAddress;
				        if(richListData.mapRichList.find(dnrAddress) == richListData.mapRichList.end())
				            richListData.mapRichList.insert(make_pair(dnrAddress, richListDataItem));
					
				        richListData.mapRichList[dnrAddress].nBalance = richListData.mapRichList[dnrAddress].nBalance - atxout.nValue;
									    
			        }
			    }
		        }
		      }
                    }
   		}
	   
	        // outputs
	        BOOST_FOREACH(const CTxOut &atxout, tx.vout) 
		{
		    CTxDestination dest;
		    if(ExtractDestination(atxout.scriptPubKey, dest))
		    {
			    std::string dnrAddress = CBitcoinAddress(dest).ToString();
			    CRichListDataItem richListDataItem;
			    richListDataItem.dAddress = dnrAddress;
			    if(richListData.mapRichList.find(dnrAddress) == richListData.mapRichList.end())
				richListData.mapRichList.insert(make_pair(dnrAddress, richListDataItem));

			    richListData.mapRichList[dnrAddress].nBalance = richListData.mapRichList[dnrAddress].nBalance + atxout.nValue;
		
		    }
		}
	    }
	}
        pindex = pindex->pnext;
    }

    richListData.nLastHeight = nLastHeightProcessed;
    return WriteRichList(richListData);
}
