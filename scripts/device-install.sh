#!/system/bin/sh
# vfdsvc device-side installer. Runs as root on the box (invoked by
# install.sh / install_windows.bat via: sh /data/local/tmp/vfdsvc-installer/install.sh)
#
# Expects the payload in /data/local/tmp/vfdsvc-installer/:
#   vfdsvc            daemon binary (for this device's ABI)
#   vfdsvc.conf       configuration (device-specific, default TX92)
#   sys_leds          bootstrap script template
#
# Installs:
#   /system/bin/vfdsvc            (daemon)
#   /system/etc/vfdsvc.conf       (config)
#   /system/bin/sys_leds          (bootstrap, original saved as sys_leds.orig)
#   /system/etc/init/vfdsvc.rc    (only when no sys_leds.rc exists)
#
# Everything is reversible: original sys_leds is kept as sys_leds.orig.

set -e
SRC=/data/local/tmp/vfdsvc-installer
LOG=/data/local/tmp/vfdsvc-install.log
CONF=/system/etc/vfdsvc.conf
BOOT=/system/bin/sys_leds
DAEMON=/system/bin/vfdsvc

exec 2>>"$LOG"
set -x

mount -o remount,rw / 2>/dev/null || true
mount -o remount,rw /system 2>/dev/null || true

[ -f "$SRC/vfdsvc" ]      || { echo "ERROR: payload missing (vfdsvc)";   exit 1; }
[ -f "$SRC/vfdsvc.conf" ] || { echo "ERROR: payload missing (conf)";      exit 1; }
[ -f "$SRC/sys_leds" ]    || { echo "ERROR: payload missing (bootstrap)"; exit 1; }

# 0) stop the running daemon and its supervisor BEFORE replacing any file
#    (otherwise 'cp vfdsvc' fails with 'Text file busy')
stop sys_leds 2>/dev/null || true
killall vfdsvc 2>/dev/null || true

# 1) daemon binary
cp "$SRC/vfdsvc" "$DAEMON"
chmod 755 "$DAEMON"
chown root:shell "$DAEMON" 2>/dev/null || true

# 2) config (timezone is auto-detected by the daemon at each start via
#    `date +%z` from bionic; set `tz_offset=+HHMM` in the conf to override)
cp "$SRC/vfdsvc.conf" "$CONF"
chmod 644 "$CONF"
chown root:root "$CONF" 2>/dev/null || true

# 3) bootstrap script + backup of the original
if [ -f "$BOOT" ] && ! grep -qs "vfdsvc-bootstrap" "$BOOT"; then
	[ -f "$BOOT.orig" ] || cp "$BOOT" "$BOOT.orig"
fi
cp "$SRC/sys_leds" "$BOOT"
chmod 755 "$BOOT"
chown root:shell "$BOOT" 2>/dev/null || true

# 4) init rc fallback (only when the ROM has no sys_leds service at all)
if ! grep -qs "service *sys_leds" /system/etc/init/sys_leds.rc /system/etc/init/*.rc /etc/init/*.rc 2>/dev/null; then
	if [ ! -f /system/etc/init/vfdsvc.rc ]; then
		cat > /system/etc/init/vfdsvc.rc <<'EOF'
# Starts vfdsvc (front display daemon) once the system finishes booting.
on property:dev.bootcomplete=1
    start sys_leds

service sys_leds /system/bin/sys_leds
    class main
    seclabel u:r:init:s0
EOF
		chmod 644 /system/etc/init/vfdsvc.rc
	fi
fi

sync
echo INSTALL-OK
exit 0
