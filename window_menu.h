#pragma once

#include <proto/intuition.h>

static const char g_title[] = "AmiBroWser";

static struct IntuiText textQuit = {
	-1, -1,         // FrontPen, BackPen
	COMPLEMENT,     // DrawMode
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
	-1, -1,         // FrontPen, BackPen
	COMPLEMENT,     // DrawMode
	0, 1,           // LeftEdge, TopEdge (relative to Item box)
	NULL,           // Font (NULL = default)
	"\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F",// The String
	NULL            // Next Text
};

/* "Address" Item */
static struct MenuItem itemSeparator1 = {
	&itemQuit,      // NextItem (End of list)
	0, 20,          // Left, Top (relative to Menu Bar)
	120, 10,        // Width, Height
	ITEMTEXT,       // Flags
	0,              // MutualExclude
	(APTR)&textSeparator,// ItemRender
	NULL,           // SelectRender
	0,              // Command Key (Right-Amiga + Q)
	NULL,           // SubItem
	0               // NextSelect
};

static struct IntuiText textLocation = {
	-1, -1,         // FrontPen, BackPen
	COMPLEMENT,     // DrawMode
	0, 1,           // LeftEdge, TopEdge (relative to Item box)
	NULL,           // Font (NULL = default)
	"Location",     // The String
	NULL            // Next Text
};

/* "Address" Item */
static struct MenuItem itemLocation = {
	&itemSeparator1,    // NextItem (End of list)
	0, 10,              // Left, Top (relative to Menu Bar)
	120, 10,            // Width, Height
	ITEMTEXT | COMMSEQ | ITEMENABLED | HIGHCOMP, // Flags
	0,                  // MutualExclude
	(APTR)&textLocation,// ItemRender
	NULL,               // SelectRender
	'L',                // Command Key (Right-Amiga + Q)
	NULL,               // SubItem
	0                   // NextSelect
};

static struct IntuiText textSwitch = {
	-1, -1,           // FrontPen, BackPen
	COMPLEMENT,       // DrawMode
	0, 1,             // LeftEdge, TopEdge (relative to Item box)
	NULL,             // Font (NULL = default)
	"Switch     TAB", // The String
	NULL              // Next Text
};

/* "Address" Item */
static struct MenuItem itemSwitch = {
	&itemLocation,   // NextItem (End of list)
	0, 0,            // Left, Top (relative to Menu Bar)
	120, 10,         // Width, Height
	ITEMTEXT | ITEMENABLED | HIGHCOMP, // Flags
	0,                // MutualExclude
	(APTR)&textSwitch,// ItemRender
	NULL,             // SelectRender
	0,                // Command Key (Right-Amiga + Q)
	NULL,             // SubItem
	0                 // NextSelect
};

static struct Menu mainMenu = {
	NULL,           // NextMenu (End of bar)
	0, 0,           // Left, Top
	100, 0,         // Width, Height (Width of "Project")
	MENUENABLED,    // Flags
	g_title,        // Menu Name
	&itemSwitch     // FirstItem -> Points to Quit
};
