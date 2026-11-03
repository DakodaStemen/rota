#!/usr/bin/python3 -I
# SPDX-License-Identifier: MIT
"""rotate-evdump.py  caps | touch | capture SECONDS   (helper of rotate-selftest; stdlib only)

caps     every /dev/input/event*: name, phys, id, properties, event types, key/button names, ABS axes
         (min/max/res), switch states (EVIOCGSW).  Only ioctls; no events are consumed, nothing is grabbed.
touch    the direct-input (touchscreen) devices and their axes/slots.
capture  open every event device read-only/non-blocking for SECONDS and print each event with a timestamp.
Exit: 0 ok, 2 no /dev/input/event* at all (reason on stderr), 1 usage.
"""
import fcntl, glob, os, select, struct, sys, time

EV = {0: "SYN", 1: "KEY", 2: "REL", 3: "ABS", 4: "MSC", 5: "SW", 0x11: "LED", 0x12: "SND", 0x14: "REP", 0x15: "FF"}
SW = {0: "SW_LID", 1: "SW_TABLET_MODE", 2: "SW_HEADPHONE_INSERT", 4: "SW_MICROPHONE_INSERT", 6: "SW_DOCK",
      7: "SW_LINEOUT_INSERT", 0x0a: "SW_JACK_PHYSICAL_INSERT", 0x0d: "SW_MACHINE_COVER"}
KEYS = {1: "KEY_ESC", 28: "KEY_ENTER", 102: "KEY_HOME", 103: "KEY_UP", 105: "KEY_LEFT", 106: "KEY_RIGHT", 108: "KEY_DOWN",
        114: "KEY_VOLUMEDOWN", 115: "KEY_VOLUMEUP", 116: "KEY_POWER", 139: "KEY_MENU", 158: "KEY_BACK",
        0x120: "BTN_JOYSTICK/TRIGGER", 0x130: "BTN_SOUTH(A)", 0x131: "BTN_EAST(B)", 0x132: "BTN_C", 0x133: "BTN_NORTH(X)",
        0x134: "BTN_WEST(Y)", 0x135: "BTN_Z", 0x136: "BTN_TL", 0x137: "BTN_TR", 0x138: "BTN_TL2", 0x139: "BTN_TR2",
        0x13a: "BTN_SELECT", 0x13b: "BTN_START", 0x13c: "BTN_MODE", 0x13d: "BTN_THUMBL", 0x13e: "BTN_THUMBR",
        0x145: "BTN_TOOL_FINGER", 0x14a: "BTN_TOUCH", 0x14d: "BTN_TOOL_DOUBLETAP", 0x220: "BTN_DPAD_UP",
        0x221: "BTN_DPAD_DOWN", 0x222: "BTN_DPAD_LEFT", 0x223: "BTN_DPAD_RIGHT"}
ABS = {0: "ABS_X", 1: "ABS_Y", 2: "ABS_Z", 3: "ABS_RX", 4: "ABS_RY", 5: "ABS_RZ", 0x10: "ABS_HAT0X", 0x11: "ABS_HAT0Y",
       0x18: "ABS_PRESSURE", 0x2f: "ABS_MT_SLOT", 0x30: "ABS_MT_TOUCH_MAJOR", 0x31: "ABS_MT_TOUCH_MINOR",
       0x35: "ABS_MT_POSITION_X", 0x36: "ABS_MT_POSITION_Y", 0x39: "ABS_MT_TRACKING_ID", 0x3a: "ABS_MT_PRESSURE"}
PROPS = {0: "POINTER", 1: "DIRECT", 2: "BUTTONPAD", 3: "SEMI_MT", 4: "TOPBUTTONPAD", 5: "POINTING_STICK", 6: "ACCELEROMETER"}


def ioc(d, t, nr, size): return (d << 30) | (size << 16) | (ord(t) << 8) | nr


def name_of(tab, c, prefix): return tab.get(c, "%s_0x%x" % (prefix, c))


def bits(fd, ev, n):
    buf = bytearray(n)
    try:
        fcntl.ioctl(fd, ioc(2, 'E', 0x20 + ev, n), buf)
    except OSError:
        return set()
    return {i * 8 + b for i, v in enumerate(buf) for b in range(8) if v >> b & 1}


def info(path):
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    try:
        nm = bytearray(256); n = fcntl.ioctl(fd, ioc(2, 'E', 6, 256), nm)
        name = bytes(nm[:n]).split(b'\0')[0].decode(errors='replace')
        ph = bytearray(256)
        try: n = fcntl.ioctl(fd, ioc(2, 'E', 7, 256), ph); phys = bytes(ph[:n]).split(b'\0')[0].decode(errors='replace')
        except OSError: phys = ''
        idb = bytearray(8); fcntl.ioctl(fd, ioc(2, 'E', 2, 8), idb); bus, ven, pro, ver = struct.unpack('4H', idb)
        pb = bytearray(8)
        try: fcntl.ioctl(fd, ioc(2, 'E', 9, 8), pb)
        except OSError: pass
        props = {i * 8 + b for i, v in enumerate(pb) for b in range(8) if v >> b & 1}
        types = bits(fd, 0, 8)
        d = dict(path=path, name=name, phys=phys, id=(bus, ven, pro, ver), props=props, types=types, keys=set(), abs={}, sw={})
        if 1 in types: d['keys'] = bits(fd, 1, 96)
        if 3 in types:
            for a in bits(fd, 3, 8):
                ab = bytearray(24)
                try: fcntl.ioctl(fd, ioc(2, 'E', 0x40 + a, 24), ab); d['abs'][a] = struct.unpack('6i', ab)
                except OSError: d['abs'][a] = None
        if 5 in types:
            sup = bits(fd, 5, 4); st = bytearray(4)
            try: fcntl.ioctl(fd, ioc(2, 'E', 0x1b, 4), st)
            except OSError: pass
            v = int.from_bytes(st, 'little')
            d['sw'] = {s: v >> s & 1 for s in sup}
        return d
    finally:
        os.close(fd)


