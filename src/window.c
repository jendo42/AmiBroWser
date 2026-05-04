#include <string.h>
#include <stdarg.h>

#include <proto/layers.h>
#include <proto/graphics.h>
#include <proto/exec.h>
#include <graphics/gfxmacros.h>

#include "system.h"
#include "log.h"
#include "buffer.h"
#include "requester.h"
#include "assert.h"
#include "stb_sprintf.h"

#include "window.h"

LOG_FACILITY(Window, LL_INFO);

typedef struct column_size column_size_t;

struct column_size
{
	uint16_t start;
	uint16_t width;
};

#include "window_menu.h"

static void browser_window_safe_rect(browser_window_t *window, struct Rectangle *safeRect)
{
	struct Window *win = window->window;
	safeRect->MinX = win->BorderLeft;
	safeRect->MinY = win->BorderTop;
	safeRect->MaxX = (win->Width - win->BorderRight) - 1;
	safeRect->MaxY = (win->Height - win->BorderBottom) - 1;
}

static void browser_window_begin_paint(browser_window_t *window, struct Rectangle *safeRect)
{
	browser_window_safe_rect(window, safeRect);

	struct Region *newRegion = NewRegion();
	OrRectRegion(newRegion, safeRect);

	window->region = InstallClipRegion(window->window->WLayer, newRegion);
}

static void browser_window_end_paint(browser_window_t *window)
{
	struct Region *region = InstallClipRegion(window->window->WLayer, window->region);
	DisposeRegion(region);
	window->region = NULL;
}

static void browser_window_row_dimm(browser_window_t *window, uint16_t *rowH, uint16_t *colW, uint16_t *maxRows)
{
	uint16_t h = window->window->IFont->tf_YSize + 1;
	*maxRows = (window->window->Height - 20) / h;
	*colW = window->columnWidth;
	*rowH = h;
}

static void browser_window_item_rect(browser_window_t *window, struct Rectangle *safeRect, int index, struct Rectangle *rect)
{
	uint16_t maxRows, rowH, colW;
	browser_window_row_dimm(window, &rowH, &colW, &maxRows);

	int column = index / maxRows;
	int row = index % maxRows;
	uint32_t start = (colW + 10) * column;
	rect->MinX = safeRect->MinX + 5 + start - 1;
	rect->MinY = safeRect->MinY + 3 + row * rowH;
	rect->MaxX = rect->MinX + colW;
	rect->MaxY = rect->MinY + rowH - 1;
}

static void RectFillRect(struct RastPort *rp, struct Rectangle *rect)
{
	RectFill(rp, rect->MinX, rect->MinY, rect->MaxX, rect->MaxY);
}

static void RectEmptyRect(struct RastPort *rp, struct Rectangle *rect)
{
	WORD points[] = {
		rect->MinX, rect->MinY,
		rect->MaxX, rect->MinY,
		rect->MaxX, rect->MaxY,
		rect->MinX, rect->MaxY,
		rect->MinX, rect->MinY
	};
	/* 5 points because we must close the loop back to start */
	Move(rp, rect->MinX, rect->MinY);
	PolyDraw(rp, 5, points);
}

static void browser_window_set_title(browser_window_t *window, const char *format, ...)
{
	if (format) {
		va_list args;
		va_start(args, format);
		buffer_clear(&window->title);
		sys_vsprintf(&window->title, format, args);
		buffer_append_char(&window->title, 0);
		LOG_TRACE("Setting title: '%s'", window->title.data);
		SetWindowTitles(window->window, (STRPTR)window->title.data, g_title);
		va_end(args);
	} else {
		SetWindowTitles(window->window, "Loading...", g_title);
	}
}

