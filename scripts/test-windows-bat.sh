#!/usr/bin/env bash
# Local smoke for install_windows.bat under wine cmd (throwaway container).
# Wine lacks choice.exe and has imperfect `exist` semantics, so this smoke
# asserts parse health only: no "unexpected", both runs reach "Locating adb".
# The strict-behaviour gate lives in the CI win-smoke job (real cmd.exe).
set -u
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends wine64 >/dev/null 2>&1
WINE=/usr/lib/wine/wine64
[ -x "$WINE" ] || WINE=wine64
export WINEPREFIX=/tmp/pfx
"$WINE" -i >/dev/null 2>&1 || true
cd /w
printf "Q\n" | timeout 90 "$WINE" cmd /c "Z:\\w\\scripts\\install_windows.bat" > menu.log 2>&1
printf "1\n" | timeout 90 "$WINE" cmd /c "Z:\\w\\scripts\\install_windows.bat 192.168.88.1" > ip.log 2>&1
echo "=== menu.log (tail)"; tail -6 menu.log
echo "=== ip.log (tail)"; tail -6 ip.log
RC=0
grep -qi "unexpected" menu.log ip.log && { echo "FAIL: unexpected parse text found"; RC=1; }
grep -q "Locating adb" ip.log      || { echo "FAIL: ip branch did not reach adb stage"; RC=1; }
grep -q "Locating adb" menu.log    || { echo "FAIL: menu run did not reach adb stage"; RC=1; }
[ "$RC" = 0 ] && echo WINE-SMOKE-OK || echo WINE-SMOKE-FAIL
exit $RC
