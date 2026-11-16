# Settings panel and the `tp` program

The tablet is a display: Cog shows one URL full screen. A settings panel, opened by pressing Home three times, configures Wi-Fi,
Bluetooth, sound, microphone, camera, brightness, screen rotation, the screen-off delay, the date and time, the charge window, the CPU
boost and the Cog URL and options.

The panel is a local web page shown by Cog itself, backed by one small C program, `tp`, and Alpine's existing tools. There is no second
graphics stack: Cog owns DRM/KMS and a native UI would have to share it.

## Components

### `tp` (`rootfs/tp/`)

One static musl armv7 binary with three modes; it runs in the initramfs and in Alpine.

- `tp --daemon` (`input.c`, `power.c`, `daemon.c`): the only resident process. It owns the input devices, the power policy, the settings
  and the IPC socket `/run/tp.sock` (mode 0600).
- `tp <command...>`: the client. It sends its argument list to the daemon and prints the answer as text, or JSON with `-j`. Without a
  daemon (initramfs) it runs the commands that need no daemon state by itself.
- `tp --cgi`, installed as the link `cgi-bin/tp`: the HTTP router for the panel (mode detected by `GATEWAY_INTERFACE`, `cgi.c`).

Native commands: `battery`, `camera`, `charge`, `cpu`, `panel`, `power`, `screen`, `sound`. The other commands (`info`, `wifi`, `bt`,
`url`, `time`, `tz`, `ntp`, `rotation`, `browser`, `ssh`, `reboot`, `recovery`...) are run through the shell implementation `tp-sh`
(`initramfs/tp`), whose `-j` output goes through `tp-panel`.

IPC: a request is the argument list as NUL-terminated strings ended by an empty string (at most 16 arguments, 4 KiB); the answer is
always JSON (`{"ok":true,...}`, `{"ok":false,"error":"..."}` or an array). `camera` is a local command: it runs in the client or CGI
process, never in the daemon, because a capture blocks for about a second.

### Daemon duties

- **Buttons**: three presses of Home or more, each within 600 ms of the previous one, open or close the panel (`cogctl open` of the panel
  or of the saved URL; `/run/tp.panel` marks it as shown). One press of Home turns the screen off or on, as does the power key (PM8058).
  While the screen is off, only Home or Power turns it on.
- **Inactivity**: every evdev device counts (rescanned every 5 s, so the uinput device of `ts_srv` is picked up when it appears). After
  `screen_off` seconds the brightness goes to 0 (the level is saved and restored), the touch screen is grabbed (`EVIOCGRAB`) and its
  controller is powered off through the `ts_srv` control socket.
- **Power policy** (every 5 s): charger source detection, input current steps with fallback, charge window, power saving, clean power-off.
  - A cable always charges: the charge enable (CEN, GPIO 41) is forced high every pass, unless the charge window holds it low; there is no
    low-battery power-off with a cable, and at 5 % the screen is turned off so that even a PC port charges.
  - Without a cable: clean power-off at 5 %.
  - Power saving at 15 % whatever the source (backlight at most 20 %, CPU at its minimum), lifted at 20 %.
  - A failed gauge read is reported as unknown (`null` in JSON, "unavailable" on the page), logged, and never stops the loop; the last good
    reading is kept with its age. On battery with the gauge unreadable for 15 minutes, the daemon powers off cleanly.
- **Charge window**: `tp battery limit on|off`, `range <start> <stop>` (default 20/80, stop at least start + 10): CEN is set low at the stop
  level and high again at the start level, only while the limit is on.
- **CPU boost**: `tp cpu boost on|off` writes `/sys/devices/system/cpu/cpufreq/boost` (the operating points from 810 MHz up are
  `turbo-mode`); the saved value is applied at start, off by default; the minimum-CPU cap of power saving is kept.
- Log: `/tmp/tp.log` (also in the kernel log).

### Web panel

