#!/system/bin/sh
# vfdsvc device-side hardware check ("doctor"). Runs as root on the box.
# Invoked by install_windows.bat (CHECK menu item) / install.sh via:
#   sh /data/local/tmp/vfdsvc-installer/device-check.sh
#
# Expects the payload in /data/local/tmp/vfdsvc-installer/:
#   vfdsvc            daemon binary (this device's ABI)
#   vfdsvc.conf       configuration candidate
#
# Checks (all reversible, nothing is installed):
#   1. root                uid=0 required
#   2. soc                 SoC / Android version report
#   3. gpio                sysfs GPIO + gpiochip for the config's banks
#   4. pins                warn if display pins are owned by another driver
#                          (bootstrap frees them at install time)
#   5. export              export/unexport roundtrip of CLK/DAT/STB
#   6. display             all-segments test for 10 s (sys_leds is stopped
#                          and restarted around it)
#
# Prints one "[OK]/[WARN]/[FAIL] ..." line per check, then a final verdict:
#   CHECK-OK               everything passed, vfdsvc will work
#   CHECK-FAIL: <reason>   fix the reason and run CHECK again
# Exits 0 on OK, 1 on FAIL.

SRC=/data/local/tmp/vfdsvc-installer
CONF=$SRC/vfdsvc.conf
DAEMON=$SRC/vfdsvc

fail() { echo "CHECK-FAIL: $1"; exit 1; }
ok()   { echo "[OK] $1"; }
warn() { echo "[WARN] $1"; }

# --- sanity ----------------------------------------------------------------
[ "$(id -u)" = "0" ] 2>/dev/null || [ "$(su -c 'id -u' 2>/dev/null)" = "0" ] \
	|| fail "no root via ADB (run: adb root, or enable su in the firmware)"
ok "root access (uid=0)"

[ -f "$DAEMON" ] || fail "payload missing: vfdsvc binary not pushed"
[ -f "$CONF" ]   || fail "payload missing: vfdsvc.conf not pushed"
chmod 755 "$DAEMON"

# --- 2. SoC report ----------------------------------------------------------
soc=$(getprop ro.board.platform 2>/dev/null)
[ -n "$soc" ] || soc=$(getprop ro.hardware 2>/dev/null)
echo "[INFO] SoC: $soc, Android $(getprop ro.build.version.release), ABI: $(getprop ro.product.cpu.abilist 2>/dev/null | cut -d, -f1)"

# --- confval helpers (same parsing as the bootstrap sys_leds script) --------
confval() {
	grep -E "^[[:space:]]*$1" "$CONF" 2>/dev/null | head -1 | sed 's/.*=//' | tr -d "'\" "
}
pin_of() {
	val=$(confval "$1")
	[ -n "$val" ] || return 1
	echo "$val" | cut -d, -f2
}

# --- 3. gpiochips -----------------------------------------------------------
[ -d /sys/class/gpio ] || fail "no /sys/class/gpio (kernel too old / non-Amlogic build)"
ok "sysfs GPIO present"

bank_of() { confval "$1" | cut -d, -f1; }
for key in vfd_gpio_clk vfd_gpio_dat vfd_gpio_stb; do
	bank=$(bank_of "$key") || fail "config '$CONF': $key missing (need porting for this box)"
	case "$bank" in
		0) label=periphs-banks ;;
		1) label=aobus-banks ;;
		*) fail "config: unknown gpio bank '$bank' in $key" ;;
	esac
	found=
	for d in /sys/class/gpio/gpiochip*; do
		[ -e "$d/label" ] || continue
		if [ "$(cat "$d/label" 2>/dev/null)" = "$label" ]; then
			base=$(cat "$d/base")
			ngpio=$(cat "$d/ngpio")
			found=1
			break
		fi
	done
	[ -n "$found" ] || fail "gpiochip '$label' (bank $bank, needed by $key) not found — unsupported SoC?"
	pin=$(pin_of "$key")
	[ -n "$pin" ] && [ "$pin" -lt "$ngpio" ] 2>/dev/null \
		|| fail "$key: pin $pin outside gpiochip range (0..$((ngpio-1))) — wrong config for this box"
	eval "${key}_base=$base" "${key}_pin=$pin"
