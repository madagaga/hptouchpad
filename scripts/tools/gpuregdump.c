/*
 * gpuregdump: read-only dump of the MSM8660 GPU state through /dev/mem, to compare webOS (kgsl) with our mainline kernel.
 *   - MMSS clock controller (0x04000000, 0x000-0x2fc): GFX3D clocks, resets, footswitch (GFS_CTL), bus enables;
 *   - Adreno 220 registers (0x04300000) from the downstream kgsl list a220_registers[] (adreno_a2xx.c), ONLY when the 3D core is powered
 *     (GFX3D_GFS_CTL enable bit set, clamp released) and its clock branch is enabled: reading the GPU while it is collapsed can hang the bus.
 *   - optionally the global clock controller (0x00900000, 0x0000-0x3ffc) with "gcc".
 * Output: one "name offset value" line per register, easy to diff. Usage: gpuregdump [gcc] [retries]
 * Build (static, runs on webOS 2.6.35 and on Linux): arm-linux-gnueabihf-gcc -static -O2 gpuregdump.c -o gpuregdump
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static const unsigned int a220_registers[] = {
	0x0000, 0x0002, 0x0004, 0x000B, 0x003B, 0x003D, 0x0040, 0x0044,
	0x0046, 0x0047, 0x01C0, 0x01C1, 0x01C3, 0x01C8, 0x01D5, 0x01D9,
	0x01DC, 0x01DD, 0x01EA, 0x01EA, 0x01EE, 0x01F3, 0x01F6, 0x01F7,
	0x01FC, 0x01FF, 0x0391, 0x0392, 0x039B, 0x039E, 0x03B2, 0x03B5,
	0x03B7, 0x03B7, 0x03F8, 0x03FB, 0x0440, 0x0440, 0x0443, 0x0444,
	0x044B, 0x044B, 0x044D, 0x044F, 0x0452, 0x0452, 0x0454, 0x045B,
	0x047F, 0x047F, 0x0578, 0x0587, 0x05C9, 0x05C9, 0x05D0, 0x05D0,
	0x0601, 0x0604, 0x0606, 0x0609, 0x060B, 0x060E, 0x0613, 0x0614,
	0x0A29, 0x0A2B, 0x0A2F, 0x0A31, 0x0A40, 0x0A40, 0x0A42, 0x0A43,
	0x0A45, 0x0A45, 0x0A4E, 0x0A4F, 0x0C30, 0x0C30, 0x0C38, 0x0C39,
	0x0C3C, 0x0C3C, 0x0C80, 0x0C81, 0x0C88, 0x0C93, 0x0D00, 0x0D03,
	0x0D05, 0x0D06, 0x0D08, 0x0D0B, 0x0D34, 0x0D35, 0x0DAE, 0x0DC1,
	0x0DC8, 0x0DD4, 0x0DD8, 0x0DD9, 0x0E00, 0x0E00, 0x0E02, 0x0E04,
	0x0E17, 0x0E1E, 0x0EC0, 0x0EC9, 0x0ECB, 0x0ECC, 0x0ED0, 0x0ED0,
	0x0ED4, 0x0ED7, 0x0EE0, 0x0EE2, 0x0F01, 0x0F02, 0x2000, 0x2002,
	0x2006, 0x200F, 0x2080, 0x2082, 0x2100, 0x2102, 0x2104, 0x2109,
	0x210C, 0x2114, 0x2180, 0x2184, 0x21F5, 0x21F7, 0x2200, 0x2202,
	0x2204, 0x2204, 0x2208, 0x2208, 0x2280, 0x2282, 0x2294, 0x2294,
	0x2300, 0x2308, 0x2309, 0x230A, 0x2312, 0x2312, 0x2316, 0x2316,
	0x2318, 0x231D, 0x2324, 0x2326, 0x2380, 0x2383, 0x2400, 0x2402,
	0x2406, 0x240F, 0x2480, 0x2482, 0x2500, 0x2502, 0x2504, 0x2509,
	0x250C, 0x2514, 0x2580, 0x2584, 0x25F5, 0x25F7, 0x2600, 0x2602,
	0x2604, 0x2606, 0x2608, 0x2608, 0x2680, 0x2682, 0x2694, 0x2694,
	0x2700, 0x2708, 0x2712, 0x2712, 0x2716, 0x2716, 0x2718, 0x271D,
	0x2724, 0x2726, 0x2780, 0x2783, 0x4000, 0x4003, 0x4800, 0x4805,
	0x4900, 0x4900, 0x4908, 0x4908,
};

#define MMSS_BASE	0x04000000
#define GPU_BASE	0x04300000
#define GCC_BASE	0x00900000

static volatile uint32_t *map(int fd, unsigned long phys, size_t len)
{
	void *p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, phys);
	if (p == MAP_FAILED) { perror("mmap"); exit(1); }
	return p;
}

int main(int argc, char **argv)
{
	int gcc = 0, retries = 50, i, k;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "gcc")) gcc = 1;
		else retries = atoi(argv[i]);
	}
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) { perror("/dev/mem"); return 1; }
	volatile uint32_t *mmss = map(fd, MMSS_BASE, 0x1000);
	volatile uint32_t *gpu = map(fd, GPU_BASE, 0x20000);

	/*
	 * webOS (kgsl) only wakes the GPU for a few milliseconds per frame. Poll until the rail is on, the core clock (GFX3D_CC bit 0), the AHB interface
	 * clock (AHB_EN bit 3) and the GMEM/IMEM AXI clocks (MAXI_EN bits 24/22) are all enabled, read the whole register list into a buffer, check the
	 * clocks again, and keep the dump only if they were on before AND after and the values are not all identical (an unclocked read returns junk).
	 */
	static uint32_t vals[4096];
	static unsigned int offs[4096];
	int n = 0, on = 0, tries = 0;
	uint32_t gfs = 0, cc = 0;

