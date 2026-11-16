# HP TouchPad (APQ8060, "tenderloin" / "topaz"): hardware as used by this project

Everything below is either read from the stock webOS 3.0.5 (kernel 2.6.35-palm-tenderloin, over novacom, `scripts/collect-webos.sh`)
or taken from the device tree of this repository ([kernel/dts/](../kernel/dts/)), which is checked on the tablet. Where a value comes
from the HP / CodeAurora downstream sources only, it is said.

## SoC, memory, boot

- Board `TENDERLOIN`, `boardtype=topaz-Wifi-pvt` (Wi-Fi model). Qualcomm Scorpion, 2 cores (CPU part 0x02d), ARMv7, NEON, VFPv3.
- RAM: `0x40200000-0x466fffff` (101 MiB) and `0x48000000-0x7f5fffff` (886 MiB), as in the device tree.
- Bootloader framebuffer `0x7f600000` (0x900000 bytes): 1024x768 visible, 3 pages (virtual 1024x2304), stride 4096, 32 bpp. webOS `klog`
  buffer `0x7ff00000` (1 MiB): reused as `ramoops` by the device tree.
- Reserved for Linux: `vram@7a000000` (32 MiB, reusable CMA pool for the msm DRM scanout buffers), SMI `0x38000000` (64 MiB) for the video
  codec: 3 MiB firmware + 61 MiB buffer pool (`0x38300000`).
- Restart: PS_HOLD at `0x00800820` (qcom,pshold). Watchdog on the 32.768 kHz sleep clock.
- Kernel command line in the DTB: `console=tty0 earlycon=simple loglevel=6 rdinit=/init clk_ignore_unused regulator_ignore_unused`
  and `ath6kl_core.btcoex_dev=2 ath6kl_core.btcoex_ant=2`.

## Storage

- eMMC `SEM16G`, 14.8 GiB, `mmcblk0` on SDCC1 (`0x12400000`), 8-bit. Supplies PM8901 L5 (2.85 V) and LVS0.
- webOS: `root=/dev/mmcblk0p13` (32 MiB, ext3, boot partition), p1 100 MiB, p14 about 14.2 GiB (user data, `media` volume). Linux lives as
  a file on the media volume: `touchpad/rootfs-alpine.ext4`. Layout and install steps: [FLASHING.md](FLASHING.md).
- Aliases: `mmc0` = SDCC1 (eMMC), `mmc1` = SDCC4 (Wi-Fi), fixed so that `/dev/mmcblk0` is always the eMMC.

## Devices

