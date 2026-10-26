#!/usr/bin/env python3
"""Mock AT modem for at_csim driver A/B testing (host, pty-based).

Simulates the RG502Q-EA failure signature observed on the bench:
  - MANAGE CHANNEL open  -> channel allocated, SW 9000
  - ISD-R SELECT by AID  -> SW 6A82 (session cannot establish)
  - MANAGE CHANNEL close -> SW 9000 (logged prominently)

Every command the driver sends is logged. A leaky driver shows: open,
SELECT-fail, then the process exits with the channel still allocated
(no close). A fixed driver shows a close after the SELECT failure.

Usage: mock-modem.py <logfile> <ptyfile>
"""
import os
import pty
import re
import sys
import termios
import time

logfile, ptyfile = sys.argv[1], sys.argv[2]
master, slave = pty.openpty()

# no echo on the pty — emulate ATE0
attrs = termios.tcgetattr(slave)
attrs[3] &= ~termios.ECHO
termios.tcsetattr(slave, termios.TCSANOW, attrs)

with open(ptyfile, "w") as f:
    f.write(os.ttyname(slave))

log = open(logfile, "w", buffering=1)


def ts():
    return time.strftime("%H:%M:%S", time.gmtime()) + ".%03d" % (int(time.time() * 1000) % 1000)


def send(line):
    os.write(master, ("\r\n%s\r\n" % line).encode())


def ok():
    send("OK")


open_channels = []
buf = b""
log.write("%s mock-modem start; pty=%s\n" % (ts(), os.ttyname(slave)))

while True:
    try:
        data = os.read(master, 1024)
    except OSError:
        break
    if not data:
        break
    buf += data
    while re.search(rb"[\r\n]", buf):
        m = re.search(rb"[\r\n]", buf)
        line = buf[: m.start()].decode(errors="replace").strip()
        buf = buf[m.end():]
        if not line:
            continue
        log.write("%s TX: %s\n" % (ts(), line))
        if not line.upper().startswith("AT"):
            continue
        m2 = re.search(r'AT\+CSIM=(\d+),"?([0-9A-Fa-f]+)"?', line)
        if not m2:
            ok()
            log.write("%s RX: OK\n" % ts())
            continue
        hexin = m2.group(2).upper()
        if len(hexin) < 8:
            send("ERROR")
            log.write("%s RX: ERROR (short payload)\n" % ts())
            continue
        ins = hexin[2:4]
        if ins == "70":  # MANAGE CHANNEL
            p1, p2 = hexin[4:6], hexin[6:8]
            if p1 == "00":  # open
                ch = 1 if 1 not in open_channels else max(open_channels) + 1
                open_channels.append(ch)
                send('+CSIM: 6,"%02X9000"' % ch)
                ok()
                log.write(
                    "%s RX: OPEN -> channel %02X, SW 9000 [open=%s]\n"
                    % (ts(), ch, open_channels)
                )
            elif p1 == "80":  # close
                ch = int(p2, 16)
                if ch in open_channels:
                    open_channels.remove(ch)
                    send('+CSIM: 4,"9000"')
                    ok()
                    log.write(
                        "%s *** CLOSE-RECEIVED channel %02X -> SW 9000 [open=%s]\n"
                        % (ts(), ch, open_channels)
                    )
                else:
                    send('+CSIM: 4,"6A86"')
                    ok()
                    log.write(
                        "%s RX: close of non-open channel %02X -> 6A86\n" % (ts(), ch)
                    )
            else:
                send("ERROR")
                log.write("%s RX: ERROR (MANAGE CHANNEL P1=%s)\n" % (ts(), p1))
        elif ins == "A4":  # SELECT — scripted failure (this modem's class)
            send('+CSIM: 4,"6A82"')
            ok()
            log.write(
                "%s RX: SELECT -> SW 6A82 (scripted session failure) [open=%s]\n"
                % (ts(), open_channels)
            )
        else:
            send("ERROR")
            log.write("%s RX: ERROR (unhandled INS %s)\n" % (ts(), ins))

log.write("%s mock-modem end; FINAL STATE open_channels=%s\n" % (ts(), open_channels))
