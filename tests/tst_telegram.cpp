#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDateTime>
#include <QFile>
#include "configuration.h"
#include "zonemodel.h"
#include "telegramnotifier.h"
#include "relaylog.h"
#include "testutil.h"

/*
 * TelegramNotifier logic without network: broadcast/reply signals captured.
 * Zones salotto (relay 5) and camera (relay 4).
 */
class TstTelegram : public QObject
{
    Q_OBJECT

    QTemporaryDir       *m_dir = nullptr;
    Configuration       *m_conf = nullptr;
    ZoneModel           *m_zones = nullptr;
    TelegramNotifier    *m_tg = nullptr;
    QSignalSpy          *m_out = nullptr;
    QSignalSpy          *m_replies = nullptr;

    QString last() const { return m_out->isEmpty() ? QString() : m_out->last().at(0).toString(); }
    QString lastReply() const { return m_replies->isEmpty() ? QString() : m_replies->last().at(1).toString(); }

    void sensor(int zone, double temp, int battery)
    {
        ZoneData d;
        d.temp = temp;
        d.battery = battery;
        m_zones->setSensorData(zone, d);
    }

private slots:
    void init()
    {
        m_dir = new QTemporaryDir;
        m_conf = new Configuration(TestUtil::writeIni(*m_dir,
            "[ZONES]\nlist=salotto, camera\n[RELAY]\nsalotto\\relaynum=5\ncamera\\relaynum=4\n"));
        m_zones = new ZoneModel(m_conf);
        TelegramConfig tc;
        tc.name = "Casa";
        m_tg = new TelegramNotifier(m_zones, tc, nullptr);
        m_out = new QSignalSpy(m_tg, &TelegramNotifier::broadcast);
        m_replies = new QSignalSpy(m_tg, &TelegramNotifier::reply);
    }

    void cleanup()
    {
        delete m_replies;
        delete m_out;
        delete m_tg;
        delete m_zones;
        delete m_conf;
        delete m_dir;
    }

    void config()
    {
        QTemporaryDir dir;
        TelegramConfig off = Configuration(TestUtil::writeIni(dir, "[TELEGRAM]\nenabled=true\ntoken=x\n")).loadTelegram();
        QVERIFY(!off.enabled);                  /* no allowed chat: disabled */

        QTemporaryDir dir2;
        TelegramConfig tc = Configuration(TestUtil::writeIni(dir2,
            "[TELEGRAM]\nenabled=true\ntoken=123:ABC\nallowed_chats=111, -222\nname=Casa\n"
            "reminder_h=2\nmqtt_down_min=5\ngateway_down_min=3\napi_url=http://127.0.0.1:1/\n")).loadTelegram();
        QVERIFY(tc.enabled);
        QCOMPARE(tc.token, QString("123:ABC"));
        QCOMPARE(tc.allowedChats, QList<qint64>() << 111 << -222);
        QCOMPARE(tc.name, QString("Casa"));
        QCOMPARE(tc.reminderH, 2);
        QCOMPARE(tc.mqttDownMin, 5);
        QCOMPARE(tc.gatewayDownMin, 3);
        QCOMPARE(off.gatewayDownMin, 2);        /* default */
        QCOMPARE(tc.apiUrl, QString("http://127.0.0.1:1"));    /* trailing / removed */
    }

    void cleanExitFlag()
    {
        QTemporaryDir dir;
        Configuration conf(TestUtil::writeIni(dir, ""));
        QCOMPARE(conf.lastExitState(), -1);
        conf.setCleanExit(false);
        QCOMPARE(conf.lastExitState(), 0);
        conf.setCleanExit(true);
        QCOMPARE(conf.lastExitState(), 1);
    }

    void startMessages()
    {
        m_tg->announceStart(-1);
        QVERIFY(last().contains("First start"));
        QVERIFY(last().contains("[Casa "));
        m_tg->announceStart(0);
        QVERIFY(last().contains("did NOT stop cleanly"));
        m_tg->announceStart(1);
        QVERIFY(!last().contains("NOT"));
        m_tg->setSerialOpen(false);             /* initial state: no message */
        QCOMPARE(m_out->count(), 3);
        m_tg->announceStart(1);
        QVERIFY(last().contains("Serial port closed"));
    }

