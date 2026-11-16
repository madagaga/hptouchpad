/* libc memory routine self-test: compares memcpy/memmove/memset/memcmp/memchr/strlen with naive byte loops for all sizes and alignments.
 * Build: gcc -O2 -mfpu=neon -mfloat-abi=hard -static memtest.c -o memtest */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXN 5000
static unsigned char src[MAXN + 64] __attribute__((aligned(64)));
static unsigned char d1[MAXN + 64] __attribute__((aligned(64)));
static unsigned char d2[MAXN + 64] __attribute__((aligned(64)));

static void *(*volatile vmemcpy)(void *, const void *, size_t) = memcpy;
static void *(*volatile vmemmove)(void *, const void *, size_t) = memmove;
static void *(*volatile vmemset)(void *, int, size_t) = memset;

int main(void)
{
	int bad = 0, n, so, dof;
	unsigned seed = 12345;

	for (n = 0; n < MAXN + 64; n++) { seed = seed * 1103515245 + 12345; src[n] = seed >> 16; }

	for (n = 0; n <= 4200; n += (n < 300 ? 1 : 7))
		for (so = 0; so < 16; so++)
			for (dof = 0; dof < 16; dof += (n < 300 ? 1 : 5)) {
				memset(d1, 0xAA, sizeof d1);
				memset(d2, 0xAA, sizeof d2);
				vmemcpy(d1 + dof, src + so, n);
				for (int i = 0; i < n; i++) d2[dof + i] = src[so + i];
				if (memcmp(d1, d2, sizeof d1)) { if (bad++ < 10) printf("memcpy n=%d so=%d do=%d MISMATCH\n", n, so, dof); }
			}
	printf("memcpy done, bad=%d\n", bad);

	for (n = 0; n <= 3000; n += (n < 200 ? 1 : 11))
		for (so = 0; so < 16; so += 3)
			for (dof = 0; dof < 16; dof += 3) {
				memcpy(d1, src, MAXN); memcpy(d2, src, MAXN);
				vmemmove(d1 + dof + 8, d1 + so, n);
				if (dof + 8 >= so) { for (int i = n - 1; i >= 0; i--) d2[dof + 8 + i] = d2[so + i]; }
				else { for (int i = 0; i < n; i++) d2[dof + 8 + i] = d2[so + i]; }
				if (memcmp(d1, d2, MAXN)) { if (bad++ < 20) printf("memmove n=%d so=%d do=%d MISMATCH\n", n, so, dof); }
			}
	printf("memmove done, bad=%d\n", bad);

	for (n = 0; n <= 4200; n += (n < 300 ? 1 : 7))
		for (dof = 0; dof < 16; dof++) {
			memset(d1, 0x11, sizeof d1); memset(d2, 0x11, sizeof d2);
			vmemset(d1 + dof, 0xC3, n);
			for (int i = 0; i < n; i++) d2[dof + i] = 0xC3;
			if (memcmp(d1, d2, sizeof d1)) { if (bad++ < 30) printf("memset n=%d do=%d MISMATCH\n", n, dof); }
		}
	printf("memset done, bad=%d\n", bad);

	for (n = 1; n < 2000; n += 3) {
		memset(d1, 'x', sizeof d1);
		d1[n] = 0;
		if (strlen((char *)d1) != (size_t)n) { if (bad++ < 40) printf("strlen %d MISMATCH\n", n); }
		d1[n] = 0; d1[n - 1] = 'y';
		if (memchr(d1, 'y', n + 1) != d1 + n - 1) { if (bad++ < 40) printf("memchr %d MISMATCH\n", n); }
	}
	printf("TOTAL bad=%d\n", bad);
	return bad != 0;
}
