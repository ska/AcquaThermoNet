#include "mqttparse.h"
#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include "climatezones.h"

/**
 * @brief MqttParse::matchZoneTopic
 * @param topic
 * @param base
 * @param tail
 * @param zoneName  out
 * @return true on match
 */
bool MqttParse::matchZoneTopic(const QString &topic, const QString &base, const QString &tail, QString &zoneName)
{
    const QString prefix = base + '/';
    const QString suffix = '/' + tail;

    if( topic.length() <= prefix.length() + suffix.length() )
        return false;
    if( !topic.startsWith(prefix) || !topic.endsWith(suffix) )
        return false;

    zoneName = topic.mid(prefix.length(), topic.length() - prefix.length() - suffix.length());
    return !zoneName.contains('/');
}

/**
 * @brief MqttParse::jsonNumber
 * @param obj
 * @param key
 * @param out
 * @return true if present and valid
 */
bool MqttParse::jsonNumber(const QJsonObject &obj, const char *key, double &out)
{
    const QJsonValue v = obj.value(key);
    if( v.isDouble() )
    {
        out = v.toDouble();
        return true;
    }
    if( v.isString() )
    {
        bool ok;
        const double d = v.toString().toDouble(&ok);
        if( ok )
            out = d;
        return ok;
    }
    return false;
}

/**
 * @brief MqttParse::sensorJson
 * @param message   RoomSense JSON
 * @param data      in/out
 * @param error     optional reason on failure
 * @return true if data was updated
 */
bool MqttParse::sensorJson(const QByteArray &message, ZoneData &data, QString *error)
{
    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(message, &jsonError);
    if( jsonError.error != QJsonParseError::NoError || !doc.isObject() )
    {
        if( error )
            *error = jsonError.error != QJsonParseError::NoError ? jsonError.errorString() : "not an object";
        return false;
    }
    const QJsonObject obj = doc.object();

    double temp;
    if( !jsonNumber(obj, "temperature", temp) )
    {
        if( error )
            *error = "no valid temperature";
        return false;
    }

    double value;
    data.temp = temp;
    if( obj.contains("mac") )
        data.mac = obj["mac"].toString();
    if( jsonNumber(obj, "data_time", value) )
        data.unixTime = value;
    if( jsonNumber(obj, "humidity", value) )
        data.humidity = value;
    if( jsonNumber(obj, "battery", value) )
        data.battery = value;
    if( jsonNumber(obj, "battmv", value) )
        data.battmv = value;
    return true;
}

/**
 * @brief MqttParse::weatherState
 * @param w
 * @param source    data provider (attribution): "met.no", "wttr.in"
 * @return compact JSON
 */
