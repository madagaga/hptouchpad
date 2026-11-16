/*
 * ctp-old: power-up and raw-data test of the HP TouchPad touch panel (Cypress CY8CTMA395) on an OLD downstream kernel (2.6.35, sysfs GPIO,
 * no GPIO character device, no regulator-output node). Same sequence as ctp-test.c (CM digitizer.c), other interfaces:
 *   - 5 V boost: GPIO 102 (sysfs), XRES: GPIO 70 (active low), WAKE: GPIO 123, all driven through /sys/class/gpio;
 *   - I2C at 0x67 through /dev/i2c-<bus> (the bus number of GSBI10 depends on the kernel: "scan" tries every bus);
 *   - raw frames on the GSBI10 UART at 4 Mbaud: "uart" tries the given tty devices and counts the frame markers (ff 43 / ff 47).
 * Usage: ctp-old scan                         : first handshake write on /dev/i2c-0..9, reports which bus answers at 0x67 (power cycle between tries)
 *        ctp-old run <bus> <seconds> <tty>... : power up, write the 6 configuration messages, capture each tty for <seconds> -> /tmp/ctp-<tty>.raw
 *        ctp-old off                          : XRES asserted, 5 V off
 * Build: gcc -O2 -static ctp-old.c -o ctp-old   (musl, Alpine armv7: a glibc static binary needs a kernel newer than 2.6.35)
 */
#include <errno.h>
#include <fcntl.h>
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
#define GPIO_5V 102

#ifndef B4000000
#define B4000000 0010007
#endif

static int gpio_write(const char *file, const char *text)
{
	FILE *f = fopen(file, "w");

	if (!f)
		return -1;
	fputs(text, f);
	return fclose(f);
}

static void gpio_export(int n)
{
	char path[64], num[16];

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", n);
	if (access(path, F_OK) != 0) {
		snprintf(num, sizeof(num), "%d", n);
		if (gpio_write("/sys/class/gpio/export", num) < 0)
			printf("gpio%d export: %s\n", n, strerror(errno));
	}
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", n);
	if (gpio_write(path, "out") < 0)
		printf("gpio%d direction: %s\n", n, strerror(errno));
}

static void gpio_set(int n, int v)
{
	char path[64];

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", n);
	if (gpio_write(path, v ? "1" : "0") < 0)
		printf("gpio%d value: %s\n", n, strerror(errno));
}

/* xres: 1 = reset asserted (GPIO low); wake: raw level */
static void lines(int xres, int wake)
{
	gpio_set(GPIO_XRES, !xres);
	gpio_set(GPIO_WAKE, wake);
}

static void supply(int on)
{
	gpio_set(GPIO_5V, on);
}

static int i2c_write(int fd, const unsigned char *buf, int len, int verbose)
{
	struct i2c_msg msg = { .addr = 0x67, .flags = 0, .len = len, .buf = (unsigned char *)buf };
	struct i2c_rdwr_ioctl_data data = { .msgs = &msg, .nmsgs = 1 };
	int rc = ioctl(fd, I2C_RDWR, &data);

	if (verbose)
		printf("i2c write %02x (%d bytes): %s\n", buf[0], len, rc == 1 ? "ok" : strerror(errno));
	return rc == 1 ? 0 : -1;
}

/* digitizer.c: reset asserted, power on, 50 ms, wake, release reset, 50 ms, wake low, 50 ms, first write */
static int power_up(int fd, int verbose)
{
	static const unsigned char first[2] = { 0x08, 0x00 };
	int retry;

	supply(0);
	usleep(100000);
	for (retry = 0; retry < 4; retry++) {
		lines(1, 0);
		supply(1);
		usleep(50000);
		lines(1, 1);
		lines(0, 1);
		usleep(50000);
		lines(0, 0);
		usleep(50000);
		if (i2c_write(fd, first, 2, verbose) == 0)
			return 0;
		supply(0);
		usleep(10000);
	}
	return -1;
}

