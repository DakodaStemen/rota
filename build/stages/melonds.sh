#!/usr/bin/env bash
# Stage melonds / melonds-pgo: melonDS DS libretro core (JesseTG/melonds-ds 1.4.0) built NATIVELY inside an
# aarch64 Arch Linux ARM chroot (systemd-nspawn + qemu-user), with plorpOS's PGO recipe.
#
#   ./build.sh melonds       -> -fprofile-generate build (training core). Output: out/melonds/melondsds_libretro.pgo.so
#   MELONDS_PROFILE=<dir|tgz> ./build.sh melonds-pgo   -> -fprofile-use build. Output: out/melonds/melondsds_libretro.so
#
# Why the chroot and not aarch64-linux-gnu-g++ on the host: a .gcda profile is only valid for the GCC release
# that wrote it. The device runs Arch's GCC runtime and the profile must be consumed by the same compiler that
# instrumented the code; the chroot gives one compiler for both passes (the host cross GCC 15.2 would be a second one).
#
# Recipe borrowed (re-typed, MIT, thwonp/plorpOS @ f555b88):
#   mk/build-melondsds.sh L52-L53  source pins (melonds-ds f394adba, melonDS 16f127db)
#   mk/build-melondsds.sh L83-L96  cmake flags: -DENABLE_OPENGL=OFF -DENABLE_JIT=ON -DENABLE_THREADED_RENDERER=ON, Release,
#                                  -mcpu=cortex-a75.cortex-a55, instrument = -fprofile-generate -fprofile-update=atomic,
#                                  use = -fprofile-use -fprofile-partial-training -fprofile-correction -Wno-missing-profile
#   mk/patches/melondsds-plorpos.patch, mk/patches/melonds-plorpos.patch  -> build/patches/melonds/ (verbatim, MIT)
#   tools/ds-pgo-train.sh          training protocol; our device-side version is rootfs/overlay/usr/lib/rotate/ds-pgo-train.sh
# The profile is keyed by the object's absolute path, so BOTH passes build in /build/out/build inside the chroot.
# Needs sudo + network (first run: ALARM chroot, pacman, git clones of FetchContent deps).
STAGE_NAME="${MELONDS_MODE:-instrument}"
. "$(dirname "$0")/../lib/common.sh"

MODE="$STAGE_NAME"                       # instrument | use
CORE_PIN=f394adbacb5722ee97c1b37c8064da9a25818310    # JesseTG/melonds-ds v1.4.0
MELONDS_PIN=16f127dbb587a73371827ce79689c5d237c4b59b # JesseTG/melonDS jtg/fix-uninitialized-opengl
MCPU="-mcpu=cortex-a75.cortex-a55"
CH="$WORK/melonds-chroot"
OD="$OUT/melonds"
PROFILE="${MELONDS_PROFILE:-}"

if [[ "$MODE" == use ]]; then
  [[ -n "$PROFILE" && -e "$PROFILE" ]] || { STAGE=melonds-pgo; die "melonds-pgo needs MELONDS_PROFILE=<GCOV_PREFIX dir or .tgz from the device> (see build/README.md, 'melonDS PGO')"; }
  PHASH="$( [[ -d "$PROFILE" ]] && (cd "$PROFILE" && find . -name '*.gcda' | sort | xargs sha256sum | sha256sum) || sha256sum < "$PROFILE")"
  OUTF="$OD/melondsds_libretro.so"; SN=melonds-pgo
else
  PHASH=none; OUTF="$OD/melondsds_libretro.pgo.so"; SN=melonds
fi
stage_begin "$SN" "$MODE $CORE_PIN $MELONDS_PIN $MCPU $PHASH $(file_hash "$0" "$B"/patches/melonds/*.patch) $ALARM_MD5" "$OUTF"
[[ -f /proc/sys/fs/binfmt_misc/qemu-aarch64 ]] || die "qemu-aarch64 binfmt not registered"
mkdir -p "$OD"
T0=$(date +%s)
nsp() { sudo systemd-nspawn -q --register=no --resolv-conf=replace-host -D "$CH" "$@"; }

# ---- 1. build chroot (kept between runs; delete work/melonds-chroot to rebuild) ----
if [[ ! -f "$CH/.ready" ]]; then
  [[ "$(md5sum "$ROOT/$ALARM_TARBALL" | cut -d' ' -f1)" == "$ALARM_MD5" ]] || die "ALARM tarball md5 mismatch"
  say "creating aarch64 build chroot (ALARM tarball + base-devel cmake ninja git python)"
  sudo rm -rf "$CH"; mkdir -p "$CH"
  sudo bsdtar -xpf "$ROOT/$ALARM_TARBALL" --numeric-owner -C "$CH"
  sudo tee "$CH/root/prep.sh" >/dev/null <<'EOS'
