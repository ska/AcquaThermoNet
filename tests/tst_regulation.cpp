#include <QTest>
#include <QSignalSpy>
#include "configuration.h"
#include "zonemodel.h"
#include "termoregolazione.h"
#include "monoclock.h"
#include "testutil.h"

typedef QPair<int, bool> RelayWrite;

/*
 * Zone "a" relay 1, zone "b" no relay, both setpoint 20 (hysteresis 0.5).
 * Relay commands are read from the Modbus processor frames.
 */
class TstRegulation : public QObject
{
    Q_OBJECT

    QTemporaryDir           *m_dir = nullptr;
    Configuration           *m_conf = nullptr;
    ZoneModel               *m_zones = nullptr;
    ModBusFrameProcessor    *m_fp = nullptr;
    Termoregolazione        *m_tr = nullptr;
    QSignalSpy              *m_frames = nullptr;

    void start(int minCycleS, int sensorTimeoutS = 900)
    {
        RegulationConfig rc;
        rc.minCycleS = minCycleS;
        rc.sensorTimeoutS = sensorTimeoutS;
        rc.relaySettleMs = 0;
        rc.frost.enabled = false;           /* covered by tst_frost */
        m_tr = new Termoregolazione(m_zones, m_fp, rc);
    }

    void sensor(int zone, double temp)
    {
        ZoneData d;
        d.temp = temp;
        m_zones->setSensorData(zone, d);
    }

    QList<RelayWrite> writes() const { return TestUtil::relayWrites(*m_frames); }

private slots:
    void init()
    {
        m_dir = new QTemporaryDir;
        m_conf = new Configuration(TestUtil::writeIni(*m_dir,
            "[ZONES]\nlist=a, b\na\\setpoint=20\nb\\setpoint=20\n[RELAY]\na\\relaynum=1\n"));
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

    void hysteresis()
    {
        start(0);
        sensor(0, 19.7);                                /* inside the band: no switch */
        QVERIFY(writes().isEmpty());
        QVERIFY(!m_zones->zone(0).heat);

        sensor(0, 19.4);                                /* < 20 - 0.5 */
        QCOMPARE(writes(), QList<RelayWrite>() << RelayWrite(1, true));
        QVERIFY(m_zones->zone(0).heat);

        sensor(0, 20.0);                                /* inside the band: stays ON */
        QCOMPARE(writes().size(), 1);

        sensor(0, 20.1);                                /* > setpoint */
        QCOMPARE(writes().last(), RelayWrite(1, false));
        QVERIFY(!m_zones->zone(0).heat);
    }

    void setPointChangeRegulates()
    {
        start(0);
        sensor(0, 19.8);
        QVERIFY(writes().isEmpty());
        m_zones->setSetPoint(0, 21);
        QCOMPARE(writes(), QList<RelayWrite>() << RelayWrite(1, true));
    }

    void houseModeRegulates()
    {
        start(0);
        sensor(0, 19.0);
        QCOMPARE(writes(), QList<RelayWrite>() << RelayWrite(1, true));
        m_zones->setHouseMode(ZoneModel::ModeAway);     /* 15: no more demand */
        QCOMPARE(writes().last(), RelayWrite(1, false));
        m_zones->setHouseMode(ZoneModel::ModeNormal);   /* back to 20 */
        QCOMPARE(writes().last(), RelayWrite(1, true));

        sensor(0, 21.0);                                /* above 20: OFF */
        QCOMPARE(writes().last(), RelayWrite(1, false));
        m_zones->setHouseMode(ZoneModel::ModeBoost);    /* 25: heat */
        QCOMPARE(writes().last(), RelayWrite(1, true));
    }

    void zoneWithoutRelay()
    {
        start(0);
        sensor(1, 15);
        QVERIFY(m_zones->zone(1).heat);                 /* demand shown */
        QVERIFY(writes().isEmpty());                    /* nothing on the bus */
    }

    void allRelaysOff()
    {
        start(0);
        m_tr->allRelaysOff();
        QCOMPARE(writes(), QList<RelayWrite>() << RelayWrite(1, false));
    }

    void sensorTimeout()
    {
        start(0, 1);
        sensor(0, 18);
        QVERIFY(m_zones->zone(0).heat);

        MonoClock::advanceForTest(1100);
        m_tr->forceRefreshAllZones();                   /* periodic check */
        QVERIFY(m_zones->zone(0).sensorLost);
        QVERIFY(!m_zones->zone(0).heat);
        QCOMPARE(writes().last(), RelayWrite(1, false));

        sensor(0, 18);                                  /* sensor back */
        QVERIFY(!m_zones->zone(0).sensorLost);
        QVERIFY(m_zones->zone(0).heat);
    }

    void sensorTimeoutMonotonic()
    {
        /* Real 900s timeout on the monotonic clock: a wall clock jump
         * (NTP at boot, manual change) cannot shorten or extend it */
        start(0);
        sensor(0, 18);
        MonoClock::advanceForTest(899 * 1000);
        m_tr->forceRefreshAllZones();
        QVERIFY(!m_zones->zone(0).sensorLost);
        QVERIFY(m_zones->zone(0).heat);

        MonoClock::advanceForTest(2 * 1000);
        m_tr->forceRefreshAllZones();
        QVERIFY(m_zones->zone(0).sensorLost);
        QVERIFY(!m_zones->zone(0).heat);
    }

    void minCycleDefers()
    {
        start(1);
        sensor(0, 18);
        QCOMPARE(writes().size(), 1);

        m_zones->setSetPoint(0, 15);                    /* OFF wanted, too early */
        QCOMPARE(writes().size(), 1);
        QVERIFY(m_zones->zone(0).heat);
        QCOMPARE(m_zones->zone(0).pendingSwitch, 0);

        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 2, 3000);
        QCOMPARE(writes().last(), RelayWrite(1, false));
        QCOMPARE(m_zones->zone(0).pendingSwitch, -1);
    }

