#!/bin/sh
# The launcher's update is an uninstall of the old version followed by an
# install of the new one (as found for CanGateway on the same devices), so
# everything not in the package is lost: save the device configuration
# (setting.ini), the application state (state.ini: setpoints, MQTT
# unique_id, relay activations for the valve exercise) and log/ (relay
# activity for the statistics). install.sh restores them.

cd "$(dirname "$0")"
BACKUP=/tmp/acquathermonet-backup

./stop.sh

rm -rf "$BACKUP"
mkdir -p "$BACKUP"
for f in setting.ini state.ini; do
    [ -f "deploy/$f" ] && cp -p "deploy/$f" "$BACKUP/"
done
[ -d deploy/log ] && cp -rp deploy/log "$BACKUP/"
sync

echo "AcquaThermoNet uninstall: configuration, state and log saved to $BACKUP" | logger -t AcquaThermoNet
exit 0
