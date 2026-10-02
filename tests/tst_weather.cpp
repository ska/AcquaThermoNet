#include <QTest>
#include "weather.h"

class TstWeather : public QObject
{
    Q_OBJECT

private slots:
    void parse()
    {
        weather_t w;
        QVERIFY(Weather::parseWttr(QString::fromUtf8("\"home: +12°C 70% ↓3m/s 0.0mm 1015hPa 14:05:31+0200\""), w));
        QCOMPARE(w.comune, QString("home"));
        QCOMPARE(w.temp, 12.0);
        QCOMPARE(w.hum, 70);
        QCOMPARE(w.ws, 3.0);
        QCOMPARE(w.rain, 0.0);
        QCOMPARE(w.press, 1015);
        QCOMPARE(w.hour, QString("14:05:31"));
        QCOMPARE(w.utcdel, QString("0200"));
    }

    void negativeAndZero()
    {
        weather_t w;
        QVERIFY(Weather::parseWttr(QString::fromUtf8("\"home: -7°C 90% ↑1.5m/s 0.2mm 1022hPa 06:00:00+0100\""), w));
        QCOMPARE(w.temp, -7.0);         /* was quint8: -7 became 249 */
        QCOMPARE(w.ws, 1.5);

        QVERIFY(Weather::parseWttr(QString::fromUtf8("\"home: 0°C 80% ↓2m/s 0.0mm 1010hPa 07:00:00+0100\""), w));
        QCOMPARE(w.temp, 0.0);
    }

    void invalid()
    {
        weather_t w;
        QVERIFY(!Weather::parseWttr("Unknown location", w));
        QVERIFY(!Weather::parseWttr("", w));
    }

    void parseMetNo()
    {
        /* Trimmed api.met.no locationforecast/2.0/compact answer */
        const QByteArray json = R"({
          "type": "Feature",
          "properties": {
            "meta": { "updated_at": "2026-09-29T13:12:04Z" },
            "timeseries": [
              { "time": "2026-09-29T14:00:00Z",
                "data": {
                  "instant": { "details": {
                    "air_pressure_at_sea_level": 1014.6, "air_temperature": -2.6,
                    "cloud_area_fraction": 90.2, "relative_humidity": 71.4,
                    "wind_from_direction": 180.3, "wind_speed": 3.2 } },
                  "next_1_hours": { "summary": { "symbol_code": "rain" },
                                    "details": { "precipitation_amount": 0.4 } } } },
              { "time": "2026-09-29T15:00:00Z",
                "data": { "instant": { "details": { "air_temperature": 5.0 } } } }
            ]
          }
        })";

        weather_t w;
        QVERIFY(Weather::parseMetNo(json, "home", w));
        QCOMPARE(w.comune, QString("home"));
        QCOMPARE(w.temp, -2.6);         /* not rounded */
        QCOMPARE(w.hum, 71);
        QCOMPARE(w.press, 1015);
        QCOMPARE(w.ws, 3.2);
        QCOMPARE(w.rain, 0.4);
        QCOMPARE(w.hour, QString("14:00:00"));
    }

    void parseMetNoWithoutRain()
    {
        const QByteArray json = R"({"properties":{"timeseries":[{"time":"2026-09-29T14:00:00Z",
            "data":{"instant":{"details":{"air_temperature":12.0,"relative_humidity":50.0,
            "air_pressure_at_sea_level":1010.0}}}}]}})";
        weather_t w;
        QVERIFY(Weather::parseMetNo(json, "x", w));
        QCOMPARE(w.temp, 12.0);
        QCOMPARE(w.rain, 0.0);
        QCOMPARE(w.ws, 0.0);
    }

    void httpDate()
    {
        const QDateTime t = Weather::parseHttpDate("Tue, 29 Sep 2026 14:44:19 GMT");
        QVERIFY(t.isValid());
        QCOMPARE(t, QDateTime(QDate(2026, 9, 29), QTime(14, 44, 19), Qt::UTC));
        QVERIFY(!Weather::parseHttpDate("tomorrow").isValid());
        QVERIFY(!Weather::parseHttpDate("").isValid());
    }

    void retryDelay()
    {
        QCOMPARE(Weather::retryDelayS(0, 600), 600);
        QCOMPARE(Weather::retryDelayS(1, 600), 30);
        QCOMPARE(Weather::retryDelayS(2, 600), 60);
        QCOMPARE(Weather::retryDelayS(3, 600), 120);
        QCOMPARE(Weather::retryDelayS(5, 600), 480);
        QCOMPARE(Weather::retryDelayS(6, 600), 600);        /* capped at poll_s */
        QCOMPARE(Weather::retryDelayS(1000, 600), 600);
        QCOMPARE(Weather::retryDelayS(1, 60), 30);
        QCOMPARE(Weather::retryDelayS(3, 60), 60);
    }

    void parseMetNoInvalid_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty")          << QByteArray();
        QTest::newRow("not json")       << QByteArray("<html>403 Forbidden</html>");
        QTest::newRow("no timeseries")  << QByteArray(R"({"properties":{}})");
        QTest::newRow("empty series")   << QByteArray(R"({"properties":{"timeseries":[]}})");
        QTest::newRow("no temperature") << QByteArray(R"({"properties":{"timeseries":[{"data":{"instant":{"details":{"relative_humidity":50.0,"air_pressure_at_sea_level":1010.0}}}}]}})");
    }

    void parseMetNoInvalid()
    {
        QFETCH(QByteArray, json);
        weather_t w;
        QVERIFY(!Weather::parseMetNo(json, "x", w));
    }
};

int runWeatherTests(int argc, char *argv[])
{
    TstWeather t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_weather.moc"
