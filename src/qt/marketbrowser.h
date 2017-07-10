#ifndef MARKETBROWSER_H
#define MARKETBROWSER_H

#include "clientmodel.h"
#include "main.h"
#include "wallet.h"
#include "base58.h"

#include <QWidget>
#include <QObject>
#include <QtNetwork/QtNetwork>


extern QString bitcoing;
extern QString dollarg;
extern QString dnrmarket;

namespace Ui {
class MarketBrowser;
}
class ClientModel;


class MarketBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit MarketBrowser(QWidget *parent = 0);
    ~MarketBrowser();
    
    void setModel(ClientModel *model);

private:
    void getRequest( const QString &url );

signals:
    void networkError( QNetworkReply::NetworkError err );

public slots:
    void parseNetworkResponse(QNetworkReply *finished );
    void requests();
    void update();

private:
    QNetworkAccessManager m_nam;
    Ui::MarketBrowser *ui;
    ClientModel *model;

};

#endif // MARKETBROWSER_H
