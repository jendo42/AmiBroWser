#include <stdlib.h>

#include "log.h"
#include "requester.h"

#include "assert.h"

void _assert(const char *cond, const char *func, const char *file, uint16_t line)
{
	if (!requester_message(NULL, "Continue", "Abort",
		"Assertion failed: '%s'\n"
		"in function '%s' (%s:%hu)\n\n"
		"System may be in unstable state!", cond, func, file, line)) {
			exit(-1);
	}
}
