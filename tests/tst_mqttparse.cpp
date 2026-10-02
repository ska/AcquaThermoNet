#include <QTest>
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
};

int runMqttParseTests(int argc, char *argv[])
{
    TstMqttParse t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_mqttparse.moc"
