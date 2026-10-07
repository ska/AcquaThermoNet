#include "mqtt.h"
#include "logging.h"
#include "mqttparse.h"
#include <QFile>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>

/**
 * @brief Mqtt::Mqtt
 * @param broker
 * @param uniqueId  stable id for clientId and HA entities
 * @param zones
 * @param parent
 */
Mqtt::Mqtt(const mqtt_brk_t &broker, const QString &uniqueId, ZoneModel *zones, QObject *parent) :
    QObject(parent),
    m_zones(zones),
    m_mbi(broker),
    m_uniqueId(uniqueId)
{
    m_connectingTry = 0;
    m_init = true;
    m_shuttingDown = false;
    m_tlsInvalid = false;
    m_tlsSocket = nullptr;
    m_tlsConnectTimer = nullptr;

    if(m_mbi.address.isEmpty())
    {
        qCWarning(lcMqtt) << "No MQTT broker address, using localhost";
        m_mbi.address = "localhost";
    }

    qCInfo(lcMqtt).noquote() << "MQTT Broker: " << QString("%1:%2").arg(m_mbi.address).arg(m_mbi.port);
    m_client = new QMqttClient(this);
    m_client->setHostname( m_mbi.address );
    m_client->setPort( m_mbi.port );
    if( !m_mbi.uname.isEmpty() && !m_mbi.password.isEmpty() )
    {
        qCInfo(lcMqtt).noquote() << "MQTT Username: " << m_mbi.uname;

        m_client->setUsername( m_mbi.uname );
        m_client->setPassword( m_mbi.password );
    }
    qCInfo(lcMqtt).noquote() << "MQTT UniqueID: " << m_uniqueId;
    /* Stable across hostname changes and SW updates */
    m_client->setClientId( QString(SW_NAME "-%1").arg(m_uniqueId) );

    /* Broker publishes "offline" if we drop without a clean disconnect */
    m_client->setWillTopic(STATUS_TOPIC);
    m_client->setWillQoS(1);
    m_client->setWillMessage(PAYLOAD_OFFLINE);
    m_client->setWillRetain(true);
    m_client->setKeepAlive(10);

    if(m_mbi.tls && !setupTls())
    {
        qCCritical(lcMqtt) << "MQTT TLS configuration invalid: not connecting";
        m_tlsInvalid = true;
    }

    m_ReconnectTimer = new QTimer(this);
    m_ReconnectTimer->setSingleShot(true);
    connect(m_ReconnectTimer, &QTimer::timeout, this, &Mqtt::connectToBroker);

    /*
     * SIGNALS
     */
    connect(m_client, &QMqttClient::stateChanged, this, &Mqtt::stateChangedSlot );
    connect(m_client, &QMqttClient::errorChanged, this, [](QMqttClient::ClientError error) {
        if(error != QMqttClient::NoError)
            qCWarning(lcMqtt) << "MQTT client error: " << error;
    });
    connect(m_client, &QMqttClient::messageReceived, this, &Mqtt::messageReceivedSlot);

    /* Local state is the source of truth: publish what changes */
    connect(m_zones, &ZoneModel::setPointChanged,   this, &Mqtt::publishSetPoint);
    connect(m_zones, &ZoneModel::heatChanged,       this, &Mqtt::publishMode);
    connect(m_zones, &ZoneModel::houseModeChanged,  this, &Mqtt::publishHouseMode);
    m_houseModeTimer = new QTimer(this);
    connect(m_houseModeTimer, &QTimer::timeout, this, [this] {
        if( m_zones->remainingS() > 0 )
            publishHouseMode();
    });
    m_houseModeTimer->start(60 * 1000);

    /* Chrono state for HA: on zone and mode changes, and every minute
     * (a slot with the same setpoint changes only the next change) */
    m_chronoSent.resize(m_zones->count());
    auto publishAllChrono = [this] {
        for(int zone = 0; zone < m_zones->count(); zone++)
            publishChrono(zone);
    };
    connect(m_zones, &ZoneModel::zoneChanged, this, &Mqtt::publishChrono);
    connect(m_zones, &ZoneModel::houseModeChanged, this, publishAllChrono);
    connect(m_houseModeTimer, &QTimer::timeout, this, publishAllChrono);
    /* Chrono program for RoomSense: when it changes */
    m_profileSent.resize(m_zones->count());
    connect(m_zones, &ZoneModel::zoneChanged, this, &Mqtt::publishChronoProfile);

    connectToBroker();
}

