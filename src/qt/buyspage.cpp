#include "buyspage.h"
#include "ui_buyspage.h"
#include "deliverydetailsdialog.h"

#include "market.h"
#include "util.h"
#include "main.h"
#include "wallet.h"
#include "base58.h"
#include "key.h"
#include "init.h"

#include "bitcoinrpc.h"
#include "txdb.h"

using namespace std;

#include <QClipboard>
#include <QMessageBox>

BuysPage::BuysPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::BuysPage)
{
    ui->setupUi(this);
    ui->deliveryDetailsButton->setEnabled(false);
    ui->payButton->setEnabled(false);
    ui->refundButton->setEnabled(false);
    ui->copyAddressButton->setEnabled(false);
    ui->escrowLockButton->setEnabled(false);

    LoadBuys();

    subscribeToCoreSignals();
}


BuysPage::~BuysPage()
{
    unsubscribeFromCoreSignals();
    delete ui;
}

void BuysPage::LoadBuys()
{
    LogPrintf("BuysPage::LoadBuys() called.\n");
   // date, status, vendor, item, itemid, request id
    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);
    BOOST_FOREACH(PAIRTYPE(const uint256, CBuyRequest)& p, mapBuyRequests)
    {
	
	    CTxDestination dest = p.second.buyerKey.GetID();
            if(IsMine(*pwalletMain, dest))
	    {
		LogPrintf("Buy Request buyer key Is Mine.\n");
		// add it
		QTableWidgetItem *dateItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat(p.second.nDate)));
		
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
		
		QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(statusText));
		QTableWidgetItem *vendorItem = new QTableWidgetItem(QString::fromStdString(CBitcoinAddress(mapListings[p.second.listingId].listing.sellerKey.GetID()).ToString()));
		QTableWidgetItem *itemItem = new QTableWidgetItem(QString::fromStdString(mapListings[p.second.listingId].listing.sTitle));
		QTableWidgetItem *itemIdItem = new QTableWidgetItem(QString::fromStdString(mapListings[p.second.listingId].GetHash().ToString()));
		QTableWidgetItem *requestIdItem = new QTableWidgetItem(QString::fromStdString(p.second.requestId.ToString()));

		ui->tableWidget->insertRow(0);
		ui->tableWidget->setItem(0, 0, dateItem);
		ui->tableWidget->setItem(0, 1, statusItem);
		ui->tableWidget->setItem(0, 2, vendorItem);
		ui->tableWidget->setItem(0, 3, itemItem);
		ui->tableWidget->setItem(0, 4, itemIdItem);
		ui->tableWidget->setItem(0, 5, requestIdItem);
	    }
	    else
	    {
		LogPrintf("Buy request buyer key Is NOT Mine.\n");
	    }
        
    }
}

void BuysPage::on_tableWidget_itemSelectionChanged()
{
    if(ui->tableWidget->selectedItems().count() > 0)
    {
	ui->copyAddressButton->setEnabled(true);

        QItemSelectionModel* selectionModel = ui->tableWidget->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();
        if(selected.count() == 0)
            return;

        QModelIndex index = selected.at(0);
        int r = index.row();
        std::string status = ui->tableWidget->item(r, 1)->text().toStdString();
               
	// if buy status is ESCROW_LOCKED, enable delivery details
	if(status == "Escrow Locked")
	{
	    ui->refundButton->setEnabled(true);
	    ui->payButton->setEnabled(true);
	    ui->deliveryDetailsButton->setEnabled(true);
	    ui->escrowLockButton->setEnabled(false);
	}
	else if(status == "Delivery Details Sent")
	{
            // if buy status is DELIVERY_DETAILS, enable refund button and pay button
	    ui->payButton->setEnabled(true);
	    ui->refundButton->setEnabled(true);
	}
	else if(status == "Accepted")
	{
	    ui->payButton->setEnabled(false);
	    ui->refundButton->setEnabled(false);
	    ui->escrowLockButton->setEnabled(true);
	}
	else if(status == "Payment Requested" || status == "Escrow Locked")
	{
	    ui->payButton->setEnabled(true);
	    ui->refundButton->setEnabled(true);
	    ui->escrowLockButton->setEnabled(false);
	}
	else if(status == "Escrow Paid")
	{
	    ui->payButton->setEnabled(false);
	    ui->refundButton->setEnabled(true);
	    ui->deliveryDetailsButton->setEnabled(true);
	}
    }
}

