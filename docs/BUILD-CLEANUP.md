# Build chain cleanup (plan)

Goal: one entry point (`make`), one build image, Alpine as the only userspace, a chain that runs unchanged on a clean clone and in
GitHub Actions. Nothing here is done yet: this file is the list, to be ordered and agreed before any change.

## Today (see README, "Build")

* Chain 1, in the Debian build image (`build.sh` -> `scripts/build.sh`): `initramfs.sh`, `kernel.sh`, `uimage.sh`.
* Chain 2, started from the host, each script runs its own `docker run` (Alpine armv7 under QEMU): `build-ts-srv.sh`, `build-tp.sh`,
  `build-cog-alpine.sh`, `alpine-rootfs.sh` (which also extracts files from `out/initramfs.cpio.gz`).

## Problems found

1. **Debian leftovers.** Build image `FROM debian:bookworm`; the initramfs takes `busybox-static`, `iw`, `bluez`, `libnl`, `dropbear`,
   `wireless-regdb` and glibc from Debian armhf packages (`apt-get download` at every build: network, not reproducible). Two userspaces
   (glibc and musl) for one project.
2. **Two ways of running containers**: the wrapper `build.sh`/`build.ps1` for chain 1, hand-written `docker run` in each script of chain 2.
3. **Version of Alpine (3.21) written in 5 places** (three `build-*.sh`, `docker/alpine-rootfs/Dockerfile`, path `out/cog/alpine3.21`).
4. **Hidden coupling rootfs <- initramfs**: `alpine-rootfs.sh` extracts `busybox`, `bccmd` and `bluecore6.psr` from the cpio, but `bccmd`
   and the PSR only exist with `INITRAMFS_TOOLS=full`, while the release uses the default. To check first: it may break the release.
5. **`firmware/` is git-ignored** (stock webOS files, not redistributable): a clean clone cannot build the initramfs
   (`bdata.SD32.bin`). Needs a defined source for each file (public download, or provided by the owner).
6. **Dead or unclear targets** in `scripts/build.sh`: `ref34`, `uImage.multi`, `INIT_SRC` variants; `scripts/` mixes build steps and
   work tools.
7. **Nested containers** (chain 2 starts Docker from the host): blocks "everything runs inside the build image" (CI target).
8. `out/` holds artifacts and logs/backups together; the Makefile `clean` has to list artifacts one by one.

## Target design: one multi-stage Dockerfile

One `Dockerfile` at the root builds everything; the only host requirement is Docker with BuildKit (no make, no Git Bash needed):

    docker buildx build --target artifacts --output type=local,dest=out/release/<name> .

The result is exactly what the flashing step needs: `uImage`, `rootfs-alpine.ext4`, `SHA256SUMS`, `BUILD-INFO`.

Stages (each one `COPY`s only its own inputs, so BuildKit rebuilds a stage only when they change: this replaces the dependency
tracking of the Makefile):

| Stage | Platform | Does |
|---|---|---|
| `ARG`s at the top | | the only place for the Alpine, kernel and Cog versions |
| `tp`, `ts-srv` | alpine armv7 (QEMU) | static `make` of `rootfs/tp`, `rootfs/ts_srv` |
| `cog` | alpine armv7 (QEMU) | Cog DRM module (version of the Alpine package) with `rootfs/cog-patches`, always built |
| `initramfs` | alpine armv7, then native for `cpio` | busybox-static etc. from apk, helpers of `initramfs/`, firmware -> `initramfs.cpio.gz` |
| `kernel` | native build image | patches, DTS, config, kernel build (sources and objects in a `RUN --mount=type=cache`, so incremental), `mkimage` -> `uImage` |
| `rootfs-tree` | alpine armv7 | `apk add`, overlay, binaries from `tp`/`ts-srv`/`cog` |
| `rootfs-img` | native | `mke2fs -d` of the tree -> `rootfs-alpine.ext4` |
| `artifacts` | `scratch` | the files above + checksums + BUILD-INFO (commit passed as a build argument) |

What it removes: nested `docker run`/`docker export` (problem 7), the two mechanisms (2), version duplicates (3), the cpio extraction
(4: the rootfs stage takes BusyBox and `bccmd` from their own source), and the Makefile dependency lists. Development targets
(`menuconfig`, a shell) become extra targets of the same Dockerfile.

Inputs that are not in git come in as named build contexts or secrets, not by copying from the working directory:
`--build-context firmware=<dir>` (see problem 5) and `--secret id=sshkey` for the optional public key.

