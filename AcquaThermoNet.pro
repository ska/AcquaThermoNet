# AcquaThermoNet: open this file in Qt Creator or run qmake on it.
#
#   ThirdParty   Qt modules missing in the Qt in use (QtMqtt always,
#                QtSerialPort on desktop), static, from the git submodules
#                (first checkout: git submodule update --init)
#   app          the application -> <build>/bin_<arch>/AcquaThermoNet
#                (and the deployment package on ARM, see tools/package)
#
# Optional, qmake arguments:
#   CONFIG+=tests   unit tests  -> <build>/tests/tests
#   CONFIG+=tools   GUI screenshot tool (tools/guishot), desktop only

TEMPLATE = subdirs

SUBDIRS += ThirdParty app
app.depends = ThirdParty

CONFIG(tests) {
    SUBDIRS += tests
}

CONFIG(tools) {
    SUBDIRS += guishot
    guishot.subdir = tools/guishot
    guishot.depends = ThirdParty
}

OTHER_FILES += \
    .qmake.conf \
    setting.default.ini \
    ThirdParty/qtmodules.pri
