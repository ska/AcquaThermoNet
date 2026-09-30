#include "termoregolazione.h"
#include "logging.h"
#include "monoclock.h"
#include <climits>

/**
 * @brief Termoregolazione::Termoregolazione
 * @param zones
 * @param fp
 * @param parent
 */
Termoregolazione::Termoregolazione(ZoneModel *zones, ModBusFrameProcessor *fp, const RegulationConfig &rc,
                                   Configuration *conf, QObject *parent) :
    QObject(parent),
    m_zones(zones),
    fp(fp),
    m_rc(rc),
    m_conf(conf)
{
    qCInfo(lcRegulation).noquote() << "Start Termoregolazione, min cycle" << rc.minCycleS << "s, sensor timeout" << rc.sensorTimeoutS << "s";

    m_Forcerefresh  = false;
    m_shuttingDown  = false;
    m_lastRelayBm   = 0;
    m_lastStatusMs  = 0;
    m_verifyReqMs   = 0;
    m_ctl.resize(m_zones->count());
    m_startMs           = MonoClock::nowMs();
    m_outdoorTemp       = 0;
    m_outdoorMs         = 0;
    m_frostCycleStartMs = 0;
    m_frostLastUsedMs   = 0;
    m_frostTimer = new QTimer(this);
    m_frostTimer->setSingleShot(true);
    connect(m_frostTimer, &QTimer::timeout, this, &Termoregolazione::evaluateAllZones);

    /* heatChanged is not listened to: setting heat here must not re-enter */
    connect(m_zones, &ZoneModel::sensorUpdated,     this, &Termoregolazione::evaluateZone);
    connect(m_zones, &ZoneModel::setPointChanged,   this, &Termoregolazione::evaluateZone);
    connect(fp, &ModBusFrameProcessor::relayStatus,   this, &Termoregolazione::onRelayStatus);
    connect(fp, &ModBusFrameProcessor::onlineChanged, this, &Termoregolazione::onModbusOnlineChanged);

    /* Periodic check: also catches sensors that stopped sending */
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Termoregolazione::forceRefreshAllZones);
    m_timer->start(60*1000);

    /*
     * Valve exercise. A relay without history counts as active now:
     * the first exercise comes after idle_days, not at the first start.
     * */
    if(m_conf)
        m_lastOn = m_lastOnSaved = m_conf->loadRelayLastOn();
    for(int relay : configuredRelays())
    {
        if(!m_lastOn.contains(relay))
            noteRelayOn(relay);
    }
    m_exercise = new ValveExercise(m_rc.exercise, this);
    connect(m_exercise, &ValveExercise::due,           this, &Termoregolazione::startValveExercise);
    connect(m_exercise, &ValveExercise::relayCommand,  this, &Termoregolazione::onExerciseRelay);
    connect(m_exercise, &ValveExercise::finished,      this, &Termoregolazione::onExerciseFinished);
    m_exercise->startSchedule();
}

/**
 * @brief Termoregolazione::configuredRelays
 * @return relays used by the zones, each once
 */
QList<int> Termoregolazione::configuredRelays() const
{
    QList<int> relays;
    for(int zone=0; zone<m_zones->count(); zone++)
    {
        const int relay = m_zones->zone(zone).relay;
        if(relay > 0 && !relays.contains(relay))
            relays.append(relay);
    }
    return relays;
}

/**
 * @brief Termoregolazione::sendRelay
 * Every relay command goes through here
 */
void Termoregolazione::sendRelay(int relay, bool on, const char *reason)
{
    fp->requestSetRelay(relay, on);
    if(m_relayLog)
        m_relayLog->record(relay, relayZones(relay), on, reason);

    const qint64 now = MonoClock::nowMs();
    for(int zone=0; zone<m_zones->count(); zone++)
    {
        if(m_zones->zone(zone).relay != relay)
            continue;
        m_ctl[zone].expected  = on ? 1 : 0;
        m_ctl[zone].cmdTimeMs = now;
    }
    if(on)
        noteRelayOn(relay);
}

/**
 * @brief Termoregolazione::relayZones
 * @return zones using the relay, "+" separated
 */
QString Termoregolazione::relayZones(int relay) const
{
    QStringList names;
    for(int zone=0; zone<m_zones->count(); zone++)
    {
        if(m_zones->zone(zone).relay == relay)
            names.append(m_zones->zone(zone).name);
    }
    return names.join('+');
}

/**
 * @brief Termoregolazione::noteRelayOn
 * Last activation for the valve exercise. Needs the calendar (idle days
 * across restarts): wall clock, ignored until it is set.
 */
