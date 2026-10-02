#include "mqttparse.h"
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
