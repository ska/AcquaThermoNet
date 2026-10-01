# Renders the main window with sample zone states to a PNG, no display needed.
# Built by the top level project with CONFIG+=tools (needs ThirdParty):
#   qmake AcquaThermoNet.pro CONFIG+=tools && make
#   QT_QPA_PLATFORM=offscreen tools/guishot/guishot setting.ini gui.png

QT       += core gui network widgets
CONFIG   += c++17
TARGET    = guishot

INCLUDEPATH += $$ATN_SRC
include($$ATN_SRC/ThirdParty/qtmodules.pri)
include($$ATN_SRC/version.pri)

SOURCES += \
    guishot.cpp \
    $$ATN_SRC/configuration.cpp \
    $$ATN_SRC/logging.cpp \
    $$ATN_SRC/mainwindow.cpp \
    $$ATN_SRC/monoclock.cpp \
    $$ATN_SRC/mqtt.cpp \
    $$ATN_SRC/mqttparse.cpp \
    $$ATN_SRC/netinfo.cpp \
    $$ATN_SRC/zonecard.cpp \
    $$ATN_SRC/zonemodel.cpp

HEADERS += \
    $$ATN_SRC/mainwindow.h \
    $$ATN_SRC/mqtt.h \
    $$ATN_SRC/zonecard.h \
    $$ATN_SRC/zonemodel.h

FORMS     += $$ATN_SRC/mainwindow.ui
RESOURCES += $$ATN_SRC/resources.qrc
