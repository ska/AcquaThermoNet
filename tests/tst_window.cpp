#include <QTest>
#include <QSignalSpy>
#include "configuration.h"
#include "zonemodel.h"
#include "windowdetector.h"
#include "monoclock.h"
#include "testutil.h"

static const qint64 MIN = 60 * 1000;

/*
 * Open window detection: WindowDetector with explicit times (defaults:
 * 1.0 degC within 10 min, closed after a rise of 0.3 degC), WindowWatch on
 * a ZoneModel with zones a and b, time moved with MonoClock::advanceForTest.
 */
class TstWindow : public QObject
{
    Q_OBJECT

    QTemporaryDir   *m_dir = nullptr;
    Configuration   *m_conf = nullptr;
    ZoneModel       *m_zones = nullptr;
    WindowWatch     *m_watch = nullptr;
    QSignalSpy      *m_opened = nullptr;
    QSignalSpy      *m_closed = nullptr;

    void sensor(int zone, double temp, int afterMin = 1)
    {
        MonoClock::advanceForTest(afterMin * MIN);
        ZoneData d;
        d.temp = temp;
        m_zones->setSensorData(zone, d);
    }

private slots:
    void init()
    {
        m_dir = new QTemporaryDir;
        m_conf = new Configuration(TestUtil::writeIni(*m_dir, "[ZONES]\nlist=a, b\n"));
        m_zones = new ZoneModel(m_conf);
        m_watch = new WindowWatch(m_zones, WindowConfig());
        m_opened = new QSignalSpy(m_watch, &WindowWatch::windowOpened);
        m_closed = new QSignalSpy(m_watch, &WindowWatch::windowClosed);
    }

    void cleanup()
    {
        delete m_closed;
        delete m_opened;
        delete m_watch;
        delete m_zones;
        delete m_conf;
        delete m_dir;
    }

    void config()
    {
        QTemporaryDir dir;
        const WindowConfig def = Configuration(TestUtil::writeIni(dir, "[ZONES]\nlist=a\n")).loadWindow();
        QVERIFY(def.enabled);
        QCOMPARE(def.dropC, 1.0);
        QCOMPARE(def.windowMin, 10);
        QCOMPARE(def.recoverC, 0.3);

        QTemporaryDir dir2;
        const WindowConfig wc = Configuration(TestUtil::writeIni(dir2,
            "[WINDOW_DETECTION]\nenabled=false\ndrop_c=1.5\nwindow_min=5\nrecover_c=0.5\n")).loadWindow();
        QVERIFY(!wc.enabled);
        QCOMPARE(wc.dropC, 1.5);
        QCOMPARE(wc.windowMin, 5);
        QCOMPARE(wc.recoverC, 0.5);

        /* out of range: the defaults */
        QTemporaryDir dir3;
        const WindowConfig bad = Configuration(TestUtil::writeIni(dir3,
            "[WINDOW_DETECTION]\ndrop_c=0.1\nwindow_min=0\nrecover_c=2\n")).loadWindow();
        QCOMPARE(bad.dropC, 1.0);
        QCOMPARE(bad.windowMin, 10);
        QCOMPARE(bad.recoverC, 0.3);
    }

    void dropOpens()
    {
        WindowDetector d;
        QCOMPARE(d.feed(0, 21.0), WindowDetector::None);
        QCOMPARE(d.feed(2 * MIN, 20.8), WindowDetector::None);
        QCOMPARE(d.feed(4 * MIN, 20.3), WindowDetector::None);
        QCOMPARE(d.feed(6 * MIN, 20.0), WindowDetector::Opened);     /* 1.0 below 21.0, 6 min */
        QVERIFY(d.isOpen());
        QCOMPARE(d.startTemp(), 21.0);
        QCOMPARE(d.startMs(), 0LL);
        QCOMPARE(d.openedMs(), 6 * MIN);
    }

    void slowCoolingIgnored()
    {
        /* heating off: 0.2 degC every 5 min, 1.2 degC in 30 min */
        WindowDetector d;
        for(int i = 0; i <= 6; i++)
            QCOMPARE(d.feed(i * 5 * MIN, 21.0 - 0.2 * i), WindowDetector::None);
        QVERIFY(!d.isOpen());
    }

