/*
 * ctp-test: power-up and raw-data test of the HP TouchPad touch panel (Cypress CY8CTMA395), the sequence of the
 * CyanogenMod userspace driver (touchscreen_drv/digitizer.c, GPL-2.0) with mainline interfaces:
 *   - 5 V supply: regulator-output consumer, /sys/devices/platform/touch-power/state ("enabled"/"disabled");
 *   - XRES (GPIO 70, active low: the 3.4 driver's xres "1" = assert = GPIO 0) and WAKE (GPIO 123, raw level, the
 *     user-pins "level" file writes the GPIO value as is): held through the GPIO character device for the whole run
 *     (a gpioset that exits releases the lines, which go back to inputs);
 *   - I2C at 0x67 on the GSBI10 bus: the 7 configuration writes of digitizer.c;
 *   - raw frames on the GSBI10 UART (/dev/ttyMSM2) at 4 Mbaud.
 * Usage: ctp-test [i2c bus, default 1] [seconds of UART capture, default 3] [mask of the 6 configuration writes, default 3f]
 *        -> /tmp/ctp.raw (the controller noise shows which sensor columns are scanned, no touch needed)
 * Build: gcc -O2 -static ctp-test.c -o ctp-test
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define GPIO_XRES 70
#define GPIO_WAKE 123

static int lines_fd;

/* xres: 1 = reset asserted (GPIO low), as the cy8ctma395 sysfs file; wake: raw GPIO level */
static void set_lines(int xres, int wake)
{
	struct gpiohandle_data d = { .values = { !xres, wake } };

	if (ioctl(lines_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d) < 0)
		perror("set lines");
}

static int i2c_write(int fd, const unsigned char *buf, int len)
{
	struct i2c_msg msg = { .addr = 0x67, .flags = 0, .len = len, .buf = (unsigned char *)buf };
	struct i2c_rdwr_ioctl_data data = { .msgs = &msg, .nmsgs = 1 };
	int rc = ioctl(fd, I2C_RDWR, &data);

	printf("i2c write %02x (%d bytes): %s\n", buf[0], len, rc == 1 ? "ok" : strerror(errno));
	return rc == 1 ? 0 : -1;
}

static void supply(const char *state)
{
	FILE *f = fopen("/sys/devices/platform/touch-power/state", "w");

	if (!f) { perror("touch-power"); return; }
	fputs(state, f);
	fclose(f);
}

int main(int argc, char **argv)
{
	int bus = argc > 1 ? atoi(argv[1]) : 1, secs = argc > 2 ? atoi(argv[2]) : 3, retry;
	unsigned mask = argc > 3 ? strtoul(argv[3], NULL, 16) : 0x3f;
	char path[32];
	static const unsigned char init[][6] = {
		{ 0x31, 0x01, 0x08, 0x0C, 0x0D, 0x0A }, { 0x30, 0x0F }, { 0x40, 0x02 }, { 0x41, 0x10 }, { 0x0A, 0x04 }, { 0x08, 0x03 },
	};
	static const int init_len[] = { 6, 2, 2, 2, 2, 2 };

	int chip = open("/dev/gpiochip0", O_RDWR);
	if (chip < 0) { perror("gpiochip0"); return 1; }
	struct gpiohandle_request req = { .lineoffsets = { GPIO_XRES, GPIO_WAKE }, .flags = GPIOHANDLE_REQUEST_OUTPUT,
					  .default_values = { 0, 0 }, .lines = 2 };
	strcpy(req.consumer_label, "ctp-test");
	if (ioctl(chip, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) { perror("line handle"); return 1; }
	lines_fd = req.fd;

	snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
	int i2c = open(path, O_RDWR);
	if (i2c < 0) { perror(path); return 1; }

	/* start from a powered-down controller so that no configuration of a previous run is kept */
	supply("disabled");
	usleep(100000);
	for (retry = 0; retry < 4; retry++) {
		/* digitizer.c: reset asserted, power on, 50 ms, wake, release reset, 50 ms, wake low, 50 ms */
		set_lines(1, 0);
		supply("enabled");
		usleep(50000);
		set_lines(1, 1);
		set_lines(0, 1);
		usleep(50000);
		set_lines(0, 0);
		usleep(50000);
		static const unsigned char first[2] = { 0x08, 0x00 };
		if (i2c_write(i2c, first, 2) == 0)
			break;
		printf("no answer, power cycle (retry %d)\n", retry + 1);
		supply("disabled");
		usleep(10000);
	}
	for (unsigned i = 0; i < sizeof(init_len) / sizeof(init_len[0]); i++)
		if (mask & (1u << i))
			i2c_write(i2c, init[i], init_len[i]);
	set_lines(0, 1);

	int uart = open("/dev/ttyMSM2", O_RDONLY | O_NOCTTY);
	if (uart < 0) { perror("ttyMSM2"); return 1; }
	struct termios t;
	tcgetattr(uart, &t);
	cfmakeraw(&t);
	cfsetispeed(&t, B4000000);
	cfsetospeed(&t, B4000000);
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 2;
	if (tcsetattr(uart, TCSANOW, &t) < 0)
		perror("tcsetattr 4000000");
	tcflush(uart, TCIFLUSH);

	setvbuf(stdout, NULL, _IONBF, 0);
	FILE *out = fopen("/tmp/ctp.raw", "w");
	unsigned char buf[4096];
	long total = 0;
	time_t end = time(NULL) + secs;
	while (time(NULL) < end) {
		int n = read(uart, buf, sizeof(buf));
		if (n > 0) {
			fwrite(buf, 1, n, out);
			total += n;
		}
	}
	fclose(out);
	printf("captured %ld bytes in %d s on /dev/ttyMSM2 -> /tmp/ctp.raw (lines and supply left on)\n", total, secs);
	return 0;
}
