#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "monoclock.h"
#include <QFile>
#include <QTimer>

MainWindow::MainWindow(ZoneModel *zones, Mqtt *mq, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_zones(zones)
{
    /*
     * styleSheet
     */
    QFile file(":/qss/default.qss");
    file.open(QFile::ReadOnly);
    qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
    ui->setupUi(this);
    ui->statusbar->setSizeGripEnabled(false);

    m_mqttStatus    = "MQTT: disconnected";
    m_serialOpen    = false;
    m_modbusOnline  = true;

    connect( mq,        &Mqtt::clientStateChanged,  this, &MainWindow::clientStateChanged );
    connect( m_zones,   &ZoneModel::zoneChanged,    this, &MainWindow::zoneDataIsChanged );

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
    ui->statusbar->showMessage(parts.join("   |   "));
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
    card->setSetPoint(QString("Set %1°").arg(t.setPoint, 0, 'f', 1));
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
