# Telegram alarms and status

AcquaThermoNet talks directly to the Telegram Bot API (HTTPS, long
polling): no inbound port, no Home Assistant needed, works as long as the
device reaches the internet.

## Setup

1. In Telegram, talk to **@BotFather**: `/newbot`, choose a name; it gives
   the bot **token** (`123456:ABC...`).
2. In `setting.ini` on the device:
   ```ini
   [TELEGRAM]
   enabled=true
   token=123456:ABC...
   allowed_chats=
   name=Casa
   ```
   and restart the application.
3. Write anything to the bot. The message is ignored (the chat is not
   allowed yet), but its **chat id** is in the log:
   `Telegram message from a chat not allowed, ignored: chat id 12345678 ...`
4. Put the id in `allowed_chats` (several: comma separated; a group chat
   id is negative) and restart. `/help` lists the commands.

Only the allowed chats are answered and notified; the token gives full
control of the bot, keep `setting.ini` private (plain text, like the MQTT
password).

## Messages

Alarms are sent when a problem starts and when it ends; the ones still
active are repeated every `reminder_h` hours (0 = off). Every message
carries the time of the event: without network they are queued and sent
when Telegram is reachable again.

| Event | |
|---|---|
| zone sensor lost / back | alarm |
| relay does not follow the commands / OK again | alarm |
| frost mode on / off | alarm |
| sensor battery low (below 20%, OK again from 30%) | alarm |
| Modbus relay board offline / online | alarm |
| serial port lost / open again | alarm |
| MQTT broker not connected for `mqtt_down_min` minutes / back | alarm |
| RoomSense (the sensor gateway) offline for `gateway_down_min` minutes / online again | alarm |
| window open? / closed? guessed from the temperature of a zone (`[WINDOW_DETECTION]`) | information |
| start, with a warning if the previous run did not stop cleanly (crash, power loss, watchdog) | info |
| clean stop | info |

## Commands

| | |
|---|---|
| `/status` | all zones (temperature, applied setpoint, heating, relay), MQTT, Modbus, outdoor, house mode, active alarms |
| `/zone <name>` | one zone: last reading and its age, battery, relay, pending switch, alarms, heating time today |
| `/today` | relay ON time per zone since midnight (from the relay log) |
| `/week` | relay ON time per zone in the last 7 days |
| `/help` | commands and zone names |

Commands are read only. `/today` and `/week` need `[RELAY_LOG]`.

## Testing without Telegram

`tools/minitelegram.py` is a local Bot API server (`api_url` in
`setting.ini`); `tools/e2e.sh` uses it. `api_url` is only for tests.

## Desktop with Qt 5.13

Qt 5.13 needs OpenSSL 1.1 for HTTPS, recent distributions have only
OpenSSL 3 (`SSL handshake failed`). With a Qt < 5.15 on x86_64 the build
compiles OpenSSL 1.1.1w from `ThirdParty/openssl` and the application
preloads it at start (log: `OpenSSL 1.1 preloaded from ...`). Development
only: OpenSSL 1.1 is end of life. The device uses the SDK OpenSSL.
