#include <string.h>
#include <stdlib.h>

#include "system.h"
#include "log.h"
#include "sort.h"
#include "assert.h"

#include "browser.h"

LOG_FACILITY(Browser, LL_INFO);

static int __regargs browser_item_compare(const void *first, const void *second, void *user)
{
	const browser_t *browser = (const browser_t *)user;
	const fileinfo_t *a = (const fileinfo_t *)first;
	const fileinfo_t *b = (const fileinfo_t *)second;
	browser_ordering_t ordering = browser->ordering;
	int result = 0;
	if (ordering & BO_TYPE) {
		if (sys_iscontainer(a->ctype)) {
			result -= 1;
		}
		if (sys_iscontainer(b->ctype)) {
			result += 1;
		}
	}
	if (ordering & BO_ICON && !result) {
		if (a->ficon) {
			result -= 1;
		}
		if (b->ficon) {
			result += 1;
		}
	}
	if (ordering & BO_NAME && !result) {
		result = strcmp(a->name, b->name);
	}
	return result;
}

static bool browser_isseparator(char ch)
{
	switch (ch) {
		case ':':
		case '/':
			return true;
		default:
			return false;
	}
}

const char* const browser_error(browser_t *browser)
{
	return sys_ioerrmessage(browser->error);
}

bool browser_init(browser_t *browser, const char *path, bool release)
{
	LOG_DEBUG("Init (%p)", browser);
	buffer_init(&browser->listing, sizeof(fileinfo_t), 32);
	buffer_init(&browser->stack, sizeof(browser_state_t), 4);
	buffer_init(&browser->sorted, sizeof(fileinfo_t *), 32);
	buffer_init(&browser->message, 1, 32);
	browser->error = 0;
	browser->hash = 0;
	browser->state = NULL;
	browser->ordering = BO_NAME | BO_TYPE | BO_ICON;
	browser->descending = false;
	return browser_push(browser, path, release);
}

void browser_cleanup(browser_t *browser)
{
	LOG_DEBUG("Cleanup (%p)", browser);

	// free browsing stack path strings
	for (int i = 0; i < browser->stack.count; i++) {
		browser_state_t *state = (browser_state_t *)buffer_at(&browser->stack, i);
		if (state->release_path && state->path) {
			free((void *)state->path);
		}
	}

	// release buffers
	buffer_cleanup(&browser->listing);
	buffer_cleanup(&browser->stack);
	buffer_cleanup(&browser->sorted);
	buffer_cleanup(&browser->message);
}

bool browser_push(browser_t *browser, const char *path, bool release)
{
	LOG_DEBUG("Push (%p, %s)", browser, path);
	browser_state_t *state = (browser_state_t *)buffer_emplace_back(&browser->stack);
	if (!state) {
		return false;
	}

	// do not allow empty string to be pushed
	if (path && !path[0]) {
		// free precious memory
		if (release) {
			release = false;
			free((void *)path);
		}
		path = NULL;
	}

	state->path = path;
	state->release_path = release;
	state->cursor = 0;
	browser->state = state;

	return browser_refresh(browser);
}

bool browser_pop(browser_t *browser)
{
	LOG_DEBUG("Pop (%p)", browser);
	if (!browser->state) {
		return false;
	}
	if (browser->stack.count <= 1) {
		return false;
	}
	browser_state_t *state = browser->state;
	if (state->release_path && state->path) {
		free((void *)state->path);
	}

	buffer_pop_back(&browser->stack);

	browser->state = buffer_back(&browser->stack);
	return browser_refresh(browser);
}

bool browser_refresh(browser_t *browser)
{
	uint32_t err;
	LOG_TRACE("Refresh (%p)", browser);
	buffer_clear(&browser->listing);
	buffer_clear(&browser->sorted);

	browser_state_t *state = browser->state;
	if (!state) {
		browser->error = ERROR_NO_MORE_ENTRIES;
		browser->hash = sys_hcombine(-1, ERROR_NO_MORE_ENTRIES);
		return false;
	}

	PROFILE_START();
	err = sys_listdir(state->path, &browser->listing);
	browser->hash = sys_hcombine((uint32_t)browser->listing.user, err);
	browser->hash = sys_hcombine(browser->hash, browser->listing.count);
	browser->error = err;
	PROFILE_END("Refresh/sys_listdir");

	if (err) {
		LOG_DEBUG("Refresh: %d: %s", err, sys_ioerrmessage(err));
		return false;
	}

	// prepare sorted array
	if (buffer_resizec(&browser->sorted, browser->listing.count)) {
		fileinfo_t **table = (fileinfo_t **)browser->sorted.data;
		fileinfo_t *data = (fileinfo_t *)browser->listing.data;
		for (uint16_t i = 0; i < browser->sorted.count; i++) {
			table[i] = data++;
		}
	}

	browser_ordering_t ordering = browser->ordering;
	if (ordering) {
		PROFILE_START();
		quicksort((void **)browser->sorted.data, browser->sorted.count, &browser_item_compare, browser);
		PROFILE_END("Refresh/quicksort");
	}

	LOG_TRACE("Refresh: 'listing' has %u bytes capacity", browser->listing.capacity);
	LOG_TRACE("Refresh: 'sorted' has %u bytes capacity", browser->sorted.capacity);
	return true;
}

