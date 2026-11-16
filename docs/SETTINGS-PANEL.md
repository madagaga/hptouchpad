# Settings panel (plan, 2026-10-29)

The tablet is a display: Cog shows one URL full screen. A settings panel, opened by pressing Home three times, configures Wi-Fi, Bluetooth,
sound, camera, the Cog URL, the brightness and the screen-off delay. Steps 0-2 are done (see "Steps"); the page still shows mock data.

## Decision
- **No second graphics stack.** Cog owns DRM/KMS (`--platform=drm`); a native UI (Flutter, Slint, Phosh) would have to take DRM master from
  it or move everything under a Wayland compositor. Rejected:
  - Phosh (postmarketOS): full GNOME stack (phoc, GTK4, NetworkManager, PipeWire), too heavy for 1 GB / 756 MHz / Adreno 220 (GLES2), no kiosk
    or "browser URL" setting.
  - Flutter (flutter-pi / elinux): engine built for glibc, not musl; GLES2 on armv7 fragile; still needs DRM sharing.
  - Rust + Slint: light, but still DRM sharing and a second UI toolkit to maintain.
- **The panel is a local web page shown by Cog itself**, backed by Alpine's existing tools. Our own code is one small C daemon for the inputs.
  This follows the project rule: prebuilt Alpine packages, rebuild only the minimum.

## Components

### 1. Input side: `tp --daemon` (`rootfs/tp/input.c`; was `tpd`, removed after the move to the single `tp` binary)
Only what the tools cannot do:
- **Triple Home**: `KEY_HOMEPAGE` on the device that reports it (`gpio-keys`). Three presses or more, each within 600 ms of the previous one,
  open or close the panel (`cogctl open` of the panel, or of the saved URL; `/run/tp.panel` marks it as shown). One press turns the screen
  off; while it is off only Home or Power turns it on (user choice). Not done yet: Home LEDs (LM8502) as feedback.
- **Power**: `KEY_POWER` of the PM8058 power key: one press turns the screen on when it is off, off otherwise (no panel gesture).
- **Sound**: `tp sound get|volume <0-100>|mute on|off|test` (rootfs/tp/sound.c, alsa-utils amixer/aplay): the daemon sets the WM8958 playback path at
  start; volume on "DAC1 Volume" (-30..-12 dB, 0 % mutes), mute on "DAC1 Switch", saved as sound_volume / sound_mute in tpd.conf; the
  test plays /usr/share/sounds/tp-test.wav.
- **Inactivity**: every evdev device counts (the devices are rescanned every 5 s, so `ts_srv`'s uinput device is picked up whenever it
  appears). After `screen_off` seconds: brightness 0 (the level is saved and restored). While the screen is off the touch screen is grabbed
  (`EVIOCGRAB`), its events are dropped and its controller is powered off through the ts_srv control socket.
- `screen_off` is read from `tpd.conf` at start, every 5 s and after `tp screen set`. Log: `/tmp/tp.log` (also in the kernel log).
- The power policy of the old `tp-powerd` runs in the same daemon (`rootfs/tp/power.c`); the initramfs keeps the shell `tp-powerd`.

### 2. Web panel: `busybox-extras` `httpd` + shell CGI
- `httpd -p 127.0.0.1:8080 -h /usr/share/tpd/www`, static HTML/JS + one CGI. Bound to localhost only.
- **The backend is the `tp` command**: in Alpine the C binary `tp` (`rootfs/tp`), which runs the commands it has not ported yet through the
  shell tp (`initramfs/tp`, installed as `tp-sh`, whose `-j` goes to `tp-panel`); also driven from the PC by `scripts/tp.sh`. The panel, SSH
  and the PC use the same code.
- **The page builds the command, as an argument list, never as text.** It POSTs a JSON array such as `["wifi","add","MySSID","secret"]`.
  The only CGI, `cgi-bin/tp`, is the `tp` binary itself (a link; CGI mode detected by `GATEWAY_INTERFACE`, `rootfs/tp/cgi.c`): it checks
  the first word against the list of `tp` subcommands, then answers a native command through the daemon and `execv`s `tp-sh -j <args...>`
  for the others, with each element as its own argv entry. No shell ever sees request data, so an
  SSID like `a;reboot` stays a string. New buttons need no CGI change, only a `tp` subcommand; `tp` validates its own arguments (needed for
  SSH use anyway).
