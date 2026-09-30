#ifndef ZONEMODEL_H
#define ZONEMODEL_H

#include <QObject>
#include <QVector>
#include <QSet>
#include <QTimer>
#include "common.h"
#include "climatezones.h"

class Configuration;

/* One heating zone: configuration, regulation state, last sensor reading */
struct ZoneData
{
    /* Configuration (setting.ini) */
    QString name;
    int     relay       = 0;            /* 1..RELAY_NUM_MAX, 0 = no relay */
    double  setPoint    = TEMP_DEFAULT;
    /* Regulation state */
    bool    heat        = false;        /* heat demand */
    int     relayState  = -1;           /* read back from the board: -1 unknown, 0 off, 1 on */
    bool    relayFault  = false;        /* relay does not follow commands */
    bool    sensorLost  = false;        /* no sensor data for too long */
    bool    valveExercise = false;      /* relay cycled by the valve exercise */
    bool    frostProtection = false;    /* no sensor data, outdoor cold: timed ON/OFF */
    int     pendingSwitch   = -1;       /* switch waiting for the min cycle: -1 none, 0 OFF, 1 ON */
    qint64  pendingSwitchAtMs = 0;      /* MonoClock ms when it is expected */
    /* Last sensor reading */
    double  temp        = 0;
    quint8  humidity    = 0;
    quint8  battery     = 0;
    quint16 battmv      = 0;
    quint32 unixTime    = 0;            /* sensor timestamp */
    qint64  lastSeenMs  = 0;            /* MonoClock ms of last sensor message, 0 = never */
    QString mac;
};

/*
 * Single source of truth for zone state. Mqtt, Termoregolazione and
 * MainWindow change it through the setters and react to its signals.
 */
class ZoneModel : public QObject
{
    Q_OBJECT
public:
    /* Setpoints are written to flash this long after the last change */
    static const int SAVE_DELAY_MS = 5000;

    explicit ZoneModel(Configuration *conf, int saveDelayMs = SAVE_DELAY_MS, QObject *parent = nullptr);

    int count() const { return m_zones.size(); }
    const ZoneData &zone(int i) const { return m_zones.at(i); }
    int indexOf(const QString &name) const;

    void setSetPoint(int i, double temp);
    void stepSetPoint(int i, int steps);
    void setHeat(int i, bool heat);
    void setSensorData(int i, const ZoneData &data);
    void setRelayState(int i, int state);
    void setRelayFault(int i, bool fault);
    void setSensorLost(int i, bool lost);
    void setValveExercise(int i, bool active);
    void setFrostProtection(int i, bool active);
    void setPendingSwitch(int i, int target, qint64 atMs);

public slots:
    void flushPendingSaves();

signals:
    void zoneChanged(int i);            /* any change, for the GUI */
    void setPointChanged(int i);        /* also when unchanged: republish the clamped value */
    void heatChanged(int i);
    void sensorUpdated(int i);

private:
    Configuration       *m_conf;
    QVector<ZoneData>   m_zones;
    QSet<int>           m_unsaved;          /* zones with setpoint not yet on flash */
    QTimer              *m_saveTimer;
    int                 m_saveDelayMs;

    bool isValid(int i) const { return i >= 0 && i < m_zones.size(); }
    template<typename T> void setField(int i, T ZoneData::*field, T value);
    static double normalizeSetPoint(double temp);
};

#endif // ZONEMODEL_H
