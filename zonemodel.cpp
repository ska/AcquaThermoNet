#include "zonemodel.h"
#include "logging.h"
#include "configuration.h"
#include "monoclock.h"

/**
 * @brief ZoneModel::ZoneModel
 * @param conf
 * @param parent
 */
ZoneModel::ZoneModel(Configuration *conf, int saveDelayMs, QObject *parent) :
    QObject(parent),
    m_conf(conf),
    m_saveDelayMs(saveDelayMs)
{
    m_zones = m_conf->loadZones();
    for(ZoneData &z : m_zones)
        z.setPoint = normalizeSetPoint(z.setPoint);

    for(const ZoneData &z : m_zones)
        qCInfo(lcConfig).noquote() << "Zone" << z.name << "relay" << z.relay << "setpoint" << z.setPoint;

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    connect(m_saveTimer, &QTimer::timeout, this, &ZoneModel::flushPendingSaves);

    /* House mode: away goes on after a restart; window and boost resume
     * with the time left (state.ini [MODE] until, the panel has RTC and
     * NTP), or end if it is over or the clock is not set */
    m_modeConfig = m_conf->loadModes();
    m_modeTimer = new QTimer(this);
    m_modeTimer->setSingleShot(true);
    /* coarse: end up to 5% late and remainingTime() above the length */
    m_modeTimer->setTimerType(Qt::PreciseTimer);
    connect(m_modeTimer, &QTimer::timeout, this, [this] {
        qCInfo(lcConfig).noquote() << QString("House mode %1: time over, setpoints restored").arg(modeName(m_mode));
        setHouseMode(ModeNormal);
    });
    m_mode = ModeNormal;
    HouseMode saved;
    if(parseMode(m_conf->loadHouseMode(), saved) && saved != ModeNormal)
    {
        if(saved == ModeAway)
        {
            m_mode = ModeAway;
            qCInfo(lcConfig) << "House mode away, as before the restart";
        }
        else
        {
            const QDateTime now = QDateTime::currentDateTime();
            const int leftS = resumeS(m_conf->loadHouseModeUntil(), durationS(saved), now);
            if(leftS > 0)
            {
                m_mode = saved;
                m_modeTimer->start(leftS * 1000);
                qCInfo(lcConfig).noquote() << QString("House mode %1 resumed after the restart, %2 s left").arg(modeName(saved)).arg(leftS);
            }
            else
            {
                qCInfo(lcConfig).noquote() << QString("House mode %1 ended during the restart (%2), setpoints restored")
                                              .arg(modeName(saved), clockValid(now) ? "time over" : "clock not set");
                m_conf->saveHouseMode(modeName(ModeNormal));
            }
        }
    }
    for(int i = 0; i < m_zones.size(); i++)
        updateTarget(i);
}

QString ZoneModel::modeName(HouseMode mode)
{
    switch(mode)
    {
    case ModeWindow:    return "window";
    case ModeAway:      return "away";
    case ModeBoost:     return "boost";
    default:            return "normal";
    }
}

/**
 * @brief ZoneModel::parseMode
 * @param name      "normal", "window" or "away" (exact)
 * @param mode      out
 * @return false for anything else
 */
bool ZoneModel::parseMode(const QString &name, HouseMode &mode)
{
    for(HouseMode m : { ModeNormal, ModeWindow, ModeAway, ModeBoost })
    {
        if(name == modeName(m))
        {
            mode = m;
            return true;
        }
    }
    return false;
}

/**
 * @brief ZoneModel::resumeS
 * A clock set back after the save would give more than the whole mode:
 * capped at maxS
 */
int ZoneModel::resumeS(qint64 untilS, int maxS, const QDateTime &now)
{
    if(untilS <= 0 || !clockValid(now))
        return 0;
    const qint64 leftS = untilS - now.toSecsSinceEpoch();
    return leftS > 0 ? int(qMin<qint64>(leftS, maxS)) : 0;
}

/* Length of window and boost, 0 for the modes without an end */
int ZoneModel::durationS(HouseMode mode) const
{
    switch(mode)
    {
    case ModeWindow:    return m_modeConfig.windowS;
    case ModeBoost:     return m_modeConfig.boostS;
    default:            return 0;
    }
}

/* state.ini: the mode and, for window and boost, its end (none while the
 * clock is not set: then a restart ends the mode) */
void ZoneModel::saveMode(int durationS)
{
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 untilS = durationS > 0 && clockValid(now) ? now.toSecsSinceEpoch() + durationS : 0;
    m_conf->saveHouseMode(modeName(m_mode), untilS);
}

int ZoneModel::remainingS() const
{
    if(!m_modeTimer->isActive())
        return 0;
    return (m_modeTimer->remainingTime() + 999) / 1000;
}

void ZoneModel::setModeConfig(const ModeConfig &config)
{
    m_modeConfig = config;
    for(int i = 0; i < m_zones.size(); i++)
    {
        updateTarget(i);
        emit setPointChanged(i);
    }
}

/**
 * @brief ZoneModel::setHouseMode
 * Applies the mode value to every zone: min(own, value) for window and
 * away, max(own, value) for boost; the own setpoints stay as they are:
 * normal applies them again. A new mode replaces the current one, except
 * window while away (refused); a new window or boost restarts its time.
 * @return false if refused
 */
