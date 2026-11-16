/* GLES2 vertex attribute test: draws the quad of Cog's GL renderer into an FBO with several ways of feeding the attributes and compares the pixels read
 * back with the analytic result (R = u, G = v from the texture coordinate attribute). Run on the msm GPU and with GBM_ALWAYS_SOFTWARE=1.
 * Build: gcc -O1 vbotest.c -o vbotest -lEGL -lGLESv2 -lgbm
 * Usage: vbotest [render node, default /dev/dri/renderD128] */
#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define W 64
#define H 64

static const char *vs_src = "attribute vec2 position;\nattribute vec2 texture;\nvarying vec2 v_texture;\nvoid main() {\n v_texture = texture;\n gl_Position = vec4(position, 0.0, 1.0);\n}\n";
static const char *fs_src = "precision mediump float;\nvarying vec2 v_texture;\nvoid main() {\n gl_FragColor = vec4(v_texture, 0.0, 1.0);\n}\n";
/* VBO_RED=1: constant red fragment shader, to tell whether a wrong draw runs a stale shader */
static const char *fs_red_src = "precision mediump float;\nvarying vec2 v_texture;\nvoid main() {\n gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n}\n";

static GLuint compile(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "shader: %s\n", log); exit(1); }
	return s;
}

/* position: (-1,1) (1,1) (-1,-1) (1,-1); texture: (0,0) (1,0) (0,1) (1,1) : the rotation 0 layout of Cog */
static const GLfloat pos[8] = { -1, 1, 1, 1, -1, -1, 1, -1 };
static const GLfloat tex[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };

static int verbose = 1;

