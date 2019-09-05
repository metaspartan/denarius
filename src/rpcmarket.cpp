// Copyright (c) 2019 The Denarius developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcmarket.h"
#include "bitcoinrpc.h"
#include "market.h"
#include "smessage.h"
#include "txdb.h"
#include "main.h"
#include "wallet.h"
#include "init.h" // pwalletMain

#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>

using namespace boost;
using namespace json_spirit;
using namespace std;

Value marketlistings(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("marketlistings \n"
                            "List of all active dMarket listings \n"
                            "parameters: none");

    Array ret;

    LOCK(cs_markets);

    BOOST_FOREACH(PAIRTYPE(const uint256, CSignedMarketListing)& p, mapListings)
    {
        // only show if no buy requests
        CSignedMarketListing item = p.second;

        BOOST_FOREACH(PAIRTYPE(const uint256, CBuyRequest)& b, mapBuyRequests)
        {
            if(b.second.listingId == item.GetHash())
            {
                if(b.second.nStatus != LISTED || b.second.nStatus != BUY_REQUESTED)
                {
                    //found
                    continue; // go to next item, this one someone is already buying
                }
            }
        }

        if (item.listing.sTitle != "")
        {
            Object obj;
            //int price
            obj.push_back(Pair("title", item.listing.sTitle));
            obj.push_back(Pair("category", item.listing.sCategory));
            obj.push_back(Pair("itemId", item.GetHash().ToString()));
            obj.push_back(Pair("vendorId", CBitcoinAddress(item.listing.sellerKey.GetID()).ToString()));
            obj.push_back(Pair("price", item.listing.nPrice));
            std::string statusText = "UNKNOWN";
            switch(item.listing.nStatus)
            {
                case LISTED:
                    statusText = "Listed";
                    break;
                case BUY_REQUESTED:
                    statusText = "Buy Requested";
                    break;
                case BUY_ACCEPTED:
                    statusText = "Accepted";
                    break;
                case BUY_REJECTED:
                    statusText = "Rejected";
                    break;
                case ESCROW_LOCK:
                    statusText = "Escrow Locked";
                    break;
                case DELIVERY_DETAILS:
                    statusText = "Delivery Details";
                    break;
                case ESCROW_PAID:
                    statusText = "Escrow Paid";
                    break;
                case REFUND_REQUESTED:
                    statusText = "Refund Requested";
                    break;
                case REFUNDED:
                    statusText = "Refunded";
                    break;
                case PAYMENT_REQUESTED:
                    statusText = "Payment Requested";
                    break;
                default:
                    statusText = "UNKNOWN";
                    break;
            }
            obj.push_back(Pair("status", statusText));
            obj.push_back(Pair("urlImage1", item.listing.sImageOneUrl));
            obj.push_back(Pair("urlImage2", item.listing.sImageTwoUrl));
            obj.push_back(Pair("description", item.listing.sDescription));
            obj.push_back(Pair("creationDate", DateTimeStrFormat(item.listing.nCreated)));
            obj.push_back(Pair("expirationDate", DateTimeStrFormat(item.listing.nCreated + (LISTING_DEFAULT_DURATION))));

            ret.push_back(obj);
        }

    }

    return ret;
}