void Termoregolazione::noteRelayOn(int relay)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if(QDateTime::currentDateTime().date().year() < 2024)
        return;

    m_lastOn[relay] = now;
    if(m_conf && now - m_lastOnSaved.value(relay, 0) >= LAST_ON_SAVE_S)
    {
        m_lastOnSaved[relay] = now;
        m_conf->saveRelayLastOn(relay, now);
    }
}

/**
 * @brief Termoregolazione::startValveExercise
 * @return number of relays exercised
 */
int Termoregolazione::startValveExercise()
{
    if(m_shuttingDown || m_exercise->isRunning())
        return 0;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 idleS = qint64(m_rc.exercise.idleDays) * 86400;
    QList<int> relays;
    for(int relay : configuredRelays())
    {
        /* No history yet (wall clock was not set at startup): start counting now */
        if(!m_lastOn.contains(relay))
        {
            noteRelayOn(relay);
            continue;
        }
        /* Wall clock set back: a last_on in the future counts as now */
        const qint64 idle = now - qMin(now, m_lastOn.value(relay, now));
        bool heating = false;
        for(int zone=0; zone<m_zones->count(); zone++)
            heating |= m_zones->zone(zone).relay == relay && m_zones->zone(zone).heat;
        if(!heating && idle >= idleS)
            relays.append(relay);
    }

    if(relays.isEmpty())
    {
        qCInfo(lcRegulation) << "Valve exercise: no relay idle for" << m_rc.exercise.idleDays << "days";
        return 0;
    }

    qCInfo(lcRegulation) << "Valve exercise start, relays" << relays << ":" << m_rc.exercise.cycles
                         << "x (" << m_rc.exercise.onS << "s ON," << m_rc.exercise.offS << "s OFF )";
    for(int zone=0; zone<m_zones->count(); zone++)
        m_zones->setValveExercise(zone, relays.contains(m_zones->zone(zone).relay));
    m_exercise->start(relays);
    return relays.size();
}

/**
 * @brief Termoregolazione::onExerciseRelay
 * @param relay
 * @param on
 */
void Termoregolazione::onExerciseRelay(int relay, bool on)
{
    if(on && m_shuttingDown)
        return;
    sendRelay(relay, on, "exercise");
}

/**
 * @brief Termoregolazione::onExerciseFinished
 * Exercised relays end OFF; released ones belong to the regulation
 */
void Termoregolazione::onExerciseFinished()
{
    qCInfo(lcRegulation) << "Valve exercise done";
    for(int zone=0; zone<m_zones->count(); zone++)
        m_zones->setValveExercise(zone, false);
}

/**
 * @brief Termoregolazione::allRelaysOff
 * Safe state, used at startup and on exit
 */
void Termoregolazione::allRelaysOff()
{
    qCInfo(lcRegulation) << "All relays OFF";
    for(int zone=0; zone<m_zones->count(); zone++)
        driveZone(zone, false, m_shuttingDown ? "shutdown" : "startup");
}

/**
 * @brief Termoregolazione::beginShutdown
 * Stop regulation, block ON commands, send all relays OFF
 */
void Termoregolazione::beginShutdown()
{
    m_shuttingDown = true;
    m_timer->stop();
    m_frostTimer->stop();
    m_exercise->stop();
    allRelaysOff();
}

/**
 * @brief Termoregolazione::verifyRelaysOff
 * Call only when the serial bus is idle: a status requested then is
 * answered after every command already sent, so it shows the real state.
 * Relays still ON get OFF again, then a new status is requested.
 * @return true when a fresh status confirms all relays OFF
 */
bool Termoregolazione::verifyRelaysOff()
{
    if( m_verifyReqMs > 0 && m_lastStatusMs > m_verifyReqMs )
    {
        bool allOff = true;
        for(int zone=0; zone<m_zones->count(); zone++)
        {
            const int relay = m_zones->zone(zone).relay;
            if( relay > 0 && ((m_lastRelayBm >> (relay-1)) & 0x01) )
            {
                allOff = false;
                qCWarning(lcRegulation) << "Relay" << relay << "still ON on exit, resend OFF";
                fp->requestSetRelay(relay, false);
            }
        }
        if( allOff )
            return true;
    }

    m_verifyReqMs = MonoClock::nowMs();
    fp->requestStatus();
    return false;
}

/**
 * @brief Termoregolazione::driveZone
 * @param zone
 * @param on
 */
