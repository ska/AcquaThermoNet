# RoomSense → AcquaThermoNet: MQTT interface specification

This document is for the developer of **RoomSense**, the sensor gateway:
the application that receives the BLE room sensors and publishes their
readings over MQTT (formerly named Thermonator).
It is the complete contract with **AcquaThermoNet**, the heating controller
that drives the boiler room (pumps and zone valves) from those readings.
You do not need the AcquaThermoNet source code: everything the gateway must
do is here.

RoomSense also has a panel in the house: it shows the zones and changes
their setpoints, always through AcquaThermoNet, which owns them (§7), and
switches the whole house to the "windows open", "away" or "boost" mode
(§8), and shows and edits the chrono thermostat program of each zone
(§9).

Interface version: matches AcquaThermoNet **2.4.0**. The readings are
unchanged since 2.1.0; the setpoint commands of §7 use topics the
controller handles since 2.2.2; the house mode of §8 needs 2.3.0; the
chrono thermostat of §9 needs 2.4.0 (it also changes the setpoints on its
own, §7).

---

## 1. Roles

```mermaid
flowchart LR
    subgraph House
        BLE1["BLE sensor<br/>living room"]
        BLE2["BLE sensor<br/>bedroom"]
        BLEn["BLE sensor ..."]
        GW["RoomSense<br/>sensor gateway"]
    end
    B[("MQTT broker")]
    ATN["AcquaThermoNet<br/>boiler room HMI"]
    HA["Home Assistant"]
    PLANT["Pumps and<br/>zone valves"]

    BLE1 & BLE2 & BLEn -- "BLE advertising" --> GW
    GW -- "RoomSense/apartment/ZONE/data<br/>AcquaThermoNet/ZONE/set_temp<br/>AcquaThermoNet/mode/set<br/>ZONE/chrono/set, ZONE/chrono/profile/set" --> B
    B -- "AcquaThermoNet/status, ZONE/state_temp, ZONE/state_mode<br/>mode/state, ZONE/chrono, ZONE/chrono/profile" --> GW
    B <--> ATN
    B --> HA
    ATN -- "Modbus RTU" --> PLANT
```

| | RoomSense (sensor gateway) | AcquaThermoNet |
|---|---|---|
| Owns | BLE scanning, decoding, sensor → zone mapping, filtering; the house panel | zones, **setpoints** (the only source of truth), **house mode** (saved setpoints, timer, restore), **chrono programs**, regulation, relays |
| Publishes | one reading per zone on `RoomSense/apartment/<zone>/data`; setpoint commands on `AcquaThermoNet/<zone>/set_temp` (§7); mode commands on `AcquaThermoNet/mode/set` (§8); chrono commands on `AcquaThermoNet/<zone>/chrono/set` and `…/chrono/profile/set` (§9) | its own topics under `AcquaThermoNet/…` |
| Subscribes | `AcquaThermoNet/status`, `AcquaThermoNet/<zone>/state_temp`, `…/state_mode` (§7), `AcquaThermoNet/mode/state` (§8), `AcquaThermoNet/<zone>/chrono`, `…/chrono/profile` (§9) | `RoomSense/apartment/#`, `AcquaThermoNet/#` |

The two applications never talk directly: only through the broker. For
the readings there is no request/response, no acknowledgement: the gateway
publishes, the controller consumes. For a setpoint the confirmation is the
controller's retained `state_temp` (§7). **Home Assistant reads the same
reading topic** to show the current temperature of each zone (its climate
entities point to it), and sends setpoints the same way as the panel.

---

## 2. Contract summary

