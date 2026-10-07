#ifndef CHRONO_H
#define CHRONO_H

#include <QDateTime>
#include <QStringList>
#include <QVector>

/* Chrono thermostat of a zone: from this time on, this setpoint */
struct ChronoSlot
{
    QTime   at;
    double  temp = 0;
};

/* Chrono thermostat of a zone, [CHRONO] of setting.ini */
struct ChronoConfig
{
    /* Slots per profile, more are dropped */
    static constexpr int MAX_SLOTS = 8;

    bool                enabled     = false;
    QVector<ChronoSlot> weekday;            /* sorted by time */
    QVector<ChronoSlot> holiday;            /* sorted by time, empty: weekday */
    quint8              holidayDays = 0;    /* bit n = day n (QDate::dayOfWeek, 1 = Monday) */
    bool                edited      = false;    /* from state.ini (edited on the panel) */
};

/* A slot occurrence: setpoint and when it begins (local time) */
struct ChronoPoint
{
    double      temp = 0;
    QDateTime   start;                      /* invalid: no chrono */

    bool isValid() const { return start.isValid(); }
};

namespace Chrono
{
    bool isHoliday(const QDate &date, const ChronoConfig &config);
    /* Profile of that day (the weekday one if the holiday one is empty) */
    const QVector<ChronoSlot> &profile(const QDate &date, const ChronoConfig &config);

    /* Slot in force at now: the last one begun today, before the first
     * slot of the day the last one of the previous day. Invalid if the
     * chrono is disabled. */
    ChronoPoint current(const QDateTime &now, const ChronoConfig &config);
    /* Next slot after now. Invalid if the chrono is disabled. */
    ChronoPoint next(const QDateTime &now, const ChronoConfig &config);

    /* Setpoint of each stepMin interval of that day, from 00:00 (96 with
     * 15 minutes). Empty if the chrono is disabled. */
    QVector<double> dayTemps(const QDate &date, const ChronoConfig &config, int stepMin = 15);
    /* "weekday" or "holiday": profile used that day */
    QString profileName(const QDate &date, const ChronoConfig &config);

    /* "HH:MM=temp" entries (also "HH:MM temp"), sorted by time; invalid,
     * duplicated and extra (over MAX_SLOTS) entries are dropped with a
     * warning naming where (e.g. "CHRONO/salotto/weekday") */
    QVector<ChronoSlot> parseProfile(const QStringList &entries, const QString &where);
    /* Editing steps of the panel editor, false if not possible: time of
     * slot k to the next (dir > 0) or previous EDIT_STEP_MIN point,
     * strictly between its neighbours (the order never changes) */
    constexpr int EDIT_STEP_MIN = 15;
    bool stepTime(QVector<ChronoSlot> &list, int k, int dir);
    /* setpoint of slot k +/- TEMP_STEP, within TEMP_MIN..TEMP_MAX */
    bool stepTemp(QVector<ChronoSlot> &list, int k, int dir);
    /* one hour after the last slot, else in the middle of the longest
     * gap, with the setpoint of the slot before; empty: 08:00 20.0 */
    bool addSlot(QVector<ChronoSlot> &list);

    /* "08:00=20.5, 23:00=17" */
    QString toString(const QVector<ChronoSlot> &profile);
}

#endif // CHRONO_H
