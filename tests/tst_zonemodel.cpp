#include <QTest>
#include <QSignalSpy>
#include <QSettings>
#include "configuration.h"
#include "zonemodel.h"
#include "testutil.h"

class TstZoneModel : public QObject
{
    Q_OBJECT

    QTemporaryDir   *m_dir = nullptr;
    QString         m_path;

private slots:
    void init()
    {
        m_dir = new QTemporaryDir;
        m_path = TestUtil::writeIni(*m_dir,
            "[ZONES]\nlist=a, b\na\\setpoint=20\nb\\setpoint=18\n[RELAY]\na\\relaynum=1\n");
    }

    void cleanup()
    {
        delete m_dir;
        m_dir = nullptr;
    }

    void loadAndIndex()
    {
        Configuration conf(m_path);
        ZoneModel zones(&conf);
        QCOMPARE(zones.count(), 2);
        QCOMPARE(zones.indexOf("b"), 1);
        QCOMPARE(zones.indexOf("x"), -1);
        QCOMPARE(zones.zone(0).relay, 1);
    }

    void setPointNormalized_data()
    {
        QTest::addColumn<double>("in");
        QTest::addColumn<double>("out");
        QTest::newRow("step")       << 21.5 << 21.5;
        QTest::newRow("round up")   << 20.3 << 20.5;
        QTest::newRow("round down") << 20.2 << 20.0;
        QTest::newRow("max")        << 30.0 << double(TEMP_MAX);
        QTest::newRow("min")        << 1.0  << double(TEMP_MIN);
    }

    void setPointNormalized()
    {
        QFETCH(double, in);
        QFETCH(double, out);
        Configuration conf(m_path);
        ZoneModel zones(&conf);
        zones.setSetPoint(0, in);
        QCOMPARE(zones.zone(0).setPoint, out);
    }

    void setPointSignals()
    {
        Configuration conf(m_path);
        ZoneModel zones(&conf);
        QSignalSpy changed(&zones, &ZoneModel::zoneChanged);
        QSignalSpy setPoint(&zones, &ZoneModel::setPointChanged);

        zones.setSetPoint(0, 20);           /* same value */
        QCOMPARE(changed.count(), 0);
        QCOMPARE(setPoint.count(), 1);      /* republish anyway */

        zones.stepSetPoint(0, +1);
        QCOMPARE(zones.zone(0).setPoint, 20.5);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(setPoint.count(), 2);

        zones.setSetPoint(9, 20);           /* invalid zone ignored */
        QCOMPARE(setPoint.count(), 2);
    }

    void heatAndStatusSignals()
    {
        Configuration conf(m_path);
        ZoneModel zones(&conf);
        QSignalSpy heat(&zones, &ZoneModel::heatChanged);
        QSignalSpy changed(&zones, &ZoneModel::zoneChanged);

        zones.setHeat(0, true);
        zones.setHeat(0, true);             /* no change */
        QCOMPARE(heat.count(), 1);
        QCOMPARE(changed.count(), 1);

        zones.setRelayFault(0, true);
        zones.setRelayFault(0, true);
        zones.setSensorLost(0, true);
        zones.setRelayState(0, 1);
        zones.setPendingSwitch(0, 0, 1000);
        zones.setPendingSwitch(0, 0, 1000);
        QCOMPARE(changed.count(), 5);
        QCOMPARE(zones.zone(0).pendingSwitch, 0);

        zones.setPendingSwitch(0, -1, 1000);
        QCOMPARE(zones.zone(0).pendingSwitchAtMs, qint64(0));
    }

    void sensorData()
    {
        Configuration conf(m_path);
        ZoneModel zones(&conf);
        QSignalSpy sensor(&zones, &ZoneModel::sensorUpdated);

        ZoneData d;
        d.temp = 19.5;
        d.humidity = 40;
        d.setPoint = 99;                    /* not a sensor field: ignored */
        zones.setSensorData(1, d);

        QCOMPARE(sensor.count(), 1);
        QCOMPARE(zones.zone(1).temp, 19.5);
        QCOMPARE(zones.zone(1).setPoint, 18.0);
        QVERIFY(zones.zone(1).lastSeenMs > 0);
    }

