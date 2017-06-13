/****************************************************************************
** Meta object code from reading C++ file 'messagepage.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/qt/messagepage.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'messagepage.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_MessagePage[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
      12,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: signature, parameters, type, tag, flags
      13,   12,   12,   12, 0x0a,
      29,   12,   12,   12, 0x08,
      53,   12,   12,   12, 0x08,
      76,   12,   12,   12, 0x08,
     111,   12,   12,   12, 0x08,
     144,   12,   12,   12, 0x08,
     170,   12,   12,   12, 0x08,
     194,   12,   12,   12, 0x08,
     215,   12,   12,   12, 0x08,
     234,   12,   12,   12, 0x08,
     257,   12,   12,   12, 0x08,
     281,  275,   12,   12, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_MessagePage[] = {
    "MessagePage\0\0exportClicked()\0"
    "on_sendButton_clicked()\0on_newButton_clicked()\0"
    "on_copyFromAddressButton_clicked()\0"
    "on_copyToAddressButton_clicked()\0"
    "on_deleteButton_clicked()\0"
    "on_backButton_clicked()\0messageTextChanged()\0"
    "selectionChanged()\0itemSelectionChanged()\0"
    "incomingMessage()\0point\0contextualMenu(QPoint)\0"
};

void MessagePage::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        MessagePage *_t = static_cast<MessagePage *>(_o);
        switch (_id) {
        case 0: _t->exportClicked(); break;
        case 1: _t->on_sendButton_clicked(); break;
        case 2: _t->on_newButton_clicked(); break;
        case 3: _t->on_copyFromAddressButton_clicked(); break;
        case 4: _t->on_copyToAddressButton_clicked(); break;
        case 5: _t->on_deleteButton_clicked(); break;
        case 6: _t->on_backButton_clicked(); break;
        case 7: _t->messageTextChanged(); break;
        case 8: _t->selectionChanged(); break;
        case 9: _t->itemSelectionChanged(); break;
        case 10: _t->incomingMessage(); break;
        case 11: _t->contextualMenu((*reinterpret_cast< const QPoint(*)>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData MessagePage::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject MessagePage::staticMetaObject = {
    { &QWidget::staticMetaObject, qt_meta_stringdata_MessagePage,
      qt_meta_data_MessagePage, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &MessagePage::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *MessagePage::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *MessagePage::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_MessagePage))
        return static_cast<void*>(const_cast< MessagePage*>(this));
    return QWidget::qt_metacast(_clname);
}

int MessagePage::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    return _id;
}
QT_END_MOC_NAMESPACE