CI: GitHub Actions only needs `docker/setup-qemu-action` (armv7 stages) and `docker/build-push-action` with `outputs: type=local`.
The Makefile shrinks to a thin wrapper (clean-tree check for `release`, the command above) or disappears.

## Constraints from the owner (2026-10-30)

* **The host is aarch64 (Windows on ARM, Docker Desktop with a Linux arm64 VM) for now**, and CI runners are usually x86_64: the
  Dockerfile must work on both. Native stages use `$BUILDPLATFORM` and only multi-arch tools (Debian packages: `gcc-arm-linux-gnueabihf`,
  `u-boot-tools`, `device-tree-compiler`, `apk-tools` from the Alpine repository of the running platform); no x86-only binary
  (no `apk-tools-static` x86_64, no x86 prebuilt toolchain). Whether the armv7 stages run natively or under QEMU depends on the CPU
  (a core without AArch32 needs QEMU): check with `docker run --rm --platform linux/arm/v7 alpine:3.21 uname -m` and the speed of a build.
* **Cog stays on the Alpine version** (no newer one is packaged for Alpine; WPEPlatform comes later, with our own launcher). **The
  modeset patch is always applied**: it was written for the no-GPU case, but QEMU is needed anyway for the rootfs, so the `cog` stage is a
  normal stage of the default build (patched module over the Alpine package, as `alpine-rootfs.sh` does today). Dropping the patch
  later only means deleting the stage.
* To check before the move to WPEPlatform: does the `wpewebkit` of Alpine 3.21 ship WPEPlatform? What shows and hides the panel
  today (`cogctl`)? The stage of the new launcher will be a normal armv7 stage.
* Consequence for QEMU: it stays (scripts of `apk add`, optional patched Cog, a future launcher linking WPE libraries), so there is no
  point in cross-compiling `tp`/`ts_srv` to avoid it: all armv7 stages use the same Alpine armv7 base.

## Decisions on the userspace pieces (2026-10-30)

