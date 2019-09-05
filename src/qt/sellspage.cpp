#include "sellspage.h"
#include "ui_sellspage.h"
#include "ui_interface.h"
#include "createmarketlistingdialog.h"
#include "market.h"
#include "base58.h"
#include "init.h"

#include "bitcoinrpc.h"
#include "txdb.h"

using namespace std;

#include <QClipboard>
#include <QMessageBox>
#include <QDebug>

SellsPage::SellsPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SellsPage)
{
    ui->setupUi(this);
    subscribeToCoreSignals();

    ui->cancelButton->setEnabled(false);
    ui->acceptButton->setEnabled(false);
    ui->rejectButton->setEnabled(false);
    ui->copyAddressButton->setEnabled(false);
    ui->refundButton->setEnabled(false);
    ui->cancelButton->setEnabled(false);
    ui->requestPaymentButton->setEnabled(false);

    LoadSells();
    LoadBuyRequests();
}



SellsPage::~SellsPage()
{
    unsubscribeFromCoreSignals();
    delete ui;
}

void SellsPage::on_createButton_clicked()
{
    CreateMarketListingDialog* cm = new CreateMarketListingDialog();
    cm->exec();
}

void SellsPage::on_cancelButton_clicked()
{
    if (pwalletMain->IsLocked())
    {
        QMessageBox msg;
        msg.setText("Error: Wallet is locked, unable to cancel.");
        msg.exec();
        return;
    };

    if (fWalletUnlockStakingOnly)
    {
        QMessageBox msg;
        msg.setText("Error: Wallet unlocked for staking only, unable to cancel.");
        msg.exec();
        return;
    };

    QItemSelectionModel* selectionModel = ui->listingsTableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string id = ui->listingsTableWidget->item(r, 0)->text().toStdString();
    uint256 idHash = uint256(id);

    // ask the user if they really want to put in a buy request
    QMessageBox::StandardButton reply;
      reply = QMessageBox::question(this, "Cancel Listing", "Are you sure you want to cancel the listing for this item?",
                                QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) 
    {
	CCancelListing cancel;
	cancel.listingId = idHash;
	cancel.sellerKey = pwalletMain->GenerateNewKey();
	cancel.nDate = GetTime();
	SignCancelListing(cancel, cancel.vchSig);
	ReceiveCancelListing(cancel);
	cancel.BroadcastToAll();
	LoadSells();
    }
}

void SellsPage::on_rejectButton_clicked()
{
    if (pwalletMain->IsLocked())
    {
        QMessageBox msg;
        msg.setText("Error: Wallet is locked, unable to reject.");
        msg.exec();
        return;
    };

    if (fWalletUnlockStakingOnly)
    {
        QMessageBox msg;
        msg.setText("Error: Wallet unlocked for staking only, unable to reject.");
        msg.exec();
        return;
    };

    QItemSelectionModel* selectionModel = ui->buysTableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string id = ui->buysTableWidget->item(r, 4)->text().toStdString();
    uint256 listingIdHash = uint256(id);
    std::string rid = ui->buysTableWidget->item(r, 5)->text().toStdString();
    uint256 requestIdHash = uint256(rid);

    // ask the user if they really want to reject the buy request
    QMessageBox::StandardButton reply;
      reply = QMessageBox::question(this, "Reject Buy", "Are you sure you want to reject the buy request for this item?",
                                QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) 
    {
	CBuyReject reject;
	reject.listingId = listingIdHash;
	reject.buyRequestId = requestIdHash;
	reject.nDate = GetTime();
	reject.sellerKey = pwalletMain->GenerateNewKey();
	SignBuyReject(reject, reject.vchSig);
	ReceiveBuyReject(reject);
	reject.BroadcastToAll();
	LoadSells();
	LoadBuyRequests();
    }
}

