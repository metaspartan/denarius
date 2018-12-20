#ifndef CLIENTMODEL_H
#define CLIENTMODEL_H

#include <QObject>

class OptionsModel;
class PeerTableModel;
class AddressTableModel;
class TransactionTableModel;
class CWallet;

QT_BEGIN_NAMESPACE
class QDateTime;
class QTimer;
QT_END_NAMESPACE

/** Model for Bitcoin network client. */
class ClientModel : public QObject
{
    Q_OBJECT
public:
    explicit ClientModel(OptionsModel *optionsModel, QObject *parent = 0);
    ~ClientModel();

    OptionsModel *getOptionsModel();
    PeerTableModel *getPeerTableModel();

    int getNumConnections() const;
    int getNumBlocks() const;
    int getNumBlocksAtStartup();

    quint64 getTotalBytesRecv() const;
    quint64 getTotalBytesSent() const;

    QDateTime getLastBlockDate() const;

    //! Return true if client connected to testnet
    bool isTestNet() const;

    //! Return true if client connected to Tor
    bool isNativeTor() const;

    //! Return true if core is doing initial block download
    bool inInitialBlockDownload() const;
    //! Return conservative estimate of total number of blocks, or 0 if unknown
    int getNumBlocksOfPeers() const;
    //! Return warnings to be displayed in status bar
    QString getStatusBarWarnings() const;

    QString formatFullVersion() const;
    QString formatBuildDate() const;
    QString clientName() const;
    QString formatClientStartupTime() const;
#ifdef USE_NATIVE_I2P
    QString formatI2PNativeFullVersion() const;
    int getNumI2PConnections() const;

    QString getPublicI2PKey() const;
    QString getPrivateI2PKey() const;
    bool isI2PAddressGenerated() const;
    bool isI2POnly() const;
    QString getB32Address(const QString& destination) const;
    void generateI2PDestination(QString& pub, QString& priv) const;
#endif

private:
    OptionsModel *optionsModel;
    PeerTableModel *peerTableModel;

    int cachedNumBlocks;
    int cachedNumBlocksOfPeers;

    int numBlocksAtStartup;

    QTimer *pollTimer;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();
signals:
    void numConnectionsChanged(int count);
#ifdef USE_NATIVE_I2P
    void numI2PConnectionsChanged(int count);
#endif
    void numBlocksChanged(int count, int countOfPeers);
    void bytesChanged(quint64 totalBytesIn, quint64 totalBytesOut);

    //! Asynchronous error notification
    void error(const QString &title, const QString &message, bool modal);

public slots:
    void updateTimer();
    void updateNumConnections(int numConnections);
    void updateNumBlocks(int newNumBlocks, int newNumBlocksOfPeers);
    void updateAlert(const QString &hash, int status);
#ifdef USE_NATIVE_I2P
    void updateNumI2PConnections(int numI2PConnections);
#endif
};

#endif // CLIENTMODEL_H
