#ifndef RICHLIST_H
#define RICHLIST_H

#include <QWidget>
#include <QTimer>

namespace Ui {
    class RichListPage;
}

class RichListPage : public QWidget
{
    Q_OBJECT

public:
    explicit RichListPage(QWidget *parent = 0);
    ~RichListPage();

public slots:

private:
    Ui::RichListPage *ui;
    void ShowRichList();
    void DisplayCharts();

private slots:
    void on_refreshButton_clicked();
    void on_showButton_clicked();
};

#endif // RICHLISTPAGE_H
