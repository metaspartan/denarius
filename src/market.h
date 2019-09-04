// Copyright (c) 2019 The Denarius Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DMARKET_H
#define DMARKET_H

#include "util.h"
#include "main.h"
#include "sync.h"

#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include "db.h"

#include "wallet.h"
#include "init.h"
#include "keystore.h"
#include "key.h"
#include "script.h"

// Forward Declarations of Serialized Object Classes
class CMarketListing;
class CSignedMarketListing;
class CBuyRequest;
class CBuyAccept;
class CBuyReject;
class CDeliveryDetails;
class CEscrowRelease;
class CEscrowPayment;
class CRefundRequest;
class CCancelListing;
class CPaymentRequest;

static const int MARKET_RETENTION_CUTOFF_DAYS = 30 * 24 * 60 * 60; // market history retained and relayed for 30 days

extern CCriticalSection cs_marketdb;
extern CCriticalSection cs_markets;
extern std::map<uint256, CSignedMarketListing> mapListings; // stored in local db
extern std::map<std::string, std::set<CSignedMarketListing> > mapListingsByCategory; // maintained in memory
extern std::map<uint256, CBuyRequest> mapBuyRequests;
extern std::map<uint256, CBuyAccept> mapBuyAccepts;
extern std::map<uint256, CBuyReject> mapBuyRejects;
extern std::map<uint256, CDeliveryDetails> mapDeliveryDetails;
extern std::map<uint256, CRefundRequest> mapRefundRequests;
extern std::map<uint256, CEscrowRelease> mapEscrowReleases;
extern std::map<uint256, CEscrowPayment> mapEscrowPayments;
extern std::map<uint256, CCancelListing> mapCancelListings;
extern std::map<uint256, CPaymentRequest> mapPaymentRequests;

// Status of market listings and buys in the workflow
enum BUY_STATUS
{
    LISTED,
    BUY_REQUESTED,
    BUY_ACCEPTED,
    BUY_REJECTED,
    ESCROW_LOCK,
    DELIVERY_DETAILS,
    ESCROW_PAID,
    REFUND_REQUESTED,
    REFUNDED,
    PAYMENT_REQUESTED
};

bool AddMultisigAddress(CPubKey sellerKey, CPubKey buyerKey, std::string& escrowAddress, std::string& errors);
bool CreateEscrowLockTx(std::string escrowAddress, int64_t nValue, std::string& strError, CWalletTx& wtxNew);
std::string RefundEscrow(uint256 buyerTxHash, uint256 sellerTxHash, CPubKey sellerKey, int64_t nValue, CPubKey buyerKey, std::string strError);
std::string PayEscrow(uint256 buyerTxHash, uint256 sellerTxHash, CPubKey sellerKey, int64_t nValue);
std::string SignMultiSigTransaction(std::string rawTxHex);

// Initialize the market and load data from local database on wallet startup
void MarketInit();
// Process p2p messages received from peers
void ProcessMessageMarket(CNode* pfrom, std::string strCommand, CDataStream& vRecv);
void ReceiveListing(CSignedMarketListing listing);
void ReceiveCancelListing(CCancelListing can);
bool ReceiveBuyRequest(CBuyRequest request);
bool ReceiveBuyAccept(CBuyAccept accept);
bool ReceiveBuyReject(CBuyReject reject);
bool ReceiveDeliveryDetails(CDeliveryDetails details);
bool ReceiveEscrowRelease(CEscrowRelease release);
bool ReceiveEscrowPayment(CEscrowPayment payment);
bool ReceiveRefundRequest(CRefundRequest refund);
bool ReceivePaymentRequest(CPaymentRequest request);

// Sign listings
bool SignListing(CMarketListing listing, std::vector<unsigned char>& vchListingSig);
bool CheckSignature(CSignedMarketListing listing);
bool SignBuyRequest(CBuyRequest req, std::vector<unsigned char>& vchSig);
bool CheckSignature(CBuyRequest req);
bool SignBuyAccept(CBuyAccept req, std::vector<unsigned char>& vchSig);
bool CheckSignature(CBuyAccept req);
bool SignBuyReject(CBuyReject req, std::vector<unsigned char>& vchSig);
bool CheckSignature(CBuyReject req);
bool SignDeliveryDetails(CDeliveryDetails req, std::vector<unsigned char>& vchSig);
bool CheckSignature(CDeliveryDetails req);
bool SignCancelListing(CCancelListing listing, std::vector<unsigned char>& vchSig);
bool CheckSignature(CCancelListing listing);
bool SignRefundRequest(CRefundRequest req, std::vector<unsigned char>& vchSig);
bool CheckSignature(CRefundRequest req);
bool SignEscrowRelease(CEscrowRelease req, std::vector<unsigned char>& vchSig);
bool CheckSignature(CEscrowRelease req);
bool SignEscrowPayment(CEscrowPayment req, std::vector<unsigned char>& vchSig);
bool CheckSignature(CEscrowPayment req);
bool SignPaymentRequest(CPaymentRequest req, std::vector<unsigned char>& vchSig);
bool CheckSignature(CPaymentRequest req);

