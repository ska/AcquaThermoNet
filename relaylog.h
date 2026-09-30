#ifndef RELAYLOG_H
#define RELAYLOG_H

#include <QDate>
#include <QFile>
#include <QMap>
#include <QString>

/*
 * Relay activity log for statistics and charts: one CSV line per relay
 * state change, one file per month (<base>-YYYY-MM.csv).
 *
 *   time,epoch,relay,zones,state,reason,on_s
 *   2026-09-29T14:00:05+02:00,1790596805,5,salotto,ON,regulation,
 *   2026-09-29T14:42:10+02:00,1790599330,5,salotto,OFF,regulation,2525
 *
 * on_s: on OFF lines, how long the relay was ON (monotonic clock, not
 * affected by wall clock jumps). Empty when the ON was not seen.
 */
class RelayLog
{
public:
    static const int DEFAULT_KEEP_MONTHS = 24;

    /* basePath "log/relays.csv" -> "log/relays-2026-09.csv" */
    explicit RelayLog(const QString &basePath, int keepMonths = DEFAULT_KEEP_MONTHS);

    /* Logged only when the relay state changes (or is not known yet) */
    void record(int relay, const QString &zones, bool on, const QString &reason);

    /* File for the given month */
    QString fileFor(const QDate &month) const;

    /* ON seconds per "zones" between two epochs, from the monthly files
     * (same rules as tools/relaystats.py: an ON without OFF duration is
     * closed at the next line of that relay, one still open at nowEpoch) */
    QMap<QString, qint64> onSeconds(qint64 fromEpoch, qint64 toEpoch) const;
    static QMap<QString, qint64> onSeconds(const QStringList &files, qint64 fromEpoch, qint64 toEpoch, qint64 nowEpoch);

private:
    struct RelayState {
        bool    on      = false;
        qint64  onSinceMs = 0;      /* MonoClock of the ON, 0 = not seen */
    };

    QString                 m_basePath;
    int                     m_keepMonths;
    QMap<int, RelayState>   m_state;
    QFile                   m_file;
    QDate                   m_month;        /* month of the open file */

    bool openFor(const QDate &today);
    void removeOld(const QDate &today);
};

#endif // RELAYLOG_H
