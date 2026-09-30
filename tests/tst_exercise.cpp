#include <QTest>
#include <QSignalSpy>
#include <QSettings>
#include "configuration.h"
#include "zonemodel.h"
#include "termoregolazione.h"
#include "valveexercise.h"
#include "testutil.h"

typedef QPair<int, bool> RelayCmd;
Q_DECLARE_METATYPE(ExerciseConfig)

/* 2026-09-27 is a Sunday */
static QDateTime at(int day, int h, int m, int s = 0)
{
    return QDateTime(QDate(2026, 9, day), QTime(h, m, s));
}

class TstExercise : public QObject
{
    Q_OBJECT

    QTemporaryDir           *m_dir = nullptr;
    Configuration           *m_conf = nullptr;
    ZoneModel               *m_zones = nullptr;
    ModBusFrameProcessor    *m_fp = nullptr;
    Termoregolazione        *m_tr = nullptr;
    QSignalSpy              *m_frames = nullptr;

    static ExerciseConfig fast()
    {
        ExerciseConfig c;
        c.cycles = 1;
        c.onS = 1;
        c.offS = 1;
        return c;
    }

    /* Zones a (relay 1) and b (relay 2); relay last activations in state.ini */
    void start(qint64 lastOn1, qint64 lastOn2)
    {
        m_dir = new QTemporaryDir;
        m_conf = new Configuration(TestUtil::writeIni(*m_dir,
            "[ZONES]\nlist=a, b\na\\setpoint=20\nb\\setpoint=20\n[RELAY]\na\\relaynum=1\nb\\relaynum=2\n"));
        if(lastOn1)
            m_conf->saveRelayLastOn(1, lastOn1);
        if(lastOn2)
            m_conf->saveRelayLastOn(2, lastOn2);

        m_zones = new ZoneModel(m_conf);
        m_fp = new ModBusFrameProcessor;
        m_frames = new QSignalSpy(m_fp, &ModBusFrameProcessor::frameToSend);
        RegulationConfig rc;
        rc.minCycleS = 0;
        rc.relaySettleMs = 0;
        rc.exercise = fast();
        m_tr = new Termoregolazione(m_zones, m_fp, rc, m_conf);
    }

    QList<RelayCmd> writes() const { return TestUtil::relayWrites(*m_frames); }
    static qint64 now() { return QDateTime::currentSecsSinceEpoch(); }
    qint64 storedLastOn(int relay) const
    {
        return QSettings(m_conf->statePath(), QSettings::IniFormat).value(QString("RELAYS/%1/last_on").arg(relay)).toLongLong();
    }

private slots:
    void cleanup()
    {
        delete m_tr;
        delete m_frames;
        delete m_fp;
        delete m_zones;
        delete m_conf;
        delete m_dir;
        m_tr = nullptr;
        m_frames = nullptr;
        m_fp = nullptr;
        m_zones = nullptr;
        m_conf = nullptr;
        m_dir = nullptr;
    }

    void isDue_data()
    {
        QTest::addColumn<QDateTime>("now");
        QTest::addColumn<QDate>("lastRun");
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<bool>("due");

        QVERIFY(QDate(2026, 9, 27).dayOfWeek() == 7);
        QTest::newRow("sunday 07:00")       << at(27, 7, 0)      << QDate()              << true  << true;
        QTest::newRow("end of window")      << at(27, 7, 9, 59)  << QDate()              << true  << true;
        QTest::newRow("after window")       << at(27, 7, 10)     << QDate()              << true  << false;
        QTest::newRow("before")             << at(27, 6, 59, 59) << QDate()              << true  << false;
        QTest::newRow("saturday")           << at(26, 7, 5)      << QDate()              << true  << false;
        QTest::newRow("already run today")  << at(27, 7, 5)      << QDate(2026, 9, 27)   << true  << false;
        QTest::newRow("run last week")      << at(27, 7, 5)      << QDate(2026, 9, 20)   << true  << true;
        QTest::newRow("disabled")           << at(27, 7, 5)      << QDate()              << false << false;
    }

    void isDue()
    {
        QFETCH(QDateTime, now);
        QFETCH(QDate, lastRun);
        QFETCH(bool, enabled);
        QFETCH(bool, due);
        ExerciseConfig c;
        c.enabled = enabled;
        QCOMPARE(ValveExercise::isDue(now, c, lastRun), due);
    }

    void isDueCustomDay()
    {
        ExerciseConfig c;
        c.dayOfWeek = 3;                    /* Wednesday */
        c.time = QTime(18, 30);
        QVERIFY(ValveExercise::isDue(at(30, 18, 31), c, QDate()));
        QVERIFY(!ValveExercise::isDue(at(27, 18, 31), c, QDate()));
    }

    void config()
    {
        QTemporaryDir dir;
        ExerciseConfig def = Configuration(TestUtil::writeIni(dir, "")).loadRegulation().exercise;
        QVERIFY(def.enabled);
        QCOMPARE(def.dayOfWeek, 7);
        QCOMPARE(def.time, QTime(7, 0));
        QCOMPARE(def.cycles, 3);
        QCOMPARE(def.onS, 60);
        QCOMPARE(def.offS, 60);
        QCOMPARE(def.idleDays, 7);

        QTemporaryDir dir2;
        ExerciseConfig c = Configuration(TestUtil::writeIni(dir2,
            "[VALVE_EXERCISE]\nenabled=false\nday=wed\ntime=18:30\ncycles=5\non_s=30\noff_s=90\nidle_days=3\n"))
            .loadRegulation().exercise;
        QVERIFY(!c.enabled);
        QCOMPARE(c.dayOfWeek, 3);
        QCOMPARE(c.time, QTime(18, 30));
        QCOMPARE(c.cycles, 5);
        QCOMPARE(c.onS, 30);
        QCOMPARE(c.offS, 90);
        QCOMPARE(c.idleDays, 3);

        QTemporaryDir dir3;
        ExerciseConfig n = Configuration(TestUtil::writeIni(dir3, "[VALVE_EXERCISE]\nday=5\n")).loadRegulation().exercise;
        QCOMPARE(n.dayOfWeek, 5);

        QTemporaryDir dir4;
        ExerciseConfig bad = Configuration(TestUtil::writeIni(dir4,
            "[VALVE_EXERCISE]\nday=someday\ntime=25:99\ncycles=0\n")).loadRegulation().exercise;
        QCOMPARE(bad.dayOfWeek, 7);
        QCOMPARE(bad.time, QTime(7, 0));
        QCOMPARE(bad.cycles, 3);
    }