/**
 * @brief Mqtt::setupTls
 * QSslSocket configured here and given to QtMqtt as transport: works
 * with QtMqtt 5.13 (device), which has no connectToHostEncrypted(QSslConfiguration)
 * @return false if a configured file cannot be used
 */
bool Mqtt::setupTls()
{
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    bool ok = true;

    if(!m_mbi.caFile.isEmpty())
    {
        const QList<QSslCertificate> ca = QSslCertificate::fromPath(m_mbi.caFile, QSsl::Pem);
        if(ca.isEmpty())
        {
            qCCritical(lcMqtt).noquote() << "MQTT TLS: no certificate in ca_file" << m_mbi.caFile;
            ok = false;
        }
        ssl.setCaCertificates(ca);
    }

    if(!m_mbi.certFile.isEmpty())
    {
        const QList<QSslCertificate> cert = QSslCertificate::fromPath(m_mbi.certFile, QSsl::Pem);
        QFile keyFile(m_mbi.keyFile);
        QSslKey key;
        if(keyFile.open(QIODevice::ReadOnly))
        {
            const QByteArray pem = keyFile.readAll();
            key = QSslKey(pem, QSsl::Rsa);
            if(key.isNull())
                key = QSslKey(pem, QSsl::Ec);
        }
        if(cert.isEmpty() || key.isNull())
        {
            qCCritical(lcMqtt).noquote() << "MQTT TLS: cannot load cert_file/key_file" << m_mbi.certFile << m_mbi.keyFile;
            ok = false;
        }
        else
        {
            ssl.setLocalCertificate(cert.first());
            ssl.setPrivateKey(key);
        }
    }

    if(!m_mbi.tlsVerify)
    {
        qCWarning(lcMqtt) << "MQTT TLS: broker certificate NOT verified (tls_verify=false)";
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    }
    else
        ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);

    m_tlsSocket = new QSslSocket(this);
    m_tlsSocket->setSslConfiguration(ssl);
    connect(m_tlsSocket, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors), this, [](const QList<QSslError> &errors) {
        for(const QSslError &e : errors)
            qCWarning(lcMqtt).noquote() << "MQTT TLS error:" << e.errorString();
    });
    connect(m_tlsSocket, &QSslSocket::encrypted,              this, &Mqtt::onTlsEncrypted);
    connect(m_tlsSocket, &QAbstractSocket::stateChanged,      this, &Mqtt::onTlsStateChanged);
    m_client->setTransport(m_tlsSocket, QMqttClient::SecureSocket);

    m_tlsConnectTimer = new QTimer(this);
    m_tlsConnectTimer->setSingleShot(true);
    connect(m_tlsConnectTimer, &QTimer::timeout, this, [this] {
        qCWarning(lcMqtt) << "MQTT TLS connection timeout";
        m_tlsSocket->abort();
    });

    qCInfo(lcMqtt).noquote() << "MQTT TLS on, verify" << m_mbi.tlsVerify
                             << (m_mbi.caFile.isEmpty() ? "system CAs" : "CA " + m_mbi.caFile)
                             << (m_mbi.certFile.isEmpty() ? "" : ", client certificate");
    return ok;
}

/**
 * @brief Mqtt::connectToBroker
 * Plain: QtMqtt opens the socket. TLS: QtMqtt does not follow an external
 * transport until it is encrypted, so the TLS socket is opened here and
 * MQTT CONNECT starts in onTlsEncrypted().
 */
void Mqtt::connectToBroker()
{
    if(m_tlsInvalid)
        return;
    if(!m_tlsSocket)
    {
        m_client->connectToHost();
        return;
    }
    if(m_tlsSocket->state() != QAbstractSocket::UnconnectedState)
        return;

    m_tlsConnectTimer->start(TLS_CONNECT_TIMEOUT_MS);
    /* peer_name: name to verify in the certificate instead of broker_addr */
    if(m_mbi.peerName.isEmpty())
        m_tlsSocket->connectToHostEncrypted(m_mbi.address, m_mbi.port);
    else
        m_tlsSocket->connectToHostEncrypted(m_mbi.address, m_mbi.port, m_mbi.peerName);
}

/**
 * @brief Mqtt::onTlsEncrypted
 * Transport ready: QtMqtt sends CONNECT at once on a connected transport
 */