* **`devmem`**: Alpine's BusyBox has none. It becomes a mode of our `tp` (called as `devmem`, same syntax as BusyBox: `devmem addr
  [8|16|32] [value]`, prints the value only), symlinked in the rootfs, so `charge-mode`, `cpu-state`, `tp-powerd`, `tp-sh`, `init` stay
  unchanged. The initramfs gets a small static build of the same source file (`devmem.c`): the whole `tp` (427 KB) is too big for the
  kernel image. BusyBox itself is Alpine's `busybox-static`, as is.
* **`bccmd`**: not in Alpine (`bluez-deprecated` has `hciattach`, `hciconfig`, `hcitool`, ... but no `bccmd`): stage that builds it
  statically from BlueZ 5.55 with `initramfs/tools/csr_usb_stub.c`, for the rootfs (`bt-power`) and the optional full initramfs.
  `hciup` is no longer needed (`hciconfig` is packaged).
* **Accounts**: user `touchpad` / password `alpine` over SSH (password given by a build argument, fixed salt). `RELEASE=1`: password
  login only, root locked. Without it (development): the project key stays in root's `authorized_keys` as today. `S05ssh` always
  starts dropbear (no `-s`). Open: `touchpad` as a normal user with `doas` to root (recommended), or uid 0.
* **Firmware**: undecided; named build context `firmware`, clear error listing the missing files, and a "no board data" option.
* **Testing**: in a separate git worktree (committed tree only), own image names and caches, nothing shared with the work in progress.

## Separate task (not part of the build cleanup): port to `tp`

Port `charge-mode` (`tp charge ...` already exists in C, check it covers every case), `cpu-state` (`tp cpu`) and `recovery`/`info`/`temp`
from `tp-sh` to C, then shrink `tp-sh`. `wifi` and `bt` stay shell scripts (they only orchestrate external programs). The initramfs `init`
keeps the small `devmem`. Needs tests on the tablet.

## Firmware: what can go in the repository (research, 2026-10-30)

Used by the build: the four groups below. Not used by the build and to keep out of the repository: `firmware/webos-touch/` (HP/Palm/Cypress
touch controller firmware, `PmTpUpdater`, libraries: dumps of the stock webOS), `firmware/webos/ath6k/{athwlan,otp,data.patch}.bin` and the
`dbglog*.h` headers (webOS copies, unused).

| File | Source | Licence | In the repository? |
|---|---|---|---|
| `ath6k/AR6003/hw2.1.1/fw-2.bin`, `fw-3.bin` | upstream linux-firmware, also the Alpine package `linux-firmware-ath6k` (same bytes) | "Redistributable", `LICENCE.atheros_firmware`: redistribution **in binary form, without modification**, must reproduce the copyright and the disclaimer, no reverse engineering, patents only with an Atheros chipset | Not needed: the build gets them from apk. Shipping them in an image needs the licence text in the image |
| `qcom/leia_pfp_470.fw`, `leia_pm4_470.fw` (Adreno 220) | linux-firmware / Alpine `linux-firmware-qcom` (same bytes as ours) | "Redistributable", `LICENSE.qcom`: binary only, **the licence .txt must travel with it**, only on platforms with Qualcomm chipsets, no sublicensing, notices kept | Same: from apk, licence text to ship |
| `ath6k/.../bdata.SD32.bin` (board data of the TouchPad, from the stock webOS) | HP/Palm webOS | none found; **not** the upstream file (upstream `AR6003.1/hw2.1.1/bdata.SD32.bin` is also 1792 bytes but 733 bytes differ; the other upstream SD31 differs in 878) | **No** (proprietary, no licence). No public copy of the webOS file found |
| `cm/bluecore6.psr` (BlueCore keys, 3210 bytes) | `CyanogenMod/android_device_hp_tenderloin`, branch `cm-11.0`, `bluetooth/bluecore6.psr` (identical sha256, last commit of that path `dc3efeb6…`, 2013-11-21) | the GitHub repository declares none | Unclear: no licence, origin HP/CSR. Best: **do not store it, download it at build time** pinned by commit and sha256 (checked: the commit URL returns the same file) |
| `webos/gpu/leia_*.fw` (optional, `ADRENO_FW=webos`) | stock webOS, other CP microcode version than upstream | HP/Qualcomm, none found | No: stays a private input |

Consequences for the build
* The public part of `firmware/` disappears from the inputs: `linux-firmware` files come from apk, the PSR from a pinned download
  (`ADD --checksum` like the BlueZ and Cog sources). Only `bdata.SD32.bin` (and the optional webOS GPU files) remain as the private
  named context `firmware`.
* Ship the licence texts (`LICENCE.atheros_firmware`, `LICENSE.qcom`) in the initramfs/rootfs and say it in `BUILD-INFO`, since an image
  that is published redistributes these binaries (to do in the stages; not done).
* The board data is the only real obstacle to a public build. Options, to decide: (a) private input (secret/context in CI, or a release
  built by the owner); (b) test the generic upstream `bdata.SD32.bin` on the tablet (Wi-Fi range and, above all, the TX power limits of a
  board data are tied to the board and to the regulatory approval: not a drop-in); (c) the tablet provides it: a user-run script reads it
  from their own webOS install (as `scripts/collect-webos.sh` already reads files) and puts it on the media volume, the Wi-Fi start uses it.
  (c) keeps the repository and the images free of HP files and is the usual way for such projects.
* Nothing here is legal advice: the licences above are read from linux-firmware's WHENCE and LICENSES; the webOS and CM files need the
  owner's decision (`docs/PUBLIC-RELEASE.md`).

### Firmware: owner decisions and the webOS ports (2026-10-30, later)

* **`bluecore6.psr` stays in the repository** (owner decision: no dependency on the CyanogenMod repository staying online). In the worktree:
  `rootfs/bluetooth/bluecore6.psr`, byte-identical to the CM file, with a `README.md` giving the origin, commit and sha256; the
  Dockerfile filters it as before (resulting `/etc/bluetooth/bluecore6.psr` identical to the previous image). The `rootfs` stages no
  longer need the `firmware` context at all: only the initramfs does (board data).
* **Open webOS and the ports** (GitHub API, no licence declared by any of these repositories, which is not a permission):
  * `openwebos` organisation: nothing for the TouchPad firmware found.
  * `CyanogenMod/android_device_hp_tenderloin` (branch `cm-11.0`) and `webOS-ports/android_device_hp_tenderloin(-common)` publish
    `prebuilt/wifi/ath6k/AR6003/hw2.1.1/{athwlan.bin,otp.bin,data.patch.bin,bdata.SD31.bin,bdata.SD32.bin,bdata.WB31.bin}`.
    Their `bdata.SD31.bin` = `bdata.WB31.bin` = upstream linux-firmware `AR6003.1/hw2.1.1/bdata.SD31.bin` (same bytes). Their
    **`bdata.SD32.bin` (sha256 `cf8b4588…`) is a third variant**: 571 bytes differ from the webOS file of our tablets (`a2d0fc52…`), 698
    from upstream's SD32 (`106e48ea…`; webOS vs upstream: 733). So the webOS file is not published by these projects; the CM one is
    public and was shipped for this hardware with Wi-Fi working, but it is **untested with the mainline `ath6kl`** here.
  * `webOS-ports/android_vendor_hp` (HP Android blobs): `leia_pfp_470.fw` / `leia_pm4_470.fw` have the **same bytes as upstream
    linux-firmware**, i.e. what the default build uses; the webOS stock microcode (`firmware/webos/gpu`, optional `ADRENO_FW=webos`) is not
    published there. Also there: `a6.txt`/`a6_1.txt` (A6 microcontroller firmware), `q6.*`, `vidc_1080p.fw`, audio `wm8958*.wfw`.
  * `webOS-ports/android_device_hp_tenderloin-common` has another `bluecore6.psr` (3211 bytes, sha256 `182fe9c3…`) than the CM one (3210).
  * `webOS-ports/tenderloin-prebuilt`: kernel binaries (3.0.101 CyanogenMod), no firmware.
* **Board data, remaining options**: keep the webOS file as private input / user extraction (a, c), or test the CM `bdata.SD32.bin` with the
  mainline driver (b'): if the Wi-Fi works as well, the repository can hold or download a public file (pin: commit and sha256; no
  declared licence, so the same open question as the PSR for `docs/PUBLIC-RELEASE.md`). A test image for the tablet is cheap to build
  (same build with that file as `bdata.SD32.bin` in the context); the judgement (range, throughput, TX power limits) is the owner's.

### Board data: what really differs, and what ships (2026-10-30, later)

Owner's rule: the target is this tablet only, so a firmware or microcode that Linux offers and that is not used is not shipped.
* **Shipped in the initramfs now (6 files, 755 KB gz)**: `fw-3.bin` (the driver tries `fw-5`, `fw-4`, `fw-3`, `fw-2`, then the legacy files; the
  tablet runs `fw-3`, so `fw-2.bin` is dropped), `bdata.bin`, `leia_pfp_470.fw`, `leia_pm4_470.fw`, `regulatory.db` and `.p7s`. The rootfs holds
  no firmware (`/lib/firmware` empty), the build image the Alpine packages are only read from.
* **Which upstream file**: the real linux-firmware file for our chip is `ath6k/AR6003/hw2.1.1/bdata.SD32.bin` (sha256 `cf8b4588…`; the Alpine
  package makes it a link to `hw2.0/`, same bytes), **identical to CyanogenMod's**. `AR6003.1/hw2.1.1/` is another chip variant (`106e48ea…`).
* **webOS file vs upstream (`cf8b4588…`)**, both 1792 bytes, same layout and both with a valid checksum (XOR of the 16-bit words = 0):
  * header: upstream carries the module name `SD32` and part number `42D_0655a00-`, webOS has 0xff there;
  * **MAC address**: upstream has the placeholder `00:03:7f:04:02:8f` (Atheros OUI, the same on every unit); the webOS file holds
    **the MAC of the tablet it was read from** (Palm OUI `00:1d:fe`; the files of the two tablets carry different addresses).
    So the webOS board data is **per unit**: shared, it gives two tablets the same MAC; published, it leaks one;
  * calibration tables (0x100-0x4ff, the radio part): 60 to 70 % of the bytes differ (733 bytes in 47 runs over the file). These are TX
    power and gain tables of HP's design versus Atheros' reference module: nothing says from the files whether upstream's are good enough
    for the TouchPad's antenna path; only a measurement does.
* **Build**: `BOARD_DATA=upstream` (default, public), `webos` (the unit's own file, build context `firmware`), `none` (the driver then uses its
  default `bdata.SD31.bin`). Two images built from the same commit differ only by that: `out/release/bdata-upstream/` and
  `out/release/bdata-webos/` of the worktree (same rootfs, `uImage` 7.22 MB each).
* **To test on the tablet (not done)**: flash each `uImage` and compare at the same place: `iw dev wlan0 link` (signal), `iw dev wlan0 info`
  (txpower), throughput (`scripts/wifi-throughput.py`), dmesg of `ath6kl`. The generic file gives every tablet the same MAC: if it works,
  the MAC has to be set per unit at boot (the webOS tokens partition `/dev/mmcblk0p12` already gives `BToADDR` for Bluetooth: check
  whether a Wi-Fi MAC token exists with `tr -cs ' -~' '
