#ifndef ZONECARD_H
#define ZONECARD_H

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>

/*
 * One zone on the HMI, sized for a touch panel. Only shows what it is
 * given: MainWindow turns ZoneData into texts and states.
 */
class ZoneCard : public QFrame
{
    Q_OBJECT

public:
    explicit ZoneCard(QWidget *parent = nullptr);

    void setName(const QString &text);
    void setTemperature(const QString &text);
    void setInfo(const QString &text);
    void setSetPoint(const QString &text);
    void setStatus(const QString &text, bool alarm);
    void setHeat(bool on);
    void setRelay(int state);           /* -1 unknown, 0 off, 1 on */
    /* Chrono line under the setpoint, blank if empty; manual: the
     * setpoint differs from the chrono one */
    void setChrono(const QString &text, bool manual);

signals:
    void minusClicked();
    void plusClicked();

private:
    QLabel      *m_name;
    QLabel      *m_temp;
    QLabel      *m_info;
    QLabel      *m_setPoint;
    QLabel      *m_status;
    QLabel      *m_chrono;
    QLabel      *m_heatIcon;
    QLabel      *m_relayIcon;
    QPushButton *m_minus;
    QPushButton *m_plus;

    QPixmap     m_flameOn;
    QPixmap     m_flameOff;
    QPixmap     m_relayOn;
    QPixmap     m_relayOff;

    void setAlarmProperty(QWidget *w, bool alarm);
};

#endif // ZONECARD_H