- Localhost is not a full boundary here: the page Cog normally shows is an external site running in the same browser, and it can send
  requests to `127.0.0.1:8080` (form POST, DNS rebinding). The router therefore also rejects requests whose `Host` is not `127.0.0.1:8080`
  and changes state only on POST carrying a custom header (`X-TP: 1`), which a foreign page cannot send without a CORS preflight that
  `httpd` never accepts.
- `tp` evolves:
  - `-j`: JSON output for the subcommands the panel reads (`info`, `battery`, `backlight status`, `wifi status/scan/list`, `bt status/scan`,
    `url`, `screen`); without `-j` the human output stays as today.
  - New subcommands: `url [get|set <url>]` (writes `/mnt/media/touchpad/url`, then `cogctl open`), `screen [get|set <seconds>]`
    (writes `tpd.conf`, the daemon re-reads it), `wifi list|add|remove|select` (persistent networks through `wpa_cli` + `save_config`; the current
    `wifi wpa` keeps the passphrase in `/tmp` only), `bt pair|trust|remove` (through `bluetoothctl` in Alpine), `panel open|close`
    (`cogctl open` of the panel or of the saved URL; also used by the daemon for the triple Home).
  - Subcommands that need tools absent from the initramfs (`wpa_cli`, `bluetoothctl`, `cogctl`) say so and fail cleanly there.
- What `tp` calls:

| Setting | Tool / interface |
|---|---|
| Wi-Fi | `wpa_cli` (`scan`, `scan_results`, `add_network`, `set_network`, `enable_network`, `save_config`), then `udhcpc` |
| Bluetooth | `bluetoothctl` (packages `bluez`, `bluez-deprecated`, our `bccmd`); classic only, the CSR BlueCore 6 has no BLE |
| Brightness | `/sys/class/backlight/backlight/brightness` (0-100) |
| Cog URL | config file, then `cogctl open <url>` |
| Screen-off delay | `tpd.conf` (`screen_off`), re-read by the `tp` daemon |
| Date and time | `tp time [get\|set <YYYY-MM-DD HH:MM>]` (`date -s`, then `hwclock -w` when an RTC exists); `tp tz <Area/City>` (`tzdata`, `/etc/localtime` link + `/mnt/media/touchpad/tz`); `tp ntp [on\|off\|server <host>\|status]` |
| Sound | `tp sound get\|volume <0-100>\|mute on\|off\|test` (`amixer`, `aplay`; `rootfs/tp/sound.c`) |
| Microphone | `tp sound mic-volume <0-100>\|mic-mute on\|off\|mic-test\|mic-level` (same file; see "Microphone and camera") |
| Camera | `tp camera get\|snap\|rotate` (`rootfs/tp/camera.c`, direct V4L2 `read()`, no extra package) |

### 3. Configuration
Persistent files stay on the webOS media volume, like today (`/mnt/media/touchpad/`):
- `url` (existing, read by `S90browser`),
- `wpa_supplicant.conf` (existing; must contain `ctrl_interface=/var/run/wpa_supplicant` and `update_config=1` for `save_config`),
- `tpd.conf` (new): `brightness=60`, `screen_off=300` (seconds, 0 = never), `ntp=1`, `ntp_server=pool.ntp.org`,
- `tz` (new): time zone name, e.g. `Europe/Paris`.

### 3b. Time and NTP
- NTP the Alpine way without an extra package: BusyBox `ntpd` (what `setup-ntp` offers as "busybox"), started by `/etc/tp.d/S15time`
  after Wi-Fi: `ntpd -p <server>` when `ntp=1`. Chrony/openntpd are not worth it for a display.