const char *browser_currentpath(browser_t *browser)
{
	LOG_TRACE("Currentpath (%p, %p)", browser, browser->state);
	browser_state_t *state = browser->state;
	return state ? state->path : NULL;
}

bool browser_move(browser_t *browser, int step)
{
	browser_state_t *state = browser->state;
	if (!state) {
		return false;
	}

	int newCursor = state->cursor + step;
	if (newCursor < 0) {
		newCursor = 0;
	}
	if (newCursor >= browser->sorted.count) {
		newCursor = browser->sorted.count - 1;
	}

	bool changed = state->cursor != newCursor;
	state->cursor = newCursor;
	return changed;
}

bool browser_open(browser_t *browser, const char *path)
{
	bool valid = false;
	fileinfo_t info;
	browser_state_t *state = browser->state;
	assert(state != NULL);

	// prepare path as malloced string
	if (path) {
		// amiga standard level-up
		if (path[0] == '/' && path[1] == 0) {
			return browser_up(browser);
		}

		path = strdup(path);
		if (!path) {
			LOG_ERROR("Failed to strdup 'path' !");
			return false;
		}

		uint32_t err = sys_examine(path, &info);
		valid = !err;
	} else {
		fileinfo_t **pinfo = (fileinfo_t **)buffer_at(&browser->sorted, state->cursor);
		assert(pinfo != NULL);

		// we have to copy the item to local memory
		// because the browser->listing will change later
		// when browser_push() is called
		info = **pinfo;
		valid = true;

		// generate full path for selected file
		const char *basePath = browser_currentpath(browser);
		const char *format = basePath ? "%s%s/" : "%s%s:";
		if (!basePath) {
			basePath = "";
		}

		LOG_TRACE("Open: basePath: '%s', info->name: '%s'", basePath, info.name);

		buffer_t buffer;
		buffer_init(&buffer, 1, 128);
		sys_sprintf(&buffer, format, basePath, info.name);
		if (info.ctype == CT_NONE) {
			// for file remove trailing '/' or ':'
			buffer_pop_back(&buffer);
		}

		// null-terminate
		buffer_append(&buffer, "", 1);

		// full path generated
		// NOTE: path must be malloc-ed string here,
		// will be freed on browser_pop(). No need to cleanup the buffer here.
		path = buffer.data;
	}

	// NOTE: path should be malloc-ed here
	if (valid) {
		LOG_INFO("Opening: CT%u '%s'", (int)info.ctype, path);
	}
	bool result = browser_push(browser, path, true);

	// selected item is file
	if (valid && info.ctype == CT_NONE) {
		char *tmpname = NULL;
		BPTR out = sys_tmpfile(&tmpname);

		// change current directory to path
		// TODO:
		//sys_changedir(state->path);

		uint32_t err = sys_execute((char *)path, true, 0, out);
		browser->error = err;

		Seek(out, 0, OFFSET_END);
		uint32_t size = Seek(out, 0, OFFSET_BEGINNING);
		buffer_clear(&browser->message);
		buffer_append_file(&browser->message, out, size);
		Close(out);
		DeleteFile(tmpname);
		free(tmpname);

		//systimeval_t time;
		//sys_gettime(&time);
		browser->hash = sys_djb2(browser->message.data, browser->message.count);
		browser->hash = sys_hcombine(browser->hash, err);
		//browser->hash = sys_hcombine(browser->hash, time.tv_sec ^ time.tv_usec);
		LOG_TRACE("Open: '%s' exited with code %d; Hash: %.8X", path, err, browser->hash);
		return err ? false : true;
	}

	// selected item is container
	return result;
}

bool browser_up(browser_t *browser)
{
	char *path = (char *)browser_currentpath(browser);
	if (!path) {
		return false;
	}
	if (!*path) {
		return false;
	}

	// make copy
	path = strdup(path);

	// odjeb the last part
	int len = strlen(path);
	char *end = path + len - 1;
	if (browser_isseparator(*end)) {
		// skip trailing separator
		end--;
	}
	// skip level
	for ( ; end > path; end--) {
		if (*end == ':' || *end == '/') {
			break;
		}
	}
	// adjust to preserve trailing symbol
	if (browser_isseparator(*end)) {
		end++;
	}
	// odjeb the part away, path is dupped so will be released with browser_pop
	*end = 0;

	return browser_push(browser, path, true);
}
