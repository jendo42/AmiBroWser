#pragma once

#include <proto/dos.h>
#include <stdarg.h>
#include <stdint.h>

#include "buffer.h"
#include "timer.h"

typedef struct fileinfo fileinfo_t;
typedef enum containertype containertype_t;

enum containertype
{
	CT_NONE,
	CT_DIR,
	CT_DEV,
	CT_VOL
};

struct fileinfo
{
	char name[256];
	uint16_t len;
	uint32_t hash;
	containertype_t ctype : 2;

	bool fhold : 1;
	bool fscript : 1;
	bool fpure : 1;
	bool farch : 1;
	bool fread : 1;
	bool fwrite : 1;
	bool fexec : 1;
	bool fdel : 1;
};

int sys_vfprintf(BPTR fd, const char *format, va_list args);
int sys_vprintf(const char *format, va_list args);
int sys_vsprintf(buffer_t *buffer, const char *format, va_list args);

int sys_fprintf(BPTR fd, const char *format, ...);
int sys_printf(const char *format, ...);
int sys_sprintf(buffer_t *buffer, const char *format, ...);

int sys_bstr2cstr(BSTR bstr, char *buffer);
const char *const sys_ioerrmessage(uint32_t err);

void sys_gettime(systimeval_t *time);

bool sys_init();
void sys_cleanup();

// @returns DOS Error code; hash from path and names stored in `array->user`
uint32_t sys_listdir(const char *path, buffer_t *array);

// @returns DOS Error code; hash from path and names stored in `array->user`
uint32_t sys_listvol(buffer_t *array);

// @returns `true` for the browsable container type
bool sys_iscontainer(containertype_t ct);

uint32_t sys_djb2(const void *data, uint32_t len);
uint32_t sys_fnv1a32(const void * data, uint32_t len);

// @brief Use for merging hashes together
uint32_t sys_hcombine(uint32_t in, uint32_t value);
