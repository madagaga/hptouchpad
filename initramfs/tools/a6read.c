/* Reads registers of an HP TouchPad "A6" microcontroller (battery gauge, touch panel) over I2C.
 * Protocol (from the downstream drivers/misc/a6/a6.c): write the 16-bit register address, big endian,
 * then a repeated start and read one byte; multi-byte values are read register by register.
 * Usage: a6read <i2c-bus> <7-bit address, hex> <reg hex> [reg hex ...]      Static armhf build. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

int main(int argc, char **argv)
{
	char dev[32];
	int fd, i, rc = 0;
	unsigned addr;

	if (argc < 4) {
		fprintf(stderr, "usage: a6read <bus> <addr hex> <reg hex> [reg hex ...]\n");
		return 2;
	}
	snprintf(dev, sizeof(dev), "/dev/i2c-%s", argv[1]);
	addr = strtoul(argv[2], NULL, 16);
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return 1;
	}
	for (i = 3; i < argc; i++) {
		unsigned reg = strtoul(argv[i], NULL, 16);
		unsigned char out[2] = { reg >> 8, reg & 0xff }, in = 0;
		struct i2c_msg msgs[2] = {
			{ .addr = addr, .flags = 0, .len = 2, .buf = out },
			{ .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = &in },
		};
		struct i2c_rdwr_ioctl_data d = { .msgs = msgs, .nmsgs = 2 };

		if (ioctl(fd, I2C_RDWR, &d) < 0) {
			printf("0x%04x: error %s\n", reg, strerror(errno));
			rc = 1;
		} else {
			printf("0x%04x: 0x%02x\n", reg, in);
		}
	}
	return rc;
}
