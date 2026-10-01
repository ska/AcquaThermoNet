# Unit tests (QtTest), no hardware and no broker needed:
#   mkdir build-tests && cd build-tests
#   qmake ../tests/tests.pro && make && ./tests

QT       += core network testlib
QT       -= gui
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle
TARGET    = tests

SRC = $$PWD/..
INCLUDEPATH += $$SRC
include($$SRC/version.pri)

SOURCES += \
    main.cpp \
    tst_config.cpp \
    tst_exercise.cpp \
    tst_frost.cpp \
    tst_logging.cpp \
    tst_modbus.cpp \
    tst_monoclock.cpp \
    tst_mqttparse.cpp \
    tst_netinfo.cpp \
    tst_relaylog.cpp \
    tst_telegram.cpp \
    tst_regulation.cpp \
    tst_weather.cpp \
    tst_zonemodel.cpp \
    $$SRC/configuration.cpp \
    $$SRC/logging.cpp \
    $$SRC/modbusframe.cpp \
    $$SRC/modbusframeprocessor.cpp \
    $$SRC/monoclock.cpp \
    $$SRC/mqttparse.cpp \
    $$SRC/netinfo.cpp \
    $$SRC/relaylog.cpp \
    $$SRC/telegramnotifier.cpp \
    $$SRC/termoregolazione.cpp \
    $$SRC/valveexercise.cpp \
    $$SRC/weather.cpp \
    $$SRC/zonemodel.cpp

HEADERS += \
    testutil.h \
    $$SRC/modbusframeprocessor.h \
    $$SRC/termoregolazione.h \
    $$SRC/telegramnotifier.h \
    $$SRC/valveexercise.h \
    $$SRC/weather.h \
    $$SRC/zonemodel.h
