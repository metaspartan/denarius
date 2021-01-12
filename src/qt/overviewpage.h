#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include <QWidget>
#include <QTimer>
#include <QtNetwork/QtNetwork>
#include <curl/curl.h>

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

namespace Ui {
    class OverviewPage;
}
class WalletModel;
class TxViewDelegate;
class TransactionFilterProxy;

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(QWidget *parent = 0);
    ~OverviewPage();

    void setModel(WalletModel *model);
    void showOutOfSyncWarning(bool fShow);

private:
	void getRequest1( const QString &url );
    void getRequest2( const QString &url );
    void getRequest3( const QString &url );
    void getRequest4( const QString &url );

public slots:
    void setBalance(qint64 balance, qint64 lockedbalance, qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance, qint64 watchOnlyBalance, qint64 watchUnconfBalance, qint64 watchImmatureBalance);
    void PriceRequest();

signals:
    void transactionClicked(const QModelIndex &index);

private:
    QTimer *timer;
	QTimer *refreshbtnTimer;
	QTimer *updateDisplayTimer;
    Ui::OverviewPage *ui;
    WalletModel *model;
    qint64 currentBalance;
    qint64 currentLockedBalance;
    qint64 currentStake;
    qint64 currentUnconfirmedBalance;
    qint64 currentImmatureBalance;
    qint64 currentWatchOnlyBalance;
    qint64 currentWatchUnconfBalance;
    qint64 currentWatchImmatureBalance;
    qint64 totalBalance;
    qint64 lastNewBlock;

    int cachedNumBlocks;
    TxViewDelegate *txdelegate;
    TransactionFilterProxy *filter;

private slots:
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
    void updateWatchOnlyLabels(bool showWatchOnly);
};

#endif // OVERVIEWPAGE_H
