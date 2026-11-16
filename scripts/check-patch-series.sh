#!/usr/bin/env bash
# Host helper (Git Bash): applies the active kernel patches (kernel/patches/*.patch, in file name order, as scripts/kernel.sh does) and the
# out-of-tree sources (kernel/src) on the pristine Linux tarball, in a scratch directory of the kernel toolchain image (tp-kernel-env, built here if needed; the tarball is kept in its volume tp-kernel-dev), and prints one hash of
# every file they touch. Two series (e.g. before and after renumbering the patches) that print the same hash give the same kernel source.
# Usage: scripts/check-patch-series.sh [-v]     (-v also lists the hash of each file)
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
docker buildx build -q --target kernel-env --load -t tp-kernel-env "$HERE" >/dev/null
MSYS_NO_PATHCONV=1 docker run --rm -v "$HERE:/work:ro" -v tp-kernel-dev:/build -e VERBOSE="${1:-}" tp-kernel-env bash -c '
  set -eu
  TARBALL=/build/linux-6.12.40.tar.xz
  [ -f $TARBALL ] || wget -q -O $TARBALL https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.40.tar.xz
  W=$(mktemp -d)
  PATCHES=$(ls /work/kernel/patches/*.patch | sort)
  TOUCHED=$(grep -h "^+++ b/" $PATCHES | sed "s|^+++ b/||; s|[[:space:]].*||" | sort -u)
  cd $W
  # one pass over the tarball for every touched file (members that do not exist are skipped)
  tar -xJf $TARBALL --ignore-failed-read $(for f in $TOUCHED; do echo "linux-6.12.40/$f"; done) 2>/dev/null || true
  mkdir -p linux-6.12.40
  cd linux-6.12.40
  if [ -d /work/kernel/src ]; then cp -r /work/kernel/src/. .; fi
  for p in $PATCHES; do patch -p1 -s --no-backup-if-mismatch < $p || { echo "FAILED: $p" >&2; exit 1; }; done
  SRCFILES=$( (cd /work/kernel/src 2>/dev/null && find . -type f | sed "s|^\./||") || true )
  FILES=$(printf "%s\n" $TOUCHED $SRCFILES | sort -u)
  if [ -n "${VERBOSE:-}" ]; then for f in $FILES; do [ -f $f ] && sha1sum $f; done; fi
  echo "$(printf "%s\n" $PATCHES | wc -l) patches, $(printf "%s\n" $FILES | wc -l) files, series hash: $(for f in $FILES; do [ -f $f ] && sha1sum $f; done | sha1sum | cut -d" " -f1)"
'
