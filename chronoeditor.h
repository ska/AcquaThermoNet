#ifndef CHRONOEDITOR_H
#define CHRONOEDITOR_H

#include <QWidget>
#include <QVector>
#include "chrono.h"

class ZoneModel;
class QLabel;
class QPushButton;
class QStackedLayout;
class ChronoPreview;

/*
 * Chrono editor of one zone: weekday and holiday profiles, one row per
 * slot with time -/+ (15 minutes) and setpoint -/+ (TEMP_STEP), chrono
 * on/off. Works on a copy: MainWindow saves it (Save) or drops it (Cancel).
 */
class ChronoEditor : public QWidget
{
    Q_OBJECT

public:
    explicit ChronoEditor(ZoneModel *zones, QWidget *parent = nullptr);

    /* Start editing zone i with its current profiles, showing the weekday
     * or the holiday one */
    void edit(int zone, bool holiday = false);
    int zone() const { return m_zone; }
    const ChronoConfig &config() const { return m_cfg; }

signals:
    void resetRequested(int zone);          /* back to the profiles of setting.ini */

private:
    struct Row
    {
        QWidget         *cell;
        QStackedLayout  *stack;             /* slot, "+ Slot", empty */
        QLabel          *time;
        QLabel          *temp;
        QPushButton     *remove;
    };

    ZoneModel       *m_zones;
    int             m_zone      = -1;
    ChronoConfig    m_cfg;
    bool            m_holiday   = false;    /* profile shown */

    QLabel          *m_name;
    QPushButton     *m_weekdayButton;
    QPushButton     *m_holidayButton;
    QPushButton     *m_copyButton;
    QPushButton     *m_resetButton;
    QPushButton     *m_enableButton;
    ChronoPreview   *m_preview;
    QVector<Row>    m_rows;

    QVector<ChronoSlot> &profile() { return m_holiday ? m_cfg.holiday : m_cfg.weekday; }
    void showProfile(bool holiday);
    void refresh();
};

#endif // CHRONOEDITOR_H
