#pragma once

#include <workbench/workbench.h>
#include <proto/dos.h>
#include <stdarg.h>
#include <stdint.h>

#include "macros.h"
#include "buffer.h"
#include "timer.h"

typedef struct fileinfo fileinfo_t;
typedef enum containertype containertype_t;
typedef int (*taskfunc_t)(struct Task *task, void *user);
typedef struct taskdata taskdata_t;
typedef struct fakeseg fakeseg_t;

enum containertype
{
	CT_NONE,
	CT_DIR,
	CT_DEV,
	CT_VOL
};

struct taskdata
{
	taskfunc_t func;
	void *user;
};

struct fakeseg {
	ULONG dosSize;       /* -8 bytes: So DOS knows how much to FreeMem */
	ULONG nextBPTR;      /* -4 bytes: Pointer to next segment (0) */
	UWORD jmpInstruction;/* +0 bytes: The M68k instruction */
	void  (*entry)(void);/* +2 bytes: The address to jump to */
	taskdata_t data;
};

struct fileinfo
{
	char name[108];
	uint16_t len;
	uint16_t glen;
	uint32_t hash;
	containertype_t ctype : 2;

	union {
		uint8_t attr;
		struct {
			bool fdel : 1;
			bool fexec : 1;
			bool fwrite : 1;
			bool fread : 1;
			bool farch : 1;
			bool fpure : 1;
			bool fscript : 1;
			bool fhold : 1;
		};
	};
};

bool sys_init();
void sys_cleanup();

int sys_vfprintf(BPTR fd, const char *format, va_list args);
int sys_vprintf(const char *format, va_list args);
int sys_vsprintf(buffer_t *buffer, const char *format, va_list args);

int sys_fprintf(BPTR fd, const char *format, ...);
int sys_printf(const char *format, ...);
int sys_sprintf(buffer_t *buffer, const char *format, ...);

uint16_t sys_bstr2cstr(BSTR bstr, char *buffer, uint16_t size);
const char *const sys_ioerrmessage(uint32_t err);

// Does not alloc anything, only finds file part of the path.
const char * sys_filepart(const char* path);

// Attaches the console window to the process
// only if there is not assigned one (stdin/stdout)
uint32_t sys_attachconsole(const char *title, int x, int y, int w, int h);

// Returns time elapsed from start of the application
// `sys_init()` must be called before usage
void sys_gettime(systimeval_t *time);

struct Task *sys_spawntask(taskfunc_t func, void *user, const char *name, int8_t prio, uint32_t stack);
struct MsgPort *sys_spawnproc(taskfunc_t func, void *user, const char* name, int8_t prio, uint32_t stack);

// Walks thru all parent locks up and generates full path of `lock`.
// If the `lock` itself is a directory, the path will have trailing
// separator present (`/` or `:`)
// @returns DOS Error Code
uint32_t sys_getpath(BPTR lock, buffer_t *buffer);

// Creates file in temp dir with randomized name
// @param `name` If set, returns malloced file name
// @returns System file handle
BPTR sys_tmpfile(char **name);

// Changes current working directory.
// @returns DOS Error code
uint32_t sys_changedir(const char *dir);

// Finds tool in system path
// @param `tool` tool name / path
// @param `buffer` returns full path to the tool
// @returns DOS Error Code
uint32_t sys_which(const char *tool, buffer_t *buffer);

// Automatically detects file type (specified by `cmdline`) and tries to launch the Tool/Project.
// When .info file exits, `sys_launchwb()` is called automatically on that object
// @param `cmdline` must be writable and will be modified when called
// @param `wait` whether wait for program to exit
// @param `input` stdin file pointer
// @param `output` stdout file pointer
// @returns DOS Error code
uint32_t sys_execute(char *cmdline, bool wait, BPTR input, BPTR output);

// Launches disk object with icon
// @param `dobj` valid DiskObject (loaded .info file)
// @param `path` full path to target file
uint32_t sys_launchwb(struct DiskObject *dobj, char *path);

// @returns DOS Error code
uint32_t sys_examine(const char *path, fileinfo_t *item);

// @returns DOS Error code; hash from path and names stored in `array->user`
uint32_t sys_listdir(const char *path, buffer_t *array);

// @returns DOS Error code; hash from path and names stored in `array->user`
uint32_t sys_listvol(buffer_t *array);

// @returns `true` for the browsable container type
bool sys_iscontainer(containertype_t ct);

uint32_t sys_djb2(const void *data, uint32_t len);
uint32_t sys_fnv1a32(const void * data, uint32_t len);

// @brief Use for merging hashes together
static inline uint32_t sys_hcombine(uint32_t in, uint32_t value)
{
	return in ^ (value + 0x9e3779b9 + (in << 6) + (in >> 2));
}