void SellsPage::on_requestPaymentButton_clicked()
{
    if (pwalletMain->IsLocked())
    {
        QMessageBox msg;
        msg.setText("Error: Wallet is locked, unable to request payment.");
        msg.exec();
        return;
    };

    if (fWalletUnlockStakingOnly)
    {
        QMessageBox msg;
        msg.setText("Error: Wallet unlocked for staking only, unable to request payment.");
        msg.exec();
        return;
    };

    QItemSelectionModel* selectionModel = ui->buysTableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string id = ui->buysTableWidget->item(r, 4)->text().toStdString();
    uint256 listingIdHash = uint256(id);
    std::string rid = ui->buysTableWidget->item(r, 5)->text().toStdString();
    uint256 requestIdHash = uint256(rid);

    // ask the user if they really want to accept the buy request
    QMessageBox::StandardButton reply;
      reply = QMessageBox::question(this, "Request Payment", "Are you sure you want to request payment for this item?",
                                QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) 
    {
        CPaymentRequest request;
	request.listingId = listingIdHash;
	request.requestId = requestIdHash;
	request.nDate = GetTime();
	request.sellerKey = pwalletMain->GenerateNewKey();

        CBuyRequest buyRequest = mapBuyRequests[requestIdHash];

	std::string rawTx = PayEscrow(buyRequest.buyerEscrowLockTxHash, buyRequest.sellerEscrowLockTxHash, mapListings[buyRequest.listingId].listing.sellerKey, 2*mapListings[buyRequest.listingId].listing.nPrice);

	request.rawTx = rawTx;	
	LogPrintf("Request Payment: rawtx: %s\n", rawTx);
	SignPaymentRequest(request, request.vchSig);
	ReceivePaymentRequest(request);
	request.BroadcastToAll();
	LoadSells();
	LoadBuyRequests();
    }
}


void SellsPage::on_acceptButton_clicked()
{
    if (pwalletMain->IsLocked())
    {
        QMessageBox msg;
        msg.setText("Error: Wallet is locked, unable to accept buy request.");
        msg.exec();
        return;
    };

    if (fWalletUnlockStakingOnly)
    {
        QMessageBox msg;
        msg.setText("Error: Wallet unlocked for staking only, unable to accept buy request.");
        msg.exec();
        return;
    };

    QItemSelectionModel* selectionModel = ui->buysTableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string id = ui->buysTableWidget->item(r, 4)->text().toStdString();
    uint256 listingIdHash = uint256(id);
    std::string rid = ui->buysTableWidget->item(r, 5)->text().toStdString();
    uint256 requestIdHash = uint256(rid);

    // ask the user if they really want to accept the buy request
    QMessageBox::StandardButton reply;
      reply = QMessageBox::question(this, "Accept Buy", "Are you sure you want to accept the buy request for this item?",
                                QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) 
    {
	CBuyAccept accept;
	accept.listingId = listingIdHash;
	accept.buyRequestId = requestIdHash;
	accept.nDate = GetTime();
	accept.sellerKey = pwalletMain->GenerateNewKey();

        CBuyRequest buyRequest = mapBuyRequests[requestIdHash];

	// create the escrow lock address
	std::string errors;
	std::string escrowAddress;
	bool res = AddMultisigAddress(mapListings[listingIdHash].listing.sellerKey, mapBuyRequests[requestIdHash].buyerKey, escrowAddress, errors);
	accept.escrowAddress = escrowAddress;

	// fund it
        std::string strError = "";
        CWalletTx wtxNew;
        CReserveKey reserveKey(pwalletMain);
        bool result = CreateEscrowLockTx(accept.escrowAddress, mapListings[buyRequest.listingId].listing.nPrice + (0.01 * COIN), strError, wtxNew);
        //pwalletMain->CommitTransaction(wtxNew, reserveKey);
    
	//accept.sellerEscrowLockTxHash = wtxNew.GetHash();

	// serialize the tx to a string
	CDataStream ssTx(SER_NETWORK, CLIENT_VERSION);
	ssTx.reserve(sizeof(wtxNew));
    	ssTx << wtxNew;

	// misuse this parameter like a boss
	accept.raw = ssTx.str();

	SignBuyAccept(accept, accept.vchSig);
	ReceiveBuyAccept(accept);
	accept.BroadcastToAll();
	LoadSells();
	LoadBuyRequests();
    }
}

