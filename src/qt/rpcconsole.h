#ifndef DENARIUS_QT_RPCCONSOLE_H
#define DENARIUS_QT_RPCCONSOLE_H

#include "guiutil.h"
#include "peertablemodel.h"

#include "net.h"

#include <QDialog>
#include <QCompleter>
#include <QThread>
#include <QWidget>

class QMenu;
class ClientModel;
class PlatformStyle;
class RPCTimerInterface;

namespace Ui {
    class RPCConsole;
}

QT_BEGIN_NAMESPACE
class QMenu;
class QItemSelection;
QT_END_NAMESPACE

/** Local Bitcoin RPC console. */
class RPCConsole: public QDialog
{
    Q_OBJECT

public:
    explicit RPCConsole(QWidget *parent = 0);
    ~RPCConsole();

    void setClientModel(ClientModel *model);

    enum MessageClass {
        MC_ERROR,
        MC_DEBUG,
        CMD_REQUEST,
        CMD_REPLY,
        CMD_ERROR
    };

	enum TabTypes {
        TAB_INFO = 0,
        TAB_CONSOLE = 1,
        TAB_GRAPH = 2,
        TAB_PEER = 3
    };

protected:
    virtual bool eventFilter(QObject* obj, QEvent *event);

private slots:
    void on_lineEdit_returnPressed();
    void on_tabWidget_currentChanged(int index);
    /** open the debug.log from the current datadir */
    void on_openDebugLogfileButton_clicked();
    /** display messagebox with program parameters (same as bitcoin-qt --help) */
    void on_showCLOptionsButton_clicked();

    /** change the time range of the network traffic graph */
    void on_sldGraphRange_valueChanged(int value);
    /** update traffic statistics */
    void updateTrafficStats(quint64 totalBytesIn, quint64 totalBytesOut);
    void resizeEvent(QResizeEvent *event);
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);
    /** Show custom context menu on Peers tab */
    void showPeersTableContextMenu(const QPoint& point);
    /** Show custom context menu on Bans tab */
    void showBanTableContextMenu(const QPoint& point);
    /** Hides ban table if no bans are present */
    void showOrHideBanTableIfRequired();
    /** clear the selected node */
    void clearSelectedNode();

public slots:
    void clear();
    void message(int category, const QString &message, bool html = false);
    /** Set number of connections shown in the UI */
    void setNumConnections(int count);
    /** Set number of blocks shown in the UI */
    void setNumBlocks(int count, int countOfPeers);
    /** Go forward or back in history */
    void browseHistory(int offset);
    /** Scroll console view to end */
    void scrollToEnd();
    /** Handle selection of peer in peers list */
    void peerSelected(const QItemSelection &selected, const QItemSelection &deselected);
    /** Handle selection caching before update */
    void peerLayoutAboutToChange();
    /** Handle updated peer information */
    void peerLayoutChanged();
     /** Disconnect a selected node on the Peers tab */
    void disconnectSelectedNode();
    /** Ban a selected node on the Peers tab */
    void banSelectedNode(int bantime);
    /** Unban a selected node on the Bans tab */
    void unbanSelectedNode();

    /** Wallet repair options */
    void walletSalvage();
    void walletRescan();
    void walletZaptxes1();
    void walletZaptxes2();
    void walletUpgrade();
    void walletReindex();

	/** set which tab has the focus (is visible) */
    void setTabFocus(enum TabTypes tabType);
signals:
    // For RPC command executor
    void stopExecutor();
    void cmdRequest(const QString &command);
    /** Get restart command-line parameters and handle restart */
    void handleRestart(QStringList args);

private:
    static QString FormatBytes(quint64 bytes);
    void setTrafficGraphRange(int mins);
    /** show detailed information on ui about selected node */
    void updateNodeDetail(const CNodeCombinedStats *stats);
    /** Update UI with latest network info from model. */
    void updateNetworkState();
    /** Build parameter list for restart */
    void buildParameterlist(QString arg);

    enum ColumnWidths
    {
        ADDRESS_COLUMN_WIDTH = 120,
        SUBVERSION_COLUMN_WIDTH = 110,
        PING_COLUMN_WIDTH = 40, // 120
        BYTES_COLUMN_WIDTH = 50, // 80
        BANSUBNET_COLUMN_WIDTH = 150,
        BANTIME_COLUMN_WIDTH = 300
    };

    Ui::RPCConsole *ui;
    ClientModel *clientModel;
    QList<NodeId> cachedNodeids;
    QString cmdBeforeBrowsing;
    QStringList history;
    NodeId cachedNodeid;
    int historyPtr;
    QCompleter *autoCompleter;
    QThread thread;
    QMenu* peersTableContextMenu;
    QMenu* banTableContextMenu;
    const PlatformStyle* platformStyle;
    RPCTimerInterface* rpcTimerInterface;
    void startExecutor();
};

#endif // RPCCONSOLE_H
