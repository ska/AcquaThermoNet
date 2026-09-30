#include "configuration.h"
#include "logging.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <unistd.h>

/**
 * @brief Configuration::Configuration
 * @param path  setting.ini
 */
Configuration::Configuration(const QString &settingsPath, const QString &statePath) :
    m_path(settingsPath),
    m_statePath(statePath)
{
    if(m_statePath.isEmpty())
        m_statePath = QFileInfo(m_path).absoluteDir().filePath("state.ini");
    qCInfo(lcConfig).noquote() << "Configuration:" << m_path << " state:" << m_statePath;
}

/**
 * @brief Configuration::ensureSettings
 * @param settingsPath
 * @param defaultPath   deployed template (setting.default.ini)
 * @return true if settingsPath exists afterwards
 */
bool Configuration::ensureSettings(const QString &settingsPath, const QString &defaultPath)
{
    if(QFile::exists(settingsPath))
        return true;
    if(!QFile::copy(defaultPath, settingsPath))
    {
        qCWarning(lcConfig).noquote() << "No" << settingsPath << "and cannot create it from" << defaultPath;
        return false;
    }
    /* copy keeps the read-only mode of a deployed file: it is edited by hand */
    QFile::setPermissions(settingsPath, QFile::permissions(settingsPath) | QFileDevice::WriteOwner);
    qCInfo(lcConfig).noquote() << "Created" << settingsPath << "from" << defaultPath;
    return true;
}

/**
 * @brief Configuration::syncToDisk
 * Write the ini now, then flush FS buffers to flash (power loss safe)
 */
void Configuration::syncToDisk(QSettings &settings)
{
    settings.sync();
    ::sync();
}

/**
 * @brief Configuration::loadMqttInfo
 * @param mqi
 */
void Configuration::loadMqttInfo(mqtt_brk_t &mqi) const
{
    QSettings settings(m_path, QSettings::IniFormat);
    settings.beginGroup("MQTT");
    mqi.address  = settings.value("broker_addr", mqi.address).toString();
    mqi.port     = settings.value("broker_port", mqi.port).toInt();
    mqi.uname    = settings.value("broker_uname").toString();
    mqi.password = settings.value("broker_password").toString();

    const QDir dir = QFileInfo(m_path).absoluteDir();
    auto path = [&settings, &dir](const char *key) {
        const QString p = settings.value(key).toString().trimmed();
        return p.isEmpty() ? p : dir.filePath(p);
    };
    mqi.tls       = settings.value("tls", false).toBool();
    mqi.tlsVerify = settings.value("tls_verify", true).toBool();
    mqi.caFile    = path("ca_file");
    mqi.certFile  = path("cert_file");
    mqi.keyFile   = path("key_file");
    mqi.peerName  = settings.value("peer_name").toString().trimmed();
    settings.endGroup();
}

/**
 * @brief Configuration::loadSerial
 * [SERIAL] port / baud, defaults com1 / 9600
 * @param port
 * @param baud
 */
void Configuration::loadSerial(QString &port, qint32 &baud) const
{
    QSettings settings(m_path, QSettings::IniFormat);
    port = settings.value("SERIAL/port", "com1").toString();
    baud = settings.value("SERIAL/baud", 9600).toInt();
}

/**
 * @brief Configuration::loadRegulation
 * [REGULATION] min_cycle_s (0 disables), sensor_timeout_s (> 0).
 * Missing or invalid values keep the defaults of RegulationConfig.
 */
