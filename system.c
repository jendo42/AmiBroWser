#include <memory.h>
#include <assert.h>
#include <libraries/dos.h>
#include <proto/exec.h>

#include "stb_sprintf.h"
#include "timer.h"
#include "log.h"
#include "buffer.h"

#include "system.h"

LOG_FACILITY(System, LL_DEBUG);

static systimer_t g_timer;
static systimeval_t g_start;
const uint32_t g_maxprime = 4294967291;

static void sys_fib2info(fileinfo_t *item, struct FileInfoBlock *fib)
{
	if (!item->len) {
		item->len = strlen(fib->fib_FileName);
		item->name[item->len] = 0;
		strncpy(item->name, fib->fib_FileName, sizeof(item->name));
	}
	item->hash = sys_djb2(item->name, item->len);
	item->ctype = fib->fib_DirEntryType > 0 ? CT_DIR : CT_NONE;
	item->fhold = (fib->fib_Protection & FIBF_HOLD);
	item->fscript = (fib->fib_Protection & FIBF_SCRIPT);
	item->fpure = (fib->fib_Protection & FIBF_PURE);
	item->farch = (fib->fib_Protection & FIBF_ARCHIVE);
	item->fread = (fib->fib_Protection & FIBF_READ);
	item->fwrite = (fib->fib_Protection & FIBF_WRITE);
	item->fexec = (fib->fib_Protection & FIBF_EXECUTE);
	item->fdel = (fib->fib_Protection & FIBF_DELETE);
}

static char *sprintf_callback(const char *buf, void *user, int len)
{
	Write((BPTR)user, buf, len);
	return (char *)buf;
}

static char *sprintf_callback_buffer(const char *buf, void *user, int len)
{
	buffer_t *buffer = (buffer_t *)user;
	if (!buffer_append(buffer, buf, len)) {
		return 0;
	}

	return (char *)buf;
}

bool sys_init()
{
	if (!timer_init(&g_timer)) {
		return false;
	}
	if (!timer_gettime(&g_timer, &g_start)) {
		timer_cleanup(&g_timer);
		return false;
	}
	return true;
}

int sys_vfprintf(BPTR fd, const char *format, va_list args)
{
	char printf_buffer[STB_SPRINTF_MIN];
	if (!fd) {
		return 0;
	}
	return stbsp_vsprintfcb(sprintf_callback, (void *)fd, printf_buffer, format, args);
}

int sys_vsprintf(buffer_t *buffer, const char *format, va_list args)
{
	char printf_buffer[STB_SPRINTF_MIN];
	if (!buffer) {
		return 0;
	}
	return stbsp_vsprintfcb(sprintf_callback_buffer, buffer, printf_buffer, format, args);
}

int sys_vprintf(const char *format, va_list args)
{
	return sys_vfprintf(Output(), format, args);
}

int sys_printf(const char *format, ...)
{
	int len;
	va_list args;
	va_start(args, format);
	len = sys_vprintf(format, args);
	va_end(args);
	return len;
}

int sys_fprintf(BPTR fd, const char *format, ...)
{
	int len;
	va_list args;
	va_start(args, format);
	len = sys_vfprintf(fd, format, args);
	va_end(args);
	return len;
}

int sys_sprintf(buffer_t *buffer, const char *format, ...)
{
	int len;
	va_list args;
	va_start(args, format);
	len = sys_vsprintf(buffer, format, args);
	va_end(args);
	return len;
}

void sys_gettime(systimeval_t *time)
{
	if (timer_gettime(&g_timer, time)) {
		timer_diff(&g_start, time);
	} else {
		memset(time, 0, sizeof(*time));
	}
}

void sys_cleanup()
{
	LOG_DEBUG("Cleanup");
	timer_cleanup(&g_timer);
}

int sys_bstr2cstr(BSTR bstr, char *buffer)
{
	if (bstr == 0) {
		if (buffer) {
			*buffer = '\0';
		}
		return 0;
	}

	UBYTE *ptr = (UBYTE *)BADDR(bstr);
	int length = ptr[0];
	if (buffer) {
		memcpy(buffer, ptr + 1, length);
		buffer[length] = '\0';
	}
	return length;
}

