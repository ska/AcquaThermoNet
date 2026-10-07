#ifndef TELEGRAMNOTIFIER_H
#define TELEGRAMNOTIFIER_H

#include <QObject>
#include <QTimer>
#include <QVector>
#include "common.h"
#include "configuration.h"
#include "zonemodel.h"

class RelayLog;

/*
 * What is told on Telegram, independent of the transport (TelegramBot):
 *  - alarms on transitions only (one message when a problem starts, one
 *    when it ends), still active ones repeated every reminder_h hours:
 *    sensor lost, relay fault, frost mode, battery low, Modbus board
 *    offline, serial port lost, MQTT down for mqtt_down_min, RoomSense
 *    offline for gateway_down_min
 *  - open window guessed from the temperature (WindowWatch), and closed
 *  - start (with a crash/power loss/watchdog of the previous run) and stop
 *  - answers to /status /zone /today /week /help
 * Every message carries the time of the event: queued messages sent late
 * (no network) still tell when it happened.
 */
class TelegramNotifier : public QObject
{
    Q_OBJECT
public:
    /* Battery alarm hysteresis: low below BATTERY_LOW_PCT, OK again from this */
    static const int BATTERY_OK_PCT = 30;

    TelegramNotifier(ZoneModel *zones, const TelegramConfig &config, RelayLog *relayLog, QObject *parent = nullptr);

    /* lastExit: Configuration::lastExitState() */
    void announceStart(int lastExit);
    void announceStop();

    QString statusText() const;
    QString zoneText(int zone) const;
    QString onTimeText(qint64 fromEpoch, qint64 toEpoch, const QString &label) const;

public slots:
    void onCommand(qint64 chatId, const QString &text);
    void setModbusOnline(bool online);
    void setSerialOpen(bool open);
    void setMqttConnected(bool connected);
    /* GatewayState: offline starts the alarm timer, unknown (broker
     * not connected) stops it, online ends the alarm */
    void setGatewayState(int state);
    void setWeather(weather_t info);
    /* WindowWatch: information, not alarms (no reminder) */
    void onWindowOpened(int zone, double fromTemp, double toTemp, int minutes);
    void onWindowClosed(int zone, double lowestTemp, int minutes);

signals:
    void broadcast(const QString &text);
    void reply(qint64 chatId, const QString &text);

private slots:
    void onZoneChanged(int zone);
    void remind();
    void onMqttDown();
    void onGatewayDown();

private:
    struct ZoneAlarms {
        bool sensorLost = false;
        bool relayFault = false;
        bool frost      = false;
        bool batteryLow = false;
    };

    ZoneModel               *m_zones;
    TelegramConfig          m_config;
    RelayLog                *m_relayLog;
    QVector<ZoneAlarms>     m_alarms;
    bool                    m_modbusOffline;
    bool                    m_serialClosed;
    bool                    m_serialKnown;
    bool                    m_mqttConnected;
    bool                    m_mqttAlarm;
    int                     m_gatewayState;
    bool                    m_gatewayAlarm;
    bool                    m_weatherKnown;
    weather_t               m_weather;
    qint64                  m_startMs;
    QTimer                  *m_reminder;
    QTimer                  *m_mqttDownTimer;
    QTimer                  *m_gatewayDownTimer;

    QString stamp(const char *icon, const QString &text) const;
    QString zoneName(int zone) const;
    QStringList activeAlarms() const;
    QString helpText() const;
    static QString duration(qint64 secs);
};

#endif // TELEGRAMNOTIFIER_H