void Termoregolazione::driveZone(int zone, bool on, const char *reason)
{
    /* Events are still processed while shutting down: never switch ON */
    if(on && m_shuttingDown)
        return;

    if(on != m_zones->zone(zone).heat)
        m_ctl[zone].switchTimeMs = MonoClock::nowMs();
    m_zones->setHeat(zone, on);

    const int relay = m_zones->zone(zone).relay;
    if(relay <= 0)
        return;

    /* Relay in the valve exercise: heat demand takes it back, OFF
     * (including the periodic resend) leaves it to the exercise */
    if(m_exercise->owns(relay))
    {
        if(!on)
            return;
        m_exercise->release(relay);
        for(int z=0; z<m_zones->count(); z++)
        {
            if(m_zones->zone(z).relay == relay)
                m_zones->setValveExercise(z, false);
        }
    }
    sendRelay(relay, on, reason);
}

/**
 * @brief Termoregolazione::evaluateZone
 * @param zone
 */
void Termoregolazione::evaluateZone(int zone)
{
    if(zone < 0 || zone >= m_ctl.size())
        return;

    const ZoneData &t = m_zones->zone(zone);

    /*
     * No data yet or sensor silent too long: zone OFF, or frost
     * protection cycle if the sensor is missing and it is cold outside
     * * */
    const qint64 nowMs = MonoClock::nowMs();
    const qint64 timeoutMs = qint64(m_rc.sensorTimeoutS) * 1000;
    const qint64 ageMs = nowMs - t.lastSeenMs;
    if( t.lastSeenMs == 0 || ageMs > timeoutMs )
    {
        if( t.lastSeenMs != 0 && !t.sensorLost )
        {
            qCWarning(lcRegulation) << "Sensor timeout zone" << t.name << "last data" << ageMs / 1000 << "s ago, zone OFF";
            m_zones->setSensorLost(zone, true);
        }
        m_zones->setPendingSwitch(zone, -1, 0);

        /* never seen: missing once the timeout has passed since the start */
        const bool missing = t.lastSeenMs != 0 || nowMs - m_startMs > timeoutMs;
        const bool frost = missing && frostWanted();
        if( frost != t.frostProtection )
        {
            qCWarning(lcRegulation) << "Frost protection zone" << t.name << (frost ? "ON" : "OFF");
            m_zones->setFrostProtection(zone, frost);
        }

        const bool on = frost && frostPhaseOn();
        if( t.heat != on || m_Forcerefresh )
            driveZone(zone, on, frost ? "frost" : "no_sensor");
        return;
    }

    if( t.frostProtection )
    {
        qCInfo(lcRegulation) << "Frost protection zone" << t.name << "OFF: sensor back";
        m_zones->setFrostProtection(zone, false);
    }

    if( t.sensorLost )
    {
        qCInfo(lcRegulation) << "Sensor back zone" << t.name;
        m_zones->setSensorLost(zone, false);
    }

    /* A real switch waits for the min cycle; resending the same state does not */
    bool deferred = false;
    if( (t.temp < (t.setPoint-TEMP_HYST)) && ((t.heat != true) || m_Forcerefresh))
    {
        if( t.heat || minCycleElapsed(zone, true) )
            driveZone(zone, true, "regulation");
        else
            deferred = true;
    }
    else
    if( (t.temp > t.setPoint) && ((t.heat == true) || m_Forcerefresh))
    {
        if( !t.heat || minCycleElapsed(zone, false) )
            driveZone(zone, false, "regulation");
        else
            deferred = true;
    }

    /* Switched, or no longer needed (e.g. setpoint changed back) */
    if( !deferred )
        m_zones->setPendingSwitch(zone, -1, 0);
}

/**
 * @brief Termoregolazione::minCycleElapsed
 * Short cycling protection for valves/pumps. Safety OFF (sensor lost,
 * startup, shutdown) does not ask. When blocked, the zone is evaluated
 * again as soon as the min cycle is over, and the pending switch is
 * published in the model for the GUI.
 * @param zone
 * @param target    wanted state
 * @return true if the zone may switch now
 */
bool Termoregolazione::minCycleElapsed(int zone, bool target)
{
    ZoneCtl &c = m_ctl[zone];
    const qint64 minCycleMs = qint64(m_rc.minCycleS) * 1000;
    if( minCycleMs <= 0 || c.switchTimeMs == 0 )
        return true;

    const qint64 left = c.switchTimeMs + minCycleMs - MonoClock::nowMs();
    if( left <= 0 )
        return true;

    m_zones->setPendingSwitch(zone, target ? 1 : 0, c.switchTimeMs + minCycleMs);

    if( !c.evalPending )
    {
        c.evalPending = true;
        qCInfo(lcRegulation) << "Zone" << m_zones->zone(zone).name << "switch deferred" << (left+999)/1000 << "s (min cycle)";
        QTimer::singleShot(left + 100, this, [this, zone] {
            m_ctl[zone].evalPending = false;
            evaluateZone(zone);
        });
    }
    return false;
}

/**
 * @brief Termoregolazione::setOutdoorTemperature
 * From the weather service
 * @param celsius
 */
