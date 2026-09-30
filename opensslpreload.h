#ifndef OPENSSLPRELOAD_H
#define OPENSSLPRELOAD_H

/*
 * Desktop with Qt < 5.15 only (ATN_OPENSSL_DIR set by qtmodules.pri):
 * Qt looks for OpenSSL with dlopen("libssl.so") from libQt5Network,
 * whose RUNPATH is its own directory, so the executable rpath does not
 * help and the system OpenSSL 3 is found. Loading the OpenSSL 1.1 built
 * from ThirdParty/openssl first, by absolute path, makes that dlopen get
 * it (already loaded, same soname: see ThirdParty/openssl.conf).
 * Call before any network use; logs the result. No-op elsewhere.
 */
namespace OpenSslPreload
{
    void load();
}

#endif // OPENSSLPRELOAD_H
