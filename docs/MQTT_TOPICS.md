# MQTT topics and payloads

Reference of every MQTT topic used by **AcquaThermoNet** (the boiler room
controller), **RoomSense** (the BLE sensor gateway and house panel) and
**Home Assistant**: who publishes it, who reads it, the payload, QoS and
retain. The rules and the reasons behind them are in
[`SENSOR_GATEWAY_INTERFACE.md`](SENSOR_GATEWAY_INTERFACE.md) (the contract
with RoomSense) and [`DOCUMENTATION.md`](DOCUMENTATION.md) §5 (the
controller side): this page is the quick lookup.

---

## 1. Conventions

| Item | Rule |
|---|---|
| Protocol | MQTT 3.1.1, one broker for the three clients |
| `<zone>` | a zone name of AcquaThermoNet `[ZONES] list`, case sensitive, `[A-Za-z0-9_-]`; `list` and `mode` are reserved |
| Ownership | each value has one owner, which publishes its **state** retained; the others send **commands**, never retained, and wait for the state |
| States | retained, QoS 1, republished at every connection of the owner |
| Commands | **not** retained, QoS 1; the owner ignores a command delivered retained (logged `Retained message ignored on …`) |
| Readings | `RoomSense/<zone>/data`, not retained (a retained reading would count as fresh for a dead sensor); ignored when delivered retained |
| Availability | `<app>/status`, `online` at connect, `offline` as Will and at a clean exit, retained |
| Numbers | plain text payloads: a decimal number with `.` (`21.5`); JSON payloads: JSON numbers. `nan` and `inf` are never numbers |
| Unknown zone | a command or reading for a zone the controller does not have is ignored silently |
| Subscriptions | clean session: subscribed again at every connection. AcquaThermoNet: QoS 1 (commands and readings at least once, as they are sent), only what it reads; RoomSense: QoS 0 (retained states) |

Clients:

| Client | Client id | Keepalive | Will |
|---|---|---|---|
| AcquaThermoNet | `AcquaThermoNet-<unique_id>` | 10 s | `AcquaThermoNet/status` `offline` |
| RoomSense | `RoomSense-<unique_id>` (host name when empty) | 30 s | `RoomSense/status` `offline` |
| Home Assistant | its own | its own | `homeassistant/status` `offline` |

---

## 2. All topics

Pub. = publisher, Sub. = subscribers. ATN = AcquaThermoNet, RS =
RoomSense, HA = Home Assistant.

| Topic | Pub. | Sub. | Payload | Retain | QoS | § |
|---|---|---|---|---|---|---|
| `AcquaThermoNet/status` | ATN | RS, HA | `online` / `offline` | yes | 1 | 3.1 |
| `RoomSense/status` | RS | ATN | `online` / `offline` | yes | 1 | 3.1 |
| `homeassistant/status` | HA | ATN | `online` / `offline` | yes | – | 3.1 |
| `RoomSense/<zone>/data` | RS | ATN, HA | reading JSON | **no** | 1 | 3.2 |
| `AcquaThermoNet/<zone>/state_temp` | ATN | RS, HA | setpoint, `20.5` | yes | 1 | 3.3 |
| `AcquaThermoNet/<zone>/set_temp` | RS, HA | ATN | setpoint, `21.5` | **no** | 1 | 3.3 |
| `AcquaThermoNet/<zone>/state_mode` | ATN | RS, HA | `heat` / `off` | yes | 1 | 3.4 |
| `AcquaThermoNet/mode/state` | ATN | RS | house mode JSON | yes | 1 | 3.5 |
| `AcquaThermoNet/mode/set` | RS | ATN | `normal` / `window` / `away` / `boost` | **no** | 1 | 3.5 |
| `AcquaThermoNet/<zone>/chrono` | ATN | RS, HA | chrono state JSON | yes | 1 | 3.6 |
| `AcquaThermoNet/<zone>/chrono/set` | RS, HA | ATN | `on` / `off` | **no** | 1 | 3.6 |
| `AcquaThermoNet/<zone>/chrono/profile` | ATN | RS | chrono program JSON | yes | 1 | 3.7 |
| `AcquaThermoNet/<zone>/chrono/profile/set` | RS | ATN | chrono program JSON or `{"reset":true}` | **no** | 1 | 3.7 |
| `AcquaThermoNet/weather` | ATN | HA | outdoor weather JSON | yes | 1 | 3.8 |
| `homeassistant/climate/<zone>/config` | ATN | HA | discovery JSON | yes | 1 | 3.9 |
| `homeassistant/switch/<zone>_chrono/config` | ATN | HA | discovery JSON | yes | 1 | 3.9 |
| `homeassistant/sensor/<unique_id>_outdoor_<field>/config` | ATN | HA | discovery JSON, empty when the weather is off | yes | 1 | 3.9 |

