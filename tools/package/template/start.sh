#!/bin/sh
# Starts AcquaThermoNet and restarts it if it crashes. Stays in the
# foreground, so the launcher's supervision of start.sh still applies.
#
# Exit status of the application (see common.h):
#   0  stopped on purpose (SIGTERM/SIGINT, clean shutdown with relays OFF)
#   3  another instance already runs on this device
#   anything else, or killed by a signal: crash -> restart
# Restart delay grows 2, 4, 8, 16, 20s while it keeps crashing within a
# minute, and goes back to 2s after a run of at least a minute. The delay
# stays short: the hardware watchdog armed by the crashed instance keeps
# running until the new one refreshes it.

cd "$(dirname "$0")"
SCRIPTDIR=$(pwd)

[ -z "$DISPLAY" ] && export DISPLAY=:0
[ -z "$QT_QPA_PLATFORM" ] && [ -e /tmp/.X0-lock ] && export QT_QPA_PLATFORM=xcb
[ -e "$SCRIPTDIR/plugin/platforminputcontexts/libdbusvirtualkeyboardplugin.so" ] && export QT_IM_MODULE=dbusvirtualkeyboard

export LD_LIBRARY_PATH="$SCRIPTDIR:$SCRIPTDIR/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$SCRIPTDIR/plugin:$SCRIPTDIR:$QT_PLUGIN_PATH"
export PATH="$SCRIPTDIR:$PATH"

ulimit -c unlimited
# reduce stack size
ulimit -s 1024

delay=2
while true; do
    started=$(date +%s)
    ./AcquaThermoNet "$@"
    status=$?

    case "$status" in
        0) echo "AcquaThermoNet: stopped" | logger -t AcquaThermoNet; break ;;
        3) echo "AcquaThermoNet: another instance is running, not restarting" | logger -t AcquaThermoNet; break ;;
    esac

    if [ $(( $(date +%s) - started )) -ge 60 ]; then
        delay=2
    fi
    echo "AcquaThermoNet: exited with status $status, restarting in ${delay}s" | logger -t AcquaThermoNet
    sleep $delay
    delay=$(( delay * 2 ))
    [ $delay -gt 20 ] && delay=20
done