RegulationConfig Configuration::loadRegulation() const
{
    QSettings settings(m_path, QSettings::IniFormat);
    RegulationConfig rc;
    bool ok;

    int v = settings.value("REGULATION/min_cycle_s", rc.minCycleS).toInt(&ok);
    if(ok && v >= 0)
        rc.minCycleS = v;
    else
        qCWarning(lcConfig) << "Invalid REGULATION/min_cycle_s, using" << rc.minCycleS;

    v = settings.value("REGULATION/sensor_timeout_s", rc.sensorTimeoutS).toInt(&ok);
    if(ok && v > 0)
        rc.sensorTimeoutS = v;
    else
        qCWarning(lcConfig) << "Invalid REGULATION/sensor_timeout_s, using" << rc.sensorTimeoutS;

    /* Frost protection */
    FrostConfig &fr = rc.frost;
    settings.beginGroup("FROST_PROTECTION");
    fr.enabled        = settings.value("enabled", fr.enabled).toBool();
    fr.unknownProtect = settings.value("outdoor_unknown_protect", fr.unknownProtect).toBool();
    const double below = settings.value("outdoor_below", fr.outdoorBelow).toDouble(&ok);
    if(ok)
        fr.outdoorBelow = below;
    else
        qCWarning(lcConfig) << "Invalid FROST_PROTECTION/outdoor_below, using" << fr.outdoorBelow;
    auto minutes = [&settings](const char *key, int &out) {
        bool okv;
        const int val = settings.value(key, out).toInt(&okv);
        if(okv && val > 0)
            out = val;
        else
            qCWarning(lcConfig) << "Invalid FROST_PROTECTION/" << key << ", using" << out;
    };
    minutes("on_min", fr.onMin);
    minutes("period_min", fr.periodMin);
    minutes("outdoor_max_age_min", fr.outdoorMaxAgeMin);
    if(fr.onMin > fr.periodMin)
    {
        qCWarning(lcConfig) << "FROST_PROTECTION/on_min longer than period_min, using the period";
        fr.onMin = fr.periodMin;
    }
    settings.endGroup();

    /* Valve exercise */
    ExerciseConfig &ex = rc.exercise;
    settings.beginGroup("VALVE_EXERCISE");
    ex.enabled = settings.value("enabled", ex.enabled).toBool();

    static const QStringList days = { "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday" };
    const QString day = settings.value("day", "sunday").toString().trimmed().toLower();
    int d = day.toInt(&ok);
    if(!ok)
    {
        d = 0;
        for(int i=0; i<days.size(); i++)
        {
            if(day.length() >= 3 && days[i].startsWith(day))
                d = i + 1;
        }
    }
    if(d >= 1 && d <= 7)
        ex.dayOfWeek = d;
    else
        qCWarning(lcConfig) << "Invalid VALVE_EXERCISE/day" << day << ", using sunday";

    const QTime t = QTime::fromString(settings.value("time", "07:00").toString().trimmed(), "H:mm");
    if(t.isValid())
        ex.time = t;
    else
        qCWarning(lcConfig) << "Invalid VALVE_EXERCISE/time, using 07:00";

    auto positive = [&settings](const char *key, int &out) {
        bool okv;
        const int val = settings.value(key, out).toInt(&okv);
        if(okv && val > 0)
            out = val;
        else
            qCWarning(lcConfig) << "Invalid VALVE_EXERCISE/" << key << ", using" << out;
    };
    positive("cycles", ex.cycles);
    positive("on_s", ex.onS);
    positive("off_s", ex.offS);
    positive("idle_days", ex.idleDays);
    settings.endGroup();

    return rc;
}

/**
 * @brief Configuration::loadRelayLastOn
 * @return relay -> epoch secs of the last activation (state.ini)
 */
QMap<int, qint64> Configuration::loadRelayLastOn() const
{
    QSettings state(m_statePath, QSettings::IniFormat);
    QMap<int, qint64> out;
    state.beginGroup("RELAYS");
    for(const QString &group : state.childGroups())
    {
        bool okRelay, okTime;
        const int relay = group.toInt(&okRelay);
        const qint64 t = state.value(group + "/last_on").toLongLong(&okTime);
        if(okRelay && okTime)
            out.insert(relay, t);
    }
    state.endGroup();
    return out;
}

/**
 * @brief Configuration::saveRelayLastOn
 * @param relay
 * @param epochSecs
 */
void Configuration::saveRelayLastOn(int relay, qint64 epochSecs)
{
    QSettings state(m_statePath, QSettings::IniFormat);
    state.setValue(QString("RELAYS/%1/last_on").arg(relay), epochSecs);
    syncToDisk(state);
}

/**
 * @brief Configuration::loadWeather
 * [WEATHER] provider (wttr|metno), location, lat/lon/altitude, contact,
 * poll_s (default 600)
 */
