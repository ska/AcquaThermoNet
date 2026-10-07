#include "chronoeditor.h"
#include "chronoview.h"
#include "zonemodel.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <cmath>
#include <algorithm>

/* Bars of the profile being edited, with its own °C scale */
class ChronoPreview : public QWidget
{
public:
    explicit ChronoPreview(QWidget *parent) : QWidget(parent) { setObjectName("chronoPreview"); }

    /* asWeekday: an empty holiday profile, the weekday one is shown dimmed */
    void setProfile(const QVector<ChronoSlot> &list, bool asWeekday)
    {
        m_list = list;
        m_asWeekday = asWeekday;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#2A2A2A"));
        p.drawRoundedRect(rect(), 6, 6);

        QFont f = font();
        f.setPointSizeF(8);
        p.setFont(f);
        const QRectF chart(rect().adjusted(34, 8, -12, -18));
        if(m_list.isEmpty())
        {
            p.setPen(QColor("#AAAAAA"));
            p.drawText(chart, Qt::AlignCenter, "No slot: add one");
            return;
        }

        ChronoConfig c;
        c.enabled = true;
        c.weekday = m_list;
        const QVector<double> temps = Chrono::dayTemps(QDate::currentDate(), c, ChronoView::STEP_MIN);
        const auto mm = std::minmax_element(temps.cbegin(), temps.cend());
        int lo = int(std::floor((*mm.first - 2) / 2)) * 2;
        int hi = int(std::ceil(*mm.second / 2)) * 2;
        if(hi - lo < 6)
            lo = hi - 6;
        auto y = [&](double t) { return chart.bottom() - chart.height() * (t - lo) / (hi - lo); };

        for(int t = lo; t <= hi; t += 2)
        {
            const qreal yy = std::round(y(t)) + 0.5;
            p.setPen(QColor("#444444"));
            p.drawLine(QPointF(chart.left(), yy), QPointF(chart.right(), yy));
            p.setPen(QColor("#AAAAAA"));
            p.drawText(QRectF(0, yy - 8, chart.left() - 4, 16), Qt::AlignRight | Qt::AlignVCenter, QString("%1°").arg(t));
        }
        const qreal bw = chart.width() / temps.size();
        for(int k = 0; k < temps.size(); k++)
            p.fillRect(QRectF(chart.left() + k * bw, y(temps[k]), bw - 1, chart.bottom() - y(temps[k])),
                       ChronoView::barColor(temps[k], m_asWeekday));
        p.setPen(QColor("#AAAAAA"));
        for(int h = 0; h <= 24; h += 3)
        {
            const qreal x = chart.left() + chart.width() * h / 24;
            p.drawText(QRectF(x - 16, chart.bottom() + 2, 32, 14), Qt::AlignCenter, QString("%1").arg(h, 2, 10, QChar('0')));
        }
        if(m_asWeekday)
        {
            f.setPointSizeF(10);
            f.setBold(true);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(chart, Qt::AlignHCenter | Qt::AlignTop, "Holiday profile empty: the weekday one is used");
        }
    }

private:
    QVector<ChronoSlot> m_list;
    bool m_asWeekday = false;
};

