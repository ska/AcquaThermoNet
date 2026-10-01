# AcquaThermoNet deployment package

`make_package.sh` builds the zip installed on the device by its launcher
(JMLauncher): a `package.info` descriptor, the lifecycle hooks and the
payload. Layout and hooks follow the CanGateway package for the same
devices (`WeSoftware/CanGateway/tools/package`), adapted to a full screen
Qt HMI. No Qt library is bundled: QtMqtt is linked statically (built from
the `ThirdParty/qtmqtt` submodule), QtSerialPort and the rest of Qt come
from the device.

## Built with every build

`AcquaThermoNet.pro` runs `make_package.sh` after every link of an ARM
build (Arm32, Arm64), so building in Qt Creator or with `make` is enough:

```
<build dir>/dist/AcquaThermoNet_Package_Arm32_2.2.0.zip
<build dir>/dist/AcquaThermoNet_Arm32_2.2.0.debug
```

- desktop builds: only with `CONFIG+=package` (qmake argument);
- ARM builds without the package: `CONFIG+=nopackage`;
- it runs only when the binary is relinked, not on a no-op `make`.

Debug information: the binary in the zip is **stripped** (the SDK compiles
with `-g` also in release: 17 MB with, about 0.5 MB without). The full
binary is kept next to the zip as `AcquaThermoNet_<arch>_<version>.debug`:
keep it with the release to read a core dump of the device
(`gdb AcquaThermoNet_Arm32_2.2.0.debug core`; `start.sh` enables core
dumps). The strip is the one of the build toolchain (`$(STRIP)` of the
Makefile, searched next to the compiler when the SDK environment is not
loaded, as in Qt Creator); a missing or wrong strip stops the packaging
with an error. `--no-strip` packages the full binary.

Version: the latest git tag `vX.Y.Z`, the same as the application
(`version.pri`; `--version` to override); built after the tag, the commit
short hash is appended (`2.0.0-0566e34`). A release:

```
git tag -a v2.0.0 -m v2.0.0
```

then rerun qmake and build (the version is taken when qmake runs).

By hand:

```
tools/package/make_package.sh --bin-dir <build>/bin_arm --arch Arm32 \
    --strip arm-poky-linux-gnueabi-strip      # SDK environment loaded
```

## Content

```
AcquaThermoNet              binary, stripped (QtMqtt linked statically)
setting.default.ini         configuration template
package.info                name, version, installationFolder, executeAsRoot=true, background=false
install.sh uninstall.sh update.sh run.sh start.sh stop.sh
```

- **run.sh**: entry point called by the launcher. Runs `start.sh` in a
  subshell, keeps `/var/run/acquathermonet-run.pid`, sends the
  `appLoaded`/`appFinished` dbus notifications.
- **start.sh**: environment of the former root `start.sh` (DISPLAY, xcb, Qt
  plugins, virtual keyboard, `lib/` in `LD_LIBRARY_PATH`) and a restart
  loop. Exit status of the application:
  - `0`: stopped on purpose (SIGTERM/SIGINT, relays OFF): loop ends;
  - `3`: another instance already runs on the device: loop ends;
  - anything else or a signal: crash, restarted after 2, 4, 8, 16, 20s
    (back to 2s after a run of at least a minute). The delay stays short
    because the watchdog armed by the crashed instance keeps running until
    the new instance refreshes it.
- **stop.sh**: SIGTERM to the application and wait for the clean shutdown
  (relays OFF confirmed, MQTT offline); killed after 15s.
- **install.sh**: restore of the files saved by `uninstall.sh`,
  `showProgress`/`installFinished` dbus notifications (and soname symlinks
  for libraries in `lib/`, if a package ever bundles some with `--lib-dir`).
- **uninstall.sh**: stops the application and saves `setting.ini`,
  `state.ini` and `log/` to `/tmp/acquathermonet-backup`. The launcher's
  update is uninstall + install (found on these devices for CanGateway),
  so this is what keeps the device configuration, setpoints, MQTT
  `unique_id`, relay history and relay activity log across an update.
- **update.sh**: nothing to do, `updateFinished` notification.

On a fresh install there is no `setting.ini`: the application creates it
from `setting.default.ini` at the first start. Edit it for the device
(broker, zones, relays, serial port) and restart.

## To verify on the device

The launcher contract comes from the CanGateway package, not from
documentation:

- `background=false` is assumed for a full screen HMI (CanGateway is a
  background service): change it with `--background` if the launcher
  wants it otherwise;
- `installationFolder` is `AcquaThermoNet`;
- the `appLoaded` check waits 5s for the process `AcquaThermoNet`.
