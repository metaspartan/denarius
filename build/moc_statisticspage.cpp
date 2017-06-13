/****************************************************************************
** Meta object code from reading C++ file 'statisticspage.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/qt/statisticspage.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'statisticspage.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_StatisticsPage[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       2,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: signature, parameters, type, tag, flags
      16,   15,   15,   15, 0x0a,
      46,   35,   15,   15, 0x0a,

       0        // eod
};

static const char qt_meta_stringdata_StatisticsPage[] = {
    "StatisticsPage\0\0updateStatistics()\0"
    ",,,,,,,,,,\0"
    "updatePrevious(int,int,int,QString,QString,double,double,double,QStrin"
    "g,int,int)\0"
};

void StatisticsPage::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        StatisticsPage *_t = static_cast<StatisticsPage *>(_o);
        switch (_id) {
        case 0: _t->updateStatistics(); break;
        case 1: _t->updatePrevious((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3])),(*reinterpret_cast< QString(*)>(_a[4])),(*reinterpret_cast< QString(*)>(_a[5])),(*reinterpret_cast< double(*)>(_a[6])),(*reinterpret_cast< double(*)>(_a[7])),(*reinterpret_cast< double(*)>(_a[8])),(*reinterpret_cast< QString(*)>(_a[9])),(*reinterpret_cast< int(*)>(_a[10])),(*reinterpret_cast< int(*)>(_a[11]))); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData StatisticsPage::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject StatisticsPage::staticMetaObject = {
    { &QWidget::staticMetaObject, qt_meta_stringdata_StatisticsPage,
      qt_meta_data_StatisticsPage, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &StatisticsPage::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *StatisticsPage::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *StatisticsPage::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_StatisticsPage))
        return static_cast<void*>(const_cast< StatisticsPage*>(this));
    return QWidget::qt_metacast(_clname);
}

int StatisticsPage::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    return _id;
}
QT_END_MOC_NAMESPACE
