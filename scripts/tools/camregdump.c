/*
 * camregdump: read-only dump of the MSM8660 camera capture registers through /dev/mem, to take from webOS (front camera preview
 * running) the values its closed camera library writes (CAMIF, operation config, write masters: STATUS.md "Camera step 3").
 *   - MMSS clock controller (0x04000000, 0x000-0x2fc): camera clocks, resets, VFE footswitch;
 *   - CSI1 receiver + PHY (0x04900000, 0x000-0x3fc) when csi1_clk (CSI_CC 0x040 bit 7) and csi1_p_clk (AHB_EN 0x008 bit 20) are on;
 *   - VFE 3.1 (0x04500000, 0x000-0x7fc) when the VFE footswitch is on and unclamped (VFE_GFS_CTL 0x198 bit 8, not bit 5) and
 *     vfe_clk (VFE_CC 0x104 bit 0), vfe_p_clk (AHB_EN bit 13) and vfe_axi_clk (MAXI_EN 0x018 bit 18) are on: reading an unpowered or
 *     unclocked block can hang the bus.
 * Several VFE samples (the ping/pong addresses and the status registers move between frames).
 * Output: "block offset value" lines. Usage: camregdump [samples] [poll_ms]
 * Build (static, runs on webOS 2.6.35 and on Linux): gcc -static -O2 camregdump.c -o camregdump (armv7)
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define MMSS_BASE	0x04000000
#define CSI1_BASE	0x04900000
#define VFE_BASE	0x04500000
#define VFE_LEN		0x800

static volatile uint32_t *map(int fd, unsigned long phys, size_t len)
{
	void *p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, phys);

	if (p == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	return p;
}

int main(int argc, char **argv)
{
	int samples = argc > 1 ? atoi(argv[1]) : 3, poll_ms = argc > 2 ? atoi(argv[2]) : 30000, i, s, waited = 0;
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);

	if (fd < 0) {
		perror("/dev/mem");
		return 1;
	}
	volatile uint32_t *mmss = map(fd, MMSS_BASE, 0x1000);
	volatile uint32_t *csi = map(fd, CSI1_BASE, 0x1000);
	volatile uint32_t *vfe = map(fd, VFE_BASE, 0x1000);

#define CSI1_ON() ((mmss[0x040 / 4] & (1 << 7)) && (mmss[0x008 / 4] & (1 << 20)))
#define VFE_ON() ((mmss[0x198 / 4] & (1 << 8)) && !(mmss[0x198 / 4] & (1 << 5)) && (mmss[0x104 / 4] & 1) && \
		  (mmss[0x008 / 4] & (1 << 13)) && (mmss[0x018 / 4] & (1 << 18)))

	/* wait for the camera to stream */
	while (!(VFE_ON() && CSI1_ON()) && waited < poll_ms) {
		usleep(10000);
		waited += 10;
	}
	printf("# camregdump: vfe_on=%d csi1_on=%d after %d ms; vfe_gfs=0x%08x vfe_cc=0x%08x csi_cc=0x%08x ahb_en=0x%08x maxi_en=0x%08x\n",
	       !!VFE_ON(), !!CSI1_ON(), waited, mmss[0x198 / 4], mmss[0x104 / 4], mmss[0x040 / 4], mmss[0x008 / 4], mmss[0x018 / 4]);
	for (i = 0; i < 0x300; i += 4)
		printf("mmss 0x%03x 0x%08x\n", i, mmss[i / 4]);
	if (CSI1_ON())
		for (i = 0; i < 0x400; i += 4)
			printf("csi1 0x%03x 0x%08x\n", i, csi[i / 4]);
	else
		printf("# csi1 clocks off: not read\n");

	for (s = 0; s < samples; s++) {
		static uint32_t v[VFE_LEN / 4];

		if (!VFE_ON()) {
			printf("# sample %d: vfe off, not read\n", s);
			continue;
		}
		for (i = 0; i < VFE_LEN; i += 4)
			v[i / 4] = vfe[i / 4];
		if (!VFE_ON()) {
			printf("# sample %d: vfe went off during the read, dropped\n", s);
			continue;
		}
		for (i = 0; i < VFE_LEN; i += 4)
			printf("vfe%d 0x%03x 0x%08x\n", s, i, v[i / 4]);
		usleep(50000);
	}
	return 0;
}
