#include <stdbool.h>

#include <proto/intuition.h>

#include "browser.h"

typedef struct browser_window browser_window_t;

struct browser_window
{
	struct Window *window;
	struct Region *region;
	buffer_t title;

	browser_t browser;

	bool closing : 1;
	bool closed : 1;
	bool tabulator : 1;
	bool active : 1;
	bool cursor_active : 1;

	int cursor;
	int maxRows;
	int offset;
	buffer_t columns;
	uint32_t names_hash;
};

bool browser_window_init(browser_window_t *window, const char* path, WORD LeftEdge, WORD TopEdge, WORD Width, WORD Height);
void browser_window_cleanup(browser_window_t *window);
bool browser_window_dispatch(uint32_t signal, browser_window_t *windows, int count);
uint32_t browser_window_wait(browser_window_t *window, int count);
