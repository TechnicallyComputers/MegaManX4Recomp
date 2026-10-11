#!/usr/bin/env bash
# Build MegaManX4Recomp natively on Linux.
#
#   1. build the psxrecomp-game recompiler from the framework
#   2. extract the boot EXE to mmx4/SLUS_005.61 from --disc if absent (local only)
#   3. regenerate generated/SLUS_005.61_*.c (never hand-edit generated/)
#   4. configure + build the runtime with CMake
#
# Needs your own legally obtained disc image (see DISC.md); nothing is bundled.
#
# usage: tools/build_linux.sh [options]
#   --build-dir DIR     runtime build dir            (default: build)
#   --type TYPE         Release|RelWithDebInfo|Debug (default: Release)
#   --sdl 2|3           SDL backend                  (default: 3)
#   --jobs N            parallel jobs                (default: nproc)
#   --skip-regen        reuse existing generated/
#   --debug-tools       enable PSX_DEBUG_TOOLS (TCP debug server)
#   --disc FILE         .cue/.bin/.iso; extracts mmx4/SLUS_005.61 if missing
#   --dev-lobby-list    lobby browser lists all versions (join still needs exact match)
#   --clean             wipe the runtime build dir first
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fw=${PSXRECOMP_ROOT:-"$root/psxrecomp-v4"}
build=build; type=Release; sdl=3; jobs=$(nproc); regen=1; debug=OFF; devlist=OFF; clean=0; disc=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir) build=$2; shift 2 ;;
        --type) type=$2; shift 2 ;;
        --sdl) sdl=$2; shift 2 ;;
        --jobs) jobs=$2; shift 2 ;;
        --skip-regen) regen=0; shift ;;
        --debug-tools) debug=ON; shift ;;
        --dev-lobby-list) devlist=ON; shift ;;
        --clean) clean=1; shift ;;
        --disc) disc=$2; shift 2 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
case "$build" in /*) ;; *) build=$root/$build ;; esac

for tool in cmake cc c++ make python3; do
    command -v "$tool" >/dev/null || { echo "missing required tool: $tool" >&2; exit 1; }
done
[ -f "$fw/runtime/runtime.cmake" ] || {
    echo "framework not found at $fw (run: git submodule update --init --recursive)" >&2
    exit 1
}

cd "$root"

if [ "$regen" = 1 ]; then
    emitter_dir=$fw/recompiler/build
    echo "==> building recompiler"
    cmake -S "$fw/recompiler" -B "$emitter_dir" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$emitter_dir" -j"$jobs" --target psxrecomp-game
    emitter=$emitter_dir/psxrecomp-game

    if [ ! -f mmx4/SLUS_005.61 ]; then
        [ -n "$disc" ] || {
            echo "missing mmx4/SLUS_005.61: pass --disc path/to/'Mega Man X4.cue'" >&2
            exit 1
        }
        echo "==> preparing disc + extracting boot EXE"
        python3 "$fw/tools/prepare_disc.py" "$disc" --config game.toml \
            --out-dir mmx4 --bin-name "Mega Man X4" --cue-name "Mega Man X4" \
            --boot-exe SLUS_005.61 --skip-hash-check
        [ -f mmx4/SLUS_005.61 ] || { echo "prepare_disc did not produce mmx4/SLUS_005.61" >&2; exit 1; }
    fi

    echo "==> regenerating game C"
    "$emitter" --config game.toml
fi

compgen -G "generated/SLUS_005.61_full*.c" >/dev/null || {
    echo "generated/SLUS_005.61_full*.c missing; run without --skip-regen" >&2
    exit 1
}

[ "$clean" = 1 ] && rm -rf "$build"
echo "==> configuring ($type, SDL$sdl)"
cmake -S "$root" -B "$build" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE="$type" -DPSX_SDL_BACKEND="SDL$sdl" \
    -DPSX_DEBUG_TOOLS="$debug" -DPSX_LOBBY_DEV_LIST="$devlist"

echo "==> building runtime"
cmake --build "$build" -j"$jobs"

exe=$(find "$build" -maxdepth 1 -type f -name 'MegaManX4Recomp*' -perm -u+x | head -1)
echo "==> done: ${exe:-$build}"
