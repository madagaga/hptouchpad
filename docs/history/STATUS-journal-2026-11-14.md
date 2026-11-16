# Project status: mainline Linux + Cog on the HP TouchPad

Last updated: 2026-11-09. Conversation language is French, everything in this repository is English. The project is under git (local repository,
no remote) since 2026-10-26.

## Goal
Boot a mainline Linux kernel (6.12.x) on the HP TouchPad (APQ8060, "tenderloin"/"topaz") and run WPE WebKit + Cog on it. Our work is the
Linux (kernel, device tree, drivers); the userspace comes from a distribution's prebuilt packages (Alpine 3.21 apk) with adjustments only.
All builds run in Docker.

## Milestones

| # | Milestone | State |
|---|---|---|
| 1 | Docker build environment (kernel, initramfs, uImage) | Done |
| 2 | Talk to the tablet: novacom through Docker + usbipd-win | Done |
| 3 | Dump the stock webOS hardware data | Done (`data/`; the webOS root LV can also be mounted read-only from our Linux) |
| 4 | Mainline boot (RAM boot with `novacom boot`, `scripts/cycle.sh`) | Done |
| 5 | USB gadget (ECM + ACM) | Serial (ACM) works; network (ECM) does not (not needed: Wi-Fi + SSH) |
| 6 | Remote shell on the tablet | Done: USB serial (`scripts/tablet-cmd.sh`) and SSH over Wi-Fi (`scripts/tablet-ssh.sh`) |
| 7 | eMMC (mmcblk0, SDCC1) | Done: webOS partitions readable, rootfs image file on the webOS media volume |
| 8 | Display (LG panel, MDP4 LCDC, KMS) and backlight | **Works**: msm DRM/KMS 1024x768 (patch 0018, KMS DTB), backlight PWM (PM8058 LPG, patch 0016) |
| 8b | GPU Adreno 220 (freedreno) | **Works (2026-10-26)**: varying defect fixed by a GFX3D power cycle at every hw_init (patch 0029), vbotest 180/180; Cog on the GPU (GLES renderer) confirmed by the user: smooth, no flicker. The browser starts on the GPU by default (software fallback) |
| 9 | Touchscreen (Cypress CY8CTMA395) | **Works on the live area**: GSBI10 UART/I2C in the DT + userspace `ts_srv` port (uinput touch screen, libinput/Cog), exact physical calibration. **This tablet's edge electrodes do not respond (hardware, 2026-10-26; firmware, its files and configuration ruled out on 2026-10-29 by comparison with a second TouchPad, see "Touch controller comparison with a virgin TouchPad")**: left ~190 px and right ~90 px do not detect. See "Touch screen" |
| 10 | Wi-Fi (ath6kl, SDIO on SDCC4) | **Works**: scan, WPA2 (wpa_supplicant), DHCP, SSH (`scripts/tablet-wifi.sh`); 5 GHz used |
| 10b | Bluetooth | **Works (classic, controller level)** in the initramfs: `hci0` up, PSR loaded, inquiry and visibility verified. 2.1+EDR only, **no BLE** (CSR BlueCore 6). **Alpine (2026-10-29, second unit)**: `bt-power` + `bluetoothd`, driven by the settings panel (`docs/SETTINGS-PANEL.md`, 3c); pairing of "Just Works" devices only, no actual pairing tried yet |
| 11 | Audio (WM8958), mic/sound | **Speaker playback works (2026-11-08)**: DSP-less LPAIF driver (patches 0024-0026) + WM8958 + machine driver `tenderloin.c`, `tp sound` and panel section. **Microphone blocked (2026-11-09)**: capture runs at the right rate but samples stay at the floor while webOS records with identical LPAIF registers; headphones/jack not tested. See "Sound: research" and "Microphone" |
| 11b | Camera (Aptina mt9m113, front) | **Sensor works as a V4L2 sub-device (2026-10-27)**: GSBI4 I2C, PM8058 LVS0/L11, MCLK 24 MHz (camclk0), our `mt9m113` driver streams 640x480 UYVY on one CSI-2 lane, its auto exposure verified against the light. Data path (MIPI CSI-2 into VFE 3.1) has no mainline driver: next, step 3 (receiver). See "Camera" |
| 11c | Buttons, LEDs, battery, sensors | **Buttons work** (power, volume up/down, home: 2026-10-26, see "Buttons"); **Home button LEDs work** (LM8502, 2026-10-27, see "Home button LEDs"); charger (MAX8903B) described in the DT, status exposed; battery gauge (A6 over I2C) in progress; sensors, vibrator and torch not started |
| 11d | CPU frequency and thermal | **Works up to 756 MHz** (Scorpion ACPU clock driver patch 0017, cpufreq-dt, schedutil); TSENS + passive trip (patch 0010). Above 756 MHz needs the SAW voltage path (not started) |
| 12 | Rootfs with Mesa + WPE + Cog | **Alpine 3.21 (prebuilt apk packages) works** (2026-10-26, see "Alpine 3.21 rootfs"): Cog on the GPU, touch, Wi-Fi, SSH; only the Cog DRM module is rebuilt (crash fix). **Debian and Buildroot removed on 2026-10-27** (see "Cleanup") |
| 13 | Boot from eMMC / permanent install | Procedure written in `docs/FLASHING.md` (novacom/novaterm only, moboot menu required: stock webOS gets moboot then Linux; CM: add Linux or replace CM). 32 GB unit: `uImage.Linux` installed in `/boot`, starts from the moboot menu (2026-10-29) |
| 14 | Second TouchPad (32 GB, virgin) | Analysed read-only on 2026-10-28/29 (see "Second TouchPad" and "Touch controller comparison"): partition table and bootloaders identical to tpdebrick's, A6 firmware flashed on request (same version), battery seen again (38 %), our kernel boots on it through the `bootie` recovery. **Next work happens on this unit (user decision 2026-10-29)** |

## What works (verified on the device, 2026-10-26)
- Reference image: **`out/uImage-clean`** (Linux 6.12.40 with the 19 patches and `kernel/src`, KMS DTB `qcom-apq8060-hp-touchpad-kms`, rebuilt on
  2026-10-27 from the cleaned repository; the earlier `uImage-led` is gone); rootfs `touchpad/rootfs-alpine.ext4` on the webOS media volume
  (`touchpad/rootfile`), also rebuilt from the repository (`scripts/build-ts-srv.sh`, `build-cog-alpine.sh`, `alpine-rootfs.sh`) and checked on the tablet:
  GPU browser, touch (musl `ts_srv`), buttons, LEDs, camera probe. Boot: `scripts/cycle.sh out/uImage-clean`, then `scripts/tablet-wifi.sh <ssid> <key>`.
- Boot to the browser in about 20 s: BusyBox init, `/etc/tp.d` scripts (gadget, udev, power policy, SSH, Wi-Fi, touch, browser).
- Display KMS + backlight, GPU (freedreno, Mesa 24.2), Cog on the GPU, touch (live area), buttons, Wi-Fi WPA2 + SSH, Bluetooth (initramfs),
  cpufreq 432-756 MHz, TSENS, charger status, USB serial console, software reboot into recovery/webOS.

## Known issues / open questions
1. **USB network (ECM) does not work, USB serial (ACM) does** (the gadget seems to drop outgoing frames). Not needed: Wi-Fi + SSH.
2. **Touch edges**: the dead columns of this unit (0-2, 4-5, 33-39 minus 36) do not come from the firmware, its files or its configuration (identical on a virgin unit whose 40 columns all respond) nor from pressing or flexing the camera-side edge; the opposite edge was not tested under pressure. Hardware as far as we can tell; nothing to fix in software. The user decided on 2026-10-29 to stop investigating this unit.
3. **Mesa regression**: the right column / bottom row of a draw is not rendered with Mesa >= 22.3 (fine with 20.3.5); proposed as a separate task.
4. **No system suspend** and the CPU is capped at 756 MHz (no SAW voltage driver): the tablet draws about 1 A when running (backlight ~0.5 A).
5. Kernel config is merged from `kernel/touchpad.config` whenever it changes (stamp), into `/build/obj-kernel/.config` in the `hptouchpad-build` volume.
6. Several kernel patches are field fixes, not upstream quality yet (e.g. 0029 writes the MMSS clock controller registers directly).
6b. ~~`poweroff` does not switch the tablet off~~ **fixed 2026-11-05**: patch 0008 still writes "recover" early (crashes land in recovery),
   and a reboot notifier writes "reboot" / "shutdown" (HP values) on a clean restart / power-off. See "Restart and power off" below.
6c. **Low-battery power saving looks like a dead screen**: `tp-powerd` (initramfs) sets the backlight to 5 % and caps the CPUs when the battery is at 15 % or less and discharging (`initramfs/tp-powerd`, lines 12 and 100-101); on 2026-10-27 it made the tablet look off at 11 %. Plugging a real charger restores it; the planned `tp` application should show a visible hint.
6d. **Tooling lessons (2026-10-28/29)**: the container `hptouchpad-novacom` is the user's work VM: never recreate it (`docker restart` is tolerated, it hung once when `novacomd` was stuck on a USB request after a `find /` on the tablet; the user restarted it). Do no wide scans over novacom, and use the proven detached launcher (`setsid`) for anything that stops `hidd`. `catnip scan` without `--count` never returns. After a PC reboot Docker Desktop and WSL are stopped; starting Docker is allowed, the container comes back by itself.
7. **Planned** (user design 2026-10-26, not started): turn `tp` into a C application (`tp daemon`: Home key toggles the screen, triple Home opens a
   settings page served on 127.0.0.1 with civetweb + cJSON: Wi-Fi, start URL, rotation, inactivity timeout, battery).
   **Revised 2026-10-29, see `docs/SETTINGS-PANEL.md`**: Phosh, Flutter and Rust/Slint rejected (second DRM client, musl, weight); the panel is a page
   shown by Cog itself (`cogctl open`, needs a D-Bus session bus), served by `busybox-extras` httpd with one CGI routing to the existing `tp` command (extended with `-j` JSON output and new subcommands) which calls Alpine tools (`wpa_cli`,
   `bluetoothctl`, sysfs); our code is only a small C daemon `tpd` (libevdev: triple Home, inactivity screen-off with touch grab). The page POSTs the `tp` argument list,
   a C router execs `tp -j` without a shell. Date/time added: BusyBox `ntpd`, `tzdata`; **no RTC in the kernel yet** (PM8058 RTC to add).
   Step 0 done (2026-10-29): UI template `rootfs/overlay/usr/share/tpd/www/index.html` with mock data, reviewed at 1024x768 in a desktop browser;
   not on the tablet yet, no backend.
   Step 1 done (2026-10-29, second unit, live install with `apk add` + files piped over SSH, image not rebuilt): packages `busybox-extras dbus
   libevdev bluez bluez-deprecated tzdata` (also in `docker/alpine-rootfs/Dockerfile`), session bus `/run/dbus-session` started by `S90browser`,
   `S95panel` (httpd 127.0.0.1:8080, `/usr/share/tpd/www`); `cogctl open http://127.0.0.1:8080/` shows the template in Cog. Rootfs 69 % used
   after the packages (162 MB free).
   Step 2 done (2026-10-29, checked by the user): `tpd` (`rootfs/tpd`, `scripts/build-tpd.sh`, started by `S95panel`) runs `tp panel toggle`
   on a triple Home (new `tp panel open|close|toggle`), turns the screen off on one press or after `screen_off` (300 s, `touchpad/tpd.conf`)
   and grabs the touch screen while it is off. First try failed only because `tpd` called `tp` through a PATH without `/usr/local/bin`
   (started by hand): it now uses the absolute path. libevdev is not needed (dropped from the Dockerfile).
   Step 3 started (2026-10-29): CGI router `tp-cgi` (C, no shell), `tp -j` -> `tp-panel` (JSON), `S15time`, page on the real API; read
   commands, brightness, delay, URL, time zone and NTP wired, Wi-Fi/Bluetooth changes not yet (see `docs/SETTINGS-PANEL.md`, "Steps").
   `tp` now sets its own PATH (`/usr/local/bin` first) for callers like daemons and CGIs.
   Step 3b (Wi-Fi) done the same day: saved networks in `touchpad/wpa_supplicant.conf` (hex PSK), DHCP by `wifi-event` on each connection;
   the second unit now reconnects to `Crossover-IOT_5G` by itself at boot (`tablet-wifi.sh` no longer needed there).
   Screen rotation added to the Display page (Cog `rotation` parameter + libinput calibration matrix through udev for the touch):
   checked by the user in portrait, rotations 2 and 3 seem to work too.
   **Battery gauge unreadable on battery (2026-10-29 evening, second unit, cable unplugged)**: every read of `capacity`, `current_now`,
   `status` fails with -6 (ENXIO: the A6 at 0x31 does not answer). Raising GPIO 155 (A6 wake) makes it answer at once (0 %, 3459 mV,
   -777 mA, 28.6 C); released, the reads fail again. The earlier "no wake line needed" was only true with the cable plugged. Consequences:
   the panel showed 0 % (tp-panel turns a failed read into 0) and **tp-powerd skipped its loop, so the 5 % shutdown never ran**: the tablet
   went silent right after these reads (battery empty, presumably). To do: wake GPIO in the driver around each transfer (DT `wake-gpios`),
   tp-panel `null` instead of 0, tp-powerd that does not skip its protection when the gauge fails.
   **Decided the same evening (user)**: replace `tp`, `tp-panel`, `tp-powerd`, `tpd` and `tp-cgi` by one static C binary `tp` (`--daemon`,
   client over the Unix socket `/run/tp.sock`, `--cgi`), external tools still called as programs. Design and migration order:
   `docs/SETTINGS-PANEL.md`, "Next architecture: one `tp` binary". Migration steps 1 (A6 wake GPIO in the driver + DT) and 2 (`rootfs/tp`:
   daemon, client, CGI; native battery/charge/panel/power/screen) written and checked locally only (compile, QEMU container); nothing
   sent to the tablet (another agent was testing on it). See "Progress" there.
   **On the tablet (2026-10-29 21:00, second unit, user go-ahead)**: RAM boot of `out/kernel-a6/uImage` (isolated build: qcom_defconfig
   base of d60c35e + the A6 wake fix + the other agent's uncommitted SAW regulator, which now compiles): gauge read at probe ("sense
   resistor 10 mOhm"), 27 % on a PC port; Wi-Fi joined `SFR_E070` by itself (192.168.1.249). Installed live over SSH (MD5 checked): `tp`
   (C), `tp-sh` (the shell tp), `tp-panel`, `S03power`, `S95panel`; `tp-powerd` and `tpd` stopped. `tp --daemon` runs, sees the touch
   screen, Home and power key, detects the PC source; `tp battery|power|screen` and `tp -j info` (through tp-sh) answer; the CGI answers
   native and delegated commands and refuses `ls` (403). **Cable unplugged: the gauge answers** (5 reads in a row: 27 %, 3679 mV,
   -1150 mA, Discharging; `tp power` gauge ok): the A6 wake fix works. Side effect seen: the unplug came 20 s after a step up to 900 mA,
   so the daemon lowered the PC limit to 750 mA ("input lost after a step up"), a false conclusion it keeps while a PC comes back within
   2 minutes. Not checked yet: triple Home and screen sleep with the new daemon (the screen went off after the 30 s set on the tablet).
   The page showed "[mock]": the first failed `fetch` (httpd restarting) switched it to mock data for good. Now mock data only when
   the page is opened from a file (`file://`); on the tablet a failed call is an error and the next one retries. Also: `tp --cgi`
   unsets `GATEWAY_INTERFACE` before handing over to tp-sh (tp-panel calls tp, which would have started in CGI mode again).
   Both installed on the tablet; an SSH drop left the daemon stopped for a moment, restarted.
   **Screen sleep (user choice)**: only Home wakes the screen now (touches are grabbed and dropped while it is off), and the touch
   controller is powered off meanwhile through the ts_srv control socket (`/run/tsdriver`: 'C' closes the UART and cuts its supply,
   'O' powers it up again). Checked on the tablet: `tp screen off` -> ts_srv "touchscreen_power: enable=0", 0 touch UART interrupts in
   3 s; `tp screen on` -> "enable=1"; the user confirmed the touch screen answers right after a Home wake. Saving not measured. Caution for restarts: `pkill -f "tp --daemon"` inside a command line that
   contains that text kills its own shell (happened once); use `kill $(pidof tp)`.
   **Browser page: "Clear the cache"** (`tp -j browser cache-size|clear-cache`): Cog runs with `HOME=/`, its WebKit cache is `/.cache/wpe`
   (`/root/.cache/wpe` comes from runs started by hand). Clears `WebKitCache` and `CacheStorage` only (site data in `/.local/share/wpe` and
   the HSTS list stay); Cog is stopped first (its network process keeps the files open), started again, then the panel reopens. Installed
   on the tablet: the size readout answers (1564 kB); the user cleared the cache from the panel: works, the panel comes back. The page now uses mock data whenever it is
   not served over HTTP (a `data:` preview counted as a real page before).
   **`Makefile`** (2026-10-30, meant to replace `scripts/release.sh` written the day before; not run yet, not even `make -n`: no make on the
   PC, Docker Desktop was down): dependency tracking over the existing scripts (initramfs, kernel/uImage, `ts_srv`, `tp`, Cog module, rootfs
   image; settings stamps `out/.vars-*` so that a changed `DTB_NAME`/`KERNEL_VERSION`/... rebuilds). `make release`: refuses uncommitted changes
   unless `DIRTY=1`; output `out/release/<date>-<commit>/` (`uImage`, `rootfs-alpine.ext4`, `SHA256SUMS`, `BUILD-INFO`). No upload.
   Needs make in Git Bash (`winget install ezwinports.make`). `scripts/release.sh` still there, to delete once the Makefile is checked.
   **Power rule changed (user)**: a cable always means charging (CEN high), no low-battery power-off on a cable (screen off at 5 % so
   that even a PC port charges); power-off at 5 % only without a cable. In the `tp` daemon and in the initramfs `tp-powerd`.
   Battery page: charge window (start/stop levels, toggle) applied by `tp-powerd` (now the `tp` daemon in Alpine) through the charger CEN pin (GPIO 41); mechanism checked
   on a PC port, a real stop/resume on a wall charger not seen yet (`docs/SETTINGS-PANEL.md`). The second unit is back in landscape (rotation 0).
   Notes of the day: `scripts/tablet-put.sh` returned an empty `__MD5__` for two small files to `/mnt/media/touchpad` (cause not found; Python
   is present on the PC); a 375-line command file sent with `tablet-put-inner.sh` (60 s) worked for `/tmp`; the Wi-Fi of the second unit at its
   current place drops to -81 dBm and loses the gateway, use the USB serial path then.

## Priority hardware plan (requested 2026-10-15: Wi-Fi, Bluetooth, camera, mic/sound first)
Common step 0: read the webOS root filesystem (it holds the Wi-Fi firmware and calibration data, the
Bluetooth bring-up scripts and chip identification). It lives in an LVM volume on `mmcblk0p14`; the
kernel can map it read-only with `dm-mod.create=` on the command line (needs the LVM metadata, read
from the start of p14) and no extra tools. Needs `CONFIG_BLK_DEV_DM` and `CONFIG_DM_INIT`.

| Block | Facts from webOS (dmesg, config) | Mainline status | Effort |
|---|---|---|---|
| Wi-Fi | SDIO card on SDCC4 (`mmc1`, `121c0000`, 4-bit); power sequence: `8058_s3` 1.8 V, `8901_l1` 3.3 V, `8058_l19` 1.8 V, then GPIO `CHIP_PWD`; "colocated bt" string (Atheros AR6003-like) | `ath6kl` SDIO driver exists; needs firmware + board data from webOS; DT: `sdcc4`, regulators, `mmc-pwrseq-simple` with the CHIP_PWD GPIO | Medium |
| Bluetooth | "Powering on BT" at boot, a UART-attached controller, powered together with Wi-Fi | `hci_uart` over `qcom,msm-uartdm` (serdev) exists; the controller and its GPIOs must be identified first | Medium, uncertain |
| Audio / mic | WM8958 codec rev B at I2C 0x1a (webOS bus 4); MSM8x60 ASoC platform + DAI over the ADSP (`audio pmem`); codec has no IRQ | `wm8994` codec driver exists; the MSM8660 audio path (ADSP/LPA/MI2S) does not | Hard (new driver from the Android sources) |
| Camera | Aptina `mt9m113` on the MSM camera subsystem (`CONFIG_MSM_CAMERA`) | `mt9m111` driver exists; the VFE/CSI camera pipeline for MSM8660 does not (camss starts at msm8916) | Very hard (new driver) |

Suggested order: step 0, Wi-Fi, Bluetooth, then decide on audio and camera (each is a driver port).

## Wi-Fi and Bluetooth facts (read from the stock webOS rootfs and the CyanogenMod kernel source)
The webOS root filesystem was read through a read-only loop mount from the serial shell:
LVM volume group `store` on `mmcblk0p14` (extent size 8 MiB, `pe_start` 2048 sectors), `root` LV = 71
extents at the start: `losetup -r -o 1048576 /dev/loop0 /dev/mmcblk0p14; mount -t ext3 -o ro,noload`.
The LVM layout is in `out/lvm-metadata.txt`.
- **Wi-Fi chip**: Atheros AR6003 (webOS package `pmwifidriver-atheros-6003`, module `ar6000.ko`),
  hardware revision hw2.1.1. Firmware copied to `firmware/webos/ath6k/hw2.1.1/` (`athwlan.bin`,
  `otp.bin`, `data.patch.bin`, `bdata.SD32.bin` = board data of this module). The mainline `ath6kl`
  driver wants the same file names under `ath6k/AR6003/hw2.1.1/` and the board data as `bdata.bin`.
- **Wi-Fi power sequence** (downstream `board-tenderloin.c`, CyanogenMod `hp-kernel-tenderloin`,
  branch `ics`): `8058_s3` 1.8 V (always on), `8901_l3` 3.3 V (WLAN_PA_3V3), `8901_l1` 3.3 V
  (VDD_WLAN_3V3), `8058_l19` 1.8 V (VDD 1.8), 5 ms, then WLAN_RST_N (= CHIP_PWD) GPIO 135 low then
  high. Other pins: WL_HOST_WAKE GPIO 93, HOST_WAKE_WL GPIO 137. SDIO on SDCC4, 4-bit.
- **Bluetooth chip**: CSR BlueCore 6 (rfkill name `csrbc6`, CSR Synergy stack in webOS, BCSP-like
  "Casira" options). UART: UART1DM = GSBI6 at `16540000` (mainline `gsbi6_serial`), GPIO 53 TX,
  54 RX, 55 CTS, 56 RTS; 115200 baud at start, 3 686 400 baud afterwards. Power: BT_POWER GPIO 130
  high, BT_WAKE 131, BT_RST_N 138, BT_HOST_WAKE 129. The BD address comes from the webOS token
  `/dev/tokens/BToADDR`. There is no mainline serdev driver for CSR: use `btattach` from userspace.
- **Other firmware found in webOS `/lib/firmware`**: touchscreen `cy8ctma395.*` (Cypress TMA395) and
  `a6_firmware.txt.*`, audio DSP `q6.*`, GPU `yamato_*` / `leia_*`, video `vidc_1080p.fw`, codec
  `wm8958_*.wfw`. webOS has no `CONFIG_BT` in its kernel: Bluetooth runs entirely in userspace.

## Downstream kernel source for analysis
The full CyanogenMod TouchPad kernel (branch `ics`, 597 MB, last commit 2013-07-14) is cloned in the
Docker volume `hptouchpad-build` at `/build/ref/hp-kernel-tenderloin-ics`. Read it with
`docker run --rm -v hptouchpad-build:/build hptouchpad-builder sh -c 'cd /build/ref/hp-kernel-tenderloin-ics && ...'`.
Useful places: `arch/arm/mach-msm/board-tenderloin.c`, `gpiomux-tenderloin.[ch]`,
`board-tenderloin-rfkill.c`, `drivers/mmc/host/msm_sdcc.c`, `drivers/ar6000` and
`drivers/staging/ath6kl` (Wi-Fi), `sound/soc/msm` (audio), `drivers/media/video/msm` (camera).

## Wi-Fi bring-up log (2026-10-15)
1. Every SDIO command timed out (CMD5, CMD52, CMD8, CMD55): the AR6003 was asleep. The downstream
   gpiomux table drives HOST_WAKE_WL (GPIO 137) high while Wi-Fi is active; it was left as an input.
   Fix: `wifi_wake_pins` in the DTS (137 output high, 93 input pull-down). The card then enumerates
   (`mmc1: new high speed SDIO card`) and `ath6kl_sdio` probes.
2. At 48 MHz the first command after a CMD53 write (a 256-byte BMI write) times out. Cause, from
   `msm_sdcc.c`: the MSM controller does not wait for the card to leave the busy state after a CMD53
   write; the downstream driver sends a dummy CMD52 with `MCI_CPSM_PROGENA` (waits for `PROGDONE`)
   before the next request whenever the previous one was a CMD53 write
   (`dummy52_required`, `CONFIG_MMC_MSM_SDC4_DUMMY52_REQUIRED=y` in the webOS config). Mainline `mmci`
   has no equivalent. Workaround for now: `max-frequency = <12000000>` on `sdcc4`, with which all BMI
   transfers succeed. A proper fix would be a small `mmci` quirk (not written yet).
3. With the bus at 12 MHz and the printk delays of the dynamic debug, the firmware (webOS `athwlan.bin`)
   downloaded and started, then the target crashed (register dump, `htc wait target failed: -12`): that
   old Atheros SDK firmware does not work with the mainline driver. The image now uses `fw-3.bin` from
   linux-firmware plus the TouchPad `bdata.SD32.bin` as `bdata.bin`.
4. With `fw-3.bin` and no debug output the probe fails right after the message "temporary war to avoid
   sdio crc error" with `-70`. `-70` is `ECOMM`, which `mmci.c` returns for `MCI_STARTBITERR`: a start
   bit error while reading data, i.e. the card is not driving the DAT lines correctly. Cause found by
   comparing with the downstream driver (`drivers/staging/ath6kl/os/linux/ar6000_drv.c`): that
   workaround reprograms the AR6003 SDIO pad registers (`GPIO_PIN9..12` in mainline) and the stock
   driver applies it **only to hw 2.0** (`AR6003_REV2_VERSION`, pins 10..13), whereas mainline also
   applies it to hw 2.1.1, the revision of the TouchPad. Patch `0002` removes it for hw 2.1.1.
   The earlier 12 MHz run that worked was only slowed down by the debug printing (not a clock fix).
5. Patch `0001` adds the dummy CMD52 after a CMD53 write (`qcom,dummy52-required`), as the stock
   kernel does on SDC4. The first version was wrong: it also stopped waiting on `CMDRESPEND`, so it
   returned before the card left its busy phase (the stock driver waits only for `PROGDONE`,
   `CMDCRCFAIL` or `CMDTIMEOUT`). That explains why runs R4/R5 (below) still failed.
6. Patch `0003` is temporary instrumentation: it prints on the console the number of dummy CMD52
   runs (at 1, 2, 4, 8, ...), the time waited for `PROGDONE`, timeouts, and the exact status of any
   SDIO data error (`dev_warn_ratelimited`). Remove it once the Wi-Fi works.

Run history (all with the AR6003 at `mmc1`, 4-bit bus):

| Run | Bus | Debug | Patches | Firmware | Result |
|---|---|---|---|---|---|
| R1 | 48 MHz | dynamic debug on | none | webOS | `-110` on the first read after a 256-byte write |
| R2 | 12 MHz | dynamic debug on | none | webOS | whole BMI exchange OK; the target crashes later (firmware incompatible) |
| R3 | 12 MHz | off | none | fw-3 | `-70` (start bit error) at the extended board data write |
| R4 | 48 MHz | off | 0001 (buggy) | fw-3 | same `-70` |
| R5 | 48 MHz (also 16 MHz) | off | 0001 (buggy) + 0002 | fw-3 | same `-70`, no more "temporary war" message |
| R6 | 48 MHz | off | 0001 (fixed) + 0002 + 0003 | fw-3 | `-70` again, but the instrumentation showed the cause (below) |
| R7 | 48 MHz | off | 0001-0004 | fw-3 | **`wlan0` created**: `ath6kl: ar6003 hw 2.1.1 sdio fw 3.2.0.144 api 3`. Real dummy52 waits of 5-765 us, no timeout. The system then stopped answering during the first `iw dev wlan0 scan` (to investigate) |

R6 instrumentation: the dummy CMD52 ran at least 16 times, no timeout, but each wait lasted only 2-4 us
and the final status was `0x00c00c00` with `PROGDONE` (bit 23) already set: a stale bit, never cleared because
`PROGDONE` is not in the interrupt mask, so the wait returned at once. The failing request is always the
4-byte read of the BMI credit register (`cmd 53 arg 1408a004`, `STARTBITERR`). Patch `0004` clears the stale
events before sending the dummy command.

Only R2 passed, and only because the printk output slowed the bus: the failure depends on the delay
between a write and the next read, not on the clock. Offline checks done before R6: the patch logic
matches `msm_sdcc.c` (same register bits, same wait condition), the zImage payload contains the new
strings, and the decompiled DTB has `qcom,dummy52-required`, `vmmc`, `mmc-pwrseq` and the GPIO 137 pin.

## Software reboot (no buttons)
From our Linux the tablet can reset itself into the bootloader recovery mode, so test cycles do not need
the Power/Volume buttons any more (found in the downstream `arch/arm/mach-msm/restart.c`):
- restart reason word at physical `0x2A05F65C` (IMEM `0x2A05F000` + `0x65C`): `0x6f656d11` = recover
  (USB bootloader mode), `0x6f656d44` = normal reboot, `0x6f656d00` = shutdown;
- reset: write 0 to `PS_HOLD`, physical `0x00800820` (TLMM base `0x00800000` + `0x820`).
`devmem 0x2A05F65C 32 0x6f656d11; devmem 0x00800820 32 0` from the serial shell (BusyBox `devmem`, `/dev/mem`
is open) lands in recovery (`0830:8071`) in about 3 s. `scripts/cycle.sh [image]` does the whole cycle:
reboot to recovery, attach, boot the image, attach the gadget. It works only when the tablet runs our
Linux and answers on the serial shell; after a hang the Power + Home buttons are still needed.
The image now also: declares the restart handler (`qcom,pshold` at `0x800820`, so `reboot` and a panic
reset the SoC), enables the SoC watchdog (`CONFIG_QCOM_WDT=y`, the driver binds to `timer@2000000`, BusyBox
`watchdog -t 5 -T 30` in the init), writes the "recover" restart reason at boot so that any later reset
(watchdog bite, panic, reboot) lands in recovery, panics on soft lockup / hung task (60 s) with a reboot after
5 s, and mounts pstore (`/sys/fs/pstore`) to read the console of the previous boot after such a reset.
Untested: after the first hang the tablet still has to be reset with the buttons once.

## Bluetooth bring-up (2026-10-15) - WORKS at the HCI level
Result: `hci0` is up, the CSR chip answers HCI commands. Read Local Version: HCI/LMP version 4
(Bluetooth 2.1 + EDR), manufacturer 0x000A (CSR), revision 0x12E9. **This chip has no Low Energy: "BLE" is not
possible on the TouchPad, only classic Bluetooth.** BD address is still the CSR default `00:02:5B:00:A5:A5`
(the webOS token `BToADDR` or the PSR `&0001` value has not been applied yet).
- DT: GSBI6 in UART with flow control mode, `gsbi6_serial` enabled (`ttyMSM1`, alias `serial1`), pads as in
  the downstream gpiomux (53-56 function gsbi6, 8 mA, no pull); BT_POWER (130), BT_WAKE (131) and BT_RST_N (138)
  are outputs that start low; BT_HOST_WAKE (129) input with pull-up.
- Power-up sequence (from the notes of the webOS analysis): BT_POWER high, then BT_RST_N low -> high. Without
  BT_POWER the chip stayed silent (first attempt, my mistake: the notes already said it).
- Attach: the kernel `hci_bcsp` does **not** do the BCSP link establishment (SYNC/CONF), `btattach` neither
  (it only showed "Found a LE sync pkt, card has reset" forever). BlueZ `hciattach -n -s 115200 /dev/ttyMSM1 bcsp
  115200 noflow` does it (CyanogenMod `bt_vendor.conf`: Type bcsp, InitSpeed 115200, FlowCtl 0).
- Tools in the initramfs: `hciattach`, `btattach`, `hcitool`, `btmon` (Debian bluez) and `hciup`
  (`initramfs/tools/hciup.c`); `bt-test` runs the whole sequence (GPIO via sysfs, `CONFIG_GPIO_SYSFS`, bring-up only).
- PSR: BlueCore keys of the CyanogenMod device tree (`firmware/cm/bluecore6.psr`, crystal 26 MHz, trim 25, TX power
  table, PCM...) are loaded into the volatile PS RAM store with BlueZ 5.55 `bccmd` (built statically by
  `scripts/initramfs.sh`; recent BlueZ no longer has it): `bt-psr`. A warm reset (`-r`) is needed, which drops the
  BCSP link, so `hciattach` is run again without cycling the power. The UART keys (baud 3.6864 Mbaud, UART
  config, host wake) are left out for now: the link stays at 115200. The BD address comes from the webOS token
  `BToADDR` (ASCII text in the tokens partition `mmcblk0p12`, read at run time by `bt-psr`; PSKEY_BDADDR words
  are LAP[23:16], LAP[15:0], UAP, NAP). Busybox `tr` has no `[:print:]`: use ` -~`.
- Radio check, both directions: with the PSR loaded the inquiry (`hcitool scan`) lists a nearby device, and the
  tablet (`bt-visible`, name "HP TouchPad", laptop class) is seen in the scan of a phone. Without the PSR (crystal
  trim) nothing was seen: the PSR is required at every power-up, like in webOS and Android.
- To do: apply the PSR (`firmware/.../bluecore6.psr` from the CM device tree: BD address, TX power table, PCM, UART
  3.6864 Mbaud, host wake), proper BD address from the webOS token, DT description (reset/power as
  `rfkill-gpio` or a power sequence instead of sysfs), host wake IRQ, coexistence with the Wi-Fi, BlueZ + audio
  profiles later.

## CPU frequency and thermal (2026-10-15)
**Facts (measured on the tablet, or read in the sources):**
- Boot state left by the bootloader: CPU0 runs from its SCPLL at L=14 (2 x 27 MHz x 14 = **756 MHz**), L2 at
  432 MHz (L=8), and CPU1, released by the kernel (`qcom,gcc-msm8660` enable method), sits on the slow AFAB bus
  clock: it ran about **twice slower** than CPU0 (same job: 3 s on CPU0, 6 s on CPU1). Both SCPLLs are
  already calibrated and in normal mode.
- Fix applied in `initramfs/init` (temporary): set the core source of CPU1 (`0x02051008`, bits 2:1) to the SCPLL,
  like CPU0. Result: both cores 756 MHz, same timing, no hang. `cpu-state` decodes the registers.
  Registers: ACC0/1 `0x02041000` / `0x02051000` (CLK_CTL +4, CLK_SEL +8), SCPLL CPU0/CPU1/L2 `0x00903200` /
  `0x00903300` / `0x00903400` (CTL +4), L2 select `0x02082038`, QFPROM PTE efuse `0x007000C0` (reads 0 here).
- Downstream driver (`acpuclock-8x60.c`): per-core SCPLL + L2 SCPLL, PLL8 384 MHz as a safe intermediate, frequency
  tables per speed bin / PVS (efuse 0 -> the 1188 MHz table, which is what webOS reports: `Max ACPU freq:
  1188000 KHz`), each step has a core voltage (756 MHz: 1.000 V, 1188 MHz: 1.1875 V, 1512 MHz: 1.25 V), an L2 level
  (with `vdd_dig`/`vdd_mem` and an AXI bandwidth vote) and an AVS setting; the voltage is changed through the
  SAW regulators (`saw-regulator.c`, PM8058 S0/S1) around each switch. webOS uses the performance governor.
- Mainline: **no SCPLL/ACPU clock driver for MSM8660** (a 2018 LKML thread on the Krait cpufreq series says it
  still has to be written; nothing posted since), no SAW/SPM description for this SoC, so no cpufreq today.
- Thermal: the webOS kernel has `THERMAL_PM8901` (PMIC die temperature) and **not** `THERMAL_TSENS`; the
  hardware over-temperature protection is in the PMICs. The SoC TSENS exists (GCC `0x3620`+, interrupt GIC SPI 178),
  mainline `tsens-8960.c` has 8660 constants (11 sensors, only 5 handled) but no 8660 compatible, no DT node, and
  its calibration comes from QFPROM cells whose layout is not known for this SoC.
**Temperature sensor (TSENS) - works (2026-10-15):**
- By hand first (`scripts/tsens-probe.sh`, devmem, sequence of the vendor `drivers/thermal/msm_tsens.c`): QFPROM word
  `0x007000BC` = `0x006F0000` -> calibration code at 30 C = 111 (primary byte 0xBE; redundant byte 0xBF = 0), CNTL at
  `0x00903620` (default `0x20000000`, the vendor driver sets CONFIG bits 29:28 = 1, measure period 4, SLP_CLK_ENA
  bit 24), sensor 0 status at `0x00903628`: code 123 -> 38 C. Only sensor 0 is useful (sensors 1-4 read 0).
- Kernel: patch 0010 adds `qcom,msm8660-tsens` to `tsens.c`/`tsens-8960.c` (one sensor, slope 1424 = 1000 / 0.702, the
  redundant calibration byte wins when programmed, CONFIG field set); patch 0011 makes `gcc-msm8660` populate its child
  nodes (the TSENS node is a child of the clock controller, whose registers it shares as a syscon: `gcc` gets
  `"syscon"` in its compatible); DT: `qfprom` node with the calibration cells (`calib@be`, `calib-backup@bf`), `tsens`
  node (IRQ GIC SPI 178 "uplow", `#qcom,sensors = <1>`), `soc-thermal` zone with a `hot` trip at 90 C and a
  `critical` trip at 105 C (the vendor upper threshold was code 0xDF, about 109 C); config: `QCOM_TSENS`,
  `NVMEM_QCOM_QFPROM`, `THERMAL_OF`, `THERMAL_HWMON`.
- Result: `/sys/class/thermal/thermal_zone0` (`soc-thermal`) and `hwmon0` (`soc_thermal`): 38 C at idle, 41 C with both
  CPUs loaded for 40 s. One harmless `Temperature check failed (-110)` at boot (first read before the sensor is
  ready, the next poll works). The `critical` trip makes the kernel power the tablet off in an orderly way.
- Not done: no cooling device yet (needs cpufreq), PM8901 die temperature (webOS `THERMAL_PM8901`) and the battery
  thermistor are not read. One boot of this image never brought the USB gadget up (the tablet fell back to recovery
  after the 60 s wait of `cycle.sh`); the next boot of the same image was fine: not reproduced, to watch.
**Plan:** (1) done: CPU1 on the SCPLL. (2) done: TSENS read-only. (3) cpufreq: new SCPLL/ACC clock driver + voltage
control (SAW or direct PMIC) + OPP table + `cpufreq-dt`; the voltage side must be understood before any frequency
above the boot one is tried. (4) Cooling device = cpufreq (throttling) once (3) exists, with passive trips below the
`hot` one.

## CPU frequency driver: analysis (2026-10-16, before any code)
Sources read: `acpuclock-8x60.c` (CM11 3.4, table `acpu_freq_tbl_oc`), `saw-regulator.c`, `spm.c`, the tenderloin board file.
- **Frequency switch (vendor sequence):** per core, SCPLL registers at `0x00903200 + n*0x100` (CTL +4, CAL +8, STATUS +0x10, CFG +0x1C, FSM_CTL_EXT +0x24), `scpll_change_freq`:
  write `(L << 3) | COMPLEX_SLEW(7)` to FSM_CTL_EXT, `SCPLL_NORMAL(7)` to CTL, wait until CTL bits 8:3 == L, then until STATUS bit 0 clears. SCPLL <-> PLL8 (384 MHz,
  ACC CLK_CTL bank mux + divider, CLK_SEL bits 2:1 = core source) goes through `select_clk_source_div` / `select_core_source`; the SCPLL is powered up (`STANDBY`,
  shot switch) before it becomes the source. Initial SCPLL calibration (`scpll_init`, full cal for L 8..max) was done by the bootloader here (both PLLs are in normal mode).
- **Voltage (vdd_sc):** per core regulator `8901_s0` / `8901_s1` = PM8901 FTSMPS driven by the **SAW** (`msm_spm_set_vdd`: write VCTL at SAW+0x08, poll STS at +0x0C until the PMIC
  state is idle and the current data equals the request). VCTL code = band | vprog: band 2 (0x80) = 700..1400 mV in 12.5 mV steps, e.g. 0x94 = 950 mV (the vendor `awake_vlevel`).
  Table (mV per frequency, `acpu_freq_tbl_oc`): 432 MHz 850, 540 875, 648 900, 702 925, **756 (L=0x0E) 950**, 810 975, 864 975, 918 1000, 1026 1025, 1080 1050, 1188 1100, 1296 1150,
  1404 1200, 1512 1225 ... up to 2157 MHz 1400 (CM11 overclock table; webOS stops at 1188). Ordering: raise vdd_mem (PM8058 S0), vdd_dig (PM8058 S1) then vdd_sc, switch, then lower.
  vdd_mem/vdd_dig are RPM votes (PM8058 S0/S1 in our DT are RPM regulators already), `vdd_mem >= vdd_sc`, vdd_dig >= 1.0 V (1.1 V above 594 MHz or L2 above 594 MHz).
- **L2:** own SCPLL (`0x00903400`), 432 MHz at boot, table of L2 levels with their vdd_dig/vdd_mem and an AXI bandwidth vote (bus scaling); the L2 follows the maximum vote of both cores.
- **Measured SAW state on the tablet (read only, Linux booted from the bootloader, nothing initialised):** SAW0 (`0x02042000`) VCTL +0x08 = `0x35`, STS +0x0C = `0x7800`, CFG +0x10 = `0x14`,
  AVS_CTL +0x04 = `0x10241BFF`; SAW1 (`0x02052000`) VCTL = `0x13`, STS = `0x0803FC00`, CFG = 0. **These are not valid FTSMPS codes** (band bits 00/01 = band 1 = 350..650 mV): the
  bootloader leaves the SAW in its reset state and the cores run on the PMIC default voltage, i.e. the real core voltage is unknown. The vendor driver initialises the SPM first
  (`msm_spm_init`: SAW_CFG 0x0F/0x1C, SPM_CTL 0x68, timers, PMIC_CTL...) and uses `msm_spm_set_vdd`.
- Mainline has: `qcom,saw2` SPM driver (`drivers/soc/qcom/spm.c`, cpuidle only, no msm8660 compatible, no voltage setting), `clk-krait`/`krait-cc` (HFPLL based, 8064/8960/8974, not SCPLL),
  `cpufreq-dt`, `qcom-cpufreq-nvmem` (Krait speed bins). **Nothing for the Scorpion SCPLL/ACC clocks nor for a SAW voltage regulator.**
- **Decision (safe order):**
  1. **Frequencies at or below the boot one (756 MHz) only, no voltage change**: the voltage is already enough for 756 MHz, so any lower frequency is safe. New clock driver for ACC + SCPLL
     (per-core `clk`, parent PLL8 for 192/384 MHz and the SCPLL for L=8..14), `cpufreq-dt` with an OPP table without regulator, `schedutil` governor, cpufreq cooling device in the TSENS zone
     (passive trips below the 90 C `hot` trip). Gives throttling and idle savings, with no risk of a bad voltage.
  2. **Above 756 MHz** only after the SAW voltage path works and is understood: SPM/SAW init, a SAW regulator (VCTL/STS), checking the PMIC answer, the L2/vdd_dig/vdd_mem votes, and
     an incremental test (one OPP at a time, starting 810 MHz at 975 mV, watch TSENS, never a blind jump). The user's performance target (Cog/WPE) depends on this step.
- Risks: a wrong sequence while the SCPLL is the source hangs the core (no log survives, a crash falls back to recovery); the L2 is left untouched in step 1.

## CPU frequency driver, step 1 (2026-10-16): works, partly measured
- Code: `kernel/src/drivers/clk/qcom/clk-scorpion-acpu.c` (clock provider `qcom,msm8660-acpu`, one clock per core, SCPLL complex slew, L = 8..14 = 432..756 MHz, no voltage change,
  puts the second core on its SCPLL at probe like the `initramfs/init` workaround), patch `0017-clk-qcom-add-scorpion-acpu.patch` (Kconfig, Makefile), DT (`acpu` node, `cpu_opp_table` with 7 OPPs,
  `clocks`/`operating-points-v2`/`#cooling-cells` on both CPUs, passive trip `soc-alert` at 75 C with the two CPUs as cooling devices), config (`SCORPION_ACPU_CLK`, `CPUFREQ_DT`,
  `schedutil` default, `CPU_FREQ_THERMAL`). Image `out/uImage-cpufreq`.
- Result on the tablet: `cpufreq-dt` binds by itself (no allow-list needed), `scaling_driver=cpufreq-dt`, governor schedutil, available frequencies 432..756 MHz, and the governor
  really moves the cores (seen: cpu0 L=8 / cpu1 L=10 at idle). Both cooling devices are registered.
- Pinned benchmark (`scripts/cpufreq-bench.sh`, 100000 iterations of a shell loop on cpu0; **pin = max first, then min**: leaving max at 756 lets schedutil raise it, my first runs were wrong because of that):
  432 MHz 9.06 s, 486 8.10, 540 7.32, 594 6.66, 648 6.15, 702 5.69, 756 MHz 5.30 s: the time follows 1/frequency (432 -> 756 MHz: x1.71 faster for x1.75 in frequency), all 7 OPPs work.
- Not yet checked: the thermal throttling (`echo N > /sys/class/thermal/cooling_device*/cur_state` to force it), a long stability run with the governor, CPU hotplug.
- Host problem seen during the test: only 0.6 GB of host RAM free, Docker engine answers 500 and WSL (Alpine, serial path) answers `Wsl/Service/0x8007274c`, the WebKit build log stopped at 2098/5867.

## Charging and battery (2026-10-15, in progress)
**Facts:**
- Charger chip: Maxim **MAX8903B**, GPIO controlled (downstream `drivers/power/max8903b_charger.c`, pins in
  `gpiomux-tenderloin.h`, Wi-Fi board): CEN GPIO 41 (charge enable, **high = enabled** on this board, polarity 1),
  DCM 42 (high = "USB mode", low = "DC mode" with the current set by ISET1/ISET2 = GPIO 34/30: 750 mA, 900 mA,
  2 A or 1.4 A depending on the hardware revision), IUSB 133 (high = 500 mA, low = 100 mA, USB mode), USUS 33
  (high = USB input suspended), outputs DOK_N 140 (low = valid DC input), CHG_N 36 (low = charging, high-Z = not
  charging or done), FLT_N 35 (low = fault); the three outputs are read with a pull-up. The cable VBUS is on the
  DC input (DOK_N low even on a PC port). The downstream driver sets DCM = USB mode, IUSB = 500 mA, USUS = 0,
  CEN = 1 at probe and switches to the DC mode with a higher current when a wall charger is detected (by the
  USB PHY, "gadget_event: source=charger mA=900" in the webOS dmesg).
- State left by the bootloader: CEN 1, DCM 1, IUSB 1, **USUS 1**, ISET 0/0, DOK_N 0, CHG_N 0 (charging),
  FLT_N 1.
- Battery gauge: not in the SoC: the **A6 microcontroller** (TI MSP430 "TS2" firmware, also the touch panel
  controller; two of them on the GSBI8 I2C bus, which also has the LM8502): `a6_0` at 7-bit address 0x31 (`0x62 >> 1`)
  is the one connected to the battery (`power_supply_connected = 1`), `a6_1` at 0x32. IRQ GPIO 37 (a6_0) and 94
  (a6_1), wake GPIO 155 (a6_0) and 141 (a6_1) (high = force awake; the A6 otherwise wakes periodically), Spy-Bi-Wire
  pins 157/158 and 115/116 for the firmware programming. Protocol: write the 16-bit register address (big endian),
  repeated start, read one byte; multi-byte values are read byte by byte. Battery registers (DS2784-like) from
  `0x0100`: STATUS, RARC (relative capacity, %) `0x0101`, avg current `0x0103/04`, temperature `0x0105/06`,
  voltage `0x0107/08`, current `0x0109/0a`, coulomb counter `0x010b/0c`, full capacity `0x010e/0f`, thresholds at
  `0x0180...` and the command/status register at `0x0140` (auth, refresh, wake, off).
**Done:** DT + config: MAX8903 node (`dok`/`chg`/`flt`/`cen`) for the mainline `max8903` driver, which exposes
`/sys/class/power_supply/max8903_charger` (Mains, online, status, health) and enables CEN while DOK_N is asserted;
`gpio-hog`s keep DCM high (USB mode), IUSB high (500 mA, safe for a PC port), USUS low, ISET 0. The higher DC
current is not used because the kind of source is not detected. Observed: Mains online, health Good, status
"Not charging" (CHG_N high): not known whether the battery was full or not charging.
**Incident:** after this change, the next reboot request from our Linux (PS_HOLD) never brought the tablet back: explained
later (see "Root cause of the boot failures": the TSENS hardware thermal trip, plus the battery discharging on the 500 mA input).
**Root cause of the boot failures of 2026-10-15/16 (found 2026-10-16) - TSENS, not GSBI8:**
- The kernel died right after its last initcall, before the first command of `init`, then the tablet landed in recovery (or
  off after two failures in a row). Filming the screen with `initcall_debug` showed it (the video frames are in `out/video*`).
- A by-hand test on the stable image (`scripts/tsens-hw-test.sh`) killed the tablet the moment the temperature sensor was
  enabled: after a reset the TSENS **threshold register is 0** (`0x00903624`), a MAX threshold of 0 trips the **hardware thermal
  shutdown** of the block as soon as the first conversion completes. The mainline driver (generic code) enables the sensor,
  registers the thermal zone (a slow first read) and only then writes the real MIN/MAX thresholds: a race, hence "intermittent".
- Fix: patch 0010 now writes the thresholds (vendor values: lower 0x50, upper 0xdf, min 0x38, max 0xff) in `init_8660()`,
  **before** the sensor is enabled. Verified: 3 boots out of 3, 37 C at idle, 41 C under load, `hot` 90 C and `critical` 105 C
  trips back in the DT, TSENS interrupt left out of the DT (the "uplow" interrupt of this old TSENS version runs the threaded handler
  in hard interrupt context, a mutex there is a bug; the zone is polled every second instead; harmless `IRQ uplow not found`).
- Everything blamed earlier was wrong: **GSBI8 and the I2C bus are fine** (pads GPIO **64/65**, 62/63 was my mistake),
  the CPU1 clock fix is fine, patch 0012 is fine, the battery was not empty. All the failing images simply had TSENS enabled.
- Side effects of the failures to know: a first crash lands in recovery (patch 0012 writes the Palm "recover" reason early),
  a second consecutive failure powers the tablet off (bootloader protection), and a crash overwrites nothing useful in RAM
  (no pstore log survives: webOS/bootie reuse the area). `scripts/webos-recover.sh` goes from webOS to recovery without buttons.

**Battery gauge (A6) - works (2026-10-16):**
- The A6 `a6_0` answers at 7-bit address 0x31 on the GSBI8 I2C bus (Linux bus 0; `a6_1` at 0x32 does not answer). No wake line
  is needed for reads (the GPIO 155 forced high was not required). Registers verified against webOS (`scripts/webos-battery.sh`:
  96 % / 4.08 V on webOS vs our decoding): voltage = 11 bits x 4.88 mV (`raw >> 5`), temperature = signed 11 bits x 0.125 C,
  current = signed 16 bits x 1.5625 uV / Rsense with Rsense = 1000 / RSNSP(0x0112) = 10 mOhm here, capacity = RARC percent.
- Driver `kernel/src/drivers/power/supply/hp_a6_battery.c` (new file copied into the tree by `scripts/kernel.sh`, Kconfig/Makefile in
  patch 0013, `CONFIG_BATTERY_HP_A6`), DT node `battery@31` under `&gsbi8_i2c` with `power-supplies = <&charger>`. Result in
  `/sys/class/power_supply/battery`: 79 %, 3.87 V, 32.7 C, **-896 mA** (discharging while plugged).
- Important consequence: with the input limited to 500 mA (USB mode, source not detected) the tablet **discharges even when plugged
  to a PC port** (it draws more than 500 mA at 756 MHz with the screen on). That is why a few overnight/long runs ended with the
  tablet off. Need: charger detection to use the DC mode (750 mA - 2 A), lower consumption (backlight via the LM8502, CPU frequency,
  idle states), charge/temperature policy, low-battery shutdown.

## Wi-Fi WPA2, SSH and the USB network (2026-10-16)
- **WPA2 works**: `wpa_supplicant` 2.10 built statically (minimal config: nl80211, WPA2-PSK, internal TLS) by `scripts/initramfs.sh`, helper
  `wifi-wpa <ssid> <passphrase>` (the passphrase only goes to `/tmp` in RAM, never to the repository; `out/tablet-cmd.txt` is wiped after use),
  `wifi-dhcp`, `wifi-connect` (open networks), `wifi-selftest`. Tested on the user's WPA2 network (the SSID the user typed was slightly wrong: the access
  points are called `2sWifi_5G` and `2sWifi`): association completed, DHCP lease on the PC's subnet (192.168.10.x), gateway and internet reachable. The
  signal there is very weak (about -88 dBm), so rates are low (MCS 0).
- **SSH (Dropbear, key authentication only)**: the initramfs starts `dropbear -R -s -E -p 22` when `/root/.ssh/authorized_keys` exists. The key pair is
  generated once in `secrets/touchpad_ed25519(.pub)` (git-ignored; the public key is also copied to `buildroot/overlay/root/.ssh/authorized_keys`,
  git-ignored, for the future rootfs). `scripts/tablet-ssh.sh [-i ip] 'command'` runs a command from the PC (default ip 192.168.10.116, the lease seen).
  Needed `libgmp` (via `libtomcrypt`). Works over the Wi-Fi when the PC and the tablet are on the same network (not through the isolated guest
  network `AKTO-GUEST`).
- **Wi-Fi power save**: with the default power save on, packets sent *to* the tablet were mostly lost (ping from the PC 66 % loss, SSH timeouts);
  `iw dev wlan0 set power_save off` fixes it (0 % loss, 2-24 ms). To make permanent in the init later (power cost to evaluate).
- **USB network (ECM) still does not work**, serial (ACM) does: the device receives the host frames (RX counter increases) but never transmits
  (TX 0, the IN queue of the ECM endpoint is idle, no tx error/drop either: the stack does not even answer ARP). The aligned-DMA idea
  (`CI_HDRC_REQUIRES_ALIGNED_DMA`, patch 0015, kept in `kernel/patches/removed/`) did not change anything. Open.
- **Battery warning**: running on a PC USB port (500 mA input) the tablet draws about 0.9-1.1 A and discharges: 78 % -> 21 % over a few hours. Use a wall
  charger for long sessions, or the Wi-Fi + SSH instead of the PC cable.

## Charging on a wall adapter (2026-10-16)
- With the input limited to 500 mA (USB mode, the default of the DT) the battery discharged at about 1.06 A even on a wall adapter (tablet draws ~1.06 A:
  CPU 756 MHz, screen, Wi-Fi). Switching to the **DC mode** raises the input current (set by ISET1/ISET2, EVT2+ table): 900 mA -> -0.18 A net,
  **1400 mA -> +0.43 A net (battery charging)**, adapter stable (`DOK_N` stays low). `charge-mode usb|750|900|1400|2000|status` (initramfs `/usr/bin`)
  writes the pads directly in the TLMM registers (`GPIO_IN_OUT` at `0x801004 + 0x10 * gpio`, bit 1 = output value). The default stays USB / 500 mA
  (safe on a PC port); the source type is still not detected (the vendor kernel detects "charger" through the USB PHY: `gadget_event: source=charger mA=900`).
  A permanent policy (detection, temperature limits, low-battery shutdown) is in the backlog.

## Consumption profile and Wi-Fi throughput (2026-10-16, tablet on an adapter, DC mode 1400 mA, 21 % battery)
Method: `scripts/power-profile.sh` (battery `current_now`, positive = charging): with the adapter in the DC mode the net current drops when the
load rises. **The gauge reading lags by several seconds** (an effect shows up one measurement later): average over time and compare states.
- Idle, screen on, Wi-Fi connected (power save off): net **+0.435 A** (the tablet draws about 1 A).
- **Backlight off: net +0.95 A, i.e. the screen backlight is about 0.5 A, half of the consumption.** Control: PM8058 GPIO 25 (`BACKLIGHT_EN`, system GPIO
  697+24 = 721 in our kernel, `pm8058-gpio`) switches it (the user saw the screen go dark); the brightness is a PWM of the PM8058 LPG channel
  (`lcdc_lg_panel`, `pmic8058_pwm`, 5 V boost GPIO 102 `vdd50_boost`). Not the LM8502, which only drives the two "core navi" LEDs (D1/D2 at 0x26/0x27),
  the vibrator and the torch (I2C 0x33, enable GPIO 121, interrupt GPIO 128).
- CPU: one core at 100 %: about -35 mA, both cores: about -80 mA: **not** the main consumer (756 MHz).
- Wi-Fi power save on: about +20 mA net (small gain) but it loses incoming packets (SSH/ping), keep it off while developing.
- Remaining about 0.4 A: SoC and rails, panel, A6, Wi-Fi radio idle (to split later, ideally on battery without the adapter).
- Wi-Fi throughput (`scripts/wifi-throughput.py`, link 5 GHz at -88 dBm, MCS 1, 13 Mbit/s): PC to tablet 4.9 Mbit/s, tablet to PC 1.7 Mbit/s. Very weak
  signal: to be repeated close to the access point.
- Backlight dimming needs a driver for the PM8058 LPG PWM (mainline `leds-qcom-lpg` is SPMI only): written, see "Backlight PWM" below.

## Backlight PWM (written 2026-10-16; boots and registers, visual result to be confirmed)
Source: the vendor driver `drivers/misc/pmic8058-pwm.c` (3.4) and the panel driver `arch/arm/mach-msm/hp/tenderloin/display/lcdc_tenderloin.c`
(`pwm_request(0, "backlight")`, 20 kHz, 256 levels through `pwm_config2` = 9-bit mode, TPS61187 LED driver max 22 kHz).
- `kernel/src/drivers/pwm/pwm-pm8058-lpg.c`: mainline PWM chip (`qcom,pm8058-lpg`, 8 channels, constant duty only, LUT bypassed). SSBI registers
  LPG_CTL(0..6) at 0x13c, BANK_SEL 0x143, BANK_EN 0x144 through the regmap of the PM8058 MFD. Period = 512 * pre_div(2,3,5) * 2^m / clock (1 kHz,
  32.768 kHz, 19.2 MHz), the largest period not above the request is chosen (50 us gives about 18.75 kHz).
- Patch `0016-pwm-add-pm8058-lpg.patch` (Kconfig, Makefile); `kernel/touchpad.config`: PWM, PWM_PM8058_LPG, BACKLIGHT_CLASS_DEVICE, BACKLIGHT_PWM.
- DT: `pm8058_lpg: pwm@13c` under the PM8058 and a `pwm-backlight` node (`pwms = <&pm8058_lpg 0 50000>`, `enable-gpios = <&pm8058_gpio 25>`,
  levels 0 8 32 80 150 255 with 20 interpolated steps = 0..100, default 60).
- Image: `out/uImage-backlight`. To check after boot: `ls /sys/class/backlight/`, `dmesg | grep -i pwm`, `tp backlight 20`, then the user confirms the screen dims
  and `tp battery`/`scripts/power-profile.sh` shows the consumption drop. Result of the first boot (`out/uImage-backlight`): `/sys/class/backlight/backlight` exists (max_brightness 100, default 60), `tp backlight N` writes without error; whether the screen really dims is confirmed by the user (see below). Risks: the output could need another pin routing than channel 0 alone (the vendor board
  file does none for channel 0); the 5 V boost GPIO 102 is not managed (left on by the bootloader).

## Command-line utilities: `tp` (2026-10-16)

Direction set by the user: drive the hardware with commands. `initramfs/tp` is a single entry point (installed in `/usr/bin` by `scripts/initramfs.sh`) that wraps the helpers (`charge-mode`, `cpu-state`, `wifi-*`, `bt-*`, `hciup`) and the sysfs interfaces. From the PC: `scripts/tp.sh <args>` runs it over SSH (default IP 192.168.10.116, `-i <ip>` to change it).

| Command | What it does | Tested |
|---|---|---|
| `tp info` | kernel, battery, temperatures, CPU clocks, Wi-Fi link, IP, Bluetooth | yes |
| `tp battery` | capacity, voltage, current, temperature, status, adapter state | yes |
| `tp charge [usb\|750\|900\|1400\|2000\|status]` | input current limit (`charge-mode`) | status only |
| `tp temp` | battery and SoC temperatures, thermal trips (zones looked up by type) | yes |
| `tp cpu` | SCPLL clocks of both CPUs and L2 | yes |
| `tp backlight [on\|off\|0-100\|status]` | brightness in percent through `/sys/class/backlight/backlight` (enable GPIO fallback without the PWM driver) | yes (fixed image) |
| `tp wifi status\|scan\|connect\|wpa\|ps\|test` | Wi-Fi helpers; `wpa` takes the passphrase as an argument (never stored in the repo) | status only |
| `tp bt up\|psr\|visible\|scan\|status\|down` | CSR BC6 controller | not re-tested |
| `tp ssh` | SSH daemon and IP addresses | yes |
| `tp recovery` | reboot into the USB recovery mode (writes the Palm reason, then drops PS_HOLD; same two writes as `scripts/cycle.sh`) | not run from `tp` |

Note: thermal zone numbering is not stable (`thermal_zone0` is the battery), hence the lookup by type.
To do next: brightness (needs the PM8058 LPG PWM driver), a persistent `power_save off`, `tp` subcommands for audio and touch once those drivers exist.

## Cog in DRM mode (decision of the user, 2026-10-16)
- Target: `cog --platform=drm` straight on the KMS device (no Weston, no Wayland compositor). Buildroot already builds `COG_PLATFORM_DRM`; `buildroot/overlay/etc/init.d/S90browser`
  now starts Cog in DRM mode by default (`MESA_LOADER_DRIVER_OVERRIDE=kms_swrast`: simpledrm has no render node, so GBM/EGL go through the Mesa software rasteriser) and keeps the
  Weston + Wayland path only as a fallback (`/mnt/media/touchpad/weston`). **Untested**: the points to check on the tablet are (1) GBM/EGL on simpledrm with `kms_swrast`,
  (2) that Cog's DRM platform starts without any input device (the touch panel has no driver yet; it needs libinput/udev), (3) the display mode (1024x768 only), (4) speed at 756 MHz.
- If a Debian armhf rootfs is used for a quick test instead of waiting for the Buildroot WebKit build, its `cog` package must provide the DRM platform plugin (to check before building).

## Debian armhf rootfs for a quick Cog test (2026-10-16, prebuilt packages)
- Reason: the Buildroot WebKit build saturates the host (WSL VM up to 4.7 GB, Docker engine 500 errors, build killed twice) and takes hours. The user asked to test with prebuilt binaries in parallel.
- Debian bookworm armhf: `cog` 0.16.1 (modules `libcogplatform-drm.so`, `-wl.so`, `-headless.so`), `libwpewebkit-1.1-0` 2.38.6, `libwpebackend-fdo` 1.14.2, Mesa 22.3.6
  (`kms_swrast_dri.so`, LLVM 15). Docker Desktop runs armhf containers through qemu-user, so no debootstrap or cross build is needed.
- Build: `scripts/debian-rootfs.sh` (log `out/debian-rootfs.log`) -> `docker/debian-rootfs/Dockerfile` (apt install, no systemd, BusyBox applets linked into `/usr/local/bin`, `/sbin/init` = busybox) +
  `debian/overlay` (`etc/inittab`, `etc/rcS` running `etc/tp.d/S*`: `S01gadget` USB serial+ECM (full speed like the initramfs), `S02udev`, `S05ssh` dropbear key-only, `S10wifi` wpa_supplicant from
  `/mnt/media/touchpad/wpa_supplicant.conf` + DHCP, `S90browser` = Cog DRM mode) -> exported into `out/rootfs-debian.ext4` (363 MB of files, **599 MB image**, `mke2fs -d`).
  Root shells without password on tty0 and ttyGS0 (as the initramfs); SSH only with `secrets/touchpad_ed25519`.
- Checked in the image: init, cog + DRM module, `kms_swrast_dri.so`, `libWPEWebKit-1.1.so.0`, LLVM 15, GBM, udevd, dropbear, iw, wpa_supplicant, authorized_keys, the tp.d scripts.
- To do on the tablet: copy the image to the webOS media volume (`touchpad/rootfs-debian.ext4`), boot with `touchpad.rootfile=touchpad/rootfs-debian.ext4`, then check `/tmp/cog.log`.
  The copy needs a working link: the USB network cannot be used (the tablet's TX is dead, so TCP gets no ACK), Wi-Fi at -85 dBm gives about 0.6 MB/s (about 17 min), the serial port is a last resort.

## State at the end of the session of 2026-10-16 (resume here)
- **The tablet is off: the battery is empty.** Charge it first (wall adapter, then `tp charge 1400` once Linux runs); then recovery with the buttons (Volume Up + Power) and `scripts/cycle.sh`.
- On the webOS media partition (`/mnt/media/touchpad/`): `rootfs-debian.ext4` (599 MB, sha256 `63989231...98df13`, checked after the copy), `rootfile` (contains `touchpad/rootfs-debian.ext4`),
  `wifi-wpa` and `wifi-dhcp` (helpers, no password inside). **No Wi-Fi password is stored on the tablet or in the repository** (it was typed once in the RAM of the initramfs, `out/tablet-cmd.txt` wiped).
- `out/uImage-rootfs` = initramfs that reads `touchpad/rootfile` and switches to the Debian rootfs (`touchpad.rootfile=none` on the command line, or deleting `rootfile`, gives the initramfs shell back).
  The older images (`uImage-cpufreq`, `uImage-backlight2`) only look at the command line, so they still boot the plain initramfs. **The boot of `uImage-rootfs` was started but never observed** (battery died).
- Next test, in order: boot `out/uImage-rootfs`; if it stays in the initramfs read the `rootfs:` lines on the screen/serial; in the Debian rootfs check `/tmp/cog.log`, `ls /dev/dri`, then Wi-Fi by hand
  (`sh /mnt/media/touchpad/wifi-wpa 2sWifi_5G <passphrase>`, RAM only) and `tp`-style checks over SSH.
- Done today (all noted above): backlight PWM driver + `tp backlight`, `tp` command set, CPU frequency step 1 (432..756 MHz, cpufreq-dt, schedutil, cooling device, measured), Cog DRM-mode decision,
  Debian armhf rootfs build. Not done: thermal throttling test, cpufreq above 756 MHz (SAW voltage), Buildroot WebKit build (stopped at 2098/5867; restart with `./build.sh rootfs`, now 4 jobs, PC idle).

## Debian rootfs boot and Cog DRM: first results (2026-10-16 evening)
- **The boot of the rootfs failed first because `switch_root` was not linked in the initramfs** (`/init: exec: switch_root: not found`: ash runs applets by itself but `exec` needs a real file).
  Found with a diagnostic image (`panic=0` in the bootargs, panic left on screen, read by the user). Fix: `switch_root` added to the applet list of `scripts/initramfs.sh`.
  Method that worked: no log before `/sbin/init` -> replace `/sbin/init` by a wrapper script and make `rcS` log each step with `sync` into `/mnt/media/touchpad/boot.log` (survives a reset).
  The image currently has this wrapper as `/sbin/init` and the logging `rcS` (the overlay `debian/overlay/etc/rcS` has the logging version, the wrapper is only in the image).
- With `out/uImage-rootfs` the tablet boots Debian 12 (armv7l): `rcS` done in 9 s (udev 5 s), 946 MB RAM, 837 MB free, `/dev/dri/card0` (simpledrm) present, root shell on ttyGS0.
- **`cog --platform=drm file:///tmp/t.html` runs** (Cog 0.16.1, WPE WebKit 2.38.6, `MESA_LOADER_DRIVER_OVERRIDE=kms_swrast`): "Load started / Loaded successfully", processes `cog`, `WPENetworkProcess`, `WPEWebProcess`,
  110 MB used. Only warnings: `Renderer 'modeset' does not support rotation 0`, no accessibility bus, harmless `WRITE_ZEROES not supported` on the loop device. What is shown on the screen is to be confirmed by eye.
- `/mnt/media/touchpad/nobrowser` exists (the browser does not start by itself); delete it for the autostart of `S90browser`. Wi-Fi: new network `SRF_E070` (passphrase given by the user in chat, RAM only).

## Cog DRM test results and the graphics driver question (2026-10-16 night)
- Debian 12 stack (Cog 0.16.1, WPE 2.38.6, wpebackend-fdo 1.14.2, Mesa 22.3): `cog -P drm` falls back to its `modeset` renderer (WPE shm buffers copied to a dumb buffer). A static page works;
  the user's page (`http://192.168.1.101:8080/display.html`, "Maison - Afficheur", web fonts) loads, then Cog dies after a few seconds with SIGSEGV after the libwayland message
  `Buffer address requested when its parent pool has an external reference and a deferred resize pending` (wl_shm_buffer_get_data returns NULL while WPE resizes its pool). `-O renderer=gles` (EGL/GBM
  with `kms_swrast`) initialises but segfaults too. `WEBKIT_DISABLE_COMPOSITING_MODE=1` does not cure it reliably.
- Debian 13 stack (trixie armhf: Cog 0.18.4, WPE 2.48.3, wpebackend-fdo 1.16.0, Mesa 25.0.7), image `out/rootfs-debian.ext4` -> `rootfs-debian13.ext4` on the media partition (sha256 `5c8e320f...21d9`),
  `rootfile` points to it: boots (rcS 9 s, MIME database present). Cog 0.18 needs EGL: `Failed to initialize EGL` / `DRI2: failed to get compatible render device` on simpledrm (no render node),
  fixed by **`GBM_ALWAYS_SOFTWARE=1`** (Mesa 25); then WebKit's sandbox fails (`bwrap: Creating new namespace failed`: the kernel lacks user namespaces, to enable later with `CONFIG_USER_NS` and friends;
  for now `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1`); then the **same shm crash as with Cog 0.16** (still "Renderer 'modeset'", SIGSEGV after the pool message). So the software path through shm is fragile in both versions.
- Environment needed on the Debian 13 image so far: `GBM_ALWAYS_SOFTWARE=1 WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1` (+ `MESA_LOADER_DRIVER_OVERRIDE` is useless on Mesa 25). The `S90browser` scripts still carry the Mesa 22 settings: to update.
- **Graphics driver (question of the user: should we work on it?) - what mainline 6.12 has for this SoC:** the `msm` DRM driver has MDP4 with an LCDC encoder (`mdp4_lcdc_encoder.c`) and an a2xx GPU
  (`a2xx_gpu.c`, own GPU MMU `a2xx_gpummu.c`, so no IOMMU needed for the GPU), `msm_iommu` exists. **Missing for MSM8660:** the display/GPU clock controller (`mmcc-msm8660`: only `mmcc-msm8960` is mainline, the 8660 one has to be
  written from the vendor `clock-8x60.c`), the DT nodes (only APQ8064 has an MDP4 node), the LG panel/LCDC timings and its power sequence (supplies, GPIOs, the LPG backlight already works), MDP4 version quirks, and the Adreno 220
  entry/firmware (`yamato_pm4.fw`, `yamato_pfp.fw` from the vendor image). Mesa has freedreno a2xx but its shader support is limited (WebKit's compositor shaders are simple, still to verify).
- Proposed order: (1) KMS display with the msm driver (replaces simpledrm: modes, vblank, page flip, no more fragile shm copy); (2) GPU a220 + freedreno; meanwhile (3) keep a software path usable.

## Cog crash analysed with a core dump and the Wayland trace (2026-10-16 night)
- The user ran Cog successfully on a Raspberry Pi 3: there Cog takes the GPU path (render node, GBM/EGL, dma-buf), not the shm path we are on, so that does not say the shm path works.
- Method: `ulimit -c unlimited; echo /tmp/core.%e.%p > /proc/sys/kernel/core_pattern` (a core written to the vfat media volume stays empty, use tmpfs), copy the core with `ssh ... gzip -c`, `gdb` in the armhf container
  built from the same packages (`docker run --platform linux/arm/v7 -v out/core:/core hptouchpad-debian-armhf`, `apt-get install gdb`), and `WAYLAND_DEBUG=server` for the protocol trace.
- Cog 0.18.4 `-O renderer=gles`: SIGSEGV in the `cog` process itself, **PC = 0** called through libffi from libwayland-server (a request dispatched to a NULL handler).
  Trace just before: the web process creates a `wl_shm` pool (3 MB), one 1024x768 buffer, destroys the pool, then `attach`/`damage`/`commit`: **WebKit renders in software into wl_shm buffers**
  (no GPU path: simpledrm has no render node), and the GLES exporter of wpebackend-fdo cannot take them.
- Default (`modeset`) renderer, the one made for shm buffers: no `wl_shm_pool.resize` request appears in the trace, yet libwayland-server reports `deferred resize pending` when Cog reads the buffer
  (second use of `wl_buffer#16` after its pool was destroyed) and `wl_shm_buffer_get_data` returns NULL: SIGSEGV. Not understood; looks like a libwayland / wpebackend-fdo problem with pool reference counting
  on this 32-bit build rather than a bug in the page. Not tested: an armhf Cog patched to skip a NULL buffer, a different libwayland.
- Consequence: the software route is fragile; the reliable route is a render node with a real GPU path (see the graphics driver section).

## Graphics driver: verified references and plan (2026-10-16, before any code)
Sources read: webOS `data/webos/dmesg.txt` and probes, CM11 3.4 `arch/arm/mach-msm/hp/tenderloin/display/*` (`board-tenderloin-panel.c`, `lcdc_tenderloin*.c`), `drivers/video/msm/{lcdc.c,mdp4_overlay_lcdc.c}`,
`clock-8x60.c`, mainline 6.12 `drivers/gpu/drm/msm/disp/mdp4/*`, `mmcc-msm8960.c`; plus a read-only dump of the live registers on the tablet.
- **Panel** (`lcdc_tenderloin_xga_pt.c`): 1024x768, LCDC parallel RGB666 (18 bpp), pixel clock **96 MHz**, h: back porch 400, front porch 272, pulse 328 (total 2024), v: back porch 6, front porch 10, pulse 7 (total 791) = 60 Hz.
  The RGB lines (TLMM GPIO 0..27: pclk, hsync, vsync, den, R/G/B) feed an **external LVDS serializer** (not the on-chip LVDS PHY of the APQ8064).
- **Panel power sequence** (`lcdc_panel_power`): PM8058 L10 at 3.05 V (VDD_LVDS 3.3 V) -> (5 V boost GPIO 102 only on boards before EVT1) -> `LCD_PWR_EN` TLMM GPIO 62 = 1 -> 500 us -> `LVDS_SHDN_N` GPIO 63 = 1 -> 200 ms ->
  `BACKLIGHT_EN` PM8058 GPIO 25 = 1 -> 20 ms. Off in reverse. Backlight: PWM LPG channel 0 (done).
- **MDP**: revision 4.1 (`MDP_REV_41`), max clock 200 MHz, registers at `0x05100000`, `CONFIG_FB_MSM_MDP40` => **LCDC block at MDP+0xC0000**, overlay0 at +0x10000, DMA_P at +0x90000; MDP clock 200 MHz in webOS; bus scaling (msm_bus) in 3.4.
- **Live state left by the bootloader (read on the tablet):** MDP HW_VERSION `0x04010311`; LCDC ENABLE = 1, HSYNC_CTL `0x07E80148` (period 2024, pulse 328), VSYNC_PERIOD `0x00186DD8` (= 2024 x 791),
  DISPLAY_HCTL `0x06D702D8`, border `0xF`, underflow `0xFF`, polarity `0x3`; DMA_P buffer `0x7F600000`, stride `0x1000`, config `0x0300212A` (the simple framebuffer). MMSS clock block at `0x04000000`: MDP_CC `0x80000505`,
  MDP_MD0 `0x1FB`, MDP_NS `0x3F000008`, PIXEL_CC `0x105`, PIXEL_MD `0xFFFF`, PIXEL_NS `0xFFFFC002`, PIXEL_CC2 `0x400`, MM_PLL1 mode `0x7` (running).
  So the whole LCDC pipeline is already running with exactly the vendor timings: a driver has to take it over, not to bring it up from nothing.
- **Clocks** (3.4 `clock-8x60.c`): MMSS registers `MDP_CC 0xC0`, `MDP_MD0/1 0xC4/0xC8`, `MDP_NS 0xD0`, `PIXEL_CC 0xD4`, `PIXEL_MD 0xD8`, `PIXEL_NS 0xDC`, `PIXEL_CC2 0x120`; clocks `mdp_clk` (table 9.6..200 MHz from pll8/pll2/pxo),
  `mdp_vsync_clk`, `mdp_axi_clk`, `mdp_p_clk` (AHB), `pixel_mdp_clk` (drives the LCDC block, rate 96 MHz from pll8 1/4) and `pixel_lcdc_clk` (the LCDC interface, child). **The mainline `mmcc-msm8960` has the same MDP register layout**
  (`mdp_src` ns `0xd0`, md `0xc4/0xc8`, bank/enable `0xc0`), but no `pixel_mdp`/`pixel_lcdc` (only DSI pixel clocks) and no 8660 compatible: an 8660 variant is needed.
- **Mainline msm DRM / MDP4 gaps for this board:** (1) `mdp4_lcdc_encoder` is hard-wired to the **APQ8064 LVDS PHY and LVDS PLL** (`mpd4_lvds_pll_init`, `REG_MDP4_LVDS_PHY_CFG0`, `lvds-*` regulators): the TouchPad needs a plain parallel RGB
  encoder whose pixel clock is a normal DT clock; (2) `mdp4_kms` uses 266.7 MHz as max core clock (the TODO says other chips have 200 MHz) -> cap at 200 MHz; (3) no `qcom,mdp4` node in any 8660 DT; (4) no IOMMU for MDP on 8660 in DT
  (mainline falls back to physically contiguous memory: needs reserved memory, to check); (5) the panel has no driver: `panel-dpi`/`panel-simple` with the timings, an `enable-gpios` (LVDS_SHDN_N, GPIO 63), a supply (fixed regulator on GPIO 62 fed by PM8058 L10) and the existing `pwm-backlight`
  covers it; (6) pinctrl for GPIO 0..27 (the bootloader already muxed them).
- **Staged plan:** A) `mmcc-msm8660` (MDP, pixel, AHB/AXI clocks) with a rate check against the live registers; B) MDP4 patches (parallel LCDC encoder, 200 MHz cap) and DT (mdp4 node, panel, supplies, `simple-framebuffer` replaced by DRM);
  C) check that the KMS device comes up, scan-out works, then drop `simpledrm`; D) later GPU: Adreno 220 (a2xx) entry + firmware + freedreno. Each stage is tested with the known-good image as fallback (a wrong MDP/clock change can blank the screen
  or hang the bus: the recovery boot and the USB serial/SSH stay available because they do not need the screen).

## Charge policy and KMS test: state at the end of the session (2026-10-16 night, resume here)
- **The tablet is off again: battery empty** (it discharges at about 0.9 A on a PC port, whose input is limited to 500 mA; even with the backlight off the load stays around 0.9 A, there is no suspend or deep idle yet).
  Charge it on the wall adapter before anything else. The user asked that charging is active when plugged in next time:
- **Charge policy `initramfs/tp-powerd` (automatic detection and adjustment), tested on the PC port (2026-10-16 night):** every 5 s it detects the source with the USB device controller
  (`not attached` = wall adapter, anything else = PC) and raises the MAX8903 input current one step at a time while the source stays healthy: PC port `usb` (500 mA) -> 750 -> 900 mA,
  wall adapter 900 -> 1400 mA; a step up every 20 s of stable operation; if the input disappears or the charger reports a fault within 30 s of a step up the limit is lowered and remembered
  for that kind of source for 2 minutes. Low battery while discharging: <= 15 % backlight 5 % and lowest CPU frequency; <= 5 % `sync`, media volume read-only, `poweroff -f` (**to check that `poweroff` really cuts the
  power**: the PS_HOLD handler may only reset; a sudden cut would corrupt the webOS vfat volume and the rootfs file). State in `/tmp/tp-powerd.state`, shown by `tp power`.
- **Measured on the user's powered USB 3 PC port** (running tablet, Debian 13, backlight off, 20 % battery, 25 s per mode): `usb` 500 mA -> **-264 mA** (discharging), DC 750 mA -> **+459 mA**, DC 900 mA -> **+607 mA**; DOK stayed valid and the host
  stayed `configured`. The policy reached 900 mA by itself in 40 s (log `tp-powerd: pc: input current 750 / 900`). Not tested: the wall-adapter branch (the single USB port was on the PC) and the collapse/back-off branch.
  Caution: more than 500 mA from a USB data port is outside the USB 2.0 rule for a device that enumerates at high/full speed; it worked here, the back-off protects the port only if it browns out rather than trips.
- Installed live in the running Debian rootfs (`/usr/local/bin/{tp-powerd,charge-mode,tp}`, `/etc/tp.d/S03power`) and in the sources (`scripts/debian-rootfs.sh` copies them for the next image; the initramfs starts `tp-powerd` itself).
- **Safe image for the next boot: `out/uImage-power`** = current initramfs (with `tp-powerd`, `tp power`, the `switch_root` fix, `rootfile` support) and the kernel **without** `DRM_MSM` (everything else as `uImage-rootfs`).
  With the `rootfile` still present on the media volume it boots the Debian 13 rootfs, which **does not have `tp-powerd` yet** (the helpers are only in the initramfs and in the next `scripts/debian-rootfs.sh` build); to get the policy
  now, boot with the initramfs (rename or delete `/mnt/media/touchpad/rootfile`, or `touchpad.rootfile=none`), or rebuild and re-copy the Debian image.
- **KMS test (image `out/uImage-kms`, diagnostic `out/uImage-kms-debug` with `panic=0 loglevel=8`)**: the kernel with the msm DRM driver (patch 0018, `DRM_MSM=y`, MDP4 node, panel-dpi, fixed clocks, L10, `QCOM_OCMEM`/`QCOM_COMMAND_DB`
  disabled so that `DRM_MSM` can be built in) boots and then falls back to recovery within a minute: nothing reached USB. With the diagnostic image the user saw **only the HP logo** (the bootloader splash never replaced by the
  kernel console: the failure is before or during the framebuffer handover). Not diagnosed. Next steps: boot the known-good `out/uImage-rootfs`, then bisect: (1) the same kernel with `DRM_MSM` disabled (to prove the rest is fine),
  (2) `DRM_MSM` without the `mdp` node (probe not reached), (3) with the node but `status = "disabled"` on the panel, reading the earliest message with `earlycon`/`initcall_debug` filmed as before (`out/video*`).
  Prime suspects: the MDP4 probe resets the MDP while the bootloader's LCDC is running (bus hang), a clock/regulator access, or an exception in `msm_drm_bind` (no GPU node, no IOMMU).

## KMS display with the msm DRM driver: WORKS (2026-10-16 night)
`out/uImage-kms8` (DTB `qcom-apq8060-hp-touchpad-kms`): `[drm] Initialized msm 1.12.0 for 5100000.display-controller`, `fb0: msmdrmfb frame buffer device` (console on it), `/dev/dri/card0` **and `renderD128`**, `MDP4 version v4.1`,
LCDC enabled, `LVDS_SHDN_N` GPIO 63 = 1, backlight `bl_power=0`, `no GPU device was found` (expected, no GPU node yet). Whether the picture is right on the screen is for the user to confirm.
**Bugs found and fixed on the way (all in `kernel/patches/0018-drm-msm-mdp4-parallel-rgb-msm8660.patch`, generated by `scripts/tools/gen-patch-0018.py`):**
1. The kernel panicked in the init thread (hence the recovery fall-back and the HP logo): `msm_drv_probe()` passes `match = NULL` to `component_master_add_with_match()` when the DT has no HDMI/DSI/GPU, which dereferences it
   (`match->num`). Found with **breadcrumbs written into IMEM (`0x2A05F670`) and read back from the next good boot** (the IMEM survives the reset, `ioremap(0x2A05F000)` + `writel`; a temporary patch, now in `kernel/patches/removed/`). Fix: bring the DRM device up
   directly (`priv->no_components`, `component_bind_all/unbind_all` skipped).
2. `mdp4_lcdc_encoder` only knew the APQ8064 LVDS PHY/PLL: parallel RGB variant for `qcom,msm8660-mdp4` (plain `pixel_clk`, no PHY, no regulators); core clock limited to 200 MHz.
3. The panel driver (`panel-simple`) probes after the MDP4: the MDP4 now returns `-EPROBE_DEFER` until `of_drm_find_panel()` works.
4. The DTV/HDMI encoder asked for `hdmi_clk`/`tv_clk` and made the whole modeset init fail (`-ENOENT`): skipped when there is no HDMI.
**DT lessons:** (a) a `panel-dpi` node with nobody to enable it makes `panel-simple` switch the panel link and the backlight off: the `panel` and `mdp` nodes are `disabled` in the base DTS and enabled only by the KMS variant;
(b) a phandle reference to the `pwm-backlight` node (`backlight = <&backlight>`) makes it derive its initial power state from the PWM state (unknown, no `get_state`): it started powered down (`bl_power=4`, screen black), so the reference is only in the KMS DTS;
(c) `DRM_MSM` could only be built in after `QCOM_OCMEM=n` and `QCOM_COMMAND_DB=n` (a `depends on X || X=n` with X=m forces a module); the msm DRM options DSI/HDMI/DP/DPU/MDSS are off.
Build: `DTB_NAME=qcom-apq8060-hp-touchpad-kms ./build.sh all` (base DTB without DRM display: `./build.sh all`, image `out/uImage-safe`). Known noise: `WARNING ... mdp4_destroy` (kthread) on the deferred first attempts, to clean up; `panel-simple: Specify missing bus_format / Expected bpc` (panel-dpi without bpc).
**Next:** the clocks are still fixed clocks (a real MMSS clock controller is to write); the GPU: Adreno 220 (a2xx) entry/firmware/DT node; then Cog on `renderD128` (software `GBM_ALWAYS_SOFTWARE=1` first: the KMS device now gives real dumb/prime buffers, the shm path may no longer be needed).

## GPU Adreno 220 (a2xx): references and state (2026-10-16 night)
**References read:** 3.4 `drivers/gpu/msm/adreno.c` (A220 = chip `2.1`, firmware `leia_pm4_470.fw` / `leia_pfp_470.fw`, GMEM 512 KiB), `clock-8x60.c` (gfx3d_clk, gmem_axi_clk, gfx3d_p_clk, imem clocks),
`footswitch-8x60.c` (GFX3D_GFS_CTL at MMSS+0x188: CLAMP bit 5, ENABLE bit 8, RETENTION bit 9), webOS `/proc/iomem` (3D at `0x04300000` size 0x20000, 2D0 `0x04100000`, 2D1 `0x04200000`) and `/proc/interrupts`
(GFX3D_IRQ = GIC SPI 80, 2D0 = SPI 81, 2D1 = SPI 212); mainline 6.12 `a2xx_catalog.c` **already has the A220** (`0x02020000`, same firmware names, GMEM 512 KiB).
**Live registers (bootloader state):** GFX3D_CC (MMSS+0x80) = `0x80000000` (clock off, retain bit set), MD/NS = 0, AHB_EN `0x93FFFFFF` (gfx3d_p and imem_p on), MAXI_EN `0x17FC0001` (imem_axi on, gmem_axi bit 24 off),
**GFX3D_GFS_CTL = `0x300` = ENABLE + RETENTION, CLAMP off: the power domain is already on**, only the GPU clock is off. So no power-domain driver is needed for a first test.
**Done (built into `out/uImage-gpu1`, DTB `qcom-apq8060-hp-touchpad-kms`; NOT yet booted):**
- `kernel/patches/0020-clk-qcom-mmcc-msm8960-add-msm8660-gpu-clocks.patch` (generator `scripts/tools/gen-patch-0020.py`): `qcom,mmcc-msm8660` in `mmcc-msm8960.c`: PLL2, `gfx3d_src` (reset bits 23/22 instead of 25/24, 27..320 MHz table), `gfx3d_clk`,
  `gfx3d_ahb_clk`, GMEM AXI (exposed as `GFX3D_AXI_CLK`, MAXI_EN bit 24), IMEM AXI/AHB (copies without the 8960 hardware clock gating bits), resets `GFX3D_RESET` / `GFX3D_AHB_RESET`; PLL15 (APQ8064) is not configured on the 8660.
- DT: `mmcc` node (0x04000000), `gpu` node (`qcom,adreno-02020000`, reg `kgsl_3d0_reg_memory`, irq `kgsl_3d0_irq` SPI 80, clocks core/iface/gmem_axi/mem/mem_iface, OPP 200 MHz) disabled in the base DTS and enabled by the KMS variant;
  `vram@7a000000` (32 MiB `shared-dma-pool`) and `memory-region` on the `mdp` node (with an a2xx the GPU buffers use the GPU MMU, but the MDP4 scanout buffers, no IOMMU, must come from this physically contiguous carveout).
- Firmware: `firmware/linux-firmware/qcom/leia_pm4_470.fw` (9220 bytes) and `leia_pfp_470.fw` (1156 bytes), downloaded from linux-firmware (not committed anywhere else), copied into the initramfs (`lib/firmware/qcom`) and the Debian image by the build scripts.
- Config: `MSM_MMCC_8960=y`, `DEVFREQ_GOV_SIMPLE_ONDEMAND=y`.
**Not done:** the GPU is only probed at boot (no hardware access before the first DRM open: `adreno_load_gpu`); the first real test is opening `/dev/dri/card0` and reading `dmesg` (firmware load, a2xx hw_init, RBBM version); the 2D cores
(Z180, not supported by msm), the power domain (footswitch) when we want to save power, bus scaling.
**Incident at the end of the session:** after building `uImage-gpu1` the running system (KMS image) stopped executing serial commands (tty echo only), so the tablet could not be rebooted by software: a button reset
(Power long press, then Volume Up + Power for recovery) is needed before `scripts/cycle.sh out/uImage-gpu1`. Cause not known (possibly the battery: `tp-powerd` powers off at 5 %).

## GPU bring-up, first boots (2026-10-16 night)
- `uImage-gpu1`: the kernel boots with the GPU node: **`mdp4 ...: bound 4300000.gpu`**, `adreno 4300000.gpu: supply vdd/vddcx not found, using dummy regulator` (a2xx init OK, firmware not loaded yet: it is loaded at the first DRM open);
  GFX3D clock still off as expected (`GFX3D_CC=0x80000000`), power domain on (`GFS=0x300`).
- Bugs on the VRAM path (all with an a2xx present, where the scanout buffers need a contiguous carveout): (1) a `shared-dma-pool` is only the DMA pool of a device if the driver calls `of_reserved_mem_device_init()` (added in patch 0018);
  (2) `msm_deinit_vram()` freed with a NULL address: a declared coherent pool then never frees, every deferred probe leaked the VRAM (fixed: the address is kept in `priv->vram_cpu_addr`); (3) the MDP4 probe now defers **before** allocating anything
  when the panel is not registered yet (removes the noisy `mdp4_destroy` warnings); (4) **a `no-map` carveout cannot be used as msm VRAM**: msm builds the pages with `phys_to_page()` and a no-map region has no struct page: with it
  (`uImage-gpu3`) the screen turned blue with lines and the kernel hung (no gadget, no recovery fall-back: a button reset was needed). The VRAM now comes from the default CMA area (64 MiB, 16 MiB used), no `memory-region`.
- Debug aids that worked: IMEM breadcrumbs (`kernel/patches/removed/0019-...`), `dmesg | grep -i -E "adreno|[drm|mdp4"` after each boot, a known-good fall-back image (`out/uImage-safe`, `out/uImage-kms8`).

## End of the session of 2026-10-16 (resume here)
- Tablet left in recovery (USB `0830:8071`) after a hung GPU/KMS test. To leave it: hold Power about 15 s (full power off, the "recover" reason kept in IMEM is lost), then press Power: it boots webOS. Put it on the wall adapter (battery about 22 %).
- Next test: `scripts/cycle.sh out/uImage-gpu5` (KMS + GPU node + reusable `vram_mem` CMA area), after a recovery (buttons Volume Up + Power, or `scripts/webos-recover.sh` from webOS). Fall-backs: `out/uImage-safe` (no display driver), `out/uImage-kms8` (KMS without GPU, display OK).
- Then: open `/dev/dri/card0`/`renderD128` once (firmware load, a2xx `hw_init`, GPU clock enable: `GFX3D_CC`), `tp-powerd` is installed in the Debian rootfs, Cog needs `GBM_ALWAYS_SOFTWARE=1` until the GPU path works.

## GPU Adreno 220: ALIVE (2026-10-17)
`out/uImage-gpu8` (DTB `qcom-apq8060-hp-touchpad-kms`, patches 0018, 0020, 0021): the a2xx initialises on the real hardware. `/sys/kernel/debug/dri/5100000.display-controller/gpu`: **`revision: 220 (2.2.0.0)`**, ring buffer
`iova 0x01001000`, **`rptr: 21 = wptr: 21`** (the command processor consumed the ME init packet: microcode `leia_pm4_470.fw`/`leia_pfp_470.fw` loaded and running), `rbbm-status 0x110` (idle), GPU registers readable.
`/dev/dri/card1` + `renderD128`, console on `msmdrmfb`, `GFX3D_CC = 0x80000505` (clock running).
**Root cause of the earlier hang found with IMEM breadcrumbs** (generator `scripts/tools/gen-diag-crumbs.py`, output in `kernel/patches/diag/`, applied only for one build; two words: `0x2A05F670` = GPU path, `0x2A05F674` = rest of msm):
the fbdev client opens the DRM device at boot, which calls `adreno_load_gpu()` (firmware, runtime PM resume: GFX3D clock to 200 MHz through the new mmcc, `a2xx_hw_init`, ME init), then the runtime autosuspend (66 ms later)
runs `msm_gpu_pm_suspend()` (AXI, clocks, rail) and **the system locks up right after** (the screen shows garbage lines, the CPU stops, a watchdog reset follows). Disabling the same clocks (`MAXI_EN` bits 22/24, `AHB_EN` bits 3/6) by hand on an image
without the GPU driver does NOT hang, so it is the gating of a GPU that has been initialised (CP running, MH/MMU/arbiter configured) that wedges the bus, not the clock registers themselves. The vendor kgsl idles the core and halts the GPU port of the fabric
(`msm_bus_axi_porthalt`) before it gates anything.
**Workaround in place: patch 0021** (`scripts/tools/gen-patch-0021.py`): `a2xx_pm_suspend()` does nothing, power/clocks/bus stay on (costs the idle power of the GPU). To do properly: wait idle + halt the CP + MMU off + soft reset, and/or the bus port halt (`msm_bus` for the 8660), then re-enable the gating.
Tooling lessons: `scripts/kernel.sh` now also restores the files touched by `kernel/patches/diag/*.patch`; after changing the set of patches delete `/build/linux-6.12.40/.touchpad-patches` if a stale tree is suspected (a failed build leaves the previous `out/uImage`: check `BUILD_EXIT` and the timestamp before booting a copy).
**Next:** Mesa/freedreno on `renderD128` (Debian 13 Mesa 25 has the msm driver, a2xx via NIR/ir2), Cog `-O renderer=gles` without `GBM_ALWAYS_SOFTWARE`; real power management of the GPU; clock controller for the display (MDP/pixel) instead of the fixed clocks.

## Mesa freedreno a2xx: Debian 13 (Mesa 25.0.7) crashes, Debian 12 (Mesa 22.3.6) works (2026-10-17)
- **Debian 13 / Mesa 25.0.7**: any EGL/GLES context on `renderD128` (`eglinfo -p gbm`, Cog) segfaults in the a2xx shader compiler: `update_range (ctx, reg)` at
  `src/gallium/drivers/freedreno/a2xx/ir2_nir.c:196`, called from `make_legacy_src` (ir2_nir.c:245) <- `emit_alu` (ir2_nir.c:445). The `reg` pointer is NULL/invalid
  (the ir2 backend does not cope with the NIR legacy register handling of Mesa 25). Backtrace: `out/core/bt-gallium.txt`. `MESA_LOADER_DRIVER_OVERRIDE=swrast/msm/kgsl` all crash.
- **Debian 12 / Mesa 22.3.6** (`rootfs-debian.ext4`, selected through `touchpad/rootfile`): Cog 0.16.1 (WPE WebKit 2.38.6) with `-P drm -O renderer=gles` runs on
  `msm_dri.so` (`libEGL debug: using driver msm`, the WPE process holds `renderD128`), no `GBM_ALWAYS_SOFTWARE`, no crash over 90 s; the test page loads. Needs
  `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1` and `WEBKIT_DISABLE_COMPOSITING_MODE=1`.
- Seen while Cog runs: `mdp4_irq_error_handler errors: 00000100` (MDP4 underrun bursts, to analyse: AXI clock/bus bandwidth, patch 0021 keeps the GPU on).
- To check next: what is visible on the screen (user), frame rate, touch-less page navigation; Debian 13 option: patch/backport the ir2 fix or use the Mesa 22.3 libraries.

### Display test results with Debian 12 / Mesa 22.3 (2026-10-17, photos from the user)
- Kernel scanout is fine: `modetest -M msm -s 43@38:1024x768` (CPU-written dumb buffer, SMPTE bars) is straight with the right colours (the panel is seen rotated by 90 degrees when the tablet is held in portrait). DRM state during Cog: XR24, 1024x768, pitch 4096, same as fbcon.
- **Cog (`-P drm -O renderer=gles`) on the GPU: the page is sheared** (squares become parallelograms, a 4 px frame becomes a thin diagonal line, about 1.4 px of shift per line, i.e. NOT a whole-pixel pitch error). Same kind of shear with `GBM_ALWAYS_SOFTWARE=1` (opposite direction), with and without `WEBKIT_DISABLE_COMPOSITING_MODE`. `-O renderer=modeset` shows nothing (`failed to schedule a page flip: Invalid argument`, plus `WARN !aspace` in `msm_gem_vma_init`).
- `kmscube` (Debian 12 armhf, sent with the new `scripts/tablet-put.sh`) renders on `FD220` with Mesa 22.3.6: the cube spins and the texture mode (`-M rgba`) shows the checkerboard, but the background is white instead of grey and only one face is coloured, also with `GBM_ALWAYS_SOFTWARE=1 MESA_LOADER_DRIVER_OVERRIDE=swrast`. So that colour/clear defect is not specific to the a2xx.
- Tool pitfalls: `modetest` and `kmscube` exit at once when stdin is not blocking (feed them `sleep N |`); `busybox devmem` on MDP4 pipe registers (VG pipes at `0x05120000`/`0x05124000`) **hung the whole tablet** (no watchdog fall-back: hard power cycle needed): do not read the unused pipes; BusyBox lines are limited to about 1000 characters; Git Bash rewrites `/tmp/x` arguments (use `//tmp/x`); the usbipd attach drops now and then (re-attach with `usbipd attach --wsl --busid 1-2`).
- **Corrected diagnosis (same day, measured, not guessed):**
  - The scanout path is innocent: reading the MDP4 RGB1 pipe (`0x05140000`: `+0x10` base, `+0x40` stride 4096, `+0x50` format, `+0x08` dst size 1024x768; registers identical to the fbcon ones, only the base differs) and the buffer through `/dev/mem` (`busybox devmem`, one word at a time: `dd` reads nothing) shows **the distortion is already in the pixels written by the GPU**. A cross written by hand into the displayed buffer is perfectly straight on the screen.
  - The "software rendering" tests were **not** software: `GBM_ALWAYS_SOFTWARE=1`, `LIBGL_ALWAYS_SOFTWARE=1`, `MESA_LOADER_DRIVER_OVERRIDE=swrast` all still report `renderer: FD220` with a render node. So everything seen so far is the Adreno path. (A real llvmpipe test is still to be done, e.g. with another EGL platform.)
  - Cog 0.16.1 draws (platform/common/cog-gl-utils.c) one VBO (positions then, at byte offset `(rotation+1)*32`, the texture coordinates, stride 0) with `glDrawArrays(GL_TRIANGLE_STRIP, 0, 4)`; kmscube also uses one VBO with offsets.
  - `scripts/tools/vbotest.c` (draws that quad into an FBO with six ways of feeding the attributes and compares `glReadPixels` with the analytic result): on the FD220 **every run shows a diagonal seam** (about 120 of 4096 pixels along the shared edge of the two triangles keep the clear colour) and **non-deterministically, from one draw to the next inside the same process, the whole image is mirrored in x** (3661 bad pixels), independently of the way the attributes are fed (VBO offset, two VBOs, client arrays, interleaved). So the vertex data is read correctly and the same compiled shader gives different results: a GPU state / command stream problem (stale or dropped register writes, cache coherency of the command stream or of the vertex fetch, rasteriser set-up), not a compiler bug.
  - Negative results: `scripts/tools/fptest.c` (float, NEON f32/u8: 18 families) gives bit-identical results on the tablet and under QEMU; `scripts/tools/memtest.c` (glibc memcpy/memmove/memset/memchr/strlen, all sizes and alignments) has 0 errors; `/proc/cpu/alignment` shows 0 faults; `PIXMAN_DISABLE=arm-neon` changes nothing. The CPU, the libc and the alignment handling are fine. The missing text in Cog (the buffer has no glyphs at the expected place) is a separate, still open symptom (WebKit software painting).
  - Candidate causes to examine next (kernel side, in `a2xx_gpu.c`/`a2xx_gpummu.c`/`msm_ringbuffer`): command-stream and ring-buffer coherency (WC mappings, barriers before the write pointer is updated), `MH`/`RBBM` set-up that downstream kgsl does for the a220 (`MH_ARBITER_CONFIG`, `MH_CLNT_AXI_ID_REUSE`, clock gating overrides, `RBBM_PM_OVERRIDE`), `CP_INVALIDATE_STATE` between submits, and the GPU clock being a fixed 200 MHz. Comparison point: CM 3.4 `kgsl` `adreno_a2xx.c` and the webOS `/etc` settings.
  - Tools added: `scripts/tablet-put.sh` (send a small file over the serial shell: paced lines of at most 600 characters because of the BusyBox line editor; use `//tmp/x` as remote path from Git Bash) and `scripts/tablet-put-inner.sh`; test sources `scripts/tools/{fptest,memtest,vbotest}.c` (built in a `debian:bookworm-slim` armhf container under QEMU, `--platform linux/arm/v7`).

### GPU start-up aligned with the downstream kgsl: Cog displays correctly (2026-10-17)
- Comparison of CM 3.4 `drivers/gpu/msm/adreno_a2xx.c` (`a2xx_start`, `a2xx_rb_init`), `adreno.c` (`ADRENO_CFG_MHARB`, `kgsl_mh_start`), `adreno_ringbuffer.c` (`CP_DEBUG_DEFAULT`) with the mainline `a2xx_hw_init()`: for the A22x kgsl writes `RBBM_PM_OVERRIDE2 = 0x80` (mainline 0), `CP_DEBUG = DYNAMIC_CLK_DISABLE | MIU_128BIT_WRITE_ENABLE` (`(1<<27)|(1<<25)`, mainline only bit 25) and leaves `MH_ARBITER_CONFIG.IN_FLIGHT_LIMIT_ENABLE` at 0 (mainline 1). Other differences noted, not applied yet: kgsl runs the GPU at **266.7 MHz** with a bus vote (`grp3d_bus_scale`, `ib` 990-2484 MB/s from GRAPHICS_3D to EBI_CH0 per power level; level 0 = 266 MHz / vote 2484, level 2 = 200 MHz / vote 1300... see `arch/arm/mach-msm/devices-msm8x60.c`: mainline has no interconnect driver for the MSM8660), WPTR polling (`CP_RB_WPTR_BASE`, `rb_poll_en`) instead of register writes, MH set up with the MMU off + idle before the MMU is enabled.
- **Patch 0022** (`scripts/tools/gen-patch-0022.py`: `PM_OVERRIDE2 = 0x80` on A22x, `CP_DEBUG |= DYNAMIC_CLK_DISABLE`, no `IN_FLIGHT_LIMIT_ENABLE`), image `out/uImage-gpu9` (`DTB_NAME=qcom-apq8060-hp-touchpad-kms ./build.sh all`). Result with Cog (`-P drm -O renderer=gles`, Debian 12 / Mesa 22.3.6, `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1 WEBKIT_DISABLE_COMPOSITING_MODE=1`): **the page is correct on the screen** (red and blue squares straight and in place, text drawn, no diagonal line; user photo, 13 min after boot).
- The three changes were NOT bisected: it is not known which one fixes it. `vbotest` after the patch: 72 draws without any mirrored image (before: a mirrored image in 25 % of the draws in short series, period 16 draws, always in blocks of 4), but the mirrored image did show up in the first minutes after boot on `uImage-gpu9` too (6 of 12 runs right after the boot, none later): to re-check on a fresh boot before calling it fixed. Remaining defect in `vbotest`: the last column (x = 63) of the 64x64 FBO keeps the clear colour (120 pixels counted as bad, among them the whole right column): off-by-one in the coverage of the right (and maybe bottom) edge, not seen on the Cog photo.
- **Mistake in the earlier test pages (not a GPU defect):** `style=...;font:bold 80px sans-serif>` and `border:4px solid black` in an unquoted attribute stop at the first space, so the frame never existed and the text was small. Quote the attribute (`style="..."`) in the next data: URLs.
- Tooling: `scripts/cycle.sh` now sends `busybox devmem` (the Debian shell has no `devmem`); `scripts/tablet-put.sh`; `scripts/tools/vbotest.c -n N mode` prints the sequence of good/mirrored draws.
- ~~Next~~ superseded by the section below: the "fix" seen on the Cog photo was luck (position in a deterministic cycle).

### FD220 wrong output: verified facts and eliminated causes (2026-10-17, evening)
**Verified (measured, reproducible):**
- `vbotest` is a correct test: with llvmpipe it reports 0 wrong pixels in every mode and every run, in the armhf container AND on the tablet
  (`LIBGL_ALWAYS_SOFTWARE=1 vbotest surfaceless`). So the CPU, glibc, Mesa's GL front-end and the test itself are fine; the defect is strictly in the FD220 path.
  NOTE: with a render node present, `GBM_ALWAYS_SOFTWARE`, `LIBGL_ALWAYS_SOFTWARE` (gbm platform), `GALLIUM_DRIVER=llvmpipe` and renaming `msm_dri.so`
  (all `*_dri.so` are hard links of one 16 MB megadriver; `kgsl` is tried after `msm`) do NOT select software rendering: only the surfaceless platform +
  `LIBGL_ALWAYS_SOFTWARE=1` does. The earlier "software" tests were all on the GPU.
- On the FD220 the result of a draw follows a fixed cycle: the same 8-run pattern (48 draws, wrong values 3841/4073/4077, right values 120 = only the last
  column x=63 missing) restarts identically after every reboot, independent of time, delays, Mesa debug flags, the Mesa shader cache, ASLR, CPU1 on/off.
- **Identical inputs, different outputs:** the diagnostic kernel (`kernel/patches/diag/0097-diag-a2xx-submit-trace.patch`, `scripts/tools/gen-diag-submit-trace.py`:
  CRC32 of every command stream and every buffer of each submit) shows that for a given draw the command stream and all input buffers (vertices, shaders,
  state) are byte-identical in the right and in the wrong cases; only the render target differs (it holds the previous result). The GPU executes the same
  commands differently: some GPU internal state survives from one draw/process to the next and is only cleared by a full reboot.
- The wrong images are exact functions of (u, v) or of the vertex positions (e.g. R = u, G = 1 - u - v; G = (y - x)/2 of the positions): the shader or the
  vertex fetch set-up that runs is not the one in the command stream, as if registers/constants/instruction memory kept stale content.
- The GPU virtual addresses of the buffers are identical in every run; their physical pages change; no address bit separates right and wrong draws.
**Eliminated (each with a kernel or a test, the cycle stayed bit-identical):** CPU cache maintenance on cpu_prep and on submit (patch 0023, now in
`kernel/patches/removed/`); no `CP_SCRATCH_REG2 = seqno` write; GPU MMU TLB flush from the command stream like kgsl (`CP_WAIT_FOR_IDLE`, `MH_MMU_INVALIDATE`
inside `CP_SET_PROTECTED_MODE 0/1`, `CP_INVALIDATE_STATE 0x7fff`; without leaving the protected mode every submit hangs the GPU) (patch 0024, removed);
Scorpion MP `NMRR = 0x40e080e0` (inner write-through, as downstream proc-v7-2level.S) (patch 0025, still applied, no effect); RPM bus clocks
(`mmfab`, `sfab`, `afab`, `ebi1`, enabled through the GPU node, they had enable count 0 before) (DTS, still applied, no effect); GFX3D core reset through
`SW_RESET_CORE` bit 12 (0x04000210) by hand; delay before the read-back; the downstream strongly ordered barrier is not used on the 8x60 (only MSM7x27).
**Downstream facts found on the way (not yet reproduced):** kgsl saves and restores the whole GPU state per context (gpustate shadow, `a2xx_drawctxt_*`);
the downstream clock init sets `GFX3D_CC` FORCE_CORE_ON/retention (bit 31, we already have `0x80000505`) and resets the 3D core while clocked at
27 MHz at boot (`msm8660_clock_post_init`); the footswitch enable sequence asserts all clock resets, powers the rail (`GFX3D_GFS_CTL` 0x04000188,
ENABLE bit 8, CLAMP bit 5), releases the resets, toggles the core reset again ("required for GFX3D") and sets memory retention; kgsl runs the GPU
at 266.7 MHz with a 2484 MB/s bus vote. postmarketOS has no mainline nor GPU support for this tablet (downstream 3.4 only); Herman van Hazendonk is
upstreaming MSM8x60 MMCC, NoC interconnect (fabric floor 384 MHz, "266 MHz insufficient"), MPM and USB patches for the TouchPad (LKML, 2026-05/06).
**Method from now on (differential, measured):** dump the GPU register state after a right and after a wrong draw and diff it; then the same dump on
webOS (kgsl, GPU in use by its UI) and diff with ours; only then reproduce the differences one by one.

### Register dump webOS vs mainline (2026-10-17, `scripts/tools/gpuregdump.c`, results in `data/webos/gpuregs/`)
- Tool: static armhf, reads `/dev/mem` (read-only): MMSS clock block 0x04000000 (0x000-0x2fc) and the 390 A220 registers of the kgsl snapshot list
  (`a220_registers[]`, adreno_a2xx.c). It reads the GPU only when the rail, the core clock, the AHB interface clock (`AHB_EN` bit 3) and the GMEM/IMEM AXI
  clocks (`MAXI_EN` bits 24/22) are on before AND after the read, and rejects uniform reads (an unclocked read returns `0x110` everywhere).
  webOS gates the GPU between frames: the user has to keep the UI busy. Run: `novacom put file:///tmp/gpuregdump`, `novacom run file:///tmp/gpuregdump 60000`.
- webOS idle: `GFX3D_GFS_CTL = 0x300` (rail on, retention on), `GFX3D_CC = 0x80ff0400`; active `0x80ff0505`. 544 registers stable over 3 dumps.
- Configuration differences (webOS stable / mainline): every MM `*_CC` register has the memory sleep/wake-up delays `[23:16] = 0xFF` on webOS and `0x00` with
  mainline (downstream clock init: "set sleep and wake-up delays to safe values"); `GFX3D_NS/MD0` `0x00340008/0x1c` vs `0x00300008/0x1b` (other rate);
  `MH_MMU_CONFIG` `0x02aaaaa1` (client behaviour 2 = translate + fault) vs `0x01555551` (1); `CP_DEBUG` `0x02000000` vs `0x0a000000` (bit 27 added by patch 0022:
  webOS does NOT set it); `RBBM_PM_OVERRIDE1/2` read back `0x0/0x3a0` vs `0xffffffff/0x1fff`; `CP_INT_CNTL` `0xef880000` vs `0xcf800000`; `MAXI_EN`, `AHB_EN2` differ
  (other blocks). The 0x2000+ context registers differ as expected (different last draw).
- **The GPU internal state survives a reboot:** after a boot coming from webOS the very first vbotest run gives NEW wrong values (4004, 3770, 3999, 4041, 3733,
  4033, 3661) instead of the usual first "all right" run; the cycle is still 16 draws long with 10 wrong and 6 right. Setting the `GFX3D_CC` delays to 0xFF at
  run time does not change it.
- Hypothesis now (from the kgsl A20x/A220 `a2xx_drawctxt_draw_workaround`: "if the events for shader space reuse gets dropped, the CP block would wait
  indefinitely; sending CP_SET_SHADER_BASES unblocks it"): the CP microcode manages the shader instruction store by rotation and some of our shader loads do
  not take effect, so a draw runs a shader left in the instruction memory by an earlier draw (or by webOS). Test: `CP_SET_SHADER_BASES 0x80000180` per submit.
  kgsl uses the same A220 parameters as mainline (leia_pm4_470/leia_pfp_470, istore 512, pixel shader start 384, GMEM 512 KiB); the webOS microcode files are
  still to be compared with ours (md5 `dbc0f42b...` pm4, `73697be0...` pfp).

### Cog displays correctly in software rendering (2026-10-25)
- **Working combination (user photo: squares straight and in place, text sharp):** Debian 12 rootfs, `uImage-gpu22`,
  `MESA_LOADER_DRIVER_OVERRIDE=kms_swrast` (alone), `WEBKIT_DISABLE_COMPOSITING_MODE=1`, `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1`,
  `cog --platform=drm --platform-params=renderer=modeset <url>`. Now in `debian/overlay/etc/tp.d/S90browser` (installed on the tablet; the
  `nobrowser` flag on the media partition was renamed `nobrowser.off` and `touchpad/url` = `file:///root/test.html`, so the browser starts at boot).
- Why the GLES renderer crashed: core dump analysed off-line (`out/core/core.cog.kms_swrast`, gdb in a `debian:bookworm-slim` armhf container with the same
  packages and debuginfod; gdb on the tablet itself is killed by the OOM killer at 900 MB): SIGSEGV at address 0 from `wl_closure_invoke` in the private
  Wayland server of wpebackend-fdo (`ws.cpp`), request **`wl_surface.commit`**: with llvmpipe the web process sends shared-memory frames and the GLES
  renderer of Cog 0.16 only handles EGLImage frames. The modeset renderer copies shared-memory frames into a KMS dumb buffer (VRAM, scanout possible
  without IOMMU); with the GPU driver it had failed with "page flip: Invalid argument" (GPU buffers outside VRAM cannot be scanned out by the MDP4).
- `shared-mime-info` was missing in the Debian 12 rootfs: `file://*.html` was shown as plain text. Installed with apt (after freeing space: the 573 MB
  image was full; gdb removed again). The trixie Dockerfile already lists it; the next rootfs image must be bigger (gdb + debug tools do not fit).
- Wayland is NOT used for display: wpebackend-fdo uses the Wayland protocol only as private IPC between WPEWebProcess and Cog; display is DRM/KMS.
- Still to check: the 6 px black frame of the test page is not visible on the photo (screen edges or coverage); speed of llvmpipe on real pages;
  the user's page `http://192.168.1.101:8080/display.html`; touch input.
- Docker Desktop was not running after a PC reboot: start it (nothing running, nothing lost) before `webos-recover.sh` / builds.

### The user's page displays correctly: Cog modeset renderer bug fixed (2026-10-25)
- **User confirmed:** `http://192.168.1.101:8080/display.html` displays correctly and keeps updating (Debian 12 rootfs, `uImage-gpu22`, S90browser as above).
- **Root cause (measured on core dumps, not guessed):** Cog's DRM modeset renderer (0.16.1 and 0.18.4, not fixed upstream as of the GitHub history)
  calls `wl_resource_set_user_data(buffer_resource, self)` on the client's wl_buffer when it first sees it. For a `wl_shm` buffer that user data IS the
  `struct wl_shm_buffer` of libwayland, so the next `wl_shm_buffer_get()` on the same buffer (the web process reuses its buffers as soon as a page updates)
  returns the renderer: garbage width/stride/pool, libwayland prints the misleading "Buffer address requested when its parent pool has an external reference
  and a deferred resize pending" (garbage pool counters) and the copy segfaults. Static pages send one frame only, so they worked. On a Raspberry Pi the GPU
  path (dmabuf) never goes through this code.
- **Fix:** `debian/cog-patches/0001-drm-modeset-do-not-overwrite-wl_buffer-user-data.patch` (13 lines: keep the renderer pointer in Cog's own
  `buffer_object`). Applies to 0.16.1 and 0.18.4. Build: `scripts/build-cog.sh [bookworm|trixie]` (Debian source package in an armhf container, about 10 min)
  -> `out/cog/<suite>/libcogplatform-drm.so`; installed on the tablet over `/usr/lib/arm-linux-gnueabihf/cog/modules/libcogplatform-drm.so` (original kept as
  `.orig`). TO DO: install it in `scripts/debian-rootfs.sh` so a rebuilt rootfs has it; worth sending upstream (Igalia/cog).
- Two wrong fixes on the way (kept for the record, not applied): early release of shm buffers + NULL check in the copy (the pointer was never NULL),
  and a wpebackend-fdo patch not exporting on a commit without attach (`debian/wpebackend-fdo-patches/unused/`, `scripts/build-wpebackend-fdo.sh`).
  Lesson: read the actual values in the core (`x/2wx` of the exported buffer showed `shm_buffer == self`) before writing a fix.
- Debian 13 (Cog 0.18.4, WebKit 2.48) has the same bug; a patched trixie module can be built the same way (newer WebKit for modern pages).
- Core dump analysis recipe: `echo /tmp/core.%e.%p > /proc/sys/kernel/core_pattern; ulimit -c unlimited` on the tablet, stream the core with
  `ssh ... 'cat /tmp/core...' > out/core/...` (Dropbear has no sftp: `scp` fails), then gdb in a `debian:bookworm-slim` armhf container with the same packages,
  the patched files copied in, and `DEBUGINFOD_URLS=https://debuginfod.debian.net`.
- Notes: `busybox wget` segfaults on this rootfs (not investigated); `fb0: Framebuffer is not in virtual address space` at boot is harmless (msm fbdev
  emulation in VRAM without kernel mapping, something opens /dev/fb0).

### Buttons: power, volume up/down, home WORK (2026-10-26)
- Downstream (`board-tenderloin-keypad.c`, `board-tenderloin.h`, Wi-Fi models): volume up **GPIO 103**, volume down **GPIO 104**, home ("core navi")
  **GPIO 40** (`KEY_HOMEPAGE`), inputs with pull-up, active low (3G models: volume keys on PM8058 GPIOs 6/7). Power key: PM8058 pwrkey.
- Mainline: `gpio-keys` node + `gpio_keys_pins` pinctrl state in our DTS (debounce 15 ms, home is a wakeup source); the `pwrkey@1c` node already in
  `pm8058.dtsi`, only `CONFIG_INPUT_PMIC8XXX_PWRKEY=y` was missing (added to `kernel/touchpad.config`, with `CONFIG_KEYBOARD_GPIO=y`). Image
  `out/uImage-keys` (KMS dtb).
- Checked: `pmic8xxx_pwrkey` = event0, `gpio-keys` = event1 (the touch screen moves to event2); interrupts counted for all four keys and the captured
  events are right (`KEY_POWER`, `KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`, `KEY_HOMEPAGE`, down then up). Two isolated volume-up events were seen (an "up" right
  after the power key, a final "down" without "up"): accidental contact or a bounce, to watch.
- Nothing acts on the keys yet (no handler for KEY_POWER: pressing it does not power off). Next: automatic screen blanking with wake on power key/touch.

### Cleanup of 2026-10-27: Debian and Buildroot removed, patches renumbered
- **Removed from the repository** (all still in git history): the Debian rootfs (`docker/debian-rootfs/`, `scripts/debian-rootfs.sh`), `scripts/build-cog.sh`
  (Debian Cog), `scripts/build-wpebackend-fdo.sh` + its unused patch, `scripts/build-mesa-old.sh`, the Buildroot external tree `buildroot/` and
  `scripts/rootfs.sh`, the `rootfs` target of `scripts/build.sh`. `debian/` was renamed **`rootfs/`** (`overlay/`, `ts_srv/`, `cog-patches/`: all that goes on
  top of Alpine). `scripts/build-ts-srv.sh` now builds `ts_srv` with musl in an `alpine:3.21` container (static, 357 KB instead of 560 KB glibc).
  The sections of this file about Debian (rootfs, Mesa 22.3/25 tests, `touchpad/rootfs-debian*.ext4`) are history; the commands in them no longer exist.
  Still Debian: the build image `hptouchpad-builder` (`docker/Dockerfile`, `FROM debian:bookworm`: cross toolchain) and the initramfs, which takes
  `busybox-static`, `iw`, `bluez` and `libnl` from Debian armhf packages (`scripts/initramfs.sh`).
- **Deleted**: PC `out/`: the Debian rootfs image (783 MB), 20 intermediate kernels (`uImage-*`, `uImage.multi`; kept `uImage-led` and `uImage-ref34`),
  `out/cog/{bookworm,trixie}`, `out/mesa-bullseye`, `out/wpebackend-fdo`...; Docker volume `hptouchpad-build`: the abandoned Buildroot WebKit build
  (`obj-buildroot` 15.8 GB, sources 0.75 GB) and the extracted Debian root (0.5 GB), **17 GB freed**; 13 Docker images (Debian/Alpine/gcc bases pulled for
  tests); the tablet media partition: `rootfs-debian.ext4`, `rootfs-debian13.ext4`, `rootfile.debian` (**1.4 GB freed**, 790 MB used).
  **Second pass the same day** (`out/` 1.2 GB -> 0.7 GB, of which 592 MB is the Alpine image): deleted the Cog core dumps (`out/core`, 281 MB, root cause
  documented), the test video frames, TWRP, the raw touch captures, the partial git clones of Jonathan Marek's kernel/Mesa and of the Evervolv/flintman device
  trees (the useful parts are kept: his two kernel patches and the a2xx 5.8->6.12 diff in `out/refs/`, `out/refs/{pmos,evervolv,cm-cam,cm-led}`), scratch scripts,
  logs and traces. Unique measurements were moved to `data/`: `data/webos-touch/` (the controller state before the recalibration, the recalibration and the reflash
  logs) and `data/webos/gpuregs/` (all nine webOS GPU register dumps), `data/webos/lvm-metadata.txt` (LVM layout of the webOS data partition).
  Kept in `out/`: the build outputs (`uImage`, `uImage-clean`, `uImage-ref34`, `zImage*`, `initramfs*.cpio.gz`, dtb, `rootfs-alpine.ext4`, `cog/`, `ts_srv/`, `tools/`),
  `out/mesa` and `out/cm-tenderloin` (reference sources), and **`out/tokens-p12.gz`: a backup of the webOS "tokens" partition (device identity, TOC1 envelopes):
  keep it, it cannot be regenerated without reading the partition again, and never commit it**.
- **Patches renumbered** 0001..0019 (table in "Kernel patches"), proved identical by `scripts/check-patch-series.sh`.

### Home button LEDs: LM8502 driver WORKS (2026-10-27)
- **Hardware** (CM `board-tenderloin.c`, `leds-lm8502.c`): the two white "core navi" LEDs on both sides of the Home button are outputs D1/D2 of an
  **LM8502** lighting driver (10 LED outputs, haptic H-bridge, flash/torch) on the **GSBI8 I2C at 0x33** (the same bus as the A6 battery gauge at 0x31,
  `i2c-0`), **enable GPIO 121** (vendor gpiomux output low, 8 mA, no pull), interrupt GPIO 128 (not used). Register map close to the LP5523 family
  (control 0x06+n, brightness/current 0x26+n, `ENGINE_CNTRL1` 0x00 with CHIP_EN 0x40, `MISC` 0x36, `RESET` 0x3D).
- **Findings (measured from userspace with `i2cset`/`i2cget`, GPIO 121 through sysfs = 512 + 121):** with enable high the chip acknowledges its address
  (`i2cdetect` shows 0x33) and **plain register writes always work** (8/8 consecutive, 8/8 right after power-up); but **the write of the RESET register
  and the first transfer after it are not acknowledged** (-ENXIO, about every other attempt: the chip resets itself while receiving the byte), and
  **every register read returns 0xff** (also with repeated start, with or without a stop). The vendor driver ignores the return value of its reset
  write and its read-modify-write sequences worked on 0xff, so its init effectively wrote 0xff-based values. Our driver only writes, with clean values.
- **Driver:** `kernel/src/drivers/leds/leds-lm8502.c` (+ patch 0031 Kconfig/Makefile, `CONFIG_LEDS_LM8502=y`, generator `scripts/tools/gen-patch-0031.py`):
  enable GPIO high, reset (result ignored), 50 ms, then `CHIP_EN`, `MISC` = power save | boost | PWM input (0x2a) with up to 5 attempts (the first transfer
  after the reset is refused), per LED: control register = full-scale current setting 2 (vendor board value) and current register = brightness. DT: node
  `led-controller@33` `ti,lm8502` under `&gsbi8_i2c`, children `led@0`/`led@1` with `label = "core_navi_left"` / `"core_navi_right"` (the CM names), pins
  `lm8502_pins`. **User confirmed: both LEDs light and the brightness is adjustable** (`echo 40 > /sys/class/leds/core_navi_left/brightness`, 0-255).
  Image `out/uImage-led`.
- Not done: the vibrator (haptic output, vendor `timed_output` + sysfs) and the torch/flash of the same chip; the interrupt line; a trigger so that the
  LEDs follow the screen (e.g. `linux,default-trigger = "backlight"`) or the future `tp` daemon (Home key toggles the screen).

### Camera: hardware path and plan (2026-10-26, read-only analysis)
- **Hardware** (CM 3.4 `board-tenderloin.c`, webOS config `CONFIG_WEBCAM_MT9M113=y`, `msm_sync_init: initialized mt9m113` in the webOS dmesg): front
  camera Aptina **MT9M113** (1.3 MP SoC sensor with its own ISP, YUV output), control on **GSBI4 I2C** (GPIO 47 SDA / 48 SCL), address 0x3C (0x78 8-bit),
  model id 0x2480 at register 0x0000; **MCLK 24 MHz** on GPIO 32 (CAMIF function); reset GPIO 106, power-down GPIO 107 (both output high when active);
  supplies **PM8058 LVS0** (1.8 V) and **L11** (2.85 V); data on **MIPI CSI-2 into CSI1** (`csiphy` 0x04900000, `CSI_1_IRQ`) then **VFE 3.1**
  (0x04500000, `VFE_IRQ`, vfe clock 228.57 MHz). Downstream code: `drivers/media/video/msm/mt9m113.c` (901 lines) + `mt9m113_reg.c` (559),
  `msm_vfe31.c` (3189), `msm_io_vfe31.c` (910), `msm_io_8x60.c` (1024), `csi/msm_ispif.c` (949).
- **Mainline 6.12:** no `mt9m113` driver (`mt9m114` exists, same Aptina family, different registers); **no CSI/VFE pipeline for the MSM8660**:
  `camss` starts at the MSM8916 (VFE 4.x); GSBI4 is not in `qcom-msm8660.dtsi`; PM8058 L11/LVS0 not described in our DT.
- **Step 1 DONE (2026-10-26): the sensor answers, `mt9m113 2-003c: model id 0x2480, clock 24000000 Hz`** (image `out/uImage-cam1`).
  - DT: `gsbi4` (0x16300000, `GSBI_PROT_I2C`) + `gsbi4_i2c` QUP (0x16380000, GIC SPI 153, pins 47/48 function `gsbi4`), PM8058 `l11` 2.85 V
    (`vdd_l2_l11_l12` from VPH) and `lvs0` (`vdd_l0_l1_lvs` from S3, as on the mainline dragonboard), `camera@3c` `aptina,mt9m113` with
    `clocks = <&mmcc CAMCLK0_CLK>`, **`assigned-clocks = <&mmcc CAMCLK0_SRC>`** at 24 MHz (on the clock branch it was silently not applied: the
    clock stayed at 27 MHz PXO), reset GPIO 106 (active low), `powerdown-gpios` GPIO 107 (active high), pins `cam_mclk` (32) and 106/107,
    CSI-2 endpoint (1 lane, no remote yet).
  - Clock: `camclk0` of `mmcc-msm8960` is the MSM8660 one unchanged (CAMCLK_CC/MD/NS 0x140/0x144/0x148, MN reset bit 8 of CC, PLL8 = 2, same rate
    table as the vendor `clk_tbl_cam`), now in the MSM8660 clock list of patch 0020, **except its halt bit**: the 8x60 has none (vendor
    `halt_check = DELAY`), the 8960 bit (0x1e8 bit 15) gave "camclk0_clk status stuck at 'on'" -> `BRANCH_HALT_DELAY` for the 8660.
  - Driver: `kernel/src/drivers/media/i2c/mt9m113.c` (+ patch 0030 Kconfig/Makefile, `CONFIG_VIDEO_MT9M113=y`): the vendor sequence (supplies,
    power-down low, 20 ms, MCLK, reset pulse, 10 ms) then a 16-bit read of register 0x0000; no V4L2 sub-device yet.
  - Tried first: the mainline `mt9m114` driver (same Aptina family) as a probe tool. It needs `link-frequencies` (384 MHz for its fixed PLL), then
    powered the sensor and **read its command register 0x0080 over I2C successfully** (so the bus worked) but stopped with "Command 2 failed":
    the MT9M113 does not have the MT9M114 host command interface. Our own driver was written instead.
- **Step 2 DONE (2026-10-27): V4L2 sub-device** (`kernel/src/drivers/media/i2c/mt9m113.c`, image `out/uImage-cam2`, `mt9m113 2-003c: MT9M113 front camera
  ready (640x480 UYVY, one CSI-2 lane)`). Rewritten on the 6.12 APIs (CCI regmap with retries as the vendor driver, `v4l2_subdev` with one source pad,
  `v4l2_async_register_subdev_sensor`, `init_state`, `s_stream`); the 480-write initialisation table of the vendor driver (PLL, core, ISP, AE/AWB
  tables) is generated into `mt9m113_regs.h` by `scripts/tools/gen-mt9m113-table.py` from `out/refs/cm-cam/mt9m113_reg.c`; the sequence is the vendor
  one: ID check, table with the update-freeze of variable 0x0248, wait for `SEQ_CMD` (0xA103) = 0, refresh (5), `MIPI_CONTROL` 0x3400 = 0x7A08 (LP clock lane;
  `mt9m113.continuous_clock=1` selects 0x7A0C, continuous clock, to ease the first receiver bring-up), `RESET_REGISTER` 0x301A = 0x120C, `SEQ_CAP_MODE`
  0xA115 = 0x30, preview command (1), preview AE limits. The sensor is powered only while streaming (power-up, ID, init ~0.8 s). Formats: only 640x480
  `MEDIA_BUS_FMT_UYVY8_2X8` (context A; context B = 1280x1024 later); the byte order/range are to be confirmed on real frames.
  **Test without a receiver** (debugfs `mt9m113/stream`, `mt9m113/regs`): after `echo 1 > stream`: chip id 0x2480, PLL_CONTROL 0xb046 (locked), STANDBY 0x002a,
  RESET_REGISTER 0x120c, MIPI_CONTROL 0x7a08, `SEQ_CMD` 0 (done), `SEQ_STATE` 3 (preview), no I2C error. **The auto exposure runs and tracks the light**:
  sampled each second over 90 s, a phone lamp on the lens drops the coarse integration time 732 -> 244 -> 190 -> 5 (minimum) within 3 s, and 1 s after the lamp is
  removed it is back at 732 (gain 13-15). Covering the lens in room light changed nothing (the exposure was already at the limit of the preview AE
  index 8). So the sensor core, its ISP and its MCU are alive; only the MIPI output cannot be checked without a receiver.
  Next: step 3 (below). Nodes `/dev/v4l-subdev*` will appear when the receiver driver binds it (async notifier).
- **Plan proposed to the user:** (1) sensor alive: GSBI4 I2C + LVS0/L11 + MCLK + reset/power-down, read the model id 0x2480; (2) minimal V4L2
  sub-device driver from the CM tables (YUV422, the sensor does its own 3A); (3) new minimal CSI-2 PHY + CSI receiver + VFE 3.1 driver in YUV
  pass-through to memory (no statistics/ISP processing), the big piece; (4) browser side (GStreamer/getUserMedia in WPE).

### GPU varying defect FIXED: GFX3D power cycle at init (2026-10-26)
- **Cause:** mainline never switches the GFX3D power rail off (the bootloader leaves `GFX3D_GFS_CTL = 0x300`: ENABLE + RETENTION, and there is no
  MSM8660 footswitch driver), while the downstream kgsl collapses it at every GPU idle through `footswitch-8x60.c`. The GPU internal memories kept
  stale content across draws, processes and reboots (hence "the state survives a reboot").
- **Fix:** `kernel/patches/0029-drm-msm-a2xx-msm8660-gfx3d-power-cycle.patch` (generator `scripts/tools/gen-patch-0029.py`): at the start of
  `a2xx_hw_init`, on `hp,touchpad` only, the downstream footswitch_disable + footswitch_enable sequence on the MMSS registers: core clock at 27 MHz,
  `GFX3D_CC` bit 31 (retain) off, resets asserted (`SW_RESET_AHB` bit 10 = gfx3d_p, `SW_RESET_CORE` bit 12 = gfx3d), clamp (bit 5), rail off (bit 8),
  20 ms, rail on, unclamp, resets released, core reset toggled again ("required for GFX3D"), retain on, rate restored. Image `out/uImage-gpu-pc`.
- **Result:** vbotest **180 right draws out of 180** (30 processes; before: 10 wrong out of every 16, whatever the Mesa version, microcode or clocks).
  The only remaining bad pixels (120 = right column/bottom row) are the Mesa regression after 20.3 (also in Alpine's Mesa 24.2, which runs fine on the
  FD220: renderer "FD220", same cycle as 22.3 before the fix).
- Cog on the GPU (Alpine 3.21, Mesa 24.2): `cog --platform=drm --platform-params=renderer=gles` runs; the `modeset` renderer cannot flip GPU buffers
  ("failed to schedule a page flip: Invalid argument": the buffers are not scanout-capable, see Jonathan Marek's "HACK: use vram for scanout buffers").
- `hw_init` (and so the power cycle) runs again at every runtime resume (66 ms autosuspend), like webOS collapsing the rail at idle. **That is
  needed:** with a single cycle per boot the wrong varyings come back (66 wrong draws out of 180, `out/uImage-gpu-pc1`).
- **Flicker on touch (user report) fixed:** each cycle made the MDP4 underrun (`errors: 00000100` = primary interface underrun, one per cycle) because
  of the downstream 27 MHz `reset_rate` step of the core clock. Without the rate change: 180/180 right draws and 0 MDP error over 31 cycles
  (`out/uImage-gpu-pc3`), **user confirmed: no more flicker, "much smoother" than software rendering**. The 27 MHz step stays available with the
  module parameter `msm.tp_gfx3d_reset_rate=1` (off by default).
- **The browser starts on the GPU by default** (`/etc/tp.d/S90browser`, shared by the Debian and Alpine images): `cog --platform=drm
  --platform-params=renderer=gles` when `/dev/dri/renderD128` exists; software rendering (kms_swrast + modeset renderer) as the fallback when there is
  no render node (non-KMS image) or when `/mnt/media/touchpad/swrender` exists. The first line of `/tmp/cog.log` says which path was taken.
  Checked after a reboot of `out/uImage-gpu-pc3` (current reference image): "browser: GPU (freedreno)", page loaded 21 s after boot, no MDP error.
- The RPM fabric/EBI1 clocks are back in the GPU node (bandwidth while the GPU runs; they did not cause nor fix the underruns: 32 errors over 31 cycles
  with them and the 27 MHz step).
- To align further with downstream: its probe also sets `GFS_DELAY_CNT` (31, low bits of GFS_CTL) and clears `RETENTION_BIT` (bit 9) so that a
  disable really collapses the core; not done yet (the fix works with bit 9 still set).

### Alpine 3.21 rootfs: WORKS in software rendering (2026-10-26)
- **Direction set by the user:** our job is the Linux (kernel + DT); the userspace comes from a distribution's prebuilt packages with adjustments
  only. Alpine (apk, musl, the base of postmarketOS) was preferred to Debian for that.
- Alpine armv7 packages (checked): 3.18/3.19 Mesa 23.0/23.3, WPE 2.40.5, Cog 0.17.1; **3.20/3.21 Mesa 24.0/24.2, WPE 2.40.5, Cog 0.18.3**; 3.22 has
  Mesa 25.1 but no WPE/Cog any more for armv7. WPE stays at 2.40.5 (2023) on armv7.
- Build: `scripts/alpine-rootfs.sh` -> `out/rootfs-alpine.ext4` (Dockerfile `docker/alpine-rootfs/Dockerfile`: cog, wpewebkit, mesa-dri-gallium,
  mesa-egl, mesa-gbm, mesa-gles, libinput, eudev, dropbear, wpa_supplicant, iw, font-dejavu, ca-certificates, shared-mime-info, bash; 357 MB, image
  592 MB). **Same overlay as Debian** (`debian/overlay`: BusyBox init, `/etc/rcS`, `/etc/tp.d`); `S02udev` now starts systemd-udevd or eudev.
  Alpine's BusyBox has no `devmem`: the static BusyBox of the initramfs is added as `/usr/local/bin/busybox-tp` with a `devmem` link (tp, charge-mode,
  software reboot); `scripts/cycle.sh` tries `devmem` then `busybox devmem`.
- **Only rebuilt component:** the Cog DRM module (same crash as Debian: "Buffer address requested when its parent pool has an external reference..."
  on the first page update): `scripts/build-cog-alpine.sh` builds Cog 0.18.3 with `-Dplatforms=drm -Dprograms=false` and `debian/cog-patches` in an
  alpine:3.21 armv7 container -> `out/cog/alpine3.21/libcogplatform-drm.so`, copied over `/usr/lib/cog/modules/` by `alpine-rootfs.sh`.
  Worth sending the fix upstream (Igalia/cog) so that even this goes away.
- Install: copy the image (gzip over SSH, md5 checked) to the media partition as `touchpad/rootfs-alpine.ext4`, write that path in `touchpad/rootfile`
  (the Debian one is kept: `touchpad/rootfile.debian`; the user's URL is kept in `touchpad/url.bak`, `url` points to
  `file:///mnt/media/touchpad/touch-test.html` for the tests).
- Result (`out/uImage-gpu-flto`): Alpine 3.21.9 reaches the end of rcS in about 9.6 s, Cog loads the local page (about 13 s after its start), ts_srv +
  eudev give `ID_INPUT_TOUCHSCREEN=1`, **user confirmed: the page stays on screen and touches draw points**. Wi-Fi with `scripts/tablet-wifi.sh`
  (wpa_supplicant + BusyBox udhcpc/ifconfig) and SSH work unchanged.
- Not checked yet on Alpine: the GPU path (Mesa 24.2: Debian 13's Mesa 25 crashed on a2xx, Mesa 22.3 and 20.3 run with the varying defect).

### Touch screen: partially working, ts_srv port (2026-10-25/26)
**User verdict (2026-10-26): partially working, too much offset** (taps near the left/right edges do not land where the finger is; the strips over
the missing columns do not detect). Parked to work on the GPU. **To do when resuming:**
1. Reduce the offset: re-run `touch-calib.html` with more targets near the edges of the sensitive area, check the bias near x = 183-300 and 855-942
   (centroid pulled inward by the missing columns: an edge-aware centroid or a per-zone correction table may be needed), and the double/missed taps.
2. Test taps and scrolling on a real page in Cog (S80touch + S90browser at boot).
3. Display rotation: Cog `rotation` (needs the GLES renderer or a change to the modeset renderer) + libinput `LIBINPUT_CALIBRATION_MATRIX` for the touch.
4. Edge strips: find whether the controller can report the missing columns 0-6, 34-35, 37-39 (webOS firmware `cy8ctma395.fw`/`.hex`, controller id
   `1e0b8069 03`; webOS hidd may send more configuration than the 6 CM writes; not flashed, not tried).

**Edge stretching (2026-10-26, user choice "centre exact, edges stretched"):** the paint page (`scripts/tools/touch-paint.html`, brush 20 px, prints the
extremes) showed the reachable area x 194..934, y 0..760 with the physical calibration. ts_srv now maps x 194..300 -> 0..300 and 830..934 -> 830..1023
(y 700..760 -> 700..767), identity in between (`edge_stretch()`). User verdict: **still too much offset on the sides**.
**Why the dead strips (research 2026-10-26):** no TouchPad source documents dead strips as normal; TouchPad-specific answers blame a failing digitizer or
an aftermarket replacement digitizer that does not respond like the original ([iFixit](https://proxy.ifixit.com/Answers/View/193303/Touch+not+working+on+few+areas),
[TechRepublic thread](https://www.techrepublic.com/forums/discussions/why-doesnt-a-replacement-hp-touchpad-digitizer-respond-like-the-original/)). Our data agree: the
silent channels are not one contiguous range (0-6, 34-35 dead while 36 works, 37-39), which looks like broken traces / connector contacts rather than
a firmware setting (Cypress TMA controllers test channels for opens/shorts and zero the failing ones), and webOS shows two dead edges too. Open
question for the user: was the screen/digitizer of this tablet ever replaced or opened? Next hardware check: reseat the digitizer flex cable.
**User (2026-10-26): the screen is original, everything worked before; the offset started when webOS was restored (webOS Doctor).**
**webOS touch firmware tools (read from the webOS root LV):** `/usr/bin/PmTpUpdater` (shell script) and `catnip`. The touch system is a PSoC3
CY8CTMA395 bridge (firmware `cy8ctma395.fw` + `cy8ctma395_hssp_bridge.fw`, version file `cy8ctma395.ver` = "15 0 0") in front of **5 "Indium"
CY8CTMA375 sensing chips** (I2C bus 5 address 0x69, firmware `cy8ctma375.hex`) with **global/local IDAC calibration tables**. `PmTpUpdater` flashes
when the file version is newer (`-f` forces) and **calibrates** (`catnip --i2c-only cal -s 64`) when the tables are empty/zero (`-c` forces).
**Read-only state under webOS** (hidd stopped, `out/tools/ctp-idac.sh`, log `data/webos-touch/state-before-recalibration-2026-10-26.log`): jtag id `1e0b8069` rev 03, firmware 15.0.0 (= file),
`cols` 30, mode idle, subs 4, cycles 4, freq 160, gain 2x, noise 10, cmn 1, jitter 1, `pid` read fails (I2C error). Global IDAC (150 values) and local IDAC
(1200 values) all 11..24, unified IDAC 600..950 per line: **uniform, dead columns included**. **Raw buffer (40 groups of 30): columns 0-2, 4-5 and
33-39 read 0 over their whole length, columns 3 and 6-32 read 200-250** (28 live columns, as with our kernel). Raw 0 with a normal IDAC = channel
over-compensated: its capacitance is far below the stored calibration (stale calibration after the restore, or disconnected electrodes). The test is
a recalibration with the official `PmTpUpdater -c` (reversible: the current tables are in the logs and `catnip gidac/lidac` can write them back).
**Recalibration done (2026-10-26, user approved, tablet flat, nothing on the screen; log `data/webos-touch/recalibration-2026-10-26.log`, which also holds the previous factory tables):**
live columns 6-32 now IDAC 11-27 with a uniform raw ~178; **the dead columns 0-2, 4-5, 33-39 got their local IDAC driven to 0 (the minimum) and still
read only 20-50 raw**: even without compensation they cannot reach the target, their capacitance is almost nil. The factory tables had normal IDAC
for them, so they WERE connected at the factory. **Conclusion: hardware fault, the edge electrodes are electrically disconnected from the
controller** (digitizer flex cable unseated/damaged at its connector, or broken traces at the panel edges); not firmware, calibration or driver.
The new calibration is kept (better for the live columns). Only a hardware repair (reseating/replacing the digitizer flex or the digitizer) can
bring the edges back; in software the choice is between exact positions (dead strips) and stretched edges (user found too much offset).
**After the recalibration (2026-10-26):** the dead columns now carry background noise in the UART stream (no longer exact 0) but still **no touch
response** (raw capture while sliding along both edges, `ctp-edges2.raw`, deleted afterwards: 0-4 cells >= 20 in columns 0-6, 34-35, 37-39 against hundreds in 7-33
and 36). ts_srv keeps masking them (needed now). TWRP 2.6.3.0 (official image, sha256 `32442e4d...`, `out/twrp/` (deleted afterwards, re-download from dl.twrp.me), booted in RAM with `cycle.sh`
from the recovery mode, nothing written) gave the user the impression of more reachable edges: most likely its layout (big buttons, portrait UI).
**User choice: exact physical mapping** (`EDGE_STRETCH 0` in ts_srv, the stretching code is kept).
**Forced reflash (2026-10-26, user accepted the risk): `PmTpUpdater -f` under webOS** (log `data/webos-touch/reflash-2026-10-26.log`): HSSP bridge, the TMA375 "Indium"
firmware (128 rows; its checksum read back `f8ff` against `5596` with status 0x07, a check the webOS script itself has commented out), the PSoC3
UART firmware v15, then a calibration; rc 0, firmware still 15.0.0, touch works. **The dead columns are exactly the same** (0-2, 4-5, 33-39: raw
20-50, local IDAC driven to 0): firmware and calibration are definitely ruled out, the edge electrodes are physically disconnected.
**postmarketOS reference (found by the user):** pmaports commit `80131a50` (Ben Wolsieffer, 2021-12), package `main/ts-srv`: the Evervolv ts_srv
(`android_device_hp_tenderloin-common` commit `af457915`, 2019) patched to build without Android, for the downstream 3.4 kernel (`/dev/ctp_uart`).
Fetched with `git fetch` (the GitLab web UI is behind the Anubis bot check), copies in `out/refs/pmos/` and `out/refs/evervolv/`. Its final
`ts_srv.c` reads the 40 columns as they come and maps them linearly over the full width (x = 1023 - j * 1024/39, ~26.3 px per column, our measured
25.3): on a sound panel all edges are reachable, nothing compensates missing columns. Taken from it: `ABS_MT_TRACKING_ID` max 2147483000 (the
IDs keep increasing; ours declared 65535) and `ABS_MT_TOUCH_MAJOR` 25..500 for libinput, and the udev rule `/etc/udev/rules.d/90-touchscreen.rules`
(`debian/overlay`) that tags "HPTouchpad" as a touch screen.

Details of the work so far:
- **Architecture (CM 3.4 / webOS, read before coding):** the Cypress CY8CTMA395 is configured over I2C (0x67 on the GSBI10 QUP) and streams raw
  capacitance lines on the GSBI10 UARTDM RX (GPIO 71) at 4 Mbaud; the kernel driver only handles power (PM8058 L10 3.05 V + `vdd50_boost` 5 V on
  GPIO 102), XRES (GPIO 70) and SWD flashing. Touch points are computed in userspace (CM `ts_srv`, webOS `hidd` + `libhidtouchpanel.so`).
- **Kernel/DT** (`uImage-touch1`): `gsbi10` node (`GSBI_PROT_I2C_UART`), `gsbi10_serial` (msm_serial, `/dev/ttyMSM2`, alias serial2, 64 MHz clock so
  4 Mbaud works), `gsbi10_i2c` (`/dev/i2c-1`), pinctrl for GPIO 70-73/102/123, `vdd50_boost` fixed regulator + `touch-power` regulator-output consumer
  (`/sys/devices/platform/touch-power/state`), `CONFIG_INPUT_UINPUT`, `CONFIG_REGULATOR_USERSPACE_CONSUMER`.
- **XRES is active low**: the 3.4 sysfs `xres` "1" = assert = GPIO 0 (`cy8ctma395_xres_assert`). Driving GPIO 70 high as "reset" kept the chip in reset:
  every I2C write NACKed. WAKE (GPIO 123, user-pins `level`) is the raw level. Lines are held through the GPIO chardev (a `gpioset` that exits releases them).
- **Stream format:** `ff 43 <row | 0x80 start> + 40 bytes` per row (30 rows), `ff 47 ...` ends a scan; partial scans start at the first non-empty row.
  Data only flows while a finger is on the panel (~55-95 scans/s). Rows of empty scans are really empty (skipping them is right).
- **Port** (`debian/ts_srv/`, GPL-2.0 from CM `android_device_hp_tenderloin` cm-11.0; build `scripts/build-ts-srv.sh`, static armhf `out/ts_srv/ts_srv`;
  started by `/etc/tp.d/S80touch`, copied into the image by `scripts/debian-rootfs.sh`): stderr logging, termios 4 Mbaud on `/dev/ttyMSM2`, GPIO chardev,
  regulator-output, `/dev/i2c-1`, uinput "HPTouchpad" with MT protocol B + `BTN_TOUCH`/`ABS_X`/`ABS_Y` + `INPUT_PROP_DIRECT` (udev: `ID_INPUT_TOUCHSCREEN=1`).
  Bugs found and fixed on the way:
  - msm_serial reads hold a few lines, hsuart reads held whole scans: a read without a `0x47` line was taken as "0 touches" -> liftoff every frame.
    `snarf2()` now returns -1 when no scan completed.
  - protocol B path of the original (never used by CM): `slot_in_use[-1] = 1` when a tracked point had no slot yet, which overwrote `uinput_fd`
    (the events went to the log file).
  - a held finger gives about one scan in five under the continue threshold: such scans are skipped (up to 2 in a row) without touching the tracking state.
  - orientation: rotated by 180 degrees from the CM build (native KMS 1024x768 frame). Display rotation will be applied on top (Cog + libinput
    `LIBINPUT_CALIBRATION_MATRIX`), not in ts_srv.
  - debug: `TS_SRV_DEBUG=1` (down/up positions with sensor i/j and peak, per-second rates), `TS_SRV_RAW=<file>` (copy of the raw stream).
- **This controller reports only sensor columns 7..33 and 36** (of 40): measured with the noise of every column (`scripts/tools/ctp-cols.py`), edge
  traces, the 6 I2C configuration writes masked one by one (`ctp-test ... <mask>`: none changes the columns; without `30 0F` or `08 03` nothing is sent,
  without `41 10` the format changes) and **the same capture under the stock webOS kernel with the CM sequence** (`scripts/tools/ctp-webos.c`,
  `data/webos-touch/`: hidd config `HidTouchpanel_evt1.xml` = 30 x 40 grid of 194.6 x 145.4 mm on the 196.6 x 147.5 mm panel; controller id `1e0b8069 03`,
  firmware ver `15 0 0`, checksum `e34e`). The other bytes are 0 except metadata in rows 0-3 and 22 (cleared: they would make ghost touches). The user also
  sees two dead edges under webOS. So the left ~183 px and right ~82 px strips do not detect; the columns keep their physical position.
- **Calibration** (`scripts/tools/touch-calib.html`, 5x5 targets, least squares on the logged sensor positions): x = (j + 0.745) x 25.29,
  y = (i + 0.06) x 26.13 (40 x 30 grid at 4.9 mm covering the panel). Error inside the sensitive area under ~16 px; x ~ 855-942 is less precise (missing
  columns 34-35). Test page with drawing: `scripts/tools/touch-test.html`.
- Running under webOS: `stop hidd` drops the novacom USB session (and kills a script started through it): start captures detached (`setsid`). When novacom
  hangs, a `--init` container can be started beside the stuck one (`docker run -d --name hptouchpad-novacom3 --init ...`) after a `usbipd detach/attach`.
- To do: the list at the top of this section.
- Slowness seen with the test pages: CPU capped at 756 MHz (no SAW voltage driver, see CPU frequency) + 100 % software rendering.

### End of the session of 2026-10-17 (resume here)
- **Working software path:** `MESA_LOADER_DRIVER_OVERRIDE=kms_swrast` ALONE (no `GBM_ALWAYS_SOFTWARE`, no `LIBGL_ALWAYS_SOFTWARE`: those make Mesa 22.3 try zink,
  which fails, then fall back to the FD220) gives llvmpipe on `/dev/dri/card1`, vbotest 0 wrong pixels. **Cog with it segfaults** (rc 139, `-P drm
  -O renderer=gles`); next step: backtrace with gdb (installed on the tablet over Wi-Fi: `gdb -q -batch -iex "set debuginfod enabled on" -ex run -ex bt
  --args cog ...` with `DEBUGINFOD_URLS=https://debuginfod.debian.net`), then fix or work around the GLES renderer path (likely an EGL extension missing in llvmpipe).
- Tablet left on `out/uImage-gpu22` (patches up to 0028 + RPM bus clocks in the DTS), Debian 12 rootfs, Wi-Fi up by hand (passphrase in RAM only;
  `wifi-dhcp` calls `ifconfig`/`route` which are not in the PATH of the Debian shell: use `busybox ifconfig/route`), `/tmp/t2.html` = quoted test page.
- Patch set to review before keeping: 0022 sets `CP_DEBUG` bit 27, which webOS does not set; 0025 (NMRR) and 0028 had no measurable effect.

### Narrowing down inside the GPU pipeline (2026-10-17, late evening, `vbotest` variants)
- `VBO_RED=1` (fragment shader = constant red): 48 draws out of 48 right (3969/4096 red; the 127 others are the right column and the bottom row, the known
  edge coverage defect). The fragment shader always runs and the positions are always right.
- `VBO_VS=1` (texture coordinate computed from the position, no second attribute): same wrong cycle. Not the vertex fetch of the second attribute.
- `VBO_VS=2` (constant varying 0.25/0.75 from the vertex shader): right (`3fbf00`) in some draws, other values in the others, including gradients of earlier
  draws. **The defect is in the varying path (vertex shader exports -> parameter cache -> interpolation -> fragment shader inputs)**: the fragment shader
  reads stale parameter data in 10 draws out of 16.
- Mesa 22.3.6 sources (Debian source package, `out/mesa/a2xx-22.3.6/`): `fd2_emit_restore` already sends `CP_INVALIDATE_STATE`, `CP_SET_SHADER_BASES 0x80000180`,
  `RBBM_PM_OVERRIDE1/2 = 0xffffffff/0xfff` (explains the read-back), `SQ_INTERPOLATOR_CNTL = 0xffffffff`; for the A20x it writes `VGT_VERTEX_REUSE_BLOCK_CNTL = 2`
  and `VGT_OUT_DEALLOC_CNTL = 2`, for the A22x only `VGT_VERTEX_REUSE_BLOCK_CNTL = 0x3b` and never `VGT_OUT_DEALLOC_CNTL` (read back 0).
- Tried without effect (the cycle stays bit-identical, no hang): `CP_SET_SHADER_BASES` per submit (patch 0026, removed: Mesa already sends it),
  `VGT_ENHANCE = 0x00420000` like webOS (patch 0027, removed; verified applied: reads back 0x00420000), `VGT_OUT_DEALLOC_CNTL = 2` per submit (patch 0028,
  still in the set; NOT verified: reads back 0, also after a direct MMIO write), `GFX3D_CC` sleep/wake delays 0xFF.
- Ring writes of context registers through `CP_SET_CONSTANT` from the kernel do take effect (VGT_ENHANCE).
- **CP microcode eliminated (2026-10-26):** webOS ships a different A220 microcode (`/lib/firmware/leia_pm4_470.fw` md5 `2a81fc15...`, 1436 of 2305
  words differ from linux-firmware `dbc0f42b...`; `leia_pfp_470.fw` `b045ebda...` vs `73697be0...`, 275 of 289 words differ). Read from the webOS root LV
  mounted read-only from our Linux (`losetup -r -o 1048576 --sizelimit 595591168 /dev/mmcblk0p14`: LV `root` = extents 0-70 of 8 MiB after a 1 MiB
  pe_start), kept in `firmware/webos/gpu/` (git-ignored), built in with `ADRENO_FW=webos DTB_NAME=qcom-apq8060-hp-touchpad-kms ./build.sh all`
  (`out/uImage-touch1-wfw`; the microcode comes from the initramfs). Result: the same vbotest cycle with the same wrong values (3841, 4077, right = 120).
- **Reference found (2026-10-26): a2xx in mainline msm WAS tested on this tablet.** Jonathan Marek's kernel commit adding a2xx to drm/msm (Dec 2018)
  lists "msm8060 tenderloin with an A220"; his tree is public: `github.com/flto/linux` branch `msm8660` (Linux 5.8-rc4 + "Tenderloin WIP" 2018-11-14
  + "HACK: use vram for scanout buffers" 2020-07-08), patches saved in `out/refs/flto-tenderloin-wip.patch`, `out/refs/flto-vram-hack.patch`; his Mesa
  work is `github.com/flto/mesa` branch `a2xx` (2018, merged upstream later). Freedreno itself was first developed on an A220 (Rob Clark, kgsl, 2013).
  His GPU node: `qcom,adreno-220.0`, clocks GFX3D / GFX3D_AHB / GMEM_AXI only, 266.67 MHz, PLL2 forced on at the mmcc probe, no power domain.
- Aligned on it, measured with vbotest (same wrong cycle every time, 3661/4077/4073/4072/3841 ... and 120 for the right draws):
  - GPU at 266.67 MHz (`gfx3d_clk` reads 266666666; the OPP was 200 MHz): no change (`out/uImage-gpu266`). Kept at 266.67 MHz.
  - GPU node with his three clocks (IMEM AXI/AHB and the RPM fabric/EBI1 clocks removed) and patches 0022 (A22x init like kgsl), 0025 (NMRR) and 0028
    (VGT_OUT_DEALLOC_CNTL) moved to `kernel/patches/removed/`: no change (`out/uImage-gpu-flto`). Kept that way (closest to the tested setup).
  - The 5.8 and 6.12 a2xx submit sequences are the same (`out/refs/a2xx_gpu-5.8-to-6.12.diff`; 6.12 only adds the legacy-firmware protection switch,
    not active with our microcode). The kernel side is therefore unlikely; next: an older Mesa (`scripts/build-mesa-old.sh`, 20.1.10, mid-2020).
  - **Older Mesa: prebuilt Debian bullseye Mesa 20.3.5** (no build: `apt-get download` of libgl1-mesa-dri, libegl-mesa0, libgbm1, libglapi-mesa,
    libllvm11, libsensors5, libz3-4, libvulkan1, libffi7 in a bullseye armhf container, security source removed; extracted to `/tmp/m20` on the tablet
    with only `msm_dri.so`; run with `LD_LIBRARY_PATH=/tmp/m20/opt/mesa20/lib LIBGL_DRIVERS_PATH=/tmp/m20/opt/mesa20/lib/dri vbotest`):
    **the same wrong cycle** (10 of 16 draws, values 4080/3840/4011/3750/3600...), so not a Mesa 22.3 regression. But the right draws give 0 bad pixels
    instead of 120: the missing right column/bottom row IS a Mesa regression between 20.3.5 and 22.3.6 (secondary).
  - Left after these eliminations: the state that survives a reboot. The GFX3D rail is never power-cycled (bootloader leaves it on with retention, no
    footswitch sequence in mainline), so the GPU internal memories may keep content from earlier runs/webOS. Next: the downstream footswitch/reset
    sequence at GPU init, before the microcode load.
  - The user's direction (2026-10-26): our job is the Linux (kernel); userspace should come from a distribution's prebuilt packages (Debian, or Alpine
    3.20 armv7 which has cog 0.18.3) with adjustments only, not rebuilt.
- Open questions for the freedreno developers (the A220 path of Mesa was written for/tested on which devices?): parameter cache / export synchronisation on the
  A220, the meaning of `VGT_OUT_DEALLOC_CNTL` read-back 0, `VGT_VERTEX_REUSE_BLOCK_CNTL = 0x3b` vs kgsl 2 / webOS 0x28f.

## Cog / WPE (started 2026-10-16)
**Decisions:**
- WPE WebKit (Buildroot 2025.02.5, `buildroot/configs/hptouchpad_defconfig`) needs OpenGL ES + EGL, and there is no GPU driver yet (the MDP4/
  Adreno 220 `msm` DRM path is a big piece of work): stage 1 is **software rendering**, Mesa **swrast = llvmpipe** (LLVM) with GBM, on the KMS-only
  `simpledrm` device (`/dev/dri/card0` from the bootloader framebuffer, 1024x768 a8r8g8b8). Cog is built with both the Wayland (`fdo`) and the `drm`
  platforms, with Weston (DRM backend) as a fallback compositor; no touch input yet (keyboard/none), the touch panel is a later block.
- CPU target: `cortex_a8` + `BR2_ARM_FPU_NEON` (the Scorpion is ARMv7 with VFPv3 + NEON), Thumb-2, glibc, kernel headers 6.12, BusyBox init + eudev
  (libinput needs udev), Dropbear, wpa_supplicant, iw, BlueZ utilities, fonts, CA certificates, glib-networking (https).
- The rootfs (about 1.5 GB image) cannot live in RAM (1 GB): it will be a file `rootfs.ext4` on the **webOS `media` LV** (vfat, 12.6 GiB, `/dev/mmcblk0p14`
  at byte offset 1116733440, found with the LVM metadata of `out/lvm-metadata.txt`), reached from the initramfs through two loop devices (`losetup -o`),
  then `switch_root`. webOS stays untouched (the file is simply added to its media partition; delete it to undo). The initramfs keeps the USB serial/
  network debug shell until the new init takes over. Other LVs: root 0.55 GiB, var, update, log, mojodb, filecache, swap 0.5 GiB.
**Status:** the Buildroot build was started (`./build.sh rootfs`, log `out/rootfs-build.log`; host LLVM first, then Mesa, WebKit...; expect hours; the
downloads are slow). Prepared meanwhile: the initramfs boots a rootfs file when the command line has `touchpad.rootfile=<path on the media volume>`
(loop device with offset 1116733440 on `mmcblk0p14` -> vfat -> loop on the ext4 image -> `switch_root`; tested: the media volume mounts, 12.4 GB free,
applets present); the rootfs overlay (`buildroot/overlay`: `inittab`, `S01gadget` USB serial+ethernet, `S10wifi` wpa_supplicant from a config **read on the
media partition (`touchpad/wpa_supplicant.conf`, never in the repository)**, `S90browser` Weston + Cog, SSH from Buildroot's Dropbear with the project key).
**Fallback if the Buildroot build fails or is too slow:** Debian bookworm already ships armhf binaries (`cog` 0.16.1, `libwpewebkit-1.1-0` 2.38.6,
`libwpebackend-fdo`, Mesa with llvmpipe, `weston`): Debian armhf needs ARMv7 + VFPv3-D16 + Thumb-2, which the Scorpion has. The packages could be
downloaded with `apt-get download` / `dpkg-deb -x` in the builder (as done for BlueZ, Dropbear...) into a rootfs directory, then packed in an ext4
image (post-install steps done by hand: CA certificates, gio module cache, fonts). The user chose to keep the Buildroot build running (2026-10-16).
To do: copy `rootfs.ext4` to `/mnt/media/touchpad/` (1.5 GB: over the weak Wi-Fi it takes a while, or over USB once the USB network works), boot with the
parameter, first page on screen, then performance (llvmpipe on two 756 MHz cores will be slow).

## Backlog and priorities (set by the user on 2026-10-16)
**Current priority: Wi-Fi** (scan works; connect and DHCP next). Then, in no fixed order:
- **Charging and power consumption (standby/idle)**, to be handled later: charger source detection and the DC input current
  (750 mA - 2 A), charge policy from the battery temperature, low-battery shutdown, lower consumption (backlight, CPU frequency,
  idle states, suspend). Facts: see "Charging and battery" (the tablet discharges at about 0.9 A on a PC port).
- **Camera** (Aptina mt9m113 on the MSM camera subsystem: VFE/CSI driver missing in mainline).
- **Sound** (WM8958 codec, MSM8660 audio path through the ADSP missing in mainline; microphone).
- **Touch panel** (Cypress cy8ctma395 / the A6 "TP" firmware: the A6 also is the touch panel controller; driver to write).
- **Lighting**: backlight and the "core navi" lights through the LM8502 on the GSBI8 I2C bus (the bus now works), LCD panel.
Already done: Bluetooth (classic), TSENS temperature, battery gauge, charger status, CPU1 clock.

## Next hardware blocks (agreed 2026-10-15, after the Wi-Fi scan is understood or parked)
1. **Bluetooth** (CSR BC6, independent of the Wi-Fi SDIO bus): downstream board file `board-tenderloin.c`
   has the reset line `BT_RST_N` and `BT_HOST_WAKE` as "user pins" (`bt_pins`, resolved through `pin_table`),
   a high speed UART on GSBI6 (`msm_uart_dm1_pdata`, wake-up interrupt on GPIO 54) and `board_gsbi6_init`.
   Plan: DT node for the GSBI6 UARTDM + reset GPIO (gpio-hog or `shutdown-gpios`), `hci_uart` (BCSP or H4,
   to be checked on the webOS side) or `btattach`, test with `hciconfig` / `btmgmt`.
2. **CPU frequency and thermal throttling** (see the section above for the results): webOS reports `Max ACPU freq: 1188000 KHz`, uses cpufreq
   (performance governor by default, no thermal framework in its config). Mainline has no MSM8660 ACPU
   clock driver as far as known (to verify), so the CPU probably stays at the bootloader frequency:
   a frequency table + `cpufreq-dt` and a TSENS-based thermal zone have to be checked.
3. Audio (WM8958) and camera (mt9m113): driver porting, later.

## Wi-Fi scan: SOLVED (2026-10-16)
**Root cause:** the old AR6003 firmware (`fw-3.bin`, 3.2.0.144, api 3) announces the capability `sta-p2pdev-duplex`, so the 6.12
driver sends the new `WMI_BEGIN_SCAN` command (id `0xF08B`, `ath6kl_wmi_beginscan_cmd`), which this firmware **never processes**:
the host sends it, the HTC credits come back, and no scan ever starts (no scan result, no completion event). The 3.4 driver of
CyanogenMod, which works with the same `fw-3.bin`, only knows the legacy `WMI_START_SCAN` (id 7).
- How it was found: the firmware debug log read straight from target memory (`debugfs .../ath6kl/fwlog`, works; the earlier "hang"
  was the broken serial node) showed the firmware receiving `SET_BSS_FILTER` (0x9) and `SET_APPIE` (0x3f) but not the scan command;
  the driver trace (`debug_mask = 0x104E1`: HTC, HIF, IRQ, WMI, SDIO) showed `wmi tx id 61579 len 54` (= 0xF08B) sent and completed with
  its credit returned; the HTC credit statistics (`credit_dist_stats`, `endpoint_stats`) were clean (16 sent, 16 credits returned).
- Fix: patch 0014: `ath6kl_wmi_beginscan_cmd()` uses the legacy `ath6kl_wmi_startscan_cmd()` when `fw_api < 4` (or the capability is
  absent). Result: `iw dev wlan0 scan` returns **23 BSS** on 2.4 GHz (channels 1, 2, 6, 11) and 5 GHz (36-44, 52, 100): the chip is dual band.
- Everything tried before (power save, bus clock, SDIO interrupts, BT coexistence, regulatory database, probed SSID slots (0008, 0009),
  s3 high power mode, SDIO CRC workaround...) was not the cause; some of those patches/settings can now be re-evaluated and removed
  one by one (0003 debug instrumentation, 0008/0009 probed SSIDs, the `qcom,dummy52` stats). Keep 0001/0004/0006 (SDIO host fixes).
- **Connection tested (2026-10-16)** on the open guest network `AKTO-GUEST` (user's choice): `iw dev wlan0 connect AKTO-GUEST`, then
  `udhcpc` with a tiny script (address, default route, `resolv.conf`): lease obtained, gateway ping 0 % loss (7-167 ms), DNS answers,
  a raw TCP request to the internet is answered with a **captive portal redirect** (HTTP 307 to a "sponsored guest" login page): the
  link is fully working, the portal itself was not touched (it needs the user's decision). Link: 2.4 GHz, -67 dBm, up to 72 Mbit/s
  (MCS 7). The helper is `wifi-connect <ssid>` (initramfs `/usr/bin`, open networks only). Busybox `wget` segfaults in this build (unrelated);
  `ping`, `nc`, `nslookup`, `route` are available as `busybox <applet>`.
- Next for the Wi-Fi: WPA2 (wpa_supplicant + its libraries in the initramfs; credentials must never be stored in the repository),
  throughput test, power management / suspend, removal of the experimental patches that were not the cause.

## Wi-Fi scan investigation (history, state at the end of 2026-10-15)
**Established (measured):**
- The chip boots, answers commands (target statistics) and the host gets interrupts during bus activity
  (the controller SDIO interrupt, status bit 25, fires). `wlan0` exists with the board MAC from the
  calibration data (`00:1d:fe:e8:79:9b`, same as webOS), regulatory domain FR from `regulatory.db`
  (now in the initramfs, upstream-signed variant).
- The firmware itself scans fine at boot (its log shows 11 channels scanned and 34 BSS events sent).
- After the host requests a scan, no scan result and no scan-complete event ever reaches the host
  (`iw dev wlan0 scan` never returns). At idle the SDIO status register shows no pending interrupt
  (the bit 22 seen set was a stale latched bit: clearing it by hand, it stays 0).
- The reference that works is CM: its device repo (`CyanogenMod/android_device_hp_tenderloin`) ships
  the **mainline `ath6kl` backported (`external/backports-wireless`) with the same `fw-3.bin`**
  (identical size 104562 bytes), the same board data names, and the kernel 3.4 `msm_sdcc` host driver.
  LuneOS only reuses that same CM/Halium kernel; it has no extra Wi-Fi/BT handling (it does put
  `regulatory.db` in the initramfs for the TouchPad).

**Tried and ruled out (each tested on the device):** Wi-Fi power save off; bus clock 12/16/48 MHz;
SDIO polling thread vs controller SDIO interrupt; arming interrupt bits 25 and 22; clock PWRSAVE bit;
BT coexistence command (`colocated bt type 2`, antenna 2, as webOS sends); regulatory database;
limiting the probed SSID slots to 9 (3.4 value) and sending none for a wildcard scan (3.4 behaviour).
The begin-scan command builder is identical in 3.4 and 6.12.

**Corrections of earlier statements (kept for honesty):** (1) "the firmware hangs after SET_APPIE" was
wrong: the firmware log pool is only about 7.5 KB, so every capture simply ends when the pool is full;
(2) the `MMCIMASK0` reading at `0x38` was the clear register, the mask is at `0x3C`;
(3) the 4-bit async interrupt mode is enabled by the 3.4 driver too, so it is not a difference.

**Findings of the second half of 2026-10-15 (webOS comparison):**
- webOS is Linux (2.6.35-palm kernel) and gives a root shell through `novacom run file:///bin/sh script`
  (push the script with `novacom put`; `sh -c` and `luna-send` do not work from there). `debugfs` is rich
  (gpio, regulator, clk, mmc1, msmsdcc). Probe scripts: `scripts/webos-probe.sh`, `scripts/webos-wifi.sh`
  (could not switch the Wi-Fi on: luna-send gives no reply), results in `data/webos/probe-*.txt`.
  `/dev/mem` writes fail on webOS, so it cannot be sent to recovery by software: use the buttons.
- webOS Wi-Fi power sequence (`board-tenderloin.c`, `tenderloin_wifi_power`): 8058_s3 gets a 80 mA load
  (high power mode), then 8901_l3 and 8901_l1 (3.3 V), 8058_l19 (1.8 V), wait 5 ms, WLAN_RST_N low
  5 ms then high. Same order as ours. The webOS driver is the vendor `libra_sdioif` module (not ath6kl).
- Tested, no effect on the scan: `qcom,force-mode = HPM` on `pm8058_s3` (kept in the DTS, it matches
  webOS); SDIO CRC workaround restored for hw 2.1.1 as the 3.4 driver does (pins 10-13 only, no pin 9;
  patch 0002 rewritten, my earlier claim that the downstream driver skips hw 2.1.1 was wrong).
- Reading the firmware log from target memory (`debugfs .../ath6kl/fwlog`, diag window) seemed to block the
  shell, but the same session had a broken serial path (`/dev/ttyACM0` in the Alpine WSL distro had become a
  plain file after the Docker/WSL restart), so that conclusion is **not established**: redo the capture
  now that the node is a real device before trusting any theory about a stuck SDIO host.
- Reference kernel (CM11 3.4) booted with a blue screen then fell back to webOS twice (fbcon enabled in
  the second try): not understood, needs a log source; parked.

**Open leads, in order:**
1. Capture the firmware log correctly (the pool must be drained during the scan): `scripts/fwlog.sh`
   and `scripts/fwlog-decode.py` exist, but the drain step returned empty files; needs a fix.
2. Compare the 3.4 and 6.12 initialisation WMI sequence after the firmware is ready
   (`ath6kl_target_config_wlan_params`, host commands `0xf02b/0xf02a/0xf024/0x3d/0xd/0xf029/0xf019`).
3. Get the actual CM `ath6kl.ko` / its module options and the CM kernel boot log of the Wi-Fi
   (not in the repositories: it would have to come from a CM ROM zip).
4. Check whether events are lost on the host side after the last SDIO interrupt (HTC rx path).

## Downstream reference kernel (CM11, Linux 3.4) - started 2026-10-15
Idea: boot the vendor kernel in RAM with our initramfs and the same firmware, to learn what it does
differently for the Wi-Fi (GPIO, regulators, clocks read from debugfs) instead of guessing.
- Source: `willcast/kernel_tenderloin`, branch `cm-11.0_3.4` (3.4.103, `cyanogenmod_tenderloin_defconfig`),
  cloned in the Docker volume under `ref/kernel-tenderloin-cm11-3.4`. It has the in-tree `ath6kl` of 3.4.
  Other branches: `desktop_3.4`, `desktop_3.0`, `cm-10.2`, `ics`, `kitkat`, `lollipop-5.1`.
- The tree does not build with gcc 12 (`.err` in assembler macros, duplicate symbols): it is built with
  gcc 4.9 in an Ubuntu 16.04 image (`docker/ref34/Dockerfile`). Run `./build.sh ref34` (Git Bash), result
  `out/uImage-ref34`. Config: defconfig + `kernel/ref34.config` (devtmpfs, /dev/mem, forced command line,
  embedded initramfs `out/initramfs-ref34.cpio.gz` built from `initramfs/init-ref34`, Android USB gadget
  with `acm,ecm` set through `/sys/class/android_usb/android0`).
- The board file needs `USB_G_ANDROID` (do not replace it with `g_multi`).
- Boot test (`scripts/cycle.sh out/uImage-ref34`): the image is accepted by the bootloader, but the USB
  gadget never appears and the tablet ends up in webOS (`0830:8072`). No console output available yet
  (no earlyprintk, no RAM console in the board file): the first thing to find out is what the screen
  shows. Unknowns: machine type 3079 (`MACH_TENDERLOIN`) versus what the bootloader passes, ATAGs,
  early crash followed by a watchdog reset.

## Kernel patches (kernel/patches, applied in order by scripts/kernel.sh)
**Renumbered on 2026-10-27**: 19 active patches, contiguous 0001..0019, grouped by subsystem; the relative order of the patches that touch the same
file is unchanged (mmci.c 0001 < 0002 < 0003, ath6kl 0004 < 0005 < 0006, msm-poweroff 0007 < 0008, a2xx_gpu.c 0016 < 0017). Proof that nothing changed:
`scripts/check-patch-series.sh` applies the series (and `kernel/src`) on the pristine 6.12.40 tarball and hashes the 32 touched files: **identical hash
(`dd76fe49...`) before and after**. The generators `scripts/tools/gen-patch-00NN.py` carry the new numbers; those of the removed patches are in
`scripts/tools/removed/`. The names and numbers in `removed/` and `diag/` and all the older sections of this file use the OLD numbers:

| New | Old | Patch |
|---|---|---|
| 0001 | 0001 | `mmci` dummy CMD52 after a CMD53 write (Qualcomm SDCC quirk) |
| 0002 | 0004 | `mmci` clear the stale PROGDONE bit before the dummy CMD52 |
| 0003 | 0006 | `mmci` Qualcomm SDIO interrupt |
| 0004 | 0002 | `ath6kl` SDIO CRC workaround for hw 2.1.1 as the 3.4 driver does |
| 0005 | 0007 | `ath6kl` BT coexistence commands (`ath6kl_core.btcoex_dev` / `btcoex_ant`) |
| 0006 | 0014 | **`ath6kl` legacy `WMI_START_SCAN` with the old firmware (the Wi-Fi fix)** |
| 0007 | 0005 | `msm-poweroff` maps PS_HOLD without requesting the region |
| 0008 | 0012 | `msm-poweroff` writes the Palm "recover" restart reason early (crash -> recovery; also why `poweroff` ends in recovery) |
| 0009 | 0013 | Kconfig/Makefile of the A6 battery driver (`kernel/src/drivers/power/supply/hp_a6_battery.c`) |
| 0010 | 0010 | `tsens` MSM8660 (thresholds written before the sensor is enabled) |
| 0011 | 0011 | `gcc-msm8660` populates its child nodes (TSENS) |
| 0012 | 0017 | Scorpion ACPU clocks (cpufreq), Kconfig/Makefile of `clk-scorpion-acpu.c` |
| 0013 | 0020 | MMCC MSM8660 clocks: GPU (GFX3D, GMEM AXI, IMEM) and camera MCLK (`camclk0`, no halt bit) |
| 0014 | 0016 | PM8058 LPG PWM (backlight), Kconfig/Makefile of `pwm-pm8058-lpg.c` |
| 0015 | 0018 | MDP4 parallel RGB (LCDC) for the MSM8660 (display) |
| 0016 | 0021 | a2xx keeps its clocks on runtime suspend |
| 0017 | 0029 | **a2xx GFX3D power cycle at every hw_init (the GPU fix, 2026-10-26)** |
| 0018 | 0030 | Kconfig/Makefile of the MT9M113 camera sensor driver |
| 0019 | 0031 | Kconfig/Makefile of the LM8502 LED driver |

Removed on 2026-10-26 (`removed/`, no measured effect on the GPU defect, configuration aligned on Jonathan Marek's tested tree): 0022 A22x init like
kgsl, 0025 Scorpion NMRR write-through, 0028 VGT_OUT_DEALLOC_CNTL; earlier GPU experiments 0019, 0023, 0024, 0026, 0027 are there too.
Removed on 2026-10-16 (kept in `kernel/patches/removed/`, they were experiments that were not the cause of the Wi-Fi problem; scan and
connection re-tested without them, 40 pings 0 % loss): 0003 `mmci` debug instrumentation, 0008 probed SSID slots, 0009 no probed SSID
commands for a wildcard scan. `scripts/kernel.sh` restores from the pristine tarball every file touched by the active **and** the
removed patches (bug fixed: files of a removed patch used to stay patched). To drop a patch and test the Wi-Fi: `scripts/wifi-bisect.sh
<patch file> [tag]` (moves it to `removed/`, rebuilds, boots, runs `wifi-selftest` twice). Candidates to re-evaluate later the same way: old
0002, 0004, 0006, 0007 (= new 0004, 0002, 0003, 0005).

## Prior art (found 2026-10-15)
- **Mainline patches**: Ben Wolsieffer, Jan 2022, "Add support for the HP TouchPad" (linux-arm-msm;
  `qcom-apq8060-tenderloin` derived from the dragonboard DTS: eMMC and the headphone-jack UART only).
  Details read from the patch: eMMC `vmmc` = PM8901 L5, `vqmmc` = PM8901 LVS0; PM8058 L10 = 3.05 V,
  L11 = 2.85 V, L19 = 1.8 V, L22 = 1.15 V; **GSBI12 is used as a UART with flow control on the
  headphone jack** (RX gpio117, TX gpio118, gpio58 driven high to route the jack to the UART), which
  gives a real serial console if a 3.5 mm to UART adapter is available. The patch has no USB nodes.
  It is not in 6.12 (no `tenderloin` DTS in `arch/arm/boot/dts/qcom`).
- Herman van Hazendonk, 2024: USB HS PHY compatible for msm8660 (cites the TouchPad), PM8901
  compatible. Our PHY node already uses `qcom,usb-hs-phy-msm8660`.
- postmarketOS: draft port `hp-tenderloin` (pmaports MR 2791) on the downstream 3.4 kernel (Evervolv),
  USB networking, framebuffer, touchscreen, Wi-Fi and headphone UART reported working; not mainline.
  The postmarketOS wiki and GitLab pages are behind a bot protection and could not be read.
  Useful as a source for the touchscreen/Wi-Fi/USB details and downstream kernel sources.

## Decisions
- Build everything in Docker, nothing installed on the host (except `usbipd-win`).
- novacom runs in a container because the Windows Novacom driver has no ARM64 section in its `.inf`.
- RAM boot only until the system is stable; the eMMC and webOS are not touched.
- DTB-driven memory and cmdline (`CONFIG_ARM_ATAG_DTB_COMPAT` disabled), with `clk_ignore_unused`
  and `regulator_ignore_unused` until the display/RPM consumers have drivers.
- Kernel 6.12 LTS from kernel.org, with our DTS injected at build time (no kernel fork yet).

## Next steps
0. Decide where the Linux rootfs lives (RAM initramfs, USB, or a webOS LVM volume on p14) and build
   it with Buildroot (software rendering first, DRM through simpledrm).
1. Use the serial shell to test devices without the screen.
   Optional: a 3.5 mm to UART adapter (1.8 V logic) would give a console on the headphone jack
   (GSBI12, see Prior art).
2. Read the boot log remotely (dmesg) instead of photographing the screen.
3. Verify the eMMC (issue 3) and mount the webOS partitions read-only.
4. Describe Wi-Fi (SDCC4) and audio (WM8958) in the DTS.
5. Decide the display strategy (simple-framebuffer + software rendering vs. a real MDP/LCDC driver).
6. Build the Buildroot rootfs and run Cog (software rendering first).

## How to run a test
See the "Boot a build" section of [README.md](README.md).

### Backlight PWM: tested (2026-10-16)
- **First image (`out/uImage-backlight`) was broken and its measurements are invalid.** The LPG driver refused the 50 us (20 kHz) period of the DT: the shortest hardware period is
  512 * 2 / 19.2 MHz = 53.3 us, so `apply` returned -EINVAL, the PWM stayed disabled (duty 0 in `/sys/kernel/debug/pwm`) and `pwm-backlight` never switched the pin. The screen
  was only lit by what the bootloader had left (the dimming seen at first is not explained and must not be relied on); a later 0 % then left it black. Found with
  `/sys/kernel/debug/pwm` and by driving `/sys/class/pwm/pwmchip0/pwm0` by hand (a 54 us period worked).
- Fix (`out/uImage-backlight2`): when no hardware period fits, use the shortest one (53.3 us, about 18.75 kHz), like the vendor driver (closest period); the duty is a ratio and stays correct.
  Debug tips: `mount -t debugfs none /sys/kernel/debug; grep pwm-0 /sys/kernel/debug/pwm` (must read `requested enabled`, duty > 0).
- Result with the fixed image: the PWM is enabled with the expected duty (60 % = level 80/255 = 15686 ns of 50000, 20 % = 1568 ns, 100 % = 50000 ns, 0 % = disabled).
  Note the interpolated table is a perceptual curve (0 8 32 80 150 255), not linear: 20 % is only 3 % duty.
- Consumption (`scripts/backlight-profile.sh`, tablet on the PC USB port, charger in USB mode 500 mA, battery 20 %, gauge current positive = charging, 12 s settling, 8 samples):

| backlight | battery current | backlight draw vs 0 % |
|---|---|---|
| 100 % | -981 mA | about 560 mA |
| 60 % (default) | -605 mA | about 185 mA |
| 30 % | -478 mA | about 58 mA |
| 10 % | -438 mA | about 18 mA |
| 0 % | -420 mA | 0 |

  The full backlight is about 0.56 A, i.e. as large as the rest of the tablet at idle; going from 100 % to 60 % saves about 0.38 A. The 100 % repeat gave -983 mA (reproducible).
  Visual check by the user of the fixed image (sweep 100, 60, 30, 10, 3, 0 %, then 60 %): the levels look correct. Backlight item of the lighting backlog: done (the LM8502 navi LEDs and the 5 V boost GPIO 102 remain).
- The DC-mode comparison is no longer needed for this question: the earlier "0.5 A at GPIO 25 off" matches the 0.56 A at 100 %.

## USB gadget and cable (2026-10-16)
- With the data cable plugged **after** Linux has booted (or with an adapter on the only port) the gadget stays `not attached` and the PC sees nothing (not even in Windows PnP); rebinding the UDC and the charger USB mode did not help. `tp recovery` over SSH reaches the recovery mode, where the cable works (0830:8071), so cable and port are fine. Working procedure: `tp recovery` (SSH) then `scripts/cycle.sh <image>` with the data cable on the PC.
- Restarting Docker Desktop kills a running Buildroot container (BUILD_EXIT=125): the objects stay in the `hptouchpad-build` volume and `./build.sh rootfs` resumes. The Docker engine returned 500 errors while WebKit was compiling (probably memory pressure): the cause is memory: the Docker VM has 8 GB and WebKit needs about 2 GB per compile job, so 8 jobs made the engine crash twice (BUILD_EXIT=125). Buildroot now runs with 4 jobs (`BR2_JLEVEL=4`, `BR_JOBS`), the WebKit compilation restarted from the beginning (1/5867).

## Second TouchPad (analysis, 2026-10-28)
- A second TouchPad (32 GB model, not ours) does not boot and does not show anything on the screen. It enters the `bootie` DFU mode (`0830:8070`, Home + Power + Volume Up)
  but never the recovery `8071` nor webOS. Nothing was written to its flash; everything below was read.
- Access without a Linux host: tpdebrick (https://github.com/Herrie82/tpdebrick, files `moboot-dfu-v004`, `TPToolbox-Headless-v004`, cloned to `out/tpdebrick-repo`, not versioned):
  DFU `dfu-util -d 0830:8070 -R -D moboot-dfu-v004` (becomes fastboot `18d1:d00d`, `product MOBOOT_MSM8660_TOPAZ`), then `fastboot flash bootmem <multi-image uImage>` (RAM only, a few seconds).
  The toolbox gadget (`g_cdc`: serial + ECM, `192.168.7.7`) cannot be used from WSL/Windows (no ECM driver, `usbipd` attach hangs). **`out/tpdebrick/TPToolbox-serial`** = the same
  image with `/etc/init.d/misc` changed to `insmod g_serial.ko` (ramdisk is a 64 MiB ext2 inside a gzip, repacked with `debugfs`): the tablet then appears as `0525:a4a7`
  "USB Serial Device (COM3)" in Windows, login `root` without password on `ttyGS0` (115200). Our own uImage (single or multi-image, 19 MB) sent by `bootmem` never
  started (no USB after it, black screen): that loader does not bring the hardware up like `bootie` does; the toolbox kernel (HP 2.6.35.7+) does.
- Read results: partition table identical to `mbr32/ebr32` of tpdebrick (p14 = 30 504 960 KiB, LVM2 label present); the bootloaders in p2, p3, p5, p6, p7, p8, p9 have the exact MD5
  of tpdebrick's `sbl1, sbl2, rpm, sbl3, emmc_appsboot, bootie-topaz305, tz`; `/boot` (p13) holds a moboot installed on 2014-09-07 (`uImage -> uImage.moboot`, default CyanogenMod,
  recovery ClockworkMod, plus the webOS kernel): this unit is **not** unmodified. The tpdebrick partition roles and the 16 GB tables also match our unit exactly.
- A6 (battery/power microcontroller) answers, firmware 2.13.25, firmware checksum valid (`cksum1 == cksum2`), but the battery is **not detected**: `present=0`, voltage/current/temperature/
  capacity 0, "Starting battery authentication..." then zeros, an A6 I2C write error; the MAX8903 charger sees USB at 500 mA. Likely a dead/disconnected battery pack (hardware), not
  software: flashing the A6 or the bootloaders is not indicated by these readings.
- The toolbox kernel is an Android one (Cypress TrueTouch, maXTouch probes fail, no HP touch driver): it cannot read the CY8CTMA395 touch controller by itself.
- A6 check (virgin 32 GB unit, 2026-10-28): `PmA6Updater -g` -> 2.13.25 (0x020d0019); `PmA6Updater -x -d 0 a6_firmware.txt.00` (tpdebrick's file, "verify only, does not flash")
  -> "Firmware verification complete", rc 0: the installed A6 firmware matches the file. Binary and firmware were copied to the tablet RAM through the serial console (base64, MD5 checked), nothing flashed.
  Together with the valid checksum and the unchanged boot chain, the missing battery (`present=0`) is a hardware matter (pack or connector), not firmware.
- A6 flash (2026-10-28, user request, outside the project scope): `PmA6Updater -f -d 0 -v a6_firmware.txt.00` on the virgin 32 GB unit: "Firmware update complete", rc 0, kernel log "A6 error_count: 0",
  checksum master == current (0xe0e7), version still 2.13.25, a second `-x` verification complete. Battery readings unchanged afterwards (`present=0`, all zeros): the flash did not make the
  A6 see a battery.

- Charger forcing (2026-10-28, virgin 32 GB unit, toolbox serial booted again: DFU `moboot-dfu-v004` -> `fastboot flash bootmem TPToolbox-serial`, COM3, login `root`): the HP 2.6.35
  `max8903-charger` driver had left **CEN (GPIO 41) low = charging disabled**, DCM low (DC mode), ISET 0/0, USUS low, IUSB low; DOK_N low (input valid), CHG_N high, FLT_N high;
  battery `present=0`. GPIO 41 is exported in `/sys/class/gpio`: `echo 1 > /sys/class/gpio/gpio41/value` -> a few seconds later the A6 reports **`present=1`, 2.362 V**,
  temperature 29.0 C, health Good, status "Not charging", capacity 0. Over 3 minutes the voltage rose 2.3619 -> 2.3717 V while CHG_N stayed high (no fast charge; trickle or
  relaxation, not known). The cell is deeply discharged (below the usual 2.5-3.0 V Li-ion cut-off): the pack is reachable, the earlier "no battery" was the cell under the
  protection threshold, not a broken connector.
  Same session, 07:12 (uptime 6522 s, no reboot): 2.4302 V (+58.5 mV in 104 min, about 0.56 mV/min), CHG_N still high, CEN still high, DCM low, 29.0 C, capacity 0.
  08:09 (uptime 9962 s): 2.4449 V (+14.6 mV in 57 min, about 0.26 mV/min: the rise slows down, more like a relaxation curve than a charge), CHG_N high, 29.0 C, capacity 0.
  Switch to the USB mode (DCM 1, IUSB 1 = 500 mA, USUS 0, CEN 1) right after 08:09: the tablet dropped off the USB at once (serial port closed, no Palm/fastboot/gadget device
  left): most likely a brown-out, the 500 mA USB-mode input limit is below the toolbox load and a 2.44 V cell cannot make up the difference. The DC mode (DCM 0, ISET 0/0) kept it
  alive for hours. Do not use the USB mode on this unit while the cell is this low.
  Restarted 08:18 (DFU -> moboot -> toolbox): same starting state (CEN low, `present=0`). CEN forced high, DC mode kept: `present=1`, **2.4449 V, the same value as at 08:09**:
  nothing was gained during the off period (LEDs blinking on the cable). CHG_N high, 29-30 C.
  Clock note: the times above are the tablet clock (toolbox, RTC counting from 1970-01-01, it kept counting across the restart), not the real time. Offset measured
  at 17:27:25 PC time = 08:22:50 tablet time (+9 h 04 min 35 s): 05:28 -> about 14:33, 07:12 -> 16:17, 08:09 -> 17:14, restart 08:18 -> 17:23. The durations between readings are valid.
  Result (2026-10-29, user report): left overnight in the DC mode with CEN forced high, the tablet recovered. The procedure and the images are in `debrick/README.md`.

### Alpine on the second TouchPad (2026-10-29)
- `out/rootfs-alpine.ext4` copied under webOS with `novacom put` (USB, 2 min, MD5 `8b25ce29...989b` checked on the tablet) to `/media/internal/touchpad/`, plus
  `touchpad/rootfile`. The media volume has 644 MB free before the copy (owner's files kept).
- The initramfs had the media LV offset of the first tablet hardcoded (1116733440); on this unit the LV starts at extent 133 = **1124139008**. `initramfs/init`
  now computes it from the newest LVM metadata copy (`media_offset`: highest seqno, `pe_start + extent * extent_size`), first tablet's value as fallback;
  checked on both metadata sets. Image: `out/uImage-lvm` (`uImage-clean` + this init, KMS DTB: `DTB_NAME=qcom-apq8060-hp-touchpad-kms ./build.sh uimage`).
- Result: Alpine 3.21.9 on `/dev/loop1`, Cog on the GPU, `ts_srv` running. Reached from webOS without buttons: `scripts/webos-recover.sh`, then `scripts/cycle.sh`.
- This unit's gadget is a new USB device for Windows: `usbipd bind --busid <id>` needs an administrator once, so `tablet-cmd.sh` (WSL) does not work yet;
  its serial console answers directly on **COM6** (PowerShell `System.IO.Ports.SerialPort`). Bound by the user later the same day: `tablet-cmd.sh` works.
- Wi-Fi: `2sWifi`/`2sWifi_5G` are out of range here; connected to **`Crossover-IOT_5G`** (5180 MHz, -74 dBm), DHCP **192.168.1.107**, SSH and internet OK
  (`scripts/tablet-ssh.sh -i 192.168.1.107`). `tablet-wifi.sh` needs `wifi-wpa`/`wifi-dhcp` on the media volume: copied there for this unit, and
  `scripts/alpine-rootfs.sh` now also puts them in `/usr/local/bin` of the image. The key stays in RAM: run `tablet-wifi.sh` again after a boot.

- **Touch edges on this unit (2026-10-29)**: the edges were unreachable under our Linux (fine under webOS) because `ts_srv` hard-coded the
  live columns of the first unit (7-33, 36). Now all 40 columns are used by default; `TS_SRV_COLUMNS` (from `/mnt/media/touchpad/ts_columns`,
  read by `S80touch`) restricts them. Confirmed by the user on the second unit: the edges respond. **The first unit needs
  `touchpad/ts_columns` = `7-33,36` before its next boot with this `ts_srv`** (ghost touches otherwise).

### Touch controller comparison with a virgin TouchPad (2026-10-29)
- Unit B: second TouchPad, 32 GB, webOS 3.0.5 (same build as ours) plus moboot/CyanogenMod; webOS developer mode was enabled by the user to get novacom. Read-only dump with
  `scripts/tools/ctp-state.sh` (catnip getters, hidd stopped and restarted, controller suspended afterwards): `data/webos-touch/tablet2-32gb/` (log + webOS touch config files).
- **Identical on both units**: `checksum e34e`, `device_config 00 00 00 07`, `/lib/firmware/cy8ctma395.ver` 15 0 0, `HidTouchpanel_evt1.xml`, `catnip ver -a` 15 0 0, 30 rows,
  mode idle, subs 4, cycles 4, freq 160, gain 2x, noise 10, timer 0, cmn 1, jitter 1; the IDAC tables have the same scale (about 560-1030). So the firmware, its configuration and the
  calibration parameters are not what differs.
- **Different**: the raw/baseline buffer (40 columns x 30 rows). Unit B: 0 cells below 20 out of 1200, every column around 188-202. Our 16 GB unit (before the recalibration): 373 cells
  at 0, whole columns 0, 1, 2, 4, 5 and 33-39 dead (about 5 or less), the others 183-231. This confirms the dead-column defect of our unit is in the sensing hardware (sensor lines or
  one of the Indium chips), not in the firmware version, the configuration or the calibration of the controller.
- Not obtained on unit B: `catnip id` (JTAG id) failed with "sysfs: error 5 during read" and the sysfs `id`/`checksum`/`device_config` reads returned I/O errors after hidd was stopped
  (the earlier plain reads gave `e34e`, `00 00 00 07`, `id` empty); I do not know why, so no claim about the silicon id of unit B.
- Under our kernel `ctp-test` on unit B: the 7 I2C configuration writes at 0x67 are accepted but the UART (`ttyMSM2`) stays silent (0 bytes in 4 and 8 s), unlike on our unit; not explained
  (webOS gets its raw buffer through the I2C path of catnip, `--i2c-only` mode, which was not tried under our kernel).
- Firmware and touch tools copied from the 32 GB unit's webOS 3.0.5 into `firmware/webos-touch/tablet2-32gb/` (git-ignored, HP/Palm files, SHA256SUMS inside): `cy8ctma395.fw/.hex`,
  `cy8ctma395_hssp_bridge.fw/.hex`, `cy8ctma375.hex`, `cy8ctma395.ver`, `a6_firmware.txt.00/.01`, `PmTpUpdater`, `catnip`, `PmA6Updater`, `libhidtouchpanel.so`, `libpmmultitouch.so`.
  `a6_firmware.txt.00` is byte-identical to the one of tpdebrick. The same 13 files pulled from our 16 GB unit (`firmware/webos-touch/tablet1-16gb/`, webOS 3.0.5 build 86 as well) have **identical SHA-256** (all 13): the firmware files, the updater tools and the touch libraries are the same on both units; together with the identical controller checksum (`e34e`) and `device_config`, firmware and its files are excluded as the cause of the dead columns.
- Live test on the 16 GB unit (2026-10-29, webOS, hidd stopped, `ctp-webos init 120`, capture `out/live-left.raw` 2.8 MB, 2148 scans): the user pressed, slid a finger along and flexed
  the edge on the camera side (landscape, camera on the left). Only **column 36** responded (rows 4-22, values about 8-12 and varying with the touch; columns 37 and 39 weakly on rows 0-1);
  columns 33, 34, 35, 38 and 0-5 stayed at 0-2 for the whole capture, and nothing in the middle columns moved (max 3-4). No dead column came back during pressure or flexing
  (no sign of an intermittent contact on that edge); the other edge (columns 0-6 side) was not tested. `catnip raw` and `catnip scan --count 1` return the same static buffer on every call
  (460 and 3455 identical samples): they cannot be used for live measurements, use `ctp-webos` (UART) instead. Careful: `catnip scan` without `--count` never returns.


### Smaller kernel image for /boot (2026-10-29)
- `/boot` of the 32 GB unit (read-only): `uImage -> uImage.moboot`, `uImage.webOS -> uImage-2.6.35-palm-tenderloin`, `uImage.CyanogenMod` (CM-11.0.0-RC0, 3.9 MB),
  `uImage.ClockworkMod` (5.4 MB), `moboot.default` = CyanogenMod, `android.default.recovery`, splash `.tga` files; **8.6 MiB free** of 27 MiB. Our uImage was 19.5 MB.
- Cause: `multi_v7_defconfig` base (about 13 MB of kernel for every ARMv7 SoC) plus a 6.5 MB gzip initramfs inside a gzip kernel.
- Change: `scripts/kernel.sh` now starts from **`qcom_defconfig`** (`KERNEL_BASE=multi_v7_defconfig` gives the old build, own object directory),
  `CONFIG_KERNEL_XZ`, initramfs embedded uncompressed; fragment keeps `PREEMPT_NONE`, NEON, IPv6, autofs, tmpfs ACL/xattr, cpufreq stats of multi_v7, disables
  the PM8058 keypad (probe failed, -22) and `dummy0`. `scripts/initramfs.sh`: Wi-Fi/Bluetooth/SSH tools only with `INITRAMFS_TOOLS=full` (busybox, init, firmware,
  `tp`/`tp-powerd`/`charge-mode`/`cpu-state` stay; the Alpine rootfs has its own tools). `build.ps1`/`build.sh` pass `DTB_NAME`, `KERNEL_BASE`, `INITRAMFS_TOOLS`.
- Result: **`out/uImage-small` = 7.2 MB** (initramfs 1.07 MB gz). Old image kept as `out/uImage-multiv7-ref`.
- RAM boot on the 32 GB unit: Alpine on `/dev/loop1`, Cog "GPU (freedreno)", `ts_srv`, `tpd`, Wi-Fi (`wlan0`), Bluetooth `hci0`, backlight, Home LEDs, battery/charger,
  cpufreq. Bound drivers compared with the multi_v7 kernel: only `serial8250` and the PS/2 `atkbd`/`psmouse` gone (no such hardware); new `rtc-pm8xxx`
  (the PM8058 RTC is now available), `pm8xxx-vib`, `alarmtimer`, `snd-soc-dummy`. Not rechecked: camera capture, touch by hand, suspend/charge behaviour over time.
- **Installed and booted from flash (2026-10-29, 32 GB unit)**: backup of p12/p13 in `out/backup/tablet2/` (MD5 checked against the partitions), `out/uImage-small`
  copied from our Linux to `/boot/uImage.Linux` (MD5 checked, 1.5 MiB left free), `moboot.default` unchanged (CyanogenMod). Power off (hold 10 s), power on,
  "Linux" chosen in the moboot menu: **our kernel starts from moboot** (user confirmed; build #2, DTB command line, Alpine on `/dev/loop1`, Cog "GPU (freedreno)").
  This answers the open question of `docs/FLASHING.md`: moboot starts our single-image uImage (kernel + built-in initramfs + appended DTB) from `/boot`.

## Browser smoothness measurement (2026-10-29)
- Stack on the tablet: WPE WebKit 2.40.5, Cog 0.18.3, libwpe 1.14.2 / wpebackend-fdo 1.14.2, Mesa 24.2.8. The WebProcesses hold `renderD128` (freedreno GPU path confirmed); Cog uses `card1`.
- The user reported scrolling is not smooth. 45 s per-thread sampling while scrolling (`top -H` every 3 s): the CPU is **mostly idle** (70-85 % idle, two peaks at 50 % usr) and
  schedutil keeps the cores at **432-486 MHz** most of the time, only short jumps to 756 MHz. The busy threads are WebKit's `PaintingThread` and `ThreadedCompositor`.
  So the CPU cap (756 MHz) is not reached: the first suspect is the governor reacting too late, not raw CPU power.
- Three WPEWebProcess are alive (280-337 MB virtual, 50-135 MB RSS each): one more than expected, to check (settings panel page kept alive?). `tp-panel` (shell loop) shows in the samples.
- Test in progress: governor set to `performance` by hand (both cores 756 MHz, not persistent) for the user to compare the feel.
- **Correction (same day):** the user's complaint is responsiveness (touch, page changes), not scrolling; the `performance` governor changed nothing (reverted to schedutil).
  In-page benchmark (`/usr/share/tpd/www/bench.html` + `cgi-bin/blog`, test files left on the tablet only): 5M-iteration JS loop **~2970 ms**, 200k objects + sort ~1300 ms,
  2000-row DOM build + layout ~1560 ms, `requestAnimationFrame` median 33 ms (30 fps), click to next frame 100-125 ms (touch event delivered in ~10 ms).
- **Cause found: JavaScriptCore runs without any JIT.** Alpine's `wpewebkit-2.40.5` armv7 has no `useBaselineJIT`/`useDFGJIT` options at all (JIT compiled out) and
  `JSC_useJIT=false` gives the same times. An interpreted JSC is tens of times slower than a JIT on a 756 MHz Scorpion: JS-heavy pages (the user's dashboard) feel slow,
  light pages are fine (confirmed by the user). Next: a WPE WebKit build with the ARMv7 (Thumb-2) JIT enabled, or a newer WPE if its 32-bit ARM build still has a JIT (to check).

## CPU above 756 MHz: what the vendor kernels do, and the work left (2026-10-29, read only)
Sources: HP ICS kernel `hp-kernel-tenderloin-ics/arch/arm/mach-msm/acpuclock-8x60.c` (the webOS lineage, PVS tables) and CM11 3.4 (`acpuclock-8x60.c`, `spm.c`,
`saw-regulator.c`, `board-msm8x60.c`), were in the `hptouchpad-build` volume under `/build/ref` (volume deleted on 2026-11-02 with the old build chain: fetch them again before the next SAW step, e.g. `willcast/kernel_tenderloin` for CM11 3.4).
- **Table to use: HP `acpu_freq_tbl_1188mhz`.** Our QFPROM PTE efuse (`0x007000C0`) reads 0 -> speed bin 0 -> this table (the HP code picks it for PVS "slow"/0 and for any
  bin other than 1). It is the most conservative one (highest voltages). vdd_sc in mV: 432 887.5, 486 912.5, 540 925, 594 937.5, 648 950, 702 975, **756 1000**, 810 1012.5,
  864 1037.5, 918 1062.5, 972 1087.5, 1026 1125, 1080 1137.5, 1134 1162.5, **1188 1187.5** (HP stops the default plan there; 1242+ are overclock rows).
- **Do not use the CM11 `acpu_freq_tbl_oc`:** lower voltages (1188 at 1100 mV) and, from the 1134 row up, an L value one step too high (row "1188000" has L=0x17 = 1242 MHz).
  Always derive the frequency from L (2 x 27 MHz x L).
- **L2 levels** (HP = CM, `l2_freq_tbl_v2`): the L2 follows the core (L2(7) = 756 MHz for a 756 MHz core); vdd_dig/vdd_mem 1.0/1.1 V up to 648 MHz, 1.1/1.1 V up to
  1026 MHz, 1.1/1.2 V at 1080-1134, **1.2/1.2 V at 1188**; AXI bandwidth level 1 (<= 864), 2 (918-1134), 3 (1188). Today our L2 stays at 432 MHz.
- **Order of a raise** (`acpuclk_8x60_set_rate`): AVS off; vdd_mem = max(vdd_sc, L2 vdd_mem) via RPM PM8058 S0; vdd_dig = max(L2 vdd_dig, 1.1 V if core or L2 > 594 MHz
  else 1.0 V) via RPM PM8058 S1; then vdd_sc via the core's SAW; switch the core SCPLL; vote the L2 (max of both cores) and switch it; bus bandwidth vote. A lowering does the
  reverse (frequencies first, then vdd_sc, vdd_dig, vdd_mem). AVS (`avsdscr` 0x03006000) is optional: static voltages are what the table gives; leave AVS off.
- **SAW (per core, vdd_sc = PM8901 S0/S1 FTSMPS):** init values from the board file (`msm_spm_data`, SoC v2+): AVS_CTL 0x586020FF, CFG 0x1C, SPM_CTL 0x68, SLP_TMR_DLY
  0x0C0CFFFF, WAKE_TMR_DLY 0x78780FFF, SLP_CLK_EN 0x01 (core 0) / 0x13 (core 1), HSFS_PRECLMP 0x07, POSTCLMP 0, CLMP_EN 0x01, RST_EN 0, MPM_CFG 0, vctl timeout 50 us;
  board quirk: with PMM8160 (platform version 1.2+) core 1 CFG bits 11:8 = 1. Register offsets: AVS_CTL 0x04, VCTL 0x08, STS 0x0C, CFG 0x10, SPM_CTL 0x14, timers 0x18/0x1C,
  PMIC_CTL 0x20, SLP_CLK_EN 0x24, PRECLMP 0x28, POSTCLMP 0x2C, CLMP 0x30, RST 0x34, MPM 0x38. Set voltage = write VCTL[7:0] = code, poll STS until PMIC state (bits 21:20) is
  idle, check STS[17:10] == code. Code (band 2) = 0x80 | (uV - 700000) / 12500; 1187.5 mV = 0xA7, 1000 mV = 0x98. Range limits of the vendor regulator: 800-1325 mV, slew 1.25 mV/us.
- **Correction of the earlier note:** PM8058 S0/S1 are **not** in our DT yet (only L6, L10, L11, L19, S3, LVS0); the mainline `qcom,rpm-pm8058-regulators` binding has them.
- **Work left, in order:** (1) DT: PM8058 S0/S1 RPM regulators + SAW nodes (`0x02042000`, `0x02052000`); (2) a small `saw-regulator` driver for msm8660 (init + VCTL/STS,
  read back STS before anything: first test = re-write the current boot voltage); (3) extend `clk-scorpion-acpu.c` with the L2 SCPLL and the L2 vote; (4) OPP table with
  `opp-microvolt` and the RPM supplies (a small cpufreq glue if cpufreq-dt cannot order three supplies); (5) test one step at a time from 810 MHz, read TSENS, up to 1188.
  Open point: the real vdd_sc today is unknown (SAW never initialised by the bootloader); reading the PM8901 S0/S1 control registers over SSBI would tell it, read only.
- **Step 1 written and built (not booted yet):** driver `kernel/src/drivers/regulator/msm8660-saw-regulator.c` (patch `0020-regulator-add-msm8660-saw.patch`,
  `CONFIG_REGULATOR_MSM8660_SAW=y`), DT nodes `saw0_vreg`/`saw1_vreg` (`qcom,msm8660-saw-regulator`, `qcom,cpu-index`, 950-1187.5 mV). The probe only reads and logs the
  registers (`boot state:` lines in dmesg); nothing is written at boot. By hand, after `mount -t debugfs none /sys/kernel/debug`, in `/sys/kernel/debug/msm8660-saw/saw<n>/`:
  `regs` (dump), `echo 1 > init` (vendor SPM init values), `echo <uV> > vctl_uv` (write the code, wait for the PMIC, check its answer, dmesg shows the result).
  First test plan: dump both; init SAW0; write 1000000 (HP voltage for 756 MHz) on SAW0 and check STS; run a CPU load on core 0; then the same on SAW1.
- PM8058 S0/S1 (vdd_mem/vdd_dig) are left out of the DT on purpose for now: the RPM regulator driver does not read the current RPM value, so a min/max range could make
  the regulator core push the rail to its minimum at boot. To add with care at step 3/4 (with exact boot values or `regulator-always-on` at a known safe voltage).
  Image: `out/uImage` (KMS DTB), built 2026-10-29 19:21.
- **First boot with the SAW driver (2026-10-29, 32 GB unit, `out/uImage` = `out/uImage-saw-noroot`, same md5):**
  - The boot loops seen before ("Linux shell, then reboot when the rootfs loads") also happened with `out/uImage-small` (no SAW driver). Most likely the low-battery shutdown of
    `tp-powerd` (`poweroff -f` at the critical level, discharging on the PC port): once charged (26 %, charging) the same image boots Alpine and stays up. Not proven by a log.
  - **On this unit the SAW is not in its reset state:** VCTL = STS data = `0x8b` = band 2, **837.5 mV** on both cores at 756 MHz (SAW0 AVS_CTL `0x10241bff`, PMIC_CTL `0x00898b20`,
    SPM_CTL 0). The earlier note (invalid codes `0x35`/`0x13`) was taken on the first unit; to recheck there. 837.5 mV is well below the HP table (1000 mV at 756 MHz).
  - **The driver did write at boot**, contrary to its intent: with a DT range of 950-1187.5 mV the regulator core found 837.5 mV and set the minimum ("vdd_sc0: Bringing
    837500uV into 950000-950000uV"). The write worked **without any SPM init**: STS data = `0x94`, PMIC idle, `vctl_uv` reads 950000 on both cores; system stable (7+ min,
    Cog, Wi-Fi, 26 C). So VCTL -> PMIC works as the vendor code expects. DT fixed afterwards: minimum 800 mV, so the core keeps the boot voltage (not rebuilt/booted yet).
- **810 MHz test (2026-10-29, `out/uImage-810`, RAM boot):** cpu-supply = SAW regulators + HP opp-microvolt, 810 MHz as a boost OPP. Voltages follow the table exactly
  (432 -> 887.5 mV, 594 -> 937.5, 756 -> 1000, 810 -> 1012.5, per core); 810 MHz on both cores under a 60 s load: no crash; a shell loop is ~6.5 % faster than at 756.
  **But the display then filled with blue lines** (user report): MDP4 primary underruns (`errors: 00000100`), first at t = 80 s (during the 810 MHz load test), then a steady
  ~2/s whatever the governor/frequency (performance 756, schedutil 432: 22 per 10 s each) after going back to 756 MHz and boost off. Cause not found.
  Suspects: (1) the higher core voltages/frequency changes disturbing the bus or the EBI (vdd_mem/vdd_dig are not voted: an L2/bus vote may be needed even at 756);
  (2) **both SAWs have the same PMIC_CTL `0x00898b20`**: if it encodes the PMIC register address, both SAWs may drive the same SMPS, so per-core voltages would
  overwrite each other (a core could then run below its OPP voltage) - to check against the vendor code (PMM8160 quirk in board-msm8x60.c touches core 1 CFG) before any
  further test; (3) the known GPU power-cycle underruns coming back. Next test: same image without the cpu-supply (voltages fixed at 1000 mV), count underruns.
- Back on the reference kernel (`out/uImage-small`, RAM boot after `tp recovery`): **0 MDP4 underrun over 90 s** (the 810 image had its first at 80 s). So the blue
  lines come from the cpufreq/SAW change, not from a lasting hardware state. The SAW keeps the last code written across a RAM reboot (boot state of the next kernel).

## New build chain and tests on the second tablet (2026-10-30 to 2026-11-02)
Plan, measurements and every detail: `docs/BUILD-CLEANUP.md`. Short version:
- **One multi-stage `Dockerfile`, all Alpine** (Alpine pinned by digest), entry point `./build.sh [target]` (`all`, `kernel`, `initramfs`, `rootfs`, `tp`, `ts-srv`, `bccmd`, `cog`,
  `release`, `menuconfig`, `shell`; results in `out/<target>/`, release in `out/release/<date>-<commit>/`). No nested containers, no Debian, the kernel is built with Alpine's
  `arm-none-eabi-gcc` 14.2 (fresh full build 603 s, rebuild after a change about 1 minute, cache mount). Removed: `docker/Dockerfile`, `docker/alpine-rootfs`, `docker/ref34`,
  `scripts/ref34.sh`, `kernel/ref34.config`, `initramfs/init-ref34` (the 3.4 reference kernel experiment is in the git history). `scripts/build-tp.sh`, `build-ts-srv.sh`,
  `build-cog-alpine.sh`, `alpine-rootfs.sh` only call `./build.sh`; the stage scripts moved to `scripts/build/`.
- **Image changes**: initramfs on Alpine packages (no `fw-2.bin`, `devmem` is a mode of `tp` plus a 21 KB standalone, `setsid -c`, no `telnetd`); `bccmd` built from BlueZ 5.55 (musl,
  static; `bt-power` loads the 32 PSR keys, `hci0` up); user `touchpad` (password `alpine`, `doas` to root, SSH password login), root key only without `RELEASE=1`; `cpu-state` added
  to the rootfs (it was never there). The Wi-Fi board data: `BOARD_DATA=upstream` (linux-firmware's generic file) by default; each tablet has its own in the webOS tokens
  (`WIFICAL`, MAC `WIFIoADDR`), reading it at boot is a task of its own.
- **Bugs found by building a clean commit**: `wake-gpios` in the wrong DTS node (did not compile); `kernel.sh` lost the "no initramfs compression" choice on a fresh object
  directory; **the eMMC and the Wi-Fi controller probed in a random order** (`mmcblk0`/`mmcblk1`, one boot out of two of the new images ended in the initramfs shell): `mmc0`/`mmc1`
  aliases in the DTS.
- **Installed on the second tablet (2026-10-30)**: `/boot/uImage.Linux` = the all-Alpine kernel (md5 `3578f840dfc01c21b59da2d8800438b6`; previous kernels in `out/backup/tablet2/`), root
  filesystem replaced in place (copied from webOS with `novacom put`, md5 checked) + `cpu-state`. Tested in RAM: boot, Wi-Fi, GPU microcode, Bluetooth, SSH (password and key), `doas`,
  `tp` and the panel's HTTP API. Wi-Fi throughput at -72 dBm does not tell the three board data apart (spread between runs as large as between files). **Not tested**: cold boot
  from moboot, the picture on the screen with this kernel, suspend.
- Test cycle: `scripts/webos-recover.sh` (webOS to recovery without buttons) then `novacom boot mem:// < out/kernel/uImage`, or `scripts/cycle.sh out/kernel/uImage` from our Linux.
- **Parked on 2026-11-02:** the SAW driver, its patch and the DT/clock/config changes moved to `kernel/wip/saw-cpufreq/` (README there; `tree.diff` restores the
  tree changes). The default build is back to 756 MHz without voltage control; `scripts/build/kernel.sh` restores the files touched by `kernel/wip/*/*.patch`.
- **PMIC_CTL checked (2026-11-04):** CM11 3.4 sources fetched again into `_work/cm11-3.4` (sparse clone of `github.com/willcast/kernel_tenderloin`, branch
  `cm-11.0_3.4`; `_work/` is git-ignored). `spm.c` `msm_spm_set_pmic_ctl()`: PMIC_CTL = `(mid_vlevel << 16) | (awake_vlevel << 8) | sleep_vlevel`. So `0x00898b20`
  = mid 0x89, awake 0x8b (837.5 mV), sleep 0x20 (the board file's `collapse_vlevel`): voltage levels for the SPM low-power modes, **not a PMIC address**. Identical on
  both cores only because the bootloader set both the same; the board file has one regulator per core (`8901_s0`/`8901_s1`). Suspect (2) of the 810 MHz test is ruled out.
  Note for later: the SPM restores the awake level from PMIC_CTL when it leaves a low-power mode, so the vendor code rewrites PMIC_CTL with the current awake level
  each time (`msm_spm_set_low_power_mode`); our driver must do the same once the SPM sleep modes are used (today SPM_CTL = 0, SPM off).
  Remaining suspects for the blue lines: the missing vdd_mem/vdd_dig votes, L2 and bus bandwidth (test 5), or the voltage changes themselves (tests 2-3).
- **What the vendor does besides vdd_sc (CM11 `acpuclock-8x60.c`, read 2026-11-04) and how it maps to mainline:**
  - **vdd_mem / vdd_dig:** `rpm_vreg_set_voltage(RPM_VREG_ID_PM8058_S0 / _S1, voter = cpu)`, raised before vdd_sc and lowered after. Mainline: `qcom,rpm-pm8058-regulators`
    nodes `s0`/`s1` + `regulator_set_voltage()`. Open: their current RPM value (the mainline driver does not read it back), so the DT range must not push them down at boot.
  - **L2:** own SCPLL (`0x00903400`, same programming as the cores) and source select at GCC + 0x38 (`SPSS_L2_CLK_SEL`); the L2 runs at the highest vote of the two cores
    (`compute_l2_speed`), switched after the core (`set_l2_speed`). Mainline: a third clock in `clk-scorpion-acpu.c` (L2 = max of the core rates, like the vendor table).
  - **Bus:** `msm_bus` vote AMPSS_M0 -> EBI_CH0, instantaneous bandwidth by level (0: 824 MB/s ~ 103 MHz bus, 1: 1336 ~ 167, 2: 2008 ~ 251, 3: 2480 ~ 310, 4: 3200 ~ 360),
    active only; level 1 up to 864 MHz, 2 for 918-1134, 3 at 1188. Mainline has no msm8660 interconnect; the nearest equivalent is a rate on the RPM clocks `RPM_EBI1_CLK` /
    `RPM_APPS_FABRIC_CLK` (`clk-rpm`, already used by the GPU node).
  - **Link with the blue lines (hypothesis, not tested):** only the GPU node holds the RPM fabric/EBI1 clocks, and only while it is powered (66 ms autosuspend); the MDP4 has
    fixed clocks and **no bus vote at all**. The DT comment of the GPU node already says that without these clocks "the fabrics stay at their idle rates and the MDP4 underruns".
    More CPU memory traffic at a higher clock could starve the scan-out the same way. Cheap test before any voltage work: give the MDP4 node (or the CPU) a permanent
    `RPM_EBI1_CLK`/fabric vote and rerun `out/uImage-810`'s 810 MHz load test, counting underruns.
- **Going above 1 GHz in the vendor code (CM11 `acpuclock-8x60.c` + `cyanogenmod_tenderloin_defconfig`, read 2026-11-04):**
  - Same switch at every step: SCPLL to SCPLL by **complex slew** (`scpll_change_freq`: `(L << 3) | 7` to FSM_CTL_EXT, NORMAL to CTL, wait for L in CTL then STATUS bit 0
    clear) - exactly what `clk-scorpion-acpu.c` already does. No intermediate step through PLL8 for big jumps.
  - The SCPLL must be **calibrated up to the highest L used**: the vendor recalibrates each PLL at probe (`scpll_init`: CAL = max L << 24 | 8 << 16, FULL_CAL, wait for the LUT
    entry then STATUS bit 1). Our bootloader already calibrated L = 8..28 (432..1512 MHz), enough for 1188 (L = 22); no recalibration needed below 1512.
  - What changes above ~1 GHz is only the supplies and the L2: vdd_sc per the table (1026 1125 mV ... 1188 1187.5 mV in the HP table), vdd_dig 1.1 V (any core or L2 above 594),
    **vdd_mem 1.2 V from L2 1080 MHz**, **vdd_dig 1.2 V at L2 1188**, bus level 2 (918-1134) then 3 (1188). Order: raise vdd_mem, vdd_dig, vdd_sc; switch the core; switch the L2;
    bus vote; on the way down the reverse.
  - **AVS is off in CM** (`# CONFIG_MSM_AVS_HW is not set`): static table voltages only, as we do.
  - CM defaults: `CONFIG_MSM_CPU_FREQ_MAX=1188000`, governor `performance`; at probe the driver jumps straight to its "1674000" row (tenderloin `#ifdef`, a boot speed-up),
    then cpufreq caps at 1188 MHz. Its rows from 1134 up have L one step too high (see above): its "1188000" really runs at 1242 MHz with 1.1 V. HP stays the reference.
- **Fixed or adaptive voltage elsewhere (2026-11-04):** webOS (saved `data/webos/_proc_config.gz`): `# CONFIG_MSM_AVS_HW is not set`, default governor `performance`,
  `CPU_IDLE` with the ladder/menu governors (SPM sleep modes used at idle), `THERMAL_PM8901`; dmesg "Max ACPU freq: 1188000 KHz". CM11 3.4: AVS off too. **So both use
  the fixed table voltages**; the AVS settings in the HP tables are never applied. Our plan (static HP voltages, AVS off) matches the vendor builds.

### 810 MHz retest, 2026-11-04 (out/uImage-810, battery 20 %)

- 60 s load on both cores at 810 MHz with the GPU forced on (`power/control=on`): 0 `mdp4_irq_error`.
- Same 60 s with GPU autosuspend (`auto`): 0 errors. The 2026-10 blue lines did not come back.
- One warning when boost was enabled: `saw0 code 99: PMIC answered 8f`. Read afterwards, both SAW
  STS data fields hold 0x99 (1012.5 mV): the 50 us poll is shorter than the PMIC slew from 0x8b.
  The driver must wait longer (vendor waits for the slew) before treating it as a failure.
- ssh over wlan0 (DHCP now gives 192.168.1.113) hung without running commands; the USB serial
  gadget (`scripts/tablet-cmd.sh`) worked.
- The user watched the screen during both runs: no blue lines.

### 864 to 1188 MHz, 2026-11-04 (out/uImage-1188, md5 c23fbc2d...)

- Build: the parked `kernel/wip/saw-cpufreq` applied, `L_MAX` 22, boost OPPs 864..1188 MHz with the HP voltages
  (`acpu_freq_tbl_1188mhz`), SAW write now waits for idle *and* the new code (500 us). Full rebuild took ~75 min.
- Each step 60 s, both cores loaded, governor performance, `scaling_max_freq` raised step by step:
  864, 918, 972, 1026, 1080, 1134, 1188 MHz all reached on both cores, 0 `mdp4_irq_error`, no reboot,
  thermal_zone0 33.8 -> 34.6 C. Battery 23 %, charger plugged.
- Still open: SAW0 sometimes needs more than 500 us (at boost enable: asked a7, still 8f after 500 us; next
  write asked 9b, read a7). The value lands later (both SAW STS read 0x9b afterwards), but cpufreq must not raise
  the clock before the voltage is there: next build polls with sleeps up to a few ms.
- Not done yet: vdd_mem/vdd_dig (PM8058 S0/S1), L2 clock and bus votes as the vendor does above 594/864 MHz;
  10-minute loads; the browser measurement at 1188.

## Charger type detection in the kernel (2026-11-04)

- How the vendor does it (CM 3.4 `_work/cm11-3.4`): `msm72k_udc.c` `usb_multi_chg_detect()` (`CONFIG_USB_MULTIPLE_CHARGER_DETECT=y`)
  probes D+/D- through the ULPI viewport (pull-downs off, vendor register 0x34 current source on D+, CDP comparator bit 4)
  -> SDP 500, CDP 1400, HP 10 W adapter 2000, phone adapter 900, unknown 100 mA; `board-tenderloin.c`
  `max8903b_set_vbus_draw()` sets the MAX8903 pins; the A6 (`a6.c`, TS2_I2C_FLAGS_2 PUCK_CHARGE) reports the Touchstone dock (1400 mA).
- New driver `kernel/src/drivers/power/supply/tenderloin_charger.c` (`hp,tenderloin-charger`, `CONFIG_CHARGER_TENDERLOIN`,
  patch 0021) replaces the mainline max8903 binding and the GPIO hogs: same D+/D- sequence on DOK_N, input limit from it
  (unknown -> 500 mA), sysfs `usb_type`, writable `input_current_limit` and `charge_behaviour` (CEN), and VBUS extcon of the
  ChipIdea controller and its PHY (EXTCON_USB for anything but a known adapter). Keeps the name `max8903_charger`.
- `tp` (rootfs/tp/power.c): with `usb_type` present it reads the source from it, stops its step-up and drives the charge
  window through `charge_behaviour` instead of /dev/mem.
- First boot: `devm_of_iomap` requested the controller region -> ci_hdrc -EBUSY, no gadget; the detection ran with the
  controller down -> "unknown". Fixed (plain ioremap, unknown keeps the gadget on, stale DOK edge at probe ignored).
- out/uImage-charger2 on the user's PC USB 3 port: `CDP (charging PC port), input limit 1400 mA`, gadget `configured`,
  battery +512 mA. To test: cable unplugged/plugged after boot, wall adapter, dock.
- Wall adapter (user's 2 A adapter, USB 3 brick, USB 2 cable), replugged without reboot: D+/D- read low at every step,
  CDP comparator clear -> "SDP 500 mA", battery discharging (-741 mA with the screen on). PORTSC.LS and the ULPI Debug
  register (always 0x34, suspicious) agree; the adapter likely leaves D+/D- open or uses a scheme the vendor sequence
  does not know. Added the fallback of the old tp: SDP/unknown with no USB address after 5 s -> "adapter (no USB host)",
  1400 mA, usb_type DCP. out/uImage-charger4 (md5 d76cbccd...): PC USB 3 port -> CDP 1400 mA, gadget configured;
  moved to the wall adapter -> SDP, then after 5 s 1400 mA, charger "Charging", battery +410 mA.
- Open: plug back to the PC after boot (gadget must come back), the HP 10 W adapter and the Touchstone dock (A6 flag),
  check that the ULPI reads are right (Debug register always 0x34).
- Back on the PC USB 3 port without reboot: gadget `configured` again (the cable-after-boot bug is fixed by the VBUS
  extcon), but the detection now says SDP (CDP comparator read 0x24, it read 0x34 at boot on the same port): 500 mA,
  battery -617 mA with the screen on. The D+/D- probe gives different answers at boot and after the controller ran:
  the PHY state the sequence starts from is not the vendor one. The old tp stepped a PC port up to 900 mA (stable here).
- Next (not built yet, user paused): PC port step-up in the driver (SDP enumerated -> 900 mA after 20 s stable; input
  lost within 30 s of the step -> PC ports stay at 500 mA for 2 min), code already in tenderloin_charger.c (`up_work`).
  Then: CDP detection after the controller ran, HP 10 W adapter, dock, tp low battery rules with the new driver.

### SAW wait and 10 minutes at 1188 MHz, 2026-11-05 (out/uImage-charger4, PC cable, battery 100 %)

- 200 jumps 432 <-> 1188 MHz on both cores (performance governor, scaling_max_freq): 0 `mdp4_irq_error`, one SAW
  timeout: SAW1 asked 0x8f (887.5 mV, going down) and still read 0xa7 after 5 ms. Going up never timed out.
  Driver changed (not built yet): a late answer when lowering is logged at debug level and returns 0.
- 10 minutes, both cores loaded at 1188 MHz: stayed at 1188/1188, 0 MDP errors, no further SAW message, no reboot,
  thermal_zone0 21.6 -> 27.1 C (still rising slowly), battery +213..229 mA on the PC CDP port (1400 mA input).
- SAW0 PMIC_CTL read 0089a720 at this boot (00898b20 before): the awake level byte follows the last code we wrote
  (0xa7) across the RAM reboot. To keep in mind for the SPM/cpuidle work.

### L2, vdd_mem, vdd_dig and EBI1 votes, 2026-11-05 (out/uImage-l2, md5 e7469c79...)

- `clk-scorpion-acpu.c`: the L2 SCPLL (`0x00903400`, same slew as the cores) follows the fastest core; its source
  select is in the **apps GCC** `0x02082000 + 0x38` (third `reg` of the acpu node), not in the mainline GCC (first try
  read 0 there and left the L2 alone). Before an L2 raise / after a drop: vdd_dig (PM8058 S1 via RPM) 1.1 V, 1.2 V at
  L2 1188 MHz; RPM EBI1 clock 167 / 251 / 310 MHz (vendor 1336 / 2008 / 2480 MB/s at 8 bytes per cycle).
  vdd_dig floor 1.1 V (the vendor goes to 1.0 V up to 648 MHz but adds the GPU/display votes; nobody else votes here).
- DT: `pm8058_s0` "vdd_mem" fixed 1.2 V (vendor max, always above vdd_sc, so no ordering with cpufreq-dt),
  `pm8058_s1` "vdd_dig" 1.1-1.2 V, both 1.6 MHz switching as the vendor; acpu node `vdd-dig-supply`, `clocks = RPM_EBI1`.
  The regulator core reported "Bringing 0uV into ...": the RPM values left by the bootloader are not readable.
- Checked: L2 432 at probe, then 756 / 918 / 1188 MHz with the cores; vdd_dig 1.2 V and `ebi1_clk` 310 MHz at 1188.
- 200 jumps 432 <-> 1188 MHz: 0 MDP errors; one SAW timeout **going up** (SAW1 asked 0xa7, still 0x8f after 5 ms): the
  regulator call fails, so cpufreq keeps the old rate (no undervolt), but the cause is not understood (SAW1 slow to
  start a PMIC transaction?). 10 min both cores at 1188 MHz with the L2 at 1188: stable, 0 MDP errors, no reboot,
  thermal_zone0 30.7 -> 32.6 C, battery 100 % on the PC port.
- The kernel also has the PC port step-up to 900 mA (charger driver), not tested yet.

### SAW writes from the SAW's own core, 2026-11-05 (out/uImage-sawcpu, md5 3a606948...)

- Vendor check: `cpufreq.c` (msm) always runs `acpuclk_set_rate()` on the core being changed (`queue_work_on(policy->cpu)`),
  so `msm_spm_set_vdd()` only ever writes a core's SAW from that core (and only waits 50 us). cpufreq-dt calls the
  regulator from any core. `msm8660-saw-regulator.c` now does the write + poll with `work_on_cpu(vreg->core)`
  (no cpus_read_lock: cpufreq may hold it), and logs the writing CPU on a failure.
- Test pitfall: writing `scaling_max_freq` in a tight loop is applied asynchronously and coalesced (300 jumps gave
  ~140 real transitions per core): the earlier "200 jumps" tests did far fewer voltage changes than counted.
  No userspace governor in the config; spaced writes (performance, 20 ms sleep) give real transitions.
- 1000 spaced jumps 432 <-> 1188 MHz (cpu0 `total_trans` 3610 since boot): 0 SAW errors, 0 MDP errors. Before: about
  1 failure per 400 requests (the rate itself was underestimated, see the pitfall).

### CPU boost switch in the settings panel, 2026-11-05
- `tp cpu get|boost on|off` (native command, CGI allowed), saved as `cpu_boost` in `tpd.conf`, applied by the daemon at start;
  panel Battery page -> Performance -> "CPU boost". Installed live on the tablet (tp md5 77611eaa..., index.html), checked:
  boost on -> max 1188 MHz, off -> 756 MHz, saved to `/mnt/media/touchpad/tpd.conf`, same answer through the CGI.
  Governor back to schedutil (the tests had left performance). Not in a rootfs image yet. Not tried by hand on the panel.

### Committed, 2026-11-05 (branch cpufreq-charger, merged into main)
- The SAW/cpufreq/L2 work is now in the normal sources (`kernel/src`, patch 0020, DT); `kernel/wip/saw-cpufreq/` is
  removed and `scripts/build/kernel.sh` no longer looks at `kernel/wip/`. Default image: 432 .. 756 MHz, boost OPPs up to
  1188 MHz behind the "CPU boost" switch. The charger driver (patch 0021) and the tp changes are committed too.

### Restart and power off, 2026-11-05 (kernel #10 in /boot/uImage.Linux, md5 ad3605b3...)
- Patch 0008: Palm reason "recover" (0x6f656d11) written at boot as before; a reboot notifier writes "reboot" (0x6f656d44) on a
  restart, "shutdown" (0x6f656d00) on a power-off/halt, "recover" for `reboot recover|recovery` (values of the HP restart_stock.c).
- `tp-panel reboot` first wrote `Linux` to `/boot/moboot.next`, but moboot 0.3.8 does not remove that file (still there after
  the boot it chose), so it would force Linux at every boot: dropped. Instead `moboot.default` = Linux on the 32 GB unit
  (FLASHING.md 4.2), the menu still shows. New `tp-panel poweroff`, allowed by the CGI, button "Power off" next to "Restart".
- Checked on the 32 GB unit, USB cable plugged: Restart from the panel -> moboot -> Linux from the flash in < 1 min (no recovery);
  Power off -> no network, no USB, screen off (user).
- Flash: `/boot/uImage.Linux` replaced by this kernel; the previous one (md5 3578f840...) is kept as
  `/mnt/media/touchpad/uImage.Linux.prev` because `/boot` has only 1.7 MB free. The rootfs image was not rebuilt: `tp`,
  `tp-panel` and `index.html` were copied into it; the media volume has 32.7 MB free, too little for a second 620 MB image.
- Cleanup on the tablet (user request): Linux set as `moboot.default` (was CyanogenMod), leftover `moboot.next` removed,
  deleted `/mnt/media/touchpad/uImage.Linux.prev`, `/usr/local/bin/tp.bak`, the test scripts and logs in `/root`. Kept:
  the webOS, CyanogenMod and ClockworkMod kernels in `/boot`, `touch-test.html` and `boot.log` on the media volume.

### Camera step 3 research: vendor CSI/VFE 3.1 path (2026-11-05, CM 3.4 sources in `_work/cm11-3.4`)
- Stack used on the TouchPad: legacy `CONFIG_MSM_CAMERA` (msm_camera.c, msm_vfe31.c, msm_io_8x60.c); no ISPIF on 8660; the CAMIF,
  OPERATION_CFG and write-master (image size, UB, burst) values are built by the **closed userspace** (mm-camera) and only copied by
  the kernel: they are not in any source.
- Blocks: CSI1 (receiver + PHY in one 0x400 window) `0x04900000`, IRQ CSI_1 = SPI 83 (CSI0 `0x04800000`, SPI 84); VFE 3.1 `0x04500000`
  (1 MiB, CAMIF at +0x1E0..0x204), IRQ SPI 48. VFE footswitch `VFE_GFS_CTL` MMSS_CC + 0x198 (ENABLE bit 8, CLAMP bit 5).
- Clocks (MMSS_CC): vfe_axi (MAXI_EN 0x018 bit 18), vfe_p (AHB_EN bit 13), csi1_p (AHB_EN bit 20), csi_src 384 MHz (CSI_CC 0x040/NS 0x048),
  csi1 (CSI_CC bit 7), vfe 228.57 MHz then 153.6 MHz at start (VFE_CC 0x104), csi1_vfe (VFE_CC bit 10); the vendor turns CSI0 and CSI1 on.
  Mainline `mmcc-msm8960.h` has VFE_*, CSI0-2_*, CSIPHY*_TIMER, CAMCLK*: registers to compare with the 8660 ones before use.
- CSI1 sequence for the MT9M113 (1 lane, 8-bit, settle 0x14), msm_io_8x60.c:891-971: PHY_CONTROL 0x00 = 0x4; PROTOCOL 0x04 = 0x08000000
  (reset) then 0x00260000; D0_CONTROL2 0x38 = 0x140F0018; CL_CONTROL 0x48 = 0x0F000004; D0_CONTROL 0x34 = 0; D1_CONTROL 0x20 = 0x300
  (PHY power-up); D2/D3 0x2C/0x30 = 0; CAMERA_CNTL 0x24 = 0xE404; INT_MASK 0x0C = 0xF017F3C0, INT_STATUS 0x08 cleared. No data type or
  VC register.
- VFE: reset (GLOBAL_RESET 0x04 = 0x3FF, ack STATUS_1 bit 22) + defaults, OPERATION_CFG (CFG 0x14 pixel pattern 6 = CbYCrY), CAMIF_CFG
  (32 bytes at 0x1E4), AXI (blob at 0x38..0xF3, write masters at 0x4C + 0x18 n: cfg, ping, pong, 3 size/UB/burst words), start
  (IRQ_MASK_0 = 0x00EFE021, REG_UPDATE 0x260 = 1, CAMIF_COMMAND 0x1E0 = 1), frame done = STATUS_0 bit 21, ping/pong in 0x180,
  stop (CAMIF_COMMAND 2, AXI halt 0x1D8/0x1DC, GLOBAL_RESET 0x3EF). Buffers are physical addresses (VFE SMMU unused by this stack).
- Missing piece: the userspace values. Plan: dump the VFE (0x04500000) and CSI1 (0x04900000) registers from **webOS while its camera
  is streaming** (front camera preview), then replay them from the new driver.

### Camera: webOS register dump with the front camera streaming (2026-11-05, `data/webos/camregs/`, `scripts/tools/camregdump.c`)
- `camregdump` (static armv7, /dev/mem, reads CSI1/VFE only when their clocks and the VFE footswitch are on), run by novacom while the
  user had a camera app open. `webos-camregs-1.txt`: camera not running (clocks off). `webos-camregs-2.txt`: streaming, MMSS clock
  controller + CSI1 (0x400) + two VFE samples (0x800).
- Clocks while streaming (MMSS): AHB_EN 0x90913c83, MAXI_EN 0x100f07f9, CSI_CC 0x85 (CSI1 + root), CSI_NS 0x2, VFE_CC 0x80ff14a5,
  VFE_MD 0x2f8 / NS 0x00fa0001, CAMCLK_CC 0xa5 / NS 0xfc00c002, VFE_GFS_CTL 0x300 (enabled + retention, as at boot).
- CSI1 = the vendor sequence (0x00 4, 0x04 0x00260000, 0x20 0x300, 0x24 0xE404, CONTROL2 0x140F0018 on all four data lanes,
  CL 0x0F000004); INT_MASK 0xF0F7F3FF here.
- VFE (version 0x00030217): CFG 0x14 = 0x46 (pattern 6 = CbYCrY), MODULE_CFG 0x01c00c0c; CAMIF 0x1e4 = 0x40, frame 0x1ec = 0x01e00500
  (480 lines x 1280 bytes = 640 px UYVY), window 0x1f0 0x4ff / 0x1f4 0x1df; chroma/scalers at 0x35c-0x37c and 0x4d0-0x4f4
  (640x480 and 320x240 outputs); bus 0x3c 0x02aaa771, xbar 0x40 1 / 0x44 0x1a1b; write masters 0 and 1 (Y, CbCr of 640x480, ping/pong
  at +4/+8 moving each frame: 0x4268xxxx..), 4 and 5 (320x240), size/UB words 0x58-0x60, 0x70-0x78, 0xb8-0xc0, 0xd0-0xd8.
- Consequence: the driver can replay this whole VFE state (webOS output: semi-planar Y + CbCr, i.e. NV12/NV21-like 640x480) with its
  own buffer addresses, instead of guessing the closed library values.

### Camera step 3: first frames on /dev/video0 (2026-11-05, out/uImage-cam3, md5 b3a7927c...)
- New driver `kernel/src/drivers/media/platform/qcom/msm8660_vfe.c` (`qcom,msm8660-vfe`, patch 0022, `CONFIG_VIDEO_QCOM_MSM8660_VFE`):
  CSI1 vendor sequence; VFE reset then replay of the webOS VFE state (`msm8660_vfe_regs.h`, generated from
  `data/webos/camregs/webos-camregs-2.txt` without command/status/address registers); write master 0 = Y and 4 = CbCr at + 640 * 480
  (webOS buffers showed WM4 = WM0 + 0x4b000, CbCr 240 lines: NV12); frame done = IRQ_STATUS_0 bit 21, ping/pong from 0x180 bit 0;
  scratch buffer when no buffer is queued. Clocks: vfe_ahb/vfe_axi by the clock framework (same bits as the MSM8960 table); the others
  (VFE core NS 0x10c/MD 0x108/CC 0x104, CSI 0x040/0x048, CSI interface AHB bits 7/20, MAXI bit 19) differ from the MSM8960 layout
  of mmcc-msm8960 and are written directly with the webOS values. DT node `camera-controller@4500000` linked to the sensor endpoint.
- Result: `msm8660-vfe ...: front camera on video0 (NV12 640x480)`; `dd if=/dev/video0 bs=460800 count=10` gives 10 frames in 1.1 s
  (with the 0.8 s sensor start), 0 VFE error interrupts; frame 5 converted as NV12 is a sharp, correctly exposed picture of the ceiling.
  **NV12 confirmed** on a colourful scene (green bottle label, skin tones right; NV21 would swap them). The picture comes rotated
  by 90 degrees (sensor mounting, landscape tablet), not mirrored: rotation is left to the integrator.
- Frame rate (read() from /dev/video0 with dd): 300 extra frames take 10.0 s more = **30 fps**; 9000 frames (5 min) in 317.8 s
  = 28.3 fps average (sensor auto exposure can lengthen frames in dim light, or read() missed a few), **0 VFE error interrupts**,
  thermal_zone0 34.6 C at the end.
- Not done: the footswitch/clock state after stop, other sizes (context B 1280x1024),
  controls. The integrator gets a standard V4L2 capture node (no GStreamer in the image, user decision).

### Hardware H.264: the VIDC 1080p core is a Samsung MFC v5 (2026-11-05, CM 3.4 `drivers/video/msm/vidc/1080p`)
- webOS: `msm_vidc.0` at `0x04400000` (1 MiB), IRQ "vidc" 81 = SPI 49, drivers `msm_vidc_enc`/`msm_vidc_dec`; firmware
  `vidc_1080p.fw` in webOS `/lib/firmware` (one image for encoder and decoder, copied to DRAM base A and booted from there).
- **Same register map as mainline s5p-mfc v5** (`regs-mfc.h`, checked on 6.12.40): SW_RESET 0x0000, RISC_HOST_INT 0x0008,
  HOST2RISC_CMD/ARG 0x0030.., RISC2HOST_CMD/ARG 0x0044.., FW_VERSION 0x0058, FW_STATUS 0x0080, MC_DRAMBASE_ADR_A/B 0x0508/0x050c,
  MC_STATUS 0x0510, SI_CH0/1_INST_ID 0x2040/0x2080, ENC_SI_CH0_SB_ADR/SIZE 0x2044/0x204c, CUR_Y/C_ADR 0x2050/0x2054, FRAME_INS
  0x2058, HOST_WR_ADR 0x2064; same command codes and the same two-port addressing (offsets from base A/B >> 11).
  Qualcomm additions: an AXI wrapper at +0x80000 (halt/reset/burst at release from reset), a pixel cache, a block at +0xc0000,
  possibly different shared-memory fields.
- 8660 resources (vendor clock-8x60.c / footswitch-8x60.c): vcodec_clk (CC 0x0f8, MD 0x0fc, NS 0x100; up to 228.57 MHz), vcodec_axi
  (MAXI_EN bit 19), vcodec_p (AHB_EN bit 11), footswitch VED_GFS_CTL MMSS + 0x194; no IOMMU (physical buffers, 2 KB aligned, inside
  the base A/B windows). About 4-5 MB contiguous for 640x480 (firmware 1 MB, 800 KB context, 4 reference frames, stream buffer).
- Plan: a standalone V4L2 mem2mem H.264 encoder that borrows the s5p-mfc v5 structure (~2000-2500 lines), firmware from webOS.
- Detailed comparison s5p-mfc v5 (6.12.40) vs vendor ddl (2026-11-05): reset values, DRAM bases, R2H/H2R codes, codec 16 = H.264
  encode, encoder registers (frame size 0x818/0x81c, profile/level 0x830, loop filter, 0xc504 GOP, slices, 0xc51c input 0 = linear
  NV12M, RC 0xc5a0.., entropy 0xd004...), reference/work buffer registers and ports, frame-done readback (0x2004..0x2018) and the
  interrupt acknowledge all match. Variant needs: AXI wrapper init at +0x80000 before SW_RESET = 0x3ff (+0x80004 = 1, +0x80008 = 1,
  +0x80014 = 1, poll +0x80018 [1:0] == 3, +0x80014 = 0x10 then 0, +0x8003c = 0x0808); firmware buffer 800 KB (also the SYS_INIT
  argument; the 500140-byte blob exceeds the v5 384 KB); OPEN_INSTANCE arg1 |= 1 << 29; no SLEEP/WAKEUP commands; write 0 to 0x720
  (MB info), 0x2058/0x2068, a sequence number at 0x206c and 0xffff to 0x2040 before each channel command; skip Samsung-only writes
  (0xc508, shm 0x74/0x78/0x9c/0xa0), initial bitrate at shm 0x11c; slice mode MAX_BYTES = 3; pixel cache left off (OPEN arg2 0);
  one shared-dma-pool used for both ports; VED footswitch MMSS + 0x194 and vcodec clocks (mainline mmcc bits match the 8660 ones).

### H.264 encoder bring-up, state when paused (2026-11-05)
- Patch 0023 (s5p-mfc variant `qcom,msm8660-vidc`, editable copy in `_work/mfc-edit`, diff against `_work/linux/mfc`), DT node
  `video-codec@4400000` (`qcom,mmcc`, `qcom,rpm`, `memory-region` = `vidc_mem` 48 MiB at 0x77000000 twice), config
  `CONFIG_VIDEO_SAMSUNG_S5P_MFC=y`, firmware `firmware/webos/vidc/vidc_1080p.fw` (git-ignored) copied to the tablet `/lib/firmware`.
- First boot hung: udev opens `/dev/video*` (~6.7 s) and the firmware boot ran. Now a bring-up switch
  `/sys/module/s5p_mfc/parameters/qcom_stage` (0 = refuse, 1 footswitch, 2 + reset, 3 + AXI wrapper, 4 full), default 0.
- Found: the mainline mmcc vcodec_clk does not start the core clock on the 8660 (VCODEC_CC 0x80000000, NS 0): webOS values written
  (CC 0xa5 + bit 30, NS 0x7c002, MD0 0x1f7). Stages 1-3 pass (core registers readable, reset done, AXI halt ack).
- Stage 4: no FW_STATUS_RET in 2 s, no interrupt; RISC released (SW_RESET 0x3ff), FW_STATUS 0, MC_STATUS 0x1 (memory controller
  busy), AXI status 0x20. Tried without effect: RPM MM fabric unhalt of HD_CODEC ports 12/13 (returns 0), vendor MM AXI init
  (MAXI_EN mask, MAXI_EN3 0x3fe7fcff, SAXI_EN, AHB_EN2), GPU kept on (MM fabric clocks). Written, built (out/uImage-mfc7) but not
  tested: codec block reset through SW_RESET_AXI/AHB/CORE before power-up.
- Next ideas: test mfc7; firmware in SMI as the vendor (fw_addr 0x38000000) instead of DDR; compare the AXI wrapper registers
  (+0x80000..) and VIDC registers with a webOS dump while encoding (camregdump-style tool); check the RPM MM fabric clock/arbitration.
- Camera work (VFE driver, patch 0022, DT, config, camregdump, dumps) is still uncommitted too; the vfe driver no longer touches
  MAXI_EN bit 19 (vcodec AXI).
- 2026-11-06, out/uImage-mfc7 (codec block reset through SW_RESET_AXI/AHB/CORE before power-up, GPU kept on): same result, no
  FW_STATUS_RET, MC_STATUS 0x1; only the AXI wrapper status after the run changes (0x30 instead of 0x20).
- webOS dump with the camera app open (`scripts/tools/vidcregdump.c`, `data/webos/vidcregs/webos-vidcregs-1.txt`): the vcodec
  clock is on (CC 0xc0ff00a5) but the core is NOT running (SW_RESET 0x3fe, FW_VERSION 0, garbage DRAM bases and buffer registers):
  the camera app does not boot the codec firmware. Only hint: AXI wrapper +0x8003c = 0x0303 there (CM writes 0x0808), +0x80000 0x1d,
  +0x8000c 6, +0x80030..38 0x1000/0x1010/0x1010. Next: dump while webOS plays an H.264 video (same firmware boot).
- 2026-11-06: webOS dump while it plays a video (`data/webos/vidcregs/webos-vidcregs-2.txt`): codec running, FW_VERSION 0x110909,
  FW_STATUS 1, SW_RESET 0x3fb, **DRAM bases A = B = 0x38000000 (SMI)**, AXI wrapper OOO read/write 0, burst 0x0808, +0x80000 0x1d,
  +0x8000c 6, +0x80030..38 0x1000/0x1010/0x1010; MMSS as at idle plus vcodec clocks.
- Tried: OOO 0 (mfc8), codec memory in SMI 0x38000000 with the RPM SMI clock (mfc9), RISC released with 0x3fb (mfc10): no answer.
  But **SMI is not usable as left by our kernel**: a word written with devmem reads back 0, and the firmware was never in SMI
  (zeros where the file has data). So the SMI tries are void; the DDR tries had the firmware in place and the core still did not
  read it (MC_STATUS 0x1 stuck). Open: SMI controller init (bootloader/RPM), MM fabric arbitration (vendor msm_bus votes for the
  codec to EBI_CH0 and SMI, RPM MM_FABRIC_ARB), codec AXI path to DDR.
- After one `tp recovery` from out/uImage-mfc9 the tablet came up in Qualcomm emergency download mode (05c6:9008); a 30 s
  Power + Home press brought it back to the bootie recovery. Nothing had been written to flash.
- 2026-11-06: **SMI made usable**: the RPM SMI clock needs a rate (`assigned-clock-rates` 66.666 MHz) and the codec keeps it
  enabled from probe; then devmem writes stick and the firmware is really in SMI (0x9c at +0x108, 0x1801d4 at +0x1000). Still no
  answer: the AXI wrapper shows port A not idle (status bit 4 = A idle, 0x20 here, 0x30 under webOS) = the core's first memory read
  is never answered by the fabric. Our state otherwise equals webOS running (AXI wrapper, MC bases, SW_RESET 0x3fb, clocks).
- RPM request registers (0x104600 + target id * 4, `scripts/tools/physdump.c`, `data/webos/vidcregs/{webos,linux}-rpm.txt`):
  webOS with the codec running sends MM_FABRIC_ARB (id 111) `0c5c0f07 0 00008c5c 0 0 0 0 00030000 01700138 00008c5c 0...` and
  APPS_FABRIC_ARB (id 51) `00000c5f 3 00030000 00008c5c 0 0`; mainline sends neither (all zero). Next build writes both.
- The webOS camera app stopped showing the camera after our test kernels (RPM state survives warm reboots); after the user
  recorded/replayed a video it works again.
- out/uImage-mfc13/14 (MM + APPS fabric arbitration with the webOS values, accepted by the RPM): the AXI port A now goes idle
  from time to time (status alternates 0x20 / 0x30 over 20 s) = the core's reads complete, slowly; still no FW_STATUS_RET after
  20 s, reg 0x70 stays 7 (webOS 1). The copied ARB words are a snapshot; the vendor msm_bus computes them from per-master tables
  and votes. Proposed next: build the GPL webOS/CM7 2.6.35 kernel (`_work/cm7-2.6.35`, CyanogenMod/hp-kernel-tenderloin
  gingerbread) with msm_bus and vidc debug traces, RAM-boot it, and log the exact RPM requests and codec register writes.

## Sound: research (2026-11-06, CM 3.4 / CM7 2.6.35 sources, mainline 6.12.40 in `_work/snd-ref`)
- Vendor path: Q6 DSP (QDSP6v3, PIL "q6", APR v1 over SMD `apr_audio_svc`) drives the internal codec I2S pair (AFE PRIMARY_I2S
  RX/TX): speaker -> WM8958 AIF1, mic -> AIF2 (inferred), 48 kHz 16-bit, MSM is clock master, no MCLK: the WM8958 FLL locks to
  BCLK 1.536 MHz -> 12.288 MHz. Mainline q6afe/q6asm/q6adm use newer opcodes (APR v2+), no Q6v3 remoteproc, no SMD/SMEM in the
  8660 DT: the Q6 route is weeks.
- DSP-less route exists in the vendor tree (`sound/soc/msm/lpass-i2s.c`, `lpass-dma.c`, CONFIG_SND_SOC_MSM8660_LPAIF, unused
  by tenderloin): LPAIF at 0x28100000, IRQ SPI 85, register layout = mainline lpass-ipq806x (I2S_CTL at 0x4 + 4 * port with the
  same bit fields, DMA 0x6000 + 0x1000 * ch, write DMA ch 5). LCC (0x28000000) = mainline lcc-msm8960 layout, but PLL4 runs at
  540.672 MHz and is switched through the RPM (PLL_4 resource).
- WM8958 (rev B): mainline wm8994 MFD/codec; on GSBI7 I2C (0x16680000) at 0x1a; LDO1_EN GPIO 66, LDO2_EN GPIO 108; supplies
  PM8058 S3 1.8 V and vdd50_boost; gpio-cfg 0x0001, 0x8001 x4, 0x2005, 0xa101 x5; micbias 0x29, 0x2d; speaker = LINEOUT1/2 with an
  external amp enabled by WM8958 GPIO1 (0x41 on / 0x01 off); headphone HPOUT1L/R, jack detect GPIO 67; internal mic IN1LN +
  MICBIAS1, headset mic IN2LN + MICBIAS2 (mic detect via GPIO6 -> GPIO 57). No codec IRQ wired.
- Plan (DSP-less, ~1.5-2 weeks): 0) feasibility: read LCC/LPAIF from Linux, Q6 held in reset, PLL4 state; 1) LCC msm8660 clocks
  (540.672 MHz table) + PLL4 via RPM; 2) lpass-msm8660 variant of lpass-cpu; 3) WM8958 node; 4) machine driver (FLL from BCLK,
  speaker amp GPIO1, jack). Firmware: only the optional wm8958_*.wfw.
- Feasibility (2026-11-06, our Linux, serial devmem): **LCC (0x28000000) and LPAIF (0x28100000) are readable from the apps CPU**,
  no hang. LCC: PLL4 L/M/N already programmed for 540.672 MHz (L 20, M 28, N 1125) but mode 0 (off); codec I2S spkr/mic NS 0x2C200
  (configured, off); LCC_Q6_FUNC 0x0696AD04 = Q6 held stopped (STOP_CORE, CLAMP_IO, SS_ARES, CORE_ARES). LPAIF I2S_CTL(spkr) 0x4,
  DMA idle. Mainline clk-rpm already has RPM_PLL4_CLK (QCOM_RPM_PLL_4, fixed 540.672 MHz) for msm8660. Next: lcc-msm8960 variant
  for msm8660 (540.672 MHz OSR table), then the lpass-cpu msm8660 variant.
- Step 1 done (2026-11-06, out/uImage-lcc1, md5 15a416ea...): patch 0024 adds `qcom,lcc-msm8660` to lcc-msm8960 with the vendor
  540.672 MHz OSR table (selected when the PLL4 L register reads 0x14); DT `clock-controller@28000000` with clocks pxo and
  `pll4_vote` = RPM_PLL4_CLK. Booted: the LCC registers, `pll4` and `pll4_clk` read 540672000 Hz, codec_i2s_spkr/mic and mi2s
  clocks present (unused, so not enabled yet). Enabling them needs a consumer: step 2 (LPAIF driver).
- Step 2 in progress (2026-11-06): `kernel/src/sound/soc/qcom/lpass-msm8660.c` (`qcom,msm8660-lpass-cpu`, patch 0025,
  `CONFIG_SND_SOC_LPASS_MSM8660`), codec speaker I2S only (port 0, read DMA ch 0, interface code 1 via dmactl_audif_start 1);
  DT `audio-controller@28100000` (SPI 85, LCC CODEC_I2S_SPKR OSR/BIT clocks) + test card `sound-test` (simple-audio-card with a
  `linux,spdif-dit` dummy codec, MSM master). Config: SND_SOC_QCOM, SND_SIMPLE_CARD, SND_SOC_SPDIF.
- out/uImage-snd1: card "TouchPad LPAIF test" registered. Test with tinyalsa (`out/snd/tinyplay`, from Alpine 3.21 armv7, copied
  to /usr/local/bin; tone `out/snd/sine440.wav` at /root): first play opened the stream but OSR stayed at 27 MHz (no mclk-fs),
  bit clock 1.5 MHz, DMA read 32 bytes then stopped, no LPAIF IRQ; at stop `codec_i2s_spkr_bit_div_clk status stuck at 'on'`,
  next open fails with -EBUSY on the OSR clock. Vendor 8660: OSR clock halt check ENABLE bit 1, bit clock no check (DELAY).
- Done but NOT built/tested (build interrupted): patch 0024 sets the five LCC bit_div clocks to BRANCH_HALT_DELAY on msm8660;
  DT test card `simple-audio-card,mclk-fs = <256>` (OSR 12.288 MHz). Next: build (out/uImage-snd2), play, check OSR 12.288 MHz /
  bit 1.536 MHz, DMA CURR moving and LPAIF IRQs (/proc/interrupts lpass); if the DMA still stalls, compare with the vendor
  lpass-dma.c start sequence (DMA_CTL 0x813, I2S_CTL 0x4400 | WS, IRQ_EN). Then step 3 (WM8958 node on gsbi7_i2c) and step 4.
- **Step 2 done (2026-11-08, out/uImage-snd5)**: LPAIF playback works on the test card: `aplay -D hw:0,0` of a 2 s tone takes
  2.05 s, 16 LPAIF IRQs per play (128 ms periods), DMA CURR moves, repeated opens fine. Root cause of the stall: on the 8660 the
  codec/spare I2S NS registers use the MI2S bit layout (vendor clock-8x60.c CLK_AIF_OSR/CLK_AIF_BIT): OSR enable bit 17, bit
  clock enable bit 15, divider 13:10, external bit clock select 14, **bit 19 = interface reset**. The mainline 8960 values
  (21, 19, 8-bit divider, 18) held the I2S in reset while "enabling" the bit clock. Patch 0024 now sets those fields on msm8660
  (bit clocks keep BRANCH_HALT_DELAY like the vendor; the OSR status check works again). NS while playing 0xF5029F5A
  (OSR 12.288 MHz, /8 = 1.536 MHz).
- Other findings: the OSR rate is set in DT (`assigned-clocks = <&lcc CODEC_I2S_SPKR_OSR_SRC>`, 12.288 MHz); otherwise the
  source keeps the boot value (768 kHz) and the bit clock set_rate divides by zero in the divider rounding. `simple-audio-card,
  mclk-fs` was removed (it set the OSR to 768 kHz). lpass-platform fixes 2 periods of 6144 frames: tinyplay with its defaults
  (1024 x 4, stop_threshold 4096) underruns at once; use `aplay` (Alpine alsa-utils copied to the rootfs from
  `out/snd/aplay/aplay.tar`) or `tinyplay -p 6144 -n 2`. Debug kit that worked: regmap/clk trace events and kprobes
  (`p:pstop snd_pcm_stop` with a stacktrace trigger) in /sys/kernel/debug/tracing.
- **Step 3 done (2026-11-08, out/uImage-snd7)**: GSBI7 in I2C mode (QUP 0x16680000, GPIO 59/60 function gsbi7, 100 kHz) and
  the `wlf,wm8958` node at 0x1a (supplies PM8058 S3, LDO1/LDO2 enable GPIO 66/108, vendor gpio-cfg and micbias values);
  config MFD_WM8994 + REGULATOR_WM8994. Boot log: "WM8958 revision B CUST_ID 00", wm8994-codec/gpio/ldo.0/ldo.1 created.
  `vdd50_boost` cannot be the SPKVDD/LDO1VDD supply: the touch-power regulator-output holds it exclusively (-EPERM), so those
  supplies are left out (dummy; the boost stays on). SND_SOC_WM8994 is a hidden symbol: the step 4 machine driver selects it.
- **Step 4: speaker sound heard (2026-11-08, out/uImage-snd9)**. Machine driver `kernel/src/sound/soc/qcom/tenderloin.c`
  (`hp,tenderloin-sndcard`, CONFIG_SND_SOC_TENDERLOIN in patch 0025, selects SND_SOC_WM8994), a port of the logic of the
  CM/vendor `sound/soc/msm/msm8x60.c` (Q6-based, 2.6.35 ASoC API, not portable as is): LPAIF I2S master -> WM8958 AIF1,
  FLL1 from BCLK x 8 (12.288 MHz), SYSCLK = FLL1; "Ext Spk" widget on LINEOUT1/2 with the amp switched by writing WM8958
  GPIO_1 0x41 / 0x01; "Headphones" on HPOUT1L/R with the jack on TLMM 67 (active high). DT node `sound` (qcom,cpu-dai,
  audio-codec, hp-det-gpios) replaced the step 2 test card. Card "TouchPad"; widget names avoid the codec's own "Headphone".
- Mixer path used for the test (tinymix, not persistent; an integrator UCM/alsactl state should set it): DAC1L/R Mixer AIF1.1
  Switch, DAC1 Switch, DAC1 Volume 80 (-12 dB), Left/Right Output Mixer DAC Switch, LINEOUT1N/1P Mixer Left Output Switch,
  LINEOUT2N/2P Mixer Right Output Switch, LINEOUT1N/1P/2N/2P Switch; headphones: Left/Right Headphone Mux = DAC.
- Open: headphone output and jack detection not tested; boot warning "update_bits ... register 0x3a -16" (cause unknown);
  optional firmware wm8958_mbc/mbc_vss/enh_eq.wfw missing (not needed to play); capture (mic) not done at that time (see "Microphone" below); the playback code was committed in 4b0f41c.
- Panel and tp (2026-11-08): `tp sound get|volume <0-100>|mute on|off|test` (rootfs/tp/sound.c): the daemon sets the mixer path
  above at start, volume on "DAC1 Volume" (-21..0 dB, 0 % mutes), mute on "DAC1 Switch", saved as sound_volume / sound_mute;
  test = /usr/share/sounds/tp-test.wav (0.7 s two-note chime, rootfs overlay) through `tinyplay -p 6144 -n 2`. Rootfs gets the
  Alpine package `tinyalsa` (73 KiB, tinymix/tinyplay) as the minimal mixer/player. Panel section Sound enabled (volume slider,
  mute switch, Play). Tested on the tablet with the new tp copied by hand (rootfs image not rebuilt yet).
- Sound fixes after the user's test (2026-11-08): `tinymix set` with one value sets only the first channel (right DAC stayed at
  0 dB): stereo controls get two values; 100 % = -6 dB (0 dB saturated the speaker), range -27..-6 dB; the test sound has 0.3 s
  of silence appended (tinyplay closes without draining the 256 ms buffer); test refused with a message while muted; Play
  button styled primary. Deployed by hand on the tablet (tp + wav), not yet in a rebuilt rootfs image.
- **Playback start order (2026-11-08, patch 0026, kernel md5 e2d9bd83 flashed)**: mainline lpass enables the DMA (platform
  prepare) and the I2S port (cpu prepare) at prepare. On the 8660 a DMA running from prepare advances the pointer before the
  start (xrun at once, only bits of sound: aplay passed by luck of timing), and a DMA enabled after the I2S port does not move
  (CURR stuck at +0x20, checked live with devmem: DMA then I2S runs and interrupts come). Patch 0026 adds the variant flag
  `dma_enable_at_start` (lpass.h, lpass-platform.c, lpass-cpu.c): prepare only programs the DMA and enables the bit clock, the
  trigger enables the DMA then the port. aplay: 1.0 s file in 1.11 s, 8 IRQs.
- tinyplay (tinyalsa 2.0) still underruns at once (it starts the stream with ~no data): `tp sound` now uses alsa-utils (amixer
  cset, aplay -D hw:0,0); the rootfs package list has alsa-utils instead of tinyalsa (~4 MB, user's choice). DAC1 Volume steps
  are 0.75 dB (96 = 0 dB): 1-100 % = -30..-12 dB. Deployed by hand (tp, amixer, aplay); rootfs image not rebuilt yet.
- **Microphone, in progress (2026-11-08, out/uImage-mic3, RAM only)**: vendor capture = WM8958 AIF2 (FLL2 from BCLK2) on the
  LPAIF codec mic I2S (Q6 AFE PRIMARY_I2S_TX, line SD0, MSM master), internal mic IN1LN + MICBIAS1, headset IN2LN + MICBIAS2.
  Done: lpass-msm8660 second DAI "lpass-codec-mic" (port 1, write DMA ch 5 at 0xb000, `wrdma_audif` = 1 in patch 0026),
  tenderloin.c capture link to wm8994-aif2 with FLL2/SYSCLK_FLL2 and the mic widgets/routes, DT mic OSR/BIT clocks (12.288 MHz)
  and `qcom,cpu-dai = <&lpass_cpu 0>, <&lpass_cpu 1>`. Result: PCM 0,1 records at the right rate (IRQs, 8 per s), FLL2 locks
  (0x732 bit 6 with cache_bypass), the whole codec path is powered (DAPM and registers: MICB1, IN1L, MIXINL, ADCL, DAC2 mixer
  ADCL, AIF2ADCL/R, AIF2 slave I2S 16-bit), but the samples are all zero, even with a digital AIF1 -> AIF2 loopback
  ("AIF2DAC2L Mixer AIF1.1 Switch") while playing. Tried live: mic SD lines 0-3 and write DMA interface codes 0-4 (no data;
  a first SD1 hit was an artefact of writing I2S_CTL during the stream). Mixer path used: IN1L PGA IN1LN, IN1L Switch/Volume 25,
  MIXINL IN1L Switch/Volume 1, ADCL Mux ADC, Left Sidetone ADC/DMIC1, AIF2DAC2L/R Mixer Left Sidetone Switch, AIF2ADC Mux.
  Playback unaffected (aplay 8 IRQs). Next idea: dump the LPAIF (mic I2S_CTL, DMA 0xb000) and WM8958 registers under webOS
  while it records (voice memo), as done for the camera, and compare.
- Power key (2026-11-08): tp handles KEY_POWER of `pmic8xxx_pwrkey`: one press wakes the screen when off, turns it off otherwise.
- **CPU frequency stuck (found 2026-11-08)**: after some restarts SAW0 holds PMIC data 0xfd (not band 2) with AVS_CTL 0x10241bff;
  get_voltage returned -ENOTRECOVERABLE, the regulator was refused, cpufreq-dt stayed deferred and the cores stayed at the boot
  frequency (the panel's "now ... MHz" did not move). Fix in msm8660-saw-regulator.c: at probe a non band 2 code is replaced by
  the highest allowed voltage (0xa7, 1.1875 V; vendor SPM init first if refused). out/uImage-snd10: "writing a7", cpufreq back.
- **Flashed 2026-11-08** (build 0274489-dirty, out/artifacts): kernel #34 in /boot/uImage.Linux (md5 209cb19d...) from our
  Linux, rootfs-alpine.ext4 (md5 dfe86143...) replaced in place from webOS with `novacom put` (the bootie recovery only RAM-boots;
  the initramfs has no Wi-Fi and its USB ECM link does not pass through usbipd: tablet TX 0). Checked after a cold boot through
  moboot: card TouchPad + "sound: mixer path set", cpufreq up (after the webOS reboot SAW1 held 0x20: the new probe fix wrote a7).
- **Touch broken by the codec bus, fixed 2026-11-08**: GSBI7 I2C (0x16680000) became i2c-0 and shifted GSBI10 to i2c-2, while
  ts_srv opens /dev/i2c-1 ("TSPower, ioctl failed errno 6"). DT aliases now fix the numbers: i2c0 GSBI8, i2c1 GSBI10, i2c2 GSBI4,
  i2c3 GSBI7. Kernel reflashed to /boot/uImage.Linux (md5 06936107...).
- Steps 3 (WM8958 node) and 4 (machine driver) are done, see above. Remaining: headphones and jack, microphone, rootfs rebuild.

## Session state (updated 2026-11-09; shared summary for agents in MEMORY.md)
- Tablet: kernel md5 06936107... (latest flash, includes patch 0026 and the I2C alias fix; earlier e2d9bd83 and 209cb19d are superseded)
  in /boot/uImage.Linux, rootfs of 2026-11-08 with tp, aplay, amixer and tp-test.wav copied by hand; RAM-booted for the mic tests:
  out/uImage-mic3. **`out/kernel/uImage` and the tablet's RAM kernel still hold the diagnostic build (AIF2 DAC LRCLK forced to 3 at
  capture prepare)**: rebuild a clean kernel before any flash. Staging to delete: `touchpad/luneos-ram-test` on the media volume.
  Uncommitted: the microphone and LuneOS sections, `data/webos/audioregs`. Wi-Fi 192.168.1.249; SSH root works
  with `scripts/tablet-ssh.sh -i <ip>` (project key); `devmem` = /usr/local/bin/devmem (a mode of tp); `tablet-put.sh` fails
  silently, copy files with `cat file | scripts/tablet-ssh.sh -i <ip> 'cat > dest'`. After each `cycle.sh`, re-run
  `usbipd attach --wsl --busid <id>` if the serial gadget shows "Shared".
- Uncommitted work of this session: camera (VFE driver, patch 0022, DT, config, camregdump, data/webos/camregs), H.264 encoder
  (patch 0023 + `_work/mfc-edit`, DT vidc node + SMI memory, vidcregdump/physdump, data/webos/vidcregs, firmware in git-ignored
  firmware/webos/vidc), LCC (patch 0024, `_work/lcc-edit`), sound (lpass-msm8660.c, patch 0025, DT, config). Not mine and left
  alone: Dockerfile, scripts/build/kernel.sh, README, docker/novacom, docs/SETTINGS-PANEL.md, the browser cache work in tp-panel /
  index.html, the staged rootfs/tpd removal.
- Encoder paused (user): firmware boot still silent after the fabric arbitration fix; next idea = build the GPL 2.6.35 kernel
  (`_work/cm7-2.6.35`) with msm_bus/vidc traces.

### Microphone: Linux reference capture (2026-11-09)

- RAM-booted `out/uImage-mic3` from bootie. ALSA exposes capture on `hw:0,1`. With the mixer defaults, four seconds of
  48 kHz stereo S16_LE capture produced 768000 bytes, all zero. During capture, codec power registers 0x0001/0x0004
  read 0x0000/0x0000, and the LPAIF mic I2S and write DMA control read 0x00000110 and 0x0000081f.
- Applied the previously documented internal-mic mixer route explicitly: IN1LN PGA on, IN1L on at 25, MIXINL IN1L on
  at 1, ADCL Mux ADC, Left Sidetone ADC/DMIC1, both AIF2DAC2 mixers Left Sidetone on, AIF2ADC Mux AIF2.
  A five-second capture produced 960000 bytes, of which 477424 were nonzero. LPAIF control values were unchanged.
  With codec regmap cache bypass, 0x0001/0x0002/0x0004/0x0005 read 0x0013/0x2240/0x3302/0x3300,
  0x0604/0x0605 read 0x0010/0x0010, and 0x0732 read 0x6140 (FLL2 locked).
- Next: verify that the recorded waveform follows a deliberate sound near the built-in mic. This run establishes nonzero
  capture with the explicit route. No webOS comparison yet. The mixer changes were made on the running test system.

- Voice check: the 20-second capture completed (3840000 bytes). Both channels peaked at one least significant bit in
  each of the first four seconds, and every byte in the entire capture was 0x00, 0x01 or 0xff. The preceding five-second
  capture also peaked at one bit (RMS 0.705). The nonzero-byte count did not establish a microphone signal.
  Resume the comparison with webOS while its recorder is running.

### Microphone: active webOS reference (2026-11-09)

- Stock webOS `arecord -D hw:0,0 -f S16_LE -c 1 -r 44100 -d 5 -t raw`: PCM RUNNING, 441000 bytes
  captured, including 239289 bytes outside 0x00, 0x01 and 0xff. The temporary audio was removed after counting.
  The active codec dump is in `data/webos/audioregs/wm8958-capture.txt`.
- LPAIF mic I2S_CTL = 0x00000110 and WRDMA_CTL = 0x0000081f, identical to mic3. The rest of the webOS WRDMA
  snapshot (offsets 0x04..0x14) was 0x28410300, 0x0000005f, 0x28410420, 0x0000002f, 0x00100018.
  The buffer and period sizes differ between the runs. Both codec mic clocks were enabled.
- Codec differences (webOS vs mic3): AIF2_CONTROL_1 0x8013 vs 0x4013 (bits 15/14 select the ADC source
  for each AIF2 channel), AIF2DAC LRCLK (0x315) 0x0003 vs 0x0020, power 0x0002/0x0004/0x0005
  = 0x6240/0x3002/0x0003 vs 0x2240/0x3302/0x3300. The two sidetone mixer registers match.
  Start by testing the AIF2 source controls one change at a time on mic3, with the same mono rate if supported.

### Microphone: webOS channel selection on Linux (2026-11-09)

- RAM-booted mic3 and restored the explicit internal microphone mixer route. Compared two mono S16_LE
  captures before and after setting AIF2ADCL Source to Right and AIF2ADCR Source to Left. The active codec
  register 0x0310 became 0x8013, matching webOS. Both captures had zero bytes outside 0x00, 0x01 and 0xff.
  The driver negotiated 48 kHz when asked for 44.1 kHz (192000 bytes in two seconds of mono capture).
- Enabled IN1LP alongside IN1LN as a separate follow-up to match webOS input mixer 0x28. The capture still
  had zero bytes outside those values. The channel swap and IN1LP alone did not recover audio.

### Microphone: active Linux clock check (2026-11-09)

- During stereo S16_LE capture at 48 kHz, Linux reports the codec mic OSR clock at 12.288 MHz and BCLK at
  1.536 MHz, consistent with 32 bit clocks per frame and the vendor configuration. LCC mic NS/MD at
  0x28000060/0x28000064 = 0xf5029f5a/0x000001f4. FLL2 locked (codec 0x732 = 0x6140).
  These are driver/register observations; the physical BCLK and LRCLK waveforms were not measured.
- Active codec AIF2 ADC LRCLK (0x314) is 0x0040 on both systems. The residual difference at 0x315 is
  AIF2 **DAC** LRCLK, not the ADC divider. The old WM8994 driver uses that register for capture only
  when its shared-LRCLK option is active. Verify which LRCLK signal the board uses before changing it.
- Raising IN1L Volume from 25 to 27 (matching the webOS gain field) still produced zero bytes outside
  0x00, 0x01 and 0xff in 384000 bytes. Restored volume to 25.

- Follow-up on mic3 with audible ambient noise: five-second stereo capture still had no bytes outside
  0x00, 0x01 and 0xff. MICBIAS1 (0x3d) read 0x0029. Running silent AIF1 playback concurrently locked
  both FLLs (0x732 = 0x61e0) but did not recover input. Enabling both AIF2 ADC high-pass filters to
  match webOS 0x510 = 0x1800 likewise did not recover input; restored the filter settings.
- The old codec driver selects shared LRCLK for AIF2 when GPIO6 is not in its pin-specific function.
  The active GPIO6 value is 0x2005 on both systems (jack detection), so the AIF2 DAC LRCLK register
  is still worth testing with a controlled codec change. No raw register writes were made.

- A diagnostic kernel set the AIF2 DAC LRCLK rate field to 3 at capture prepare. Docker build succeeded and
  the image RAM-booted. Verified live 0x0315 = 0x0003 and locked FLL2 (0x0732 = 0x6140). A four-second
  stereo capture still had zero bytes outside 0x00, 0x01 and 0xff (768000 bytes). Removed the diagnostic
  code after the test. The tablet and out/kernel/uImage still contain the diagnostic build.

- Analog monitor test on RAM-booted mic3: enabled IN1LN and the direct IN1L-to-left-output mixer
  route for 20 seconds at low gain. The owner heard a noise, without recognizable room sound, and
  reported sound on only one side afterward. Verified the four modified mixer controls were restored
  and the normal left/right DAC switches, output routes and gains were symmetric. Played the stereo
  test WAV three times to check audible output. The analog monitor result did not establish mic pickup.

- The owner confirmed the stereo test WAV was audible on both sides after the analog monitor test.
  Rechecked the built-in mic bias during mic3 capture: codec 0x01/0x3a/0x3d/0x3e = 0x0013/0/0x0029/0x002d,
  matching the active webOS dump. This verifies the register configuration only. The capture remained
  at the previous floor. The old webOS ASoC debugfs codec_reg interface has a write handler, and
  WM8994_OUTPUT_MIXER_1 bit 2 can route IN1L to MIXOUTL for a temporary analog monitor test.
- The postmarketOS TouchPad draft (https://gitlab.com/postmarketOS/pmaports/-/merge_requests/2791) used
  Evervolv's 3.4 kernel and reported I/O errors for both playback and capture.

- Stock webOS analog monitor: ran silent playback and capture on hw:0,0, confirmed both streams RUNNING,
  then briefly enabled the IN1L-to-MIXOUTL route (0x2d from 1 to 5, 0x2f from 0 to 7). The owner heard
  acoustic feedback, confirming that the built-in mic and speaker can form a live analog loop on this
  tablet under webOS. GPIO1 read 1 throughout the snapshots even after an attempted amp-enable write.
  The monitor registers were restored (0x2d/0x2f/0x700 = 1/0/1). The earlier Linux analog loop produced
  noise but no recognizable room sound or reported feedback, at a different gain and stream state.

- Linux duplex monitor comparison on mic3: silent stereo playback and stereo capture at 48 kHz were
  both RUNNING. Enabled both IN1L input legs, set the IN1L gain to the webOS field value, and enabled
  the left analog monitor for five seconds at minimum output mixer gain. The ten-second capture
  contained 1920000 bytes, none outside 0x00, 0x01 and 0xff. Mixer settings were restored. Audible
  monitor result pending the owner's observation.

- The owner heard no sound during the Linux duplex monitor. A follow-up read during a separate short
  Linux analog monitor with silent playback confirmed the route bit (0x2d = 0x0105), both input legs
  (0x28 = 0x0030), output power (0x03 = 0x3cf0), and MICBIAS1 enabled (0x01 = 0x0013).
  The remaining analog differences include the AntiPOP registers 0x38/0x39 and input power 0x02.
  Compare these under equivalent live conditions before changing the codec initialization.

- Matched duplex monitor register snapshots. Both systems had 0x01 = 0x0013, 0x03 = 0x3cf0,
  0x28 = 0x0030, 0x2f = 0x0007 and 0x3d/0x3e = 0x0029/0x002d; both had the IN1L-to-MIXOUTL bit set.
  Linux vs webOS: 0x02 0x2240/0x6240 (thermal shutdown enable), 0x18 0x001b/0x011b (volume update),
  0x29 0x0020/0x0021 (MIXOUTL feedback gain field), 0x38 0/0x0030, 0x39 0x018c/0x022c,
  and 0x208 0x001f/0x001e (codec system clock source bit differs). The webOS monitor was closed and
  registers restored. Full reference: data/webos/audioregs/wm8958-duplex-monitor.txt.

- Tested three Linux-like settings independently on stock webOS while capturing. For each 12-second
  mono run, changed one field for five seconds, verified the hardware readback, and restored it.
  VMID_RAMP cleared (0x39 0x022c -> 0x020c): 44390-44595 bytes per second outside 0x00, 0x01, 0xff
  in the measured test intervals. This only tests a live ramp field change, not power-up sequencing.
  SYSCLK_SRC set (0x208 0x001e -> 0x001f, both FLLs locked, silent playback active): 47074-48115.
  MIXOUTL-to-MIXINL feedback gain field cleared (0x29 0x0021 -> 0x0020): 46599-46859. None reproduced
  the Linux floor. Temporary recordings were deleted after counting. Next compare the codec state
  across cold and warm boots and trace the analog bias/power sequencing before capture starts.

- First mic3 capture after a warm boot from webOS via bootie: 768000 bytes (four seconds, stereo
  S16_LE, 48 kHz), with no bytes outside 0x00, 0x01 and 0xff. Idle codec regmap showed 0x01 = 0,
  0x39 = 0x0180, 0x208 = 0. Live capture read 0x01 = 0x0013, 0x39 = 0x018c, 0x208 = 0x000f and
  0x732 = 0x6140 (FLL2 locked). Cold boot comparison pending physical power-on into bootie.

- First mic3 capture after a full power-off and physical power-on into recovery: identical to the
  warm-boot run for all sampled codec registers, idle and active. Captured 768000 bytes, none outside
  0x00, 0x01 and 0xff. The boot history did not affect this failure under the tested conditions.

### LuneOS 6.18 comparison (2026-11-09)

- Reviewed shr-distribution/linux commit 37d5887cd67dae88b0eb37564adc8e6c0c39c609.
  The branch contains a direct LPAIF card as well as a Q6 path. Direct capture uses codec-mic port 1
  and WRDMA 5, consistent with mic3. The codec uses the same MICBIAS values and IN1LN routing.
- Their codec node maps AVDD2, CPVDD and LDO1VDD to PM8058 L4 (2.85 V). Our node maps AVDD2/CPVDD
  to S3 and leaves LDO1VDD undescribed. Their GPIO108 notes also conflict with our permanent use as
  LDO2 enable: their direct card lists GPIO108 as LRCLK after codec probe. Verify the board wiring and
  actual pin mux on our Wi-Fi unit before adopting either difference. No confirmed LuneOS mic capture found.

### LuneOS 6.18 RAM test preparation

- Built shr-distribution/linux commit 37d5887c for Topaz Wi-Fi in Docker with Q6 APR/ASoC and VIDC
  enabled, direct LPAIF card disabled, an existing project initramfs, and A6 disabled because its
  downstream programming code fails to compile (i_need_definition). Test-only recipe and log are in
  _work/luneos-audio. Output: out/luneos-test/uImage (SHA256 fbc5edadaa9b9c4c5d5fa48e8345097345a3a5e29c11e0473fd9ef3f90e106d6).
- Embedded q6.mdt and eight split files extracted from this tablet's webOS, and its 500140-byte VIDC
  firmware. The LuneOS driver comments describe a 605428-byte blob; compatibility still to be checked.
  Temporarily staged the two VIDC tools and 12-frame NV12 pattern under touchpad/luneos-ram-test
  on the media volume. Remove after testing.
- Novacom completed the RAM transfer from bootie. No USB device reappeared afterward, so kernel and
  userspace startup are unconfirmed and neither audio nor video test ran. Check the tablet display
  and retry from recovery. No kernel image was written to flash.

- Owner confirmed boot of the OTG image through penguin splash, shell and Alpine/Cog. USB and the
  previous Wi-Fi addresses did not respond from the PC. Built a second image with only the Topaz
  USB role changed from otg to peripheral, matching our kernel. Original saved as uImage-otg;
  new out/luneos-test/uImage awaits bootie. Q6 and H.264 validation are still pending.

- Forcing USB peripheral mode did not expose a USB console. Built an autonomous test initramfs
  that ran before Alpine init and wrote results.txt to the staged media folder. Recovered the log
  from our 6.12 mic3 RAM boot. VIDC encoded 11 frames (16604 bytes, first IDR, flags/timestamps PASS)
  using this tablet's 500140-byte firmware. Hardware decode returned six 320x240 visible frames,
  with distinct timestamps and EOS but no LAST; this is a partial decode, not a full round trip.
- Q6 did not register in that build. Found the common DTS unconditionally included the direct
  LPAIF override, which disables the Q6 remoteproc, APR and sound node independently of Kconfig.
  Removed that include in the isolated LuneOS test build to retry Q6.

- LuneOS 6.18 direct LPAIF RAM boot registered HP-TouchPad-LPAIF and exposed capture on hw:0,1
  via WM8994 AIF1. Both 48 kHz S16_LE mono and stereo failed on PCM read with EIO. The driver
  requested a 1.536 MHz bit clock but reported 1.500 MHz, and the mono machine driver supplied
  768 kHz as the FLL reference. No microphone signal validated. Returned to our mic3 RAM kernel
  to read the journal at touchpad/luneos-ram-test/mic-direct.txt.

- Q6-enabled LuneOS RAM image ran the test and rebooted automatically to Linux 6.12.40.
  The DSP loaded q6.mdt, started, registered APR AFE/ASM/ADM services and exposed two ALSA PCM
  frontends. Both arecord opens returned EINVAL. ASoC reports no backend DAI enabled for MultiMedia1.
  Next enable the MultiMedia1 Mixer SEC_MI2S_TX control and verify the codec capture route.
  VIDC again encoded 11 frames and decoded six. Tablet log: touchpad/luneos-ram-test/q6-results.txt.

- Q6 routing retry: enabled and read back MultiMedia1 Mixer SEC_MI2S_TX. hw:0,0 moved past
  open but both capture formats failed at hw_params. DSP rejects ASM_STREAM_CMD_OPEN_READ_V3
  (0x10db4) with status 3; the generic q6asm-dai log incorrectly says open_write even for
  capture. HP/CM7 uses legacy ASM_STREAM_CMD_OPEN_READ (0x10bcb) and its own payload.
  No audio recorded. The image rebooted automatically back to Linux.

- Legacy Q6 OPEN_READ test: sent HP opcode 0x10bcb with its four-field PCM payload. The first
  capture progressed to format setup, where the DSP rejected 0x10c10 with status 2. The second
  OPEN_READ returned status 9 after the failed setup. Use one capture per boot to isolate subsequent
  changes and compare the vendor PCM encoder configuration and teardown. Automatic reboot worked.

### Microphone: what the LuneOS tree says, and the next experiment (2026-11-09, read-only, tablet unreachable)

- LuneOS (`_work/luneos-audio/qcom-apq8060-tenderloin-common.dtsi`) is itself contradictory about GPIO 108: one comment says it is
  the primary I2S WS (LRCLK) switched to the I2S function after codec probe, another that it stays a GPIO output high (LDO2 enable)
  in a "BCLK only" mode. It muxes GPIO 109 (SCK) and 110 (SD0) to function `i2s` (8 mA); **our DT muxes none of 108-111**.
  Its codec supplies AVDD2/CPVDD/LDO1VDD come from PM8058 L4 (2.85 V), ours from S3 (1.8 V). Its direct-LPAIF capture gives EIO,
  so it proves nothing about the microphone by itself.
- Vendor CM 3.4 `board-tenderloin-gpiomux.c` configures only GPIO 111-114 (aux PCM, function 1) for audio; 108-110 are not touched
  ("boot configuration: pins are already muxed"). So the bootloader state is what the vendor relies on, and our `wlf,ldo2ena-gpios`
  on GPIO 108 may overwrite the WS pin when the codec driver sets it as a GPIO output (not yet shown to be the same net).
- Next experiment (needs the tablet): dump TLMM GPIO_CFG 102..115 (`physdump tlmm-cfg 0x00803598 0x38`, config at 0x3400 + 4 * n) and
  GPIO_IN_OUT (0x3E00 + 4 * n) under webOS while recording, then under mic3 while recording. If 108 (0x35B0) or 109/110 differ in
  function or direction, copy webOS; then try dropping `wlf,ldo2ena-gpios` (LDO2 is unused when DCVDD is on S3) and
  testing L4 for AVDD2/CPVDD. Nothing changed in the DT or the kernel yet.
- First reading (2026-11-09, tablet at 192.168.1.107, flashed kernel without capture, idle; TLMM of the 8660 is 0x00800000 + 0x1000 +
  0x10 * n: ctl, +4 io; the 0x3400 layout in the text above is the 8960's and reads 0): gpio108 ctl 0x201 (function gpio, output enabled,
  pull-down, reads 0), gpio109/110 ctl 0x001 (plain GPIO inputs, not I2S), gpio111 ctl 0x200. Playback works with 109 not in I2S
  function, so the codec BCLK/LRCLK are not carried by these pins; the webOS values while recording are still needed to know whether
  108-110 matter at all (reboot into webOS required).
- webOS TLMM (2026-11-09, idle, no recording running; `data/webos/audioregs/webos-tlmm-idle.txt`, ctl/io): gpio108 0x200/3 = GPIO, output,
  **driven high** (LDO2 enable on); gpio66 0x200/3 = output high (LDO1); gpio109/110 0x001/0 plain inputs with pull-down (same as Linux,
  so no I2S function on them in webOS either); gpio111-114 0x000. **Linux idle reads gpio108 = 0x201 / io 0: LDO2 is OFF** (and pull-down
  set). Candidate cause of the silent mic: the WM8958 LDO2 (DCVDD / analog side) is never enabled by our driver stack because no
  consumer holds the `wm8994-ldo.1` regulator. Test (live, no rebuild): while capturing on mic3, set GPIO 108 high
  (`devmem 0x008016C4 32 2`), then re-capture; if it helps, add a supply/hog in the DT.
- Live test on mic3 (RAM boot, 2026-11-09, tablet 192.168.1.107): Linux reads gpio66 and gpio108 as output low at idle (webOS: both high).
  With the explicit internal-mic route and GPIO 66 + 108 forced high through TLMM (`devmem 0x00801424 / 0x008016C4 32 2`, both read 3
  afterwards), a 3 s stereo 48 kHz capture still contains only 0x00 and 0xff bytes (identical to the baseline). **The LDO enable pins
  are not the cause**; the GPIO 108 / 109 / 110 idea is closed (webOS has the same GPIO function there). Remaining differences:
  analog power-up/AntiPOP sequencing (0x38/0x39, 0x208), the L4 supplies of LuneOS, and the ADC/AIF2 digital path.
- Live codec register test (2026-11-09, mic3, new tool `scripts/tools/wmreg.c`: raw WM8958 read/write on /dev/i2c-3 with I2C_RDWR; reads
  answer only while a stream keeps the codec powered, ENXIO otherwise). During a stereo capture, with the internal-mic route: 0x38 0 -> 0x30,
  0x39 0x018c -> 0x022c (reads back 0x002c) and 0x02 0x2240 -> 0x6240 (the webOS values) written live: the following 2.5 s still contain
  only 0x00 / 0xff. **AntiPOP, VMID ramp and thermal-shutdown bits are not the cause either.** Still open: PM8058 L4 supplies (needs a DT
  rebuild), and the digital path between the ADC and the LPAIF write DMA (webOS uses AIF1 for capture on hw:0,0: R4 0x3002 = AIF2ADC +
  ADCL only; our test uses AIF2 on LPAIF port 1 with `AIF2ADC Mux`).

### Q6 legacy PCM setup and read (2026-11-10)

- Legacy PCM encoder configuration reproduces the HP 0x10c2c parameter and padded config block.
  One mono 48 kHz capture moved past setup to PCM read, then returned EIO with zero bytes. The
  automatic reboot returned to Linux 6.12. Journal: touchpad/luneos-ram-test/q6-pcm-results.txt.
- Built a follow-up RAM image (out/luneos-test/uImage-q6-read) using legacy ASM READ 0x10bda and
  READ_DONE 0x10be0. This isolates the remaining protocol difference in transfer. Awaiting recovery.

- Legacy READ/READ_DONE test: capture still returned EIO with zero bytes; Q6 reached RUN.
  Journal confirmed the legacy path skipped memory mapping, unlike HP capture. Built
  out/luneos-test/uImage-q6-map with the HP map/unmap commands for contiguous capture buffers,
  preserving the legacy playback path. RAM test pending recovery.

- Legacy capture buffer map test: DSP acknowledged the map and reached RUN, but capture still
  returned EIO with zero bytes. Unmap returned DSP status 2. Found another compatibility gap:
  AFE uses legacy port 1 while ADM passes modern 0x1003 (endpoint 3 after truncation). The ADM
  matrix command also still uses a variable V5-style payload. Prepared an image with the HP
  capture endpoint and fixed-size matrix route payload (out/luneos-test/uImage-q6-adm).

- HP ADM route test: COPP opened on legacy port 1. arecord completed successfully for the first
  time (288000 bytes, three seconds of mono 48 kHz S16_LE). Zero bytes outside 0x00, 0x01 and 0xff.
  Transport now delivers PCM but microphone signal is not validated. Unmap still returned -22.
  Next explicitly set the WM8958 input and inspect live codec registers during Q6 capture.
  Automatic reboot returned to Linux 6.12.

- Explicit Q6 mixer route test: six-second mono capture completed (576000 bytes) with no bytes
  outside 0x00, 0x01 and 0xff. While PCM was RUNNING, WM8958 0x01 = 0x0003 (MICBIAS1 off),
  0x02 = 0x2000 (analog input path off), 0x204 = 0 (AIF2 clock off), 0x732 = 0x6040 (FLL2
  not locked). Analog switches and AIF2 sidetone mixer were set. The LuneOS DT used zero DAI
  cells for both links, defaulting to the first codec DAI. Built an isolated variant with
  explicit AIF1 playback and AIF2 capture (out/luneos-test/uImage-q6-aif2).

- Explicit AIF2 selection test: Q6 capture completed, 576000 bytes with no bytes outside
  0x00, 0x01, 0xff. AIF2 clock now enabled (0x204=0x0019), FLL2 locked (0x732=0x6140),
  and analog input/ADC powered (0x02=0x2240, 0x04=0x3302). MICBIAS1 still off (0x01=0x0003,
  versus 0x0013 on webOS). Next add the missing DAPM supply dependency for the internal mic.
  Journal: touchpad/luneos-ram-test/q6-aif2-results.txt. Automatic reboot worked.

### Stock webOS APR capture reference (2026-11-10)

- Dynamic debug works on the original webOS kernel. Temporarily enabled APR/AFE/ADM logs and
  preserved the existing ASM debug flags. A two-second mono ALSA capture at 44.1 kHz produced
  176400 bytes, 95319 outside 0x00/0x01/0xff. Removed the temporary audio and restored debug flags.
- The trace shows successful acknowledgements for OPEN_READ (0x10bcb), memory map (0x10c32),
  PCM config (0x10c10), ADM matrix route (0x10301), RUN (0x10bd2) and unmap (0x10c33).
  It contains 39 READ_DONE (0x10be0) events. The first returned status 0, 4800 PCM bytes,
  offset 0. ADM opens port 1 in mono at 48000 Hz with topology 0x10be3; our Q6 test used
  0x10312. The webOS trace explicitly reports no audproc or audvol calibration data to send.
- Separate active capture: LCC mic NS/MD 0xf5029f5a/0x000001f4, I2S_CTL 0x110, WRDMA_CTL 0x81f.
  Those clock and control registers match the earlier direct Linux readings. Codec live reads
  again show MICBIAS1 on (0x01=0x0013), AIF2 enabled (0x204=0x0019), FLL2 locked.
  The trace records incoming APR packets and driver diagnostics, not the entire outgoing payload
  or physical pin voltages. See data/webos/audioregs/webos-apr-capture-*.txt. Left tablet in
  webOS with the temporary boot selector removed and Linux still the moboot default.

- Stock webOS MICBIAS1 A/B/A: one continuous 44.1 kHz mono capture, PM1 0x0013 -> 0x0003 for five
  seconds -> 0x0013, verified by readback. Follow-up AC RMS measurements over 4096 samples per
  window were 136.60-166.79 counts before, 2.45-2.54 during, and 95.43-344.96 after. Ambient sound
  was not controlled, but the reduction and recovery were clear. Bias off alone did not reduce
  all samples to the Linux floor. Restored PM1 and deleted temporary audio. Logs:
  data/webos/audioregs/webos-micbias-ab.txt and webos-micbias-rms.txt.

- Combined webOS-reference Q6 test: explicit AIF2, internal-mic MICBIAS1 supply and ADM
  topology 0x10be3 all verified live (0x01=0x0013, 0x204=0x0019, 0x732=0x6140, COPP port 1).
  Six seconds mono S16_LE at 48 kHz completed (576000 bytes); no bytes outside 0x00, 0x01,
  0xff. The combined settings are insufficient. Compare Q6 READ_DONE status, address,
  valid length and offset against the working webOS trace before changing more codec fields.
  The diagnostic LuneOS callback currently reports a whole ALSA period for each completion.
  Journal: touchpad/luneos-ram-test/q6-webos-ref-results.txt. Automatic reboot worked.

### Direct Linux capture DMA inspection (2026-11-10)

- RAM-booted mic3 and enabled the documented internal microphone mixer route, including both
  IN1L pins and the webOS-like AIF2 source swap. Six-second stereo 48 kHz S16_LE capture ran.
  WRDMA5 base was 0x43090000 and ring size 49152 bytes. Confirmed the entire ring lies within
  /proc/iomem System RAM before reading. The DMA current pointer changed across three reads.
- Three read-only 4096-byte samples from the ring base and the 1152000-byte ALSA recording
  contained no bytes other than 0x00, 0x01 and 0xff. This is consistent with the signal being
  absent by the DMA buffer, but it is not a byte-for-byte simultaneous comparison of the
  complete ring, and /dev/mem CPU cache visibility was not independently verified. Temporary
  raw audio and DMA samples were removed. No LPAIF register was written.

### Microphone works: AIF2 output was muted (2026-11-13)

- **Cause of the silent capture**: the WM8958 AIF2 output volume is muted by default. `DAC2 Switch` (registers 0x612/0x613,
  bit 9; regmap default 0x02c0) mutes the DAC2 mixer output, which is also the AIF2 ADC (capture) stream in the
  AIF2DAC2 mixer route. webOS records with 0x612/0x613 = 0x00c0/0x01c0. None of the earlier tests changed it.
- Test on the running flashed kernel (6.12.40, mic3 capture link `hw:0,1`, USB serial, tablet on the PC port, 36 %):
  - documented route only (IN1L PGA IN1LN, IN1L 25, MIXINL IN1L 1, AIF2DAC2L/R Mixer Left Sidetone): 768000 bytes, none
    outside 0x00/0x01/0xff (the known floor, reproduced);
  - plus `DAC2 Switch` on (only change): 405178 bytes outside the floor, AC RMS about 200;
  - MICBIAS1 A/B/A in one 9 s capture (`wmreg 3 w 0x01 0x0003`, then the read value 0x0013 back): AC RMS 183-207 before,
    **9-11 with the bias off**, 144-271 after. Same signature as webOS (137-167 / 2.5). The signal is the microphone.
    (`Int Mic Switch` off is not a bias test: it powers the whole input path down and gives exact zeros.)
- Gain staging: webOS uses MIXINL IN1L 0 dB (0x29 = 0x0021) and `DAC2 Left Sidetone Volume` 0 dB (0x603 = 0x000c, the field
  of our Left Sidetone route), IN1L field 27. Our earlier values were +30 dB then -36 dB (0x603 default 0). With the webOS
  values (`MIXINL IN1L Volume` 0, `DAC2 Left Sidetone Volume` 12, `IN1L Volume` 27): room AC RMS 326-745. A first
  "speaker pickup" window (AC RMS 2228, peak 22562) coincided with the live gain change, so it was most likely that
  transient, not the test sound (corrected after the clean test below).
- Working capture mixer settings (on top of the defaults): `IN1L PGA IN1LN Switch` on, `IN1L Switch` on, `IN1L Volume` 27,
  `MIXINL IN1L Switch` on, `MIXINL IN1L Volume` 0, `AIF2DAC2L Mixer Left Sidetone Switch` on,
  `AIF2DAC2R Mixer Left Sidetone Switch` on, `DAC2 Left Sidetone Volume` 12, `DAC2 Switch` on.
- **Applied at boot by `tp`** (`capture_path` in `sound_init()`, rootfs/tp/sound.c, same day). Built in Docker (tp-bin stage,
  no warning), copied over the USB serial shell (gzip + base64, md5 checked; Wi-Fi was on another subnet and the ECM link is
  still broken) to /usr/local/bin/tp on the tablet's rootfs, old binary kept as /usr/local/bin/tp.bak-20261113. With every
  capture control first reset to its default, restarting the daemon set them all ("sound: mixer path set", no failed
  control) and the codec read 0x18/0x29/0x603/0x612/0x613 = 0x001b/0x0020/0x000c/0x00c0/0x00c0, the webOS values (0x613
  0x1c0 on webOS differs only by the volume update bit).
- Clean speaker test (nothing else changing): 8 s capture, panel test sound (1 s) played three times about 2 s apart:
  AC RMS 600-725 in the sound windows, 93-138 in the quiet ones (0.5 s, 3.0 s, after 6.5 s); the 4.5-5.5 s gap stayed at
  about 410. Pickup of the speaker confirmed at a modest level (speaker at DAC1 -12 dB).
- Not tested: headset microphone (IN2LN, MICBIAS2), recording quality by ear, the rootfs image rebuild (the tablet has the
  hand-copied binary only), the Q6 path (not needed any more).

### RTSP test server, first userspace H.264 path (2026-11-13)

- Test tool, not in the rootfs image: go2rtc 1.9.7 + ffmpeg 6.1.2 from Alpine 3.21 (`apk add go2rtc v4l-utils`, about 61 MB
  in the rootfs, 105 MB left). Config, workaround script and install notes: `scripts/rtsp/`. Other candidates looked at:
  live555 `live-media-utils` (1.3 MB, but demo programs fed from a file or pipe), mediamtx (not in Alpine 3.21).
- **ffmpeg `h264_v4l2m2m` drives our qcom-vidc encoder unchanged**: it picks /dev/video7 (-> video2, "Qualcomm VIDC 1080p
  Encoder", mplane, NV12 in, H264 out), first try. Camera NV12 640x480 -> encoder at 1 Mbit/s: 3 s file, 90 frames,
  H.264 Constrained Baseline, decodes without error, image correct (rotated 90 degrees, sensor orientation).
- go2rtc stream `cam` (camera + microphone) recorded from the PC (192.168.10.45 reaches the tablet at 192.168.1.108):
  8 s, H.264 640x480 102 frames (about 13 fps: the camera delivers about 15 fps, 32 frames per 2 s), AAC LC mono 48 kHz
  351 frames, room sound mean -36.6 dB / max -17.8 dB. A few audio DTS warnings from the muxer, harmless for tests.
- **Camera start bug (new)**: VIDIOC_STREAMON fails about half of the time with `VFE reset 0x3ff: no acknowledge
  (status1 0x00000000)` (msm8660_vfe.c `vfe_start` -> `vfe_reset`, 50 ms poll), before the sensor is started. 8 attempts
  in a row: OK on 3, failure on 5 (no strict alternation), also on the first start after a boot. A new open usually works
  (a `retry-start` wrapper was used in the go2rtc config until the fix below). Not caused by the encoder: it happens with
  the encoder never opened and the MMSS registers at their boot values. Earlier camera tests started it once, which is
  why it was not seen. **Fixed the same day**, see "Camera start fix".
- MMSS registers rewritten by `vidc_legacy_power_on()` (boot -> after an encode): MAXI_EN 0x17fc0001 -> 0x17fe07f9,
  MAXI_EN3 0x00200400 -> 0x3fe7fcff, SAXI_EN 0x3f8 -> 0x1d8, AHB_EN2 0x1 -> 0x7f9. Not the cause of the camera bug, but
  shared hardware-gating settings to keep in mind for power management.
- Pitfall paid for: a `reboot` of the tablet falls back to the **flashed** kernel (#36, 2026-11-08, no capture link, no
  vidc); the session was running the RAM-booted `out/uImage-vidc1` (#44). Restored with the cycle.sh sequence (recover
  reason in IMEM + PS_HOLD over SSH, attach 0830:8071, `novacom boot mem://`).
- Also checked across that reboot: the new `tp` binary and the microphone mixer path survive (rootfs file is rw, `tp`
  sets the path at start).

### Camera start fix: reset acknowledge through the interrupt (2026-11-13, `out/uImage-vfefix3`, md5 e4702b54...)

- **Cause**: `vfe_reset()` masked every VFE interrupt, wrote `GLOBAL_RESET` and polled `IRQ_STATUS_1` bit 22. The status
  register does not reliably latch the reset acknowledge while that interrupt is masked, so the poll saw it only on about
  half of the starts. The vendor `vfe31_reset()` (msm_vfe31.c) enables only the acknowledge interrupt
  (`VFE_IMASK_WHILE_STOPPING_1` = 0x00400000) before the reset command and receives it in the interrupt handler.
- **Fix** (msm8660_vfe.c): the same sequence; `vfe_irq()` completes `reset_done` on bit 22, `vfe_reset()` waits for it
  (100 ms), then masks it again. Used by the start (`RESET_ALL`) and the stop (`RESET_STOP`).
- Ruled out on the way (diagnostic kernels `vfediag1`, `vfefix1`, built and RAM-booted): every MMSS clock, halt (vendor
  `DBG_BUS_VEC_B/E/F` halt bits: VFE core, CSI1->VFE, AXI, AHB all running) and block reset register identical on good and
  bad starts; the acknowledge never came late (polled for 1 s); a full power cycle retry helped only by chance; the vendor
  memory sleep/wake-up delays in VFE_CC (0x80FF0000) and a vendor-style block reset (`SW_RESET_CORE` bits 15/23/24,
  `SW_RESET_AHB` bit 0, `SW_RESET_AXI` bit 9 pulsed with clocks on) changed nothing; PLL2 (the VFE clock source in the webOS
  NS value) is held on by the GPU.
- Result: 32 starts out of 32 on two kernels with the fix (16 + 16, `ffmpeg` 2 s captures, 26-33 frames each, 0 error
  interrupts), then 3 RTSP sessions in a row from the PC through go2rtc without any wrapper (H.264 61-70 frames + AAC
  each), 0 go2rtc warnings, 0 "no acknowledge". Before: 3/8, 5/8, 8/16 at the first try.
- Only in the RAM image; not flashed. Kernel built with the arm64 toolchain image restored (see the pitfall below).
- Pitfall paid for: `docker run --platform linux/arm/v7 alpine:3.21` (package probing) replaced the local `alpine:3.21`
  content, including the digest pinned by the Dockerfile, with the armv7 image: the `kernel-env` stage was rebuilt as
  armv7 (32-bit gcc), the kernel object cache was invalidated and the full rebuild took about an hour. Restored with
  `docker image rm alpine@<digest>` then `docker pull --platform linux/arm64 alpine:3.21@<digest>`; check with
  `docker buildx build --target kernel-env --load -t x . && docker image inspect x --format '{{.Architecture}}'`.

### Audio and H.264 stop point (2026-11-10)

The owner stopped work. Last observed tablet state: mic3 (6.12.40) booted in RAM, with the
direct internal-mic mixer settings applied for the DMA inspection. Temporary raw captures
were removed. Stock-webOS APR, codec, clock and MICBIAS A/B/A references are in
data/webos/audioregs/. Direct DMA ring samples and ALSA PCM stayed near zero, but CPU cache
visibility and a complete-ring comparison remain unverified. The isolated LuneOS Q6 test
enabled AIF2, MICBIAS1 and the webOS ADM topology; capture completed but PCM stayed near zero.
Q6 READ_DONE length, offset and address still need comparison with the webOS trace.

H.264: our 6.12 Samsung MFC v5 variant remains paused with firmware boot unresponsive.
LuneOS 6.18 at commit 37d5887cd67dae88b0eb37564adc8e6c0c39c609 uses its dedicated
drivers/media/platform/qcom/vidc driver. RAM-booted on this same tablet with the stock webOS
500140-byte firmware at firmware/webos/vidc/vidc_1080p.fw, its VIDC encoder produced a
16604-byte H.264 stream of 11 frames from a synthetic moving 320x240 NV12 pattern. The test
reported first-frame IDR, frame flags and timestamps passing. Its hardware decoder returned
six frames with EOS but no LAST, so full decoding and drain are not validated. A second run
reproduced 11 encoded and six decoded frames. The upstream LuneOS audit and test harness are
at reports/vidc-encoder-upstream-audit-2026-05-25.md and reports/vidc-tests/ in that tree.
Our isolated sources, scripts and logs are in git-ignored _work/luneos-audio, and RAM images
are in out/luneos-test. The tablet media holds touchpad/luneos-ram-test with test tools,
synthetic video and logs. When requested, inspect the LuneOS driver, firmware loading,
reserved SMI layout and interconnect configuration before deciding how to integrate VIDC
into 6.12. Observe firmware licensing during any release review.

### H.264: comparison with the LuneOS 6.18 VIDC driver (2026-11-10)
Sources extracted in git-ignored `_work/luneos-vidc` (from `_work/luneos-audio/linux-37d5887c.tar.gz`). Differences from our
`_work/mfc-edit` bring-up, to try next (not yet tested):
- **Fabric bus clocks**: their interconnect provider (`drivers/interconnect/qcom/msm8660.c`, 1008 lines, not in 6.12) sets the RPM
  fabric clocks (`afab`, `mmfab`, `ebi1`, plus `smi`) to at least 384 MHz on every vote. We only copied the MM/APPS ARB words and
  never set these rates. The 6.12 `clk-rpm` already has `afab/mmfab/ebi1/smi` for msm8660: set their rates from the codec driver.
- **Firmware copy order**: they copy the firmware into SMI *after* AXI halt/reset/burst config and before releasing the RISC
  (earlier CPU writes to SMI are dropped); the copy is also repeated at every boot. Ours is copied at load time, before the reset sequence.
- Release value `SW_RESET = 0x3ff` (ours 0x3fb); reset normalization 0x3fe -> 0x000 -> 0x3fe with 1 ms sleeps; 10 ms before the AXI halt.
- Five codec clocks (core, iface, axi, axi_a, axi_b), axi_a/b kept on for the driver's life; VED power domain kept on.
- Firmware blob endianness is detected per image (word swap if the header is little-endian); the stock image loads verbatim.
- Built `out/uImage-mfc15` (`DTB_NAME=qcom-apq8060-hp-touchpad-kms ./build.sh kernel`; patch 0023 regenerated from `_work/mfc-edit`): fabric clocks
  (`mmfab/afab/ebi1` + `_a`, 384 MHz, DT clock-names), firmware re-copied into the SMI after the AXI init, reset pulse 0x3fe/0/0x3fe, 10 ms
  before the AXI halt, release with SW_RESET 0x3ff. Module parameter `qcom_lune` (default 3: bit 0 pulse + re-copy, bit 1 release 0x3ff)
  and `qcom_stage` (4 = full boot). Not yet booted. `./build.sh all` failed in the rootfs stage (apk I/O error on libLLVM: Docker VM disk is
  full, 19 GB of reclaimable build cache; never prune the volumes, the Buildroot objects live there).
- 2026-11-10 test results (RAM boots `out/uImage-mfc15..19`, firmware delivered by the initramfs: `VIDC_FW=webos ./build.sh kernel` copies
  `firmware/webos/vidc/vidc_1080p.fw` into the initramfs, and `initramfs/init` hands it to the ext4 rootfs before switch_root; serial
  copy (`tablet-put.sh`) and the USB ECM link do not work for 500 KB: the serial loses data, host rx stays 0 on usb0):
  1. fabric clocks (mmfab/afab/ebi1 and `_a`, 384 MHz; `clk_get_rate` reads 2147483647 for all but ebi1), SMI re-copy after the AXI init,
     reset pulse and release 0x3ff (mfc15-17): **no change**: no FW_STATUS_RET in 20 s, MC_STATUS 1, AXI status 0x20/0x30 alternating,
     reg 0x70 = 7. After the re-copy a readl of the SMI buffer gives 0 (write-only, as LuneOS notes).
  2. **The MSM8660 mmcc table (patch 0013) has no VCODEC clocks at all**: the DT `VCODEC_*` clocks resolve to nothing (NULL, the driver
     pokes the registers itself); `VCODEC_AXI_A/B_CLK` "not given". mfc19 sets 0x114 bits 25/23 (axi_a/axi_b gates) by hand: **no change**.
  Still different from LuneOS: their bandwidth votes are computed by an interconnect provider (our MM/APPS ARB words are a webOS snapshot
  taken while a video played), the GDSC/pm_runtime handling, and the firmware + context pool + descriptor + shared memory layout in the SMI
  (we only put the firmware there). Next: port their `vidc` driver (route A) or log their exact RPM/register sequence on the same tablet.

### H.264 hardware: LuneOS `qcom-vidc` driver on 6.12, WORKS (2026-11-10, branch `vidc-luneos-port`, image `out/uImage-vidc1`)
- The six routes tried with the s5p-mfc variant all failed the same way (see above). Route A: the LuneOS 6.18 `drivers/media/platform/qcom/vidc`
  driver (6 files, GPL, from `_work/luneos-audio/linux-37d5887c.tar.gz`) imported as `kernel/src/drivers/media/platform/qcom/vidc/` (+ patch 0027
  Kconfig/Makefile hook, `CONFIG_VIDEO_QCOM_VIDC=y`, s5p-mfc not built: same compatible `qcom,msm8660-vidc`).
- Ported to 6.12: only `v4l2_fh_add/del` lost their `file` argument. Added `vidc_legacy.c`: the mmcc of this tree has no VCODEC clocks / VED
  power domain and there is no interconnect provider, so the MMSS registers are written directly (footswitch, vcodec clock, MAXI_EN, AXI A/B
  gates 0x114, block reset), plus the RPM MM-fabric unhalt, the MM/APPS ARB snapshot, the SMI clock and the fabric clocks (DT `qcom,mmcc`,
  `qcom,rpm`). The LuneOS clocks (core/iface/axi/axi_a/axi_b) and ICC paths are optional/NULL here ("video-smi ICC path is NULL" is expected).
- DT: reserved memory `vidc_fw_mem` 0x38000000 3 MiB + `vidc_smipool_mem` 0x38300000 61 MiB (index order fixed), firmware `qcom/vidc_1080p.fw`.
- **Result**: with the tablet test tools (`touchpad/luneos-ram-test`, `ln -s /dev/video2 /dev/video7`; `VIDC_LINEAR=1 vidc_encode pattern.nv12 out.h264
  /dev/video2`): ENCODE 11 frames, 16604 bytes, IPPPPPPPPPP, frame 0 IDR, timestamps and flags PASS (same as LuneOS 6.18). `vidc_capture out.h264
  dec.tile /dev/video1`: DECODE 6 frames 320x256 at 67.7 fps, timestamps PASS, EOS yes, LAST no (same partial result as LuneOS).
- So the missing piece of the s5p-mfc variant was the driver's own boot logic (firmware + context pool + descriptor + shared memory layout in the
  SMI, its reset/boot order), not the clocks or fabric votes: the legacy power-up (our webOS-snapshot ARB) is enough.
- Firmware delivery: `VIDC_FW=webos ./build.sh kernel` puts `firmware/webos/vidc/vidc_1080p.fw` in the initramfs as `lib/firmware/qcom/`, `initramfs/init`
  copies it to the ext4 rootfs before switch_root (the file is not redistributable: never in a release).
- Open: decoder LAST flag, a real integration (GStreamer/ffmpeg v4l2 m2m? Cog/WPE hardware path), power management and run-time suspend with
  the legacy power-up, replacing the legacy shim by proper mmcc clocks + VED GDSC + interconnect provider, cleanup of the `_work/mfc-edit` route
  (patch 0023 can be dropped).
- 2026-11-13: patch 0023 (s5p-mfc variant) moved to `kernel/patches/removed/0023b-...` (renamed: removed/ already holds an old 0023);
  DT and config comments updated. Decoder "LAST no / 6 of 11 frames": likely a test tool bug, not the driver. `vidc_capture` left its
  loop on the EOS event after a single CAPTURE DQBUF, while the driver queues the drained frames and the LAST buffer before EOS (DPB_EMPTY
  path in `vidc_dec.c`). Tool fixed in `_work/luneos-audio/vidc_capture.c` (stops on LAST, dequeues every ready CAPTURE buffer per poll),
  rebuilt static armv7 (`docker run --platform linux/arm/v7 alpine:3.20 ... gcc -static`), copied to the tablet media
  `touchpad/luneos-ram-test/vidc_capture` (old one kept as `vidc_capture.old`).
- **Tested 2026-11-13** (RAM boot `out/uImage-vidc1`, unchanged kernel): encode PASS (11 frames), decode **11 frames 320x256, 105 fps,
  timestamps PASS, LAST=yes** (CAP[11] payload 0, flags 0x104001) -> full round trip, decoder drain OK, no driver change needed.
  Session notes: after `docker start` novacomd saw nothing until `docker restart hptouchpad-novacom`; the WSL kernel (6.18) had no
  cdc_acm loaded: `wsl -d Alpine -u root -- modprobe cdc_acm` before `tablet-cmd.sh`.
- Remaining H.264 work: userspace integration (GStreamer/ffmpeg v4l2 m2m, Cog/WPE path), power management, proper mmcc clocks + VED GDSC +
  interconnect instead of `vidc_legacy.c`.
