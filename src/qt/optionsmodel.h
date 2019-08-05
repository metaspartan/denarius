#ifndef OPTIONSMODEL_H
#define OPTIONSMODEL_H

#include <QAbstractListModel>

/** Interface from Qt to configuration data structure for Bitcoin client.
   To Qt, the options are presented as a list with the different options
   laid out vertically.
   This can be changed to a tree once the settings become sufficiently
   complex.
 */
class OptionsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit OptionsModel(QObject *parent = 0);

    enum OptionID {
        StartAtStartup,    // bool
        MinimizeToTray,    // bool
        MapPortUPnP,       // bool
        MinimizeOnClose,   // bool
        ProxyUse,          // bool
        ProxyIP,           // QString
        ProxyPort,         // int
        ProxySocksVersion, // int
        Fee,               // qint64
        ReserveBalance,    // qint64
        DisplayUnit,       // BitcoinUnits::Unit
        DisplayAddresses,  // bool
        DetachDatabases,   // bool
        Language,          // QString
        CoinControlFeatures, // bool
#ifdef USE_NATIVE_I2P
        I2PUseI2POnly,              // bool
        I2PSAMHost,                 // QString
        I2PSAMPort,                 // int
        I2PSessionName,             // QString

        I2PInboundQuantity,         // int
        I2PInboundLength,           // int
        I2PInboundLengthVariance,   // int
        I2PInboundBackupQuantity,   // int
        I2PInboundAllowZeroHop,     // bool
        I2PInboundIPRestriction,    // int

        I2POutboundQuantity,        // int
        I2POutboundLength,          // int
        I2POutboundLengthVariance,  // int
        I2POutboundBackupQuantity,  // int
        I2POutboundAllowZeroHop,    // bool
        I2POutboundIPRestriction,   // int
        I2POutboundPriority,        // int
#endif
        OptionIDRowCount,
    };

    void Init();

    int rowCount(const QModelIndex & parent = QModelIndex()) const;
    QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const;
    bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole);

    /* Explicit getters */
    qint64 getTransactionFee();
    qint64 getReserveBalance();
    bool getMinimizeToTray();
    bool getMinimizeOnClose();
    int getDisplayUnit();
    bool getDisplayAddresses();
    bool getCoinControlFeatures();
    QString getLanguage() { return language; }

private:
    int nDisplayUnit;
    bool bDisplayAddresses;
    bool fMinimizeToTray;
    bool fMinimizeOnClose;
    bool fCoinControlFeatures;
    QString language;
#ifdef USE_NATIVE_I2P
    int i2pInboundQuantity;
    int i2pInboundLength;
    int i2pInboundLengthVariance;
    int i2pInboundBackupQuantity;
    bool i2pInboundAllowZeroHop;
    int i2pInboundIPRestriction;
    int i2pOutboundQuantity;
    int i2pOutboundLength;
    int i2pOutboundLengthVariance;
    int i2pOutboundBackupQuantity;
    bool i2pOutboundAllowZeroHop;
    int i2pOutboundIPRestriction;
    int i2pOutboundPriority;
    QString i2pOptions;
#endif

signals:
    void displayUnitChanged(int unit);
    void transactionFeeChanged(qint64);
    void reserveBalanceChanged(qint64);
    void coinControlFeaturesChanged(bool);
};

#endif // OPTIONSMODEL_H