Value marketsearchlistings(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error("marketsearchlistings \n"
                            "List active dMarket listings that meet search criteria \n"
                            "parameters: <search>");

    string strSearch = params[0].get_str();
    std::transform(strSearch.begin(), strSearch.end(),strSearch.begin(), ::tolower);

    Array ret;

    LOCK(cs_markets);

    BOOST_FOREACH(PAIRTYPE(const uint256, CSignedMarketListing)& p, mapListings)
    {
        // only show if no buy requests
        bool bFound = false;
        CSignedMarketListing item = p.second;

        BOOST_FOREACH(PAIRTYPE(const uint256, CBuyRequest)& b, mapBuyRequests)
        {
            if(b.second.listingId == item.GetHash())
            {
                if(b.second.nStatus != LISTED || b.second.nStatus != BUY_REQUESTED)
                {
                    bFound = true;
                }
            }
        }

        if(bFound)
            continue; // go to next item, this one someone is already buying

        std::string titleItem = item.listing.sTitle;
        std::transform(titleItem.begin(), titleItem.end(), titleItem.begin(), ::tolower);

        int foundSearch = titleItem.find(strSearch);

        if (foundSearch != -1 && titleItem != "")
        {
            Object obj;

            obj.push_back(Pair("title", item.listing.sTitle));
            obj.push_back(Pair("category", item.listing.sCategory));
            obj.push_back(Pair("itemId", item.GetHash().ToString()));
            obj.push_back(Pair("vendorId", CBitcoinAddress(item.listing.sellerKey.GetID()).ToString()));
            obj.push_back(Pair("price", item.listing.nPrice));
            std::string statusText = "UNKNOWN";
            switch(item.listing.nStatus)
            {
                case LISTED:
                    statusText = "Listed";
                    break;
                case BUY_REQUESTED:
                    statusText = "Buy Requested";
                    break;
                case BUY_ACCEPTED:
                    statusText = "Accepted";
                    break;
                case BUY_REJECTED:
                    statusText = "Rejected";
                    break;
                case ESCROW_LOCK:
                    statusText = "Escrow Locked";
                    break;
                case DELIVERY_DETAILS:
                    statusText = "Delivery Details";
                    break;
                case ESCROW_PAID:
                    statusText = "Escrow Paid";
                    break;
                case REFUND_REQUESTED:
                    statusText = "Refund Requested";
                    break;
                case REFUNDED:
                    statusText = "Refunded";
                    break;
                case PAYMENT_REQUESTED:
                    statusText = "Payment Requested";
                    break;
                default:
                    statusText = "UNKNOWN";
                    break;
            }
            obj.push_back(Pair("status", statusText));
            obj.push_back(Pair("urlImage1", item.listing.sImageOneUrl));
            obj.push_back(Pair("urlImage2", item.listing.sImageTwoUrl));
            obj.push_back(Pair("description", item.listing.sDescription));
            obj.push_back(Pair("creationDate", DateTimeStrFormat(item.listing.nCreated)));
            obj.push_back(Pair("expirationDate", DateTimeStrFormat(item.listing.nCreated + (LISTING_DEFAULT_DURATION))));


            ret.push_back(obj);
        }

    }

    return ret;
}

Value marketbuy(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error("marketbuy \n"
                            "Buys a dMarket listing \n"
                            "parameters: <listingId>");

    string itemID = params[0].get_str();
    uint256 listingHashId = uint256(itemID);

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first to unlock your wallet.");

    CBuyRequest buyRequest;
    buyRequest.buyerKey = pwalletMain->GenerateNewKey();
    buyRequest.listingId = listingHashId;
    buyRequest.nStatus = BUY_REQUESTED;
    buyRequest.requestId = buyRequest.GetHash();
    SignBuyRequest(buyRequest, buyRequest.vchSig);
    ReceiveBuyRequest(buyRequest);
    buyRequest.BroadcastToAll();

    return Value::null;
}

Value marketapprovebuy(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 2)
        throw runtime_error("marketapprovebuy \n"
                            "Approves dMarket listing buy request \n"
                            "parameters: <requestId>");


    string requestID = params[0].get_str();

    uint256 requestHashId = uint256(requestID);
    uint256 listingHashId = mapBuyRequests[requestHashId].listingId;

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first to unlock your wallet.");

    CBuyAccept accept;
    accept.listingId = listingHashId;
    accept.buyRequestId = requestHashId;
    accept.nDate = GetTime();
    accept.sellerKey = pwalletMain->GenerateNewKey();

    CBuyRequest buyRequest = mapBuyRequests[requestHashId];
    // create the escrow lock address
    std::string errors;
    std::string escrowAddress;

    AddMultisigAddress(mapListings[listingHashId].listing.sellerKey, mapBuyRequests[requestHashId].buyerKey, escrowAddress, errors);
    accept.escrowAddress = escrowAddress;

    // fund it
    std::string strError = "";
    CWalletTx wtxNew;

    CreateEscrowLockTx(accept.escrowAddress, mapListings[buyRequest.listingId].listing.nPrice + (0.01 * COIN), strError, wtxNew);

    // serialize the tx to a string
    CDataStream ssTx(SER_NETWORK, CLIENT_VERSION);
    ssTx.reserve(sizeof(wtxNew));
    ssTx << wtxNew;

    // misuse this parameter like a boss
    accept.raw = ssTx.str();

    SignBuyAccept(accept, accept.vchSig);
    ReceiveBuyAccept(accept);
    accept.BroadcastToAll();

    return Value::null;
}

