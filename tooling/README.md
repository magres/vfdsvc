# tooling/ — adb binaries are NOT stored in git

This directory is populated at release-build time (`scripts/package.sh` +
`.github/workflows/release.yml` fetch official binaries from Google):

- `windows/adb.exe`, `windows/AdbWinApi.dll`, `windows/AdbWinUsbApi.dll`
- `linux/adb`
- `macos/adb`

Source: the official Android **platform-tools** package
(https://developer.android.com/studio/releases/platform-tools, Apache-2.0).

To build a self-contained release zip locally:

```
curl -fsSL -o pt-w.zip https://dl.google.com/android/repository/platform-tools-latest-windows.zip
curl -fsSL -o pt-m.zip https://dl.google.com/android/repository/platform-tools-latest-darwin.zip
mkdir -p tooling/windows tooling/macos
cd <unpacked windows zip>  ; cp adb.exe AdbWinApi.dll AdbWinUsbApi ../../vfdsvc/tooling/windows/
cd <unpacked macos zip>    ; cp adb ../../vfdsvc/tooling/macos/ && chmod +x ../../vfdsvc/tooling/macos/adb
```
