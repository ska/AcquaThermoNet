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

    /* Home Assistant device of a zone: its climate and chrono switch */
    QJsonObject zoneDevice(const QString &uniqueId, const QString &zoneName);

    /* <zone>/chrono payload, state of the chrono switch and attributes of
     * the climate: {"chrono":"on","paused":false,"manual":false,
     * "profile":"weekday","next_change":"22:30","next_change_at":
     * "2026-10-03T22:30:00+02:00","next_setpoint":17.0}; {"chrono":"off"}
     * when off; no profile and next change while the clock is not set.
     * paused: away; manual: ZoneModel::chronoManual */
    QByteArray chronoState(const ZoneData &zone, bool paused, bool manual, const QDateTime &now);

    /* <zone>/chrono/profile payload, the program of the zone (interface
     * §9): {"enabled":true,"edited":false,"holiday_days":[6,7],
     * "weekday":[{"at":"06:30","temp":20.5},...],"holiday":[...]} */
    QByteArray chronoProfile(const ChronoConfig &config);

    /* <zone>/chrono/profile/set payload (interface §9): the whole program
     * {"enabled":true,"weekday":[{"at":"06:30","temp":20.5},...],
     * "holiday":[...]} into enabled, weekday and holiday of config (temps
     * rounded to TEMP_STEP, slots sorted), or {"reset":true}. Strict:
     * anything invalid refuses the whole payload, config untouched. */
    bool chronoProgram(const QByteArray &payload, ChronoConfig &config, bool &reset, QString *error = nullptr);

    /* Home Assistant switch of the chrono of a zone (same device as its
     * climate), state from <zone>/chrono, commands on <zone>/chrono/set */
    Message chronoSwitchDiscovery(const QString &uniqueId, const QString &zoneName);

    /* "on" / "off" (any case, spaces ignored) */
    bool parseOnOff(const QByteArray &payload, bool &on);
}

#endif // MQTTPARSE_H
