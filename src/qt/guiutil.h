#ifndef GUIUTIL_H
#define GUIUTIL_H

#include <QString>

QT_BEGIN_NAMESPACE
class QFont;
class QLineEdit;
class QWidget;
class QDateTime;
class QUrl;
QT_END_NAMESPACE
class SendCoinsRecipient;

/** Static utility functions used by the Litecoin Qt UI.
 */
class GUIUtil
{
public:
    // Create human-readable string from date
    static QString dateTimeStr(qint64 nTime);
    static QString dateTimeStr(const QDateTime &datetime);

    // Render litecoin addresses in monospace font
    static QFont bitcoinAddressFont();

    // Set up widgets for address and amounts
    static void setupAddressWidget(QLineEdit *widget, QWidget *parent);
    static void setupAmountWidget(QLineEdit *widget, QWidget *parent);

    // Parse "litecoin:" URL into recipient object, return true on succesful parsing
    // See Litecoin URL definition discussion here: https://bitcointalk.org/index.php?topic=33490.0
    static bool parseBitcoinURL(const QUrl *url, SendCoinsRecipient *out);
};

#endif // GUIUTIL_H
