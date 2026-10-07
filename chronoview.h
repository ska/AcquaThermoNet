#ifndef CHRONOVIEW_H
#define CHRONOVIEW_H

#include <QWidget>
#include <QVector>
#include <QPair>
#include <QDate>

class ZoneModel;
class QPainter;

/*
 * Chrono page: the setpoint of each zone along a day, one bar every
 * 15 minutes. Overview: one row per zone; a tap on a row shows that zone
 * with scales in °C and hours. Day from today to a week ahead.
 */
class ChronoView : public QWidget
{
    Q_OBJECT

public:
    static constexpr int STEP_MIN = 15;
    static constexpr int MAX_DAY  = 6;          /* days ahead */

    explicit ChronoView(ZoneModel *zones, QWidget *parent = nullptr);

    int day() const { return m_day; }
    QDate date() const;                     /* day shown */
    int zone() const { return m_zone; }
    /* Bar of setpoint t: blue (cold) .. amber .. red (comfort); dim: past */
    static QColor barColor(double t, bool dim = false);
    /* "Sat 03/10 · today" */
    QString dayText() const;

public slots:
    void showOverview();
    void showZone(int zone);
    void stepDay(int steps);

signals:
    void viewChanged();                     /* day or zone shown */

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /* Tap areas of the last paint: zone index, ALL_ZONES for the overview */
    static constexpr int ALL_ZONES = -1;
    QVector<QPair<QRect, int>> m_taps;

    ZoneModel   *m_zones;
    int         m_day   = 0;                /* 0 today .. MAX_DAY */
    int         m_zone  = ALL_ZONES;

    bool isToday() const { return m_day == 0; }
    void paintOverview(QPainter &p);
    void paintZone(QPainter &p);
    void paintNowLine(QPainter &p, const QRectF &chart, bool label);
};

#endif // CHRONOVIEW_H
