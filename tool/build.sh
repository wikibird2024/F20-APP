#!/usr/bin/env bash
set -Eeuo pipefail

# UNCOMMENT THE NEXT LINE IF YOU WANT BASH TO PRINT EVERY COMMAND EXECUTED:
# set -x

# CMake build: configure + build a preset, check the output, optionally copy it.
#
#   ./tool/build.sh              first preset in PRESETS
#   ./tool/build.sh Release      any preset listed in PRESETS
#
# From the dotfiles template project_scripts/build.sh; only the settings
# block below (and the cd to the project root) changes between projects.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
cd "$SCRIPT_DIR/.."   # the script lives in tool/, the project is one up

###############################################################################
# Project settings -- edit these for each project
###############################################################################

PRESETS=(Debug Release asan)  # CMakePresets.json names; the first one is the default
BUILD_ROOT="build"       # the presets' binaryDir without the preset name
ARTIFACTS=()             # no firmware files in this project
PROGRAM="F20APP"         # PC program: executable name in $BUILD_DIR/bin
COPY_DIR=""              # nothing to copy
COPY_TYPES=()

###############################################################################

PRESET="${1:-${PRESETS[0]}}"
BUILD_DIR="$BUILD_ROOT/$PRESET"

step() {
    echo
    echo "=== [$((++STEP))/$STEPS] $1 ==="
}

ok() {
    echo "[SUCCESS] $1"
}

warn() {
    echo "[WARNING] $1" >&2
}

fail() {
    echo "[ERROR] $1" >&2
    exit 1
}

[[ " ${PRESETS[*]} " == *" $PRESET "* ]] ||
    fail "Unknown preset '$PRESET' -- use one of: ${PRESETS[*]} (see CMakePresets.json)"

STEP=0
STEPS=3
[[ -n "$COPY_DIR" ]] && STEPS=4
START=$SECONDS

###############################################################################
# Configure
###############################################################################

step "Configuring $PRESET"

# Cheap no-op when $BUILD_DIR is current; recreates a deleted or stale build
# tree (fresh clone, generator re-run), which a build-only script can't.
if cmake --preset "$PRESET"; then
    echo
    ok "Configure succeeded"
else
    echo
    fail "Configure failed"
fi

###############################################################################
# Build
###############################################################################

step "Building $PRESET"

# NOTE: If you need to see raw full compiler commands, append '-- -v' to the cmake line below:
# cmake --build --preset "$PRESET" -- -v
if cmake --build --preset "$PRESET"; then
    echo
    ok "Build succeeded"
else
    echo
    fail "Build failed"
fi

###############################################################################
# Verify output
###############################################################################

step "Verify output"

# One file per type: two .hex files would mean a stale one could get flashed.
declare -A FOUND=()
for type in "${ARTIFACTS[@]}"; do
    shopt -s nullglob
    files=("$BUILD_DIR"/*."$type")
    shopt -u nullglob
    case ${#files[@]} in
        0) fail "No .$type found in $BUILD_DIR" ;;
        1) FOUND[$type]="${files[0]}" ;;
        *)
            echo "[ERROR] Multiple .$type files found:" >&2
            printf '  %s\n' "${files[@]}" >&2
            exit 1
            ;;
    esac
    ok "Found $(basename "${FOUND[$type]}")"
done

if [[ -n "$PROGRAM" ]]; then
    PROGRAM_FILE="$BUILD_DIR/bin/$PROGRAM"
    [[ -x "$PROGRAM_FILE" ]] || fail "No $PROGRAM_FILE after the build"
    ok "Found $PROGRAM_FILE"
    # pgrep -x only matches the first 15 characters of a name, so match the command line.
    if pgrep -f "(^|/)$PROGRAM( |$)" >/dev/null; then
        warn "$PROGRAM is running -- close and start it again to use this build"
    fi
fi

[[ ${#ARTIFACTS[@]} -eq 0 && -z "$PROGRAM" ]] && echo "Nothing to check (ARTIFACTS and PROGRAM are empty)"

###############################################################################
# Copy
###############################################################################

COPIED="-"
if [[ -n "$COPY_DIR" ]]; then
    step "Copy to $COPY_DIR"

    # Missing parent = the other project isn't checked out here: warn, don't fail the build.
    if [[ -d "$(dirname "$COPY_DIR")" ]]; then
        mkdir -p "$COPY_DIR"
        for type in "${COPY_TYPES[@]}"; do
            [[ -n "${FOUND[$type]:-}" ]] || fail "COPY_TYPES has '$type', which is not in ARTIFACTS"
            cp -f "${FOUND[$type]}" "$COPY_DIR/"
        done
        COPIED="$(cd "$COPY_DIR" && pwd)"
        ok "Copied ${COPY_TYPES[*]} to $COPIED"
    else
        COPIED="(not copied: $(dirname "$COPY_DIR") not found)"
        warn "$(dirname "$COPY_DIR") not found -- nothing copied"
    fi
fi

###############################################################################
# Summary
###############################################################################

ELAPSED=$((SECONDS - START))

echo
echo "=== SUMMARY ==="
echo "Preset   : $PRESET"
echo "Output   : $BUILD_DIR"
for type in "${ARTIFACTS[@]}"; do
    printf 'Firmware : %s\n' "$(basename "${FOUND[$type]}")"
done
[[ -n "$PROGRAM" ]] && echo "Run      : ./$PROGRAM_FILE"
[[ -n "$COPY_DIR" ]] && echo "Copied   : $COPIED"
echo "Duration : ${ELAPSED}s"