| Item | Value |
|---|---|
| Protocol | MQTT 3.1.1, same broker as AcquaThermoNet (plain 1883 or TLS 8883) |
| Topic | `RoomSense/apartment/<zone>/data` (fixed prefix, case sensitive) |
| `<zone>` | exactly a zone name configured in AcquaThermoNet (`[ZONES] list`), charset `[A-Za-z0-9_-]` |
| Payload | UTF-8 JSON object, one reading |
| Required field | `temperature` (°C) |
| Optional fields | `humidity`, `battery`, `battmv`, `data_time`, `mac`; extra fields are ignored |
| QoS | 0 or 1 (the controller subscribes at QoS 0) |
| Retain | **false** (see §6) |
| Rate | every 60–300 s per zone; **never** less often than every 15 min |
| Stale data | **stop publishing** a zone whose sensor is no longer heard |
| Setpoint commands | `AcquaThermoNet/<zone>/set_temp`, plain number, retain false (§7) |
| Controller state read | `AcquaThermoNet/status`, `…/<zone>/state_temp`, `…/<zone>/state_mode`, all retained (§7) |
| House mode | command `AcquaThermoNet/mode/set` (`normal`/`window`/`away`/`boost`), state `AcquaThermoNet/mode/state` JSON, retained (§8) |
| Chrono thermostat | commands `AcquaThermoNet/<zone>/chrono/set` (`on`/`off`) and `…/chrono/profile/set` (JSON); state `…/chrono` and program `…/chrono/profile`, JSON, retained (§9) |

---

## 3. Topic

```
RoomSense/apartment/<zone>/data
```

- `RoomSense/apartment` is a fixed prefix in AcquaThermoNet: use it verbatim.
- `<zone>` must match, **case sensitive**, one of the zone names configured
  in the controller. Today's installation uses:

  | Zone | Room |
  |---|---|
  | `salotto` | living room |
  | `ingresso` | entrance |
  | `cameretta` | small bedroom |
  | `camera` | bedroom |
  | `bagno` | bathroom |

  `mode` is reserved (house mode topics, §8): never a zone name.

  Confirm the list with whoever configures the controller: the zone names
  are the only shared configuration between the two applications. Make
  them a configuration item of the gateway (sensor MAC → zone name), never
  hard-coded.
- The last level must be `data`. Anything else under
  `RoomSense/apartment/` is ignored by the controller, as are zone names it
  does not know (silently).

**One topic = one zone = one temperature.** If a zone has several sensors,
the gateway decides which value to send (one chosen sensor, average, …):
the controller treats every message on the topic as the room temperature.

---

## 4. Payload

### 4.1 Example

```json
{
  "temperature": 20.4,
  "humidity": 48,
  "battery": 85,
  "battmv": 2950,
  "data_time": 1790596805,
  "mac": "A4:C1:38:12:34:56"
}
```

Minimal valid message:

```json
{"temperature": 20.4}
```

### 4.2 Fields

| Field | Required | JSON type | Unit / range | Used by the controller for |
|---|---|---|---|---|
| `temperature` | **yes** | number, or string with a number (`"20.4"`) | °C, one decimal is enough | regulation, display, Home Assistant |
| `humidity` | no | number or numeric string | %RH, 0–100, stored as integer (decimals dropped) | display |
| `battery` | no | number or numeric string | %, 1–100, integer | display, low battery alarm |
| `battmv` | no | number or numeric string | mV, 0–65535 | stored only |
| `data_time` | no | number or numeric string | Unix epoch seconds (UTC) of the reading | stored only (see §5) |
| `mac` | no | string | sensor address | stored only |

Rules applied by the controller:

- The message must be a **JSON object** with a valid `temperature`,
  otherwise it is rejected and logged (`Invalid sensor data for zone …`).
  A rejected message does **not** refresh the zone.
- Numbers may be JSON numbers or strings containing a number (both
  `20.4` and `"20.4"` work). Prefer JSON numbers.
- An optional field that is missing keeps its previous value. Send all the
  fields you have in every message.
- `battery`: `0` means *unknown* (no alarm). If the battery level is not
  known, **omit** the field rather than sending 0. Below 20 % the controller
  raises a "battery low" alarm (panel and Telegram), cleared from 30 %.
- Unknown extra fields (`rssi`, `sensor_type`, …) are ignored: you may add
  them for Home Assistant or diagnostics.
- Home Assistant extracts the temperature with the template
  `{{ value_json.temperature }}`: keep the field name and top-level
  position.

### 4.3 Plausibility

The controller does **not** range-check the temperature: a decoding error
such as `85.0` or `-40.0` would switch a zone OFF or ON. The gateway must
drop implausible readings instead of publishing them, e.g.:

- outside −20…50 °C;
- a jump of more than ~3 °C from the previous reading of the same sensor
  within a few minutes (unless confirmed by the next reading);
