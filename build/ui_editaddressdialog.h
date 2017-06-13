/********************************************************************************
** Form generated from reading UI file 'editaddressdialog.ui'
**
** Created by: Qt User Interface Compiler version 4.8.6
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_EDITADDRESSDIALOG_H
#define UI_EDITADDRESSDIALOG_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtGui/QApplication>
#include <QtGui/QButtonGroup>
#include <QtGui/QCheckBox>
#include <QtGui/QDialog>
#include <QtGui/QDialogButtonBox>
#include <QtGui/QFormLayout>
#include <QtGui/QHeaderView>
#include <QtGui/QLabel>
#include <QtGui/QLineEdit>
#include <QtGui/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_EditAddressDialog
{
public:
    QVBoxLayout *verticalLayout;
    QFormLayout *formLayout;
    QLabel *label;
    QLineEdit *labelEdit;
    QLabel *label_2;
    QLineEdit *addressEdit;
    QCheckBox *stealthCB;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *EditAddressDialog)
    {
        if (EditAddressDialog->objectName().isEmpty())
            EditAddressDialog->setObjectName(QString::fromUtf8("EditAddressDialog"));
        EditAddressDialog->resize(457, 129);
        verticalLayout = new QVBoxLayout(EditAddressDialog);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        label = new QLabel(EditAddressDialog);
        label->setObjectName(QString::fromUtf8("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        labelEdit = new QLineEdit(EditAddressDialog);
        labelEdit->setObjectName(QString::fromUtf8("labelEdit"));

        formLayout->setWidget(0, QFormLayout::FieldRole, labelEdit);

        label_2 = new QLabel(EditAddressDialog);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        addressEdit = new QLineEdit(EditAddressDialog);
        addressEdit->setObjectName(QString::fromUtf8("addressEdit"));

        formLayout->setWidget(1, QFormLayout::FieldRole, addressEdit);

        stealthCB = new QCheckBox(EditAddressDialog);
        stealthCB->setObjectName(QString::fromUtf8("stealthCB"));

        formLayout->setWidget(2, QFormLayout::SpanningRole, stealthCB);


        verticalLayout->addLayout(formLayout);

        buttonBox = new QDialogButtonBox(EditAddressDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);

#ifndef QT_NO_SHORTCUT
        label->setBuddy(labelEdit);
        label_2->setBuddy(addressEdit);
#endif // QT_NO_SHORTCUT

        retranslateUi(EditAddressDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), EditAddressDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), EditAddressDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(EditAddressDialog);
    } // setupUi

    void retranslateUi(QDialog *EditAddressDialog)
    {
        EditAddressDialog->setWindowTitle(QApplication::translate("EditAddressDialog", "Edit Address", 0, QApplication::UnicodeUTF8));
        label->setText(QApplication::translate("EditAddressDialog", "&Label", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        labelEdit->setToolTip(QApplication::translate("EditAddressDialog", "The label associated with this address book entry", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        label_2->setText(QApplication::translate("EditAddressDialog", "&Address", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        addressEdit->setToolTip(QApplication::translate("EditAddressDialog", "The address associated with this address book entry. This can only be modified for sending addresses.", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        stealthCB->setText(QApplication::translate("EditAddressDialog", "&Stealth Address", 0, QApplication::UnicodeUTF8));
    } // retranslateUi

};

namespace Ui {
    class EditAddressDialog: public Ui_EditAddressDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_EDITADDRESSDIALOG_H
