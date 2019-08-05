#include "clientmodel.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "peertablemodel.h"
#include "addresstablemodel.h"
#include "transactiontablemodel.h"

#include "alert.h"
#include "main.h"
#include "ui_interface.h"

#include <QDateTime>
#include <QTimer>

#ifdef USE_NATIVE_I2P
#include "i2p.h"
#endif

static const int64_t nClientStartupTime = GetTime();

ClientModel::ClientModel(OptionsModel *optionsModel, QObject *parent) :
    QObject(parent), optionsModel(optionsModel),
    cachedNumBlocks(0), cachedNumBlocksOfPeers(0), pollTimer(0)
{
    peerTableModel = new PeerTableModel(this);

    numBlocksAtStartup = -1;

    pollTimer = new QTimer(this);
	connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateTimer()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

ClientModel::~ClientModel()
{
    unsubscribeFromCoreSignals();
}

int ClientModel::getNumConnections() const
{
    return vNodes.size();
}

int ClientModel::getNumBlocks() const
{
    return nBestHeight;
}

int ClientModel::getNumBlocksAtStartup()
{
    if (numBlocksAtStartup == -1) numBlocksAtStartup = getNumBlocks();
    return numBlocksAtStartup;
}

quint64 ClientModel::getTotalBytesRecv() const
{
    return CNode::GetTotalBytesRecv();
}

quint64 ClientModel::getTotalBytesSent() const
{
    return CNode::GetTotalBytesSent();
}

QDateTime ClientModel::getLastBlockDate() const
{
    if (pindexBest)
        return QDateTime::fromTime_t(pindexBest->GetBlockTime());
    else
        return QDateTime::fromTime_t(1497476511); // D e n a r i u s - Genesis block's time
}

void ClientModel::updateTimer()
{
    // Get required lock upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
//    TRY_LOCK(cs_main, lockMain);
//    if(!lockMain)
//        return;

    // Some quantities (such as number of blocks) change so fast that we don't want to be notified for each change.
    // Periodically check and update with a timer.
    int newNumBlocks = getNumBlocks();
    int newNumBlocksOfPeers = getNumBlocksOfPeers();

    if(cachedNumBlocks != newNumBlocks || cachedNumBlocksOfPeers != newNumBlocksOfPeers)
    {
        cachedNumBlocks = newNumBlocks;
        cachedNumBlocksOfPeers = newNumBlocksOfPeers;

        emit numBlocksChanged(newNumBlocks, newNumBlocksOfPeers);
    }

    emit bytesChanged(getTotalBytesRecv(), getTotalBytesSent());
}

void ClientModel::updateNumBlocks(int newNumBlocks, int newNumBlocksOfPeers)
{
    // Get required lock upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
//    TRY_LOCK(cs_main, lockMain);
//    if(!lockMain)
//        return;

    emit numBlocksChanged(newNumBlocks, newNumBlocksOfPeers);
    emit bytesChanged(getTotalBytesRecv(), getTotalBytesSent());
}

void ClientModel::updateNumConnections(int numConnections)
{
    emit numConnectionsChanged(numConnections);
}

void ClientModel::updateAlert(const QString &hash, int status)
{
    // Show error message notification for new alert
    if(status == CT_NEW)
    {
        uint256 hash_256;
        hash_256.SetHex(hash.toStdString());
        CAlert alert = CAlert::getAlertByHash(hash_256);
        if(!alert.IsNull())
        {
            emit error(tr("Network Alert"), QString::fromStdString(alert.strStatusBar), false);
        }
    }

    // Emit a numBlocksChanged when the status message changes,
    // so that the view recomputes and updates the status bar.
    emit numBlocksChanged(getNumBlocks(), getNumBlocksOfPeers());
}

bool ClientModel::isTestNet() const
{
    return fTestNet;
}

bool ClientModel::isNativeTor() const
{
    return fNativeTor;
}

bool ClientModel::inInitialBlockDownload() const
{
    return IsInitialBlockDownload();
}

int ClientModel::getNumBlocksOfPeers() const
{
    return GetNumBlocksOfPeers();
}

QString ClientModel::getStatusBarWarnings() const
{
    return QString::fromStdString(GetWarnings("statusbar"));
}

OptionsModel *ClientModel::getOptionsModel()
{
    return optionsModel;
}

PeerTableModel *ClientModel::getPeerTableModel()
{
    return peerTableModel;
}

#ifdef USE_NATIVE_I2P
QString ClientModel::formatI2PNativeFullVersion() const
{
    return QString::fromStdString(FormatI2PNativeFullVersion());
}

void ClientModel::updateNumI2PConnections(int numI2PConnections)
{
    emit numI2PConnectionsChanged(numI2PConnections);
}

int ClientModel::getNumI2PConnections() const
{
    return nI2PNodeCount;
}

QString ClientModel::getPublicI2PKey() const
{
    return QString::fromStdString(I2PSession::Instance().getMyDestination().pub);
}

QString ClientModel::getPrivateI2PKey() const
{
    return QString::fromStdString(I2PSession::Instance().getMyDestination().priv);
}

bool ClientModel::isI2PAddressGenerated() const
{
    return I2PSession::Instance().getMyDestination().isGenerated;
}

bool ClientModel::isI2POnly() const
{
    return IsI2POnly();
}

QString ClientModel::getB32Address(const QString& destination) const
{
    return QString::fromStdString(I2PSession::GenerateB32AddressFromDestination(destination.toStdString()));
}

void ClientModel::generateI2PDestination(QString& pub, QString& priv) const
{
    const SAM::FullDestination generatedDest = I2PSession::Instance().destGenerate();
    pub = QString::fromStdString(generatedDest.pub);
    priv = QString::fromStdString(generatedDest.priv);
}

#endif

QString ClientModel::formatFullVersion() const
{
    return QString::fromStdString(FormatFullVersion());
}

QString ClientModel::formatBuildDate() const
{
    return QString::fromStdString(CLIENT_DATE);
}

QString ClientModel::clientName() const
{
    return QString::fromStdString(CLIENT_NAME);
}

QString ClientModel::formatClientStartupTime() const
{
    return QDateTime::fromTime_t(nClientStartupTime).toString();
}

#ifdef USE_NATIVE_I2P
static void NotifyNumI2PConnectionsChanged(ClientModel *clientmodel, int newNumI2PConnections)
{
    QMetaObject::invokeMethod(clientmodel, "updateNumI2PConnections", Qt::QueuedConnection,
                              Q_ARG(int, newNumI2PConnections));
}
#endif

// Handlers for core signals
static void NotifyBlocksChanged(ClientModel *clientmodel, int nHeight, int newNumBlocksOfPeers)
{
    // This notification is too frequent. Don't trigger a signal.
    // Don't remove it, though, as it might be useful later.
    QMetaObject::invokeMethod(clientmodel, "updateNumBlocks", Qt::QueuedConnection, Q_ARG(int, nHeight), Q_ARG(int, newNumBlocksOfPeers));

}

static void NotifyNumConnectionsChanged(ClientModel *clientmodel, int newNumConnections)
{
    // Too noisy: OutputDebugStringF("NotifyNumConnectionsChanged %i\n", newNumConnections);
    QMetaObject::invokeMethod(clientmodel, "updateNumConnections", Qt::QueuedConnection,
                              Q_ARG(int, newNumConnections));
}

static void NotifyAlertChanged(ClientModel *clientmodel, const uint256 &hash, ChangeType status)
{
    OutputDebugStringF("NotifyAlertChanged %s status=%i\n", hash.GetHex().c_str(), status);
    QMetaObject::invokeMethod(clientmodel, "updateAlert", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(hash.GetHex())),
                              Q_ARG(int, status));
}

void ClientModel::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.NotifyBlocksChanged.connect(boost::bind(NotifyBlocksChanged, this, _1, _2));
    uiInterface.NotifyNumConnectionsChanged.connect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyAlertChanged.connect(boost::bind(NotifyAlertChanged, this, _1, _2));
#ifdef USE_NATIVE_I2P
    uiInterface.NotifyNumI2PConnectionsChanged.connect(boost::bind(NotifyNumI2PConnectionsChanged, this, _1));
#endif
}

void ClientModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.NotifyBlocksChanged.disconnect(boost::bind(NotifyBlocksChanged, this, _1, _2));
    uiInterface.NotifyNumConnectionsChanged.disconnect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyAlertChanged.disconnect(boost::bind(NotifyAlertChanged, this, _1, _2));
#ifdef USE_NATIVE_I2P
    uiInterface.NotifyNumI2PConnectionsChanged.disconnect(boost::bind(NotifyNumI2PConnectionsChanged, this, _1));
#endif
}
