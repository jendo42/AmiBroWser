#include <proto/exec.h>

#include <string.h>

#include "log.h"
#include "timer.h"
#include "system.h"
#include "stb_sprintf.h"

#define X(x) x
#define XX(x) X(x)

#define PRINTF_TEMPLATE(level1, level2) \
void level1##_printf(logfacility_t *facility, const char *format, ...) \
{ \
	va_list args; \
	va_start(args, format); \
	log_vprintf(level2, facility, format, args); \
	va_end(args); \
}

#include "log_defs.inc"
static logfacility_t *g_logfacility[] = {
	#include "log_items.inc"
	NULL
};

const char *const log_levelstring(loglevel_t level)
{
	switch (level) {
		case LL_TRACE:
			return "TRACE";
		case LL_DEBUG:
			return "DEBUG";
		case LL_INFO:
			return "INFO";
		case LL_WARN:
			return "WARN";
		case LL_ERROR:
			return "ERROR";
		case LL_FATAL:
			return "FATAL";
		default:
			return "UNKNOWN";
	}
}

loglevel_t log_parselevel(const char *str)
{
	if (!str) {
		return LL_UNKNOWN;
	}
	switch(*str) {
		case 'T':
		case 't':
			return LL_TRACE;
		case 'D':
		case 'd':
			return LL_DEBUG;
		case 'I':
		case 'i':
			return LL_INFO;
		case 'W':
		case 'w':
			return LL_WARN;
		case 'E':
		case 'e':
			return LL_ERROR;
		case 'F':
		case 'f':
			return LL_FATAL;
		default:
			return LL_UNKNOWN;
	}
}

void log_vprintf(loglevel_t level, logfacility_t *facility, const char *format, va_list args)
{
	if (level >= facility->level && !facility->locked) {
		// hack? save the last IoErr
		struct Process *proc = (struct Process *)FindTask(NULL);
		uint32_t saved = proc->pr_Result2;
		char buffer[STB_SPRINTF_MIN];
		systimeval_t time;
		sys_gettime(&time);
		stbsp_vsnprintf(buffer, sizeof(buffer), format, args);
		sys_printf("%lu.%06lu %s [%s] %s\n", time.tv_sec, time.tv_usec, log_levelstring(level), facility->name, buffer);
		proc->pr_Result2 = saved;
		facility->locked = false;
	}
}

void log_printf(loglevel_t level, logfacility_t *facility, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	log_vprintf(level, facility, format, args);
	va_end(args);
}

bool log_setlevel(const char *name, loglevel_t level)
{
	if (name) {
		for (logfacility_t **it = g_logfacility; *it; it++) {
			logfacility_t *facility = *it;
			if (!strcmp(facility->name, name)) {
				facility->level = level;
				return true;
			}
		}
		return false;
	} else {
		for (logfacility_t **it = g_logfacility; *it; it++) {
			(*it)->level = level;
		}
		return true;
	}
}

logfacility_t **log_facilitylist()
{
	return g_logfacility;
}

/*
PRINTF_TEMPLATE(trace, LL_TRACE)
PRINTF_TEMPLATE(debug, LL_DEBUG)
PRINTF_TEMPLATE(info, LL_INFO)
PRINTF_TEMPLATE(warn, LL_WARN)
PRINTF_TEMPLATE(error, LL_ERROR)
PRINTF_TEMPLATE(fatal, LL_FATAL)
*/