WeatherConfig Configuration::loadWeather() const
{
    QSettings settings(m_path, QSettings::IniFormat);
    settings.beginGroup("WEATHER");
    WeatherConfig wc;

    const QString provider = settings.value("provider", "wttr").toString().trimmed().toLower();
    if(provider == "metno")
        wc.provider = WeatherConfig::MetNo;
    else if(provider != "wttr")
        qCWarning(lcConfig) << "Unknown WEATHER/provider" << provider << ", using wttr";

    wc.location = settings.value("location").toString().trimmed();
    bool okLat, okLon;
    wc.lat      = settings.value("lat").toDouble(&okLat);
    wc.lon      = settings.value("lon").toDouble(&okLon);
    wc.hasCoords = okLat && okLon;
    wc.altitude = settings.value("altitude", 0).toInt();
    wc.contact  = settings.value("contact").toString().trimmed();
    wc.pollS    = settings.value("poll_s", wc.pollS).toInt();

    if(wc.provider == WeatherConfig::MetNo && !wc.hasCoords)
        qCWarning(lcConfig) << "WEATHER provider metno needs lat and lon: weather disabled";
    return wc;
}

/**
 * @brief Configuration::loadLog
 * [LOG] file (relative to setting.ini, empty: no file), max_kb, files
 */
void Configuration::loadLog(QString &path, qint64 &maxBytes, int &files) const
{
    QSettings settings(m_path, QSettings::IniFormat);
    path     = settings.value("LOG/file").toString().trimmed();
    maxBytes = settings.value("LOG/max_kb", 512).toLongLong() * 1024;
    files    = settings.value("LOG/files", 3).toInt();
    if(!path.isEmpty())
        path = QDir(QFileInfo(m_path).absolutePath()).filePath(path);
}

/**
 * @brief Configuration::loadRelayLog
 * [RELAY_LOG] file (relative to setting.ini, empty: disabled), keep_months
 */
void Configuration::loadRelayLog(QString &path, int &keepMonths) const
{
    QSettings settings(m_path, QSettings::IniFormat);
    path       = settings.value("RELAY_LOG/file").toString().trimmed();
    keepMonths = settings.value("RELAY_LOG/keep_months", 24).toInt();
    if(!path.isEmpty())
        path = QDir(QFileInfo(m_path).absolutePath()).filePath(path);
}

/**
 * @brief Configuration::loadTelegram
 * [TELEGRAM], disabled without token or allowed chats
 */
TelegramConfig Configuration::loadTelegram() const
{
    QSettings settings(m_path, QSettings::IniFormat);
    settings.beginGroup("TELEGRAM");
    TelegramConfig tc;
    tc.enabled     = settings.value("enabled", false).toBool();
    tc.token       = settings.value("token").toString().trimmed();
    tc.name        = settings.value("name", tc.name).toString().trimmed();
    tc.reminderH   = qMax(0, settings.value("reminder_h", tc.reminderH).toInt());
    tc.mqttDownMin = qMax(1, settings.value("mqtt_down_min", tc.mqttDownMin).toInt());
    tc.apiUrl      = settings.value("api_url", tc.apiUrl).toString().trimmed();
    while(tc.apiUrl.endsWith('/'))
        tc.apiUrl.chop(1);

    /* "123, -456" or a list: QSettings splits on commas */
    QStringList chats = settings.value("allowed_chats").toStringList();
    if(chats.size() == 1)
        chats = chats.first().split(',');
    for(const QString &c : chats)
    {
        bool ok;
        const qint64 id = c.trimmed().toLongLong(&ok);
        if(ok)
            tc.allowedChats.append(id);
        else if(!c.trimmed().isEmpty())
            qCWarning(lcConfig) << "Invalid TELEGRAM/allowed_chats entry" << c;
    }
    settings.endGroup();

    if(tc.enabled && (tc.token.isEmpty() || tc.allowedChats.isEmpty()))
    {
        qCWarning(lcConfig) << "TELEGRAM enabled without token or allowed_chats: disabled";
        tc.enabled = false;
    }
    return tc;
}

/**
 * @brief Configuration::lastExitState
 * @return -1 first start, 0 unexpected stop (crash, power loss, watchdog), 1 clean
 */
int Configuration::lastExitState() const
{
    QSettings state(m_statePath, QSettings::IniFormat);
    if(!state.contains("APP/clean_exit"))
        return -1;
    return state.value("APP/clean_exit").toBool() ? 1 : 0;
}

/**
 * @brief Configuration::setCleanExit
 * false at start, true after the clean shutdown
 */