' < /dev/mmcblk0p12 | grep -i -B1 -A1 mac`).

### Board data: the tablet has its own, in the tokens (checked on a tablet, 2026-10-30)

The webOS tokens partition (`/dev/mmcblk0p12`) holds, next to `BToADDR`, two Wi-Fi tokens: **`WIFIoADDR`** (the MAC address) and **`WIFICAL`**
(base64 of a complete 1792-byte board data). Read on the attached tablet: `WIFICAL` decodes to exactly 1792 bytes, valid checksum (XOR of
the words = 0), the MAC of that unit at offset 0x16, and 198 bytes different from the `firmware/webos` file (which comes from another
unit). So webOS builds its `bdata` from the tokens: **each tablet has its own factory calibration and MAC**, and no file can be the right
one for all of them, neither the webOS one nor linux-firmware's (the generic one stays a fallback).

Design to do (separate task, needs tablet tests): at boot, take `WIFICAL` from the tokens (like `bt-power` does for `BToADDR`), write
`/lib/firmware/ath6k/AR6003/hw2.1.1/bdata.bin` and (re)bind the SDIO device, or find the first point where the driver asks for it
(`ath6kl` is built in and probes while the kernel boots, before `/init`: the file must exist before, or the driver be rebound). Then the
repository and the images need no board data from HP at all, and each tablet gets its own MAC and calibration.

