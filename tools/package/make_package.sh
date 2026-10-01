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
strip_tool="${STRIP:-strip}"
cxx=""
do_strip="true"

usage() {
    cat <<EOF
Usage: $0 --bin-dir <dir> --arch <label> [options]

Required:
  --bin-dir <dir>     Directory with the built AcquaThermoNet binary
  --arch <label>      Architecture label in the file name: Arm32, Arm64, x86_64

Options:
  --lib-dir <dir>     Shared libraries to bundle in deploy/lib/ (not needed
                      today: QtMqtt and QtSerialPort are static, the rest
                      comes from the device); only
                      the real files are packaged, install.sh makes the
                      soname symlinks. Default: none
  --version <ver>     Package version (default: the latest git tag vX.Y.Z,
                      plus the commit short hash when built after it, e.g.
                      2.0.0-0566e34; same as the application, version.pri)
  --out-dir <dir>     Where to write the zip (default: <bin-dir>/../dist)
  --no-root           executeAsRoot=false (default true: watchdog, serial port)
  --background        background=true (default false: full screen HMI)
  --strip <tool>      strip of the target toolchain (default: \$STRIP, else
                      strip); looked up in PATH, then next to --cxx
  --cxx <compiler>    C++ compiler of the build (its directory is searched
                      for the strip tool when it is not in PATH)
  --no-strip          package the binary with its debug information
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
        --strip) strip_tool="$2"; shift 2 ;;
        --cxx) cxx="$2"; shift 2 ;;
        --no-strip) do_strip="false"; shift ;;
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

# Layout of the panel packages: descriptor and launcher hooks at the top,
# the application with its start.sh (and working directory) in deploy/
deploy="$staging/deploy"
mkdir -p "$deploy"
cp "$binary" "$deploy/AcquaThermoNet"

# Debug information stripped from the packaged binary (about 30x smaller);
# the full binary is kept next to the zip, with the same version in the
# name, to read the core dumps of the device: gdb <file>.debug core
debug_copy=""
if [ "$do_strip" = "true" ]; then
    strip_path="$(command -v "$strip_tool" 2>/dev/null || true)"
    if [ -z "$strip_path" ] && [ -n "$cxx" ]; then
        # e.g. Qt Creator without the SDK environment: the compiler is
        # reachable (it just built the binary), the strip is next to it
        cxx_path="$(command -v "${cxx%% *}" 2>/dev/null || true)"
        if [ -n "$cxx_path" ] && [ -x "$(dirname "$cxx_path")/$(basename "$strip_tool")" ]; then
            strip_path="$(dirname "$cxx_path")/$(basename "$strip_tool")"
        fi
    fi
    if [ -z "$strip_path" ]; then
        echo "error: strip tool '$strip_tool' not found (pass --strip, or --no-strip)" >&2
        exit 1
    fi
    # a strip of another architecture fails here instead of packaging garbage
    if ! "$strip_path" --strip-unneeded "$deploy/AcquaThermoNet"; then
        echo "error: '$strip_path' cannot strip $binary (wrong toolchain?)" >&2
        exit 1
    fi
    debug_copy="$out_dir/AcquaThermoNet_${arch}_${version}.debug"
    cp "$binary" "$debug_copy"
fi
cp "$repo_root/setting.default.ini" "$deploy/"
cp "$template_dir/start.sh" "$deploy/"
for f in install.sh uninstall.sh update.sh run.sh stop.sh; do
    cp "$template_dir/$f" "$staging/"
done
sed -e "s/@VERSION@/$version/" \
    -e "s/@EXECUTE_AS_ROOT@/$execute_as_root/" \
    -e "s/@BACKGROUND@/$background/" \
    "$template_dir/package.info.in" > "$staging/package.info"

if [ -n "$lib_dir" ]; then
    mkdir -p "$deploy/lib"
    # fully versioned files only (libX.so.A.B.C): install.sh recreates the
    # .so.A.B / .so.A / .so names (copies of them in lib_dir are skipped)
    find "$lib_dir" -maxdepth 1 -regex '.*\.so\.[0-9]+\.[0-9]+\.[0-9]+' -exec cp -L {} "$deploy/lib/" \;
fi

chmod +x "$deploy/AcquaThermoNet" "$deploy/start.sh" "$staging"/*.sh

out_zip="$out_dir/AcquaThermoNet_Package_${arch}_${version}.zip"
rm -f "$out_zip"
( cd "$staging" && zip -q -X -r "$out_zip" . )

echo "Package written to $out_zip (version $version, executeAsRoot=$execute_as_root, background=$background)"
if [ -n "$debug_copy" ]; then
    echo "Binary with debug information: $debug_copy"
else
    echo "Binary not stripped (--no-strip)"
fi
