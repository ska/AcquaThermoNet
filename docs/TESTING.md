# AcquaThermoNet: tests

What the automatic tests check, suite by suite. How to build and run
them: [`BUILD.md`](BUILD.md) §3; overview in
[`DOCUMENTATION.md`](DOCUMENTATION.md) §20.

| Level | Where | Needs | Size |
|---|---|---|---|
| Unit tests | `tests/`, one binary `tests/tests` | nothing: no broker, relay board, network or display | 15 suites, about 150 test functions |
| End to end | `tools/e2e.sh` | Python 3, no other AcquaThermoNet instance on the PC | 46 checks |

Run both after every change to the code. The GUI screenshot
tool (`guishot`, BUILD.md §3.3) is not a test: it only renders the pages.

---

## 1. How the unit tests work

- **QtTest**, one class per suite (`tests/tst_<name>.cpp`), all run in
  order by `tests/main.cpp`; the exit status is non zero if any test
  function fails.
- **No real files of the user**: every test writes its `setting.ini` in a
  `QTemporaryDir` (`TestUtil::writeIni`); `state.ini` is created next to
  it.
- **No hardware**: the Modbus relay board is replaced by the frames that
  `ModBusFrameProcessor` would send (`frameToSend`) and by hand-made
  answers (`TestUtil::relayAnswer`); the relay commands are read back from
  the frames (`TestUtil::relayWrites`).
- **No waiting**: timeouts and cycles run on the monotonic clock, moved
  forward with `MonoClock::advanceForTest()`. Only a few tests wait real
  seconds (timers of 1 s: house mode end, minimum cycle, valve exercise).
- **Fixed dates**: the chrono and the valve exercise get the time as an
  argument, with dates whose weekday is known (e.g. 2026-09-27 Sunday,
  2040-01-02 Monday), so the result does not depend on when the tests
  run.

---

## 2. Unit test suites

### 2.1 `tst_monoclock`: monotonic clock

| Test | Checks |
|---|---|
| `monotonic` | `MonoClock::nowMs()` is never 0 ("never") and grows with real time |
| `advance` | `advanceForTest()` moves it forward (used by the other suites) |

### 2.2 `tst_modbus`: Modbus RTU frames

| Test | Checks |
|---|---|
| `requestFrames` | read of the 8 relay registers and relay ON write, byte for byte with the CRC |
| `verifyCrc` | good and bad CRC, bytes ≥ 0x80 (an old signed-char bug), frames too short or empty |
| `verifyCrcLongBuffer` | more than 255 bytes of line noise: the check ends (an old 8-bit loop counter never did) |
| `relayStatusParsed` | a status answer gives the relay bitmap (relays 1 and 5) |
| `invalidAnswersIgnored` | wrong CRC, truncated answer, write echo, empty data: no status |
| `setRelayWritesThenReads` | a relay command is a write followed by a status read |
| `offlineAfterMissedPolls` | board offline after the missed polls, online again at the first answer |

### 2.3 `tst_mqttparse`: MQTT topics and payloads

