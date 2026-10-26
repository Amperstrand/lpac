#!/bin/sh
# ab-run3.sh — at_csim channel-leak A/B harness v3.
# Patient IO: drain before send, 5s waits. A leaks -> exhaustion; B keeps pool open.
# Read-only vs card content; transient logical-channel opens only.
# Lock: caller holds modem-lane (owner ab-leak-demo).
ts() { date -u +%FT%TZ; }
TS=$(date -u +%Y%m%dT%H%M%SZ)
LOG=/root/ab/ab3-$TS.log
mkdir -p /root/ab

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

at() {  # at <wait-s> <command>; drains 1.5s, captures response
  f=/tmp/atresp.$$
  cat "$TTY" > /tmp/atdrain.$$ 2>/dev/null & dp=$!
  sleep 1.5; kill $dp 2>/dev/null      # drain stale backlog
  cat "$TTY" > "$f" 2>/dev/null & cp=$!
  sleep 0.3
  printf '%s\r' "$2" > "$TTY" 2>/dev/null
  sleep "$1"
  kill $cp 2>/dev/null
  RESP=$(grep -v '^$' "$f" 2>/dev/null | tr '\n' '|')
  rm -f "$f" /tmp/atdrain.$$
  echo "$(ts) TX[$2] -> $RESP" >> "$LOG"
}

csim_hex() {
  printf '%s' "$RESP" | sed -n 's/.*+CSIM: [0-9]*,"\([0-9A-Fa-f]*\)".*/\1/p' | tail -1
}

probe() {  # open channel; report OPEN-OK/EXHAUSTED; close what opened
  at 5 'AT+CSIM=10,"0070000001"'
  hex=$(csim_hex)
  if [ -z "$hex" ]; then printf 'NO-CSIM-RESPONSE'; return; fi
  case "$hex" in
    *6A81*) printf 'EXHAUSTED(6A81)';;
    *9000*) ch=$(printf '%s' "$hex" | sed 's/..$//')
            printf 'OPEN-OK ch=%s' "$ch"
            at 5 "AT+CSIM=10,\"007080${ch}00\"";;
    *)      printf 'SW=%s' "$hex";;
  esac
}

cleanup_pool() {  # close ch 01..04, report final probe
  for ch in 01 02 03 04; do at 5 "AT+CSIM=10,\"007080${ch}00\""; done
}

run() {
  out=$(LD_LIBRARY_PATH=/tmp/$1/usr/lib LPAC_APDU=at_csim \
        LPAC_APDU_AT_DEVICE=$TTY LPAC_APDU_AT_HEX=UPPER \
        /tmp/$1/usr/bin/lpac chip info 2>&1)
  kept=$(printf '%s\n' "$out" | grep -E 'euicc_init|"eid"|Cleaning up leaked' | head -2 | tr '\n' ' ')
  [ -n "$kept" ] && printf '%s' "$kept" || printf 'OTHER'
}

{
echo "=== AB-RUN3 $TS tty=$TTY (A=083d080 no-fix | B=8269852 fix) ==="
at 3 'AT+QCCID'
p=$(probe); echo "--- PRE probe: $p"

echo "--- baseline cleanup (close 01..04)"
cleanup_pool
p=$(probe); echo "--- baseline probe after cleanup: $p"
case "$p" in *OPEN-OK*) ;; *) echo ">>> BASELINE NOT CLEAN — continuing anyway (see TX lines)";; esac

echo "=== PHASE A (no fix): 4 failing runs ==="
i=0; ex=0
while [ $i -lt 4 ]; do
  i=$((i+1))
  r=$(run ab-A); p=$(probe)
  echo "A/$i: [$r] probe: $p"
  case "$p" in *EXHAUSTED*) echo ">>> A-DEMO: pool exhausted after $i failing inits"; ex=1; break;; esac
done
[ $ex -eq 0 ] && echo "A: no exhaustion in 4 runs — inspect TX lines"

echo "--- A cleanup"
cleanup_pool
p=$(probe); echo "--- post-A-cleanup probe: $p"

echo "=== PHASE B (fix): 6 failing runs ==="
i=0
while [ $i -lt 6 ]; do
  i=$((i+1))
  r=$(run ab-B); p=$(probe)
  echo "B/$i: [$r] probe: $p"
  case "$p" in *EXHAUSTED*) echo ">>> B-FAIL: exhausted at run $i DESPITE FIX"; break;; esac
done
echo "=== DONE $TS ==="
} >> "$LOG" 2>&1
cat "$LOG"
