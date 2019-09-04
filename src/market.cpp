// Copyright (c) 2019 The Denarius Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <leveldb/slice.h>

#include "db.h"
#include "net.h"
#include "init.h"
#include "util.h"
#include "main.h"
#include "wallet.h"
#include "keystore.h"
#include "key.h"
#include "base58.h"
#include "script.h"
#include "txdb-leveldb.h"
#include "market.h"

CCriticalSection cs_marketdb;
CCriticalSection cs_markets;

leveldb::DB *marketDB = NULL;
namespace fs = boost::filesystem;

using namespace std;

std::map<uint256, CSignedMarketListing> mapListings; // stored in local db
std::map<std::string, std::set<CSignedMarketListing> > mapListingsByCategory; // maintained in memory
std::map<uint256, CBuyRequest> mapBuyRequests;
std::map<uint256, CBuyAccept> mapBuyAccepts;
std::map<uint256, CBuyReject> mapBuyRejects;
std::map<uint256, CDeliveryDetails> mapDeliveryDetails;
std::map<uint256, CRefundRequest> mapRefundRequests;
std::map<uint256, CEscrowRelease> mapEscrowReleases;
std::map<uint256, CEscrowPayment> mapEscrowPayments;
std::map<uint256, CCancelListing> mapCancelListings;
std::map<uint256, CPaymentRequest> mapPaymentRequests;

bool AddMultisigAddress(CPubKey sellerKey, CPubKey buyerKey, std::string& escrowAddress, std::string& errors)
{
    int nRequired = 2; // 2 of 2 multisig

    // Gather public keys
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(2);
    pubkeys[0] = sellerKey;
    pubkeys[1] = buyerKey;

    // Construct using pay-to-script-hash:
    CScript inner;
    inner.SetMultisigpub(nRequired, pubkeys);
    CScriptID innerID = inner.GetID();
    if (!pwalletMain->AddCScript(inner))
    {
        errors = "AddCScript() failed";
        return false;
    }

    pwalletMain->SetAddressBookName(innerID, "dMarket Escrow");
    escrowAddress = CBitcoinAddress(innerID).ToString();
    return true;
}

