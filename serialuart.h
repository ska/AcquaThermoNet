#ifndef SERIALUART_H
#define SERIALUART_H

#include <QObject>
#include <QtSerialPort/qserialport.h>
#include <QQueue>
#include <QTimer>
#include "common.h"

/*
 * Modbus RTU transport on RS485, event driven, runs in the caller thread.
 * One request at a time: next frame is sent when the answer is complete
 * (line silent for MB_FRAME_GAP_MS) or after MB_ANSWER_TIMEOUT_MS.
 * A port that fails to open or disappears is retried every REOPEN_MS.
 */
class SerialUart : public QObject
{
    Q_OBJECT
public:
    /* No answer within this time: give up and send the next frame */
    static const int MB_ANSWER_TIMEOUT_MS = 1000;
    /* Line silent this long after data: answer frame complete */
    static const int MB_FRAME_GAP_MS      = 50;
    /* Closed or lost port: retry this often */
    static const int REOPEN_MS            = 10000;

    explicit SerialUart(const QString &portName, qint32 baudRate, QObject *parent = nullptr);
    ~SerialUart();

    bool open();
    bool isIdle() const;
    bool isOpen() const { return m_serial->isOpen(); }

public slots:
    void sendFrame(const QByteArray &frame);

signals:
    void frameReceived(const QByteArray &frame);
    void portOpenChanged(bool open);

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);
    void openPort();
    void onFrameGap();
    void onAnswerTimeout();

private:
    void sendNext();
    void closePort();
    void setOpenState(bool open);

    QSerialPort         *m_serial;
    QQueue<QByteArray>  m_txQueue;
    QByteArray          m_rxBuffer;
    QTimer              *m_gapTimer;
    QTimer              *m_answerTimer;
    QTimer              *m_reopenTimer;
    bool                m_waitAns;
    bool                m_open;
    bool                m_openErrorLogged;
};

#endif // SERIALUART_H