ChronoEditor::ChronoEditor(ZoneModel *zones, QWidget *parent) :
    QWidget(parent),
    m_zones(zones)
{
    auto button = [this](const QString &text, const char *name, QWidget *parent) {
        QPushButton *b = new QPushButton(text, parent);
        b->setObjectName(name);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    /* Header: zone, profile, copy, reset, on/off */
    QFrame *header = new QFrame(this);
    header->setObjectName("chronoHeader");
    QHBoxLayout *hl = new QHBoxLayout(header);
    hl->setContentsMargins(10, 4, 6, 4);
    hl->setSpacing(6);
    m_name = new QLabel(header);
    m_name->setObjectName("chronoZone");
    m_weekdayButton = button("Weekday", "profileButton", header);
    m_holidayButton = button("Holiday", "profileButton", header);
    m_copyButton    = button("", "chronoAction", header);
    m_resetButton   = button("Reset", "chronoAction", header);
    m_enableButton  = button("", "chronoEnable", header);
    for(QPushButton *b : { m_weekdayButton, m_holidayButton, m_enableButton })
        b->setCheckable(true);
    hl->addWidget(m_name, 1);
    hl->addWidget(m_weekdayButton);
    hl->addWidget(m_holidayButton);
    hl->addWidget(m_copyButton);
    hl->addWidget(m_resetButton);
    hl->addWidget(m_enableButton);

    connect(m_weekdayButton, &QPushButton::clicked, this, [this] { showProfile(false); });
    connect(m_holidayButton, &QPushButton::clicked, this, [this] { showProfile(true); });
    connect(m_copyButton, &QPushButton::clicked, this, [this] {
        if(m_holiday)
            m_cfg.holiday.clear();          /* "Use weekday" */
        else
            m_cfg.holiday = m_cfg.weekday;  /* "Copy to holiday" */
        refresh();
    });
    connect(m_resetButton, &QPushButton::clicked, this, [this] { emit resetRequested(m_zone); });
    connect(m_enableButton, &QPushButton::clicked, this, [this](bool on) {
        m_cfg.enabled = on;
        refresh();
    });

    m_preview = new ChronoPreview(this);
    m_preview->setFixedHeight(104);

    /* Slots: two columns of four, the 8 slots at most */
    QGridLayout *grid = new QGridLayout();
    grid->setSpacing(6);
    for(int k = 0; k < ChronoConfig::MAX_SLOTS; k++)
    {
        Row row;
        row.cell = new QWidget(this);
        row.stack = new QStackedLayout(row.cell);

        QFrame *frame = new QFrame(row.cell);
        frame->setObjectName("slotRow");
        QHBoxLayout *rl = new QHBoxLayout(frame);
        rl->setContentsMargins(6, 4, 6, 4);
        rl->setSpacing(4);
        QPushButton *timeMinus = button("-", "slotButton", frame);
        QPushButton *timePlus  = button("+", "slotButton", frame);
        QPushButton *tempMinus = button("-", "slotButton", frame);
        QPushButton *tempPlus  = button("+", "slotButton", frame);
        row.remove = button("×", "slotRemove", frame);
        row.time = new QLabel(frame);
        row.temp = new QLabel(frame);
        row.time->setObjectName("slotValue");
        row.temp->setObjectName("slotValue");
        row.time->setAlignment(Qt::AlignCenter);
        row.temp->setAlignment(Qt::AlignCenter);
        rl->addWidget(timeMinus);
        rl->addWidget(row.time, 1);
        rl->addWidget(timePlus);
        rl->addSpacing(8);
        rl->addWidget(tempMinus);
        rl->addWidget(row.temp, 1);
        rl->addWidget(tempPlus);
        rl->addSpacing(8);
        rl->addWidget(row.remove);
        /* keep pressed: repeat */
        for(QPushButton *b : { timeMinus, timePlus, tempMinus, tempPlus })
        {
            b->setAutoRepeat(true);
            b->setAutoRepeatDelay(400);
            b->setAutoRepeatInterval(120);
        }
        connect(timeMinus, &QPushButton::clicked, this, [this, k] { if(Chrono::stepTime(profile(), k, -1)) refresh(); });
        connect(timePlus,  &QPushButton::clicked, this, [this, k] { if(Chrono::stepTime(profile(), k, +1)) refresh(); });
        connect(tempMinus, &QPushButton::clicked, this, [this, k] { if(Chrono::stepTemp(profile(), k, -1)) refresh(); });
        connect(tempPlus,  &QPushButton::clicked, this, [this, k] { if(Chrono::stepTemp(profile(), k, +1)) refresh(); });
        connect(row.remove, &QPushButton::clicked, this, [this, k] {
            if(k < profile().size())
                profile().remove(k);
            refresh();
        });

        QPushButton *add = button("+ Slot", "slotAdd", row.cell);
        connect(add, &QPushButton::clicked, this, [this] { if(Chrono::addSlot(profile())) refresh(); });

        row.stack->addWidget(frame);
        row.stack->addWidget(add);
        row.stack->addWidget(new QWidget(row.cell));    /* empty: the grid keeps its rows */
        grid->addWidget(row.cell, k % 4, k / 4);
        m_rows.append(row);
    }

    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(6);
    main->addWidget(header);
    main->addWidget(m_preview);
    main->addLayout(grid, 1);
}

void ChronoEditor::edit(int zone, bool holiday)
{
    if(zone < 0 || zone >= m_zones->count())
        return;
    m_zone = zone;
    m_cfg = m_zones->zone(zone).chrono;
    /* a zone never configured: start from one slot */
    if(m_cfg.weekday.isEmpty())
        Chrono::addSlot(m_cfg.weekday);
    m_holiday = holiday;
    refresh();
}

void ChronoEditor::showProfile(bool holiday)
{
    m_holiday = holiday;
    refresh();
}

void ChronoEditor::refresh()
{
    if(m_zone < 0)
        return;

    QString name = m_zones->zone(m_zone).name;
    if(!name.isEmpty())
        name[0] = name[0].toUpper();
    m_name->setText(name);

    m_weekdayButton->setChecked(!m_holiday);
    m_holidayButton->setChecked(m_holiday);
    m_copyButton->setText(m_holiday ? "Use weekday" : "Copy to holiday");
    m_copyButton->setEnabled(m_holiday ? !m_cfg.holiday.isEmpty() : !m_cfg.weekday.isEmpty());
    m_resetButton->setEnabled(m_zones->zone(m_zone).chrono.edited);

    if(m_cfg.weekday.isEmpty())
        m_cfg.enabled = false;
    m_enableButton->setEnabled(!m_cfg.weekday.isEmpty());
    m_enableButton->setChecked(m_cfg.enabled);
    m_enableButton->setText(m_cfg.enabled ? "Chrono ON" : "Chrono OFF");

    const QVector<ChronoSlot> &list = profile();
    const bool asWeekday = m_holiday && list.isEmpty();
    m_preview->setProfile(asWeekday ? m_cfg.weekday : list, asWeekday);

    for(int k = 0; k < m_rows.size(); k++)
    {
        Row &row = m_rows[k];
        if(k > list.size())
            row.stack->setCurrentIndex(2);
        else if(k < list.size())
        {
            row.stack->setCurrentIndex(0);
            row.time->setText(list[k].at.toString("HH:mm"));
            row.temp->setText(QString("%1°").arg(list[k].temp, 0, 'f', 1));
            /* the weekday profile keeps one slot at least */
            row.remove->setEnabled(m_holiday || list.size() > 1);
        }
        else
            row.stack->setCurrentIndex(1);
    }
}