Value marketrejectbuy(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 2)
        throw runtime_error("marketrejectbuy \n"
                            "Rejects dMarket listing buy request \n"
                            "parameters: <requestId>");

    string requestID = params[0].get_str();

    uint256 requestHashId = uint256(requestID);
    uint256 listingHashId = mapBuyRequests[requestHashId].listingId;

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first to unlock your wallet.");

    CBuyReject reject;
    reject.listingId = listingHashId;
    reject.buyRequestId = requestHashId;
    reject.nDate = GetTime();
    reject.sellerKey = pwalletMain->GenerateNewKey();
    SignBuyReject(reject, reject.vchSig);
    ReceiveBuyReject(reject);
    reject.BroadcastToAll();

    return Value::null;
}

Value marketmylistings(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("marketmylistings \n"
                            "Gets your dMarket listings \n"
                            "parameters: none");

    Array ret;

    LOCK(cs_markets);

    BOOST_FOREACH(PAIRTYPE(const uint256, CSignedMarketListing)& p, mapListings)
    {

        CTxDestination dest = p.second.listing.sellerKey.GetID();

        if(IsMine(*pwalletMain, dest))
        {

            CSignedMarketListing item = p.second;
            Object obj;

            obj.push_back(Pair("title", item.listing.sTitle));
            obj.push_back(Pair("category", item.listing.sCategory));
            obj.push_back(Pair("itemId", item.GetHash().ToString()));
            obj.push_back(Pair("vendorId", CBitcoinAddress(item.listing.sellerKey.GetID()).ToString()));
            obj.push_back(Pair("price", item.listing.nPrice));
            obj.push_back(Pair("status", item.listing.nStatus));
            obj.push_back(Pair("urlImage1", item.listing.sImageOneUrl));
            obj.push_back(Pair("urlImage2", item.listing.sImageTwoUrl));
            obj.push_back(Pair("description", item.listing.sDescription));
            obj.push_back(Pair("creationDate", DateTimeStrFormat(item.listing.nCreated)));
            obj.push_back(Pair("expirationDate", DateTimeStrFormat(item.listing.nCreated + (LISTING_DEFAULT_DURATION))));

            ret.push_back(obj);
        }
    }

    return ret;
}

Value marketsell(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 6)
        throw runtime_error("marketsell\n"
                            "Creates a new dMarket listing\n"
                            "parameters: <title> <category> <price> <image1> <image2> <description> \n");

    std::string title = params[0].get_str();
    std::string category = params[1].get_str();
    double price = boost::lexical_cast<double>(params[2].get_str());;
    std::string image1 = params[3].get_str();
    std::string image2 = params[4].get_str();
    std::string description = params[5].get_str();

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first to unlock your wallet.");

    // create market listing object
    CMarketListing listing;
    listing.sTitle = title;
    listing.sCategory = category;
    listing.nPrice = price * COIN;
    listing.sImageOneUrl = image1;
    listing.sImageTwoUrl = image2;
    listing.sDescription = description;
    listing.nCreated = GetTime();
    listing.sellerKey = pwalletMain->GenerateNewKey();

    CSignedMarketListing signedListing;
    signedListing.listing = listing;
    SignListing(listing, signedListing.vchListingSig);
    signedListing.BroadcastToAll();
    ReceiveListing(signedListing);

    LogPrintf("marketsell RPC: Submitted new Listing Key %s", signedListing.GetHash().ToString());

    Object obj;

    obj.push_back(Pair("sellerKey", listing.sellerKey.GetHash().ToString()));
    obj.push_back(Pair("listingKey", signedListing.GetHash().ToString()));

    return obj;
}

