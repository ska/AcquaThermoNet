#ifndef SINGLEINSTANCE_H
#define SINGLEINSTANCE_H

#include <QString>

/*
 * One instance per target: binds a Linux abstract unix socket named
 * after the application. The kernel allows one owner per name, for any
 * user, and frees it when the process ends for any reason (exit, crash,
 * SIGKILL): no stale lock to clean, no file that can be removed.
 */
class SingleInstance
{
public:
    explicit SingleInstance(const QString &name);
    ~SingleInstance();

    bool tryLock();

private:
    QByteArray  m_name;
    int         m_fd;

    Q_DISABLE_COPY(SingleInstance)
};

#endif // SINGLEINSTANCE_H
