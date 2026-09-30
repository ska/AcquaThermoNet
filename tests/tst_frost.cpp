#include <QTest>
#include <QSignalSpy>
#include "configuration.h"
#include "zonemodel.h"
#include "termoregolazione.h"
#include "monoclock.h"
#include "testutil.h"

typedef QPair<int, bool> RelayWrite;

/*
 * Zones a (relay 1) and b (relay 2), sensor timeout 1s, frost 10 min ON
 * every 60 min below 6°C. Time moves with MonoClock::advanceForTest.
 */
class TstFrost : public QObject
{
    Q_OBJECT

    QTemporaryDir           *m_dir = nullptr;
    Configuration           *m_conf = nullptr;
    ZoneModel               *m_zones = nullptr;
    ModBusFrameProcessor    *m_fp = nullptr;
    Termoregolazione        *m_tr = nullptr;
    QSignalSpy              *m_frames = nullptr;

    void start(bool unknownProtect = true)
    {
        RegulationConfig rc;
        rc.minCycleS = 0;
        rc.sensorTimeoutS = 1;
        rc.relaySettleMs = 0;
        rc.exercise.enabled = false;
        rc.frost.unknownProtect = unknownProtect;
        m_tr = new Termoregolazione(m_zones, m_fp, rc);
    }

    void sensor(int zone, double temp)
    {
        ZoneData d;
        d.temp = temp;
        m_zones->setSensorData(zone, d);
    }

    /* both sensors seen, then silent past the timeout */
    void loseSensors()
    {
        sensor(0, 22);
        sensor(1, 22);
        MonoClock::advanceForTest(2000);
    }

    void minutes(int m)
    {
        MonoClock::advanceForTest(qint64(m) * 60000);
        m_tr->forceRefreshAllZones();
    }

    QList<RelayWrite> writes() const { return TestUtil::relayWrites(*m_frames); }
    bool relay1On() const
    {
        for(int i = writes().size() - 1; i >= 0; i--)
            if(writes()[i].first == 1)
                return writes()[i].second;
        return false;
    }

private slots:
    void init()
    {
        m_dir = new QTemporaryDir;
        m_conf = new Configuration(TestUtil::writeIni(*m_dir,
            "[ZONES]\nlist=a, b\na\\setpoint=20\nb\\setpoint=20\n[RELAY]\na\\relaynum=1\nb\\relaynum=2\n"));
        m_zones = new ZoneModel(m_conf);
        m_fp = new ModBusFrameProcessor;
        m_frames = new QSignalSpy(m_fp, &ModBusFrameProcessor::frameToSend);
    }

    void cleanup()
    {
        delete m_tr;
        delete m_frames;
        delete m_fp;
        delete m_zones;
        delete m_conf;
        delete m_dir;
        m_tr = nullptr;
    }

    void config()
    {
        QTemporaryDir dir;
        FrostConfig def = Configuration(TestUtil::writeIni(dir, "")).loadRegulation().frost;
        QVERIFY(def.enabled);
        QCOMPARE(def.outdoorBelow, 6.0);
        QCOMPARE(def.onMin, 10);
        QCOMPARE(def.periodMin, 60);
        QCOMPARE(def.outdoorMaxAgeMin, 180);
        QVERIFY(def.unknownProtect);

        QTemporaryDir dir2;
        FrostConfig c = Configuration(TestUtil::writeIni(dir2,
            "[FROST_PROTECTION]\nenabled=false\noutdoor_below=3.5\non_min=15\nperiod_min=45\n"
            "outdoor_max_age_min=60\noutdoor_unknown_protect=false\n")).loadRegulation().frost;
        QVERIFY(!c.enabled);
        QCOMPARE(c.outdoorBelow, 3.5);
        QCOMPARE(c.onMin, 15);
        QCOMPARE(c.periodMin, 45);
        QCOMPARE(c.outdoorMaxAgeMin, 60);
        QVERIFY(!c.unknownProtect);

        QTemporaryDir dir3;
        FrostConfig bad = Configuration(TestUtil::writeIni(dir3,
            "[FROST_PROTECTION]\non_min=90\nperiod_min=60\n")).loadRegulation().frost;
        QCOMPARE(bad.onMin, 60);                /* never longer than the period */
    }

