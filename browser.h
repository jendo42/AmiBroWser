#pragma once

#include "buffer.h"
#include "list.h"

typedef struct browser browser_t;
typedef enum browser_ordering browser_ordering_t;
typedef struct browser_state browser_state_t;

enum browser_ordering
{
	BO_NONE = 0,
	BO_NAME = 1,
	BO_TYPE = 2
};

struct browser_state
{
	const char *path;
	int cursor;
	uint32_t hash;
	bool release_path : 1;
};

struct browser 
{
	buffer_t listing;
	buffer_t sorted;
	buffer_t stack;
	buffer_t message; // not null terminated!
	uint32_t error;
	uint32_t hash;

	browser_state_t *state;
	browser_ordering_t ordering;
	bool descending : 1;
};

bool browser_init(browser_t *browser, const char *path, bool release);
void browser_cleanup(browser_t *browser);

// Reloads browsing container from storage media.
bool browser_refresh(browser_t *browser);

// Changes the ordering of the container items. Calls `browser_refresh` internally.
void browser_ordering(browser_ordering_t flags);
// @returns Path on top of the history stack.
const char *browser_currentpath(browser_t *browser);
// @returns Pointer to error message, NULL if no error
const char* const browser_error(browser_t *browser);

// Changes cursor position (relative).
bool browser_move(browser_t *browser, int step);

// Opens the selected or specified file/directory in the browser,
// pushes on the browsing stack (preserving history)
bool browser_open(browser_t *browser, const char *path);

// Step one directory level up
bool browser_up(browser_t *browser);

bool browser_push(browser_t *browser, const char *path, bool release);
bool browser_pop(browser_t *browser);
