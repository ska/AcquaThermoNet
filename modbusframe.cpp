#include "modbusframe.h"
#include <QtEndian>

/**
 * @brief static ModBusFrame::qbaToQuint16
 * @param ba
 * @param offset
 * @return big endian quint16 at offset, 0 if out of range
 */
quint16 ModBusFrame::qbaToQuint16(const QByteArray &ba, int offset)
{
    if(offset < 0 || ba.length() < offset+2)
        return 0x0000;
    return qFromBigEndian<quint16>(reinterpret_cast<const unsigned char*>(ba.constData() + offset));
}

/**
 * @brief ModBusFrame::getByte
 * @param index
 * @return
 */
char ModBusFrame::getByte(int index) const
{
    return m_buffer.at(index);
}

/**
 * @brief ModBusFrame::requestRegisters
 * @param address
 * @param length
 */
void ModBusFrame::requestRegisters(quint16 address, quint16 length)
{
    m_buffer.append((char) MB_DEVICE_ID);
    m_buffer.append((char) MB_READ_AN16_FC3);
    m_buffer.append((char) ((address & 0xFF00) >> 8));
    m_buffer.append((char) ((address & 0x00FF) >> 0));
    m_buffer.append((char) ((length  & 0xFF00) >> 8));
    m_buffer.append((char) ((length  & 0x00FF) >> 0));
    quint16 crc = crcGenerator(m_buffer, m_buffer.length() );
    m_buffer.append((char) ((crc&0xFF00)>>8) );
    m_buffer.append((char) ((crc&0x00FF)>>0) );
}

/**
 * @brief ModBusFrame::setSingleRegisterU16
 * @param address
 * @param value
 */
void ModBusFrame::setSingleRegisterU16(quint16 address, quint16 value)
{
    m_buffer.append((char) MB_DEVICE_ID);
    m_buffer.append((char) MB_WRITE_AN16_FC6);
    m_buffer.append((char) ((address & 0xFF00) >> 8));
    m_buffer.append((char) ((address & 0x00FF) >> 0));
    m_buffer.append((char) ((value  & 0xFF00) >> 8));
    m_buffer.append((char) ((value  & 0x00FF) >> 0));
    quint16 crc = crcGenerator(m_buffer, m_buffer.length() );
    m_buffer.append((char) ((crc&0xFF00)>>8) );
    m_buffer.append((char) ((crc&0x00FF)>>0) );
}

/**
 * @brief ModBusFrame::clear
 */
void ModBusFrame::clear()
{
    m_buffer.clear();
}

/**
 * @brief ModBusFrame::length
 * @return
 */
int ModBusFrame::length() const
{
    return m_buffer.length();
}

/**
 * @brief ModBusFrame::addQbyteArray
 * @param data
 */
void ModBusFrame::addQbyteArray(const QByteArray &data)
{
    m_buffer.append(data);
}

/**
 * @brief ModBusFrame::verifyCrc
 * @return
 */
bool ModBusFrame::verifyCrc() const
{
    /* Address, function code and CRC at least */
    if(m_buffer.length() < 4)
        return false;
    quint16 data_crc = qbaToQuint16(m_buffer, m_buffer.length()-2);
    quint16 calc_crc = crcGenerator(m_buffer, m_buffer.length()-2);
    return data_crc == calc_crc;
}

/**
 * @brief ModBusFrame::crcGenerator
 * @param ba
 * @param sz
 * @return CRC16 Modbus, byte swapped (as sent on the wire)
 */
quint16 ModBusFrame::crcGenerator(const QByteArray &ba, int sz)
{
    quint16 crc=0xFFFF, result=0x0000;

    for(int j=0; j<sz; j++)
    {
        crc ^= (quint8) ba.at(j);
        for(int i=0; i<8; i++)
        {
            if(crc & 0x01)
                crc = (crc>>1)^0xA001;
            else
                crc = (crc>>1);
        }
    }
    result = (( crc<<8)&0xFF00)|((crc>>8)&0x00FF);
    return result;
}
