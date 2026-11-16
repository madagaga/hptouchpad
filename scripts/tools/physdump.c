/*
 * physdump: read-only dump of a physical register range through /dev/mem, one "name offset value" line per word.
 * Used to compare the RPM requests (0x00104000: status 0x000-0x3fc, control 0x400-0x5fc, requests 0x600-0x9fc) between webOS and
 * our kernel (MM fabric arbitration of the video codec, STATUS.md). Only for always-on blocks: reading a powered-off block can hang.
 * Usage: physdump <name> <phys_base> <length>     Build (static armv7): gcc -static -O2 physdump.c -o physdump
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	unsigned long base, len, page, o;
	volatile uint32_t *p;
	int fd;

	if (argc != 4) {
		fprintf(stderr, "usage: physdump <name> <phys_base> <length>\n");
		return 1;
	}
	base = strtoul(argv[2], NULL, 0);
	len = strtoul(argv[3], NULL, 0);
	page = base & ~0xfffUL;
	fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) {
		perror("/dev/mem");
		return 1;
	}
	p = mmap(NULL, len + (base - page), PROT_READ, MAP_SHARED, fd, page);
	if (p == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	for (o = 0; o < len; o += 4)
		printf("%s 0x%03lx 0x%08x\n", argv[1], o, p[(base - page + o) / 4]);
	return 0;
}