    void deferredSave()
    {
        Configuration conf(m_path);
        ZoneModel zones(&conf, 200);

        zones.setSetPoint(0, 22);
        zones.setSetPoint(1, 16);
        auto stored = [&conf](const QString &zone) {
            return QSettings(conf.statePath(), QSettings::IniFormat).value("ZONES/" + zone + "/setpoint").toDouble();
        };
        QCOMPARE(stored("a"), 0.0);         /* not yet on flash */
        QTRY_COMPARE_WITH_TIMEOUT(stored("a"), 22.0, 2000);
        QCOMPARE(stored("b"), 16.0);        /* same write */
    }

    void houseModes()
    {
        /* a 20, b 18: window 8, away 15 -> both lowered */
        Configuration conf(m_path);
        ZoneModel zones(&conf);
        QCOMPARE(zones.houseMode(), ZoneModel::ModeNormal);
        QCOMPARE(zones.zone(0).target, 20.0);
        QSignalSpy setPoint(&zones, &ZoneModel::setPointChanged);
        QSignalSpy mode(&zones, &ZoneModel::houseModeChanged);

        QVERIFY(zones.setHouseMode(ZoneModel::ModeAway));
        QCOMPARE(zones.zone(0).target, 15.0);
        QCOMPARE(zones.zone(1).target, 15.0);
        QCOMPARE(zones.zone(0).setPoint, 20.0);         /* own kept */
        QCOMPARE(setPoint.count(), 2);                  /* state_temp of every zone */
        QCOMPARE(mode.count(), 1);
        QCOMPARE(zones.remainingS(), 0);

        QVERIFY(!zones.setHouseMode(ZoneModel::ModeWindow));    /* refused while away */
        QCOMPARE(zones.houseMode(), ZoneModel::ModeAway);

        /* own setpoint changed during the mode: applied after it */
        zones.setSetPoint(0, 22);
        QCOMPARE(zones.zone(0).target, 15.0);
        zones.setSetPoint(1, 12);                       /* lower than the mode: kept */
        QCOMPARE(zones.zone(1).target, 12.0);

        QVERIFY(zones.setHouseMode(ZoneModel::ModeNormal));
        QCOMPARE(zones.zone(0).target, 22.0);
        QCOMPARE(zones.zone(1).target, 12.0);

        QVERIFY(zones.setHouseMode(ZoneModel::ModeWindow));
        QCOMPARE(zones.zone(0).target, 8.0);
        QVERIFY(zones.remainingS() > 30 * 60 - 2 && zones.remainingS() <= 30 * 60);
        QVERIFY(zones.setHouseMode(ZoneModel::ModeAway));      /* away replaces window */
        QCOMPARE(zones.zone(0).target, 15.0);
        QCOMPARE(zones.remainingS(), 0);
    }

    void boost()
    {
        /* a 20, b 18: boost 25 for every zone, never lowered */
        Configuration conf(m_path);
        ZoneModel zones(&conf);
        QVERIFY(zones.setHouseMode(ZoneModel::ModeAway));
        QVERIFY(zones.setHouseMode(ZoneModel::ModeBoost));     /* ends away */
        QCOMPARE(zones.zone(0).target, 25.0);
        QCOMPARE(zones.zone(1).target, 25.0);
        QVERIFY(zones.remainingS() > 30 * 60 - 2 && zones.remainingS() <= 30 * 60);

        ModeConfig mc;
        mc.boostTemp = 19;
        zones.setModeConfig(mc);
        QCOMPARE(zones.zone(0).target, 20.0);           /* max(own, 19) */
        QCOMPARE(zones.zone(1).target, 19.0);

        QVERIFY(zones.setHouseMode(ZoneModel::ModeWindow));    /* windows open stop the boost */
        QCOMPARE(zones.zone(0).target, 8.0);
        QVERIFY(zones.setHouseMode(ZoneModel::ModeBoost));
        QVERIFY(zones.setHouseMode(ZoneModel::ModeNormal));
        QCOMPARE(zones.zone(0).target, 20.0);
        QCOMPARE(zones.remainingS(), 0);
    }

