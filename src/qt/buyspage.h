#ifndef BUYSPAGE_H
#define BUYSPAGE_H

#include <QWidget>

namespace Ui {
    class BuysPage;
}

class BuysPage : public QWidget
{
    Q_OBJECT

public:
    explicit BuysPage(QWidget *parent = 0);
    ~BuysPage();
    

public slots:
    void LoadBuys();

private:
    Ui::BuysPage *ui;
    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

private slots:
    void on_tableWidget_itemSelectionChanged();
    void on_copyAddressButton_clicked();
    void on_escrowLockButton_clicked();
    void on_deliveryDetailsButton_clicked();
    void on_payButton_clicked();
    void on_refundButton_clicked();
};

#endif // BUYSPAGE_H
