#include "managenamespage.h"
#include "ui_managenamespage.h"

#include "walletmodel.h"
#include "nametablemodel.h"
#include "csvmodelwriter.h"
#include "guiutil.h"
#include "base58.h"
#include "main.h"
#include "wallet.h"
#include "guiconstants.h"
#include "ui_interface.h"

#include <QSortFilterProxyModel>
#include <QMessageBox>
#include <QMenu>
#include <QScrollBar>
#include <QFileDialog>
#include <QTimer>

//
// NameFilterProxyModel
//

NameFilterProxyModel::NameFilterProxyModel(QObject *parent /* = 0*/)
    : QSortFilterProxyModel(parent)
{
}

void NameFilterProxyModel::setNameSearch(const QString &search)
{
    nameSearch = search;
    invalidateFilter();
}

void NameFilterProxyModel::setValueSearch(const QString &search)
{
    valueSearch = search;
    invalidateFilter();
}

void NameFilterProxyModel::setAddressSearch(const QString &search)
{
    addressSearch = search;
    invalidateFilter();
}

bool NameFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    QString name = index.sibling(index.row(), NameTableModel::Name).data(Qt::EditRole).toString();
    QString value = index.sibling(index.row(), NameTableModel::Value).data(Qt::EditRole).toString();
    QString address = index.sibling(index.row(), NameTableModel::Address).data(Qt::EditRole).toString();

    Qt::CaseSensitivity case_sens = filterCaseSensitivity();
    return name.contains(nameSearch, case_sens)
        && value.contains(valueSearch, case_sens)
        && address.startsWith(addressSearch, Qt::CaseSensitive);   // Address is always case-sensitive
}

bool NameFilterProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    NameTableEntry *rec1 = static_cast<NameTableEntry*>(left.internalPointer());
    NameTableEntry *rec2 = static_cast<NameTableEntry*>(right.internalPointer());

    switch (left.column())
    {
    case NameTableModel::Name:
        return QString::localeAwareCompare(rec1->name, rec2->name) < 0;
    case NameTableModel::Value:
        return QString::localeAwareCompare(rec1->value, rec2->value) < 0;
    case NameTableModel::Address:
        return QString::localeAwareCompare(rec1->address, rec2->address) < 0;
    case NameTableModel::ExpiresIn:
        return rec1->nExpiresAt < rec2->nExpiresAt;
    }

    // should never reach here
    return QString::localeAwareCompare(rec1->name, rec2->name) < 0;
}

//
// ManageNamesPage
//

const static int COLUMN_WIDTH_NAME = 300,
                 COLUMN_WIDTH_ADDRESS = 256,
                 COLUMN_WIDTH_EXPIRES_IN = 100;

ManageNamesPage::ManageNamesPage(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ManageNamesPage),
    model(0),
    walletModel(0),
    proxyModel(0)
{
    ui->setupUi(this);

    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableWidget->setSortingEnabled(true);

    timer = new QTimer(this);
    QTimer::singleShot(1000, this, SLOT(updateNames()));
    QTimer::singleShot(5000, this, SLOT(updateNames()));
    QTimer::singleShot(10000, this, SLOT(updateNames()));
    QTimer::singleShot(30000, this, SLOT(updateNames())); // try to load the name list ASAP for the user
	QTimer::singleShot(60000, this, SLOT(updateNames()));

    // Context menu actions
    QAction *copyNameAction = new QAction(tr("Copy &Name"), this);
    QAction *copyValueAction = new QAction(tr("Copy &Value"), this);
    QAction *copyAddressAction = new QAction(tr("Copy &Address"), this);
    QAction *copyAllAction = new QAction(tr("Copy all to edit boxes"), this);
    QAction *saveValueAsBinaryAction = new QAction(tr("Save value as binary file"), this);

    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyNameAction);
    contextMenu->addAction(copyValueAction);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyAllAction);
    contextMenu->addAction(saveValueAsBinaryAction);

    // Connect signals for context menu actions
    connect(copyNameAction, SIGNAL(triggered()), this, SLOT(onCopyNameAction()));
    connect(copyValueAction, SIGNAL(triggered()), this, SLOT(onCopyValueAction()));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(onCopyAddressAction()));
    connect(copyAllAction, SIGNAL(triggered()), this, SLOT(onCopyAllAction()));
    connect(saveValueAsBinaryAction, SIGNAL(triggered()), this, SLOT(onSaveValueAsBinaryAction()));
    connect(ui->refreshButton, SIGNAL(pressed()), this, SLOT( updateNames()));

    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // connect(ui->tableView_2, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
    // ui->tableView_2->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Reset gui sizes and visibility (for name new)
    ui->registerAddress->setEnabled(true);

    ui->tableWidget->setEnabled(true);

    // Catch focus changes to make the appropriate button the default one (Submit or Configure)
    ui->registerName->installEventFilter(this);
    ui->registerValue->installEventFilter(this);
    ui->txTypeSelector->installEventFilter(this);
    ui->submitNameButton->installEventFilter(this);
    ui->tableView->installEventFilter(this);
    ui->nameFilter->installEventFilter(this);
    ui->valueFilter->installEventFilter(this);
    ui->addressFilter->installEventFilter(this);

    ui->registerName->setMaxLength(MAX_NAME_LENGTH);

    ui->nameFilter->setMaxLength(MAX_NAME_LENGTH);
    ui->valueFilter->setMaxLength(GUI_MAX_VALUE_LENGTH);
    GUIUtil::setupAddressWidget(ui->addressFilter, this);