    void boostEnds()
    {
        Configuration conf(m_path);
        ZoneModel zones(&conf);
        ModeConfig mc;
        mc.boostS = 1;
        zones.setModeConfig(mc);
        QVERIFY(zones.setHouseMode(ZoneModel::ModeBoost));
        QTRY_COMPARE_WITH_TIMEOUT(zones.houseMode(), ZoneModel::ModeNormal, 3000);
        QCOMPARE(zones.zone(1).target, 18.0);
    }

    void windowEnds()
    {
        Configuration conf(m_path);
        ZoneModel zones(&conf);
        ModeConfig mc;
        mc.windowS = 1;
        zones.setModeConfig(mc);
        QVERIFY(zones.setHouseMode(ZoneModel::ModeWindow));
        QCOMPARE(zones.zone(0).target, 8.0);
        QTRY_COMPARE_WITH_TIMEOUT(zones.houseMode(), ZoneModel::ModeNormal, 3000);
        QCOMPARE(zones.zone(0).target, 20.0);
    }

    void modeAfterRestart()
    {
        Configuration conf(m_path);
        {
            ZoneModel zones(&conf);
            zones.setHouseMode(ZoneModel::ModeAway);
        }
        {
            ZoneModel zones(&conf);                     /* away goes on */
            QCOMPARE(zones.houseMode(), ZoneModel::ModeAway);
            QCOMPARE(zones.zone(0).target, 15.0);
            zones.setHouseMode(ZoneModel::ModeBoost);
        }
        {
            ZoneModel zones(&conf);                     /* boost resumed */
            QCOMPARE(zones.houseMode(), ZoneModel::ModeBoost);
            QVERIFY(zones.remainingS() > 30 * 60 - 5 && zones.remainingS() <= 30 * 60);
            QCOMPARE(zones.zone(1).target, 25.0);
            zones.setHouseMode(ZoneModel::ModeWindow);
        }
        {
            ZoneModel zones(&conf);                     /* window resumed */
            QCOMPARE(zones.houseMode(), ZoneModel::ModeWindow);
            QCOMPARE(zones.zone(0).target, 8.0);
            zones.setHouseMode(ZoneModel::ModeNormal);
        }
        QCOMPARE(conf.loadHouseModeUntil(), qint64(0));
        ZoneModel zones(&conf);
        QCOMPARE(zones.houseMode(), ZoneModel::ModeNormal);
        QCOMPARE(zones.zone(0).target, 20.0);
    }

    /* state.ini written as by an earlier run, `until` relative to now */
    void modeResume_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<qint64>("untilFromNowS");     /* 0: no until saved */
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("minLeftS");

