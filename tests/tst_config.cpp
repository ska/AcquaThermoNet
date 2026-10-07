#include <QTest>
#include <QSettings>
#include <QRegularExpression>
#include <QFile>
#include "configuration.h"
#include "testutil.h"

class TstConfig : public QObject
{
    Q_OBJECT

private slots:
    void loadZones()
    {
        QTemporaryDir dir;
        Configuration conf(TestUtil::writeIni(dir,
            "[ZONES]\n"
            "list=salotto, studio\n"
            "salotto\\setpoint=17\n"
            "studio\\setpoint=21.5\n"
            "[RELAY]\n"
            "salotto\\relaynum=5\n"
            "studio\\relaynum=2\n"));

        const QVector<ZoneData> z = conf.loadZones();
        QCOMPARE(z.size(), 2);
        QCOMPARE(z[0].name, QString("salotto"));
        QCOMPARE(z[0].relay, 5);
        QCOMPARE(z[0].setPoint, 17.0);
        QCOMPARE(z[1].name, QString("studio"));
        QCOMPARE(z[1].relay, 2);
        QCOMPARE(z[1].setPoint, 21.5);
    }

    void loadZonesRejectsInvalid()
    {
        QTemporaryDir dir;
        Configuration conf(TestUtil::writeIni(dir,
            "[ZONES]\n"
            "list=ok, bad/name, ok, with space, list, norelay, bigrelay\n"
            "[RELAY]\n"
            "ok\\relaynum=1\n"
            "bigrelay\\relaynum=9\n"));

        const QVector<ZoneData> z = conf.loadZones();
        QCOMPARE(z.size(), 3);
        QCOMPARE(z[0].name, QString("ok"));
        QCOMPARE(z[1].name, QString("norelay"));
        QCOMPARE(z[1].relay, 0);                /* missing: no relay */
        QCOMPARE(z[2].name, QString("bigrelay"));
        QCOMPARE(z[2].relay, 0);                /* out of range: no relay */
        QCOMPARE(z[1].setPoint, double(TEMP_DEFAULT));
    }

    void modeIsReserved()
    {
        QTemporaryDir dir;
        Configuration conf(TestUtil::writeIni(dir, "[ZONES]\nlist=mode, a\n"));
        const QVector<ZoneData> z = conf.loadZones();
        QCOMPARE(z.size(), 1);
        QCOMPARE(z[0].name, QString("a"));
    }

    void modes()
    {
        QTemporaryDir dir;
        Configuration def(TestUtil::writeIni(dir, "[ZONES]\nlist=a\n"));
        ModeConfig mc = def.loadModes();
        QCOMPARE(mc.windowTemp, 8.0);
        QCOMPARE(mc.windowS, 30 * 60);
        QCOMPARE(mc.awayTemp, 15.0);
        QCOMPARE(mc.boostTemp, 25.0);
        QCOMPARE(mc.boostS, 30 * 60);

        QTemporaryDir dir2;
        Configuration conf(TestUtil::writeIni(dir2, "[MODES]\nwindow_temp=7.3\nwindow_min=45\naway_temp=16\n"
                                                    "boost_temp=23\nboost_min=20\n"));
        mc = conf.loadModes();
        QCOMPARE(mc.windowTemp, 7.5);                   /* rounded to the step */
        QCOMPARE(mc.windowS, 45 * 60);
        QCOMPARE(mc.awayTemp, 16.0);
        QCOMPARE(mc.boostTemp, 23.0);
        QCOMPARE(mc.boostS, 20 * 60);

        QTemporaryDir dir3;
        Configuration bad(TestUtil::writeIni(dir3, "[MODES]\nwindow_temp=2\nwindow_min=0\naway_temp=x\n"
                                                   "boost_temp=30\nboost_min=2000\n"));
        mc = bad.loadModes();
        QCOMPARE(mc.windowTemp, 8.0);
        QCOMPARE(mc.windowS, 30 * 60);
        QCOMPARE(mc.awayTemp, 15.0);
        QCOMPARE(mc.boostTemp, 25.0);
        QCOMPARE(mc.boostS, 30 * 60);

        QCOMPARE(conf.loadHouseMode(), QString());
        conf.saveHouseMode("away");
        QCOMPARE(conf.loadHouseMode(), QString("away"));
        QCOMPARE(conf.loadHouseModeUntil(), qint64(0));
        conf.saveHouseMode("boost", 1790000000);
        QCOMPARE(conf.loadHouseMode(), QString("boost"));
        QCOMPARE(conf.loadHouseModeUntil(), qint64(1790000000));
        conf.saveHouseMode("normal");                   /* until removed */
        QCOMPARE(conf.loadHouseModeUntil(), qint64(0));
    }