static bool browser_window_refresh_cursor(browser_window_t *window)
{
	static uint16_t g_ditherData[] = {
		0x5555, 0xAAAA
	};

	LOG_TRACE("RefreshCursor (%p)", window);
	switch (window->opcode) {
		case BWO_CLOSED:
		case BWO_CLOSING:
			return false;
	}
	struct Window *win = (struct Window *)window->window;
	if (!win) {
		return false;
	}

	browser_t *browser = &window->browser;
	browser_state_t *state = browser->state;
	if (!state) {
		// no cursor when no browsing state
		return false;
	}

	bool cursor_active = !!(window->flags & BWF_CURSOR);
	bool active = !!(window->flags & BWF_ACTIVE);
	if (window->cursor == state->cursor && cursor_active == active) {
		// no change, do nothing
		return false;
	}
	if (!browser->listing.count) {
		// no cursor in empty container
		return false;
	}
	if (!browser->sorted.count) {
		// no sorted items
		return false;
	}

	PROFILE_START();

	struct RastPort *rp = win->RPort;
	struct Rectangle safeRect;

	browser_window_begin_paint(window, &safeRect);

	struct Rectangle newRect;
	browser_window_item_rect(window, &safeRect, state->cursor, &newRect);

	int viewOffset = window->offset;
	if (newRect.MinX < safeRect.MinX + viewOffset || newRect.MaxX > safeRect.MaxX + viewOffset) {
		viewOffset = newRect.MaxX - safeRect.MaxX + 10;
		if (viewOffset < 0) {
			viewOffset = 0;
		}
	}

	if (viewOffset != window->offset) {
		LOG_TRACE("viewOffset: %d; window->offset: %d", viewOffset, window->offset);
		window->offset = viewOffset;
		browser_window_end_paint(window);
		return true; // redraw request
	}

	newRect.MinX -= viewOffset;
	newRect.MaxX -= viewOffset;

	SetDrMd(rp, COMPLEMENT);

	// remove old cursor
	if (window->cursor >= 0) {
		struct Rectangle oldRect;
		browser_window_item_rect(window, &safeRect, window->cursor, &oldRect);
		oldRect.MinX -= viewOffset;
		oldRect.MaxX -= viewOffset;
		if (cursor_active) {
			SetAfPt(rp, NULL, 0);
			RectFillRect(rp, &oldRect);
		} else {
			SetAfPt(rp, g_ditherData, 1);
			RectFillRect(rp, &oldRect);
		}
	}

	// draw new cursor
	if (active) {
		SetAfPt(rp, NULL, 0);
		RectFillRect(rp, &newRect);
	} else {
		SetAfPt(rp, g_ditherData, 1);
		RectFillRect(rp, &newRect);
	}

	window->cursor = state->cursor;
	if (active) {
		window->flags |= BWF_CURSOR;
	} else {
		window->flags &= ~BWF_CURSOR;
	}

	browser_window_end_paint(window);

	PROFILE_END("RefreshCursor");
	return false;
}

