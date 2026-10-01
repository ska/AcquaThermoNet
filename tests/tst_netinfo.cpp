#include <QTest>
#include "netinfo.h"

class TstNetInfo : public QObject
{
    Q_OBJECT

    static NetInfo::Interface itf(const QString &name, const QString &mac, bool active, bool loopback,
                                  const QStringList &addresses)
    {
        NetInfo::Interface i;
        i.name = name;
        i.mac = mac;
        i.active = active;
        i.loopback = loopback;
        for(const QString &a : addresses)
            i.addresses.append(QHostAddress(a));
        return i;
    }

    static QList<NetInfo::Interface> system()
    {
        return {
            itf("lo",    "00:00:00:00:00:00", true,  true,  { "127.0.0.1", "::1" }),
            itf("eth0",  "00:30:d8:12:34:56", true,  false, { "fe80::230:d8ff:fe12:3456", "192.168.1.50" }),
            itf("wlan0", "00:30:d8:ab:cd:ef", true,  false, { "10.0.0.7" }),
        };
    }

private slots:
    void brokerInterfaceWins()
    {
        const NetInfo::Info i = NetInfo::choose(system(), QHostAddress("10.0.0.7"), "hmi");
        QCOMPARE(i.interface, QString("wlan0"));
        QCOMPARE(i.ip, QString("10.0.0.7"));
        QCOMPARE(i.mac, QString("00:30:D8:AB:CD:EF"));
        QCOMPARE(i.hostname, QString("hmi"));
    }

    void mappedIpv6Address()
    {
        const NetInfo::Info i = NetInfo::choose(system(), QHostAddress("::ffff:192.168.1.50"), "hmi");
        QCOMPARE(i.interface, QString("eth0"));
        QCOMPARE(i.ip, QString("192.168.1.50"));
    }

    void noBrokerFirstActive()
    {
        /* loopback skipped, IPv4 taken after the link local IPv6 */
        const NetInfo::Info i = NetInfo::choose(system(), QHostAddress(), "hmi");
        QCOMPARE(i.interface, QString("eth0"));
        QCOMPARE(i.ip, QString("192.168.1.50"));
        QCOMPARE(i.mac, QString("00:30:D8:12:34:56"));
    }

    void unknownBrokerAddressFallsBack()
    {
        const NetInfo::Info i = NetInfo::choose(system(), QHostAddress("172.16.0.1"), "hmi");
        QCOMPARE(i.interface, QString("eth0"));
    }

    void inactiveAndIpv6OnlySkipped()
    {
        const QList<NetInfo::Interface> l = {
            itf("eth0",  "00:30:d8:12:34:56", false, false, { "192.168.1.50" }),
            itf("eth1",  "00:30:d8:12:34:57", true,  false, { "fe80::1" }),
            itf("wlan0", "00:30:d8:ab:cd:ef", true,  false, { "10.0.0.7" }),
        };
        QCOMPARE(NetInfo::choose(l, QHostAddress(), "hmi").interface, QString("wlan0"));
    }

    void noNetwork()
    {
        const QList<NetInfo::Interface> l = { itf("lo", "", true, true, { "127.0.0.1" }) };
        const NetInfo::Info i = NetInfo::choose(l, QHostAddress(), "hmi");
        QVERIFY(i.interface.isEmpty());
        QCOMPARE(NetInfo::fields(i), QStringList({ "Host: hmi", "Network: none" }));
    }

    void fields()
    {
        QCOMPARE(NetInfo::fields(NetInfo::choose(system(), QHostAddress(), "hmi")),
                 QStringList({ "Host: hmi", "IP: 192.168.1.50", "MAC: 00:30:D8:12:34:56" }));

        /* tun/ppp: no hardware address */
        const QList<NetInfo::Interface> l = { itf("tun0", "", true, false, { "10.8.0.2" }) };
        QCOMPARE(NetInfo::fields(NetInfo::choose(l, QHostAddress(), "hmi")), QStringList({ "Host: hmi", "IP: 10.8.0.2", "MAC: --" }));
    }
};

int runNetInfoTests(int argc, char *argv[])
{
    TstNetInfo t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_netinfo.moc"
