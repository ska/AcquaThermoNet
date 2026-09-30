#ifndef MODBUSFRAME_H
#define MODBUSFRAME_H
#include <QByteArray>
#include "common.h"

/* Modbus RTU frame buffer: build requests, check answers */
class ModBusFrame
{
public:
    static const quint8 MB_DEVICE_ID        = 0x01;
    static const quint8 MB_READ_AN16_FC3    = 0x03;
    static const quint8 MB_WRITE_AN16_FC6   = 0x06;

    ModBusFrame() = default;
    explicit ModBusFrame(const QByteArray &data) : m_buffer(data) {}

    const QByteArray &getBuffer() const { return m_buffer; }
    char getByte(int index) const;
    void requestRegisters(quint16 address, quint16 length);
    void setSingleRegisterU16(quint16 address, quint16 value);
    void addQbyteArray(const QByteArray &data);
    void clear();
    int length() const;
    bool verifyCrc() const;

private:
    QByteArray m_buffer;
    static quint16 crcGenerator(const QByteArray &ba, int sz);
    static quint16 qbaToQuint16(const QByteArray &ba, int offset);
};

#endif // MODBUSFRAME_H
