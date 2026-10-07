#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "monoclock.h"
#include "netinfo.h"
#include <QFile>
#include <QTimer>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

MainWindow::MainWindow(ZoneModel *zones, Mqtt *mq, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_zones(zones)
    , m_mqtt(mq)
{
    /*
     * styleSheet
     */
    QFile file(":/qss/default.qss");
    file.open(QFile::ReadOnly);
    qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
    ui->setupUi(this);
    ui->statusbar->setSizeGripEnabled(false);

    /* Two rows at the bottom, status and network identity, one grid cell
     * per field: a column is as wide as its longest field, so the '|' of
     * the two rows line up. Labels, not showMessage(): a temporary message
     * would hide the widgets. */
    QWidget *bottom = new QWidget(this);
    QGridLayout *grid = new QGridLayout(bottom);
    grid->setContentsMargins(4, 0, 10, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(0);
    for(int row = 0; row < 2; row++)
    {
        for(int f = 0; f < BAR_FIELDS; f++)
        {
            if(f > 0)
            {
                QLabel *sep = new QLabel("|", bottom);
                sep->setObjectName(row == 0 ? "barStatus" : "barNetwork");
                grid->addWidget(sep, row, 2*f - 1);
                m_barSeparators[row].append(sep);
            }
            QLabel *field = new QLabel(bottom);
            field->setObjectName(row == 0 ? "barStatus" : "barNetwork");
            grid->addWidget(field, row, 2*f);
            m_barFields[row].append(field);
        }
    }
    /* last field (weather) clipped rather than widening the window */
    for(int row = 0; row < 2; row++)
        m_barFields[row].last()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    grid->setColumnStretch(2*BAR_FIELDS - 2, 1);
    /* wall clock at the right end: date on the status row, time below */
    for(int row = 0; row < 2; row++)
    {
        m_barClock[row] = new QLabel(bottom);
        m_barClock[row]->setObjectName(row == 0 ? "barStatus" : "barNetwork");
        m_barClock[row]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(m_barClock[row], row, 2*BAR_FIELDS - 1);
    }
    ui->statusbar->addWidget(bottom, 1);

    m_mqttStatus    = "MQTT: disconnected";
    m_serialOpen    = false;
    m_modbusOnline  = true;

    connect( mq,        &Mqtt::clientStateChanged,  this, &MainWindow::clientStateChanged );
    connect( m_zones,   &ZoneModel::zoneChanged,    this, &MainWindow::zoneDataIsChanged );
    connect( m_zones,   &ZoneModel::houseModeChanged, this, &MainWindow::refreshAllZones );

    /* One card per configured zone */
    for(int zone=0; zone<m_zones->count(); zone++)
    {
        ZoneCard *card = new ZoneCard(this);
        connect(card, &ZoneCard::minusClicked, this, [this, zone] { m_zones->stepSetPoint(zone, -1); });
        connect(card, &ZoneCard::plusClicked,  this, [this, zone] { m_zones->stepSetPoint(zone, +1); });
        ui->zonesLayout->addWidget(card);
        m_cards.append(card);
    }

    /* Second page: chrono thermostat, pages chosen in a bar at the top */
    m_chrono = new ChronoView(m_zones, this);
    QWidget *chronoPage = new QWidget(this);
    QVBoxLayout *chronoLayout = new QVBoxLayout(chronoPage);
    chronoLayout->setContentsMargins(6, 6, 6, 6);
    chronoLayout->addWidget(m_chrono);
    ui->pages->addWidget(chronoPage);
    m_editor = new ChronoEditor(m_zones, this);
    QWidget *editorPage = new QWidget(this);
    QVBoxLayout *editorLayout = new QVBoxLayout(editorPage);
    editorLayout->setContentsMargins(6, 6, 6, 6);
    editorLayout->addWidget(m_editor);
    ui->pages->addWidget(editorPage);
    connect(m_editor, &ChronoEditor::resetRequested, this, [this](int zone) {
        m_zones->resetChrono(zone);
        showChrono(zone);
    });
    ui->mainLayout->insertWidget(0, createTopBar());
    connect(m_chrono, &ChronoView::viewChanged, this, &MainWindow::updateTopBar);
    showZones();
    refreshAllZones();

    /* "updated N min" / "switch in N s" change with time, not only with data */
    QTimer *refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &MainWindow::refreshAllZones);
    refreshTimer->start(5*1000);

    /* IP/interface: when the broker connection changes (it tells which
     * interface is used) and periodically (DHCP, cable, Wi-Fi) */
    QTimer *networkTimer = new QTimer(this);
    connect(networkTimer, &QTimer::timeout, this, &MainWindow::updateNetworkInfo);
    networkTimer->start(30*1000);
    updateNetworkInfo();

    /* every second, so that the minute changes on time */
    QTimer *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    clockTimer->start(1000);
    updateClock();

    updateStatusbar();
}

MainWindow::~MainWindow()
{
    delete ui;
}

/**
 * @brief MainWindow::createTopBar
 * Zones / Chrono page buttons; on the chrono page, day back and forward
 */
QWidget *MainWindow::createTopBar()
{
    QWidget *bar = new QWidget(this);
    QHBoxLayout *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(6, 6, 6, 0);
    layout->setSpacing(6);

    auto button = [bar](const QString &text, const char *name) {
        QPushButton *b = new QPushButton(text, bar);
        b->setObjectName(name);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    m_zonesButton  = button("Zones", "pageButton");
    m_chronoButton = button("Chrono", "pageButton");
    m_dayPrev      = button("‹", "dayButton");
    m_dayNext      = button("›", "dayButton");
    m_editButton   = button("Edit", "barButton");
    m_cancelButton = button("Cancel", "barButton");
    m_saveButton   = button("Save", "barButton");
    m_saveButton->setProperty("primary", true);
    m_dayLabel     = new QLabel(bar);
    m_dayLabel->setObjectName("dayLabel");
    m_dayLabel->setAlignment(Qt::AlignCenter);
    m_dayLabel->setMinimumWidth(170);
    for(QPushButton *b : { m_zonesButton, m_chronoButton })
        b->setCheckable(true);

    layout->addWidget(m_zonesButton);
    layout->addWidget(m_chronoButton);
    layout->addStretch(1);
    layout->addWidget(m_dayPrev);
    layout->addWidget(m_dayLabel);
    layout->addWidget(m_dayNext);
    layout->addWidget(m_editButton);
    layout->addWidget(m_cancelButton);
    layout->addWidget(m_saveButton);

    connect(m_zonesButton,  &QPushButton::clicked, this, &MainWindow::showZones);
    /* Chrono again on the chrono page: back to the overview */
    connect(m_chronoButton, &QPushButton::clicked, this, [this] { showChrono(); });
    connect(m_dayPrev, &QPushButton::clicked, m_chrono, [this] { m_chrono->stepDay(-1); });
    connect(m_dayNext, &QPushButton::clicked, m_chrono, [this] { m_chrono->stepDay(+1); });
    connect(m_editButton, &QPushButton::clicked, this, [this] { editChrono(m_chrono->zone()); });
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { showChrono(m_editor->zone()); });
    connect(m_saveButton, &QPushButton::clicked, this, [this] {
        m_zones->setChrono(m_editor->zone(), m_editor->config());
        showChrono(m_editor->zone());
    });
    return bar;
}

void MainWindow::showZones()
{
    ui->pages->setCurrentIndex(PageZones);
    updateTopBar();
}

void MainWindow::showChrono(int zone)
{
    if(zone < 0)
        m_chrono->showOverview();
    else
        m_chrono->showZone(zone);
    ui->pages->setCurrentIndex(PageChrono);
    updateTopBar();
}

void MainWindow::editChrono(int zone)
{
    if(zone < 0 || zone >= m_zones->count())
        return;
    /* the profile of the day shown on the chrono page */
    const ChronoConfig &c = m_zones->zone(zone).chrono;
    m_editor->edit(zone, Chrono::isHoliday(m_chrono->date(), c) && !c.holiday.isEmpty());
    ui->pages->setCurrentIndex(PageEditor);
    updateTopBar();
}

void MainWindow::updateTopBar()
{
    const int page = ui->pages->currentIndex();
    const bool chrono = page == PageChrono;
    const bool editor = page == PageEditor;
    m_zonesButton->setChecked(page == PageZones);
    m_chronoButton->setChecked(page != PageZones);
    /* the editor ends with Save or Cancel only */
    m_zonesButton->setEnabled(!editor);
    m_chronoButton->setEnabled(!editor);
    m_dayPrev->setVisible(chrono);
    m_dayNext->setVisible(chrono);
    m_dayLabel->setVisible(chrono);
    m_dayPrev->setEnabled(m_chrono->day() > 0);
    m_dayNext->setEnabled(m_chrono->day() < ChronoView::MAX_DAY);
    m_dayLabel->setText(m_chrono->dayText());
    m_editButton->setVisible(chrono && m_chrono->zone() >= 0);
    m_cancelButton->setVisible(editor);
    m_saveButton->setVisible(editor);
}

/**
 * @brief MainWindow::ago
 * @param ms    past MonoClock time
 * @return "now", "N min", "N h", "N d" (short: one line on a ~150px card)
 */
QString MainWindow::ago(qint64 ms)
{
    const qint64 s = qMax<qint64>(0, MonoClock::nowMs() - ms) / 1000;
    if(s < 60)
        return "now";
    if(s < 3600)
        return QString("%1 min").arg(s / 60);
    if(s < 86400)
        return QString("%1 h").arg(s / 3600);
    return QString("%1 d").arg(s / 86400);
}

/**
 * @brief MainWindow::in
 * @param ms    future MonoClock time
 * @return "N s" under a minute, then "N min" (rounded up)
 */
QString MainWindow::in(qint64 ms)
{
    const qint64 s = (qMax<qint64>(0, ms - MonoClock::nowMs()) + 999) / 1000;
    if(s < 60)
        return QString("%1 s").arg(s);
    return QString("%1 min").arg((s + 59) / 60);
}

void MainWindow::clientStateChanged(quint8 state)
{
    switch (state) {
    case QMqttClient::Disconnected:
        m_mqttStatus = "MQTT: disconnected";
        break;
    case QMqttClient::Connecting:
        m_mqttStatus = "MQTT: connecting";
        break;
    case QMqttClient::Connected:
        m_mqttStatus = "MQTT: connected";
        break;
    }
    updateStatusbar();
    updateNetworkInfo();
}

void MainWindow::onModbusOnlineChanged(bool online)
{
    m_modbusOnline = online;
    updateStatusbar();
}

void MainWindow::onSerialPortChanged(bool open)
{
    m_serialOpen = open;
    updateStatusbar();
}

void MainWindow::weatherInfoIsChanged(weather_t wi)
{
    m_weatherInfo = QString("%1  %2°C  %3%  %4hPa").arg(wi.comune.toUpper()).arg(wi.temp, 0, 'f', 1).arg(wi.hum).arg(wi.press);
    updateStatusbar();
}

void MainWindow::updateStatusbar()
{
    QString modbus;
    if(!m_serialOpen)
        modbus = "Modbus: port closed";
    else
        modbus = m_modbusOnline ? "Modbus: online" : "Modbus: OFFLINE";

    QStringList parts = { QString("%1 v%2").arg(SW_NAME).arg(SW_VER), m_mqttStatus, modbus };
    if(!m_weatherInfo.isEmpty())
        parts.append(m_weatherInfo);
    setBarRow(0, parts);
}

/**
 * @brief MainWindow::updateClock
 * Local wall clock (NTP/RTC of the device), not MonoClock
 */
void MainWindow::updateClock()
{
    const QDateTime now = QDateTime::currentDateTime();
    m_barClock[0]->setText(now.toString("dd/MM/yyyy"));
    m_barClock[1]->setText(now.toString("HH:mm"));
}

/**
 * @brief MainWindow::setBarRow
 * Fields beyond the given ones (and their separator) are hidden
 */
void MainWindow::setBarRow(int row, const QStringList &fields)
{
    for(int f = 0; f < BAR_FIELDS; f++)
    {
        const bool shown = f < fields.size();
        m_barFields[row][f]->setText(fields.value(f));
        m_barFields[row][f]->setVisible(shown);
        if(f > 0)
            m_barSeparators[row][f-1]->setVisible(shown);
    }
}

/**
 * @brief MainWindow::updateNetworkInfo
 * Interface used for the broker, else the first active one
 */
void MainWindow::updateNetworkInfo()
{
    setNetworkInfo(NetInfo::current(m_mqtt->localAddress()));
}

/**
 * @brief MainWindow::setNetworkInfo
 * @param info  second row of the bar
 */
void MainWindow::setNetworkInfo(const NetInfo::Info &info)
{
    setBarRow(1, NetInfo::fields(info));
}

void MainWindow::refreshAllZones()
{
    for(int zone=0; zone<m_cards.size(); zone++)
        zoneDataIsChanged(zone);
}

/**
 * @brief MainWindow::setChronoLine
 * Next chrono change ("22:30 → 17.0°"); "manual → 22:30" while the
 * setpoint is not the chrono one; "chrono paused" while away
 */
void MainWindow::setChronoLine(ZoneCard *card, const ZoneData &t)
{
    const QDateTime now = QDateTime::currentDateTime();
    if(!t.chrono.enabled)
        card->setChrono("", false);
    else if(!ZoneModel::clockValid(now))
        card->setChrono("chrono: clock not set", false);
    else if(m_zones->houseMode() == ZoneModel::ModeAway)
        card->setChrono("chrono paused", false);
    else
    {
        const ChronoPoint next = Chrono::next(now, t.chrono);
        const bool manual = m_zones->chronoManual(m_zones->indexOf(t.name), now);
        if(manual)
            card->setChrono("manual → " + next.start.toString("HH:mm"), true);
        else
            card->setChrono(QString("%1 → %2°").arg(next.start.toString("HH:mm")).arg(next.temp, 0, 'f', 1), false);
    }
}

/**
 * @brief MainWindow::zoneDataIsChanged
 * ZoneData to card texts. Status line shows the most important problem.
 * @param zone
 */
void MainWindow::zoneDataIsChanged(int zone)
{
    if( zone < 0 || zone >= m_cards.size() )
        return;

    const ZoneData &t = m_zones->zone(zone);
    ZoneCard *card = m_cards[zone];

    QString name = t.name;
    name[0] = name[0].toUpper();
    card->setName(name);

    const bool hasData = t.lastSeenMs != 0;
    card->setTemperature(hasData ? QString("%1°").arg(t.temp, 0, 'f', 1) : "--.-°");
    card->setInfo(hasData ? QString("Hum %1%   Batt %2%").arg(t.humidity).arg(t.battery) : "");
    /* House mode (from RoomSense): the applied value; +/- change the own one */
    static const char *setLabel[] = { "Set", "Window", "Away", "Boost" };
    card->setSetPoint(QString("%1 %2°").arg(setLabel[m_zones->houseMode()]).arg(t.target, 0, 'f', 1));
    card->setHeat(t.heat);
    card->setRelay(t.relay > 0 ? t.relayState : -1);
    setChronoLine(card, t);

    if(t.relayFault)
        card->setStatus(QString("RELAY %1 FAULT").arg(t.relay), true);
    else if(t.frostProtection)
        card->setStatus("FROST MODE", true);
    else if(t.sensorLost)
        card->setStatus("NO SENSOR " + ago(t.lastSeenMs), true);
    else if(!hasData)
        card->setStatus("waiting for sensor", false);
    else if(t.battery > 0 && t.battery < BATTERY_LOW_PCT)
        card->setStatus(QString("BATTERY LOW %1%").arg(t.battery), true);
    else if(t.valveExercise)
        card->setStatus("valve exercise", false);
    else if(t.pendingSwitch >= 0)
        card->setStatus(QString("switch %1 in %2").arg(t.pendingSwitch ? "ON" : "OFF").arg(in(t.pendingSwitchAtMs)), false);
    else if(t.relay == 0)
        card->setStatus("no relay - updated " + ago(t.lastSeenMs), false);
    else
        card->setStatus("updated " + ago(t.lastSeenMs), false);
}
