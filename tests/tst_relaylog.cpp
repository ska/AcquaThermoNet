#include <QTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFile>
#include <QRegularExpression>
#include "relaylog.h"
#include "monoclock.h"
#include "configuration.h"
#include "zonemodel.h"
#include "termoregolazione.h"
#include "testutil.h"

class TstRelayLog : public QObject
{
    Q_OBJECT

    static QStringList lines(const QString &path)
    {
        QFile f(path);
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        QStringList l = QString::fromUtf8(f.readAll()).split('\n');
        l.removeAll(QString());
        return l;
    }

    /* column of a CSV line */
    static QString col(const QString &line, int i) { return line.split(',').value(i); }

private slots:
    void monthlyFileAndHeader()
    {
        QTemporaryDir dir;
        RelayLog log(dir.filePath("log/relays.csv"));
        QCOMPARE(log.fileFor(QDate(2026, 9, 29)), dir.filePath("log/relays-2026-09.csv"));

        log.record(5, "salotto", true, "regulation");
        const QStringList l = lines(log.fileFor(QDate::currentDate()));
        QCOMPARE(l.size(), 2);
        QCOMPARE(l[0], QString("time,epoch,relay,zones,state,reason,on_s"));
        QCOMPARE(col(l[1], 2), QString("5"));
        QCOMPARE(col(l[1], 3), QString("salotto"));
        QCOMPARE(col(l[1], 4), QString("ON"));
        QCOMPARE(col(l[1], 5), QString("regulation"));
        QCOMPARE(col(l[1], 6), QString(""));
        /* ISO time with UTC offset (or Z) */
        QVERIFY(QRegularExpression(R"(^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d([+-]\d\d:\d\d|Z)$)").match(col(l[1], 0)).hasMatch());
    }

    void onlyChangesWithDuration()
    {
        QTemporaryDir dir;
        RelayLog log(dir.filePath("relays.csv"));
        log.record(4, "camera", false, "startup");      /* unknown -> OFF: logged, no duration */
        log.record(4, "camera", false, "regulation");   /* no change: not logged */
        log.record(4, "camera", true, "regulation");
        log.record(4, "camera", true, "regulation");    /* resend: not logged */
        MonoClock::advanceForTest(3600 * 1000);
        log.record(4, "camera", false, "regulation");

        const QStringList l = lines(log.fileFor(QDate::currentDate()));
        QCOMPARE(l.size(), 4);
        QCOMPARE(col(l[1], 4), QString("OFF"));
        QCOMPARE(col(l[1], 6), QString(""));
        QCOMPARE(col(l[2], 4), QString("ON"));
        QCOMPARE(col(l[3], 4), QString("OFF"));
        QCOMPARE(col(l[3], 6).toInt(), 3600);           /* monotonic duration */
    }

    void removeOldMonths()
    {
        QTemporaryDir dir;
        const QDate today = QDate::currentDate();
        auto touch = [&dir](const QString &name) {
            QFile f(dir.filePath(name));
            f.open(QIODevice::WriteOnly);
            f.write("x\n");
        };
        const QString old = QString("relays-%1.csv").arg(today.addMonths(-3).toString("yyyy-MM"));
        const QString recent = QString("relays-%1.csv").arg(today.addMonths(-1).toString("yyyy-MM"));
        touch(old);
        touch(recent);
        touch("other-2000-01.csv");

        RelayLog log(dir.filePath("relays.csv"), 2);    /* this month and the previous one */
        log.record(1, "a", true, "regulation");
        QVERIFY(!QFile::exists(dir.filePath(old)));
        QVERIFY(QFile::exists(dir.filePath(recent)));
        QVERIFY(QFile::exists(dir.filePath("other-2000-01.csv")));
    }

    void reasonsFromRegulation()
    {
        QTemporaryDir dir;
        Configuration conf(TestUtil::writeIni(dir,
            "[ZONES]\nlist=a, b\na\\setpoint=20\nb\\setpoint=20\n[RELAY]\na\\relaynum=1\nb\\relaynum=1\n"));
        ZoneModel zones(&conf);
        ModBusFrameProcessor fp;
        RegulationConfig rc;
        rc.minCycleS = 0;
        rc.exercise.enabled = false;
        rc.frost.enabled = false;
        Termoregolazione tr(&zones, &fp, rc);
        RelayLog log(dir.filePath("relays.csv"));
        tr.setRelayLog(&log);

        tr.allRelaysOff();
        ZoneData d;
        d.temp = 15;
        zones.setSensorData(0, d);                      /* a needs heat */
        d.temp = 22;
        zones.setSensorData(0, d);
        tr.allRelaysOff();                              /* already OFF: not a change */
        d.temp = 15;
        zones.setSensorData(0, d);
        tr.beginShutdown();

        const QStringList l = lines(log.fileFor(QDate::currentDate()));
        QCOMPARE(l.size(), 6);
        QCOMPARE(col(l[1], 3), QString("a+b"));         /* shared relay */
        QCOMPARE(col(l[1], 4) + col(l[1], 5), QString("OFFstartup"));
        QCOMPARE(col(l[2], 4) + col(l[2], 5), QString("ONregulation"));
        QCOMPARE(col(l[3], 4) + col(l[3], 5), QString("OFFregulation"));
        QVERIFY(!col(l[3], 6).isEmpty());
        QCOMPARE(col(l[4], 4) + col(l[4], 5), QString("ONregulation"));
        QCOMPARE(col(l[5], 4) + col(l[5], 5), QString("OFFshutdown"));
    }
};

int runRelayLogTests(int argc, char *argv[])
{
    TstRelayLog t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_relaylog.moc"
