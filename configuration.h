#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <QSettings>
#include <QVector>
#include <QMap>
#include <QTime>
#include <QDebug>
#include "common.h"
#include "zonemodel.h"

/*
 * setting.ini: configuration, written only by hand, never by the application.
 * Deployed as setting.default.ini and copied to setting.ini on first start,
 * so a deploy does not overwrite the device configuration.
 *
 * state.ini (next to setting.ini): written by the application, never
 * deployed. Holds what changes at run time:
 *   [MQTT]   unique_id
 *   [ZONES]  <zone>\setpoint    (overrides the initial one of setting.ini)
 *   [RELAYS] <n>\last_on       (epoch secs of the last activation, valve exercise)
 *   [MODE]   house             (normal, window, away, boost: house mode, interface §8)
 *            until             (epoch secs: end of window or boost, resumed after a restart)
 *   [APP]    clean_exit        (false while running: tells a crash at next start)
 *
 * setting.ini format:
 *
 *   [MQTT]
 *   broker_addr, broker_port, broker_uname, broker_password
 *   unique_id           (optional: fixed id instead of the MAC)
 *   tls=false           (usually with broker_port=8883)
 *   ca_file=            (PEM CA of the broker, empty: system CAs)
 *   cert_file= key_file=    (PEM client certificate and key, optional)
 *   tls_verify=true     (false: no certificate check, tests only)
 *   peer_name=          (name in the broker certificate if broker_addr is an IP)
 *   Relative paths are relative to setting.ini.
 *
 *   [ZONES]
 *   list=salotto, ingresso
 *   salotto\setpoint=17    (initial setpoint)
 *   ingresso\setpoint=18
 *
 *   [RELAY]
 *   salotto\relaynum=5
 *   ingresso\relaynum=2
 *
 *   [REGULATION]
 *   min_cycle_s=180
 *   sensor_timeout_s=900
 *
 *   [MODES]             (house mode from the RoomSense panel, interface §8)
 *   window_temp=8       (windows open: every zone at min(own, window_temp))
 *   window_min=30       (then back to normal)
 *   away_temp=15        (away: min(own, away_temp) until normal)
 *   boost_temp=25       (boost: every zone at max(own, boost_temp))
 *   boost_min=30        (then back to normal)
 *
 *   [FROST_PROTECTION]  (zones without sensor data, outdoor below outdoor_below)
 *   enabled=true
 *   outdoor_below=6
 *   on_min=10  period_min=60
 *   outdoor_max_age_min=180     (older outdoor data counts as unknown)
 *   outdoor_unknown_protect=true
 *
 *   [VALVE_EXERCISE]    (anti-seize: cycle the relays idle for idle_days)
 *   enabled=true
 *   day=sunday          (monday..sunday or 1..7)
 *   time=07:00
 *   cycles=3  on_s=60  off_s=60  idle_days=7
 *
 *   [WEATHER]
 *   provider=metno      (metno = api.met.no, wttr = wttr.in)
 *   location=home       (name shown; wttr.in place)
 *   lat=45.1234  lon=12.3456  altitude=100    (metno)
 *   contact=            (metno: e-mail or URL for the User-Agent, asked by met.no)
 *   poll_s=600
 *
 *   [LOG]
 *   file=log/AcquaThermoNet.log   (relative to setting.ini, empty: no file)
 *   max_kb=512
 *   files=3
 *
 *   [TELEGRAM]          (alarms and status via a Telegram bot)
 *   enabled=false
 *   token=              (from @BotFather; like the MQTT password, plain text)
 *   allowed_chats=      (chat ids, comma separated: the only ones answered
 *                        and notified; others are logged with their id)
 *   name=AcquaThermoNet (prefix of the messages, useful with several devices)
 *   reminder_h=6        (still active alarms repeated every N hours, 0 = off)
 *   mqtt_down_min=10    (MQTT disconnected this long: alarm)
 *   api_url=https://api.telegram.org   (tests only)
 *
 *   [RELAY_LOG]         (CSV of the relay state changes, one file per month)
 *   file=log/relays.csv (-> log/relays-YYYY-MM.csv, empty: disabled)
 *   keep_months=24
 */
