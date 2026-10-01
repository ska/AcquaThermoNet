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

public slots:
    void forceReconnectToHost();

signals:
    void clientStateChanged( quint8 );

private slots:
    void stateChangedSlot();
    void messageReceivedSlot(const QByteArray &message, const QMqttTopicName &topic);
    void publishSetPoint(int zone);
    void publishMode(int zone);

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
