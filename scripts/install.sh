#!/usr/bin/env bash
# vfdsvc installer — Linux/macOS side (Windows users: run install_windows.bat).
#   ./install.sh 192.168.88.8:5555            install
#   ./install.sh 192.168.88.8:5555 --uninstall
#   ./install.sh 192.168.88.8:5555 --test 10  run 'all segments' display test
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ZIPDIR="$(cd "$HERE/.." && pwd)"   # repo root (when run from release zip, scripts/ is top-level)

ARGS=(); ACTION=install; TESTSECS=10
while [ $# -gt 0 ]; do
	case "$1" in
		--uninstall) ACTION=uninstall ;;
		--test) ACTION=test; shift; [ $# -gt 0 ] && [ "$1" -ge 0 ] 2>/dev/null && TESTSECS="$1" || true ;;
		*) ARGS+=("$1") ;;
	esac
	shift
done
IP="${ARGS[0]:-}"
[ -n "$IP" ] || { echo "usage: ./install.sh <android-box-ip> [--uninstall|--test [sec]]"; exit 2; }
case "$IP" in *:*) ;; *) IP="$IP:5555";; esac

# --- locate adb ------------------------------------------------------------
ADB=""
for cand in "$ZIPDIR/tooling/linux/adb" "$ZIPDIR/tooling/macos/adb" "$(command -v adb || true)"; do
	if [ -n "$cand" ] && [ -x "$cand" ]; then ADB="$cand"; break; fi
done
[ -n "$ADB" ] || { echo "adb not found (expected in tooling/)"; exit 1; }
echo "== using adb: $ADB"

echo "== connecting to $IP ..."
"$ADB" connect "$IP" || true
sleep 1
"$ADB" connect "$IP" || true
"$ADB" devices | tail -3

root_check() {
	if "$ADB" shell id -u 2>/dev/null | grep -q 0; then
		SU=""
	else
		if "$ADB" shell su -c 'id -u' 2>/dev/null | grep -q 0; then
			SU="su -c "
		else
			echo "ERROR: no root access from ADB."
			echo "  vfdsvc needs root: try '$ADB root' or enable su access in your ROM."
			exit 1
		fi
	fi
}
root_check

setup_remote() {
	R="/data/local/tmp/vfdsvc-installer"
	HDR=$("$ADB" shell getprop ro.product.cpu.abi | tr -d '\r')
	case "$HDR" in arm64*|aarch64*) BIN=vfdsvc-arm64 ;; *) BIN=vfdsvc-armv7 ;; esac
	BINPATH=""
	for p in "$ZIPDIR/$BIN" "$ZIPDIR/dist/$BIN"; do
		[ -f "$p" ] && BINPATH="$p" && break
	done
	[ -n "$BINPATH" ] || { echo "ERROR: $BIN not found (build with 'make armv7 arm64' first)"; exit 1; }
	echo "== device ABI: $HDR, using $BIN ($BINPATH)"
	"$ADB" push "$BINPATH"                          "$R/vfdsvc"             >/dev/null
"$ADB" push "$ZIPDIR/conf/tx92-vfd.conf"         "$R/vfdsvc.conf"        >/dev/null
"$ADB" push "$ZIPDIR/scripts/sys_leds"           "$R/sys_leds"           >/dev/null
"$ADB" push "$ZIPDIR/scripts/device-install.sh"  "$R/device-install.sh"  >/dev/null
"$ADB" push "$ZIPDIR/scripts/device-uninstall.sh" "$R/device-uninstall.sh" >/dev/null
echo "== payload staged on device (/data/local/tmp/vfdsvc-installer)"
}
setup_remote

case "$ACTION" in
	install)
		echo "== installing..."
		"$ADB" shell ${SU}sh /data/local/tmp/vfdsvc-installer/device-install.sh
		echo "== rebooting the box (display will wake up in ~30s)"
		"$ADB" shell ${SU}sync
		"$ADB" reboot || "$ADB" shell ${SU}reboot
		;;
	test)
		echo "== Reload display ($TESTSECS s): all segments + icons light up"
		"$ADB" shell ${SU}sh -c "'stop sys_leds 2>/dev/null; killall vfdsvc 2>/dev/null; true'"
		"$ADB" shell ${SU}'/data/local/tmp/vfdsvc-installer/vfdsvc -c /system/etc/vfdsvc.conf --test '"$TESTSECS"
		echo "== done"
		;;
	uninstall)
		echo "== uninstalling (restores original files)"
		"$ADB" shell ${SU}sh /data/local/tmp/vfdsvc-installer/device-uninstall.sh
		echo "== rebooting"
		"$ADB" shell ${SU}sync
		"$ADB" reboot || "$ADB" shell ${SU}reboot
		;;
esac
echo "DONE."