// Market leveldb ops
bool WriteListings(std::map<uint256, CSignedMarketListing> mapListings);
bool ReadListings(std::map<uint256, CSignedMarketListing>& mapListings);
bool WriteBuyRequests(std::map<uint256, CBuyRequest> buyRequests);
bool ReadBuyRequests(std::map<uint256, CBuyRequest>& buyRequests);
bool WriteBuyAccepts(std::map<uint256, CBuyAccept> buyAccepts);
bool ReadBuyAccepts(std::map<uint256, CBuyAccept>& buyAccepts);
bool WriteBuyRejects(std::map<uint256, CBuyReject> buyRejects);
bool ReadBuyRejects(std::map<uint256, CBuyReject>& buyRejects);
bool WriteCancelListings(std::map<uint256, CCancelListing> cancels);
bool ReadCancelListings(std::map<uint256, CCancelListing>& cancels);
bool WriteDeliveryDetails(std::map<uint256, CDeliveryDetails> details);
bool ReadDeliveryDetails(std::map<uint256, CDeliveryDetails>& details);
bool WriteEscrowReleases(std::map<uint256, CEscrowRelease> releases);
bool ReadEscrowReleases(std::map<uint256, CEscrowRelease>& releases);
bool WriteEscrowPayments(std::map<uint256, CEscrowPayment> payments);
bool ReadEscrowPayments(std::map<uint256, CEscrowPayment>& payments);
bool WriteRefundRequests(std::map<uint256, CRefundRequest> refunds);
bool ReadRefundRequests(std::map<uint256, CRefundRequest>& refunds);
bool WritePaymentRequests(std::map<uint256, CPaymentRequest> requests);
bool ReadPaymentRequests(std::map<uint256, CPaymentRequest>& requests);
bool TryOpenMarketDB();

class CCancelListing
{
public:
    int nVersion;
    uint256 listingId;
    CPubKey sellerKey;
    int64_t nDate;
    std::vector<unsigned char> vchSig;

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(listingId);
            READWRITE(sellerKey);
            READWRITE(nDate);
            READWRITE(vchSig);
    )

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    void BroadcastToAll() const;
    bool RelayTo(CNode* pnode) const;

    bool operator < (const CCancelListing &rhs) const
    {
        return GetHash() < rhs.GetHash();
    }
};

class CDeliveryDetails
{
public:
    int nVersion;
    uint256 listingId;
    uint256 buyRequestId;
    std::string sDetails;
    int64_t nDate;
    CPubKey buyerKey;
    std::vector<unsigned char> vchSig;

    CDeliveryDetails()
    {
        nVersion = 0;
    }

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(listingId);
            READWRITE(buyRequestId);
            READWRITE(sDetails);
            READWRITE(nDate);
            READWRITE(buyerKey);
            READWRITE(vchSig);
    )

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    void BroadcastToAll() const;
    bool RelayTo(CNode* pnode) const;

    bool operator < (const CDeliveryDetails &rhs) const
    {
        return GetHash() < rhs.GetHash();
    }
};

class CRefundRequest
{
public:
    int nVersion;
    uint256 listingId;
    uint256 buyRequestId;
    int64_t nDate;
    CPubKey buyerKey;
    std::vector<unsigned char> vchSig;
    std::string rawTx; // buyer created raw multisig tx

    CRefundRequest()
    {
        nVersion = 0;
    }

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(listingId);
            READWRITE(buyRequestId);
            READWRITE(nDate);
            READWRITE(buyerKey);
            READWRITE(rawTx);
    )

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    void BroadcastToAll() const;
    bool RelayTo(CNode* pnode) const;

    bool operator < (const CRefundRequest &rhs) const
    {
        return GetHash() < rhs.GetHash();
    }
};

class CPaymentRequest
{
public:
    int nVersion;
    std::string raw;
    int64_t nDate;
    CPubKey sellerKey;
    uint256 listingId;
    uint256 requestId;
    std::vector<unsigned char> vchSig;
    std::string rawTx; // raw tx created by vendor