        QTest::newRow("window, time left")  << "window" << qint64(100)  << "window" << 95;
        QTest::newRow("boost, time left")   << "boost"  << qint64(600)  << "boost"  << 595;
        QTest::newRow("window, over")       << "window" << qint64(-10)  << "normal" << 0;
        QTest::newRow("boost, no until")    << "boost"  << qint64(0)    << "normal" << 0;
        QTest::newRow("boost, more than boost_min (clock set back)")
                                            << "boost"  << qint64(5000) << "boost"  << 30 * 60 - 5;
    }

    void modeResume()
    {
        QFETCH(QString, mode);
        QFETCH(qint64, untilFromNowS);
        QFETCH(QString, expected);
        QFETCH(int, minLeftS);

        Configuration conf(m_path);
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        conf.saveHouseMode(mode, untilFromNowS ? now + untilFromNowS : 0);

        ZoneModel zones(&conf);
        QCOMPARE(ZoneModel::modeName(zones.houseMode()), expected);
        QCOMPARE(conf.loadHouseMode(), expected);
        if(expected == "normal")
        {
            QCOMPARE(zones.remainingS(), 0);
            QCOMPARE(conf.loadHouseModeUntil(), qint64(0));
            QCOMPARE(zones.zone(0).target, 20.0);
        }
        else
        {
            /* +1: remainingS() rounds up */
            QVERIFY2(zones.remainingS() >= minLeftS && zones.remainingS() <= qMin<qint64>(untilFromNowS, 30 * 60) + 1,
                     qPrintable(QString::number(zones.remainingS())));
        }
    }

    void resumeS()
    {
        const QDateTime now = QDateTime::fromSecsSinceEpoch(1790000000);
        QCOMPARE(ZoneModel::resumeS(1790000100, 1800, now), 100);
        QCOMPARE(ZoneModel::resumeS(1790000000, 1800, now), 0);         /* just over */
        QCOMPARE(ZoneModel::resumeS(1789999000, 1800, now), 0);
        QCOMPARE(ZoneModel::resumeS(0, 1800, now), 0);                  /* not saved */
        QCOMPARE(ZoneModel::resumeS(1790009000, 1800, now), 1800);      /* clock set back: capped */
        /* clock not set yet (no NTP): unknown, ends */
        const QDateTime unset(QDate(2000, 1, 1), QTime(0, 5));
        QCOMPARE(ZoneModel::resumeS(unset.toSecsSinceEpoch() + 100, 1800, unset), 0);
    }

    void clockValid()
    {
        QVERIFY(!ZoneModel::clockValid(QDateTime(QDate(1970, 1, 1), QTime(0, 0))));
        QVERIFY(ZoneModel::clockValid(QDateTime(QDate(2026, 10, 2), QTime(12, 0))));
    }

    void modeRestartedNewUntil()
    {
        Configuration conf(m_path);
        ZoneModel zones(&conf);
        QVERIFY(zones.setHouseMode(ZoneModel::ModeBoost));
        const qint64 first = conf.loadHouseModeUntil();
        QVERIFY(first > QDateTime::currentSecsSinceEpoch());
        conf.saveHouseMode("boost", first - 600);       /* as if started 10 min ago */
        QVERIFY(zones.setHouseMode(ZoneModel::ModeBoost));
        QVERIFY(conf.loadHouseModeUntil() >= first);    /* restarted: full time again */
        QVERIFY(zones.setHouseMode(ZoneModel::ModeAway));
        QCOMPARE(conf.loadHouseModeUntil(), qint64(0));
    }

    void modeNames()
    {
        ZoneModel::HouseMode m;
        QVERIFY(ZoneModel::parseMode("window", m));
        QCOMPARE(m, ZoneModel::ModeWindow);
        QVERIFY(ZoneModel::parseMode("away", m));
        QVERIFY(ZoneModel::parseMode("boost", m));
        QCOMPARE(m, ZoneModel::ModeBoost);
        QVERIFY(ZoneModel::parseMode("normal", m));
        QCOMPARE(m, ZoneModel::ModeNormal);
        QVERIFY(!ZoneModel::parseMode("Away", m));
        QVERIFY(!ZoneModel::parseMode("", m));
        QCOMPARE(ZoneModel::modeName(ZoneModel::ModeAway), QString("away"));
    }

    void flushOnExit()
    {
        Configuration conf(m_path);
        ZoneModel zones(&conf, 60000);
        zones.setSetPoint(0, 23);
        zones.flushPendingSaves();
        QCOMPARE(QSettings(conf.statePath(), QSettings::IniFormat).value("ZONES/a/setpoint").toDouble(), 23.0);
    }
};

int runZoneModelTests(int argc, char *argv[])
{
    TstZoneModel t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_zonemodel.moc"
