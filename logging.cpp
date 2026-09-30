#include "logging.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <cstdio>

Q_LOGGING_CATEGORY(lcApp,        "atn.app",        QtInfoMsg)
Q_LOGGING_CATEGORY(lcConfig,     "atn.config",     QtInfoMsg)
Q_LOGGING_CATEGORY(lcMqtt,       "atn.mqtt",       QtInfoMsg)
Q_LOGGING_CATEGORY(lcModbus,     "atn.modbus",     QtInfoMsg)
Q_LOGGING_CATEGORY(lcRegulation, "atn.regulation", QtInfoMsg)
Q_LOGGING_CATEGORY(lcWeather,    "atn.weather",    QtInfoMsg)
Q_LOGGING_CATEGORY(lcTelegram,   "atn.telegram",   QtInfoMsg)

namespace
{
    QMutex  s_mutex;
    QFile   *s_file     = nullptr;
    qint64  s_maxBytes  = 0;
    int     s_files     = 0;

    /* path -> path.1 -> ... -> path.<files>, oldest dropped */
    void rotate()
    {
        const QString path = s_file->fileName();
        s_file->close();
        QFile::remove(QString("%1.%2").arg(path).arg(s_files));
        for(int i = s_files - 1; i >= 1; i--)
            QFile::rename(QString("%1.%2").arg(path).arg(i), QString("%1.%2").arg(path).arg(i + 1));
        QFile::rename(path, path + ".1");
        s_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }

    void handler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
    {
        const QByteArray line = qFormatLogMessage(type, context, msg).toLocal8Bit() + '\n';

        QMutexLocker lock(&s_mutex);
        fputs(line.constData(), stderr);

        /* Debug stays off the flash */
        if(s_file && s_file->isOpen() && type != QtDebugMsg)
        {
            s_file->write(line);
            s_file->flush();
            if(s_file->size() > s_maxBytes)
                rotate();
        }
    }
}

/**
 * @brief Logging::install
 * QT_MESSAGE_PATTERN in the environment still overrides the pattern
 */
void Logging::install()
{
    qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss.zzz} "
                       "%{if-debug}D%{endif}%{if-info}I%{endif}%{if-warning}W%{endif}"
                       "%{if-critical}C%{endif}%{if-fatal}F%{endif} "
                       "%{category}: %{message}");
    qInstallMessageHandler(handler);
}

/**
 * @brief Logging::enableFile
 * @param path
 * @param maxBytes
 * @param files
 */
void Logging::enableFile(const QString &path, qint64 maxBytes, int files)
{
    if(path.isEmpty())
        return;

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile *file = new QFile(path);
    if(!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        qCWarning(lcApp) << "Cannot open log file" << path << ":" << file->errorString();
        delete file;
        return;
    }

    {
        QMutexLocker lock(&s_mutex);
        s_file      = file;
        s_maxBytes  = qMax<qint64>(16 * 1024, maxBytes);
        s_files     = qMax(1, files);
    }
    qCInfo(lcApp).noquote() << "Log file" << QFileInfo(path).absoluteFilePath()
                            << "max" << s_maxBytes / 1024 << "KB x" << s_files;
}
