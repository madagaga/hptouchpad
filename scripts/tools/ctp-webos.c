/*
 * ctp-webos: reference capture of the raw touch stream of the CY8CTMA395 under the stock webOS kernel (Palm hsuart
 * /dev/ctp_uart, cy8ctma395 sysfs files), to compare with the mainline port (which sensor columns carry data).
 *   ctp-webos read <seconds> <out>  : only reads /dev/ctp_uart (controller left as webOS configured it; stop hidd first)
 *   ctp-webos init <seconds> <out>  : power-up and I2C configuration of the CyanogenMod digitizer.c (GPL-2.0), then reads
 * Build (static, runs on webOS): arm-linux-gnueabihf-gcc -static -Os ctp-webos.c -o ctp-webos
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* from the Palm hsuart.h of the 3.4 tenderloin kernel */
struct hsuart_mode {
	int speed;
	int flags;
};
#define HSUART_IOCTL_GET_UARTMODE _IOR('h', 0x04, struct hsuart_mode)
#define HSUART_IOCTL_SET_UARTMODE _IOW('h', 0x05, struct hsuart_mode)

static void put(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY);

	if (fd < 0 || write(fd, val, strlen(val)) < 0)
		printf("write %s=%s: %s\n", path, val, strerror(errno));
	if (fd >= 0)
		close(fd);
}

static void i2c_write(int fd, const unsigned char *buf, int len)
{
	struct i2c_msg msg = { .addr = 0x67, .flags = 0, .len = len, .buf = (unsigned char *)buf };
	struct i2c_rdwr_ioctl_data data = { .msgs = &msg, .nmsgs = 1 };

	printf("i2c write %02x: %s\n", buf[0], ioctl(fd, I2C_RDWR, &data) == 1 ? "ok" : strerror(errno));
}

int main(int argc, char **argv)
{
	if (argc < 4)
		return printf("usage: ctp-webos read|init <seconds> <out>\n"), 1;
	int secs = atoi(argv[2]);
	int uart = open("/dev/ctp_uart", O_RDONLY | O_NONBLOCK);
	if (uart < 0)
		return printf("open /dev/ctp_uart: %s\n", strerror(errno)), 1;
	struct hsuart_mode mode;
	ioctl(uart, HSUART_IOCTL_GET_UARTMODE, &mode);
	printf("uart mode before: speed %d flags 0x%x\n", mode.speed, mode.flags);
	mode.speed = 0x3D0900;
	ioctl(uart, HSUART_IOCTL_SET_UARTMODE, &mode);

	if (!strcmp(argv[1], "init")) {
		static const unsigned char init[][6] = {
			{ 0x08, 0x00 }, { 0x31, 0x01, 0x08, 0x0C, 0x0D, 0x0A }, { 0x30, 0x0F }, { 0x40, 0x02 }, { 0x41, 0x10 },
			{ 0x0A, 0x04 }, { 0x08, 0x03 },
		};
		static const int len[] = { 2, 6, 2, 2, 2, 2, 2 };
		int i2c = open("/dev/i2c-5", O_RDWR);
		put("/sys/devices/platform/cy8ctma395/vdd", "0");
		usleep(100000);
		put("/sys/devices/platform/cy8ctma395/xres", "1");
		put("/sys/devices/platform/cy8ctma395/vdd", "1");
		usleep(50000);
		put("/sys/user_hw/pins/ctp/wake/level", "1");
		put("/sys/devices/platform/cy8ctma395/xres", "0");
		usleep(50000);
		put("/sys/user_hw/pins/ctp/wake/level", "0");
		usleep(50000);
		for (unsigned i = 0; i < sizeof(len) / sizeof(len[0]); i++)
			i2c_write(i2c, init[i], len[i]);
		put("/sys/user_hw/pins/ctp/wake/level", "1");
	}

	FILE *out = fopen(argv[3], "w");
	unsigned char buf[16384];
	long total = 0;
	time_t end = time(NULL) + secs;
	while (time(NULL) < end) {
		int n = read(uart, buf, sizeof(buf));
		if (n > 0) {
			fwrite(buf, 1, n, out);
			total += n;
		} else
			usleep(2000);
	}
	fclose(out);
	printf("captured %ld bytes in %d s -> %s\n", total, secs, argv[3]);
	return 0;
}