- Time zone: package `tzdata`; `tp tz` links `/etc/localtime` and stores the name on the media volume (the rootfs image is rebuilt).
- **No RTC yet**: `kernel/touchpad.config` has no `CONFIG_RTC_*`, so the clock starts at 1970 at every boot until NTP answers (TLS pages fail
  on a wrong date). Kernel item: `CONFIG_RTC_DRV_PM8XXX` + the PM8058 RTC node (`qcom,pm8058-rtc`) in our DTS; check that the RTC is writable
  (on some Qualcomm platforms it is owned by the secure side; then keep an offset file like Android). Planned, not written: restore the last
  saved time from `/mnt/media/touchpad/lasttime` at boot. Since d60c35e the qcom_defconfig kernel has `rtc-pm8xxx` (STATUS.md): to check.

### 4. Image changes (`scripts/alpine-rootfs.sh`, `docker/alpine-rootfs/Dockerfile`)
- Packages: `busybox-extras` (httpd), `dbus`, `bluez`, `bluez-deprecated`, `tzdata` (no libevdev: plain `linux/input.h`).
- `cogctl` uses the D-Bus **session** bus (checked on 2026-10-29: `cogctl help` lists `open`, `reload`, `ping`...; `dbus` is not installed by the
  `cog` package). `S90browser` starts `dbus-daemon --session --fork --address=unix:path=/run/dbus-session` and exports
  `DBUS_SESSION_BUS_ADDRESS` for Cog; the `tp` daemon and `tp-panel` use the same address. Fallback if this fails: restart Cog with the new URL.
- `/etc/tp.d/S95panel`: links the CGI and starts `httpd`; `/etc/tp.d/S03power` starts `tp --daemon`. `scripts/build-tp.sh` builds `tp` like
  `scripts/build-ts-srv.sh`.

## Open points to check on the tablet
1. ~~`cogctl open` against our Cog~~ **Works (2026-10-29, second unit)**: Cog registers `com.igalia.Cog` on the session bus of `S90browser`,
   `cogctl open http://127.0.0.1:8080/` returns 0 and the panel loads in about 2.8 s. `cogctl ping` exits 1 without a message even then: do not
   use it as a liveness test (check the bus name with `dbus-send ... ListNames` instead).
2. Whether the existing `wpa_supplicant.conf` already has `update_config=1`.
3. ~~The input daemon must open `ts_srv`'s uinput device after it appears~~ done by the periodic rescan.

## Text input and pickers (2026-10-29)
- **No keyboard today**: the tablet has none and Cog shows none, so the text fields of the panel (URL, Wi-Fi passphrase, hidden SSID, NTP
  server) cannot be filled on the device yet.
- **Bluetooth keyboard** (preferred direction, user 2026-10-29): classic HID fits the CSR BlueCore 6 (2.1 + EDR, no BLE). Needs the panel's
  Bluetooth pairing (step 3c), `bluetoothd` with its input plugin, and in the kernel `CONFIG_BT_HIDP` and/or `CONFIG_UHID` (absent from
  `kernel/touchpad.config` on 2026-10-29, which only has `CONFIG_BT=y`); libinput then gives the keys to Cog like any keyboard.
- **On-screen keyboard: maybe later.** If needed, a JavaScript keyboard inside the panel page (shown when a text field gets the focus,
  letters + digits/symbols for URLs, show/hide for passwords, layout to choose), not a system keyboard: squeekboard and the like need a
  Wayland compositor, i.e. moving Cog off the DRM platform.
- **`<select>` does not open in Cog**: WPE leaves the option popup to the embedding browser and Cog has none, so only the current value shows
  (seen on the time zone list: the API returns 490 zones, the page offered only UTC). Replaced by an in-page picker (`tzPick`: area, then
  city). The `type=date`/`type=time` fields of the manual time likely have the same problem (and need a keyboard): to rework if used.
- **Icons**: the tablet has only the DejaVu fonts, without emoji; the page uses inline SVG icons (an emoji shows as nothing).

