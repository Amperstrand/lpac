#!/bin/bash
# run-ab.sh — deterministic host A/B for the at_csim channel-leak fix.
# A = build-A (lpac 083d080, no fix); B = build-B (lpac 8269852, fix).
# Each side: start mock-modem (detached, logs to file), run `lpac chip info`
# against its pty, kill mock, print artifacts.
set -u
cd /tmp/opencode/ab-host || exit 1

for side in A B; do
  rm -f "pty-$side.txt" "mock-$side.log" "run-$side.out"
  python3 mock-modem.py "mock-$side.log" "pty-$side.txt" >/dev/null 2>&1 &
  MPID=$!
  for _ in $(seq 1 25); do [ -s "pty-$side.txt" ] && break; sleep 0.2; done
  timeout 25 env LPAC_APDU=at_csim LPAC_APDU_AT_DEVICE="$(cat "pty-$side.txt" 2>/dev/null)" \
    LPAC_APDU_AT_HEX=UPPER "build-$side/src/lpac" chip info > "run-$side.out" 2>&1
  RC=$?
  sleep 1
  kill -9 "$MPID" 2>/dev/null
  echo "=== side $side: lpac rc=$RC ==="
done

echo
echo "=========== RUN A (lpac output, no-fix 083d080) ==========="
cat run-A.out 2>/dev/null
echo
echo "=========== MOCK A LOG (no-fix 083d080) ==========="
cat mock-A.log 2>/dev/null
echo
echo "=========== RUN B (lpac output, fixed 8269852) ==========="
cat run-B.out 2>/dev/null
echo
echo "=========== MOCK B LOG (fixed 8269852) ==========="
cat mock-B.log 2>/dev/null
echo
echo "=========== VERDICT ==========="
closes_A=$(grep -c "CLOSE-RECEIVED" mock-A.log 2>/dev/null || true)
closes_B=$(grep -c "CLOSE-RECEIVED" mock-B.log 2>/dev/null || true)
final_A=$(grep "FINAL STATE" mock-A.log 2>/dev/null || echo "none")
final_B=$(grep "FINAL STATE" mock-B.log 2>/dev/null || echo "none")
echo "A (no-fix): closes=$closes_A  $final_A"
echo "B (fix):    closes=$closes_B  $final_B"
