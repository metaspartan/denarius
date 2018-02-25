#include "richlist.h"
#include "ui_richlist.h"

#include "guiutil.h"
#include "guiconstants.h"
#include "init.h"
#include "util.h"
#include "richlistdata.h"
#include "richlistdb.h"
#include "qcustomplot.h"

QVector<double> walletTicks(0), walletBalances(0);

RichListPage::RichListPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::RichListPage)
{
    ui->setupUi(this);
    ui->lastUpdatedLabel->setText("Click Show or Update to Load");
}

RichListPage::~RichListPage()
{
    delete ui;
}

void RichListPage::on_showButton_clicked()
{
    ShowRichList();
    DisplayCharts();
}

void RichListPage::on_refreshButton_clicked()
{
    UpdateRichList();
    ShowRichList();
    DisplayCharts();
}

void RichListPage::ShowRichList()
{
    CRichListData richListData;
    bool fResult = LoadRichList(richListData);
    if(fResult)
    {
	std::set<CRichListDataItem> setRichList;
	BOOST_FOREACH(const PAIRTYPE(std::string, CRichListDataItem)& p, richListData.mapRichList)
        {
	    setRichList.insert(p.second);
        }

	int n = 0;
        int nMaxCount = 100;
	ui->tableWidget->clearContents();
	ui->tableWidget->setRowCount(0);
	int t = 0;
	BOOST_REVERSE_FOREACH(CRichListDataItem p, setRichList)
        {
	    if(p.nBalance > 0)
	    {
	        walletTicks.push_back((double)t);
	        walletBalances.push_back(p.nBalance / COIN);
	        t++;
	    }
	}

	BOOST_FOREACH(CRichListDataItem p, setRichList)
        {
	    if(n > nMaxCount)
		break;

	    // Insert row
	    ui->tableWidget->insertRow(n);
	    QTableWidgetItem *balItem = new QTableWidgetItem(QString::number((p.nBalance / COIN), 'f', 8));
	    QTableWidgetItem *addrItem = new QTableWidgetItem(QString::fromStdString(p.dAddress));

	    ui->tableWidget->setItem(n, 0, balItem);
	    ui->tableWidget->setItem(n, 1, addrItem);
	}
	ui->tableWidget->scrollToTop();
        QString upd = QString::number(richListData.nLastHeight);
        int nBehind = pindexBest->nHeight - richListData.nLastHeight;
        if(nBehind == 0)
	    upd += " (Up to date)";
	else if(nBehind == 1)
	    upd += " (" + QString::number(nBehind) + " block behind)";
	else
	    upd += " (" + QString::number(nBehind) + " blocks behind)";
        ui->lastUpdatedLabel->setText(upd);
    }

}

void RichListPage::DisplayCharts()
{
// create empty bar chart objects:
  QCPBars *regen = new QCPBars(ui->distributionChartWidget->xAxis, ui->distributionChartWidget->yAxis);
  
  ui->distributionChartWidget->addPlottable(regen);
  
  // set names and colors:
  QPen pen;
  pen.setWidthF(1.2);
  regen->setName("Wallets");
  pen.setColor(QColor(21, 36, 67));
  regen->setPen(pen);
  regen->setBrush(QColor(21, 36, 67, 70));
   
  
  ui->distributionChartWidget->xAxis->setAutoTicks(true);
  ui->distributionChartWidget->xAxis->setAutoTickLabels(true);
  
  ui->distributionChartWidget->xAxis->setTickLabelRotation(60);
  ui->distributionChartWidget->xAxis->setSubTickCount(0);
  ui->distributionChartWidget->xAxis->setTickLength(0, 4);
  ui->distributionChartWidget->xAxis->grid()->setVisible(false);
    
  // prepare y axis:
  ui->distributionChartWidget->yAxis->setAutoTicks(true);
  ui->distributionChartWidget->yAxis->setAutoTickLabels(true);
  ui->distributionChartWidget->yAxis->setPadding(5); // a bit more space to the left border
  ui->distributionChartWidget->yAxis->setLabel("Wallet Balance");
  ui->distributionChartWidget->yAxis->grid()->setSubGridVisible(true);
  QPen gridPen;
  gridPen.setStyle(Qt::SolidLine);
  gridPen.setColor(QColor(0, 0, 0, 25));
  ui->distributionChartWidget->yAxis->grid()->setPen(gridPen);
  gridPen.setStyle(Qt::DotLine);
  ui->distributionChartWidget->yAxis->grid()->setSubGridPen(gridPen);
  
  // Add data:
  regen->setData(walletTicks, walletBalances);
  
  // setup legend:
  /*ui->distributionChartWidget->legend->setVisible(false);
  ui->distributionChartWidget->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop|Qt::AlignHCenter);
  ui->distributionChartWidget->legend->setBrush(QColor(255, 255, 255, 200));
  QPen legendPen;
  legendPen.setColor(QColor(130, 130, 130, 200));
  ui->distributionChartWidget->legend->setBorderPen(legendPen);
  QFont legendFont = font();
  ui->distributionChartWidget.setPointSize(10);
  ui->distributionChartWidget->legend->setFont(legendFont);
  ui->distributionChartWidget->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);*/

  ui->distributionChartWidget->rescaleAxes();
  ui->distributionChartWidget->replot();
}
