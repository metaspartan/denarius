#include "deliverydetailsdialog.h"
#include "ui_deliverydetailsdialog.h"

#include "walletdb.h"
#include "wallet.h"
#include "ui_interface.h"
#include "util.h"
#include "key.h"
#include "script.h"
#include "init.h"
#include "base58.h"
#include "market.h"

#include <QMessageBox>

DeliveryDetailsDialog::DeliveryDetailsDialog(QWidget *parent, uint256 buyRequestId) :
    QDialog(parent),
    ui(new Ui::DeliveryDetailsDialog)
{
    myBuyRequestId = buyRequestId;
    ui->setupUi(this);

}

DeliveryDetailsDialog::~DeliveryDetailsDialog()
{
    delete ui;
}


void DeliveryDetailsDialog::on_okButton_clicked()
{
    if(ui->plainTextEdit->toPlainText() == "")
    {
	QMessageBox msg;
        msg.setText("Please enter your delivery details.");
	msg.exec();
	return;
    }
    else
    {
	CDeliveryDetails details;
	details.buyRequestId = myBuyRequestId;
	details.listingId = mapBuyRequests[myBuyRequestId].listingId;
	details.nDate = GetTime();
	details.sDetails = ui->plainTextEdit->toPlainText().toStdString();
	details.buyerKey = pwalletMain->GenerateNewKey();
	SignDeliveryDetails(details, details.vchSig);
	ReceiveDeliveryDetails(details);
	details.BroadcastToAll();
        accept();
    }
}

void DeliveryDetailsDialog::on_cancelButton_clicked()
{
    reject();
}

