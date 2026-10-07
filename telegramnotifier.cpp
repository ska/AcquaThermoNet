#include "telegramnotifier.h"
#include "logging.h"
#include "monoclock.h"
#include "climatezones.h"
#include "relaylog.h"
#include <QDateTime>

static const char ICON_ALARM[] = u8"⚠️";     /* warning sign */
static const char ICON_OK[]    = u8"✅";           /* check mark */
static const char ICON_INFO[]  = u8"ℹ️";     /* information */
static const char ICON_WINDOW[] = u8"\U0001FA9F";    /* window */

/**
 * @brief TelegramNotifier::TelegramNotifier
 * @param zones
 * @param config
 * @param relayLog  for /today and /week, nullptr if the relay log is disabled
 * @param parent
 */
TelegramNotifier::TelegramNotifier(ZoneModel *zones, const TelegramConfig &config, RelayLog *relayLog, QObject *parent) :
    QObject(parent),
    m_zones(zones),
    m_config(config),
    m_relayLog(relayLog),
    m_modbusOffline(false),
    m_serialClosed(false),
    m_serialKnown(false),
    m_mqttConnected(false),
    m_mqttAlarm(false),
    m_gatewayState(GatewayUnknown),
    m_gatewayAlarm(false),
    m_weatherKnown(false),
    m_startMs(MonoClock::nowMs())
{
    m_alarms.resize(m_zones->count());
    connect(m_zones, &ZoneModel::zoneChanged, this, &TelegramNotifier::onZoneChanged);

    m_reminder = new QTimer(this);
    connect(m_reminder, &QTimer::timeout, this, &TelegramNotifier::remind);
    if(m_config.reminderH > 0)
        m_reminder->start(m_config.reminderH * 3600 * 1000);

    /* MQTT is disconnected at start too: alarm if never connected in time */
    m_mqttDownTimer = new QTimer(this);
    m_mqttDownTimer->setSingleShot(true);
    connect(m_mqttDownTimer, &QTimer::timeout, this, &TelegramNotifier::onMqttDown);
    m_mqttDownTimer->start(m_config.mqttDownMin * 60 * 1000);

    /* RoomSense offline: a restart (start.sh loop, update) is shorter */
    m_gatewayDownTimer = new QTimer(this);
    m_gatewayDownTimer->setSingleShot(true);
    connect(m_gatewayDownTimer, &QTimer::timeout, this, &TelegramNotifier::onGatewayDown);
}

/**
 * @brief TelegramNotifier::stamp
 * "<icon> [<name> HH:mm] <text>" (with the date when not today)
 */
QString TelegramNotifier::stamp(const char *icon, const QString &text) const
{
    const QDateTime now = QDateTime::currentDateTime();
    return QString("%1 [%2 %3] %4").arg(QString::fromUtf8(icon), m_config.name, now.toString("HH:mm"), text);
}

QString TelegramNotifier::zoneName(int zone) const
{
    QString n = m_zones->zone(zone).name;
    if(!n.isEmpty())
        n[0] = n[0].toUpper();
    return n;
}

/**
 * @brief TelegramNotifier::duration
 * @return "2 d 3 h", "4 h 12 min", "7 min"
 */
QString TelegramNotifier::duration(qint64 secs)
{
    secs = qMax<qint64>(0, secs);
    if(secs >= 86400)
        return QString("%1 d %2 h").arg(secs / 86400).arg((secs % 86400) / 3600);
    if(secs >= 3600)
        return QString("%1 h %2 min").arg(secs / 3600).arg((secs % 3600) / 60);
    return QString("%1 min").arg(secs / 60);
}

/**
 * @brief TelegramNotifier::announceStart
 * @param lastExit  -1 first start, 0 unexpected, 1 clean
 */
