#!/bin/sh
# ab-run2.sh — at_csim channel-leak A/B harness v2 (self-contained AT IO).
# Read-only vs card content; transient logical-channel opens only.
# A = /tmp/ab-A (lpac 083d080, hex-option, NO leak fix)
# B = /tmp/ab-B (lpac 8269852 = A + close-on-abort fix)
# Lock: caller must hold modem-lane (owner ab-leak-demo).
ts() { date -u +%FT%TZ; }
TS=$(date -u +%Y%m%dT%H%M%SZ)
LOG=/root/ab/ab-$TS.log
mkdir -p /root/ab

# --- AT port detection (shuffles across resets) ---
detect_tty() {
  for p in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyUSB2 /dev/ttyUSB3; do
    ( cat "$p" > /tmp/atdet.$$ 2>/dev/null & cp=$!; sleep 1
      printf 'AT\r' > "$p" 2>/dev/null; sleep 2; kill $cp 2>/dev/null ) 2>/dev/null
    if grep -q 'OK' /tmp/atdet.$$ 2>/dev/null; then echo "$p"; break; fi
  done
  rm -f /tmp/atdet.$$
}
TTY=$(detect_tty)
[ -n "$TTY" ] || { echo "$(ts) FATAL: no AT port"; exit 1; }

at() {  # at <wait-s> <command> -> RESP
  f=/tmp/atresp.$$
  cat "$TTY" > "$f" 2>/dev/null & cp=$!
  sleep 1
  printf '%s\r' "$2" > "$TTY" 2>/dev/null
  sleep "$1"
  kill $cp 2>/dev/null
  RESP=$(grep -v '^$' "$f" 2>/dev/null | tr '\n' '|')
  rm -f "$f"
  echo "$(ts) TX[$2] -> $RESP" >> "$LOG"
}

csim_hex() {  # extract hex payload of last +CSIM response from RESP
  printf '%s' "$RESP" | sed -n 's/.*+CSIM: [0-9]*,"\([0-9A-Fa-f]*\)".*/\1/p' | tail -1
}

probe() {  # open a channel; report; close it if opened
  at 3 'AT+CSIM=10,"0070000001"'
  hex=$(csim_hex)
  if [ -z "$hex" ]; then printf 'NO-CSIM-RESPONSE'; return; fi
  case "$hex" in
    *6A81*) printf 'EXHAUSTED(6A81)';;
    *9000*) ch=${hex%????}
            printf 'OPEN-OK ch=%s' "$ch"
            at 3 "AT+CSIM=10,\"007080${ch}00\"";;
    *)      printf 'SW=%s' "$hex";;
  esac
}

run() {  # run <tree> <label>: one lpac chip info attempt
  out=$(LD_LIBRARY_PATH=/tmp/$1/usr/lib LPAC_APDU=at_csim \
        LPAC_APDU_AT_DEVICE=$TTY LPAC_APDU_AT_HEX=UPPER \
        /tmp/$1/usr/bin/lpac chip info 2>&1)
  kept=$(printf '%s\n' "$out" | grep -E 'euicc_init|"eid"|Cleaning up leaked' | head -2 | tr '\n' ' ')
  [ -n "$kept" ] && printf '%s' "$kept" || printf 'OTHER'
}

{
echo "=== AB-RUN2 $TS tty=$TTY (A=083d080 no-fix | B=8269852 fix) ==="
at 3 'AT+QCCID'
echo "--- PRE: QCCID RESP above; channel probe:"
p=$(probe); echo "--- PRE probe: $p"

echo "=== PHASE A (no fix): up to 6 failing euicc_init runs ==="
i=0; exhausted=0
while [ $i -lt 6 ]; do
  i=$((i+1))
  r=$(run ab-A); p=$(probe)
  echo "A/$i: [$r] probe: $p"
  case "$p" in *EXHAUSTED*)
    echo ">>> A-DEMO: pool exhausted after $i failing inits (LEAK CONFIRMED)"
    exhausted=1; break;;
  esac
done
[ $exhausted -eq 0 ] && echo "A: no exhaustion within 6 runs — inspect TX lines above"

echo "--- post-A cleanup: close ch 01..04, verify pool"
for ch in 01 02 03 04; do at 3 "AT+CSIM=10,\"007080${ch}00\""; done
p=$(probe); echo "--- post-A-cleanup probe: $p"

echo "=== PHASE B (fix): 8 failing runs, pool must stay open ==="
i=0
while [ $i -lt 8 ]; do
  i=$((i+1))
  r=$(run ab-B); p=$(probe)
  echo "B/$i: [$r] probe: $p"
  case "$p" in *EXHAUSTED*)
    echo ">>> B-FAIL: pool exhausted at run $i DESPITE FIX"; break;;
  esac
done
echo "=== DONE $TS ==="
} >> "$LOG" 2>&1
cat "$LOG"
