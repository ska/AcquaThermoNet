#include "relaylog.h"
#include "logging.h"
#include "monoclock.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

/**
 * @brief RelayLog::RelayLog
 * @param basePath
 * @param keepMonths
 */
RelayLog::RelayLog(const QString &basePath, int keepMonths) :
    m_basePath(basePath),
    m_keepMonths(qMax(1, keepMonths))
{
    QDir().mkpath(QFileInfo(m_basePath).absolutePath());
    qCInfo(lcRegulation).noquote() << "Relay log" << fileFor(QDate::currentDate()) << "keep" << m_keepMonths << "months";
}

/**
 * @brief RelayLog::fileFor
 * @param month
 * @return <dir>/<name>-YYYY-MM.<ext>
 */
QString RelayLog::fileFor(const QDate &month) const
{
    const QFileInfo fi(m_basePath);
    const QString suffix = fi.suffix().isEmpty() ? QString("csv") : fi.suffix();
    return fi.absoluteDir().filePath(QString("%1-%2.%3").arg(fi.completeBaseName(), month.toString("yyyy-MM"), suffix));
}

/**
 * @brief RelayLog::openFor
 * Monthly file, header when new
 */
bool RelayLog::openFor(const QDate &today)
{
    if(m_file.isOpen() && m_month.year() == today.year() && m_month.month() == today.month())
        return true;

    m_file.close();
    m_month = today;
    m_file.setFileName(fileFor(today));
    const bool isNew = !m_file.exists() || m_file.size() == 0;
    if(!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        qCWarning(lcRegulation).noquote() << "Cannot open relay log" << m_file.fileName() << ":" << m_file.errorString();
        return false;
    }
    if(isNew)
    {
        m_file.write("time,epoch,relay,zones,state,reason,on_s\n");
        removeOld(today);
    }
    return true;
}

/**
 * @brief RelayLog::removeOld
 * Monthly files older than keepMonths
 */
void RelayLog::removeOld(const QDate &today)
{
    const QFileInfo fi(m_basePath);
    const QRegularExpression re("^" + QRegularExpression::escape(fi.completeBaseName()) + "-(\\d{4})-(\\d{2})\\.");
    const QDate oldest = QDate(today.year(), today.month(), 1).addMonths(-(m_keepMonths - 1));

    for(const QFileInfo &f : fi.absoluteDir().entryInfoList(QDir::Files))
    {
        const QRegularExpressionMatch m = re.match(f.fileName());
        if(!m.hasMatch())
            continue;
        const QDate month(m.captured(1).toInt(), m.captured(2).toInt(), 1);
        if(month.isValid() && month < oldest)
        {
            qCInfo(lcRegulation).noquote() << "Remove old relay log" << f.fileName();
            QFile::remove(f.absoluteFilePath());
        }
    }
}

/**
 * @brief RelayLog::record
 * @param relay
 * @param zones     zone name(s) using the relay, "+" separated
 * @param on
 * @param reason    regulation, frost, exercise, no_sensor, startup, shutdown
 */
void RelayLog::record(int relay, const QString &zones, bool on, const QString &reason)
{
    const bool known = m_state.contains(relay);
    RelayState &st = m_state[relay];
    if(known && st.on == on)
        return;

    const qint64 nowMs = MonoClock::nowMs();
    QString onS;
    if(!on && known && st.on && st.onSinceMs > 0)
        onS = QString::number((nowMs - st.onSinceMs + 500) / 1000);
    st.on = on;
    st.onSinceMs = on ? nowMs : 0;

    const QDateTime now = QDateTime::currentDateTime();
    if(!openFor(now.date()))
        return;

    const QString line = QString("%1,%2,%3,%4,%5,%6,%7\n")
            /* with the UTC offset: unambiguous across DST changes */
            .arg(now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate))
            .arg(now.toSecsSinceEpoch())
            .arg(relay)
            .arg(zones)
            .arg(on ? "ON" : "OFF")
            .arg(reason)
            .arg(onS);
    m_file.write(line.toUtf8());
    m_file.flush();
}

/**
 * @brief RelayLog::onSeconds
 * Files of every month touched by the period
 */
QMap<QString, qint64> RelayLog::onSeconds(qint64 fromEpoch, qint64 toEpoch) const
{
    QStringList files;
    QDate month = QDateTime::fromSecsSinceEpoch(fromEpoch).date();
    month = QDate(month.year(), month.month(), 1);
    const QDate last = QDateTime::fromSecsSinceEpoch(toEpoch).date();
    while(month <= last)
    {
        files.append(fileFor(month));
        month = month.addMonths(1);
    }
    return onSeconds(files, fromEpoch, toEpoch, QDateTime::currentSecsSinceEpoch());
}

/**
 * @brief RelayLog::onSeconds
 * @param files     monthly CSV files, in time order (missing ones skipped)
 * @param fromEpoch
 * @param toEpoch
 * @param nowEpoch  end of a relay still ON
 * @return zones -> seconds ON within [fromEpoch, toEpoch]
 */
QMap<QString, qint64> RelayLog::onSeconds(const QStringList &files, qint64 fromEpoch, qint64 toEpoch, qint64 nowEpoch)
{
    struct Open { qint64 start; QString zones; };
    QMap<QString, qint64> out;
    QMap<int, Open> open;

    auto add = [&](const QString &zones, qint64 start, qint64 end) {
        const qint64 s = qMax(start, fromEpoch);
        const qint64 e = qMin(end, toEpoch);
        if(e > s)
            out[zones] += e - s;
    };

    for(const QString &path : files)
    {
        QFile f(path);
        if(!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        f.readLine();                                   /* header */
        while(!f.atEnd())
        {
            const QStringList c = QString::fromUtf8(f.readLine()).trimmed().split(',');
            if(c.size() < 7)
                continue;
            bool okEpoch, okRelay;
            const qint64 epoch = c[1].toLongLong(&okEpoch);
            const int relay = c[2].toInt(&okRelay);
            if(!okEpoch || !okRelay)
                continue;

            if(c[4] == "ON")
            {
                if(!open.contains(relay))
                    open.insert(relay, Open{ epoch, c[3] });
                continue;
            }
            if(!open.contains(relay))
                continue;
            const Open o = open.take(relay);
            bool okOn;
            const qint64 onS = c[6].toLongLong(&okOn);
            add(o.zones, okOn ? epoch - onS : o.start, epoch);
        }
    }
    for(const Open &o : open)
        add(o.zones, o.start, nowEpoch);
    return out;
}