    void oldReadingsForgotten()
    {
        /* the high reading is 11 min old: the drop is not "within 10 min" */
        WindowDetector d;
        QCOMPARE(d.feed(0, 21.0), WindowDetector::None);
        QCOMPARE(d.feed(11 * MIN, 20.0), WindowDetector::None);
    }

    void closesOnRise()
    {
        WindowDetector d;
        d.feed(0, 21.0);
        QCOMPARE(d.feed(3 * MIN, 19.8), WindowDetector::Opened);
        QCOMPARE(d.feed(6 * MIN, 19.0), WindowDetector::None);       /* still falling */
        QCOMPARE(d.feed(9 * MIN, 19.2), WindowDetector::None);       /* +0.2 */
        QCOMPARE(d.lowestTemp(), 19.0);
        QCOMPARE(d.feed(12 * MIN, 19.3), WindowDetector::Closed);    /* +0.3 */
        QVERIFY(!d.isOpen());

        /* the readings before the drop do not open it again */
        QCOMPARE(d.feed(13 * MIN, 19.3), WindowDetector::None);
        QCOMPARE(d.feed(14 * MIN, 18.3), WindowDetector::Opened);    /* a new drop */
    }

    void reset()
    {
        WindowDetector d;
        d.feed(0, 21.0);
        d.feed(1 * MIN, 19.5);
        QVERIFY(d.isOpen());
        d.reset();
        QVERIFY(!d.isOpen());
        QCOMPARE(d.feed(2 * MIN, 19.0), WindowDetector::None);       /* no history */
    }

    void watchSignals()
    {
        sensor(0, 21.0);
        sensor(1, 20.0);
        sensor(0, 20.5, 2);
        sensor(0, 19.9, 2);
        QCOMPARE(m_opened->count(), 1);
        QCOMPARE(m_watch->isOpen(0), true);
        QCOMPARE(m_watch->isOpen(1), false);
        const QList<QVariant> o = m_opened->first();
        QCOMPARE(o.at(0).toInt(), 0);
        QCOMPARE(o.at(1).toDouble(), 21.0);
        QCOMPARE(o.at(2).toDouble(), 19.9);
        QCOMPARE(o.at(3).toInt(), 5);           /* from the 21.0 reading, 5 min */

        sensor(0, 19.5, 5);
        sensor(0, 19.8, 10);
        QCOMPARE(m_closed->count(), 1);
        const QList<QVariant> c = m_closed->first();
        QCOMPARE(c.at(0).toInt(), 0);
        QCOMPARE(c.at(1).toDouble(), 19.5);
        QCOMPARE(c.at(2).toInt(), 15);          /* since the drop was detected */
        QCOMPARE(m_opened->count(), 1);
    }

    void sensorLostResets()
    {
        sensor(0, 21.0);
        sensor(0, 19.5);
        QVERIFY(m_watch->isOpen(0));
        m_zones->setSensorLost(0, true);
        QVERIFY(!m_watch->isOpen(0));
        sensor(0, 21.0, 30);
        QCOMPARE(m_closed->count(), 0);         /* no message for a lost sensor */
    }

    void suspendedInWindowMode()
    {
        sensor(0, 21.0);
        m_zones->setHouseMode(ZoneModel::ModeWindow);
        sensor(0, 19.0);
        QCOMPARE(m_opened->count(), 0);
        m_zones->setHouseMode(ZoneModel::ModeNormal);
        sensor(0, 19.2);                        /* the readings during the mode are not history */
        QCOMPARE(m_opened->count(), 0);
    }

    void disabled()
    {
        WindowConfig off;
        off.enabled = false;
        WindowWatch watch(m_zones, off);
        QSignalSpy opened(&watch, &WindowWatch::windowOpened);
        sensor(0, 21.0);
        sensor(0, 18.0);
        QCOMPARE(opened.count(), 0);
    }
};

int runWindowTests(int argc, char *argv[])
{
    TstWindow t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_window.moc"
