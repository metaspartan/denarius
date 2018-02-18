#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "darksend.h"
#include "darksendconfig.h"
#include "walletmodel.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"
#include "transactiontablemodel.h"
#include "transactionfilterproxy.h"
#include "guiutil.h"
#include "guiconstants.h"
#include "marketbrowser.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QTimer>
#include <QDebug>
#include <QScrollArea>
//#include <QScroller>

#define DECORATION_SIZE 64
#define NUM_ITEMS 3

const QString BaseURL = "http://denarius.io/dnrusd.php";
const QString BaseURL2 = "http://denarius.io/dnrbtc.php";
double denariusx;
double dnrbtcx;

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(): QAbstractItemDelegate(), unit(BitcoinUnits::BTC), unitUSD(BitcoinUnits::USD)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(qVariantCanConvert<QColor>(value))
        {
            foreground = qvariant_cast<QColor>(value);
        }

        painter->setPen(foreground);
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address);

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
	int unitUSD;

};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    currentBalance(-1),
    currentStake(0),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    txdelegate(new TxViewDelegate()),
    filter(0)
{
    ui->setupUi(this);
	
	ui->frameDarksend->setVisible(false);
	
	//PriceRequest();
	QObject::connect(&m_nam, SIGNAL(finished(QNetworkReply*)), this, SLOT(parseNetworkResponse(QNetworkReply*)));
	connect(ui->refreshButton, SIGNAL(pressed()), this, SLOT( PriceRequest()));
	
    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));
	
	showingDarkSendMessage = 0;
    darksendActionCheck = 0;
    lastNewBlock = 0;

    if(fLiteMode){
        ui->frameDarksend->setVisible(false);
    } else {
	qDebug() << "Dark Send Status Timer";
        timer = new QTimer(this);
        connect(timer, SIGNAL(timeout()), this, SLOT(darkSendStatus()));
		//timer->start(333);
	if(!GetBoolArg("-reindexaddr", false))
            timer->start(60000);
    }
	
	if(fMasterNode || fLiteMode){
        ui->toggleDarksend->setText("(" + tr("Disabled") + ")");
        ui->toggleDarksend->setEnabled(false);
    }else if(!fEnableDarksend){
        ui->toggleDarksend->setText(tr("Start Darksend Mixing"));
    } else {
        ui->toggleDarksend->setText(tr("Stop Darksend Mixing"));
    }

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("Out of Sync!") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("Out of Sync!") + ")");

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
}

void OverviewPage::PriceRequest()
{
	getRequest(BaseURL);
	getRequest(BaseURL2);
	updateDisplayUnit(); //Maybe not?
}

void OverviewPage::getRequest( const QString &urlString )
{
    QUrl url ( urlString );
    QNetworkRequest req ( url );
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8");
    m_nam.get(req);
}

void OverviewPage::parseNetworkResponse(QNetworkReply *finished )
{

    QUrl what = finished->url();

    if ( finished->error() != QNetworkReply::NoError )
    {
        // A communication error has occurred
        emit networkError( finished->error() );
        return;
    }
	
if (what == BaseURL) // Denarius Price
{

    // QNetworkReply is a QIODevice. So we read from it just like it was a file
    QString denarius = finished->readAll();
    denariusx = (denarius.toDouble());
    denarius = QString::number(denariusx, 'f', 2);

	dollarg = denarius;
}
if (what == BaseURL2) // Denarius BTC Price
{

    // QNetworkReply is a QIODevice. So we read from it just like it was a file
    QString dnrbtc = finished->readAll();
    dnrbtcx = (dnrbtc.toDouble());
    dnrbtc = QString::number(dnrbtcx, 'f', 8);

	bitcoing = dnrbtc;
}
finished->deleteLater();
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        emit transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(qint64 balance, qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance, qint64 anonymizedBalance)
{
    int unit = model->getOptionsModel()->getDisplayUnit();
    //int unitUSD = BitcoinUnits::USD;
    int unitdBTC = BitcoinUnits::dBTC;
    currentBalance = balance;
    currentStake = stake;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
	currentAnonymizedBalance = anonymizedBalance;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance));
    ui->labelStake->setText(BitcoinUnits::formatWithUnit(unit, stake));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balance + stake + unconfirmedBalance + immatureBalance));
	ui->labelAnonymized->setText(BitcoinUnits::formatWithUnit(unit, anonymizedBalance));
    //ui->labelUSDTotal->setText(BitcoinUnits::formatWithUnit(unitUSD, dollarg.toDouble() * (balance + stake + unconfirmedBalance + immatureBalance)));
	
	QString total;
	double dollarg2 = (dollarg.toDouble() * (balance + stake + unconfirmedBalance + immatureBalance) / 100000000);
	total = QString::number(dollarg2, 'f', 2);
	ui->labelUSDTotal->setText("$" + total + " USD");
	
    ui->labelBTCTotal->setText(BitcoinUnits::formatWithUnit(unitdBTC, bitcoing.toDouble() * (balance + stake + unconfirmedBalance + immatureBalance)));
    ui->labelTradeLink->setTextFormat(Qt::RichText);
    ui->labelTradeLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->labelTradeLink->setOpenExternalLinks(true);
	
    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    ui->labelImmature->setVisible(showImmature);
    ui->labelImmatureText->setVisible(showImmature);
}

