/****************************************************************************
** Meta object code from reading C++ file 'blockbrowser.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/qt/blockbrowser.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'blockbrowser.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_BlockBrowser[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: signature, parameters, type, tag, flags
      14,   13,   13,   13, 0x0a,
      29,   13,   13,   13, 0x0a,
      41,   13,   13,   13, 0x0a,

       0        // eod
};

static const char qt_meta_stringdata_BlockBrowser[] = {
    "BlockBrowser\0\0blockClicked()\0txClicked()\0"
    "updateExplorer(bool)\0"
};

void BlockBrowser::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        BlockBrowser *_t = static_cast<BlockBrowser *>(_o);
        switch (_id) {
        case 0: _t->blockClicked(); break;
        case 1: _t->txClicked(); break;
        case 2: _t->updateExplorer((*reinterpret_cast< bool(*)>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData BlockBrowser::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject BlockBrowser::staticMetaObject = {
    { &QWidget::staticMetaObject, qt_meta_stringdata_BlockBrowser,
      qt_meta_data_BlockBrowser, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &BlockBrowser::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *BlockBrowser::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *BlockBrowser::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_BlockBrowser))
        return static_cast<void*>(const_cast< BlockBrowser*>(this));
    return QWidget::qt_metacast(_clname);
}

int BlockBrowser::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    return _id;
}
QT_END_MOC_NAMESPACE
