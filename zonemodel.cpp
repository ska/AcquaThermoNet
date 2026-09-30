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