set -euxo pipefail
sed -i 's/^CheckSpace/#CheckSpace/' /etc/pacman.conf
grep -q '^DisableSandbox' /etc/pacman.conf || sed -i '/^\[options\]/a DisableSandbox' /etc/pacman.conf
sed -i 's/^#\?IgnorePkg.*/IgnorePkg = linux-aarch64 linux-aarch64-headers/' /etc/pacman.conf
pacman-key --init; pacman-key --populate archlinux archlinuxarm
pacman -Rdd --noconfirm linux-aarch64 2>/dev/null || true
pacman -Syu --noconfirm --needed base-devel cmake ninja git python
gcc --version | head -1; cmake --version | head -1
EOS
  nsp /bin/bash /root/prep.sh
  sudo touch "$CH/.ready"
fi

# ---- 2. sources: local clones of the pinned commits + plorpOS patches ----
say "sources"
sudo rm -rf "$CH/build/melonds-ds" "$CH/build/melonDS" "$CH/build/out/build"
sudo mkdir -p "$CH/build/out"
for spec in "melonds-ds:$CORE_PIN" "melonDS:$MELONDS_PIN"; do
  n="${spec%%:*}"; sha="${spec##*:}"
  [[ -d "$CACHE/$n.git" ]] || git clone -q --bare "https://github.com/JesseTG/$n.git" "$CACHE/$n.git"
  sudo git clone -q "$CACHE/$n.git" "$CH/build/$n"
  sudo git -C "$CH/build/$n" -c advice.detachedHead=false checkout -q "$sha"
done
sudo git -C "$CH/build/melonds-ds" apply "$B/patches/melonds/melondsds-plorpos.patch"
sudo git -C "$CH/build/melonDS"    apply "$B/patches/melonds/melonds-plorpos.patch"

# ---- 3. profile (use mode) ----
sudo find "$CH/build/out" -name '*.gcda' -delete
FLAGS="$MCPU"; LFLAGS=""
if [[ "$MODE" == instrument ]]; then
  FLAGS="$FLAGS -fprofile-generate -fprofile-update=atomic"; LFLAGS="-fprofile-generate"
else
  # device wrote $GCOV_PREFIX/build/out/build/**.gcda ; unpack at the chroot root so the paths line up
  if [[ -d "$PROFILE" ]]; then sudo rsync -a "$PROFILE"/ "$CH"/ --include='*/' --include='*.gcda' --exclude='*'
  else sudo tar -xf "$PROFILE" -C "$CH"; fi
  n=$(sudo find "$CH/build/out" -name '*.gcda' | wc -l); say "profile: $n .gcda files"; (( n > 0 )) || die "no .gcda files landed under /build/out (expected build/out/build/... inside the profile)"
  FLAGS="$FLAGS -fprofile-use -fprofile-partial-training -fprofile-correction -Wno-missing-profile"
fi

# ---- 4. configure + build (native, qemu-user) ----
sudo tee "$CH/root/build.sh" >/dev/null <<EOS
set -euo pipefail
cd /build
cmake -S /build/melonds-ds -B /build/out/build -G Ninja -DCMAKE_BUILD_TYPE=Release \\
  -DFETCHCONTENT_BASE_DIR=/build/out/deps -DFETCHCONTENT_SOURCE_DIR_MELONDS=/build/melonDS -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \\
  -DENABLE_OPENGL=OFF -DENABLE_JIT=ON -DENABLE_THREADED_RENDERER=ON \\
  -DCMAKE_C_FLAGS='$FLAGS' -DCMAKE_CXX_FLAGS='$FLAGS' -DCMAKE_SHARED_LINKER_FLAGS='$LFLAGS' > /build/out/configure.log 2>&1 \\
  || { tail -40 /build/out/configure.log; exit 1; }
S=\$(date +%s)
cmake --build /build/out/build -j$JOBS > /build/out/build.log 2>&1 || { grep -m5 -B2 -A8 -E 'error|FAILED' /build/out/build.log; exit 1; }
echo "compile+link seconds under qemu-user: \$(( \$(date +%s)-S ))"
so=/build/out/build/src/libretro/melondsds_libretro.so
strip --strip-unneeded -o /build/out/melondsds_libretro.so "\$so"
# never ship a core that cannot dlopen (same gate as plorpOS)
[ "\$(nm -D -u /build/out/melondsds_libretro.so | grep -v ' w ' | grep -vc '@')" = 0 ] || { echo 'undefined unversioned symbols' >&2; exit 1; }
file /build/out/melondsds_libretro.so
objdump -T /build/out/melondsds_libretro.so | grep -oE 'GLIBC_2\.[0-9]+' | sort -uV | tail -1
gcc --version | head -1
EOS
say "configure + build under qemu-user, -j$JOBS"
nsp /bin/bash /root/build.sh
sudo cp "$CH/build/out/melondsds_libretro.so" "$OUTF"; sudo chown "$(id -u):$(id -g)" "$OUTF"
if [[ "$MODE" == instrument ]]; then
  # keep the object list so a user can see what the profile will be keyed to
  sudo find "$CH/build/out/build" -name '*.gcno' | wc -l | sed 's/^/gcno notes files: /' >&2
fi
( cd "$OD" && sha256sum "$(basename "$OUTF")" | tee -a SHA256SUMS ) >&2
say "stage wall time $(( $(date +%s)-T0 )) s"
stage_end
