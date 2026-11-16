# Mainline Linux + Cog on the HP TouchPad (APQ8060)

Goal: mainline Linux kernel + WPE WebKit/Cog on the HP TouchPad.
Everything is built in Docker: nothing is installed on the host (except `usbipd-win` to pass the tablet to Docker).
The project is the kernel and the device tree; the userspace is an Alpine Linux root filesystem made of prebuilt packages (apk), with a few
adjustments (touch screen driver, one Cog module fix).
## What was done

- **Build chain**: one multi-stage `Dockerfile` (Alpine) builds the kernel, the initramfs, the root filesystem and our small programs;
  `./build.sh` is the only entry point, nothing is installed on the host.
- **Kernel 6.12.40** (`qcom_defconfig` + `kernel/touchpad.config`) with a series of 26 patches and about 15 out-of-tree drivers
  (`kernel/src/`), and two device trees (`kernel/dts/`). Drivers written or ported for this tablet: Scorpion ACPU clocks, SAW CPU
  regulator, PM8058 LPG backlight PWM, A6 battery gauge, charger, LM8502 Home LEDs, MT9M113 camera + MSM8660 VFE capture, LPASS LPAIF
  audio + machine driver, `qcom-vidc` H.264 codec (from LuneOS, with a legacy power-up shim). Fixes: SDCC SDIO, ath6kl legacy scan,
  TSENS MSM8660, MDP4 parallel RGB, Adreno 220 power cycle, PS_HOLD / Palm restart reasons.
- **Userspace**: Alpine 3.21 prebuilt packages + a BusyBox init with `/etc/tp.d` start scripts, `ts_srv` (touch screen driver in
  userspace), `tp` (power, battery, Wi-Fi, Bluetooth, sound, camera daemon / client / CGI), the settings panel page served to Cog, `tp-rtsp`
  (optional H.264 + audio RTSP server), one Cog module fix. WPE WebKit + Cog render full screen on the GPU.
- **Install and recovery**: install next to webOS through moboot (`docs/FLASHING.md`), RAM boot without touching the flash
  (`scripts/cycle.sh`), serial and SSH remote shell, `debrick/` toolbox for a deeply discharged battery.
- **Notes**: hardware description in `docs/HARDWARE.md`.

## What works

| Block | State |
|---|---|
| Boot, restart, power off | Works (moboot entry, RAM boot, clean restart and power off) |
| eMMC | Works; the rootfs is an ext4 file on the webOS media volume |
| Display | Works: msm DRM/KMS 1024x768, PWM backlight |
| GPU | Works: Adreno 220 (freedreno), Cog on the GPU with a software fallback |
| Touch screen | Works on the live area (userspace `ts_srv`) |
| Wi-Fi | Works: ath6kl SDIO, scan, WPA2, DHCP, SSH, saved networks reconnect at boot |
| CPU, thermal | cpufreq up to 1188 MHz (boost switch in the panel), TSENS with a passive trip |
| Battery, charger | Works: A6 gauge, SDP/CDP/DCP detection, charge window in the panel |
| Buttons, LEDs | Power, volume, Home; Home LEDs |
| Settings panel | Works: Home x3 opens it; screen, power, Wi-Fi, Bluetooth, sound, camera, Cog options |
| Sound playback | Works (speaker, volume, mute, test); LPAIF without the Q6 DSP |
| Microphone | Works: internal mic, S16_LE stereo 48 kHz |
| Camera | Works: `/dev/video0` NV12 640x480, about 11-15 fps |
| H.264 | Works: hardware encoder and decoder (`/dev/video1`, `/dev/video2`), ffmpeg `h264_v4l2m2m` drives the encoder |
| RTSP | `tp-rtsp` tested with a test client (video, audio, several clients); opt-in with `RTSP=1` |
| Remote shell | USB serial and SSH over Wi-Fi |

## Not handled or not finished

- **System suspend**: none; the tablet draws about 1 A when running (the panel switches screen and touch controller off).
- **Bluetooth**: classic only (CSR BlueCore 6), controller up, but no real pairing was tried.
- **Sound**: headphones, jack detection and the headset microphone are not tested.
- **Camera**: only 640x480 (replay of webOS register values), no rotation in the driver (the image is rotated by 90 degrees), about
  one start in four gives dark frames at 3 fps; no rear camera on this model.
- **Codec power management**: `vidc_legacy.c` writes MMSS registers directly and never powers the codec down.
- **Kernel quality**: several patches are field fixes, not upstream quality.
- **Charger**: PC port after boot, the HP 10 W adapter and the Touchstone dock are not tested.
- **Browser**: heavy pages are slow (no JIT in the Alpine WPE WebKit build for 32-bit ARM).
- **Mesa**: with Mesa >= 22.3 the right column / bottom row of a draw was not rendered (not rechecked on Alpine 3.21).
- **USB network (ECM)** does not work (serial and Wi-Fi are used); **sensors, vibrator, torch, HDMI** not started.
- **Touch edges**: the first unit has dead edge electrodes (hardware).
- **Firmware** read from the stock webOS (Wi-Fi board data, Adreno, vidc) cannot be redistributed: build with your own copy
  (`firmware/`, `BOARD_DATA=`, `ADRENO_FW=`, `VIDC_FW=`).
- **Not yet checked**: the one-hour `tp-rtsp` run, VLC/ffplay playback, speech intelligibility.

## Layout