| Test | Checks |
|---|---|
| `matchZoneTopic` | `AcquaThermoNet/<zone>/<tail>`: other tail or base, empty zone, nested levels, a base that is only a prefix |
| `matchSensorTopic` | `RoomSense/<zone>/data`; `RoomSense/status` and the old `RoomSense/apartment/<zone>/data` are not readings |
| `sensorJsonStringsAndNumbers` | sensor JSON with numbers or numeric strings, all fields, negative temperature |
| `sensorJsonKeepsMissingFields` | optional fields missing: the previous humidity and battery stay |
| `sensorJsonInvalid` | not JSON, array, no or bad `temperature`, `"nan"`, `"inf"`, `"-inf"`, 85, −40, `"1e9"`: refused with a reason, zone untouched |
| `sensorJsonLimits` | −30 and 60 °C accepted |
| `sensorJsonOptionalOutOfRange` | humidity 300, battery `"nan"`, `battmv` −1, `data_time` `"inf"`: reading accepted, those fields keep their value |
| `parseSetPoint` | `set_temp` payload: integer, decimal, spaces, out of range (clamped later); text, empty, `nan`, `inf` refused |
| `weatherState` | `AcquaThermoNet/weather` payload |
| `weatherDiscovery` | the 5 Home Assistant outdoor sensors (topics, ids, availability, expiry, units); disabled: empty payloads that remove them |
| `chronoState` | `<zone>/chrono`: off; on with paused, manual, profile of the day, next change with its UTC offset; nothing about the next change while the clock is not set |
| `chronoProfile` | `<zone>/chrono/profile`: empty program, slots sorted, holiday profile, `holiday_days`, compact JSON, integer setpoints as plain numbers |
| `chronoProgram` | `<zone>/chrono/profile/set` accepted: slots sorted and rounded to 0.5, `holiday_days` ignored, chrono off with an empty weekday profile, eight slots, `{"reset":true}` |
| `chronoProgramInvalid` | 15 refused payloads (not JSON, array, `reset` false, `enabled` missing or not a bool, profile missing, on without weekday slots, time `6:30` or `24:00`, slot not an object, `temp` as a string, out of 5…25, time twice, nine slots): refused with a reason, program untouched; topic matching of `chrono/profile/set` |
| `chronoSwitchDiscovery` | Home Assistant chrono switch: topics, payloads, availability, same device as the zone climate |
| `subscriptions` | the filters and their QoS (1 except `homeassistant/status`): commands, readings and `RoomSense/status` subscribed, none of the controller's own topics |
| `refusedWhenRetained` | commands (`set_temp`, `chrono/set`, `chrono/profile/set`, `mode/set`) and readings refused when retained; states, status, weather, `homeassistant/status` accepted |
| `parseOnOff` | `on`/`off` in any case, with spaces; anything else refused |

### 2.4 `tst_netinfo`: network row of the GUI

| Test | Checks |
|---|---|
| `brokerInterfaceWins` | the interface that reaches the broker is shown (IP, MAC upper case, host) |
| `mappedIpv6Address` | an IPv4-mapped IPv6 local address is matched to its interface |
| `noBrokerFirstActive` | no broker connection: first active interface, loopback skipped, IPv4 preferred to link-local IPv6 |
| `unknownBrokerAddressFallsBack` | an address of no interface: same fallback |
| `inactiveAndIpv6OnlySkipped` | interfaces down or with IPv6 only are skipped |
| `noNetwork` | only loopback: `Network: none` |
| `fields` | text of the row; `MAC: --` for an interface without a hardware address (VPN) |

### 2.5 `tst_config`: `setting.ini` and `state.ini`

| Test | Checks |
|---|---|
| `loadZones` | zones, relays and setpoints from `[ZONES]` and `[RELAY]` |
| `loadZonesRejectsInvalid` | invalid names, duplicates and the reserved `list` dropped; missing or out of range relay = no relay; default setpoint |
| `modeIsReserved` | a zone named `mode` is dropped |
| `modes` | `[MODES]` defaults, values (rounded), invalid values back to the defaults; house mode and its end time in `state.ini`, end time removed with `normal` |
| `loadZonesEmpty` | no `[ZONES]`: no zone |
| `saveSetPoints` | setpoints and `setpoint_at` go to `state.ini`, which wins over `setting.ini`; `setting.ini` never written; no `setpoint_at` when the time is unknown |
| `chronoEditedInState` | chrono edited on a panel saved in `state.ini` and preferred to `setting.ini` (`holiday_days` still from `setting.ini`); empty holiday profile; reset back to `setting.ini` |
| `ensureSettings` | first start: `setting.ini` created writable from the read-only `setting.default.ini`; an existing one is kept; missing template reported |
| `uniqueId` | MQTT unique id: from the MAC, then kept in `state.ini`; one set by hand in `setting.ini` wins; random 12 hex digits without a MAC |
| `regulation` | `[REGULATION]` defaults, values, invalid values |
| `weatherProviders` | `[WEATHER]`: wttr by default, met.no needs lat/lon, unknown provider = wttr, no location = disabled |
| `mqttTls` | `[MQTT]` TLS keys; certificate paths relative to `setting.ini`, absolute ones kept |
| `mqttSerialWeatherLog` | broker, user, default port; serial port defaults; met.no settings; `[LOG]` path and sizes |

