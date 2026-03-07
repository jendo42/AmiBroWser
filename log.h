#pragma once

#include <stdarg.h>

#include "system.h"

typedef enum loglevel loglevel_t;
typedef struct logfacility logfacility_t;

enum loglevel {
	LL_TRACE,
	LL_DEBUG,
	LL_INFO,
	LL_WARN,
	LL_ERROR,
	LL_FATAL,

	LL_UNKNOWN,
};

struct logfacility
{
	loglevel_t level;
	const char *name;
	volatile bool locked;
};

#ifdef PROFILE
	#define PROFILE_START() { systimeval_t t1, t2; sys_gettime(&t1)
	#define PROFILE_END(name) sys_gettime(&t2); timer_diff(&t1, &t2); LOG_TRACE("PROBE '%s' took: %lu.%06lu", name, t2.tv_sec, t2.tv_usec); }
#else
	#define PROFILE_START()
	#define PROFILE_END(name)
#endif // PROFILE

#ifdef NLOG

	#define LOG_FACILITY(n, l)
	#define LOG(level, format, ...) (void)(##__VA_ARGS__)

#else // NLOG

	#define LOG_FACILITY(n, l) \
	logfacility_t g_facility_##n = { \
		.name = #n, \
		.level = l, \
		.locked = false \
	}; \
	static logfacility_t *g_facility = &g_facility_##n;

	#define LOG_FACILITY_ITEM(n) &g_facility_##n,

	#define LOG_FACILITY_DEF(n) extern logfacility_t g_facility_##n;

	#define LOG(level, format, ...) log_printf(level, g_facility, format, ##__VA_ARGS__)

#endif // NLOG

#define LOG_TRACE(format, ...) LOG(LL_TRACE, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) LOG(LL_DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) LOG(LL_INFO, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) LOG(LL_WARN, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOG(LL_ERROR, format, ##__VA_ARGS__)
#define LOG_FATAL(format, ...) LOG(LL_FATAL, format, ##__VA_ARGS__)

const char *const log_levelstring(loglevel_t level);
loglevel_t log_parselevel(const char *str);

logfacility_t **log_facilitylist();
void log_vprintf(loglevel_t level, logfacility_t *facility, const char *format, va_list args);
void log_printf(loglevel_t level, logfacility_t *facility, const char *format, ...);