    void loadZonesEmpty()
    {
        QTemporaryDir dir;
        Configuration conf(TestUtil::writeIni(dir, "[MQTT]\nbroker_addr=x\n"));
        QVERIFY(conf.loadZones().isEmpty());
    }

    void saveSetPoints()
    {
        QTemporaryDir dir;
        const QString path = TestUtil::writeIni(dir, "[ZONES]\nlist=a, b\na\\setpoint=18\nb\\setpoint=19\n");
        Configuration conf(path);
        QCOMPARE(conf.statePath(), dir.filePath("state.ini"));

        QMap<QString, QPair<double, qint64>> sp;
        sp.insert("a", qMakePair(20.5, qint64(2000000000)));
        conf.saveSetPoints(sp);

        /* state.ini overrides, setting.ini is never written */
        const QVector<ZoneData> z = conf.loadZones();
        QCOMPARE(z[0].setPoint, 20.5);
        QCOMPARE(z[1].setPoint, 19.0);
        QCOMPARE(z[0].chronoDone, QDateTime::fromSecsSinceEpoch(2000000000));
        QVERIFY(!z[1].chronoDone.isValid());
        QCOMPARE(QSettings(path, QSettings::IniFormat).value("ZONES/a/setpoint").toDouble(), 18.0);
        QCOMPARE(QSettings(conf.statePath(), QSettings::IniFormat).value("ZONES/a/setpoint").toDouble(), 20.5);

        /* time unknown: no setpoint_at */
        sp.insert("a", qMakePair(21.0, qint64(0)));
        conf.saveSetPoints(sp);
        QVERIFY(!QSettings(conf.statePath(), QSettings::IniFormat).contains("ZONES/a/setpoint_at"));
        QVERIFY(!conf.loadZones()[0].chronoDone.isValid());
    }

    void chronoEditedInState()
    {
        QTemporaryDir dir;
        Configuration conf(TestUtil::writeIni(dir,
            "[CHRONO]\nholiday_days=sunday\na\\enabled=true\na\\weekday=06:30=20.5, 22:30=17\n"));
        ChronoConfig c = conf.loadChrono("a");
        QVERIFY(!c.edited);
        QCOMPARE(c.weekday.size(), 2);

        c.enabled = false;
        c.weekday = Chrono::parseProfile({ "07:00=19" }, "test");
        c.holiday = Chrono::parseProfile({ "09:00=20", "23:00=16.5" }, "test");
        conf.saveChrono("a", c);
        ChronoConfig s = conf.loadChrono("a");
        QVERIFY(s.edited);
        QVERIFY(!s.enabled);
        QCOMPARE(Chrono::toString(s.weekday), QString("07:00=19.0"));
        QCOMPARE(Chrono::toString(s.holiday), QString("09:00=20.0, 23:00=16.5"));
        QCOMPARE(s.holidayDays, quint8(1 << 7));    /* still from setting.ini */

        c.holiday.clear();
        conf.saveChrono("a", c);
        QVERIFY(conf.loadChrono("a").holiday.isEmpty());

        conf.resetChrono("a");
        s = conf.loadChrono("a");
        QVERIFY(!s.edited);
        QVERIFY(s.enabled);
        QCOMPARE(s.weekday.size(), 2);
    }