void Mqtt::onTlsEncrypted()
{
    m_tlsConnectTimer->stop();
    qCDebug(lcMqtt) << "MQTT TLS established" << m_tlsSocket->sessionProtocol();
    m_client->connectToHost();
}

/**
 * @brief Mqtt::onTlsStateChanged
 * TCP/TLS failures happen before QtMqtt knows about the connection:
 * retry from here with the same backoff
 * @param state
 */
void Mqtt::onTlsStateChanged(QAbstractSocket::SocketState state)
{
    qCDebug(lcMqtt) << "MQTT TLS socket" << state << "client" << m_client->state();
    if(state != QAbstractSocket::UnconnectedState)
        return;
    m_tlsConnectTimer->stop();
    if(m_shuttingDown)
        return;

    if(m_client->state() != QMqttClient::Disconnected)
    {
        /* Dropped while connected. QtMqtt only watches aboutToClose on an
         * external transport (disconnectFromHost would try to write DISCONNECT
         * and stay "Connected"): close() emits it, QtMqtt goes Disconnected
         * and stateChangedSlot retries. */
        m_tlsSocket->close();
        return;
    }
    qCWarning(lcMqtt) << "MQTT TLS connection failed:" << m_tlsSocket->error();
    scheduleReconnect();
}

/**
 * @brief Mqtt::scheduleReconnect
 * Backoff 1, 2, 5, 10, then 30s
 */
void Mqtt::scheduleReconnect()
{
    static const int backoff[] = { 1000, 2000, 5000, 10000, 30000 };
    static const int backoffLen = sizeof(backoff) / sizeof(backoff[0]);

    if(m_shuttingDown || m_ReconnectTimer->isActive())
        return;

    const int delay = backoff[qMin<int>(m_connectingTry, backoffLen - 1)];
    qCDebug(lcMqtt) << "MQTT disconnected, retry in " << delay << "ms";
    m_init = true;
    m_connectingTry++;
    m_ReconnectTimer->start(delay);
}

Mqtt::~Mqtt()
{
    if(!m_shuttingDown)
        shutdown();
}

/**
 * @brief Mqtt::shutdown
 * Clean disconnect does not trigger the Will: publish offline ourselves.
 * No reconnect afterwards; keep processing events to flush the socket.
 */
void Mqtt::shutdown()
{
    m_shuttingDown = true;
    m_ReconnectTimer->stop();
    if(m_client->state() == QMqttClient::Connected)
        m_client->publish(QMqttTopicName(STATUS_TOPIC), PAYLOAD_OFFLINE, 1, true);
    m_client->disconnectFromHost();
}

/**
 * @brief Mqtt::localAddress
 * Socket of the connection: ours with TLS, the one of QtMqtt otherwise
 */
QHostAddress Mqtt::localAddress() const
{
    if(m_client->state() != QMqttClient::Connected)
        return QHostAddress();
    const QAbstractSocket *socket = m_tlsSocket ? m_tlsSocket : qobject_cast<QAbstractSocket *>(m_client->transport());
    return socket ? socket->localAddress() : QHostAddress();
}

/**
 * @brief Mqtt::zoneTopic
 * @return AcquaThermoNet/<zone>
 */
QString Mqtt::zoneTopic(int zone) const
{
    return BASE_TOPIC "/" + m_zones->zone(zone).name;
}

/**
 * @brief Mqtt::sensorTopic
 * @return RoomSense/apartment/<zone>/data
 */
QString Mqtt::sensorTopic(int zone) const
{
    return BASE_TOPIC_SENSOR "/" + m_zones->zone(zone).name + "/" TAIL_DATA;
}

/**
 * @brief Mqtt::stateChangedSlot
 */