#if QT_VERSION >= 0x040700
    /* Do not move this to the XML file, Qt before 4.7 will choke on it */
    ui->nameFilter->setPlaceholderText(tr("Name filter"));
    ui->valueFilter->setPlaceholderText(tr("Value filter"));
    ui->addressFilter->setPlaceholderText(tr("Address filter"));
#endif

    ui->nameFilter->setFixedWidth(COLUMN_WIDTH_NAME);
    ui->addressFilter->setFixedWidth(COLUMN_WIDTH_ADDRESS);
    ui->horizontalSpacer_ExpiresIn->changeSize(
        COLUMN_WIDTH_EXPIRES_IN + ui->tableView->verticalScrollBar()->sizeHint().width()

#ifdef Q_OS_MAC
        // Not sure if this is needed, but other Mac code adds 2 pixels to scroll bar width;
        // see transactionview.cpp, search for verticalScrollBar()->sizeHint()
        + 2
#endif

        ,
        ui->horizontalSpacer_ExpiresIn->sizeHint().height(),
        QSizePolicy::Fixed);

}

ManageNamesPage::~ManageNamesPage()
{
    delete ui;
}

class SortedWidgetItem : public QTableWidgetItem
{
public:
    bool operator <( const QTableWidgetItem& other ) const
    {
        return (data(Qt::UserRole) < other.data(Qt::UserRole));
    }
};


void ManageNamesPage::updateNames()
{
    ui->countLabel->setText("Waiting for sync...");
    if (IsInitialBlockDownload()) return;
    int nameCount;

    ui->tableWidget->setSortingEnabled(false);
    ui->tableWidget->setUpdatesEnabled(false);
    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);

    QString nname;
    QString nvalue;
    int nexpire = 0;

    vector<unsigned char> vchNameUniq;
    CNameDB dbName("r");
    int nMax = 500;
    NameTxInfo nti;
    vector<pair<vector<unsigned char>, pair<CNameIndex,int> > > nameScan;

    if (!dbName.ScanNames(vchNameUniq, nMax, nameScan))
        throw JSONRPCError(RPC_WALLET_ERROR, "scan failed");

    pair<vector<unsigned char>, pair<CNameIndex,int> > pairScan;

    ui->countLabel->setText("Loaded Name DB...");
    BOOST_FOREACH(pairScan, nameScan)
    {
 
        bool bFound = false;
        int nodeRow = 0;
        for(int i=0; i < ui->tableWidget->rowCount(); i++)
        {
            if(ui->tableWidget->item(i, 0)->text() == QString::fromStdString(stringFromVch(pairScan.first)))
            {
                bFound = true;
                nodeRow = i;
                break;
            }
        }

        if(nodeRow == 0 && !bFound)
        {
            ui->tableWidget->insertRow(ui->tableWidget->rowCount());
            nodeRow = ui->tableWidget->rowCount()-1;
        }

        CNameIndex txName = pairScan.second.first;
        int nExpiresAt    = pairScan.second.second;
        // bool someMine = false;
        // string addy = "denarius";

        vector<unsigned char> vchValue = txName.vchValue;
        string value = stringFromVch(vchValue);

        nname = QString::fromStdString(stringFromVch(pairScan.first));
        nvalue =  QString::fromStdString(stringFromVch(vchValue));
        nexpire = nExpiresAt;

        QTableWidgetItem *nameItem = new QTableWidgetItem();
        nameItem->setData(Qt::DisplayRole, nname);
        SortedWidgetItem *expireItem = new SortedWidgetItem();
        expireItem->setData(Qt::DisplayRole, QString("%2").arg(nexpire));
        QTableWidgetItem *valueItem = new QTableWidgetItem();
        valueItem->setData(Qt::DisplayRole, nvalue);

        ui->tableWidget->setItem(nodeRow, 0, nameItem);
        ui->tableWidget->setItem(nodeRow, 1, expireItem);
        ui->tableWidget->setItem(nodeRow, 2, valueItem);
        // ui->countLabel->setText(QString("%1 names registered in total").arg(nameScan.size()));
    }
    ui->countLabel->setText(QString("%1 names registered in total").arg(nameScan.size()));
    ui->tableWidget->setSortingEnabled(true);
    ui->tableWidget->setUpdatesEnabled(true);
}

