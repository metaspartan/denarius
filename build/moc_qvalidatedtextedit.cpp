/****************************************************************************
** Meta object code from reading C++ file 'qvalidatedtextedit.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/qt/qvalidatedtextedit.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'qvalidatedtextedit.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_QValidatedTextEdit[] = {

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
      26,   20,   19,   19, 0x0a,
      51,   41,   19,   19, 0x0a,
      73,   19,   19,   19, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_QValidatedTextEdit[] = {
    "QValidatedTextEdit\0\0valid\0setValid(bool)\0"
    "errorText\0setErrorText(QString)\0"
    "markValid()\0"
};

void QValidatedTextEdit::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        QValidatedTextEdit *_t = static_cast<QValidatedTextEdit *>(_o);
        switch (_id) {
        case 0: _t->setValid((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 1: _t->setErrorText((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 2: _t->markValid(); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData QValidatedTextEdit::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject QValidatedTextEdit::staticMetaObject = {
    { &QPlainTextEdit::staticMetaObject, qt_meta_stringdata_QValidatedTextEdit,
      qt_meta_data_QValidatedTextEdit, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &QValidatedTextEdit::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *QValidatedTextEdit::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *QValidatedTextEdit::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_QValidatedTextEdit))
        return static_cast<void*>(const_cast< QValidatedTextEdit*>(this));
    return QPlainTextEdit::qt_metacast(_clname);
}

int QValidatedTextEdit::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QPlainTextEdit::qt_metacall(_c, _id, _a);
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
