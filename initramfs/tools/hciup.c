/* Brings a Bluetooth HCI device up or down (the Debian bluez package no longer ships hciconfig).
 * Usage: hciup <index> [down]      Static armhf build, see scripts/initramfs.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#define AF_BT      31
#define BTPROTO_HCI 1
#define HCIDEVUP   _IOW('H', 201, int)
#define HCIDEVDOWN _IOW('H', 202, int)

int main(int argc, char **argv)
{
	int fd, idx, down = argc > 2 && !strcmp(argv[2], "down");

	if (argc < 2) {
		fprintf(stderr, "usage: hciup <index> [down]\n");
		return 2;
	}
	idx = atoi(argv[1]);
	fd = socket(AF_BT, SOCK_RAW, BTPROTO_HCI);
	if (fd < 0) {
		perror("socket");
		return 1;
	}
	if (ioctl(fd, down ? HCIDEVDOWN : HCIDEVUP, idx) < 0 && errno != EALREADY) {
		perror(down ? "HCIDEVDOWN" : "HCIDEVUP");
		return 1;
	}
	printf("hci%d %s\n", idx, down ? "down" : "up");
	return 0;
}
