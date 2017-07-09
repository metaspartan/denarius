#ifndef POOLBROWSER_H
#define POOLBROWSER_H

#include "clientmodel.h"
#include "main.h"
#include "wallet.h"
#include "base58.h"

#include <QWidget>
#include <QObject>
#include <QtNetwork/QtNetwork>


extern QString bitcoing;
extern QString dollarg;

namespace Ui {
class PoolBrowser;
}
class ClientModel;


class PoolBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit PoolBrowser(QWidget *parent = 0);
    ~PoolBrowser();
    
    void setModel(ClientModel *model);

private:
    void getRequest( const QString &url );

signals:
    void networkError( QNetworkReply::NetworkError err );

public slots:
    void parseNetworkResponse(QNetworkReply *finished );
    void randomChuckNorrisJoke();
    void randomChuckNorrisJoke2();
    void bittrex();
    void poloniex();
    void egaldo();
    void overv();

private:
    QNetworkAccessManager m_nam;
    Ui::PoolBrowser *ui;
    ClientModel *model;

};

#endif // POOLBROWSER_H
