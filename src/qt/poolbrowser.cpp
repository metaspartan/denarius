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

const QString kBaseUrl = "https://www.coinexchange.io/api/v1/getmarketsummaries";
const QString kBaseUrl1 = "http://blockchain.info/tobtc?currency=USD&value=1";
const QString kBaseUrl2 = "http://bittrex.com/api/v1/public/getorderbook?market=BTC-DNR&type=both&depth=50";
const QString kBaseUrl3 = "http://bittrex.com/api/v1/public/getmarkethistory?market=BTC-DNR&count=100";

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


void PoolBrowser::bittrex()
{
    QDesktopServices::openUrl(QUrl("https://www.coinexchange.io/market/DNR/BTC"));
}

void PoolBrowser::overv()
{
    double yes = (yestp.toDouble()+yestp2.toDouble())/2;
    double average2 = (lastp.toDouble()+lastp2.toDouble()+lastp3.toDouble())/3;
    QString average3 = QString::number((lastp.toDouble()-average2)/(average2/100),'g',2);
    QString average4 = QString::number((lastp2.toDouble()-average2)/(average2/100),'g',2);
    QString average5 = QString::number((lastp3.toDouble()-average2)/(average2/100),'g',2);


    if(average3.toDouble() > 0)
    {
        ui->diff1->setText("<font color=\"green\">+" + average3 + " %</font>");
    } else {
        ui->diff1->setText("<font color=\"red\">" + average3 + " %</font>");
        }
    if(average4.toDouble() > 0)
    {
        ui->diff2->setText("<font color=\"green\">+" + average4 + " %</font>");
    } else {
        ui->diff2->setText("<font color=\"red\">" + average4 + " %</font>");
        }
    if(average5.toDouble() > 0)
    {
        ui->diff3->setText("<font color=\"green\">+" + average5 + " %</font>");
    } else {
        ui->diff3->setText("<font color=\"red\">" + average5 + " %</font>");
        }
    if((yestp.toDouble()+yestp2.toDouble()) > 0)
    {
        ui->yest_3->setText("<font color=\"green\">+" + QString::number(yes) + " %</font>");
    } else {
        ui->yest_3->setText("<font color=\"red\">" + QString::number(yes) + " %</font>");
        }


    if(lastp > lastpp)
    {
        ui->last_4->setText("<font color=\"green\">" + lastp + "</font>");
    } else if (lastp < lastp) {
        ui->last_4->setText("<font color=\"red\">" + lastp + "</font>");
        } else {
    ui->last_4->setText(lastp);
    }

    if(lastp2 > lastpp2)
    {
        ui->last_5->setText("<font color=\"green\">" + lastp2 + "</font>");
    } else if (lastp2 < lastpp2) {
        ui->last_5->setText("<font color=\"red\">" + lastp2 + "</font>");
        } else {
    ui->last_5->setText(lastp2);
    }

    if(lastp3 > lastpp3)
    {
        ui->last_6->setText("<font color=\"green\">" + lastp3 + "</font>");
    } else if (lastp3 < lastpp3) {
        ui->last_6->setText("<font color=\"red\">" + lastp3 + "</font>");
        } else {
    ui->last_6->setText(lastp3);
    }

    lastpp = lastp;
    lastpp2 = lastp2;
    lastpp3 = lastp3;

}

void PoolBrowser::poloniex()
{
    QDesktopServices::openUrl(QUrl("https://poloniex.com/exchange/btc_dnr"));
}