void Mqtt::stateChangedSlot()
{
    const QMqttClient::ClientState state = m_client->state();
    emit clientStateChanged( (quint8)state );

    if (state == QMqttClient::Disconnected)
    {
        /* TLS: the socket may still be open (e.g. broker refused CONNECT) */
        if(m_tlsSocket && m_tlsSocket->state() != QAbstractSocket::UnconnectedState)
            m_tlsSocket->abort();
        scheduleReconnect();
    }
    else if(state == QMqttClient::Connecting)
    {
        qCDebug(lcMqtt) << "QMqttClient Connecting " << m_connectingTry;
    }
    else if(state == QMqttClient::Connected)
    {
        qCInfo(lcMqtt) << "MQTT Client connected to broker ";
        m_connectingTry = 0;
        m_ReconnectTimer->stop();
        m_client->publish(QMqttTopicName(STATUS_TOPIC), PAYLOAD_ONLINE, 1, true);
        if(m_init)
        {
            m_init = false;

            MqttHomeAssistantDiscovery();

            const QStringList filters = { HA_STATUS_TOPIC, BASE_TOPIC "/#", BASE_TOPIC_SENSOR "/#" };
            for(const QString &filter : filters)
            {
                if( !m_client->subscribe(QMqttTopicFilter(filter)) )
                    qCWarning(lcMqtt) << "subscription error on topic: " << filter;
                else
                    qCDebug(lcMqtt) << "subscription OK on topic: " << filter;
            }
        }
    }
}

/**
 * @brief Mqtt::ForceReconnectToHost
 */
void Mqtt::forceReconnectToHost()
{
    m_connectingTry = 0;
    m_ReconnectTimer->stop();
    if(m_client->state() == QMqttClient::Disconnected)
        connectToBroker();
    else
        m_client->disconnectFromHost();     // stateChangedSlot schedules the reconnect
}

/**
 * @brief Mqtt::messageReceivedSlot
 * @param message
 * @param topic
 */
void Mqtt::messageReceivedSlot(const QByteArray &message, const QMqttTopicName &topic)
{
    const QString topicName = topic.name();
    QString zoneName;
    int zone;

    //qCDebug(lcMqtt) << "Received Topic: " << topicName << " Message: " << message;

    //homeassistant/status
    if( topicName == HA_STATUS_TOPIC )
    {
        if( message == PAYLOAD_ONLINE )
        {
            qCInfo(lcMqtt) << "Home Assistant online, republish discovery";
            MqttHomeAssistantDiscovery();
        }
        return;
    }

    //AcquaThermoNet/mode/set: state published after every command,
    //also when unchanged or refused
    if( topicName == MODE_SET_TOPIC )
    {
        ZoneModel::HouseMode mode;
        if( !ZoneModel::parseMode(QString::fromUtf8(message).trimmed(), mode) )
            qCWarning(lcMqtt) << "Invalid house mode:" << message;
        else
            m_zones->setHouseMode(mode);
        publishHouseMode();
        return;
    }

    //AcquaThermoNet/ZONA/set_temp
    if( MqttParse::matchZoneTopic(topicName, BASE_TOPIC, TAIL_SET_TEMP, zoneName) )
    {
        zone = m_zones->indexOf(zoneName);
        if( zone < 0 )
            return;

        bool ok;
        double temp = message.toDouble(&ok);
        if( !ok )
        {
            qCWarning(lcMqtt) << "Invalid setpoint for zone" << zoneName << ":" << message;
            return;
        }

        m_zones->setSetPoint(zone, temp);
        return;
    }

    //AcquaThermoNet/ZONA/chrono/set: on/off, state republished
    if( MqttParse::matchZoneTopic(topicName, BASE_TOPIC, TAIL_CHRONO_SET, zoneName) )
    {
        zone = m_zones->indexOf(zoneName);
        if( zone < 0 )
            return;

        bool on;
        if( !MqttParse::parseOnOff(message, on) )
            qCWarning(lcMqtt) << "Invalid chrono command for zone" << zoneName << ":" << message;
        else if( on != m_zones->zone(zone).chrono.enabled )
        {
            ChronoConfig c = m_zones->zone(zone).chrono;
            c.enabled = on;
            if( on && c.weekday.isEmpty() )
                qCWarning(lcMqtt) << "Chrono of zone" << zoneName << "has no weekday slot: stays off";
            else
                m_zones->setChrono(zone, c, "from Home Assistant");
        }
        m_chronoSent[zone].clear();
        publishChrono(zone);
        return;
    }

    //AcquaThermoNet/ZONA/chrono/profile/set: whole program or reset (RoomSense
    //panel), program republished after every command, also when refused
    if( MqttParse::matchZoneTopic(topicName, BASE_TOPIC, TAIL_CHRONO_PROFILE_SET, zoneName) )
    {
        zone = m_zones->indexOf(zoneName);
        if( zone < 0 )
            return;

        const QByteArray before = m_profileSent[zone];
        ChronoConfig c = m_zones->zone(zone).chrono;
        bool reset;
        QString error;
        if( !MqttParse::chronoProgram(message, c, reset, &error) )
            qCWarning(lcMqtt).noquote() << "Invalid chrono program for zone" << zoneName << ":" << error;
        else if( reset )
            m_zones->resetChrono(zone, "over MQTT");
        else
            m_zones->setChrono(zone, c, "over MQTT");
        /* not sent yet by the zone change: same program again */
        if( m_profileSent[zone] == before )
        {
            m_profileSent[zone].clear();
            publishChronoProfile(zone);
        }
        return;
    }

    //AcquaThermoNet/ZONA/set_mode
    if( MqttParse::matchZoneTopic(topicName, BASE_TOPIC, TAIL_SET_MODE, zoneName) )
    {
        zone = m_zones->indexOf(zoneName);
        if( zone < 0 )
            return;

        m_zones->setHeat(zone, message == "heat");
        return;
    }

    //RoomSense/apartment/ZONA/data
    if( MqttParse::matchZoneTopic(topicName, BASE_TOPIC_SENSOR, TAIL_DATA, zoneName) )
    {
        zone = m_zones->indexOf(zoneName);
        if( zone < 0 )
            return;

        parseSensorData(zone, message);
        return;
    }
}