void TelegramNotifier::announceStart(int lastExit)
{
    QString text = QString("started, version %1.").arg(SW_VER);
    const char *icon = ICON_INFO;
    if(lastExit == 0)
    {
        icon = ICON_ALARM;
        text += " The previous run did NOT stop cleanly (crash, power loss or watchdog reboot).";
    }
    else if(lastExit < 0)
        text += " First start.";
    if(m_serialKnown && m_serialClosed)
        text += " Serial port closed: relays not reachable.";
    emit broadcast(stamp(icon, text + " /help for the commands."));
}

void TelegramNotifier::announceStop()
{
    emit broadcast(stamp(ICON_INFO, "stopping (clean shutdown, relays OFF)."));
}

/**
 * @brief TelegramNotifier::onZoneChanged
 * Alarm flags of the zone against the last ones told
 */
void TelegramNotifier::onZoneChanged(int zone)
{
    if(zone < 0 || zone >= m_alarms.size())
        return;
    const ZoneData &z = m_zones->zone(zone);
    ZoneAlarms &a = m_alarms[zone];
    const QString name = zoneName(zone);

    if(z.sensorLost != a.sensorLost)
    {
        a.sensorLost = z.sensorLost;
        emit broadcast(z.sensorLost ? stamp(ICON_ALARM, name + ": no sensor data, zone OFF.")
                                    : stamp(ICON_OK, name + ": sensor data back."));
    }
    if(z.relayFault != a.relayFault)
    {
        a.relayFault = z.relayFault;
        emit broadcast(z.relayFault ? stamp(ICON_ALARM, QString("%1: relay %2 does not follow the commands (FAULT).").arg(name).arg(z.relay))
                                    : stamp(ICON_OK, QString("%1: relay %2 follows the commands again.").arg(name).arg(z.relay)));
    }
    if(z.frostProtection != a.frost)
    {
        a.frost = z.frostProtection;
        emit broadcast(z.frostProtection ? stamp(ICON_ALARM, name + ": FROST MODE (no sensor data and cold outside): timed heating ON.")
                                         : stamp(ICON_OK, name + ": frost mode off."));
    }

    /* hysteresis: a reading around the threshold must not flood the chat */
    if(z.lastSeenMs != 0 && z.battery > 0)
    {
        if(!a.batteryLow && z.battery < BATTERY_LOW_PCT)
        {
            a.batteryLow = true;
            emit broadcast(stamp(ICON_ALARM, QString("%1: sensor battery low (%2%).").arg(name).arg(z.battery)));
        }
        else if(a.batteryLow && z.battery >= BATTERY_OK_PCT)
        {
            a.batteryLow = false;
            emit broadcast(stamp(ICON_OK, QString("%1: sensor battery OK (%2%).").arg(name).arg(z.battery)));
        }
    }
}

void TelegramNotifier::setModbusOnline(bool online)
{
    if(online == !m_modbusOffline)
        return;
    m_modbusOffline = !online;
    emit broadcast(online ? stamp(ICON_OK, "Modbus relay board online again.")
                          : stamp(ICON_ALARM, "Modbus relay board OFFLINE: relays not controlled."));
}

/**
 * @brief TelegramNotifier::setSerialOpen
 * First call: initial state, told by announceStart
 */
void TelegramNotifier::setSerialOpen(bool open)
{
    if(!m_serialKnown)
    {
        m_serialKnown = true;
        m_serialClosed = !open;
        return;
    }
    if(open == !m_serialClosed)
        return;
    m_serialClosed = !open;
    emit broadcast(open ? stamp(ICON_OK, "serial port open again.")
                        : stamp(ICON_ALARM, "serial port LOST: relays not reachable."));
}

void TelegramNotifier::setMqttConnected(bool connected)
{
    if(connected == m_mqttConnected)
        return;
    m_mqttConnected = connected;

    if(connected)
    {
        m_mqttDownTimer->stop();
        if(m_mqttAlarm)
        {
            m_mqttAlarm = false;
            emit broadcast(stamp(ICON_OK, "MQTT broker connected again."));
        }
    }
    else if(!m_mqttDownTimer->isActive() && !m_mqttAlarm)
        m_mqttDownTimer->start(m_config.mqttDownMin * 60 * 1000);
}

