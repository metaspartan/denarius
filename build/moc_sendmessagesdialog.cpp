/****************************************************************************
** Meta object code from reading C++ file 'sendmessagesdialog.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/qt/sendmessagesdialog.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'sendmessagesdialog.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_SendMessagesDialog[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
      10,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: signature, parameters, type, tag, flags
      27,   20,   19,   19, 0x0a,
      37,   19,   19,   19, 0x0a,
      45,   19,   19,   19, 0x0a,
      54,   19,   19,   19, 0x0a,
      82,   19,   63,   19, 0x0a,
      93,   19,   19,   19, 0x0a,
     115,   19,   19,   19, 0x08,
     145,  139,   19,   19, 0x08,
     177,   19,   19,   19, 0x08,
     208,   19,   19,   19, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_SendMessagesDialog[] = {
    "SendMessagesDialog\0\0retval\0done(int)\0"
    "clear()\0reject()\0accept()\0SendMessagesEntry*\0"
    "addEntry()\0updateRemoveEnabled()\0"
    "on_sendButton_clicked()\0entry\0"
    "removeEntry(SendMessagesEntry*)\0"
    "on_addressBookButton_clicked()\0"
    "on_pasteButton_clicked()\0"
};

void SendMessagesDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        SendMessagesDialog *_t = static_cast<SendMessagesDialog *>(_o);
        switch (_id) {
        case 0: _t->done((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->clear(); break;
        case 2: _t->reject(); break;
        case 3: _t->accept(); break;
        case 4: { SendMessagesEntry* _r = _t->addEntry();
            if (_a[0]) *reinterpret_cast< SendMessagesEntry**>(_a[0]) = _r; }  break;
        case 5: _t->updateRemoveEnabled(); break;
        case 6: _t->on_sendButton_clicked(); break;
        case 7: _t->removeEntry((*reinterpret_cast< SendMessagesEntry*(*)>(_a[1]))); break;
        case 8: _t->on_addressBookButton_clicked(); break;
        case 9: _t->on_pasteButton_clicked(); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData SendMessagesDialog::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject SendMessagesDialog::staticMetaObject = {
    { &QDialog::staticMetaObject, qt_meta_stringdata_SendMessagesDialog,
      qt_meta_data_SendMessagesDialog, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &SendMessagesDialog::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *SendMessagesDialog::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *SendMessagesDialog::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_SendMessagesDialog))
        return static_cast<void*>(const_cast< SendMessagesDialog*>(this));
    return QDialog::qt_metacast(_clname);
}

int SendMessagesDialog::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    return _id;
}
QT_END_MOC_NAMESPACE
