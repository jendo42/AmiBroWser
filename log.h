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
	LL_FATAL
};

struct logfacility
{
	loglevel_t level;
	const char *name;
	volatile bool locked;
};

#define PROFILE_START() { systimeval_t t1, t2; sys_gettime(&t1)
#define PROFILE_END(name) sys_gettime(&t2); timer_diff(&t1, &t2); LOG_TRACE("PROBE '%s' took: %lu.%06lu", name, t2.tv_sec, t2.tv_usec); }

#ifdef NLOG
	#define LOG_FACILITY(n, l)
	#define LOG(level, format, ...)
	#define LOG_TRACE(format, ...)
	#define LOG_DEBUG(format, ...)
	#define LOG_INFO(format, ...)
	#define LOG_WARN(format, ...)
	#define LOG_ERROR(format, ...)
	#define LOG_FATAL(format, ...)
#else // NLOG
	#define LOG_FACILITY(n, l) \
	static logfacility_t g_staticFacility = { \
		.name = #n, \
		.level = l, \
		.locked = false \
	}; \
	static logfacility_t *g_facility = &g_staticFacility;

	#define LOG(level, format, ...) log_printf(level, g_facility, format, ##__VA_ARGS__)
	#define LOG_TRACE(format, ...) LOG(LL_TRACE, format, ##__VA_ARGS__)
	#define LOG_DEBUG(format, ...) LOG(LL_DEBUG, format, ##__VA_ARGS__)
	#define LOG_INFO(format, ...) LOG(LL_INFO, format, ##__VA_ARGS__)
	#define LOG_WARN(format, ...) LOG(LL_WARN, format, ##__VA_ARGS__)
	#define LOG_ERROR(format, ...) LOG(LL_ERROR, format, ##__VA_ARGS__)
	#define LOG_FATAL(format, ...) LOG(LL_FATAL, format, ##__VA_ARGS__)
/*
	#define LOG_TRACE(format, ...) trace_printf(g_facility, format, ##__VA_ARGS__)
	#define LOG_DEBUG(format, ...) debug_printf(g_facility, format, ##__VA_ARGS__)
	#define LOG_INFO(format, ...) info_printf(g_facility, format, ##__VA_ARGS__)
	#define LOG_WARN(format, ...) warn_printf(g_facility, format, ##__VA_ARGS__)
	#define LOG_ERROR(format, ...) error_printf(g_facility, format, ##__VA_ARGS__)
	#define LOG_FATAL(format, ...) fatal_printf(g_facility, format, ##__VA_ARGS__)
*/
#endif // NLOG

const char *const log_levelstring(loglevel_t level);

void log_vprintf(loglevel_t level, logfacility_t *facility, const char *format, va_list args);
void log_printf(loglevel_t level, logfacility_t *facility, const char *format, ...);

/*
void trace_printf(logfacility_t *facility, const char *format, ...);
void debug_printf(logfacility_t *facility, const char *format, ...);
void info_printf(logfacility_t *facility, const char *format, ...);
void warn_printf(logfacility_t *facility, const char *format, ...);
void error_printf(logfacility_t *facility, const char *format, ...);
void fatal_printf(logfacility_t *facility, const char *format, ...);
*/
