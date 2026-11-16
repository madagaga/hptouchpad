/*
 * This is a userspace power management driver for the digitizer in the HP
 * Touchpad to turn the digitizer on and off.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Copyright (c) 2012 CyanogenMod Touchpad Project.
 *
 * HP TouchPad mainline port: the downstream sysfs files (cy8ctma395 vdd/xres, user_hw ctp wake) are replaced by
 *   - the 5 V supply: regulator-output consumer /sys/devices/platform/touch-power/state ("enabled"/"disabled");
 *   - XRES (GPIO 70, active low) and WAKE (GPIO 123) held through the GPIO character device /dev/gpiochip0;
 *   - the GSBI10 I2C bus: /dev/i2c-1.
 * The xres_set() argument keeps the meaning of the downstream sysfs file: 1 = reset asserted (GPIO low).
 *
 *
 */

#define LOG_TAG "ts_power"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>

#include "digitizer.h"

#define TS_POWER_STATE "/sys/devices/platform/touch-power/state"
#define TS_GPIOCHIP "/dev/gpiochip0"
#define TS_GPIO_XRES 70
#define TS_GPIO_WAKE 123
#define TS_I2C_DEV "/dev/i2c-1"

static int vdd_fd, lines_fd, i2c_fd, ts_state, xres_val = 1, wake_val;

/* Both lines are requested together, so every change writes the two values */
static int lines_update(void)
{
	struct gpiohandle_data d = { .values = { !xres_val, wake_val } };

	return ioctl(lines_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &d) < 0 ? -1 : 1;
}

static int xres_set(int assert)
{
	xres_val = assert;
	return lines_update();
}

static int wake_set(int level)
{
	wake_val = level;
	return lines_update();
}

static int vdd_set(int on)
{
	const char *state = on ? "enabled" : "disabled";

	lseek(vdd_fd, 0, SEEK_SET);
	return write(vdd_fd, state, strlen(state)) == (ssize_t)strlen(state) ? 1 : -1;
}