bool ZoneModel::setHouseMode(HouseMode mode)
{
    if(mode == ModeWindow && m_mode == ModeAway)
    {
        qCInfo(lcConfig) << "House mode window ignored: away is active";
        return false;
    }

    const int lengthS = durationS(mode);
    if(lengthS > 0)
        m_modeTimer->start(lengthS * 1000);
    else
        m_modeTimer->stop();

    if(mode == m_mode)
    {
        if(lengthS > 0)
            saveMode(lengthS);              /* window or boost restarted: new end */
        emit houseModeChanged();
        return true;
    }

    qCInfo(lcConfig).noquote() << "House mode" << modeName(m_mode) << "->" << modeName(mode);
    m_mode = mode;
    saveMode(lengthS);
    for(int i = 0; i < m_zones.size(); i++)
    {
        updateTarget(i);
        emit zoneChanged(i);
        emit setPointChanged(i);            /* regulation, state_temp */
    }
    emit houseModeChanged();
    return true;
}

/**
 * @brief ZoneModel::updateTarget
 * The applied setpoint of zone i for the current house mode
 */
void ZoneModel::updateTarget(int i)
{
    ZoneData &z = m_zones[i];
    switch(m_mode)
    {
    case ModeWindow:    z.target = qMin(z.setPoint, m_modeConfig.windowTemp); break;
    case ModeAway:      z.target = qMin(z.setPoint, m_modeConfig.awayTemp); break;
    case ModeBoost:     z.target = qMax(z.setPoint, m_modeConfig.boostTemp); break;
    default:            z.target = z.setPoint; break;
    }
}

/**
 * @brief ZoneModel::flushPendingSaves
 * Write the changed setpoints now (also call it on exit)
 */
void ZoneModel::flushPendingSaves()
{
    m_saveTimer->stop();
    if(m_unsaved.isEmpty())
        return;

    QMap<QString, double> setpoints;
    for(int i : m_unsaved)
        setpoints.insert(m_zones[i].name, m_zones[i].setPoint);
    m_unsaved.clear();
    m_conf->saveSetPoints(setpoints);
}

/**
 * @brief ZoneModel::indexOf
 * @param name
 * @return zone index, -1 if unknown
 */
int ZoneModel::indexOf(const QString &name) const
{
    for(int i=0; i<m_zones.size(); i++)
    {
        if(m_zones[i].name == name)
            return i;
    }
    return -1;
}

/**
 * @brief ZoneModel::normalizeSetPoint
 * Round to TEMP_STEP and clamp to TEMP_MIN..TEMP_MAX
 */
double ZoneModel::normalizeSetPoint(double temp)
{
    temp = qRound(temp / TEMP_STEP) * TEMP_STEP;
    return qBound<double>(TEMP_MIN, temp, TEMP_MAX);
}

/**
 * @brief ZoneModel::setSetPoint
 * @param i
 * @param temp
 */
void ZoneModel::setSetPoint(int i, double temp)
{
    if(!isValid(i))
        return;

    temp = normalizeSetPoint(temp);
    ZoneData &z = m_zones[i];
    if(temp != z.setPoint)
    {
        z.setPoint = temp;
        updateTarget(i);
        /* Several +/- taps: one flash write after the last one */
        m_unsaved.insert(i);
        m_saveTimer->start(m_saveDelayMs);
        emit zoneChanged(i);
    }
    emit setPointChanged(i);
}

/**
 * @brief ZoneModel::stepSetPoint
 * @param i
 * @param steps     +/- number of TEMP_STEP
 */
void ZoneModel::stepSetPoint(int i, int steps)
{
    if(!isValid(i))
        return;
    setSetPoint(i, m_zones[i].setPoint + steps * TEMP_STEP);
}

/**
 * @brief ZoneModel::setHeat
 * @param i
 * @param heat
 */
void ZoneModel::setHeat(int i, bool heat)
{
    if(!isValid(i) || m_zones[i].heat == heat)
        return;

    m_zones[i].heat = heat;
    emit heatChanged(i);
    emit zoneChanged(i);
}

/**
 * @brief ZoneModel::setSensorData
 * Copy the sensor fields of data, mark the zone as seen now
 * @param i
 * @param data
 */
void ZoneModel::setSensorData(int i, const ZoneData &data)
{
    if(!isValid(i))
        return;

    ZoneData &z = m_zones[i];
    z.temp      = data.temp;
    z.humidity  = data.humidity;
    z.battery   = data.battery;
    z.battmv    = data.battmv;
    z.unixTime  = data.unixTime;
    z.mac       = data.mac;
    z.lastSeenMs = MonoClock::nowMs();

    emit sensorUpdated(i);
    emit zoneChanged(i);
}

/**
 * @brief ZoneModel::setField
 * Set a status field, zoneChanged only on change
 */
template<typename T>
void ZoneModel::setField(int i, T ZoneData::*field, T value)
{
    if(!isValid(i) || m_zones[i].*field == value)
        return;
    m_zones[i].*field = value;
    emit zoneChanged(i);
}

void ZoneModel::setRelayState(int i, int state)
{
    setField(i, &ZoneData::relayState, state);
}

void ZoneModel::setRelayFault(int i, bool fault)
{
    setField(i, &ZoneData::relayFault, fault);
}

void ZoneModel::setSensorLost(int i, bool lost)
{
    setField(i, &ZoneData::sensorLost, lost);
}

/**
 * @brief ZoneModel::setPendingSwitch
 * @param i
 * @param target    -1 none, 0 OFF, 1 ON
 * @param atMs      expected switch time (MonoClock)
 */
void ZoneModel::setPendingSwitch(int i, int target, qint64 atMs)
{
    if(!isValid(i))
        return;
    if(target < 0)
        atMs = 0;

    ZoneData &z = m_zones[i];
    if(z.pendingSwitch == target && z.pendingSwitchAtMs == atMs)
        return;
    z.pendingSwitch     = target;
    z.pendingSwitchAtMs = atMs;
    emit zoneChanged(i);
}

void ZoneModel::setValveExercise(int i, bool active)
{
    setField(i, &ZoneData::valveExercise, active);
}

void ZoneModel::setFrostProtection(int i, bool active)
{
    setField(i, &ZoneData::frostProtection, active);
}