static void browser_window_refresh(browser_window_t *window)
{
	LOG_TRACE("Refresh (%p)", window);
	switch (window->opcode) {
		case BWO_CLOSED:
		case BWO_CLOSING:
			return;
	}
	struct Window *win = (struct Window *)window->window;
	if (!win) {
		return;
	}
	browser_t *browser = &window->browser;
	LOG_INFO("Refresh: View: %.8X; Browser: %.8X", window->view_hash, browser->hash);
	if (window->view_hash == browser->hash) {
		// no change in the listing
		return;
	}

	PROFILE_START();

	struct RastPort *rp = win->RPort;
	struct Rectangle safeRect;
	browser_window_begin_paint(window, &safeRect);

	// 2. Set the drawing mode 
	// JAM1 = only draw text; JAM2 = draw text and background color
	SetDrMd(rp, JAM1);
	SetAPen(rp, 0);
	SetBPen(rp, 0);
	SetAfPt(rp, NULL, 0);

	// clear screen
	RectFillRect(rp, &safeRect);

	if (!browser->listing.count && browser->error) {
		const char *err = browser_error(browser);
		sys_sprintf(&browser->message, "Result: %s (%d)", err, browser->error);
	}

	// convert browser message text into intuition text lines
	if (browser->message.count) {
		// terminate the message by '\0'
		buffer_append_char(&browser->message, 0);
		requester_text2lines((char *)browser->message.data, &window->lines);
		buffer_clear(&browser->message);
	}

	// print IText lines
	if (window->lines.count) {
		PrintIText(rp, window->lines.data, safeRect.MinX, safeRect.MinY);
		goto end;
	}

	uint16_t maxRows, rowH, colW;
	browser_window_row_dimm(window, &rowH, &colW, &maxRows);
	window->maxRows = maxRows;

	uint16_t maxWidth = 0;
	uint16_t viewOffset = window->offset;
	LOG_TRACE("viewOffset: %d", viewOffset);

	int16_t baseX = safeRect.MinX + 5 - viewOffset;
	int16_t baseY = safeRect.MinY + 10;

	fileinfo_t **items = (fileinfo_t **)buffer_at(&browser->sorted, 0);
	uint16_t items_count = browser->sorted.count;
	uint16_t maxLen = window->columnChars;
	uint16_t wh = win->Height;
	assert(browser->listing.count == browser->sorted.count);

	// for each column
	maxWidth = 0;
	for (uint16_t j = 0; j < items_count; j += maxRows) {
		uint16_t colWidth = colW;
		int16_t currentX = baseX + maxWidth;
		int16_t currentY = baseY;
		int16_t nextX = currentX + colW;
		uint8_t currentPen = -1;

		// vertical bars, for each column
		SetAPen(rp, 1);
		Move(rp, nextX + 5, 0);
		Draw(rp, nextX + 5, wh);
		SetAPen(rp, 3);
		Move(rp, nextX + 4, 0);
		Draw(rp, nextX + 4, wh);
		SetAPen(rp, 2);
		Move(rp, nextX + 3, 0);
		Draw(rp, nextX + 3, wh);

		LOG_DEBUG("currentX: %d", currentX);
		if (currentX > safeRect.MaxX || nextX < safeRect.MinX) {
			// skip invisible columns
			colWidth += 10;
			maxWidth += colWidth;
			items += maxRows;
			continue;
		}

		// text items
		uint16_t end = j+maxRows;
		for (uint16_t i = j; i < end; i++) {
			if (i >= items_count) {
				break;
			}

			fileinfo_t *item = *items++;

			// move
			Move(rp, currentX, currentY);

			// directory color
			uint8_t targetPen;
			if (item->ficon || item->type == IT_VOL) {
				targetPen = 3;
			} else {
				targetPen = sys_iscontainer(item) ? 1 : 2;
			}
			if (currentPen != targetPen) {
				currentPen = targetPen;
				SetAPen(rp, currentPen);
			}

			// draw text
			const char *name = &item->name[0];
			uint16_t len = item->len;
			if (len > maxLen) {
				len = maxLen;
				char buffer[128];
				uint16_t toCopy = len - 1;
				const char *ext = strrchr(name, '.');
				if (ext) {
					uint16_t extLen = strlen(ext);
					if (extLen >= toCopy) {
						goto noext;
					}
					toCopy -= extLen;
					memcpy(buffer, name, toCopy);
					buffer[toCopy++] = '~';
					strcpy(buffer + toCopy, ext);
				} else { noext:
					memcpy(buffer, name, toCopy);
					buffer[toCopy++] = '~';
					buffer[toCopy] = 0;
				}
				Text(rp, buffer, len);
			} else {
				// visibility check
				int16_t textEnd = currentX + TextLength(rp, name, len);
				if (textEnd > safeRect.MinX) {
					Text(rp, name, len);
				}
			}

			currentY += rowH;
		}

		colWidth += 10;

		maxWidth += colWidth;
	}


end:
	browser_window_end_paint(window);

	// invalidate cursor
	window->cursor = -1;

	// mark window as updated
	window->view_hash = browser->hash;

	PROFILE_END("Refresh");
	browser_window_refresh_cursor(window);
}

static const char * browser_window_current_path(browser_window_t *window)
{
	const char *current_path = browser_currentpath(&window->browser);
	return current_path && *current_path ? current_path : "<Computer>";
}

static bool browser_window_open(browser_window_t *window, const char *path)
{
	browser_t *browser = &window->browser;
	browser_window_set_title(window, NULL);
	buffer_clear(&window->lines);
	bool result = browser_open(browser, path);
	const char *title = browser_window_current_path(window);
	if (!result) {
		LOG_ERROR("Failed to open '%s': %s", title, sys_ioerrmessage(browser->error));
	}
	window->offset = 0;
	browser_window_refresh(window);
	browser_window_set_title(window, "%s", title);
	return result;
}

static bool browser_window_back(browser_window_t *window)
{
	browser_t *browser = &window->browser;
	browser_window_set_title(window, NULL);
	buffer_clear(&window->lines);
	bool result = browser_pop(browser);
	browser_window_refresh_cursor(window);
	browser_window_refresh(window);
	browser_window_set_title(window, "%s", browser_window_current_path(window));
	return result;
}