bool CreateEscrowLockTx(std::string escrowAddress, int64_t nValue, std::string& strError, CWalletTx& wtxNew)
{
    CReserveKey reservekey(pwalletMain);
    int64_t nFeeRequired;
    std::string sNarr = "";

    if (nValue <= 0)
    {
        strError = "Invalid amount";
        return false;
    }
    if (nValue + nTransactionFee > pwalletMain->GetBalance())
    {
        strError = "Insufficient funds";
        return false;
    }

    // Parse Bitcoin address
    CBitcoinAddress address(escrowAddress);
    CScript scriptPubKey;
    scriptPubKey.SetDestination(address.Get());

    if (!pwalletMain->CreateTransaction(scriptPubKey, nValue, sNarr, wtxNew, reservekey, nFeeRequired))
    {
        string strError;
        if (nValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf(_("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!"), FormatMoney(nFeeRequired).c_str());
        else
            strError = _("Error: Transaction creation failed!");
        LogPrintf("CreateEscrowLockTx() : %s\n", strError);
        return false;
    }
    else
        return true;
}

std::string RefundEscrow(uint256 buyerTxHash, uint256 sellerTxHash, CPubKey sellerKey, int64_t nValue, CPubKey buyerKey, std::string strError)
{
    // create a raw transaction
    // that spends the buyer and seller tx inputs
    int64_t nAmount = nValue / 2;
    // create a raw tx that sends nAmount to seller and nAmount to buyer
    // inputs buyerTxHash, sellerTxHash
    CTransaction rawTx;
    CTxIn inbuyer(COutPoint(buyerTxHash, nAmount));
    CTxIn inseller(COutPoint(sellerTxHash, nAmount));
    rawTx.vin.push_back(inbuyer);
    rawTx.vin.push_back(inseller);
    CBitcoinAddress buyerAddress(buyerKey.GetID());
    CBitcoinAddress sellerAddress(sellerKey.GetID());
    CScript scriptBuyerPubKey;
    scriptBuyerPubKey.SetDestination(buyerAddress.Get());
    CScript scriptSellerPubKey;
    scriptSellerPubKey.SetDestination(sellerAddress.Get());
    CTxOut outbuyer(nAmount, scriptBuyerPubKey);
    CTxOut outseller(nAmount, scriptSellerPubKey);
    rawTx.vout.push_back(outbuyer);
    rawTx.vout.push_back(outseller);

    // Buyer signs it
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;
    return SignMultiSigTransaction(HexStr(ss.begin(), ss.end()));
}

// Buyer creates a tx that spends the escrow to the seller
std::string PayEscrow(uint256 buyerTxHash, uint256 sellerTxHash, CPubKey sellerKey, int64_t nValue)
{
    // create a raw transaction
    // that spends the buyer and seller tx inputs
    int64_t nAmount = nValue;
    // create a raw tx that sends nAmount to seller and nAmount to buyer
    // inputs buyerTxHash, sellerTxHash
    CTransaction rawTx;
    CTxIn inbuyer(COutPoint(buyerTxHash, nAmount / 2));
    CTxIn inseller(COutPoint(sellerTxHash, nAmount / 2));
    rawTx.vin.push_back(inbuyer);
    rawTx.vin.push_back(inseller);
    CBitcoinAddress sellerAddress(sellerKey.GetID());
    CScript scriptSellerPubKey;
    scriptSellerPubKey.SetDestination(sellerAddress.Get());
    CTxOut outseller(nAmount, scriptSellerPubKey);
    rawTx.vout.push_back(outseller);

    // Buyer signs it
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;
    return SignMultiSigTransaction(HexStr(ss.begin(), ss.end()));
}

std::string SignMultiSigTransaction(std::string rawTxHex)
{
    LogPrintf("SignMultiSigTransaction: rawTxHex: %s\n", rawTxHex);
    vector<unsigned char> txData(ParseHex(rawTxHex));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
//    CTransaction mergedTx;
//    ssData >> mergedTx;
    vector<CTransaction> txVariants;
    while (!ssData.empty())
    {
        try {
            CTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        }
        catch (std::exception &e) {
            LogPrintf("TX decode failed. %s\n", e.what());
        }
    }

    if (txVariants.empty())
        LogPrintf("TX decode failed. txVariants empty.\n");


    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    map<COutPoint, CScript> mapPrevOut;
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
    {
        CTransaction tempTx;
        MapPrevTx mapPrevTx;
        CTxDB txdb("r");
        map<uint256, CTxIndex> unused;
        bool fInvalid;

        // FetchInputs aborts on failure, so we go one at a time.
        tempTx.vin.push_back(mergedTx.vin[i]);
        tempTx.FetchInputs(txdb, unused, false, false, mapPrevTx, fInvalid);

        // Copy results into mapPrevOut:
        BOOST_FOREACH(const CTxIn& txin, tempTx.vin)
        {
            const uint256& prevHash = txin.prevout.hash;
            if (mapPrevTx.count(prevHash) && mapPrevTx[prevHash].second.vout.size()>txin.prevout.n)
                mapPrevOut[txin.prevout] = mapPrevTx[prevHash].second.vout[txin.prevout.n].scriptPubKey;
        }
    }

    //EnsureWalletIsUnlocked();

    const CKeyStore& keystore = *pwalletMain;

    int nHashType = SIGHASH_ALL;
    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
    {
        CTxIn& txin = mergedTx.vin[i];
        if (mapPrevOut.count(txin.prevout) == 0)
        {
            continue;
        }
        const CScript& prevPubKey = mapPrevOut[txin.prevout];

        txin.scriptSig.clear();
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            SignSignature(keystore, prevPubKey, mergedTx, i, nHashType);

        // ... and merge in other signatures:
        BOOST_FOREACH(const CTransaction& txv, txVariants)
        {
            txin.scriptSig = CombineSignatures(prevPubKey, mergedTx, i, txin.scriptSig, txv.vin[i].scriptSig);
        }
        if (!VerifyScript(txin.scriptSig, prevPubKey, mergedTx, i, STANDARD_SCRIPT_VERIFY_FLAGS, 0))
        {
            LogPrintf("SignMultiSigTransaction(): couldn't verify script.\n");
        }
    }

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << mergedTx;
    return HexStr(ssTx.begin(), ssTx.end());
}


bool CheckSignature(CPaymentRequest request)
{
    CPubKey key = request.sellerKey;
    return key.Verify(request.GetHash(), request.vchSig);
}

bool SignPaymentRequest(CPaymentRequest request, std::vector<unsigned char>& vchSig)
{
    CKeyID keyId = request.sellerKey.GetID();
    CKey key;
    if(pwalletMain->GetKey(keyId, key))
    {
        return key.Sign(request.GetHash(), vchSig);
    }
    else
    {
        return false;
    }
}


bool CheckSignature(CSignedMarketListing listing)
{
    CPubKey key = listing.listing.sellerKey;
    return key.Verify(listing.listing.GetHash(), listing.vchListingSig);
}

bool SignListing(CMarketListing listing, std::vector<unsigned char>& vchListingSig)
{
    CKeyID keyId = listing.sellerKey.GetID();
    CKey key;
    if(pwalletMain->GetKey(keyId, key))
    {
        return key.Sign(listing.GetHash(), vchListingSig);
    }
    else
    {
        return false;
    }
}

bool CheckSignature(CBuyRequest req)
{
    CPubKey key = req.buyerKey;
    return key.Verify(req.listingId, req.vchSig);
}

bool SignBuyRequest(CBuyRequest req, std::vector<unsigned char>& vchSig)
{
    CKeyID keyId = req.buyerKey.GetID();
    CKey key;
    if(pwalletMain->GetKey(keyId, key))
    {
        return key.Sign(req.listingId, vchSig);
    }
    else
    {
        return false;
    }
}

bool CheckSignature(CBuyAccept req)
{
    CPubKey key = req.sellerKey;
    return key.Verify(req.listingId, req.vchSig);
}

bool SignBuyAccept(CBuyAccept req, std::vector<unsigned char>& vchSig)
{
    CKeyID keyId = req.sellerKey.GetID();
    CKey key;
    if(pwalletMain->GetKey(keyId, key))
    {
        return key.Sign(req.listingId, vchSig);
    }
    else
    {
        return false;
    }
}

bool CheckSignature(CBuyReject req)
{
    CPubKey key = req.sellerKey;
    return key.Verify(req.listingId, req.vchSig);
}

bool SignBuyReject(CBuyReject req, std::vector<unsigned char>& vchSig)
{
    CKeyID keyId = req.sellerKey.GetID();
    CKey key;
    if(pwalletMain->GetKey(keyId, key))
    {
        return key.Sign(req.listingId, vchSig);
    }
    else
    {
        return false;
    }
}

bool CheckSignature(CDeliveryDetails req)
{
    CPubKey key = req.buyerKey;
    return key.Verify(req.listingId, req.vchSig);
}

bool SignDeliveryDetails(CDeliveryDetails req, std::vector<unsigned char>& vchSig)
{
    CKeyID keyId = req.buyerKey.GetID();
    CKey key;
    if(pwalletMain->GetKey(keyId, key))
    {
        return key.Sign(req.listingId, vchSig);
    }
    else
    {
        return false;
    }
}

bool CheckSignature(CRefundRequest req)
{
    CPubKey key = req.buyerKey;
    return key.Verify(req.listingId, req.vchSig);
}

bool SignRefundRequest(CRefundRequest req, std::vector<unsigned char>& vchSig)
{
    CKeyID keyId = req.buyerKey.GetID();
    CKey key;
    if(pwalletMain->GetKey(keyId, key))
    {
        return key.Sign(req.listingId, vchSig);
    }
    else
    {
        return false;
    }
}

bool CheckSignature(CEscrowRelease req)
{
    CPubKey key = req.buyerKey;
    return key.Verify(req.listingId, req.vchSig);
}

bool SignEscrowRelease(CEscrowRelease req, std::vector<unsigned char>& vchSig)
{
    CKeyID keyId = req.buyerKey.GetID();
    CKey key;
    if(pwalletMain->GetKey(keyId, key))
    {
        return key.Sign(req.listingId, vchSig);
    }
    else
    {
        return false;
    }
}

bool CheckSignature(CEscrowPayment req)
{
    CPubKey key = req.buyerKey;
    return key.Verify(req.listingId, req.vchSig);
}

bool SignEscrowPayment(CEscrowPayment req, std::vector<unsigned char>& vchSig)
{
    CKeyID keyId = req.buyerKey.GetID();
    CKey key;
    if(pwalletMain->GetKey(keyId, key))
    {
        return key.Sign(req.listingId, vchSig);
    }
    else
    {
        return false;
    }
}

bool CheckSignature(CCancelListing req)
{
    CPubKey key = req.sellerKey;
    return key.Verify(req.listingId, req.vchSig);
}

bool SignCancelListing(CCancelListing req, std::vector<unsigned char>& vchSig)
{
    CKeyID keyId = req.sellerKey.GetID();
    CKey key;
    if(pwalletMain->GetKey(keyId, key))
    {
        return key.Sign(req.listingId, vchSig);
    }
    else
    {
        return false;
    }
}

void CPaymentRequest::BroadcastToAll() const
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((pnode->nServices & NODE_MARKET))
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(GetHash()).second)
            {
                pnode->PushMessage("mktpayr", *this);
            }
        }
    }
}

