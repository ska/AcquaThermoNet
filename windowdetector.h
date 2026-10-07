#ifndef WINDOWDETECTOR_H
#define WINDOWDETECTOR_H

#include <QObject>
#include <QPair>
#include <QVector>
#include "configuration.h"

class ZoneModel;

/*
 * Open window guessed from the temperature of a zone: a drop of at least
 * dropC below the highest reading of the last windowMin minutes means
 * "window open?"; a rise of recoverC above the lowest reading since then
 * means "window closed?". Pure (times given by the caller): unit tested.
 * Passive cooling, also with the heating off, is much slower than dropC
 * in windowMin.
 */
class WindowDetector
{
public:
    enum Event { None, Opened, Closed };

    explicit WindowDetector(const WindowConfig &config = WindowConfig()) : m_config(config) {}

    /* A new reading of the zone at monotonic time ms */
    Event feed(qint64 ms, double temp);
    /* Forget everything (sensor lost, detection suspended): no event */
    void reset();

    bool isOpen() const { return m_open; }
    double startTemp() const { return m_startTemp; }   /* highest reading before the drop */
    qint64 startMs() const { return m_startMs; }       /* its time */
    double lowestTemp() const { return m_lowestTemp; } /* lowest reading since the drop */
    qint64 openedMs() const { return m_openedMs; }     /* time of the drop detection */

private:
    WindowConfig                    m_config;
    QVector<QPair<qint64, double>>  m_history;          /* last windowMin minutes, while closed */
    bool                            m_open = false;
    double                          m_startTemp = 0;
    qint64                          m_startMs = 0;
    double                          m_lowestTemp = 0;
    qint64                          m_openedMs = 0;
};

/*
 * One WindowDetector per zone, fed by the sensor readings of ZoneModel.
 * Only signals today (Telegram, log): what to do with an open window is
 * not decided yet. Suspended while the house mode is "window" (the
 * windows were opened on purpose); a lost sensor resets its zone.
 */
class WindowWatch : public QObject
{
    Q_OBJECT
public:
    WindowWatch(ZoneModel *zones, const WindowConfig &config, QObject *parent = nullptr);

    bool isOpen(int zone) const;

signals:
    /* fromTemp at the highest reading, minutes from it to toTemp */
    void windowOpened(int zone, double fromTemp, double toTemp, int minutes);
    /* lowest temperature reached, minutes since the drop was detected */
    void windowClosed(int zone, double lowestTemp, int minutes);

private slots:
    void onSensorUpdated(int zone);
    void onZoneChanged(int zone);
    void onHouseModeChanged();

private:
    ZoneModel                   *m_zones;
    WindowConfig                m_config;
    QVector<WindowDetector>     m_detectors;
};

#endif // WINDOWDETECTOR_H