static bool browser_window_input(browser_window_t *window, UWORD code, UWORD qualifier)
{
	switch (window->opcode) {
		case BWO_CLOSED:
		case BWO_CLOSING:
			return false;
	}
	struct Window *win = (struct Window *)window->window;
	if (!win) {
		return false;
	}
	browser_t *browser = &window->browser;
	bool beep = false;
	switch (code) {
		case 0x4C: // UP
			if (qualifier & IEQUALIFIER_RALT) {
				beep = browser->error || !browser_window_open(window, "/");
				break;
			} else {
				return browser_move(browser, -1);
			}
		case 0x4D: // DOWN
			return browser_move(browser, +1);
		case 0x4F: // LEFT
			return browser_move(browser, -window->maxRows);
		case 0x4E: // RIGHT
			return browser_move(browser, +window->maxRows);
		case 0x44: // ENTER
			beep = browser->error || !browser_window_open(window, NULL);
			break;
		case 0x41: // Backspace
			beep = !browser_window_back(window);
			break;
		case 0x42: // tabulator
			window->opcode = BWO_SWITCH;
			break;
		case 0x52: // F3
			if (!browser_open_by(&window->browser, window->viewer_path)) {
				requester_message(window->window, NULL, "Close", "Failed to start viewer tool '%s': %s\n\nPlease set correct tooltype in the icon.", window->viewer_path, sys_ioerrmessage(window->browser.error));
			}
			break;
		case 0x53: // F4
			if (!browser_open_by(&window->browser, window->editor_path)) {
				requester_message(window->window, NULL, "Close", "Failed to start editor tool '%s': %s\n\nPlease set correct tooltype in the icon.", window->editor_path, sys_ioerrmessage(window->browser.error));
			}
			break;
		default:
			//LOG_FATAL("Unhandled key: %X, %X", code, qualifier);
			break;
	}

	if (beep) {
		DisplayBeep(NULL);
	}

	return false;
}

void browser_window_ask_location(browser_window_t *window)
{
	char buffer[512] = {0};
	strncpy(buffer, browser_currentpath(&window->browser), sizeof(buffer) - 1);
	if (requester_text(window->window, g_title, "Enter location:", buffer, sizeof(buffer))) {
		browser_window_open(window, buffer);
	}
}

bool browser_window_init(browser_window_t *window, const char* path, bool path_release, WORD LeftEdge, WORD TopEdge, WORD Width, WORD Height, struct Screen *screen)
{
	LOG_DEBUG("Init (%p)", window);
	window->opcode = BWO_OPENING;
	window->offset = 0;
	window->flags = 0;

	// assign default viewer
	window->viewer_path = sys_matchtooltype("VIEWER");
	if (sys_isnullempty(window->viewer_path)) {
		// use MultiView on 3.0+
		window->viewer_path = "SYS:Utilities/MultiView";
		if (sys_exists(window->viewer_path) != ER_IS_FILE) {
			// fallback to More on 1.3 / 2.0
			window->viewer_path = "SYS:Utilities/More";
		}
	}
	LOG_INFO("Using '%s' as viewer tool", window->viewer_path);

	// assign default editor
	window->editor_path = sys_matchtooltype("EDITOR");
	if (sys_isnullempty(window->editor_path)) {
		window->editor_path = "C:Ed";
	}
	LOG_INFO("Using '%s' as editor tool", window->editor_path);

	bool customScreen = screen && screen != IntuitionBase->ActiveScreen;

	/* Window Flags: Capabilities of the window */
	/* It has a close gadget, depth gadget, drag bar */

	ULONG flags = WFLG_NEWLOOKMENUS;
	if (!customScreen) {
		flags |= WFLG_DEPTHGADGET | WFLG_SIZEGADGET | WFLG_DRAGBAR;
	}

	struct NewWindow nw = {
		LeftEdge, TopEdge,
		Width, Height,
		-1, -1,            /* DetailPen, BlockPen (-1 = Use Screen Defaults) */
		
		/* IDCMP Flags: Events we want to hear about */
		/* We only want to know when the Close Gadget is clicked */
		IDCMP_DISKINSERTED | IDCMP_DISKREMOVED | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_RAWKEY | IDCMP_MENUPICK | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW | IDCMP_NEWPREFS,
		flags,				/* Window Flags */

		NULL,				/* FirstGadget (User custom gadgets) */
		NULL,				/* CheckMark (Custom imagery) */
		"Starting...",		/* Window Title */
		screen,				/* Screen (NULL = Workbench) */
		NULL,				/* BitMap (Custom bitmap) */
		
		100, 50,			/* MinWidth, MinHeight */
		-1, -1,				/* MaxWidth, MaxHeight */

		/* Type (Open on Workbench) */
		customScreen ? CUSTOMSCREEN : WBENCHSCREEN
	};

	nw.Title = (STRPTR)g_title;

	// open window
	struct Window *win = OpenWindow(&nw);
	if (!win) {
		LOG_WARN("Failed to 'OpenWindow' new browser window (%p)", window);
		return false;
	}

	win->UserData = (BYTE *)window;
	window->window = win;

	ClearMenuStrip(win);
	if (!SetMenuStrip(win, &mainMenu)) {
		LOG_WARN("Failed to set menu strip (%p)", window);
	}

	buffer_init(&window->title, 1, 16);
	buffer_init(&window->lines, sizeof(struct IntuiText), 8);

	// init browser
	browser_t *browser = &window->browser;
	if (!browser_init(browser, path, path_release)) {
		LOG_ERROR("Failed to initialize browser (%p)", window);
		browser_cleanup(browser);
		CloseWindow(win);
		return false;
	}

	// redraw window
	window->opcode = BWO_OPENED;
	window->view_hash = 0;

	window->columnChars = 16;
	window->columnWidth = TextLength(win->RPort, "****************", 16);

	browser_window_refresh(window);
	browser_window_set_title(window, "%s", browser_window_current_path(window));
	return true;
}

