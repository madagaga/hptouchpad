/*
 * Replacement for Android's <cutils/log.h>: the ALOG macros print to stderr (the init script sends it to /tmp/ts_srv.log),
 * with a wall-clock timestamp.
 * Part of the HP TouchPad port of the CyanogenMod ts_srv, GPL-2.0 like the original.
 */
#ifndef TS_SRV_LOG_H
#define TS_SRV_LOG_H

#include <stdio.h>
#include <time.h>

static inline void ts_log_time(void)
{
	struct timespec ts;
	struct tm tm;

	clock_gettime(CLOCK_REALTIME, &ts);
	localtime_r(&ts.tv_sec, &tm);
	fprintf(stderr, "%02d:%02d:%02d.%03ld ", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
}

#define TS_LOG(level, fmt, ...) \
	do { ts_log_time(); fprintf(stderr, "%s %s: " fmt "\n", level, LOG_TAG, ##__VA_ARGS__); } while (0)
#define ALOGE(fmt, ...) TS_LOG("E", fmt, ##__VA_ARGS__)
#define ALOGI(fmt, ...) TS_LOG("I", fmt, ##__VA_ARGS__)
#define ALOGD(fmt, ...) TS_LOG("D", fmt, ##__VA_ARGS__)
#define ALOGV(fmt, ...) do { } while (0)

#endif
