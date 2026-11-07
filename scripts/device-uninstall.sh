#!/system/bin/sh
# vfdsvc device-side uninstaller. Runs as root on the box.
# Restores the original /system/bin/sys_leds from sys_leds.orig (if present),
# removes /system/bin/vfdsvc and /system/etc/vfdsvc.conf, stops the daemon.

set -e
LOG=/data/local/tmp/vfdsvc-install.log
exec 2>>"$LOG"
set -x

stop sys_leds 2>/dev/null || true
killall vfdsvc 2>/dev/null || true
sleep 1

mount -o remount,rw / 2>/dev/null || true
mount -o remount,rw /system 2>/dev/null || true

if [ -f /system/bin/sys_leds.orig ]; then
	cp /system/bin/sys_leds.orig /system/bin/sys_leds
	chmod 755 /system/bin/sys_leds
	rm -f /system/bin/sys_leds.orig
fi
rm -f /system/bin/vfdsvc
rm -f /system/etc/vfdsvc.conf
rm -f /system/etc/init/vfdsvc.rc

# release exported pins (harmless if absent)
for pin in 476 477 454; do
	echo "$pin" > /sys/class/gpio/unexport 2>/dev/null || true
done

sync
echo UNINSTALL-OK
exit 0
