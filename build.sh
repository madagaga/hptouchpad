#!/usr/bin/env bash
# Entry point of the build chain (Git Bash, Linux, macOS; needs Docker with BuildKit, nothing else). Everything is built by the stages of
# the Dockerfile; this script only chooses a stage, passes the settings and puts the result in out/<target>/.
#
#   ./build.sh [target]        all (default) | kernel | initramfs | rootfs | tp | tp-rtsp | ts-srv | bccmd | cog
#   ./build.sh release         the same as `all`, from a clean committed tree, as out/release/<date>-<commit>/ (RELEASE=1: no developer key)
#   ./build.sh menuconfig      kernel configuration (the result goes to out/kernel.defconfig); shell: a shell in the kernel toolchain image
#   ./build.sh clean | help
#
# Settings (environment): RELEASE=1, BOARD_DATA=upstream|webos|none, ADRENO_FW=webos, VIDC_FW=webos, RTSP=1 (adds /usr/local/bin/tp-rtsp to the rootfs), TOUCHPAD_PASSWORD, KERNEL_VERSION (with KERNEL_SHA256),
# KERNEL_BASE, DTB_NAME, FIRMWARE_DIR (stock webOS files, see the Dockerfile), SSH_PUBKEY (default secrets/touchpad_ed25519.pub, ignored with
# RELEASE=1), DIRTY=1 or --dirty (release of a tree with uncommitted changes). Plan and measurements: docs/BUILD-CLEANUP.md.
set -euo pipefail
cd "$(dirname "$0")"

usage() { sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; }

TARGET="${1:-all}"
[ $# -gt 0 ] && shift
DIRTY="${DIRTY:-}"
for a in "$@"; do
  case "$a" in
    --dirty) DIRTY=1 ;;
    *) echo "unknown option: $a" >&2; usage >&2; exit 1 ;;
  esac
done

NAME=dev
case "$TARGET" in
  all)            STAGE=artifacts;     OUT=out/artifacts ;;
  kernel)         STAGE=kernel-bin;    OUT=out/kernel ;;
  initramfs)      STAGE=initramfs-bin; OUT=out/initramfs ;;
  rootfs)         STAGE=rootfs-bin;    OUT=out/rootfs ;;
  tp)             STAGE=tp-bin;        OUT=out/tp ;;
  tp-rtsp)        STAGE=tp-rtsp-bin;   OUT=out/tp-rtsp ;;
  ts-srv|ts_srv)  STAGE=ts-srv-bin;    OUT=out/ts_srv ;;
  bccmd)          STAGE=bccmd-bin;     OUT=out/bccmd ;;
  cog)            STAGE=cog-bin;       OUT=out/cog ;;
  reboot-cmd)     STAGE=reboot-cmd-bin; OUT=out/reboot-cmd ;;
  release)
    # The build reads the files on disk, not git: a release refuses a tree with uncommitted changes (or untracked files under the build
    # inputs), so that it is exactly a commit.
    INPUTS="kernel initramfs rootfs scripts Dockerfile build.sh .dockerignore"
    CHANGES=$(git status --porcelain --untracked-files=no; git status --porcelain --untracked-files=all -- $INPUTS | grep '^??' || true)
    if [ -n "$CHANGES" ]; then
      if [ -z "$DIRTY" ]; then
        echo "!! uncommitted changes in the tree (a release is a commit); commit them, or use --dirty:" >&2
        echo "$CHANGES" | head -20 >&2
        exit 1
      fi
      echo ">> --dirty: building the tree as it is on disk, with these changes:"
      echo "$CHANGES" | head -20
    fi
    REV=$(git rev-parse --short HEAD)
    [ -n "$CHANGES" ] && REV="$REV-dirty"
    NAME="$(date +%Y%m%d-%H%M)-$REV"
    STAGE=artifacts; OUT="out/release/$NAME"; export RELEASE=1 ;;
  menuconfig|shell) STAGE=; OUT= ;;
  clean)
    rm -rf out/artifacts out/kernel out/initramfs out/rootfs out/tp out/tp-rtsp out/ts_srv out/bccmd out/cog out/reboot-cmd
    echo ">> out/<target> removed (not the other files of out/). The BuildKit cache (kernel objects): docker builder prune;"
    echo ">> the volume of ./build.sh menuconfig: docker volume rm tp-kernel-dev"
    exit 0 ;;
  help|-h|--help) usage; exit 0 ;;
  *) echo "unknown target: $TARGET" >&2; usage >&2; exit 1 ;;