| Function | Part and wiring |
|---|---|
| PMICs | PM8058 rev E3 (interrupt on TLMM GPIO 88; power key, RTC, LPG PWM, GPIOs), PM8901 rev F4; regulators through the RPM |
| Display | LG XGA panel 1024x768 (197 x 148 mm), parallel RGB666 from MDP4 (rev 4.1, `0x05100000`, LCDC) into an LVDS serializer. 96 MHz pixel clock, 60 Hz, total 2024 x 791 (hfp 272, hbp 400, hsync 328, vfp 10, vbp 6, vsync 7, both syncs active low). Power sequence: PM8058 L10 (3.05 V), LCD_PWR_EN (GPIO 62), 500 us, LVDS_SHDN_N (GPIO 63), 200 ms, backlight |
| Backlight | TPS61187 LED driver: PWM of PM8058 LPG channel 0 (20 kHz) + enable on PM8058 GPIO 25; the 5 V boost (GPIO 102) is shared with the touch controller and the audio codec |
| GPU | Adreno 220 (a2xx), 3D core `0x04300000`, IRQ SPI 80, 512 KiB GMEM, 266.667 MHz (PLL2 / 3), firmware `qcom/leia_pm4_470.fw` and `leia_pfp_470.fw`. Clocks GFX3D, AHB, AXI plus RPM fabrics and EBI1 |
| Touch panel | Cypress CY8CTMA395: raw capacitance frames on the GSBI10 UART (RX only, GPIO 71, 4 Mbaud), configuration on GSBI10 I2C at `0x67` (GPIO 72 SDA / 73 SCL, the same pads are the SWD port used to flash it), XRES GPIO 70 (high = reset), WAKE GPIO 123, supplied by PM8058 L10 and the 5 V boost. 40 x 30 grid, 4.9 mm pitch; driven by the userspace `ts_srv` |
| Wi-Fi | Atheros AR6003 (hw 2.1.1) on SDIO, SDCC4 (`0x121c0000`), 4-bit. Supplies PM8901 L1 (3.3 V) and L3 (PA, 3.3 V), PM8058 L19 (1.8 V) and S3 (SDIO I/O 1.8 V). CHIP_PWD / reset GPIO 135, WL_HOST_WAKE GPIO 93 (input, pull-down), HOST_WAKE_WL GPIO 137 (must be driven high). The SDCC needs a dummy CMD52 after a CMD53 write (`qcom,dummy52-required`) |
| Bluetooth | CSR BlueCore 6 (classic only) on the GSBI6 UART with flow control (GPIO 53-56), BCSP protocol, attached from userspace. BT_POWER GPIO 130 (high = on), BT_WAKE 131, BT_RST_N 138 (active low), BT_HOST_WAKE 129 |
| Audio codec | Wolfson WM8958 rev B, I2C `0x1a` on GSBI7 (GPIO 59/60, 100 kHz), no codec IRQ. Internal LDOs enabled by GPIO 66 and 108, speaker amp on WM8958 GPIO1, headset mic detect on WM8958 GPIO6 to TLMM 57, jack detect TLMM GPIO 67 (high = inserted). Supplies PM8058 S3 and the 5 V boost |
| Audio interface | LPASS LPAIF `0x28100000` (IRQ SPI 85) driven from the CPU without the Q6 DSP; LCC clock controller `0x28000000`, PLL4 540.672 MHz; OSR 12.288 MHz (256 fs at 48 kHz). Codec speaker I2S is the clock master; the WM8958 FLL locks on the bit clock |
| Charger | Maxim MAX8903B driven by GPIOs: CEN 41 (high = charge enabled on this board), DOK_N 140, CHG_N 36, FLT_N 35, DCM 42, IUSB 133, USUS 33, ISET1 34, ISET2 30. DC input limit: ISET 0/0 = 750, 0/1 = 900, 1/1 = 1400, 1/0 = 2000 mA; USB mode (DCM high) IUSB high = 500 mA, low = 100 mA. The bootloader leaves USUS high (suspended). The cable VBUS is on the DC input, even from a PC port |
| Battery gauge | "A6" microcontroller `0x31` (a6_0) on GSBI8 I2C (GPIO 64/65, 100 kHz). It sleeps most of the time: GPIO 155 high wakes it. A second A6 at `0x32` does not answer |
| Home LEDs | TI LM8502 at `0x33` on the same GSBI8 bus, enable GPIO 121, interrupt GPIO 128; two white LEDs on both sides of the Home button (outputs D1, D2). The vibrator and torch outputs of the chip are not described |
| Buttons | Volume up GPIO 103, volume down GPIO 104, Home ("core navi") GPIO 40, all active low with pull-up; power key on the PM8058. (3G models have the volume keys on PM8058 GPIOs 6/7) |
| Front camera | Aptina MT9M113 (1.3 MP, own ISP) on GSBI4 I2C `0x3c` (GPIO 47/48); MCLK 24 MHz on GPIO 32; reset GPIO 106 (active low), power-down GPIO 107 (high = powered down); supplies PM8058 LVS0 (1.8 V) and L11 (2.85 V); data on one MIPI CSI-2 lane into CSI1 (`0x04900000`, IRQ SPI 83) and the VFE 3.1 (`0x04500000`, IRQ SPI 48). No rear camera |
| Video codec | VIDC 1080p core at `0x04400000` (IRQ SPI 49), firmware `vidc_1080p.fw` from webOS in the SMI |
| USB | ChipIdea controller `0x12500000` (IRQ SPI 100), ULPI PHY, peripheral mode (the serial gadget works, the Ethernet one does not); PHY supplies PM8058 S3 and L6 |
| Serial | `ttyMSM0` GSBI12 UART (`0x19c40000`, serial0, debug), GSBI6 UART (Bluetooth), GSBI10 UART (touch, serial2) |
| Thermal | TSENS, one sensor, calibration in the QFPROM (`0xbe` primary, `0xbf` backup; 30 C code, 0.702 C per code, about 109 C at code 0xDF). Its interrupt is not used (polled): the handler of this old TSENS version calls a mutex in interrupt context |

## CPU and power rails

- One SCPLL per core selected by the ACC block (`0x02041000`, `0x02051000`), L2 source select in the apps GCC (`0x02082000`). The bootloader
  calibrates L = 8 to 28 (432 to 1512 MHz).
- Core voltage through the SAW of each core (`0x02042000`, `0x02052000`), 800 to 1187.5 mV. vdd_mem (PM8058 S0) is held at 1.2 V, vdd_dig
  (PM8058 S1) is 1.1 V (1.2 V at L2 1188 MHz), both at 1.6 MHz switching.
- Operating points (speed bin of the tablets used, from the HP `acpu_freq_tbl_1188mhz`): 432 MHz 887.5 mV, 486 912.5, 540 925, 594 937.5,
  648 950, 702 975, 756 1000; boost points 810 1012.5, 864 1037.5, 918 1062.5, 972 1087.5, 1026 1125, 1080 1137.5, 1134 1162.5,
  1188 1187.5 mV (off at boot, switched on by the settings panel).
- Thermal zone: passive trip at 75 C (cpufreq cooling), hot at 90 C, critical at 105 C, polled every 250 ms / 1 s.
- Main supply VPH is the battery, about 3.7 V.

## Other facts

- Directory listings through novacom do not work on webOS (`ls` unsupported): files are read one by one with `novacom get`.
- The first tablet has dead electrodes at the edges of the touch panel (hardware); the area it still reports was measured there by
  painting the whole panel (see `rootfs/ts_srv/ts_srv.c`). The second unit (32 GB) is the development tablet.
- The raw webOS dumps (`/proc`, `/sys` listings, register dumps for the GPU, camera, video codec and audio) are kept locally in `data/`
  (not versioned); `scripts/collect-webos.sh` regenerates them.