AcquaThermoNet subscribes to its commands (`AcquaThermoNet/+/set_temp`,
`…/+/chrono/set`, `…/+/chrono/profile/set`, `AcquaThermoNet/mode/set`),
`RoomSense/+/data` and `RoomSense/status` at QoS 1, and
`homeassistant/status`: none of its own topics. RoomSense subscribes to
`AcquaThermoNet/status`, `AcquaThermoNet/+/state_temp`, `…/+/state_mode`,
`AcquaThermoNet/mode/state`, `AcquaThermoNet/+/chrono` and
`AcquaThermoNet/+/chrono/profile`. Nothing else is published under
`AcquaThermoNet/` by RoomSense, nor under `homeassistant/` by anyone but
AcquaThermoNet.

---

## 3. Topics

### 3.1 Availability

`AcquaThermoNet/status`, `RoomSense/status`: `online` at every
connection, `offline` as the Will (connection lost) and before a clean
disconnect. Retained, QoS 1.

- `AcquaThermoNet/status` is the `avty_t` of every Home Assistant entity;
  RoomSense sends no command while it is not `online` (and shows the last
  known values).
- `RoomSense/status`: the controller shows `RoomSense OFFLINE` in its
  status bar and, after `[TELEGRAM] gateway_down_min` (2 min: a restart is
  shorter), sends one Telegram alarm instead of waiting for a NO SENSOR
  per zone (15 min). The zones still follow the sensor timeout.
- `homeassistant/status` `online` (Home Assistant restarted): AcquaThermoNet
  publishes the discovery and all its states again.

### 3.2 Sensor reading

`RoomSense/<zone>/data`, published by RoomSense when its sensor
is heard: every `publish_interval_s` (60…300 s), earlier on a 0.1 °C
change, at most once every 10 s per zone; **nothing** when the sensor is
no longer heard (the controller detects the loss by 15 min of silence).
QoS 1, not retained.

```json
{"temperature":20.4,"humidity":48,"battery":85,"battmv":2950,"data_time":1790596805,"mac":"A4:C1:38:12:34:56"}
```

| Field | Required | Type | Range | Controller |
|---|---|---|---|---|
| `temperature` | **yes** | number (a numeric string is accepted) | −30…60 °C | out of range, missing or not a number: message refused (`Invalid sensor data for zone …`), the zone timer is not reset |
| `humidity` | no | number | 0…100 %RH | stored as integer |
| `battery` | no | number | 1…100 %; omitted when unknown | alarm below 20 %, cleared from 30 % |
| `battmv` | no | number | 0…65535 mV | stored |
| `data_time` | no | number | Unix epoch s of the reading | stored |
| `mac` | no | string | sensor address | stored |

A missing or out of range optional field keeps its previous value; extra
fields are ignored. RoomSense drops implausible readings before
publishing (−20…50 °C, a jump over 3 °C waits for a confirming reading).
Home Assistant reads the same topic for the current temperature
(`{{ value_json.temperature }}`).

### 3.3 Setpoint

| Topic | Payload | Notes |
|---|---|---|
| `AcquaThermoNet/<zone>/state_temp` | applied setpoint, `20` or `20.5` | after every `set_temp` (also unchanged or clamped), at every chrono slot, house mode change and connection. During a house mode: the mode value (e.g. 8) |
| `AcquaThermoNet/<zone>/set_temp` | number, `21.5` (not JSON) | rounded to 0.5, clamped to 5…25; not a number (also `nan`, `inf`): logged `Invalid setpoint for zone …`, ignored. During a house mode it changes the zone's own setpoint, restored at the end |

RoomSense shows the value as pending until `state_temp` arrives (5 s at
most) and sends one `set_temp` per zone per second at most.

### 3.4 Heat demand

`AcquaThermoNet/<zone>/state_mode`: `heat` while the zone asks for heat,
`off` when satisfied. Decided only by the regulation: there is no command
topic (Home Assistant shows it as the climate mode and cannot change it).

### 3.5 House mode

| Topic | Payload |
|---|---|
| `AcquaThermoNet/mode/set` | `normal`, `window`, `away` or `boost`; anything else logged (`Invalid house mode …`) and ignored |
| `AcquaThermoNet/mode/state` | `{"mode":"window","remaining_s":1740}`; `remaining_s` only for `window` and `boost` |

`mode/state` is published after every `mode/set` (also unchanged or
refused), at every change and every minute while `window` or `boost`
runs. `remaining_s` is a countdown (the clocks of the two panels may
differ). `window` while `away` is refused. Values: `window` min(own, 8 °C)
for 30 min, `away` min(own, 15 °C) until `normal`, `boost` max(own,
25 °C) for 30 min (AcquaThermoNet `[MODES]`).

### 3.6 Chrono state and on/off

`AcquaThermoNet/<zone>/chrono`, sent when it changes (checked at every
zone or mode change and every minute):

```json
{"chrono":"on","manual":false,"next_change":"22:30","next_change_at":"2026-10-03T22:30:00+02:00","next_setpoint":17,"paused":false,"profile":"weekday"}
```

