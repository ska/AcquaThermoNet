# AcquaThermoNet: build, test, package

A quick reference for building AcquaThermoNet for each target, running the
tests and making the package zip for the panel. Full details are in
[`DOCUMENTATION.md`](DOCUMENTATION.md) §18–20 and
[`tools/package/README.md`](../tools/package/README.md).

Always build **outside the source tree**, in a directory of your own. In
the examples, `$ATN` is the repository checkout:

```sh
ATN=/home/devel/Sviluppi/Pers/HA_Hmi_Relay
```

---

## 1. Once, after the clone

```sh
cd $ATN
git submodule update --init
```

The submodules provide QtMqtt, QtSerialPort (both linked statically, also
on the panel, which has neither) and OpenSSL 1.1 for the desktop. The
first build compiles them, so it takes longer.

---

## 2. Targets

| Target | Toolchain | Output |
|---|---|---|
| Desktop x86_64 (reference) | Qt 5.13.2, `/home/devel/Sviluppi/Qt/5.13.2/bin/qmake` | `bin_x86_64/AcquaThermoNet` |
| Desktop x86_64 (optional) | Qt 5.15.x qmake, system OpenSSL 3 | `bin_x86_64/AcquaThermoNet` |
| Panel UN60 (Cortex-A8) | SDK `/home/devel/Sviluppi/Sdk/1.3.4-un60`, Qt 5.13.2 | `bin_arm/AcquaThermoNet` + package zip in `dist/` |

> **Never use `Sdk/1.3.x` for the panel.** It is the UN83 SDK (Cortex-A7):
> its binaries stop with `Illegal instruction` on the UN60. Check with
> `readelf -A <binary> | grep Tag_CPU_name`: it must be `"7-A"`, not `7VE`.

### 2.1 Desktop, Qt 5.13.2

```sh
mkdir -p ~/build/atn-desktop && cd ~/build/atn-desktop
/home/devel/Sviluppi/Qt/5.13.2/bin/qmake $ATN/AcquaThermoNet.pro
make -j$(nproc)
./bin_x86_64/AcquaThermoNet --version
```

With Qt < 5.15 the build also compiles OpenSSL 1.1.1w from
`ThirdParty/openssl`, which the application loads at start (MQTT TLS,
HTTPS for the weather and Telegram).

### 2.2 Desktop, Qt 5.15

The same commands with the Qt 5.15 `qmake`. OpenSSL is the system one; no
other difference. The code must build with both Qt versions.

### 2.3 Panel UN60

Load the SDK environment in the shell, then use the `qmake` it puts in
`PATH`:

```sh
source /home/devel/Sviluppi/Sdk/1.3.4-un60/environment-setup-cortexa8hf-neon-poky-linux-gnueabi
mkdir -p ~/build/atn-un60 && cd ~/build/atn-un60
qmake $ATN/AcquaThermoNet.pro
make -j$(nproc)
ls dist/
```

Every ARM link also makes the package zip (§4). In Qt Creator the kit of
the panel must point to `Sdk/1.3.4-un60`.

### 2.4 qmake options

| Option | Effect |
|---|---|
| `CONFIG+=tests` | also build the unit tests (`tests/tests`) |
| `CONFIG+=tools` | also build `guishot`, the GUI screenshot tool (desktop) |
| `CONFIG+=package` | desktop: make the package zip too |
| `CONFIG+=nopackage` | ARM: no package zip |

### 2.5 Version

The version comes from the latest git tag `vX.Y.Z` **when qmake runs**:
`2.4.0` exactly at the tag, `2.4.0-<hash>` after it, `0.0.0` without a
tag. Rerun qmake after a commit or a tag, or the binary and the zip keep
the old version.

---

## 3. Tests

What each test checks: [`TESTING.md`](TESTING.md).

### 3.1 Unit tests

No broker, relay board or panel needed (desktop build):

```sh
mkdir -p ~/build/atn-tests && cd ~/build/atn-tests
/home/devel/Sviluppi/Qt/5.13.2/bin/qmake $ATN/AcquaThermoNet.pro CONFIG+=tests
make -j$(nproc)
QT_QPA_PLATFORM=offscreen ./tests/tests
```

All the suites run in one go (`tests/main.cpp`); each prints a `Totals:`
line, and the exit status is 0 only when every test passes.
Quick check: `./tests/tests 2>&1 | grep -E '^Totals|FAIL'`.

### 3.2 End to end

Runs the real application (offscreen) against local simulators of the
MQTT broker, the Modbus relay board and the Telegram API, and checks
start, regulation, chrono, shutdown, state files and logs:

```sh
$ATN/tools/e2e.sh ~/build/atn-tests/bin_x86_64/AcquaThermoNet
```

**Only when no other AcquaThermoNet instance is running on the desktop**:
a second instance exits at once with code 3.

### 3.3 GUI screenshots

```sh
qmake $ATN/AcquaThermoNet.pro CONFIG+=tools && make -j$(nproc)
QT_QPA_PLATFORM=offscreen tools/guishot/guishot setting.ini gui.png
```

Other pages: `chrono`, `chrono=N`, `edit=N` as last argument
(DOCUMENTATION.md §20.3).

---

## 4. Package zip for the panel

### 4.1 From a build

The UN60 build (§2.3) makes, after every link:

```
dist/AcquaThermoNet_Package_Arm32_<version>.zip     installed by JMLauncher
dist/AcquaThermoNet_Arm32_<version>.debug           not stripped, keep it for core dumps
```

The binary in the zip is stripped. A `make` that relinks nothing makes no
new zip: after a qmake rerun for the version, `make` relinks.

By hand, from an existing build (SDK environment loaded):

```sh
$ATN/tools/package/make_package.sh --bin-dir ~/build/atn-un60/bin_arm --arch Arm32 \
    --strip arm-poky-linux-gnueabi-strip
```

### 4.2 Release package

A package to keep or install is built from a **tag**, in a clean
checkout, so that the version is exactly the tag (after it: `-<hash>`;
the application banner also says `-dirty` with uncommitted changes):

```sh
git -C $ATN worktree add /tmp/atn-v2.4.0 v2.4.0
cd /tmp/atn-v2.4.0 && git submodule update --init
source /home/devel/Sviluppi/Sdk/1.3.4-un60/environment-setup-cortexa8hf-neon-poky-linux-gnueabi
mkdir build && cd build && qmake ../AcquaThermoNet.pro && make -j$(nproc)
```

Before installing, check:

| Check | Command | Expected |
|---|---|---|
| version | `unzip -p dist/*.zip package.info` | `<version>2.4.0</version>`, no hash |
| CPU | `readelf -A bin_arm/AcquaThermoNet \| grep Tag_CPU_name` | `"7-A"` |
| stripped | `unzip -l dist/*.zip` | `deploy/AcquaThermoNet` about 0.6 MB (17 MB not stripped) |
| checksum | `(cd dist && sha256sum *.zip > AcquaThermoNet_Package_Arm32_2.4.0.zip.sha256)` | kept with the zip |

Then store the zip and the `.debug` together, and remove the worktree
(`git -C $ATN worktree remove /tmp/atn-v2.4.0`).

### 4.3 Install

Copy the zip to the panel and install it from the launcher. An update
keeps `setting.ini`, `state.ini` and `log/` (saved by `uninstall.sh`,
restored by `install.sh`). On a first install the application creates
`setting.ini` from `setting.default.ini`: edit it (broker, zones, relays,
serial port) and restart.
