#ifndef MARKETLISTINGDETAILSDIALOG_H
#define MARKETLISTINGDETAILSDIALOG_H

#include "util.h"
#include "market.h"
#include <QDialog>
#include <QNetworkAccessManager>

namespace Ui {
class MarketListingDetailsDialog;
}

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
QT_END_NAMESPACE


class MarketListingDetailsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MarketListingDetailsDialog(QWidget *parent = 0, uint256 listingId = uint256(0));
    ~MarketListingDetailsDialog();

protected:

private slots:
    void on_okButton_clicked();
    void getImgOneReplyFinished();
    void getImgTwoReplyFinished();

signals:

private:
    Ui::MarketListingDetailsDialog *ui;
    QNetworkAccessManager* manager;
};

#endif // MARKETLISTINGDETAILSDIALOG_H