Value marketbuyrequests(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("marketbuyrequests \n"
                            "Returns your dMarket buy requests \n"
                            "parameters: none");
    Array ret;

    LOCK(cs_markets);

    BOOST_FOREACH(PAIRTYPE(const uint256, CBuyRequest)& p, mapBuyRequests)
    {
        // if it is a buy request for one of our listings, add it to the JSON array
        CBuyRequest buyRequest = p.second;
        if(mapListings.find(buyRequest.listingId) != mapListings.end())
        {
            CMarketListing item = mapListings[buyRequest.listingId].listing;
            CTxDestination dest = item.sellerKey.GetID();
            if(IsMine(*pwalletMain, dest))
            {
                Object obj;

                std::string statusText = "UNKNOWN";
                switch(item.nStatus)
                {
                    case LISTED:
                        statusText = "Listed";
                        break;
                    case BUY_REQUESTED:
                        statusText = "Buy Requested";
                        break;
                    case BUY_ACCEPTED:
                        statusText = "Accepted";
                        break;
                    case BUY_REJECTED:
                        statusText = "Rejected";
                        break;
                    case ESCROW_LOCK:
                        statusText = "Escrow Locked";
                        break;
                    case DELIVERY_DETAILS:
                        statusText = "Delivery Details";
                        break;
                    case ESCROW_PAID:
                        statusText = "Escrow Paid";
                        break;
                    case REFUND_REQUESTED:
                        statusText = "Refund Requested";
                        break;
                    case REFUNDED:
                        statusText = "Refunded";
                        break;
                    case PAYMENT_REQUESTED:
                        statusText = "Payment Requested";
                        break;
                    default:
                        statusText = "UNKNOWN";
                        break;
                }

                obj.push_back(Pair("requestId", buyRequest.GetHash().ToString()));
                obj.push_back(Pair("buyerId", buyRequest.buyerKey.GetID().ToString()));
                obj.push_back(Pair("title", item.sTitle));
                obj.push_back(Pair("category", item.sCategory));
                obj.push_back(Pair("itemId", item.GetHash().ToString()));
                obj.push_back(Pair("vendorId", CBitcoinAddress(item.sellerKey.GetID()).ToString()));
                obj.push_back(Pair("price", item.nPrice));
                obj.push_back(Pair("status", statusText));
                obj.push_back(Pair("urlImage1", item.sImageOneUrl));
                obj.push_back(Pair("urlImage2", item.sImageTwoUrl));
                obj.push_back(Pair("description", item.sDescription));
                obj.push_back(Pair("creationDate", DateTimeStrFormat(item.nCreated)));
                obj.push_back(Pair("buyRequestDate", DateTimeStrFormat(buyRequest.nDate)));

                ret.push_back(obj);
            }
        }
    }

    return ret;
}

Value marketmybuys(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error("marketmybuys \n"
                            "Returns your dMarket buys \n"
                            "parameters: none");

    Array ret;

    LOCK(cs_markets);

    BOOST_FOREACH(PAIRTYPE(const uint256, CBuyRequest)& p, mapBuyRequests)
    {
        CBuyRequest buyRequest = p.second;
        CTxDestination dest = buyRequest.buyerKey.GetID();
        if(IsMine(*pwalletMain, dest))
        {
            std::string statusText = "UNKNOWN";
            switch(p.second.nStatus)
            {
                case LISTED:
                    statusText = "Listed";
                    break;
                case BUY_REQUESTED:
                    statusText = "Buy Requested";
                    break;
                case BUY_ACCEPTED:
                    statusText = "Accepted";
                    break;
                case BUY_REJECTED:
                    statusText = "Rejected";
                    break;
                case ESCROW_LOCK:
                    statusText = "Escrow Locked";
                    break;
                case DELIVERY_DETAILS:
                    statusText = "Delivery Details";
                    break;
                case ESCROW_PAID:
                    statusText = "Escrow Paid";
                    break;
                case REFUND_REQUESTED:
                    statusText = "Refund Requested";
                    break;
                case REFUNDED:
                    statusText = "Refunded";
                    break;
                case PAYMENT_REQUESTED:
                    statusText = "Payment Requested";
                    break;
                default:
                    statusText = "UNKNOWN";
                    break;
            }

            Object obj;
            CMarketListing item = mapListings[buyRequest.listingId].listing;

            obj.push_back(Pair("requestId", buyRequest.GetHash().ToString()));
            obj.push_back(Pair("title", item.sTitle));
            obj.push_back(Pair("category", item.sCategory));
            obj.push_back(Pair("itemId", item.GetHash().ToString()));
            obj.push_back(Pair("vendorId", CBitcoinAddress(item.sellerKey.GetID()).ToString()));
            obj.push_back(Pair("price", item.nPrice));
            obj.push_back(Pair("status", statusText));
            obj.push_back(Pair("urlImage1", item.sImageOneUrl));
            obj.push_back(Pair("urlImage2", item.sImageTwoUrl));
            obj.push_back(Pair("description", item.sDescription));
            obj.push_back(Pair("creationDate", DateTimeStrFormat(item.nCreated)));
            obj.push_back(Pair("buyRequestDate", DateTimeStrFormat(buyRequest.nDate)));
            ret.push_back(obj);
        }
    }

    return ret;
}

