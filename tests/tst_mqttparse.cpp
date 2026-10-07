#include <QTest>
#include <QRegularExpression>
#include <algorithm>
#include "mqttparse.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

class TstMqttParse : public QObject
{
    Q_OBJECT

private slots:
    void matchZoneTopic_data()
    {
        QTest::addColumn<QString>("topic");
        QTest::addColumn<bool>("match");
        QTest::addColumn<QString>("zone");

        QTest::newRow("ok")         << "AcquaThermoNet/salotto/set_temp"        << true  << "salotto";
        QTest::newRow("other tail") << "AcquaThermoNet/salotto/set_mode"        << false << "";
        QTest::newRow("other base") << "Other/salotto/set_temp"                 << false << "";
        QTest::newRow("no zone")    << "AcquaThermoNet//set_temp"               << false << "";
        QTest::newRow("nested")     << "AcquaThermoNet/a/b/set_temp"            << false << "";
        QTest::newRow("short")      << "AcquaThermoNet/set_temp"                << false << "";
        QTest::newRow("prefix only")<< "AcquaThermoNetX/salotto/set_temp"       << false << "";
    }

    void matchZoneTopic()
    {
        QFETCH(QString, topic);
        QFETCH(bool, match);
        QFETCH(QString, zone);

        QString name;
        QCOMPARE(MqttParse::matchZoneTopic(topic, "AcquaThermoNet", "set_temp", name), match);
        if(match)
            QCOMPARE(name, zone);
    }

    void matchSensorTopic()
    {
        QString name;
        QVERIFY(MqttParse::matchZoneTopic("RoomSense/camera/data", BASE_TOPIC_SENSOR, TAIL_DATA, name));
        QCOMPARE(name, QString("camera"));
        /* the gateway availability is not a zone */
        QVERIFY(!MqttParse::matchZoneTopic("RoomSense/status", BASE_TOPIC_SENSOR, TAIL_DATA, name));
        QVERIFY(!MqttParse::matchZoneTopic("RoomSense/apartment/camera/data", BASE_TOPIC_SENSOR, TAIL_DATA, name));
    }

    void sensorJsonStringsAndNumbers()
    {
        ZoneData d;
        QVERIFY(MqttParse::sensorJson(R"({"temperature":"21.5","humidity":"40","battery":"80","battmv":"2900","data_time":"1700000000","mac":"aa:bb"})", d));
        QCOMPARE(d.temp, 21.5);
        QCOMPARE(int(d.humidity), 40);
        QCOMPARE(int(d.battery), 80);
        QCOMPARE(int(d.battmv), 2900);
        QCOMPARE(d.unixTime, quint32(1700000000));
        QCOMPARE(d.mac, QString("aa:bb"));

        ZoneData n;
        QVERIFY(MqttParse::sensorJson(R"({"temperature":-2.5,"humidity":55})", n));
        QCOMPARE(n.temp, -2.5);
        QCOMPARE(int(n.humidity), 55);
    }

    void sensorJsonKeepsMissingFields()
    {
        ZoneData d;
        d.humidity = 33;
        d.battery = 77;
        QVERIFY(MqttParse::sensorJson(R"({"temperature":20})", d));
        QCOMPARE(d.temp, 20.0);
        QCOMPARE(int(d.humidity), 33);
        QCOMPARE(int(d.battery), 77);
    }

    void sensorJsonInvalid_data()
    {
        QTest::addColumn<QByteArray>("payload");
        QTest::newRow("not json")       << QByteArray("not json");
        QTest::newRow("array")          << QByteArray("[1,2]");
        QTest::newRow("no temperature") << QByteArray(R"({"humidity":40})");
        QTest::newRow("bad temperature")<< QByteArray(R"({"temperature":"warm"})");
        /* QString::toDouble accepts them: a nan would stop the regulation */
        QTest::newRow("nan")            << QByteArray(R"({"temperature":"nan"})");
        QTest::newRow("inf")            << QByteArray(R"({"temperature":"inf"})");
        QTest::newRow("-inf")           << QByteArray(R"({"temperature":"-inf"})");
        QTest::newRow("too hot")        << QByteArray(R"({"temperature":85})");
        QTest::newRow("too cold")       << QByteArray(R"({"temperature":-40})");
        QTest::newRow("huge string")    << QByteArray(R"({"temperature":"1e9"})");
    }

