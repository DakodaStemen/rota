# Rotate OS: device steps for Dakoda

Short steps, one action each. Run every command on the PC. If a step says **Stop if**, stop there and send me the output.

> [!WARNING]
> Do these steps in order. Do not write anything to the device until Step 3 has a verified backup.

Sources: `upstream/base618/AGENTS.md`, `upstream/base618/docs/install.md`, `upstream/base618/scripts/flash-spl.sh`, `upstream/MIROKI/README.md` (flashing section), and `spd_dump --help` (ver 239).

---

## Step 0 — Charge

1. Charge the RG Rotate above 50 %.

## Step 1 — PC setup (once)

1. Make a working folder and copy in the tool and loaders:

```sh
mkdir -p ~/rotate-flash && cd ~/rotate-flash
cp ~/Projects/rotate-os/upstream/rg-rotate-linux/tools/spd_dump/spd_dump .
cp ~/Projects/rotate-os/upstream/MIROKI/tools/fdl1-dl.bin ~/Projects/rotate-os/upstream/MIROKI/tools/fdl2-dl.bin .
./spd_dump --help | head -1
```

Expect: `ver:239, sha1:d61a6a2e…`

2. Allow USB access to download mode:

```sh
sudo cp ~/Projects/rotate-os/upstream/rg-rotate-linux/tools/spd_dump/90-spd-dump.rules /etc/udev/rules.d/
sudo cp ~/Projects/rotate-os/upstream/rg-rotate-linux/tools/spd_dump/40-rgrotate-console-mm-ignore.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

Expect: no output.

## How to enter download mode (used in Steps 2–3)

All three sources that name a button name the same one: the single **Home/Back** button, on the front between the sticks. base618 says "slide the screen open, hold Home/Back … and plug it in", and MIROKI says "hold the Home/Back combo button while plugging in USB".

1. Start the `spd_dump` command **first**. It waits up to 300 s.
2. Unplug USB-C. Power the unit off.
   - Vol-Down+Power is a **reset**, not a power-off.
3. Slide the screen open.
4. Hold **Home/Back** and plug in USB-C.
5. Keep holding until the PC terminal shows progress. The unit's screen stays dark.

**Stop if:** nothing happens within 30 s. Then run `lsusb | grep 1782:4d00` and send me the output.

## Step 2 — Back up the stock eMMC (nothing is written)

1. Make the backup folder:

```sh
mkdir -p ~/rotate-backup/emmc && cd ~/rotate-backup/emmc
```

2. Start the backup, then enter download mode:

```sh
~/rotate-flash/spd_dump --wait 300 keep_charge 1 \
  fdl ~/rotate-flash/fdl1-dl.bin 0x5500 \
  fdl ~/rotate-flash/fdl2-dl.bin 0x9EFFFE00 \
  exec \
  partition_list partitions.xml \
  read_part splloader 0 0x400000 splloader.bin \
  read_part splloader_bak 0 0x400000 splloader_bak.bin \
  r all \
  r userdata \
  poweroff
```

- `r all` reads every partition except blackbox, cache and userdata.
- `r userdata` adds your Android saves. It is large, so expect a long wait.
- Expect: a progress line per partition, and the unit powers off at the end.

3. Check the files:

```sh
ls -la . */ 2>/dev/null | head -60
```

**Stop if:**
- `splloader.bin` is missing or 0 bytes.
- No files named like `pm_sys_a`, `l_agdsp_a` or `wcnmodem_a` appear.

4. Checksum the backup and copy it off the PC:

```sh
cd ~/rotate-backup && find emmc -type f -exec sha256sum {} + > emmc.sha256 && wc -l emmc.sha256
```

Then copy `~/rotate-backup` to a second drive.

## Step 3 — Flash the Rotate OS SPL once (boot0 only)

This writes **only** `splloader` (boot0). `splloader_bak` (boot1) keeps the unit's own SPL. If boot0 ever fails its check, the BootROM falls back to boot1 and the unit boots as before. (`base618/scripts/flash-spl.sh` header.)

The SPL is base618's: crackerjacques's open SPL with `cm4_sec()` skipped. Deep sleep needs it.

1. Check the image:

```sh
cd ~/Projects/rotate-os/build/out/image && sha256sum -c SHA256SUMS
```

Expect: `rotate-os.spl.img: OK`.

2. Start the flash, then enter download mode:

```sh
cd ~/rotate-flash
SPD_DUMP=$PWD/spd_dump sh ~/Projects/rotate-os/upstream/base618/scripts/flash-spl.sh \
  ~/rotate-flash ~/Projects/rotate-os/build/out/image/rotate-os.spl.img
```

The script backs up the slot, writes it, reads it back and compares.

Expect: `flash-spl: verified; old SPL kept in splloader-backup-<date>.bin`

**Stop if:** it says `readback differs` or `no readback`. Run it again; do not boot.

3. Unplug USB.

## Step 4 — Write the microSD card

1. Insert the card into the PC and list disks:

```sh
lsblk -d -o NAME,SIZE,RM,TRAN,MODEL
```

Find the card by its size. It must show `RM 1`.

2. Write it:

```sh
sudo ~/Projects/rotate-os/build/flash-sd.sh /dev/sdX
```

- Replace `sdX` with the card's name from step 1.
- The script refuses non-removable disks and the system disk, and it asks you to retype the name.
- Expect: dd progress, then the partitions `uboot`, `bootfs` and `rootfs`.

**Stop if:** the script refuses. Send me the `lsblk` output.

3. Run `sync`, then remove the card.

## Step 5 — First boot

1. Put the card in the RG Rotate.
2. **Unplug USB.** With USB in, a power-off lands on U-Boot's charge screen. Cold-boot unplugged.
3. Hold Power to turn it on.
4. Wait about 2 minutes. On first boot it copies the Wi-Fi, audio and pm_sys firmware from its own eMMC, then **powers itself off**.
5. Power it on again, then plug USB into the PC.
6. Open the console:

```sh
sudo picocom -b 115200 /dev/ttyACM0
```

- Nothing appears before Linux is up. The login prompt can take 1–2 minutes (base618 `AGENTS.md`).
- Expect: `rotate login:`. Log in as `root` / `root`.

7. Send me this output:

```sh
uname -a; ls /lib/firmware/sprd /lib/firmware/updates 2>/dev/null; cat /sys/class/watchdog/watchdog0/identity; journalctl -b -p err --no-pager | tail -30
```

Expect `uname` to show `7.2.9-sprd-ums512`.

**Stop if:** there is no `ttyACM0` after 3 minutes. Pull the card. Card out = stock Android, so the unit is fine.

---

## Rollback

**Boot Android:** power off, remove the card, power on. The SPL falls back to the eMMC `uboot_a`/`uboot_b`.

**Restore the stock SPL into boot0**, using the copy from Step 3:

```sh
cd ~/rotate-flash
./spd_dump --wait 300 keep_charge 1 fdl fdl1-dl.bin 0x5500 fdl fdl2-dl.bin 0x9EFFFE00 exec \
  write_part splloader splloader-backup-<date>.bin poweroff
```

Then enter download mode as above.

`splloader_bak` was never written, so the unit's own SPL is always in boot1.