done
ok "gpiochip(s) for banks found, config pins in range"

# --- 4. pin owners (debugfs) ------------------------------------------------
mount -t debugfs none /sys/kernel/debug 2>/dev/null
for key in vfd_gpio_clk vfd_gpio_dat vfd_gpio_stb; do
	eval "pin=\$${key}_pin"
	[ -n "$pin" ] || continue
	for d in /sys/kernel/debug/pinctrl/*/pinmux-pins; do
		[ -e "$d" ] || continue
		owner=$(grep -E "^pin ${pin} \(" "$d" 2>/dev/null | head -1 |
			sed 's/^[^:]*: *//' | awk '{print $1}')
		case "$owner" in
			""|"(MUX"|"pin") ;;
			*) warn "pin $pin ($key) owned by '$owner' — bootstrap will unbind it at install/boot" ;;
		esac
	done
done

# --- 5. export/unexport roundtrip -------------------------------------------
# If the daemon is already running and holds the pins, do NOT probe: the pins
# are obviously claimable (we hold them). Otherwise free the pins exactly the
# way the bootstrap does at install/boot (unbind any driver claiming them),
# drop any leftover export from a dead process, and verify each pin with an
# export/unexport roundtrip (a pin held by an ununbindable driver fails).
mount -t debugfs none /sys/kernel/debug 2>/dev/null
free_pin() {
	pin=$1
	[ -n "$pin" ] || return 0
	for d in /sys/kernel/debug/pinctrl/*/pinmux-pins; do
		[ -e "$d" ] || continue
		owner=$(grep -E "^pin ${pin} \(" "$d" 2>/dev/null | head -1 |
			sed 's/^[^:]*: *//' | awk '{print $1}')
		case "$owner" in
			""|"(MUX"|"pin") continue ;;
		esac
		if [ -e "/sys/bus/platform/devices/$owner/driver/unbind" ]; then
			echo "$owner" > "/sys/bus/platform/devices/$owner/driver/unbind" 2>/dev/null \
				&& ok "pin $pin freed (unbound $owner)"
		fi
	done
}

daemon_running=$(pgrep vfdsvc 2>/dev/null | head -1)
for key in vfd_gpio_clk vfd_gpio_dat vfd_gpio_stb; do
	eval "base=\$${key}_base"; eval "pin=\$${key}_pin"
	n=$((base + pin))
	if [ -n "$daemon_running" ]; then
		if [ -e "/sys/class/gpio/gpio$n" ]; then
			ok "$key: gpio $n held by running vfdsvc"
		else
			warn "$key: vfdsvc running but gpio $n not exported"
		fi
		continue
	fi
	free_pin "$pin"
	echo "$n" > /sys/class/gpio/unexport 2>/dev/null
	if echo "$n" > /sys/class/gpio/export 2>/dev/null; then
		echo "$n" > /sys/class/gpio/unexport 2>/dev/null
		ok "$key: gpio $n export/unexport roundtrip OK"
	else
		fail "$key: cannot export gpio $n (pin claimed by another driver)"
	fi
done

# --- 6. display test --------------------------------------------------------
echo "== Display test: all segments and icons light up for 10 seconds ..."
stop sys_leds 2>/dev/null
killall vfdsvc 2>/dev/null
sleep 1
"$DAEMON" --test 10 -c "$CONF"
start sys_leds 2>/dev/null

# wait until the bootstrap brings the daemon back (a few seconds, up to 60)
waited=0
while [ "$waited" -lt 60 ]; do
	pid=$(pgrep vfdsvc 2>/dev/null | head -1)
	[ -n "$pid" ] && break
	sleep 2
	waited=$((waited + 2))
done
if [ -n "$pid" ]; then
	echo "[OK] daemon restarted (pid $pid)"
else
	warn "daemon did not come back within 60 s — check /data/local/tmp/vfdsvc_boot.log"
fi

echo CHECK-OK
exit 0
