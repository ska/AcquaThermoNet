#ifndef MQTTPARSE_H
#define MQTTPARSE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include "zonemodel.h"

/* Pure helpers for MQTT topics and payloads (no broker needed: unit tested) */
namespace MqttParse
{
    /* Match "<base>/<zone>/<tail>" and extract <zone> (no '/' inside) */
    bool matchZoneTopic(const QString &topic, const QString &base, const QString &tail, QString &zoneName);

    /* Number sent either as JSON number or as string */
    bool jsonNumber(const QJsonObject &obj, const char *key, double &out);

    /* RoomSense JSON into the sensor fields of data. "temperature" is required,
     * missing optional fields keep their value. On error data is untouched. */
    bool sensorJson(const QByteArray &message, ZoneData &data, QString *error = nullptr);
}

#endif // MQTTPARSE_H