void ManageNamesPage::setModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    model = walletModel->getNameTableModel();

    proxyModel = new NameFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);

    ui->tableView->horizontalHeader()->setHighlightSections(false);

    // Set column widths
    ui->tableView->horizontalHeader()->resizeSection(
            NameTableModel::Name, COLUMN_WIDTH_NAME);
    ui->tableView->horizontalHeader()->setResizeMode(
            NameTableModel::Value, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->resizeSection(
            NameTableModel::Address, COLUMN_WIDTH_ADDRESS);
    ui->tableView->horizontalHeader()->resizeSection(
            NameTableModel::ExpiresIn, COLUMN_WIDTH_EXPIRES_IN);

    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
            this, SLOT(selectionChanged()));

    connect(ui->nameFilter, SIGNAL(textChanged(QString)), this, SLOT(changedNameFilter(QString)));
    connect(ui->valueFilter, SIGNAL(textChanged(QString)), this, SLOT(changedValueFilter(QString)));
    connect(ui->addressFilter, SIGNAL(textChanged(QString)), this, SLOT(changedAddressFilter(QString)));

    selectionChanged();
}

void ManageNamesPage::changedNameFilter(const QString &filter)
{
    if (!proxyModel)
        return;
    proxyModel->setNameSearch(filter);
}

void ManageNamesPage::changedValueFilter(const QString &filter)
{
    if (!proxyModel)
        return;
    proxyModel->setValueSearch(filter);
}

void ManageNamesPage::changedAddressFilter(const QString &filter)
{
    if (!proxyModel)
        return;
    proxyModel->setAddressSearch(filter);
}