void OverviewPage::setModel(WalletModel *model)
{
    this->model = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Status, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getStake(), model->getUnconfirmedBalance(), model->getImmatureBalance(), model->getAnonymizedBalance());
        connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64, qint64, qint64)), this, SLOT(setBalance(qint64, qint64, qint64, qint64, qint64)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
		
		connect(ui->darksendAuto, SIGNAL(clicked()), this, SLOT(darksendAuto()));
        connect(ui->darksendReset, SIGNAL(clicked()), this, SLOT(darksendReset()));
        connect(ui->toggleDarksend, SIGNAL(clicked()), this, SLOT(toggleDarksend()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, model->getStake(), currentUnconfirmedBalance, currentImmatureBalance, currentAnonymizedBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = model->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::updateDarksendProgress()
{
    qDebug() << "updateDarksendProgress()";
    if(IsInitialBlockDownload()) return;
    
    qDebug() << "updateDarksendProgress() getbalance";
    int64_t nBalance = pwalletMain->GetBalance();
    if(nBalance == 0)
    {
        ui->darksendProgress->setValue(0);
        QString s(tr("No inputs detected"));
        ui->darksendProgress->setToolTip(s);
        return;
    }

    //get denominated unconfirmed inputs
    if(pwalletMain->GetDenominatedBalance(true, true) > 0)
    {
        QString s(tr("Found unconfirmed denominated outputs, will wait till they confirm to recalculate."));
        ui->darksendProgress->setToolTip(s);
        return;
    }

    //Get the anon threshold
    int64_t nMaxToAnonymize = nAnonymizeDenariusAmount*COIN;

    // If it's more than the wallet amount, limit to that.
    if(nMaxToAnonymize > nBalance) nMaxToAnonymize = nBalance;

    if(nMaxToAnonymize == 0) return;

    // calculate parts of the progress, each of them shouldn't be higher than 1:
    // mixing progress of denominated balance
    int64_t denominatedBalance = pwalletMain->GetDenominatedBalance();
    float denomPart = 0;
    if(denominatedBalance > 0)
    {
        denomPart = (float)pwalletMain->GetNormalizedAnonymizedBalance() / pwalletMain->GetDenominatedBalance();
        denomPart = denomPart > 1 ? 1 : denomPart;
    }

    // % of fully anonymized balance
    float anonPart = 0;
    if(nMaxToAnonymize > 0)
    {
        anonPart = (float)pwalletMain->GetAnonymizedBalance() / nMaxToAnonymize;
        // if anonPart is > 1 then we are done, make denomPart equal 1 too
        anonPart = anonPart > 1 ? (denomPart = 1, 1) : anonPart;
    }

    // apply some weights to them (sum should be <=100) and calculate the whole progress
    int progress = 80 * denomPart + 20 * anonPart;
    if(progress > 100) progress = 100;

    ui->darksendProgress->setValue(progress);

    std::ostringstream convert;
    convert << "Progress: " << progress << "%, inputs have an average of " << pwalletMain->GetAverageAnonymizedRounds() << " of " << nDarksendRounds << " rounds";
    QString s(convert.str().c_str());
    ui->darksendProgress->setToolTip(s);
}


void OverviewPage::darkSendStatus()
{
    int nBestHeight = pindexBest->nHeight;

    if(nBestHeight != darkSendPool.cachedNumBlocks)
    {
        //we we're processing lots of blocks, we'll just leave
        if(GetTime() - lastNewBlock < 10) return;
        lastNewBlock = GetTime();

        updateDarksendProgress();

        QString strSettings(" " + tr("Rounds"));
        strSettings.prepend(QString::number(nDarksendRounds)).prepend(" / ");
        strSettings.prepend(BitcoinUnits::formatWithUnit(
            model->getOptionsModel()->getDisplayUnit(),
            nAnonymizeDenariusAmount * COIN)
        );

        ui->labelAmountRounds->setText(strSettings);
    }

    if(!fEnableDarksend) {
        if(nBestHeight != darkSendPool.cachedNumBlocks)
        {
            darkSendPool.cachedNumBlocks = nBestHeight;

            ui->darksendEnabled->setText(tr("Disabled"));
            ui->darksendStatus->setText("");
            ui->toggleDarksend->setText(tr("Start Darksend Mixing"));
        }

        return;
    }

    // check darksend status and unlock if needed
    if(nBestHeight != darkSendPool.cachedNumBlocks)
    {
        // Balance and number of transactions might have changed
        darkSendPool.cachedNumBlocks = nBestHeight;

        /* *******************************************************/

        ui->darksendEnabled->setText(tr("Enabled"));
    }

    int state = darkSendPool.GetState();
    int entries = darkSendPool.GetEntriesCount();
    int accepted = darkSendPool.GetLastEntryAccepted();

    /* ** @TODO this string creation really needs some clean ups ---vertoe ** */
    std::ostringstream convert;

    if(state == POOL_STATUS_ACCEPTING_ENTRIES) {
        if(entries == 0) {
            if(darkSendPool.strAutoDenomResult.size() == 0){
                convert << tr("Darksend is idle.").toStdString();
            } else {
                convert << darkSendPool.strAutoDenomResult;
            }
            showingDarkSendMessage = 0;
        } else if (accepted == 1) {
            convert << tr("Darksend request complete: Your transaction was accepted into the pool!").toStdString();
            if(showingDarkSendMessage % 10 > 8) {
                darkSendPool.lastEntryAccepted = 0;
                showingDarkSendMessage = 0;
            }
        } else {
            if(showingDarkSendMessage % 70 <= 40) convert << tr("Submitted following entries to masternode:").toStdString() << " " << entries << "/" << darkSendPool.GetMaxPoolTransactions();
            else if(showingDarkSendMessage % 70 <= 50) convert << tr("Submitted to masternode, Waiting for more entries").toStdString() << " (" << entries << "/" << darkSendPool.GetMaxPoolTransactions() << " ) .";
            else if(showingDarkSendMessage % 70 <= 60) convert << tr("Submitted to masternode, Waiting for more entries").toStdString() << " (" << entries << "/" << darkSendPool.GetMaxPoolTransactions() << " ) ..";
            else if(showingDarkSendMessage % 70 <= 70) convert << tr("Submitted to masternode, Waiting for more entries").toStdString() << " (" << entries << "/" << darkSendPool.GetMaxPoolTransactions() << " ) ...";
        }
    } else if(state == POOL_STATUS_SIGNING) {
        if(showingDarkSendMessage % 70 <= 10) convert << tr("Found enough users, signing ...").toStdString();
        else if(showingDarkSendMessage % 70 <= 20) convert << tr("Found enough users, signing ( waiting. )").toStdString();
        else if(showingDarkSendMessage % 70 <= 30) convert << tr("Found enough users, signing ( waiting.. )").toStdString();
        else if(showingDarkSendMessage % 70 <= 40) convert << tr("Found enough users, signing ( waiting... )").toStdString();
    } else if(state == POOL_STATUS_TRANSMISSION) {
        convert << tr("Transmitting final transaction.").toStdString();
    } else if (state == POOL_STATUS_IDLE) {
        convert << tr("Darksend is idle.").toStdString();
    } else if (state == POOL_STATUS_FINALIZE_TRANSACTION) {
        convert << tr("Finalizing transaction.").toStdString();
    } else if(state == POOL_STATUS_ERROR) {
        convert << tr("Darksend request incomplete:").toStdString() << " " << darkSendPool.lastMessage << ". " << tr("Will retry...").toStdString();
    } else if(state == POOL_STATUS_SUCCESS) {
        convert << tr("Darksend request complete:").toStdString() << " " << darkSendPool.lastMessage;
    } else if(state == POOL_STATUS_QUEUE) {
        if(showingDarkSendMessage % 70 <= 50) convert << tr("Submitted to masternode, waiting in queue .").toStdString();
        else if(showingDarkSendMessage % 70 <= 60) convert << tr("Submitted to masternode, waiting in queue ..").toStdString();
        else if(showingDarkSendMessage % 70 <= 70) convert << tr("Submitted to masternode, waiting in queue ...").toStdString();
    } else {
        convert << tr("Unknown state:").toStdString() << " id = " << state;
    }

    if(state == POOL_STATUS_ERROR || state == POOL_STATUS_SUCCESS) darkSendPool.Check();

    QString s(convert.str().c_str());
    s = tr("Last Darksend message:\n") + s;

    if(s != ui->darksendStatus->text())
        printf("Last Darksend message: %s\n", convert.str().c_str());

    ui->darksendStatus->setText(s);

    if(darkSendPool.sessionDenom == 0){
        ui->labelSubmittedDenom->setText(tr("N/A"));
    } else {
        std::string out;
        darkSendPool.GetDenominationsToString(darkSendPool.sessionDenom, out);
        QString s2(out.c_str());
        ui->labelSubmittedDenom->setText(s2);
    }

    showingDarkSendMessage++;
    darksendActionCheck++;

    // Get DarkSend Denomination Status
}

void OverviewPage::darksendAuto(){
    darkSendPool.DoAutomaticDenominating();
}

void OverviewPage::darksendReset(){
    darkSendPool.Reset();

    QMessageBox::warning(this, tr("Darksend"),
        tr("Darksend was successfully reset."),
        QMessageBox::Ok, QMessageBox::Ok);
}

void OverviewPage::toggleDarksend(){
    if(!fEnableDarksend){
        int64_t balance = pwalletMain->GetBalance();
        float minAmount = 1.49 * COIN;
        if(balance < minAmount){
            QString strMinAmount(
                BitcoinUnits::formatWithUnit(
                    model->getOptionsModel()->getDisplayUnit(),
                    minAmount));
            QMessageBox::warning(this, tr("Darksend"),
                tr("Darksend requires at least %1 to use.").arg(strMinAmount),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }

        // if wallet is locked, ask for a passphrase
        if (model->getEncryptionStatus() == WalletModel::Locked)
        {
            WalletModel::UnlockContext ctx(model->requestUnlock());
            if(!ctx.isValid())
            {
                //unlock was cancelled
                darkSendPool.cachedNumBlocks = 0;
                QMessageBox::warning(this, tr("Darksend"),
                    tr("Wallet is locked and user declined to unlock. Disabling Darksend."),
                    QMessageBox::Ok, QMessageBox::Ok);
                if (fDebug) printf("Wallet is locked and user declined to unlock. Disabling Darksend.\n");
                return;
            }
        }

    }

    darkSendPool.cachedNumBlocks = 0;
    fEnableDarksend = !fEnableDarksend;

    if(!fEnableDarksend){
        ui->toggleDarksend->setText(tr("Start Darksend Mixing"));
    } else {
        ui->toggleDarksend->setText(tr("Stop Darksend Mixing"));

        /* show darksend configuration if client has defaults set */

        if(nAnonymizeDenariusAmount == 0){
            DarksendConfig dlg(this);
            dlg.setModel(model);
            dlg.exec();
        }

        darkSendPool.DoAutomaticDenominating();
    }
}