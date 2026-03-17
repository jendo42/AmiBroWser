#include <intuition/intuition.h>
#include <proto/exec.h>
#include <string.h>
#include <stdarg.h>

#include "log.h"
#include "requester.h"
#include "buffer.h"
#include "system.h"

LOG_FACILITY(Requester, LL_INFO);

static buffer_t g_buffer;
static buffer_t g_lines;


void requester_init()
{
	buffer_init(&g_buffer, 1, 256);
	buffer_init(&g_lines, sizeof(struct IntuiText), 8);
}

void requester_cleanup()
{
	buffer_cleanup(&g_lines);
	buffer_cleanup(&g_buffer);
}

int requester_text2lines(char *text, buffer_t *lines)
{
	// loop for each line
	struct IntuiText *prevline = NULL;
	uint16_t top = 10;
	int cnt = 0;
	buffer_clear(lines);
	for (char *line = text, *nextline = strchr(line, '\n'); line; cnt++, nextline = line ? strchr(line, '\n') : NULL) {
		if (nextline) {
			*nextline++ = 0;
		}

		struct IntuiText* iline = (struct IntuiText*)buffer_emplace_back(lines);
		iline->FrontPen = 1;
		iline->BackPen = 0;
		iline->DrawMode = JAM1;
		iline->LeftEdge = 20;
		iline->TopEdge = top;
		iline->ITextFont = NULL;
		iline->IText = line;
		iline->NextText = NULL;

		if (prevline) {
			prevline->NextText = iline;
		}

		prevline = iline;
		// TODO: read from actual window font
		top += 10;
		line = nextline;
	}

	return cnt;
}

bool requester_message(struct Window *window, const char *positive, const char *negative, const char *format, ...)
{
	struct IntuiText pos  = { 2, 0, JAM1, 6, 3, NULL, (char *)positive, NULL };
	struct IntuiText neg  = { 2, 0, JAM1, 6, 3, NULL, (char *)negative, NULL };
	bool result = false;

	va_list args;
	va_start(args, format);
	buffer_clear(&g_buffer);
	sys_vsprintf(&g_buffer, format, args);
	// null-terminated message, multiline
	buffer_append(&g_buffer, "", 1);

	char *message = (char*)g_buffer.data;
	LOG_WARN("Message: %s", message);
	requester_text2lines(message, &g_lines);

	/* AutoRequest(Window, BodyText, PosText, NegText, PosFlags, NegFlags, Width, Height) */
	struct IntuiText *body = (struct IntuiText*)buffer_at(&g_lines, 0);
	if (body) {
		result = AutoRequest(window, body, positive ? &pos : NULL, negative ? &neg : NULL, 0, 0, -1, -1) == TRUE;
	}

	va_end(args);
	return result;
}