static int run(const char *name, int mode, GLuint prog)
{
	GLint a_pos = glGetAttribLocation(prog, "position"), a_tex = glGetAttribLocation(prog, "texture");
	GLuint vbo[2] = { 0, 0 };
	GLfloat both[16], inter[16];
	unsigned char px[W * H * 4];
	int bad = 0, x, y;
	double maxerr = 0;

	glClearColor(1, 0, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	memcpy(both, pos, sizeof pos);
	memcpy(both + 8, tex, sizeof tex);
	for (int i = 0; i < 4; i++) { inter[i * 4] = pos[i * 2]; inter[i * 4 + 1] = pos[i * 2 + 1]; inter[i * 4 + 2] = tex[i * 2]; inter[i * 4 + 3] = tex[i * 2 + 1]; }

	glEnableVertexAttribArray(a_pos);
	glEnableVertexAttribArray(a_tex);
	switch (mode) {
	case 0: /* one VBO, texture at a byte offset, stride 0: what Cog and kmscube do */
		glGenBuffers(1, vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
		glBufferData(GL_ARRAY_BUFFER, sizeof both, both, GL_STATIC_DRAW);
		glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
		glVertexAttribPointer(a_tex, 2, GL_FLOAT, GL_FALSE, 0, (void *)(8 * sizeof(GLfloat)));
		break;
	case 1: /* two VBOs */
		glGenBuffers(2, vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo[0]); glBufferData(GL_ARRAY_BUFFER, sizeof pos, pos, GL_STATIC_DRAW);
		glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
		glBindBuffer(GL_ARRAY_BUFFER, vbo[1]); glBufferData(GL_ARRAY_BUFFER, sizeof tex, tex, GL_STATIC_DRAW);
		glVertexAttribPointer(a_tex, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
		break;
	case 2: /* client arrays */
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 0, pos);
		glVertexAttribPointer(a_tex, 2, GL_FLOAT, GL_FALSE, 0, tex);
		break;
	case 3: /* one VBO, interleaved */
		glGenBuffers(1, vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
		glBufferData(GL_ARRAY_BUFFER, sizeof inter, inter, GL_STATIC_DRAW);
		glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
		glVertexAttribPointer(a_tex, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
		break;
	case 4: /* one VBO, texture at a byte offset, explicit stride 8 */
		glGenBuffers(1, vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
		glBufferData(GL_ARRAY_BUFFER, sizeof both, both, GL_STATIC_DRAW);
		glVertexAttribPointer(a_pos, 2, GL_FLOAT, GL_FALSE, 8, (void *)0);
		glVertexAttribPointer(a_tex, 2, GL_FLOAT, GL_FALSE, 8, (void *)(8 * sizeof(GLfloat)));
		break;
	}
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glFinish();
	{
		const char *dl = getenv("VBO_DELAY_MS");
		if (dl) usleep(atoi(dl) * 1000);
	}
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);

	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++) {
			double eu = (x + 0.5) / W, ev = 1.0 - (y + 0.5) / H;	/* GL row 0 is the bottom: v = 1 there */
			unsigned char *p = px + (y * W + x) * 4;
			double e = fmax(fabs(p[0] / 255.0 - eu), fabs(p[1] / 255.0 - ev));
			if (e > maxerr) maxerr = e;
			if (e > 0.06) {
				if (verbose && bad < 8 && mode == 0)
					printf("  bad pixel x=%d y=%d value=%02x%02x%02x expected=%02x%02x00\n", x, y, p[0], p[1], p[2], (int)(eu * 255), (int)(ev * 255));
				bad++;
			}
		}
	if (getenv("VBO_RED")) {
		int red = 0;

		for (x = 0; x < W * H; x++)
			if (px[x * 4] == 0xff && px[x * 4 + 1] == 0 && px[x * 4 + 2] == 0)
				red++;
		printf("RED %-28s red=%4d/%d px(2,2)=%02x%02x%02x px(61,61)=%02x%02x%02x px(32,32)=%02x%02x%02x\n", name, red, W * H,
		       px[(2 * W + 2) * 4], px[(2 * W + 2) * 4 + 1], px[(2 * W + 2) * 4 + 2],
		       px[(61 * W + 61) * 4], px[(61 * W + 61) * 4 + 1], px[(61 * W + 61) * 4 + 2],
		       px[(32 * W + 32) * 4], px[(32 * W + 32) * 4 + 1], px[(32 * W + 32) * 4 + 2]);
	}
	if (verbose && bad > 1000 && mode == 0) {
		printf("  R/G grid (rows from the top, x = 4,12,..60):\n");
		for (y = H - 4; y >= 0; y -= 8) {
			printf("  y=%02d:", y);
			for (x = 4; x < W; x += 8)
				printf(" %02x%02x", px[(y * W + x) * 4], px[(y * W + x) * 4 + 1]);
			putchar(10);
		}
	}
	if (verbose && bad > 1000 && mode == 0) {
		for (y = H - 1; y >= 0; y -= 4) {
			char line[W / 2 + 1];
			for (x = 0; x < W; x += 2) {
				unsigned char *q = px + (y * W + x) * 4;
				double eu = (x + 0.5) / W, ev = 1.0 - (y + 0.5) / H;
				double e = fmax(fabs(q[0] / 255.0 - eu), fabs(q[1] / 255.0 - ev));
				line[x / 2] = e <= 0.06 ? 'g' : (q[0] == 0xff && q[1] == 0 && q[2] == 0xff) ? 'M' : (q[0] == 0 && q[1] == 0 && q[2] == 0) ? '0' : '.';
			}
			line[W / 2] = 0;
			printf("  %s\n", line);
		}
	}
	if (verbose)
		printf("%-28s bad=%4d/%d maxerr=%.2f  px(2,2)=%02x%02x%02x  px(61,2)=%02x%02x%02x  px(2,61)=%02x%02x%02x  px(61,61)=%02x%02x%02x  px(32,32)=%02x%02x%02x\n", name, bad, W * H, maxerr,
	       px[(2 * W + 2) * 4], px[(2 * W + 2) * 4 + 1], px[(2 * W + 2) * 4 + 2],
	       px[(2 * W + 61) * 4], px[(2 * W + 61) * 4 + 1], px[(2 * W + 61) * 4 + 2],
	       px[(61 * W + 2) * 4], px[(61 * W + 2) * 4 + 1], px[(61 * W + 2) * 4 + 2],
	       px[(61 * W + 61) * 4], px[(61 * W + 61) * 4 + 1], px[(61 * W + 61) * 4 + 2],
	       px[(32 * W + 32) * 4], px[(32 * W + 32) * 4 + 1], px[(32 * W + 32) * 4 + 2]);
	glDisableVertexAttribArray(a_pos);
	glDisableVertexAttribArray(a_tex);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	if (vbo[0]) glDeleteBuffers(2, vbo);
	return bad;
}

int main(int argc, char **argv)
{
	const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	PFNEGLGETPLATFORMDISPLAYEXTPROC getdpy = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay dpy;
	if (!strcmp(node, "surfaceless")) {
		/* reference run without any GPU (llvmpipe/softpipe), e.g. in a container: vbotest surfaceless */
		dpy = getdpy(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	} else {
		int fd = open(node, O_RDWR);
		if (fd < 0) { perror(node); return 1; }
		struct gbm_device *gbm = gbm_create_device(fd);
		dpy = getdpy(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	}
	EGLint maj, min;
	if (!eglInitialize(dpy, &maj, &min)) { fprintf(stderr, "eglInitialize failed\n"); return 1; }
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint cfg_attr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	EGLConfig cfg;
	EGLint n;
	if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
		cfg = EGL_NO_CONFIG_KHR;	/* surfaceless swrast exposes no config: configless context */
	EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { fprintf(stderr, "eglMakeCurrent failed 0x%x\n", eglGetError()); return 1; }
	printf("renderer: %s\n", glGetString(GL_RENDERER));

	GLuint tex_id, fbo;
	glGenTextures(1, &tex_id);
	glBindTexture(GL_TEXTURE_2D, tex_id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_id, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "FBO incomplete\n"); return 1; }
	glViewport(0, 0, W, H);

	GLuint prog = glCreateProgram();
	/*
	 * VBO_VS=1: the texture coordinate is computed from the position (no second attribute, same expected image);
	 * VBO_VS=2: constant varying (0.25, 0.75): expected colour 40bf00 everywhere.
	 */
	{
		const char *vs = vs_src, *e = getenv("VBO_VS");
		static const char *vs_pos = "attribute vec2 position;\nattribute vec2 texture;\nvarying vec2 v_texture;\nvoid main() {\n v_texture = vec2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);\n gl_Position = vec4(position, 0.0, 1.0);\n}\n";
		static const char *vs_const = "attribute vec2 position;\nattribute vec2 texture;\nvarying vec2 v_texture;\nvoid main() {\n v_texture = vec2(0.25, 0.75);\n gl_Position = vec4(position, 0.0, 1.0);\n}\n";

		if (e && *e == '1') vs = vs_pos;
		if (e && *e == '2') vs = vs_const;
		glAttachShader(prog, compile(GL_VERTEX_SHADER, vs));
	}
	glAttachShader(prog, compile(GL_FRAGMENT_SHADER, getenv("VBO_RED") ? fs_red_src : fs_src));
	glLinkProgram(prog);
	glUseProgram(prog);

	if (argc > 3 && !strcmp(argv[2], "-n")) {
		/* sequence mode: N identical draws of the given mode, prints o (seam only) or X (mirrored) for each */
		int cnt = atoi(argv[3]), mode = argc > 4 ? atoi(argv[4]) : 0;
		verbose = 0;
		for (int i = 0; i < cnt; i++) putchar(run("", mode, prog) > 1000 ? 'X' : 'o');
		putchar(10);
		return 0;
	}
	run("VBO offset, stride 0 (Cog)", 0, prog);
	run("two VBOs", 1, prog);
	run("client arrays", 2, prog);
	run("interleaved VBO", 3, prog);
	run("VBO offset, stride 8", 4, prog);
	run("VBO offset, stride 0 (again)", 0, prog);
	return 0;
}