bool CPaymentRequest::RelayTo(CNode* pnode) const
{
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        pnode->PushMessage("mktpayr", *this);
        return true;
    }
    return false;
}

void CBuyRequest::BroadcastToAll() const
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((pnode->nServices & NODE_MARKET))
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(GetHash()).second)
            {
                pnode->PushMessage("mktbuyr", *this);
            }
        }
    }
}

bool CBuyRequest::RelayTo(CNode* pnode) const
{
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        pnode->PushMessage("mktbuyr", *this);
        return true;
    }
    return false;
}

bool CBuyAccept::RelayTo(CNode* pnode) const
{
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        pnode->PushMessage("mktbuya", *this);
        return true;
    }
    return false;
}

void CBuyAccept::BroadcastToAll() const
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((pnode->nServices & NODE_MARKET))
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(GetHash()).second)
            {
                pnode->PushMessage("mktbuya", *this);
            }
        }
    }
}

bool CBuyReject::RelayTo(CNode* pnode) const
{
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        pnode->PushMessage("mktbuyj", *this);
        return true;
    }
    return false;
}

void CBuyReject::BroadcastToAll() const
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((pnode->nServices & NODE_MARKET))
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(GetHash()).second)
            {
                pnode->PushMessage("mktbuyj", *this);
            }
        }
    }
}

bool CCancelListing::RelayTo(CNode* pnode) const
{
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        pnode->PushMessage("mktcan", *this);
        return true;
    }
    return false;
}

void CCancelListing::BroadcastToAll() const
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((pnode->nServices & NODE_MARKET))
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(GetHash()).second)
            {
                pnode->PushMessage("mktcan", *this);
            }
        }
    }
}

bool CDeliveryDetails::RelayTo(CNode* pnode) const
{
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        pnode->PushMessage("mktdet", *this);
        return true;
    }
    return false;
}

void CDeliveryDetails::BroadcastToAll() const
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((pnode->nServices & NODE_MARKET))
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(GetHash()).second)
            {
                pnode->PushMessage("mktdet", *this);
            }
        }
    }
}

bool CRefundRequest::RelayTo(CNode* pnode) const
{
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        pnode->PushMessage("mktref", *this);
        return true;
    }
    return false;
}

void CRefundRequest::BroadcastToAll() const
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((pnode->nServices & NODE_MARKET))
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(GetHash()).second)
            {
                pnode->PushMessage("mktref", *this);
            }
        }
    }
}

bool CEscrowRelease::RelayTo(CNode* pnode) const
{
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        pnode->PushMessage("mktesc", *this);
        return true;
    }
    return false;
}

void CEscrowRelease::BroadcastToAll() const
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((pnode->nServices & NODE_MARKET))
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(GetHash()).second)
            {
                pnode->PushMessage("mktesc", *this);
            }
        }
    }
}

bool CEscrowPayment::RelayTo(CNode* pnode) const
{
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        pnode->PushMessage("mktpay", *this);
        return true;
    }
    return false;
}

void CEscrowPayment::BroadcastToAll() const
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((pnode->nServices & NODE_MARKET))
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(GetHash()).second)
            {
                pnode->PushMessage("mktpay", *this);
            }
        }
    }
}



typedef std::map<std::string, std::set<CSignedMarketListing> > mapListingSetType;
void MarketInit()
{
    // open database
    if(TryOpenMarketDB())
    {
        LogPrintf("dMarketInit() Opened local dMarket database.\n");
        LOCK(cs_markets);
        ReadListings(mapListings);
        ReadBuyRequests(mapBuyRequests);
        ReadBuyAccepts(mapBuyAccepts);
        ReadBuyRejects(mapBuyRejects);
        ReadCancelListings(mapCancelListings);
        ReadDeliveryDetails(mapDeliveryDetails);
        ReadRefundRequests(mapRefundRequests);
        ReadEscrowReleases(mapEscrowReleases);
        ReadEscrowPayments(mapEscrowPayments);
        ReadPaymentRequests(mapPaymentRequests);

        // Clean up any cancelled listings
        BOOST_FOREACH(PAIRTYPE(const uint256, CCancelListing)& p, mapCancelListings)
        {
            if(mapListings.find(p.second.listingId) != mapListings.end())
                mapListings.erase(p.second.listingId);
        }

        // Update category map
        BOOST_FOREACH(PAIRTYPE(const uint256, CSignedMarketListing)& p, mapListings)
        {
            CTxDestination dest = p.second.listing.sellerKey.GetID();
            if(IsMine(*pwalletMain, dest))
            {
                uiInterface.NotifyNewSellerListing(p.second);
            }

            if(mapListingsByCategory.find(p.second.listing.sCategory) == mapListingsByCategory.end())
            {
                std::set<CSignedMarketListing> setListings;
                setListings.insert(p.second);
                mapListingsByCategory.insert(std::make_pair(p.second.listing.sCategory, setListings));
            }
            else
                mapListingsByCategory[p.second.listing.sCategory].insert(p.second);
        }

        // Send categories to ui
        BOOST_FOREACH(const mapListingSetType::value_type& p, mapListingsByCategory)
        {
            uiInterface.NotifyMarketCategory(p.first);
        }

        LogPrintf("dMarketInit(): Read %d buy requests.\n", mapBuyRequests.size());
        // Scan for buy requests for our listings
        BOOST_FOREACH(PAIRTYPE(const uint256, CBuyRequest)& p, mapBuyRequests)
        {
            if(mapListings.find(p.second.listingId) != mapListings.end())
            {
                LogPrintf("dMarketInit(): Loading buy request for listing: %s\n", p.second.listingId.ToString());
                CTxDestination dest = mapListings[p.second.listingId].listing.sellerKey.GetID();
                if(IsMine(*pwalletMain, dest))
                {
                    uiInterface.NotifyBuyRequest(p.second);
                }
                else
                {
                    CTxDestination dest = p.second.buyerKey.GetID();
                    if(IsMine(*pwalletMain, dest))
                    {
                        uiInterface.NotifyBuyRequest(p.second);
                    }
                }
            }
        }
    }
}