void BuysPage::on_copyAddressButton_clicked()
{
    QItemSelectionModel* selectionModel = ui->tableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string address = ui->tableWidget->item(r, 2)->text().toStdString();
    QApplication::clipboard()->setText(QString::fromStdString(address));
}

void BuysPage::on_escrowLockButton_clicked()
{
    // get the vendor created escrow lock tx, sign it and broadcast it
    // ask the user if they really want to pay
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Lock Escrow?", "Do you want to lock escrow for this item?  This will send money to the escrow address.",
                                QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::No) 
    {
	    return;
    }

    //Segfault currently need to investigate
    QItemSelectionModel* selectionModel = ui->tableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string buyRequestIdHash = ui->tableWidget->item(r, 5)->text().toStdString();
    uint256 buyRequestId = uint256(buyRequestIdHash);

    CBuyRequest buyRequest = mapBuyRequests[buyRequestId];

    bool accepted = false;
    // deserialize the seller's escrow tx
    BOOST_FOREACH(PAIRTYPE(const uint256, CBuyAccept)& p, mapBuyAccepts)
    {
	if(p.second.listingId == buyRequest.listingId && p.second.buyRequestId == buyRequestId)
	{
	    // found seller's buy accept
	    CWalletTx wtxSeller;
	    CDataStream ssTx(p.second.raw.data(), p.second.raw.data() + p.second.raw.size(), SER_NETWORK, CLIENT_VERSION);
	    ssTx >> wtxSeller;
	    accepted = wtxSeller.AcceptToMemoryPool();
	    break;
	}
    }

    if(!accepted)
    {
	QMessageBox msg;
        msg.setText("The seller's escrow transaction is invalid.");
        msg.exec();
        return;
    } else {
        CEscrowRelease release;
        release.nDate = GetTime();
        release.buyerKey = buyRequest.buyerKey;
        release.listingId = buyRequest.listingId;
        release.requestId = buyRequestId;

        std::string strError = "";
        CWalletTx wtxNew;
        CReserveKey reserveKey(pwalletMain);
        bool res = CreateEscrowLockTx(buyRequest.escrowAddress, mapListings[buyRequest.listingId].listing.nPrice + (0.01 * COIN), strError, wtxNew);
        pwalletMain->CommitTransaction(wtxNew, reserveKey);
    
        release.buyerEscrowLockTxHash = wtxNew.GetHash();
        SignEscrowRelease(release, release.vchSig);
        ReceiveEscrowRelease(release);
        release.BroadcastToAll();
    }
}

void BuysPage::on_deliveryDetailsButton_clicked()
{
    QItemSelectionModel* selectionModel = ui->tableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string buyRequestIdHash = ui->tableWidget->item(r, 5)->text().toStdString();
    uint256 buyRequestId = uint256(buyRequestIdHash);
    DeliveryDetailsDialog* ddd = new DeliveryDetailsDialog(this, buyRequestId);
    ddd->exec(); 
}

void BuysPage::on_payButton_clicked()
{
    // ask the user if they really want to pay
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Pay?", "Do you want to pay for this item?  This will release the escrow funds to the vendor.",
                                QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::No) 
    {
	    return;
    }

    //TODO: Fix segfault and TX broadcasting
    QItemSelectionModel* selectionModel = ui->tableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string buyRequestIdHash = ui->tableWidget->item(r, 5)->text().toStdString();
    uint256 buyRequestId = uint256(buyRequestIdHash);

    CBuyRequest buyRequest;
    buyRequest = mapBuyRequests[buyRequestId];

    CEscrowPayment payment;
    payment.nDate = GetTime();
    payment.buyerKey = buyRequest.buyerKey;
    payment.listingId = buyRequest.listingId;
    payment.requestId = buyRequestId;
    // get the raw tx off of the request
    //std::string rawTx = PayEscrow(buyRequest.buyerEscrowLockTxHash, buyRequest.sellerEscrowLockTxHash, mapListings[buyRequest.listingId].listing.sellerKey, 2*mapListings[buyRequest.listingId].listing.nValue);
    LogPrintf("on_payButton_clicked(): buyRequest.rawTx: %s\n", buyRequest.rawTx);
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

   // notify the vendor
   payment.rawTx = rawTx;
   SignEscrowPayment(payment, payment.vchSig);
   ReceiveEscrowPayment(payment);
   payment.BroadcastToAll();
}

