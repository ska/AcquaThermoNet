#include "valveexercise.h"
#include "logging.h"

/**
 * @brief ValveExercise::ValveExercise
 * @param config
 * @param parent
 */
ValveExercise::ValveExercise(const ExerciseConfig &config, QObject *parent) :
    QObject(parent),
    m_config(config),
    m_cycle(0),
    m_on(false)
{
    m_checkTimer = new QTimer(this);
    connect(m_checkTimer, &QTimer::timeout, this, &ValveExercise::check);

    m_stepTimer = new QTimer(this);
    m_stepTimer->setSingleShot(true);
    connect(m_stepTimer, &QTimer::timeout, this, &ValveExercise::step);
}

/**
 * @brief ValveExercise::isDue
 * @param localNow
 * @param config
 * @param lastRun
 * @return true if the exercise has to start now
 */
bool ValveExercise::isDue(const QDateTime &localNow, const ExerciseConfig &config, const QDate &lastRun)
{
    if(!config.enabled)
        return false;
    if(localNow.date().dayOfWeek() != config.dayOfWeek || localNow.date() == lastRun)
        return false;

    const int s = config.time.secsTo(localNow.time());
    return s >= 0 && s < WINDOW_S;
}

/**
 * @brief ValveExercise::startSchedule
 */
void ValveExercise::startSchedule()
{
    if(!m_config.enabled)
    {
        qCInfo(lcRegulation) << "Valve exercise disabled";
        return;
    }
    qCInfo(lcRegulation).noquote() << "Valve exercise on day" << m_config.dayOfWeek << "at" << m_config.time.toString("HH:mm")
                                   << "for relays idle for" << m_config.idleDays << "days";
    m_checkTimer->start(CHECK_MS);
}

/**
 * @brief ValveExercise::check
 * The wall clock is needed for day and time: skip while it is not set
 * (board without RTC before NTP)
 */
void ValveExercise::check()
{
    const QDateTime now = QDateTime::currentDateTime();
    if(now.date().year() < 2024)
        return;
    if(isRunning() || !isDue(now, m_config, m_lastRun))
        return;

    m_lastRun = now.date();
    emit due();
}

/**
 * @brief ValveExercise::start
 * @param relays    relays to cycle, first step ON now
 */
void ValveExercise::start(const QList<int> &relays)
{
    if(isRunning() || relays.isEmpty())
        return;

    m_relays = relays;
    m_cycle  = 0;
    m_on     = true;
    for(int r : m_relays)
        emit relayCommand(r, true);
    m_stepTimer->start(m_config.onS * 1000);
}

/**
 * @brief ValveExercise::step
 * ON -> OFF; after the last OFF the exercise is over
 */
void ValveExercise::step()
{
    if(!isRunning())
        return;

    const QList<int> relays = m_relays;
    if(m_on)
    {
        m_on = false;
        for(int r : relays)
            emit relayCommand(r, false);
        m_cycle++;
        if(m_cycle >= m_config.cycles)
        {
            finish();
            return;
        }
        m_stepTimer->start(m_config.offS * 1000);
    }
    else
    {
        m_on = true;
        for(int r : relays)
            emit relayCommand(r, true);
        m_stepTimer->start(m_config.onS * 1000);
    }
}

/**
 * @brief ValveExercise::release
 * @param relay
 */
void ValveExercise::release(int relay)
{
    if(!m_relays.removeAll(relay))
        return;
    qCInfo(lcRegulation) << "Valve exercise: relay" << relay << "released to the regulation";
    if(m_relays.isEmpty())
        finish();
}

/**
 * @brief ValveExercise::stop
 */
void ValveExercise::stop()
{
    if(!isRunning())
        return;
    qCInfo(lcRegulation) << "Valve exercise aborted";
    finish();
}

/**
 * @brief ValveExercise::finish
 */
void ValveExercise::finish()
{
    m_stepTimer->stop();
    m_relays.clear();
    emit finished();
}