void MarketProcessMessage(CNode* pfrom, std::string strCommand, CDataStream& vRecv)
{
    if(strCommand == "mktlst")
    {
        CSignedMarketListing listing;
        vRecv >> listing;
        pfrom->setKnown.insert(listing.GetHash());
        // check signature
        if(!CheckSignature(listing))
        {
            pfrom->Misbehaving(20);
        }
        else
        {
            // If we don't already have this listing, add it to our local db and notify the UI
            ReceiveListing(listing);
        }
        // Relay it on to our peers
        listing.BroadcastToAll();
    }
    else if(strCommand == "mktbuyr")
    {
        CBuyRequest buyr;
        vRecv >> buyr;
        pfrom->setKnown.insert(buyr.requestId);
        if(!CheckSignature(buyr))
        {
            LogPrintf("Buy Request CheckSignature failed! %s\n", buyr.requestId.ToString());
            pfrom->Misbehaving(20);
        }
        else
        {
            if(!ReceiveBuyRequest(buyr))
                buyr.BroadcastToAll(); // pass it on if it's not for our item
        }
    }
    else if(strCommand == "mktpayr")
    {
        CPaymentRequest payr;
        vRecv >> payr;
        pfrom->setKnown.insert(payr.requestId);
        if(!CheckSignature(payr))
        {
            LogPrintf("Payment Request CheckSignature failed! %s\n", payr.GetHash().ToString());
            pfrom->Misbehaving(20);
        }
        else
        {
            if(!ReceivePaymentRequest(payr))
                payr.BroadcastToAll(); // pass it on if it's not for our item
        }
    }
    else if(strCommand == "mktbuya")
    {
        // buy accept
        CBuyAccept buya;
        vRecv >> buya;
        pfrom->setKnown.insert(buya.GetHash());
        if(!CheckSignature(buya))
        {
            pfrom->Misbehaving(20);
        }
        else
        {
            if(!ReceiveBuyAccept(buya))
                buya.BroadcastToAll();
        }
    }
    else if(strCommand == "mktbuyj")
    {
        // buy reject
        CBuyReject buyj;
        vRecv >> buyj;
        pfrom->setKnown.insert(buyj.GetHash());
        if(!CheckSignature(buyj))
        {
            pfrom->Misbehaving(20);
        }
        else
        {
            if(!ReceiveBuyReject(buyj))
                buyj.BroadcastToAll();
        }
    }
    else if(strCommand == "mktdel")
    {
        CDeliveryDetails det;
        vRecv >> det;
        pfrom->setKnown.insert(det.GetHash());
        if(!CheckSignature(det))
        {
            pfrom->Misbehaving(20);
        }
        else
        {
            if(!ReceiveDeliveryDetails(det))
                det.BroadcastToAll();
        }
    }
    else if(strCommand == "mktref")
    {
        CRefundRequest refr;
        vRecv >> refr;
        pfrom->setKnown.insert(refr.GetHash());
        if(!CheckSignature(refr))
        {
            pfrom->Misbehaving(20);
        }
        else
        {
            if(!ReceiveRefundRequest(refr))
                refr.BroadcastToAll();
        }
    }
    else if(strCommand == "mktesc")
    {
        CEscrowRelease esc;
        vRecv >> esc;
        pfrom->setKnown.insert(esc.GetHash());
        if(!CheckSignature(esc))
        {
            pfrom->Misbehaving(20);
        }
        else
        {
            if(!ReceiveEscrowRelease(esc))
                esc.BroadcastToAll();
        }
    }
    else if(strCommand == "mktescp")
    {
        CEscrowPayment esc;
        vRecv >> esc;
        pfrom->setKnown.insert(esc.GetHash());
        if(!CheckSignature(esc))
        {
            pfrom->Misbehaving(20);
        }
        else
        {
            if(!ReceiveEscrowPayment(esc))
                esc.BroadcastToAll();
        }
    }
    else if(strCommand == "mktcan")
    {
        CCancelListing can;
        vRecv >> can;
        pfrom->setKnown.insert(can.GetHash());
        if(!CheckSignature(can))
        {
            pfrom->Misbehaving(20);
        }
        else
        {
            ReceiveCancelListing(can);
            can.BroadcastToAll();
        }
    }
    else if(strCommand == "mktinv")
    {
        int64_t nTime = 0;
        vRecv >> nTime;
        LOCK(cs_markets);
        // send inventory we know about since this time
        // listings
        BOOST_FOREACH(PAIRTYPE(const uint256, CSignedMarketListing)& p, mapListings)
        {
            if(p.second.listing.nCreated >= nTime)
                p.second.RelayTo(pfrom);
        }

        // buy requests
        BOOST_FOREACH(PAIRTYPE(const uint256, CBuyRequest)& p, mapBuyRequests)
        {
            if(p.second.nDate >= nTime)
                p.second.RelayTo(pfrom);
        }

        // buy accepts
        BOOST_FOREACH(PAIRTYPE(const uint256, CBuyAccept)& p, mapBuyAccepts)
        {
            if(p.second.nDate >= nTime)
                p.second.RelayTo(pfrom);
        }

        // buy rejects
        BOOST_FOREACH(PAIRTYPE(const uint256, CBuyReject)& p, mapBuyRejects)
        {
            if(p.second.nDate >= nTime)
                p.second.RelayTo(pfrom);
        }

        // listing cancels
        BOOST_FOREACH(PAIRTYPE(const uint256, CCancelListing)& p, mapCancelListings)
        {
            if(p.second.nDate >= nTime)
                p.second.RelayTo(pfrom);
        }

        // delivery details
        BOOST_FOREACH(PAIRTYPE(const uint256, CDeliveryDetails)& p, mapDeliveryDetails)
        {
            if(p.second.nDate >= nTime)
                p.second.RelayTo(pfrom);
        }

        // refund requests
        BOOST_FOREACH(PAIRTYPE(const uint256, CRefundRequest)& p, mapRefundRequests)
        {
            if(p.second.nDate >= nTime)
                p.second.RelayTo(pfrom);
        }

        // escrow lock
        BOOST_FOREACH(PAIRTYPE(const uint256, CEscrowRelease)& p, mapEscrowReleases)
        {
            if(p.second.nDate >= nTime)
                p.second.RelayTo(pfrom);
        }

        // escrow payment
        BOOST_FOREACH(PAIRTYPE(const uint256, CEscrowPayment)& p, mapEscrowPayments)
        {
            if(p.second.nDate >= nTime)
                p.second.RelayTo(pfrom);
        }

        // payment requests
        BOOST_FOREACH(PAIRTYPE(const uint256, CPaymentRequest)& p, mapPaymentRequests)
        {
            if(p.second.nDate >= nTime)
                p.second.RelayTo(pfrom);
        }
    }
}

