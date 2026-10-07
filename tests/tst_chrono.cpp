#include <QTest>
#include "chrono.h"
#include "configuration.h"
#include "testutil.h"

/* 2026-09-25 is a Friday, 26 Saturday, 27 Sunday, 28 Monday */
static QDateTime at(int day, int h, int m)
{
    return QDateTime(QDate(2026, 9, day), QTime(h, m));
}

static QVector<ChronoSlot> parse(const QString &profile)
{
    return Chrono::parseProfile(profile.split(','), "test");
}

/* salotto of the documentation example */
static ChronoConfig example()
{
    ChronoConfig c;
    c.enabled     = true;
    c.weekday     = parse("06:30=20.5, 08:00=18, 17:00=20.5, 22:30=17");
    c.holiday     = parse("08:00=20.5, 23:00=17");
    c.holidayDays = (1 << 6) | (1 << 7);
    return c;
}

class TstChrono : public QObject
{
    Q_OBJECT

private slots:
    void current_data()
    {
        QTest::addColumn<QDateTime>("now");
        QTest::addColumn<double>("temp");
        QTest::addColumn<QDateTime>("start");

        QTest::newRow("slot start")         << at(25, 6, 30)  << 20.5 << at(25, 6, 30);
        QTest::newRow("inside a slot")      << at(25, 12, 0)  << 18.0 << at(25, 8, 0);
        QTest::newRow("last slot")          << at(25, 23, 59) << 17.0 << at(25, 22, 30);
        QTest::newRow("after midnight")     << at(25, 0, 10)  << 17.0 << at(24, 22, 30);
        QTest::newRow("friday to saturday") << at(26, 7, 0)   << 17.0 << at(25, 22, 30);
        QTest::newRow("saturday")           << at(26, 9, 0)   << 20.5 << at(26, 8, 0);
        QTest::newRow("sunday to monday")   << at(28, 5, 0)   << 17.0 << at(27, 23, 0);
        QTest::newRow("monday")             << at(28, 6, 30)  << 20.5 << at(28, 6, 30);
    }

    void current()
    {
        QFETCH(QDateTime, now);
        QFETCH(double, temp);
        QFETCH(QDateTime, start);
        const ChronoPoint p = Chrono::current(now, example());
        QVERIFY(p.isValid());
        QCOMPARE(p.temp, temp);
        QCOMPARE(p.start, start);
    }

    void next_data()
    {
        QTest::addColumn<QDateTime>("now");
        QTest::addColumn<double>("temp");
        QTest::addColumn<QDateTime>("start");

        QTest::newRow("before first")       << at(25, 5, 0)   << 20.5 << at(25, 6, 30);
        QTest::newRow("at a slot start")    << at(25, 6, 30)  << 18.0 << at(25, 8, 0);
        QTest::newRow("after last")         << at(25, 22, 30) << 20.5 << at(26, 8, 0);
        QTest::newRow("sunday to monday")   << at(27, 23, 30) << 20.5 << at(28, 6, 30);
    }

    void next()
    {
        QFETCH(QDateTime, now);
        QFETCH(double, temp);
        QFETCH(QDateTime, start);
        const ChronoPoint p = Chrono::next(now, example());
        QVERIFY(p.isValid());
        QCOMPARE(p.temp, temp);
        QCOMPARE(p.start, start);
    }

    void emptyHolidayUsesWeekday()
    {
        ChronoConfig c = example();
        c.holiday.clear();
        QVERIFY(Chrono::isHoliday(QDate(2026, 9, 26), c));
        const ChronoPoint p = Chrono::current(at(26, 7, 0), c);
        QCOMPARE(p.temp, 20.5);
        QCOMPARE(p.start, at(26, 6, 30));
        QCOMPARE(Chrono::next(at(26, 7, 0), c).start, at(26, 8, 0));
    }

    void singleSlot()
    {
        ChronoConfig c;
        c.enabled = true;
        c.weekday = parse("07:00=19");
        QCOMPARE(Chrono::current(at(25, 6, 0), c).start, at(24, 7, 0));
        QCOMPARE(Chrono::current(at(25, 7, 0), c).start, at(25, 7, 0));
        QCOMPARE(Chrono::next(at(25, 6, 0), c).start, at(25, 7, 0));
        QCOMPARE(Chrono::next(at(25, 7, 0), c).start, at(26, 7, 0));
    }

