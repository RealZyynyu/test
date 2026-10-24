#!/usr/bin/env bash
set -Eeuo pipefail
if [ "$EUID" -ne 0 ]; then echo 'Please run as root' >&2; exit 1; fi
for exe in lb debootstrap xorriso mksquashfs rsvg-convert; do
  command -v "$exe" >/dev/null || { echo "Missing $exe" >&2; exit 1; }
done
ROOT="$(cd "$(dirname "$0")" && pwd)"
WORK="$ROOT/.build"
OUT="$ROOT/output"
mkdir -p "$WORK" "$OUT"
cd "$WORK"
lb clean --purge || true
rm -rf config
lb config --mode debian --distribution trixie --architecture amd64 \
  --binary-image iso-hybrid --debian-installer none \
  --archive-areas "main contrib non-free non-free-firmware" \
  --linux-flavours amd64 --iso-application "NOVA OS Live" \
  --iso-publisher "NOVA OS" --iso-volume "NOVA_OS" \
  --bootappend-live "boot=live components quiet splash"
mkdir -p config/package-lists config/includes.chroot
cp -a "$ROOT/config/package-lists/." config/package-lists/
cp -a "$ROOT/config/includes.chroot/." config/includes.chroot/
mkdir -p config/includes.chroot/usr/share/wallpapers/NOVA
rsvg-convert -w 1920 -h 1080 "$ROOT/assets/nova-light.svg" -o config/includes.chroot/usr/share/wallpapers/NOVA/nova-light.png
rsvg-convert -w 1920 -h 1080 "$ROOT/assets/nova-dark.svg" -o config/includes.chroot/usr/share/wallpapers/NOVA/nova-dark.png
lb build 2>&1 | tee "$OUT/build.log"
ISO="$(find "$WORK" -maxdepth 1 -name '*.iso' -type f | head -n 1)"
test -n "$ISO" || { echo 'ISO not produced' >&2; exit 1; }
cp "$ISO" "$OUT/nova-os-amd64.iso"
(cd "$OUT" && sha256sum nova-os-amd64.iso > nova-os-amd64.iso.sha256)
echo "Built $OUT/nova-os-amd64.iso"