    void zoneAlarmsOnTransitions()
    {
        m_zones->setSensorLost(0, true);
        QVERIFY(last().contains("Salotto: no sensor data"));
        m_zones->setSensorLost(0, true);        /* no change: no message */
        m_zones->setRelayState(0, 1);           /* not an alarm */
        QCOMPARE(m_out->count(), 1);
        m_zones->setSensorLost(0, false);
        QVERIFY(last().contains("Salotto: sensor data back"));

        m_zones->setRelayFault(1, true);
        QVERIFY(last().contains("Camera: relay 4 does not follow"));
        m_zones->setRelayFault(1, false);
        QVERIFY(last().contains("relay 4 follows the commands again"));

        m_zones->setFrostProtection(1, true);
        QVERIFY(last().contains("Camera: FROST MODE"));
        m_zones->setFrostProtection(1, false);
        QVERIFY(last().contains("frost mode off"));
        QCOMPARE(m_out->count(), 6);
    }

    void batteryHysteresis()
    {
        sensor(0, 20, 80);
        QCOMPARE(m_out->count(), 0);
        sensor(0, 20, 15);
        QVERIFY(last().contains("battery low (15%)"));
        sensor(0, 20, 21);                      /* above low, below OK: no message */
        sensor(0, 20, 18);
        QCOMPARE(m_out->count(), 1);
        sensor(0, 20, 95);                      /* replaced */
        QVERIFY(last().contains("battery OK (95%)"));
        sensor(0, 20, 0);                       /* 0 = not reported: ignored */
        QCOMPARE(m_out->count(), 2);
    }

    void systemAlarms()
    {
        m_tg->setModbusOnline(true);            /* already online */
        QCOMPARE(m_out->count(), 0);
        m_tg->setModbusOnline(false);
        QVERIFY(last().contains("Modbus relay board OFFLINE"));
        m_tg->setModbusOnline(true);
        QVERIFY(last().contains("online again"));

        m_tg->setSerialOpen(true);              /* initial */
        m_tg->setSerialOpen(false);
        QVERIFY(last().contains("serial port LOST"));
        m_tg->setSerialOpen(true);
        QVERIFY(last().contains("serial port open again"));

        /* MQTT: alarm only after mqtt_down_min, then back */
        m_tg->setMqttConnected(true);
        m_tg->setMqttConnected(false);
        const int before = m_out->count();
        QMetaObject::invokeMethod(m_tg, "onMqttDown");
        QCOMPARE(m_out->count(), before + 1);
        QVERIFY(last().contains("MQTT broker not connected"));
        m_tg->setMqttConnected(true);
        QVERIFY(last().contains("MQTT broker connected again"));
        m_tg->setMqttConnected(false);          /* short drop: no message */
        m_tg->setMqttConnected(true);
        QCOMPARE(m_out->count(), before + 2);
    }

    void gatewayAlarm()
    {
        /* offline: alarm only after gateway_down_min (a restart is shorter) */
        m_tg->setGatewayState(GatewayOnline);
        m_tg->setGatewayState(GatewayOffline);
        QMetaObject::invokeMethod(m_tg, "onGatewayDown");
        QCOMPARE(m_out->count(), 1);
        QVERIFY(last().contains("RoomSense gateway OFFLINE for 2 min"));
        m_tg->onCommand(111, "/status");
        QVERIFY(lastReply().contains("RoomSense OFFLINE"));
        QVERIFY(lastReply().contains("Alarms: RoomSense offline."));

        /* broker lost meanwhile: unknown, the alarm stays until online */
        m_tg->setGatewayState(GatewayUnknown);
        m_tg->setGatewayState(GatewayOffline);
        QCOMPARE(m_out->count(), 1);
        m_tg->setGatewayState(GatewayOnline);
        QCOMPARE(m_out->count(), 2);
        QVERIFY(last().contains("RoomSense gateway online again"));

        /* short restart: no message */
        m_tg->setGatewayState(GatewayOffline);
        m_tg->setGatewayState(GatewayOnline);
        QMetaObject::invokeMethod(m_tg, "onGatewayDown");
        QCOMPARE(m_out->count(), 2);

        /* offline, then the broker lost before the timeout: no alarm */
        m_tg->setGatewayState(GatewayOffline);
        m_tg->setGatewayState(GatewayUnknown);
        QMetaObject::invokeMethod(m_tg, "onGatewayDown");
        QCOMPARE(m_out->count(), 2);
    }

