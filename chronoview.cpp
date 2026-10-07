#include "chronoview.h"
#include "zonemodel.h"
#include <QPainter>
#include <QMouseEvent>
#include <QLocale>
#include <QTimer>
#include <cmath>
#include <algorithm>

/* Colors of qss/default.qss: the page is painted, not styled */
static const QColor CARD("#2A2A2A");
static const QColor GRID("#444444");
static const QColor TEXT("#D3D3D3");
static const QColor DIM("#AAAAAA");
static const QColor RED("#AD1625");

static const qreal GAP      = 4;        /* between rows and tabs */
static const qreal RADIUS   = 6;

/* Bar color: blue (cold) .. amber .. red (comfort); past bars of today dimmed */
QColor ChronoView::barColor(double t, bool dim)
{
    struct Stop { double t; int r, g, b; };
    static const Stop stops[] = { { 15, 47, 111, 176 }, { 18, 196, 140, 40 }, { 21, 173, 22, 37 } };
    const int n = sizeof(stops) / sizeof(stops[0]);
    QColor c(stops[n-1].r, stops[n-1].g, stops[n-1].b);
    if(t <= stops[0].t)
        c = QColor(stops[0].r, stops[0].g, stops[0].b);
    for(int i = 0; i < n - 1; i++)
    {
        const Stop &a = stops[i], &b = stops[i+1];
        if(t > a.t && t <= b.t)
        {
            const double k = (t - a.t) / (b.t - a.t);
            c = QColor(int(a.r + (b.r - a.r) * k), int(a.g + (b.g - a.g) * k), int(a.b + (b.b - a.b) * k));
        }
    }
    if(dim)
        c.setAlpha(100);
    return c;
}

static QString zoneTitle(const QString &name)
{
    QString t = name;
    if(!t.isEmpty())
        t[0] = t[0].toUpper();
    return t;
}

static QString degrees(double t)
{
    return QString("%1°").arg(t, 0, 'f', 1);
}

/* Minutes since midnight, local wall clock */
static int nowMinutes()
{
    const QTime t = QTime::currentTime();
    return t.hour() * 60 + t.minute();
}

ChronoView::ChronoView(ZoneModel *zones, QWidget *parent) :
    QWidget(parent),
    m_zones(zones)
{
    /* the now line moves; setpoints of disabled zones change */
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { if(isVisible()) update(); });
    timer->start(30 * 1000);
    connect(m_zones, &ZoneModel::zoneChanged, this, [this] { if(isVisible()) update(); });
}

QDate ChronoView::date() const
{
    return QDate::currentDate().addDays(m_day);
}

QString ChronoView::dayText() const
{
    QString text = QLocale::c().toString(date(), "ddd dd/MM");
    if(m_day == 0)
        text += " · today";
    else if(m_day == 1)
        text += " · tomorrow";
    return text;
}

void ChronoView::showOverview()
{
    m_zone = ALL_ZONES;
    update();
    emit viewChanged();
}

void ChronoView::showZone(int zone)
{
    if(zone < 0 || zone >= m_zones->count())
        return;
    m_zone = zone;
    update();
    emit viewChanged();
}

void ChronoView::stepDay(int steps)
{
    m_day = qBound(0, m_day + steps, MAX_DAY);
    update();
    emit viewChanged();
}

void ChronoView::mouseReleaseEvent(QMouseEvent *event)
{
    for(const auto &tap : m_taps)
    {
        if(tap.first.contains(event->pos()))
        {
            if(tap.second == ALL_ZONES)
                showOverview();
            else
                showZone(tap.second);
            return;
        }
    }
}

void ChronoView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    m_taps.clear();
    if(m_zones->count() == 0)
        return;
    if(m_zone == ALL_ZONES)
        paintOverview(p);
    else
        paintZone(p);
}

static QFont sized(const QFont &base, qreal pt, bool bold = false)
{
    QFont f(base);
    f.setPointSizeF(pt);
    f.setBold(bold);
    return f;
}

/**
 * @brief ChronoView::paintNowLine
 * Dashed line at the current time across chart; label: time under it
 */
