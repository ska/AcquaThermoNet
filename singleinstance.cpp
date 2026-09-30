#include "singleinstance.h"
#include "logging.h"
#include <QDebug>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/**
 * @brief SingleInstance::SingleInstance
 * @param name  lock name, unique on the target
 */
SingleInstance::SingleInstance(const QString &name) :
    m_name(name.toUtf8()),
    m_fd(-1)
{
}

SingleInstance::~SingleInstance()
{
    if(m_fd >= 0)
        ::close(m_fd);
}

/**
 * @brief SingleInstance::tryLock
 * Keep the object alive for the whole run: the lock lasts as long as the fd
 * @return true if this is the only instance
 */
bool SingleInstance::tryLock()
{
    if(m_fd >= 0)
        return true;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    /* sun_path[0] = '\0': abstract namespace, no file on disk */
    const int len = qMin<int>(m_name.size(), sizeof(addr.sun_path) - 1);
    memcpy(addr.sun_path + 1, m_name.constData(), len);
    const socklen_t addrLen = offsetof(struct sockaddr_un, sun_path) + 1 + len;

    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(fd < 0)
    {
        qCCritical(lcApp) << "SingleInstance: socket() failed:" << strerror(errno);
        return false;
    }

    if(::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), addrLen) != 0)
    {
        if(errno == EADDRINUSE)
            qCCritical(lcApp) << "Another instance of" << m_name << "is already running";
        else
            qCCritical(lcApp) << "SingleInstance: bind() failed:" << strerror(errno);
        ::close(fd);
        return false;
    }

    m_fd = fd;
    return true;
}