/* Valve exercise: relays idle for idleDays are switched ON/OFF weekly */
struct ExerciseConfig
{
    bool  enabled   = true;
    int   dayOfWeek = 7;            /* 1 = Monday .. 7 = Sunday (QDate::dayOfWeek) */
    QTime time      = QTime(7, 0);  /* local time */
    int   cycles    = 3;
    int   onS       = 60;
    int   offS      = 60;
    int   idleDays  = 7;
};

/* Frost protection: zones without sensor data, outdoor cold -> on_min ON every period_min */
struct FrostConfig
{
    bool    enabled             = true;
    double  outdoorBelow        = 6.0;      /* °C, strictly below */
    int     onMin               = 10;
    int     periodMin           = 60;
    int     outdoorMaxAgeMin    = 180;
    bool    unknownProtect      = true;     /* no (recent) outdoor data: protect */
};

/* Thermoregulation timings */
struct RegulationConfig
{
    int minCycleS       = 180;      /* min time between two switches of a zone, 0 = off */
    int sensorTimeoutS  = 15*60;    /* no sensor data for this long: zone forced OFF */
    int relaySettleMs   = 3000;     /* relay feedback ignored this long after a command (not in ini) */
    ExerciseConfig exercise;
    FrostConfig frost;
};

/* Telegram bot */
struct TelegramConfig
{
    bool            enabled     = false;
    QString         token;
    QList<qint64>   allowedChats;
    QString         name        = "AcquaThermoNet";
    int             reminderH   = 6;
    int             mqttDownMin = 10;
    QString         apiUrl      = "https://api.telegram.org";
};

/* Outdoor weather source */
struct WeatherConfig
{
    enum Provider { Wttr, MetNo };
    Provider provider   = Wttr;
    QString  location;              /* shown in the status bar; wttr.in place */
    double   lat        = 0;
    double   lon        = 0;
    int      altitude   = 0;
    bool     hasCoords  = false;
    QString  contact;               /* met.no User-Agent contact */
    int      pollS      = 600;

    /* Enough data to poll the selected provider */
    bool enabled() const { return provider == MetNo ? hasCoords : !location.isEmpty(); }
};

class Configuration
{
public:
    /* statePath empty: state.ini in the directory of settingsPath */
    explicit Configuration(const QString &settingsPath, const QString &statePath = QString());

    /* Create settingsPath from defaultPath if it does not exist yet */
    static bool ensureSettings(const QString &settingsPath, const QString &defaultPath);

    const QString &path() const { return m_path; }
    const QString &statePath() const { return m_statePath; }

    void loadMqttInfo(mqtt_brk_t &mqi) const;
    void loadSerial(QString &port, qint32 &baud) const;
    RegulationConfig loadRegulation() const;
    WeatherConfig loadWeather() const;
    void loadLog(QString &path, qint64 &maxBytes, int &files) const;
    void loadRelayLog(QString &path, int &keepMonths) const;
    TelegramConfig loadTelegram() const;
    ModeConfig loadModes() const;

    /* state.ini [MODE] house: "normal", "window", "away", "boost" (empty:
     * never set); until: end of window or boost, epoch secs, 0 = none */
    QString loadHouseMode() const;
    qint64 loadHouseModeUntil() const;
    void saveHouseMode(const QString &mode, qint64 untilS = 0);

    /* state.ini [APP] clean_exit: -1 never started, 0 unexpected stop, 1 clean */
    int  lastExitState() const;
    void setCleanExit(bool clean);
    QString uniqueId(const QString &fallback);

    QVector<ZoneData> loadZones();
    void saveSetPoints(const QMap<QString, double> &setpoints);

    /* state.ini [RELAYS] <n>\last_on: relay -> epoch secs */
    QMap<int, qint64> loadRelayLastOn() const;
    void saveRelayLastOn(int relay, qint64 epochSecs);

private:
    QString m_path;
    QString m_statePath;

    static bool validZoneName(const QString &name);
    static int  relayNum(QSettings &settings, const QString &zoneName);
    static void syncToDisk(QSettings &settings);
};

#endif // CONFIGURATION_H
