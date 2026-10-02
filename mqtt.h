#ifndef MQTT_H
#define MQTT_H

#include "common.h"
#include "QtMqtt/QMqttClient"
#include <QObject>
#include <QTimer>
#include <QHostAddress>
#include <QSslSocket>
#include "climatezones.h"
#include "zonemodel.h"

/*
 * MQTT transport: Home Assistant discovery and state, commands from HA
 * and sensor data into ZoneModel. Holds no zone state of its own.
 */
class Mqtt : public QObject
{
    Q_OBJECT

public:
    /* TLS connection + handshake not done in this time: abort and retry */
    static const int TLS_CONNECT_TIMEOUT_MS = 15000;

    Mqtt(const mqtt_brk_t &broker, const QString &uniqueId, ZoneModel *zones, QObject *parent = nullptr);
    ~Mqtt();

    void shutdown();
    bool isDisconnected() const { return m_client->state() == QMqttClient::Disconnected; }

    /* Local address of the broker connection, null when not connected */
    QHostAddress localAddress() const;

    /* Outdoor weather for HA (sensors on WEATHER_TOPIC): call before the
     * connection. Disabled: the sensors are removed from HA. */
    void setWeatherConfig(bool enabled, int expireS, const QString &source);

public slots:
    void forceReconnectToHost();
    void setWeather(weather_t info);

signals:
    void clientStateChanged( quint8 );

private slots:
    void stateChangedSlot();
    void messageReceivedSlot(const QByteArray &message, const QMqttTopicName &topic);
    void publishSetPoint(int zone);
    void publishMode(int zone);
    void publishHouseMode();
    void publishWeather();

private:
    QMqttClient             *m_client;
    ZoneModel               *m_zones;

    mqtt_brk_t              m_mbi;
    quint16                 m_connectingTry;
    QString                 m_uniqueId;
    bool                    m_init;
    bool                    m_shuttingDown;
    bool                    m_tlsInvalid;       /* never fall back to a weaker connection */
    QSslSocket              *m_tlsSocket;       /* nullptr: plain TCP */
    QTimer                  *m_tlsConnectTimer;
    QTimer                  *m_ReconnectTimer;
    QTimer                  *m_houseModeTimer;  /* window countdown republished every minute */
    /* Outdoor weather */
    bool                    m_weatherEnabled = false;
    int                     m_weatherExpireS = 0;
    QString                 m_weatherSource;
    weather_t               m_weather;
    bool                    m_weatherKnown = false;

    void connectToBroker();
    void scheduleReconnect();
    bool setupTls();
    void onTlsEncrypted();
    void onTlsStateChanged(QAbstractSocket::SocketState state);
    QString zoneTopic(int zone) const;
    QString sensorTopic(int zone) const;
    void MqttHomeAssistantDiscovery();
    void parseSensorData(int zone, const QByteArray &message);
};

#endif // MQTT_H
