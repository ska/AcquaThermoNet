#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QVector>
#include "common.h"
#include "zonemodel.h"
#include "mqtt.h"
#include "zonecard.h"
#include "netinfo.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(ZoneModel *zones, Mqtt *mq, QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void weatherInfoIsChanged(weather_t);
    void setNetworkInfo(const NetInfo::Info &info);
    void onModbusOnlineChanged(bool online);
    void onSerialPortChanged(bool open);

private slots:
    void clientStateChanged( quint8 );
    void zoneDataIsChanged(int zone);
    void refreshAllZones();
    void updateNetworkInfo();

private:
    Ui::MainWindow          *ui;
    ZoneModel               *m_zones;
    Mqtt                    *m_mqtt;
    /* Bottom bar, two rows of fields in a grid so that the separators line
     * up: [0] version, MQTT, Modbus, weather; [1] host, IP, MAC */
    static const int BAR_FIELDS = 4;
    QVector<QLabel*>        m_barFields[2];
    QVector<QLabel*>        m_barSeparators[2];     /* before field 1.. */
    QString                 m_mqttStatus;
    QString                 m_weatherInfo;
    bool                    m_serialOpen;
    bool                    m_modbusOnline;

    QVector<ZoneCard*>      m_cards;

    void updateStatusbar();
    void setBarRow(int row, const QStringList &fields);
    static QString ago(qint64 ms);
    static QString in(qint64 ms);
};
#endif // MAINWINDOW_H
