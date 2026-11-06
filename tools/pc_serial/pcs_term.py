#!/usr/bin/env python3
"""PCS serial terminal for the PC: talks to the Mega WITHOUT resetting it.

The CH340 resets the Mega when a program opens the port with DTR on (Arduino IDE Serial Monitor always does).
This opens it with DTR and RTS off, so the controller keeps running (no re-boot, no V2 re-park).

Needs Python 3 and pyserial:   py -m pip install pyserial
Run:                           py pcs_term.py              (finds the CH340 by itself)
                               py pcs_term.py COM5         (or name the port)
                               py pcs_term.py --log pcs.csv   (also appends every received line to a file)
Type a command (?, h, m2, csv, Rad=-15, ...) and press Enter. Ctrl+C quits.
If the Mega still resets on open, you'll see "# PCS controller fw ..." right after connecting: tell Claude.
"""
import argparse
import sys
import threading
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial missing: run  py -m pip install pyserial")

BAUD = 115200
CH340_VID = 0x1A86


def find_port():
    ports = list(list_ports.comports())
    for p in ports:
        if p.vid == CH340_VID:
            return p.device
    if len(ports) == 1:
        return ports[0].device
    names = ", ".join(p.device for p in ports) or "none"
    sys.exit(f"no CH340 found (ports: {names}); give the port, e.g.  py pcs_term.py COM5")


def open_port(name):
    s = serial.Serial()
    s.port = name
    s.baudrate = BAUD
    s.timeout = 0.2
    s.dtr = False          # set BEFORE open: the CH340 resets the Mega on DTR
    s.rts = False
    s.open()
    return s


def reader(s, log, stop):
    t0 = time.time()
    buf = b""
    while not stop.is_set():
        try:
            chunk = s.read(256)
        except serial.SerialException as e:
            print(f"\n[port lost: {e}]")
            stop.set()
            return
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode(errors="replace").rstrip("\r")
            if text.startswith("# PCS controller fw") and time.time() - t0 < 3:
                text += "   <-- the Mega reset on connect (DTR still toggled by the driver)"
            print(text, flush=True)
            if log:
                log.write(text + "\n")
                log.flush()


def main():
    ap = argparse.ArgumentParser(description="PCS serial terminal (no reset on connect)")
    ap.add_argument("port", nargs="?", help="COM port, e.g. COM5 (default: find the CH340)")
    ap.add_argument("--log", help="append received lines to this file")
    a = ap.parse_args()

    name = a.port or find_port()
    s = open_port(name)
    log = open(a.log, "a", encoding="utf-8") if a.log else None
    print(f"[connected to {name} at {BAUD}, DTR off. Type commands, Ctrl+C quits]")

    stop = threading.Event()
    threading.Thread(target=reader, args=(s, log, stop), daemon=True).start()
    try:
        while not stop.is_set():
            cmd = sys.stdin.readline()
            if not cmd:
                break
            cmd = cmd.strip()
            if cmd:
                s.write((cmd + "\n").encode())
    except KeyboardInterrupt:
        pass
    stop.set()
    s.close()
    if log:
        log.close()
    print("[closed]")


if __name__ == "__main__":
    main()