void TelegramNotifier::onMqttDown()
{
    if(m_mqttConnected || m_mqttAlarm)
        return;
    m_mqttAlarm = true;
    emit broadcast(stamp(ICON_ALARM, QString("MQTT broker not connected for %1 min: no sensor data, no Home Assistant.")
                                        .arg(m_config.mqttDownMin)));
}

void TelegramNotifier::setGatewayState(int state)
{
    if(state == m_gatewayState)
        return;
    m_gatewayState = state;

    if(state == GatewayOnline)
    {
        m_gatewayDownTimer->stop();
        if(m_gatewayAlarm)
        {
            m_gatewayAlarm = false;
            emit broadcast(stamp(ICON_OK, "RoomSense gateway online again."));
        }
    }
    else if(state == GatewayOffline)
    {
        if(!m_gatewayAlarm && !m_gatewayDownTimer->isActive())
            m_gatewayDownTimer->start(m_config.gatewayDownMin * 60 * 1000);
    }
    else
        m_gatewayDownTimer->stop();     /* broker down: told by the MQTT alarm */
}

void TelegramNotifier::onGatewayDown()
{
    if(m_gatewayState != GatewayOffline || m_gatewayAlarm)
        return;
    m_gatewayAlarm = true;
    emit broadcast(stamp(ICON_ALARM, QString("RoomSense gateway OFFLINE for %1 min: no sensor data, "
                                             "the zones go OFF after the sensor timeout.").arg(m_config.gatewayDownMin)));
}

void TelegramNotifier::onWindowOpened(int zone, double fromTemp, double toTemp, int minutes)
{
    emit broadcast(stamp(ICON_WINDOW, QString("%1: window open? Temperature %2 -> %3°C in %4 min.")
                                          .arg(zoneName(zone)).arg(fromTemp, 0, 'f', 1).arg(toTemp, 0, 'f', 1).arg(minutes)));
}

void TelegramNotifier::onWindowClosed(int zone, double lowestTemp, int minutes)
{
    emit broadcast(stamp(ICON_WINDOW, QString("%1: temperature rising again, window closed? (lowest %2°C, %3 min after the drop)")
                                          .arg(zoneName(zone)).arg(lowestTemp, 0, 'f', 1).arg(minutes)));
}

void TelegramNotifier::setWeather(weather_t info)
{
    m_weather = info;
    m_weatherKnown = true;
}

QStringList TelegramNotifier::activeAlarms() const
{
    QStringList out;
    if(m_modbusOffline)
        out << "Modbus board offline";
    if(m_serialKnown && m_serialClosed)
        out << "serial port closed";
    if(m_mqttAlarm)
        out << "MQTT not connected";
    if(m_gatewayAlarm)
        out << "RoomSense offline";
    for(int zone=0; zone<m_alarms.size(); zone++)
    {
        const ZoneAlarms &a = m_alarms[zone];
        const QString name = zoneName(zone);
        if(a.sensorLost)
            out << name + " no sensor";
        if(a.relayFault)
            out << name + " relay fault";
        if(a.frost)
            out << name + " frost mode";
        if(a.batteryLow)
            out << name + " battery low";
    }
    return out;
}

/**
 * @brief TelegramNotifier::remind
 * Still active alarms, every reminder_h hours
 */
void TelegramNotifier::remind()
{
    const QStringList alarms = activeAlarms();
    if(!alarms.isEmpty())
        emit broadcast(stamp(ICON_ALARM, "still active: " + alarms.join(", ") + "."));
}

/**
 * @brief TelegramNotifier::statusText
 */
