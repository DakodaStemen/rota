#!/usr/bin/env python3
"""serial-exec.py SERIAL_SOCK LOG BOOT_TIMEOUT -- 'TIMEOUT_S:CMD' ['TIMEOUT_S:CMD' ...]
Like serial-run.py (VM-only root/root login on the QEMU serial socket) but runs several commands in order, each with its
own timeout, prints each command's output, and exits 0 when every command finished before its timeout, 2 on boot/login
timeout, 3 when a command timed out. Prints BOOT_SECONDS=<n> and, per command, 'CMD_SECONDS=<n>'."""
import re, socket, sys, time

sock, log, bt = sys.argv[1], sys.argv[2], int(sys.argv[3])
cmds = sys.argv[sys.argv.index('--') + 1:]
t0 = time.time(); s = None
while time.time() - t0 < 30:
    try:
        s = socket.socket(socket.AF_UNIX); s.connect(sock); break
    except OSError: time.sleep(0.5)
if not s: print("cannot connect serial"); sys.exit(2)
s.settimeout(1); buf = b""; lf = open(log, "wb")

def pump(until, timeout):
    global buf
    end = time.time() + timeout
    while time.time() < end:
        if until(buf): return True
        try: d = s.recv(65536)
        except socket.timeout: continue
        if not d: break
        buf += d; lf.write(d); lf.flush()
    return until(buf)

prompted = False; end = time.time() + bt
while time.time() < end and not prompted:
    prompted = pump(lambda b: b"login: " in b, 5)
    if not prompted: s.send(b"\r")
if not prompted: print("TIMEOUT waiting for login prompt"); sys.exit(2)
print("BOOT_SECONDS=%.0f" % (time.time() - t0))
buf = b""; s.send(b"root\r"); pump(lambda b: b"assword" in b, 20); s.send(b"root\r")
if not pump(lambda b: re.search(rb"# $", b), 60): print("login failed"); sys.exit(2)
rc = 0
for i, spec in enumerate(cmds):
    tmo, cmd = spec.split(":", 1); tmo = int(tmo)
    buf = b""; mark = "SX%d_" % i; c0 = time.time()
    s.send((cmd + "; echo " + mark + "DONE_$?\r").encode())
    done = pump(lambda b: re.search((mark + r"DONE_\d+\r?\n").encode(), b) is not None and b.count((mark + "DONE_").encode()) >= 2, tmo)
    print("CMD_SECONDS=%.0f" % (time.time() - c0))
    print(buf.decode(errors="replace"))
    if not done:
        print("COMMAND TIMED OUT after %d s: %s" % (tmo, cmd)); rc = 3; s.send(b"\x03\r"); time.sleep(2)
sys.exit(rc)
