# Firmware and licences

What the images contain that is not our code, where it comes from, and what the build needs from your own tablet.

## Firmware

| File | Used for | Source | Redistributable |
|---|---|---|---|
| `ath6k/AR6003/hw2.1.1/fw-3.bin` | Wi-Fi (Atheros AR6003) | Alpine `linux-firmware-ath6k` | yes (linux-firmware) |
| `ath6k/AR6003/hw2.1.1/bdata.bin` | Wi-Fi board data | default `BOARD_DATA=upstream`: generic file of linux-firmware (placeholder MAC address, the same on every tablet). `BOARD_DATA=webos`: the file of your tablet | upstream: yes. webOS: no |
| `qcom/leia_pm4_470.fw`, `leia_pfp_470.fw` | Adreno 220 microcode | Alpine `linux-firmware-qcom` | yes (`LICENSE.qcom`: binary only, the licence text must travel with it, Qualcomm platforms only) |
| `regulatory.db` | Wi-Fi regulatory data | Alpine `wireless-regdb` | yes |
| `qcom/vidc_1080p.fw` | H.264 codec (`VIDC_FW=webos`) | your tablet's webOS | **no** |
| `bluecore6.psr` | Bluetooth (CSR BlueCore 6) keys | `rootfs/bluetooth/`, from the CyanogenMod device tree | licence to check |

The default build needs nothing from the tablet and produces an image with no H.264 codec.

## Files from your own tablet

Optional inputs are read from the directory `firmware/` (named build context `firmware`, or `FIRMWARE_DIR=<dir>`), which is not versioned:

| Setting | File expected |
|---|---|
| `VIDC_FW=webos` | `firmware/webos/vidc/vidc_1080p.fw`: required by the H.264 codec and by `tp-rtsp` |
| `BOARD_DATA=webos` | `firmware/webos/ath6k/hw2.1.1/bdata.SD32.bin`: board data with the MAC address of your tablet |
| `ADRENO_FW=webos` | `firmware/webos/gpu/leia_pm4_470.fw` and `leia_pfp_470.fw`: the microcode of the stock webOS instead of the linux-firmware one; not needed for the GPU to work |

Files from the stock webOS belong to HP and must not be redistributed or published: keep `firmware/` out of the repository.

## Licences

- Kernel patches and drivers (`kernel/`): GPL-2.0.
- `rootfs/ts_srv/`: GPL-2.0, ported from the CyanogenMod `android_device_hp_tenderloin` userspace driver.
- `rootfs/tp/`, `rootfs/tp-rtsp/`, the scripts and the overlay: licence not chosen yet.
- Third-party pieces that are copied or patched: the Cog patch in `rootfs/cog-patches/`, BlueZ `bccmd` and `wpa_supplicant` built from source,
  `bluecore6.psr` (the CyanogenMod repository it comes from declares no licence).
- `debrick/`: binaries of [tpdebrick](https://github.com/Herrie82/tpdebrick) v004, unchanged or minimally modified; licence not checked.
- moboot is a prerequisite of [FLASHING.md](FLASHING.md): a public download, not shipped here.
