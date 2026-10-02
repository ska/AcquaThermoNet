#!/bin/sh
# Entry point invoked by the launcher (JMLauncher). Runs deploy/start.sh
# (the restart loop around the application) in a subshell and does the
# launcher's pid-file and dbus bookkeeping around it, as the reference
# Qt HMI package and CanGateway do. The pid file name is specific to this
# package so it cannot collide with other runtimes on the same device.

cd "$(dirname "$0")"

# D-Bus name of the panel launcher, looked up on the system bus
launcher=$(dbus-send --system --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.ListNames 2>/dev/null | grep -o '"[^"]*\.JMLauncher"' | tr -d '"' | head -n 1)

[ -e /etc/default/rcS ] && . /etc/default/rcS
export FASTBOOT

PIDFILE=/var/run/acquathermonet-run.pid

PGID=$(awk '{print $5}' /proc/$$/stat)
echo $PGID > $PIDFILE # save pid for termination
(
    # export required variables (not available if started by jmlauncher daemon)
    export USER=$(busybox whoami)
    export HOME=$(eval echo ~$USER)
    echo "Starting AcquaThermoNet as user \"$USER\" with home \"$HOME\""
    # the boot splash of the panel must not stay over the HMI
    killall xsplash 2>/dev/null
    ./deploy/start.sh "$@"
    if [ -z "$FASTBOOT" ] && (pidof jmlauncher >/dev/null 2>&1); then
        dbus-send --print-reply --system --dest="$launcher" '/' "$launcher".appFinished string:"AcquaThermoNet"
    fi
    PID="$(cat $PIDFILE)"
    rm -f $PIDFILE
    pkill -TERM -s "$PID" # kill all child spawned processes if any
) &

# ensure the boot splash is gone (as the reference panel packages do)
killall xsplash 2>/dev/null

# wait for the application to come up
sleep 5

if pidof AcquaThermoNet > /dev/null; then
    if [ -z "$FASTBOOT" ]; then
        dbus-send --print-reply --system --dest="$launcher" '/' "$launcher".appLoaded string:"AcquaThermoNet"
    fi
fi