void SellsPage::on_refundButton_clicked()
{
    if (pwalletMain->IsLocked())
    {
        QMessageBox msg;
        msg.setText("Error: Wallet is locked, unable to refund transaction.");
        msg.exec();
        return;
    };

    if (fWalletUnlockStakingOnly)
    {
        QMessageBox msg;
        msg.setText("Error: Wallet unlocked for staking only, unable to refund transaction.");
        msg.exec();
        return;
    };
    QItemSelectionModel* selectionModel = ui->buysTableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string id = ui->buysTableWidget->item(r, 4)->text().toStdString();
    uint256 listingIdHash = uint256(id);
    std::string rid = ui->buysTableWidget->item(r, 5)->text().toStdString();
    uint256 requestIdHash = uint256(rid);

    // ask the user if they really want to accept the buy request
    QMessageBox::StandardButton reply;
      reply = QMessageBox::question(this, "Refund Buy", "Are you sure you want to refund the buyer for this item?  This will return the security deposit to both buyer and seller.",
                                QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) 
    {
	// Construct a transaction that spends the escrow multisig and sends the security deposit back to
	// the seller as well as the buyer

	CBuyRequest buyRequest = mapBuyRequests[requestIdHash];

    // get the raw tx off of the request
    //std::string rawTx = RefundEscrow(buyRequest.buyerEscrowLockTxHash, buyRequest.sellerEscrowLockTxHash, mapListings[buyRequest.listingId].listing.sellerKey, 2*mapListings[buyRequest.listingId].listing.nValue, buyRequest.buyerKey, strErrors);
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

	LoadSells();
	LoadBuyRequests();
    }
}

void SellsPage::on_copyAddressButton_clicked()
{
    QItemSelectionModel* selectionModel = ui->buysTableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string address = ui->buysTableWidget->item(r, 0)->text().toStdString();
    QApplication::clipboard()->setText(QString::fromStdString(address));
}

void SellsPage::on_cancelEscrowButton_clicked()
{
    if (pwalletMain->IsLocked())
    {
        QMessageBox msg;
        msg.setText("Error: Wallet is locked, unable to cancel transaction.");
        msg.exec();
        return;
    };

    if (fWalletUnlockStakingOnly)
    {
        QMessageBox msg;
        msg.setText("Error: Wallet unlocked for staking only, unable to cancel transaction.");
        msg.exec();
        return;
    };
    QItemSelectionModel* selectionModel = ui->buysTableWidget->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    if(selected.count() == 0)
        return;

    QModelIndex index = selected.at(0);
    int r = index.row();
    std::string id = ui->buysTableWidget->item(r, 4)->text().toStdString();
    uint256 listingIdHash = uint256(id);
    std::string rid = ui->buysTableWidget->item(r, 5)->text().toStdString();
    uint256 requestIdHash = uint256(rid);

    // ask the user if they really want to reject the buy request
    QMessageBox::StandardButton reply;
      reply = QMessageBox::question(this, "Cancel and Reject", "Are you sure you want to cancel the escrow lock and reject the buy request for this item?",
                                QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) 
    {
	CBuyReject reject;
	reject.listingId = listingIdHash;
	reject.buyRequestId = requestIdHash;
	reject.nDate = GetTime();
	reject.sellerKey = pwalletMain->GenerateNewKey();
	SignBuyReject(reject, reject.vchSig);
	ReceiveBuyReject(reject);
	reject.BroadcastToAll();
	LoadSells();
	LoadBuyRequests();
    }
}