bool ReceivePaymentRequest(CPaymentRequest request)
{
    LOCK(cs_markets);
    if(mapPaymentRequests.find(request.GetHash()) == mapPaymentRequests.end())
    {
        mapPaymentRequests.insert(std::make_pair(request.GetHash(), request));
        WritePaymentRequests(mapPaymentRequests);
    }

    // does it apply to us?
    // look through our buy requests
    if(mapBuyRequests.find(request.requestId) != mapBuyRequests.end())
    {
        CTxDestination dest = mapBuyRequests[request.requestId].buyerKey.GetID();
        if(IsMine(*pwalletMain, dest))
        {
            // it is one of ours
            LogPrintf("ReceivePaymentRequest: request.requestId: %s rawTx: %s\n", request.requestId.ToString(), request.rawTx);
            mapBuyRequests[request.requestId].rawTx = request.rawTx;
            mapBuyRequests[request.requestId].nStatus = PAYMENT_REQUESTED;
            WriteBuyRequests(mapBuyRequests);
            uiInterface.NotifyPaymentRequest();
            return true;
        }
    }
    else
        return false; // we don't have the listing
}


bool ReceiveEscrowPayment(CEscrowPayment escrow)
{
    LOCK(cs_markets);
    if(mapEscrowPayments.find(escrow.GetHash()) == mapEscrowPayments.end())
    {
        mapEscrowPayments.insert(std::make_pair(escrow.GetHash(), escrow));
        WriteEscrowPayments(mapEscrowPayments);
    }

    LogPrintf("Received escrow payment notification for buy request Id: %s\n", escrow.requestId.ToString());
    mapBuyRequests[escrow.requestId].rawTx = escrow.rawTx;
    mapBuyRequests[escrow.requestId].nStatus = ESCROW_PAID;
    WriteBuyRequests(mapBuyRequests);
    uiInterface.NotifyEscrowPayment();

    // does it apply to us?
    // look through our listings
    if(mapListings.find(escrow.listingId) != mapListings.end())
    {
        CTxDestination dest = mapListings[escrow.listingId].listing.sellerKey.GetID();
        if(IsMine(*pwalletMain, dest))
        {
            // it is one of ours
            mapListings[escrow.listingId].listing.nStatus = ESCROW_PAID;
            WriteListings(mapListings);
            uiInterface.NotifyEscrowPayment();
            return true;
        }
    }
    else
        return false; // we don't have the listing
}

bool ReceiveEscrowRelease(CEscrowRelease escrow)
{
    LOCK(cs_markets);
    if(mapEscrowReleases.find(escrow.GetHash()) == mapEscrowReleases.end())
    {
        mapEscrowReleases.insert(std::make_pair(escrow.GetHash(), escrow));
        WriteEscrowReleases(mapEscrowReleases);
    }

    mapBuyRequests[escrow.requestId].buyerEscrowLockTxHash = escrow.buyerEscrowLockTxHash;
    WriteBuyRequests(mapBuyRequests);

    // does it apply to us?
    // look through our listings
    if(mapListings.find(mapBuyRequests[escrow.requestId].listingId) != mapListings.end())
    {
        CTxDestination dest = mapListings[escrow.listingId].listing.sellerKey.GetID();
        if(IsMine(*pwalletMain, dest))
        {
            // it is one of ours
            mapListings[escrow.listingId].listing.nStatus = ESCROW_LOCK;
            mapBuyRequests[escrow.requestId].nStatus = ESCROW_LOCK;
            WriteBuyRequests(mapBuyRequests);
            WriteListings(mapListings);
            uiInterface.NotifyEscrowRelease();
            return true;
        }
    }
    else
        return false; // we don't have the listing
}

bool ReceiveBuyAccept(CBuyAccept accept)
{
    LOCK(cs_markets);
    if(mapBuyAccepts.find(accept.GetHash()) == mapBuyAccepts.end())
    {
        mapBuyAccepts.insert(std::make_pair(accept.GetHash(), accept));
        WriteBuyAccepts(mapBuyAccepts);
    }

    // does it apply to us?
    // look through our buy requests
    if(mapBuyRequests.find(accept.buyRequestId) != mapBuyRequests.end())
    {
        CTxDestination dest = mapBuyRequests[accept.buyRequestId].buyerKey.GetID();
        if(IsMine(*pwalletMain, dest))
        {
            mapBuyRequests[accept.buyRequestId].escrowAddress = accept.escrowAddress;
            mapBuyRequests[accept.buyRequestId].sellerEscrowLockTxHash = accept.sellerEscrowLockTxHash;
            mapBuyRequests[accept.buyRequestId].nStatus = BUY_ACCEPTED;
            WriteBuyRequests(mapBuyRequests);
            uiInterface.NotifyBuyAccepted();
            return true;
        }
    }
    else
        return false; // doesn't apply to us
}

bool ReceiveBuyReject(CBuyReject reject)
{
    LOCK(cs_markets);
    if(mapBuyRejects.find(reject.GetHash()) == mapBuyRejects.end())
    {
        mapBuyRejects.insert(std::make_pair(reject.GetHash(), reject));
        WriteBuyRejects(mapBuyRejects);
    }

    // does it apply to us?
    // look through our buy requests
    if(mapBuyRequests.find(reject.buyRequestId) != mapBuyRequests.end())
    {
        CTxDestination dest = mapBuyRequests[reject.buyRequestId].buyerKey.GetID();
        if(IsMine(*pwalletMain, dest))
        {
            mapBuyRequests[reject.buyRequestId].nStatus = BUY_REJECTED;
            WriteBuyRequests(mapBuyRequests);
            uiInterface.NotifyBuyRejected();
            return true;
        }
    }
    else
        return false; // doesn't apply to us
}