uint32_t sys_listvol(buffer_t *array)
{
	uint32_t result = 0;
	uint32_t hash = g_maxprime;
	LOG_TRACE("sys_listvol (%p)", array);
	assert(array->size == sizeof(fileinfo_t));
	array->user = (void *)hash;
	buffer_clear(array);

	Forbid();

	fileinfo_t * item;
	struct DosInfo *info = (struct DosInfo *)BADDR(DOSBase->dl_Root->rn_Info);
	struct DosList *list = (struct DosList *)BADDR(info->di_DevInfo);
	struct FileInfoBlock fib;
	do {
		item = (fileinfo_t *)buffer_emplace(array);
		if (!item) {
			result = ERROR_NO_FREE_STORE;
			break;
		}
		if (!Examine(list->dol_Lock, &fib)) {
			continue;
		}

		item->len = 0;
		if (sys_bstr2cstr(list->dol_Name, NULL)) {
			item->len = sys_bstr2cstr(list->dol_Name, item->name);
		}

		sys_fib2info(item, &fib);
		hash = sys_hcombine(hash, item->hash);

		switch (list->dol_Type) {
			case DLT_DEVICE:
				item->ctype = CT_DEV;
				break;
			case DLT_VOLUME:
				item->ctype = CT_VOL;
				break;
			case DLT_DIRECTORY:
			case DLT_LATE:
			case DLT_NONBINDING:
				item->ctype = CT_DIR;
				break;
		}

		LOG_TRACE("Found T%d: %s", (int)item->ctype, item->name);
	} while ((list = (struct DosList *)BADDR(list->dol_Next)));

	Permit();

	array->user = (void *)hash;
	return result;
}

uint32_t sys_listdir(const char *path, buffer_t *array)
{
	uint32_t result = 0;
	LOG_TRACE("sys_listdir (%s, %p)", path, array);
	assert(array->size == sizeof(fileinfo_t));
	if (path == NULL || *path == 0) {
		return sys_listvol(array);
	}

	// clear the output array, reset the hash
	uint32_t hash = g_maxprime;
	hash = sys_hcombine(hash, sys_djb2(path, 0));
	array->user = (void *)hash;
	buffer_clear(array);

	BPTR dirLock = Lock(path, ACCESS_READ);
	if (!dirLock) {
		return IoErr();
	}

	struct FileInfoBlock fib;
	if (!Examine(dirLock, &fib)) {
		UnLock(dirLock);
		return IoErr();
	}

	// test if it is a directory
	if (fib.fib_DirEntryType < 0) {
		UnLock(dirLock);
		return ERROR_OBJECT_WRONG_TYPE;
	}

	while (ExNext(dirLock, &fib)) {
		fileinfo_t *item = (fileinfo_t *)buffer_emplace(array);
		if (!item) {
			// out of memory
			result = ERROR_NO_FREE_STORE;
			break;
		}

		// store item into list
		item->len = 0;
		sys_fib2info(item, &fib);
		hash = sys_hcombine(hash, item->hash);

		// Print the name found in the FIB
		// In a real app, you would add this string to your ListView array here
		LOG_TRACE("sys_listdir: added '%s' (%s)", fib.fib_FileName, fib.fib_DirEntryType > 0 ? "DIR" : "FIL");
	}

	array->user = (void *)hash;

	uint32_t err = IoErr();
	if (err != ERROR_NO_MORE_ENTRIES) {
		result = err;
	}

	UnLock(dirLock);
	return result;
}

bool sys_iscontainer(containertype_t ct)
{
	switch (ct) {
		case CT_DIR:
		case CT_VOL:
			return true;
		default:
			return false;
	}
}

uint32_t sys_fnv1a32(const void * data, uint32_t len)
{
	uint8_t *bytes = (uint8_t *)data;

	// 1. Inicializácia počiatočnou hodnotou (Offset Basis)
	uint32_t hash = 2166136261u;

	// 2. FNV Prime konštanta
	const uint32_t fnv_prime = 16777619u;

	for (uint32_t i = 0; i < len; i++) {
		// Krok A: XOR (vtiahnutie dát do hashu)
		hash = hash ^ bytes[i];

		// Krok B: Násobenie (premiešanie bitov)
		// Vďaka pretečeniu (overflow) uint32_t to funguje ako modulo 2^32
		hash = hash * fnv_prime;
	}

	return hash;
}

