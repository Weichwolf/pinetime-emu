#!/bin/bash
# Run all emulator tests. Exit code = number of failures.
set -e

BUILD=${1:-../build}
EMU="$BUILD/pinetime-emu"
PASS=0
FAIL=0
ERRORS=""

# ARM assembly tests (run via emulator)
echo "=== ARM Assembly Tests ==="
for bin in test_*.bin; do
    [ -f "$bin" ] || continue
    name="${bin%.bin}"
    printf "%-30s" "$name"
    output=$("$EMU" --headless --timeout 2000 "$bin" 2>&1)
    rc=$?
    if [ $rc -eq 0 ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    elif [ $rc -eq 124 ]; then
        echo "TIMEOUT"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS  $name: TIMEOUT\n"
    else
        echo "FAIL (test $rc)"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS  $name: test $rc failed\n"
    fi
done

# C++ unit tests (native executables)
echo ""
echo "=== C++ Unit Tests ==="
for exe in "$BUILD"/test_bus "$BUILD"/test_cpu "$BUILD"/test_device_*; do
    [ -x "$exe" ] || continue
    name=$(basename "$exe")
    printf "%-30s" "$name"
    output=$("$exe" 2>&1)
    rc=$?
    if [ $rc -eq 0 ]; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS  $name: exit $rc\n"
        echo "$output" | head -5
    fi
done

echo ""
echo "$((PASS + FAIL)) suites, $PASS passed, $FAIL failed"
if [ -n "$ERRORS" ]; then
    echo ""
    echo "Failures:"
    printf "$ERRORS"
fi
exit $FAIL