void touchscreen_power(int enable)
{
    struct i2c_rdwr_ioctl_data i2c_ioctl_data;
    struct i2c_msg i2c_msg;
    __u8 i2c_buf[16];
    int rc;

    ALOGI("touchscreen_power: enable=%d, ts_state=%d", enable, ts_state);

    if (enable && !ts_state) {
		int retry_count = 0;
try_again:
		/* Set reset so the chip immediatelly sees it */
        rc = xres_set(1);
		if (rc != 1)
			ALOGE("TSpower, failed set xres");

		/* Then power on */
        rc = vdd_set(1);
		if (rc != 1)
			ALOGE("TSpower, failed to enable vdd");

		/* Sleep some more for the voltage to stabilize */
        usleep(50000);

        rc = wake_set(1);
		if (rc != 1)
			ALOGE("TSpower, failed to assert wake");

        rc = xres_set(0);
		if (rc != 1)
			ALOGE("TSpower, failed to reset xres");

        usleep(50000);

        rc = wake_set(0);
		if (rc != 1)
			ALOGE("TSpower, failed to deassert wake");

        usleep(50000);

        i2c_ioctl_data.nmsgs = 1;
        i2c_ioctl_data.msgs = &i2c_msg;

        i2c_msg.addr = 0x67;
        i2c_msg.flags = 0;
        i2c_msg.buf = i2c_buf;

        i2c_msg.len = 2;
        i2c_buf[0] = 0x08; i2c_buf[1] = 0;
        rc = ioctl(i2c_fd,I2C_RDWR,&i2c_ioctl_data);
		if (rc != 1)
			ALOGE("TSPower, ioctl1 failed %d errno %d\n", rc, errno);
		/* Ok, so the TS failed to wake, we need to retry a few times
		 * before totally giving up */
		if ((rc != 1) && (retry_count++ < MAX_DIGITIZER_RETRY)) {
			rc = vdd_set(0);
			usleep(10000);
			ALOGE("TS wakeup retry #%d\n", retry_count);
			goto try_again;
		}

        i2c_msg.len = 6;
        i2c_buf[0] = 0x31; i2c_buf[1] = 0x01; i2c_buf[2] = 0x08;
        i2c_buf[3] = 0x0C; i2c_buf[4] = 0x0D; i2c_buf[5] = 0x0A;
        rc = ioctl(i2c_fd,I2C_RDWR,&i2c_ioctl_data);
		if (rc != 1)
			ALOGE("TSPower, ioctl2 failed %d errno %d\n", rc, errno);

        i2c_msg.len = 2;
        i2c_buf[0] = 0x30; i2c_buf[1] = 0x0F;
        rc = ioctl(i2c_fd,I2C_RDWR,&i2c_ioctl_data);
		if (rc != 1)
			ALOGE("TSPower, ioctl3 failed %d errno %d\n", rc, errno);

        i2c_buf[0] = 0x40; i2c_buf[1] = 0x02;
        rc = ioctl(i2c_fd,I2C_RDWR,&i2c_ioctl_data);
		if (rc != 1)
			ALOGE("TSPower, ioctl4 failed %d errno %d\n", rc, errno);

        i2c_buf[0] = 0x41; i2c_buf[1] = 0x10;
        rc = ioctl(i2c_fd,I2C_RDWR,&i2c_ioctl_data);
		if (rc != 1)
			ALOGE("TSPower, ioctl5 failed %d errno %d\n", rc, errno);

        i2c_buf[0] = 0x0A; i2c_buf[1] = 0x04;
        rc = ioctl(i2c_fd,I2C_RDWR,&i2c_ioctl_data);
		if (rc != 1)
			ALOGE("TSPower, ioctl6 failed %d errno %d\n", rc, errno);

        i2c_buf[0] = 0x08; i2c_buf[1] = 0x03;
        rc = ioctl(i2c_fd,I2C_RDWR,&i2c_ioctl_data);
		if (rc != 1)
			ALOGE("TSPower, ioctl7 failed %d errno %d\n", rc, errno);

        rc = wake_set(1);
		if (rc != 1)
			ALOGE("TSpower, failed to assert wake again");
		ts_state = 1;
    } else if (ts_state) {
        rc = vdd_set(0);
		if (rc != 1)
			ALOGE("TSpower, failed to disable vdd");

		/* Weird, but on 4G touchpads even after vdd is off there is still
		 * stream of data from ctp that only disappears after we reset the
		 * touchscreen, even though it's supposedly powered off already
		 */
        rc = xres_set(1);
		usleep(10000);
        rc = xres_set(0);
        /* XXX, should be correllated with LIFTOFF_TIMEOUT in ts driver */
        usleep(80000);
		ts_state = 0;
    }
}

void init_digitizer_fd(void) {
	/* TS file descriptors. Ignore errors. */
	struct gpiohandle_request req = { .lineoffsets = { TS_GPIO_XRES, TS_GPIO_WAKE },
		.flags = GPIOHANDLE_REQUEST_OUTPUT, .default_values = { 0, 0 }, .lines = 2 };
	int chip;

	vdd_fd = open(TS_POWER_STATE, O_WRONLY);
	if (vdd_fd < 0)
		ALOGE("TScontrol: Cannot open vdd - %d", errno);
	chip = open(TS_GPIOCHIP, O_RDWR);
	strcpy(req.consumer_label, "ts_srv");
	if (chip < 0 || ioctl(chip, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0)
		ALOGE("TScontrol: Cannot get xres/wake lines - %d", errno);
	lines_fd = req.fd;
	if (chip >= 0)
		close(chip);
	i2c_fd = open(TS_I2C_DEV, O_RDWR);
	if (i2c_fd < 0)
		ALOGE("TScontrol: Cannot open i2c dev - %d", errno);
}