/**
 * @brief Mqtt::parseSensorData
 * @param zone
 * @param message   RoomSense JSON
 */
void Mqtt::parseSensorData(int zone, const QByteArray &message)
{
    ZoneData data = m_zones->zone(zone);
    QString error;
    if( !MqttParse::sensorJson(message, data, &error) )
    {
        qCWarning(lcMqtt) << "Invalid sensor data for zone" << data.name << ":" << error;
        return;
    }
    m_zones->setSensorData(zone, data);
}

/**
 * @brief Mqtt::MqttHomeAssistantDiscovery
 * All messages are retained so HA gets entities and state after a restart
 */
void Mqtt::MqttHomeAssistantDiscovery()
{
    QMqttTopicName tn;
    QJsonObject  payload;

    for(int zone=0; zone<m_zones->count(); zone++)
    {
        const QString &name = m_zones->zone(zone).name;
        tn.setName("homeassistant/climate/" + name + "/config");

        payload.insert("name",          "clima." + name );
        payload.insert("uniq_id",       m_uniqueId +"_"+ name);

        payload.insert("avty_t",        STATUS_TOPIC);                                  // Topic availability (online/offline)
        //payload.insert("mode_cmd_t",    zoneTopic(zone) + "/" TAIL_SET_MODE);         // Topic per impostare la modalità
        payload.insert("mode_stat_t",   zoneTopic(zone) + "/" TAIL_STATE_MODE);         // Topic per la modalità corrente
        payload.insert("temp_cmd_t",    zoneTopic(zone) + "/" TAIL_SET_TEMP);           // Topic per impostare la temperatura target
        payload.insert("temp_stat_t",   zoneTopic(zone) + "/" TAIL_STATE_TEMP);         // Topic per la temperatura target corrente
        payload.insert("curr_temp_t",   sensorTopic(zone));                             // Topic per la temperatura corrente (deve essere quello del sensore)
        payload.insert("curr_temp_tpl", "{{ value_json.temperature }}");
        payload.insert("min_temp",      QString("%1").arg(TEMP_MIN)  );
        payload.insert("max_temp",      QString("%1").arg(TEMP_MAX)  );
        payload.insert("temp_step",     QString("%1").arg(TEMP_STEP) );

        QJsonArray modesArray;
        modesArray.push_back("off");
        modesArray.push_back("heat");
        payload.insert("modes", modesArray);
        /* chrono: next change, manual, profile as attributes */
        payload.insert("json_attr_t",   zoneTopic(zone) + "/" TAIL_CHRONO);
        payload.insert("device",        MqttParse::zoneDevice(m_uniqueId, name));

        QJsonDocument doc(payload);
        m_client->publish(tn, doc.toJson(QJsonDocument::Compact), 1, true);

        const MqttParse::Message sw = MqttParse::chronoSwitchDiscovery(m_uniqueId, name);
        m_client->publish(QMqttTopicName(sw.topic), sw.payload, 1, true);

        publishMode(zone);
        publishSetPoint(zone);
        m_chronoSent[zone].clear();
        publishChrono(zone);
        m_profileSent[zone].clear();
        publishChronoProfile(zone);
    }
    publishHouseMode();

    for(const MqttParse::Message &m : MqttParse::weatherDiscovery(m_uniqueId, m_weatherEnabled, m_weatherExpireS))
        m_client->publish(QMqttTopicName(m.topic), m.payload, 1, true);
    publishWeather();
}

