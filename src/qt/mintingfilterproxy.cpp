#include "mintingfilterproxy.h"
#include "mintingtablemodel.h"

MintingFilterProxy::MintingFilterProxy(QObject * parent) :
    QSortFilterProxyModel(parent),
    addrPrefix()
{

}

bool MintingFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    QString address = index.data(MintingTableModel::AddressRole).toString();

    if (!address.contains(addrPrefix, Qt::CaseInsensitive))
        return false;

    return true;
}

void MintingFilterProxy::setAddressPrefix(const QString &addrPrefix)
{
    this->addrPrefix = addrPrefix;
    invalidateFilter();
}
