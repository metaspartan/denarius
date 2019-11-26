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
    fileCont = "";
    ui->checkButton->setHidden(true);
    ui->checkLabel->setHidden(true);
}

Jupiter::~Jupiter()
{
    delete ui;
}

void Jupiter::on_filePushButton_clicked()
{  
  //Upload a file
	fileName = QFileDialog::getOpenFileName(this,
    tr("Upload File to IPFS"), "./", tr("All Files (*.*)"));

  //fileCont = QFileDialog::getOpenFileContent("All Files (*.*)",  fileContentReady);

  ui->labelFile->setText(fileName);
}

void Jupiter::on_createPushButton_clicked()
{

#ifdef USE_IPFS
    try {
        std::stringstream contents;
        ipfs::Json add_result;

        //Ensure IPFS connected
        ipfs::Client client("ipfs.infura.io:5001");

        if(fileName == "")
        {
          noImageSelected();   
          return;
        }

        //read whole file
        std::ifstream ipfsFile;
        std::string filename = fileName.toStdString().c_str();
        // Remove directory if present.
        // Do this before extension removal incase directory has a period character.
        const size_t last_slash_idx = filename.find_last_of("\\/");
        if (std::string::npos != last_slash_idx)
        {
            filename.erase(0, last_slash_idx + 1);
        }
        
        // Remove extension if present.
        /*
        const size_t period_idx = filename.rfind('.');
        if (std::string::npos != period_idx)
        {
            filename.erase(period_idx);
        }
        */

        ipfsFile.open(fileName.toStdString().c_str(), std::ios::binary);
        std::vector<char> ipfsContents((std::istreambuf_iterator<char>(ipfsFile)), std::istreambuf_iterator<char>());

        std::string ipfsC(ipfsContents.begin(), ipfsContents.end());

        std::string fileContents = ipfsC.c_str();

        printf("Jupiter Upload File Start: %s\n", filename.c_str());
        //printf("Jupiter File Contents: %s\n", ipfsC.c_str());

        client.FilesAdd(
        {{filename.c_str(), ipfs::http::FileUpload::Type::kFileName, fileName.toStdString().c_str()}},
        &add_result);
        
        const std::string& hash = add_result[0]["hash"];

        ui->lineEdit->setText(QString::fromStdString(hash));

        std::string r = add_result.dump();
        printf("Jupiter Successfully Added IPFS File(s): %s\n", r.c_str());

        if (hash != "") {
          ui->checkButton->setHidden(false);
          ui->checkLabel->setHidden(false);
        }

    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl; //302 error on large files: passing null and throwing exception
        QMessageBox errbox;
        errbox.setText(QString::fromStdString(e.what()));
        errbox.exec();
    }
#endif

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
    if(fileName == "")
    {
      noImageSelected();   
      return;
    }

    //go to public IPFS gateway
    std::string linkurl = "https://ipfs.infura.io/ipfs/";
    //open url
    QString link = QString::fromStdString(linkurl + ui->lineEdit->text().toStdString());
    QDesktopServices::openUrl(QUrl(link));
    
}

void Jupiter::on_checkTxButton_clicked()
{

}

void Jupiter::noImageSelected()
{
  //err message
  QMessageBox errorbox;
  errorbox.setText("No file selected!");
  errorbox.exec();
}
