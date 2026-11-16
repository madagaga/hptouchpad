# Installing Linux on the HP TouchPad

This document installs our Linux on the TouchPad flash next to webOS, using only `novacom` and `novaterm` from a PC.
The system to start is chosen at power-on in the **moboot** boot menu, which is a prerequisite.
It assumes the two images are ready and tested by a RAM boot:

* `uImage`: kernel with its initramfs (one file, about 7.2 MB).
* `rootfs-alpine.ext4`: root filesystem (about 620 MB).

Two starting points are covered:

1. **Stock TouchPad**, webOS only: install moboot, then add Linux (section 3).
2. **TouchPad with CyanogenMod** (moboot already there): **add** Linux to the menu, or **replace** CyanogenMod with Linux (section 4).

Statements marked *(untested)* have not been tried on a tablet yet.

## 1. What changes on the flash

| Where | What | Touched |
|---|---|---|
| p1 - p12 | modem firmware, bootloaders (`sbl*`, `rpm`, `tz`, `bootie`), webOS tokens (device identity, Bluetooth address) | **never** |
| p13, `/boot` (ext3, 32 MiB) | webOS kernel `uImage-2.6.35-palm-tenderloin`; symlink `uImage` = what the bootloader starts (`uImage.moboot` once moboot is installed); one `uImage.<Name>` per moboot menu entry | `uImage.Linux` added (moboot too on a stock tablet) |
| p14, LVM group `store` | webOS volumes (`root`, `var`, `mojodb`, `media`...); with CM also the CM volumes | the rootfs file is added to `media`; CM volumes removed only in 4.3 |

The bootloader (`bootie`) starts `/boot/uImage`, which is moboot. moboot shows one entry per `/boot/uImage.<Name>` file and starts the chosen
kernel. Our initramfs mounts the webOS `media` volume (the USB drive seen by a PC, `/media/internal` under webOS), reads the first line of
`touchpad/rootfile` and boots the ext4 image it names. webOS and its data stay as they are.

moboot starts our kernel from `/boot/uImage.Linux`.

## 2. Before you start

* PC with `novacom` and `novaterm` (Palm webOS SDK, or the open-source build of novacomd/novacom).
* Tablet on the wall charger for the whole procedure.
* **webOS developer mode** on: in webOS type `webos20090606` in Just Type, open the Developer Mode app, switch it on, reboot.
  `novacom -l` must then list the tablet.
* Space: about 650 MB free in the USB drive (`df -h /media/internal`) and room for our kernel in `/boot` (`df -h /boot`). If `/boot` is
  short, the webOS first-boot archives `boot-genesis.tar.gz` and `boot-images.tar.gz` (0.6 MB) can go, they are in the backup below.

All tablet commands below are typed in a root shell opened with `novaterm` under webOS. On a CM tablet, choose webOS in the moboot menu first.

### Backup (do it once, before writing anything)

```sh
# novaterm (webOS)
mkdir -p /media/internal/backup
dd if=/dev/mmcblk0p12 of=/media/internal/backup/p12-tokens.img
dd if=/dev/mmcblk0p13 of=/media/internal/backup/p13-boot.img
lvm.static vgcfgbackup -f /media/internal/backup/store-lvm.txt store
ls -l /boot > /media/internal/backup/boot-listing.txt
```

```sh
# PC
novacom get file:///media/internal/backup/p12-tokens.img > p12-tokens.img
novacom get file:///media/internal/backup/p13-boot.img > p13-boot.img
novacom get file:///media/internal/backup/store-lvm.txt > store-lvm.txt
novacom get file:///media/internal/backup/boot-listing.txt > boot-listing.txt
```

Keep these files on the PC. `p12-tokens.img` is the only copy of the device identity: the webOS Doctor does not rebuild it.

### Copy the root filesystem (both cases)

```sh
# novaterm
mkdir -p /media/internal/touchpad
```

