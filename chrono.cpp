#include "chrono.h"
#include "climatezones.h"
#include "logging.h"
#include <algorithm>

/* Local date and time; a time skipped by the DST change starts an hour later */
static QDateTime localAt(const QDate &date, const QTime &time)
{
    QDateTime dt(date, time);
    if(!dt.isValid())
        dt = QDateTime(date, time.addSecs(3600));
    return dt;
}

bool Chrono::isHoliday(const QDate &date, const ChronoConfig &config)
{
    return config.holidayDays & (1 << date.dayOfWeek());
}

const QVector<ChronoSlot> &Chrono::profile(const QDate &date, const ChronoConfig &config)
{
    if(isHoliday(date, config) && !config.holiday.isEmpty())
        return config.holiday;
    return config.weekday;
}

ChronoPoint Chrono::current(const QDateTime &now, const ChronoConfig &config)
{
    ChronoPoint p;
    if(!config.enabled || config.weekday.isEmpty())
        return p;

    const QDate today = now.date();
    const QVector<ChronoSlot> &day = profile(today, config);
    for(int i = day.size() - 1; i >= 0; i--)
    {
        if(day[i].at <= now.time())
        {
            p.temp  = day[i].temp;
            p.start = localAt(today, day[i].at);
            return p;
        }
    }

    /* Before the first slot: every profile has slots (empty holiday = weekday) */
    const QDate yesterday = today.addDays(-1);
    const ChronoSlot &last = profile(yesterday, config).last();
    p.temp  = last.temp;
    p.start = localAt(yesterday, last.at);
    return p;
}

ChronoPoint Chrono::next(const QDateTime &now, const ChronoConfig &config)
{
    ChronoPoint p;
    if(!config.enabled || config.weekday.isEmpty())
        return p;

    const QDate today = now.date();
    for(const ChronoSlot &s : profile(today, config))
    {
        if(s.at > now.time())
        {
            p.temp  = s.temp;
            p.start = localAt(today, s.at);
            return p;
        }
    }

    const QDate tomorrow = today.addDays(1);
    const ChronoSlot &first = profile(tomorrow, config).first();
    p.temp  = first.temp;
    p.start = localAt(tomorrow, first.at);
    return p;
}

QVector<double> Chrono::dayTemps(const QDate &date, const ChronoConfig &config, int stepMin)
{
    QVector<double> out;
    if(!config.enabled || config.weekday.isEmpty() || stepMin <= 0)
        return out;
    /* Wall clock times, no QDateTime: a time skipped by DST is still an interval */
    const QVector<ChronoSlot> &day = profile(date, config);
    double temp = profile(date.addDays(-1), config).last().temp;
    int next = 0;
    for(int m = 0; m < 24 * 60; m += stepMin)
    {
        const QTime t(m / 60, m % 60);
        while(next < day.size() && day[next].at <= t)
            temp = day[next++].temp;
        out.append(temp);
    }
    return out;
}

QString Chrono::profileName(const QDate &date, const ChronoConfig &config)
{
    return &profile(date, config) == &config.holiday ? "holiday" : "weekday";
}

QVector<ChronoSlot> Chrono::parseProfile(const QStringList &entries, const QString &where)
{
    QVector<ChronoSlot> out;
    for(const QString &entry : entries)
    {
        const QString e = entry.trimmed();
        if(e.isEmpty())
            continue;

        /* "06:30=20.5" or "06:30 20.5" */
        int sep = e.indexOf('=');
        if(sep < 0)
            sep = e.indexOf(' ');
        ChronoSlot s;
        bool ok = false;
        if(sep > 0)
        {
            s.at   = QTime::fromString(e.left(sep).trimmed(), "H:mm");
            s.temp = e.mid(sep + 1).trimmed().toDouble(&ok);
        }
        if(!ok || !s.at.isValid() || s.temp < TEMP_MIN || s.temp > TEMP_MAX)
        {
            qCWarning(lcConfig).noquote() << "Invalid" << where << "slot" << e << ", skipped";
            continue;
        }
        s.temp = qRound(s.temp / TEMP_STEP) * TEMP_STEP;

        if(std::any_of(out.cbegin(), out.cend(), [&s](const ChronoSlot &o) { return o.at == s.at; }))
        {
            qCWarning(lcConfig).noquote() << "Duplicated" << where << "slot" << e << ", skipped";
            continue;
        }
        if(out.size() >= ChronoConfig::MAX_SLOTS)
        {
            qCWarning(lcConfig).noquote() << "More than" << ChronoConfig::MAX_SLOTS << where
                                          << "slots, skipped:" << e;
            continue;
        }
        out.append(s);
    }

    std::sort(out.begin(), out.end(), [](const ChronoSlot &a, const ChronoSlot &b) { return a.at < b.at; });
    return out;
}