    void sequence()
    {
        ExerciseConfig c = fast();
        c.cycles = 2;
        ValveExercise ex(c);
        QSignalSpy cmds(&ex, &ValveExercise::relayCommand);
        QSignalSpy done(&ex, &ValveExercise::finished);

        ex.start({1, 3});
        QVERIFY(ex.isRunning());
        QCOMPARE(cmds.count(), 2);              /* ON now */

        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 6000);
        QList<RelayCmd> got;
        for(const QList<QVariant> &a : cmds)
            got << RelayCmd(a.at(0).toInt(), a.at(1).toBool());
        QCOMPARE(got, QList<RelayCmd>() << RelayCmd(1, true)  << RelayCmd(3, true)
                                        << RelayCmd(1, false) << RelayCmd(3, false)
                                        << RelayCmd(1, true)  << RelayCmd(3, true)
                                        << RelayCmd(1, false) << RelayCmd(3, false));
        QVERIFY(!ex.isRunning());
    }

    void release()
    {
        ValveExercise ex(fast());
        QSignalSpy cmds(&ex, &ValveExercise::relayCommand);
        QSignalSpy done(&ex, &ValveExercise::finished);

        ex.start({1, 3});
        ex.release(1);
        QVERIFY(!ex.owns(1));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 3000);
        QCOMPARE(cmds.count(), 3);              /* 1 ON, 3 ON, 3 OFF */
        QCOMPARE(cmds.last().at(0).toInt(), 3);

        ex.start({2});
        ex.release(2);                          /* last relay: over */
        QCOMPARE(done.count(), 2);
    }

    void onlyIdleRelays()
    {
        start(now() - 8 * 86400, now() - 86400);
        QCOMPARE(m_tr->startValveExercise(), 1);
        QVERIFY(m_zones->zone(0).valveExercise);
        QVERIFY(!m_zones->zone(1).valveExercise);
        QCOMPARE(writes(), QList<RelayCmd>() << RelayCmd(1, true));

        /* periodic resend of OFF does not cut the exercise */
        m_tr->forceRefreshAllZones();
        QCOMPARE(writes().count(RelayCmd(1, false)), 0);

        QTRY_VERIFY_WITH_TIMEOUT(!m_tr->valveExerciseRunning(), 3000);
        QCOMPARE(writes().last(), RelayCmd(1, false));
        QVERIFY(!m_zones->zone(0).valveExercise);

        /* the exercise counts as activation */
        QVERIFY(storedLastOn(1) >= now() - 5);
        QCOMPARE(m_tr->startValveExercise(), 0);
    }

    void heatDemandTakesRelay()
    {
        start(now() - 8 * 86400, now() - 8 * 86400);
        QCOMPARE(m_tr->startValveExercise(), 2);

        ZoneData d;
        d.temp = 15;
        m_zones->setSensorData(0, d);           /* zone a needs heat */
        QVERIFY(m_zones->zone(0).heat);
        QVERIFY(!m_zones->zone(0).valveExercise);

        QTRY_VERIFY_WITH_TIMEOUT(!m_tr->valveExerciseRunning(), 3000);
        /* relay 2 exercised and OFF, relay 1 left ON to the regulation */
        QCOMPARE(writes().count(RelayCmd(1, false)), 0);
        QCOMPARE(writes().last(), RelayCmd(2, false));
    }

    void heatingZoneNotExercised()
    {
        start(now() - 8 * 86400, now() - 8 * 86400);
        ZoneData d;
        d.temp = 15;
        m_zones->setSensorData(1, d);           /* zone b heating right now */
        m_frames->clear();
        QCOMPARE(m_tr->startValveExercise(), 1);
        QCOMPARE(writes(), QList<RelayCmd>() << RelayCmd(1, true));
    }

    void shutdownAborts()
    {
        start(now() - 8 * 86400, now() - 86400);
        QCOMPARE(m_tr->startValveExercise(), 1);
        m_tr->beginShutdown();
        QVERIFY(!m_tr->valveExerciseRunning());
        QCOMPARE(writes().last(), RelayCmd(2, false));
        QTest::qWait(1500);
        QCOMPARE(writes().count(RelayCmd(1, true)), 1);   /* no ON after shutdown */
    }

    void firstStartBaseline()
    {
        start(0, 0);                            /* no history */
        QVERIFY(storedLastOn(1) >= now() - 5);
        QVERIFY(storedLastOn(2) >= now() - 5);
        QCOMPARE(m_tr->startValveExercise(), 0);
    }

    void clockSetBack()
    {
        start(now() + 30 * 86400, now() - 8 * 86400);     /* relay 1 "in the future" */
        QCOMPARE(m_tr->startValveExercise(), 1);
        QCOMPARE(writes(), QList<RelayCmd>() << RelayCmd(2, true));
    }
};

int runExerciseTests(int argc, char *argv[])
{
    TstExercise t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_exercise.moc"