QString TelegramNotifier::statusText() const
{
    QStringList l;
    l << QString("%1 %2, up %3").arg(m_config.name, SW_VER, duration((MonoClock::nowMs() - m_startMs) / 1000));

    const char *gateway = m_gatewayState == GatewayOnline ? "online"
                        : (m_gatewayState == GatewayOffline ? "OFFLINE" : "unknown");
    QString sys = QString("MQTT %1 | RoomSense %2 | Modbus %3").arg(m_mqttConnected ? "connected" : "NOT connected", gateway,
                                                                    m_serialKnown && m_serialClosed ? "port CLOSED"
                                                                                                    : (m_modbusOffline ? "OFFLINE" : "online"));
    if(m_weatherKnown)
        sys += QString(" | outdoor %1 %2°C").arg(m_weather.comune).arg(m_weather.temp, 0, 'f', 1);
    if(m_zones->houseMode() == ZoneModel::ModeWindow)
        sys += QString(" | windows open, %1 left").arg(duration(m_zones->remainingS()));
    else if(m_zones->houseMode() == ZoneModel::ModeBoost)
        sys += QString(" | boost, %1 left").arg(duration(m_zones->remainingS()));
    else if(m_zones->houseMode() == ZoneModel::ModeAway)
        sys += " | away";
    l << sys << "";

    for(int zone=0; zone<m_zones->count(); zone++)
    {
        const ZoneData &z = m_zones->zone(zone);
        QString line = QString("%1: %2 set %3").arg(zoneName(zone),
                                                    z.lastSeenMs ? QString("%1°").arg(z.temp, 0, 'f', 1) : QString("--.-°"),
                                                    QString("%1°").arg(z.target, 0, 'f', 1));
        line += z.heat ? " - heating" : " - idle";
        if(z.relay > 0)
            line += z.relayState == 1 ? ", relay ON" : (z.relayState == 0 ? ", relay OFF" : ", relay ?");
        l << line;
    }

    const QStringList alarms = activeAlarms();
    l << "" << (alarms.isEmpty() ? QString("No active alarm.") : "Alarms: " + alarms.join(", ") + ".");
    return l.join('\n');
}

/**
 * @brief TelegramNotifier::zoneText
 */
QString TelegramNotifier::zoneText(int zone) const
{
    const ZoneData &z = m_zones->zone(zone);
    QStringList l;
    l << zoneName(zone);
    if(z.lastSeenMs)
    {
        l << QString("Temperature %1°C, humidity %2%, battery %3%").arg(z.temp, 0, 'f', 1).arg(z.humidity).arg(z.battery);
        l << QString("Last data %1 ago").arg(duration((MonoClock::nowMs() - z.lastSeenMs) / 1000));
    }
    else
        l << "No sensor data since the start";
    if(m_zones->houseMode() == ZoneModel::ModeNormal)
        l << QString("Setpoint %1°C, %2").arg(z.setPoint, 0, 'f', 1).arg(z.heat ? "heating" : "idle");
    else
        l << QString("Setpoint %1°C (%2, own %3°C), %4").arg(z.target, 0, 'f', 1).arg(ZoneModel::modeName(m_zones->houseMode()))
                                                         .arg(z.setPoint, 0, 'f', 1).arg(z.heat ? "heating" : "idle");
    if(z.relay > 0)
        l << QString("Relay %1: %2").arg(z.relay).arg(z.relayState == 1 ? "ON" : (z.relayState == 0 ? "OFF" : "unknown"));
    else
        l << "No relay";

    if(z.pendingSwitch >= 0)
        l << QString("Switch %1 in %2 (min cycle)").arg(z.pendingSwitch ? "ON" : "OFF")
                 .arg(duration((z.pendingSwitchAtMs - MonoClock::nowMs()) / 1000 + 59));
    if(z.valveExercise)
        l << "Valve exercise running";
    if(z.relayFault)
        l << "ALARM: relay fault";
    if(z.sensorLost)
        l << "ALARM: no sensor data";
    if(z.frostProtection)
        l << "ALARM: frost mode";
    if(z.lastSeenMs && z.battery > 0 && z.battery < BATTERY_LOW_PCT)
        l << "ALARM: battery low";

    if(m_relayLog && z.relay > 0)
    {
        const QDateTime now = QDateTime::currentDateTime();
        const QMap<QString, qint64> today = m_relayLog->onSeconds(QDateTime(now.date(), QTime(0, 0)).toSecsSinceEpoch(), now.toSecsSinceEpoch());
        qint64 secs = 0;
        for(auto it = today.cbegin(); it != today.cend(); ++it)
        {
            if(it.key().split('+').contains(z.name))
                secs += it.value();
        }
        l << QString("Heating today: %1").arg(duration(secs));
    }
    return l.join('\n');
}