void BuysPage::on_refundButton_clicked()
{
    // ask the user if they really want to make a refund request
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Request A Refund", "Are you sure you want to request a refund for this item?",
                                QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::No) 
    {
	return;
    }

    QItemSelectionModel* selectionModel = ui->tableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string buyRequestIdHash = ui->tableWidget->item(r, 5)->text().toStdString();
    uint256 buyRequestId = uint256(buyRequestIdHash);
    CBuyRequest buyRequest = mapBuyRequests[buyRequestId];
    // request a refund
    CRefundRequest refund;
    refund.nDate = GetTime();
    refund.listingId = mapBuyRequests[buyRequestId].listingId;
    refund.buyRequestId = buyRequestId;
    refund.buyerKey = mapBuyRequests[buyRequestId].buyerKey;

    // create a raw transaction that refunds our money
    std::string strError;
    std::string rawTx = RefundEscrow(buyRequest.buyerEscrowLockTxHash, buyRequest.sellerEscrowLockTxHash, mapListings[refund.listingId].listing.sellerKey, 2*mapListings[refund.listingId].listing.nPrice, buyRequest.buyerKey, strError);

    refund.rawTx = rawTx;

    SignRefundRequest(refund, refund.vchSig);
    ReceiveRefundRequest(refund);
    refund.BroadcastToAll();
}

static void NotifyBuyAccepted(BuysPage* page)
{
    QMetaObject::invokeMethod(page, "LoadBuys", Qt::QueuedConnection);
}

static void NotifyBuyRejected(BuysPage* page)
{
    QMetaObject::invokeMethod(page, "LoadBuys", Qt::QueuedConnection);
}

static void NotifyBuyRequest(BuysPage* page, CBuyRequest buyr)
{
    QMetaObject::invokeMethod(page, "LoadBuys", Qt::QueuedConnection);
}

static void NotifyPaymentRequest(BuysPage* page)
{
    QMetaObject::invokeMethod(page, "LoadBuys", Qt::QueuedConnection);
}

void BuysPage::subscribeToCoreSignals()
{
    // Connect signals to core
    uiInterface.NotifyBuyAccepted.connect(boost::bind(&NotifyBuyAccepted, this));
    uiInterface.NotifyBuyRejected.connect(boost::bind(&NotifyBuyRejected, this));
    uiInterface.NotifyBuyRequest.connect(boost::bind(&NotifyBuyRequest, this, _1));
    uiInterface.NotifyPaymentRequest.connect(boost::bind(&NotifyPaymentRequest, this));
    uiInterface.NotifyEscrowRelease.connect(boost::bind(&NotifyPaymentRequest, this));
    uiInterface.NotifyEscrowPayment.connect(boost::bind(&NotifyPaymentRequest, this));
}

void BuysPage::unsubscribeFromCoreSignals()
{
    // Disconnect signals from core
    uiInterface.NotifyBuyAccepted.disconnect(boost::bind(&NotifyBuyAccepted, this));
    uiInterface.NotifyBuyRejected.disconnect(boost::bind(&NotifyBuyRejected, this));
    uiInterface.NotifyBuyRequest.disconnect(boost::bind(&NotifyBuyRequest, this, _1));
    uiInterface.NotifyPaymentRequest.disconnect(boost::bind(&NotifyPaymentRequest, this));
    uiInterface.NotifyEscrowRelease.disconnect(boost::bind(&NotifyPaymentRequest, this));
    uiInterface.NotifyEscrowPayment.disconnect(boost::bind(&NotifyPaymentRequest, this));
}