## Next architecture: one `tp` binary (design, 2026-10-29, not started)
Before it the panel ran on five programs: `tp` (shell, initramfs + Alpine), `tp-panel` (shell, the JSON side of `tp -j`), `tp-powerd` (shell
loop), `tpd` (C) and `tp-cgi` (C), plus the shell helpers `charge-mode`, `cpu-state`, `bt-power`, `wifi-*` (about 1100 lines in all).
The state is spread over them and the failure modes too: on 2026-10-29 `tp-powerd` silently skipped its loop (low-battery shutdown
included) because the gauge could not be read, and `tp-panel` turned the same failure into "0 %". Decision of the user: **one C binary**,
with a daemon and IPC between client and daemon; **external tools stay called as programs** (`wpa_cli`, `wpa_passphrase`, `iw`, `udhcpc`,
`bluetoothctl`, `hciattach`, `bccmd`, `hciconfig`, `cogctl`, `ntpd`, `date`, `udevadm`), without a shell.

### Modes of the binary (`rootfs/tp/`, static musl armv7 like `ts_srv`, works in the initramfs and in Alpine)
- `tp --daemon`: the only resident process (replaces `tpd` and `tp-powerd`). Event loop (`poll`) on the input devices, the IPC socket,
  a 5 s power timer and the screen timer. Owns the state: settings (single writer of `tpd.conf`), screen on/off and saved level, panel
  shown, charge source and input current step, charge window hold, last good battery reading, Bluetooth/Wi-Fi state.
- `tp <command...>`: client. Sends its argument list to the daemon and prints the answer: human text by default, JSON with `-j`.
  **No daemon** (initramfs, or daemon not started): the client runs the command itself with the same code, for the commands that need no
  daemon state (info, battery, temp, cpu, backlight, charge, wifi status/scan, bt power, recovery...). Commands that need the daemon
  (panel, screen, charge window) say so.
- `tp --cgi` (installed as the link `cgi-bin/tp`): the HTTP router of the former `tp-cgi` (POST, Host `127.0.0.1:8080`, `X-TP: 1`, JSON
  array of strings, allowed first word), forwarding to the daemon socket instead of exec'ing a shell script.

### IPC
- Unix stream socket `/run/tp.sock`, mode 0600 (root only; httpd runs as root today). One request per connection.
- Request: the argument list as NUL-terminated strings, then an empty string; at most 16 arguments, 4 KiB in all (same limits as the CGI).
- Answer: always JSON (`{"ok":true,...}`, `{"ok":false,"error":"..."}` or an array for lists), then the daemon closes the connection.
  The client renders it as text without `-j` (a generic `key: value` printer, with per-command formats only where it helps).
- Slow commands (Bluetooth power ~14 s, Bluetooth scan 10 s, Wi-Fi scan, pairing, rotation with the Cog restart) must not block Home and
  the screen: the daemon forks a worker for them; the worker answers on the client connection and reports state changes back to the daemon
  (settings are re-read on SIGCHLD).

### Commands (union of today's `tp` and `tp-panel`)
`info`, `battery get|limit on|off|range`, `charge usb|750|900|1400|2000|status`, `power`, `temp`, `cpu get|boost on|off`, `backlight get|set|on|off`,
`screen get|set|on|off`, `rotation get|set`, `url get|set|open`, `browser reload`, `panel open|close|toggle`, `time set`, `tz list|<zone>`,
`ntp status|on|off|server`, `wifi status|list|scan|add|remove|select|power`, `bt status|power|visible|scan|pair|connect|remove`, `ssh`,
`reboot`, `recovery`. Each command validates its own arguments (as `tp-panel` does now).

### Daemon duties carried over
- From `tpd`: triple Home (panel toggle), one press = screen off/on, inactivity screen off, device rescan. Changed (user): only
  Home wakes the screen; while it is off the touch screen is grabbed and its controller powered off (ts_srv socket, 'C'/'O').
- From `tp-powerd`: source detection (UDC state), input current steps with fallback, low battery (backlight 5 %, CPU at minimum) and the
  clean power-off, charge window on CEN (GPIO 41).
- **Battery reads**: fixed in the kernel driver (wake the A6 with GPIO 155 around each transfer, DT `wake-gpios`, STATUS.md). The daemon
  keeps the last good reading with its age; a failed read is reported as unknown (`null` in JSON, "unavailable" on the page), is logged,
  and never stops the rest of the power loop. If the gauge stays unreadable on battery for long (no level at all), the daemon warns and
  powers off cleanly rather than letting the battery die (exact rule to decide).