    void cycle()
    {
        start();
        m_tr->setOutdoorTemperature(3);
        loseSensors();
        m_frames->clear();

        m_tr->forceRefreshAllZones();
        QVERIFY(m_zones->zone(0).frostProtection);
        QVERIFY(m_zones->zone(1).frostProtection);
        QVERIFY(relay1On());                    /* cycle starts ON */
        QVERIFY(writes().contains(RelayWrite(2, true)));

        minutes(9);
        QVERIFY(relay1On());
        minutes(2);                             /* 11 min */
        QVERIFY(!relay1On());
        QVERIFY(!m_zones->zone(0).heat);
        minutes(48);                            /* 59 min */
        QVERIFY(!relay1On());
        minutes(2);                             /* 61 min: next period */
        QVERIFY(relay1On());
    }

    void warmOutside()
    {
        start();
        m_tr->setOutdoorTemperature(8);
        loseSensors();
        m_tr->forceRefreshAllZones();
        QVERIFY(!m_zones->zone(0).frostProtection);
        QVERIFY(!relay1On());
    }

    void thresholdIsStrict()
    {
        start();
        loseSensors();
        m_tr->setOutdoorTemperature(6.0);
        QVERIFY(!m_tr->frostWanted());
        m_tr->setOutdoorTemperature(5.9);       /* met.no 5.6 must not become 6 */
        QVERIFY(m_tr->frostWanted());
        QVERIFY(m_zones->zone(0).frostProtection);
    }

    void outdoorGoesUp()
    {
        start();
        m_tr->setOutdoorTemperature(2);
        loseSensors();
        m_tr->forceRefreshAllZones();
        QVERIFY(relay1On());
        m_tr->setOutdoorTemperature(9);         /* evaluated at once */
        QVERIFY(!m_zones->zone(0).frostProtection);
        QVERIFY(!relay1On());
    }

    void unknownOutdoor()
    {
        start();                                /* no weather at all */
        loseSensors();
        m_tr->forceRefreshAllZones();
        QVERIFY(m_zones->zone(0).frostProtection);      /* fail safe */
    }

    void unknownOutdoorNoProtect()
    {
        start(false);
        loseSensors();
        m_tr->forceRefreshAllZones();
        QVERIFY(!m_zones->zone(0).frostProtection);
    }

    void staleOutdoor()
    {
        start(false);
        m_tr->setOutdoorTemperature(10);        /* warm, but old */
        MonoClock::advanceForTest(181 * 60000);
        QVERIFY(!m_tr->frostWanted());          /* unknown, no protect */
        m_tr->setOutdoorTemperature(1);
        QVERIFY(m_tr->frostWanted());
        MonoClock::advanceForTest(181 * 60000);
        QVERIFY(!m_tr->frostWanted());          /* cold, but too old */
    }

    void workingSensorNotProtected()
    {
        start();
        m_tr->setOutdoorTemperature(-5);
        sensor(0, 22);
        sensor(1, 22);
        MonoClock::advanceForTest(2000);
        sensor(0, 22);                          /* zone a still reporting */
        m_tr->forceRefreshAllZones();
        QVERIFY(!m_zones->zone(0).frostProtection);
        QVERIFY(m_zones->zone(1).frostProtection);
    }

    void sensorBack()
    {
        start();
        m_tr->setOutdoorTemperature(0);
        loseSensors();
        m_tr->forceRefreshAllZones();
        QVERIFY(m_zones->zone(0).frostProtection);

        sensor(0, 22);                          /* warm room: normal regulation */
        QVERIFY(!m_zones->zone(0).frostProtection);
        QVERIFY(!m_zones->zone(0).sensorLost);
        QVERIFY(!relay1On());
    }

    void neverSeenSensor()
    {
        start();
        m_tr->setOutdoorTemperature(0);
        m_tr->forceRefreshAllZones();
        QVERIFY(!m_zones->zone(0).frostProtection);     /* just started */
        MonoClock::advanceForTest(2000);
        m_tr->forceRefreshAllZones();
        QVERIFY(m_zones->zone(0).frostProtection);      /* timeout since start */
    }

    void disabled()
    {
        RegulationConfig rc;
        rc.sensorTimeoutS = 1;
        rc.exercise.enabled = false;
        rc.frost.enabled = false;
        m_tr = new Termoregolazione(m_zones, m_fp, rc);
        m_tr->setOutdoorTemperature(-10);
        loseSensors();
        m_tr->forceRefreshAllZones();
        QVERIFY(!m_zones->zone(0).frostProtection);
    }

    void shutdownBlocksOn()
    {
        start();
        m_tr->setOutdoorTemperature(0);
        loseSensors();
        m_tr->beginShutdown();
        m_frames->clear();
        minutes(61);
        QVERIFY(!writes().contains(RelayWrite(1, true)));
    }
};

int runFrostTests(int argc, char *argv[])
{
    TstFrost t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_frost.moc"
