#pragma once

#include <stdlib.h>

#include "log.h"
#include "requester.h"

#define STR(x) #x
#define XSTR(x) STR(x)

#ifdef NDEBUG
	#define assert(x) ((void)(x))
#else
	#define assert(x) \
		if (!(x)) { \
			if (!requester_message(NULL, "Continue", "Abort", "Assertion failed: '%s'\nin function '%s' (%s:%u)\n\nSystem may be in unstable state!", XSTR(x), __FUNCTION__, XSTR(BASE_FILE_NAME), __LINE__)) { \
				exit(-1); \
			} \
		}
#endif // NDEBUG