| Path | Role |
|---|---|
| `Dockerfile` | the whole build chain, one multi-stage file on Alpine: programs, initramfs, kernel, root filesystem, artifacts |
| `docker/novacom/Dockerfile` | image with `novacomd` + `novacom` built from the openwebos sources |
| `build.sh` (`build.ps1`) | entry point: chooses a stage of the Dockerfile and passes the settings (`./build.sh help`) |
| `scripts/build/` | the steps run inside the stages (kernel, uImage, initramfs) |
| `scripts/` | host helpers: `cycle.sh`, `tablet-*.sh`, `novacom*.ps1`, `collect-webos.sh`, `check-patch-series.sh`...; `scripts/tools/`: probes and register dump tools |
| `kernel/dts/` | TouchPad device trees (copied into the kernel tree at build time) |
| `kernel/patches/` | the kernel patch series (0001..0027, applied in order by `scripts/build/kernel.sh`; `scripts/check-patch-series.sh` hashes the patched sources) |
| `kernel/src/` | out-of-tree driver sources copied into the kernel tree (Scorpion ACPU clock, MSM8660 SAW CPU regulator, PM8058 LPG PWM, A6 battery, charger, LM8502 LEDs, MT9M113 camera + MSM8660 VFE capture, sound: LPASS LPAIF `lpass-msm8660.c` and the machine driver `tenderloin.c`) |
| `kernel/touchpad.config` | Kconfig fragment applied on `qcom_defconfig` |
| `initramfs/` | first-boot initramfs (screen shell, USB gadget, Wi-Fi/Bluetooth tools, the `tp` command) |
| `rootfs/` | what goes on top of Alpine: `overlay/` (BusyBox init, `/etc/tp.d` start scripts), `ts_srv/` (touch screen driver), `tp/` (the `tp` program), `bluetooth/` (BlueCore PSR), `cog-patches/` |
| `docs/` | [HARDWARE.md](docs/HARDWARE.md): the tablet's hardware, addresses, GPIOs, rails; [FLASHING.md](docs/FLASHING.md): eMMC layout, what to keep, steps to install Linux on the flash; [SETTINGS-PANEL.md](docs/SETTINGS-PANEL.md): the `tp` daemon and the settings panel; [RTSP-SERVER.md](docs/RTSP-SERVER.md): `tp-rtsp`; [PUBLIC-RELEASE.md](docs/PUBLIC-RELEASE.md) |
| `debrick/` | RAM-booted toolbox to force the charge of a deeply discharged tablet |

The kernel sources and objects live in a BuildKit cache (not on the host filesystem).

## Build

Everything is built in Docker (BuildKit); from Git Bash, Linux or macOS (`build.ps1` forwards to `build.sh`):

```sh
./build.sh                # uImage + rootfs-alpine.ext4 with checksums and BUILD-INFO: out/artifacts/
./build.sh kernel         # one piece: kernel (out/kernel/), initramfs, rootfs, tp, ts-srv, bccmd, cog
./build.sh release        # the same from a clean committed tree, as out/release/<date>-<commit>/ (--dirty to allow uncommitted changes)
./build.sh menuconfig     # reconfigure the kernel (out/kernel.defconfig); ./build.sh shell: a shell in the toolchain image
./build.sh help
```

Settings are environment variables: `RELEASE=1` (no developer key in the image), `BOARD_DATA=upstream|webos|none` (Wi-Fi board data),
`ADRENO_FW=webos`, `TOUCHPAD_PASSWORD`, `KERNEL_VERSION` (with `KERNEL_SHA256`), `KERNEL_BASE`, `DTB_NAME`, `FIRMWARE_DIR`, `SSH_PUBKEY`.
The image has a user `touchpad` (password `alpine` unless `TOUCHPAD_PASSWORD`, `doas` to root); without `RELEASE=1` the project key
(`secrets/touchpad_ed25519.pub`) also lets root in over SSH. The ARM stages run under QEMU when the host cannot run 32-bit code.
`uImage` goes to the flash through moboot (`docs/FLASHING.md`); `rootfs-alpine.ext4` is copied to the webOS media partition as
`touchpad/rootfs-alpine.ext4` and selected by `touchpad/rootfile`. `scripts/cycle.sh out/kernel/uImage` boots a kernel in RAM without writing anything.

Note: `kernel/touchpad.config` is merged again into the kernel config whenever it changes. Kernel patches in
`kernel/patches/` are applied by `scripts/build/kernel.sh` on a pristine copy of the files they touch.

## novacom (Docker + usbipd-win)

The Windows Novacom driver has no ARM64 support (its `.inf` only has `NTx86`/`NTamd64` sections),
so `novacom` runs in the `hptouchpad-novacom` container and the tablet is passed to it with `usbipd-win`:

```powershell
docker build -t hptouchpad-novacom docker/novacom
usbipd list
usbipd bind --busid <id>              # admin, once per USB PID (webOS and recovery have different PIDs)
usbipd attach --wsl --busid <id>      # redo after every re-plug or mode change
.\scripts\novacom-up.ps1              # persistent container running novacomd, then lists devices
.\scripts\novacom.ps1 -l              # docker exec ... novacom
docker exec hptouchpad-novacom sh /work/scripts/collect-webos.sh   # hardware dump of the stock webOS
```

## Boot a build

1. Power the tablet off completely (hold Power + Home about 30 s if it is hung).
2. Recovery mode: with the USB cable plugged in, hold Volume Up, press Power, keep Volume Up until the big USB symbol appears.
3. `usbipd attach --wsl --busid <id>` (the recovery PID is `0830:8071`), then:

```powershell
.\scripts\boot.ps1                  # boots out/uImage into RAM, the eMMC is not touched
```

The kernel image is loaded in RAM only; a power cycle always brings back webOS.
