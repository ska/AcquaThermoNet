#include "serialuart.h"
#include "logging.h"
#include <QDebug>
#include <sys/ioctl.h>
#include <linux/serial.h>
#include <asm-generic/termbits.h>
#include <fcntl.h>

/**
 * @brief SerialUart::SerialUart
 * @param portName
 * @param baudRate
 * @param parent
 */
SerialUart::SerialUart(const QString &portName, qint32 baudRate, QObject *parent) :
    QObject(parent)
{
    m_waitAns = false;
    m_open = false;
    m_openErrorLogged = false;

    m_serial = new QSerialPort(this);
    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);
    connect(m_serial, &QSerialPort::readyRead, this, &SerialUart::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &SerialUart::onSerialError);

    m_gapTimer = new QTimer(this);
    m_gapTimer->setSingleShot(true);
    connect(m_gapTimer, &QTimer::timeout, this, &SerialUart::onFrameGap);

    m_answerTimer = new QTimer(this);
    m_answerTimer->setSingleShot(true);
    connect(m_answerTimer, &QTimer::timeout, this, &SerialUart::onAnswerTimeout);

    m_reopenTimer = new QTimer(this);
    connect(m_reopenTimer, &QTimer::timeout, this, &SerialUart::openPort);
}

SerialUart::~SerialUart()
{
    m_reopenTimer->stop();
    if(m_serial->isOpen())
        m_serial->close();
}

/**
 * @brief SerialUart::open
 * First attempt; on failure the port is retried every REOPEN_MS
 * @return true if the port is open now
 */
bool SerialUart::open()
{
    openPort();
    if(!m_open)
        m_reopenTimer->start(REOPEN_MS);
    return m_open;
}

/**
 * @brief SerialUart::openPort
 */
void SerialUart::openPort()
{
    if(m_serial->isOpen())
        return;

    if(!m_serial->open(QIODevice::ReadWrite))
    {
        /* Logged once, not at every retry */
        if(!m_openErrorLogged)
        {
            qCWarning(lcModbus) << "Error open serial port" << m_serial->portName() << ":" << m_serial->errorString() << ", retry every" << REOPEN_MS/1000 << "s";
            m_openErrorLogged = true;
        }
        setOpenState(false);
        return;
    }
    m_openErrorLogged = false;
    m_reopenTimer->stop();
    qCInfo(lcModbus).noquote() << "Serial port open" << m_serial->portName() << m_serial->baudRate() << "8N1";

    /*
     * Set registers for RS485
     * * */
    int fd = m_serial->handle();
    struct serial_rs485 rs485conf;
    memset(&rs485conf, 0, sizeof(rs485conf));
    ::ioctl(fd, TIOCGRS485, &rs485conf);
    rs485conf.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
    ::ioctl(fd, TIOCSRS485, & rs485conf);

    setOpenState(true);
}

/**
 * @brief SerialUart::closePort
 * Drop everything pending: the processor sees the missing answers
 */
void SerialUart::closePort()
{
    m_serial->close();
    m_txQueue.clear();
    m_rxBuffer.clear();
    m_waitAns = false;
    m_gapTimer->stop();
    m_answerTimer->stop();
    setOpenState(false);
}

/**
 * @brief SerialUart::setOpenState
 * @param open
 */
void SerialUart::setOpenState(bool open)
{
    if(open == m_open)
        return;
    m_open = open;
    emit portOpenChanged(open);
}

/**
 * @brief SerialUart::onSerialError
 * Device gone (e.g. USB adapter unplugged): close and retry
 * @param error
 */
void SerialUart::onSerialError(QSerialPort::SerialPortError error)
{
    if(error != QSerialPort::ResourceError)
        return;

    if(!m_open)
        return;

    qCWarning(lcModbus) << "Serial port lost:" << m_serial->errorString() << ", retry every" << REOPEN_MS/1000 << "s";
    setOpenState(false);    /* further errors until the close are ignored */
    /* Not from inside the QSerialPort signal */
    QTimer::singleShot(0, this, [this] {
        closePort();
        m_reopenTimer->start(REOPEN_MS);
    });
}

/**
 * @brief SerialUart::isIdle
 * @return true when nothing is queued or waiting for an answer
 */
bool SerialUart::isIdle() const
{
    return m_txQueue.isEmpty() && !m_waitAns;
}

/**
 * @brief SerialUart::sendFrame
 * @param frame
 */
void SerialUart::sendFrame(const QByteArray &frame)
{
    /* Port closed: drop, the processor sees the missing answers */
    if(!m_serial->isOpen())
        return;

    m_txQueue.enqueue(frame);
    if(!m_waitAns)
        sendNext();
}

/**
 * @brief SerialUart::sendNext
 */
void SerialUart::sendNext()
{
    if(m_txQueue.isEmpty())
        return;

    m_rxBuffer.clear();
    m_waitAns = true;
    m_serial->write(m_txQueue.dequeue());
    m_answerTimer->start(MB_ANSWER_TIMEOUT_MS);
}

/**
 * @brief SerialUart::onReadyRead
 */
void SerialUart::onReadyRead()
{
    m_rxBuffer.append(m_serial->readAll());
    /* Data is flowing: wait for the end of frame, not for the answer */
    m_answerTimer->stop();
    m_gapTimer->start(MB_FRAME_GAP_MS);
}

/**
 * @brief SerialUart::onFrameGap
 */
void SerialUart::onFrameGap()
{
    const QByteArray frame = m_rxBuffer;
    m_rxBuffer.clear();
    emit frameReceived(frame);

    if(m_waitAns)
    {
        m_waitAns = false;
        sendNext();
    }
}

/**
 * @brief SerialUart::onAnswerTimeout
 */
void SerialUart::onAnswerTimeout()
{
    qCWarning(lcModbus) << "Modbus answer timeout";
    m_waitAns = false;
    sendNext();
}
