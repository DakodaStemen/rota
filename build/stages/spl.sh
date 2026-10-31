#!/usr/bin/env bash
# Stage spl: crackerjacques/ums512_spl (armbian branch, pinned) -> 4 MiB DHTB 'open' image.
# Output: out/spl/spl_a_open.img  (flashed ONCE to eMMC boot0 by Dakoda; never part of the SD image)
. "$(dirname "$0")/../lib/common.sh"
SRC_GIT="$ROOT/upstream/ums512_spl"
PATCHES=("$B"/patches/spl/*.patch)
stage_begin spl "$SPL_SHA $SPL_VARIANT $(file_hash "$0" "${PATCHES[@]}")" "$OUT/spl/spl_a_open.img"
git -C "$SRC_GIT" cat-file -e "$SPL_SHA^{commit}" || die "pinned commit $SPL_SHA not in $SRC_GIT"
D="$WORK/spl"; rm -rf "$D"; mkdir -p "$D" "$OUT/spl"
git -C "$SRC_GIT" archive "$SPL_SHA" | tar -x -C "$D"      # pristine tree; upstream/ untouched
for p in "${PATCHES[@]}"; do say "patch $(basename "$p")"; patch -d "$D" -p1 --no-backup-if-mismatch < "$p"; done
( cd "$D" && CROSS_COMPILE="$CROSS" ./build.sh "$SPL_VARIANT" )
cp "$D/out/$SPL_VARIANT/spl_a_$SPL_VARIANT.img" "$OUT/spl/"
python3 -I "$B/lib/check-dhtb.py" "$OUT/spl/spl_a_$SPL_VARIANT.img" 4194304
( cd "$OUT/spl" && sha256sum spl_a_$SPL_VARIANT.img | tee SHA256SUMS )
stage_end
