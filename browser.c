#include <string.h>
#include <stdlib.h>

#include "browser.h"
#include "system.h"
#include "log.h"
#include "stb_sprintf.h"

typedef int (*comparer_t)(const void *, const void *);

LOG_FACILITY(Browser, LL_TRACE);

static int browser_item_compare_name(const void *first, const void *second)
{
	const fileinfo_t *a = (const fileinfo_t *)first;
	const fileinfo_t *b = (const fileinfo_t *)second;
	return strcmp(a->name, b->name);
}

static int browser_item_compare_type(const void *first, const void *second)
{
	const fileinfo_t *a = (const fileinfo_t *)first;
	const fileinfo_t *b = (const fileinfo_t *)second;
	int result = 0;
	if (sys_iscontainer(a->ctype)) {
		result -= 1;
	}
	if (sys_iscontainer(b->ctype)) {
		result += 1;
	}
	return result;
}

static int browser_item_compare_name_type(const void *first, const void *second)
{
	int type = browser_item_compare_type(first, second);
	if (type == 0) {
		return browser_item_compare_name(first, second);
	}
	return type;
}

static comparer_t browser_get_comparer(browser_ordering_t ordering)
{
	switch (ordering) {
		case BO_NAME:
			return &browser_item_compare_name;
		case BO_TYPE:
			return &browser_item_compare_type;
		case BO_NAME | BO_TYPE:
			return &browser_item_compare_name_type;
		default:
			return NULL;
	}
}

bool browser_init(browser_t *browser)
{
	LOG_DEBUG("Init (%p)", browser);
	buffer_init(&browser->listing, sizeof(fileinfo_t), 0);
	buffer_init(&browser->stack, sizeof(const char *), 0);
	browser->ordering = BO_NAME | BO_TYPE;
	browser->descending = false;
	browser->cursor = 0;
	browser->names_hash = 0;
	return true;
}

void browser_cleanup(browser_t *browser)
{
	LOG_DEBUG("Cleanup (%p)", browser);

	// free browsing stack path strings
	for (int i = 0; i < browser->stack.count; i++) {
		char **p = buffer_at(&browser->stack, i);
		if (p) {
			free(*p);
		}
	}

	// release buffers
	buffer_cleanup(&browser->listing);
	buffer_cleanup(&browser->stack);
}

bool browser_push(browser_t *browser, const char *path)
{
	LOG_DEBUG("Push (%p, %s)", browser, path);
	char **p = (char **)buffer_emplace(&browser->stack);
	if (p) {
		*p = strdup(path);
		// TODO: save cursor
		browser->cursor = 0;
		return browser_refresh(browser);
	}

	return false;
}

bool browser_pop(browser_t *browser)
{
	LOG_DEBUG("Pop (%p)", browser);
	char **p = (char **)buffer_top(&browser->stack);
	if (p) {
		free(*p);
		buffer_pop(&browser->stack);
		browser->cursor = 0; // TODO: restore cursor
		return browser_refresh(browser);
	}
	return false;
}

bool browser_refresh(browser_t *browser)
{
	LOG_TRACE("Refresh (%p)", browser);
	char **p = (char **)buffer_top(&browser->stack);

	LOG_TRACE("Listing BEGIN");
	uint32_t err = sys_listdir(p ? *p : NULL, &browser->listing);
	browser->names_hash = sys_hcombine((uint32_t)browser->listing.user, err);
	browser->error = err;
	LOG_TRACE("Listing END");

	if (err) {
		LOG_DEBUG("%d: %s", err, strerror(err));
		return false;
	}

	comparer_t comparer = browser_get_comparer(browser->ordering);
	if (comparer) {
		LOG_TRACE("Sort BEGIN (%p, %X)", browser, browser->ordering);
		qsort(browser->listing.data, browser->listing.count, browser->listing.size, comparer);
		LOG_TRACE("Sort END");
	}

	return true;
}

const char *browser_currentpath(browser_t *browser)
{
	LOG_TRACE("Currentpath (%p)", browser);
	char **top = (char **)buffer_top(&browser->stack);
	return top ? *top : NULL;
}

bool browser_move(browser_t *browser, int step)
{
	int newCursor = browser->cursor + step;
	if (newCursor < 0) {
		newCursor = 0;
	}
	if (newCursor >= browser->listing.count) {
		newCursor = browser->listing.count - 1;
	}

	bool changed = browser->cursor != newCursor;
	browser->cursor = newCursor;
	return changed;
}

bool browser_open(browser_t *browser, const char *path)
{
	if (path) {
		return browser_push(browser, path);
	} else {
		char buffer[STB_SPRINTF_MIN];
		fileinfo_t *info = (fileinfo_t *)buffer_at(&browser->listing, browser->cursor);
		if (!info) {
			return false;
		}
		const char *basePath = browser_currentpath(browser);
		const char *format = basePath ? "%s%s/" : "%s%s:";
		if (!basePath) {
			basePath = "";
		}
		stbsp_snprintf(buffer, sizeof(buffer), format, basePath, info->name);
		return browser_push(browser, buffer);
	}
}

const char* browser_error(browser_t *browser)
{
	return browser->error ? sys_ioerrmessage(browser->error) : NULL;
}