    void minCycleCancelled()
    {
        start(1);
        sensor(0, 18);
        m_zones->setSetPoint(0, 15);
        QCOMPARE(m_zones->zone(0).pendingSwitch, 0);

        m_zones->setSetPoint(0, 20);                    /* back: OFF no longer needed */
        QCOMPARE(m_zones->zone(0).pendingSwitch, -1);
        QTest::qWait(1500);
        QCOMPARE(writes().size(), 1);
        QVERIFY(m_zones->zone(0).heat);
    }

    void safetyOffIgnoresMinCycle()
    {
        start(60, 1);
        sensor(0, 18);
        MonoClock::advanceForTest(1100);
        m_tr->forceRefreshAllZones();
        QCOMPARE(writes().last(), RelayWrite(1, false));
    }

    void shutdownBlocksOn()
    {
        start(0);
        m_tr->beginShutdown();
        m_frames->clear();
        sensor(0, 15);
        QVERIFY(writes().isEmpty());
        QVERIFY(!m_zones->zone(0).heat);
    }

    void relayFeedback()
    {
        start(0);
        sensor(0, 18);                                  /* relay 1 ON commanded */
        m_frames->clear();

        m_fp->frameIncoming(TestUtil::relayAnswer(0x01));
        QCOMPARE(m_zones->zone(0).relayState, 1);
        QVERIFY(writes().isEmpty());

        /* board says OFF: resend, fault after RELAY_MAX_RETRY + 1 mismatches */
        for(int i = 0; i <= Termoregolazione::RELAY_MAX_RETRY; i++)
            m_fp->frameIncoming(TestUtil::relayAnswer(0x00));
        QCOMPARE(m_zones->zone(0).relayState, 0);
        QVERIFY(m_zones->zone(0).relayFault);
        QCOMPARE(writes().size(), Termoregolazione::RELAY_MAX_RETRY + 1);
        QCOMPARE(writes().last(), RelayWrite(1, true));

        m_fp->frameIncoming(TestUtil::relayAnswer(0x01));
        QVERIFY(!m_zones->zone(0).relayFault);
    }

    void modbusOfflineClearsRelayState()
    {
        start(0);
        m_fp->frameIncoming(TestUtil::relayAnswer(0x01));
        QCOMPARE(m_zones->zone(0).relayState, 1);
        emit m_fp->onlineChanged(false);
        QCOMPARE(m_zones->zone(0).relayState, -1);
    }
};

int runRegulationTests(int argc, char *argv[])
{
    TstRegulation t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_regulation.moc"
