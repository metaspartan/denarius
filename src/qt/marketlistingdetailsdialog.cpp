#include "marketlistingdetailsdialog.h"
#include "ui_marketlistingdetailsdialog.h"

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
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QPixmap>
#include <QImage>

MarketListingDetailsDialog::MarketListingDetailsDialog(QWidget *parent, uint256 listingId) :
    QDialog(parent),
    ui(new Ui::MarketListingDetailsDialog)
{
    ui->setupUi(this);
    manager = new QNetworkAccessManager(this);

    CSignedMarketListing listing = mapListings[listingId];
    setWindowTitle(QString::fromStdString(listing.listing.sTitle));
    ui->titleLabel->setText(QString::fromStdString(listing.listing.sTitle));
    ui->descriptionLabel->setText(QString::fromStdString(listing.listing.sDescription));
    ui->untilLabel->setText(QString::fromStdString(DateTimeStrFormat(listing.listing.nCreated + (7 * 24 * 60 * 60))));
    ui->priceLabel->setText(QString::number(listing.listing.nPrice / COIN, 'f', 8));
    ui->sellerAddressLabel->setText(QString::fromStdString(CBitcoinAddress(listing.listing.sellerKey.GetID()).ToString()));

    if(listing.listing.sImageOneUrl != "")
    {
        QUrl url(QString::fromStdString(listing.listing.sImageOneUrl));
        QNetworkRequest request;
        request.setUrl(url);
        QNetworkReply* currentReply = manager->get(request);
        connect(currentReply, SIGNAL(finished()), this, SLOT(getImgOneReplyFinished()));
    } else if (listing.listing.sImageOneUrl != "null") {

    }

    if(listing.listing.sImageTwoUrl != "")
    {
        QUrl url(QString::fromStdString(listing.listing.sImageTwoUrl));
        QNetworkRequest request;
        request.setUrl(url);
        QNetworkReply* currentReply = manager->get(request);
        connect(currentReply, SIGNAL(finished()), this, SLOT(getImgTwoReplyFinished()));
    } else if (listing.listing.sImageOneUrl != "null") {

    }
}

MarketListingDetailsDialog::~MarketListingDetailsDialog()
{
    delete ui;
}


void MarketListingDetailsDialog::on_okButton_clicked()
{
    accept();
}

void MarketListingDetailsDialog::getImgOneReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray bytes(reply->readAll());

    QImage image = QImage::fromData(bytes, "PNG");

    ui->imageOneLabel->setPixmap(QPixmap::fromImage(image));
    ui->imageOneLabel->setScaledContents(true);
}

void MarketListingDetailsDialog::getImgTwoReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray bytes(reply->readAll());

    QImage image = QImage::fromData(bytes, "PNG");

    ui->imageTwoLabel->setPixmap(QPixmap::fromImage(image));
    ui->imageTwoLabel->setScaledContents(true);
}