    void disabled()
    {
        ChronoConfig c = example();
        c.enabled = false;
        QVERIFY(!Chrono::current(at(25, 12, 0), c).isValid());
        QVERIFY(!Chrono::next(at(25, 12, 0), c).isValid());
        QVERIFY(!Chrono::current(at(25, 12, 0), ChronoConfig()).isValid());
    }

    void dstChange()
    {
        /* 2026-03-29: 02:00 -> 03:00 in Europe; in other zones a plain day */
        ChronoConfig c;
        c.enabled = true;
        c.weekday = parse("02:30=20, 22:00=17");
        const QDateTime now(QDate(2026, 3, 29), QTime(4, 0));
        const ChronoPoint p = Chrono::current(now, c);
        QVERIFY(p.isValid());
        QCOMPARE(p.temp, 20.0);
        QVERIFY(p.start <= now);
        QVERIFY(Chrono::next(now, c).start > now);
    }

    void dayTemps()
    {
        const ChronoConfig c = example();
        QVector<double> t = Chrono::dayTemps(QDate(2026, 9, 25), c);   /* Friday */
        QCOMPARE(t.size(), 96);
        QCOMPARE(t[0], 17.0);                   /* previous day's last slot */
        QCOMPARE(t[25], 17.0);                  /* 06:15 */
        QCOMPARE(t[26], 20.5);                  /* 06:30 */
        QCOMPARE(t[32], 18.0);                  /* 08:00 */
        QCOMPARE(t[89], 20.5);                  /* 22:15 */
        QCOMPARE(t[90], 17.0);                  /* 22:30 */
        QCOMPARE(Chrono::profileName(QDate(2026, 9, 25), c), QString("weekday"));

        t = Chrono::dayTemps(QDate(2026, 9, 28), c, 60);               /* Monday, hourly */
        QCOMPARE(t.size(), 24);
        QCOMPARE(t[6], 17.0);                   /* 06:00: Sunday 23:00 slot */
        QCOMPARE(t[7], 20.5);
        QCOMPARE(Chrono::profileName(QDate(2026, 9, 27), c), QString("holiday"));

        ChronoConfig noHoliday = c;
        noHoliday.holiday.clear();
        QCOMPARE(Chrono::profileName(QDate(2026, 9, 27), noHoliday), QString("weekday"));

        noHoliday.enabled = false;
        QVERIFY(Chrono::dayTemps(QDate(2026, 9, 25), noHoliday).isEmpty());

        /* DST day: 96 intervals of wall clock time, 02:30 slot included */
        ChronoConfig dst;
        dst.enabled = true;
        dst.weekday = parse("02:30=20, 22:00=17");
        t = Chrono::dayTemps(QDate(2026, 3, 29), dst);
        QCOMPARE(t.size(), 96);
        QCOMPARE(t[9], 17.0);
        QCOMPARE(t[10], 20.0);
    }

    void editSteps()
    {
        QVector<ChronoSlot> l = parse("06:40=20, 08:00=18, 08:15=19");
        QVERIFY(Chrono::stepTime(l, 0, -1));
        QCOMPARE(l[0].at, QTime(6, 30));        /* to the 15-minute grid */
        QVERIFY(Chrono::stepTime(l, 0, +1));
        QCOMPARE(l[0].at, QTime(6, 45));
        QVERIFY(!Chrono::stepTime(l, 1, +1));   /* would reach the next slot */
        QVERIFY(!Chrono::stepTime(l, 2, -1));
        QVERIFY(Chrono::stepTime(l, 2, +1));
        QCOMPARE(l[2].at, QTime(8, 30));
        QVERIFY(!Chrono::stepTime(l, 3, +1));   /* no such slot */

        QVector<ChronoSlot> e = parse("00:00=20, 23:45=17");
        QVERIFY(!Chrono::stepTime(e, 0, -1));
        QVERIFY(!Chrono::stepTime(e, 1, +1));   /* not past midnight */

        QVERIFY(Chrono::stepTemp(l, 0, +1));
        QCOMPARE(l[0].temp, 20.5);
        l[1].temp = TEMP_MIN;
        QVERIFY(!Chrono::stepTemp(l, 1, -1));
        l[1].temp = TEMP_MAX;
        QVERIFY(!Chrono::stepTemp(l, 1, +1));
    }

