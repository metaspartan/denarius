#ifndef DELIVERYDETAILSDIALOG_H
#define DELIVERYDETAILSDIALOG_H

#include "util.h"
#include "init.h"
#include "main.h"

#include <QDialog>

namespace Ui {
class DeliveryDetailsDialog;
}


class DeliveryDetailsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DeliveryDetailsDialog(QWidget *parent = 0, uint256 buyRequestId = uint256(0));
    ~DeliveryDetailsDialog();
    uint256 myBuyRequestId;

protected:

private slots:
    void on_okButton_clicked();
    void on_cancelButton_clicked();

signals:

private:
    Ui::DeliveryDetailsDialog *ui;
};

#endif // DELIVERYDETAILSDIALOG_H
