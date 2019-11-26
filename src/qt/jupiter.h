#ifndef JUPITER_H
#define JUPITER_H

#include <QWidget>
//#include <QWebView>

namespace Ui {
    class Jupiter;
}

/** Jupiter page widget */
class Jupiter : public QWidget
{
    Q_OBJECT

public:
    explicit Jupiter(QWidget *parent = 0);
    ~Jupiter();
    QString fileName;
    QString fileCont;

public slots:

signals:

private:
    Ui::Jupiter *ui;
    void noImageSelected();

private slots:
    void on_filePushButton_clicked();
    void on_createPushButton_clicked();
    void on_checkButton_clicked();
    void on_checkTxButton_clicked();
};

#endif // JUPITER_H