    CPaymentRequest()
    {
        nVersion = 0;
    }

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(raw);
            READWRITE(nDate);
            READWRITE(sellerKey);
            READWRITE(listingId);
            READWRITE(requestId);
            READWRITE(vchSig);
            READWRITE(rawTx);
    )

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    void BroadcastToAll() const;
    bool RelayTo(CNode* pnode) const;

    bool operator < (const CPaymentRequest &rhs) const
    {
        return GetHash() < rhs.GetHash();
    }

};

class CEscrowRelease
{
public:
    int nVersion;
    std::string raw;
    int64_t nDate;
    CPubKey buyerKey;
    uint256 listingId;
    uint256 requestId;
    std::vector<unsigned char> vchSig;
    uint256 buyerEscrowLockTxHash; // hash of the buyers escrow lock tx funding the escrow address

    CEscrowRelease()
    {
        nVersion = 0;
    }

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(raw);
            READWRITE(nDate);
            READWRITE(buyerKey);
            READWRITE(listingId);
            READWRITE(requestId);
            READWRITE(vchSig);
            READWRITE(buyerEscrowLockTxHash);
    )

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    void BroadcastToAll() const;
    bool RelayTo(CNode* pnode) const;

    bool operator < (const CEscrowRelease &rhs) const
    {
        return GetHash() < rhs.GetHash();
    }
};

class CEscrowPayment
{
public:
    int nVersion;
    std::string raw;
    int64_t nDate;
    CPubKey buyerKey;
    std::vector<unsigned char> vchSig;
    uint256 listingId;
    uint256 requestId;
    std::string rawTx;

    CEscrowPayment()
    {
        nVersion = 0;
    }

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(raw);
            READWRITE(nDate);
            READWRITE(buyerKey);
            READWRITE(listingId);
            READWRITE(vchSig);
            READWRITE(rawTx);
    )

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    void BroadcastToAll() const;
    bool RelayTo(CNode* pnode) const;

    bool operator < (const CEscrowPayment &rhs) const
    {
        return GetHash() < rhs.GetHash();
    }
};

class CBuyAccept
{
public:
    int nVersion;
    uint256 listingId;
    uint256 buyRequestId;
    int64_t nDate;
    std::string raw;
    CPubKey sellerKey;
    std::vector<unsigned char> vchSig;
    uint256 sellerEscrowLockTxHash; // hash of the sellers escrow lock tx funding the escrow address
    std::string escrowAddress;

    CBuyAccept()
    {
        nVersion = 0;
        nDate = GetTime();
    }

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(listingId);
            READWRITE(buyRequestId);
            READWRITE(nDate);
            READWRITE(raw);
            READWRITE(sellerKey);
            READWRITE(vchSig);
            READWRITE(sellerEscrowLockTxHash);
            READWRITE(escrowAddress);
    )

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    void BroadcastToAll() const;
    bool RelayTo(CNode* pnode) const;

    bool operator < (const CBuyAccept &rhs) const
    {
        return GetHash() < rhs.GetHash();
    }
};

class CBuyReject
{
public:
    int nVersion;
    uint256 listingId;
    uint256 buyRequestId;
    int64_t nDate;
    CPubKey sellerKey;
    std::vector<unsigned char> vchSig;

    CBuyReject()
    {
        nVersion = 0;
        nDate = GetTime();
    }

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(listingId);
            READWRITE(buyRequestId);
            READWRITE(nDate);
            READWRITE(sellerKey);
            READWRITE(vchSig);
    )

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    void BroadcastToAll() const;
    bool RelayTo(CNode* pnode) const;

    bool operator < (const CBuyReject &rhs) const
    {
        return GetHash() < rhs.GetHash();
    }
};

class CBuyRequest
{
public:
    int nVersion;
    uint256 listingId;
    int nStatus;
    CPubKey buyerKey;
    int64_t nDate;
    uint256 requestId; // need to store the request id hash
    std::vector<unsigned char> vchSig;
    uint256 buyerEscrowLockTxHash; // hash of the buyers escrow lock tx funding the escrow address
    uint256 sellerEscrowLockTxHash; // hash of the sellers escrow lock tx funding the escrow address
    std::string rawTx;
    std::string escrowAddress;

