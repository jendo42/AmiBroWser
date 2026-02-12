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

void log_vprintf(loglevel_t level, logfacility_t *facility, const char *format, va_list args)
{
	if (level >= facility->level && !facility->locked) {
		char buffer[STB_SPRINTF_MIN];
		systimeval_t time;
		sys_gettime(&time);
		stbsp_vsnprintf(buffer, sizeof(buffer), format, args);
		sys_printf("%lu.%06lu %s [%s] %s\n", time.tv_sec, time.tv_usec, log_levelstring(level), facility->name, buffer);
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

/*
PRINTF_TEMPLATE(trace, LL_TRACE)
PRINTF_TEMPLATE(debug, LL_DEBUG)
PRINTF_TEMPLATE(info, LL_INFO)
PRINTF_TEMPLATE(warn, LL_WARN)
PRINTF_TEMPLATE(error, LL_ERROR)
PRINTF_TEMPLATE(fatal, LL_FATAL)
*/
