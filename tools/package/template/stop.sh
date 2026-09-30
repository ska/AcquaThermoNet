#!/bin/sh
# Stops AcquaThermoNet with SIGTERM: the application switches the relays
# OFF (confirmed by reading the board back, max 5s), publishes MQTT
# "offline" and exits 0, which also ends the start.sh restart loop.
# Waits for the exit so that an update never replaces a running binary;
# after 15s the process is killed.

pkill -TERM -x AcquaThermoNet 2>/dev/null || exit 0

i=0
while pidof AcquaThermoNet >/dev/null 2>&1; do
    i=$((i + 1))
    if [ $i -gt 15 ]; then
        echo "AcquaThermoNet: not stopped after 15s, killing it" | logger -t AcquaThermoNet
        pkill -KILL -x AcquaThermoNet 2>/dev/null
        break
    fi
    sleep 1
done
exit 0
