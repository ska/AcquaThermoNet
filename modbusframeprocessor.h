#ifndef MODBUSFRAMEPROCESSOR_H
#define MODBUSFRAMEPROCESSOR_H
#include <QObject>
#include "modbusframe.h"
#include <QTimer>

class ModBusFrameProcessor : public QObject
{
    Q_OBJECT
public:
    /* Relay n is holding register n (1..RELAY_NUM_MAX) */
    static const quint16 MB_CMD_RELAY_ON     = 0x0100;
    static const quint16 MB_CMD_RELAY_OFF    = 0x0200;


    explicit ModBusFrameProcessor(QObject *parent = nullptr);
    void startPoll(quint16 polltime);
    void requestSetRelay(quint16 relayNum, bool value);
    void requestStatus() { requestAllRegs(); }

    /* Poll answers missed before the board is considered offline */
    static const int     MB_MAX_MISSED       = 3;

    bool isOnline() const { return m_online; }

signals:
    void frameToSend(const QByteArray &frame);
    void relayStatus(quint8 relayBm);       /* bit n = relay n+1 */
    void onlineChanged(bool online);

public slots:
    void frameIncoming(const QByteArray &data);

private slots:
    void onPollTimer();

private:
    void requestWriteSingleReg(quint16 address, quint16 value);
    void requestAllRegs();

    QTimer               *m_timer;
    bool                 m_waitAllRegs;
    int                  m_missed;
    bool                 m_online;
};

#endif // MODBUSFRAMEPROCESSOR_H
