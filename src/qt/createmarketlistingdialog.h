#ifndef CREATEMARKETLISTINGDIALOG_H
#define CREATEMARKETLISTINGDIALOG_H

#include <QDialog>

namespace Ui {
class CreateMarketListingDialog;
}


class CreateMarketListingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateMarketListingDialog(QWidget *parent = 0);
    ~CreateMarketListingDialog();

protected:

private slots:
    void on_okButton_clicked();
    void on_cancelButton_clicked();

signals:

private:
    Ui::CreateMarketListingDialog *ui;
};

#endif // CREATEMARKETLISTINGDIALOG_H
