#include "mqttparse.h"
#include <QJsonDocument>

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
