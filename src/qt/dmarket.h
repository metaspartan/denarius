#ifndef DMARKET_H
#define DMARKET_H

#include <QWidget>
#include <QTimer>
#include <QListWidgetItem>

namespace Ui {
    class DenariusMarket;
}

class DenariusMarket : public QWidget
{
    Q_OBJECT

public:
    explicit DenariusMarket(QWidget *parent = 0);
    ~DenariusMarket();
    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();
    void updateCategories();
    double lastPrice;

public slots:
    void updateCategory(QString category);

private:
    Ui::DenariusMarket *ui;

private slots:
    void on_tableWidget_itemSelectionChanged();
    void on_categoriesListWidget_currentItemChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void on_buyButton_clicked();
    void on_viewDetailsButton_clicked();
    void on_copyAddressButton_clicked();
};

#endif // DMARKET_H