def devices():
    out = []
    for p in sorted(glob.glob('/dev/input/event*'), key=lambda s: int(s[16:])):
        try: out.append(info(p))
        except OSError as e: print("%s: %s" % (p, e), file=sys.stderr)
    return out


def show(d, only_touch=False):
    print("%s  name=\"%s\" phys=\"%s\" bus=0x%04x vendor=0x%04x product=0x%04x version=0x%04x" % ((d['path'], d['name'], d['phys']) + d['id']))
    print("  properties: %s" % (",".join(name_of(PROPS, p, "PROP") for p in sorted(d['props'])) or "-"))
    print("  event types: %s" % ",".join(name_of(EV, t, "EV") for t in sorted(d['types'])))
    if d['keys']: print("  keys/buttons (%d): %s" % (len(d['keys']), " ".join(name_of(KEYS, k, "KEY") for k in sorted(d['keys']))))
    for a, v in sorted(d['abs'].items()):
        if v is None: print("  abs %s: unreadable" % name_of(ABS, a, "ABS"))
        else: print("  abs %-20s value=%d min=%d max=%d fuzz=%d flat=%d res=%d" % ((name_of(ABS, a, "ABS"),) + v))
    for s, v in sorted(d['sw'].items()): print("  switch %s = %d%s" % (name_of(SW, s, "SW"), v, "  (1 = closed)" if s == 0 else ""))


def cmd_caps(touch=False):
    ds = devices()
    if not ds:
        print("no /dev/input/event* devices", file=sys.stderr); return 2
    n = 0
    for d in ds:
        is_touch = (1 in d['props'] and 3 in d['types']) or 0x35 in d['abs'] or "touch" in d['name'].lower() or "goodix" in d['name'].lower()
        if touch and not is_touch: continue
        show(d); n += 1
    if touch and not n: print("no touchscreen-like device (PROP_DIRECT + ABS, MT axes, or name touch/goodix)")
    pad = [d['name'] for d in ds if {0x130, 0x131, 0x13a, 0x13b} <= d['keys'] or {0x220, 0x221} & d['keys']]
    lid = [d['name'] for d in ds if 0 in d['sw'] or 1 in d['sw']]
    print("SUMMARY devices=%d gamepad=%s switch=%s touch=%d" % (len(ds), ",".join(pad) or "none", ",".join(lid) or "none",
          sum(1 for d in ds if (1 in d['props'] and 3 in d['types']) or 0x35 in d['abs'])))
    return 0


def cmd_capture(secs):
    ds = devices()
    if not ds:
        print("no /dev/input/event* devices", file=sys.stderr); return 2
    fds = {}
    for d in ds:
        try: fds[os.open(d['path'], os.O_RDONLY | os.O_NONBLOCK)] = d
        except OSError as e: print("%s: %s" % (d['path'], e), file=sys.stderr)
    t0 = time.monotonic(); counts = {}
    while True:
        left = secs - (time.monotonic() - t0)
        if left <= 0: break
        r, _, _ = select.select(list(fds), [], [], min(left, 0.5))
        for fd in r:
            try: data = os.read(fd, 24 * 64)
            except BlockingIOError: continue
            except OSError as e: print("read %s: %s" % (fds[fd]['path'], e)); del fds[fd]; break
            for i in range(0, len(data) - 23, 24):
                _, _, t, c, v = struct.unpack('llHHi', data[i:i + 24])
                if t == 0: continue
                dn = fds[fd]
                nm = SW.get(c, "SW_%d" % c) if t == 5 else name_of(KEYS, c, "KEY") if t == 1 else name_of(ABS, c, "ABS") if t == 3 else "code%d" % c
                print("%7.3f %s \"%s\" %s %s value=%d" % (time.monotonic() - t0, dn['path'], dn['name'], EV.get(t, "EV%d" % t), nm, v))
                k = (dn['name'], EV.get(t, "EV%d" % t)); counts[k] = counts.get(k, 0) + 1
    for (n, t), c in sorted(counts.items()): print("COUNT %s %s %d" % (n, t, c))
    print("SUMMARY events=%d switch_events=%d key_events=%d abs_events=%d" % tuple(
        [sum(counts.values())] + [sum(c for (n, t), c in counts.items() if t == x) for x in ("SW", "KEY", "ABS")]))
    return 0


def main(a):
    try:
        if len(a) >= 1 and a[0] == 'caps': return cmd_caps()
        if len(a) >= 1 and a[0] == 'touch': return cmd_caps(True)
        if len(a) == 2 and a[0] == 'capture': return cmd_capture(float(a[1]))
    except OSError as e:
        print("error: %s" % e, file=sys.stderr); return 2
    print(__doc__, file=sys.stderr); return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
