#include <assert.h>
#include <string.h>
#include <stdarg.h>

#include <proto/layers.h>
#include <proto/graphics.h>
#include <proto/exec.h>
#include <graphics/gfxmacros.h>

#include "system.h"
#include "log.h"

#include "window.h"
#include "buffer.h"

LOG_FACILITY(Window, LL_TRACE);

static const char g_title[] = "BroWser";

typedef struct column_size column_size_t;

struct column_size
{
	uint16_t start;
	uint16_t width;
};

static struct IntuiText textQuit = {
	0, -1,          // FrontPen, BackPen
	JAM1,           // DrawMode
	0, 1,           // LeftEdge, TopEdge (relative to Item box)
	NULL,           // Font (NULL = default)
	"Quit",         // The String
	NULL            // Next Text
};

/* "Quit" Item */
static struct MenuItem itemQuit = {
	NULL,           // NextItem (End of list)
	0, 30,          // Left, Top (relative to Menu Bar)
	120, 10,        // Width, Height
	ITEMTEXT | COMMSEQ | ITEMENABLED | HIGHCOMP, // Flags
	0,              // MutualExclude
	(APTR)&textQuit,// ItemRender
	NULL,           // SelectRender
	'Q',            // Command Key (Right-Amiga + Q)
	NULL,           // SubItem
	0               // NextSelect
};

static struct IntuiText textSeparator = {
	0, -1,          // FrontPen, BackPen
	JAM1,           // DrawMode
	0, 1,           // LeftEdge, TopEdge (relative to Item box)
	NULL,           // Font (NULL = default)
	"\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F",// The String
	NULL            // Next Text
};

/* "Address" Item */
static struct MenuItem itemSeparator1 = {
	&itemQuit,      // NextItem (End of list)
	0, 20,          // Left, Top (relative to Menu Bar)
	120, 10,         // Width, Height
	ITEMTEXT,       // Flags
	0,              // MutualExclude
	(APTR)&textSeparator,// ItemRender
	NULL,           // SelectRender
	0,              // Command Key (Right-Amiga + Q)
	NULL,           // SubItem
	0               // NextSelect
};

static struct IntuiText textLocation = {
	0, -1,          // FrontPen, BackPen
	JAM1,           // DrawMode
	0, 1,           // LeftEdge, TopEdge (relative to Item box)
	NULL,           // Font (NULL = default)
	"Location",         // The String
	NULL            // Next Text
};

/* "Address" Item */
static struct MenuItem itemLocation = {
	&itemSeparator1,// NextItem (End of list)
	0, 10,          // Left, Top (relative to Menu Bar)
	120, 10,         // Width, Height
	ITEMTEXT | COMMSEQ | ITEMENABLED | HIGHCOMP, // Flags
	0,              // MutualExclude
	(APTR)&textLocation,// ItemRender
	NULL,           // SelectRender
	'L',            // Command Key (Right-Amiga + Q)
	NULL,           // SubItem
	0               // NextSelect
};


static struct IntuiText textSwitch = {
	0, -1,          // FrontPen, BackPen
	JAM1,           // DrawMode
	0, 1,           // LeftEdge, TopEdge (relative to Item box)
	NULL,           // Font (NULL = default)
	"Switch     TAB",         // The String
	NULL            // Next Text
};

/* "Address" Item */
static struct MenuItem itemSwitch = {
	&itemLocation,// NextItem (End of list)
	0, 0,          // Left, Top (relative to Menu Bar)
	120, 10,         // Width, Height
	ITEMTEXT | ITEMENABLED | HIGHCOMP, // Flags
	0,              // MutualExclude
	(APTR)&textSwitch,// ItemRender
	NULL,           // SelectRender
	0,              // Command Key (Right-Amiga + Q)
	NULL,           // SubItem
	0               // NextSelect
};


static struct Menu mainMenu = {
	NULL,           // NextMenu (End of bar)
	0, 0,           // Left, Top
	120, 0,          // Width, Height (Width of "Project")
	MENUENABLED,    // Flags
	g_title,        // Menu Name
	&itemSwitch       // FirstItem -> Points to Quit
};

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

static void browser_window_row_dimm(browser_window_t *window, int *rowH, int *maxRows)
{
	int h = window->window->IFont->tf_YSize + 1;
	*maxRows = (window->window->Height - 20) / h;
	*rowH = h;
}

