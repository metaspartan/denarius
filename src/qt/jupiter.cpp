#include "jupiter.h"
#include "ui_jupiter.h"

#include "bitcoinunits.h"
#include "guiutil.h"
#include "guiconstants.h"

#include "hash.h"
#include "base58.h"
#include "key.h"
#include "util.h"
#include "init.h"
#include "wallet.h"
#include "walletdb.h"

#ifdef USE_IPFS
#include <ipfs/client.h>
#include <ipfs/http/transport.h>
#endif

#include <QScrollArea>
#include <QUrl>
#include <QFileDialog>
#include <QDesktopServices>

#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

Jupiter::Jupiter(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Jupiter)
{
    ui->setupUi(this);
    fileName = "";    
}

Jupiter::~Jupiter()
{
    delete ui;
}

void Jupiter::on_filePushButton_clicked()
{
  /*
	fileName = QFileDialog::getOpenFileName(this,
    tr("Open File"), "./", tr("All Files (*.*)"));

    ui->labelFile->setText(fileName);
*/
}

void Jupiter::on_createPushButton_clicked()
{
  /*
#ifdef USE_IPFS
    try {
        std::stringstream contents;
        ipfs::Json add_result;

        ipfs::Client client("ipfs.infura.io:5001");

        client.FilesAdd(
            {{"D", ipfs::http::FileUpload::Type::kFileContents, "Testing"},
            {"Denarius", ipfs::http::FileUpload::Type::kFileName, "../testfile.json"}},
            &add_result);

        std::string r = add_result.dump();
        printf("IPFS File(s) Added: %s\n", r.c_str());

    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
#endif
*/
  /*
    if(fileName == "")
    {
  noImageSelected();   
  return;
    }

    //read whole file
    std::ifstream imageFile;
    imageFile.open(fileName.toStdString().c_str(), std::ios::binary);
std::vector<char> imageContents((std::istreambuf_iterator<char>(imageFile)),
                               std::istreambuf_iterator<char>());

    //hash file
    uint256 imagehash = SerializeHash(imageContents);
    CKeyID keyid(Hash160(imagehash.begin(), imagehash.end()));
    CBitcoinAddress baddr = CBitcoinAddress(keyid);
    std::string addr = baddr.ToString();

    ui->lineEdit->setText(QString::fromStdString(addr));

    CAmount nAmount = 0.001 * COIN; // 0.001 D Fee
    
    // Wallet comments
    CWalletTx wtx;
    wtx.mapValue["comment"] = fileName.toStdString();
	std::string sNarr = ui->edit->text().toStdString();
    wtx.mapValue["to"]      = "Proof of Data";
    
    if (pwalletMain->IsLocked())
    {
	  QMessageBox unlockbox;
	  unlockbox.setText("Error, Your wallet is locked! Please unlock your wallet!");
	  unlockbox.exec();
      ui->txLineEdit->setText("ERROR: Your wallet is locked! Cannot send proof of data. Unlock your wallet!");
    }
    else if(pwalletMain->GetBalance() < 0.001)
    {
	  QMessageBox error2box;
	  error2box.setText("Error, You need at least 0.001 D to send proof of data!");
	  error2box.exec();
      ui->txLineEdit->setText("ERROR: You need at least a 0.001 D balance to send proof of data.");
    }
    else
    {
      //std::string sNarr;
      std::string strError = pwalletMain->SendMoneyToDestination(baddr.Get(), nAmount, sNarr, wtx);

     if(strError != "")
     {
        QMessageBox infobox;
        infobox.setText(QString::fromStdString(strError));
        infobox.exec();
     }
		QMessageBox successbox;
		successbox.setText("Proof of Data, Successful!");
		successbox.exec();
        ui->txLineEdit->setText(QString::fromStdString(wtx.GetHash().GetHex()));
     }
    */

}

void Jupiter::on_checkButton_clicked()
{
  /*
    if(fileName == "")
    {
  noImageSelected();   
  return;
    }

//read whole file
    std::ifstream imageFile;
    imageFile.open(fileName.toStdString().c_str(), std::ios::binary);
std::vector<char> imageContents((std::istreambuf_iterator<char>(imageFile)),
                               std::istreambuf_iterator<char>());

    //hash file
    uint256 imagehash = SerializeHash(imageContents);
    CKeyID keyid(Hash160(imagehash.begin(), imagehash.end()));
    std::string addr = CBitcoinAddress(keyid).ToString();

    //go to block explorer
    std::string bexp = "https://www.coinexplorer.net/D/address/";
    //open url
    QString link = QString::fromStdString(bexp + addr);
    QDesktopServices::openUrl(QUrl(link));
    */
}

void Jupiter::on_checkTxButton_clicked()
{
  //go to block explorer
    //std::string bexp = "https://www.coinexplorer.net/D/transaction/";
    //open url
    //QString link = QString::fromStdString(bexp + ui->txLineEdit->text().toStdString());
    //QDesktopServices::openUrl(QUrl(link));
}

void Jupiter::noImageSelected()
{
  //err message
  QMessageBox errorbox;
  errorbox.setText("No file selected!");
  errorbox.exec();
}
