#ifndef TELEGRAMBOT_H
#define TELEGRAMBOT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include "configuration.h"

/*
 * Telegram Bot API transport, no inbound connection needed:
 *  - sendMessage from a queue, one at a time, kept and retried while the
 *    network or Telegram is down (429: waits retry_after)
 *  - getUpdates long polling for the commands, only from allowed chats;
 *    messages from other chats are logged with their id and ignored
 * The token is part of every URL: it is removed from any logged text.
 */
class TelegramBot : public QObject
{
    Q_OBJECT
public:
    static const int POLL_TIMEOUT_S = 50;       /* long polling wait on the server */
    static const int MAX_QUEUE      = 200;      /* oldest messages dropped beyond */

    explicit TelegramBot(const TelegramConfig &config, QObject *parent = nullptr);

    void start();
    bool isIdle() const { return m_queue.isEmpty() && !m_sending; }

public slots:
    void sendToAll(const QString &text);
    void send(qint64 chatId, const QString &text);

signals:
    void commandReceived(qint64 chatId, const QString &text);

private slots:
    void poll();
    void onPollFinished();
    void sendNext();
    void onSendFinished();

private:
    struct Outgoing { qint64 chatId; QString text; };

    TelegramConfig          m_config;
    QNetworkAccessManager   *m_manager;
    QQueue<Outgoing>        m_queue;
    bool                    m_sending;
    QNetworkReply           *m_pollReply;
    qint64                  m_offset;
    int                     m_pollErrors;
    int                     m_sendErrors;
    QTimer                  *m_pollTimer;       /* next poll (after an error) */
    QTimer                  *m_sendTimer;       /* next send (spacing, retry) */
    QTimer                  *m_pollWatchdog;    /* abort a stuck long poll */
    QSet<qint64>            m_unknownLogged;

    QNetworkRequest request(const char *method) const;
    QString redact(QString text) const;
    static int backoffMs(int errors);
};

#endif // TELEGRAMBOT_H
