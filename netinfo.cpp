#include "netinfo.h"
#include <QHostInfo>
#include <QNetworkInterface>

namespace
{
    /* IPv4, also from an IPv4 mapped IPv6 address (::ffff:a.b.c.d) */
    QHostAddress ipv4(const QHostAddress &address)
    {
        if(address.protocol() == QAbstractSocket::IPv4Protocol)
            return address;
        bool ok = false;
        const quint32 v4 = address.toIPv4Address(&ok);
        return ok ? QHostAddress(v4) : QHostAddress();
    }

    QString firstIpv4(const NetInfo::Interface &itf)
    {
        for(const QHostAddress &a : itf.addresses)
        {
            const QHostAddress v4 = ipv4(a);
            if(!v4.isNull())
                return v4.toString();
        }
        return QString();
    }

    bool validMac(const QString &mac)
    {
        QString digits = mac;
        digits.remove(':');
        return !digits.isEmpty() && digits.count('0') != digits.size();
    }
}

/**
 * @brief NetInfo::choose
 * @param interfaces
 * @param preferred     local address of the MQTT connection, null if none
 * @param hostname
 */
NetInfo::Info NetInfo::choose(const QList<Interface> &interfaces, const QHostAddress &preferred, const QString &hostname)
{
    Info info;
    info.hostname = hostname;

    const Interface *found = nullptr;
    QString ip;

    const QHostAddress want = ipv4(preferred);
    if(!want.isNull())
    {
        for(const Interface &itf : interfaces)
        {
            for(const QHostAddress &a : itf.addresses)
            {
                if(ipv4(a) == want)
                {
                    found = &itf;
                    ip = want.toString();
                    break;
                }
            }
            if(found)
                break;
        }
    }

    for(int i = 0; !found && i < interfaces.size(); i++)
    {
        const Interface &itf = interfaces.at(i);
        if(!itf.active || itf.loopback)
            continue;
        ip = firstIpv4(itf);
        if(!ip.isEmpty())
            found = &itf;
    }

    if(found)
    {
        info.interface = found->name;
        info.ip = ip;
        if(validMac(found->mac))
            info.mac = found->mac.toUpper();
    }
    return info;
}

/**
 * @brief NetInfo::current
 * @param preferred
 */
NetInfo::Info NetInfo::current(const QHostAddress &preferred)
{
    QList<Interface> list;
    for(const QNetworkInterface &qi : QNetworkInterface::allInterfaces())
    {
        Interface itf;
        itf.name     = qi.name();
        itf.mac      = qi.hardwareAddress();
        itf.active   = (qi.flags() & QNetworkInterface::IsUp) && (qi.flags() & QNetworkInterface::IsRunning);
        itf.loopback = qi.flags() & QNetworkInterface::IsLoopBack;
        for(const QNetworkAddressEntry &e : qi.addressEntries())
            itf.addresses.append(e.ip());
        list.append(itf);
    }
    return choose(list, preferred, QHostInfo::localHostName());
}

/**
 * @brief NetInfo::fields
 * Fields of the network row of the GUI bar
 */
QStringList NetInfo::fields(const Info &info)
{
    QStringList parts = { "Host: " + info.hostname };
    if(info.interface.isEmpty())
        parts << "Network: none";
    else
        parts << "IP: " + info.ip << "MAC: " + (info.mac.isEmpty() ? QString("--") : info.mac);
    return parts;
}
