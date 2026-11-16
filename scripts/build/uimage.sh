#!/usr/bin/env bash
# Assembles zImage + appended DTB (CONFIG_ARM_APPENDED_DTB) into a uImage for `novacom boot`.
# The initramfs is already embedded in the kernel (see kernel.sh).
# uImage.multi (kernel + separate initramfs) is kept as an experiment: it relies on ATAGs,
# which are disabled in this kernel, so it is not expected to work.
set -euo pipefail

cd "$OUT_DIR"
cat zImage "$DTB_NAME.dtb" > zImage-dtb

# Plain kernel uImage (zImage is already compressed, hence -C none).
mkimage -A arm -O linux -T kernel -C none \
  -a "$KERNEL_LOADADDR" -e "$KERNEL_ENTRY" \
  -n "hptouchpad linux $KERNEL_VERSION" \
  -d zImage-dtb uImage

if [ -f initramfs.cpio.gz ]; then
  mkimage -A arm -O linux -T multi -C none \
    -a "$KERNEL_LOADADDR" -e "$KERNEL_ENTRY" \
    -n "hptouchpad linux $KERNEL_VERSION + initramfs" \
    -d zImage-dtb:initramfs.cpio.gz uImage.multi
fi
echo ">> OK: out/uImage$( [ -f uImage.multi ] && echo ', out/uImage.multi')"
echo ">> test: scripts/cycle.sh out/kernel/uImage"
