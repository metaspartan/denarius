/********************************************************************************
** Form generated from reading UI file 'addressbookpage.ui'
**
** Created by: Qt User Interface Compiler version 4.8.6
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_ADDRESSBOOKPAGE_H
#define UI_ADDRESSBOOKPAGE_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtGui/QApplication>
#include <QtGui/QButtonGroup>
#include <QtGui/QDialogButtonBox>
#include <QtGui/QHBoxLayout>
#include <QtGui/QHeaderView>
#include <QtGui/QLabel>
#include <QtGui/QPushButton>
#include <QtGui/QSpacerItem>
#include <QtGui/QTableView>
#include <QtGui/QVBoxLayout>
#include <QtGui/QWidget>

QT_BEGIN_NAMESPACE

class Ui_AddressBookPage
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *labelExplanation;
    QTableView *tableView;
    QHBoxLayout *horizontalLayout;
    QPushButton *newAddressButton;
    QPushButton *copyToClipboard;
    QPushButton *showQRCode;
    QPushButton *signMessage;
    QPushButton *verifyMessage;
    QPushButton *deleteButton;
    QSpacerItem *horizontalSpacer;
    QDialogButtonBox *buttonBox;

    void setupUi(QWidget *AddressBookPage)
    {
        if (AddressBookPage->objectName().isEmpty())
            AddressBookPage->setObjectName(QString::fromUtf8("AddressBookPage"));
        AddressBookPage->resize(760, 380);
        verticalLayout = new QVBoxLayout(AddressBookPage);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        labelExplanation = new QLabel(AddressBookPage);
        labelExplanation->setObjectName(QString::fromUtf8("labelExplanation"));
        labelExplanation->setTextFormat(Qt::PlainText);
        labelExplanation->setWordWrap(true);

        verticalLayout->addWidget(labelExplanation);

        tableView = new QTableView(AddressBookPage);
        tableView->setObjectName(QString::fromUtf8("tableView"));
        tableView->setContextMenuPolicy(Qt::CustomContextMenu);
        tableView->setTabKeyNavigation(false);
        tableView->setAlternatingRowColors(true);
        tableView->setSelectionMode(QAbstractItemView::SingleSelection);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView->setSortingEnabled(true);
        tableView->verticalHeader()->setVisible(false);

        verticalLayout->addWidget(tableView);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        newAddressButton = new QPushButton(AddressBookPage);
        newAddressButton->setObjectName(QString::fromUtf8("newAddressButton"));
        QIcon icon;
        icon.addFile(QString::fromUtf8(":/icons/add"), QSize(), QIcon::Normal, QIcon::Off);
        newAddressButton->setIcon(icon);

        horizontalLayout->addWidget(newAddressButton);

        copyToClipboard = new QPushButton(AddressBookPage);
        copyToClipboard->setObjectName(QString::fromUtf8("copyToClipboard"));
        QIcon icon1;
        icon1.addFile(QString::fromUtf8(":/icons/editcopy"), QSize(), QIcon::Normal, QIcon::Off);
        copyToClipboard->setIcon(icon1);

        horizontalLayout->addWidget(copyToClipboard);

        showQRCode = new QPushButton(AddressBookPage);
        showQRCode->setObjectName(QString::fromUtf8("showQRCode"));
        QIcon icon2;
        icon2.addFile(QString::fromUtf8(":/icons/qrcode"), QSize(), QIcon::Normal, QIcon::Off);
        showQRCode->setIcon(icon2);

        horizontalLayout->addWidget(showQRCode);

        signMessage = new QPushButton(AddressBookPage);
        signMessage->setObjectName(QString::fromUtf8("signMessage"));
        QIcon icon3;
        icon3.addFile(QString::fromUtf8(":/icons/edit"), QSize(), QIcon::Normal, QIcon::Off);
        signMessage->setIcon(icon3);

        horizontalLayout->addWidget(signMessage);

        verifyMessage = new QPushButton(AddressBookPage);
        verifyMessage->setObjectName(QString::fromUtf8("verifyMessage"));
        QIcon icon4;
        icon4.addFile(QString::fromUtf8(":/icons/transaction_0"), QSize(), QIcon::Normal, QIcon::Off);
        verifyMessage->setIcon(icon4);

        horizontalLayout->addWidget(verifyMessage);

        deleteButton = new QPushButton(AddressBookPage);
        deleteButton->setObjectName(QString::fromUtf8("deleteButton"));
        QIcon icon5;
        icon5.addFile(QString::fromUtf8(":/icons/remove"), QSize(), QIcon::Normal, QIcon::Off);
        deleteButton->setIcon(icon5);

        horizontalLayout->addWidget(deleteButton);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        buttonBox = new QDialogButtonBox(AddressBookPage);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        QSizePolicy sizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(buttonBox->sizePolicy().hasHeightForWidth());
        buttonBox->setSizePolicy(sizePolicy);
        buttonBox->setStandardButtons(QDialogButtonBox::Ok);

        horizontalLayout->addWidget(buttonBox);


        verticalLayout->addLayout(horizontalLayout);


        retranslateUi(AddressBookPage);

        QMetaObject::connectSlotsByName(AddressBookPage);
    } // setupUi

    void retranslateUi(QWidget *AddressBookPage)
    {
        AddressBookPage->setWindowTitle(QApplication::translate("AddressBookPage", "Address Book", 0, QApplication::UnicodeUTF8));
        labelExplanation->setText(QApplication::translate("AddressBookPage", "These are your Denarius addresses for receiving payments. You may want to give a different one to each sender so you can keep track of who is paying you.", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        tableView->setToolTip(QApplication::translate("AddressBookPage", "Double-click to edit address or label", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
#ifndef QT_NO_TOOLTIP
        newAddressButton->setToolTip(QApplication::translate("AddressBookPage", "Create a new address", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        newAddressButton->setText(QApplication::translate("AddressBookPage", "&New Address", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        copyToClipboard->setToolTip(QApplication::translate("AddressBookPage", "Copy the currently selected address to the system clipboard", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        copyToClipboard->setText(QApplication::translate("AddressBookPage", "&Copy Address", 0, QApplication::UnicodeUTF8));
        showQRCode->setText(QApplication::translate("AddressBookPage", "Show &QR Code", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        signMessage->setToolTip(QApplication::translate("AddressBookPage", "Sign a message to prove you own a Denarius address", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        signMessage->setText(QApplication::translate("AddressBookPage", "Sign &Message", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        verifyMessage->setToolTip(QApplication::translate("AddressBookPage", "Verify a message to ensure it was signed with a specified Denarius address", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        verifyMessage->setText(QApplication::translate("AddressBookPage", "&Verify Message", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        deleteButton->setToolTip(QApplication::translate("AddressBookPage", "Delete the currently selected address from the list", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        deleteButton->setText(QApplication::translate("AddressBookPage", "&Delete", 0, QApplication::UnicodeUTF8));
    } // retranslateUi

};

namespace Ui {
    class AddressBookPage: public Ui_AddressBookPage {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_ADDRESSBOOKPAGE_H
