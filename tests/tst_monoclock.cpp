#include <QTest>
#include "monoclock.h"

class TstMonoClock : public QObject
{
    Q_OBJECT

private slots:
    void monotonic()
    {
        const qint64 a = MonoClock::nowMs();
        QVERIFY(a > 0);                 /* 0 means "never" */
        QTest::qWait(20);
        const qint64 b = MonoClock::nowMs();
        QVERIFY(b >= a + 20);
    }

    void advance()
    {
        const qint64 a = MonoClock::nowMs();
        MonoClock::advanceForTest(60 * 1000);
        QVERIFY(MonoClock::nowMs() - a >= 60 * 1000);
    }
};

int runMonoClockTests(int argc, char *argv[])
{
    TstMonoClock t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_monoclock.moc"
