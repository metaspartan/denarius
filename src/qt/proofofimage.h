#ifndef PROOFOFIMAGE_H
#define PROOFOFIMAGE_H

#include <QWidget>


namespace Ui {
    class ProofOfImage;
}

/** Proof of Image page widget */
class ProofOfImage : public QWidget
{
    Q_OBJECT

public:
    explicit ProofOfImage(QWidget *parent = 0);
    ~ProofOfImage();
    QString fileName;

public slots:

signals:

private:
    Ui::ProofOfImage *ui;
    void noImageSelected();

private slots:
    void on_filePushButton_clicked();
    void on_createPushButton_clicked();
    void on_checkButton_clicked();
    void on_checkTxButton_clicked();
};

#endif // PROOFOFIMAGE_H
