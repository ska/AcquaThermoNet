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
 * A failed request (no DNS or network yet after a boot) is retried after
 * 30, 60, 120 s ... up to poll_s, instead of a whole poll_s.
 */
class Weather : public QObject
{
    Q_OBJECT
public:
    /* A request without an answer in this time is aborted (failed) */
    static const int REQUEST_TIMEOUT_MS = 30000;
    /* First retry after a failure, doubled at each further failure */
    static const int RETRY_FIRST_S = 30;

    explicit Weather(const WeatherConfig &config, QObject *parent = nullptr);
    void startPoll();

    /* wttr.in answer with format "%l:+%t+%h+%w+%p+%P+%T" and &M (wind in m/s) */
    static bool parseWttr(const QString &answer, weather_t &out);

    /* api.met.no locationforecast/2.0/compact JSON, first time step */
    static bool parseMetNo(const QByteArray &json, const QString &name, weather_t &out);

    /* HTTP date "Tue, 29 Sep 2026 14:44:19 GMT" (RFC 7231), invalid if not parsed */
    static QDateTime parseHttpDate(const QByteArray &value);

    /* Delay before the next request after `failures` failed ones in a row */
    static int retryDelayS(int failures, int pollS);

signals:
    void newWeatherInfo( weather_t data);

private slots:
    void request();
    void replyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager   *m_manager;
    QTimer                  *m_timer;           /* next request, single shot */
    WeatherConfig           m_config;
    int                     m_failures = 0;     /* failed requests in a row */

    /* met.no cache control */
    QByteArray              m_lastModified;
    QDateTime               m_expires;

    QByteArray userAgent() const;
    int pollS() const { return qMax(60, m_config.pollS); }
    void failed(const QString &reason);
};

#endif // WEATHER_H
