/*
 * Hello world using AmigaOS API.
 */

#include <stdint.h>
#include <stdio.h>
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

LOG_FACILITY(Main, LL_TRACE);

int main(int argc, char *argv[])
{
	LOG_INFO("BroWser v0.1 by Jendo");

	// init system objects
	if (!sys_init()) {
		return RETURN_FAIL;
	}

	// process arguments
	if (argc > 0) {
		// Parse argv and enter main processing loop.
		LOG_DEBUG("Arguments: ");
		for (int i = 0; i < argc; i++) {
			LOG_DEBUG(" -> '%s'", argv[i]);
		}
	} else if (argc == 0) {
		// Parse wbstartup and enter main processing loop.
		struct WBStartup *wbstartup = (struct WBStartup *)argv;
		LOG_DEBUG("WBStartup: %p", wbstartup);
		(void)wbstartup;
	} else {
		return RETURN_FAIL;
	}

	WORD left = IntuitionBase->ActiveScreen->LeftEdge;
	WORD top = IntuitionBase->ActiveScreen->TopEdge;
	WORD width = IntuitionBase->ActiveScreen->Width / 2;
	WORD height = IntuitionBase->ActiveScreen->Height;

	buffer_t windows;
	buffer_init(&windows, sizeof(browser_window_t), 2);

	browser_window_t *w = (browser_window_t *)buffer_emplace(&windows);
	if (!w || !browser_window_init(w, NULL, left, top, width, height)) {
		LOG_ERROR("Failed to create first browser window!");
		buffer_pop(&windows);
	}
	
	w = (browser_window_t *)buffer_emplace(&windows);
	if (!w || !browser_window_init(w, "SYS:", left + width, top, width, height)) {
		LOG_ERROR("Failed to create second browser window!");
		buffer_pop(&windows);
	}

	if (windows.count) {
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
	sys_cleanup();

	LOG_INFO("Exit");
	return RETURN_OK;
}
