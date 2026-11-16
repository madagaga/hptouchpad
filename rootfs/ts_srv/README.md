# ts_srv: userspace touchscreen driver (Cypress CY8CTMA395)

Port of the CyanogenMod userspace driver of the HP TouchPad (`touchscreen_drv` of
https://github.com/CyanogenMod/android_device_hp_tenderloin, branch cm-11.0, commit 9c64b22), GPL-2.0.

The controller is configured over I2C (0x67 on the GSBI10 QUP, `/dev/i2c-1`) and streams raw capacitance lines
(`ff 43 <row> + 40 bytes`, 30 rows, `ff 47` ends a scan) on the GSBI10 UARTDM at 4 Mbaud (`/dev/ttyMSM2`). ts_srv computes the
touch points and creates the `HPTouchpad` uinput touchscreen (multitouch protocol B, `INPUT_PROP_DIRECT`).

Changes from the original are listed at the top of `ts_srv.c` and `digitizer.c`, plus: the sensor columns used can be
restricted with `TS_SRV_COLUMNS` (all 40 by default; `7-33,36` on the first TouchPad, whose edge electrodes are dead, set through
`/mnt/media/touchpad/ts_columns` by `S80touch`), the coordinates are calibrated on the panel (STATUS.md, "Touch screen works"), and a held
touch survives short dips of the signal. `ts_srv_set` (finger/stylus mode client) is not ported: the control socket is
still served on `/run/tsdriver`.

Debug: `TS_SRV_DEBUG=1` (touch down/up with sensor position and peak, per-second rates on stderr), `TS_SRV_RAW=<file>`
(copy of the raw stream, analysed with `scripts/tools/ctp-cols.py`).

Build: `./build.sh ts-srv` (static armv7 binary in `out/ts_srv/ts_srv`). Started at boot by `/etc/tp.d/S80touch`.
