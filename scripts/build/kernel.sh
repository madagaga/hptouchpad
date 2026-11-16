#!/usr/bin/env bash
# Downloads the mainline kernel, injects our DTS + config fragment, builds zImage + DTB.
set -euo pipefail

SRC="$BUILD_DIR/linux-$KERNEL_VERSION"
# Base configuration: qcom_defconfig (Qualcomm SoCs only, a much smaller kernel than multi_v7_defconfig, which is still
# available with KERNEL_BASE=multi_v7_defconfig). One object directory per base.
KERNEL_BASE="${KERNEL_BASE:-qcom_defconfig}"
OBJ="$BUILD_DIR/obj-kernel"
[ "$KERNEL_BASE" = multi_v7_defconfig ] || OBJ="$BUILD_DIR/obj-kernel-${KERNEL_BASE%_defconfig}"
DTS_DIR="arch/arm/boot/dts/qcom"

# make rebuilds from modification times: a file rewritten with the same content must keep its old time, or its object (and everything
# that includes it, then the link of vmlinux) is rebuilt for nothing. Copies therefore only happen when the content differs.
sync_file() { cmp -s "$1" "$2" 2>/dev/null || { mkdir -p "$(dirname "$2")"; cp "$1" "$2"; }; }

TARBALL="$BUILD_DIR/linux-$KERNEL_VERSION.tar.xz"
fetch_tarball() {
  [ -f "$TARBALL" ] && return 0
  major="${KERNEL_VERSION%%.*}"
  echo ">> downloading linux-$KERNEL_VERSION"
  wget -q --show-progress -O "$TARBALL.part"     "https://cdn.kernel.org/pub/linux/kernel/v${major}.x/linux-${KERNEL_VERSION}.tar.xz"
  mv "$TARBALL.part" "$TARBALL"
}

if [ ! -d "$SRC" ]; then
  fetch_tarball
  tar -C "$BUILD_DIR" -xf "$TARBALL"
fi