//TODO finish this
void ManageNamesPage::on_submitNameButton_clicked()
{
    if (!walletModel)
        return;

    QString name = ui->registerName->text();
    vector<unsigned char> vchValue;  // byte-by-byte value, as is
    QString displayValue;            // for displaying value as unicode string

    if (ui->registerValue->isEnabled())
    {
        displayValue = ui->registerValue->toPlainText();
        string strValue = displayValue.toStdString();
        vchValue.assign(strValue.begin(), strValue.end());
    }
    else
    {
        vchValue = importedAsBinaryFile;
        displayValue = QString::fromStdString(stringFromVch(vchValue));
    }

    int days = ui->registerDays->text().toInt();
    QString txType = ui->txTypeSelector->currentText();
    QString newAddress = ui->registerAddress->text();
    QString address = ui->registerAddress->text();
    if (txType == "name_update")
        newAddress = ui->registerAddress->text();
    if (txType == "name_new")
        address = ui->registerAddress->text();

    if (name == "")
    {
        QMessageBox::critical(this, tr("Name is empty"), tr("Enter name please"));
        return;
    }

    if (vchValue.empty() && (txType == "name_new" || txType == "name_update"))
    {
        QMessageBox::critical(this, tr("Value is empty"), tr("Enter value please"));
        return;
    }

    // TODO: name needs more exhaustive syntax checking, Unicode characters etc.
    // TODO: maybe it should be done while the user is typing (e.g. show/hide a red notice below the input box)
    if (name != name.simplified() || name.contains(" "))
    {
        if (QMessageBox::Yes != QMessageBox::warning(this, tr("Name registration warning"),
              tr("The name you entered contains whitespace characters. Are you sure you want to use this name?"),
              QMessageBox::Yes | QMessageBox::Cancel,
              QMessageBox::Cancel))
        {
            return;
        }
    }

    int64_t txFee = MIN_TX_FEE;
    {
        string strName = name.toStdString();
        vector<unsigned char> vchName(strName.begin(), strName.end());

        if (txType == "name_new")
            txFee = GetNameOpFee(pindexBest, days, OP_NAME_NEW, vchName, vchValue);
        else if (txType == "name_update")
            txFee = GetNameOpFee(pindexBest, days, OP_NAME_UPDATE, vchName, vchValue);
    }

    if (QMessageBox::Yes != QMessageBox::question(this, tr("Confirm name registration"),
          tr("This will issue a %1. TX Fee is AT LEAST %2 D.").arg(txType).arg(txFee / (float)COIN, 0, 'f', 2),
          QMessageBox::Yes | QMessageBox::Cancel,
          QMessageBox::Cancel))
    {
        return;
    }

    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid())
        return;

    QString err_msg;

    try
    {
        NameTxReturn res;
        int nHeight;
        ChangeType status;
        if (txType == "name_new")
        {
            nHeight = NameTableEntry::NAME_NEW;
            status = CT_NEW;
            res = walletModel->nameNew(name, vchValue, days, address);
        }
        else if (txType == "name_update")
        {
            nHeight = NameTableEntry::NAME_UPDATE;
            status = CT_UPDATED;
            res = walletModel->nameUpdate(name, vchValue, days, newAddress);
        }
        else if (txType == "name_delete")
        {
            nHeight = NameTableEntry::NAME_DELETE;
            status = CT_UPDATED; //we still want to display this name until it is deleted
            res = walletModel->nameDelete(name);
        }

        importedAsBinaryFile.clear();
        importedAsTextFile.clear();

        if (res.ok)
        {
            ui->registerName->setText("");
            ui->registerValue->setEnabled(true);
            ui->registerValue->setPlainText("");
            ui->submitNameButton->setDefault(true); // EvgenijM86: is this realy needed here?

            int newRowIndex;
            // FIXME: CT_NEW may have been sent from nameNew (via transaction).
            // Currently updateEntry is modified so it does not complain
            model->updateEntry(name, displayValue, QString::fromStdString(res.address), nHeight, status, &newRowIndex);
            ui->tableView->selectRow(newRowIndex);
            ui->tableView->setFocus();
            return;
        }

        err_msg = QString::fromStdString(res.err_msg);
    }
    catch (std::exception& e)
    {
        err_msg = e.what();
    }

    if (err_msg == "ABORTED")
        return;

    QMessageBox::warning(this, tr("Name registration failed"), err_msg);
}

bool ManageNamesPage::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
    {
        if (object == ui->registerName || object == ui->submitNameButton)
        {
            ui->submitNameButton->setDefault(true);
        }
        else if (object == ui->tableView)
        {
            ui->submitNameButton->setDefault(false);
        }
    }
    return QDialog::eventFilter(object, event);
}

void ManageNamesPage::selectionChanged()
{
    // Set button states based on selected tab and selection
//    QTableView *table = ui->tableView;
//    if(!table->selectionModel())
//        return;
}

void ManageNamesPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if (index.isValid())
        contextMenu->exec(QCursor::pos());
}

void ManageNamesPage::onCopyNameAction()
{
    GUIUtil::copyEntryData(ui->tableView, NameTableModel::Name);
}

void ManageNamesPage::onCopyValueAction()
{
    GUIUtil::copyEntryData(ui->tableView, NameTableModel::Value);
}

void ManageNamesPage::onCopyAddressAction()
{
    GUIUtil::copyEntryData(ui->tableView, NameTableModel::Address);
}

void ManageNamesPage::onCopyAllAction()
{
    if(!ui->tableView || !ui->tableView->selectionModel())
        return;

    QModelIndexList selection;

    selection = ui->tableView->selectionModel()->selectedRows(NameTableModel::Name);
    if (!selection.isEmpty())
        ui->registerName->setText(selection.at(0).data(Qt::EditRole).toString());

    selection = ui->tableView->selectionModel()->selectedRows(NameTableModel::Value);
    if (!selection.isEmpty())
        ui->registerValue->setPlainText(selection.at(0).data(Qt::EditRole).toString());

    selection = ui->tableView->selectionModel()->selectedRows(NameTableModel::Address);
    if (!selection.isEmpty())
        ui->registerAddress->setText(selection.at(0).data(Qt::EditRole).toString());
}

