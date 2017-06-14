/********************************************************************************
** Form generated from reading UI file 'sendmessagesdialog.ui'
**
** Created by: Qt User Interface Compiler version 4.8.6
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_SENDMESSAGESDIALOG_H
#define UI_SENDMESSAGESDIALOG_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtGui/QApplication>
#include <QtGui/QButtonGroup>
#include <QtGui/QDialog>
#include <QtGui/QFrame>
#include <QtGui/QHBoxLayout>
#include <QtGui/QHeaderView>
#include <QtGui/QLabel>
#include <QtGui/QPushButton>
#include <QtGui/QScrollArea>
#include <QtGui/QSpacerItem>
#include <QtGui/QToolButton>
#include <QtGui/QVBoxLayout>
#include <QtGui/QWidget>
#include "qvalidatedlineedit.h"

QT_BEGIN_NAMESPACE

class Ui_SendMessagesDialog
{
public:
    QVBoxLayout *verticalLayout;
    QFrame *frameAddressFrom;
    QHBoxLayout *horizontalLayout_3;
    QLabel *addressFromLabel;
    QHBoxLayout *horizontalLayout_4;
    QValidatedLineEdit *addressFrom;
    QToolButton *addressBookButton;
    QToolButton *pasteButton;
    QScrollArea *scrollArea;
    QWidget *scrollAreaWidgetContents;
    QVBoxLayout *verticalLayout_2;
    QVBoxLayout *entries;
    QSpacerItem *verticalSpacer;
    QHBoxLayout *horizontalLayout;
    QPushButton *addButton;
    QPushButton *clearButton;
    QSpacerItem *horizontalSpacer;
    QPushButton *sendButton;
    QPushButton *closeButton;

    void setupUi(QDialog *SendMessagesDialog)
    {
        if (SendMessagesDialog->objectName().isEmpty())
            SendMessagesDialog->setObjectName(QString::fromUtf8("SendMessagesDialog"));
        SendMessagesDialog->resize(850, 400);
        verticalLayout = new QVBoxLayout(SendMessagesDialog);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(-1, -1, -1, 8);
        frameAddressFrom = new QFrame(SendMessagesDialog);
        frameAddressFrom->setObjectName(QString::fromUtf8("frameAddressFrom"));
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(frameAddressFrom->sizePolicy().hasHeightForWidth());
        frameAddressFrom->setSizePolicy(sizePolicy);
        frameAddressFrom->setMaximumSize(QSize(16777215, 16777215));
        frameAddressFrom->setFrameShape(QFrame::StyledPanel);
        frameAddressFrom->setFrameShadow(QFrame::Sunken);
        horizontalLayout_3 = new QHBoxLayout(frameAddressFrom);
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        addressFromLabel = new QLabel(frameAddressFrom);
        addressFromLabel->setObjectName(QString::fromUtf8("addressFromLabel"));

        horizontalLayout_3->addWidget(addressFromLabel);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setSpacing(1);
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        addressFrom = new QValidatedLineEdit(frameAddressFrom);
        addressFrom->setObjectName(QString::fromUtf8("addressFrom"));

        horizontalLayout_4->addWidget(addressFrom);

        addressBookButton = new QToolButton(frameAddressFrom);
        addressBookButton->setObjectName(QString::fromUtf8("addressBookButton"));
        QIcon icon;
        icon.addFile(QString::fromUtf8(":/icons/address-book"), QSize(), QIcon::Normal, QIcon::Off);
        addressBookButton->setIcon(icon);

        horizontalLayout_4->addWidget(addressBookButton);

        pasteButton = new QToolButton(frameAddressFrom);
        pasteButton->setObjectName(QString::fromUtf8("pasteButton"));
        QIcon icon1;
        icon1.addFile(QString::fromUtf8(":/icons/editpaste"), QSize(), QIcon::Normal, QIcon::Off);
        pasteButton->setIcon(icon1);

        horizontalLayout_4->addWidget(pasteButton);


        horizontalLayout_3->addLayout(horizontalLayout_4);


        verticalLayout->addWidget(frameAddressFrom);

        scrollArea = new QScrollArea(SendMessagesDialog);
        scrollArea->setObjectName(QString::fromUtf8("scrollArea"));
        scrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName(QString::fromUtf8("scrollAreaWidgetContents"));
        scrollAreaWidgetContents->setGeometry(QRect(0, 0, 830, 279));
        verticalLayout_2 = new QVBoxLayout(scrollAreaWidgetContents);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        verticalLayout_2->setContentsMargins(0, 0, 0, 0);
        entries = new QVBoxLayout();
        entries->setSpacing(6);
        entries->setObjectName(QString::fromUtf8("entries"));

        verticalLayout_2->addLayout(entries);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_2->addItem(verticalSpacer);

        scrollArea->setWidget(scrollAreaWidgetContents);

        verticalLayout->addWidget(scrollArea);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        addButton = new QPushButton(SendMessagesDialog);
        addButton->setObjectName(QString::fromUtf8("addButton"));
        QIcon icon2;
        icon2.addFile(QString::fromUtf8(":/icons/add"), QSize(), QIcon::Normal, QIcon::Off);
        addButton->setIcon(icon2);
        addButton->setAutoDefault(false);

        horizontalLayout->addWidget(addButton);

        clearButton = new QPushButton(SendMessagesDialog);
        clearButton->setObjectName(QString::fromUtf8("clearButton"));
        QSizePolicy sizePolicy1(QSizePolicy::Minimum, QSizePolicy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(clearButton->sizePolicy().hasHeightForWidth());
        clearButton->setSizePolicy(sizePolicy1);
        QIcon icon3;
        icon3.addFile(QString::fromUtf8(":/icons/remove"), QSize(), QIcon::Normal, QIcon::Off);
        clearButton->setIcon(icon3);
        clearButton->setAutoRepeatDelay(300);
        clearButton->setAutoDefault(false);

        horizontalLayout->addWidget(clearButton);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        sendButton = new QPushButton(SendMessagesDialog);
        sendButton->setObjectName(QString::fromUtf8("sendButton"));
        sendButton->setMinimumSize(QSize(150, 0));
        QIcon icon4;
        icon4.addFile(QString::fromUtf8(":/icons/send"), QSize(), QIcon::Normal, QIcon::Off);
        sendButton->setIcon(icon4);
        sendButton->setDefault(true);

        horizontalLayout->addWidget(sendButton);

        closeButton = new QPushButton(SendMessagesDialog);
        closeButton->setObjectName(QString::fromUtf8("closeButton"));
        QIcon icon5;
        icon5.addFile(QString::fromUtf8(":/icons/quit"), QSize(), QIcon::Normal, QIcon::Off);
        closeButton->setIcon(icon5);

        horizontalLayout->addWidget(closeButton);


        verticalLayout->addLayout(horizontalLayout);

        verticalLayout->setStretch(1, 1);
#ifndef QT_NO_SHORTCUT
        addressFromLabel->setBuddy(addressFrom);
#endif // QT_NO_SHORTCUT

        retranslateUi(SendMessagesDialog);

        QMetaObject::connectSlotsByName(SendMessagesDialog);
    } // setupUi

    void retranslateUi(QDialog *SendMessagesDialog)
    {
        SendMessagesDialog->setWindowTitle(QApplication::translate("SendMessagesDialog", "Send Messages", 0, QApplication::UnicodeUTF8));
        addressFromLabel->setText(QApplication::translate("SendMessagesDialog", "Address &From:", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        addressBookButton->setToolTip(QApplication::translate("SendMessagesDialog", "Choose address from address book", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        addressBookButton->setText(QString());
        addressBookButton->setShortcut(QApplication::translate("SendMessagesDialog", "Alt+A", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        pasteButton->setToolTip(QApplication::translate("SendMessagesDialog", "Paste address from clipboard", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        pasteButton->setText(QString());
        pasteButton->setShortcut(QApplication::translate("SendMessagesDialog", "Alt+P", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        addButton->setToolTip(QApplication::translate("SendMessagesDialog", "Send to multiple recipients at once", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        addButton->setText(QApplication::translate("SendMessagesDialog", "Add &Recipient", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        clearButton->setToolTip(QApplication::translate("SendMessagesDialog", "Remove all transaction fields", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        clearButton->setText(QApplication::translate("SendMessagesDialog", "Clear &All", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        sendButton->setToolTip(QApplication::translate("SendMessagesDialog", "Confirm the send action", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        sendButton->setText(QApplication::translate("SendMessagesDialog", "S&end", 0, QApplication::UnicodeUTF8));
        closeButton->setText(QApplication::translate("SendMessagesDialog", "&Close", 0, QApplication::UnicodeUTF8));
    } // retranslateUi

};

namespace Ui {
    class SendMessagesDialog: public Ui_SendMessagesDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_SENDMESSAGESDIALOG_H
