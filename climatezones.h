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

/* Room temperature accepted from a sensor (degC): a reading outside is
 * refused, e.g. the 85 or -40 of a decoding error */
#define SENSOR_TEMP_MIN -30
#define SENSOR_TEMP_MAX 60

/* Sensor battery below this (%) is an alarm (GUI, Telegram) */
#define BATTERY_LOW_PCT 20

#define BASE_TOPIC                  "AcquaThermoNet"
#define BASE_TOPIC_SENSOR           "RoomSense"
#define TAIL_DATA                   "data"
#define TAIL_SET_TEMP               "set_temp"
#define TAIL_STATE_TEMP             "state_temp"
#define TAIL_STATE_MODE             "state_mode"
#define TAIL_CHRONO                 "chrono"
#define TAIL_CHRONO_SET             "chrono/set"
#define TAIL_CHRONO_PROFILE         "chrono/profile"
#define TAIL_CHRONO_PROFILE_SET     "chrono/profile/set"

#define STATUS_TOPIC                BASE_TOPIC "/status"
/* House mode (interface §8): "mode" is never a zone name */
#define MODE_SET_TOPIC              BASE_TOPIC "/mode/set"
#define MODE_STATE_TOPIC            BASE_TOPIC "/mode/state"
#define WEATHER_TOPIC               BASE_TOPIC "/weather"
/* RoomSense availability (online/offline, retained, its Will) */
#define GATEWAY_STATUS_TOPIC        BASE_TOPIC_SENSOR "/status"
/* As known by the controller: unknown while the broker is not connected */
enum GatewayState { GatewayUnknown, GatewayOnline, GatewayOffline };
#define HA_STATUS_TOPIC             "homeassistant/status"
#define PAYLOAD_ONLINE              "online"
#define PAYLOAD_OFFLINE             "offline"

#endif // CLIMATEZONES_H