Value marketcancellisting(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error("marketcancellisting \n"
                            "Cancels a dMarket listing \n"
                            "parameters: <itemId>");

    string itemID = params[0].get_str();
    uint256 listingHashId = uint256(itemID);

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first to unlock your wallet.");

    CCancelListing cancel;
    cancel.listingId = listingHashId;
    cancel.sellerKey = pwalletMain->GenerateNewKey();
    cancel.nDate = GetTime();
    SignCancelListing(cancel, cancel.vchSig);
    ReceiveCancelListing(cancel);
    cancel.BroadcastToAll();

    return Value::null;
}

Value marketcancelescrow(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error("marketcancelescrow \n"
                            "Cancels a dMarket escrow \n"
                            "parameters: <requestId>");

    string requestID = params[0].get_str();

    uint256 requestHashId = uint256(requestID);
    uint256 listingHashId = mapBuyRequests[requestHashId].listingId;

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first to unlock your wallet.");

    CBuyReject reject;
    reject.listingId = listingHashId;
    reject.buyRequestId = requestHashId;
    reject.nDate = GetTime();
    reject.sellerKey = pwalletMain->GenerateNewKey();
    SignBuyReject(reject, reject.vchSig);
    ReceiveBuyReject(reject);
    reject.BroadcastToAll();

    return Value::null;
}

Value marketrequestpayment(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error("marketrequestpayment \n"
                            "Request a payment for a dMarket item \n"
                            "parameters: <requestId>");

    string requestID = params[0].get_str();

    uint256 requestHashId = uint256(requestID);
    uint256 listingHashId = mapBuyRequests[requestHashId].listingId;

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first to unlock your wallet.");

    CPaymentRequest request;
    request.listingId = listingHashId;
    request.requestId = requestHashId;
    request.nDate = GetTime();
    request.sellerKey = pwalletMain->GenerateNewKey();

    CBuyRequest buyRequest = mapBuyRequests[requestHashId];

    std::string rawTx = PayEscrow(buyRequest.buyerEscrowLockTxHash, buyRequest.sellerEscrowLockTxHash, mapListings[buyRequest.listingId].listing.sellerKey, 2*mapListings[buyRequest.listingId].listing.nPrice);

    request.rawTx = rawTx;
    SignPaymentRequest(request, request.vchSig);
    ReceivePaymentRequest(request);
    request.BroadcastToAll();

    return Value::null;
}

Value marketrefund(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error("marketrefund \n"
                            "Issues a refund for a buy request \n"
                            "parameters: <requestId>");
    //TODO: SegFault?
    string requestID = params[0].get_str();
    uint256 requestHashId = uint256(requestID);

    CBuyRequest buyRequest = mapBuyRequests[requestHashId];

    // get the raw tx off of the request
    std::string rawTx = SignMultiSigTransaction(buyRequest.rawTx);

    // broadcast the payment transaction
    CTransaction tx;
    vector<unsigned char> txData(ParseHex(rawTx));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    //ssData >> tx;

    //AcceptToMemoryPool(mempool, tx, false, NULL);

    // deserialize binary data stream
    try {
        ssData >> tx;
    }
    catch (std::exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    uint256 hashTx = tx.GetHash();

    // See if the transaction is already in a block
    // or in the memory pool:
    CTransaction existingTx;
    uint256 hashBlock = 0;
    if (GetTransaction(hashTx, existingTx, hashBlock))
    {
        if (hashBlock != 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("transaction already in block ")+hashBlock.GetHex());
        // Not in block, but already in the memory pool; will drop
        // through to re-relay it.
    }
    else
    {
        // push to local node
        CTxDB txdb("r");
        if (!tx.AcceptToMemoryPool(txdb))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX rejected");

        SyncWithWallets(tx, NULL, true);
    }
    RelayTransaction(tx, hashTx);

    return hashTx.GetHex();

    //return Value::null;
}

Value marketescrowlock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error("marketescrowlock \n"
                            "Locks escrow for a buy request \n"\
                            "parameters: <requestId>");

    string requestID = params[0].get_str();
    uint256 requestHashId = uint256(requestID);
    CBuyRequest buyRequest = mapBuyRequests[requestHashId];

    bool accepted = false;
    bool res;

    // deserialize the seller's escrow tx
    BOOST_FOREACH(PAIRTYPE(const uint256, CBuyAccept)& p, mapBuyAccepts)
    {
        if(p.second.listingId == buyRequest.listingId && p.second.buyRequestId == requestHashId)
        {
            // found seller's buy accept
            CWalletTx wtxSeller;
            CDataStream ssTx(p.second.raw.data(), p.second.raw.data() + p.second.raw.size(), SER_NETWORK, CLIENT_VERSION);
            ssTx >> wtxSeller;
            accepted = wtxSeller.AcceptToMemoryPool();
            break;
        }
    }

    CEscrowRelease release;
    release.nDate = GetTime();
    release.buyerKey = buyRequest.buyerKey;
    release.listingId = buyRequest.listingId;
    release.requestId = requestHashId;

    std::string strError = "";
    CWalletTx wtxNew;
    CReserveKey reserveKey(pwalletMain);
    res = CreateEscrowLockTx(buyRequest.escrowAddress, mapListings[buyRequest.listingId].listing.nPrice + (0.01 * COIN), strError, wtxNew);
    pwalletMain->CommitTransaction(wtxNew, reserveKey);

    release.buyerEscrowLockTxHash = wtxNew.GetHash();
    SignEscrowRelease(release, release.vchSig);
    ReceiveEscrowRelease(release);
    release.BroadcastToAll();

    return Value::null;
}

