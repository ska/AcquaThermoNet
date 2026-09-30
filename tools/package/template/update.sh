#!/bin/sh
# The launcher does updates as uninstall + install (uninstall.sh saves and
# install.sh restores the configuration, state and log): nothing to do.
echo "AcquaThermoNet update: nothing to do" | logger -t AcquaThermoNet
dbus-send --print-reply --system --dest=com.exor.JMLauncher '/' com.exor.JMLauncher.updateFinished int32:0 string:""
exit 0
