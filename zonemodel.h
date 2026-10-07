#ifndef ZONEMODEL_H
#define ZONEMODEL_H

#include <QObject>
#include <QVector>
#include <QSet>
#include <QTimer>
#include <QDateTime>
#include "common.h"
#include "climatezones.h"
#include "chrono.h"

class Configuration;

/* One heating zone: configuration, regulation state, last sensor reading */
struct ZoneData
{
    /* Configuration (setting.ini) */
    QString name;
    int     relay       = 0;            /* 1..RELAY_NUM_MAX, 0 = no relay */
    double  setPoint    = TEMP_DEFAULT;    /* the zone's own, kept during a house mode */
    double  target      = TEMP_DEFAULT;    /* applied: setPoint, or min(setPoint, mode value) */
    ChronoConfig chrono;                /* [CHRONO] profiles */
    QDateTime chronoDone;               /* start of the last slot applied, or of the last
                                         * manual setpoint after a restart; invalid: none */
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

/* House mode values, [MODES] of setting.ini */
struct ModeConfig
{
    double  windowTemp  = 8;            /* windows open: min(own, windowTemp) */
    int     windowS     = 30 * 60;      /* then back to normal */
    double  awayTemp    = 15;           /* away: min(own, awayTemp) until normal */
    double  boostTemp   = 25;           /* boost: max(own, boostTemp) */
    int     boostS      = 30 * 60;      /* then back to normal */
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
    /* Chrono checked this often for a new slot */
    static const int CHRONO_CHECK_MS = 30 * 1000;

    /* House mode, for all the zones (interface §8) */
    enum HouseMode { ModeNormal, ModeWindow, ModeAway, ModeBoost };

    explicit ZoneModel(Configuration *conf, int saveDelayMs = SAVE_DELAY_MS, QObject *parent = nullptr);

    /* "normal", "window", "away", "boost" */
    static QString modeName(HouseMode mode);
    static bool parseMode(const QString &name, HouseMode &mode);

    /* Wall clock set (RTC or NTP): needed to resume window and boost */
    static bool clockValid(const QDateTime &now) { return now.date().year() >= 2024; }
    /* Seconds left of a window or boost ending at untilS (epoch), at most
     * maxS; 0 if over, unknown (untilS 0) or the clock is not set */
    static int resumeS(qint64 untilS, int maxS, const QDateTime &now);

    HouseMode houseMode() const { return m_mode; }
    /* Seconds before the window or boost mode ends, 0 otherwise */
    int remainingS() const;
    /* Change the house mode; false if refused (window while away) */
    bool setHouseMode(HouseMode mode);
    /* Mode values (from the configuration; tests: shorter window) */
    void setModeConfig(const ModeConfig &config);
    const ModeConfig &modeConfig() const { return m_modeConfig; }

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
    /* Chrono edited (saved to state.ini), or back to the profiles of
     * setting.ini; the slot in force applies at once. source names who
     * changed it in the log ("on the panel", "from Home Assistant", …) */
    void setChrono(int i, const ChronoConfig &config, const QString &source = "on the panel");
    /* Chrono on and the own setpoint is not the one of the slot in force:
     * a manual change, until the next slot (normal mode, clock set) */
    bool chronoManual(int i, const QDateTime &now) const;
    void resetChrono(int i, const QString &source = "on the panel");

public slots:
    void flushPendingSaves();
    /* Chrono: a slot begun since the last one applied sets the zone's own
     * setpoint (not saved to flash). Normal mode only: window and boost
     * defer it to their end, away suspends it. Nothing while the clock is
     * not set. Called by a timer and at the mode changes; tests pass now. */
    void checkChrono(const QDateTime &now);

signals:
    void zoneChanged(int i);            /* any change, for the GUI */
    void setPointChanged(int i);        /* also when unchanged: republish the clamped value */
    void heatChanged(int i);
    void sensorUpdated(int i);
    void houseModeChanged();

private:
    Configuration       *m_conf;
    QVector<ZoneData>   m_zones;
    QSet<int>           m_unsaved;          /* zones with setpoint not yet on flash */
    QTimer              *m_saveTimer;
    int                 m_saveDelayMs;
    HouseMode           m_mode;
    ModeConfig          m_modeConfig;
    QTimer              *m_modeTimer;       /* end of window or boost */
    bool                m_chronoStarted = false;    /* first check done (logs at start) */

    void updateTarget(int i);
    void chronoReplaced(int i);
    int durationS(HouseMode mode) const;
    void saveMode(int durationS);

    bool isValid(int i) const { return i >= 0 && i < m_zones.size(); }
    template<typename T> void setField(int i, T ZoneData::*field, T value);
    static double normalizeSetPoint(double temp);
};

#endif // ZONEMODEL_H