# Kernel patches from kernel/patches, applied in file name order. Patches can modify the same lines,
# so they cannot be tested one by one: a stamp (hash of the whole patch set) records what was applied.
# When the set changed, the files touched by the patches are restored from the pristine tarball and
# every patch is applied again from scratch.
PATCH_FILES=$(ls "$WORK"/kernel/patches/*.patch 2>/dev/null | sort || true)
PATCH_STAMP=$(cat $PATCH_FILES 2>/dev/null | sha1sum | cut -d' ' -f1)
if [ "$(cat "$SRC/.touchpad-patches" 2>/dev/null || true)" = "$PATCH_STAMP" ]; then
  echo ">> kernel patches up to date ($(echo $PATCH_FILES | wc -w) patches)"
else
  fetch_tarball
  # Files touched by the current patches go back to the pristine version before the series is applied.
  TOUCHED=$(grep -h '^+++ b/' $PATCH_FILES | sed 's|^+++ b/||; s|[[:space:]].*||' | sort -u)
  # Copy of the current state of these files (with their times): afterwards a file that came out identical gets its old time back, and a
  # file whose content changed gets a new one (a restored pristine file would otherwise keep the tarball's time, older than its object).
  SNAP="$BUILD_DIR/.patch-snapshot"; rm -rf "$SNAP"; mkdir -p "$SNAP"
  for f in $TOUCHED; do
    [ -f "$SRC/$f" ] || continue
    mkdir -p "$SNAP/$(dirname "$f")"; cp -p "$SRC/$f" "$SNAP/$f"
  done
  echo ">> restoring pristine files: $(echo $TOUCHED | wc -w) file(s)"
  (cd "$BUILD_DIR" && tar -xJf "$TARBALL" $(for f in $TOUCHED; do echo "linux-$KERNEL_VERSION/$f"; done))
  find "$SRC" \( -name '*.rej' -o -name '*.orig' \) -delete
  for p in $PATCH_FILES; do
    echo ">> applying patch: $(basename "$p")"
    (cd "$SRC" && patch -p1 --no-backup-if-mismatch < "$p")
  done
  for f in $TOUCHED; do
    [ -f "$SRC/$f" ] || continue
    if [ -f "$SNAP/$f" ] && cmp -s "$SNAP/$f" "$SRC/$f"; then touch -r "$SNAP/$f" "$SRC/$f"; else touch "$SRC/$f"; fi
  done
  rm -rf "$SNAP"
  echo "$PATCH_STAMP" > "$SRC/.touchpad-patches"
fi

# Out-of-tree sources of the project (new drivers): copied over the kernel tree, their Kconfig/Makefile hooks are
# in the patches.
if [ -d "$WORK/kernel/src" ]; then
  (cd "$WORK/kernel/src" && find . -type f) | while read -r f; do sync_file "$WORK/kernel/src/$f" "$SRC/$f"; done
fi

# DTS: copy it in and register it in the Makefile (idempotent).
# Every file of kernel/dts is copied (a variant can include the base one) and registered.
for f in "$WORK"/kernel/dts/*.dts; do
  sync_file "$f" "$SRC/$DTS_DIR/$(basename "$f")"
  n=$(basename "$f" .dts)
  if ! grep -q "$n.dtb" "$SRC/$DTS_DIR/Makefile"; then
    echo "dtb-\$(CONFIG_ARCH_QCOM) += $n.dtb" >> "$SRC/$DTS_DIR/Makefile"
  fi
done

# Config: $KERNEL_BASE + the project fragment.
mk() { make -C "$SRC" O="$OBJ" -j"$JOBS" "$@"; }

# The fragment is merged again whenever it changes (stamp = hash of kernel/touchpad.config).
CFG_STAMP=$(sha1sum "$WORK/kernel/touchpad.config" | cut -d' ' -f1)
if [ ! -f "$OBJ/.config" ]; then
  mk "$KERNEL_BASE"
  rm -f "$OBJ/.touchpad-config"
fi
if [ "$(cat "$OBJ/.touchpad-config" 2>/dev/null || true)" != "$CFG_STAMP" ]; then
  echo ">> merging kernel/touchpad.config"
  (cd "$SRC" && ./scripts/kconfig/merge_config.sh -m -O "$OBJ" "$OBJ/.config" "$WORK/kernel/touchpad.config")
  mk olddefconfig
  echo "$CFG_STAMP" > "$OBJ/.touchpad-config"
fi

if [ "${1:-}" = "menuconfig" ]; then
  mk menuconfig
  mk savedefconfig
  echo ">> minimal defconfig: $OBJ/defconfig (copy the relevant options into kernel/touchpad.config)"
  cp "$OBJ/defconfig" "$OUT_DIR/kernel.defconfig"
  exit 0
fi

# Embed the initramfs in the kernel (the HP bootloader does not pass one with an appended DTB). It goes in uncompressed:
# the kernel image is compressed as a whole (XZ), a gzip inside it would only cost space.
if [ -f "$OUT_DIR/initramfs.cpio.gz" ]; then
  zcat "$OUT_DIR/initramfs.cpio.gz" > "$OBJ/initramfs.cpio.new"
  if cmp -s "$OBJ/initramfs.cpio.new" "$OBJ/initramfs.cpio"; then rm "$OBJ/initramfs.cpio.new"; else mv "$OBJ/initramfs.cpio.new" "$OBJ/initramfs.cpio"; fi
  "$SRC/scripts/config" --file "$OBJ/.config" --set-str INITRAMFS_SOURCE "$OBJ/initramfs.cpio"
  # The compression choice only exists once INITRAMFS_SOURCE is set: on a fresh object directory the value of touchpad.config was dropped
  # when the fragment was merged (source still empty) and the default (gzip) came back. Set it here, whatever the history of $OBJ.
  "$SRC/scripts/config" --file "$OBJ/.config" -d INITRAMFS_COMPRESSION_GZIP -e INITRAMFS_COMPRESSION_NONE
  mk olddefconfig
fi

mk zImage dtbs
cp "$OBJ/arch/arm/boot/zImage" "$OUT_DIR/zImage"
cp "$OBJ/arch/arm/boot/dts/qcom/$DTB_NAME.dtb" "$OUT_DIR/$DTB_NAME.dtb"
echo ">> OK: out/zImage, out/$DTB_NAME.dtb"