- advertisements that fail the sensor's own checksum/format checks.

Dropping a reading is always safe (see §5); publishing a wrong one is not.

---

## 5. Freshness and sensor loss (safety)

This is the most important part of the contract.

**The controller considers a zone "alive" from the time a message
arrives**, not from `data_time`. Each valid message restarts a timer of
`sensor_timeout_s` (default **900 s = 15 min**) for that zone. When it
expires:

- the zone is switched **OFF** (valve closed) and flagged `NO SENSOR` on
  the panel, with a Telegram alarm;
- if it is cold outside, the zone goes into **frost protection** (heating
  10 min every hour) instead.

The zone comes back to normal regulation with the next valid message.

Consequences for the gateway:

1. **Publish only real, new readings.** If a BLE sensor is no longer
   heard (battery dead, out of range, removed), **stop publishing** for its
   zone. Never republish a cached last value on a timer: the controller
   would keep heating the room on a temperature that no longer exists, and
   the sensor loss would never be detected.
2. **Publish often enough**: at least 3 messages per 15 minutes, so that one
   or two lost BLE advertisements or MQTT messages do not trip the timeout.
   Recommended: one message per zone every **60–300 s**, plus immediately
   when the temperature changes by ≥ 0.1 °C (not more than one message
   every ~10 s per zone).
3. If the gateway restarts, it may take a few minutes to hear all sensors:
   that is fine, the controller tolerates up to 15 min of silence.
4. At controller start-up, a zone never heard is treated as missing only
   after 15 min, so the gateway and the controller can start in any order.

```mermaid
sequenceDiagram
    participant S as BLE sensor
    participant GW as RoomSense
    participant B as Broker
    participant ATN as AcquaThermoNet

    S->>GW: advertisement 20.4 °C
    GW->>B: RoomSense/apartment/salotto/data, temperature 20.4
    B->>ATN: message, zone timer restarted (15 min)
    ATN->>ATN: regulation (ON below setpoint - 0.5, OFF above setpoint)
    Note over S: battery dead, no more advertisements
    Note over GW: nothing heard: publish nothing
    Note over ATN: 15 min without messages
    ATN->>ATN: zone OFF (or frost protection), NO SENSOR alarm
    S->>GW: advertisement again (new battery)
    GW->>B: new reading
    B->>ATN: zone back to normal regulation
```

How the controller uses the temperature, for information: heating ON when
the temperature is below *setpoint − 0.5 °C*, OFF above the *setpoint*,
with at least 3 minutes between two switches of the same zone. Readings
more frequent than about one per minute do not improve the regulation.

---

## 6. MQTT settings

| Setting | Value | Why |
|---|---|---|
| Client id | unique and stable, e.g. `RoomSense-<host>` | must not collide with `AcquaThermoNet-…` |
| Credentials / TLS | the broker's; same `ca_file` rules as the controller if TLS | |
| QoS | 0 or 1 | the controller subscribes at QoS 0; QoS 1 only helps up to the broker |
| **Retain** | **false** | a retained reading is delivered again when the controller restarts, and it counts as fresh (§5): a dead sensor would regulate its zone for 15 more minutes on an old value |
| Keepalive | 30–60 s | |
| Reconnect | automatic, with backoff | buffered readings older than ~1 min should be dropped, not sent late |

Recommended (not read by the controller today, useful for diagnostics and
Home Assistant): gateway availability with a Will message.

| Topic | Payload | Retain |
|---|---|---|
| `RoomSense/status` | `online` at connect, `offline` as Will | yes |

Do **not** put it under `RoomSense/apartment/`. Under `AcquaThermoNet/`
publish only the setpoint commands of §7 (`AcquaThermoNet/<zone>/set_temp`),
and nothing under `homeassistant/climate/`: those topics belong to the
controller.

---

## 7. Setpoints from the RoomSense panel

The panel in the house shows each zone with its setpoint and lets the user
change it, like Home Assistant does. **AcquaThermoNet owns the setpoints**:
it rounds, clamps, stores and applies them, and they also change from its
own panel and from Home Assistant. RoomSense never keeps a setpoint of its
own: it shows what the controller publishes and sends commands.

