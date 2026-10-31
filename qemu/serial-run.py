#!/usr/bin/env python3
"""serial-run.py SERIAL_SOCK LOG BOOT_TIMEOUT -- CMD...
Waits for login prompt on the serial socket, logs in as root/root (VM only), runs CMD, prints output.
Writes full serial transcript to LOG. Exit 0 if CMD's last RV_RESULT=PASS, 2 timeout, 1 otherwise.
Prints 'BOOT_SECONDS=<n>' (time from connect to login prompt)."""
import socket, sys, time, re
sock, log, bt = sys.argv[1], sys.argv[2], int(sys.argv[3]); cmd = " ".join(sys.argv[sys.argv.index('--')+1:])
t0 = time.time(); s = None
def connect(limit=30):
    global s
    t = time.time()
    while time.time() - t < limit:
        try:
            if s:
                try: s.close()
                except OSError: pass
            s = socket.socket(socket.AF_UNIX); s.connect(sock); s.settimeout(1); return True
        except OSError: time.sleep(0.5)
    return False
if not connect(): print("cannot connect serial"); sys.exit(2)
buf = b""; lf = open(log, "wb")
closed = 0
def send(b):
    """send tolerating a closed socket (guest poweroff/QEMU restart): reconnect once, else note it."""
    global closed
    try: s.send(b); return True
    except OSError:
        closed += 1
        if connect(5):
            try: s.send(b); return True
            except OSError: pass
        return False
def pump(until, timeout):
    global buf
    end = time.time() + timeout
    while time.time() < end:
        if until(buf): return True
        try: d = s.recv(4096)
        except socket.timeout: continue
        if not d:
            closed += 1
            if not connect(5): break
            continue
        buf += d; lf.write(d); lf.flush()
    return until(buf)
prompted = False
end = time.time() + bt
while time.time() < end and not prompted:
    prompted = pump(lambda b: b"login: " in b, 5)
    if not prompted: send(b"\r")
if not prompted: print("TIMEOUT waiting for login prompt"); sys.exit(2)
print("BOOT_SECONDS=%.0f" % (time.time() - t0))
buf = b""; send(b"root\r"); pump(lambda b: b"assword" in b, 20); send(b"root\r")
if not pump(lambda b: re.search(rb"# $", b), 60): print("login failed"); sys.exit(1)
buf = b""; send((cmd + "; echo CMD_DONE_$?\r").encode())
ok = pump(lambda b: re.search(rb"CMD_DONE_\d+\r?\n", b) is not None and b.count(b"CMD_DONE_") >= 2, 240)
out = buf.decode(errors="replace"); print(out)
if closed and not ok: print("SERIAL_CLOSED events=%d (guest closed the console before the command finished)" % closed)
sys.exit(0 if ok and "RV_RESULT=PASS" in out else 1)