    void ensureSettings()
    {
        QTemporaryDir dir;
        const QString def = dir.filePath("setting.default.ini");
        const QString path = dir.filePath("setting.ini");
        QFile f(def);
        f.open(QIODevice::WriteOnly);
        f.write("[ZONES]\nlist=a\n");
        f.close();
        QFile::setPermissions(def, QFileDevice::ReadOwner);     /* deployed read-only */

        QVERIFY(Configuration::ensureSettings(path, def));
        QCOMPARE(Configuration(path).loadZones().size(), 1);
        QVERIFY(QFile::permissions(path) & QFileDevice::WriteOwner);

        /* existing file is kept */
        TestUtil::writeIni(dir, "[ZONES]\nlist=a, b\n");
        QVERIFY(Configuration::ensureSettings(path, def));
        QCOMPARE(Configuration(path).loadZones().size(), 2);

        QVERIFY(!Configuration::ensureSettings(dir.filePath("x.ini"), dir.filePath("missing.ini")));
    }

    void uniqueId()
    {
        QTemporaryDir dir;
        const QString path = TestUtil::writeIni(dir, "");

        Configuration conf(path);
        QCOMPARE(conf.uniqueId("AABBCC"), QString("AABBCC"));
        /* stored one wins over a different MAC */
        QCOMPARE(conf.uniqueId("112233"), QString("AABBCC"));
        QCOMPARE(QSettings(conf.statePath(), QSettings::IniFormat).value("MQTT/unique_id").toString(), QString("AABBCC"));
        QVERIFY(!QSettings(path, QSettings::IniFormat).contains("MQTT/unique_id"));

        /* set by hand in setting.ini: wins over the stored one */
        QTemporaryDir dir3;
        Configuration fixed(TestUtil::writeIni(dir3, "[MQTT]\nunique_id=FIXED\n"));
        QCOMPARE(fixed.uniqueId("AABBCC"), QString("FIXED"));

        QTemporaryDir dir2;
        Configuration conf2(TestUtil::writeIni(dir2, ""));
        const QString random = conf2.uniqueId("");
        QVERIFY(QRegularExpression("^[0-9a-f]{12}$").match(random).hasMatch());
        QCOMPARE(conf2.uniqueId(""), random);
    }

    void regulation()
    {
        QTemporaryDir dir;
        RegulationConfig def = Configuration(TestUtil::writeIni(dir, "")).loadRegulation();
        QCOMPARE(def.minCycleS, 180);
        QCOMPARE(def.sensorTimeoutS, 900);

        QTemporaryDir dir2;
        RegulationConfig rc = Configuration(TestUtil::writeIni(dir2,
            "[REGULATION]\nmin_cycle_s=0\nsensor_timeout_s=60\n")).loadRegulation();
        QCOMPARE(rc.minCycleS, 0);
        QCOMPARE(rc.sensorTimeoutS, 60);

        QTemporaryDir dir3;
        RegulationConfig bad = Configuration(TestUtil::writeIni(dir3,
            "[REGULATION]\nmin_cycle_s=-5\nsensor_timeout_s=0\n")).loadRegulation();
        QCOMPARE(bad.minCycleS, 180);
        QCOMPARE(bad.sensorTimeoutS, 900);
    }