#define GPU_ON() ((mmss[0x188 / 4] & (1 << 8)) && !(mmss[0x188 / 4] & (1 << 5)) && (mmss[0x080 / 4] & 1) && \
		  (mmss[0x008 / 4] & (1 << 3)) && (mmss[0x018 / 4] & (1 << 24)) && (mmss[0x018 / 4] & (1 << 22)))
	for (k = 0; k < retries && !on; k++) {
		if (!GPU_ON()) {
			usleep(2000);
			continue;
		}
		tries++;
		n = 0;
		for (i = 0; i < (int)(sizeof(a220_registers) / sizeof(a220_registers[0])); i += 2)
			for (unsigned int r = a220_registers[i]; r <= a220_registers[i + 1]; r++) {
				offs[n] = r;
				vals[n++] = gpu[r];
			}
		if (!GPU_ON())
			continue;
		int same = 1;
		for (i = 1; i < n; i++)
			if (vals[i] != vals[0]) same = 0;
		if (!same)
			on = 1;
	}
	gfs = mmss[0x188 / 4];
	cc = mmss[0x080 / 4];
	printf("# gpuregdump: valid=%d after %d polls, %d complete reads; now gfs_ctl=0x%08x gfx3d_cc=0x%08x\n", on, k, tries, gfs, cc);
	for (i = 0; i < 0x300; i += 4)
		printf("mmss 0x%03x 0x%08x\n", i, mmss[i / 4]);
	if (on)
		for (i = 0; i < n; i++)
			printf("gpu 0x%04x 0x%08x\n", offs[i], vals[i]);
	else
		printf("# no valid GPU register read (GPU not active long enough)\n");
	if (gcc) {
		volatile uint32_t *g = map(fd, GCC_BASE, 0x4000);
		for (i = 0; i < 0x4000; i += 4)
			printf("gcc 0x%04x 0x%08x\n", i, g[i / 4]);
	}
	return 0;
}
