#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/alib.h>

#include <workbench/startup.h>
#include <exec/types.h>

#include "stb_sprintf.h"
#include "timer.h"
#include "log.h"
#include "buffer.h"
#include "assert.h"

#include "system.h"

LOG_FACILITY(System, LL_INFO);

extern int    __argc;
extern char **__argv;
extern uint32_t __commandlen;
extern char *__commandline;

extern struct WBStartup *_WBenchMsg;

typedef struct launchwb launchwb_t;
struct launchwb
{
	struct WBStartup startup;
	struct WBArg args[2];
	char toolName[108];
	char projName[108];
	uint32_t stack;
	int32_t pri;
	BPTR log;
	BPTR tool;
};

// Disable command line parsing
// argv and argc are null now
// TODO: maybe reimplement the buggy argument parsing
void __nocommandline(){};

static systimer_t g_timer;
static systimeval_t g_start;
static const uint32_t g_maxprime = 4294967291;

static buffer_t g_command;
static buffer_t g_workdir;
static buffer_t g_executable;
static buffer_t g_commandline;
static buffer_t g_tooltypes;
static BPTR g_con;
static struct DiskObject *g_dobj;

static const char * const g_path[] = {
	"",
	"PROGDIR:",
	"C:",
	"SYS:System/",
	"SYS:Utilities/",
	"SYS:Tools/",
	"SYS:Prefs/",
	"SYS:WBStartup/",
	NULL
};

static void sys_fib2info(fileinfo_t *item, struct FileInfoBlock *fib)
{
	if (!item->len) {
		item->len = strlen(fib->fib_FileName);
		assert((item->len + 1U) < sizeof(item->name));
		memcpy(item->name, fib->fib_FileName, item->len + 1);
	}
	item->glen = 0;
	item->hash = sys_djb2(item->name, item->len);
	item->ctype = fib->fib_DirEntryType > 0 ? CT_DIR : CT_NONE;
	item->attr = fib->fib_Protection & 0xFF;
	if (item->len >= 5) {
		char *ext = item->name + item->len - 5;
		if (!strcmp(ext, ".info")) {
			item->ficon = true;
		}
	}
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
	struct Process *process = (struct Process *)FindTask(NULL);
	if (!_WBenchMsg && !process->pr_CLI) {
		return false;
	}
	// initialize timer
	if (!timer_init(&g_timer)) {
		return false;
	}
	if (!timer_gettime(&g_timer, &g_start)) {
		timer_cleanup(&g_timer);
		return false;
	}

	// initialize string buffers
	buffer_init(&g_command, 1, 64);
	buffer_init(&g_workdir, 1, 64);
	buffer_init(&g_executable, 1, 64);
	buffer_init(&g_commandline, 1, 64);
	buffer_init(&g_tooltypes, sizeof(char *), 8);

	// load path of current directory
	sys_getpath(process->pr_CurrentDir, &g_workdir);
	buffer_append(&g_workdir, "", 1);

	// process arguments
	if (process->pr_CLI) {
		// Parse argv and enter main processing loop.
		struct CommandLineInterface * cli = (struct CommandLineInterface *)BADDR(process->pr_CLI);
		char *cmdname = (char *)BADDR(cli->cli_CommandName);

		// reconstruct full path to executable
		buffer_append_string(&g_executable, g_workdir.data, false);
		buffer_append(&g_executable, cmdname + 1, *cmdname);
		buffer_append(&g_executable, "", 1);

		// reconstruct full program command line
		buffer_append(&g_commandline, "\"", 1);
		buffer_append(&g_commandline, cmdname + 1, *cmdname);
		buffer_append(&g_commandline, "\"", 1);
		if (__commandlen) {
			buffer_append(&g_commandline, " ", 1);
			buffer_append(&g_commandline, __commandline, __commandlen);
		}
		char *back = (char *)buffer_back(&g_commandline);
		if (*back == '\n') {
			*back = 0;
		} else {
			buffer_append(&g_commandline, "", 1);
		}

		LOG_DEBUG("CLI Args: '%s'", g_commandline.data);
		if (SysBase->LibNode.lib_Version >= 36) {
			// load pr_Arguments
			LOG_DEBUG("pr_Arguments: %s", process->pr_Arguments);
		}
	} else if (_WBenchMsg) {
		// Parse wbstartup and enter main processing loop.
		struct WBStartup *startup = _WBenchMsg;
		LOG_DEBUG("WB Args: %d; sm_ToolWindow: '%s'", startup->sm_NumArgs, startup->sm_ToolWindow);
		for (int i = 0; i < startup->sm_NumArgs; i++) {
			struct WBArg *arg = startup->sm_ArgList + i;
			LOG_DEBUG("Arg[%u] -> %p; '%s'", i, arg->wa_Lock, arg->wa_Name);
		}

		// reconstruct full path to executable
		sys_getpath(startup->sm_ArgList[0].wa_Lock, &g_executable);
		buffer_append_string(&g_executable, startup->sm_ArgList[0].wa_Name, true);
	}

	// load toolset params
	g_dobj = GetDiskObject(sys_exepath());
	if (g_dobj) {
		for (char **it = (char **)g_dobj->do_ToolTypes; *it; it++) {
			char *tooltype = *it;
			char **back = (char **)buffer_emplace_back(&g_tooltypes);
			if (back) {
				*back = tooltype;
			}
			LOG_DEBUG("tooltype -> '%s'", tooltype);
		}
	}

	LOG_DEBUG("Executable: '%s'", g_executable.data);
	LOG_DEBUG("Workdir: '%s'", sys_workdirpath());
	LOG_DEBUG("Debug: '%s'", sys_matchtooltype("DEBUG"));
	return true;
}

