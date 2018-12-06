#ifndef COINCONTROL_H
#define COINCONTROL_H

#include "core.h"

/** Coin Control Features. */
class CCoinControl
{
public:
    CTxDestination destChange;
    //! If false, allows unselected inputs, but requires all selected inputs be used
    bool fAllowOtherInputs;
    //! Includes watch only addresses which match the ISMINE_WATCH_SOLVABLE criteria
    bool fAllowWatchOnly;

    bool fSplitBlock;
    int nSplitBlock;

    CCoinControl()
    {
        SetNull();
    }

    void SetNull()
    {
        destChange = CNoDestination();
        setSelected.clear();
        fAllowOtherInputs = false;
        fAllowWatchOnly = false;
        fSplitBlock = false;
        nSplitBlock = 1;
    }

    bool HasSelected() const
    {
        return (setSelected.size() > 0);
    }

    bool IsSelected(const uint256& hash, unsigned int n) const
    {
        COutPoint outpt(hash, n);
        return (setSelected.count(outpt) > 0);
    }

    bool IsSelectedd(const COutPoint& output) const
    {
        return (setSelected.count(output) > 0);
    }

    void Select(COutPoint& output)
    {
        setSelected.insert(output);
    }

    void UnSelect(COutPoint& output)
    {
        setSelected.erase(output);
    }

    void UnSelectAll()
    {
        setSelected.clear();
    }

    void ListSelected(std::vector<COutPoint>& vOutpoints)
    {
        vOutpoints.assign(setSelected.begin(), setSelected.end());
    }

private:
    std::set<COutPoint> setSelected;

};

#endif // COINCONTROL_H
