# syntax=docker/dockerfile:1
# check=skip=SecretsUsedInArgOrEnv
# Build chain of the project (docs/BUILD-CLEANUP.md): one multi-stage Dockerfile, the artifacts leave through --output:
#   docker buildx build --target <stage> --output type=local,dest=out/<dir> .
# Stages named *-bin only hold the files to export. The armv7 stages run under QEMU when the host cannot run 32-bit code.

ARG ALPINE_VERSION=3.21
# The multi-platform index of that release, by digest: every Alpine stage (armv7 and native) uses it, so the base never moves under us
# and a tag already pulled for another platform in a local image store is not reused by mistake. Update both together.
ARG ALPINE_DIGEST=sha256:ee16d92f96db08d9d2d07da48555a27f3a69d8f0759021dbc601195ed0d2b5bd
ARG ARM_PLATFORM=linux/arm/v7
ARG BLUEZ_VERSION=5.55
ARG COG_VERSION=0.18.3
ARG KERNEL_VERSION=6.12.40
ARG KERNEL_SHA256=4811af1317f98d2cccea3c7695969a2c03a27cb02fd2d5327032dd5341842933
ARG DTB_NAME=qcom-apq8060-hp-touchpad-kms
ARG KERNEL_BASE=qcom_defconfig
# RTSP=1 puts the optional RTSP server /usr/local/bin/tp-rtsp in the rootfs (docs/RTSP-SERVER.md); off by default
ARG RTSP=0

# --- Alpine armv7 toolchain: the base of every armv7 stage
FROM --platform=${ARM_PLATFORM} alpine:${ALPINE_VERSION}@${ALPINE_DIGEST} AS armv7
RUN apk add --no-cache build-base linux-headers

# --- tp: client, daemon and panel CGI, static (musl)
FROM armv7 AS tp
COPY rootfs/tp /src
WORKDIR /src
RUN make LDFLAGS=-static

FROM scratch AS tp-bin
COPY --from=tp /src/tp /tp
COPY --from=tp /src/devmem-small /devmem-small

# --- reboot-cmd: restarts the tablet from the stock webOS into the USB recovery mode (scripts/webos-recover.sh), static
FROM armv7 AS reboot-cmd
COPY scripts/tools/reboot-cmd.c /src/reboot-cmd.c
RUN gcc -static -Os -s -o /reboot-cmd /src/reboot-cmd.c

FROM scratch AS reboot-cmd-bin
COPY --from=reboot-cmd /reboot-cmd /reboot-cmd

# --- ts_srv: touch screen driver, static (musl)
FROM armv7 AS ts-srv
COPY rootfs/ts_srv /src
WORKDIR /src
RUN make LDFLAGS=-static

FROM scratch AS ts-srv-bin
COPY --from=ts-srv /src/ts_srv /ts_srv

# --- tp-rtsp: RTSP server (camera + hardware H.264 + microphone), dynamic against musl and libasound; opt-in (RTSP=1)
FROM armv7 AS tp-rtsp
RUN apk add --no-cache alsa-lib-dev
COPY rootfs/tp-rtsp /src
WORKDIR /src
RUN make LDFLAGS=-s

FROM scratch AS tp-rtsp-bin
COPY --from=tp-rtsp /src/tp-rtsp /tp-rtsp

# what the rootfs takes: the binary with RTSP=1, an empty stage otherwise (tp-rtsp is then not even built)
FROM scratch AS tp-rtsp-sel-0
FROM tp-rtsp-bin AS tp-rtsp-sel-1
FROM tp-rtsp-sel-${RTSP} AS tp-rtsp-sel

# --- bccmd: CSR BlueCore configuration tool (loads the PSR in bt-power), removed from recent BlueZ releases and not packaged by Alpine.
# Built statically from the BlueZ sources; its USB transport is replaced by stubs (the chip is on a UART).
FROM armv7 AS bccmd
ARG BLUEZ_VERSION
ADD --checksum=sha256:8863717113c4897e2ad3271fc808ea245319e6fd95eed2e934fae8e0894e9b88     https://www.kernel.org/pub/linux/bluetooth/bluez-${BLUEZ_VERSION}.tar.xz /bluez.tar.xz
COPY initramfs/tools/csr_usb_stub.c /csr_usb_stub.c
RUN tar -xf /bluez.tar.xz && cd bluez-${BLUEZ_VERSION}  && mkdir -p inc && ln -sfn ../lib inc/bluetooth && : > config.h  && gcc -static -Os -DHAVE_CONFIG_H -DVERSION=\"${BLUEZ_VERSION}\" -I. -Iinc -o /bccmd       tools/bccmd.c tools/csr.c tools/csr_hci.c tools/csr_h4.c tools/csr_bcsp.c tools/csr_3wire.c tools/ubcsp.c       lib/hci.c lib/bluetooth.c /csr_usb_stub.c