### 2.6 `tst_chrono`: chrono thermostat, pure functions

Profiles of the documentation example (weekday 06:30 20.5, 08:00 18,
17:00 20.5, 22:30 17; holiday 08:00 20.5, 23:00 17 on Saturday and
Sunday).

| Test | Checks |
|---|---|
| `current` | slot in force: at a slot start, inside, last slot, after midnight (previous day's last slot), Friday → Saturday, Saturday, Sunday → Monday |
| `next` | next change: before the first slot, at a slot start, after the last (next day), Sunday → Monday |
| `emptyHolidayUsesWeekday` | empty holiday profile: the weekday one on the holidays |
| `singleSlot` | one slot a day |
| `disabled` | chrono off or empty: no slot |
| `dstChange` | the day the clocks go forward: slot in force and next change still consistent |
| `dayTemps` | the 96 quarters of an hour of a day (chrono page), hourly steps, profile name of the day, DST day |
| `editSteps` | editor: time to the 15-minute grid, never past the neighbours or midnight; setpoint ± 0.5 within 5…25 |
| `editAddSlot` | editor "add": 08:00 20.0 on an empty profile, one hour after the last slot, else in the longest gap; at most 8 |
| `parseProfile` | `HH:MM=temp` and `HH:MM temp`; wrong entries, out of range, duplicates dropped; rounded, sorted |
| `parseProfileMaxSlots` | only the first 8 slots written are kept |
| `loadChrono` | `[CHRONO]`: holiday days by name or number, list or single value; enabled without valid slots = off |
| `loadChronoDefaultHolidays` | default Saturday and Sunday; empty `holiday_days` = none |

### 2.7 `tst_zonemodel`: zones, house modes, chrono applied

Zones `a` (setpoint 20, relay 1) and `b` (18, no relay).

| Test | Checks |
|---|---|
| `loadAndIndex` | zones loaded, lookup by name |
| `setPointNormalized` | rounded to 0.5, clamped to 5…25 |
| `setPointSignals` | same value: published again but no zone change; invalid zone ignored |
| `heatAndStatusSignals` | heat, relay fault, sensor lost, relay state, pending switch: a signal only on a real change |
| `sensorData` | only the sensor fields of a reading are taken |
| `deferredSave` | setpoints written to flash after the delay, all zones in one write |
| `houseModes` | `away` and `window` lower every zone (never raise), own setpoints kept and restored; `window` refused during `away`; setpoint changed during a mode applied at its end |
| `boost` | `boost` raises (never lowers), ends `away`; `window` stops it; new `boost_temp` applied at once |
| `boostEnds`, `windowEnds` | end by timer, setpoints restored |
| `modeAfterRestart` | `away`, `boost` and `window` continue after a restart; `normal` clears the saved end time |
| `modeResume` | after a restart: time left, time over, end not saved, end too far (clock set back: capped) |
| `resumeS` | the same calculation, also with the clock not set yet |
| `clockValid` | year before 2024 = clock not set |
| `modeRestartedNewUntil` | a new `boost` restarts the full time |
| `modeNames` | `normal`/`window`/`away`/`boost`, case sensitive |
| `writeChrono` | helper that writes the chrono of zone `a` to the ini (runs as a test, checks nothing) |
| `chronoApplies` | the slot in force applies at start, a new one at its time; one signal per change; values set by the chrono not written to flash |
| `chronoManualUntilNextSlot` | a manual setpoint lasts until the next slot; a manual one is saved with its time |
| `chronoWindowDefers` | a slot begun during `window` applies at its end |
| `chronoBoostNoNewSlot` | `boost` target stable; no new slot during it: the manual setpoint stays |
| `chronoAwayExit` | suspended during `away`; at its end the slot in force applies |
| `chronoAfterRestart` | manual setpoint after the slot kept; before the slot or without its time replaced |
| `chronoClock` | nothing while the clock is not set; clock set back: nothing new |
| `chronoEdited` | program edited: saved, slot in force applies at once (replacing a manual setpoint); reset to `setting.ini`; on without weekday slots = off |
| `flushOnExit` | pending setpoints written at exit |

### 2.8 `tst_regulation`: thermoregulation

Zone `a` relay 1, zone `b` no relay, both at 20 °C, hysteresis 0.5.

| Test | Checks |
|---|---|
| `hysteresis` | ON below setpoint − 0.5, stays ON inside the band, OFF above the setpoint |
| `setPointChangeRegulates` | a new setpoint is evaluated at once |
| `houseModeRegulates` | `away` turns OFF, `normal` ON again, `boost` ON |
| `zoneWithoutRelay` | demand shown, nothing on the bus |
| `allRelaysOff` | OFF command to every configured relay |
| `sensorTimeout` | sensor silent: zone OFF and "sensor lost"; back at the next reading |
| `sensorTimeoutMonotonic` | the 900 s timeout runs on the monotonic clock (a wall clock jump cannot change it) |
| `minCycleDefers` | a switch too early waits for the minimum cycle, shown as pending |
| `minCycleCancelled` | the pending switch is cancelled when no longer needed |
| `safetyOffIgnoresMinCycle` | sensor lost: OFF at once, minimum cycle ignored |
| `shutdownBlocksOn` | during the shutdown no relay goes ON |
| `relayFeedback` | relay state read back; a relay that does not follow is commanded again, then flagged as fault, cleared when it follows |
| `modbusOfflineClearsRelayState` | board offline: relay state unknown |

### 2.9 `tst_exercise`: valve exercise

| Test | Checks |
|---|---|
| `isDue` | due on Sunday 07:00…07:09 only, once a day, not when disabled |
| `isDueCustomDay` | another day and time |
| `config` | `[VALVE_EXERCISE]` defaults, values, day by number, invalid values |
| `sequence` | ON/OFF cycles of every relay together |
| `release` | a relay taken back by the regulation leaves the exercise; last relay: over |
| `onlyIdleRelays` | only relays idle for `idle_days`; the periodic OFF resend does not cut it; the exercise counts as an activation |
| `heatDemandTakesRelay` | a zone asking for heat during the exercise keeps its relay ON |
| `heatingZoneNotExercised` | a zone heating at the start is skipped |
| `shutdownAborts` | shutdown stops it, relays OFF, no ON afterwards |
| `firstStartBaseline` | first start without history: no relay counts as idle |
| `clockSetBack` | a last activation "in the future" (clock set back) does not block the others |

### 2.10 `tst_frost`: frost protection

Zones `a` (relay 1) and `b` (relay 2), sensor timeout 1 s, 10 min ON every
60 min below 6 °C.

| Test | Checks |
|---|---|
| `config` | `[FROST_PROTECTION]` defaults, values; ON time never longer than the period |
| `cycle` | sensors lost and cold outside: ON 10 min, OFF until the next hour, ON again |
| `warmOutside` | warm outside: no protection |
| `thresholdIsStrict` | 6.0 °C no, 5.9 °C yes (no rounding) |
| `outdoorGoesUp` | outdoor temperature up: stops at once |
| `unknownOutdoor`, `unknownOutdoorNoProtect` | no outdoor data: protect, or not with `outdoor_unknown_protect=false` |
| `staleOutdoor` | outdoor data older than `outdoor_max_age_min` counts as unknown |
| `workingSensorNotProtected` | only the zones without sensor data |
| `sensorBack` | sensor back: normal regulation |
| `neverSeenSensor` | a sensor never seen: protected after the timeout from the start |
| `disabled` | disabled: never |
| `shutdownBlocksOn` | no ON during the shutdown |

### 2.11 `tst_relaylog`: relay activity log

| Test | Checks |
|---|---|
| `monthlyFileAndHeader` | one CSV per month, header, columns, ISO time with UTC offset |
| `onlyChangesWithDuration` | only real changes; OFF lines carry the ON duration (monotonic) |
| `removeOldMonths` | months older than the kept ones removed, other files untouched |
| `reasonsFromRegulation` | reasons `startup`, `regulation`, `shutdown`; shared relay written as `a+b` |

### 2.12 `tst_telegram`: Telegram alarms and commands

Without network: the messages are read from the notifier signals.

| Test | Checks |
|---|---|
| `config` | `[TELEGRAM]`; no allowed chat = disabled; trailing `/` of `api_url` removed; `gateway_down_min` and its default |
| `cleanExitFlag` | first start, crash, clean exit told apart |
| `startMessages` | start message: first start, not stopped cleanly, serial port closed |
| `zoneAlarmsOnTransitions` | sensor lost/back, relay fault/ok, frost mode on/off: one message per change |
| `batteryHysteresis` | battery low at 15 %, OK again only well above; 0 = not reported |
| `systemAlarms` | Modbus board offline/online, serial port lost/open, MQTT down only after `mqtt_down_min` |
| `gatewayAlarm` | RoomSense offline: alarm only after `gateway_down_min`, in `/status` and the active alarms; broker lost meanwhile: the alarm stays until online; a short restart, or offline then the broker lost: no message |
| `windowMessages` | window open and closed texts; information, not in the reminders |
| `reminder` | reminder of the alarms still active |
| `commands` | `/status`, `/status@bot`, `/zone`, unknown zone, `/today` without relay log, unknown text |
| `onTimeFromRelayLog` | ON time of today from the relay log (skipped in the first 2 h after midnight) |
| `relayLogOnSeconds` | ON seconds per zone: closed by OFF, by a restart, still ON; period clipped; missing file |

### 2.13 `tst_weather`: outdoor weather

| Test | Checks |
|---|---|
| `parse`, `negativeAndZero`, `invalid` | wttr.in one-line answer, negative and zero temperatures, unknown location |
| `parseMetNo`, `parseMetNoWithoutRain`, `parseMetNoInvalid` | met.no JSON: values, missing optional fields, error pages and incomplete answers |
| `httpDate` | `Expires`/`Last-Modified` dates |
| `retryDelay` | quick retry after a failure: 30, 60, 120 s … up to `poll_s` |

### 2.14 `tst_logging`: log file

| Test | Checks |
|---|---|
| `rotation` | rotation at the size limit, number of old files kept, debug lines not written |

Runs last: it installs the file log handler for the rest of the process.

### 2.15 `tst_window`: open window detection

`WindowDetector` with explicit times, `WindowWatch` on zones `a` and `b`
with `MonoClock::advanceForTest()`; defaults 1.0 °C within 10 min, closed
after 0.3 °C.

| Test | Checks |
|---|---|
| `config` | `[WINDOW_DETECTION]` defaults, values, out of range values |
| `dropOpens` | 21.0 → 20.0 in 6 min: open, with the highest reading and its time |
| `slowCoolingIgnored` | 1.2 °C in 30 min (heating off): nothing |
| `oldReadingsForgotten` | a high reading older than 10 min does not count |
| `closesOnRise` | still falling: open; +0.3 above the lowest: closed; the readings before the drop do not open it again, a new drop does |
| `reset` | reset forgets the open state and the history |
| `watchSignals` | `windowOpened` (zone, from, to, minutes) and `windowClosed` (lowest, minutes) |
| `sensorLostResets` | a lost sensor resets its zone, no closed message |
| `suspendedInWindowMode` | nothing during the house mode `window`, nor from its readings afterwards |
| `disabled` | `enabled=false`: never a signal |

---

## 3. End to end: `tools/e2e.sh`

Runs the real application (desktop build, offscreen) in a temporary
directory against three local simulators:

| Simulator | Plays |
|---|---|
| `tools/minibroker.py` | MQTT broker on port 18830, logs every message (`broker.log`); `pub` publishes like the RoomSense panel or Home Assistant |
| `tools/mbsim.py` | the 8-relay Modbus board on a pseudo terminal (`sim.log`) |
| `tools/minitelegram.py` | the Telegram Bot API on port 18890 (`telegram.log`); `inject` sends a chat message |

Configuration: zones `salotto` (setpoint 17, relay 5, no chrono) and
`camera` (20, relay 4, chrono 00:00 19 °C), minimum cycle 0, log and
relay log on, Telegram on with chat 111 allowed.

Sequence (about 15 s): `salotto/set_temp 22`, a `camera` reading and
`RoomSense/status offline` published **retained** before the start → start → reading 15.0 °C for `salotto` →
`camera` reading `"nan"` and `camera/set_temp nan` → `camera` readings 20.0, 18.8, 19.2 °C → `salotto/set_temp 14` → `camera/set_temp 30` → `camera/chrono/set off` →
Telegram `/status` from chat 111 and from chat 999 → `RoomSense/status online` → an invalid chrono
program for `salotto` (30 °C) → a valid one (00:00 16 °C) → SIGTERM.

| Group | Checks |
|---|---|
| startup | Home Assistant discovery; `status online`; weather disabled: outdoor sensors removed; all relays OFF at start; the retained `set_temp` and reading ignored (log), `state_temp 22` never published; RoomSense offline at the start, online again (log) |
| regulation | 15 < 17 − 0.5: relay 5 ON, `state_mode heat`; `set_temp 14`: `state_temp 14`, `state_mode off`; `set_temp 30` clamped to 25; temperature and `set_temp` `nan` refused (log) |
| chrono | chrono switch discovery and climate attributes; slot applied (`state_temp 19`); chrono state with next change; manual after `set_temp`; off from Home Assistant; zone without chrono: off; program published at connect; invalid program refused (log); program from MQTT published with `edited` true, applied (`state_temp 16`) and logged "over MQTT" |
| shutdown | relays confirmed OFF; `status offline`; setpoint and chrono program in `state.ini`; `setting.ini` not written; log file; relay log ON/OFF lines with duration |
| telegram | start and stop messages; `/status` answered, with RoomSense OFFLINE; camera window open (20.0 → 18.8) and closed again; chat 999 ignored; the bot token never in the logs; exit code 0 |

On failure the script prints the application log. With
`E2E_KEEP_RELAYLOG=<file>` it keeps the relay log produced.

---

## 4. Not covered automatically

To check by hand, on the desktop with the real broker or on the panel:

- the GUI (touch, pages, editor): only rendered by `guishot`;
- real network services: weather providers, Telegram, MQTT over TLS with
  the CA certificates of the panel;
- the panel itself: launcher install and update (`package.info`,
  `background`), watchdog, serial port reopen after `kill -9`, boot
  splash;
- the RoomSense panel: its side of the contract is tested in its own
  repository.

---

## 5. Adding a test

- A new case in an existing suite: a private slot in `tests/tst_<name>.cpp`
  (`_data()` for a table of cases).
- A new suite: `tests/tst_<name>.cpp` with `run<Name>Tests()`, added to
  `tests/tests.pro` and called in `tests/main.cpp` (before
  `runLoggingTests`).
- A new check of the running application: a stimulus and a `check` line
  in `tools/e2e.sh`.
- Then update this document and the suite table in `DOCUMENTATION.md`
  §20.1.
