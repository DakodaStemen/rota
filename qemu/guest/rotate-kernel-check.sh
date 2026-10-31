#!/bin/sh
# Runs inside the guest booted with the REAL Rotate OS kernel (qemu/run-vm.sh -k rotate).
# Prints RV_PASS/RV_FAIL/RV_INFO lines; final line RV_RESULT=PASS|FAIL.
fail=0
chk() { if eval "$2" >/tmp/chk.out 2>&1; then echo "RV_PASS $1"; else echo "RV_FAIL $1: $(tr '\n' ' ' </tmp/chk.out | cut -c1-200)"; fail=1; fi; }
echo "RV_INFO kernel=$(uname -r)"
echo "RV_INFO cmdline=$(cat /proc/cmdline)"
chk "kernel_is_rotate_build" "uname -r | grep -q sprd-ums512"
chk "root_is_vda3_no_initramfs" "test \"\$(findmnt -no SOURCE /)\" = /dev/vda3 && ! dmesg | grep -q 'Trying to unpack rootfs image'"
# --- binderfs
mkdir -p /dev/binderfs
chk "binderfs_mount" "mount -t binder binder /dev/binderfs"
chk "binderfs_fstype" "grep -q ' /dev/binderfs binder ' /proc/mounts"
chk "binder_control_exists" "test -c /dev/binderfs/binder-control"
echo "RV_INFO binderfs_default_devices=$(ls /dev/binderfs | tr '\n' ' ')"
chk "binderfs_default_devices" "test -c /dev/binderfs/binder && test -c /dev/binderfs/hwbinder && test -c /dev/binderfs/vndbinder"
out=$(/usr/local/bin/rotate-binder-add /dev/binderfs/binder-control rotate-test-binder 2>&1); echo "RV_INFO binder_add=$out"
chk "binder_control_creates_device" "test -c /dev/binderfs/rotate-test-binder"
umount /dev/binderfs 2>/dev/null
# --- cgroup v2
chk "cgroup2_mounted" "grep -q '^cgroup2 /sys/fs/cgroup cgroup2' /proc/mounts"
ctl=$(cat /sys/fs/cgroup/cgroup.controllers 2>/dev/null); echo "RV_INFO cgroup_controllers=$ctl"
for c in cpu memory pids; do chk "cgroup_controller_$c" "echo '$ctl' | tr ' ' '\n' | grep -qx $c"; done
chk "cgroup_subtree_control_enable" "echo +pids > /sys/fs/cgroup/cgroup.subtree_control; grep -qw pids /sys/fs/cgroup/cgroup.subtree_control; mkdir /sys/fs/cgroup/rtest && test -e /sys/fs/cgroup/rtest/pids.max; rmdir /sys/fs/cgroup/rtest"
# --- PSI
for f in cpu memory io; do chk "psi_$f" "test -r /proc/pressure/$f && grep -q '^some avg10=' /proc/pressure/$f"; done
chk "psi_cgroup_files" "test -e /sys/fs/cgroup/cpu.pressure && test -e /sys/fs/cgroup/memory.pressure && test -e /sys/fs/cgroup/io.pressure"
# --- veth + bridge
chk "bridge_create" "ip link add rbr0 type bridge && ip link show rbr0"
chk "veth_create" "ip link add rv0 type veth peer name rv1 && ip link show rv0 && ip link show rv1"
chk "veth_enslave_to_bridge" "ip link set rv0 master rbr0 && ip -d link show rv0 | grep -q 'master rbr0'"
ip link del rv0 2>/dev/null; ip link del rbr0 2>/dev/null
# --- uinput + swivel helper
chk "dev_uinput" "test -c /dev/uinput"
/usr/local/bin/rotate-swivel emit 1 6 >/tmp/sw.log 2>&1 &
sleep 2
v=$(/usr/local/bin/rotate-swivel read 2>&1); echo "RV_INFO swivel_read=$v"
chk "uinput_swivel_SW_LID_closed_reads_1" "test '$v' = 1"
wait
# --- first-boot service FAILURE path (this VM has no eMMC): marker written, no poweroff, boot continues
for i in $(seq 1 40); do [ -e /boot/rotate-fw.failed ] && break; sleep 1; done
chk "firstboot_no_emmc_writes_failed_marker" "test -s /boot/rotate-fw.failed && ! test -e /boot/rotate-fw.done"
echo "RV_INFO firstboot_failed_reason=$(cat /boot/rotate-fw.failed 2>/dev/null)"
[ $fail = 0 ] && echo RV_RESULT=PASS || echo RV_RESULT=FAIL
exit $fail