void PoolBrowser::randomChuckNorrisJoke()
{
    ui->Ok->setVisible(true);
    getRequest(kBaseUrl);
    getRequest(kBaseUrl1);
    getRequest(kBaseUrl2);
    getRequest(kBaseUrl3);
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

if (what == kBaseUrl) //Bittrexdata
{
    double asku;
    QString askus;
    double lastu;
    QString lastus;
    double bidu;
    QString bidus;
    double volumeu;
    QString volumeus;
    double yestu;
    QString yestus;

    // QNetworkReply is a QIODevice. So we read from it just like it was a file
    QString data = finished->readAll();
    QStringList data2 = data.split("{\"MarketName\":\"BTC-DNR\",\"High\":");
    QStringList high = data2[1].split(",\"Low\":"); // high = high
    QStringList low = high[1].split(",\"Volume\":");
    QStringList volume = low[1].split(",\"Last\":");
    QStringList last = volume[1].split(",\"BaseVolume\":");
    QStringList basevolume = last[1].split(",\"TimeStamp\":\"");
    QStringList time = basevolume[1].split("\",\"Bid\":");
    QStringList bid = time[1].split(",\"Ask\":");
    QStringList ask = bid[1].split(",\"OpenBuyOrders\":");
    QStringList openbuy = ask[1].split(",\"OpenSellOrders\":");
    QStringList opensell = openbuy[1].split(",\"PrevDay\":");
    QStringList yest = opensell[1].split(",\"Created\":");

   // 0.00000978,"Low":0.00000214,"Volume":3718261.74455189,"Last":0.00000558,
   //"BaseVolume":22.42443460,"TimeStamp":"2014-05-13T10:08:06.553","Bid":0.00000558,"Ask":0.00000559,"OpenBuyOrders":42,"OpenSellOrders":42,"PrevDay":0.00000861}
    lastu = last[0].toDouble() * bitcoin2;
    lastuG = lastu;
    lastus = QString::number(lastu);
    dollarg = lastus;
    bitcoing = last[0];

    if(last[0] > lastp)
    {
        ui->last->setText("<font color=\"green\">" + last[0] + "</font>");
        ui->lastu->setText("<font color=\"green\">" + lastus + " $</font>");
    } else if (last[0] < lastp) {
        ui->last->setText("<font color=\"red\">" + last[0] + "</font>");
         ui->lastu->setText("<font color=\"red\">" + lastus + " $</font>");
        } else {
    ui->last->setText(last[0]);
    ui->lastu->setText(lastus + " $");
    }

    asku = ask[0].toDouble() * bitcoin2;
    askus = QString::number(asku);

    if(ask[0] > askp)
    {
        ui->ask->setText("<font color=\"green\">" + ask[0] + "</font>");
        ui->asku->setText("<font color=\"green\">" + askus + " $</font>");
    } else if (ask[0] < askp) {
        ui->ask->setText("<font color=\"red\">" + ask[0] + "</font>");
        ui->asku->setText("<font color=\"red\">" + askus + " $</font>");
        } else {
    ui->ask->setText(ask[0]);
    ui->asku->setText(askus + " $");
    }

    bidu = bid[0].toDouble() * bitcoin2;
    bidus = QString::number(bidu);


    if(bid[0] > bidp)
    {
        ui->bid->setText("<font color=\"green\">" + bid[0] + "</font>");
        ui->bidu->setText("<font color=\"green\">" + bidus + " $</font>");
    } else if (bid[0] < bidp) {
        ui->bid->setText("<font color=\"red\">" + bid[0] + "</font>");
        ui->bidu->setText("<font color=\"red\">" + bidus + " $</font>");
        } else {
    ui->bid->setText(bid[0]);
    ui->bidu->setText(bidus + " $");
    }
    if(high[0] > highp)
    {
        ui->high->setText("<font color=\"green\">" + high[0] + "</font>");
    } else if (high[0] < highp) {
        ui->high->setText("<font color=\"red\">" + high[0] + "</font>");
        } else {
    ui->high->setText(high[0]);
    }
    if(low[0] > lowp)
    {
        ui->low->setText("<font color=\"green\">" + low[0] + "</font>");
    } else if (low[0] < lowp) {
        ui->low->setText("<font color=\"red\">" + low[0] + "</font>");
        } else {
    ui->low->setText(low[0]);
    }


    if(volume[0] > volumebp)
    {
        ui->volumeb->setText("<font color=\"green\">" + volume[0] + "</font>");

    } else if (volume[0] < volumebp) {
        ui->volumeb->setText("<font color=\"red\">" + volume[0] + "</font>");
        ui->volumeu->setText("<font color=\"red\">" + volumeus + " $</font>");
        } else {
    ui->volumeb->setText(volume[0]);
    ui->volumeu->setText(volumeus + " $");
    }

    volumeu = basevolume[0].toDouble() * bitcoin2;
    volumeus = QString::number(volumeu);

    if(basevolume[0] > volumesp)
    {
        ui->volumes->setText("<font color=\"green\">" + basevolume[0] + "</font>");
        ui->volumeu->setText("<font color=\"green\">" + volumeus + " $</font>");
    } else if (basevolume[0] < volumesp) {
        ui->volumes->setText("<font color=\"red\">" + basevolume[0] + "</font>");
        ui->volumeu->setText("<font color=\"red\">" + volumeus + " $</font>");
        } else {
    ui->volumes->setText(basevolume[0]);
    ui->volumeu->setText(volumeus + " $");
    }

    if(last[0].toDouble() > yest[0].toDouble())
    {
        yestu = ((last[0].toDouble() - yest[0].toDouble())/last[0].toDouble())*100;
        yestus = QString::number(yestu);

        ui->yest->setText("<font color=\"green\"> + " + yestus + " %</font>");


    }else
    {
        yestu = ((yest[0].toDouble() - last[0].toDouble())/yest[0].toDouble())*100;
        yestus = QString::number(yestu);
        ui->yest->setText("<font color=\"red\"> - " + yestus + " %</font>");

    }


    lastp = last[0];
    askp = ask[0];
    bidp = bid[0];
    highp = high[0];
    lowp = low[0];
    volumebp = volume[0];
    volumesp = basevolume[0];
    bop = openbuy[0];
    sop = opensell[0];
    if(last[0].toDouble() > yest[0].toDouble()) yestp = yestus;
    if(last[0].toDouble() < yest[0].toDouble()) yestp = yestus.prepend("-");
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
if (what == kBaseUrl2) //Bcurrent orders
{
    // QNetworkReply is a QIODevice. So we read from it just like it was a file
    QString marketd = finished->readAll();
    marketd = marketd.replace("{","").replace("}","").replace("\"","").replace("],\"sell\":","").replace(" ","").replace("]","").replace("Quantity:","").replace("Rate:","");
    QStringList marketd2 = marketd.split("["); // marketd2[1] => buy order marketd2[2] => sell
    QStringList marketdb = marketd2[1].split(",");
    QStringList marketds = marketd2[2].split(",");
    int mat = 50;
    if (marketdb.length() > marketds.length()) mat = marketds.length();
    if (marketds.length() > marketdb.length()) mat = marketdb.length();
    int z = 0;
    double highh = 0;
    double loww = 100000;
    double cumul = 0;
    double cumul2 = 0;
    double cumult = 0;
    QVector<double> x(50), y(50);
    QVector<double> x2(50), y2(50);
    ui->sellquan->clear();
    ui->buyquan->clear();
     ui->sellquan->sortByColumn(0, Qt::AscendingOrder);
      ui->sellquan->setSortingEnabled(true);
       ui->buyquan->setSortingEnabled(true);


       for (int i = 0; i < mat-1; i++) {

           QTreeWidgetItem * item = new QTreeWidgetItem();
           item->setText(0,marketdb[i+1]);
           item->setText(1,marketdb[i]);
           ui->buyquan->addTopLevelItem(item);

           QTreeWidgetItem * item2 = new QTreeWidgetItem();
           item2->setText(0,marketds[i+1]);
           item2->setText(1,marketds[i]);
           ui->sellquan->addTopLevelItem(item2);

           if (marketds[i+1].toDouble()*100000000 > highh) highh = marketds[i+1].toDouble()*100000000;
           if (marketdb[i+1].toDouble()*100000000 < loww) loww = marketdb[i+1].toDouble()*100000000;


           x[z] = marketdb[i+1].toDouble()*100000000;
           y[z] = cumul;
           x2[z] = marketds[i+1].toDouble()*100000000;
           y2[z] = cumul2;

           cumul = marketdb[i].toDouble() + cumul;
           cumul2 = marketds[i].toDouble() + cumul2;

           i = i + 1;
           z = z + 1;

       }
       if (cumul > cumul2 ) cumult=cumul;
       if (cumul < cumul2 ) cumult=cumul2;



          // create graph and assign data to it:

          ui->customPlot2->graph(0)->setData(x, y);
          ui->customPlot2->graph(1)->setData(x2, y2);

          ui->customPlot2->graph(0)->setPen(QPen(QColor(34, 177, 76)));
          ui->customPlot2->graph(0)->setBrush(QBrush(QColor(34, 177, 76, 20)));
          ui->customPlot2->graph(1)->setPen(QPen(QColor(237, 24, 35)));
          ui->customPlot2->graph(1)->setBrush(QBrush(QColor(237, 24, 35, 20)));

          // set axes ranges, so we see all data:
          ui->customPlot2->xAxis->setRange(loww, highh);
          ui->customPlot2->yAxis->setRange(loww, cumult);

         ui->customPlot2->replot();



}

if (what == kBaseUrl3) //Bpast orders
{
    // QNetworkReply is a QIODevice. So we read from it just like it was a file
    QString marketd = finished->readAll();
    marketd = marketd.replace("{\"success\":true,\"message\":\"\",\"result\":[","");
    marketd = marketd.replace(",\"FillType\":\"FILL\"","");
    marketd = marketd.replace(",\"FillType\":\"PARTIAL_FILL\"","");
    marketd = marketd.replace("\"","");
    marketd = marketd.replace("Id:","");
    marketd = marketd.replace("TimeStamp:","");
    marketd = marketd.replace("Quantity:","");
    marketd = marketd.replace("Price:","");
    marketd = marketd.replace("Total:","");
    marketd = marketd.replace("Total:","");
    marketd = marketd.replace("OrderType:","");

    QStringList marketdb = marketd.split("},{");

    int z = 0;
    ui->trades->clear();
    ui->trades->setColumnWidth(0,  55);
        ui->trades->setColumnWidth(1,  110);
        ui->trades->setColumnWidth(2,  90);
        ui->trades->setColumnWidth(3,  90);
        ui->trades->setColumnWidth(4,  160);
     QVector<double> x(100), y(100);
      ui->trades->setSortingEnabled(false);
double highh = 0;
double loww = 100000;
    for (int i = 0; i < marketdb.length(); i++) {

        marketdb[i] = marketdb[i].replace("}","");
        marketdb[i] = marketdb[i].replace("{","");
        QStringList dad = marketdb[i].split(",");

        QTreeWidgetItem * item = new QTreeWidgetItem();
        item->setText(0,dad[5]);
        item->setText(1,dad[2]);
        item->setText(2,dad[3]);
        item->setText(3,dad[4]);
        QStringList temp = dad[1].replace("T"," ").split(".");
        item->setText(4,temp[0]);

        ui->trades->addTopLevelItem(item);

        x[z] = (marketdb.length())-z;
        y[z] = (dad[3].toDouble())*100000000;

        if (dad[3].toDouble()*100000000 > highh) highh = dad[3].toDouble()*100000000;
        if (dad[3].toDouble()*100000000 < loww) loww = dad[3].toDouble()*100000000;

        z = z + 1;
    }


       // create graph and assign data to it:

       ui->customPlot->graph(0)->setData(x, y);
       ui->customPlot->graph(0)->setPen(QPen(QColor(34, 177, 76)));
       ui->customPlot->graph(0)->setBrush(QBrush(QColor(34, 177, 76, 20)));
       // set axes ranges, so we see all data:
      ui->customPlot->xAxis->setRange(1, marketdb.length());
      ui->customPlot->yAxis->setRange(loww, highh);
      ui->customPlot->replot();

}

if (lastp != 0 && lastp2 != 0 && lastp3 != 0) ui->Ok->setVisible(false);
if (o == 0)
{
	// function()
    o = 1;
    overv();
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