static void browser_window_item_rect(browser_window_t *window, struct Rectangle *safeRect, int index, struct Rectangle *rect)
{
	int maxRows, rowH;
	browser_window_row_dimm(window, &rowH, &maxRows);

	int column = index / maxRows;
	int row = index % maxRows;
	column_size_t *size = buffer_at(&window->columns, column);
	assert(size != NULL);
	
	rect->MinX = safeRect->MinX + 10 + size->start - 1;
	rect->MinY = safeRect->MinY + 3 + row * rowH;
	rect->MaxX = rect->MinX + size->width - 5;
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

static bool browser_window_refresh_cursor(browser_window_t *window)
{
	LOG_TRACE("RefreshCursor (%p)", window);
	if (window->closed || window->closing) {
		return false;
	}
	struct Window *win = (struct Window *)window->window;
	if (!win) {
		return false;
	}

	browser_t *browser = &window->browser;
	if (window->cursor == browser->cursor && window->cursor_active == window->active) {
		// no change, do nothing
		return false;
	}
	if (!browser->listing.count) {
		// no cursor in empty container
		return false;
	}

	struct RastPort *rp = win->RPort;
	struct Rectangle safeRect;

	browser_window_begin_paint(window, &safeRect);

	struct Rectangle cursorRect;
	browser_window_item_rect(window, &safeRect, browser->cursor, &cursorRect);
	//int viewOffset = -(cursorRect.MaxX - safeRect.MaxX);
	//if (viewOffset != window->offset) {
	//	window->offset = viewOffset;
	//	browser_window_end_paint(window);
	//	return true; // redraw request
	//}

	SetDrMd(rp, COMPLEMENT);

	// remove old cursor
	if (window->cursor >= 0) {
		struct Rectangle oldRect;
		browser_window_item_rect(window, &safeRect, window->cursor, &oldRect);
		if (window->cursor_active) {
			SetAfPt(rp, NULL, 0);
			RectFillRect(rp, &oldRect);
		} else {
			//SetDrPt(rp, 0xAAAA);
			//RectEmptyRect(rp, &oldRect);
			uint16_t ditherData[] = {
				0x5555, 0xAAAA
			};
			SetAfPt(rp, ditherData, 1);
			RectFillRect(rp, &oldRect);
		}
	}

	// draw new cursor
	struct Rectangle newRect;
	browser_window_item_rect(window, &safeRect, browser->cursor, &newRect);

	if (window->active) {
		SetAfPt(rp, NULL, 0);
		RectFillRect(rp, &newRect);
	} else {
		//SetDrPt(rp, 0xAAAA);
		//RectEmptyRect(rp, &newRect);
		uint16_t ditherData[] = {
			0x5555, 0xAAAA
		};
		SetAfPt(rp, ditherData, 1);
		RectFillRect(rp, &newRect);
	}

	window->cursor = browser->cursor;
	window->cursor_active = window->active;
	browser_window_end_paint(window);
	return false;
}

static bool browser_window_show_message(struct Window *window, const char *message)
{
	struct IntuiText body = { 2, 0, JAM1, 20, 10, NULL, (char *)message, NULL };
	struct IntuiText neg  = { 2, 0, JAM1, 6, 3, NULL, "OK", NULL };

	/* AutoRequest(Window, BodyText, PosText, NegText, PosFlags, NegFlags, Width, Height) */
	LOG_WARN("Message: %s", message);
	return AutoRequest(window, &body, NULL, &neg, 0, 0, -1, -1);
}

static void browser_window_set_title(browser_window_t *window, const char *format, ...)
{
	if (format) {
		va_list args;
		va_start(args, format);
		buffer_clear(&window->title);
		sys_vsprintf(&window->title, format, args);
		buffer_append(&window->title, "", 1);
		LOG_TRACE("Setting title: '%s'", window->title.data);
		SetWindowTitles(window->window, (STRPTR)window->title.data, g_title);
		va_end(args);
	} else {
		SetWindowTitles(window->window, "Loading...", g_title);
	}
}

static void browser_window_refresh(browser_window_t *window)
{
	LOG_TRACE("Refresh (%p)", window);
	if (window->closed || window->closing) {
		return;
	}
	struct Window *win = (struct Window *)window->window;
	if (!win) {
		return;
	}
	browser_t *browser = &window->browser;
	if (window->names_hash == browser->names_hash) {
		// no change in the listing
		return;
	}

	struct RastPort *rp = win->RPort;
	struct Rectangle safeRect;
	browser_window_begin_paint(window, &safeRect);

	// 2. Set the drawing mode 
	// JAM1 = only draw text; JAM2 = draw text and background color
	SetDrMd(rp, JAM1);
	SetAPen(rp, 0);
	SetBPen(rp, 0);
	RectFillRect(rp, &safeRect);

	const char *err = browser_error(browser);
	if (err) {
		SetAPen(rp, 3);
		Move(rp, safeRect.MinX + 10, safeRect.MinY + 10);
		const char *title = "Error opening:";
		Text(rp, title, strlen(title));
		Move(rp, safeRect.MinX + 10, safeRect.MinY + 20);
		Text(rp, err, strlen(err));
	}

	int maxRows, rowH;
	browser_window_row_dimm(window, &rowH, &maxRows);
	window->maxRows = maxRows;

	buffer_clear(&window->columns);

	// TODO: shift view
	int viewOffset = 0;
	int maxWidth = 0;
	for (int j = 0, k = 0; j < browser->listing.count; j += maxRows, k++) {
		int colWidth = 0;
		for (int i = 0; i < maxRows; i++) {
			int index = j+i;
			fileinfo_t *item = (fileinfo_t *)buffer_at(&browser->listing, index);
			if (!item) {
				continue;
			}

			const char *name = &item->name[0];
			uint16_t nameLen = item->len;

			Move(rp, safeRect.MinX + 10 + maxWidth + viewOffset, safeRect.MinY + 10 + i * rowH);

			// directory color
			if (sys_iscontainer(item->ctype)) {
				SetAPen(rp, 1);
			} else {
				SetAPen(rp, 2);
			}

			Text(rp, name, nameLen);
			int textLen = TextLength(rp, name, nameLen);
			if (textLen > colWidth) {
				colWidth = textLen;
			}
		}

		colWidth += 10;
		
		column_size_t *pCol = buffer_emplace(&window->columns);
		if (pCol) {
			pCol->start = maxWidth;
			pCol->width = colWidth;
		}

		maxWidth += colWidth;
	}

	browser_window_end_paint(window);

	// invalidate cursor
	window->cursor = -1;

	// mark window as updated
	window->names_hash = browser->names_hash;

	browser_window_refresh_cursor(window);
}

static const char * browser_window_current_path(browser_window_t *window)
{
	const char *current_path = browser_currentpath(&window->browser);
	return current_path ? current_path : "<Computer>";
}

static bool browser_window_open(browser_window_t *window, const char *path)
{
	browser_t *browser = &window->browser;
	browser_window_set_title(window, NULL);
	bool result = browser_open(browser, path);
	browser_window_refresh(window);
	browser_window_set_title(window, "%s", browser_window_current_path(window));
	return result;
}

static bool browser_window_back(browser_window_t *window)
{
	browser_t *browser = &window->browser;
	browser_window_set_title(window, NULL);
	bool result = browser_pop(browser);
	browser_window_refresh(window);
	browser_window_set_title(window, "%s", browser_window_current_path(window));
	return result;
}

static bool browser_window_input(browser_window_t *window, UWORD code, UWORD qualifier)
{
	LOG_TRACE("Input (%p) %X, %X", window, code, qualifier);
	(void)qualifier;
	if (window->closed || window->closing) {
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
			return browser_move(browser, -1);
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
			window->tabulator = true;
			break;
	}

	if (beep) {
		DisplayBeep(NULL);
	}

	return false;
}

void browser_window_ask_location(browser_window_t *window)
{
	(void)window;
}

bool browser_window_init(browser_window_t *window, const char* path, WORD LeftEdge, WORD TopEdge, WORD Width, WORD Height)
{
	LOG_DEBUG("Init (%p)", window);
	window->closing = false;
	window->closed = true;
	window->offset = 0;
	struct NewWindow nw = {
		LeftEdge, TopEdge,
		Width, Height,
		-1, -1,            /* DetailPen, BlockPen (-1 = Use Screen Defaults) */
		
		/* IDCMP Flags: Events we want to hear about */
		/* We only want to know when the Close Gadget is clicked */
		IDCMP_DISKINSERTED | IDCMP_DISKREMOVED | IDCMP_NEWSIZE | IDCMP_RAWKEY | IDCMP_MENUPICK | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,   
		
		/* Window Flags: Capabilities of the window */
		/* It has a close gadget, depth gadget, drag bar, and activates on opening */
		WFLG_DEPTHGADGET | WFLG_SIZEGADGET | WFLG_DRAGBAR | WFLG_ACTIVATE,
		
		NULL,				/* FirstGadget (User custom gadgets) */
		NULL,				/* CheckMark (Custom imagery) */
		"Starting...",		/* Window Title */
		NULL,				/* Screen (NULL = Workbench) */
		NULL,				/* BitMap (Custom bitmap) */
		
		100, 50,			/* MinWidth, MinHeight */
		-1, -1,				/* MaxWidth, MaxHeight */
		
		WBENCHSCREEN		/* Type (Open on Workbench) */
	};

	nw.Title = (STRPTR)g_title;

	// open window
	window->window = OpenWindow(&nw);
	if (!window->window) {
		LOG_WARN("Failed to 'OpenWindow' new browser window (%p)", window);
		return false;
	}
	window->window->UserData = (BYTE *)window;

	ClearMenuStrip(window->window);
	if (!SetMenuStrip(window->window, &mainMenu)) {
		LOG_WARN("Failed to set menu strip (%p)", window);
	}

	// init columns size list
	buffer_init(&window->columns, sizeof(column_size_t), 0);
	buffer_init(&window->title, 1, 16);

	// init browser
	if (!browser_init(&window->browser)) {
		LOG_ERROR("Failed to initialize browser (%p)", window);
		CloseWindow(window->window);
		return false;
	}

	bool br = path ? browser_push(&window->browser, path) : browser_refresh(&window->browser);
	if (!br) {
		LOG_WARN("Failed to do initial browser refresh (%p)", window);
	}

	// redraw window
	window->closed = false;
	window->names_hash = 0;
	browser_window_refresh(window);
	browser_window_set_title(window, "%s", browser_window_current_path(window));
	return true;
}

void browser_window_cleanup(browser_window_t *window)
{
	LOG_DEBUG("Cleanup (%p, %s)", window, window->closed ? "true" : "false");
	if (!window->closed) {
		if (window->window) {
			CloseWindow(window->window);
			window->window = NULL;
		}
		buffer_cleanup(&window->title);
		buffer_cleanup(&window->columns);
		browser_cleanup(&window->browser);
		window->closed = true;
		window->closing = false;
	}
}

bool browser_window_dispatch(uint32_t signal, browser_window_t *windows, int count)
{
	bool running = true;
	int processed = 0;
	for (int i = 0; i < count; i++) {
		browser_window_t *window = windows + i;
		if (window->closed) {
			continue;
		}

		struct Window *win = window->window;
		uint32_t mask = 1L << win->UserPort->mp_SigBit;
		if (~signal & mask) {
			continue;
		}

		struct MenuItem *menuItem;
		struct IntuiMessage *msg;
		while (!window->closing && (msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
			//LOG_TRACE("Message for (%p): %X, %X", window, msg->Class, (ULONG)msg->Code);
			switch (msg->Class) {
				case IDCMP_CLOSEWINDOW:
					window->closing = true;
					break;
				case IDCMP_DISKINSERTED:
				case IDCMP_DISKREMOVED:
					browser_refresh(&window->browser);
				case IDCMP_NEWSIZE:
					browser_window_refresh(window);
					break;
				case IDCMP_REFRESHWINDOW:
					BeginRefresh(win);
					browser_window_refresh(window);
					EndRefresh(win, true);
					break;
				case IDCMP_RAWKEY:
					if (browser_window_input(window, msg->Code, msg->Qualifier)) {
						if (browser_window_refresh_cursor(window)) {
							LOG_TRACE("Redrawing offset %d!", window->offset);
							window->names_hash = 0;
							browser_window_refresh(window);
						}
					}
					break;
				case IDCMP_MENUPICK:
					menuItem = ItemAddress(&mainMenu, msg->Code);
					if (menuItem == &itemQuit) {
						running = false;
					} else if (menuItem == &itemSwitch) {
						window->tabulator = true;
					} else if (menuItem == &itemLocation) {
						browser_window_ask_location(window);
					}
					break;
				case IDCMP_ACTIVEWINDOW:
					window->active = true;
					break;
				case IDCMP_INACTIVEWINDOW:
					window->active = false;
					break;
			}
			ReplyMsg((struct Message *)msg);
		}

		if (window->tabulator) {
			window->tabulator = false;
			int index = (i + 1) % count;
			browser_window_t * win = windows + index;
			ActivateWindow(win->window);
		}

		if (window->active != window->cursor_active) {
			browser_window_refresh_cursor(window);
		}

		if (window->closing && !window->closed) {
			LOG_DEBUG("Closing (%p)", window);
			browser_window_cleanup(window);
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
		if (!window->closed) {
			signalMask |= (1L << window->window->UserPort->mp_SigBit);
		}
	}

	return signalMask ? Wait(signalMask) : 0;
}
