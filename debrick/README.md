# Forcing the charge of a deeply discharged TouchPad

With the cell under its protection threshold the A6 reports no battery, so the HP charger driver keeps the MAX8903B
charge enable **CEN (GPIO 41) low = charging off**, and the battery never charges. Forcing CEN high from a RAM-booted
toolbox breaks that loop. Nothing is written to the flash.

## Files
| File | What it is |
|---|---|
| `moboot-dfu-v004` | moboot for DFU loading, from [tpdebrick v004](https://github.com/Herrie82/tpdebrick), unchanged |
| `TPToolbox-Headless-v004` | the original toolbox from tpdebrick v004, unchanged (ECM gadget `192.168.7.7`, not usable from Windows/WSL) |
| `TPToolbox-serial` | `TPToolbox-Headless-v004` with a USB serial console (`g_serial`) instead of the ECM gadget |
| `boot-toolbox.sh` | DFU -> moboot -> `fastboot flash bootmem TPToolbox-serial` |

## Booting the toolbox
Two ways, depending on the mode the tablet can reach. Both load the toolbox in RAM only.

**DFU mode** (`0830:8070`, Home + Power + Volume Up; the only mode a flat tablet reached here). novacom does not
talk to this mode, so it goes through `dfu-util` and `fastboot`:
```sh
dfu-util -d 0830:8070 -R -D moboot-dfu-v004   # moboot comes up as fastboot 18d1:d00d
fastboot flash bootmem TPToolbox-serial
```
(`sh debrick/boot-toolbox.sh` runs the same two commands.)

**Recovery mode** (`0830:8071`, Volume Up + Power, big USB logo), if the tablet still gets there: novacom can load
the same image directly (not tried on the flat unit):
```sh
novacom boot mem:// < TPToolbox-serial
```

## Console
The toolbox does not run novacomd, so `novaterm` cannot reach it. It exposes a USB serial port instead
(`0525:a4a7`: `/dev/ttyACM0` on Linux, `USB Serial Device (COMx)` on Windows). Open it at 115200 with any serial
terminal (`picocom -b 115200 /dev/ttyACM0`, PuTTY) and log in as `root` (no password).

## Forcing the charge
1. Read the state: `cat /sys/class/power_supply/battery/uevent` (`PRESENT=0` on a flat unit) and
   `grep -E "gpio-(36|41|42|140) " /sys/kernel/debug/gpio` (CEN 41 low, DCM 42 low = DC mode, DOK_N 140 low = input valid).
2. Force the charge enable, keep the DC mode (DCM low, the driver default):
   ```sh
   echo 41 > /sys/class/gpio/export 2>/dev/null
   echo 1 > /sys/class/gpio/gpio41/value
   ```
   A few seconds later the battery shows up: `PRESENT=1` and its voltage (2.36 V here).
3. Leave it plugged in (one night was enough here). Do **not** switch to the USB mode (DCM high, 500 mA): the tablet browns out.
