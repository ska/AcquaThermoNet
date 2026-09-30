#include <QTest>
#include <QSignalSpy>
#include "modbusframe.h"
#include "modbusframeprocessor.h"
#include "testutil.h"

class TstModbus : public QObject
{
    Q_OBJECT

private slots:
    void requestFrames()
    {
        ModBusFrame read;
        read.requestRegisters(1, 8);
        QCOMPARE(read.getBuffer().toHex(), QByteArray("01030001000815cc"));

        ModBusFrame write;
        write.setSingleRegisterU16(5, ModBusFrameProcessor::MB_CMD_RELAY_ON);
        QCOMPARE(write.getBuffer().toHex(), QByteArray("010600050100985b"));
    }

    void verifyCrc()
    {
        QVERIFY(ModBusFrame(QByteArray::fromHex("01030001000815cc")).verifyCrc());
        QVERIFY(!ModBusFrame(QByteArray::fromHex("01030001000915cc")).verifyCrc());
        /* bytes >= 0x80 (signed char once gave a wrong CRC) */
        QVERIFY(ModBusFrame(QByteArray::fromHex("010380ff10905196")).verifyCrc());
        /* too short */
        QVERIFY(!ModBusFrame(QByteArray::fromHex("0103")).verifyCrc());
        QVERIFY(!ModBusFrame(QByteArray()).verifyCrc());
    }

    void verifyCrcLongBuffer()
    {
        /* > 255 bytes of line noise: must return (quint8 loop counter never ended) */
        QByteArray noise(300, char(0x55));
        QVERIFY(!ModBusFrame(noise).verifyCrc());
        QByteArray valid = noise + TestUtil::crc(noise);
        QVERIFY(ModBusFrame(valid).verifyCrc());
    }

    void relayStatusParsed()
    {
        ModBusFrameProcessor fp;
        QSignalSpy spy(&fp, &ModBusFrameProcessor::relayStatus);

        fp.frameIncoming(TestUtil::relayAnswer(0x11));      /* relays 1 and 5 */
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<quint8>(), quint8(0x11));
    }

    void invalidAnswersIgnored()
    {
        ModBusFrameProcessor fp;
        QSignalSpy spy(&fp, &ModBusFrameProcessor::relayStatus);

        QByteArray bad = TestUtil::relayAnswer(0x01);
        bad[5] = char(1);                                   /* data changed, CRC not */
        fp.frameIncoming(bad);
        fp.frameIncoming(TestUtil::relayAnswer(0x01).left(15));
        fp.frameIncoming(QByteArray::fromHex("010600050100985b"));  /* write echo */
        fp.frameIncoming(QByteArray());
        QCOMPARE(spy.count(), 0);
    }

    void setRelayWritesThenReads()
    {
        ModBusFrameProcessor fp;
        QSignalSpy spy(&fp, &ModBusFrameProcessor::frameToSend);

        fp.requestSetRelay(5, true);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toByteArray().toHex(), QByteArray("010600050100985b"));
        QCOMPARE(spy.at(1).at(0).toByteArray().toHex(), QByteArray("01030001000815cc"));
    }

    void offlineAfterMissedPolls()
    {
        ModBusFrameProcessor fp;
        QSignalSpy online(&fp, &ModBusFrameProcessor::onlineChanged);
        fp.startPoll(1);

        /* no answers: offline after MB_MAX_MISSED polls */
        QTRY_COMPARE_WITH_TIMEOUT(online.count(), 1, 6000);
        QCOMPARE(online.at(0).at(0).toBool(), false);
        QVERIFY(!fp.isOnline());

        fp.frameIncoming(TestUtil::relayAnswer(0));
        QCOMPARE(online.count(), 2);
        QCOMPARE(online.at(1).at(0).toBool(), true);
    }
};

int runModbusTests(int argc, char *argv[])
{
    TstModbus t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_modbus.moc"
