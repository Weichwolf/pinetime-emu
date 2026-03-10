#!/bin/bash
# Run all emulator tests. Exit code = number of failures.
set -e

EMU=${1:-../build/pinetime-emu}
PASS=0
FAIL=0
ERRORS=""

for bin in test_*.bin; do
    name="${bin%.bin}"
    printf "%-25s" "$name"
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

echo ""
echo "$((PASS + FAIL)) tests, $PASS passed, $FAIL failed"
if [ -n "$ERRORS" ]; then
    echo ""
    echo "Failures:"
    printf "$ERRORS"
fi
exit $FAIL
