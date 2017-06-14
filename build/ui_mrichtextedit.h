/********************************************************************************
** Form generated from reading UI file 'mrichtextedit.ui'
**
** Created by: Qt User Interface Compiler version 4.8.6
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MRICHTEXTEDIT_H
#define UI_MRICHTEXTEDIT_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtGui/QApplication>
#include <QtGui/QButtonGroup>
#include <QtGui/QComboBox>
#include <QtGui/QFrame>
#include <QtGui/QHBoxLayout>
#include <QtGui/QHeaderView>
#include <QtGui/QSpacerItem>
#include <QtGui/QTextEdit>
#include <QtGui/QToolButton>
#include <QtGui/QVBoxLayout>
#include <QtGui/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MRichTextEdit
{
public:
    QVBoxLayout *verticalLayout;
    QWidget *f_toolbar;
    QHBoxLayout *horizontalLayout;
    QComboBox *f_paragraph;
    QFrame *line_4;
    QToolButton *f_undo;
    QToolButton *f_redo;
    QToolButton *f_cut;
    QToolButton *f_copy;
    QToolButton *f_paste;
    QFrame *line;
    QToolButton *f_link;
    QFrame *line_3;
    QToolButton *f_bold;
    QToolButton *f_italic;
    QToolButton *f_underline;
    QToolButton *f_strikeout;
    QFrame *line_5;
    QToolButton *f_list_bullet;
    QToolButton *f_list_ordered;
    QToolButton *f_indent_dec;
    QToolButton *f_indent_inc;
    QFrame *line_2;
    QToolButton *f_bgcolor;
    QComboBox *f_fontsize;
    QFrame *line_6;
    QSpacerItem *horizontalSpacer;
    QTextEdit *f_textedit;

    void setupUi(QWidget *MRichTextEdit)
    {
        if (MRichTextEdit->objectName().isEmpty())
            MRichTextEdit->setObjectName(QString::fromUtf8("MRichTextEdit"));
        MRichTextEdit->resize(819, 312);
        MRichTextEdit->setWindowTitle(QString::fromUtf8(""));
        verticalLayout = new QVBoxLayout(MRichTextEdit);
        verticalLayout->setSpacing(1);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(1, 1, 1, 1);
        f_toolbar = new QWidget(MRichTextEdit);
        f_toolbar->setObjectName(QString::fromUtf8("f_toolbar"));
        horizontalLayout = new QHBoxLayout(f_toolbar);
        horizontalLayout->setSpacing(2);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        f_paragraph = new QComboBox(f_toolbar);
        f_paragraph->setObjectName(QString::fromUtf8("f_paragraph"));
        f_paragraph->setFocusPolicy(Qt::ClickFocus);
        f_paragraph->setEditable(true);

        horizontalLayout->addWidget(f_paragraph);

        line_4 = new QFrame(f_toolbar);
        line_4->setObjectName(QString::fromUtf8("line_4"));
        line_4->setFrameShape(QFrame::VLine);
        line_4->setFrameShadow(QFrame::Sunken);

        horizontalLayout->addWidget(line_4);

        f_undo = new QToolButton(f_toolbar);
        f_undo->setObjectName(QString::fromUtf8("f_undo"));
        f_undo->setEnabled(false);
        f_undo->setFocusPolicy(Qt::ClickFocus);
        QIcon icon;
        QString iconThemeName = QString::fromUtf8("edit-undo");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon = QIcon::fromTheme(iconThemeName);
        } else {
            icon.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_undo->setIcon(icon);
        f_undo->setIconSize(QSize(16, 16));

        horizontalLayout->addWidget(f_undo);

        f_redo = new QToolButton(f_toolbar);
        f_redo->setObjectName(QString::fromUtf8("f_redo"));
        f_redo->setEnabled(false);
        f_redo->setFocusPolicy(Qt::ClickFocus);
        QIcon icon1;
        iconThemeName = QString::fromUtf8("edit-redo");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon1 = QIcon::fromTheme(iconThemeName);
        } else {
            icon1.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_redo->setIcon(icon1);
        f_redo->setIconSize(QSize(16, 16));

        horizontalLayout->addWidget(f_redo);

        f_cut = new QToolButton(f_toolbar);
        f_cut->setObjectName(QString::fromUtf8("f_cut"));
        f_cut->setFocusPolicy(Qt::ClickFocus);
        QIcon icon2;
        iconThemeName = QString::fromUtf8("edit-cut");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon2 = QIcon::fromTheme(iconThemeName);
        } else {
            icon2.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_cut->setIcon(icon2);
        f_cut->setIconSize(QSize(16, 16));

        horizontalLayout->addWidget(f_cut);

        f_copy = new QToolButton(f_toolbar);
        f_copy->setObjectName(QString::fromUtf8("f_copy"));
        f_copy->setFocusPolicy(Qt::ClickFocus);
        QIcon icon3;
        iconThemeName = QString::fromUtf8("edit-copy");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon3 = QIcon::fromTheme(iconThemeName);
        } else {
            icon3.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_copy->setIcon(icon3);
        f_copy->setIconSize(QSize(16, 16));

        horizontalLayout->addWidget(f_copy);

        f_paste = new QToolButton(f_toolbar);
        f_paste->setObjectName(QString::fromUtf8("f_paste"));
        f_paste->setFocusPolicy(Qt::ClickFocus);
        QIcon icon4;
        iconThemeName = QString::fromUtf8("edit-paste");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon4 = QIcon::fromTheme(iconThemeName);
        } else {
            icon4.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_paste->setIcon(icon4);
        f_paste->setIconSize(QSize(16, 16));

        horizontalLayout->addWidget(f_paste);

        line = new QFrame(f_toolbar);
        line->setObjectName(QString::fromUtf8("line"));
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Sunken);

        horizontalLayout->addWidget(line);

        f_link = new QToolButton(f_toolbar);
        f_link->setObjectName(QString::fromUtf8("f_link"));
        f_link->setFocusPolicy(Qt::ClickFocus);
        QIcon icon5;
        iconThemeName = QString::fromUtf8("applications-internet");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon5 = QIcon::fromTheme(iconThemeName);
        } else {
            icon5.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_link->setIcon(icon5);
        f_link->setIconSize(QSize(16, 16));
        f_link->setCheckable(true);

        horizontalLayout->addWidget(f_link);

        line_3 = new QFrame(f_toolbar);
        line_3->setObjectName(QString::fromUtf8("line_3"));
        line_3->setFrameShape(QFrame::VLine);
        line_3->setFrameShadow(QFrame::Sunken);

        horizontalLayout->addWidget(line_3);

        f_bold = new QToolButton(f_toolbar);
        f_bold->setObjectName(QString::fromUtf8("f_bold"));
        f_bold->setFocusPolicy(Qt::ClickFocus);
#ifndef QT_NO_TOOLTIP
        f_bold->setToolTip(QString::fromUtf8("Bold (CTRL+B)"));
#endif // QT_NO_TOOLTIP
        QIcon icon6;
        iconThemeName = QString::fromUtf8("format-text-bold");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon6 = QIcon::fromTheme(iconThemeName);
        } else {
            icon6.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_bold->setIcon(icon6);
        f_bold->setIconSize(QSize(16, 16));
        f_bold->setCheckable(true);

        horizontalLayout->addWidget(f_bold);

        f_italic = new QToolButton(f_toolbar);
        f_italic->setObjectName(QString::fromUtf8("f_italic"));
        f_italic->setFocusPolicy(Qt::ClickFocus);
        QIcon icon7;
        iconThemeName = QString::fromUtf8("format-text-italic");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon7 = QIcon::fromTheme(iconThemeName);
        } else {
            icon7.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_italic->setIcon(icon7);
        f_italic->setIconSize(QSize(16, 16));
        f_italic->setCheckable(true);

        horizontalLayout->addWidget(f_italic);

        f_underline = new QToolButton(f_toolbar);
        f_underline->setObjectName(QString::fromUtf8("f_underline"));
        f_underline->setFocusPolicy(Qt::ClickFocus);
        QIcon icon8;
        iconThemeName = QString::fromUtf8("format-text-underline");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon8 = QIcon::fromTheme(iconThemeName);
        } else {
            icon8.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_underline->setIcon(icon8);
        f_underline->setIconSize(QSize(16, 16));
        f_underline->setCheckable(true);

        horizontalLayout->addWidget(f_underline);

        f_strikeout = new QToolButton(f_toolbar);
        f_strikeout->setObjectName(QString::fromUtf8("f_strikeout"));
        f_strikeout->setCheckable(true);

        horizontalLayout->addWidget(f_strikeout);

        line_5 = new QFrame(f_toolbar);
        line_5->setObjectName(QString::fromUtf8("line_5"));
        line_5->setFrameShape(QFrame::VLine);
        line_5->setFrameShadow(QFrame::Sunken);

        horizontalLayout->addWidget(line_5);

        f_list_bullet = new QToolButton(f_toolbar);
        f_list_bullet->setObjectName(QString::fromUtf8("f_list_bullet"));
        f_list_bullet->setFocusPolicy(Qt::ClickFocus);
        f_list_bullet->setIconSize(QSize(16, 16));
        f_list_bullet->setCheckable(true);

        horizontalLayout->addWidget(f_list_bullet);

        f_list_ordered = new QToolButton(f_toolbar);
        f_list_ordered->setObjectName(QString::fromUtf8("f_list_ordered"));
        f_list_ordered->setFocusPolicy(Qt::ClickFocus);
        f_list_ordered->setIconSize(QSize(16, 16));
        f_list_ordered->setCheckable(true);

        horizontalLayout->addWidget(f_list_ordered);

        f_indent_dec = new QToolButton(f_toolbar);
        f_indent_dec->setObjectName(QString::fromUtf8("f_indent_dec"));
        f_indent_dec->setFocusPolicy(Qt::ClickFocus);
        QIcon icon9;
        iconThemeName = QString::fromUtf8("format-indent-less");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon9 = QIcon::fromTheme(iconThemeName);
        } else {
            icon9.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_indent_dec->setIcon(icon9);
        f_indent_dec->setIconSize(QSize(16, 16));

        horizontalLayout->addWidget(f_indent_dec);

        f_indent_inc = new QToolButton(f_toolbar);
        f_indent_inc->setObjectName(QString::fromUtf8("f_indent_inc"));
        f_indent_inc->setFocusPolicy(Qt::ClickFocus);
        QIcon icon10;
        iconThemeName = QString::fromUtf8("format-indent-more");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon10 = QIcon::fromTheme(iconThemeName);
        } else {
            icon10.addFile(QString::fromUtf8(""), QSize(), QIcon::Normal, QIcon::Off);
        }
        f_indent_inc->setIcon(icon10);
        f_indent_inc->setIconSize(QSize(16, 16));

        horizontalLayout->addWidget(f_indent_inc);

        line_2 = new QFrame(f_toolbar);
        line_2->setObjectName(QString::fromUtf8("line_2"));
        line_2->setFrameShape(QFrame::VLine);
        line_2->setFrameShadow(QFrame::Sunken);

        horizontalLayout->addWidget(line_2);

        f_bgcolor = new QToolButton(f_toolbar);
        f_bgcolor->setObjectName(QString::fromUtf8("f_bgcolor"));
        f_bgcolor->setMinimumSize(QSize(16, 16));
        f_bgcolor->setMaximumSize(QSize(16, 16));
        f_bgcolor->setFocusPolicy(Qt::ClickFocus);
        f_bgcolor->setIconSize(QSize(16, 16));

        horizontalLayout->addWidget(f_bgcolor);

        f_fontsize = new QComboBox(f_toolbar);
        f_fontsize->setObjectName(QString::fromUtf8("f_fontsize"));
        f_fontsize->setFocusPolicy(Qt::ClickFocus);
        f_fontsize->setEditable(true);

        horizontalLayout->addWidget(f_fontsize);

        line_6 = new QFrame(f_toolbar);
        line_6->setObjectName(QString::fromUtf8("line_6"));
        line_6->setFrameShape(QFrame::VLine);
        line_6->setFrameShadow(QFrame::Sunken);

        horizontalLayout->addWidget(line_6);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        f_paragraph->raise();
        line_4->raise();
        f_undo->raise();
        f_redo->raise();
        f_cut->raise();
        f_copy->raise();
        f_paste->raise();
        line->raise();
        f_link->raise();
        line_3->raise();
        f_italic->raise();
        f_underline->raise();
        line_2->raise();
        f_fontsize->raise();
        line_5->raise();
        f_list_bullet->raise();
        f_list_ordered->raise();
        f_indent_dec->raise();
        f_indent_inc->raise();
        f_bold->raise();
        f_bgcolor->raise();
        f_strikeout->raise();
        line_6->raise();

        verticalLayout->addWidget(f_toolbar);

        f_textedit = new QTextEdit(MRichTextEdit);
        f_textedit->setObjectName(QString::fromUtf8("f_textedit"));
        f_textedit->setAutoFormatting(QTextEdit::AutoNone);
        f_textedit->setTabChangesFocus(true);

        verticalLayout->addWidget(f_textedit);


        retranslateUi(MRichTextEdit);

        QMetaObject::connectSlotsByName(MRichTextEdit);
    } // setupUi

    void retranslateUi(QWidget *MRichTextEdit)
    {
#ifndef QT_NO_TOOLTIP
        f_paragraph->setToolTip(QApplication::translate("MRichTextEdit", "Paragraph formatting", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
#ifndef QT_NO_TOOLTIP
        f_undo->setToolTip(QApplication::translate("MRichTextEdit", "Undo (CTRL+Z)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_undo->setText(QApplication::translate("MRichTextEdit", "Undo", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_redo->setToolTip(QApplication::translate("MRichTextEdit", "Redo", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_redo->setText(QApplication::translate("MRichTextEdit", "Redo", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_cut->setToolTip(QApplication::translate("MRichTextEdit", "Cut (CTRL+X)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_cut->setText(QApplication::translate("MRichTextEdit", "Cut", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_copy->setToolTip(QApplication::translate("MRichTextEdit", "Copy (CTRL+C)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_copy->setText(QApplication::translate("MRichTextEdit", "Copy", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_paste->setToolTip(QApplication::translate("MRichTextEdit", "Paste (CTRL+V)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_paste->setText(QApplication::translate("MRichTextEdit", "Paste", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_link->setToolTip(QApplication::translate("MRichTextEdit", "Link (CTRL+L)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_link->setText(QApplication::translate("MRichTextEdit", "Link", 0, QApplication::UnicodeUTF8));
        f_bold->setText(QApplication::translate("MRichTextEdit", "Bold", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_italic->setToolTip(QApplication::translate("MRichTextEdit", "Italic (CTRL+I)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_italic->setText(QApplication::translate("MRichTextEdit", "Italic", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_underline->setToolTip(QApplication::translate("MRichTextEdit", "Underline (CTRL+U)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_underline->setText(QApplication::translate("MRichTextEdit", "Underline", 0, QApplication::UnicodeUTF8));
        f_strikeout->setText(QApplication::translate("MRichTextEdit", "Strike Out", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_list_bullet->setToolTip(QApplication::translate("MRichTextEdit", "Bullet list (CTRL+-)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_list_bullet->setText(QApplication::translate("MRichTextEdit", "Bullet list", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_list_ordered->setToolTip(QApplication::translate("MRichTextEdit", "Ordered list (CTRL+=)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_list_ordered->setText(QApplication::translate("MRichTextEdit", "Ordered list", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_indent_dec->setToolTip(QApplication::translate("MRichTextEdit", "Decrease indentation (CTRL+,)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_indent_dec->setText(QApplication::translate("MRichTextEdit", "Decrease indentation", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_indent_inc->setToolTip(QApplication::translate("MRichTextEdit", "Increase indentation (CTRL+.)", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_indent_inc->setText(QApplication::translate("MRichTextEdit", "Increase indentation", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_bgcolor->setToolTip(QApplication::translate("MRichTextEdit", "Text background color", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        f_bgcolor->setText(QApplication::translate("MRichTextEdit", ".", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        f_fontsize->setToolTip(QApplication::translate("MRichTextEdit", "Font size", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        Q_UNUSED(MRichTextEdit);
    } // retranslateUi

};

namespace Ui {
    class MRichTextEdit: public Ui_MRichTextEdit {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MRICHTEXTEDIT_H