    void windowMessages()
    {
        m_tg->onWindowOpened(1, 21.0, 19.9, 5);
        QVERIFY(last().contains("Camera: window open? Temperature 21.0 -> 19.9°C in 5 min."));
        m_tg->onWindowClosed(1, 19.5, 15);
        QVERIFY(last().contains("Camera: temperature rising again, window closed? (lowest 19.5°C, 15 min after the drop)"));
        QMetaObject::invokeMethod(m_tg, "remind");
        QCOMPARE(m_out->count(), 2);            /* information, not an alarm */
    }

    void reminder()
    {
        QMetaObject::invokeMethod(m_tg, "remind");
        QCOMPARE(m_out->count(), 0);            /* nothing active */
        m_zones->setSensorLost(1, true);
        m_tg->setModbusOnline(false);
        QMetaObject::invokeMethod(m_tg, "remind");
        QVERIFY(last().contains("still active: Modbus board offline, Camera no sensor"));
    }

    void commands()
    {
        sensor(0, 19.5, 80);
        m_zones->setHeat(0, true);
        m_zones->setRelayState(0, 1);

        m_tg->onCommand(111, "/status");
        QCOMPARE(m_replies->last().at(0).toLongLong(), 111);
        QVERIFY(lastReply().contains("Salotto: 19.5° set 18.0° - heating, relay ON"));
        QVERIFY(lastReply().contains("Camera: --.-°"));
        QVERIFY(lastReply().contains("No active alarm."));

        m_tg->onCommand(111, "/status@CasaBot");
        QVERIFY(lastReply().contains("Salotto: 19.5°"));

        m_tg->onCommand(111, "/zone Salotto");
        QVERIFY(lastReply().startsWith("Salotto\nTemperature 19.5°C"));
        QVERIFY(lastReply().contains("Relay 5: ON"));

        m_tg->onCommand(111, "/zone bagno");    /* unknown zone: help */
        QVERIFY(lastReply().contains("/zone <name>"));
        QVERIFY(lastReply().contains("salotto, camera"));

        m_tg->onCommand(111, "/today");
        QVERIFY(lastReply().contains("Relay log disabled"));
        m_tg->onCommand(111, "hello");
        QVERIFY(lastReply().contains("/status"));
    }

    void onTimeFromRelayLog()
    {
        RelayLog log(m_dir->filePath("log/relays.csv"));
        TelegramConfig tc;
        TelegramNotifier tg(m_zones, tc, &log);
        QSignalSpy replies(&tg, &TelegramNotifier::reply);

        /* 1 h 30 min of salotto today, from a hand written log */
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 midnight = QDateTime(QDate::currentDate(), QTime(0, 0)).toSecsSinceEpoch();
        if(now - midnight < 7200)
            QSKIP("less than 2 h since midnight: no room for 1:30 today");
        QFile f(log.fileFor(QDate::currentDate()));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        const qint64 start = now - 7200;
        f.write("time,epoch,relay,zones,state,reason,on_s\n");
        f.write(QString("x,%1,5,salotto,ON,regulation,\n").arg(start).toUtf8());
        f.write(QString("x,%1,5,salotto,OFF,regulation,%2\n").arg(start + 5400).arg(5400).toUtf8());
        f.close();

        const QString text = tg.onTimeText(QDateTime(QDate::currentDate(), QTime(0, 0)).toSecsSinceEpoch(), now, "today");
        QVERIFY2(text.contains("salotto: 1:30"), qPrintable(text));
        QVERIFY(text.contains("camera: 0:00"));
    }

    void relayLogOnSeconds()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("r.csv");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("time,epoch,relay,zones,state,reason,on_s\n"
                "x,1000,5,salotto,ON,regulation,\n"
                "x,1600,5,salotto,OFF,regulation,600\n"      /* 600 s */
                "x,2000,4,camera,ON,frost,\n"
                "x,2300,4,camera,OFF,startup,\n"             /* power loss: closed here, 300 s */
                "x,3000,1,a+b,ON,regulation,\n");            /* still ON */
        f.close();

        QMap<QString, qint64> all = RelayLog::onSeconds({ path }, 0, 10000, 3500);
        QCOMPARE(all.value("salotto"), qint64(600));
        QCOMPARE(all.value("camera"), qint64(300));
        QCOMPARE(all.value("a+b"), qint64(500));             /* until now */

        QMap<QString, qint64> part = RelayLog::onSeconds({ path, dir.filePath("missing.csv") }, 1200, 2100, 3500);
        QCOMPARE(part.value("salotto"), qint64(400));        /* clipped */
        QCOMPARE(part.value("camera"), qint64(100));
    }
};

int runTelegramTests(int argc, char *argv[])
{
    TstTelegram t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_telegram.moc"
