#include "weather.h"
#include "logging.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

/**
 * @brief Weather::Weather
 * @param config
 * @param parent
 */
Weather::Weather(const WeatherConfig &config, QObject *parent)
    : QObject{parent},
      m_config(config)
{
    m_manager = new QNetworkAccessManager(this);
    connect(m_manager, &QNetworkAccessManager::finished, this, &Weather::replyFinished);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Weather::request);
}

/**
 * @brief Weather::startPoll
 */
void Weather::startPoll()
{
    if(!m_config.enabled())
    {
        qCInfo(lcWeather) << "Weather disabled";
        return;
    }
    qCInfo(lcWeather) << "Weather from" << (m_config.provider == WeatherConfig::MetNo ? "api.met.no" : "wttr.in")
                      << "every" << pollS() << "s";
    if(m_config.provider == WeatherConfig::MetNo && m_config.contact.isEmpty())
        qCWarning(lcWeather) << "met.no asks for a contact in the User-Agent: set WEATHER/contact";

    request();
}

/**
 * @brief Weather::userAgent
 * met.no: application name and version, plus a contact if configured
 */
QByteArray Weather::userAgent() const
{
    QByteArray ua = SW_NAME "/" SW_VER;
    if(!m_config.contact.isEmpty())
        ua += " " + m_config.contact.toUtf8();
    return ua;
}

/**
 * @brief Weather::request
 */
