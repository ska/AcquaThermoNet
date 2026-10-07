#include "zonecard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <QVariant>

/* Icon height on the card */
static const int ICON_SIZE = 36;

/**
 * @brief ZoneCard::ZoneCard
 * @param parent
 */
ZoneCard::ZoneCard(QWidget *parent) : QFrame(parent)
{
    setObjectName("zoneCard");

    m_flameOn   = QPixmap(":/images/flame-red.png").scaledToHeight(ICON_SIZE, Qt::SmoothTransformation);
    m_flameOff  = QPixmap(":/images/flame-black.png").scaledToHeight(ICON_SIZE, Qt::SmoothTransformation);
    m_relayOn   = QPixmap(":/images/heat-red.png").scaledToHeight(ICON_SIZE, Qt::SmoothTransformation);
    m_relayOff  = QPixmap(":/images/heat-black.png").scaledToHeight(ICON_SIZE, Qt::SmoothTransformation);

    auto label = [this](const char *name) {
        QLabel *l = new QLabel(this);
        l->setObjectName(name);
        l->setAlignment(Qt::AlignCenter);
        return l;
    };

    m_name      = label("zoneName");
    m_temp      = label("zoneTemp");
    m_info      = label("zoneInfo");
    m_setPoint  = label("zoneSetPoint");
    m_status    = label("zoneStatus");
    /* Status text never widens the card: all cards keep the same width */
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_chrono    = label("zoneChrono");
    /* as the status: never widens the card */
    m_chrono->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_chrono->setTextFormat(Qt::RichText);
    m_heatIcon  = label("zoneHeatIcon");
    m_relayIcon = label("zoneRelayIcon");

    m_minus = new QPushButton("-", this);
    m_plus  = new QPushButton("+", this);
    m_minus->setObjectName("zoneMinus");
    m_plus->setObjectName("zonePlus");
    m_minus->setFocusPolicy(Qt::NoFocus);
    m_plus->setFocusPolicy(Qt::NoFocus);
    connect(m_minus, &QPushButton::clicked, this, &ZoneCard::minusClicked);
    connect(m_plus,  &QPushButton::clicked, this, &ZoneCard::plusClicked);

    /* Buttons full width under the setpoint: card is ~150px wide with 5 zones */
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_minus);
    buttonLayout->addWidget(m_plus);

    QHBoxLayout *iconLayout = new QHBoxLayout();
    iconLayout->addWidget(m_heatIcon);
    iconLayout->addWidget(m_relayIcon);

    QFrame *separator = new QFrame(this);
    separator->setObjectName("zoneSeparator");
    separator->setFrameShape(QFrame::HLine);

    /* Top: state (name, icons, temperature, status). Bottom: control */
    QVBoxLayout *main = new QVBoxLayout(this);
    main->setContentsMargins(6, 6, 6, 6);
    main->addWidget(m_name);
    main->addLayout(iconLayout);
    main->addStretch(1);
    main->addWidget(m_temp);
    main->addWidget(m_info);
    main->addStretch(1);
    main->addWidget(m_status);
    main->addWidget(separator);
    main->addWidget(m_setPoint);
    main->addWidget(m_chrono);
    main->addLayout(buttonLayout);

    setHeat(false);
    setRelay(-1);
    setStatus("", false);
    setChrono("", false);
}

void ZoneCard::setName(const QString &text)
{
    m_name->setText(text);
}

void ZoneCard::setTemperature(const QString &text)
{
    m_temp->setText(text);
}

void ZoneCard::setInfo(const QString &text)
{
    m_info->setText(text);
}

void ZoneCard::setSetPoint(const QString &text)
{
    m_setPoint->setText(text);
}

/**
 * @brief ZoneCard::setStatus
 * @param text
 * @param alarm     highlight status line and card border
 */
void ZoneCard::setStatus(const QString &text, bool alarm)
{
    m_status->setText(text);
    setAlarmProperty(m_status, alarm);
    setAlarmProperty(this, alarm);
}

/**
 * @brief ZoneCard::setChrono
 * Clock icon and text; empty: blank line, so that the cards stay aligned
 */
void ZoneCard::setChrono(const QString &text, bool manual)
{
    m_chrono->setText(text.isEmpty() ? QString("&nbsp;")
                      : QString("<img src=\":/images/clock.png\" width=\"12\" height=\"12\">&nbsp;") + text.toHtmlEscaped());
    if(m_chrono->property("manual").toBool() != manual || !m_chrono->property("manual").isValid())
    {
        m_chrono->setProperty("manual", manual);
        m_chrono->style()->unpolish(m_chrono);
        m_chrono->style()->polish(m_chrono);
    }
}

void ZoneCard::setHeat(bool on)
{
    m_heatIcon->setPixmap(on ? m_flameOn : m_flameOff);
}

void ZoneCard::setRelay(int state)
{
    m_relayIcon->setPixmap(state == 1 ? m_relayOn : m_relayOff);
    /* Unknown state (board offline, no relay): dimmed */
    m_relayIcon->setEnabled(state >= 0);
}

/**
 * @brief ZoneCard::setAlarmProperty
 * Style sheet selects [alarm="true"]: repolish to apply the change
 */
void ZoneCard::setAlarmProperty(QWidget *w, bool alarm)
{
    if(w->property("alarm").toBool() == alarm && w->property("alarm").isValid())
        return;
    w->setProperty("alarm", alarm);
    w->style()->unpolish(w);
    w->style()->polish(w);
}