    void weatherProviders()
    {
        QTemporaryDir dir;
        WeatherConfig def = Configuration(TestUtil::writeIni(dir, "")).loadWeather();
        QCOMPARE(def.provider, WeatherConfig::Wttr);
        QVERIFY(!def.enabled());                        /* no location */

        QTemporaryDir dir2;
        WeatherConfig wttr = Configuration(TestUtil::writeIni(dir2, "[WEATHER]\nprovider=WTTR\nlocation=home\n")).loadWeather();
        QCOMPARE(wttr.provider, WeatherConfig::Wttr);
        QVERIFY(wttr.enabled());

        QTemporaryDir dir3;
        WeatherConfig nocoords = Configuration(TestUtil::writeIni(dir3, "[WEATHER]\nprovider=metno\nlocation=home\n")).loadWeather();
        QCOMPARE(nocoords.provider, WeatherConfig::MetNo);
        QVERIFY(!nocoords.enabled());                   /* met.no needs lat/lon */

        QTemporaryDir dir4;
        WeatherConfig unknown = Configuration(TestUtil::writeIni(dir4, "[WEATHER]\nprovider=other\nlocation=x\n")).loadWeather();
        QCOMPARE(unknown.provider, WeatherConfig::Wttr);
    }

    void mqttTls()
    {
        QTemporaryDir dir;
        mqtt_brk_t def;
        Configuration(TestUtil::writeIni(dir, "[MQTT]\nbroker_addr=x\n")).loadMqttInfo(def);
        QVERIFY(!def.tls);
        QVERIFY(def.tlsVerify);
        QVERIFY(def.caFile.isEmpty());

        QTemporaryDir dir2;
        mqtt_brk_t m;
        Configuration(TestUtil::writeIni(dir2,
            "[MQTT]\nbroker_addr=10.0.0.1\nbroker_port=8883\ntls=true\nca_file=certs/ca.pem\n"
            "cert_file=/etc/client.pem\nkey_file=certs/client.key\ntls_verify=false\npeer_name=broker.home\n")).loadMqttInfo(m);
        QVERIFY(m.tls);
        QCOMPARE(int(m.port), 8883);
        QVERIFY(!m.tlsVerify);
        QCOMPARE(m.caFile, dir2.filePath("certs/ca.pem"));     /* relative to setting.ini */
        QCOMPARE(m.certFile, QString("/etc/client.pem"));       /* absolute kept */
        QCOMPARE(m.keyFile, dir2.filePath("certs/client.key"));
        QCOMPARE(m.peerName, QString("broker.home"));
    }

    void mqttSerialWeatherLog()
    {
        QTemporaryDir dir;
        Configuration conf(TestUtil::writeIni(dir,
            "[MQTT]\nbroker_addr=10.0.0.1\nbroker_uname=u\nbroker_password=p\n"
            "[WEATHER]\nprovider=metno\nlocation=home\nlat=45.12\nlon=12.34\naltitude=100\n"
            "[LOG]\nfile=log/app.log\nmax_kb=100\n"));

        mqtt_brk_t m;
        conf.loadMqttInfo(m);
        QCOMPARE(m.address, QString("10.0.0.1"));
        QCOMPARE(int(m.port), 1883);           /* default */
        QCOMPARE(m.uname, QString("u"));

        QString port;
        qint32 baud;
        conf.loadSerial(port, baud);
        QCOMPARE(port, QString("com1"));
        QCOMPARE(baud, 9600);

        const WeatherConfig wc = conf.loadWeather();
        QCOMPARE(wc.provider, WeatherConfig::MetNo);
        QCOMPARE(wc.location, QString("home"));
        QCOMPARE(wc.lat, 45.12);
        QCOMPARE(wc.lon, 12.34);
        QCOMPARE(wc.altitude, 100);
        QCOMPARE(wc.pollS, 600);
        QVERIFY(wc.enabled());

        QString logPath;
        qint64 maxBytes;
        int files;
        conf.loadLog(logPath, maxBytes, files);
        QCOMPARE(logPath, dir.filePath("log/app.log"));    /* relative to setting.ini */
        QCOMPARE(maxBytes, qint64(100 * 1024));
        QCOMPARE(files, 3);
    }
};

int runConfigTests(int argc, char *argv[])
{
    TstConfig t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_config.moc"
