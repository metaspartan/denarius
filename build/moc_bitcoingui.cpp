/****************************************************************************
** Meta object code from reading C++ file 'bitcoingui.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../src/qt/bitcoingui.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'bitcoingui.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_ActiveLabel[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       1,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: signature, parameters, type, tag, flags
      13,   12,   12,   12, 0x05,

       0        // eod
};

static const char qt_meta_stringdata_ActiveLabel[] = {
    "ActiveLabel\0\0clicked()\0"
};

void ActiveLabel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        ActiveLabel *_t = static_cast<ActiveLabel *>(_o);
        switch (_id) {
        case 0: _t->clicked(); break;
        default: ;
        }
    }
    Q_UNUSED(_a);
}

const QMetaObjectExtraData ActiveLabel::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject ActiveLabel::staticMetaObject = {
    { &QLabel::staticMetaObject, qt_meta_stringdata_ActiveLabel,
      qt_meta_data_ActiveLabel, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &ActiveLabel::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *ActiveLabel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *ActiveLabel::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_ActiveLabel))
        return static_cast<void*>(const_cast< ActiveLabel*>(this));
    return QLabel::qt_metacast(_clname);
}

int ActiveLabel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QLabel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void ActiveLabel::clicked()
{
    QMetaObject::activate(this, &staticMetaObject, 0, 0);
}
static const uint qt_meta_data_BitcoinGUI[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
      35,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: signature, parameters, type, tag, flags
      18,   12,   11,   11, 0x0a,
      60,   41,   11,   11, 0x0a,
      89,   82,   11,   11, 0x0a,
     134,  114,   11,   11, 0x0a,
     182,  162,   11,   11, 0x0a,
     210,  203,   11,   11, 0x0a,
     241,  229,   11,   11, 0x0a,
     281,  229,   11,   11, 0x0a,
     326,   11,   11,   11, 0x08,
     345,   11,   11,   11, 0x08,
     366,   11,   11,   11, 0x08,
     385,   11,   11,   11, 0x08,
     403,   11,   11,   11, 0x08,
     425,   11,   11,   11, 0x08,
     448,   11,   11,   11, 0x08,
     468,   11,   11,   11, 0x08,
     491,  486,   11,   11, 0x08,
     519,   11,   11,   11, 0x28,
     540,  486,   11,   11, 0x08,
     570,   11,   11,   11, 0x28,
     593,   11,   11,   11, 0x08,
     610,   11,   11,   11, 0x08,
     632,  625,   11,   11, 0x08,
     702,  685,   11,   11, 0x08,
     743,  685,   11,   11, 0x08,
     780,   82,   11,   11, 0x08,
     800,   11,   11,   11, 0x08,
     815,   11,   11,   11, 0x08,
     834,   11,   11,   11, 0x08,
     849,   11,   11,   11, 0x08,
     876,  862,   11,   11, 0x08,
     904,   11,   11,   11, 0x28,
     928,   11,   11,   11, 0x08,
     943,   11,   11,   11, 0x08,
     958,   11,   11,   11, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_BitcoinGUI[] = {
    "BitcoinGUI\0\0count\0setNumConnections(int)\0"
    "count,nTotalBlocks\0setNumBlocks(int,int)\0"
    "status\0setEncryptionStatus(int)\0"
    "title,message,modal\0error(QString,QString,bool)\0"
    "nFeeRequired,payFee\0askFee(qint64,bool*)\0"
    "strURI\0handleURI(QString)\0orientation\0"
    "mainToolbarOrientation(Qt::Orientation)\0"
    "secondaryToolbarOrientation(Qt::Orientation)\0"
    "gotoOverviewPage()\0gotoStatisticsPage()\0"
    "gotoBlockBrowser()\0gotoHistoryPage()\0"
    "gotoAddressBookPage()\0gotoReceiveCoinsPage()\0"
    "gotoSendCoinsPage()\0gotoMessagePage()\0"
    "addr\0gotoSignMessageTab(QString)\0"
    "gotoSignMessageTab()\0gotoVerifyMessageTab(QString)\0"
    "gotoVerifyMessageTab()\0optionsClicked()\0"
    "aboutClicked()\0reason\0"
    "trayIconActivated(QSystemTrayIcon::ActivationReason)\0"
    "parent,start,end\0"
    "incomingTransaction(QModelIndex,int,int)\0"
    "incomingMessage(QModelIndex,int,int)\0"
    "encryptWallet(bool)\0backupWallet()\0"
    "changePassphrase()\0unlockWallet()\0"
    "lockWallet()\0fToggleHidden\0"
    "showNormalIfMinimized(bool)\0"
    "showNormalIfMinimized()\0toggleHidden()\0"
    "updateWeight()\0updateStakingIcon()\0"
};

void BitcoinGUI::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        BitcoinGUI *_t = static_cast<BitcoinGUI *>(_o);
        switch (_id) {
        case 0: _t->setNumConnections((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->setNumBlocks((*reinterpret_cast< int(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 2: _t->setEncryptionStatus((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 3: _t->error((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< bool(*)>(_a[3]))); break;
        case 4: _t->askFee((*reinterpret_cast< qint64(*)>(_a[1])),(*reinterpret_cast< bool*(*)>(_a[2]))); break;
        case 5: _t->handleURI((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 6: _t->mainToolbarOrientation((*reinterpret_cast< Qt::Orientation(*)>(_a[1]))); break;
        case 7: _t->secondaryToolbarOrientation((*reinterpret_cast< Qt::Orientation(*)>(_a[1]))); break;
        case 8: _t->gotoOverviewPage(); break;
        case 9: _t->gotoStatisticsPage(); break;
        case 10: _t->gotoBlockBrowser(); break;
        case 11: _t->gotoHistoryPage(); break;
        case 12: _t->gotoAddressBookPage(); break;
        case 13: _t->gotoReceiveCoinsPage(); break;
        case 14: _t->gotoSendCoinsPage(); break;
        case 15: _t->gotoMessagePage(); break;
        case 16: _t->gotoSignMessageTab((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 17: _t->gotoSignMessageTab(); break;
        case 18: _t->gotoVerifyMessageTab((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 19: _t->gotoVerifyMessageTab(); break;
        case 20: _t->optionsClicked(); break;
        case 21: _t->aboutClicked(); break;
        case 22: _t->trayIconActivated((*reinterpret_cast< QSystemTrayIcon::ActivationReason(*)>(_a[1]))); break;
        case 23: _t->incomingTransaction((*reinterpret_cast< const QModelIndex(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        case 24: _t->incomingMessage((*reinterpret_cast< const QModelIndex(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3]))); break;
        case 25: _t->encryptWallet((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 26: _t->backupWallet(); break;
        case 27: _t->changePassphrase(); break;
        case 28: _t->unlockWallet(); break;
        case 29: _t->lockWallet(); break;
        case 30: _t->showNormalIfMinimized((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 31: _t->showNormalIfMinimized(); break;
        case 32: _t->toggleHidden(); break;
        case 33: _t->updateWeight(); break;
        case 34: _t->updateStakingIcon(); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData BitcoinGUI::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject BitcoinGUI::staticMetaObject = {
    { &QMainWindow::staticMetaObject, qt_meta_stringdata_BitcoinGUI,
      qt_meta_data_BitcoinGUI, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &BitcoinGUI::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *BitcoinGUI::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *BitcoinGUI::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_BitcoinGUI))
        return static_cast<void*>(const_cast< BitcoinGUI*>(this));
    return QMainWindow::qt_metacast(_clname);
}

int BitcoinGUI::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 35)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 35;
    }
    return _id;
}
QT_END_MOC_NAMESPACE