- `httpd -p 127.0.0.1:8080 -h /usr/share/tpd/www` (BusyBox `httpd`): the static page `index.html` and the CGI. Bound to localhost.
- **The page builds the command as an argument list, never as text**: it POSTs a JSON array such as `["wifi","add","MySSID","secret"]`.
  The CGI checks the first word against the list of `tp` subcommands, then answers a native command through the daemon, or `execv`s
  `tp-sh -j <args...>` with each element as its own argv entry. No shell sees request data, so an SSID like `a;reboot` stays a string.
  New buttons need only a `tp` subcommand; every subcommand validates its own arguments.
- Localhost is not a full boundary: the page Cog normally shows is an external site in the same browser. The router therefore rejects
  requests whose `Host` is not `127.0.0.1:8080` and changes state only on POST with the custom header `X-TP: 1`, which a foreign page
  cannot send without a CORS preflight that `httpd` never accepts. It answers 403 (host or header), 400 (control character), 405 (GET).
- The page works without a backend with mock data when opened from a file in a desktop browser (for review).
- Cog limits: there is no keyboard (the text fields cannot be filled on the device without a Bluetooth keyboard or a remote client);
  `<select>` popups do not open (WPE leaves them to the embedding browser), so the time zone uses an in-page picker; the tablet has only
  the DejaVu fonts without emoji, so the page uses inline SVG icons. The page fits 768x1024 (`width=device-width`).

## What each setting uses

| Setting | Tool or interface |
|---|---|
| Wi-Fi | `wpa_cli` (`scan`, `scan_results`, `add_network`, `set_network`, `enable_network`, `save_config`), then `udhcpc` |
| Bluetooth | `bluetoothctl` (packages `bluez`, `bluez-deprecated`, our `bccmd`); classic only, the CSR BlueCore 6 has no BLE |
| Brightness | `/sys/class/backlight/backlight/brightness` (0-100) |
| Cog URL | `touchpad/url`, then `cogctl open <url>` |
| Screen-off delay | `tpd.conf` (`screen_off`), re-read by the daemon at start, every 5 s and after `tp screen set` |
| Rotation | `tp rotation get\|set <0-3>`, see below |
| Date and time | `tp time [get\|set <YYYY-MM-DD HH:MM>]` (`date -s`, then `hwclock -w` when an RTC exists); `tp tz <Area/City>` (`tzdata`, link `/etc/localtime`, name in `touchpad/tz`); `tp ntp [on\|off\|server <host>\|status]` |
| Sound | `tp sound get\|volume <0-100>\|mute on\|off\|test` (`amixer`, `aplay`, `rootfs/tp/sound.c`) |
| Microphone | `tp sound mic-volume <0-100>\|mic-mute on\|off\|mic-test\|mic-level` |
| Camera | `tp camera get\|snap\|rotate` (`rootfs/tp/camera.c`, direct V4L2 `read()`) |
| CPU | `tp cpu get\|boost on\|off` |
| Charge | `tp charge usb\|750\|900\|1400\|2000\|status`, `tp battery get\|limit on\|off\|range` |

Subcommands that need tools absent from the initramfs (`wpa_cli`, `bluetoothctl`, `cogctl`) say so and fail cleanly there.

## Configuration

Persistent files are on the webOS media volume (`/mnt/media/touchpad/`):

- `url`: the page Cog shows (read by `S90browser`);
- `wpa_supplicant.conf`: saved Wi-Fi networks (must contain `ctrl_interface=/var/run/wpa_supplicant` and `update_config=1`; created if missing);
- `tpd.conf`: written only by the daemon. Keys: `brightness`, `screen_off` (seconds, 0 = never), `ntp`, `ntp_server`, `rotation`,
  `charge_limit`, `charge_min`, `charge_max`, `cpu_boost`, `sound_volume`, `sound_mute`, `mic_volume`, `mic_mute`, `camera_turns`;
- `tz`: time zone name, for example `Europe/Paris`.

## Start-up scripts (`rootfs/overlay/etc/tp.d/`)

