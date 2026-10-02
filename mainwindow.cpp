#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "monoclock.h"
#include "netinfo.h"
#include <QFile>
#include <QTimer>
#include <QDateTime>
#include <QGridLayout>

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
