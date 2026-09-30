#!/bin/bash
#
# End to end test on the development PC: real application, minimal MQTT
# broker and Modbus relay board simulator (tools/minibroker.py, tools/mbsim.py).
#
#   tools/e2e.sh PATH/TO/AcquaThermoNet
#
# (e.g. <build>/bin_x86_64/AcquaThermoNet). No other AcquaThermoNet may be
# running on this PC (single instance). Exit code 0 when every check passes.
#
set -u

TOOLS=$(cd "$(dirname "$0")" && pwd)
APP=$(realpath "${1:?usage: $0 PATH/TO/AcquaThermoNet}")
PORT=18830
WORK=$(mktemp -d)
FAILS=0
PIDS=()

cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; rm -rf "$WORK"; }
trap cleanup EXIT

check() {   # check DESCRIPTION FILE PATTERN
    if grep -qE "$3" "$2"; then echo "PASS  $1"; else echo "FAIL  $1"; FAILS=$((FAILS+1)); fi
}
pub() { python3 "$TOOLS/minibroker.py" pub $PORT "$1" "$2"; }

cd "$WORK"
python3 "$TOOLS/minibroker.py" $PORT broker.log & PIDS+=($!)
python3 "$TOOLS/mbsim.py" sim.log > pty.txt & PIDS+=($!)
python3 "$TOOLS/minitelegram.py" $((PORT + 60)) 123:E2ETOKEN telegram.log & PIDS+=($!)
for _ in $(seq 25); do [ -s pty.txt ] && break; sleep 0.2; done

cat > setting.ini <<EOF
[MQTT]
broker_addr=127.0.0.1
broker_port=$PORT
unique_id=E2E

[ZONES]
list=salotto, camera
salotto\\setpoint=17
camera\\setpoint=20

[RELAY]
salotto\\relaynum=5
camera\\relaynum=4

[SERIAL]
port=$(cat pty.txt)

[REGULATION]
min_cycle_s=0

[LOG]
file=log/app.log

[RELAY_LOG]
file=log/relays.csv

[TELEGRAM]
enabled=true
token=123:E2ETOKEN
allowed_chats=111
name=E2E
api_url=http://127.0.0.1:$((PORT + 60))
EOF

QT_QPA_PLATFORM=offscreen "$APP" > app.log 2>&1 &
APP_PID=$!
sleep 3

pub RoomSense/apartment/salotto/data '{"temperature":"15.0","humidity":"50","battery":"90"}'
sleep 2
pub AcquaThermoNet/salotto/set_temp 14
sleep 2
pub AcquaThermoNet/camera/set_temp 30
sleep 2
python3 "$TOOLS/minitelegram.py" inject $((PORT + 60)) 111 "/status"
python3 "$TOOLS/minitelegram.py" inject $((PORT + 60)) 999 "/status"
sleep 2

kill -TERM $APP_PID
wait $APP_PID
RC=$?
sleep 0.5

echo "--- startup"
check "discovery for 2 zones"          broker.log "homeassistant/climate/camera/config"
check "availability online"            broker.log "AcquaThermoNet/status online"
check "all relays OFF at startup"      sim.log    "#1 write relay 5 OFF"
echo "--- regulation"
check "15 < 17-0.5: relay 5 ON"        sim.log    "write relay 5 ON"
check "state_mode heat published"      broker.log "salotto/state_mode heat"
check "set_temp 14: state_temp 14"     broker.log "salotto/state_temp 14 "
check "set_temp 14: state_mode off"    broker.log "salotto/state_mode off"
check "set_temp 30 clamped to 25"      broker.log "camera/state_temp 25 "
echo "--- shutdown"
check "relays confirmed OFF"           app.log    "All relays confirmed OFF"
check "availability offline"           broker.log "AcquaThermoNet/status offline"
check "setpoints saved in state.ini"   state.ini   "salotto.setpoint=14"
check "setting.ini not written"        setting.ini "salotto.setpoint=17"
check "log file written"               log/app.log "atn.regulation"
check "relay log: ON by regulation"    "$(ls log/relays-*.csv)" ",5,salotto,ON,regulation,"
check "relay log: OFF with duration"   "$(ls log/relays-*.csv)" ",5,salotto,OFF,regulation,[0-9]+$"
echo "--- telegram"
check "start message"                  telegram.log '"chat_id": 111, "text": ".*E2E .*started'
check "/status answered"               telegram.log 'Salotto: 15\.0.{1,6} set 14\.0'
check "stop message"                   telegram.log 'stopping \(clean shutdown'
check "chat not allowed ignored"       app.log      'not allowed, ignored: chat id 999'
if grep -q E2ETOKEN app.log log/app.log; then echo "FAIL  token not in the logs"; FAILS=$((FAILS+1)); else echo "PASS  token not in the logs"; fi
cp log/relays-*.csv "${E2E_KEEP_RELAYLOG:-/dev/null}" 2>/dev/null
if [ $RC -eq 0 ]; then echo "PASS  exit code 0"; else echo "FAIL  exit code $RC"; FAILS=$((FAILS+1)); fi

if [ $FAILS -ne 0 ]; then
    echo "$FAILS check(s) failed, application log:"
    cat app.log
    exit 1
fi
echo "all checks passed"