void Weather::request()
{
    /* next poll; a failure brings it forward (failed()) */
    m_timer->start(pollS() * 1000);

    QNetworkRequest req;
    req.setRawHeader("User-Agent", userAgent());

    if(m_config.provider == WeatherConfig::MetNo)
    {
        /* Terms of service: no new request before the data expires */
        if(m_expires.isValid() && QDateTime::currentDateTimeUtc() < m_expires)
        {
            qCDebug(lcWeather) << "met.no data valid until" << m_expires.toString(Qt::ISODate) << ", no request";
            return;
        }

        QUrl url("https://api.met.no/weatherapi/locationforecast/2.0/compact");
        QUrlQuery q;
        /* max 4 decimals, as asked by met.no */
        q.addQueryItem("lat", QString::number(m_config.lat, 'f', 4));
        q.addQueryItem("lon", QString::number(m_config.lon, 'f', 4));
        q.addQueryItem("altitude", QString::number(m_config.altitude));
        url.setQuery(q);
        req.setUrl(url);
        if(!m_lastModified.isEmpty())
            req.setRawHeader("If-Modified-Since", m_lastModified);
    }
    else
    {
        req.setUrl(QUrl("https://wttr.in/" + m_config.location + "?format=\"%l:+%t+%h+%w+%p+%P+%T\"&M"));
    }
    QNetworkReply *reply = m_manager->get(req);
    /* Qt 5.13 has no transfer timeout: a stalled request would never end */
    QTimer::singleShot(REQUEST_TIMEOUT_MS, reply, [reply] {
        if(reply->isRunning())
        {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });
}

/**
 * @brief Weather::retryDelayS
 * 30, 60, 120 ... s, never more than pollS
 */
int Weather::retryDelayS(int failures, int pollS)
{
    if(failures < 1)
        return pollS;
    const int shift = qMin(failures - 1, 16);
    return int(qMin<qint64>(qint64(RETRY_FIRST_S) << shift, pollS));
}

/**
 * @brief Weather::failed
 * Logged and retried sooner than the poll
 */
void Weather::failed(const QString &reason)
{
    m_failures++;
    const int delayS = retryDelayS(m_failures, pollS());
    qCWarning(lcWeather).noquote() << QString("Weather request failed: %1, retry in %2 s").arg(reason).arg(delayS);
    m_timer->start(delayS * 1000);
}

/**
 * @brief Weather::parseWttr
 * e.g. "home: +12°C 70% ↓3m/s 0.0mm 1015hPa 14:05:31+0200" (with quotes)
 * @param answer
 * @param out
 * @return true if all the fields were found
 */
bool Weather::parseWttr(const QString &answer, weather_t &out)
{
    static const QRegularExpression re(
        R"re(^"(\w+):\s*([+\-]?\d+)\WC\W*(\d+)\W*([\d.]+)m/s\W*([\d.]+)mm\W*(\d+)hPa\W*([\d:]+)\+(\d+)")re");

    const QRegularExpressionMatch m = re.match(answer);
    if(!m.hasMatch())
        return false;

    out.comune  = m.captured(1);
    out.temp    = m.captured(2).toInt();
    out.hum     = m.captured(3).toInt();
    out.ws      = m.captured(4).toDouble();
    out.rain    = m.captured(5).toDouble();
    out.press   = m.captured(6).toInt();
    out.hour    = m.captured(7);
    out.utcdel  = m.captured(8);
    return true;
}

/**
 * @brief Weather::parseMetNo
 * properties.timeseries[0]: data.instant.details (air_temperature,
 * relative_humidity, air_pressure_at_sea_level, wind_speed) and
 * data.next_1_hours.details.precipitation_amount (optional)
 * @param json
 * @param name      shown as location
 * @param out
 * @return true if temperature, humidity and pressure were found
 */
bool Weather::parseMetNo(const QByteArray &json, const QString &name, weather_t &out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QJsonArray series = doc.object()["properties"].toObject()["timeseries"].toArray();
    if(series.isEmpty())
        return false;

    const QJsonObject step    = series.first().toObject();
    const QJsonObject data    = step["data"].toObject();
    const QJsonObject details = data["instant"].toObject()["details"].toObject();

    const QJsonValue temp  = details["air_temperature"];
    const QJsonValue hum   = details["relative_humidity"];
    const QJsonValue press = details["air_pressure_at_sea_level"];
    if(!temp.isDouble() || !hum.isDouble() || !press.isDouble())
        return false;

    out.comune  = name;
    out.temp    = temp.toDouble();
    out.hum     = qRound(hum.toDouble());
    out.press   = qRound(press.toDouble());
    out.ws      = details["wind_speed"].toDouble();
    out.rain    = data["next_1_hours"].toObject()["details"].toObject()["precipitation_amount"].toDouble();

    /* "2026-09-29T14:00:00Z": UTC */
    const QDateTime t = QDateTime::fromString(step["time"].toString(), Qt::ISODate);
    out.hour    = t.isValid() ? t.toUTC().toString("hh:mm:ss") : QString();
    out.utcdel  = "0000";
    return true;
}

/**
 * @brief Weather::parseHttpDate
 * @param value
 * @return UTC date time, invalid on error
 */
QDateTime Weather::parseHttpDate(const QByteArray &value)
{
    QDateTime t = QLocale::c().toDateTime(QString::fromLatin1(value).trimmed(), "ddd, dd MMM yyyy hh:mm:ss 'GMT'");
    t.setTimeSpec(Qt::UTC);
    return t;
}

/**
 * @brief Weather::replyFinished
 * @param reply
 */
void Weather::replyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(m_config.provider == WeatherConfig::MetNo)
    {
        const QByteArray expires = reply->rawHeader("Expires");
        if(!expires.isEmpty())
            m_expires = parseHttpDate(expires);
        const QByteArray lastModified = reply->rawHeader("Last-Modified");
        if(!lastModified.isEmpty())
            m_lastModified = lastModified;

        /* Not modified: the data shown is still current */
        if(status == 304)
        {
            m_failures = 0;
            return;
        }
    }

    if(reply->error() != QNetworkReply::NoError)
    {
        if(reply->property("timedOut").toBool())
            failed(QString("no answer in %1 s").arg(REQUEST_TIMEOUT_MS / 1000));
        else
            failed(QString("%1 \"%2\"").arg(status).arg(reply->errorString()));
        return;
    }

    const QByteArray answer = reply->readAll();
    weather_t info;
    const bool ok = m_config.provider == WeatherConfig::MetNo
            ? parseMetNo(answer, m_config.location, info)
            : parseWttr(QString::fromUtf8(answer), info);
    if(ok)
    {
        if(m_failures > 0)
            qCInfo(lcWeather).noquote() << QString("Weather OK again after %1 failed request(s): %2 %3 C")
                                           .arg(m_failures).arg(info.comune).arg(info.temp, 0, 'f', 1);
        m_failures = 0;
        qCDebug(lcWeather) << "Weather" << info.comune << info.temp << "C" << info.hum << "%" << info.press << "hPa";
        emit newWeatherInfo(info);
    }
    else
        failed("answer not understood: " + QString::fromUtf8(answer.left(80)));
}
