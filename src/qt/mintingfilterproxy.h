#ifndef MINTINGFILTERPROXY_H
#define MINTINGFILTERPROXY_H

#include <QSortFilterProxyModel>

class MintingFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit MintingFilterProxy(QObject *parent = 0);
    void setAddressPrefix(const QString &addrPrefix);

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex & source_parent) const;

private:
    QString addrPrefix;

};

#endif // MINTINGFILTERPROXY_H
