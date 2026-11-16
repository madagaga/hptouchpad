#!/bin/sh
# Runs INSIDE the `initramfs` stage of the Dockerfile (Alpine armv7, plain POSIX sh): builds the initramfs of the kernel image, Alpine
# packages only. Needs: busybox-static, wireless-regdb, linux-firmware-ath6k, linux-firmware-qcom, zstd (apk), the repository mounted
# on /work, the stock webOS files (named build context `firmware`) on /firmware, devmem (rootfs/tp) as /work/devmem.
# Environment: OUT (output directory, default /out), ADRENO_FW=webos (microcode of the stock webOS instead of linux-firmware),
# BOARD_DATA=upstream|webos|none (Wi-Fi board data, see below).
# Not ported yet from the Debian version: the debug tools of INITRAMFS_TOOLS=full (iw, Bluetooth, wpa_supplicant, SSH).
set -eu

W=/work
R=/initramfs
OUT="${OUT:-/out}"
FW=$R/lib/firmware
LF=/lib/firmware          # linux-firmware files of the Alpine packages (zstd-compressed)

rm -rf "$R"
mkdir -p "$R"/bin "$R"/sbin "$R"/etc "$R"/proc "$R"/sys "$R"/dev "$R"/tmp "$R"/root "$R"/mnt "$R"/usr/bin "$OUT"

# BusyBox: static, every applet linked (Alpine's has no devmem, no telnetd and no cttyhack: see rootfs/tp/devmem.c, initramfs/init).
install -m755 /bin/busybox.static "$R/bin/busybox"
for a in $("$R/bin/busybox" --list); do ln -sf busybox "$R/bin/$a"; done
install -m755 "$W/devmem" "$R/bin/devmem"

# Minimal device nodes: without /dev/console the kernel cannot open the initial console.
mknod -m 600 "$R/dev/console" c 5 1
mknod -m 666 "$R/dev/null" c 1 3
mknod -m 666 "$R/dev/tty0" c 4 0

# Wi-Fi firmware of the Atheros AR6003 (hw2.1.1): fw-3.bin only (the driver tries it first and it is the one the tablet runs; the older
# fw-2.bin would only be a fallback, and nothing else of linux-firmware is useful on this tablet). The board data (calibration, regulatory data and the MAC address of the board) is BOARD_DATA:
#   upstream (default)  bdata.SD32.bin of linux-firmware for hw2.1.1 (public, generic: placeholder MAC 00:03:7f:04:02:8f, the same on every
#                       tablet). The mainline driver would fall back on bdata.SD31.bin without any bdata.bin.
#   webos               the file of the stock webOS of the tablet, from the build context `firmware` (webos/ath6k/hw2.1.1/bdata.SD32.bin):
#                       it is per tablet (it holds the MAC address of the unit), not redistributable.
#   none                no bdata.bin: the driver uses its default board file (bdata.SD31.bin)
mkdir -p "$FW/ath6k/AR6003/hw2.1.1" "$FW/qcom"
zstd -dcq < "$LF/ath6k/AR6003/hw2.1.1/fw-3.bin.zst" > "$FW/ath6k/AR6003/hw2.1.1/fw-3.bin"
case "${BOARD_DATA:-upstream}" in
  upstream)
    # a link to hw2.0/ in the Alpine package: read it through the shell (zstd refuses to open links)
    zstd -dcq < "$LF/ath6k/AR6003/hw2.1.1/bdata.SD32.bin.zst" > "$FW/ath6k/AR6003/hw2.1.1/bdata.bin"
    ;;
  webos)
    BD=/firmware/webos/ath6k/hw2.1.1/bdata.SD32.bin
    if [ ! -f "$BD" ]; then
      echo "initramfs: BOARD_DATA=webos but $BD is missing (read from the stock webOS; build context 'firmware')" >&2
      exit 1
    fi
    cp "$BD" "$FW/ath6k/AR6003/hw2.1.1/bdata.bin"
    ;;
  none) echo "initramfs: no Wi-Fi board data (BOARD_DATA=none): the driver falls back on bdata.SD31.bin" >&2 ;;
  *) echo "initramfs: BOARD_DATA must be upstream, webos or none" >&2; exit 1 ;;
esac
echo "initramfs: Wi-Fi board data: ${BOARD_DATA:-upstream} $(sha256sum "$FW/ath6k/AR6003/hw2.1.1/bdata.bin" 2>/dev/null | cut -c1-16)"

# Adreno 220 microcode, loaded by the msm DRM driver at the first use of the GPU. ADRENO_FW=webos takes the stock webOS files instead
# (read from the tablet, not redistributable): a different CP microcode version (STATUS.md, GPU).
# H.264 codec (Qualcomm VIDC 1080p) firmware of the stock webOS, loaded by the s5p-mfc variant (not redistributable, never in a release).
if [ "${VIDC_FW:-}" = webos ]; then
  mkdir -p "$FW/qcom"
  cp /firmware/webos/vidc/vidc_1080p.fw "$FW/qcom/"
  echo "initramfs: VIDC firmware from webOS: $(md5sum "$FW/qcom/vidc_1080p.fw")"
fi
if [ "${ADRENO_FW:-}" = webos ]; then
  cp /firmware/webos/gpu/leia_*.fw "$FW/qcom/"
  echo "initramfs: Adreno microcode from webOS: $(cd "$FW/qcom" && md5sum leia_*.fw | tr '\n' ' ')"
else
  for f in "$LF"/qcom/leia_*.fw.zst; do
    zstd -dcq < "$f" > "$FW/qcom/$(basename "$f" .zst)"
  done
fi

# Power and CPU helpers used by the init and the rescue shell (shell scripts, a few KB).
install -m755 "$W/initramfs/cpu-state" "$W/initramfs/charge-mode" "$W/initramfs/tp-powerd" "$W/initramfs/tp" "$R/usr/bin/"

# Regulatory database (signed, upstream key): the kernel asks for it while it boots and, without it, stays in the restrictive "world" domain.
cp "$LF/regulatory.db" "$LF/regulatory.db.p7s" "$FW/"

install -m755 "${INIT_SRC:-$W/initramfs/init}" "$R/init"

(cd "$R" && find . | sort | cpio -o -H newc | gzip -9n) > "$OUT/initramfs.cpio.gz"
echo ">> OK: initramfs.cpio.gz ($(find "$R" | wc -l) entries, $(wc -c < "$OUT/initramfs.cpio.gz") bytes)"
