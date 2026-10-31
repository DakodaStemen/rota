#!/bin/sh
# SPDX-License-Identifier: MIT
# Adapted from thwonp/base618 scripts/base618-firstboot.sh (MIT, https://github.com/thwonp/base618).
# Changes: no Armbian resize/PARTUUID rewrite (our PARTUUID is fixed, rotate-resize keeps it), rotate-* names.
# Extracts the unit's own firmware off its eMMC (extract-firmware.sh), writes the firmware initrd
# (make-initrd-fw.sh), indexes modules, then POWERS OFF (a reboot is unreliable on this chain: base618 docs/firmware.md).
set -u
HERE=$(dirname "$0")
BOOT=${BOOT:-/boot}
TTY=${TTY:-/dev/tty1}
log() { echo "rotate-firstboot: $*"; }
screen() { { printf '\033[2J\033[H\n'; for l; do printf '  %s\n' "$l"; done; } > "$TTY" 2>/dev/null; }
fail() {
	log "$1"; echo "$1" > "$BOOT/rotate-fw.failed"; sync
	screen "rotate: could not set up the firmware" "from this unit's internal storage:" "  $1" "" \
		"Audio, Wi-Fi/BT and deep sleep will not work." "Starting anyway..."
	sleep 15; exit 0
}
screen "rotate: setting up this unit's firmware..." "" "Please wait, do not turn it off."
udevadm settle -t 30
sh "$HERE/extract-firmware.sh" / || fail "extract-firmware.sh found no valid image (see the journal)"
sh "$HERE/make-initrd-fw.sh" / "$BOOT/initrd-fw.cpio.gz" || fail "could not write $BOOT/initrd-fw.cpio.gz"
depmod -a "$(uname -r)" || log "depmod failed; the updated modules may not load"
touch "$BOOT/rotate-fw.done"; sync
log "firmware initrd written, powering off"
screen "rotate: firmware set up." "" "The unit will now power off." "Turn it on again to boot."
sleep 8
systemctl --no-block poweroff
