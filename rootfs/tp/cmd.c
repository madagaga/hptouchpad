/* tp: native commands (the others are still run by the shell tp, TP_SH) */
#include <string.h>

#include "tp.h"

static const struct {
	const char *name;
	void (*fn)(int argc, char **argv, struct buf *out);
	int local;	/* runs in the calling process, not in the daemon (it blocks) */
} cmds[] = {
	{ "battery", cmd_battery, 0 },
	{ "camera", cmd_camera, 1 },
	{ "charge", cmd_charge, 0 },
	{ "cpu", cmd_cpu, 0 },
	{ "panel", cmd_panel, 0 },
	{ "power", cmd_power, 0 },
	{ "screen", cmd_screen, 0 },
	{ "sound", cmd_sound, 0 },
};

int cmd_native(const char *name)
{
	for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
		if (!strcmp(cmds[i].name, name))
			return 1;
	return 0;
}

int cmd_local(const char *name)
{
	for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
		if (!strcmp(cmds[i].name, name))
			return cmds[i].local;
	return 0;
}

void cmd_run(int argc, char **argv, struct buf *out)
{
	for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
		if (argc > 0 && !strcmp(cmds[i].name, argv[0])) {
			cmds[i].fn(argc, argv, out);
			return;
		}
	buf_fail(out, "unknown command");
}