void ManageNamesPage::onSaveValueAsBinaryAction()
{
    if(!ui->tableView || !ui->tableView->selectionModel())
        return;

// get name and value
    QModelIndexList selection;
    selection = ui->tableView->selectionModel()->selectedRows(NameTableModel::Name);
    if (selection.isEmpty())
        return;

    vector<unsigned char> vchName;
    {
        QString tmpName1 = selection.at(0).data(Qt::EditRole).toString();
        string tmpName2 = tmpName1.toStdString();
        vchName.assign(tmpName2.begin(), tmpName2.end());
    }

    vector<unsigned char> vchValue;
    GetNameValue(vchName, vchValue, true);


// select file and save value
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export File"), QDir::homePath(), tr("Files (*)"));
    QFile file(fileName);

    if (!file.open(QIODevice::WriteOnly))
        return;

    QDataStream in(&file);
    BOOST_FOREACH(const unsigned char& uch, vchValue)
        in << uch;
    file.close();
}

void ManageNamesPage::exportClicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Registered Names Data"), QString(),
            tr("Comma separated file (*.csv)"));

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);
    writer.setModel(proxyModel);
    // name, column, role
    writer.addColumn("Name", NameTableModel::Name, Qt::EditRole);
    writer.addColumn("Value", NameTableModel::Value, Qt::EditRole);
    writer.addColumn("Address", NameTableModel::Address, Qt::EditRole);
    writer.addColumn("Expires In", NameTableModel::ExpiresIn, Qt::EditRole);

    if(!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}

void ManageNamesPage::on_txTypeSelector_currentIndexChanged(const QString &txType)
{
    if (txType == "name_new")
    {
        ui->registerDays->setEnabled(true);
        ui->registerAddress->setEnabled(true);
        ui->registerValue->setEnabled(true);
    }
    else if (txType == "name_update")
    {
        ui->registerDays->setEnabled(true);
        ui->registerAddress->setEnabled(true);
        ui->registerValue->setEnabled(true);
    }
    else if (txType == "name_delete")
    {
        ui->registerDays->setDisabled(true);
        ui->registerAddress->setDisabled(true);
        ui->registerValue->setDisabled(true);
    }
    return;
}

void ManageNamesPage::on_cbMyNames_stateChanged(int arg1)
{
    model->fMyNames = true;
    model->update(true);
}

void ManageNamesPage::on_cbOtherNames_stateChanged(int arg1)
{
    model->fOtherNames = true;
    model->update(true);
}

void ManageNamesPage::on_cbExpired_stateChanged(int arg1)
{
    model->fExpired = true;
    model->update(true);
}

void ManageNamesPage::on_importValueButton_clicked()
{
    if (ui->registerValue->isEnabled() == false)
    {
        ui->registerValue->setEnabled(true);
        ui->registerValue->setPlainText(importedAsTextFile);
        return;
    }


    QString fileName = QFileDialog::getOpenFileName(this, tr("Import File"), QDir::homePath(), tr("Files (*)"));

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) return;
    QByteArray blob = file.readAll();
    file.close();

    if (blob.size() > MAX_VALUE_LENGTH)
    {
        QMessageBox::critical(this, tr("Value too large!"), tr("Value is larger than maximum size: %1 bytes > %2 bytes").arg(importedAsBinaryFile.size()).arg(MAX_VALUE_LENGTH));
        return;
    }

    // save textual and binary representation
    importedAsBinaryFile.clear();
    importedAsBinaryFile.reserve(blob.size());
    for (int i = 0; i < blob.size(); ++i)
        importedAsBinaryFile.push_back(blob.at(i));
    importedAsTextFile = QString::fromStdString(stringFromVch(importedAsBinaryFile));

    ui->registerValue->setDisabled(true);
    ui->registerValue->setPlainText(tr(
        "Currently file %1 is imported as binary (byte by byte) into name value. "
        "If you wish to import file as unicode string then click on Import buttton again. "
        "If you import file as unicode string its data may weight more than original file did."
        ).arg(fileName));
}

void ManageNamesPage::on_registerValue_textChanged()
{
    float byteSize;
    if (ui->registerValue->isEnabled())
    {
        string strValue = ui->registerValue->toPlainText().toStdString();
        vector<unsigned char> vchValue(strValue.begin(), strValue.end());
        byteSize = vchValue.size();
    }
    else
        byteSize = importedAsBinaryFile.size();

    ui->labelValue->setText(tr("value(%1%)").arg(int(100 * byteSize / MAX_VALUE_LENGTH)));
}
