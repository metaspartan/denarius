/****************************************************************************
** Meta object code from reading C++ file 'messagemodel.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/qt/messagemodel.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'messagemodel.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_MessageModel[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: signature, parameters, type, tag, flags
      34,   14,   13,   13, 0x05,

 // slots: signature, parameters, type, tag, flags
      67,   62,   13,   13, 0x0a,
      92,   62,   13,   13, 0x0a,
     123,   13,   13,   13, 0x0a,
     147,  140,   13,   13, 0x0a,

       0        // eod
};

static const char qt_meta_stringdata_MessageModel[] = {
    "MessageModel\0\0title,message,modal\0"
    "error(QString,QString,bool)\0smsg\0"
    "newMessage(SecMsgStored)\0"
    "newOutboxMessage(SecMsgStored)\0"
    "walletUnlocked()\0status\0"
    "setEncryptionStatus(int)\0"
};

void MessageModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        MessageModel *_t = static_cast<MessageModel *>(_o);
        switch (_id) {
        case 0: _t->error((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< bool(*)>(_a[3]))); break;
        case 1: _t->newMessage((*reinterpret_cast< const SecMsgStored(*)>(_a[1]))); break;
        case 2: _t->newOutboxMessage((*reinterpret_cast< const SecMsgStored(*)>(_a[1]))); break;
        case 3: _t->walletUnlocked(); break;
        case 4: _t->setEncryptionStatus((*reinterpret_cast< int(*)>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData MessageModel::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject MessageModel::staticMetaObject = {
    { &QAbstractTableModel::staticMetaObject, qt_meta_stringdata_MessageModel,
      qt_meta_data_MessageModel, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &MessageModel::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *MessageModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *MessageModel::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_MessageModel))
        return static_cast<void*>(const_cast< MessageModel*>(this));
    return QAbstractTableModel::qt_metacast(_clname);
}

int MessageModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractTableModel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void MessageModel::error(const QString & _t1, const QString & _t2, bool _t3)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)), const_cast<void*>(reinterpret_cast<const void*>(&_t3)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_END_MOC_NAMESPACE
