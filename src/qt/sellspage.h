#ifndef SELLSPAGE_H
#define SELLSPAGE_H

#include <QWidget>

namespace Ui {
    class SellsPage;
}

class SellsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SellsPage(QWidget *parent = 0);
    ~SellsPage();
    

public slots:
    void updateListing(QString id, QString expires, QString price, QString title);
    void LoadSells();   
    void LoadBuyRequests(QString q = ""); 

private:
    Ui::SellsPage *ui;
    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

private slots:
    void on_createButton_clicked();
    void on_listingsTableWidget_itemSelectionChanged();
    void on_buysTableWidget_itemSelectionChanged();
    void on_cancelButton_clicked();
    void on_copyAddressButton_clicked();
    void on_acceptButton_clicked();
    void on_rejectButton_clicked();
    void on_refundButton_clicked();
    void on_requestPaymentButton_clicked();
    void on_cancelEscrowButton_clicked();
};

#endif // SELLSPAGE_H
