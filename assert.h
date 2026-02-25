#pragma once

#ifdef NDEBUG
	#define assert(x) ((void)(x))
#else
	#define assert(x) \
		if (!(x)) { \
			_assert(XSTR(x), __FUNCTION__, XSTR(BASE_FILE_NAME), __LINE__); \
		}
#endif // NDEBUG

void _assert(const char *cond, const char *func, const char *file, uint16_t line);
