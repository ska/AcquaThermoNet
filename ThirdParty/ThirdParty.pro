# Builds the Qt modules from the git submodules as static libraries, with
# the qmake of the Qt in use (desktop or cross SDK), into
# <build>/ThirdParty/<module>. Only when the library is missing: after
# changing a submodule, delete <build>/ThirdParty (or run a clean).
# See qtmodules.pri for which module comes from where.
#
# First checkout: git submodule update --init

TEMPLATE = aux
include(qtmodules.pri)

# $$1 = submodule dir (qtmqtt), $$2 = library (Qt5Mqtt)
defineTest(atnBuildModule) {
    src = $$PWD/$$1
    out = $$ATN_THIRDPARTY_BUILD/$$1
    lib = $$out/lib/lib$${2}.a
    !exists($$src/$${1}.pro): \
        error("ThirdParty/$$1 is empty: run 'git submodule update --init'")

    name = build_$$1
    $${name}.target   = $$lib
    $${name}.commands = mkdir -p $$out && cd $$out && \
        $$QMAKE_QMAKE $$src/$${1}.pro CONFIG+=static CONFIG+=release CONFIG-=debug_and_release && \
        $(MAKE) sub-src
    export($${name}.target)
    export($${name}.commands)
    QMAKE_EXTRA_TARGETS += $$name
    PRE_TARGETDEPS += $$lib
    export(QMAKE_EXTRA_TARGETS)
    export(PRE_TARGETDEPS)
    return(true)
}

atnBuildModule(qtmqtt, Qt5Mqtt)
equals(ATN_BUILD_SERIALPORT, true): atnBuildModule(qtserialport, Qt5SerialPort)

# OpenSSL 1.1 shared libraries only (no apps, no tests, no docs), out of tree
equals(ATN_BUILD_OPENSSL, true) {
    !exists($$PWD/openssl/config): \
        error("ThirdParty/openssl is empty: run 'git submodule update --init'")
    build_openssl.target   = $$ATN_OPENSSL_BUILD/libssl.so
    build_openssl.commands = mkdir -p $$ATN_OPENSSL_BUILD && cd $$ATN_OPENSSL_BUILD && \
        $$PWD/openssl/Configure atn-linux-x86_64 --config=$$PWD/openssl.conf shared no-tests && \
        $(MAKE) build_libs
    QMAKE_EXTRA_TARGETS += build_openssl
    PRE_TARGETDEPS += $$build_openssl.target
}
