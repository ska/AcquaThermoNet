#ifndef TERMOREGOLAZIONE_H
#define TERMOREGOLAZIONE_H
#include <QObject>
#include <QTimer>
#include <QVector>
#include "common.h"
#include "zonemodel.h"
#include "modbusframeprocessor.h"
#include "configuration.h"
#include "valveexercise.h"
#include "relaylog.h"

/*
 * On/off regulation with hysteresis: reads ZoneModel, sets zone heat
 * and drives the relays through the Modbus processor. Also runs the weekly
 * valve exercise on relays idle for a week (only owner of the relays) and
 * the frost protection of zones without sensor data.
 */
class Termoregolazione : public QObject
{
    Q_OBJECT
public:
    /* Mismatches logged as warning before one critical log */
    static const int    RELAY_MAX_RETRY    = 3;

    /* Relay activations are stored for the valve exercise with a stable
     * wall clock only, at most once per LAST_ON_SAVE_S per relay */
    static const qint64 LAST_ON_SAVE_S     = 3600;

    /* conf: state.ini for the relay activations, nullptr = not stored */
    Termoregolazione(ZoneModel *zones, ModBusFrameProcessor *fp, const RegulationConfig &rc,
                     Configuration *conf = nullptr, QObject *parent = nullptr);

    void allRelaysOff();
    void beginShutdown();
    bool verifyRelaysOff();

    /* Valve exercise now on the relays idle for idle_days (also used by
     * the weekly schedule). Returns the number of relays exercised. */
    int  startValveExercise();

    /* Relay state changes to the CSV activity log, nullptr = none */
    void setRelayLog(RelayLog *log) { m_relayLog = log; }
    bool valveExerciseRunning() const { return m_exercise->isRunning(); }

    /* Frost protection active now for this outdoor state (tests, GUI) */
    bool frostWanted() const;

public slots:
    void forceRefreshAllZones();
    void setOutdoorTemperature(double celsius);

private slots:
    void evaluateZone(int zone);
    void onRelayStatus(quint8 relayBm);
    void onModbusOnlineChanged(bool online);
    void onExerciseRelay(int relay, bool on);
    void onExerciseFinished();

private:
    /* Per zone relay bookkeeping */
    struct ZoneCtl {
        int     expected    = -1;       /* -1 unknown, 0 off, 1 on  */
        qint64  cmdTimeMs   = 0;        /* last relay command       */
        int     mismatch    = 0;        /* consecutive feedback errors */
        qint64  switchTimeMs = 0;       /* last ON<->OFF change      */
        bool    evalPending = false;    /* re-evaluation scheduled after min cycle */
    };

    ZoneModel               *m_zones;
    ModBusFrameProcessor    *fp;
    bool                    m_Forcerefresh;
    QTimer                  *m_timer;
    QVector<ZoneCtl>        m_ctl;
    bool                    m_shuttingDown;
    quint8                  m_lastRelayBm;
    qint64                  m_lastStatusMs;     /* last relay status received  */
    qint64                  m_verifyReqMs;      /* last shutdown status request */
    RegulationConfig        m_rc;
    Configuration           *m_conf;
    ValveExercise           *m_exercise;
    RelayLog                *m_relayLog = nullptr;
    QMap<int, qint64>       m_lastOn;           /* relay -> last activation, epoch secs */
    QMap<int, qint64>       m_lastOnSaved;      /* what is in state.ini */
    qint64                  m_startMs;          /* MonoClock at start: sensors never seen */
    double                  m_outdoorTemp;
    qint64                  m_outdoorMs;        /* MonoClock of the outdoor data, 0 = none */
    qint64                  m_frostCycleStartMs;
    qint64                  m_frostLastUsedMs;
    QTimer                  *m_frostTimer;      /* next ON/OFF edge of the frost cycle */

    void driveZone(int zone, bool on, const char *reason);
    bool frostPhaseOn();
    void evaluateAllZones();
    void sendRelay(int relay, bool on, const char *reason);
    QString relayZones(int relay) const;
    void noteRelayOn(int relay);
    QList<int> configuredRelays() const;
    bool minCycleElapsed(int zone, bool target);
};

#endif // TERMOREGOLAZIONE_H