esac

command -v docker >/dev/null || { echo "docker not found" >&2; exit 1; }

# settings -> Dockerfile build arguments
ARGS=()
for v in RELEASE BOARD_DATA ADRENO_FW VIDC_FW RTSP TOUCHPAD_PASSWORD KERNEL_VERSION KERNEL_SHA256 KERNEL_BASE DTB_NAME; do
  if [ -n "${!v:-}" ]; then ARGS+=(--build-arg "$v=${!v}"); fi
done
if [ -n "${KERNEL_VERSION:-}" ] && [ -z "${KERNEL_SHA256:-}" ]; then
  echo "KERNEL_VERSION needs KERNEL_SHA256 (the tarball is checked)" >&2; exit 1
fi
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ -n "$(git status --porcelain --untracked-files=no 2>/dev/null || true)" ]; then COMMIT="$COMMIT-dirty"; fi
ARGS+=(--build-arg "BUILD_NAME=$NAME" --build-arg "GIT_COMMIT=$COMMIT")
ARGS+=(--build-arg "BUILD_DATE=$(date -u +%Y-%m-%dT%H:%MZ)")

# stock webOS files (only for BOARD_DATA=webos and ADRENO_FW=webos)
FW="${FIRMWARE_DIR:-}"
if [ -z "$FW" ] && { [ "${BOARD_DATA:-}" = webos ] || [ "${ADRENO_FW:-}" = webos ] || [ "${VIDC_FW:-}" = webos ]; } && [ -d firmware ]; then FW=firmware; fi
[ -n "$FW" ] && ARGS+=(--build-context "firmware=$FW")

# developer image: the public key of the project also lets root in over SSH
KEY="${SSH_PUBKEY:-secrets/touchpad_ed25519.pub}"
KEY_ID=none
if [ "${RELEASE:-0}" != 1 ] && [ -f "$KEY" ]; then
  ARGS+=(--secret "id=sshkey,src=$KEY")
  KEY_ID=$( (sha256sum "$KEY" 2>/dev/null || shasum -a 256 "$KEY") | cut -c1-16 )
fi
ARGS+=(--build-arg "SSH_KEY_ID=$KEY_ID")   # a secret is not part of the cache key: its fingerprint is

if [ -z "$STAGE" ]; then
  # interactive kernel configuration: BuildKit has no terminal, so the toolchain image is run by hand, on its own volume
  docker buildx build --target kernel-env --load -t tp-kernel-env .
  mkdir -p out
  TTY=-i; [ -t 0 ] && TTY=-it
  MSYS_NO_PATHCONV=1 docker run --rm $TTY -v "$PWD:/work" -v "$PWD/out:/out" -v tp-kernel-dev:/build \
    -e KERNEL_VERSION="${KERNEL_VERSION:-}" -e DTB_NAME="${DTB_NAME:-}" -e KERNEL_BASE="${KERNEL_BASE:-}" \
    tp-kernel-env bash /work/scripts/build/dispatch.sh "$TARGET"
  exit $?
fi

rm -rf "$OUT"
docker buildx build "${ARGS[@]}" --target "$STAGE" --output "type=local,dest=$OUT" .
echo ">> $TARGET: $OUT"
ls -l "$OUT"
[ -f "$OUT/SHA256SUMS" ] && cat "$OUT/SHA256SUMS" || true
