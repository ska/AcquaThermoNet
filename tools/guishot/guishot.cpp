/*
 * GUI screenshot: the real MainWindow with one zone per card state
 * (heating with pending switch, battery low, sensor lost with frost
 * protection, relay fault, waiting for data). Usage: guishot SETTING_INI OUTPUT_PNG
 * The ini needs at least 5 zones; MQTT is pointed to an unused port.
 */
#include <QApplication>
#include "monoclock.h"
#include <QTimer>
#include "configuration.h"
#include "zonemodel.h"
#include "mqtt.h"
#include "mainwindow.h"

static ZoneData reading(double t, int hum, int batt)
{
    ZoneData d;
    d.temp = t;
    d.humidity = hum;
    d.battery = batt;
    return d;
}

int main(int argc, char *argv[])
{
    if(argc < 3)
    {
        fprintf(stderr, "usage: %s SETTING_INI OUTPUT_PNG\n", argv[0]);
        return 2;
    }

    QApplication a(argc, argv);
    Configuration conf(argv[1]);
    ZoneModel zones(&conf);
    if(zones.count() < 5)
    {
        fprintf(stderr, "the ini needs at least 5 zones\n");
        return 2;
    }

    mqtt_brk_t broker;
    broker.address = "127.0.0.1";
    broker.port = 1;
    Mqtt mq(broker, "GUISHOT", &zones);
    MainWindow w(&zones, &mq);
    w.resize(800, 480);

    const qint64 now = MonoClock::nowMs();
    zones.setSensorData(0, reading(19.4, 48, 85));
    zones.setHeat(0, true);
    zones.setRelayState(0, 1);
    zones.setPendingSwitch(0, 0, now + 130 * 1000);

    zones.setSensorData(1, reading(21.2, 52, 12));
    zones.setRelayState(1, 0);

    zones.setSensorData(2, reading(18.0, 60, 70));
    zones.setSensorLost(2, true);
    zones.setFrostProtection(2, true);
    zones.setHeat(2, true);

    zones.setSensorData(3, reading(17.1, 45, 90));
    zones.setHeat(3, true);
    zones.setRelayState(3, 0);
    zones.setRelayFault(3, true);
    /* zone 4: no data yet */

    w.onSerialPortChanged(true);
    w.onModbusOnlineChanged(true);
    weather_t wi;
    wi.comune = "home";
    wi.temp = -2.6;
    wi.hum = 70;
    wi.press = 1015;
    w.weatherInfoIsChanged(wi);

    w.show();
    QTimer::singleShot(800, [&] {
        /* Sample network identity: never the one of the build machine */
        NetInfo::Info net;
        net.hostname  = "hmi-boiler";
        net.interface = "eth0";
        net.ip        = "192.168.1.20";
        net.mac       = "00:30:D8:12:34:56";
        /* MQTT state changes would refresh it with the real interfaces */
        QObject::disconnect(&mq, nullptr, &w, nullptr);
        w.setNetworkInfo(net);
        /* let the layout adapt to the new texts before the capture */
        QTimer::singleShot(200, [&] {
            w.grab().save(argv[2]);
            a.quit();
        });
    });
    return a.exec();
}