### First tablet tests (2026-10-30, RAM boot only: nothing written to the flash, `/boot` or the media volume)

Tablet: webOS 3.0.5, put in recovery with `scripts/webos-recover.sh`, kernel + initramfs from the worktree started with `novacom boot mem://`
(the root filesystem is the tablet's old `rootfs-alpine.ext4`, built by the old scripts).
* **Boot**: OK. Alpine's static BusyBox as the initramfs shell, rootfs mounted, serial gadget, Wi-Fi up and connected (the home access point,
  5 GHz), regulatory database loaded (country FR rules), GPU microcode `leia_pm4/pfp_470.fw` loaded from the initramfs, `fw-3.bin` loaded
  (`fw 3.2.0.144 api 3`) with `fw-2.bin` removed. No new warning in `dmesg` (the ones left are old: dummy supplies, panel, tsens, `fw-5`/`fw-4`
  not found by design).
* **`devmem`** (initramfs, 21 KB): its writes in `init` are in place (recovery magic `0x2A05F65C` = `0x6F656D11`, CPU1 `CLK_SEL` = 2 like CPU0);
  27 reads (9 registers, 8/16/32 bits) compared with BusyBox's `devmem`, the standalone build and `tp` called as `devmem`: identical, except
  a live GPIO line (`0x801004`) that toggles by itself (20 reads of each give both values). Bad address, bad width: clean errors; an
  unaligned address is refused (BusyBox reads it anyway).
* **`setsid -c`**: gives the shell its controlling terminal (checked on tty5: `tty_nr` = 4:5). The screen-shell fallback of `init` itself
  (no rootfs) was not exercised.
* **Found: the eMMC and the Wi-Fi controller probe in a random order** (`mmcblk0` or `mmcblk1`), and `initramfs/init` and `bt-power` use
  `/dev/mmcblk0p14` and `p12`. One boot out of two of the new images ended in the rescue shell without rootfs. The DTS had no `mmc` aliases:
  fixed in the worktree with `mmc0 = &sdcc1; mmc1 = &sdcc4;` (the MMC core honours them): then eMMC = `mmc0`, Wi-Fi = `mmc1`, rootfs mounted.
  **This race exists in the current tree too** (same DTS).
* **Wi-Fi, three board data at the same place (-72 dBm, 5180 MHz, rates MCS 0-2)**: `iw` TX power 14 dBm (generic), 15 dBm (webOS file, and
  the tablet's own `WIFICAL`). Throughput (`scripts/wifi-throughput.py`, 20 MB, up/down Mbit/s): generic 3.5/3.8, 1.9/1.6, 1.8/1.4; webOS
  file 4.5/2.6, 5.6/0.6; own `WIFICAL` upload failed twice (TCP aborted), 0.3 and 0.5 down, 13 % ping loss once. The spread between two runs
  of the same file is as large as between files, and the best-calibrated one (own token) is not better: **at -72 dBm the measurement does not
  tell the board data apart** (the link is marginal and varies with the radio environment). A fair comparison needs a strong signal (next to the
  access point), several runs each, and the same time of day. The MAC address is the tablet's own with every file (the chip's eFuse
  overrides the board data), so the placeholder MAC of the generic file is not a problem.
