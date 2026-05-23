#!/usr/bin/env bash
# tests/run_tests.sh – regression test suite for the Apple 1 emulator
#
# Usage: bash tests/run_tests.sh [<build-dir>]
#
# Exit 0 = all tests passed.  Exit 1 = at least one failure.

set -uo pipefail

BUILD="${1:-build}"
EMU="./${BUILD}/apple1emu"
PASS=0; FAIL=0

###############################################################################
# Helper: pipe INPUT into emulator, wait TSECS, check for expected strings.
# run_test <name> <tsecs> <input_keys> <expected…> -- <emu_args…>
###############################################################################
run_test() {
    local name="$1"; shift
    local tsecs="$1"; shift
    local input="$1"; shift

    local expected=()
    while [[ "$1" != "--" ]]; do expected+=("$1"); shift; done
    shift  # consume "--"

    local tmpout; tmpout=$(mktemp /tmp/a1test.XXXXXX)

    # Pipe input into the emulator in background, kill after timeout
    printf '%s' "$input" | "$EMU" "$@" >"$tmpout" 2>&1 &
    local pid=$!
    sleep "$tsecs"
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null || true

    local output; output=$(cat "$tmpout"); rm -f "$tmpout"
    local ok=1
    for expect in "${expected[@]}"; do
        if ! printf '%s' "$output" | grep -qF -- "$expect"; then
            ok=0
            echo "  MISSING: '$expect'"
        fi
    done

    if [[ "$ok" -eq 1 ]]; then
        echo "[PASS] $name"
        ((PASS++)) || true
    else
        echo "[FAIL] $name"
        printf '  Output (first 3 lines): %s\n' "$(printf '%s' "$output" | head -3)"
        ((FAIL++)) || true
    fi
}

###############################################################################
cd "$(dirname "$0")/.."
echo "=== Apple 1 Emulator Tests ==="

# 1. Wozmon prompt
run_test "wozmon_mini: boots" 2 $'\r' \
    '\' -- --rom roms/wozmon_mini.rom --fast

# 2. Echo typed input
run_test "wozmon_mini: echoes HELLO" 2 $'HELLO\r' \
    'HELLO' '\' -- --rom roms/wozmon_mini.rom --fast

# 3. Memory examine
run_test "wozmon_mini: examine 0000" 2 $'0000\r' \
    '0000' -- --rom roms/wozmon_mini.rom --fast

# 4. Multibank loader boots (menu entry M) visible)
run_test "multibank: loader menu" 5 $'\r\r\r\r\r\r\r\r' \
    'M)' -- --rom roms/32KA1COMPIL.BIN --multibank --fast

# 5. Multibank: W key → Wozmon prompt
run_test "multibank: W→Wozmon2" 5 $'\r\r\r\r\r\r\r\rW\r' \
    '\' -- --rom roms/32KA1COMPIL.BIN --multibank --fast

###############################################################################
echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[[ "$FAIL" -eq 0 ]]
