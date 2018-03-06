#ifndef ADDEDITADRENALINENODE_H
#define ADDEDITADRENALINENODE_H

#include <QDialog>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <iostream>

namespace Ui {
class AddEditAdrenalineNode;
}


class AddEditAdrenalineNode : public QDialog
{
    Q_OBJECT

public:
    explicit AddEditAdrenalineNode(QWidget *parent = 0);
    ~AddEditAdrenalineNode();

protected:

private slots:
    void on_okButton_clicked();
    void on_cancelButton_clicked();
    void on_AddEditAddressPasteButton_clicked();
    void on_AddEditPrivkeyPasteButton_clicked();
    void on_AddEditTxhashPasteButton_clicked();

signals:

private:
    Ui::AddEditAdrenalineNode *ui;
};

#endif // ADDEDITADRENALINENODE_H