void ChronoView::paintNowLine(QPainter &p, const QRectF &chart, bool label)
{
    if(!isToday())
        return;
    const int now = nowMinutes();
    const qreal x = chart.left() + chart.width() * now / (24 * 60);
    QPen pen(Qt::white, 1.5, Qt::DashLine);
    p.setPen(pen);
    p.drawLine(QPointF(x, chart.top()), QPointF(x, chart.bottom()));
    if(label)
    {
        const QRectF tag(x - 20, chart.bottom() + 1, 40, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawRoundedRect(tag, 3, 3);
        p.setFont(sized(font(), 8, true));
        p.setPen(QColor("#222222"));
        p.drawText(tag, Qt::AlignCenter, QTime(now / 60, now % 60).toString("HH:mm"));
    }
}

/**
 * @brief ChronoView::paintOverview
 * One row per zone: name and profile, the day's bars (height and color =
 * setpoint, same scale for all the zones), setpoint now and next change
 */
void ChronoView::paintOverview(QPainter &p)
{
    const int n = m_zones->count();
    const QRectF r = rect();
    const qreal axisH = 18;
    const qreal rowH = qMin<qreal>(90, (r.height() - axisH - GAP * (n - 1)) / n);
    const qreal x0 = 112, rightW = 150;
    const QDate day = date();
    const QDateTime now = QDateTime::currentDateTime();

    QVector<QVector<double>> temps(n);
    double lo = 1e9, hi = -1e9;
    for(int i = 0; i < n; i++)
    {
        temps[i] = Chrono::dayTemps(day, m_zones->zone(i).chrono, STEP_MIN);
        for(double t : temps[i])
        {
            lo = qMin(lo, t);
            hi = qMax(hi, t);
        }
    }
    if(lo > hi)
    {
        lo = 15;
        hi = 21;
    }
    /* the lowest setpoint still a visible bar */
    lo = qMin(lo - 2, hi - 4);

    const QRectF chart(x0, r.top(), r.width() - x0 - rightW, n * rowH + (n - 1) * GAP);
    const int bars = 24 * 60 / STEP_MIN;
    const qreal bw = chart.width() / bars;
    const int nowBar = isToday() ? nowMinutes() / STEP_MIN : -1;

    for(int i = 0; i < n; i++)
    {
        const ZoneData &z = m_zones->zone(i);
        const QRectF row(r.left(), r.top() + i * (rowH + GAP), r.width(), rowH);
        m_taps.append(qMakePair(row.toRect(), i));
        p.setPen(Qt::NoPen);
        p.setBrush(CARD);
        p.drawRoundedRect(row, RADIUS, RADIUS);

        const QRectF left(row.left() + 10, row.top() + 4, x0 - 16, rowH / 2 - 4);
        /* a long name gets a smaller font before being elided */
        p.setFont(sized(font(), 13, true));
        if(p.fontMetrics().horizontalAdvance(zoneTitle(z.name)) > left.width())
            p.setFont(sized(font(), 10.5, true));
        p.setPen(Qt::white);
        p.drawText(left, Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(zoneTitle(z.name), Qt::ElideRight, int(left.width())));
        const QRectF below(left.left(), row.top() + rowH / 2, left.width(), rowH / 2 - 4);
        const QRectF right(chart.right() + 10, row.top() + 4, rightW - 16, rowH - 8);

        if(temps[i].isEmpty())
        {
            p.setFont(sized(font(), 10));
            p.setPen(DIM);
            p.drawText(QRectF(chart.left(), row.top(), chart.width(), rowH), Qt::AlignCenter, "chrono off");
            p.setFont(sized(font(), 12, true));
            p.setPen(TEXT);
            p.drawText(right, Qt::AlignLeft | Qt::AlignVCenter, "Set " + degrees(z.setPoint));
            continue;
        }

        p.setFont(sized(font(), 9));
        p.setPen(DIM);
        p.drawText(below, Qt::AlignLeft | Qt::AlignVCenter, Chrono::profileName(day, z.chrono));

        const qreal base = row.bottom() - 6, hMax = rowH - 12;
        for(int k = 0; k < temps[i].size(); k++)
        {
            const double t = temps[i][k];
            const qreal h = qMax<qreal>(3, hMax * (t - lo) / (hi - lo));
            p.fillRect(QRectF(chart.left() + k * bw, base - h, bw - 1, h), barColor(t, k < nowBar));
        }

        if(isToday())
        {
            const ChronoPoint next = Chrono::next(now, z.chrono);
            p.setFont(sized(font(), 16, true));
            p.setPen(Qt::white);
            p.drawText(QRectF(right.left(), right.top(), right.width(), right.height() / 2 + 4),
                       Qt::AlignLeft | Qt::AlignBottom, degrees(Chrono::current(now, z.chrono).temp));
            p.setFont(sized(font(), 10));
            p.setPen(TEXT);
            p.drawText(QRectF(right.left(), right.center().y() + 6, right.width(), right.height() / 2 - 6),
                       Qt::AlignLeft | Qt::AlignTop,
                       QString("→ %1  %2").arg(next.start.toString("HH:mm"), degrees(next.temp)));
        }
        else
        {
            const auto mm = std::minmax_element(temps[i].cbegin(), temps[i].cend());
            p.setFont(sized(font(), 12, true));
            p.setPen(TEXT);
            p.drawText(right, Qt::AlignLeft | Qt::AlignVCenter,
                       QString("%1 – %2").arg(*mm.first, 0, 'f', 1).arg(degrees(*mm.second)));
        }
    }

    paintNowLine(p, chart, false);

    p.setFont(sized(font(), 8));
    p.setPen(DIM);
    for(int h = 0; h <= 24; h += 3)
    {
        const qreal x = chart.left() + chart.width() * h / 24;
        p.drawText(QRectF(x - 20, chart.bottom() + 2, 40, axisH - 2), Qt::AlignCenter,
                   QString("%1").arg(h, 2, 10, QChar('0')));
    }
}

/**
 * @brief ChronoView::paintZone
 * Zone tabs on the left ("All zones" back to the overview), the selected
 * zone's day with scales in °C and hours, slot values on the bars
 */
void ChronoView::paintZone(QPainter &p)
{
    const int n = m_zones->count();
    const QRectF r = rect();
    const qreal tabW = 130, allH = 40;
    const qreal tabH = qMin<qreal>(74, (r.height() - allH - GAP * n) / n);
    const QDate day = date();
    const QDateTime now = QDateTime::currentDateTime();

    /* Tabs */
    auto tab = [&](const QRectF &rect, bool selected) {
        p.setPen(Qt::NoPen);
        p.setBrush(selected ? RED : CARD);
        p.drawRoundedRect(rect, RADIUS, RADIUS);
    };
    const QRectF all(r.left(), r.top(), tabW, allH);
    tab(all, false);
    m_taps.append(qMakePair(all.toRect(), int(ALL_ZONES)));
    p.setFont(sized(font(), 11, true));
    p.setPen(TEXT);
    p.drawText(all, Qt::AlignCenter, "‹ All zones");

    for(int i = 0; i < n; i++)
    {
        const ZoneData &z = m_zones->zone(i);
        const QRectF t(r.left(), all.bottom() + GAP + i * (tabH + GAP), tabW, tabH);
        tab(t, i == m_zone);
        m_taps.append(qMakePair(t.toRect(), i));
        p.setFont(sized(font(), 12, true));
        p.setPen(Qt::white);
        p.drawText(QRectF(t.left() + 4, t.top(), t.width() - 8, t.height() / 2 + 2), Qt::AlignHCenter | Qt::AlignBottom,
                   p.fontMetrics().elidedText(zoneTitle(z.name), Qt::ElideRight, int(t.width() - 8)));
        QString sub = "chrono off";
        if(z.chrono.enabled)
            sub = isToday() ? degrees(Chrono::current(now, z.chrono).temp) : Chrono::profileName(day, z.chrono);
        p.setFont(sized(font(), 10));
        p.setPen(i == m_zone ? Qt::white : DIM);
        p.drawText(QRectF(t.left(), t.center().y() + 4, t.width(), t.height() / 2 - 4), Qt::AlignHCenter | Qt::AlignTop, sub);
    }

    /* Chart card */
    const ZoneData &z = m_zones->zone(m_zone);
    const QRectF card(r.left() + tabW + 6, r.top(), r.width() - tabW - 6, r.height());
    p.setPen(Qt::NoPen);
    p.setBrush(CARD);
    p.drawRoundedRect(card, RADIUS, RADIUS);

    const QVector<double> temps = Chrono::dayTemps(day, z.chrono, STEP_MIN);
    const QRectF header(card.left() + 14, card.top() + 6, card.width() - 28, 26);
    p.setFont(sized(font(), 13, true));
    p.setPen(Qt::white);
    QString title = zoneTitle(z.name);
    if(!temps.isEmpty())
        title += " · " + Chrono::profileName(day, z.chrono) + " profile";
    p.drawText(header, Qt::AlignLeft | Qt::AlignVCenter, title);

    if(temps.isEmpty())
    {
        p.setFont(sized(font(), 12));
        p.setPen(DIM);
        p.drawText(card, Qt::AlignCenter, "Chrono off for this zone\nSet " + degrees(z.setPoint));
        return;
    }

    if(isToday())
    {
        const ChronoPoint next = Chrono::next(now, z.chrono);
        p.setFont(sized(font(), 10));
        p.setPen(TEXT);
        p.drawText(header, Qt::AlignRight | Qt::AlignVCenter,
                   QString("now %1   next %2 → %3").arg(degrees(Chrono::current(now, z.chrono).temp),
                                                      next.start.toString("HH:mm"), degrees(next.temp)));
    }

    /* Scales: even degrees, at least 6 °C, room above the highest bar for its label */
    const auto mm = std::minmax_element(temps.cbegin(), temps.cend());
    int lo = int(std::floor((*mm.first - 2) / 2)) * 2;
    int hi = int(std::ceil((*mm.second + 1) / 2)) * 2;
    if(hi - lo < 6)
        lo = hi - 6;
    const QRectF chart(card.left() + 52, header.bottom() + 22, card.width() - 52 - 18, card.height() - header.height() - 6 - 22 - 34);
    auto y = [&](double t) { return chart.bottom() - chart.height() * (t - lo) / (hi - lo); };

    p.setFont(sized(font(), 9));
    for(int t = lo; t <= hi; t += 2)
    {
        const qreal yy = std::round(y(t)) + 0.5;
        p.setPen(QPen(GRID, 1));
        p.drawLine(QPointF(chart.left(), yy), QPointF(chart.right(), yy));
        p.setPen(DIM);
        p.drawText(QRectF(card.left() + 4, yy - 9, chart.left() - card.left() - 10, 18), Qt::AlignRight | Qt::AlignVCenter,
                   QString("%1°").arg(t));
    }

    const qreal bw = chart.width() / temps.size();
    const int nowBar = isToday() ? nowMinutes() / STEP_MIN : -1;
    for(int k = 0; k < temps.size(); k++)
        p.fillRect(QRectF(chart.left() + k * bw, y(temps[k]), bw - 1, chart.bottom() - y(temps[k])), barColor(temps[k], k < nowBar));

    /* slot values at their start */
    p.setFont(sized(font(), 9, true));
    p.setPen(Qt::white);
    for(const ChronoSlot &s : Chrono::profile(day, z.chrono))
    {
        const qreal x = chart.left() + chart.width() * (s.at.hour() * 60 + s.at.minute()) / (24 * 60);
        p.drawText(QRectF(x + 1, y(s.temp) - 18, 60, 16), Qt::AlignLeft | Qt::AlignBottom, QString::number(s.temp, 'f', 1));
    }

    paintNowLine(p, chart, true);

    p.setFont(sized(font(), 8));
    p.setPen(DIM);
    for(int h = 0; h <= 24; h += 3)
    {
        const qreal x = chart.left() + chart.width() * h / 24;
        p.drawText(QRectF(x - 24, chart.bottom() + 18, 48, 14), Qt::AlignCenter, QString("%1:00").arg(h, 2, 10, QChar('0')));
    }
}
