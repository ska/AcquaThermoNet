# AcquaThermoNet application. Sources are in the repository root;
# built through the top level AcquaThermoNet.pro (ThirdParty first).

QT       += core gui network widgets
CONFIG   += c++17
TARGET    = AcquaThermoNet

CONFIG(release, debug|release):DEFINES += QT_NO_DEBUG_OUTPUT
CONFIG(debug, debug|release):message( "Debug build." )
CONFIG(release, debug|release):message( "Release build." )

DEFINES += DESKTOP=1
DEFINES += DISPLAY32=2
DEFINES += DISPLAY64=3

message("Build Arch: " $${QT_ARCH})

INCLUDEPATH += $$ATN_SRC
include($$ATN_SRC/ThirdParty/qtmodules.pri)
include($$ATN_SRC/version.pri)

RESOURCES = $$ATN_SRC/resources.qrc

SOURCES += \
    $$ATN_SRC/configuration.cpp \
    $$ATN_SRC/logging.cpp \
    $$ATN_SRC/main.cpp \
    $$ATN_SRC/mainwindow.cpp \
    $$ATN_SRC/modbusframe.cpp \
    $$ATN_SRC/modbusframeprocessor.cpp \
    $$ATN_SRC/monoclock.cpp \
    $$ATN_SRC/mqtt.cpp \
    $$ATN_SRC/mqttparse.cpp \
    $$ATN_SRC/netinfo.cpp \
    $$ATN_SRC/opensslpreload.cpp \
    $$ATN_SRC/relaylog.cpp \
    $$ATN_SRC/serialuart.cpp \
    $$ATN_SRC/singleinstance.cpp \
    $$ATN_SRC/telegrambot.cpp \
    $$ATN_SRC/telegramnotifier.cpp \
    $$ATN_SRC/termoregolazione.cpp \
    $$ATN_SRC/valveexercise.cpp \
    $$ATN_SRC/watchdog.cpp \
    $$ATN_SRC/weather.cpp \
    $$ATN_SRC/zonecard.cpp \
    $$ATN_SRC/zonemodel.cpp

HEADERS += \
    $$ATN_SRC/climatezones.h \
    $$ATN_SRC/configuration.h \
    $$ATN_SRC/logging.h \
    $$ATN_SRC/mainwindow.h \
    $$ATN_SRC/modbusframe.h \
    $$ATN_SRC/modbusframeprocessor.h \
    $$ATN_SRC/monoclock.h \
    $$ATN_SRC/mqtt.h \
    $$ATN_SRC/mqttparse.h \
    $$ATN_SRC/netinfo.h \
    $$ATN_SRC/opensslpreload.h \
    $$ATN_SRC/relaylog.h \
    $$ATN_SRC/common.h \
    $$ATN_SRC/serialuart.h \
    $$ATN_SRC/singleinstance.h \
    $$ATN_SRC/telegrambot.h \
    $$ATN_SRC/telegramnotifier.h \
    $$ATN_SRC/termoregolazione.h \
    $$ATN_SRC/valveexercise.h \
    $$ATN_SRC/watchdog.h \
    $$ATN_SRC/weather.h \
    $$ATN_SRC/zonecard.h \
    $$ATN_SRC/zonemodel.h

FORMS += \
    $$ATN_SRC/mainwindow.ui

####################################
## Desktop x64
####################################
equals(QT_ARCH, "x86_64") {
    DEFINES += DEVICE=DESKTOP
    INST_PATH=/tmp

    message(Compile for DESKTOP)
}

####################################
## i5/i7 Arm32
####################################
equals(QT_ARCH, "arm") {
    DEFINES += DEVICE=DISPLAY32
    INST_PATH=/mnt/data/hmi/AcquaThermoNet/

    message(Compile for ARM32)
}

message("Instal PATH: " $${INST_PATH})

# Default rules for deployment.
target.path = $$INST_PATH/
!isEmpty(target.path): INSTALLS += target
DESTDIR = $$ATN_BUILD/bin_$${QT_ARCH}


##############################################################
# Default config file
##############################################################
##  local build dir
QMAKE_POST_LINK += $$quote($(COPY_FILE) $${ATN_SRC}/setting.default.ini $$DESTDIR ;)
## Install to remote target as a template: the device setting.ini is never overwritten
conf_file.path = $$INST_PATH/
conf_file.files += $${ATN_SRC}/setting.default.ini
INSTALLS += conf_file

##############################################################
# Deployable package (tools/package/README.md)
# Built after every link of an ARM build; desktop builds only with
# CONFIG+=package, ARM builds can skip it with CONFIG+=nopackage.
# Output: <build dir>/dist/AcquaThermoNet_Package_<arch>_<version>.zip
##############################################################
PKG_ARCH = $${QT_ARCH}
equals(QT_ARCH, "arm"):     PKG_ARCH = Arm32
equals(QT_ARCH, "arm64"):   PKG_ARCH = Arm64
if(equals(QT_ARCH, "arm")|equals(QT_ARCH, "arm64")|CONFIG(package)):!CONFIG(nopackage) {
    # $(STRIP) / $(CXX): the toolchain of this Makefile (the strip is searched
    # next to the compiler when the SDK environment is not loaded, Qt Creator)
    QMAKE_POST_LINK += $$quote($${ATN_SRC}/tools/package/make_package.sh --bin-dir $$DESTDIR --arch $$PKG_ARCH --out-dir $$ATN_BUILD/dist --strip \"$(STRIP)\" --cxx \"$(CXX)\" ;)
    message("Package: $$ATN_BUILD/dist ($$PKG_ARCH)")
}
