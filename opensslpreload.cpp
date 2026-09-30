#include "opensslpreload.h"
#include "logging.h"

#ifdef ATN_OPENSSL_DIR
#include <dlfcn.h>

void OpenSslPreload::load()
{
    /* libssl needs libcrypto: load it first */
    for(const char *name : { "/libcrypto.so", "/libssl.so" })
    {
        const QByteArray path = QByteArray(ATN_OPENSSL_DIR) + name;
        if(!dlopen(path.constData(), RTLD_NOW | RTLD_GLOBAL))
        {
            qCWarning(lcApp) << "OpenSSL 1.1 preload failed, HTTPS/TLS not available:" << dlerror();
            return;
        }
    }
    qCInfo(lcApp) << "OpenSSL 1.1 preloaded from" << ATN_OPENSSL_DIR;
}
#else
void OpenSslPreload::load()
{
}
#endif
