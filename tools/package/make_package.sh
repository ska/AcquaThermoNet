#!/usr/bin/env bash
# Builds the deployable AcquaThermoNet package zip for the device launcher
# (JMLauncher): package.info + install/uninstall/update/run/start/stop.sh
# hooks + payload, same layout as the CanGateway package for these devices
# (see tools/package/README.md).
#
# Run automatically after every link of an ARM build (AcquaThermoNet.pro),
# or by hand:
#   tools/package/make_package.sh --bin-dir build/bin_arm --arch Arm32

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
template_dir="$script_dir/template"

bin_dir=""
lib_dir=""
arch=""
version=""
out_dir=""
execute_as_root="true"
background="false"

usage() {
    cat <<EOF
Usage: $0 --bin-dir <dir> --arch <label> [options]

Required:
  --bin-dir <dir>     Directory with the built AcquaThermoNet binary
  --arch <label>      Architecture label in the file name: Arm32, Arm64, x86_64

Options:
  --lib-dir <dir>     Shared libraries to bundle in lib/ (not needed today:
                      QtMqtt is static, the rest comes from the device); only
                      the real files are packaged, install.sh makes the
                      soname symlinks. Default: none
  --version <ver>     Package version (default: the latest git tag vX.Y.Z,
                      plus the commit short hash when built after it, e.g.
                      2.0.0-0566e34; same as the application, version.pri)
  --out-dir <dir>     Where to write the zip (default: <bin-dir>/../dist)
  --no-root           executeAsRoot=false (default true: watchdog, serial port)
  --background        background=true (default false: full screen HMI)
  -h, --help          Show this help
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --bin-dir) bin_dir="$2"; shift 2 ;;
        --lib-dir) lib_dir="$2"; shift 2 ;;
        --arch) arch="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        --out-dir) out_dir="$2"; shift 2 ;;
        --no-root) execute_as_root="false"; shift ;;
        --background) background="true"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ -z "$bin_dir" ] || [ -z "$arch" ]; then
    echo "error: --bin-dir and --arch are required" >&2
    usage >&2
    exit 2
fi

binary="$bin_dir/AcquaThermoNet"
if [ ! -f "$binary" ]; then
    echo "error: '$binary' not found, build it first" >&2
    exit 1
fi

if [ -z "$version" ]; then
    # Same source as the application version (version.pri)
    version="$(git -C "$repo_root" describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')"
    if [ -z "$version" ]; then
        echo "error: could not determine version from the latest git tag; pass --version" >&2
        exit 1
    fi
    # Commits after the tag: short hash appended, e.g. 2.0.0-0566e34
    if ! git -C "$repo_root" describe --tags --exact-match HEAD >/dev/null 2>&1; then
        version="$version-$(git -C "$repo_root" rev-parse --short HEAD)"
    fi
fi

if [ -z "$out_dir" ]; then
    out_dir="$bin_dir/../dist"
fi
mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT

cp "$binary" "$staging/AcquaThermoNet"
cp "$repo_root/setting.default.ini" "$staging/"
for f in install.sh uninstall.sh update.sh run.sh start.sh stop.sh; do
    cp "$template_dir/$f" "$staging/"
done
sed -e "s/@VERSION@/$version/" \
    -e "s/@EXECUTE_AS_ROOT@/$execute_as_root/" \
    -e "s/@BACKGROUND@/$background/" \
    "$template_dir/package.info.in" > "$staging/package.info"

if [ -n "$lib_dir" ]; then
    mkdir -p "$staging/lib"
    # fully versioned files only (libX.so.A.B.C): install.sh recreates the
    # .so.A.B / .so.A / .so names (copies of them in lib_dir are skipped)
    find "$lib_dir" -maxdepth 1 -regex '.*\.so\.[0-9]+\.[0-9]+\.[0-9]+' -exec cp -L {} "$staging/lib/" \;
fi

chmod +x "$staging/AcquaThermoNet" "$staging"/*.sh

out_zip="$out_dir/AcquaThermoNet_Package_${arch}_${version}.zip"
rm -f "$out_zip"
( cd "$staging" && zip -q -X -r "$out_zip" . )

echo "Package written to $out_zip (version $version, executeAsRoot=$execute_as_root, background=$background)"
