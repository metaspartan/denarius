#include "createmarketlistingdialog.h"
#include "ui_createmarketlistingdialog.h"

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

CreateMarketListingDialog::CreateMarketListingDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CreateMarketListingDialog)
{
    ui->setupUi(this);

}

CreateMarketListingDialog::~CreateMarketListingDialog()
{
    delete ui;
}


void CreateMarketListingDialog::on_okButton_clicked()
{
    if(ui->categoryLineEdit->text() == "")
    {
	QMessageBox msg;
        msg.setText("Please enter a category.");
	msg.exec();
	return;
    }
    if(ui->titleLineEdit->text() == "")
    {
	QMessageBox msg;
        msg.setText("Please enter a title.");
	msg.exec();
	return;
    }
    else if(ui->descriptionTextEdit->toPlainText() == "")
    {
	QMessageBox msg;
        msg.setText("Please enter a description.");
	msg.exec();
	return;
    }
    else if(ui->priceSpinBox->value() == 0)
    {
	QMessageBox msg;
        msg.setText("Please enter a price.");
	msg.exec();
	return;
    } else {
	// create market listing object
	CMarketListing listing;
	listing.sCategory = ui->categoryLineEdit->text().toStdString();
	listing.sTitle = ui->titleLineEdit->text().toStdString();
	listing.sDescription = ui->descriptionTextEdit->toPlainText().toStdString();
	listing.sImageOneUrl = ui->imageOneLineEdit->text().toStdString();
	listing.sImageTwoUrl = ui->imageTwoLineEdit->text().toStdString();
	listing.nPrice = ui->priceSpinBox->value() * COIN;
	listing.nCreated = GetTime();
	listing.sellerKey = pwalletMain->GenerateNewKey();

	CSignedMarketListing signedListing;
	signedListing.listing = listing;
	SignListing(listing, signedListing.vchListingSig);
	signedListing.BroadcastToAll();
	ReceiveListing(signedListing);
	
	// notify ui
	uiInterface.NotifyNewSellerListing(signedListing);

        accept();
    }
}

void CreateMarketListingDialog::on_cancelButton_clicked()
{
    reject();
}

