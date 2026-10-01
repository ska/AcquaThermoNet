# AcquaThermoNet

Multi-zone heating controller for an ARM32 HMI panel (Qt 5): room
sensors over MQTT, zone valves through a Modbus RTU relay board, Home
Assistant integration via MQTT discovery, Telegram alarms.

- Technical documentation: [`docs/DOCUMENTATION.md`](docs/DOCUMENTATION.md)
  (HTML version with rendered diagrams: [`docs/AcquaThermoNet.html`](docs/AcquaThermoNet.html),
  regenerated with `tools/docs/md2html.py`)
- Telegram setup: [`docs/TELEGRAM.md`](docs/TELEGRAM.md)
- MQTT interface with RoomSense (BLE sensor gateway): [`docs/SENSOR_GATEWAY_INTERFACE.md`](docs/SENSOR_GATEWAY_INTERFACE.md)
- Deployment package: [`tools/package/README.md`](tools/package/README.md)

Quick build (desktop):

```sh
git submodule update --init
mkdir build-desktop && cd build-desktop
/path/to/Qt/5.13.2/bin/qmake ../AcquaThermoNet.pro && make -j4
./bin_x86_64/AcquaThermoNet --version
```