| Field | Type | Meaning |
|---|---|---|
| `chrono` | string | `on` / `off`; `{"chrono":"off"}` is the whole payload when off |
| `paused` | bool | house mode `away`: chrono suspended |
| `manual` | bool | the setpoint is a manual change, until the next slot |
| `profile` | string | `weekday` / `holiday`, today's profile |
| `next_change` | string | `HH:MM` of the next slot (controller local time) |
| `next_change_at` | string | the same, ISO 8601 with the UTC offset |
| `next_setpoint` | number | setpoint of the next slot |

`profile` and the `next_…` fields are absent while the controller clock is
not set. It is also the state of the Home Assistant chrono switch and the
attributes of the zone climate.

`AcquaThermoNet/<zone>/chrono/set`: `on` / `off` (any case). `on` without
weekday slots is refused (logged), the chrono stays off; `chrono` is
published again after every command.

### 3.7 Chrono program

`AcquaThermoNet/<zone>/chrono/profile`, at connection, when the program
changes (from any panel or Home Assistant) and after every
`chrono/profile/set`, also when refused:

```json
{"edited":true,"enabled":true,"holiday":[{"at":"08:00","temp":20.5},{"at":"23:00","temp":17}],"holiday_days":[6,7],"weekday":[{"at":"06:30","temp":20.5},{"at":"22:30","temp":17}]}
```

| Field | Type | Meaning |
|---|---|---|
| `enabled` | bool | chrono on |
| `edited` | bool | edited on a panel or from Home Assistant; `false`: the program of the controller configuration |
| `holiday_days` | array of int | days of the holiday profile, 1 = Monday … 7 = Sunday (configuration only) |
| `weekday`, `holiday` | arrays of slots | sorted, 0…8 slots; empty `holiday`: the weekday profile on holidays |
| slot | `{"at":"HH:MM","temp":20.5}` | from `at` (local time) the setpoint `temp`, 5…25 °C, step 0.5 |

`AcquaThermoNet/<zone>/chrono/profile/set`, the whole program (nothing is
merged) or a reset:

```json
{"enabled":true,"weekday":[{"at":"06:30","temp":20.5},{"at":"22:30","temp":17}],"holiday":[]}
```
```json
{"reset":true}
```

`enabled`, `weekday` and `holiday` are required; `holiday_days` is
ignored. Slots: `at` exactly `HH:MM` 00:00…23:59, no time twice, `temp` a
JSON number 5…25 (rounded to 0.5), at most 8, any order; `enabled` true
needs a weekday slot. Anything invalid refuses the whole command, logged
with the reason (`Invalid chrono program for zone …`).

### 3.8 Outdoor weather

`AcquaThermoNet/weather`, at every new reading of the weather service
(only when it is enabled):

```json
{"humidity":90,"location":"Oslo","precipitation":0,"pressure":1026,"source":"met.no","temperature":14.3,"wind_speed":1.8}
```

°C, %, hPa, m/s, mm (met.no: next hour); `source` is the provider
(attribution).

### 3.9 Home Assistant discovery

Published by AcquaThermoNet at every connection and when
`homeassistant/status` becomes `online`, retained, QoS 1. Full examples
and the meaning of each key: [`DOCUMENTATION.md`](DOCUMENTATION.md) §5.3.

| Topic | Entity | Reads | Commands |
|---|---|---|---|
| `homeassistant/climate/<zone>/config` | climate `clima.<zone>` | `curr_temp_t` the reading (3.2), `temp_stat_t` `state_temp`, `mode_stat_t` `state_mode`, `json_attr_t` `chrono` | `temp_cmd_t` `set_temp` |
| `homeassistant/switch/<zone>_chrono/config` | switch `chrono.<zone>` | `stat_t` `chrono` (`{{ value_json.chrono }}`) | `cmd_t` `chrono/set` |
| `homeassistant/sensor/<unique_id>_outdoor_<field>/config` | 5 outdoor sensors (`temperature`, `humidity`, `pressure`, `wind_speed`, `precipitation`) | `AcquaThermoNet/weather` | – |

All have `avty_t` = `AcquaThermoNet/status`. With the weather disabled the
outdoor sensor topics are published empty, which removes the entities.

---

## 4. Retained messages left on the broker

Retained states stay on the broker until replaced. After renaming or
removing a zone, or after a command was published retained by mistake
(log `Retained message ignored on …`), clear the topic with an empty
retained message:

```sh
mosquitto_pub -h <broker> -u <user> -P <password> -r -n -t AcquaThermoNet/<old zone>/state_temp
```

For a removed zone: `state_temp`, `state_mode`, `chrono`,
`chrono/profile` under `AcquaThermoNet/<zone>/`, and
`homeassistant/climate/<zone>/config`,
`homeassistant/switch/<zone>_chrono/config` (an empty discovery removes
the entity from Home Assistant). To see what is retained:
`mosquitto_sub -v -t '#' --retained-only` (mosquitto 2.0 or later).

Commands to try every topic by hand:
[`SENSOR_GATEWAY_INTERFACE.md`](SENSOR_GATEWAY_INTERFACE.md) §10.
