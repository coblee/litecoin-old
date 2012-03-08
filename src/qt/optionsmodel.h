#ifndef OPTIONSMODEL_H
#define OPTIONSMODEL_H

#include <QAbstractListModel>

class CWallet;

/** Interface from QT to configuration data structure for litecoin client.
   To QT, the options are presented as a list with the different options
   laid out vertically.
   This can be changed to a tree once the settings become sufficiently
   complex.
 */
class OptionsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit OptionsModel(CWallet *wallet, QObject *parent = 0);

    enum OptionID {
        StartAtStartup, // bool
        MinimizeToTray, // bool
        MapPortUPnP, // bool
        MinimizeOnClose, // bool
        ConnectSOCKS4, // bool
        ProxyIP, // QString
        ProxyPort, // QString
        Fee, // qint64
        DisplayUnit, // BitcoinUnits::Unit
        DisplayAddresses, // bool
        OptionIDRowCount
    };

    int rowCount(const QModelIndex & parent = QModelIndex()) const;
    QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const;
    bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole);

    CWallet* getWallet();
    /* Explicit getters */
    qint64 getTransactionFee();
    bool getMinimizeToTray();
    bool getMinimizeOnClose();
    int getDisplayUnit();
    bool getDisplayAddresses();
private:
    // Wallet stores persistent options
    CWallet *wallet;
    int nDisplayUnit;
    bool bDisplayAddresses;
signals:
    void displayUnitChanged(int unit);

public slots:

};

#endif // OPTIONSMODEL_H
