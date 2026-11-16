/*
 * tp: one binary for the HP TouchPad settings and policies (docs/SETTINGS-PANEL.md, "Next architecture: one tp binary").
 *   tp --daemon        resident process: input (Home, screen sleep), power policy, the commands that need state
 *   tp [-j] <cmd...>   client: native commands through the daemon socket, the others run by the shell tp (tp-sh)
 *   tp --cgi           HTTP router of the settings panel (CGI of BusyBox httpd)
 * External tools (cogctl, poweroff...) are run as programs, never through a shell.
 */
#ifndef TP_H
#define TP_H

#include <poll.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#define TP_SOCK		"/run/tp.sock"
#define TP_SH		"/usr/local/bin/tp-sh"	/* the shell tp: commands not ported to C yet */
#define TP_DIR		"/mnt/media/touchpad"	/* settings on the webOS media volume */
#define TP_CONF		TP_DIR "/tpd.conf"
#define MAX_ARGS	16
#define MAX_REQ		4096

/* growing output buffer, used for the JSON answers */
struct buf {
	char *p;
	size_t len, cap;
};
void buf_add(struct buf *b, const char *s, size_t n);
void buf_printf(struct buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void buf_json_str(struct buf *b, const char *s);	/* quoted and escaped JSON string */
void buf_ok(struct buf *b);
void buf_fail(struct buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* in a command handler: answer an error and return */
#define FAIL(out, ...) do { buf_fail(out, __VA_ARGS__); return; } while (0)

/* settings file: key=value lines */
int conf_get(const char *key, char *val, size_t size);	/* 0 when found */
int conf_int(const char *key, int def);
int conf_set(const char *key, const char *val);

/* sysfs and small files */
int read_long(const char *path, long *v);		/* 0 on success */
int read_line(const char *path, char *s, size_t size);
int write_str(const char *path, const char *s);
int int_arg(const char *s, int min, int max, int *v);	/* 0 when s is an integer in [min, max] */
int backlight_get(void);				/* percent, -1 unknown */
int backlight_percent(int pct);

/* external programs, without a shell; stdout captured in out (may be NULL) */
int run(char *const argv[], char *out, size_t size);
void spawn(char *const argv[]);				/* detached, not waited for */

/* TLMM GPIO output value written in the register (as the charger scripts did with devmem) */
int tlmm_set(int gpio, int high);
int tlmm_get(int gpio);

void tp_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));	/* stderr + kernel log */
long long now_ms(void);

/* daemon state shared by the modules */
struct state {
	/* screen and panel (input.c) */
	int asleep, saved_level, panel_shown;
	/* power (power.c) */
	char source[8];			/* none, pc, wall */
	int step, lim_pc, lim_wall, hold;
	int capacity;			/* last good reading, -1 unknown */
	long long capacity_ms;		/* time of that reading */
	long long gauge_lost_ms;	/* first failed read on battery, 0 when the gauge answers */
	int power_saving;
	int crit_screen_off;		/* screen turned off once at the critical level on a charger */
};
extern struct state st;
extern int in_daemon;

/* native commands: argv[0] is the command name; the answer is JSON */
int cmd_native(const char *name);
int cmd_local(const char *name);	/* native, but run in the calling process instead of the daemon */
void cmd_run(int argc, char **argv, struct buf *out);

/* modules */
void input_init(void);
int input_fds(struct pollfd *pfd, int max);
void input_handle(struct pollfd *pfd, int n, long long t);
long long input_tick(long long t);			/* returns the next deadline */
void input_rescan(void);
void input_reload(void);
void cmd_panel(int argc, char **argv, struct buf *out);
void cmd_screen(int argc, char **argv, struct buf *out);
void panel_set(int show);
void screen_power(int on);

void power_init(void);
void power_tick(long long t);
void cmd_battery(int argc, char **argv, struct buf *out);
void cmd_charge(int argc, char **argv, struct buf *out);
void cmd_power(int argc, char **argv, struct buf *out);
void cmd_cpu(int argc, char **argv, struct buf *out);
void cpu_boost_apply(void);

void sound_init(void);
void cmd_sound(int argc, char **argv, struct buf *out);

void cmd_camera(int argc, char **argv, struct buf *out);

int ipc_call(int argc, char **argv, struct buf *out);	/* 0 with the answer, -1 without daemon */
int ipc_parse(char *req, size_t len, char **argv);
int daemon_main(void);
int cgi_main(void);
int devmem_main(int argc, char **argv);	/* tp called as `devmem` */

#endif