bool ReceiveDeliveryDetails(CDeliveryDetails details)
{
    LOCK(cs_markets);
    if(mapDeliveryDetails.find(details.GetHash()) == mapDeliveryDetails.end())
    {
        mapDeliveryDetails.insert(std::make_pair(details.GetHash(), details));
        WriteDeliveryDetails(mapDeliveryDetails);
    }

    // does it apply to us?
    if(mapListings.find(details.listingId) != mapListings.end())
    {
        CTxDestination dest = mapListings[details.listingId].listing.sellerKey.GetID();
        if(IsMine(*pwalletMain, dest))
        {
            // it is one of ours
            mapListings[details.listingId].listing.nStatus = DELIVERY_DETAILS;
            WriteListings(mapListings);
            uiInterface.NotifyDeliveryDetails();
            return true;
        }
    }
    else
        return false; // we don't have the listing
}

bool ReceiveRefundRequest(CRefundRequest refund)
{
    LOCK(cs_markets);
    if(mapRefundRequests.find(refund.GetHash()) == mapRefundRequests.end())
    {
        mapRefundRequests.insert(std::make_pair(refund.GetHash(), refund));
        WriteRefundRequests(mapRefundRequests);
    }

    mapBuyRequests[refund.buyRequestId].nStatus = REFUND_REQUESTED;
    mapBuyRequests[refund.buyRequestId].rawTx = refund.rawTx;
    WriteBuyRequests(mapBuyRequests);

    // does it apply to us?
    if(mapListings.find(refund.listingId) != mapListings.end())
    {
        CTxDestination dest = mapListings[refund.listingId].listing.sellerKey.GetID();
        if(IsMine(*pwalletMain, dest))
        {
            // it is one of ours
            mapListings[refund.listingId].listing.nStatus = REFUND_REQUESTED;
            WriteListings(mapListings);
            uiInterface.NotifyRefundRequested();
            return true;
        }
        else
            return false;
    }
    else
        return false; // we don't have the listing
}

void ReceiveCancelListing(CCancelListing cancel)
{
    LogPrintf("Cancel listing received for item %s\n", cancel.listingId.ToString());
    // if we have a listing this cancels, remove it
    LOCK(cs_markets);
    if(mapListings.find(cancel.listingId) != mapListings.end())
    {
        mapListingsByCategory[mapListings[cancel.listingId].listing.sCategory].erase(mapListings[cancel.listingId]);
        mapListings.erase(cancel.listingId);
        LogPrintf("ReceiveCancelListing: Listing removed from mapListings.\n");
    }
    else
        LogPrintf("ReceiveCancelListing: Listing not found with id: %s\n", cancel.listingId.ToString());

    mapCancelListings.insert(std::make_pair(cancel.GetHash(), cancel));

    WriteListings(mapListings);
    WriteCancelListings(mapCancelListings);



    uiInterface.NotifyListingCancelled();
}

bool ReceiveBuyRequest(CBuyRequest buyr)
{
    LOCK(cs_markets);
    if(mapBuyRequests.find(buyr.requestId) == mapBuyRequests.end())
    {
        mapBuyRequests.insert(std::make_pair(buyr.requestId, buyr));
        WriteBuyRequests(mapBuyRequests);
    }

    // check if it is a request for one of our items
    if(mapListings.find(buyr.listingId) == mapListings.end())
    {
        LogPrintf("Buy Request received for unknown listing: %s\n", buyr.listingId.ToString());
        return false; // we don't know about the listing at all
    }
    else
    {
        LogPrintf("Buy Request received for known listing: %s\n", buyr.listingId.ToString());
        CTxDestination dest = mapListings[buyr.listingId].listing.sellerKey.GetID();
        if(IsMine(*pwalletMain, dest))
        {
            // it is for one of our listings
            // notify UI
            LogPrintf("Buy Request seller key Is Mine.\n");
            uiInterface.NotifyBuyRequest(buyr);
            return true;
        }
        else
        {
            LogPrintf("Buy Request seller key Is NOT mine.\n");
            uiInterface.NotifyBuyRequest(buyr);
            return false;
        }
    }
}

void ReceiveListing(CSignedMarketListing listing)
{
    if(mapListings.find(listing.GetHash()) == mapListings.end())
    {
        bool bFoundCancel = false;
        LOCK(cs_markets);
        BOOST_FOREACH(PAIRTYPE(const uint256, CCancelListing)& p, mapCancelListings)
        {
            if(p.second.listingId == listing.GetHash())
            {
                bFoundCancel = true;
                break;
            }
        }
        if(!bFoundCancel)
        {

            mapListings.insert(std::make_pair(listing.GetHash(), listing));
            WriteListings(mapListings);
            // Update category
            if(mapListingsByCategory.find(listing.listing.sCategory) == mapListingsByCategory.end())
            {
                std::set<CSignedMarketListing> setListings;
                setListings.insert(listing);
                mapListingsByCategory.insert(std::make_pair(listing.listing.sCategory, setListings));
                uiInterface.NotifyMarketCategory(listing.listing.sCategory);
            }
            else
                mapListingsByCategory[listing.listing.sCategory].insert(listing);

        }
    }
}

void CSignedMarketListing::BroadcastToAll() const
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if((pnode->nServices & NODE_MARKET))
        {
            // returns true if wasn't already contained in the set
            if (pnode->setKnown.insert(GetHash()).second)
            {
                pnode->PushMessage("mktlst", *this);
            }
        }
    }
}

bool CSignedMarketListing::RelayTo(CNode* pnode) const
{
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        pnode->PushMessage("mktlst", *this);
        return true;
    }
    return false;
}


bool MarketDB::Open(const char* pszMode)
{
    if (marketDB)
    {
        pdb = marketDB;
        return true;
    };

    bool fCreate = strchr(pszMode, 'c');

    fs::path fullpath = GetDataDir() / "market";

    if (!fCreate
        && (!fs::exists(fullpath)
            || !fs::is_directory(fullpath)))
    {
        LogPrintf("dMarketDB::open() - DB does not exist.");
        return false;
    };

    leveldb::Options options;
    options.create_if_missing = fCreate;
    leveldb::Status s = leveldb::DB::Open(options, fullpath.string(), &marketDB);

    if (!s.ok())
    {
        LogPrintf("dMarketDB::open() - Error opening db: %s.", s.ToString().c_str());
        return false;
    };

    pdb = marketDB;

    return true;
};

