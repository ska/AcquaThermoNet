#ifndef WEATHER_H
#define WEATHER_H

#include <QObject>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include "common.h"
#include "configuration.h"

Q_DECLARE_METATYPE(weather_t)

/*
 * Outdoor weather for the status bar, from api.met.no (locationforecast
 * compact) or wttr.in. For met.no the terms of service are followed:
 * identifying User-Agent, no request before Expires, If-Modified-Since.
 */
class Weather : public QObject
{
    Q_OBJECT
public:
    explicit Weather(const WeatherConfig &config, QObject *parent = nullptr);
    void startPoll();

    /* wttr.in answer with format "%l:+%t+%h+%w+%p+%P+%T" and &M (wind in m/s) */
    static bool parseWttr(const QString &answer, weather_t &out);

    /* api.met.no locationforecast/2.0/compact JSON, first time step */
    static bool parseMetNo(const QByteArray &json, const QString &name, weather_t &out);

    /* HTTP date "Tue, 29 Sep 2026 14:44:19 GMT" (RFC 7231), invalid if not parsed */
    static QDateTime parseHttpDate(const QByteArray &value);

signals:
    void newWeatherInfo( weather_t data);

private slots:
    void request();
    void replyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager   *m_manager;
    QTimer                  *m_timer;
    WeatherConfig           m_config;

    /* met.no cache control */
    QByteArray              m_lastModified;
    QDateTime               m_expires;

    QByteArray userAgent() const;
};

#endif // WEATHER_H