```sh
# PC (about 2 minutes over USB)
novacom put file:///media/internal/touchpad/rootfs-alpine.ext4 < rootfs-alpine.ext4
md5sum rootfs-alpine.ext4
```

```sh
# novaterm
md5sum /media/internal/touchpad/rootfs-alpine.ext4        # must match the PC
echo touchpad/rootfs-alpine.ext4 > /media/internal/touchpad/rootfile
```

## 3. Stock TouchPad (webOS only)

### 3.1 Install moboot

moboot 0.3.8 (public download, `moboot_0.3.8.zip`) contains `uImage.moboot`. Unzip it on the PC, then:

```sh
# novaterm
mount -o remount,rw /boot
ls -l /boot/uImage                                          # uImage -> uImage-2.6.35-palm-tenderloin
```

```sh
# PC
novacom put file:///boot/uImage.moboot < uImage.moboot
md5sum uImage.moboot
```

```sh
# novaterm
md5sum /boot/uImage.moboot                                  # must match the PC
ln -sfn uImage-2.6.35-palm-tenderloin /boot/uImage.webOS    # menu entry "webOS"
ln -sfn uImage.moboot /boot/uImage                          # the bootloader now starts moboot
sync
```

Leave `/boot` read-write and go on with 3.2 (one reboot at the end). To remove moboot later: `ln -sfn uImage-2.6.35-palm-tenderloin /boot/uImage`.

### 3.2 Add Linux

As in 4.2 below.

## 4. Adding or replacing in the moboot menu

### 4.1 Look first (CM tablet)

```sh
# novaterm
ls -l /boot
cat /boot/moboot.default 2>/dev/null
lvm.static lvs store
```

Keep the output with the backup. CM tablets usually have `uImage.CyanogenMod`, a recovery (`uImage.ClockworkMod` or `uImage.TWRP`) and
the CM volumes `cm-system`, `cm-cache`, `cm-data` (names to check in this output).

### 4.2 Add Linux

```sh
# novaterm
mount -o remount,rw /boot
df -h /boot                                                 # free space must exceed the size of uImage
```

```sh
# PC
novacom put file:///boot/uImage.Linux < uImage
md5sum uImage
```

```sh
# novaterm
md5sum /boot/uImage.Linux                                   # must match the PC
cat /boot/moboot.default                                    # note it, to restore it if Linux is removed
echo Linux > /boot/moboot.default                           # Linux is the default entry
sync
mount -o remount,ro /boot
reboot
```

moboot now lists "Linux" next to webOS (and CyanogenMod) and starts it when nobody picks an entry. The other systems stay in the
menu. Restart from the settings panel goes through this menu too. Do not use `/boot/moboot.next`: moboot 0.3.8 reads it but does
not remove it, so it forces that entry at every boot. To remove Linux: `rm /boot/uImage.Linux`, put the old name back in `moboot.default`, and delete the
`touchpad` folder of the USB drive.

From our Linux (SSH as `touchpad`, `doas` to root), `/boot` is the partition `/dev/mmcblk0p13` (ext3). Mount it elsewhere than
`/mnt`, which would hide `/mnt/media`:

```sh
doas mkdir -p /run/boot && doas mount /dev/mmcblk0p13 /run/boot
echo Linux | doas tee /run/boot/moboot.default
doas umount /run/boot
```

### 4.3 Replace CyanogenMod with Linux

Do 4.2 first and check that Linux starts from the menu. Then, from webOS (`novaterm`). **The CM data volume (Android apps and files) is deleted.**

1. Remove the CM and recovery kernels (names from 4.1); moboot, webOS and Linux stay:
   ```sh
   mount -o remount,rw /boot
   rm -f /boot/uImage.CyanogenMod /boot/uImage.ClockworkMod /boot/uImage.TWRP
   ls -l /boot                                              # uImage -> uImage.moboot, uImage.webOS, uImage.Linux
   mount -o remount,ro /boot
   ```
   If `moboot.default` named CyanogenMod, set it to `Linux` or `webOS` before remounting.