class MarketDbBatchScanner : public leveldb::WriteBatch::Handler
{
public:
    std::string needle;
    bool* deleted;
    std::string* foundValue;
    bool foundEntry;

    MarketDbBatchScanner() : foundEntry(false) {}

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

bool MarketDB::ScanBatch(const CDataStream& key, std::string* value, bool* deleted) const
{
    if (!activeBatch)
        return false;

    *deleted = false;
    MarketDbBatchScanner scanner;
    scanner.needle = key.str();
    scanner.deleted = deleted;
    scanner.foundValue = value;
    leveldb::Status s = activeBatch->Iterate(&scanner);
    if (!s.ok())
    {
        LogPrintf("dMarketDB ScanBatch error: %s", s.ToString().c_str());
        return false;
    };

    return scanner.foundEntry;
}

bool MarketDB::TxnBegin()
{
    if (activeBatch)
        return true;
    activeBatch = new leveldb::WriteBatch();
    return true;
};

bool MarketDB::TxnCommit()
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
        LogPrintf("dMarketDB batch commit failure: %s", status.ToString().c_str());
        return false;
    };

    return true;
};

bool MarketDB::TxnAbort()
{
    delete activeBatch;
    activeBatch = NULL;
    return true;
};

bool TryOpenMarketDB()
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if(!mdb.Open("cr")) // create if doesn't exist, read mode
        return false;

    return true;
}