### Progress (2026-10-29 evening, nothing sent to the tablet: another agent was testing on it)
- Step 1 written: `hp_a6_battery` raises `wake-gpios` (GPIO 155, new in the DT) for each property read, retrying the first transfer
  for up to ~31 ms while the A6 wakes, also at probe (a boot on battery would fail otherwise); constant properties need no wake.
  Compiled alone with `W=1` (no warning) and the DTB checked (`wake-gpios = <&tlmm 155 0>`), in a separate volume
  (`hptouchpad-build-a6`, output `out/kernel-a6`) so as not to touch `out/uImage` or the shared build volume. A full kernel build
  failed on the other agent's work in progress (`msm8660-saw-regulator.c`), not on this change. Not tried on the device.
- Step 2 written: `rootfs/tp` (C, `scripts/build-tp.sh` -> `out/tp/tp`, static musl armv7, no warning). Native commands:
  `battery`, `charge`, `panel`, `power`, `screen`; the others are exec'ed as `tp-sh` (the shell tp, installed under that name by
  `scripts/alpine-rootfs.sh`). The daemon (`S03power` starts it) replaces `tpd` and `tp-powerd`; `S95panel` links `cgi-bin/tp` to
  the binary itself (CGI mode detected by `GATEWAY_INTERFACE`). Simplification of the design: the client and the CGI run non-native
  commands themselves through `tp-sh`, so the daemon only serves native commands (no worker processes needed yet). `tp-panel` lost its
  `battery`, `screen` and `panel` branches. Battery: `capacity`/`current_ma` are `null` when the gauge does not answer (with the last
  good reading and its age), the power loop never stops on it, and on battery with the gauge unreadable for 15 minutes the daemon
  powers off cleanly.
  Checked in an alpine armv7 container (QEMU, no tablet hardware): client with and without daemon, `-j` and text output, argument
  validation, delegation to `tp-sh`, the CGI checks (403/400/403/405), malformed IPC requests, a non-native command refused by the
  daemon, error when `/dev/mem` is not reachable. Not tried on the device.
- **A cable always charges (user request, same evening).** The old rule powered off at 5 % "while discharging", and the tablet
  discharges even on a PC port (it draws more than 500-900 mA): a boot on a nearly empty battery with a cable would power off at once,
  every time, and never charge. Now, with a cable: CEN is forced high every pass (unless the charge window holds it at its stop level),
  there is never a low-battery power-off, and at 5 % the screen is turned off (the backlight is about half of the load) so that even a
  PC port charges; power saving (backlight at most 20 %, CPU at minimum; it was 5 % until 2026-11-15, which looked like a dead
  screen) at 15 % whatever the source, lifted at 20 %. Without a cable: clean
  power-off at 5 % as before. Same rule in the initramfs `tp-powerd` (power-off only without a cable, backlight off on one, CEN high
  on a cable). Checked on a fake sysfs in an armv7 container (`PS`/`UDC` overridable at build time for that): boot at 3 % on a PC port
  discharging -> no power-off, screen off; cable pulled at 3 % -> power-off; wall charger with the gauge unreadable -> no power-off;
  10 % then 25 % -> saving on, then lifted. `rootfs/tpd` and `scripts/build-tpd.sh` were removed
  once the binary was checked on the tablet (same evening: commits 1c15a56, 306d990).

### Migration
1. Kernel: A6 wake GPIO in `hp_a6_battery` (needed whatever the userspace).
2. `tp` binary: daemon (input + power, with the battery handling above), socket, client, `--cgi`; replaces `tpd`, `tp-powerd`, `tp-cgi`.
   The shell `tp` stays meanwhile, renamed `tp-sh`; the daemon runs it for the commands not ported yet.
3. Port the commands group by group (display, battery, browser/panel, time, Wi-Fi, Bluetooth), each checked on the tablet.
4. Remove `tp-sh`, `tp-panel`, `tp-powerd` and the shell helpers; the initramfs gets the static `tp`. (`tpd` and `tp-cgi` already removed.)