    CBuyRequest()
    {
        nVersion = 0;
        nDate = GetTime();
    }

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(listingId);
            READWRITE(nStatus);
            READWRITE(buyerKey);
            READWRITE(nDate);
            READWRITE(vchSig);
            READWRITE(requestId);
            READWRITE(buyerEscrowLockTxHash);
            READWRITE(sellerEscrowLockTxHash);
            READWRITE(rawTx);
            READWRITE(escrowAddress);
    )

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    void BroadcastToAll() const;
    bool RelayTo(CNode* pnode) const;

    bool operator < (const CBuyRequest &rhs) const
    {
        return GetHash() < rhs.GetHash();
    }
};

// Listing Placement
// This is created by the vendor as a sell
class CMarketListing
{
public:
    int nVersion;
    std::string sCategory;
    std::string sTitle;
    std::string sDescription;
    std::string sImageOneUrl;
    std::string sImageTwoUrl;
    int64_t nPrice;
    int64_t nCreated; // listings are valid for 7 days before auto-expiring
    CPubKey sellerKey;
    int nStatus;

    CMarketListing()
    {
        SetNull();
    }

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(sCategory);
            READWRITE(sTitle);
            READWRITE(sDescription);
            READWRITE(sImageOneUrl);
            READWRITE(sImageTwoUrl);
            READWRITE(nPrice);
            READWRITE(nCreated);
            READWRITE(sellerKey);
            READWRITE(nStatus);
    )

    void SetNull()
    {
        nStatus = LISTED;
        nVersion = 0;
        sCategory = "";
        sTitle = "";
        sDescription = "";
        sImageOneUrl = "";
        sImageTwoUrl = "";
        nPrice = 0;
        nCreated = GetTime();
    }

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

};

class CSignedMarketListing
{
public:
    int nVersion;
    CMarketListing listing;
    std::vector<unsigned char> vchListingSig;

    IMPLEMENT_SERIALIZE(
            READWRITE(nVersion);
            READWRITE(listing);
            READWRITE(vchListingSig);
    )

    CSignedMarketListing()
    {
        nVersion = 0;
    }

    uint256 GetHash() const
    {
        return SerializeHash(*this);
    }

    void BroadcastToAll() const;
    bool RelayTo(CNode* pnode) const;

    bool operator < (const CSignedMarketListing &rhs) const
    {
        return listing.GetHash() < rhs.listing.GetHash();
    }
};

class MarketDB
{
public:
    MarketDB()
    {
        activeBatch = NULL;
    };

    ~MarketDB()
    {
        if (activeBatch)
            delete activeBatch;
    };

    bool Open(const char* pszMode="r+");

    bool ScanBatch(const CDataStream& key, std::string* value, bool* deleted) const;

    bool TxnBegin();
    bool TxnCommit();
    bool TxnAbort();

    bool ReadListings(std::map<uint256, CSignedMarketListing>& mapListings);
    bool WriteListings(std::map<uint256, CSignedMarketListing> mapListings);
    bool WriteBuyRequests(std::map<uint256, CBuyRequest> buyRequests);
    bool ReadBuyRequests(std::map<uint256, CBuyRequest>& buyRequests);
    bool WriteBuyAccepts(std::map<uint256, CBuyAccept> buyAccepts);
    bool ReadBuyAccepts(std::map<uint256, CBuyAccept>& buyAccepts);
    bool WriteBuyRejects(std::map<uint256, CBuyReject> buyRejects);
    bool ReadBuyRejects(std::map<uint256, CBuyReject>& buyRejects);
    bool WriteCancelListings(std::map<uint256, CCancelListing> cancels);
    bool ReadCancelListings(std::map<uint256, CCancelListing>& cancels);
    bool WriteDeliveryDetails(std::map<uint256, CDeliveryDetails> details);
    bool ReadDeliveryDetails(std::map<uint256, CDeliveryDetails>& details);
    bool WriteEscrowReleases(std::map<uint256, CEscrowRelease> releases);
    bool ReadEscrowReleases(std::map<uint256, CEscrowRelease>& releases);
    bool WriteEscrowPayments(std::map<uint256, CEscrowPayment> payments);
    bool ReadEscrowPayments(std::map<uint256, CEscrowPayment>& payments);
    bool WriteRefundRequests(std::map<uint256, CRefundRequest> refunds);
    bool ReadRefundRequests(std::map<uint256, CRefundRequest>& refunds);
    bool WritePaymentRequests(std::map<uint256, CPaymentRequest> requests);
    bool ReadPaymentRequests(std::map<uint256, CPaymentRequest>& requests);

    leveldb::DB *pdb;       // points to the global instance
    leveldb::WriteBatch *activeBatch;
};

#endif