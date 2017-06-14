/****************************************************************************
** Meta object code from reading C++ file 'sendmessagesentry.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/qt/sendmessagesentry.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'sendmessagesentry.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_SendMessagesEntry[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: signature, parameters, type, tag, flags
      25,   19,   18,   18, 0x05,

 // slots: signature, parameters, type, tag, flags
      65,   57,   18,   18, 0x0a,
      88,   18,   18,   18, 0x0a,
      96,   18,   18,   18, 0x08,
     122,   18,   18,   18, 0x08,
     153,   18,   18,   18, 0x08,
     186,  178,   18,   18, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_SendMessagesEntry[] = {
    "SendMessagesEntry\0\0entry\0"
    "removeEntry(SendMessagesEntry*)\0enabled\0"
    "setRemoveEnabled(bool)\0clear()\0"
    "on_deleteButton_clicked()\0"
    "on_addressBookButton_clicked()\0"
    "on_pasteButton_clicked()\0address\0"
    "on_sendTo_textChanged(QString)\0"
};

void SendMessagesEntry::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        SendMessagesEntry *_t = static_cast<SendMessagesEntry *>(_o);
        switch (_id) {
        case 0: _t->removeEntry((*reinterpret_cast< SendMessagesEntry*(*)>(_a[1]))); break;
        case 1: _t->setRemoveEnabled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->clear(); break;
        case 3: _t->on_deleteButton_clicked(); break;
        case 4: _t->on_addressBookButton_clicked(); break;
        case 5: _t->on_pasteButton_clicked(); break;
        case 6: _t->on_sendTo_textChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData SendMessagesEntry::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject SendMessagesEntry::staticMetaObject = {
    { &QFrame::staticMetaObject, qt_meta_stringdata_SendMessagesEntry,
      qt_meta_data_SendMessagesEntry, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &SendMessagesEntry::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *SendMessagesEntry::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *SendMessagesEntry::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_SendMessagesEntry))
        return static_cast<void*>(const_cast< SendMessagesEntry*>(this));
    return QFrame::qt_metacast(_clname);
}

int SendMessagesEntry::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void SendMessagesEntry::removeEntry(SendMessagesEntry * _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_END_MOC_NAMESPACE