uint32_t sys_djb2(const void *data, uint32_t len)
{
	uint32_t c;
	uint32_t hash = 5381;
	uint8_t *bytes = (uint8_t *)data;

	while (len-- && (c = *bytes++)) {
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}

	return hash;
}

uint32_t sys_hcombine(uint32_t in, uint32_t value)
{
	return in ^ (value + 0x9e3779b9 + (in << 6) + (in >> 2));
}

const char *const sys_ioerrmessage(uint32_t err)
{
	switch (err) {
		case 0:
			return "no error";
		case ERROR_NO_FREE_STORE:
			return "not enough memory";
		case ERROR_TASK_TABLE_FULL:
			return "process table full";
		case ERROR_BAD_TEMPLATE:
			return "bad template";
		case ERROR_BAD_NUMBER:
			return "bad number";
		case ERROR_REQUIRED_ARG_MISSING:
			return "required argument missing";
		case ERROR_KEY_NEEDS_ARG:
			return "key needs argument";
		case ERROR_TOO_MANY_ARGS:
			return "too many arguments";
		case ERROR_UNMATCHED_QUOTES:
			return "unmatched quotes";
		case ERROR_LINE_TOO_LONG:
			return "line too long";
		case ERROR_FILE_NOT_OBJECT:
			return "file is not an object";
		case ERROR_INVALID_RESIDENT_LIBRARY:
			return "invalid resident library";
		case ERROR_NO_DEFAULT_DIR:
			return "no default directory";
		case ERROR_OBJECT_IN_USE:
			return "object is in use";
		case ERROR_OBJECT_EXISTS:
			return "object already exists";
		case ERROR_DIR_NOT_FOUND:
			return "directory not found";
		case ERROR_OBJECT_NOT_FOUND:
			return "object not found";
		case ERROR_BAD_STREAM_NAME:
			return "bad stream name";
		case ERROR_OBJECT_TOO_LARGE:
			return "object too large";
		case ERROR_ACTION_NOT_KNOWN:
			return "action not known";
		case ERROR_INVALID_COMPONENT_NAME:
			return "invalid component name";
		case ERROR_INVALID_LOCK:
			return "invalid lock";
		case ERROR_OBJECT_WRONG_TYPE:
			return "object is of wrong type";
		case ERROR_DISK_NOT_VALIDATED:
			return "disk not validated";
		case ERROR_DISK_WRITE_PROTECTED:
			return "disk is write protected";
		case ERROR_RENAME_ACROSS_DEVICES:
			return "rename across devices";
		case ERROR_DIRECTORY_NOT_EMPTY:
			return "directory not empty";
		case ERROR_TOO_MANY_LEVELS:
			return "too many levels";
		case ERROR_DEVICE_NOT_MOUNTED:
			return "device not mounted";
		case ERROR_SEEK_ERROR:
			return "seek error";
		case ERROR_COMMENT_TOO_BIG:
			return "comment too big";
		case ERROR_DISK_FULL:
			return "disk is full";
		case ERROR_DELETE_PROTECTED:
			return "file is delete protected";
		case ERROR_WRITE_PROTECTED:
			return "file is write protected";
		case ERROR_READ_PROTECTED:
			return "file is read protected";
		case ERROR_NOT_A_DOS_DISK:
			return "not a DOS disk";
		case ERROR_NO_DISK:
			return "no disk in drive";
		case ERROR_NO_MORE_ENTRIES:
			return "no more entries";
		case ERROR_IS_SOFT_LINK:
			return "object is soft link";
		case ERROR_OBJECT_LINKED:
			return "object is linked";
		case ERROR_BAD_HUNK:
			return "bad hunk";
		case ERROR_NOT_IMPLEMENTED:
			return "not implemented";
		case ERROR_RECORD_NOT_LOCKED:
			return "record not locked";
		case ERROR_LOCK_COLLISION:
			return "lock collision";
		case ERROR_LOCK_TIMEOUT:
			return "lock timeout";
		case ERROR_UNLOCK_ERROR:
			return "unlock error";
		default:
			return "unknown error";
	}
}