## Steps
Status 2026-10-29 (second unit, installed live, image not rebuilt): 0, 1 and 2 done and checked by the user (panel shown by `cogctl`, triple
Home opens and closes it, screen sleep and wake). 3 in progress: router `rootfs/tpd/tp-cgi.c` (since replaced by the CGI mode of `tp`, see "Next architecture"),
`tp -j` -> `rootfs/overlay/usr/local/bin/tp-panel` (info, backlight, screen, url, browser, panel, time, tz, ntp, wifi status/list/scan,
bt status; Wi-Fi and Bluetooth changes answer "not implemented yet"), `S15time` (time zone + BusyBox ntpd at boot), page on the real API
(mock data: since the same evening only when the page is opened from a file). Checked on the tablet with wget: the read commands answer, and the router refuses a command outside the
list (403), a control character (400), a request without `X-TP` (403) and a GET (405); `url set "a;reboot"` is refused by `tp-panel`.
3b Wi-Fi done (2026-10-29): `S10wifi` always runs wpa_supplicant on `touchpad/wpa_supplicant.conf` (created with `update_config=1` if
missing) and `wpa_cli -a wifi-event`, which starts DHCP on every connection (a network added later gets an address too).
`tp -j wifi add <ssid> [passphrase]` writes the SSID in hex and the key as the PSK from `wpa_passphrase` (no user text quoted into wpa_cli,
no passphrase on disk), selects it, re-enables the others and saves; `remove`, `select`, `power on|off`. Checked on the second unit: its
network moved from `/tmp/wpa.conf` to the saved file (key moved on the tablet itself), reconnected with DHCP, SSH back; argument errors
refused. Not tried: adding a second real network from the screen (no keyboard yet). Next: 3c Bluetooth.
3c Bluetooth (2026-10-29, second unit): `bt-power on|off` (Alpine version of the initramfs `bt-test` + `bt-psr`: GPIOs, `hciattach` BCSP,
PSR + BD address from the `BToADDR` token through `bccmd`, re-attach; about 12-14 s), `S20bluetooth` (system D-Bus, `bluetoothd`, controller
on at boot when `bt=1`), `/etc/bluetooth/main.conf` (name "HP TouchPad", laptop class, BR/EDR only). `tp -j bt status|power|visible|scan|pair|
connect|remove` through `bluetoothctl`. Checked through the API: power off/on (address `00:1D:FE:78:61:F0` from the token), visibility, a
scan that finds a nearby device, address validation. Limits: pairing works only for "Just Works" devices (no agent shows or enters a passkey,
and an incoming pairing from a phone cannot be accepted); a Bluetooth keyboard also needs `CONFIG_BT_HIDP` or `CONFIG_UHID` in the kernel.
Not tried: an actual pairing.
The `BToADDR` token of this unit has printable bytes glued in front of its name (`S-LmBToADDR`): `bt-power` and the initramfs `bt-psr` now
match `BToADDR$` (with `^BToADDR$` the chip kept its default address `00:02:5B:00:A5:A5`).

Battery page (2026-10-29): charge window. `tp -j battery get|limit on|off|range <start> <stop>` stores `charge_limit`, `charge_min`,
`charge_max` in `tpd.conf` (off by default, 20/80, stop >= start + 10). `tp-powerd` (every 5 s) applies it with hysteresis through the
charger's CEN pin (GPIO 41, TLMM register written directly like `charge-mode`): low once the battery reaches the stop level, high again when
it falls to the start level; it only touches CEN while the limit is on (and releases it once when it is turned off), otherwise the max8903
driver keeps control. The page shows level, status, source, current, and a "Charge paused (limit)" badge. Checked on the second unit (on a
PC port, discharging at about -340 mA even plugged in): window 20-50 % at 61 % -> CEN 0 and the logged stop, limit off -> CEN 1 and the
logged resume; with CEN 0 the battery current did not change (about -340 mA over 4 readings, against about 1 A of total load), so the input
keeps powering the tablet. Not seen yet: the charge actually stopping and resuming on a wall charger.

