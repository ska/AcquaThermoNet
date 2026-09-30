#ifndef VALVEEXERCISE_H
#define VALVEEXERCISE_H

#include <QObject>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QTimer>
#include "configuration.h"

/*
 * Valve anti-seize exercise: once a week (day and time from setting.ini)
 * the given relays are switched ON for onS and OFF for offS, cycles times.
 * Schedule and sequence only: relays are driven by the owner through
 * relayCommand(), so Termoregolazione stays the only one on the bus.
 */
class ValveExercise : public QObject
{
    Q_OBJECT
public:
    /* Wall clock check period and start window after the configured time */
    static const int CHECK_MS = 30 * 1000;
    static const int WINDOW_S = 10 * 60;

    explicit ValveExercise(const ExerciseConfig &config, QObject *parent = nullptr);

    /* Due at localNow: right day, within WINDOW_S after the time, not run on that date yet */
    static bool isDue(const QDateTime &localNow, const ExerciseConfig &config, const QDate &lastRun);

    void startSchedule();
    void start(const QList<int> &relays);
    void stop();                        /* abort, no command: the owner switches the relays OFF */
    void release(int relay);            /* the relay is needed by the regulation */

    bool isRunning() const { return !m_relays.isEmpty(); }
    bool owns(int relay) const { return m_relays.contains(relay); }
    const QList<int> &relays() const { return m_relays; }

signals:
    void due();
    void relayCommand(int relay, bool on);
    void finished();

private slots:
    void check();
    void step();

private:
    ExerciseConfig  m_config;
    QTimer          *m_checkTimer;
    QTimer          *m_stepTimer;
    QList<int>      m_relays;
    int             m_cycle;            /* 0-based */
    bool            m_on;
    QDate           m_lastRun;

    void finish();
};

#endif // VALVEEXERCISE_H
