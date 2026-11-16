/* reboot(RESTART2, <command>) for the stock webOS kernel (busybox reboot cannot pass a command).
 * "recover" makes the Palm restart code write the "recover" reason to IMEM, so the bootloader enters the
 * USB recovery mode (0830:8071) instead of booting webOS. Usage: reboot-cmd recover
 * Static armhf build, pushed with novacom (see scripts/webos-recover.sh). */
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/reboot.h>

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : "recover";

	sync();
	syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		LINUX_REBOOT_CMD_RESTART2, cmd);
	perror("reboot");
	return 1;
}
