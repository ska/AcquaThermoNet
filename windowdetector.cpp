#include "windowdetector.h"
#include "logging.h"
#include "monoclock.h"
#include "zonemodel.h"

/* Readings are rounded to 0.1: compare with a margin */
static const double EPS = 1e-6;

/**
 * @brief WindowDetector::feed
 * @param ms    monotonic time of the reading
 * @param temp
 * @return Opened or Closed on a change, else None
 */
WindowDetector::Event WindowDetector::feed(qint64 ms, double temp)
{
    if(m_open)
    {
        if(temp < m_lowestTemp)
            m_lowestTemp = temp;
        if(temp < m_lowestTemp + m_config.recoverC - EPS)
            return None;
        /* closed: a new history from here, the readings before the
         * drop would open it again at once */
        m_open = false;
        m_history = { qMakePair(ms, temp) };
        return Closed;
    }

    const qint64 oldest = ms - qint64(m_config.windowMin) * 60 * 1000;
    while(!m_history.isEmpty() && m_history.first().first < oldest)
        m_history.removeFirst();

    int highest = -1;
    for(int i = 0; i < m_history.size(); i++)
    {
        if(highest < 0 || m_history[i].second >= m_history[highest].second)
            highest = i;
    }
    if(highest >= 0 && m_history[highest].second - temp >= m_config.dropC - EPS)
    {
        m_open = true;
        m_startTemp = m_history[highest].second;
        m_startMs = m_history[highest].first;
        m_lowestTemp = temp;
        m_openedMs = ms;
        m_history.clear();
        return Opened;
    }
    m_history.append(qMakePair(ms, temp));
    return None;
}

void WindowDetector::reset()
{
    m_open = false;
    m_history.clear();
}

/**
 * @brief WindowWatch::WindowWatch
 * @param zones
 * @param config    disabled: no detector fed, never a signal
 * @param parent
 */
WindowWatch::WindowWatch(ZoneModel *zones, const WindowConfig &config, QObject *parent) :
    QObject(parent),
    m_zones(zones),
    m_config(config),
    m_detectors(zones->count(), WindowDetector(config))
{
    if(!m_config.enabled)
        return;
    qCInfo(lcRegulation).noquote() << QString("Open window detection: a drop of %1 degC within %2 min, closed again after a rise of %3 degC")
                                      .arg(m_config.dropC).arg(m_config.windowMin).arg(m_config.recoverC);
    connect(m_zones, &ZoneModel::sensorUpdated,    this, &WindowWatch::onSensorUpdated);
    connect(m_zones, &ZoneModel::zoneChanged,      this, &WindowWatch::onZoneChanged);
    connect(m_zones, &ZoneModel::houseModeChanged, this, &WindowWatch::onHouseModeChanged);
}

bool WindowWatch::isOpen(int zone) const
{
    return zone >= 0 && zone < m_detectors.size() && m_detectors[zone].isOpen();
}

void WindowWatch::onSensorUpdated(int zone)
{
    if(zone < 0 || zone >= m_detectors.size() || m_zones->houseMode() == ZoneModel::ModeWindow)
        return;

    WindowDetector &d = m_detectors[zone];
    const ZoneData &z = m_zones->zone(zone);
    const qint64 now = MonoClock::nowMs();
    switch(d.feed(now, z.temp))
    {
    case WindowDetector::Opened:
    {
        const int minutes = int((now - d.startMs() + 59999) / 60000);
        qCInfo(lcRegulation).noquote() << QString("Zone %1: window open? %2 -> %3 degC in %4 min")
                                          .arg(z.name).arg(d.startTemp(), 0, 'f', 1).arg(z.temp, 0, 'f', 1).arg(minutes);
        emit windowOpened(zone, d.startTemp(), z.temp, minutes);
        break;
    }
    case WindowDetector::Closed:
    {
        const int minutes = int((now - d.openedMs()) / 60000);
        qCInfo(lcRegulation).noquote() << QString("Zone %1: temperature rising again, window closed? (open %2 min, lowest %3 degC)")
                                          .arg(z.name).arg(minutes).arg(d.lowestTemp(), 0, 'f', 1);
        emit windowClosed(zone, d.lowestTemp(), minutes);
        break;
    }
    case WindowDetector::None:
        break;
    }
}

/**
 * @brief WindowWatch::onZoneChanged
 * A lost sensor: start again from its next reading
 */
void WindowWatch::onZoneChanged(int zone)
{
    if(zone >= 0 && zone < m_detectors.size() && m_zones->zone(zone).sensorLost)
        m_detectors[zone].reset();
}

/**
 * @brief WindowWatch::onHouseModeChanged
 * "Windows open" chosen on purpose: no guess during it, nor from the
 * readings before it
 */
void WindowWatch::onHouseModeChanged()
{
    if(m_zones->houseMode() != ZoneModel::ModeWindow)
        return;
    for(WindowDetector &d : m_detectors)
        d.reset();
}
