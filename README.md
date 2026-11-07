# vfdsvc

**A userspace driver for the 7-segment "clock" LED display on the front panel of Amlogic Android TV boxes — shown here on the Tanix TX92.**

No kernel module, no reflash. It runs from `adb` on any PC with root access, installs itself into the system and reboots the box. Each driver handles the display in **HH:MM with blinking colon + status icons (Wi-Fi / LAN / USB / play)**.

| Working (tested here) | other models |
| --- | --- |
| Tanix TX92 2/16 (Amlogic S912) | Porting possible with a conf file, see [docs/porting.md](docs/porting.md) |

> Screen in the "after" state: `12:34` + racestation icons. Redistribution is allowed and welcomed.

## Why your display doesn't work

On custom firmwares like **slimBOXtv** (Android 9 for Amlogic) there is simply **no driver** for the front display: the stock firmware included one, custom builds dropped it. `vfdsvc` brings it back, exactly like the factory original, only where it can:

- show time HH:MM with a blinking colon;
- **automatically follow the Android timezone and the 12/24-hour settings** (re-checked hourly; can be pinned via conf `tz_offset` / `time_format`);
- light up state icons: **USB** (a USB flash drive/stick is attached), **LAN** (eth0 up), **Wi-Fi** (wlan0 up), **play** (media wake-locks);
- what happens when the screen turns off (standby) — configurable: `dim`, `off` or `keep`;
- it removes from pins any "deptores" like the `i2c_b` controller on TX92 (slimBOX keeps this pin busy).

Under the hood it's the same protocol as [linux_openvfd](https://github.com/arthur-liberman/linux_openvfd) (FD628/PT6964-family over GPIO) but implemented **in have user space**: the same logic, but driven via sysfs, and ABI-safe to vendor kernels (kernel small difference is not an issue).

## TL;DR install — Tanix TX92

Everything below works from **Windows** (the .bat in the kit); there is also a `install.sh` for Linux/macOS.