/**
 * @brief Mqtt::setWeatherConfig
 * @param enabled   weather polled: sensors published, else removed
 * @param expireS   HA shows them unavailable after this time without data
 * @param source    provider, in the payload (attribution)
 */
void Mqtt::setWeatherConfig(bool enabled, int expireS, const QString &source)
{
    m_weatherEnabled = enabled;
    m_weatherExpireS = expireS;
    m_weatherSource  = source;
}

/**
 * @brief Mqtt::setWeather
 * New outdoor data from the weather service
 */
void Mqtt::setWeather(weather_t info)
{
    m_weather = info;
    m_weatherKnown = true;
    publishWeather();
}

/**
 * @brief Mqtt::publishWeather
 * WEATHER_TOPIC (retained), the outdoor sensors of HA
 */
void Mqtt::publishWeather()
{
    if( !m_weatherEnabled || !m_weatherKnown || m_client->state() != QMqttClient::Connected )
        return;
    m_client->publish(QMqttTopicName(WEATHER_TOPIC), MqttParse::weatherState(m_weather, m_weatherSource), 1, true);
}

/**
 * @brief Mqtt::publishSetPoint
 * Publish the applied setpoint on state_temp (retained): during a house
 * mode the mode value, not the zone's own one
 * @param zone
 */
void Mqtt::publishSetPoint(int zone)
{
    QMqttTopicName tn(zoneTopic(zone) + "/" TAIL_STATE_TEMP);
    m_client->publish(tn, QString::number(m_zones->zone(zone).target).toUtf8(), 1, true);
}

/**
 * @brief Mqtt::publishMode
 * Publish current mode on state_mode (retained)
 * @param zone
 */
void Mqtt::publishMode(int zone)
{
    QMqttTopicName tn(zoneTopic(zone) + "/" TAIL_STATE_MODE);
    m_client->publish(tn, m_zones->zone(zone).heat ? "heat" : "off", 1, true);
}

/**
 * @brief Mqtt::publishHouseMode
 * mode/state (retained): {"mode":"window","remaining_s":1740}; also every
 * minute while the window or boost mode runs
 */
void Mqtt::publishHouseMode()
{
    const ZoneModel::HouseMode mode = m_zones->houseMode();
    if( m_client->state() != QMqttClient::Connected )
        return;

    QJsonObject payload;
    payload.insert("mode", ZoneModel::modeName(mode));
    if( mode == ZoneModel::ModeWindow || mode == ZoneModel::ModeBoost )
        payload.insert("remaining_s", m_zones->remainingS());
    m_client->publish(QMqttTopicName(MODE_STATE_TOPIC), QJsonDocument(payload).toJson(QJsonDocument::Compact), 1, true);
}

/**
 * @brief Mqtt::publishChrono
 * <zone>/chrono (retained): chrono switch state and climate attributes,
 * sent only when it changes
 */
void Mqtt::publishChrono(int zone)
{
    if( zone < 0 || zone >= m_chronoSent.size() || m_client->state() != QMqttClient::Connected )
        return;
    const QDateTime now = QDateTime::currentDateTime();
    const QByteArray payload = MqttParse::chronoState(m_zones->zone(zone), m_zones->houseMode() == ZoneModel::ModeAway,
                                                      m_zones->chronoManual(zone, now), now);
    if( payload == m_chronoSent[zone] )
        return;
    m_chronoSent[zone] = payload;
    m_client->publish(QMqttTopicName(zoneTopic(zone) + "/" TAIL_CHRONO), payload, 1, true);
}

/**
 * @brief Mqtt::publishChronoProfile
 * <zone>/chrono/profile (retained): the chrono program, for the RoomSense
 * panel (interface §9), sent only when it changes
 */
void Mqtt::publishChronoProfile(int zone)
{
    if( zone < 0 || zone >= m_profileSent.size() || m_client->state() != QMqttClient::Connected )
        return;
    const QByteArray payload = MqttParse::chronoProfile(m_zones->zone(zone).chrono);
    if( payload == m_profileSent[zone] )
        return;
    m_profileSent[zone] = payload;
    m_client->publish(QMqttTopicName(zoneTopic(zone) + "/" TAIL_CHRONO_PROFILE), payload, 1, true);
}