    void editAddSlot()
    {
        QVector<ChronoSlot> l;
        QVERIFY(Chrono::addSlot(l));
        QCOMPARE(Chrono::toString(l), QString("08:00=20.0"));
        QVERIFY(Chrono::addSlot(l));            /* one hour after the last */
        QCOMPARE(Chrono::toString(l), QString("08:00=20.0, 09:00=20.0"));

        l = parse("06:00=21, 12:00=18, 23:30=17");
        QVERIFY(Chrono::addSlot(l));            /* no room after: longest gap */
        QCOMPARE(Chrono::toString(l), QString("06:00=21.0, 12:00=18.0, 17:45=18.0, 23:30=17.0"));

        l = parse("01:00=18, 02:00=19, 03:00=20, 04:00=21, 05:00=20, 06:00=19, 07:00=18");
        QVERIFY(Chrono::addSlot(l));
        QCOMPARE(l.size(), ChronoConfig::MAX_SLOTS);
        QVERIFY(!Chrono::addSlot(l));           /* at most 8 */

        l = parse("00:00=20, 00:15=19, 23:15=17");
        QVERIFY(Chrono::addSlot(l));            /* 23:15 + 1 h is past midnight */
        QCOMPARE(l[2].at, QTime(11, 45));
    }

    void parseProfile()
    {
        const QVector<ChronoSlot> s = Chrono::parseProfile(
            { "22:30=17", " 6:30 = 20.3", "08:00 18", "bad", "25:00=18", "09:00=40",
              "09:00=x", "08:00=19", "" }, "test");
        QCOMPARE(s.size(), 3);
        QCOMPARE(s[0].at, QTime(6, 30));
        QCOMPARE(s[0].temp, 20.5);              /* rounded to the step */
        QCOMPARE(s[1].at, QTime(8, 0));
        QCOMPARE(s[1].temp, 18.0);              /* first of the duplicates */
        QCOMPARE(s[2].at, QTime(22, 30));
        QCOMPARE(Chrono::toString(s), QString("06:30=20.5, 08:00=18.0, 22:30=17.0"));
    }

    void parseProfileMaxSlots()
    {
        QStringList e;
        for(int h = 10; h > 0; h--)
            e.append(QString("%1:00=18").arg(h));
        const QVector<ChronoSlot> s = Chrono::parseProfile(e, "test");
        QCOMPARE(s.size(), ChronoConfig::MAX_SLOTS);
        QCOMPARE(s.first().at, QTime(3, 0));    /* the first 8 written */
        QCOMPARE(s.last().at, QTime(10, 0));
    }

    void loadChrono()
    {
        QTemporaryDir dir;
        Configuration conf(TestUtil::writeIni(dir,
            "[CHRONO]\n"
            "holiday_days=fri, 6, sunday, never\n"
            "salotto\\enabled=true\n"
            "salotto\\weekday=06:30=20.5, 08:00=18, 17:00=20.5, 22:30=17\n"
            "salotto\\holiday=08:00=20.5\n"
            "studio\\enabled=true\n"
            "studio\\weekday=bad\n"
            "cucina\\weekday=07:00=19\n"));

        ChronoConfig c = conf.loadChrono("salotto");
        QVERIFY(c.enabled);
        QCOMPARE(c.holidayDays, quint8((1 << 5) | (1 << 6) | (1 << 7)));
        QCOMPARE(Chrono::toString(c.weekday), QString("06:30=20.5, 08:00=18.0, 17:00=20.5, 22:30=17.0"));
        QCOMPARE(Chrono::toString(c.holiday), QString("08:00=20.5"));   /* single value, no list */

        c = conf.loadChrono("studio");
        QVERIFY(!c.enabled);                    /* no valid weekday slot */

        c = conf.loadChrono("cucina");
        QVERIFY(!c.enabled);                    /* not enabled */
        QCOMPARE(c.weekday.size(), 1);

        QVERIFY(!conf.loadChrono("missing").enabled);
    }

    void loadChronoDefaultHolidays()
    {
        QTemporaryDir dir;
        Configuration conf(TestUtil::writeIni(dir, "[ZONES]\nlist=a\n"));
        QCOMPARE(conf.loadChrono("a").holidayDays, quint8((1 << 6) | (1 << 7)));

        QTemporaryDir dir2;
        Configuration none(TestUtil::writeIni(dir2, "[CHRONO]\nholiday_days=\n"));
        QCOMPARE(none.loadChrono("a").holidayDays, quint8(0));
    }
};

int runChronoTests(int argc, char *argv[])
{
    TstChrono t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_chrono.moc"
