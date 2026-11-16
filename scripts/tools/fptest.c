/* Float and NEON self-test: prints one hex line per operation family. Run it under QEMU (reference) and on the tablet and diff the outputs.
 * Build: arm-linux-gnueabihf-gcc -O2 -mfpu=neon -mfloat-abi=hard -static fptest.c -lm -o fptest */
#include <arm_neon.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static float seed(int i) { return (float)(i * 37 % 1000) / 7.0f - 60.0f; }

int main(void)
{
	volatile float vf[8];
	uint32_t h;
	int i;

#define FAMILY(name, expr) do { h = 2166136261u; for (i = 1; i < 2000; i++) { float a = seed(i), b = seed(i + 3) + 0.37f; (void)a; (void)b; h = (h ^ bits((float)(expr))) * 16777619u; } printf("%-10s %08x\n", name, h); } while (0)
	FAMILY("add", a + b);
	FAMILY("mul", a * b);
	FAMILY("div", a / b);
	FAMILY("sqrt", sqrtf(fabsf(a)));
	FAMILY("floor", floorf(a));
	FAMILY("ceil", ceilf(b));
	FAMILY("round", roundf(a));
	FAMILY("trunc", truncf(b));
	FAMILY("fmod", fmodf(a, b));
	FAMILY("sin", sinf(a));
	FAMILY("pow", powf(fabsf(a) + 1.0f, 0.5f));
	FAMILY("f2i", (float)(int)(a * 3.3f));
	FAMILY("f2u8", (float)(uint8_t)(int)(fabsf(a) * 0.5f));
	FAMILY("clamp", fminf(fmaxf(a * 0.02f, 0.0f), 1.0f));
	FAMILY("fma", fmaf(a, b, 1.5f));
	FAMILY("denorm", a * 1e-39f);
	vf[0] = 0.5f;
	printf("half*255   %d\n", (int)(vf[0] * 255.0f + 0.5f));
	printf("half>>cvt  %d\n", (int)lrintf(vf[0] * 255.0f));

	/* NEON */
	{
		float32x4_t x, y, r;
		uint32_t hn = 2166136261u;
		float buf[4];
		for (i = 1; i < 2000; i++) {
			float t[4] = { seed(i), seed(i + 1), seed(i + 2), seed(i + 3) };
			float u[4] = { seed(i + 5) + 0.3f, seed(i + 7) + 0.3f, seed(i + 11) + 0.3f, seed(i + 13) + 0.3f };
			x = vld1q_f32(t);
			y = vld1q_f32(u);
			r = vmulq_f32(x, y);
			vst1q_f32(buf, r);
			for (int k = 0; k < 4; k++) hn = (hn ^ bits(buf[k])) * 16777619u;
			r = vaddq_f32(x, y);
			vst1q_f32(buf, r);
			for (int k = 0; k < 4; k++) hn = (hn ^ bits(buf[k])) * 16777619u;
			r = vmlaq_f32(x, x, y);
			vst1q_f32(buf, r);
			for (int k = 0; k < 4; k++) hn = (hn ^ bits(buf[k])) * 16777619u;
			float32x4_t e = vrecpeq_f32(y);
			e = vmulq_f32(vrecpsq_f32(y, e), e);
			e = vmulq_f32(vrecpsq_f32(y, e), e);
			r = vmulq_f32(x, e);
			vst1q_f32(buf, r);
			for (int k = 0; k < 4; k++) hn = (hn ^ bits(buf[k])) * 16777619u;
			int32x4_t c = vcvtq_s32_f32(x);
			float cf[4];
			vst1q_f32(cf, vcvtq_f32_s32(c));
			for (int k = 0; k < 4; k++) hn = (hn ^ bits(cf[k])) * 16777619u;
			r = vminq_f32(vmaxq_f32(x, vdupq_n_f32(-5.0f)), vdupq_n_f32(5.0f));
			vst1q_f32(buf, r);
			for (int k = 0; k < 4; k++) hn = (hn ^ bits(buf[k])) * 16777619u;
		}
		printf("neon-f32   %08x\n", hn);
	}
	{
		uint32_t hn = 2166136261u;
		uint8_t a[16], b[16], o[16];
		uint16_t w[8];
		for (i = 1; i < 2000; i++) {
			for (int k = 0; k < 16; k++) { a[k] = (uint8_t)(i * 7 + k * 13); b[k] = (uint8_t)(i * 3 + k * 29); }
			uint8x16_t x = vld1q_u8(a), y = vld1q_u8(b);
			vst1q_u8(o, vqaddq_u8(x, y));
			for (int k = 0; k < 16; k++) hn = (hn ^ o[k]) * 16777619u;
			vst1q_u8(o, vhaddq_u8(x, y));
			for (int k = 0; k < 16; k++) hn = (hn ^ o[k]) * 16777619u;
			uint16x8_t m = vmull_u8(vget_low_u8(x), vget_low_u8(y));
			vst1q_u16(w, m);
			for (int k = 0; k < 8; k++) hn = (hn ^ w[k]) * 16777619u;
			vst1_u8(o, vrshrn_n_u16(m, 8));
			for (int k = 0; k < 8; k++) hn = (hn ^ o[k]) * 16777619u;
		}
		printf("neon-u8    %08x\n", hn);
	}
	return 0;
}