    void sensorJsonInvalid()
    {
        QFETCH(QByteArray, payload);
        ZoneData d;
        d.temp = 19;
        QString error;
        QVERIFY(!MqttParse::sensorJson(payload, d, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(d.temp, 19.0);     /* untouched */
    }

    void sensorJsonLimits()
    {
        ZoneData d;
        QVERIFY(MqttParse::sensorJson(R"({"temperature":-30})", d));
        QCOMPARE(d.temp, -30.0);
        QVERIFY(MqttParse::sensorJson(R"({"temperature":60})", d));
        QCOMPARE(d.temp, 60.0);
    }

    void sensorJsonOptionalOutOfRange()
    {
        /* reading accepted, the bad optional fields keep their value */
        ZoneData d;
        d.humidity = 33;
        d.battery = 77;
        d.battmv = 2900;
        d.unixTime = 1700000000;
        QVERIFY(MqttParse::sensorJson(R"({"temperature":21,"humidity":300,"battery":"nan","battmv":-1,"data_time":"inf"})", d));
        QCOMPARE(d.temp, 21.0);
        QCOMPARE(int(d.humidity), 33);
        QCOMPARE(int(d.battery), 77);
        QCOMPARE(int(d.battmv), 2900);
        QCOMPARE(d.unixTime, quint32(1700000000));
    }

    void parseSetPoint_data()
    {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<double>("temp");
        QTest::newRow("integer")    << QByteArray("20")     << true  << 20.0;
        QTest::newRow("decimal")    << QByteArray("21.3")   << true  << 21.3;
        QTest::newRow("spaces")     << QByteArray(" 19.5\n") << true  << 19.5;
        QTest::newRow("clamped later") << QByteArray("40")  << true  << 40.0;
        QTest::newRow("text")       << QByteArray("warm")   << false << 0.0;
        QTest::newRow("empty")      << QByteArray("")       << false << 0.0;
        QTest::newRow("nan")        << QByteArray("nan")    << false << 0.0;
        QTest::newRow("inf")        << QByteArray("inf")    << false << 0.0;
        QTest::newRow("-inf")       << QByteArray("-inf")   << false << 0.0;
    }

    void parseSetPoint()
    {
        QFETCH(QByteArray, payload);
        QFETCH(bool, ok);
        QFETCH(double, temp);
        double out = -1;
        QCOMPARE(MqttParse::parseSetPoint(payload, out), ok);
        if(ok)
            QCOMPARE(out, temp);
        else
            QCOMPARE(out, -1.0);
    }

    /* MQTT topic filter match, + and # (enough for the test) */
    static bool filterMatch(const QString &filter, const QString &topic)
    {
        const QStringList f = filter.split('/'), t = topic.split('/');
        for(int i = 0; i < f.size(); i++)
        {
            if(f[i] == "#")
                return true;
            if(i >= t.size() || (f[i] != "+" && f[i] != t[i]))
                return false;
        }
        return f.size() == t.size();
    }

    void subscriptions()
    {
        QStringList filters;
        for(const MqttParse::Subscription &s : MqttParse::subscriptions())
        {
            filters << s.filter;
            QCOMPARE(int(s.qos), s.filter == "homeassistant/status" ? 0 : 1);
        }
        QCOMPARE(filters, QStringList({ "homeassistant/status", "AcquaThermoNet/+/set_temp", "AcquaThermoNet/+/chrono/set",
                                        "AcquaThermoNet/+/chrono/profile/set", "AcquaThermoNet/mode/set",
                                        "RoomSense/+/data", "RoomSense/status" }));

        auto subscribed = [&filters](const QString &topic) {
            return std::any_of(filters.cbegin(), filters.cend(), [&topic](const QString &f) { return filterMatch(f, topic); });
        };
        /* commands, readings and the gateway availability */
        for(const char *t : { "AcquaThermoNet/salotto/set_temp", "AcquaThermoNet/salotto/chrono/set",
                              "AcquaThermoNet/salotto/chrono/profile/set", "AcquaThermoNet/mode/set",
                              "RoomSense/salotto/data", "RoomSense/status", "homeassistant/status" })
            QVERIFY2(subscribed(t), t);
        /* nothing the controller publishes comes back */
        for(const char *t : { "AcquaThermoNet/status", "AcquaThermoNet/salotto/state_temp", "AcquaThermoNet/salotto/state_mode",
                              "AcquaThermoNet/salotto/chrono", "AcquaThermoNet/salotto/chrono/profile",
                              "AcquaThermoNet/mode/state", "AcquaThermoNet/weather",
                              "homeassistant/climate/salotto/config", "homeassistant/switch/salotto_chrono/config" })
            QVERIFY2(!subscribed(t), t);
    }

    void refusedWhenRetained_data()
    {
        QTest::addColumn<QString>("topic");
        QTest::addColumn<bool>("refused");
        QTest::newRow("set_temp")       << "AcquaThermoNet/salotto/set_temp"            << true;
        QTest::newRow("chrono/set")     << "AcquaThermoNet/salotto/chrono/set"          << true;
        QTest::newRow("profile/set")    << "AcquaThermoNet/salotto/chrono/profile/set"  << true;
        QTest::newRow("mode/set")       << "AcquaThermoNet/mode/set"                    << true;
        QTest::newRow("reading")        << "RoomSense/salotto/data"           << true;
        /* states: retained on purpose */
        QTest::newRow("state_temp")     << "AcquaThermoNet/salotto/state_temp"          << false;
        QTest::newRow("state_mode")     << "AcquaThermoNet/salotto/state_mode"          << false;
        QTest::newRow("chrono")         << "AcquaThermoNet/salotto/chrono"              << false;
        QTest::newRow("profile")        << "AcquaThermoNet/salotto/chrono/profile"      << false;
        QTest::newRow("mode/state")     << "AcquaThermoNet/mode/state"                  << false;
        QTest::newRow("status")         << "AcquaThermoNet/status"                      << false;
        QTest::newRow("weather")        << "AcquaThermoNet/weather"                     << false;
        QTest::newRow("ha status")      << "homeassistant/status"                       << false;
        QTest::newRow("gateway status") << "RoomSense/status"                           << false;
    }

    void refusedWhenRetained()
    {
        QFETCH(QString, topic);
        QFETCH(bool, refused);
        QCOMPARE(MqttParse::refusedWhenRetained(topic), refused);
    }

    void weatherState()
    {
        weather_t w;
        w.comune = "Home";
        w.temp = 12.3;
        w.hum = 70;
        w.press = 1015;
        w.ws = 3.1;
        w.rain = 0.2;
        const QJsonObject o = QJsonDocument::fromJson(MqttParse::weatherState(w, "met.no")).object();
        QCOMPARE(o["location"].toString(), QString("Home"));
        QCOMPARE(o["source"].toString(), QString("met.no"));
        QCOMPARE(o["temperature"].toDouble(), 12.3);
        QCOMPARE(o["humidity"].toInt(), 70);
        QCOMPARE(o["pressure"].toInt(), 1015);
        QCOMPARE(o["wind_speed"].toDouble(), 3.1);
        QCOMPARE(o["precipitation"].toDouble(), 0.2);
    }

    void weatherDiscovery()
    {
        const QVector<MqttParse::Message> msgs = MqttParse::weatherDiscovery("atn1", true, 10800);
        QCOMPARE(msgs.size(), 5);
        QStringList keys;
        for(const MqttParse::Message &m : msgs)
        {
            const QJsonObject p = QJsonDocument::fromJson(m.payload).object();
            const QString key = p["val_tpl"].toString().section('.', 1).section(' ', 0, 0);
            keys << key;
            QCOMPARE(m.topic, "homeassistant/sensor/atn1_outdoor_" + key + "/config");
            QCOMPARE(p["uniq_id"].toString(), "atn1_outdoor_" + key);
            QCOMPARE(p["stat_t"].toString(), QString("AcquaThermoNet/weather"));
            QCOMPARE(p["json_attr_t"].toString(), QString("AcquaThermoNet/weather"));
            QCOMPARE(p["avty_t"].toString(), QString("AcquaThermoNet/status"));
            QCOMPARE(p["exp_aft"].toInt(), 10800);
            QCOMPARE(p["stat_cla"].toString(), QString("measurement"));
            QCOMPARE(p["device"].toObject()["identifiers"].toArray().first().toString(), QString("atn1"));
        }
        QCOMPARE(keys, QStringList({ "temperature", "humidity", "pressure", "wind_speed", "precipitation" }));
        const QJsonObject t = QJsonDocument::fromJson(msgs.first().payload).object();
        QCOMPARE(t["dev_cla"].toString(), QString("temperature"));
        QCOMPARE(t["unit_of_meas"].toString(), QString::fromUtf8("°C"));

        /* disabled: same topics, empty payloads (entities removed) */
        const QVector<MqttParse::Message> off = MqttParse::weatherDiscovery("atn1", false, 10800);
        QCOMPARE(off.size(), 5);
        for(int i = 0; i < off.size(); i++)
        {
            QCOMPARE(off[i].topic, msgs[i].topic);
            QVERIFY(off[i].payload.isEmpty());
        }
    }

    void chronoState()
    {
        ZoneData z;
        z.name = "salotto";
        QJsonObject o = QJsonDocument::fromJson(MqttParse::chronoState(z, false, false, QDateTime::currentDateTime())).object();
        QCOMPARE(o, QJsonObject({ { "chrono", "off" } }));

        z.chrono.enabled = true;
        z.chrono.weekday = Chrono::parseProfile({ "06:30=20.5", "22:30=17" }, "test");
        z.chrono.holidayDays = 1 << 7;
        /* 2026-09-27 is a Sunday */
        const QDateTime now(QDate(2026, 9, 27), QTime(12, 0));
        o = QJsonDocument::fromJson(MqttParse::chronoState(z, true, true, now)).object();
        QCOMPARE(o["chrono"].toString(), QString("on"));
        QCOMPARE(o["paused"].toBool(), true);
        QCOMPARE(o["manual"].toBool(), true);
        QCOMPARE(o["profile"].toString(), QString("weekday"));     /* no holiday profile */
        QCOMPARE(o["next_change"].toString(), QString("22:30"));
        QCOMPARE(o["next_setpoint"].toDouble(), 17.0);
        const QDateTime at = QDateTime::fromString(o["next_change_at"].toString(), Qt::ISODate);
        QCOMPARE(at, QDateTime(QDate(2026, 9, 27), QTime(22, 30)));
        QVERIFY(o["next_change_at"].toString().contains(QRegularExpression("([+-]\\d\\d:\\d\\d|Z)$")));

        /* clock not set: no next change */
        o = QJsonDocument::fromJson(MqttParse::chronoState(z, false, false, QDateTime(QDate(2000, 1, 2), QTime(8, 0)))).object();
        QCOMPARE(o["chrono"].toString(), QString("on"));
        QVERIFY(!o.contains("next_change"));
        QVERIFY(!o.contains("profile"));
    }

    void chronoProfile()
    {
        ChronoConfig c;
        QJsonObject o = QJsonDocument::fromJson(MqttParse::chronoProfile(c)).object();
        QCOMPARE(o["enabled"].toBool(), false);
        QCOMPARE(o["edited"].toBool(), false);
        QCOMPARE(o["holiday_days"].toArray(), QJsonArray());
        QCOMPARE(o["weekday"].toArray(), QJsonArray());
        QCOMPARE(o["holiday"].toArray(), QJsonArray());

        c.enabled = true;
        c.edited = true;
        c.weekday = Chrono::parseProfile({ "22:30=17", "06:30=20.5" }, "test");
        c.holiday = Chrono::parseProfile({ "08:00=18" }, "test");
        c.holidayDays = (1 << 6) | (1 << 7);
        const QByteArray payload = MqttParse::chronoProfile(c);
        QVERIFY(!payload.contains('\n'));
        o = QJsonDocument::fromJson(payload).object();
        QCOMPARE(o["enabled"].toBool(), true);
        QCOMPARE(o["edited"].toBool(), true);
        QCOMPARE(o["holiday_days"].toArray(), QJsonArray({ 6, 7 }));
        const QJsonArray w = o["weekday"].toArray();
        QCOMPARE(w.size(), 2);
        QCOMPARE(w[0].toObject(), QJsonObject({ { "at", "06:30" }, { "temp", 20.5 } }));
        QCOMPARE(w[1].toObject(), QJsonObject({ { "at", "22:30" }, { "temp", 17 } }));
        QCOMPARE(o["holiday"].toArray(), QJsonArray({ QJsonObject({ { "at", "08:00" }, { "temp", 18 } }) }));
        /* integer setpoints as plain numbers */
        QVERIFY(payload.contains("\"temp\":17}"));
    }

    void chronoProgram()
    {
        ChronoConfig c;
        c.holidayDays = 1 << 7;
        c.edited = true;
        bool reset = true;
        QString error;
        QVERIFY(MqttParse::chronoProgram(
            R"({"enabled":true,"holiday_days":[1],"weekday":[{"at":"22:30","temp":17},{"at":"06:30","temp":20.3}],"holiday":[]})",
            c, reset, &error));
        QVERIFY(!reset);
        QVERIFY(c.enabled);
        QCOMPARE(Chrono::toString(c.weekday), QString("06:30=20.5, 22:30=17.0"));   /* sorted, rounded */
        QVERIFY(c.holiday.isEmpty());
        QCOMPARE(int(c.holidayDays), 1 << 7);       /* holiday_days ignored */

        QVERIFY(MqttParse::chronoProgram(R"({"enabled":false,"weekday":[],"holiday":[{"at":"08:00","temp":5}]})", c, reset));
        QVERIFY(!c.enabled);
        QVERIFY(c.weekday.isEmpty());
        QCOMPARE(Chrono::toString(c.holiday), QString("08:00=5.0"));

        QVERIFY(MqttParse::chronoProgram(R"({"reset":true})", c, reset));
        QVERIFY(reset);

        /* the eight slots of a full profile */
        QByteArray eight = R"({"enabled":true,"holiday":[],"weekday":[)";
        for(int h = 0; h < 8; h++)
            eight += QString(R"(%1{"at":"%2:00","temp":20})").arg(h ? "," : "").arg(h + 10).toUtf8();
        QVERIFY(MqttParse::chronoProgram(eight + "]}", c, reset));
        QCOMPARE(c.weekday.size(), 8);
    }

    void chronoProgramInvalid_data()
    {
        QTest::addColumn<QByteArray>("payload");
        QTest::newRow("not json")       << QByteArray("on");
        QTest::newRow("array")          << QByteArray("[]");
        QTest::newRow("reset false")    << QByteArray(R"({"reset":false})");
        QTest::newRow("no enabled")     << QByteArray(R"({"weekday":[],"holiday":[]})");
        QTest::newRow("enabled string") << QByteArray(R"({"enabled":"on","weekday":[],"holiday":[]})");
        QTest::newRow("no holiday")     << QByteArray(R"({"enabled":true,"weekday":[{"at":"06:30","temp":20}]})");
        QTest::newRow("on, no weekday") << QByteArray(R"({"enabled":true,"weekday":[],"holiday":[{"at":"06:30","temp":20}]})");
        QTest::newRow("time H:mm")      << QByteArray(R"({"enabled":true,"weekday":[{"at":"6:30","temp":20}],"holiday":[]})");
        QTest::newRow("time 24:00")     << QByteArray(R"({"enabled":true,"weekday":[{"at":"24:00","temp":20}],"holiday":[]})");
        QTest::newRow("slot not object")<< QByteArray(R"({"enabled":true,"weekday":["06:30=20"],"holiday":[]})");
        QTest::newRow("temp string")    << QByteArray(R"({"enabled":true,"weekday":[{"at":"06:30","temp":"20"}],"holiday":[]})");
        QTest::newRow("temp high")      << QByteArray(R"({"enabled":true,"weekday":[{"at":"06:30","temp":30}],"holiday":[]})");
        QTest::newRow("temp low")       << QByteArray(R"({"enabled":true,"weekday":[{"at":"06:30","temp":4.5}],"holiday":[]})");
        QTest::newRow("duplicated")     << QByteArray(R"({"enabled":true,"weekday":[{"at":"06:30","temp":20},{"at":"06:30","temp":18}],"holiday":[]})");
        QByteArray nine = R"({"enabled":true,"holiday":[],"weekday":[)";
        for(int h = 0; h < 9; h++)
            nine += QString(R"(%1{"at":"%2:00","temp":20})").arg(h ? "," : "").arg(h + 10).toUtf8();
        QTest::newRow("nine slots")     << nine + "]}";
    }

    void chronoProgramInvalid()
    {
        QFETCH(QByteArray, payload);
        ChronoConfig c;
        c.enabled = true;
        c.weekday = Chrono::parseProfile({ "07:00=19" }, "test");
        bool reset = true;
        QString error;
        QVERIFY(!MqttParse::chronoProgram(payload, c, reset, &error));
        QVERIFY(!error.isEmpty());
        /* untouched */
        QVERIFY(c.enabled);
        QCOMPARE(Chrono::toString(c.weekday), QString("07:00=19.0"));
        QVERIFY(c.holiday.isEmpty());

        QString zone;
        QVERIFY(MqttParse::matchZoneTopic("AcquaThermoNet/salotto/chrono/profile/set", BASE_TOPIC, TAIL_CHRONO_PROFILE_SET, zone));
        QCOMPARE(zone, QString("salotto"));
        QVERIFY(!MqttParse::matchZoneTopic("AcquaThermoNet/salotto/chrono/profile/set", BASE_TOPIC, TAIL_CHRONO_SET, zone));
    }

    void chronoSwitchDiscovery()
    {
        const MqttParse::Message m = MqttParse::chronoSwitchDiscovery("atn1", "salotto");
        QCOMPARE(m.topic, QString("homeassistant/switch/salotto_chrono/config"));
        const QJsonObject p = QJsonDocument::fromJson(m.payload).object();
        QCOMPARE(p["uniq_id"].toString(), QString("atn1_salotto_chrono"));
        QCOMPARE(p["cmd_t"].toString(), QString("AcquaThermoNet/salotto/chrono/set"));
        QCOMPARE(p["stat_t"].toString(), QString("AcquaThermoNet/salotto/chrono"));
        QCOMPARE(p["json_attr_t"].toString(), QString("AcquaThermoNet/salotto/chrono"));
        QCOMPARE(p["val_tpl"].toString(), QString("{{ value_json.chrono }}"));
        QCOMPARE(p["pl_on"].toString(), QString("on"));
        QCOMPARE(p["pl_off"].toString(), QString("off"));
        QCOMPARE(p["avty_t"].toString(), QString("AcquaThermoNet/status"));
        /* same device as the climate of the zone */
        QCOMPARE(p["device"].toObject(), MqttParse::zoneDevice("atn1", "salotto"));
        QCOMPARE(p["device"].toObject()["identifiers"].toArray().first().toString(), QString("atn1_salotto"));

        QString zone;
        QVERIFY(MqttParse::matchZoneTopic("AcquaThermoNet/salotto/chrono/set", BASE_TOPIC, TAIL_CHRONO_SET, zone));
        QCOMPARE(zone, QString("salotto"));
        QVERIFY(!MqttParse::matchZoneTopic("AcquaThermoNet/salotto/chrono", BASE_TOPIC, TAIL_CHRONO_SET, zone));
    }

    void parseOnOff()
    {
        bool on = false;
        QVERIFY(MqttParse::parseOnOff("on", on));
        QVERIFY(on);
        QVERIFY(MqttParse::parseOnOff(" OFF\n", on));
        QVERIFY(!on);
        QVERIFY(!MqttParse::parseOnOff("1", on));
        QVERIFY(!MqttParse::parseOnOff("", on));
    }
};

int runMqttParseTests(int argc, char *argv[])
{
    TstMqttParse t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_mqttparse.moc"
