#!/usr/bin/env bash
# Packages vfdsvc as TWRP-flashable zips (install + uninstall).
# Prereq: dist/vfdsvc-armv7 and dist/vfdsvc-arm64 must be built (make armv7 arm64).
#
# The zip uses a shell-based update-binary (the standard TWRP "flashable
# script" channel): recovery runs  update-binary <api> <outfd> <zipfile>
# and reads our ui_print lines from /proc/self/fd/<outfd>.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
VER="${VFD_VERSION:-0.1.5}"
OUT="$HERE/dist"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

for abi in armv7 arm64; do
  [ -f "$HERE/dist/vfdsvc-$abi" ] || { echo "missing dist/vfdsvc-$abi (run: make armv7 arm64)"; exit 1; }
done

# ---------------- update-binary: install --------------------------------
write_install_binary() {
cat > "$1" <<'SH'
#!/sbin/sh
# vfdsvc TWRP installer (Tanix TX92 preset; see /system/etc/vfdsvc.conf after).
# update-binary <api> <outfd> <zipfile>
OUTFD="$2"
ZIP="$3"
TMP=/tmp/vfdsvc-tw

ui() { echo "ui_print $1" > /proc/self/fd/"$OUTFD"; echo "ui_print" > /proc/self/fd/"$OUTFD"; }
fail() { ui "ОШИБКА: $1"; exit 1; }

ui "vfdsvc: ставим часики на передний дисплей"
ui " "

# mount /system (TWRP не монтирует по умолчанию)
mount /system 2>/dev/null || true
if [ ! -d /system/bin ]; then
  for dev in /dev/block/by-name/system /dev/block/platform/*/by-name/system; do
    [ -e "$dev" ] && mount "$dev" /system 2>/dev/null && break
  done
fi
[ -d /system/bin ] || fail "не получилось смонтировать /system"

# pick binary by ABI
ABI="$(getprop ro.product.cpu.abi 2>/dev/null | tr -d '\r')"
case "$ABI" in
  arm64*|aarch64*) BIN=vfdsvc-arm64 ;;
  *)               BIN=vfdsvc-armv7 ;;
esac

# unzip payload (unzip или busybox)
rm -rf "$TMP"; mkdir -p "$TMP"
UNZ=""
for u in unzip "busybox unzip"; do
  if command -v ${u%% *} >/dev/null 2>&1; then UNZ="$u"; break; fi
done
[ -n "$UNZ" ] || fail "в recovery нет unzip — используйте .bat-установку"
$UNZ -o -q "$ZIP" "ARCH/$BIN" "ARCH/vfdsvc.conf" "ARCH/sys_leds" "ARCH/vfdsvc.rc" -d "$TMP"

# install
cp "$TMP/ARCH/$BIN"      /system/bin/vfdsvc
chmod 755 /system/bin/vfdsvc
chown root:shell /system/bin/vfdsvc 2>/dev/null || true
chcon u:object_r:system_file:s0 /system/bin/vfdsvc 2>/dev/null || true

cp "$TMP/ARCH/vfdsvc.conf" /system/etc/vfdsvc.conf
chmod 644 /system/etc/vfdsvc.conf
chcon u:object_r:system_file:s0 /system/etc/vfdsvc.conf 2>/dev/null || true

if [ -f /system/bin/sys_leds ] && ! grep -qs vfdsvc-bootstrap /system/bin/sys_leds; then
  [ -f /system/bin/sys_leds.orig ] || cp /system/bin/sys_leds /system/bin/sys_leds.orig
fi
cp "$TMP/ARCH/sys_leds" /system/bin/sys_leds
chmod 755 /system/bin/sys_leds
chown root:shell /system/bin/sys_leds 2>/dev/null || true
chcon u:object_r:system_file:s0 /system/bin/sys_leds 2>/dev/null || true

if ! grep -qs "service *sys_leds" /system/etc/init/*.rc 2>/dev/null; then
  cp "$TMP/ARCH/vfdsvc.rc" /system/etc/init/vfdsvc.rc
  chmod 644 /system/etc/init/vfdsvc.rc
  chcon u:object_r:system_file:s0 /system/etc/init/vfdsvc.rc 2>/dev/null || true
fi

rm -rf "$TMP"
umount /system 2>/dev/null || true
ui "Готово. Свайпните Reboot system"
ui "Через 30-60 секунд дисплей покажет часы."
exit 0
SH
chmod 755 "$1"
}

# ---------------- update-binary: uninstall ------------------------------
write_uninstall_binary() {
cat > "$1" <<'SH'
#!/sbin/sh
# vfdsvc TWRP uninstaller: restore original sys_leds, remove daemon files.
OUTFD="$2"
ui() { echo "ui_print $1" > /proc/self/fd/"$OUTFD"; echo "ui_print" > /proc/self/fd/"$OUTFD"; }
fail() { ui "ОШИБКА: $1"; exit 1; }

ui "vfdsvc: откатываем дисплей к оригинальному состоянию"
ui " "
mount /system 2>/dev/null || true
if [ ! -d /system/bin ]; then
  for dev in /dev/block/by-name/system /dev/block/platform/*/by-name/system; do
    [ -e "$dev" ] && mount "$dev" /system 2>/dev/null && break
  done
fi
[ -d /system/bin ] || fail "не получилось смонтировать /system"

if [ -f /system/bin/sys_leds.orig ]; then
  cp /system/bin/sys_leds.orig /system/bin/sys_leds
  rm -f /system/bin/sys_leds.orig
fi
rm -f /system/bin/vfdsvc /system/etc/vfdsvc.conf /system/etc/init/vfdsvc.rc
umount /system 2>/dev/null || true
ui "Готово. Оригинальный sys_leds возвращён."
ui "Перезагрузите приставку."
exit 0
SH
chmod 755 "$1"
}

# ---------------- assemble zips -----------------------------------------
make_zip() {
  local kind="$1" out="$2"
  local d="$STAGE/$kind"
  rm -rf "$d"; mkdir -p "$d/META-INF/com/google/android" "$d/ARCH"

  echo "# vfdsvc TWRP package (shell update-binary; edify not used)" > "$d/META-INF/com/google/android/updater-script"
  if [ "$kind" = install ]; then
    write_install_binary "$d/META-INF/com/google/android/update-binary"
  else
    write_uninstall_binary "$d/META-INF/com/google/android/update-binary"
  fi

  cp "$HERE/dist/vfdsvc-armv7" "$d/ARCH/vfdsvc-armv7"
  cp "$HERE/dist/vfdsvc-arm64" "$d/ARCH/vfdsvc-arm64"
  cp "$HERE/conf/tx92-vfd.conf" "$d/ARCH/vfdsvc.conf"
  cp "$HERE/scripts/sys_leds"     "$d/ARCH/sys_leds"
  cat > "$d/ARCH/vfdsvc.rc" <<'RC'
# Starts vfdsvc (front display daemon) once the system finishes booting.
on property:dev.bootcomplete=1
    start sys_leds

service sys_leds /system/bin/sys_leds
    class main
    seclabel u:r:init:s0
RC

  (cd "$d" && zip -q -r "$out" .)
  echo "built: $out"
}

mkdir -p "$OUT"
make_zip install  "$OUT/vfdsvc-$VER-twrp-install.zip"
make_zip uninstall "$OUT/vfdsvc-$VER-twrp-uninstall.zip"
ls -la "$OUT"/*-twrp-*.zip
