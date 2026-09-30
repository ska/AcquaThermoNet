#ifndef WATCHDOG_H
#define WATCHDOG_H
#include <QObject>
#include <QTimer>
#include "common.h"

/*
 * Hardware watchdog (/dev/watchdog), refreshed by a QTimer in the thread
 * that owns it. Must live in the main thread: if the event loop hangs the
 * refresh stops and the board reboots (relays go OFF at startup).
 */
class WatchDog: public QObject
{
    Q_OBJECT
public:
    explicit WatchDog(QObject *parent = nullptr);
    ~WatchDog();

    bool start();
    void stop();
    bool isActive() const { return m_fd >= 0; }

private slots:
    void refresh();

private:
    int     m_fd;
    QTimer  *m_timer;
};

#endif // WATCHDOG_H