2. Remove the CM volumes (names from `lvm.static lvs store`, never `root`, `var`, `update`, `log`, `mojodb`, `filecache`, `media`, `swap`):
   ```sh
   lvm.static lvremove -f store/cm-system store/cm-cache store/cm-data
   lvm.static lvs store
   ```
   The freed space stays unallocated in the group `store`; webOS and Linux keep working. *(untested on these tablets)*

The result is the same as a stock tablet after section 3: moboot with webOS and Linux.

## 5. Updating an installed tablet

Build the new images first (`./build.sh all`, files in `out/artifacts/`).

**Kernel** (from the running Linux, SSH as root):
```sh
cat out/artifacts/uImage | scripts/tablet-ssh.sh -i <ip> \
  'cat > /tmp/uImage && mkdir -p /run/boot && mount -t ext3 /dev/mmcblk0p13 /run/boot &&
   cp /tmp/uImage /run/boot/uImage.Linux && sync && md5sum /run/boot/uImage.Linux; umount /run/boot'
```
`/boot` has only about 1.6 MB free: overwrite `uImage.Linux`, do not keep a second copy there. Test a new kernel in RAM
first (`scripts/cycle.sh <uImage>`) before writing it.

**Root filesystem**: the running Linux uses `rootfs-alpine.ext4` as its root, so it cannot overwrite it, and the media volume
is usually too full for a second 620 MB copy. The simplest way is the USB drive mode of the tablet: the media volume then appears on
the PC as a FAT32 drive ("HP TOUCHPAD"), and `touchpad/rootfs-alpine.ext4` can be replaced by a plain file copy (about 9 minutes).
Check the checksum of the copied file against `SHA256SUMS`. Or replace the file from webOS:

1. From Linux, set the rootfs aside so that a Linux boot stops in the initramfs instead of using a half-written file:
   `mv /mnt/media/touchpad/rootfile /mnt/media/touchpad/rootfile.off`.
2. Reboot and choose **webOS** in moboot.
3. From the PC (novacom container):
   ```sh
   novacom put file:///media/internal/touchpad/rootfs-alpine.ext4 < out/artifacts/rootfs-alpine.ext4
   novacom run file:///usr/bin/md5sum /media/internal/touchpad/rootfs-alpine.ext4    # must match the PC
   novacom run file:///bin/mv /media/internal/touchpad/rootfile.off /media/internal/touchpad/rootfile
   novacom run file:///sbin/reboot
   ```
The settings (`touchpad/tpd.conf`, `url`, `tz`, `wpa_supplicant.conf`) stay on the media volume and survive the update.

## 6. If something goes wrong

1. **Linux does not start**: hold Power about 10 s, start again and choose webOS in moboot; fix `/boot` from `novaterm`
   (`rm /boot/uImage.Linux`, `moboot.default`).
2. **moboot does not start**: Volume Up + Power until the large USB logo (recovery mode), then start a working Linux kernel in RAM from the PC:
   ```sh
   novacom boot mem:// < uImage
   ```
   From that Linux (nothing is read from `/boot` in this mode) mount `/dev/mmcblk0p13` and point `uImage` back to
   `uImage-2.6.35-palm-tenderloin`, or restore `p13-boot.img` (copied to the USB drive) with `dd of=/dev/mmcblk0p13`.
3. **webOS damaged**: run HP's **webOS Doctor** 3.0.5 for the TouchPad from recovery mode. It rewrites `/boot` (moboot and Linux entries gone)
   and the webOS volumes (everything else in p14 removed, the USB drive included), not the tokens; `p12-tokens.img` is the copy if they were ever lost.
4. **Nothing on the screen, no recovery mode** (flat battery): see [`debrick/README.md`](../debrick/README.md).
