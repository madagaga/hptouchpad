/*
 * wmreg: raw WM8958 register access over /dev/i2c-N (I2C_SLAVE_FORCE, the codec driver keeps the address).
 * Usage: wmreg <bus> r <reg>          prints the 16-bit value
 *        wmreg <bus> w <reg> <val>    writes it (bypasses the driver cache: live experiments only, not persistent)
 * Build (static armv7): gcc -static -O2 wmreg.c -o wmreg
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define I2C_SLAVE_FORCE 0x0706

int main(int argc, char **argv)
{
	char dev[32];
	uint8_t b[4];
	unsigned reg, val;
	int fd;

	if (argc < 4) {
		fprintf(stderr, "usage: wmreg <bus> r <reg> | w <reg> <val>\n");
		return 1;
	}
	snprintf(dev, sizeof(dev), "/dev/i2c-%s", argv[1]);
	fd = open(dev, O_RDWR);
	if (fd < 0 || ioctl(fd, I2C_SLAVE_FORCE, 0x1a) < 0) {
		perror(dev);
		return 1;
	}
	reg = strtoul(argv[3], NULL, 0);
	b[0] = reg >> 8;
	b[1] = reg;
	if (argv[2][0] == 'w') {
		val = strtoul(argv[4], NULL, 0);
		b[2] = val >> 8;
		b[3] = val;
		if (write(fd, b, 4) != 4) { perror("write"); return 1; }
		return 0;
	}
	{
		uint8_t out[2] = { b[0], b[1] }, in[2];
		struct i2c_msg m[2] = {
			{ .addr = 0x1a, .flags = 0, .len = 2, .buf = out },
			{ .addr = 0x1a, .flags = I2C_M_RD, .len = 2, .buf = in },
		};
		struct i2c_rdwr_ioctl_data d = { .msgs = m, .nmsgs = 2 };

		if (ioctl(fd, I2C_RDWR, &d) < 0) { perror("read"); return 1; }
		b[0] = in[0];
		b[1] = in[1];
	}
	printf("0x%04x\n", (b[0] << 8) | b[1]);
	return 0;
}