* Not done yet: the new rootfs (account `touchpad`, `doas`, SSH password, `bccmd`, the Dockerfile's `tp`), which needs the rootfile to be
  overwritten in place; Bluetooth; the panel.

### Second tablet test: the new root filesystem (2026-10-30)

The image `bdata-upstream/rootfs-alpine.ext4` of the worktree replaced `touchpad/rootfs-alpine.ext4` on the media volume (owner's go; copied from
webOS with `novacom put`, 140 s, md5 `e85ea91d…` checked on the tablet, `rootfile` unchanged), then kernel + initramfs started in RAM
(`out/kernel-upstream/uImage`, with the `mmc` aliases).
* **Works**: boot to the new rootfs (eMMC = `mmcblk0` every time), Wi-Fi up (DHCP), `touchpad` (uid 1000, wheel), `doas` to root (setuid kept),
  **SSH with the password** (the right one is accepted, a wrong one refused), SSH as root with the project key (development image), root locked
  in `/etc/shadow`, dropbear without `-s`, **Bluetooth**: `bccmd` (musl, static, from the Dockerfile) loaded every PSR key and `hci0` is up with the BD
  address of the tablet, the `tp` daemon, `httpd` and Cog running, the panel's HTTP API answers (`battery`, `wifi status`, `bt status`; `power`,
  `charge`, `temp` return 403: not allowed for the panel), `devmem` link to `tp`, `bluecore6.psr` identical to before.
* **Found, not caused by the new chain**: `cpu-state` was never in the Alpine image (the old `alpine-rootfs.sh` did not copy it), so `tp cpu` and
  the CPU line of `tp info` fail with "cpu-state: not found". Fixed in the Dockerfile (`COPY initramfs/cpu-state`), not rebuilt yet. Cosmetic:
  `tp info` prints the Bluetooth line as `bt: hci000:1D:...` (no space).
* **Not done**: cold boot from the flash through moboot (`/boot/uImage.Linux` is still the old kernel: installing the new one is a write to
  `/boot`, see `docs/FLASHING.md`), the panel on the screen, audio/camera, suspend.

### Alpine as the build image? (tested 2026-10-30, not adopted yet)

* Packages: `gcc-arm-none-eabi` 14.2, `binutils-arm-none-eabi`, `dtc`, `u-boot-tools`, `bison`, `flex`, `elfutils-dev`, `openssl-dev`, `e2fsprogs`...
  exist in Alpine 3.21 for aarch64, x86_64 and armv7. There is no Linux/armhf cross compiler (the one of Debian, `gcc-arm-linux-gnueabihf`),
  only the bare-metal `arm-none-eabi`, which builds the kernel without any change to `scripts/kernel.sh`.
* Trial in a throwaway container (Alpine arm64, gcc 14.2, the repository scripts untouched): kernel + `uImage` built, `uImage` 7 225 414 bytes
  (Debian-built: 7 225 078), **booted in RAM on the tablet**: rootfs, Wi-Fi, GPU microcode, Cog, the same 4 error-level `dmesg` lines as before.
  Not validated: GPU rendering on the screen, Bluetooth and suspend with that kernel. Time: 549 s for the whole container (apk install, download,
  build), not comparable with the 200 s of the Debian stage (cached layers): not measured fairly.
* Toolchain install size for the same job: 497 MB (Alpine, with `build-base`, `curl`...) against 659 MB (Debian).
* To know when switching: the local Docker image store keeps one platform per tag, so `alpine:3.21` for armv7 stages and for native stages
  collide on this PC (this is why the native stages use `debian:bookworm-slim` today): pin each by digest.

### All stages on Alpine (done in the worktree, 2026-10-30)