| Script | Role |
|---|---|
| `S01gadget`, `S02udev` | USB serial gadget; udev (also writes the touch rotation rule) |
| `S03power` | starts `tp --daemon` |
| `S05ssh` | SSH |
| `S10wifi` | `wpa_supplicant` on `touchpad/wpa_supplicant.conf` and `wpa_cli -a wifi-event`, which starts DHCP on every connection |
| `S15time` | time zone and BusyBox `ntpd` (`ntpd -p <server>` when `ntp=1`) after Wi-Fi |
| `S20bluetooth` | system D-Bus, `bluetoothd`, controller on at boot when `bt=1`; `/etc/bluetooth/main.conf` (name "HP TouchPad", BR/EDR only) |
| `S80touch` | `ts_srv` |
| `S90browser` | D-Bus session bus (`unix:path=/run/dbus-session`, exported as `DBUS_SESSION_BUS_ADDRESS` for Cog, the daemon and `tp-panel`), then Cog |
| `S95panel` | links the CGI and `cam.bmp`, starts `httpd` |

`cogctl` talks to Cog over the D-Bus **session** bus (`com.igalia.Cog`). `cogctl ping` exits 1 even when Cog runs: check the bus name
with `dbus-send ... ListNames` instead. Packages added to the image: `busybox-extras` (httpd), `dbus`, `bluez`, `bluez-deprecated`, `tzdata`.

## Bluetooth

`bt-power on|off` (`rootfs/overlay/usr/local/bin/bt-power`) sets the GPIOs, attaches the controller with `hciattach` (BCSP), loads the
PSR keys and the BD address from the webOS `BToADDR` token through `bccmd`, and attaches again (about 12-14 s). The token name is matched
as `BToADDR$`, because printable bytes can be glued in front of it. `tp bt status|power|visible|scan|pair|connect|remove` go through
`bluetoothctl`. Pairing works only for "Just Works" devices: no agent shows or enters a passkey, and an incoming pairing from a phone
cannot be accepted. A Bluetooth keyboard needs `CONFIG_BT_HIDP` or `CONFIG_UHID`, which `kernel/touchpad.config` does not set.

## Screen rotation

`tp rotation set <0-3>` stores `rotation=` in `tpd.conf`. `S90browser` passes it to Cog (`--platform-params=renderer=gles,rotation=N`,
quarter turns counter-clockwise; the software path has no rotation). Cog does not rotate touch coordinates, so `tp-panel` writes
`/etc/udev/rules.d/91-tp-rotation.rules` with the matching `LIBINPUT_CALIBRATION_MATRIX` (1: `0 -1 1 1 0 0`, 2: `-1 0 1 0 -1 1`,
3: `0 1 0 -1 0 1`), re-triggers udev, restarts Cog and reopens the panel; `S02udev` writes the rule at boot.

## Microphone and camera pages

- **Microphone**: level slider, mute switch, test button, level bar. `tp sound mic-volume` maps the percentage on the WM8958 `IN1L Volume`
  (0-31, 1.5 dB steps; the default 87 % is step 27, the webOS gain). `mic-mute` drives `DAC2 Switch`, which gates the AIF2 capture stream.
  Both are applied by the daemon at start, after it sets the capture path. `mic-test` starts a detached child: `arecord` 3 s from `hw:0,1`,
  peak and RMS of the left channel written to `/run/tp.mic`, then `aplay` of the recording on `hw:0,0`. `mic-level` returns `state`
  (`none`, `recording`, `done`, `failed`), `peak_db` and `rms_db`.
- **Camera**: "Take a picture", a "Live preview" switch (a picture every 1.2 s while the page is shown) and "Rotate". `tp camera snap`
  opens the VFE device (found by its name `msm8660-vfe`), reads 4 frames with `read()`, keeps the last, converts NV12 to a 24-bit BMP
  in `/tmp/tp-cam.bmp`, served as `www/cam.bmp` (about 1.1-1.2 s per picture). `tp camera rotate` adds a quarter turn clockwise and saves
  `camera_turns`. It is not a video stream; the encoder is not used. A snapshot taken while another program streams from the camera
  waits for it.

## Time

There is no verified RTC: `kernel/touchpad.config` sets no `CONFIG_RTC_*`, so the clock starts at 1970 until NTP answers, and TLS pages
fail on a wrong date.
