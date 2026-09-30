#ifndef LOGGING_H
#define LOGGING_H

#include <QLoggingCategory>
#include <QString>

/*
 * Log categories: info and above are on by default, debug with e.g.
 *   QT_LOGGING_RULES="atn.modbus.debug=true"
 */
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcConfig)
Q_DECLARE_LOGGING_CATEGORY(lcMqtt)
Q_DECLARE_LOGGING_CATEGORY(lcModbus)
Q_DECLARE_LOGGING_CATEGORY(lcRegulation)
Q_DECLARE_LOGGING_CATEGORY(lcWeather)
Q_DECLARE_LOGGING_CATEGORY(lcTelegram)

namespace Logging
{
    /* Message pattern and handler: every message goes to stderr */
    void install();

    /* Also write info and above to path, rotated at maxBytes keeping
     * path.1 .. path.<files>. Empty path: no file. */
    void enableFile(const QString &path, qint64 maxBytes, int files);
}

#endif // LOGGING_H
