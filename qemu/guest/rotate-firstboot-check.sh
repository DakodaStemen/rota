#!/bin/sh
# Runs inside the guest on the SECOND boot (after firstboot extracted from the fake eMMC and powered off).
# usage: rotate-firstboot-check.sh SHA_AGDSP SHA_PMSYS SHA_WCN   (sha256 of the expected extracted files)
fail=0
chk() { if eval "$2" >/tmp/chk.out 2>&1; then echo "RV_PASS $1"; else echo "RV_FAIL $1: $(tr '\n' ' ' </tmp/chk.out | cut -c1-200)"; fail=1; fi; }
A=$1; P=$2; W=$3
sha() { sha256sum "$1" | cut -d' ' -f1; }
chk "agdsp_landed_size_5MiB" "test \"\$(stat -c %s /lib/firmware/updates/agdsp.bin)\" = 5242880"
chk "agdsp_content_from_slot_b" "test \"\$(sha /lib/firmware/updates/agdsp.bin)\" = $A"
chk "pm_sys_landed_size_1MiB" "test \"\$(stat -c %s /lib/firmware/sprd/pm_sys.bin)\" = 1048576"
chk "pm_sys_content" "test \"\$(sha /lib/firmware/sprd/pm_sys.bin)\" = $P"
chk "wcnmodem_landed_size_947200" "test \"\$(stat -c %s /lib/firmware/updates/wcnmodem.bin)\" = 947200"
chk "wcnmodem_content_from_slot_b" "test \"\$(sha /lib/firmware/updates/wcnmodem.bin)\" = $W"
chk "done_marker_no_failed_marker" "test -e /boot/rotate-fw.done && ! test -e /boot/rotate-fw.failed"
chk "initrd_fw_exists" "test -s /boot/initrd-fw.cpio.gz && zcat /boot/initrd-fw.cpio.gz | cpio -t 2>/dev/null | grep -q agdsp.bin"
T=$(mktemp -d)
( cd "$T" && zcat /boot/initrd-fw.cpio.gz | cpio -id --quiet 2>/dev/null )
echo "RV_INFO initrd_members=$(cd $T && find . -type f -o -type c | sort | tr '\n' ' ')"
chk "initrd_rebuilt_has_extracted_agdsp" "test \"\$(sha $T/lib/firmware/updates/agdsp.bin)\" = $A"
chk "initrd_rebuilt_has_extracted_pm_sys" "test \"\$(sha $T/lib/firmware/sprd/pm_sys.bin)\" = $P"
chk "initrd_keeps_pm_bootcode" "test -s $T/lib/firmware/sprd/pm_bootcode.bin"
rm -rf "$T"
# The unit is not re-run on this boot: the ConditionPathExists=!/boot/rotate-fw.done guard skips it.
echo "RV_INFO unit=$(systemctl show rotate-firstboot.service -p ConditionResult -p ActiveState -p Result -p UnitFileState | tr '\n' ' ')"
chk "service_skipped_after_done" "systemctl show rotate-firstboot.service -p ConditionResult | grep -q ConditionResult=no"
chk "service_not_run_this_boot" "! journalctl -b -u rotate-firstboot.service --no-pager 2>/dev/null | grep -q 'powering off'"
[ $fail = 0 ] && echo RV_RESULT=PASS || echo RV_RESULT=FAIL
exit $fail
