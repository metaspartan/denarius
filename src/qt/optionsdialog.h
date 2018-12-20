#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QDialog>
#ifdef USE_NATIVE_I2P
#include "i2poptionswidget.h"
#endif

namespace Ui {
class OptionsDialog;
}
class OptionsModel;
class MonitoredDataMapper;
class QValidatedLineEdit;
#ifdef USE_NATIVE_I2P
class ClientModel;
#endif

/** Preferences dialog. */
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent = 0);
    ~OptionsDialog();
#ifdef USE_NATIVE_I2P
    void setClientModel(ClientModel* clientModel);
#endif

    void setModel(OptionsModel *model);
    void setMapper();

protected:
    bool eventFilter(QObject *object, QEvent *event);

private slots:
    /* enable only apply button */
    void enableApplyButton();
    /* disable only apply button */
    void disableApplyButton();
    /* enable apply button and OK button */
    void enableSaveButtons();
    /* disable apply button and OK button */
    void disableSaveButtons();
    /* set apply button and OK button state (enabled / disabled) */
    void setSaveButtonState(bool fState);
    void on_okButton_clicked();
    void on_cancelButton_clicked();
    void on_applyButton_clicked();
#ifdef USE_NATIVE_I2P
    void showRestartWarning_I2P();
#endif

    void showRestartWarning_Proxy();
    void showRestartWarning_Lang();
    void updateDisplayUnit();
    void handleProxyIpValid(QValidatedLineEdit *object, bool fState);

signals:
    void proxyIpValid(QValidatedLineEdit *object, bool fValid);

private:
    Ui::OptionsDialog *ui;
    OptionsModel *model;
    MonitoredDataMapper *mapper;
    bool fRestartWarningDisplayed_Proxy;
    bool fRestartWarningDisplayed_Lang;
    bool fProxyIpValid;
#ifdef USE_NATIVE_I2P
    bool fRestartWarningDisplayed_I2P;
    I2POptionsWidget* tabI2P;
#endif
};

#endif // OPTIONSDIALOG_H