/**
 * @brief TelegramNotifier::onTimeText
 * Relay ON time per zone in the period, from the relay log
 */
QString TelegramNotifier::onTimeText(qint64 fromEpoch, qint64 toEpoch, const QString &label) const
{
    if(!m_relayLog)
        return "Relay log disabled ([RELAY_LOG] file in setting.ini).";

    const QMap<QString, qint64> on = m_relayLog->onSeconds(fromEpoch, toEpoch);
    QStringList l;
    l << QString("Relay ON time %1:").arg(label);

    /* every zone with a relay, also the ones never ON */
    QStringList keys = on.keys();
    for(int zone=0; zone<m_zones->count(); zone++)
    {
        const QString &name = m_zones->zone(zone).name;
        bool listed = false;
        for(const QString &k : keys)
            listed |= k.split('+').contains(name);
        if(m_zones->zone(zone).relay > 0 && !listed)
            keys << name;
    }
    keys.sort();
    for(const QString &k : keys)
    {
        const qint64 s = on.value(k);
        l << QString("%1: %2:%3").arg(k).arg(s / 3600).arg((s % 3600) / 60, 2, 10, QChar('0'));
    }
    return l.join('\n');
}

QString TelegramNotifier::helpText() const
{
    QStringList zones;
    for(int zone=0; zone<m_zones->count(); zone++)
        zones << m_zones->zone(zone).name;
    return QString("%1 commands:\n"
                   "/status - all zones and the system\n"
                   "/zone <name> - details of a zone (%2)\n"
                   "/today - relay ON time today\n"
                   "/week - relay ON time in the last 7 days\n"
                   "/help - this list").arg(m_config.name, zones.join(", "));
}

/**
 * @brief TelegramNotifier::onCommand
 * @param chatId    allowed chat (filtered by TelegramBot)
 * @param text      "/zone salotto", "/status@MyBot", ...
 */
void TelegramNotifier::onCommand(qint64 chatId, const QString &text)
{
    /* no SkipEmptyParts: QString:: deprecated in 5.15, Qt:: missing in 5.13 */
    QStringList words = text.split(' ');
    words.removeAll(QString());
    const QString cmd = words.value(0).section('@', 0, 0).toLower();
    const QString arg = words.value(1).toLower();
    qCInfo(lcTelegram) << "Telegram command from chat" << chatId << ":" << cmd << arg;

    const QDateTime now = QDateTime::currentDateTime();
    if(cmd == "/status")
        emit reply(chatId, statusText());
    else if(cmd == "/zone")
    {
        const int zone = m_zones->indexOf(arg);
        emit reply(chatId, zone >= 0 ? zoneText(zone) : helpText());
    }
    else if(cmd == "/today")
        emit reply(chatId, onTimeText(QDateTime(now.date(), QTime(0, 0)).toSecsSinceEpoch(), now.toSecsSinceEpoch(), "today"));
    else if(cmd == "/week")
        emit reply(chatId, onTimeText(QDateTime(now.date().addDays(-6), QTime(0, 0)).toSecsSinceEpoch(), now.toSecsSinceEpoch(), "in the last 7 days"));
    else
        emit reply(chatId, helpText());
}