bool requester_text(const char *message, char *buffer, int len)
{
	struct Window *win;
	struct Gadget edit;
	struct Gadget btnok;
	struct Gadget btncancel;
	struct IntuiText prompt;
	struct IntuiText accept;
	struct IntuiText reject;
	struct StringInfo value;
	struct IntuiMessage *msg;

	/* 1. Define the vertices for a box (Relative to Gadget Top-Left) */
	/* Order: TopLeft -> TopRight -> BottomRight -> BottomLeft -> Close Loop */
	/* Coordinates: X, Y */
	WORD vectors[] = {
		-2, -2,      /* Start slightly outside top-left */
		422, -2,     /* Top Edge (Width + 2) */
		422, 10,     /* Right Edge (Height + 2) */
		-2, 10,      /* Bottom Edge */
		-2, -2       /* Close the loop */
	};

	/* 2. Define the Border Structure */
	struct Border border = {
		0, 0,              /* LeftEdge, TopEdge (Offset from Gadget) */
		1, 0,              /* FrontPen, BackPen (1 = White/Black usually) */
		JAM1,              /* DrawMode */
		5,                 /* Count (Number of points) */
		vectors,     /* Pointer to the XY array above */
		NULL               /* NextBorder (for linking multiple borders) */
	};

	/* Setup the OK Button Text */
	reject.FrontPen = 1; reject.BackPen = 0;
	reject.DrawMode = JAM1;
	reject.LeftEdge = 6; reject.TopEdge = 4;
	reject.ITextFont = NULL;
	reject.IText = "Cancel";
	reject.NextText = NULL;

	/* Setup the Cancel Gadget (Button) */
	btncancel.NextGadget = NULL;
	btncancel.LeftEdge = 380; btncancel.TopEdge = 40;
	btncancel.Width = 60; btncancel.Height = 15;
	btncancel.Flags = GADGHCOMP;
	btncancel.Activation = RELVERIFY | ENDGADGET | GADGIMMEDIATE;
	btncancel.GadgetType = BOOLGADGET;
	btncancel.GadgetRender = NULL;
	btncancel.SelectRender = NULL;
	btncancel.GadgetText = &reject;
	btncancel.MutualExclude = 0;
	btncancel.SpecialInfo = NULL;
	btncancel.GadgetID = 3;
	btncancel.UserData = NULL;

	/* Setup the OK Button Text */
	accept.FrontPen = 1; accept.BackPen = 0;
	accept.DrawMode = JAM1;
	accept.LeftEdge = 6; accept.TopEdge = 4;
	accept.ITextFont = NULL;
	accept.IText = "OK";
	accept.NextText = NULL;

	/* Setup the OK Gadget (Button) */
	btnok.NextGadget = &btncancel;
	btnok.LeftEdge = 10; btnok.TopEdge = 40;
	btnok.Width = 40; btnok.Height = 15;
	btnok.Flags = GADGHCOMP;
	btnok.Activation = RELVERIFY | ENDGADGET | GADGIMMEDIATE;
	btnok.GadgetType = BOOLGADGET;
	btnok.GadgetRender = NULL;
	btnok.SelectRender = NULL;
	btnok.GadgetText = &accept;
	btnok.MutualExclude = 0;
	btnok.SpecialInfo = NULL;
	btnok.GadgetID = 2;
	btnok.UserData = NULL;

	/* Setup the StringInfo (The logic of the text box) */
	value.Buffer = (UBYTE *)buffer;
	value.UndoBuffer = NULL;
	value.MaxChars = len;
	value.DispPos = 0;
	value.NumChars = strlen(buffer);
	value.BufferPos = value.NumChars;

	/* Setup the Prompt Text */
	prompt.FrontPen = 1; prompt.BackPen = 0;
	prompt.DrawMode = JAM1;
	prompt.LeftEdge = 0; prompt.TopEdge = -12;
	prompt.ITextFont = NULL;
	prompt.IText = (UBYTE *)message;
	prompt.NextText = NULL;

	/* Setup the String Gadget (Text Input) */
	/* IMPORTANT: Link it to the OK gadget */
	edit.NextGadget = &btnok;
	edit.LeftEdge = 10; edit.TopEdge = 26;
	edit.Width = 420; edit.Height = 10;
	edit.Flags = GADGHCOMP;
	edit.Activation = RELVERIFY | ENDGADGET | GADGIMMEDIATE; /* ENDGADGET means pressing Enter closes it */
	edit.GadgetType = STRGADGET;
	edit.GadgetRender = &border; /* Standard box */
	edit.SelectRender = NULL;
	edit.GadgetText = &prompt;
	edit.MutualExclude = 0;
	edit.SpecialInfo = (APTR)&value;
	edit.GadgetID = 1;
	edit.UserData = NULL;

	/* Define the Window */
	struct NewWindow nw = {
		100, 80, 450, 60,       /* Left, Top, Width, Height */
		0, 1,                   /* Pens */
		GADGETUP | CLOSEWINDOW | IDCMP_INACTIVEWINDOW, /* IDCMP Flags */
		WINDOWCLOSE | WFLG_ACTIVATE | WFLG_DRAGBAR | WFLG_RMBTRAP,
		&edit,                  /* Point to the FIRST gadget */
		NULL,
		"User Input",           /* Title */
		NULL, NULL,
		260, 60, 260, 60,       /* Min/Max dimensions (Fixed size) */
		WBENCHSCREEN
	};

	win = OpenWindow(&nw);
	if (!win) {
		return false;
	}

	ActivateGadget(&edit, win, NULL);

	// make this window modal
	Forbid();

	/* Event Loop */
	bool running = true;
	bool result = false;
	while (running) {
		Wait(1 << win->UserPort->mp_SigBit);

		while(msg = (struct IntuiMessage *)GetMsg(win->UserPort)) {
			if (msg->Class == CLOSEWINDOW) {
				running = false;
			}
			else if (msg->Class == GADGETUP) {
				struct Gadget *g = (struct Gadget *)msg->IAddress;
				switch (g->GadgetID) {
					case 1: /* ID 1 = String Gadget (Enter pressed) */
					case 2: /* ID 2 = OK Button */
						result = true; /* Success */
					case 3: /* Cancel */
						running = false;
						break;
				}
			}
			else if (msg->Class == IDCMP_INACTIVEWINDOW) {
				ActivateWindow(win);
			}
			ReplyMsg((struct Message *)msg);
		}
	}

	CloseWindow(win);

	Permit();
	return result;
}
