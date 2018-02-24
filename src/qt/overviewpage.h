#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include <QWidget>
#include <QTimer>
#include <QtNetwork/QtNetwork>

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
	void updateDarksendProgress();
	
private:
	void getRequest( const QString &url );

public slots:
	void darkSendStatus();
    void setBalance(qint64 balance, qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance, qint64 anonymizedBalance);
	void parseNetworkResponse(QNetworkReply *finished );
    void PriceRequest();

signals:
    void transactionClicked(const QModelIndex &index);
	void networkError( QNetworkReply::NetworkError err );

private:
    QTimer *timer;
    Ui::OverviewPage *ui;
    WalletModel *model;
    qint64 currentBalance;
    qint64 currentStake;
    qint64 currentUnconfirmedBalance;
    qint64 currentImmatureBalance;
	qint64 currentAnonymizedBalance;
    qint64 lastNewBlock;

	int showingDarkSendMessage;
    int darksendActionCheck;
    int cachedNumBlocks;
    TxViewDelegate *txdelegate;
    TransactionFilterProxy *filter;
	QNetworkAccessManager m_nam;

private slots:
    void toggleDarksend();
    void darksendAuto();
    void darksendReset();
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
};

#endif // OVERVIEWPAGE_H
