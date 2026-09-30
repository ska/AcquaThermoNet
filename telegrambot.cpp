#include "telegrambot.h"
#include "logging.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

/**
 * @brief TelegramBot::TelegramBot
 * @param config
 * @param parent
 */
TelegramBot::TelegramBot(const TelegramConfig &config, QObject *parent) :
    QObject(parent),
    m_config(config),
    m_sending(false),
    m_pollReply(nullptr),
    m_offset(0),
    m_pollErrors(0),
    m_sendErrors(0)
{
    m_manager = new QNetworkAccessManager(this);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, &TelegramBot::poll);

    m_sendTimer = new QTimer(this);
    m_sendTimer->setSingleShot(true);
    connect(m_sendTimer, &QTimer::timeout, this, &TelegramBot::sendNext);

    /* Qt 5.13 has no transfer timeout: abort a poll well after the server one */
    m_pollWatchdog = new QTimer(this);
    m_pollWatchdog->setSingleShot(true);
    connect(m_pollWatchdog, &QTimer::timeout, this, [this] {
        if(m_pollReply)
            m_pollReply->abort();
    });
}

/**
 * @brief TelegramBot::start
 */
void TelegramBot::start()
{
    qCInfo(lcTelegram) << "Telegram bot on," << m_config.allowedChats.size() << "allowed chat(s)";
    poll();
}

/**
 * @brief TelegramBot::redact
 * The token must never reach the log (Qt error strings contain the URL)
 */
QString TelegramBot::redact(QString text) const
{
    if(!m_config.token.isEmpty())
        text.replace(m_config.token, "***");
    return text;
}

int TelegramBot::backoffMs(int errors)
{
    static const int steps[] = { 5000, 10000, 30000, 60000 };
    return steps[qBound(0, errors - 1, 3)];
}

QNetworkRequest TelegramBot::request(const char *method) const
{
    QNetworkRequest req(QUrl(m_config.apiUrl + "/bot" + m_config.token + "/" + method));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("User-Agent", SW_NAME "/" SW_VER);
    return req;
}

/**
 * @brief TelegramBot::sendToAll
 * @param text  to every allowed chat
 */
void TelegramBot::sendToAll(const QString &text)
{
    for(qint64 chat : m_config.allowedChats)
        send(chat, text);
}

/**
 * @brief TelegramBot::send
 */
void TelegramBot::send(qint64 chatId, const QString &text)
{
    if(m_queue.size() >= MAX_QUEUE)
    {
        m_queue.dequeue();
        qCWarning(lcTelegram) << "Telegram queue full, oldest message dropped";
    }
    m_queue.enqueue(Outgoing{ chatId, text });
    if(!m_sending && !m_sendTimer->isActive())
        sendNext();
}

/**
 * @brief TelegramBot::sendNext
 */
void TelegramBot::sendNext()
{
    if(m_sending || m_queue.isEmpty())
        return;

    const Outgoing &o = m_queue.head();
    QJsonObject body;
    body["chat_id"] = o.chatId;
    body["text"] = o.text;
    body["disable_web_page_preview"] = true;

    m_sending = true;
    QNetworkReply *reply = m_manager->post(request("sendMessage"), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, &TelegramBot::onSendFinished);
}

/**
 * @brief TelegramBot::onSendFinished
 * Network or server error: keep the message and retry later.
 * Refused by Telegram (bad chat, bad text): drop it, it would never pass.
 */
void TelegramBot::onSendFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    reply->deleteLater();
    m_sending = false;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();

    if(reply->error() == QNetworkReply::NoError && answer["ok"].toBool())
    {
        m_queue.dequeue();
        m_sendErrors = 0;
        /* about one message per second per chat, as Telegram asks */
        if(!m_queue.isEmpty())
            m_sendTimer->start(1000);
        return;
    }

    if(status == 429)
    {
        const int retryS = answer["parameters"].toObject()["retry_after"].toInt(5);
        qCWarning(lcTelegram) << "Telegram rate limit, retry in" << retryS << "s";
        m_sendTimer->start(retryS * 1000);
        return;
    }
    if(status >= 400 && status < 500)
    {
        qCWarning(lcTelegram).noquote() << "Telegram refused a message for chat" << m_queue.head().chatId
                                        << ":" << status << redact(answer["description"].toString());
        m_queue.dequeue();
        if(!m_queue.isEmpty())
            m_sendTimer->start(1000);
        return;
    }

    m_sendErrors++;
    const int delay = backoffMs(m_sendErrors);
    if(m_sendErrors == 1 || m_sendErrors % 10 == 0)
        qCWarning(lcTelegram).noquote() << "Telegram send failed:" << redact(reply->errorString())
                                        << ", retry in" << delay / 1000 << "s," << m_queue.size() << "queued";
    m_sendTimer->start(delay);
}

/**
 * @brief TelegramBot::poll
 * Long polling: the server answers when a message arrives or after
 * POLL_TIMEOUT_S with nothing
 */
void TelegramBot::poll()
{
    if(m_pollReply)
        return;

    QJsonObject body;
    body["offset"] = m_offset;
    body["timeout"] = POLL_TIMEOUT_S;
    body["allowed_updates"] = QJsonArray{ "message" };

    m_pollReply = m_manager->post(request("getUpdates"), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_pollReply, &QNetworkReply::finished, this, &TelegramBot::onPollFinished);
    m_pollWatchdog->start((POLL_TIMEOUT_S + 20) * 1000);
}

/**
 * @brief TelegramBot::onPollFinished
 */
void TelegramBot::onPollFinished()
{
    QNetworkReply *reply = m_pollReply;
    m_pollReply = nullptr;
    m_pollWatchdog->stop();
    reply->deleteLater();

    const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
    if(reply->error() != QNetworkReply::NoError || !answer["ok"].toBool())
    {
        m_pollErrors++;
        const int delay = backoffMs(m_pollErrors);
        if(m_pollErrors == 1 || m_pollErrors % 10 == 0)
            qCWarning(lcTelegram).noquote() << "Telegram poll failed:"
                                            << redact(reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                                              : answer["description"].toString())
                                            << ", retry in" << delay / 1000 << "s";
        m_pollTimer->start(delay);
        return;
    }
    if(m_pollErrors)
        qCInfo(lcTelegram) << "Telegram reachable again";
    m_pollErrors = 0;

    /* Telegram is back: send the queue now, not at the next retry */
    if(m_sendErrors && !m_sending && !m_queue.isEmpty())
    {
        m_sendTimer->stop();
        sendNext();
    }

    for(const QJsonValue &v : answer["result"].toArray())
    {
        const QJsonObject update = v.toObject();
        m_offset = qMax(m_offset, qint64(update["update_id"].toDouble()) + 1);

        const QJsonObject msg = update["message"].toObject();
        const qint64 chat = qint64(msg["chat"].toObject()["id"].toDouble());
        const QString text = msg["text"].toString().trimmed();
        if(text.isEmpty())
            continue;

        if(!m_config.allowedChats.contains(chat))
        {
            if(!m_unknownLogged.contains(chat))
            {
                m_unknownLogged.insert(chat);
                const QJsonObject from = msg["from"].toObject();
                qCWarning(lcTelegram).noquote() << "Telegram message from a chat not allowed, ignored: chat id" << chat
                                                << "user" << from["username"].toString() << from["first_name"].toString()
                                                << "(add it to TELEGRAM/allowed_chats to enable it)";
            }
            continue;
        }
        emit commandReceived(chat, text);
    }
    poll();
}
