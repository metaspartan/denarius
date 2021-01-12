#include "marketbrowser.h"
#include "ui_marketbrowser.h"
#include "main.h"
#include "wallet.h"
#include "base58.h"
#include "clientmodel.h"
#include "denariusrpc.h"
#include <QDesktopServices>
#include <curl/curl.h>

#include <sstream>
#include <string>

using namespace json_spirit;

const QString kBaseUrl = "https://denarius.io/dnrusd.php";
const QString kBaseUrl1 = "https://denarius.io/dbitcoin.php";
const QString kBaseUrl2 = "https://denarius.io/dnrmc.php";
const QString kBaseUrl3 = "https://denarius.io/dnrbtc.php";

QString bitcoinp = "";
QString denariusp = "";
QString dnrmcp = "";
QString dnrbtcp = "";
double bitcoin2;
double denarius2;
double dnrmc2;
double dnrbtc2;
QString bitcoing;
QString dnrnewsfeed;
QString dnrmarket;
QString dollarg;
QString eurog;
int mode=1;
int o = 0;


MarketBrowser::MarketBrowser(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MarketBrowser)
{
    ui->setupUi(this);
    setFixedSize(400, 420);


requests();
//QObject::connect(&m_nam, SIGNAL(finished(QNetworkReply*)), this, SLOT(parseNetworkResponse(QNetworkReply*)));
connect(ui->startButton, SIGNAL(pressed()), this, SLOT( requests()));
connect(ui->egal, SIGNAL(pressed()), this, SLOT( update()));

}

void MarketBrowser::update()
{
    QString temps = ui->egals->text();
    double totald = dollarg.toDouble() * temps.toDouble();
    double totaldq = bitcoing.toDouble() * temps.toDouble();
    ui->egald->setText("$ "+QString::number(totald)+" USD or "+QString::number(totaldq)+" BTC");

}

void MarketBrowser::requests()
{
	getRequest1(kBaseUrl);
    getRequest2(kBaseUrl1);
	getRequest3(kBaseUrl2);
	getRequest4(kBaseUrl3);
}

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void MarketBrowser::getRequest1( const QString &urlString )
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
      curl_easy_setopt(curl, CURLOPT_URL, urlString.toStdString().c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
      res = curl_easy_perform(curl);
      if(res != CURLE_OK){
            qWarning("curl_easy_perform() failed: \n");
      }
      curl_easy_cleanup(curl);

    //   std::cout << readBuffer << std::endl;

      //qDebug(readBuffer);
    //   qDebug("D cURL Request: %s", readBuffer.c_str());

        QString denarius = QString::fromStdString(readBuffer);
        denarius2 = (denarius.toDouble());
        denarius = QString::number(denarius2, 'f', 2);

        if(denarius > denariusp)
        {
            ui->denarius->setText("<font color=\"yellow\">$" + denarius + "</font>");
        } else if (denarius < denariusp) {
            ui->denarius->setText("<font color=\"red\">$" + denarius + "</font>");
            } else {
        ui->denarius->setText("$"+denarius+" USD");
        }

        denariusp = denarius;
        dollarg = denarius;
    }
}

void MarketBrowser::getRequest2( const QString &urlString )
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
      curl_easy_setopt(curl, CURLOPT_URL, urlString.toStdString().c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
      res = curl_easy_perform(curl);
      if(res != CURLE_OK){
            qWarning("curl_easy_perform() failed: \n");
      }
      curl_easy_cleanup(curl);

    //   std::cout << readBuffer << std::endl;

      //qDebug(readBuffer);
    //   qDebug("BTC cURL Request: %s", readBuffer.c_str());

        QString bitcoin = QString::fromStdString(readBuffer);
        bitcoin2 = (bitcoin.toDouble());
        bitcoin = QString::number(bitcoin2, 'f', 2);
        if(bitcoin > bitcoinp)
        {
            ui->bitcoin->setText("<font color=\"yellow\">$" + bitcoin + " USD</font>");
        } else if (bitcoin < bitcoinp) {
            ui->bitcoin->setText("<font color=\"red\">$" + bitcoin + " USD</font>");
            } else {
        ui->bitcoin->setText("$"+bitcoin+" USD");
        }

        bitcoinp = bitcoin;
    }
}

void MarketBrowser::getRequest3( const QString &urlString )
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
      curl_easy_setopt(curl, CURLOPT_URL, urlString.toStdString().c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
      res = curl_easy_perform(curl);
      if(res != CURLE_OK){
            qWarning("curl_easy_perform() failed: \n");
      }
      curl_easy_cleanup(curl);

    //   std::cout << readBuffer << std::endl;

      //qDebug(readBuffer);
        // qDebug("D MCap cURL Request: %s", readBuffer.c_str());

        QString dnrmc = QString::fromStdString(readBuffer);
        dnrmc2 = (dnrmc.toDouble());
        dnrmc = QString::number(dnrmc2, 'f', 2);

        if(dnrmc > dnrmcp)
        {
            ui->dnrmc->setText("<font color=\"yellow\">$" + dnrmc + "</font>");
        } else if (dnrmc < dnrmcp) {
            ui->dnrmc->setText("<font color=\"red\">$" + dnrmc + "</font>");
            } else {
        ui->dnrmc->setText("$"+dnrmc+" USD");
        }

        dnrmcp = dnrmc;
        dnrmarket = dnrmc;
    }
}

void MarketBrowser::getRequest4( const QString &urlString )
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, urlString.toStdString().c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        res = curl_easy_perform(curl);
        if(res != CURLE_OK){
            qWarning("curl_easy_perform() failed: \n");
        }
        curl_easy_cleanup(curl);

        // std::cout << readBuffer << std::endl;

        //qDebug(readBuffer);
        // qDebug("D/BTC cURL Request: %s", readBuffer.c_str());


        QString dnrbtc = QString::fromStdString(readBuffer);
        dnrbtc2 = (dnrbtc.toDouble());
        dnrbtc = QString::number(dnrbtc2, 'f', 8);

        if(dnrbtc > dnrbtcp)
        {
            ui->dnrbtc->setText("<font color=\"yellow\">" + dnrbtc + " BTC</font>");
        } else if (dnrbtc < dnrbtcp) {
            ui->dnrbtc->setText("<font color=\"red\">" + dnrbtc + " BTC</font>");
            } else {
        ui->dnrbtc->setText(dnrbtc+" BTC");
        }

        dnrbtcp = dnrbtc;
        bitcoing = dnrbtc;
    }
}

void MarketBrowser::setModel(ClientModel *model)
{
    this->model = model;
}

MarketBrowser::~MarketBrowser()
{
    delete ui;
}