bool MarketDB::ReadListings(std::map<uint256, CSignedMarketListing>& listings)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'm'; // map
    ssKey << 'l'; // market listings

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
            LogPrintf("MarketDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> listings;
    } catch (std::exception& e) {
        LogPrintf("MarketDB::ReadListings() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool MarketDB::WriteListings(std::map<uint256, CSignedMarketListing> listings)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'm'; // map
    ssKey << 'l'; // market listings

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(listings));
    ssValue << listings;

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
        LogPrintf("MarketDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};

bool MarketDB::ReadBuyRequests(std::map<uint256, CBuyRequest>& buyRequests)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'b'; // buy
    ssKey << 'r'; // requests

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
            LogPrintf("MarketDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> buyRequests;
    } catch (std::exception& e) {
        LogPrintf("MarketDB::ReadBuyRequests() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool MarketDB::WriteBuyRequests(std::map<uint256, CBuyRequest> buyRequests)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'b'; // buy
    ssKey << 'r'; // requests

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(buyRequests));
    ssValue << buyRequests;

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
        LogPrintf("MarketDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};

bool MarketDB::ReadBuyAccepts(std::map<uint256, CBuyAccept>& accepts)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'b'; // buy
    ssKey << 'a'; // accepts

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
            LogPrintf("MarketDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> accepts;
    } catch (std::exception& e) {
        LogPrintf("MarketDB::ReadBuyAccepts() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool MarketDB::WriteBuyAccepts(std::map<uint256, CBuyAccept> accepts)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'b'; // buy
    ssKey << 'a'; // accepts

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(accepts));
    ssValue << accepts;

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
        LogPrintf("MarketDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};

bool MarketDB::ReadBuyRejects(std::map<uint256, CBuyReject>& rejects)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'b'; // buy
    ssKey << 'r'; // rejects

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
            LogPrintf("MarketDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> rejects;
    } catch (std::exception& e) {
        LogPrintf("MarketDB::ReadBuyRejects() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool MarketDB::WriteBuyRejects(std::map<uint256, CBuyReject> rejects)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'b'; // buy
    ssKey << 'r'; // rejects

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(rejects));
    ssValue << rejects;

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
        LogPrintf("MarketDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};

bool MarketDB::ReadRefundRequests(std::map<uint256, CRefundRequest>& refunds)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'r'; // refund
    ssKey << 'r'; // requests

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
            LogPrintf("MarketDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> refunds;
    } catch (std::exception& e) {
        LogPrintf("MarketDB::ReadRefundRequests() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool MarketDB::WriteRefundRequests(std::map<uint256, CRefundRequest> refunds)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'r'; // refund
    ssKey << 'r'; // requests

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(refunds));
    ssValue << refunds;

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
        LogPrintf("MarketDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};

bool MarketDB::ReadDeliveryDetails(std::map<uint256, CDeliveryDetails>& details)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'd'; // delivery
    ssKey << 'd'; // details

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
            LogPrintf("MarketDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> details;
    } catch (std::exception& e) {
        LogPrintf("MarketDB::ReadDeliveryDetails() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool MarketDB::WriteDeliveryDetails(std::map<uint256, CDeliveryDetails> details)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'd'; // delivery
    ssKey << 'd'; // details

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(details));
    ssValue << details;

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
        LogPrintf("MarketDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};

bool MarketDB::ReadCancelListings(std::map<uint256, CCancelListing>& cancels)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'c'; // cancel
    ssKey << 'l'; // listings

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
            LogPrintf("MarketDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> cancels;
    } catch (std::exception& e) {
        LogPrintf("MarketDB::ReadCancelListings() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool MarketDB::WriteCancelListings(std::map<uint256, CCancelListing> cancels)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'c'; // cancel
    ssKey << 'l'; // listings

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(cancels));
    ssValue << cancels;

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
        LogPrintf("MarketDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};

bool MarketDB::ReadEscrowReleases(std::map<uint256, CEscrowRelease>& releases)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'e'; // escrow
    ssKey << 'r'; // releases

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
            LogPrintf("MarketDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> releases;
    } catch (std::exception& e) {
        LogPrintf("MarketDB::ReadEscrowReleases() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool MarketDB::WriteEscrowReleases(std::map<uint256, CEscrowRelease> releases)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'e'; // escrow
    ssKey << 'r'; // releases

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(releases));
    ssValue << releases;

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
        LogPrintf("MarketDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};

bool MarketDB::ReadEscrowPayments(std::map<uint256, CEscrowPayment>& payments)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'e'; // escrow
    ssKey << 'p'; // payments

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
            LogPrintf("MarketDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> payments;
    } catch (std::exception& e) {
        LogPrintf("MarketDB::ReadEscrowPayments() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool MarketDB::WriteEscrowPayments(std::map<uint256, CEscrowPayment> payments)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'e'; // escrow
    ssKey << 'p'; // payments

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(payments));
    ssValue << payments;

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
        LogPrintf("MarketDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};


bool MarketDB::ReadPaymentRequests(std::map<uint256, CPaymentRequest>& requests)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'p'; // payment
    ssKey << 'r'; // requests

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
            LogPrintf("MarketDB read failure: %s", s.ToString().c_str());
            return false;
        };
    };

    try {
        CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
        ssValue >> requests;
    } catch (std::exception& e) {
        LogPrintf("MarketDB::ReadPaymentRequests() unserialize threw: %s.", e.what());
        return false;
    }

    return true;
};

bool MarketDB::WritePaymentRequests(std::map<uint256, CPaymentRequest> requests)
{
    if (!pdb)
        return false;

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(2);
    ssKey << 'p'; // payment
    ssKey << 'r'; // requests

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(sizeof(requests));
    ssValue << requests;

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
        LogPrintf("MarketDB write failure: %s", s.ToString().c_str());
        return false;
    };

    return true;
};



bool WriteListings(std::map<uint256, CSignedMarketListing> listings)
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if (!mdb.Open("cw")
        || !mdb.TxnBegin())
        return false;

    if(mdb.WriteListings(listings))
    {
        mdb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadListings(std::map<uint256, CSignedMarketListing>& listings)
{
    MarketDB mdb;
    if(!mdb.Open("r"))
        return false;

    if(mdb.ReadListings(listings))
        return true;
    else
        return false;
}

bool WriteBuyRequests(std::map<uint256, CBuyRequest> buyRequests)
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if (!mdb.Open("cw")
        || !mdb.TxnBegin())
        return false;

    if(mdb.WriteBuyRequests(buyRequests))
    {
        mdb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadBuyRequests(std::map<uint256, CBuyRequest>& buyRequests)
{
    MarketDB mdb;
    if(!mdb.Open("r"))
        return false;

    if(mdb.ReadBuyRequests(buyRequests))
        return true;
    else
        return false;
}

bool WriteBuyAccepts(std::map<uint256, CBuyAccept> buyAccepts)
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if (!mdb.Open("cw")
        || !mdb.TxnBegin())
        return false;

    if(mdb.WriteBuyAccepts(buyAccepts))
    {
        mdb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadBuyAccepts(std::map<uint256, CBuyAccept>& buyAccepts)
{
    MarketDB mdb;
    if(!mdb.Open("r"))
        return false;

    if(mdb.ReadBuyAccepts(buyAccepts))
        return true;
    else
        return false;
}

bool WriteBuyRejects(std::map<uint256, CBuyReject> buyRejects)
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if (!mdb.Open("cw")
        || !mdb.TxnBegin())
        return false;

    if(mdb.WriteBuyRejects(buyRejects))
    {
        mdb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadBuyRejects(std::map<uint256, CBuyReject>& buyRejects)
{
    MarketDB mdb;
    if(!mdb.Open("r"))
        return false;

    if(mdb.ReadBuyRejects(buyRejects))
        return true;
    else
        return false;
}

bool WriteCancelListings(std::map<uint256, CCancelListing> cancels)
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if (!mdb.Open("cw")
        || !mdb.TxnBegin())
        return false;

    if(mdb.WriteCancelListings(cancels))
    {
        mdb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadCancelListings(std::map<uint256, CCancelListing>& cancels)
{
    MarketDB mdb;
    if(!mdb.Open("r"))
        return false;

    if(mdb.ReadCancelListings(cancels))
        return true;
    else
        return false;
}

bool WriteDeliveryDetails(std::map<uint256, CDeliveryDetails> details)
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if (!mdb.Open("cw")
        || !mdb.TxnBegin())
        return false;

    if(mdb.WriteDeliveryDetails(details))
    {
        mdb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadDeliveryDetails(std::map<uint256, CDeliveryDetails>& details)
{
    MarketDB mdb;
    if(!mdb.Open("r"))
        return false;

    if(mdb.ReadDeliveryDetails(details))
        return true;
    else
        return false;
}

bool WriteRefundRequests(std::map<uint256, CRefundRequest> refunds)
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if (!mdb.Open("cw")
        || !mdb.TxnBegin())
        return false;

    if(mdb.WriteRefundRequests(refunds))
    {
        mdb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadRefundRequests(std::map<uint256, CRefundRequest>& refunds)
{
    MarketDB mdb;
    if(!mdb.Open("r"))
        return false;

    if(mdb.ReadRefundRequests(refunds))
        return true;
    else
        return false;
}

bool WriteEscrowReleases(std::map<uint256, CEscrowRelease> releases)
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if (!mdb.Open("cw")
        || !mdb.TxnBegin())
        return false;

    if(mdb.WriteEscrowReleases(releases))
    {
        mdb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadEscrowReleases(std::map<uint256, CEscrowRelease>& releases)
{
    MarketDB mdb;
    if(!mdb.Open("r"))
        return false;

    if(mdb.ReadEscrowReleases(releases))
        return true;
    else
        return false;
}

bool WriteEscrowPayments(std::map<uint256, CEscrowPayment> payments)
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if (!mdb.Open("cw")
        || !mdb.TxnBegin())
        return false;

    if(mdb.WriteEscrowPayments(payments))
    {
        mdb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadEscrowPayments(std::map<uint256, CEscrowPayment>& payments)
{
    MarketDB mdb;
    if(!mdb.Open("r"))
        return false;

    if(mdb.ReadEscrowPayments(payments))
        return true;
    else
        return false;
}

bool WritePaymentRequests(std::map<uint256, CPaymentRequest> requests)
{
    LOCK(cs_marketdb);
    MarketDB mdb;
    if (!mdb.Open("cw")
        || !mdb.TxnBegin())
        return false;

    if(mdb.WritePaymentRequests(requests))
    {
        mdb.TxnCommit();
        return true;
    }
    else
    {
        return false;
    }
}

bool ReadPaymentRequests(std::map<uint256, CPaymentRequest>& requests)
{
    MarketDB mdb;
    if(!mdb.Open("r"))
        return false;

    if(mdb.ReadPaymentRequests(requests))
        return true;
    else
        return false;
}