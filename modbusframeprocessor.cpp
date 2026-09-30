#include "modbusframeprocessor.h"
#include "logging.h"

/**
 * @brief ModBusFrameProcessor::ModBusFrameProcessor
 * @param parent
 */
ModBusFrameProcessor::ModBusFrameProcessor(QObject *parent) : QObject(parent)
{
    m_waitAllRegs   = false;
    m_missed        = 0;
    m_online        = true;

    /* Created here so requests work before startPoll() */
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ModBusFrameProcessor::onPollTimer);
}

/**
 * @brief ModBusFrameProcessor::startPoll
 * @param polltime
 */
void ModBusFrameProcessor::startPoll(quint16 polltime)
{
    m_timer->start((polltime*1000));
}

/**
 * @brief ModBusFrameProcessor::frameIncoming
 * Only the answer to the relay poll is used, write echoes are ignored
 * @param data
 */
void ModBusFrameProcessor::frameIncoming(const QByteArray &data)
{
    /* id, fc3, byte count, 2 bytes per relay register, crc */
    const ModBusFrame frame(data);
    if( frame.length() != 3 + RELAY_NUM_MAX*2 + 2 || !frame.verifyCrc() )
        return;
    if( frame.getByte(1) != ModBusFrame::MB_READ_AN16_FC3 )
        return;

    /* bit n = relay n+1, low byte of register n+1 */
    quint8 relayBm = 0;
    for(int relay=0; relay<RELAY_NUM_MAX; relay++)
    {
        if(frame.getByte(4 + relay*2))
            relayBm |= (1 << relay);
    }

    m_waitAllRegs = false;
    m_missed = 0;
    if(!m_online)
    {
        m_online = true;
        qCInfo(lcModbus) << "Modbus relay board online";
        emit onlineChanged(true);
    }

    emit relayStatus(relayBm);
}

/**
 * @brief ModBusFrameProcessor::onPollTimer
 * Previous poll still unanswered: count it as missed
 */
void ModBusFrameProcessor::onPollTimer()
{
    if(m_waitAllRegs)
    {
        m_missed++;
        if(m_missed >= MB_MAX_MISSED && m_online)
        {
            m_online = false;
            qCWarning(lcModbus) << "Modbus relay board offline:" << m_missed << "poll answers missed";
            emit onlineChanged(false);
        }
    }
    requestAllRegs();
}

/**
 * @brief ModBusFrameProcessor::requestAllRegs
 */
void ModBusFrameProcessor::requestAllRegs()
{
    ModBusFrame frame;
    frame.requestRegisters(1, RELAY_NUM_MAX);
    emit frameToSend(frame.getBuffer());
    m_waitAllRegs = true;
    m_timer->start();
}

/**
 * @brief ModBusFrameProcessor::requestWrite
 */
void ModBusFrameProcessor::requestWriteSingleReg(quint16 address, quint16 value)
{
    ModBusFrame frame;
    frame.setSingleRegisterU16(address, value);
    emit frameToSend(frame.getBuffer());
    requestAllRegs();
}

void ModBusFrameProcessor::requestSetRelay(quint16 relayNum, bool value)
{
    quint16 val = value ? MB_CMD_RELAY_ON : MB_CMD_RELAY_OFF;
    requestWriteSingleReg(relayNum, val);
}