void SellsPage::on_listingsTableWidget_itemSelectionChanged()
{
    if(ui->listingsTableWidget->selectedItems().count() > 0)
    {
        ui->cancelButton->setEnabled(true);
    }
}

void SellsPage::on_buysTableWidget_itemSelectionChanged()
{
    ui->acceptButton->setEnabled(false);
    ui->rejectButton->setEnabled(false);
    ui->requestPaymentButton->setEnabled(false);
    ui->refundButton->setEnabled(false);
    ui->copyAddressButton->setEnabled(false);
    ui->cancelEscrowButton->setEnabled(false);

    if(ui->buysTableWidget->selectedItems().count() > 0)
    {
	ui->copyAddressButton->setEnabled(true);
        
        QItemSelectionModel* selectionModel = ui->buysTableWidget->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();
        if(selected.count() == 0)
            return;

        QModelIndex index = selected.at(0);
        int r = index.row();
        std::string status = ui->buysTableWidget->item(r, 1)->text().toStdString();
        if(status == "Refund Requested")
            ui->refundButton->setEnabled(true);
	else if(status == "Buy Requested")
	{
	    ui->acceptButton->setEnabled(true);
	    ui->rejectButton->setEnabled(true);
	}
	else if(status == "Buy Accepted")
	{
	    ui->cancelEscrowButton->setEnabled(true);
	}
	else if(status == "Escrow Locked" || status == "Escrow Lock")
	{
	    ui->requestPaymentButton->setEnabled(true);
	    ui->refundButton->setEnabled(true);
	}
	else if(status == "Escrow Paid" || status == "Payment Requested")
	{
	    ui->requestPaymentButton->setEnabled(false);
	    ui->refundButton->setEnabled(true);
	}
    }
}

void SellsPage::LoadSells()
{
    ui->listingsTableWidget->clearContents();
    ui->listingsTableWidget->setRowCount(0);
    BOOST_FOREACH(PAIRTYPE(const uint256, CSignedMarketListing)& p, mapListings)
    {
        
	CTxDestination dest = p.second.listing.sellerKey.GetID();
        
        if(IsMine(*pwalletMain, dest))
	{
	    updateListing(QString::fromStdString(p.second.GetHash().ToString()), QString::fromStdString(DateTimeStrFormat(p.second.listing.nCreated + (7 * 24 * 60 * 60))), QString::number(p.second.listing.nPrice / COIN, 'f', 8), QString::fromStdString(p.second.listing.sTitle));
	}
    }
}

void SellsPage::LoadBuyRequests(QString q)
{
    ui->buysTableWidget->clearContents();
    ui->buysTableWidget->setRowCount(0);
    qDebug() << "Buy Requests:";
    qDebug() << mapBuyRequests.size();

    BOOST_FOREACH(PAIRTYPE(const uint256, CBuyRequest)& p, mapBuyRequests)
    {
	// if it is a buy request for one of our listings, add it to the table
	if(mapListings.find(p.second.listingId) != mapListings.end())
        {
	    CTxDestination dest = mapListings[p.second.listingId].listing.sellerKey.GetID();
            if(IsMine(*pwalletMain, dest))
	    {
		LogPrintf("Buy Request seller key Is Mine.\n");
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
		QTableWidgetItem *buyerItem = new QTableWidgetItem(QString::fromStdString(CBitcoinAddress(p.second.buyerKey.GetID()).ToString()));
		QTableWidgetItem *itemItem = new QTableWidgetItem(QString::fromStdString(mapListings[p.second.listingId].listing.sTitle));
		QTableWidgetItem *itemIdItem = new QTableWidgetItem(QString::fromStdString(mapListings[p.second.listingId].GetHash().ToString()));
		QTableWidgetItem *requestIdItem = new QTableWidgetItem(QString::fromStdString(p.second.requestId.ToString()));

		ui->buysTableWidget->insertRow(0);
		ui->buysTableWidget->setItem(0, 0, dateItem);
		ui->buysTableWidget->setItem(0, 1, statusItem);
		ui->buysTableWidget->setItem(0, 2, buyerItem);
		ui->buysTableWidget->setItem(0, 3, itemItem);
		ui->buysTableWidget->setItem(0, 4, itemIdItem);
		ui->buysTableWidget->setItem(0, 5, requestIdItem);
	    }
	    else
	    {
		LogPrintf("Buy Request seller key Is NOT Mine.\n");
	    }
        }
	else
	{
	    LogPrintf("Couldn't find listing for Buy Request listing id: %s\n", p.second.listingId.ToString());
	}
    }

}

