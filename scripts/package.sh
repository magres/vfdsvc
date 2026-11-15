#!/usr/bin/env bash
# Packages the release zip: dist/vfdsvc-<ver>.zip (self-contained installer kit).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
VER="$(git -C "$HERE" describe --tags --always 2>/dev/null | sed 's/^v//' || echo dev)"
OUT="$HERE/dist/vfdsvc-$VER.zip"
STAGE="$HERE/dist/pack"

rm -rf "$STAGE" && mkdir -p "$STAGE"
cd "$HERE"
cp dist/vfdsvc-armv7 dist/vfdsvc-arm64 "$STAGE/"
mkdir -p "$STAGE/scripts"
cp scripts/sys_leds scripts/device-install.sh scripts/device-uninstall.sh scripts/install.sh "$STAGE/scripts/"
cp scripts/install_windows.bat "$STAGE/"
cp -r conf tooling "$STAGE/"
cp README.md LICENSE "$STAGE/"
# adb ships under Apache-2.0 (see tooling/NOTICE.md); a copy at the zip root
# keeps the redistribution notice next to the project's own LICENSE
cp tooling/NOTICE.md "$STAGE/THIRD-PARTY-NOTICE.md" 2>/dev/null || true
# adb binaries are optional at packaging time (CI fetches them; local zip
# without tooling falls back to system adb)
find "$STAGE/tooling" -type f \( -name 'adb' -o -name 'adb.exe' -o -name '*.dll' \) | while read -r f; do
  [ -s "$f" ] || rm -f "$f"
done

# nettoyage: git internals, build logs, dev junk
rm -rf "$STAGE"/.git "$STAGE"/dist 2>/dev/null || true
find "$STAGE" -name '*.log' -delete 2>/dev/null || true

cd "$STAGE"
chmod +x scripts/*.sh vfdsvc-armv7 vfdsvc-arm64 tooling/*/adb 2>/dev/null || true
mkdir -p "$HERE/dist"
zip -q -r "$OUT" .
cd "$HERE"
rm -rf "$STAGE"
ls -la "$OUT"