void Termoregolazione::setOutdoorTemperature(double celsius)
{
    m_outdoorTemp = celsius;
    m_outdoorMs   = MonoClock::nowMs();
    evaluateAllZones();
}

/**
 * @brief Termoregolazione::frostWanted
 * Outdoor below the threshold; no or old outdoor data counts as cold
 * when outdoor_unknown_protect (fail safe: no weather must not freeze pipes)
 */
bool Termoregolazione::frostWanted() const
{
    const FrostConfig &f = m_rc.frost;
    if( !f.enabled )
        return false;

    const bool known = m_outdoorMs != 0 && MonoClock::nowMs() - m_outdoorMs <= qint64(f.outdoorMaxAgeMin) * 60000;
    if( !known )
        return f.unknownProtect;
    return m_outdoorTemp < f.outdoorBelow;
}

/**
 * @brief Termoregolazione::frostPhaseOn
 * One cycle for all the protected zones: on_min ON, then OFF until
 * period_min. A new cycle (starting ON) begins when the protection was
 * not used for a whole period. The timer wakes up at the next edge.
 * @return true in the ON part of the cycle
 */
bool Termoregolazione::frostPhaseOn()
{
    const qint64 now = MonoClock::nowMs();
    const qint64 onMs = qint64(m_rc.frost.onMin) * 60000;
    const qint64 periodMs = qint64(m_rc.frost.periodMin) * 60000;

    if( m_frostCycleStartMs == 0 || now - m_frostLastUsedMs > periodMs )
        m_frostCycleStartMs = now;
    m_frostLastUsedMs = now;

    const qint64 pos = (now - m_frostCycleStartMs) % periodMs;
    const bool on = pos < onMs;
    const qint64 nextEdge = on ? onMs - pos : periodMs - pos;
    m_frostTimer->start(int(qMin<qint64>(nextEdge + 50, INT_MAX)));
    return on;
}

/**
 * @brief Termoregolazione::evaluateAllZones
 * Without forcing: only real changes are sent
 */
void Termoregolazione::evaluateAllZones()
{
    for(int i=0; i<m_ctl.size(); i++)
        evaluateZone(i);
}

/**
 * @brief Termoregolazione::forceRefreshAllZones
 * Re-evaluate every zone and resend its relay command
 */
void Termoregolazione::forceRefreshAllZones()
{
    m_Forcerefresh = true;
    for(int i=0; i<m_ctl.size(); i++)
    {
        evaluateZone(i);
    }
    m_Forcerefresh = false;
}

/**
 * @brief Termoregolazione::onRelayStatus
 * Compare relay state read from the board with the last command,
 * resend on mismatch
 * @param relayBm   bit n = relay n+1
 */
void Termoregolazione::onRelayStatus(quint8 relayBm)
{
    const qint64 now = MonoClock::nowMs();
    m_lastRelayBm  = relayBm;
    m_lastStatusMs = now;

    for(int zone=0; zone<m_ctl.size(); zone++)
    {
        const ZoneData &z = m_zones->zone(zone);
        ZoneCtl &c = m_ctl[zone];
        if( z.relay <= 0 )
            continue;

        const int actual = (relayBm >> (z.relay-1)) & 0x01;
        m_zones->setRelayState(zone, actual);

        if( c.expected < 0 )
            continue;
        if( (now - c.cmdTimeMs) < m_rc.relaySettleMs )
            continue;

        if( actual == c.expected )
        {
            if( c.mismatch > RELAY_MAX_RETRY )
                qCInfo(lcRegulation) << "Relay" << z.relay << "zone" << z.name << "recovered";
            c.mismatch = 0;
            m_zones->setRelayFault(zone, false);
            continue;
        }

        c.mismatch++;
        if( c.mismatch <= RELAY_MAX_RETRY )
            qCWarning(lcRegulation) << "Relay" << z.relay << "zone" << z.name << "is" << actual << "expected" << c.expected << ", resend";
        else if( c.mismatch == RELAY_MAX_RETRY+1 )
        {
            qCCritical(lcRegulation) << "Relay" << z.relay << "zone" << z.name << "does not follow commands";
            m_zones->setRelayFault(zone, true);
        }

        fp->requestSetRelay(z.relay, c.expected == 1);
        c.cmdTimeMs = now;
    }
}

/**
 * @brief Termoregolazione::onModbusOnlineChanged
 * Board offline: the relay states read back are no longer known
 * @param online
 */
void Termoregolazione::onModbusOnlineChanged(bool online)
{
    if(online)
        return;
    for(int zone=0; zone<m_zones->count(); zone++)
        m_zones->setRelayState(zone, -1);
}