void Configuration::setCleanExit(bool clean)
{
    QSettings state(m_statePath, QSettings::IniFormat);
    state.setValue("APP/clean_exit", clean);
    syncToDisk(state);
}

/**
 * @brief Configuration::uniqueId
 * setting.ini [MQTT] unique_id if set by hand. Otherwise the one stored in
 * state.ini, so HA entities survive MAC/interface changes; first run:
 * fallback (MAC) if available, random otherwise, then stored.
 * @param fallback
 * @return [MQTT] unique_id
 */
QString Configuration::uniqueId(const QString &fallback)
{
    QString id = QSettings(m_path, QSettings::IniFormat).value("MQTT/unique_id").toString();
    if(!id.isEmpty())
        return id;

    QSettings state(m_statePath, QSettings::IniFormat);
    id = state.value("MQTT/unique_id").toString();
    if(!id.isEmpty())
        return id;

    if(!fallback.isEmpty())
        id = fallback;
    else
        id = QString("%1").arg(QRandomGenerator::global()->generate64(), 16, 16, QLatin1Char('0')).right(12);

    state.setValue("MQTT/unique_id", id);
    syncToDisk(state);
    qCInfo(lcConfig).noquote() << "MQTT UniqueID generated and saved: " << id;
    return id;
}

/**
 * @brief Configuration::validZoneName
 * Used in MQTT topics and HA ids: no separators, wildcards or spaces
 */
bool Configuration::validZoneName(const QString &name)
{
    static const QRegularExpression re("^[A-Za-z0-9_-]+$");
    return re.match(name).hasMatch() && name != "list";
}

/**
 * @brief Configuration::relayNum
 * @return [RELAY] <zone>\relaynum, 0 if missing or out of range
 */
int Configuration::relayNum(QSettings &settings, const QString &zoneName)
{
    bool ok;
    int x = settings.value("RELAY/" + zoneName + "/relaynum").toInt(&ok);

    /* Missing or out of range: no relay, never write register 0 */
    if( !ok || x < 1 || x > RELAY_NUM_MAX )
    {
        qCWarning(lcConfig) << "No valid relay configured for zone" << zoneName;
        return 0;
    }
    return x;
}

/**
 * @brief Configuration::loadZones
 * @return configured zones, in [ZONES] list order
 */
QVector<ZoneData> Configuration::loadZones()
{
    QSettings settings(m_path, QSettings::IniFormat);
    QSettings state(m_statePath, QSettings::IniFormat);
    const QStringList names = settings.value("ZONES/list").toStringList();

    QVector<ZoneData> zones;
    QStringList seen;
    QList<int> relays;
    for(const QString &entry : names)
    {
        const QString name = entry.trimmed();
        if(!validZoneName(name) || seen.contains(name))
        {
            qCWarning(lcConfig) << "Invalid or duplicated zone name, skipped:" << name;
            continue;
        }
        seen.append(name);

        ZoneData z;
        z.name = name;

        /* Last setpoint set by the user, else the initial one */
        const QString key = "ZONES/" + name + "/setpoint";
        bool ok;
        double v = state.value(key).toDouble(&ok);
        if(!ok)
            v = settings.value(key).toDouble(&ok);
        if(ok)
            z.setPoint = v;
        else
            qCWarning(lcConfig) << "No setpoint for zone" << name << ", using" << z.setPoint;

        z.relay = relayNum(settings, name);
        if(z.relay > 0 && relays.contains(z.relay))
            qCWarning(lcConfig) << "Relay" << z.relay << "used by more than one zone";
        relays.append(z.relay);

        zones.append(z);
    }

    if(zones.isEmpty())
        qCWarning(lcConfig) << "No valid zone configured in [ZONES] list";
    return zones;
}

/**
 * @brief Configuration::saveSetPoints
 * To state.ini, one write and one flush for all the changed zones
 * @param setpoints     zone name -> setpoint
 */
void Configuration::saveSetPoints(const QMap<QString, double> &setpoints)
{
    QSettings state(m_statePath, QSettings::IniFormat);
    for(auto it = setpoints.cbegin(); it != setpoints.cend(); ++it)
    {
        qCInfo(lcConfig).noquote() << "Save setpoint zone" << it.key() << "=" << it.value();
        state.setValue("ZONES/" + it.key() + "/setpoint", it.value());
    }
    syncToDisk(state);
}