void SellsPage::updateListing(QString id, QString expires, QString price, QString title)
{
    ui->listingsTableWidget->insertRow(0);
    QTableWidgetItem *idItem = new QTableWidgetItem(id);
    QTableWidgetItem *expiresItem = new QTableWidgetItem(expires);
    QTableWidgetItem *priceItem = new QTableWidgetItem(price);
    QTableWidgetItem *titleItem = new QTableWidgetItem(title);

    ui->listingsTableWidget->setItem(0, 0, idItem);
    ui->listingsTableWidget->setItem(0, 1, expiresItem);
    ui->listingsTableWidget->setItem(0, 2, priceItem);
    ui->listingsTableWidget->setItem(0, 3, titleItem);
}

static void NotifyNewSellerListing(SellsPage *page, CSignedMarketListing listing)
{
    // id, expires, price, title
    QString id = QString::fromStdString(listing.GetHash().ToString());
    QString expires = QString::fromStdString(DateTimeStrFormat(listing.listing.nCreated + (7 * 24 * 60 * 60)));
    QString price = QString::number((listing.listing.nPrice / COIN), 'f', 8);
    QString title = QString::fromStdString(listing.listing.sTitle);

    QMetaObject::invokeMethod(page, "updateListing", Qt::QueuedConnection,
                              Q_ARG(QString, id),
                              Q_ARG(QString, expires),
                              Q_ARG(QString, price),
                              Q_ARG(QString, title)
                              );
}

static void NotifyBuyRequest(SellsPage *page, CBuyRequest buyr)
{
    QMetaObject::invokeMethod(page, "LoadBuyRequests", Qt::QueuedConnection, Q_ARG(QString, ""));
}

static void NotifyPaymentRequest(SellsPage *page)
{
    QMetaObject::invokeMethod(page, "LoadBuyRequests", Qt::QueuedConnection, Q_ARG(QString, ""));
}

void SellsPage::subscribeToCoreSignals()
{
    // Connect signals to core
    uiInterface.NotifyNewSellerListing.connect(boost::bind(&NotifyNewSellerListing, this, _1));
    uiInterface.NotifyBuyRequest.connect(boost::bind(&NotifyBuyRequest, this, _1));
    uiInterface.NotifyPaymentRequest.connect(boost::bind(&NotifyPaymentRequest, this));
    uiInterface.NotifyEscrowRelease.connect(boost::bind(&NotifyPaymentRequest, this));
    uiInterface.NotifyEscrowPayment.connect(boost::bind(&NotifyPaymentRequest, this));
}

void SellsPage::unsubscribeFromCoreSignals()
{
    // Disconnect signals from core
    uiInterface.NotifyNewSellerListing.disconnect(boost::bind(&NotifyNewSellerListing, this, _1));
    uiInterface.NotifyBuyRequest.disconnect(boost::bind(&NotifyBuyRequest, this, _1));
    uiInterface.NotifyPaymentRequest.disconnect(boost::bind(&NotifyPaymentRequest, this));
    uiInterface.NotifyEscrowRelease.disconnect(boost::bind(&NotifyPaymentRequest, this));
    uiInterface.NotifyEscrowPayment.disconnect(boost::bind(&NotifyPaymentRequest, this));
}
