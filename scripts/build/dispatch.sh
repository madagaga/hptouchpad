#!/usr/bin/env bash
# Runs INSIDE the kernel stage of the Dockerfile (and in the kernel-env image for menuconfig/shell). Kernel build step dispatcher.
# Everything else is built by the other stages of the Dockerfile: ./build.sh at the top of the repository is the entry point.
set -euo pipefail

export WORK=/work
export BUILD_DIR="${BUILD_DIR:-/build}"
export OUT_DIR="${OUT_DIR:-/out}"
export KERNEL_VERSION="${KERNEL_VERSION:-6.12.40}"
export JOBS="${JOBS:-$(nproc)}"

# Boot parameters (load address confirmed by the webOS kernel layout).
export DTB_NAME="${DTB_NAME:-qcom-apq8060-hp-touchpad-kms}"
export KERNEL_LOADADDR="${KERNEL_LOADADDR:-0x40208000}"
export KERNEL_ENTRY="${KERNEL_ENTRY:-0x40208000}"

S="$WORK/scripts/build"
mkdir -p "$BUILD_DIR" "$OUT_DIR"

case "${1:-}" in
  kernel)     bash "$S/kernel.sh" ;;
  menuconfig) bash "$S/kernel.sh" menuconfig ;;
  uimage)     bash "$S/uimage.sh" ;;
  shell)      exec bash ;;
  *) echo "usage: dispatch.sh kernel|menuconfig|uimage|shell" >&2; exit 1 ;;
esac