static const int DAY_MIN = 24 * 60;

static int minutes(const QTime &t)
{
    return t.hour() * 60 + t.minute();
}

static QTime fromMinutes(int m)
{
    return QTime(m / 60, m % 60);
}

/**
 * @brief Chrono::stepTime
 * Slot k to the next (dir > 0) or previous point of the 15-minute grid,
 * strictly between its neighbours: the order of the slots never changes
 */
bool Chrono::stepTime(QVector<ChronoSlot> &list, int k, int dir)
{
    if(k < 0 || k >= list.size() || dir == 0)
        return false;
    const int m = minutes(list[k].at);
    const int target = dir > 0 ? (m / EDIT_STEP_MIN + 1) * EDIT_STEP_MIN
                               : ((m + EDIT_STEP_MIN - 1) / EDIT_STEP_MIN - 1) * EDIT_STEP_MIN;
    const int prev = k > 0 ? minutes(list[k-1].at) : -1;
    const int next = k + 1 < list.size() ? minutes(list[k+1].at) : DAY_MIN;
    if(target < 0 || target <= prev || target >= next)
        return false;
    list[k].at = fromMinutes(target);
    return true;
}

bool Chrono::stepTemp(QVector<ChronoSlot> &list, int k, int dir)
{
    if(k < 0 || k >= list.size() || dir == 0)
        return false;
    const double t = list[k].temp + (dir > 0 ? TEMP_STEP : -TEMP_STEP);
    if(t < TEMP_MIN || t > TEMP_MAX)
        return false;
    list[k].temp = t;
    return true;
}

/**
 * @brief Chrono::addSlot
 * One hour after the last slot, else in the middle of the longest gap
 * (at least 30 minutes), with the setpoint of the slot before it.
 * Empty profile: 08:00, 20.0 °C.
 */
bool Chrono::addSlot(QVector<ChronoSlot> &list)
{
    if(list.size() >= ChronoConfig::MAX_SLOTS)
        return false;
    if(list.isEmpty())
    {
        list.append({ QTime(8, 0), 20.0 });
        return true;
    }

    const int last = minutes(list.last().at);
    const int after = (last + 60) / EDIT_STEP_MIN * EDIT_STEP_MIN;
    if(after < DAY_MIN)
    {
        list.append({ fromMinutes(after), list.last().temp });
        return true;
    }

    int best = -1, bestGap = 0;
    for(int i = 0; i + 1 < list.size(); i++)
    {
        const int gap = minutes(list[i+1].at) - minutes(list[i].at);
        if(gap > bestGap)
        {
            bestGap = gap;
            best = i;
        }
    }
    if(best < 0 || bestGap < 2 * EDIT_STEP_MIN)
        return false;
    const int start = minutes(list[best].at);
    int at = (start + bestGap / 2) / EDIT_STEP_MIN * EDIT_STEP_MIN;
    if(at <= start)
        at += EDIT_STEP_MIN;
    list.insert(best + 1, { fromMinutes(at), list[best].temp });
    return true;
}

QString Chrono::toString(const QVector<ChronoSlot> &profile)
{
    QStringList parts;
    for(const ChronoSlot &s : profile)
        parts.append(s.at.toString("HH:mm") + "=" + QString::number(s.temp, 'f', 1));
    return parts.join(", ");
}
