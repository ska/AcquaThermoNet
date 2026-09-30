#ifndef TESTUTIL_H
#define TESTUTIL_H

#include <QByteArray>
#include <QPair>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QFile>

namespace TestUtil
{
    /* Independent CRC16 Modbus (low byte first, as on the wire) */
    inline QByteArray crc(const QByteArray &data)
    {
        quint16 c = 0xFFFF;
        for(unsigned char b : data)
        {
            c ^= b;
            for(int i = 0; i < 8; i++)
                c = (c & 1) ? (c >> 1) ^ 0xA001 : (c >> 1);
        }
        QByteArray out;
        out.append(char(c & 0xFF));
        out.append(char(c >> 8));
        return out;
    }

    /* Answer to "read 8 relay registers": bit n of relayBm = relay n+1 ON */
    inline QByteArray relayAnswer(quint8 relayBm)
    {
        QByteArray f;
        f.append(char(1)).append(char(3)).append(char(16));
        for(int i = 0; i < 8; i++)
            f.append(char(0)).append(char((relayBm >> i) & 1));
        return f + crc(f);
    }

    /* setting.ini in a temporary dir */
    inline QString writeIni(const QTemporaryDir &dir, const QString &content)
    {
        const QString path = dir.filePath("setting.ini");
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
        QTextStream(&f) << content;
        return path;
    }

    /* Relay writes (FC6) among the frames captured by a frameToSend spy */
    inline QList<QPair<int, bool>> relayWrites(const QSignalSpy &spy)
    {
        QList<QPair<int, bool>> out;
        for(const QList<QVariant> &args : spy)
        {
            const QByteArray f = args.at(0).toByteArray();
            if(f.size() == 8 && f.at(1) == 6)
                out.append(qMakePair(int(quint8(f.at(3))), f.at(4) == 1));
        }
        return out;
    }
}

#endif // TESTUTIL_H