| Topic | Dir. (RoomSense) | Payload | Retain | Notes |
|---|---|---|---|---|
| `AcquaThermoNet/status` | in | `online` / `offline` | yes | `offline` (also as the controller's Will): no commands |
| `AcquaThermoNet/<zone>/state_temp` | in | setpoint, e.g. `20` or `20.5` | yes | the value to show; republished after **every** command, also when unchanged or clamped |
| `AcquaThermoNet/<zone>/state_mode` | in | `heat` / `off` | yes | heat demand of the zone (heating icon) |
| `AcquaThermoNet/<zone>/set_temp` | **out** | plain number, e.g. `21.5` (not JSON) | **no** | rounded to 0.5 and clamped to 5…25 by the controller |

Rules for the gateway:

1. **Only `set_temp`** (and `mode/set`, §8, `chrono/set` and
   `chrono/profile/set`, §9). Never publish `state_temp`, `state_mode`,
   `set_mode`, `mode/state`, `chrono`, `chrono/profile` or
   `AcquaThermoNet/status`.
2. **Retain false.** A retained command would be applied again at every
   controller restart, undoing the changes made since then from its panel
   or Home Assistant.
3. **Show the confirmed value.** After a command show the new value as
   pending until `state_temp` arrives; without it within ~5 s, show the
   last `state_temp` again (command lost or controller busy).
4. **Collect the presses.** Several +/- presses become one command per
   zone, at most one per second (the controller also delays its flash
   write until the presses stop).
5. **Controller not reachable** (`AcquaThermoNet/status` `offline`, no
   `state_temp` received yet, or the broker disconnected): show the last
   known setpoints and send no command.
6. **Same zone names** as the readings (§3). A command for an unknown zone
   is ignored silently; a payload that is not a number is logged by the
   controller (`Invalid setpoint for zone …`) and ignored.
7. QoS 1 for the commands, so that a press is not lost silently up to the
   broker; any QoS for the subscriptions.
8. **Setpoints also change by themselves.** Since 2.4.0 the controller
   can run a chrono thermostat per zone: at the start of each time slot
   it sets the zone's setpoint and publishes `state_temp`, with no
   command from anyone. A `set_temp` is a manual change: it lasts until
   the next slot of that zone. Showing `state_temp` as it arrives (rule 3)
   is enough. The chrono state and program are in §9.

```mermaid
sequenceDiagram
    participant U as User
    participant GW as RoomSense panel
    participant B as Broker
    participant ATN as AcquaThermoNet

    U->>GW: + + (20.5 to 21.5)
    Note over GW: 21.5 shown as pending, presses collected
    GW->>B: AcquaThermoNet/salotto/set_temp 21.5
    B->>ATN: command
    ATN->>ATN: round 0.5, clamp 5..25, store, regulate
    ATN->>B: AcquaThermoNet/salotto/state_temp 21.5 (retained)
    B->>GW: state_temp 21.5
    Note over GW: 21.5 shown as confirmed
```

---

## 8. House mode from the RoomSense panel

Three modes for the whole house, chosen on the RoomSense panel. Like the
setpoints, **AcquaThermoNet owns the mode**: it saves the setpoints,
applies the mode, runs the timer and restores them, also when RoomSense
is off or restarting.

| Mode | Setpoint of every zone | Ends |
|---|---|---|
| `normal` | its own (as set from the panels or Home Assistant) | — |
| `window` (windows open) | min(own, 8 °C) | after 30 min, or `normal` |
| `away` (holidays) | min(own, 15 °C) | only with `normal` |
| `boost` (heat the house quickly) | max(own, 25 °C) | after 30 min, or `normal` |

The values (8 °C, 30 min, 15 °C, 25 °C, 30 min) are configurable in
AcquaThermoNet (`[MODES]`). `window` and `away` never raise a setpoint (a
zone kept lower stays at its own); `boost` never lowers one.

| Topic | Dir. (RoomSense) | Payload | Retain | Notes |
|---|---|---|---|---|
| `AcquaThermoNet/mode/set` | **out** | `normal` / `window` / `away` / `boost` | **no** | QoS 1 |
| `AcquaThermoNet/mode/state` | in | JSON, e.g. `{"mode":"window","remaining_s":1740}` | yes | after every command (also when unchanged or refused), at every change and every minute while `window` or `boost` runs |

`mode/state` fields:

| Field | Type | Meaning |
|---|---|---|
| `mode` | string | `normal`, `window`, `away` or `boost` |
| `remaining_s` | integer | `window` and `boost` only: seconds before the setpoints are restored. A countdown, not a time of day: the clocks of the two panels may differ |

Behaviour of the controller:

1. **Setpoints saved, not lost.** Entering a mode keeps each zone's own
   setpoint (also across a controller restart); `normal`, or the end of
   `window` or `boost`, restores them.
2. **`state_temp` is the applied value** during a mode (e.g. 8), so that
   both panels and Home Assistant show what the zone regulates to.
3. **`set_temp` during a mode** (from Home Assistant or the controller
   panel) changes the zone's own setpoint, restored when the mode ends;
   `state_temp` keeps showing the applied value (e.g. min(new, 15)).
4. **A new mode replaces the current one**, except `window` while `away`,
   which is ignored (logged): `away` is already low. So `boost` ends
   `away` (back home) and `window`, and `window` ends `boost` (opening the
   windows stops the heating). A new `window` or `boost` restarts its
   time. Every end goes back to `normal`, never to the previous mode.
5. **Controller restart**: `away` continues; `window` and `boost` resume
   with the time left (the controller keeps their end time), or end and
   restore the setpoints if the time is over or the controller clock is
   not set. `mode/state` is published again after the restart, with the
   new `remaining_s`.
6. **Invalid payload** on `mode/set`: logged
   (`Invalid house mode …`) and ignored.

Rules for the gateway:

1. Show the mode from the retained `mode/state` only; after a command
   show it as pending until `mode/state` arrives, as for `set_temp` (§7
   rule 3).
2. Count `remaining_s` down locally from the moment `mode/state` arrived;
   a new message replaces it.
3. While a mode is active, the zone −/+ are disabled on the panel: the
   mode is left from the mode popup (`normal`).
4. No command while the controller is not reachable (§7 rule 5);
   retain false, QoS 1 (§7 rules 2 and 7).

```mermaid
sequenceDiagram
    participant U as User
    participant GW as RoomSense panel
    participant B as Broker
    participant ATN as AcquaThermoNet

    U->>GW: windows open
    GW->>B: AcquaThermoNet/mode/set window
    B->>ATN: command
    ATN->>ATN: save setpoints, apply min(own, 8), start 30 min
    ATN->>B: mode/state {"mode":"window","remaining_s":1800} (retained)
    ATN->>B: ZONE/state_temp 8 for every zone (retained)
    Note over GW: "Windows open, 30 min left", −/+ disabled
    ATN->>ATN: 30 min later: restore the setpoints
    ATN->>B: mode/state {"mode":"normal"}, ZONE/state_temp (own values)
```

---

## 9. Chrono thermostat from the RoomSense panel

Since 2.4.0 each zone can follow a weekly program (chrono thermostat):
a **weekday** and a **holiday** profile of up to 8 slots, "from this time
on, this setpoint". The RoomSense panel shows the program of each zone,
turns it on and off and edits the profiles. As for the setpoints and the
house mode, **AcquaThermoNet owns the program**: it validates, stores and
runs it, also when RoomSense is off; the program is also edited on the
controller panel and turned on and off from Home Assistant. RoomSense
keeps no program of its own: it shows what the controller publishes and
sends commands.

What the chrono does to the setpoints (the controller does it all):

- at the start of each slot the zone's own setpoint becomes the slot
  value, published as `state_temp` (§7 rule 8);
- a manual `set_temp` lasts until the next slot of that zone;
- during `window` and `boost` a new slot waits for the end of the mode;
  during `away` the chrono is suspended, and at the end of `away` the
  slot in force applies at once (§8);
- before the first slot of a day the last slot of the previous day holds;
  `holiday_days` (default Saturday and Sunday) chooses the days of the
  holiday profile; an empty holiday profile means "as weekday".

| Topic | Dir. (RoomSense) | Payload | Retain | Notes |
|---|---|---|---|---|
| `AcquaThermoNet/<zone>/chrono` | in | JSON, chrono state (below) | yes | when it changes: next change, manual, paused |
| `AcquaThermoNet/<zone>/chrono/profile` | in | JSON, the program (below) | yes | at connect, at every change (from any panel or Home Assistant) and after every `chrono/profile/set`, also when refused |
| `AcquaThermoNet/<zone>/chrono/set` | **out** | `on` / `off` | **no** | QoS 1; also from the Home Assistant switch |
| `AcquaThermoNet/<zone>/chrono/profile/set` | **out** | JSON, the new program or a reset (below) | **no** | QoS 1 |

### 9.1 Chrono state: `<zone>/chrono`

```json
{"chrono":"on","manual":false,"next_change":"22:30",
 "next_change_at":"2026-10-03T22:30:00+02:00","next_setpoint":17,
 "paused":false,"profile":"weekday"}
```

| Field | Type | Meaning |
|---|---|---|
| `chrono` | string | `on` / `off`; only this field when `off` |
| `paused` | bool | house mode `away`: chrono suspended |
| `manual` | bool | the zone's own setpoint is not the one of the slot in force (a manual change, until `next_change`) |
| `profile` | string | `weekday` / `holiday`: the profile of today |
| `next_change` | string | `HH:MM` of the next slot, controller local time |
| `next_change_at` | string | the same, ISO 8601 with the UTC offset |
| `next_setpoint` | number | setpoint of the next slot |

`profile` and the `next_…` fields are absent while the controller clock
is not set. This is the controller's own view (its clock): show it as it
is, e.g. `22:30 → 17.0°`, `manual → 22:30` or `chrono paused` on the zone
card. Republished at
least when a field changes; checked every minute.

### 9.2 Program: `<zone>/chrono/profile`

```json
{"enabled":true,"edited":true,"holiday_days":[6,7],
 "weekday":[{"at":"06:30","temp":20.5},{"at":"08:00","temp":18},
            {"at":"17:00","temp":20.5},{"at":"22:30","temp":17}],
 "holiday":[{"at":"08:00","temp":20.5},{"at":"23:00","temp":17}]}
```

| Field | Type | Meaning |
|---|---|---|
| `enabled` | bool | chrono on (same as `chrono` of 9.1) |
| `edited` | bool | edited on a panel or from Home Assistant (stored by the controller); `false`: the program of the controller configuration |
| `holiday_days` | array of integers | days of the holiday profile, 1 = Monday … 7 = Sunday; may be empty. Controller configuration only: not changed by commands |
| `weekday` | array of slots | sorted by time, 0…8 slots; empty only with `enabled` false |
| `holiday` | array of slots | sorted by time, 0…8 slots; empty: the weekday profile on the holiday days too |

A slot is `{"at":"HH:MM","temp":20.5}`: from `at` (local time, 24 h, two
digits each) the setpoint `temp`, 5…25 °C in steps of 0.5.

To draw a day, take its profile (`holiday` if the day is in
`holiday_days` and `holiday` is not empty, else `weekday`); before the
first slot the last slot of the previous day holds. The day and time come
from the RoomSense clock, which may differ slightly from the controller's:
for the next change prefer 9.1.

### 9.3 Commands

**On/off**: `AcquaThermoNet/<zone>/chrono/set` `on` or `off`. `on` with
no weekday slot is refused (logged), the chrono stays off. Turning it on
applies the slot in force at once.

**New program**: `AcquaThermoNet/<zone>/chrono/profile/set`, the whole
program of the zone:

```json
{"enabled":true,
 "weekday":[{"at":"06:30","temp":20.5},{"at":"22:30","temp":17}],
 "holiday":[]}
```

- `enabled`, `weekday` and `holiday` are all required: the command
  replaces the program, nothing is merged. `holiday_days` is not accepted
  (ignored if present).
- Valid program: at most 8 slots per profile; `at` exactly `HH:MM`,
  00:00…23:59 (`6:30` and `24:00` are refused); no time twice in a
  profile; `temp` a JSON number (not a string) 5…25, rounded to 0.5 by
  the controller; the slots in any order (the controller sorts them);
  `enabled` true needs at least one weekday slot. Steps of 15 minutes are
  what the panels use, not a rule.
- **Anything invalid refuses the whole command** (logged with the
  reason, e.g. `Invalid chrono program for zone salotto : weekday 00:00:
  temp 30 outside 5...25`) and the program does not change.
  Unlike the controller configuration, no slot is dropped silently.
- Accepted: the controller stores it (it survives restarts), logs it and
  applies the slot in force at once, also replacing a manual setpoint
  (during `window` or `boost`: at the end of the mode; during `away`: at
  its end).

**Reset**: `{"reset":true}` on `chrono/profile/set` goes back to the
program of the controller configuration (`edited` false), applied at
once as above.

In every case `chrono/profile` is published again (also unchanged, also
when refused); `chrono` and `state_temp` follow when they change, in no
fixed order. A command for an unknown zone is ignored silently, with no
answer (§7 rule 6).

Rules for the gateway:

1. Show the program from the retained `chrono/profile` only. After a
   command show "saving" until `chrono/profile` arrives; then show what
   arrived: if it is not what was sent (refused, or rounded), tell the
   user. Compare after sorting the slots and rounding the setpoints to
   0.5, as the controller does. Without an answer within ~5 s, show the last program again
   (command lost).
2. One `chrono/profile/set` per Save of the editor, never one per +/−
   press: the controller stores every accepted command.
3. **Last command wins.** The program may also change on the controller
   panel or from Home Assistant while the user is editing on RoomSense:
   if `chrono/profile` changes during an edit, warn the user before a
   Save overwrites it.
4. No command while the controller is not reachable (§7 rule 5);
   retain false, QoS 1 (§7 rules 2 and 7).
5. The zone −/+ stay as in §7: a manual setpoint while the chrono runs is
   normal (`manual` in 9.1).

```mermaid
sequenceDiagram
    participant U as User
    participant GW as RoomSense panel
    participant B as Broker
    participant ATN as AcquaThermoNet

    U->>GW: edit salotto weekday, Save
    Note over GW: "saving"
    GW->>B: AcquaThermoNet/salotto/chrono/profile/set {"enabled":true,...}
    B->>ATN: command
    ATN->>ATN: validate, store, slot in force applies
    ATN->>B: salotto/chrono/profile {...,"edited":true} (retained)
    ATN->>B: salotto/chrono, salotto/state_temp if changed (retained, any order)
    B->>GW: chrono/profile, chrono, state_temp
    Note over GW: new program shown as saved
```

---

## 10. Testing the integration

With `mosquitto_pub` (or the gateway itself), against the real broker:

```sh
# one reading for the living room
mosquitto_pub -h <broker> -u <user> -P <password> \
  -t RoomSense/apartment/salotto/data \
  -m '{"temperature":20.4,"humidity":48,"battery":85}'

# watch what the gateway publishes
mosquitto_sub -h <broker> -u <user> -P <password> -v -t 'RoomSense/#'

# watch the controller reacting (setpoint and heat demand per zone)
mosquitto_sub -h <broker> -u <user> -P <password> -v -t 'AcquaThermoNet/#'

# a setpoint command, as the panel sends it (§7)
mosquitto_pub -h <broker> -u <user> -P <password> \
  -t AcquaThermoNet/salotto/set_temp -m 21.3

# windows open for the whole house, then back to normal (§8)
mosquitto_pub -h <broker> -u <user> -P <password> \
  -t AcquaThermoNet/mode/set -m window
mosquitto_pub -h <broker> -u <user> -P <password> \
  -t AcquaThermoNet/mode/set -m normal

# a chrono program for the living room, then back to the configuration (§9)
mosquitto_pub -h <broker> -u <user> -P <password> \
  -t AcquaThermoNet/salotto/chrono/profile/set \
  -m '{"enabled":true,"weekday":[{"at":"06:30","temp":20.5},{"at":"22:30","temp":17}],"holiday":[]}'
mosquitto_pub -h <broker> -u <user> -P <password> \
  -t AcquaThermoNet/salotto/chrono/profile/set -m '{"reset":true}'
```

What to check:

| Check | Expected result |
|---|---|
| Reading received | panel card of the zone shows the temperature, humidity, battery and `updated now` |
| Below setpoint − 0.5 | `AcquaThermoNet/<zone>/state_mode` becomes `heat`, flame icon red |
| Wrong zone name | nothing happens on the panel (message ignored) |
| Invalid payload (`{"temp":20}`, not JSON) | controller log: `Invalid sensor data for zone …` |
| Battery 15 | card shows `BATTERY LOW 15%`, Telegram alarm |
| Stop the sensor for 15 min | card shows `NO SENSOR`, zone OFF, Telegram alarm |
| Home Assistant | the `clima.<zone>` entity shows the current temperature |
| `set_temp` 21.3 | `AcquaThermoNet/salotto/state_temp 21.5` (retained), the controller panel and Home Assistant show 21.5 |
| `set_temp` 40 | clamped: `state_temp 25` |
| Controller stopped | `AcquaThermoNet/status offline`: the RoomSense panel shows the setpoints as last known, no commands |
| `mode/set window` | `mode/state {"mode":"window","remaining_s":1800}`, every `state_temp` at min(own, 8); after 30 min (or `normal`) the own setpoints again |
| `mode/set away` | `mode/state {"mode":"away"}`, every `state_temp` at min(own, 15) until `normal` |
| `mode/set boost` | `mode/state {"mode":"boost","remaining_s":1800}`, every `state_temp` at 25; after 30 min (or `normal`) the own setpoints again |
| `set_temp` 22 during `away` | `state_temp 15`; after `normal`, `state_temp 22` |
| `chrono/profile/set` program above | `chrono/profile` with `"edited":true` and the new slots, `chrono` with the next change, `state_temp` of the slot in force (20.5 or 17) |
| `chrono/profile/set` with `"temp":30` or 9 slots | refused, controller log `Invalid chrono program for zone …`, `chrono/profile` republished unchanged |
| `chrono/set off` | `chrono {"chrono":"off"}`, `chrono/profile` with `"enabled":false`; the setpoint stays |
| `chrono/profile/set {"reset":true}` | `chrono/profile` of the controller configuration, `"edited":false` |
| Program edited on the controller panel | `chrono/profile` republished: RoomSense shows the new program |

The controller's Telegram bot (`/zone salotto`) also shows the last reading
and how long ago it arrived.

---

## 11. Acceptance checklist for RoomSense

- [ ] Topic `RoomSense/apartment/<zone>/data`, zone names from configuration, exact case
- [ ] JSON object with numeric `temperature` in °C in every message
- [ ] `humidity` and `battery` (1–100 %) sent when known, `battery` omitted when unknown
- [ ] Implausible readings dropped (range, jumps, decode errors)
- [ ] One message per zone every 60–300 s while the sensor is heard
- [ ] **No message** for a zone whose sensor is not heard (no cached republishing)
- [ ] Retain false; no stale buffered readings sent after a reconnect
- [ ] Several sensors in one zone combined into one value by the gateway
- [ ] Own client id; optional `RoomSense/status` availability with Will
- [ ] Under `AcquaThermoNet/` only `…/<zone>/set_temp` (plain number), `mode/set` (`normal`/`window`/`away`/`boost`), `…/<zone>/chrono/set` (`on`/`off`) and `…/<zone>/chrono/profile/set` (JSON), all retain false; nothing under `homeassistant/`
- [ ] Setpoints shown from the retained `state_temp`, pending until confirmed; no commands while the controller is offline
- [ ] House mode shown from the retained `mode/state`, pending until confirmed, `remaining_s` counted down locally; zone −/+ disabled while a mode is active
- [ ] Chrono program shown from the retained `chrono/profile`, next change from `chrono`; one `chrono/profile/set` per Save, with the whole program; "saving" until `chrono/profile` arrives, refusal shown; warning when the program changes during an edit

---

## 12. Changing the contract

The topic prefixes and tails, the field names, the setpoint step and range
(`TEMP_STEP`, `TEMP_MIN`, `TEMP_MAX`), the timeout, the house mode values
and the chrono limits are defined in AcquaThermoNet (`climatezones.h`,
`mqttparse.cpp`, `chrono.h`, `[REGULATION] sensor_timeout_s`, `[MODES]`,
`[CHRONO]`). Any change on either side (new zone, renamed zone,
different topic or field) must be agreed and made in both applications at
the same time; a new zone also needs its relay configured in the
controller. Full controller documentation: [`DOCUMENTATION.md`](DOCUMENTATION.md).
