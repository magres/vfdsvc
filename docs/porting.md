# Porting vfdsvc to other Amlogic boxes

`vfdsvc` is fully driven by a single config file (the same format used by the
excellent [linux_openvfd `vfd.conf` collection](https://github.com/arthur-liberman/vfd-configurations)).
To support another box you usually only need a slightly different conf.

## Quick route

1. Open the upstream repo list and check whether your box already has a
   `*-vfd.conf` (e.g. `tanix-tx3-mini-vfd.conf`, `x96-max-vfd.conf`, …).
2. Copy it to `conf/<your-box>-vfd.conf`, then append the vfdsvc extras:
   ```
   brightness=7
   standby=dim
   icons=usb,eth,wifi,play
   # optional manual UTC offset; daemon auto-detects otherwise
   # tz_offset=+0500
   ```
3. Run:
   ```
   ./scripts/install.sh <box-ip>            # uses conf/tx92-vfd.conf by default
   ```
   (installer currently hardcodes `conf/tx92-vfd.conf`; pass your own file by
   replacing its contents during experiments, or just edit it.)
4. Verify geometry with the display test:
   ```
   ./scripts/install.sh <box-ip> --test 10
   ```

## What each key controls

| Key | Meaning |
| --- | --- |
| `vfd_gpio_clk/dat/stb` | `bank,pin,res`: bank 0 = main GPIO bank (`periphs-banks`), 1 = AO bank (`aobus-banks`) |
| `vfd_chars` | digit placement `D,H10,H,M10,M` → grid indexes |
| `vfd_dot_bits` | logical icon → hardware bit order |
| `vfd_display_type` | `layout,reserved,flags,controller` — layout picks the glyph tables (1 = T95/TX92-like → `tab1`, 7 = G9SX → `tab3`, 9 = TAP1 → `tab5`, other → `tab2`); controller `0` = FD628, `1` = FD620, `2` = TM1618 (SPI 3-wire). FD650 (I2C) is **not** supported yet |

## Finding the pins yourself (when a conf doesn't exist for your box)

On a **stock** firmware that still shows the clock, nothing easier:

```
adb shell
su
cat /sys/kernel/debug/pinctrl/*/pinmux-pins | grep -v UNCLAIMED
```
look for lines whose "store" column shows some LE display-related consumer
specific for your box, e.g. "periphs-banks:477". The pin in () is the bank number…
Actually the mapping is per-bank: `/sys/class/gpio/gpiochip*/label` gives
`periphs-banks`/`aobus-banks`; the conf pin index equals the debugfs `pin N`.

If you need to "discover" the pins of the display, the earlier linux_openvfd
effort has debug instructions: run the box's stock driver with verbose logging
and watch `dmesg`, or try the controller chips by trial with `--test`.

## If pins are held by a built-in driver

The shipped bootstrap (`scripts/sys_leds`) automatically unbinds whatever
driver claims the display pins (reads it from
`/sys/kernel/debug/pinctrl/*/pinmux-pins`), so conflicts like TX92's
`c11087c0.i2c` get resolved at every boot without manual work.

## Known limitations

- Only SPI-3-wire family controllers (FD628/FD620/TM1618). FD650-family
  (2-wire, I2C-like) is not implemented yet — patches welcome.
- Icon semantics depend on the layout group; groups are mapped in
  `src/vfdsvc.c` (`grp_icons`), tweak as needed.
- Timezone is auto-detected every hour via the box's own `date +%z`; set
  `tz_offset=+HHMM` in the conf to pin it manually.