static void capture(const char *tty, int secs)
{
	char out[96];
	unsigned char buf[4096];
	long total = 0, m43 = 0, m47 = 0;
	int prev = -1, uart = open(tty, O_RDONLY | O_NOCTTY | O_NONBLOCK);
	struct termios t;
	FILE *f;
	time_t end;

	if (uart < 0) {
		printf("%s: open: %s\n", tty, strerror(errno));
		return;
	}
	tcgetattr(uart, &t);
	cfmakeraw(&t);
	cfsetispeed(&t, B4000000);
	cfsetospeed(&t, B4000000);
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(uart, TCSANOW, &t) < 0)
		printf("%s: 4 Mbaud: %s\n", tty, strerror(errno));
	tcflush(uart, TCIFLUSH);
	snprintf(out, sizeof(out), "/tmp/ctp-%s.raw", strrchr(tty, '/') ? strrchr(tty, '/') + 1 : tty);
	f = fopen(out, "w");
	end = time(NULL) + secs;
	while (time(NULL) < end) {
		int i, n = read(uart, buf, sizeof(buf));

		if (n <= 0) {
			usleep(2000);
			continue;
		}
		if (f)
			fwrite(buf, 1, n, f);
		total += n;
		for (i = 0; i < n; i++) {
			if (prev == 0xff && buf[i] == 0x43)
				m43++;
			if (prev == 0xff && buf[i] == 0x47)
				m47++;
			prev = buf[i];
		}
	}
	if (f)
		fclose(f);
	close(uart);
	printf("%s: %ld bytes, %ld 'ff 43' row markers, %ld 'ff 47' frame ends -> %s\n", tty, total, m43, m47, out);
}

int main(int argc, char **argv)
{
	char path[32];
	int i, fd;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 2) {
		fprintf(stderr, "usage: ctp-old scan | run <bus> <seconds> <tty>... | off\n");
		return 1;
	}
	gpio_export(GPIO_XRES);
	gpio_export(GPIO_WAKE);
	gpio_export(GPIO_5V);

	if (!strcmp(argv[1], "off")) {
		lines(1, 0);
		supply(0);
		printf("off\n");
		return 0;
	}
	if (!strcmp(argv[1], "scan")) {
		for (i = 0; i < 10; i++) {
			snprintf(path, sizeof(path), "/dev/i2c-%d", i);
			fd = open(path, O_RDWR);
			if (fd < 0)
				continue;
			printf("%s: %s\n", path, power_up(fd, 0) == 0 ? "ANSWER at 0x67" : "no answer");
			close(fd);
		}
		lines(1, 0);
		supply(0);
		return 0;
	}
	if (!strcmp(argv[1], "run") && argc >= 5) {
		static const unsigned char init[][6] = {
			{ 0x31, 0x01, 0x08, 0x0C, 0x0D, 0x0A }, { 0x30, 0x0F }, { 0x40, 0x02 }, { 0x41, 0x10 }, { 0x0A, 0x04 }, { 0x08, 0x03 },
		};
		static const int init_len[] = { 6, 2, 2, 2, 2, 2 };

		snprintf(path, sizeof(path), "/dev/i2c-%d", atoi(argv[2]));
		fd = open(path, O_RDWR);
		if (fd < 0) {
			perror(path);
			return 1;
		}
		if (power_up(fd, 1) < 0) {
			printf("no answer from the controller\n");
			return 1;
		}
		for (i = 0; i < 6; i++)
			i2c_write(fd, init[i], init_len[i], 1);
		lines(0, 1);
		for (i = 4; i < argc; i++)
			capture(argv[i], atoi(argv[3]));
		printf("done (lines and supply left on; run 'ctp-old off' to stop)\n");
		return 0;
	}
	fprintf(stderr, "usage: ctp-old scan | run <bus> <seconds> <tty>... | off\n");
	return 1;
}
