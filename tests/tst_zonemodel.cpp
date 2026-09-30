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
