#!/bin/sh
# Runs inside the guest. Prints RV_* lines; final line RV_RESULT=PASS|FAIL.
fail=0
chk() { if eval "$2" >/dev/null 2>&1; then echo "RV_PASS $1"; else echo "RV_FAIL $1"; fail=1; fi; }
modprobe virtio-gpu 2>/dev/null; modprobe uinput 2>/dev/null
for i in 1 2 3 4 5 6 7 8 9 10; do [ -e /dev/dri/card0 ] && break; sleep 1; done
chk "dri_card0_exists" "test -c /dev/dri/card0"
echo "RV_INFO kernel=$(uname -r)"
echo "RV_INFO modes=$(cat /sys/class/drm/card*-*/modes 2>/dev/null | tr '\n' ' ')"
chk "mode_720x720_listed" "cat /sys/class/drm/card*-*/modes | grep -qx 720x720"
echo "RV_INFO fb0_size=$(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null)"
chk "fb0_720x720" "grep -qx 720,720 /sys/class/graphics/fb0/virtual_size"
/usr/local/bin/rotate-swivel emit 1 6 >/tmp/sw.log 2>&1 &
sleep 2
v=$(/usr/local/bin/rotate-swivel read 2>&1); echo "RV_INFO swivel_read=$v"
chk "swivel_SW_LID_closed_reads_1" "test '$v' = 1"
wait
chk "net_virtio" "ls /sys/class/net | grep -qv '^lo$'"
[ $fail = 0 ] && echo RV_RESULT=PASS || echo RV_RESULT=FAIL
exit $fail
