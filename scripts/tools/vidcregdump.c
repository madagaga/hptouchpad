/*
 * vidcregdump: read-only dump of the MSM8660 video codec (VIDC 1080p = Samsung MFC v5) through /dev/mem, to compare the state
 * webOS leaves while its camera app runs the encoder with ours (STATUS.md "H.264 encoder bring-up").
 *   - MMSS clock controller (0x04000000, 0x000-0x2fc): vcodec clocks, footswitch, resets, MM AXI setup;
 *   - VIDC core (0x04400000): host interface 0x000-0x0fc, memory controller 0x500-0x51c, encoder buffers 0x600-0x75c,
 *     frame size/profile 0x800-0x85c, channel interface 0x2000-0x20bc, encoder control 0xc500-0xc5bc, 0xd000-0xd0dc,
 *     0xe000-0xe00c, AXI wrapper 0x80000-0x8007c, pixel cache 0xc0000-0xc007c;
 *   read ONLY while the codec is powered and clocked (VED_GFS_CTL 0x194 enable and not clamped, VCODEC_CC 0x0f8 bit 0,
 *   AHB_EN bit 11, MAXI_EN bit 19): reading an unclocked block can hang the bus.
 * Output: "block offset value" lines. Usage: vidcregdump [samples] [poll_ms]
 * Build (static armv7): gcc -static -O2 vidcregdump.c -o vidcregdump
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define MMSS_BASE	0x04000000
#define VIDC_BASE	0x04400000
#define VIDC_LEN	0x100000

static const unsigned int ranges[][2] = {
	{ 0x00000, 0x000fc }, { 0x00500, 0x0051c }, { 0x00600, 0x0075c }, { 0x00800, 0x0085c },
	{ 0x02000, 0x020bc }, { 0x0c500, 0x0c5bc }, { 0x0d000, 0x0d0dc }, { 0x0e000, 0x0e00c },
	{ 0x80000, 0x8007c }, { 0xc0000, 0xc007c },
};

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
	int samples = argc > 1 ? atoi(argv[1]) : 3, poll_ms = argc > 2 ? atoi(argv[2]) : 60000, waited = 0, s;
	unsigned int r, o;
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);

	if (fd < 0) {
		perror("/dev/mem");
		return 1;
	}
	volatile uint32_t *mmss = map(fd, MMSS_BASE, 0x1000);
	volatile uint32_t *vidc = map(fd, VIDC_BASE, VIDC_LEN);

#define VIDC_ON() ((mmss[0x194 / 4] & (1 << 8)) && !(mmss[0x194 / 4] & (1 << 5)) && (mmss[0x0f8 / 4] & 1) && \
		   (mmss[0x008 / 4] & (1 << 11)) && (mmss[0x018 / 4] & (1 << 19)))

	while (!VIDC_ON() && waited < poll_ms) {
		usleep(5000);
		waited += 5;
	}
	printf("# vidcregdump: vidc_on=%d after %d ms; ved_gfs=0x%08x vcodec_cc=0x%08x ahb_en=0x%08x maxi_en=0x%08x\n",
	       !!VIDC_ON(), waited, mmss[0x194 / 4], mmss[0x0f8 / 4], mmss[0x008 / 4], mmss[0x018 / 4]);
	for (o = 0; o < 0x300; o += 4)
		printf("mmss 0x%03x 0x%08x\n", o, mmss[o / 4]);

	for (s = 0; s < samples; s++) {
		static uint32_t v[2048];
		static unsigned int off[2048];
		int n = 0, i;

		if (!VIDC_ON()) {
			printf("# sample %d: codec off, not read\n", s);
			usleep(200000);
			continue;
		}
		for (r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++)
			for (o = ranges[r][0]; o <= ranges[r][1]; o += 4) {
				off[n] = o;
				v[n++] = vidc[o / 4];
			}
		if (!VIDC_ON()) {
			printf("# sample %d: codec went off during the read, dropped\n", s);
			continue;
		}
		for (i = 0; i < n; i++)
			printf("vidc%d 0x%05x 0x%08x\n", s, off[i], v[i]);
		usleep(100000);
	}
	return 0;
}