1. **Download** `vfdsvc-<version>.zip` from the [latest Release](https://github.com/magres/vfdsvc/releases/latest) and unpack it to any folder.
2. **Find the IP** of the box: `Настройки (Settings) → Wi‑Fi / Сеть (Network) → Текущая сеть / Connected` — the address looks like `192.168.1.100`. Alternatively see your router's admin page (DHCP clients list). *The box must be powered ON and reachable by network.*
3. **Double-click** `install_windows.bat` → choose **[1] INSTALL** → enter the IP → press Enter.
   The script will connect over ADB, verify root, backup the original file, install the daemon and reboot the box by itself.
4. **Wait 30–60 seconds** after the box boots — the display will show the clock. That's done!
5. Want to customize? Open `conf\tx92-vfd.conf` (simple text file) — brightness (`brightness=1..7`), standby action (`standby=dim/off/keep`), icon set (`icons=usb,eth,wifi,play`). Re-run [1] to apply.
6. **Rollback**: run the same `.bat` → **[0] UNINSTALL**. The original system file is restored from its `.orig` backup and the box reboots. Everything as it was before.

### What it changes (so you know what you accept / and can remove later)

- adds `vfdsvc` (front-display daemon) → `/system/bin/`
- adds config `/system/etc/vfdsvc.conf`
- replaces `/system/bin/sys_leds` with a bootstrap that starts the daemon at boot (the original is preserved next to it as `sys_leds.orig`)
- that's it; the firmware itself is NOT touched.

Requires **root over ADB** (`adb root` reached the shell as 0 or `su -c` works). On most custom firmwares (slimBOX, ATV9) it's already the case.

## For Linux / macOS power users

```
./scripts/install.sh 192.168.88.8            # install
./scripts/install.sh 192.168.88.8 --test 10  # all segments+icons for 10 s (handy debug)
./scripts/uninstall.sh 192.168.88.8          # rollback
```

## How to check "why it doesn't work" without installing?

```
./scripts/install.sh 192.168.88.8 --test 10
```
For 10 seconds the display shows all segments and all available icons — so you see wiring-wise everything is wired.

## Contributing

PRs welcome. Grab the conf for your box from the upstream list [arthur-liberman/vfd-configurations](https://github.com/arthur-liberman/vfd-configurations), follow [docs/porting.md](docs/porting.md), test on your hardware and send results.

## Credits & license

Contains logic derived from [arthur-liberman/linux_openvfd](https://github.com/arthur-liberman/linux_openvfd) (© Arthur Liberman, GPL-3.0). Distributed as [GPL-3.0](LICENSE). Bundled `adb` tooling comes from the official Android *platform-tools* package (Apache-2.0).

---

# ЧТО ЭТО И ЗАЧЕМ (по-русски, максимум подробно)

## Приставка — Tanix TX92 (Amlogic S912), прошивка — slimBOXtv / ATV9 и похожие

На корпусе приставки спереди есть небольшой дисплей-«часики»: 4 цифры с иконками. На заводской прошивке он показывал время. После прошивки *slimBOXtv* (или других кастомных Android 9) — погас: людей прошивок прошил и так и не вернул.

## Что умеет устанавливать

- показывать **часы HH:MM** с мигающим двоеточием («:» мигает раз в секунду);
- **сам находить часовой пояс и формат 12/24 часа** как у Android (обновляет раз в час; можно зафиксировать через `tz_offset` / `time_format` в конфиге);
- зажигать **иконки состояния**: USB (вставлена флешка), LAN (кабель в сети), Wi-Fi (подключён), «играть/播放» (медиа воспроизводится)
- настраивать **яркость** (1–7) и поведение при уходе в сон: **тускло / погасить / не трогать**
- НЕ перепрошивать и ничего не бросать при выключенной приставке

## Пошаговая установка на Tanix TX92 (ничего переустанавливать не нужно)

### Шаг 1: Скачайте архив
Вкладка [Releases](https://github.com/magres/vfdsvc/releases/latest) → скачайте `vfdsvc-…zip` → распакуйте в любую папку (например, `Рабочий стол\vfdsvc`).

### Шаг 2: Узнайте адрес приставки по сети — IP
На пульте приставки: `Настройки → Wi‑Fi` — строка «IP адрес» выглядит как «192.168.х.х» (например, `192.168.1.100`).
Или: в личном кабинете роутера адрес коробки находится в списке устройств.

> Прядок приставка должна быть **включена в сеть** (LAN или Wi-Fi) и логично подключена к тому же роутеру, что и ваш ПК.

### Шаг 3: Двойной клик `install_windows.bat`
Появится чёрное окно с меню:

```
[1] INSTALL  — установить драйвер
[0] UNINSTALL — откат
[T] TEST     — тест дисплея (все сегменты + иконки 10 секунд)
[Q] EXIT
```

Наберите `1` и **Enter**, затем введите IP приставки (из «Шага 2») и **Enter**.
Скрипт сам подключиться, проверит root, сделает резервную копию, установит всё необходимое и сам перезагрузит приставку.

### Шаг 4: Подождите минуту
Приставка перезагрузится (30–60 сек) — **дисплей покажет часы со значками**. Всё!

### Шаг 5: Настройка (по желанию)
Откройте файл `conf\tx92-vfd.conf` в обычном «Блокноте»:
- `brightness=7` — яркость (поставьте, например, `4` для слабой);
- `standby=dim` — что делать при погасшем экране: `dim` = слабая яркость, `off` = display погаснет, `keep` = не меняется;
- `icons=usb,eth,wifi,play` — какие значки отслеживать; можно убрать мешающие.

После правки повторите «Шаг 3» (кнопка [1]) — настройки применятся.

### Шаг 6: Как вернуть всё как было
Снова двойной клик `install_windows.bat` → **[0] UNINSTALL** → скрипт восстановит оригинал из резервной копии и перезагрузит приставку. Никаких изменений не останется.

## Частые вопросы и проблемы (FAQ)

**Окно консоли закрывается сразу, что-то мигнуло.**
Запускайте из `cmd`: Win+R → cmd → перейдите в папку командой `cd /d C:\путь\к\папке\vfdsvc` → наберите `install_windows.bat`. Тогда видны все шибки.

**«нет root» / `[ERROR] No root access via ADB`.**
В прошивке должен быть включён root. Проверьте: запустите из cmd `tooling\windows\adb.exe root`. На прошивках вроде slimBOX root обычно уже есть.

**«Connection refused / unable to connect».**
Приставка выключена / другой Wi-Fi у ПК и приставки / включена «Adb по сети» не в положении… на приставке в настройках включите «Отладка по USB/ADB» (или на заводских прошивках: настройки → Для разработчиков → «Отладка по ADB в сети»). При ADB по кабелю (USB): подключите приставку USB-кабелем к ПК и в .bat введите IP не нужно — просто повторите, но в cmd наберите `adb devices`, чтобы убедиться, что абонент виден.

**Дисплей показывает нечто странное (все цифры, хаос).**
На вашей модели другая раскладка пинов/символов. Это решается conf-файлом — см. `docs/porting.md` (пункт про Getting vfd.conf of your box) и предоставляйте результат, чтобы мы добавили конфиг в репо.

**Приставка потеряла.setIcon смысл после записи? Нет, что-то тому же.**
Сравните: у кастомных прошивок это нормально, что Root уже есть; НЕ ставьте правило root и не делайте платных `superoxy`-хаков.

## Про техническую часть (занятно)

Дисплей TX92 — это кристалл **FD628/PT6964**, подключённый к 3 ножкам процессора: **CLK=76, DAT=75, STB=53** «главного» gpio-банка. На прошивке slimBOX этот банк также **занял включённый в дереве оборудования идол `i2c_b`** — поэтому нечто, что «просто работало» в boot, перепрошивка ломает. Bootstrap-скрипт vfdsvc: это замечает (по `/sys/kernel/debug/pinctrl`), **отвязывает** мешающий драйвер на время своей работы и запускает протоколированный юзерспейс-демон. Разделитель мигает, иконки логично отражают то, что фактически произошло.

Эти же протоколы и код открытых проектов `arthur-liberman/linux_openvfd` и CoreELEC/OpenWRT «openvfd-скриптов» — вся информация по протоколам и каждая конфигурация прошивок. Драйвер здесь — все 200 строк чистого C, без внешних зависимостей, собранный для armv7 (все TX92) и arm64 (другие боксы).

## Похожие (но другие) проекты

- [arthur-liberman/linux_openvfd](https://github.com/arthur-liberman/linux_openvfd) — тот же протокол, но ядерный модуль (для CoreELEC/LibreELEC/Armbian и «своих» прошивок).
- Конфиги по прошивке — [arthur-liberman/vfd-configurations](https://github.com/arthur-liberman/vfd-configurations), у vfdsvc формат файлов такой же («vfd.conf»).

## История изменений

- **v0.1.0**: первый публичный релиз, Tanix TX92 как ref. Приставки.
