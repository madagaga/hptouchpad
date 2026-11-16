/*
 * devmem: read or write a physical register through /dev/mem, with the syntax and the output of the BusyBox applet (Alpine's BusyBox
 * has none): devmem ADDRESS [WIDTH [VALUE]]; WIDTH is 8, 16 or 32 (default 32). A read prints the value as 0x%08X, a write prints nothing.
 * It is a mode of tp (called as `devmem`, usually a link to tp); the initramfs, which cannot hold the whole tp, builds this file alone
 * (-DDEVMEM_STANDALONE).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "tp.h"

int devmem_main(int argc, char **argv)
{
	unsigned long addr, value = 0, page, off;
	int width = 32, write = 0, fd;
	volatile void *map;
	char *end;

	if (argc < 2 || argc > 4) {
		fprintf(stderr, "usage: devmem ADDRESS [WIDTH [VALUE]]\n");
		return 1;
	}
	addr = strtoul(argv[1], &end, 0);
	if (*end) {
		fprintf(stderr, "devmem: bad address '%s'\n", argv[1]);
		return 1;
	}
	if (argc > 2) {
		width = atoi(argv[2]);
		if (width != 8 && width != 16 && width != 32) {
			fprintf(stderr, "devmem: bad width '%s' (8, 16 or 32)\n", argv[2]);
			return 1;
		}
	}
	if (argc > 3) {
		value = strtoul(argv[3], &end, 0);
		if (*end) {
			fprintf(stderr, "devmem: bad value '%s'\n", argv[3]);
			return 1;
		}
		write = 1;
	}
	if (addr % (width / 8)) {
		fprintf(stderr, "devmem: address 0x%lx is not aligned on %d bits\n", addr, width);
		return 1;
	}

	fd = open("/dev/mem", write ? O_RDWR | O_SYNC : O_RDONLY | O_SYNC);
	if (fd < 0) {
		fprintf(stderr, "devmem: /dev/mem: %s\n", strerror(errno));
		return 1;
	}
	page = (unsigned long)sysconf(_SC_PAGESIZE);
	off = addr & (page - 1);
	map = mmap(NULL, off + width / 8, write ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, fd, addr - off);
	if (map == MAP_FAILED) {
		fprintf(stderr, "devmem: mmap 0x%lx: %s\n", addr, strerror(errno));
		close(fd);
		return 1;
	}
	if (width == 32) {
		volatile uint32_t *p = (volatile uint32_t *)((volatile char *)map + off);
		if (write)
			*p = (uint32_t)value;
		else
			printf("0x%08lX\n", (unsigned long)*p);
	} else if (width == 16) {
		volatile uint16_t *p = (volatile uint16_t *)((volatile char *)map + off);
		if (write)
			*p = (uint16_t)value;
		else
			printf("0x%04lX\n", (unsigned long)*p);
	} else {
		volatile uint8_t *p = (volatile uint8_t *)((volatile char *)map + off);
		if (write)
			*p = (uint8_t)value;
		else
			printf("0x%02lX\n", (unsigned long)*p);
	}
	munmap((void *)map, off + width / 8);
	close(fd);
	return 0;
}

#ifdef DEVMEM_STANDALONE
int main(int argc, char **argv)
{
	return devmem_main(argc, argv);
}
#endif
