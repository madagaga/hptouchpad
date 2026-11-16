# Public release (plan, 2026-10-29)

Status: **on hold**, waiting for the licence and redistribution review (owner).

## Decision proposed

Two repositories:

* **work** (this one, stays private): full history, lab notes (`STATUS.md`), raw data, debug tools.
* **public**: new repository with a fresh history, filled from an explicit list of files taken from the work repository.

Reasons not to publish this repository as it is:

* the history keeps everything ever committed; cleaning it afterwards is error-prone;
* `data/webos/` (45 files): dumps of HP/Palm files and readings of the tablets (the files with identifiers are git-ignored, the others are still HP files);
* `debrick/`: tpdebrick binaries (moboot, TPToolbox), licence not checked;
* `STATUS.md`: lab journal (about 1 350 lines), useful to us, not to a user;
* many `scripts/` are work tools tied to this PC (usbipd, IP addresses, COM ports, `cycle.sh`, webOS probes).

## Content of the public repository

* `docker/` (build image, Alpine rootfs, novacom), `build.ps1`, `build.sh`, `Makefile`
* `kernel/`: `dts/`, active patches, `src/`, `touchpad.config`* `initramfs/`, `rootfs/` (overlay, `ts_srv`, `tpd`, Cog patches)
* build scripts only: `kernel.sh`, `initramfs.sh`, `uimage.sh`, `build.sh`, `alpine-rootfs.sh`, `build-*.sh`
* `docs/FLASHING.md`, `docs/SETTINGS-PANEL.md`

## To review before publishing (owner)

* Licence of the project: GPL-2.0 for the kernel patches and drivers; `ts_srv` is GPL-2.0 (from CM `android_device_hp_tenderloin`); `tpd`, scripts, overlay: to choose.
* Licences of the third-party pieces that are copied or patched (Cog patches, BlueZ `bccmd` built from source, wpa_supplicant built from source).
* Firmware, not redistributable or to check:
  * public (linux-firmware): `ath6k/AR6003/hw2.1.1/fw-*.bin`, `qcom/leia_pfp_470.fw`, `qcom/leia_pm4_470.fw`, `regulatory.db`;
  * from webOS (HP, not redistributable): `bdata.SD32.bin` (Wi-Fi board data);
  * from CM device tree: `bluecore6.psr` (Bluetooth PSR keys), licence to check.
  The public README must explain how a user gets the non-redistributable files (read from their own tablet's webOS).
* moboot: prerequisite of `docs/FLASHING.md`, public download, not shipped.

## Public README (to write)

* What the project is: the TouchPad as an HTML display (Cog / WPE WebKit) on a minimal mainline Linux 6.12 + Alpine.
* What works, what does not yet (to take from `STATUS.md`, checked on the tablet). Known gaps to mention: power off lands in recovery
  (patch 0008), Wi-Fi key not stored (typed at each boot), audio, camera capture, suspend.
* How to build the images with Docker (kernel/uImage, Alpine rootfs), with the firmware step.
* Link to `docs/FLASHING.md` for the installation.
