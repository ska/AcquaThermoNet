#ifndef CLIMATEZONES_H
#define CLIMATEZONES_H

/*
 * Zone limits and MQTT topics. Zones themselves are configured in
 * setting.ini and held by ZoneModel.
 */

#define TEMP_HYST       0.5
#define TEMP_STEP       0.5
#define TEMP_MAX        25
#define TEMP_MIN        5
#define TEMP_DEFAULT    18

/* Sensor battery below this (%) is an alarm (GUI, Telegram) */
#define BATTERY_LOW_PCT 20

#define BASE_TOPIC                  "AcquaThermoNet"
#define BASE_TOPIC_SENSOR           "RoomSense/apartment"
#define TAIL_DATA                   "data"
#define TAIL_SET_TEMP               "set_temp"
#define TAIL_SET_MODE               "set_mode"
#define TAIL_STATE_TEMP             "state_temp"
#define TAIL_STATE_MODE             "state_mode"

#define STATUS_TOPIC                BASE_TOPIC "/status"
/* House mode (interface §8): "mode" is never a zone name */
#define MODE_SET_TOPIC              BASE_TOPIC "/mode/set"
#define MODE_STATE_TOPIC            BASE_TOPIC "/mode/state"
#define WEATHER_TOPIC               BASE_TOPIC "/weather"
#define HA_STATUS_TOPIC             "homeassistant/status"
#define PAYLOAD_ONLINE              "online"
#define PAYLOAD_OFFLINE             "offline"

#endif // CLIMATEZONES_H
