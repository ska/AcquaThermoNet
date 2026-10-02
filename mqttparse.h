#ifndef MQTTPARSE_H
#define MQTTPARSE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
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

    struct Message
    {
        QString     topic;
        QByteArray  payload;
    };

    /* WEATHER_TOPIC payload: {"location":"…","source":"met.no","temperature":12.3,
     * "humidity":70,"pressure":1015,"wind_speed":3.1,"precipitation":0.2} */
    QByteArray weatherState(const weather_t &w, const QString &source);

    /* Home Assistant discovery of the outdoor sensors (one device,
     * "AcquaThermoNet"), all reading WEATHER_TOPIC (its fields also as
     * attributes: location, source); unavailable after expireS without
     * new data. Disabled: empty payloads, which remove entities published
     * by an earlier run. */
    QVector<Message> weatherDiscovery(const QString &uniqueId, bool enabled, int expireS);
}

#endif // MQTTPARSE_H