Screen rotation (Display page, 2026-10-29): `tp -j rotation get|set <0-3>` stores `rotation=` in `tpd.conf`. `S90browser` passes it to Cog
(`--platform-params=renderer=gles,rotation=N`, quarter turns counter-clockwise; the modeset/software path has no rotation). Cog does not
rotate touch coordinates (it only scales them to the rotated size), so `tp-panel` writes `/etc/udev/rules.d/91-tp-rotation.rules` with the
matching `LIBINPUT_CALIBRATION_MATRIX` (1: `0 -1 1 1 0 0`, 2: `-1 0 1 0 -1 1`, 3: `0 1 0 -1 0 1`), re-triggers udev, restarts Cog in the
background and reopens the panel; `S02udev` writes the rule at boot. The page uses `width=device-width` and fits 768x1024. Checked by the
user on the second unit: rotation 1 (portrait) shows the right way and touches land in place; rotations 2 and 3 tried by the user too ("seems to work").

0. UI template: `rootfs/overlay/usr/share/tpd/www/index.html`, static, 1024x768 touch, mock data when no backend answers (opened in a
   desktop browser for review). Every control already maps to the `tp` argument list it will send.
1. Image: packages + D-Bus session + `S95panel` with `httpd` and a static page; check `cogctl open` by hand.
2. `tpd`: triple Home, inactivity, grab, `SIGHUP`.
3. `tp -j` and the new subcommands, then the `cgi-bin/tp` router and the page: brightness, URL, delay; then Wi-Fi; then Bluetooth.
4. Sound and camera sections when their drivers exist (sound 2026-11-08, microphone and camera 2026-11-15).

## Microphone and camera (2026-11-15)
- **Sound page, group "Microphone"**: level slider, mute switch, test button, level bar.
  - `tp sound mic-volume <0-100>` maps the percentage on the WM8958 `IN1L Volume` (0-31, 1.5 dB steps) and saves `mic_volume`
    in `tpd.conf`; the default 87 % is step 27, the webOS gain. `tp sound mic-mute on|off` drives `DAC2 Switch`, which gates
    the AIF2 capture stream, and saves `mic_mute`. Both are applied by the daemon at start after the capture path.
  - `tp sound mic-test` starts a detached child: `arecord` 3 s from `hw:0,1` to `/tmp/tp-mic.wav`, peak and RMS of the left
    channel (DC offset removed) written to `/run/tp.mic`, then `aplay` of the recording on `hw:0,0`, then the file is removed.
    `tp sound mic-level` returns `state` (`none`, `recording`, `done`, `failed`), `peak_db`, `rms_db` (dB below full scale).
    The page asks for the level 0.8 s after the recording ends and maps -60..0 dB on the bar.
  - `tp sound get` now also returns `mic_volume` and `mic_mute`.
- **Camera page**: "Take a picture", "Live preview" switch (a picture every 1.2 s while the page is shown), "Rotate".
  - `tp camera snap` opens the VFE video device (found by its name `msm8660-vfe`), reads 4 frames with `read()` and keeps the
    last, converts NV12 to a 24-bit BMP and writes `/tmp/tp-cam.bmp`; `S95panel` links it as `www/cam.bmp`. About 1.1-1.2 s per
    picture, most of it the camera start and the dropped frames.
  - `tp camera rotate` adds a quarter turn clockwise to the pictures and saves `camera_turns` (0-3) in `tpd.conf`.
  - `camera` is a "local" native command (`cmd_local()` in `cmd.c`): it runs in the CGI or client process, never in the daemon,
    because a capture blocks for about a second.
  - The picture is not a video stream: the encoder is not used here. A snapshot started while another program streams from the
    camera waited for it in the one test done (2965 ms instead of 1150 ms).
- Tested on the tablet over SSH and through the CGI (2026-11-15); the on-screen check of both pages is the owner's.

## CPU boost (2026-11-05)
- Battery page, group "Performance": switch "CPU boost". `tp cpu get` returns `boost`, `max_mhz` and the current rate of each
  core; `tp cpu boost on|off` writes `/sys/devices/system/cpu/cpufreq/boost` and saves `cpu_boost=1|0` in `tpd.conf`. The
  daemon applies the saved value at start (off by default). `cpu` is allowed by the CGI.
- The OPPs above 756 MHz (810 .. 1188 MHz) are `turbo-mode` in the DT: schedutil only uses them with boost on. The low battery
  cap (CPU at its minimum at 15 %) is kept when boost is switched while it is active.
