/****************************************************************************
** Meta object code from reading C++ file 'mrichtextedit.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/qt/plugins/mrichtexteditor/mrichtextedit.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mrichtextedit.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_MRichTextEdit[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
      19,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: signature, parameters, type, tag, flags
      20,   15,   14,   14, 0x0a,
      37,   14,   14,   14, 0x0a,
      45,   15,   14,   14, 0x09,
      67,   15,   14,   14, 0x09,
      84,   14,   14,   14, 0x09,
      95,   14,   14,   14, 0x09,
     111,   14,   14,   14, 0x09,
     127,   14,   14,   14, 0x09,
     142,  140,   14,   14, 0x09,
     168,  160,   14,   14, 0x09,
     189,  183,   14,   14, 0x09,
     204,   14,   14,   14, 0x09,
     218,  160,   14,   14, 0x09,
     235,  160,   14,   14, 0x09,
     260,  253,   14,   14, 0x09,
     306,   14,   14,   14, 0x09,
     334,   14,   14,   14, 0x09,
     361,   14,   14,   14, 0x09,
     383,   14,   14,   14, 0x09,

       0        // eod
};

static const char qt_meta_stringdata_MRichTextEdit[] = {
    "MRichTextEdit\0\0text\0setText(QString)\0"
    "clear()\0setPlainText(QString)\0"
    "setHtml(QString)\0textBold()\0textUnderline()\0"
    "textStrikeout()\0textItalic()\0p\0"
    "textSize(QString)\0checked\0textLink(bool)\0"
    "index\0textStyle(int)\0textBgColor()\0"
    "listBullet(bool)\0listOrdered(bool)\0"
    "format\0slotCurrentCharFormatChanged(QTextCharFormat)\0"
    "slotCursorPositionChanged()\0"
    "slotClipboardDataChanged()\0"
    "increaseIndentation()\0decreaseIndentation()\0"
};

void MRichTextEdit::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        MRichTextEdit *_t = static_cast<MRichTextEdit *>(_o);
        switch (_id) {
        case 0: _t->setText((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 1: _t->clear(); break;
        case 2: _t->setPlainText((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: _t->setHtml((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->textBold(); break;
        case 5: _t->textUnderline(); break;
        case 6: _t->textStrikeout(); break;
        case 7: _t->textItalic(); break;
        case 8: _t->textSize((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 9: _t->textLink((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 10: _t->textStyle((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 11: _t->textBgColor(); break;
        case 12: _t->listBullet((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 13: _t->listOrdered((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 14: _t->slotCurrentCharFormatChanged((*reinterpret_cast< const QTextCharFormat(*)>(_a[1]))); break;
        case 15: _t->slotCursorPositionChanged(); break;
        case 16: _t->slotClipboardDataChanged(); break;
        case 17: _t->increaseIndentation(); break;
        case 18: _t->decreaseIndentation(); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData MRichTextEdit::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject MRichTextEdit::staticMetaObject = {
    { &QWidget::staticMetaObject, qt_meta_stringdata_MRichTextEdit,
      qt_meta_data_MRichTextEdit, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &MRichTextEdit::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *MRichTextEdit::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *MRichTextEdit::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_MRichTextEdit))
        return static_cast<void*>(const_cast< MRichTextEdit*>(this));
    if (!strcmp(_clname, "Ui::MRichTextEdit"))
        return static_cast< Ui::MRichTextEdit*>(const_cast< MRichTextEdit*>(this));
    return QWidget::qt_metacast(_clname);
}

int MRichTextEdit::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 19)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 19;
    }
    return _id;
}
QT_END_MOC_NAMESPACE