FROM scratch AS bccmd-bin
COPY --from=bccmd /bccmd /bccmd

# --- Cog DRM platform module: the version of the Alpine package, with the modeset renderer fix (rootfs/cog-patches).
# Only this module is rebuilt: the rest of Cog comes from the Alpine package.
FROM armv7 AS cog
ARG COG_VERSION
RUN apk add --no-cache coreutils meson curl xz patch cairo-dev eudev-dev libepoxy-dev libinput-dev libsoup3-dev       libwpebackend-fdo-dev mesa-dev wayland-dev wayland-protocols wpewebkit-dev libdrm-dev
ADD --checksum=sha256:cd4ec937175a290ccd7c8ec398e4569aec04084cd94b11b2d83518778ba9d055     https://github.com/Igalia/cog/releases/download/${COG_VERSION}/cog-${COG_VERSION}.tar.xz /cog.tar.xz
COPY rootfs/cog-patches /patches
RUN tar -xf /cog.tar.xz && cd cog-${COG_VERSION}  && for p in /patches/*.patch; do echo "applying $(basename $p)"; patch -p1 < $p; done  && meson setup build --prefix=/usr --buildtype=release -Dplatforms=drm -Dprograms=false  && meson compile -C build  && cp "$(find build -name libcogplatform-drm.so | head -1)" /libcogplatform-drm.so

FROM scratch AS cog-bin
COPY --from=cog /libcogplatform-drm.so /libcogplatform-drm.so

# --- initramfs: embedded in the kernel image. Alpine packages only (BusyBox, regulatory database, linux-firmware); the stock webOS files
# (board data of the TouchPad, optional GPU microcode) are only needed with BOARD_DATA=webos / ADRENO_FW=webos and come from the named
# build context `firmware`:  docker buildx build --build-context firmware=<dir> --build-arg BOARD_DATA=webos ...
# (<dir> holds webos/ath6k/hw2.1.1/bdata.SD32.bin). The empty stage below is the default of that context.
# BOARD_DATA: upstream (default, linux-firmware's generic file), webos (the file of the tablet), none (see scripts/initramfs.sh).
FROM scratch AS firmware

FROM armv7 AS initramfs
ARG ADRENO_FW=
ARG VIDC_FW=
ARG BOARD_DATA=upstream
RUN apk add --no-cache busybox-static wireless-regdb linux-firmware-ath6k linux-firmware-qcom zstd
COPY --from=tp-bin /devmem-small /work/devmem
COPY initramfs /work/initramfs
COPY scripts/build/initramfs.sh /initramfs.sh
RUN --mount=type=bind,from=firmware,target=/firmware \
    ADRENO_FW="$ADRENO_FW" VIDC_FW="$VIDC_FW" BOARD_DATA="$BOARD_DATA" sh /initramfs.sh

FROM scratch AS initramfs-bin
COPY --from=initramfs /out/initramfs.cpio.gz /initramfs.cpio.gz

# --- kernel: mainline Linux + kernel/ (patches, new drivers, device trees, config fragment) + the initramfs, then the uImage for moboot.
# Native stage on the platform of the builder (Alpine; the kernel is built with Alpine's arm-none-eabi cross compiler). Sources and objects live in a BuildKit cache mount, so a second
# build only recompiles what changed (kernel.sh restores and re-applies the patches when the set changed). The cache is not part of the image:
# everything wanted from it is copied to /out in the same RUN. `docker builder prune` empties it (full rebuild).
# The toolchain alone, also the image of `./build.sh menuconfig` and `./build.sh shell`.
FROM --platform=$BUILDPLATFORM alpine:${ALPINE_VERSION}@${ALPINE_DIGEST} AS kernel-env
ENV ARCH=arm CROSS_COMPILE=arm-none-eabi- BUILD_DIR=/build OUT_DIR=/out
RUN apk add --no-cache build-base bash bc bison flex openssl-dev elfutils-dev ncurses-dev gcc-arm-none-eabi binutils-arm-none-eabi \
      dtc u-boot-tools xz cpio rsync perl python3 patch linux-headers diffutils findutils coreutils tar wget ca-certificates

FROM kernel-env AS kernel
ARG KERNEL_VERSION
ARG KERNEL_SHA256
ARG DTB_NAME
ARG KERNEL_BASE
ADD --checksum=sha256:${KERNEL_SHA256} https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_VERSION}.tar.xz /linux.tar.xz
COPY kernel /work/kernel
COPY scripts/build/dispatch.sh scripts/build/kernel.sh scripts/build/uimage.sh /work/scripts/build/
COPY --from=initramfs-bin /initramfs.cpio.gz /out/initramfs.cpio.gz
RUN --mount=type=cache,id=tp-kernel-alpine,target=/build     set -e; mkdir -p /build; cp -n /linux.tar.xz /build/linux-${KERNEL_VERSION}.tar.xz  && KERNEL_VERSION="${KERNEL_VERSION}" DTB_NAME="${DTB_NAME}" KERNEL_BASE="${KERNEL_BASE}" bash /work/scripts/build/dispatch.sh kernel  && KERNEL_VERSION="${KERNEL_VERSION}" DTB_NAME="${DTB_NAME}" KERNEL_BASE="${KERNEL_BASE}" bash /work/scripts/build/dispatch.sh uimage  && cp /build/obj-kernel-${KERNEL_BASE%_defconfig}/.config /out/kernel.config

FROM scratch AS kernel-bin
ARG DTB_NAME
COPY --from=kernel /out/uImage /out/zImage /out/kernel.config /
COPY --from=kernel /out/${DTB_NAME}.dtb /

# --- Alpine root filesystem (prebuilt packages, BusyBox init, the overlay rootfs/overlay), as a tree
# Accounts: `touchpad` (password TOUCHPAD_PASSWORD, doas to root without a password), root locked. Without RELEASE=1 (development) the public
# key given as the secret `sshkey` (--secret id=sshkey,src=secrets/touchpad_ed25519.pub) also lets root in over SSH.
FROM --platform=${ARM_PLATFORM} alpine:${ALPINE_VERSION}@${ALPINE_DIGEST} AS rootfs-tree
ARG RELEASE=0
ARG TOUCHPAD_PASSWORD=alpine
# cog: WPE launcher with its DRM platform. Mesa: GBM/EGL + the software rasteriser (llvmpipe through kms_swrast) and the msm driver.
# eudev: libinput only sees the devices tagged by udev. dropbear: SSH. bash: needed by some helper scripts. Settings panel
# (docs/SETTINGS-PANEL.md): busybox-extras (httpd), dbus (session bus for cogctl), bluez (-deprecated for hciattach/hcitool/hciconfig),
# tzdata (time zones). doas: root for the user touchpad. alsa-utils (amixer, aplay; ~4 MB with alsa-lib): the mixer and player
# behind `tp sound` (volume, mute, test sound); tinyalsa 2.0's tinyplay starts the stream empty and underruns on this driver.
RUN apk add --no-cache \
      cog wpewebkit mesa-dri-gallium mesa-egl mesa-gbm mesa-gles libinput \
      eudev dropbear wpa_supplicant iw \
      font-dejavu ca-certificates shared-mime-info \
      bash \
      busybox-extras dbus bluez bluez-deprecated tzdata \
      doas alsa-utils \
 && mkdir -p /root/.ssh /etc/tp.d /etc/doas.d /mnt/media /run /tmp \
 && chmod 700 /root/.ssh
COPY rootfs/overlay/ /
# helpers shared with the initramfs (single source); the shell tp becomes tp-sh: the C tp runs it for the commands not ported yet
COPY initramfs/charge-mode initramfs/cpu-state initramfs/wifi-wpa initramfs/wifi-dhcp /usr/local/bin/
COPY initramfs/tp /usr/local/bin/tp-sh
COPY --from=tp-bin /tp /usr/local/bin/tp
COPY --from=ts-srv-bin /ts_srv /usr/local/bin/ts_srv
COPY --from=bccmd-bin /bccmd /usr/local/bin/bccmd
COPY --from=cog-bin /libcogplatform-drm.so /usr/lib/cog/modules/libcogplatform-drm.so
# devmem is a mode of tp (rootfs/tp/devmem.c). The file modes of a checkout are not reliable (Windows, git index): set them here.
RUN ln -s tp /usr/local/bin/devmem \
 && chmod 755 /etc/rcS /etc/tp.d/S* /usr/local/bin/*
# optional RTSP server (RTSP=1): the stage is empty without it
RUN --mount=type=bind,from=tp-rtsp-sel,target=/mnt/rtsp \
    if [ -f /mnt/rtsp/tp-rtsp ]; then install -m 755 /mnt/rtsp/tp-rtsp /usr/local/bin/tp-rtsp; fi
# BlueCore PSR (rootfs/bluetooth/bluecore6.psr, see its README): the example BD address and the UART keys of the file are dropped (the
# address comes from the webOS token, bt-power)
COPY rootfs/bluetooth/bluecore6.psr /tmp/bluecore6.psr
RUN mkdir -p /etc/bluetooth  && awk '/^\/\/ PSKEY_(BDADDR|UART_BAUDRATE|UART_CONFIG_BCSP|UART_HOST_WAKE|UART_HOST_WAKE_SIGNAL)$/ {skip=2} skip>0 {skip--; next} {print}'       /tmp/bluecore6.psr > /etc/bluetooth/bluecore6.psr  && rm /tmp/bluecore6.psr
# A secret is not part of the cache key: without this argument (the fingerprint of the key, `none` without one) a build with another key,
# or none, would reuse the layer of an earlier build.
ARG SSH_KEY_ID=none
RUN --mount=type=secret,id=sshkey,required=false \
    set -e; echo "developer key: $SSH_KEY_ID"; \
    H=$(cryptpw -m sha512 -S tptouchpadsalt01 "$TOUCHPAD_PASSWORD"); \
    adduser -D -u 1000 -s /bin/sh touchpad; \
    echo "touchpad:$H" | chpasswd -e; \
    addgroup touchpad wheel; \
    echo 'permit nopass touchpad as root' > /etc/doas.d/touchpad.conf; chmod 400 /etc/doas.d/touchpad.conf; \
    if [ "$RELEASE" != 1 ] && [ -s /run/secrets/sshkey ]; then install -m600 /run/secrets/sshkey /root/.ssh/authorized_keys; fi; \
    chmod 700 /root /root/.ssh

# ext4 image of the tree (label tproot): copied to the media volume as touchpad/rootfs-alpine.ext4, selected by touchpad/rootfile
FROM --platform=$BUILDPLATFORM alpine:${ALPINE_VERSION}@${ALPINE_DIGEST} AS rootfs-img
RUN apk add --no-cache e2fsprogs
COPY --from=rootfs-tree / /rootfs
RUN set -e; MB=$(du -sm /rootfs | cut -f1); SIZE=$((MB * 13 / 10 + 128)); \
    echo ">> rootfs content: ${MB} MB, image: ${SIZE} MB"; \
    mke2fs -q -t ext4 -L tproot -d /rootfs /rootfs-alpine.ext4 ${SIZE}M

FROM scratch AS rootfs-bin
COPY --from=rootfs-img /rootfs-alpine.ext4 /rootfs-alpine.ext4

# --- artifacts: what the flashing step needs (docs/FLASHING.md), with checksums and BUILD-INFO. The default target (last stage).
# Checksums depend on the content only (cached while the images are unchanged); the date and commit go into a tiny stage of their own,
# so a rebuild with nothing changed does not copy or hash the 600 MB image again.
FROM alpine:${ALPINE_VERSION}@${ALPINE_DIGEST} AS sums
RUN --mount=type=bind,from=kernel-bin,target=/k --mount=type=bind,from=rootfs-bin,target=/r     { cd /k && sha256sum uImage; cd /r && sha256sum rootfs-alpine.ext4; } > /SHA256SUMS

FROM alpine:${ALPINE_VERSION}@${ALPINE_DIGEST} AS info
ARG RELEASE=0
ARG BUILD_NAME=dev
ARG GIT_COMMIT=unknown
ARG BUILD_DATE=unknown
ARG KERNEL_VERSION
ARG KERNEL_BASE
ARG DTB_NAME
ARG ALPINE_VERSION
RUN {       echo "release:  $BUILD_NAME";       echo "commit:   $GIT_COMMIT";       echo "date:     $BUILD_DATE";       echo "kernel:   Linux $KERNEL_VERSION, base $KERNEL_BASE, DTB $DTB_NAME";       echo "rootfs:   Alpine $ALPINE_VERSION (prebuilt packages), rootfs-alpine.ext4";       echo "access:   user touchpad (password), doas to root$( [ "$RELEASE" = 1 ] || echo '; development image: the project key is accepted for root')";       echo "install:  uImage -> /boot/uImage.Linux (docs/FLASHING.md); rootfs-alpine.ext4 -> media volume touchpad/";     } > /BUILD-INFO

FROM scratch AS artifacts
COPY --from=kernel-bin /uImage /uImage
COPY --from=rootfs-bin /rootfs-alpine.ext4 /rootfs-alpine.ext4
COPY --from=sums /SHA256SUMS /SHA256SUMS
COPY --from=info /BUILD-INFO /BUILD-INFO