Value marketreleaseescrow(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error("marketreleaseescrow \n"
                            "Releases your dMarket escrow \n"
                            "parameters: <requestId>");

    string requestID = params[0].get_str();
    uint256 requestHashId = uint256(requestID);

    CBuyRequest buyRequest;
    buyRequest = mapBuyRequests[requestHashId];

    CEscrowPayment payment;
    payment.nDate = GetTime();
    payment.buyerKey = buyRequest.buyerKey;
    payment.listingId = buyRequest.listingId;
    payment.requestId = requestHashId;

    // get the raw tx off of the request
    std::string rawTx = SignMultiSigTransaction(buyRequest.rawTx);

    // broadcast the payment transaction
    CTransaction tx;
    vector<unsigned char> txData(ParseHex(rawTx));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    //TO DO? dMarket
    //ssData >> tx;
    //AcceptToMemoryPool(mempool, tx, false, NULL);

    // deserialize binary data stream
    try {
        ssData >> tx;
    }
    catch (std::exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    uint256 hashTx = tx.GetHash();

    // See if the transaction is already in a block
    // or in the memory pool:
    CTransaction existingTx;
    uint256 hashBlock = 0;
    if (GetTransaction(hashTx, existingTx, hashBlock))
    {
        if (hashBlock != 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("transaction already in block ")+hashBlock.GetHex());
        // Not in block, but already in the memory pool; will drop
        // through to re-relay it.
    }
    else
    {
        // push to local node
        CTxDB txdb("r");
        if (!tx.AcceptToMemoryPool(txdb))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX rejected");

        SyncWithWallets(tx, NULL, true);
    }
    RelayTransaction(tx, hashTx);

    //return hashTx.GetHex();

    // notify the vendor
    payment.rawTx = rawTx;
    SignEscrowPayment(payment, payment.vchSig);
    ReceiveEscrowPayment(payment);
    payment.BroadcastToAll();

    return Value::null;
}

Value marketrequestrefund(const Array& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error("marketrequestrefund \n"
                            "Returns your dMarket buy requests \n"
                            "parameters: <requestId>");

    string requestID = params[0].get_str();
    uint256 requestHashId = uint256(requestID);

    CBuyRequest buyRequest = mapBuyRequests[requestHashId];

    // request a refund
    CRefundRequest refund;
    refund.nDate = GetTime();
    refund.listingId = mapBuyRequests[requestHashId].listingId;
    refund.buyRequestId = requestHashId;
    refund.buyerKey = mapBuyRequests[requestHashId].buyerKey;

    // create a raw transaction that refunds our money
    std::string strError;
    std::string rawTx = RefundEscrow(buyRequest.buyerEscrowLockTxHash, buyRequest.sellerEscrowLockTxHash, mapListings[refund.listingId].listing.sellerKey, 2*mapListings[refund.listingId].listing.nPrice, buyRequest.buyerKey, strError);

    refund.rawTx = rawTx;

    SignRefundRequest(refund, refund.vchSig);
    ReceiveRefundRequest(refund);
    refund.BroadcastToAll();

    return Value::null;
}