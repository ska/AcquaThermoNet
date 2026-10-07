#include <QTest>
#include <QRegularExpression>
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
        /* base with a '/' inside */
        QString name;
        QVERIFY(MqttParse::matchZoneTopic("RoomSense/apartment/camera/data", "RoomSense/apartment", "data", name));
        QCOMPARE(name, QString("camera"));
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
