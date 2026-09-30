# Qt modules used by the application, from the Qt in use when it has them,
# otherwise built from the git submodules by ThirdParty.pro as static
# libraries (same version as the device Qt, see the submodule tags):
#
#   QtMqtt       not in any of the Qt used (desktop 5.13.2, device SDK):
#                always from ThirdParty/qtmqtt, static
#   QtSerialPort in the device Qt (SDK sysroot): used from there;
#                missing in the desktop Qt: from ThirdParty/qtserialport, static
#
# Include it in a project to link them; ThirdParty.pro must be built first
# (the top level AcquaThermoNet.pro takes care of the order).

ATN_THIRDPARTY_BUILD = $$ATN_BUILD/ThirdParty

qtHaveModule(serialport): ATN_BUILD_SERIALPORT = false
else:                     ATN_BUILD_SERIALPORT = true

# OpenSSL: Qt loads it at run time (dlopen). Qt < 5.15 needs OpenSSL 1.1,
# missing on recent desktop distributions (OpenSSL 3): on the desktop
# (x86_64) with Qt < 5.15 it is built from ThirdParty/openssl (1.1.1w,
# shared) and preloaded by the application at start (opensslpreload.cpp).
# The device SDK has its own OpenSSL 1.1. Development only: 1.1 is EOL.
ATN_BUILD_OPENSSL = false
equals(QT_ARCH, x86_64):equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 15): \
    ATN_BUILD_OPENSSL = true
ATN_OPENSSL_BUILD = $$ATN_THIRDPARTY_BUILD/openssl

# Consumer side (application, tools): skipped by ThirdParty.pro itself
!equals(TEMPLATE, aux) {
    QT += network

    INCLUDEPATH    += $$ATN_THIRDPARTY_BUILD/qtmqtt/include
    LIBS           += $$ATN_THIRDPARTY_BUILD/qtmqtt/lib/libQt5Mqtt.a
    PRE_TARGETDEPS += $$ATN_THIRDPARTY_BUILD/qtmqtt/lib/libQt5Mqtt.a

    equals(ATN_BUILD_SERIALPORT, true) {
        INCLUDEPATH    += $$ATN_THIRDPARTY_BUILD/qtserialport/include
        LIBS           += $$ATN_THIRDPARTY_BUILD/qtserialport/lib/libQt5SerialPort.a
        PRE_TARGETDEPS += $$ATN_THIRDPARTY_BUILD/qtserialport/lib/libQt5SerialPort.a

        # The static QtSerialPort calls libudev directly when the Qt in use
        # has the libudev feature (serialport-lib.pri: LINK_LIBUDEV), and a
        # static library does not bring its dependencies: same condition and
        # library as the module, read from the Qt qmodule.pri (not included:
        # only these two values are taken)
        ATN_QMODULE = $$[QT_HOST_DATA/get]/mkspecs/qmodule.pri
        exists($$ATN_QMODULE) {
            ATN_QT_FEATURES = $$fromfile($$ATN_QMODULE, QT.global_private.enabled_features)
            contains(ATN_QT_FEATURES, libudev): LIBS += $$fromfile($$ATN_QMODULE, QMAKE_LIBS_LIBUDEV)
        }
    } else {
        QT += serialport
    }

    equals(ATN_BUILD_OPENSSL, true) {
        DEFINES        += ATN_OPENSSL_DIR=\\\"$$ATN_OPENSSL_BUILD\\\"
        LIBS           += -ldl
        PRE_TARGETDEPS += $$ATN_OPENSSL_BUILD/libssl.so
    }
}
