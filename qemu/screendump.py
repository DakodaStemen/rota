#!/usr/bin/env python3
"""screendump.py QMP_SOCK OUT.png  -- headless screenshot via QMP (needs OUT dir to exist)."""
import socket, json, sys, os
sock, out = sys.argv[1], os.path.abspath(sys.argv[2])
s = socket.socket(socket.AF_UNIX); s.connect(sock); f = s.makefile('rw'); f.readline()
def c(cmd, **a):
    f.write(json.dumps({"execute": cmd, "arguments": a}) + "\n"); f.flush()
    while True:
        r = json.loads(f.readline())
        if 'return' in r or 'error' in r: return r
c("qmp_capabilities")
r = c("screendump", filename=out, format="png")
if 'error' in r: print(r['error']['desc'], file=sys.stderr); sys.exit(1)
print(out)
