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
#include "glob.h"

LOG_FACILITY(Main, LL_INFO);

int main(int argc, char *argv[])
{
	(void)argc, (void)argv;

	if (!sys_init()) {
		return ERROR_INVALID_RESIDENT_LIBRARY;
	}

#ifndef NLOG
	{
		struct Process *process = (struct Process *)FindTask(NULL);
		const char *debug = sys_matchtooltype("DEBUG");
		if (!process->pr_CLI && debug) {
			sys_attachconsole("AmiBroWser Debug", 0, 0, 600, 200);
		}
		buffer_t buffer;
		loglevel_t defaultLevel = log_parselevel(debug);
		buffer_init(&buffer, 1, 64);
		for (logfacility_t **it = log_facilitylist(); *it; it++) {
			logfacility_t *facility = *it;
			buffer_clear(&buffer);
			sys_sprintf(&buffer, "LOG:%s", facility->name);
			buffer_append_char(&buffer, 0);
			const char *value = sys_matchtooltype(buffer.data);
			loglevel_t level = log_parselevel(value);
			if (level != LL_UNKNOWN) {
				facility->level = level;
			} else if (defaultLevel != LL_UNKNOWN) {
				facility->level = defaultLevel;
			}
		}
		buffer_cleanup(&buffer);
	}
#endif // NLOG

	LOG_INFO(
		"AmiBroWser %s\n"
		"Copyright (c) 2026 by Michal 'Jendo' Jenikovsky\n"
		"This program comes with ABSOLUTELY NO WARRANTY."
	, XSTR(GIT_VERSION));

	requester_init();

	struct Screen *screen = IntuitionBase->ActiveScreen;
	struct Screen *customScreen = NULL;

	// if user wants to run on custom screen
	if (sys_matchtooltype("CUSTOMSCREEN")) { /* 1. Define the parameters of your custom screen */
		struct NewScreen ns = {
			0, 0,                   /* LeftEdge, TopEdge (Always 0,0 for screens) */
			/* Width, Height */
			screen->Width, screen->Height,
			2,                      /* Depth (3 bitplanes = 8 colors) */
			/* DetailPen, BlockPen (Colors for text/blocks) */
			screen->DetailPen, screen->BlockPen,
			HIRES,                  /* ViewModes (0 = LowRes. Use HIRES for 640x200) */
			CUSTOMSCREEN,           /* Type: This tells the OS to make a new one! */
			NULL,                   /* Font: NULL uses the system default (Topaz) */
			"AmiBroWser Screen",    /* DefaultTitle: THE DRAG HANDLE! */
			NULL,                   /* Gadgets: No custom screen gadgets yet */
			NULL                    /* CustomBitMap: Let Intuition allocate the RAM */
		};

		/* 2. Ask Intuition to build it and program the Copper! */
		customScreen = OpenScreen(&ns);
		if (customScreen) {
			screen = customScreen;
		} else {
			LOG_ERROR("Failed to create custom screen!");
		}
	}

	WORD left = IntuitionBase->ActiveScreen->LeftEdge;
	WORD top = IntuitionBase->ActiveScreen->TopEdge + screen->BarHeight;
	WORD width = IntuitionBase->ActiveScreen->Width / 2;
	WORD height = IntuitionBase->ActiveScreen->Height - screen->BarHeight;

	buffer_t windows;
	buffer_init(&windows, sizeof(browser_window_t), 2);

	browser_window_t *w = (browser_window_t *)buffer_emplace_back(&windows);
	if (!w || !browser_window_init(w, sys_workdirpath(), false, left, top, width, height, screen)) {
		LOG_ERROR("Failed to create first browser window!");
		buffer_pop_back(&windows);
	}

	w = (browser_window_t *)buffer_emplace_back(&windows);
	if (!w || !browser_window_init(w, NULL, false, left + width, top, width, height, screen)) {
		LOG_ERROR("Failed to create second browser window!");
		buffer_pop_back(&windows);
	}

	if (windows.count) {
		w = (browser_window_t *)buffer_at(&windows, 0);
		ActivateWindow(w->window);

		LOG_INFO("Entering main loop");
		bool running = true;
		do {
			browser_window_t *wins = (browser_window_t *)windows.data;
			uint32_t signal = browser_window_wait(wins, windows.count);
			running = browser_window_dispatch(signal, wins, windows.count);
		} while (running);
	} else {
		LOG_FATAL("Cannot create browser window!");
	}
	
	LOG_INFO("Shutdown");
	for (int i = 0; i < windows.count; i++) {
		browser_window_t *wins = (browser_window_t *)windows.data;
		browser_window_cleanup(wins + i);
	}

	if (customScreen) {
		LOG_DEBUG("Custom screen cleanup");
		CloseScreen(customScreen);
	}

	buffer_cleanup(&windows);
	requester_cleanup();
	sys_cleanup();
	return RETURN_OK;
}
