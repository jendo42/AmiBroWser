#pragma once

#include <stdbool.h>

#include <proto/intuition.h>

#include "browser.h"


// main thread operations that
// need access to all windows
enum bw_operation
{
	BWO_OPENING,
	BWO_OPENED,
	BWO_CLOSING,
	BWO_CLOSED,
	BWO_SWITCH,
	BWO_COPY,
};

enum bw_flags
{
	BWF_ACTIVE = 1,
	BWF_CURSOR = 2,
};

typedef struct browser_window browser_window_t;

struct browser_window
{
	struct Window *window;
	struct Region *region;

	buffer_t title;
	buffer_t lines;

	browser_t browser;

	const char * viewer_path;
	const char * editor_path;

	uint8_t opcode;
	uint8_t flags;

	int16_t cursor;
	int16_t offset;

	uint16_t maxRows;
	uint16_t columnWidth;
	uint16_t columnChars;

	uint32_t view_hash;
};

bool browser_window_init(browser_window_t *window, const char* path, bool path_release, WORD LeftEdge, WORD TopEdge, WORD Width, WORD Height, struct Screen *screen);
void browser_window_cleanup(browser_window_t *window);
bool browser_window_dispatch(uint32_t signal, browser_window_t *windows, int count);
uint32_t browser_window_wait(browser_window_t *window, int count);
