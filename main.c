/*

	AmiBroWser, blazing fast and memory thin commander for AmigaOS 1.3
	Copyright (C) 2026 by Michal 'Jendo' Jenikovsky

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

#include <stdint.h>
#include <stdarg.h>

#include <fcntl.h>
#include <unistd.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <workbench/startup.h>

#include "system.h"
#include "timer.h"
#include "log.h"
#include "buffer.h"
#include "browser.h"
#include "window.h"
#include "requester.h"

LOG_FACILITY(Main, LL_TRACE);

extern int    __argc;
extern char **__argv;
extern uint32_t __commandlen;
extern char *__commandline;

extern struct WBStartup *_WBenchMsg;

// Disable command line parsing
// argv and argc are null now
// TODO: maybe reimplement the buggy argument parsing
void __nocommandline(){};

int main(int argc, char *argv[])
{
	// init system objects
	(void)argc, (void)argv;
	struct Process *process = (struct Process *)FindTask(NULL);
	if (!_WBenchMsg && !process->pr_CLI) {
		return ERROR_INVALID_RESIDENT_LIBRARY;
	}

#ifndef NLOG
	if (!process->pr_CLI) {
		sys_attachconsole("AmiBroWser Debug", 0, 0, 600, 200);
	}
#endif // NLOG

	LOG_INFO(
		"AmiBroWser %s\n"
		"Copyright (c) 2026 by Michal 'Jendo' Jenikovsky\n"
		"This program comes with ABSOLUTELY NO WARRANTY.\n"
	, XSTR(GIT_VERSION));
	if (!sys_init()) {
		return RETURN_FAIL;
	}

	// NOTE: workdir doesn't need to be released by buffer_cleanup
	// workdir.data passed into window init function, will be released there
	buffer_t workdir;
	buffer_init(&workdir, 1, 256);
	sys_getpath(process->pr_CurrentDir, &workdir);
	buffer_append(&workdir, "", 1);
	LOG_DEBUG("CurrentDir: '%s'", workdir.data);

	requester_init();

	// process arguments
	if (process->pr_CLI) {
		// Parse argv and enter main processing loop.
		struct CommandLineInterface * cli = (struct CommandLineInterface *)BADDR(process->pr_CLI);
		char *cmdname = (char *)BADDR(cli->cli_CommandName);

		buffer_t buffer;
		buffer_init(&buffer, 1, 256);
		buffer_append(&buffer, "\"", 1);
		buffer_append(&buffer, cmdname + 1, *cmdname);
		buffer_append(&buffer, "\" ", 2);
		buffer_append(&buffer, __commandline, __commandlen);
		char *back = (char *)buffer_back(&buffer);
		if (*back == '\n') {
			*back = 0;
		} else {
			buffer_append(&buffer, "", 1);
		}

		LOG_DEBUG("CLI Args: '%s'", buffer.data);
		buffer_cleanup(&buffer);
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
			LOG_TRACE("Arg[%u] -> %p; '%s'", i, arg->wa_Lock, arg->wa_Name);
		}
	}

	WORD left = IntuitionBase->ActiveScreen->LeftEdge;
	WORD top = IntuitionBase->ActiveScreen->TopEdge;
	WORD width = IntuitionBase->ActiveScreen->Width / 2;
	WORD height = IntuitionBase->ActiveScreen->Height;

	buffer_t windows;
	buffer_init(&windows, sizeof(browser_window_t), 2);

	browser_window_t *w = (browser_window_t *)buffer_emplace_back(&windows);
	if (!w || !browser_window_init(w, (char *)workdir.data, true, left, top, width, height)) {
		LOG_ERROR("Failed to create first browser window!");
		buffer_pop_back(&windows);
	}

	w = (browser_window_t *)buffer_emplace_back(&windows);
	if (!w || !browser_window_init(w, NULL, false, left + width, top, width, height)) {
		LOG_ERROR("Failed to create second browser window!");
		buffer_pop_back(&windows);
	}

	if (windows.count) {
		w = (browser_window_t *)buffer_at(&windows, 0);
		ActivateWindow(w->window);

		LOG_INFO("Entering main loop");
		bool running = true;
		while (running) {
			browser_window_t *wins = (browser_window_t *)windows.data;
			uint32_t signal = browser_window_wait(wins, windows.count);
			running = browser_window_dispatch(signal, wins, windows.count);
		}
	} else {
		LOG_FATAL("Cannot create browser window!");
	}
	
	LOG_INFO("Shutdown");
	for (int i = 0; i < windows.count; i++) {
		browser_window_t *wins = (browser_window_t *)windows.data;
		browser_window_cleanup(wins + i);
	}

	buffer_cleanup(&windows);
	requester_cleanup();
	sys_cleanup();
	return RETURN_OK;
}