* The Dockerfile has no Debian left: kernel (Alpine's `arm-none-eabi-gcc` 14.2, cache id `tp-kernel-alpine`), ext4 image (`e2fsprogs`) and
  artifacts stages are Alpine like the armv7 ones; Alpine is pinned by the digest of its multi-platform index (`ALPINE_DIGEST`, next to
  `ALPINE_VERSION`), which also ends the local tag collision between platforms. Fresh full build (nothing cached, kernel included): 603 s.
* Checked: root filesystem identical to the previous one except the new `usr/local/bin/cpu-state` (tree, modes and file contents compared);
  `uImage` 7 227 246 bytes, started in RAM on the tablet: eMMC `mmc0`, Wi-Fi `mmc1`, GPU microcode, Wi-Fi up, `bt-power on` loads the 32 PSR keys and
  brings `hci0` up, `cpu-state` prints the CPU/L2 clocks. Not checked with this kernel: the picture on the screen, suspend.
* Left on Debian/Ubuntu in the repository (not in the images): `docker/Dockerfile` (the old build image), `docker/novacom/Dockerfile` (the
  novacom workbench image: not part of the build chain), `docker/ref34/Dockerfile` (Ubuntu 16.04, the old reference kernel experiment) and a
  comment in `scripts/initramfs.sh`. The old ones go in T7.
* Installed on the tablet (2026-10-30, owner's go): `/boot/uImage.Linux` = the all-Alpine `alpine-all/uImage` (md5 `3578f840dfc01c21b59da2d8800438b6`, checked after
  writing; the previous kernels are saved in `out/backup/tablet2/`: `uImage.Linux-2026-10-29.bak` and `uImage.Linux-debian-built-2026-10-30.bak`);
  root filesystem `bdata-upstream` (Alpine content, ext4 written by Debian's `mke2fs`) plus `usr/local/bin/cpu-state` copied into the live image
  (same content as the `alpine-all` image; `tp cpu` works). `moboot.default` still CyanogenMod. Cold boot through moboot not tested yet.

## Tasks (to order)

- [ ] T0. Check problem 4 (does the current release reach the end with the default initramfs?). Cheap, tells how much is broken now.
- [ ] T1. Single versions block (Alpine, kernel, Cog) as `ARG`s.
- [x] T2. `Dockerfile` stages `tp`, `ts-srv`, `cog` (patched, always built) in the worktree `build-chain`: `tp`, `ts_srv` and the Cog module are
      byte-identical (sha256) to the ones of the current scripts (2026-10-30; build 26 s, 10 s, 49 s).
- [x] T2c. `devmem` mode in `tp` (`rootfs/tp/devmem.c`, 21 KB standalone for the initramfs, stripped) and `bccmd` stage (BlueZ 5.55, musl static, runs
      under armv7). Not tried on the tablet (`devmem` needs /dev/mem: compare its output with BusyBox's `devmem` on the same registers).
- [x] T3. Stage `initramfs` on Alpine packages (no `apt-get`), built in the worktree (2026-10-30): `scripts/initramfs.sh` rewritten for Alpine (runs in the
      stage), 833 KB instead of 1075 KB. The `linux-firmware` files (`fw-2/3.bin`, `leia_*.fw`) come from the Alpine packages and are
      byte-identical to `firmware/linux-firmware/`: only the TouchPad Wi-Fi board data (`bdata.SD32.bin`, stock webOS) needs the named context
      `firmware` (error with a clear message when missing, `--build-arg BOARD_DATA=skip` to build without it). `bdata.bin`, `tp-powerd`,
      `cpu-state`, `charge-mode`, `tp` are identical to the current initramfs.
      **To check on the tablet (not done)**: boot; the screen shell (`setsid -c sh` replaces `cttyhack`, absent from Alpine's BusyBox); the
      regulatory database (Alpine's 2024.07.04, upstream signature, differs from Debian's: `dmesg` must not complain about the signature).
      **Lost for now**: `telnetd` (only `busybox-extras`, dynamic; only `scripts/gadget-test.sh` looks at port 23) and the debug tools of
      `INITRAMFS_TOOLS=full` (iw, Bluetooth, wpa_supplicant, dropbear), to port to a separate target if still wanted.
      Finding for T0: the current default initramfs (62 entries) holds no `bccmd` nor PSR, so `alpine-rootfs.sh`, which extracts them from it,
      cannot complete with it.
- [x] T4. Stage `kernel` in the worktree (2026-10-30): Debian bookworm cross compiler (kept: the Alpine `gcc-arm-none-eabi` option was not tried),
      tarball pinned by sha256 (checked against kernel.org), sources and objects in a cache mount (`tp-kernel`). Fresh build 200 s on the 12 CPUs
      of Docker Desktop (aarch64), rebuild after a change 50 s, nothing changed 2 s. `uImage` 7.27 MB (was 7.47 MB with the Debian initramfs).
      Found by building from the committed tree, both fixed in the worktree only (not in your tree):
      1. **Commit `1c15a56` does not compile**: `wake-gpios` is in the `lm8502-state` pinctrl node ("Properties must precede subnodes"),
         the uncommitted version of your tree has it in `battery@31` (where the driver reads it).
      2. **`kernel.sh` lost `CONFIG_INITRAMFS_COMPRESSION_NONE` on a fresh object directory** (the choice is hidden while `INITRAMFS_SOURCE` is empty,
         so the merge of `touchpad.config` dropped it and gzip came back: the initramfs was compressed inside an image that is XZ-compressed
         anyway, +200 KB). Only an object directory with history had it right. `kernel.sh` now sets it after `INITRAMFS_SOURCE`.
      Other differences with the config of the old volume: `CC_CAN_LINK*` (the new image has no armhf libc: no effect on the image) and
      `REGULATOR_MSM8660_SAW` (your uncommitted work). Not booted on the tablet.
- [x] T5. Stages `rootfs-tree`, `rootfs-img`, `rootfs-bin`, `artifacts` in the worktree (2026-10-30). Full chain in 1.5 min on top of the cached kernel:
      `docker buildx build --build-context firmware=<dir> --secret id=sshkey,src=secrets/touchpad_ed25519.pub --build-arg RELEASE=0|1
      --build-arg BUILD_NAME=.. --build-arg GIT_COMMIT=.. --build-arg BUILD_DATE=.. -o type=local,dest=out/release/<name> .`
      gives `uImage`, `rootfs-alpine.ext4` (625 MB), `SHA256SUMS`, `BUILD-INFO`. `--target artifacts` is the default (last stage); `--target tp-bin`,
      `kernel-bin`, `initramfs-bin`, `rootfs-bin`... give the pieces.
      Accounts: `touchpad` (uid 1000, password from `TOUCHPAD_PASSWORD`, default `alpine`, fixed salt), `doas` to root without password
      (`/etc/doas.d/touchpad.conf`), root locked; the key of the `sshkey` secret goes to root only without `RELEASE=1`; `S05ssh` always starts
      dropbear (no `-s`). Checked on the image: passwd/shadow/doas (setuid kept), key in `/root/.ssh`.
      Compared with the old `out/rootfs-alpine.ext4` (debugfs): that image is from 2026-10-27, older than the repository (no bluez, dbus,
      `S15time`, `S20bluetooth`, `S95panel`), so the comparison only shows expected differences (new `tp`, `ts_srv`, `devmem` link, no
      `busybox-tp` / `tp-powerd`). Nothing was booted.
      **Found**: the files of `rootfs/overlay` are mode 100644 in the git index (written from Windows): `rcS`, `tp.d/S*` and `usr/local/bin/*`
      would not be executable on a Linux/CI checkout, so the Dockerfile sets the modes itself. Better also fix the index
      (`git update-index --chmod=+x`), to do with the commit. Native stages use `debian:bookworm-slim`, not `alpine`: with the local
      image store a tag already pulled for armv7 is reused for the native stage (wrong platform, QEMU, warning).
- [ ] T6. `firmware/`: research done (section above). To do: `linux-firmware` files from apk (done in the initramfs stage), PSR as a pinned download in the
      `rootfs-tree` stage, licence texts shipped, decision on the board data (a/b/c), then the named context only holds `bdata` (and optional GPU files).
- [x] T7. Done on the branch (2026-11-02): `./build.sh [target]` (all, kernel, initramfs, rootfs, tp, ts-srv, bccmd, cog, release, menuconfig, shell, clean, help;
      results in `out/<target>/`), `build.ps1` forwards to it, the four old build scripts only call it, the stage scripts are in `scripts/build/`
      (dispatcher reduced to kernel/uimage/menuconfig/shell, new `kernel-env` stage for menuconfig/shell/check-patch-series), `docker/Dockerfile`,
      `docker/alpine-rootfs`, `docker/ref34`, `scripts/ref34.sh`, `kernel/ref34.config` and `initramfs/init-ref34` removed (the 3.4 reference kernel
      experiment stays in git history), `scripts/cycle.sh` and `boot.ps1` default to `out/kernel/uImage`. Tested: every target (tp 5 s, kernel 67 s,
      rootfs 11 s, all 18 s when cached), the four wrappers, `release --dirty` (120 s; image without the developer key: `/root/.ssh` empty, BUILD-INFO
      without the development line), `release` on a dirty tree refused. `Makefile` and `scripts/release.sh` exist only as untracked files of the main
      working tree: obsolete, to delete by the owner. Left: the abandoned `buildroot/` directory, `docker/novacom/` (the workbench image).
- [ ] T8. GitHub Actions workflow.
- [ ] T9. Update README, STATUS.md, `docs/PUBLIC-RELEASE.md`.

## Open questions (owner)

* Firmware in CI: public sources only, a secret for `bdata.SD32.bin`, or a private repository?
* Is a different initramfs userspace (musl) acceptable for the rescue shell? (Same programs, other libc: needs a boot test on the tablet.)
* Keep `build.ps1`, or Docker command / thin Makefile only?