QByteArray MqttParse::weatherState(const weather_t &w, const QString &source)
{
    QJsonObject obj;
    obj.insert("location",      w.comune);
    obj.insert("source",        source);
    obj.insert("temperature",   w.temp);
    obj.insert("humidity",      w.hum);
    obj.insert("pressure",      w.press);
    obj.insert("wind_speed",    w.ws);
    obj.insert("precipitation", w.rain);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

/**
 * @brief MqttParse::weatherDiscovery
 * homeassistant/sensor/<uniqueId>_outdoor_<field>/config, retained
 * @param uniqueId
 * @param enabled       false: empty payloads (remove the entities)
 * @param expireS       expire_after
 */
QVector<MqttParse::Message> MqttParse::weatherDiscovery(const QString &uniqueId, bool enabled, int expireS)
{
    struct Field { const char *key; const char *name; const char *deviceClass; const char *unit; };
    static const Field fields[] = {
        { "temperature",   "Outdoor temperature",   "temperature",          "°C"  },
        { "humidity",      "Outdoor humidity",      "humidity",             "%"   },
        { "pressure",      "Outdoor pressure",      "atmospheric_pressure", "hPa" },
        { "wind_speed",    "Outdoor wind speed",    "wind_speed",           "m/s" },
        { "precipitation", "Outdoor precipitation", "precipitation",        "mm"  },
    };

    QJsonObject device;
    device.insert("name",           SW_NAME);
    device.insert("model",          SW_NAME);
    device.insert("manufacturer",   "Luigi Scagnet");
    device.insert("sw_version",     SW_VER);
    device.insert("identifiers",    QJsonArray{ uniqueId });

    QVector<Message> out;
    for(const Field &f : fields)
    {
        const QString id = uniqueId + "_outdoor_" + f.key;
        Message m;
        m.topic = "homeassistant/sensor/" + id + "/config";
        if(enabled)
        {
            QJsonObject p;
            p.insert("name",            f.name);
            p.insert("uniq_id",         id);
            p.insert("stat_t",          WEATHER_TOPIC);
            p.insert("val_tpl",         QString("{{ value_json.%1 }}").arg(f.key));
            p.insert("dev_cla",         f.deviceClass);
            p.insert("unit_of_meas",    QString::fromUtf8(f.unit));
            p.insert("stat_cla",        "measurement");
            p.insert("avty_t",          STATUS_TOPIC);
            p.insert("exp_aft",         expireS);
            p.insert("json_attr_t",     WEATHER_TOPIC);
            p.insert("device",          device);
            m.payload = QJsonDocument(p).toJson(QJsonDocument::Compact);
        }
        out.append(m);
    }
    return out;
}

/**
 * @brief MqttParse::zoneDevice
 * One HA device per zone (identifier <uniqueId>_<zone>)
 */
QJsonObject MqttParse::zoneDevice(const QString &uniqueId, const QString &zoneName)
{
    QJsonObject device;
    device.insert("name",           "AcquaThermoNet");
    device.insert("model",          "AcquaThermoNet Ver 0.1");
    device.insert("manufacturer",   "Luigi Scagnet");
    device.insert("identifiers",    QJsonArray{ uniqueId + "_" + zoneName });
    return device;
}

/**
 * @brief MqttParse::chronoState
 * @return compact JSON for BASE_TOPIC/<zone>/chrono
 */
QByteArray MqttParse::chronoState(const ZoneData &zone, bool paused, bool manual, const QDateTime &now)
{
    QJsonObject obj;
    obj.insert("chrono", zone.chrono.enabled ? "on" : "off");
    if(zone.chrono.enabled)
    {
        obj.insert("paused", paused);
        obj.insert("manual", manual);
        if(ZoneModel::clockValid(now))
        {
            const ChronoPoint next = Chrono::next(now, zone.chrono);
            obj.insert("profile",           Chrono::profileName(now.date(), zone.chrono));
            obj.insert("next_change",       next.start.toString("HH:mm"));
            /* with the UTC offset, for automations */
            obj.insert("next_change_at",    next.start.toOffsetFromUtc(next.start.offsetFromUtc()).toString(Qt::ISODate));
            obj.insert("next_setpoint",     next.temp);
        }
    }
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

static QJsonArray slotsJson(const QVector<ChronoSlot> &profile)
{
    QJsonArray out;
    for(const ChronoSlot &s : profile)
        out.append(QJsonObject{ { "at", s.at.toString("HH:mm") }, { "temp", s.temp } });
    return out;
}

/**
 * @brief MqttParse::chronoProfile
 * @return compact JSON for BASE_TOPIC/<zone>/chrono/profile
 */
QByteArray MqttParse::chronoProfile(const ChronoConfig &config)
{
    QJsonArray days;
    for(int d = 1; d <= 7; d++)
        if(config.holidayDays & (1 << d))
            days.append(d);

    QJsonObject obj;
    obj.insert("enabled",       config.enabled);
    obj.insert("edited",        config.edited);
    obj.insert("holiday_days",  days);
    obj.insert("weekday",       slotsJson(config.weekday));
    obj.insert("holiday",       slotsJson(config.holiday));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

static bool fail(QString *error, const QString &reason)
{
    if(error)
        *error = reason;
    return false;
}

/* A profile of chronoProgram: at most MAX_SLOTS {"at":"HH:MM","temp":n} */
static bool slotsFromJson(const QJsonValue &value, const QString &name, QVector<ChronoSlot> &out, QString *error)
{
    if(!value.isArray())
        return fail(error, name + " missing or not an array");
    const QJsonArray list = value.toArray();
    if(list.size() > ChronoConfig::MAX_SLOTS)
        return fail(error, QString("%1: more than %2 slots").arg(name).arg(ChronoConfig::MAX_SLOTS));

    out.clear();
    for(const QJsonValue &v : list)
    {
        const QJsonObject o = v.toObject();
        const QString at = o.value("at").toString();
        ChronoSlot s;
        s.at = QTime::fromString(at, "HH:mm");
        if(!v.isObject() || at.length() != 5 || !s.at.isValid())
            return fail(error, name + ": invalid slot time " + QString::fromUtf8(QJsonDocument(QJsonArray{ v }).toJson(QJsonDocument::Compact)));
        if(!o.value("temp").isDouble())
            return fail(error, name + " " + at + ": temp missing or not a number");
        s.temp = o.value("temp").toDouble();
        if(s.temp < TEMP_MIN || s.temp > TEMP_MAX)
            return fail(error, QString("%1 %2: temp %3 outside %4...%5").arg(name, at).arg(s.temp).arg(TEMP_MIN).arg(TEMP_MAX));
        s.temp = qRound(s.temp / TEMP_STEP) * TEMP_STEP;
        if(std::any_of(out.cbegin(), out.cend(), [&s](const ChronoSlot &x) { return x.at == s.at; }))
            return fail(error, name + ": " + at + " twice");
        out.append(s);
    }
    std::sort(out.begin(), out.end(), [](const ChronoSlot &a, const ChronoSlot &b) { return a.at < b.at; });
    return true;
}

/**
 * @brief MqttParse::chronoProgram
 * @param payload   <zone>/chrono/profile/set JSON
 * @param config    in/out: enabled, weekday, holiday
 * @param reset     out: {"reset":true}
 * @param error     optional reason on failure
 * @return true if valid
 */
bool MqttParse::chronoProgram(const QByteArray &payload, ChronoConfig &config, bool &reset, QString *error)
{
    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &jsonError);
    if( jsonError.error != QJsonParseError::NoError )
        return fail(error, jsonError.errorString());
    if( !doc.isObject() )
        return fail(error, "not an object");
    const QJsonObject obj = doc.object();

    reset = false;
    if( obj.contains("reset") )
    {
        if( obj.value("reset") != QJsonValue(true) )
            return fail(error, "reset must be true");
        reset = true;
        return true;
    }

    if( !obj.value("enabled").isBool() )
        return fail(error, "enabled missing or not a bool");
    QVector<ChronoSlot> weekday, holiday;
    if( !slotsFromJson(obj.value("weekday"), "weekday", weekday, error)
        || !slotsFromJson(obj.value("holiday"), "holiday", holiday, error) )
        return false;
    const bool enabled = obj.value("enabled").toBool();
    if( enabled && weekday.isEmpty() )
        return fail(error, "enabled without weekday slots");

    config.enabled = enabled;
    config.weekday = weekday;
    config.holiday = holiday;
    return true;
}

/**
 * @brief MqttParse::chronoSwitchDiscovery
 * homeassistant/switch/<zone>_chrono/config, retained
 */
MqttParse::Message MqttParse::chronoSwitchDiscovery(const QString &uniqueId, const QString &zoneName)
{
    const QString zoneTopic = QString(BASE_TOPIC "/") + zoneName + "/";
    QJsonObject p;
    p.insert("name",        "chrono." + zoneName);
    p.insert("uniq_id",     uniqueId + "_" + zoneName + "_chrono");
    p.insert("avty_t",      STATUS_TOPIC);
    p.insert("cmd_t",       zoneTopic + TAIL_CHRONO_SET);
    p.insert("stat_t",      zoneTopic + TAIL_CHRONO);
    p.insert("val_tpl",     "{{ value_json.chrono }}");
    p.insert("pl_on",       "on");
    p.insert("pl_off",      "off");
    p.insert("json_attr_t", zoneTopic + TAIL_CHRONO);
    p.insert("icon",        "mdi:calendar-clock");
    p.insert("device",      zoneDevice(uniqueId, zoneName));

    Message m;
    m.topic = "homeassistant/switch/" + zoneName + "_chrono/config";
    m.payload = QJsonDocument(p).toJson(QJsonDocument::Compact);
    return m;
}

bool MqttParse::parseOnOff(const QByteArray &payload, bool &on)
{
    const QByteArray v = payload.trimmed().toLower();
    if(v != "on" && v != "off")
        return false;
    on = v == "on";
    return true;
}
