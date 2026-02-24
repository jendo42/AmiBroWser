#include <memory.h>
#include <libraries/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <workbench/startup.h>
#include <proto/alib.h>
#include <exec/types.h>

#include "stb_sprintf.h"
#include "timer.h"
#include "log.h"
#include "buffer.h"
#include "assert.h"

#include "system.h"

LOG_FACILITY(System, LL_DEBUG);

static systimer_t g_timer;
static systimeval_t g_start;
static const uint32_t g_maxprime = 4294967291;
static buffer_t g_command;
static BPTR g_con = 0;

static void sys_fib2info(fileinfo_t *item, struct FileInfoBlock *fib)
{
	if (!item->len) {
		item->len = strlen(fib->fib_FileName);
		item->name[item->len] = 0;
		strncpy(item->name, fib->fib_FileName, sizeof(item->name));
	}
	item->glen = 0;
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

	buffer_init(&g_command, 1, 128);
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
	buffer_cleanup(&g_command);
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
		if (sys_bstr2cstr(list->dol_Name, NULL)) {
			item->len = sys_bstr2cstr(list->dol_Name, item->name);
		}

		sys_fib2info(item, &fib);
		item->ctype = sys_dlt2ct(list->dol_Type);
		hash = sys_hcombine(hash, item->hash);

		LOG_TRACE("Listvol: Found T%d: %s", (int)item->ctype, item->name);
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

		// Print the name found in the FIB
		// In a real app, you would add this string to your ListView array here
		LOG_TRACE("Listdir: Found: '%s' (%s)", fib.fib_FileName, fib.fib_DirEntryType > 0 ? "DIR" : "FIL");
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

static void sys_command(buffer_t *buffer, const char *tool, const char *path, uint32_t stack, bool wait)
{
	buffer_clear(buffer);

	if (stack) {
		sys_sprintf(buffer, "Stack %d\n", stack);
	}

	if (!wait) {
		sys_sprintf(buffer, "Run >NIL: <NIL: ");
	}

	// silent
	//sys_sprintf(buffer, "");

	sys_sprintf(buffer, "\"%s\"", tool);
	if (path) {
		sys_sprintf(buffer, " \"%s\"", path);
	}

	//sys_sprintf(buffer, "If Warn\nRequestChoice \"BroWser\" \"'%s' failed with code: $RC\"\nEndIf\n", path);
	buffer_append(buffer, "\n", 2);
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

uint32_t sys_which(const char *tool, buffer_t *buffer)
{
	char *tmpname;
	uint32_t result = 0;
	BPTR tmp = sys_tmpfile(&tmpname);
	sys_command(&g_command, "Which", tool, 0, true);
	if (!Execute(g_command.data, 0, tmp)) {
		result = IoErr();
	}
	if (!result) {
		Seek(tmp, 0, OFFSET_END);
		uint32_t size = Seek(tmp, 0, OFFSET_BEGINNING);
		buffer_clear(buffer);
		buffer_append_file(buffer, tmp, size);
	}
	Close(tmp);
	DeleteFile(tmpname);
	free(tmpname);
	return result;
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

uint32_t sys_execute(char *path, bool wait, BPTR input, BPTR output)
{
	LOG_DEBUG("Execute (%s, %s)", path, wait ? "true" : "false");
	assert(path != NULL);

	uint32_t result = 0;
	char *dot = strrchr(path, '.');
	if (dot) {
		// temporaly remove extension
		*dot = 0;
	}

	// decide what to do with the file
	struct DiskObject *dobj = GetDiskObject(path);
	if (dot) {
		// restore extension
		*dot = '.';
	}

	uint32_t stack = 0;
	char *tool = path;
	if (dobj) {
		// TODO: pass tool types
		// fetch .info metadata
		switch (dobj->do_Type) {
			case WBPROJECT:
				tool = dobj->do_DefaultTool;
				break;
			case WBTOOL:
				path = NULL;
				break;
		}
		stack = dobj->do_StackSize;
		if (stack < 4096) {
			stack = 4096;
		}
	} else {
		path = NULL;
	}

	sys_command(&g_command, tool, path, stack, wait);
	LOG_DEBUG("Execute: Command: ----8<----\n%s----8<----", g_command.data);
	if (!Execute(g_command.data, input, output)) {
		result = IoErr();
	}

	FreeDiskObject(dobj);
	return result;
}

// TODO: right now not working correctly
uint32_t sys_launchwb(struct DiskObject *dobj, char *path)
{
	buffer_t buffer;
	buffer_init(&buffer, 1, 128);

	LOG_DEBUG("LaunchWB (%p, %s)", dobj, path);
	for (unsigned char **it = dobj->do_ToolTypes; *it; it++) {
		char *tooltype = *it;
		LOG_DEBUG("LaunchWB: tooltype -> '%s'", tooltype);
	}

	// get full path to tool
	bool isProject = dobj->do_Type == WBPROJECT;
	char *tool = isProject ? (char *)dobj->do_DefaultTool : path;
	uint32_t result = sys_which(tool, &buffer);
	if (result) {
		LOG_DEBUG("LaunchWB: sys_which failed: %d", result);
		buffer_cleanup(&buffer);
		return result;
	}
	//if (((char *)buffer.data)[buffer.count - 1] == '\n') {
	//	buffer_pop_back(&buffer);
	//}
	buffer_append(&buffer, "", 1);

	tool = (char *)buffer.data;
	const char *toolfile = sys_filepart(tool);

	LOG_DEBUG("LaunchWB: Tool: '%s'; ToolFile: '%s'", tool, toolfile);

	/*
	BPTR tooldir = Lock(tool, ACCESS_READ);
	if (tooldir) {
		BPTR p = ParentDir(tooldir);
		UnLock(tooldir);
		tooldir = p;
	}

	BPTR argdir = 0;
	const char *argfile = NULL;
	(void)argfile;
	if (isProject) {
		argfile = sys_filepart(path);
		argdir = Lock(path, ACCESS_READ);
		if (argdir) {
			BPTR p = ParentDir(argdir);
			UnLock(argdir);
			argdir = p;
		}
		if (!argdir) {
			LOG_DEBUG("LaunchWB: argdir failed (%p, %s)", dobj, path);
			UnLock(tooldir);
			return IoErr();
		}
	}
	*/

	uint32_t stack = 0;
	if (dobj->do_Type == WBTOOL) {
		stack = dobj->do_StackSize;
	}
	if (stack < 4096) {
		// minimal stack size
		stack = 4096;
	}

	BPTR seglist = LoadSeg(tool);
	if (!seglist) {
		LOG_DEBUG("LaunchWB: LoadSeg(%s) failed", tool);
		//UnLock(tooldir);
		//UnLock(argdir);
		buffer_cleanup(&buffer);
		return IoErr();
	}

	struct WBStartup *msg = (struct WBStartup *)AllocMem(sizeof(struct WBStartup), MEMF_PUBLIC | MEMF_CLEAR);
	if (!msg) {
		//UnLock(tooldir);
		//UnLock(argdir);
		UnLoadSeg(seglist);
		buffer_cleanup(&buffer);
		return IoErr();
	}

	// initialize WBStartup message
	// allocate one item more, make it null (sentinel)
	int numArgs = 0;
	msg->sm_NumArgs = numArgs + 1;
	msg->sm_ArgList = (struct WBArg *)AllocMem(sizeof(struct WBArg) * (numArgs + 1), MEMF_PUBLIC | MEMF_CLEAR);

	// arg 0
	msg->sm_ArgList[0].wa_Lock = 0; /* Pass the lock directly */
	msg->sm_ArgList[0].wa_Name = NULL;
	//msg->sm_ArgList[0].wa_Name = (char *)AllocMem(strlen(toolfile) + 1, MEMF_PUBLIC);
	//strcpy(msg->sm_ArgList[0].wa_Name, toolfile);

	// arg 1
	//if (isProject) {
	//	msg->sm_ArgList[1].wa_Lock = argdir; /* Pass the lock directly */
	//	msg->sm_ArgList[1].wa_Name = (char *)AllocMem(strlen(argfile) + 1, MEMF_PUBLIC);
	//	strcpy(msg->sm_ArgList[1].wa_Name, argfile);
	//}

	/* Setup Message Header */
	msg->sm_Message.mn_Node.ln_Type = NT_MESSAGE;
	msg->sm_Message.mn_Length = sizeof(struct WBStartup);

	/* ReplyPort: Where the program replies when it exits.
	   Usually we create a port if we want to wait, or NULL if we fire-and-forget.
	   BUT: standard WB programs reply to this. We must handle it or memory leaks.
	   For a simple launcher, we often cheat and set ReplyPort to NULL
	   (the system will reclaim process resources, but message memory might leak).

	   PROPER WAY: Create a persistent background process (your "Workbench")
	   that listens to this port. For this snippet, we will set NULL
	   and rely on the OS to clean up the Process structure,
	   but we essentially "leak" the WBStartup struct until reboot.

	   *BETTER HACK:* Set mn_ReplyPort to a port you control, wait for it,
	   then free memory. If you want fire-and-forget, creating a tiny
	   "cleanup" task is required.
	*/
	msg->sm_Message.mn_ReplyPort = NULL;

	/* ToolTypes (Optional) */
	/* You would copy the char** array from the DiskObject here */
	msg->sm_ToolWindow = NULL;

	int32_t pri = 0;
	struct Process *proc = (struct Process *)CreateProc(toolfile, pri, seglist, stack);
	if (!proc) {
		// TODO: free allocated arguments
		FreeMem(msg, sizeof(struct WBStartup));
		//UnLock(tooldir);
		//UnLock(argdir);
		UnLoadSeg(seglist);
		buffer_cleanup(&buffer);
		return IoErr();
	}

	/* 5. Launch! */
	/* Put the message into the new process's Message Port */
	PutMsg(&proc->pr_MsgPort, (struct Message *)msg);

	// Clean up our local locks (duplicated in message)
	// No need to unlock the `argdir` or `tooldir` here
	buffer_cleanup(&buffer);
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
	ml->ml_ME[0].me_Addr = (APTR)memory;       /* Point to the start of our giant block */
	ml->ml_ME[0].me_Length = totalSize;    /* The total size to free */

	NewList(&task->tc_MemEntry);
	AddHead(&task->tc_MemEntry, &ml->ml_Node);

	// setup stack
	APTR stackptr = &memory->stack;
	task->tc_SPLower = stackptr;
	task->tc_SPUpper = (APTR)((ULONG)stackptr + stack);
	task->tc_SPReg   = task->tc_SPUpper; /* Stack grows downwards! */

	/* --- THE STACK MAGIC --- */
	uint32_t *sp = (uint32_t *)task->tc_SPUpper;
	*(--sp) = (uint32_t)user;   /* Push Arg 2 */
	*(--sp) = (uint32_t)task; /* Push Arg 1 */
	task->tc_SPReg = (APTR)sp;

	AddTask(task, func, NULL);
	return task;
}

static void sys_proc_enry()
{
	struct Process* self = (struct Process *)FindTask(NULL);
	fakeseg_t *seg = (fakeseg_t *)(((uint32_t *)BADDR(self->pr_SegList)) - 2);
	if (seg->data.func) {
		seg->data.func((struct Task *)self, seg->data.user);
	}
}

struct MsgPort *sys_spawnproc(taskfunc_t func, void *user, const char* name, int8_t prio, uint32_t stack)
{
	fakeseg_t *seg = (fakeseg_t *)AllocMem(sizeof(fakeseg_t), MEMF_PUBLIC | MEMF_CLEAR);
	if (!seg) {
		return NULL;
	}

	seg->dosSize = sizeof(fakeseg_t);
	seg->nextBPTR = 0;

	/* C. Build the M68k Trampoline */
	seg->jmpInstruction = 0x4EF9; /* 0x4EF9 is the 68000 opcode for JMP (Absolute) */
	seg->entry = sys_proc_enry;
	seg->data.func = func;
	seg->data.user = user;

	BPTR bptrSeg = (BPTR)((ULONG)&seg->jmpInstruction >> 2);
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