void sys_cleanup()
{
	LOG_DEBUG("Cleanup");
	if (g_dobj) {
		FreeDiskObject(g_dobj);
		g_dobj = NULL;
	}

	buffer_cleanup(&g_command);
	buffer_cleanup(&g_workdir);
	buffer_cleanup(&g_executable);
	buffer_cleanup(&g_commandline);
	buffer_cleanup(&g_tooltypes);
	timer_cleanup(&g_timer);

	struct Process *proc = (struct Process *)FindTask(NULL);
	if (proc->pr_CIS == g_con) {
		proc->pr_CIS = 0;
	}
	if (proc->pr_COS == g_con) {
		proc->pr_COS = 0;
	}
	if (g_con) {
		Close(g_con);
		g_con = 0;
	}
}

const char *sys_matchtooltype(const char *key)
{
	char **tooltypes = (char **)buffer_at(&g_tooltypes, 0);
	uint16_t len = strlen(key);
	for (uint16_t i = 0; i < g_tooltypes.count; i++, tooltypes++) {
		char *tooltype = *tooltypes;
		if (!memcmp(tooltype, key, len)) {
			char *value = tooltype + len;
			switch (*value) {
				case '=':
					return value + 1;
				case 0:
					return value;
			}
		}
	}

	return NULL;
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

uint16_t sys_bstr2cstr(BSTR bstr, char *buffer, uint16_t size)
{
	if (bstr == 0) {
		if (buffer) {
			*buffer = '\0';
		}
		return 0;
	}

	UBYTE *ptr = (UBYTE *)BADDR(bstr);
	uint16_t length = MIN(ptr[0], size - 1);
	if (buffer) {
		memcpy(buffer, ptr + 1, length);
		buffer[length] = '\0';
	}
	return length;
}

static containertype_t sys_dlt2ct(LONG dol_Type)
{
	switch (dol_Type) {
		case DLT_DEVICE:
			return CT_DEV;
		case DLT_VOLUME:
			return CT_VOL;
		case DLT_DIRECTORY:
		case DLT_LATE:
		case DLT_NONBINDING:
			return CT_DIR;
		default:
			return CT_NONE;
	}
}

uint32_t sys_listvol(buffer_t *array)
{
	uint32_t result = 0;
	uint32_t hash = g_maxprime;
	LOG_TRACE("Listvol (%p)", array);
	assert(array->size == sizeof(fileinfo_t));
	array->user = (void *)hash;
	buffer_clear(array);

	Forbid();

	fileinfo_t * item;
	struct DosInfo *info = (struct DosInfo *)BADDR(DOSBase->dl_Root->rn_Info);
	struct DosList *list = (struct DosList *)BADDR(info->di_DevInfo);
	struct FileInfoBlock fib;
	do {
		item = (fileinfo_t *)buffer_emplace_back(array);
		if (!item) {
			result = ERROR_NO_FREE_STORE;
			break;
		}
		if (!Examine(list->dol_Lock, &fib)) {
			continue;
		}

		item->len = 0;
		if (sys_bstr2cstr(list->dol_Name, NULL, 0)) {
			item->len = sys_bstr2cstr(list->dol_Name, item->name, sizeof(item->name));
		}

		sys_fib2info(item, &fib);
		item->ctype = sys_dlt2ct(list->dol_Type);
		hash = sys_hcombine(hash, item->hash);

		LOG_TRACE("Listvol: Found %s '%s'", sys_ctmessage(item->ctype), item->name);
	} while ((list = (struct DosList *)BADDR(list->dol_Next)));

	Permit();

	array->user = (void *)hash;
	return result;
}

uint32_t sys_listdir(const char *path, buffer_t *array)
{
	uint32_t result = 0;
	LOG_TRACE("Listdir (%s, %p)", path, array);
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
		fileinfo_t *item = (fileinfo_t *)buffer_emplace_back(array);
		if (!item) {
			// out of memory
			result = ERROR_NO_FREE_STORE;
			break;
		}

		// store item into list
		item->len = 0;
		sys_fib2info(item, &fib);
		hash = sys_hcombine(hash, item->hash);

		LOG_TRACE("Listdir: Found %s '%s'", sys_ctmessage(item->ctype), item->name);
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

char *sys_isicon(const char *path)
{
	if (path) {
		char *dot = strrchr(path, '.');
		if (dot && !strcmp(dot, ".info")) {
			return dot;
		}
	}
	return NULL;
}

uint32_t sys_examine(const char *path, fileinfo_t *item)
{
	LOG_DEBUG("Examine (%s, %p)", path, item);
	assert(path != NULL);
	assert(item != NULL);
	BPTR lock = Lock(path, ACCESS_READ);
	if (!lock) {
		return IoErr();
	}

	struct FileInfoBlock fib;
	if (!Examine(lock, &fib)) {
		UnLock(lock);
		return IoErr();
	}

	item->len = 0;
	sys_fib2info(item, &fib);

	BPTR parent = ParentDir(lock);
	if (!parent) {
		Forbid();

		// load type of container from the DosList (root node)
		struct FileLock *realLock = (struct FileLock *)BADDR(lock);
		struct DosList *volumeNode = (struct DosList *)BADDR(realLock->fl_Volume);
		item->ctype = sys_dlt2ct(volumeNode->dol_Type);

		Permit();
	} else {
		UnLock(parent);
	}

	UnLock(lock);
	return 0;
}

uint32_t sys_changedir(const char *path)
{
	BPTR lock = Lock(path, ACCESS_READ);
	if (!lock) {
		return IoErr();
	}
	BPTR oldLock = CurrentDir(lock);
	if (oldLock) {
		UnLock(oldLock);
	}
	return 0;
}

const char *sys_workdirpath()
{
	return g_workdir.count ? g_workdir.data : NULL;
}

const char *sys_exepath()
{
	return g_executable.count ? g_executable.data : NULL;
}

existsresult_t sys_exists(const char *path)
{
	if (path && *path) {
		BPTR targetLock = Lock(path, ACCESS_READ);
		if (targetLock) {
			struct FileInfoBlock fib;
			Examine(targetLock, &fib);
			UnLock(targetLock);
			return fib.fib_DirEntryType < 0 ? ER_IS_FILE : ER_IS_DIRECTORY;
		}
	}

	return ER_NO_EXISTS;
}

uint32_t sys_getpath(BPTR lock, buffer_t *buffer)
{
	uint32_t result = 0;
	struct FileInfoBlock fib;
	if (!lock) {
		return 0;
	}

	BPTR currLock = DupLock(lock);

	buffer_t stack;
	buffer_init(&stack, sizeof(fib.fib_FileName), 8);

	while (currLock) {
		if (!Examine(currLock, &fib)) {
			result = IoErr();
			break;
		}

		char *name = (char *)buffer_emplace_back(&stack);
		if (!name) {
			result = ERROR_NO_FREE_STORE;
			break;
		}

		BPTR parentLock = ParentDir(currLock);

		// store the file name on stack
		// don't have to be null terminated here
		*name = strlen(fib.fib_FileName);
		strcpy(name + 1, fib.fib_FileName);

		// generate separator for directory
		if (fib.fib_DirEntryType > 0) {
			name[*name + 1] = parentLock ? '/' : ':';
			*name = *name + 1;
		}

		// next parent
		UnLock(currLock);
		currLock = parentLock;
	}

	// append to buffer
	buffer_clear(buffer);
	while (stack.count) {
		char *name = (char *)buffer_back(&stack);
		buffer_append(buffer, name + 1,  *name);
		buffer_pop_back(&stack);
	}

	buffer_cleanup(&stack);
	return result;
}

BPTR sys_tmpfile(char **name)
{
	systimeval_t time;
	sys_gettime(&time);
	buffer_clear(&g_command);
	sys_sprintf(&g_command, "T:sys_%llX", time.tv_sec, time.tv_usec);
	buffer_append(&g_command, "", 1);
	if (name) {
		*name = strdup(g_command.data);
	}
	return Open(g_command.data, MODE_NEWFILE);
}

uint32_t sys_execute(char *path, const char *arguments, const char *workdir, uint32_t stack, BPTR input, BPTR output)
{
	LOG_DEBUG("Execute (%s, %s)", path, workdir);
	assert(path != NULL);

	uint32_t result = 0;
	if (!arguments) {
		arguments = "";
	}

	BPTR wdlock = 0;
	if (workdir) {
		// lock working dir
		wdlock = Lock(workdir, ACCESS_READ);
		if (!wdlock) {
			LOG_DEBUG("Failed to lock workdir '%s'", workdir);
			return IoErr();
		}
	}

	// change to working dir and save previous
	BPTR oldlock = 0;
	if (wdlock) {
		oldlock = CurrentDir(wdlock);
	}

	// generate launch script
	buffer_clear(&g_command);
	if (stack) {
		sys_sprintf(&g_command, "Stack %d\n", stack);
	}
	sys_sprintf(&g_command, "\"%s\" %s\n", path, arguments);

	LOG_DEBUG("Execute: Command: ----8<----\n%s----8<----", g_command.data);
	if (!Execute(g_command.data, input, output)) {
		result = IoErr();
	}

	// restore current dir
	if (wdlock) {
		CurrentDir(oldlock);
		UnLock(wdlock);
	}
	return result;
}

static int sys_launchwb_proc(struct Task *task, void *user)
{
	struct Process *process = (struct Process *)task;
	launchwb_t *data = (launchwb_t *)user;

	data->startup.sm_Message.mn_ReplyPort = &process->pr_MsgPort;

	Forbid();
	struct MsgPort *port = CreateProc(data->toolName, data->pri, data->startup.sm_Segment, data->stack);
	if (port) {
		struct Process *proc = (struct Process *)((UBYTE *)port - sizeof(struct Task));
		proc->pr_COS = data->log;
		data->startup.sm_Process = port;
		PutMsg(port, (struct Message *)&data->startup);
	}
	Permit();

	if (port) {
		WaitPort(&process->pr_MsgPort);
		struct Message *msg = GetMsg(&process->pr_MsgPort);
		assert(msg == &data->startup.sm_Message);
	}

	Forbid();
	UnLoadSeg(data->startup.sm_Segment);
	if (data->tool) {
		UnLock(data->tool);
	}
	for (uint16_t i = 0; i < data->startup.sm_NumArgs; i++) {
		UnLock(data->startup.sm_ArgList[i].wa_Lock);
	}
	FreeMem(data, sizeof(launchwb_t));
	return 0;
}

uint32_t sys_launchwb(const char *path)
{
	LOG_DEBUG("LaunchWB (%s)", path);

	struct DiskObject *dobj = GetDiskObject(path);
	if (!dobj) {
		LOG_DEBUG("LaunchWB: GetDiskObject(%s) failed", path);
		return IoErr();
	}

	LONG stack = dobj->do_StackSize;
	if (stack < 4096) {
		// minimal stack size
		stack = 4096;
	}

	UBYTE type = dobj->do_Type;

	// get full path to tool
	const char *proj = NULL;
	const char *tool = NULL;
	switch (type) {
		case WBPROJECT:
			tool = (char *)dobj->do_DefaultTool;
			proj = path;
			break;
		case WBTOOL:
			tool = path;
			break;
		default:
			FreeDiskObject(dobj);
			return ERROR_OBJECT_WRONG_TYPE;
	}

	// locate the tool in system
	// hardcoded paths for compatibility
	// TODO: maybe som improvement by config file?
	BPTR toolLock = 0;
	for (const char * const * it = g_path; *it; it++) {
		const char * const pathdir = *it;
		buffer_clear(&g_command);
		buffer_append_string(&g_command, pathdir, false);
		buffer_append_string(&g_command, tool, true);
		if (sys_exists(g_command.data) == ER_IS_FILE) {
			toolLock = Lock(g_command.data, ACCESS_READ);
			break;
		}
	}
	if (!toolLock) {
		// tool not found
		LOG_DEBUG("LaunchWB: tool '%s' not found", tool);
		FreeDiskObject(dobj);
		return IoErr();
	}

	// get full path of real locked file
	// (normalize the string especially on OS2.0+)
	sys_getpath(toolLock, &g_command);
	buffer_append(&g_command, "", 1);
	tool = (char *)g_command.data;

	// parent dir of tool
	BPTR toolDir = ParentDir(toolLock);
	if (!toolDir) {
		LOG_DEBUG("LaunchWB: tool '%s' dir lock failed", tool);
		UnLock(toolLock);
		FreeDiskObject(dobj);
		return IoErr();
	}

	// locate the project (by the very special way)
	// we do not need the file to exist, just pass the
	// path to disk object without .info extension
	BPTR projDir = 0;
	if (proj) {
		char *filepart = (char *)sys_filepart(proj);
		char backup = filepart[-1];
		filepart[-1] = 0;
		projDir = Lock(proj, ACCESS_READ);
		filepart[-1] = backup;
	}

	// get the default stack size for the tool
	struct DiskObject *tooldobj = GetDiskObject(tool);
	if (tooldobj) {
		if (tooldobj->do_StackSize > stack) {
			stack = tooldobj->do_StackSize;
		}
		FreeDiskObject(tooldobj);
	}

	// close DiskObject as early as possible to free the memory
	LOG_DEBUG("LaunchWB: tool: '%s'; proj: '%s';", tool, proj);
	FreeDiskObject(dobj);

	// alloc data
	launchwb_t *data = (launchwb_t *)AllocMem(sizeof(launchwb_t), MEMF_PUBLIC | MEMF_CLEAR);
	if (!data) {
		// TODO: release all locks
		LOG_DEBUG("LaunchWB: AllocMem(data) failed");
		goto exit_ioerr_locks;
	}

	// alloc seglist
	data->startup.sm_Segment = LoadSeg(tool);
	if (!data->startup.sm_Segment) {
		LOG_DEBUG("LaunchWB: LoadSeg '%s' failed (%d)", tool, IoErr());
		goto exit_ioerr_data_locks;
	}

	// fill-up stack size and priority
	data->stack = stack;
	data->pri = 0;

	LOG_DEBUG("LaunchWB: stack: %u; pri: %u;", data->stack, data->pri);

	// populate arguments
	int numArgs = 0;
	data->args[numArgs].wa_Lock = toolDir;
	data->args[numArgs].wa_Name = data->toolName;
	strcpy(data->toolName, sys_filepart(tool));
	++numArgs;

	if (proj && projDir) {
		data->args[numArgs].wa_Lock = projDir;
		data->args[numArgs].wa_Name = data->projName;
		strcpy(data->projName, sys_filepart(proj));
		++numArgs;
	}

	// populate rest of the startup structure
	data->startup.sm_NumArgs = numArgs;
	data->startup.sm_ArgList = data->args;
	data->startup.sm_Message.mn_Node.ln_Type = NT_MESSAGE;
	data->startup.sm_Message.mn_Length = sizeof(struct WBStartup);
	data->log = Output();
	data->tool = toolLock;

	struct MsgPort *port = sys_spawnproc(&sys_launchwb_proc, data, "LaunchWB", 0, 512);
	LOG_DEBUG("LaunchWB: sys_spawnproc: %p, %d", port, IoErr());
	if (!port) {
		UnLoadSeg(data->startup.sm_Segment);
exit_ioerr_data_locks:
		FreeMem(data, sizeof(launchwb_t));
exit_ioerr_locks:
		if (toolLock) {
			UnLock(toolLock);
		}
		if (toolDir) {
			UnLock(toolDir);
		}
		if (projDir) {
			UnLock(projDir);
		}
		return !data ? ERROR_NO_FREE_STORE : IoErr();
	}

	return 0;
}

const char * sys_filepart(const char* path)
{
	const char* current = path;
	const char* lastSplit = path;
	if (!path) {
		return NULL;
	}
	while (*current != '\0') {
		if (*current == '/' || *current == ':') {
			lastSplit = current + 1;
		}
		current++;
	}
	return lastSplit;
}

uint32_t sys_attachconsole(const char *title, int x, int y, int w, int h)
{
	struct Process *proc = (struct Process *)FindTask(NULL);
	if (!proc->pr_COS) {
		if (!g_con) {
			buffer_t buffer;
			buffer_init(&buffer, 1, 128);
			sys_sprintf(&buffer, "CON:%d/%d/%d/%d/%s", x, y, w, h, title);
			buffer_append(&buffer, "", 1);
			g_con = Open((char *)buffer.data, MODE_NEWFILE);
			buffer_cleanup(&buffer);
		}
		proc->pr_COS = g_con;
		if (!proc->pr_CIS) {
			proc->pr_CIS = g_con;
		}
	}

	return g_con ? 0 : IoErr();
}

static void __attribute__ ((noreturn)) sys_spawntask_cleanup()
{
	RemTask(NULL);

	// should not execute
	while (1);
}

struct Task *sys_spawntask(taskfunc_t func, void *user, const char *name, int8_t prio, uint32_t stack)
{
	struct task_memory
	{
		struct Task task;
		struct MemList list;
		char name[32];
		uint32_t stack;
	};

	uint32_t totalSize = sizeof(struct task_memory) + stack;
	struct task_memory *memory = (struct task_memory *)AllocMem(totalSize, MEMF_PUBLIC | MEMF_CLEAR);
	if (!memory) {
		return NULL;
	}

	// task name
	uint32_t nameLen = name ? strlen(name) : 0;
	if (nameLen) {
		strncpy(memory->name, name, sizeof(memory->name));
		if (nameLen >= sizeof(memory->name)) {
			nameLen = sizeof(memory->name) - 1;
			memory->name[nameLen] = 0;
		}
	}

	// initialize task structure
	struct Task *task = &memory->task;
	task->tc_Node.ln_Type = NT_TASK;
	task->tc_Node.ln_Pri  = prio;
	task->tc_Node.ln_Name = memory->name;
	task->tc_UserData = memory;

	// setup memory list for cleanup
	struct MemList *ml = &memory->list;
	ml->ml_Node.ln_Type = NT_MEMORY;
	ml->ml_NumEntries = 1;
	ml->ml_ME[0].me_Addr = (APTR)memory;
	ml->ml_ME[0].me_Length = totalSize;

	NewList(&task->tc_MemEntry);
	AddHead(&task->tc_MemEntry, &ml->ml_Node);

	// setup stack
	APTR stackptr = &memory->stack;
	task->tc_SPLower = stackptr;
	task->tc_SPUpper = (APTR)((ULONG)stackptr + stack);
	task->tc_SPReg   = task->tc_SPUpper; /* Stack grows downwards! */

	/* --- THE STACK MAGIC --- */
	uint32_t *sp = (uint32_t *)task->tc_SPUpper;
	*(--sp) = (uint32_t)user; /* Push Arg 2 */
	*(--sp) = (uint32_t)task; /* Push Arg 1 */
	*(--sp) = (uint32_t)&sys_spawntask_cleanup; /* return address */
	task->tc_SPReg = (APTR)sp;

	AddTask(task, func, NULL);
	return task;
}

static int sys_proc_entry(char* cmd_line __asm("a0"), int32_t length __asm("d0"))
{
	int result = 0;
	struct Process* self = (struct Process *)FindTask(NULL);
	ULONG *array = (ULONG *)BADDR(self->pr_SegList);
	BPTR seglist = array[3];
	fakeseg_t *seg = (fakeseg_t *)(((uint32_t *)BADDR(seglist)) - 1);
	if (seg->data.func) {
		result = seg->data.func(&self->pr_Task, seg->data.user);
	}

	// seg can be released because we are not currently executing
	// in the seg itself, we did only jump to another location
	FreeMem(seg, sizeof(fakeseg_t));
	return result;
}

struct MsgPort *sys_spawnproc(taskfunc_t func, void *user, const char* name, int8_t prio, uint32_t stack)
{
	fakeseg_t *seg = (fakeseg_t *)AllocMem(sizeof(fakeseg_t), MEMF_PUBLIC | MEMF_CLEAR);
	if (!seg) {
		return NULL;
	}

	seg->size = sizeof(fakeseg_t);
	seg->next = 0;

	/* C. Build the M68k Trampoline */
	/* 0x4EF9 is the 68000 opcode for JMP (Absolute) */
	seg->jump = 0x4EF9;	// 0100_1110_1111_1001
	seg->entry = &sys_proc_entry;
	seg->data.func = func;
	seg->data.user = user;

	BPTR bptrSeg = MKBADDR(&seg->next);
	struct MsgPort *newProc = CreateProc(name, prio, bptrSeg, stack);
	if (!newProc) {
		FreeMem(seg, sizeof(fakeseg_t));
		return NULL;
	}

	return newProc;
}

/*
BPTR sys_loadseg(char *progName)
{
	struct CommandLineInterface *cli;
	struct PathNode *node;
	BPTR pathNodePtr;
	BPTR oldDir;
	char fallback[256];


	BPTR seglist = LoadSeg(progName);
	if (seglist) return seglist;


	struct Process *proc = (struct Process *)FindTask(NULL);

	if (proc->pr_CLI == 0) {
		strcpy(fallback, "C:");
		strcat(fallback, progName);
		seglist = LoadSeg(fallback);
		if (seglist) return seglist;

		strcpy(fallback, "SYS:Utilities/");
		strcat(fallback, progName);
		return LoadSeg(fallback);
	}

	cli = (struct CommandLineInterface *)BADDR(proc->pr_CLI);
	pathNodePtr = cli->cli_CommandDir;

	while (pathNodePtr != 0) {
		node = (struct PathNode *)BADDR(pathNodePtr);

		if (node->lock != 0) {
			oldDir = CurrentDir(node->lock);

			seglist = LoadSeg(progName);

			CurrentDir(oldDir);

			if (seglist) break;
		}

		pathNodePtr = node->next;
	}

	return seglist;
}

*/

uint32_t sys_fnv1a32(const void * data, uint32_t len)
{
	uint8_t *bytes = (uint8_t *)data;
	uint32_t hash = 2166136261u;
	const uint32_t fnv_prime = 16777619u;
	for (uint32_t i = 0; i < len; i++) {
		hash = hash ^ bytes[i];
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
		/* hash * 33 + c */
		hash = ((hash << 5) + hash) + c;
	}

	return hash;
}

const char *const sys_ctmessage(containertype_t type)
{
	switch (type) {
	case CT_NONE:
		return "fil";
	case CT_DIR:
		return "dir";
	case CT_DEV:
		return "dev";
	case CT_VOL:
		return "vol";
	default:
		return "unk";
	}
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
