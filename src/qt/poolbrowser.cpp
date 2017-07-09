#include "poolbrowser.h"
#include "ui_poolbrowser.h"
#include "main.h"
#include "wallet.h"
#include "base58.h"
#include "clientmodel.h"
#include "bitcoinrpc.h"
#include <QDesktopServices>

#include <sstream>
#include <string>

using namespace json_spirit;

const QString kBaseUrl = "https://api.coinmarketcap.com/v1/ticker/denarius-dnr/";
const QString kBaseUrl1 = "http://blockchain.info/tobtc?currency=USD&value=1";

QString bitcoinp = "";
double bitcoin2;
double lastuG;
QString bitcoing;
QString dollarg;
int mode=1;
int o = 0;
QString lastp = "";
QString lastpp = "";
QString lastpp2 = "";
QString lastpp3 = "";
QString askp = "";
QString bidp = "";
QString highp = "";
QString lowp = "";
QString volumebp = "";
QString volumesp = "";
QString bop = "";
QString sop = "";
QString lastp2 = "";
QString askp2 = "";
QString bidp2 = "";
QString highp2 = "";
QString lowp2 = "";
QString yestp = "";
QString yestp2 = "";
QString volumebp2 = "";
QString volumesp2 = "";
QStringList marketdbmint;

QString lastp3 = "";
QString volumebp3 = "";
double volumesp3;

PoolBrowser::PoolBrowser(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PoolBrowser)
{
    ui->setupUi(this);
    setFixedSize(400, 420);
    ui->customPlot->addGraph();
    ui->customPlot->setBackground(QBrush(QColor("#edf1f7")));
    ui->customPlot2->addGraph();
    ui->customPlot2->addGraph();
    ui->customPlot2->setBackground(QBrush(QColor("#edf1f7")));
    ui->customPlot_2->addGraph();
    ui->customPlot_2->setBackground(QBrush(QColor("#edf1f7")));
    ui->customPlot2_2->addGraph();
    ui->customPlot2_2->addGraph();
    ui->customPlot2_2->setBackground(QBrush(QColor("#edf1f7")));
    ui->customPlot_3->addGraph();
    ui->customPlot_3->setBackground(QBrush(QColor("#edf1f7")));
    ui->customPlot2_3->addGraph();
    ui->customPlot2_3->addGraph();
    ui->customPlot2_3->setBackground(QBrush(QColor("#edf1f7")));


randomChuckNorrisJoke();
QObject::connect(&m_nam, SIGNAL(finished(QNetworkReply*)), this, SLOT(parseNetworkResponse(QNetworkReply*)));
connect(ui->startButton, SIGNAL(pressed()), this, SLOT( randomChuckNorrisJoke()));
connect(ui->egal, SIGNAL(pressed()), this, SLOT( egaldo()));

}

void PoolBrowser::egaldo()
{
    QString temps = ui->egals->text();
    double totald = lastuG * temps.toDouble();
    double totaldq = bitcoing.toDouble() * temps.toDouble();
    ui->egald->setText(QString::number(totald) + " $ / "+QString::number(totaldq)+" BTC");

}


void PoolBrowser::coinex()
{
    QDesktopServices::openUrl(QUrl("https://www.coinexchange.io/market/DNR/BTC"));
}

void PoolBrowser::poloniex()
{
    QDesktopServices::openUrl(QUrl("https://poloniex.com/exchange/btc_dnr"));
}

void PoolBrowser::randomChuckNorrisJoke()
{
    ui->Ok->setVisible(true);
    getRequest(kBaseUrl1);
}

void PoolBrowser::getRequest( const QString &urlString )
{
    QUrl url ( urlString );
    QNetworkRequest req ( url );
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8");
    m_nam.get(req);
}

void PoolBrowser::parseNetworkResponse(QNetworkReply *finished )
{

        QUrl what = finished->url();

    if ( finished->error() != QNetworkReply::NoError )
    {
        // A communication error has occurred
        emit networkError( finished->error() );
        return;
    }

if (what == kBaseUrl1) //bitcoinprice
{

    // QNetworkReply is a QIODevice. So we read from it just like it was a file
    QString bitcoin = finished->readAll();
    bitcoin2 = (1 / bitcoin.toDouble());
    bitcoin = QString::number(bitcoin2);
    if(bitcoin > bitcoinp)
    {
        ui->bitcoin->setText("<font color=\"green\">" + bitcoin + " $</font>");
    } else if (bitcoin < bitcoinp) {
        ui->bitcoin->setText("<font color=\"red\">" + bitcoin + " $</font>");
        } else {
    ui->bitcoin->setText(bitcoin + " $");
    }

    bitcoinp = bitcoin;
}

finished->deleteLater();
}


void PoolBrowser::setModel(ClientModel *model)
{
    this->model = model;
}

PoolBrowser::~PoolBrowser()
{
    delete ui;
}