void browser_window_cleanup(browser_window_t *window)
{
	LOG_DEBUG("Cleanup (%p, %u)", window, window->opcode);
	if (window->opcode != BWO_CLOSED) {
		if (window->window) {
			CloseWindow(window->window);
			window->window = NULL;
		}
		buffer_cleanup(&window->title);
		buffer_cleanup(&window->lines);
		browser_cleanup(&window->browser);
		window->opcode = BWO_CLOSED;
	}
}

bool browser_window_dispatch(uint32_t signal, browser_window_t *windows, int count)
{
	bool running = true;
	int processed = 0;
	for (int i = 0; i < count; i++) {
		browser_window_t *window = windows + i;
		if (window->opcode == BWO_CLOSED) {
			continue;
		}

		struct Window *win = window->window;
		uint32_t mask = 1L << win->UserPort->mp_SigBit;
		if (~signal & mask) {
			continue;
		}

		struct MenuItem *menuItem;
		struct IntuiMessage *msg;
		while (window->opcode == BWO_OPENED && (msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
			//LOG_TRACE("Message for (%p): %X, %X", window, msg->Class, (ULONG)msg->Code);
			switch (msg->Class) {
				case IDCMP_CLOSEWINDOW:
					window->opcode = BWO_CLOSING;
					break;
				case IDCMP_DISKINSERTED:
				case IDCMP_DISKREMOVED:
					browser_refresh(&window->browser);
					browser_window_refresh(window);
					break;
				case IDCMP_NEWPREFS:
				case IDCMP_NEWSIZE:
					window->view_hash = 0;
					browser_window_refresh(window);
					break;
				case IDCMP_REFRESHWINDOW:
					BeginRefresh(win);
					window->view_hash = 0;
					browser_window_refresh(window);
					EndRefresh(win, true);
					break;
				case IDCMP_RAWKEY:
					if (browser_window_input(window, msg->Code, msg->Qualifier)) {
						if (browser_window_refresh_cursor(window)) {
							LOG_TRACE("Redrawing offset %d!", window->offset);
							window->view_hash = 0;
							browser_window_refresh(window);
						}
					}
					break;
				case IDCMP_MENUPICK:
					menuItem = ItemAddress(&mainMenu, msg->Code);
					if (menuItem == &itemQuit) {
						running = false;
					} else if (menuItem == &itemSwitch) {
						window->opcode = BWO_SWITCH;
					} else if (menuItem == &itemLocation) {
						browser_window_ask_location(window);
					}
					break;
				case IDCMP_ACTIVEWINDOW:
					window->flags |= BWF_ACTIVE;
					break;
				case IDCMP_INACTIVEWINDOW:
					window->flags &= ~BWF_ACTIVE;
					break;
			}
			ReplyMsg((struct Message *)msg);
		}

		switch (window->opcode) {
			case BWO_SWITCH: {
				int index = (i + 1) % count;
				browser_window_t * win = windows + index;
				WindowToFront(win->window);
				ActivateWindow(win->window);
				window->opcode = BWO_OPENED;
				break;
			}
			case BWO_CLOSING:
				LOG_DEBUG("Closing (%p)", window);
				browser_window_cleanup(window);
				break;
		}

		if (!!(window->flags & BWF_ACTIVE) != !!(window->flags & BWF_CURSOR)) {
			browser_window_refresh_cursor(window);
		}

		++processed;
	}

	return running && processed;
}

uint32_t browser_window_wait(browser_window_t *windows, int count)
{
	uint32_t signalMask = 0;
	for (int i = 0; i < count; i++) {
		browser_window_t *window = windows + i;
		if (window->opcode != BWO_CLOSED) {
			signalMask |= (1L << window->window->UserPort->mp_SigBit);
		}
	}

	return signalMask ? Wait(signalMask) : 0;
}
