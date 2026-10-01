#ifndef NETINFO_H
#define NETINFO_H

#include <QHostAddress>
#include <QList>
#include <QString>
#include <QStringList>

/*
 * Network identity of the panel for the GUI: hostname, IPv4 and MAC of the
 * interface used to reach the MQTT broker (the one that matters for a
 * diagnosis), else of the first active interface.
 */
namespace NetInfo
{
    /* One network interface, as far as the choice is concerned */
    struct Interface
    {
        QString             name;
        QString             mac;            /* empty or 00:00:..: none */
        bool                active = false; /* up and running */
        bool                loopback = false;
        QList<QHostAddress> addresses;
    };

    struct Info
    {
        QString hostname;
        QString interface;                  /* empty: no network */
        QString ip;                         /* IPv4 */
        QString mac;
    };

    /* Interface holding preferred (the MQTT local address, may be null);
     * otherwise the first active, non loopback one with an IPv4, in list
     * order. Pure: unit tested. */
    Info choose(const QList<Interface> &interfaces, const QHostAddress &preferred, const QString &hostname);

    /* choose() on the interfaces of this system */
    Info current(const QHostAddress &preferred);

    /* "Host: hmi", "IP: 192.168.1.20", "MAC: 00:30:D8:12:34:56"
     * (or "Host: hmi", "Network: none") */
    QStringList fields(const Info &info);
}

#endif // NETINFO_H
