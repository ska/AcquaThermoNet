#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "logging.h"

class TstLogging : public QObject
{
    Q_OBJECT

private slots:
    void rotation()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("log/test.log");
        Logging::install();
        Logging::enableFile(path, 16 * 1024, 2);

        /* ~60 KB of info: rotated, only 2 old files kept */
        const QString filler(100, 'x');
        for(int i = 0; i < 600; i++)
            qCInfo(lcApp) << i << filler;
        qCDebug(lcApp) << "debug is not written";

        QVERIFY(QFile::exists(path));
        QVERIFY(QFile::exists(path + ".1"));
        QVERIFY(QFile::exists(path + ".2"));
        QVERIFY(!QFile::exists(path + ".3"));
        QVERIFY(QFileInfo(path).size() <= 16 * 1024 + 200);

        QFile f(path);
        f.open(QIODevice::ReadOnly);
        const QByteArray last = f.readAll();
        QVERIFY(last.contains("atn.app: 599"));
        QVERIFY(!last.contains("debug is not written"));
    }
};

int runLoggingTests(int argc, char *argv[])
{
    TstLogging t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_logging.moc"
