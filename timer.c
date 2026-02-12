#include <exec/types.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <clib/exec_protos.h>
#include <clib/alib_protos.h>
#include <memory.h>

#include "timer.h"
#include "system.h"
#include "log.h"

/* GCC defines for 1.3 compatibility */
#ifndef UNIT_MICROHZ
#define UNIT_MICROHZ 0
#endif

LOG_FACILITY(Timer, LL_TRACE);

/* Initialize the timer device */
bool timer_init(systimer_t *timer)
{
	LOG_DEBUG("Init (%p)", timer);
	if (timer->open) {
		// already opened
		LOG_DEBUG("Init (%p): already opened\n", timer);
		return false;
	}

	timer->open = false;
	timer->port = CreatePort(0, 0);
	if (!timer->port) {
		LOG_DEBUG("Init (%p): no CreatePort\n", timer);
		return false;
	}

	/* Timer device requires 'struct timerequest', not standard IORequest */
	timer->req = (struct timerequest *)CreateExtIO(timer->port, sizeof(*timer->req));
	if (!timer->req) {
		LOG_DEBUG("Init (%p): no CreateExtIO\n", timer);
		DeletePort(timer->port);
		return false;
	}

	/* Open timer.device with UNIT_MICROHZ for high resolution */
	if (OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)timer->req, 0) != 0) {
		LOG_DEBUG("Init (%p): no OpenDevice\n", timer);
		DeleteExtIO((struct IORequest *)timer->req);
		DeletePort(timer->port);
		return false;
	}
	
	timer->open = true;
	return true;
}

/* Close and cleanup */
void timer_cleanup(systimer_t *timer)
{
	LOG_DEBUG("Cleanup (%p, %s)", timer, timer->open ? "true" : "false");
	if (timer->open) {
		timer->open = false;
		CloseDevice((struct IORequest *)timer->req);
		DeleteExtIO((struct IORequest *)timer->req);
		DeletePort(timer->port);
	}
}

/* Helper to get current system time */
bool timer_gettime(systimer_t *timer, systimeval_t *tv)
{
	if (timer->open) {
		timer->req->tr_node.io_Command = TR_GETSYSTIME;
		DoIO((struct IORequest *)timer->req);
		/* Result is stored in tr_time inside the request structure */
		*tv = timer->req->tr_time;
		if (timer->req->tr_node.io_Error == 0) {
			return true;
		}
	}

	memset(tv, 0, sizeof(*tv));
	return false;
}

void timer_diff(const systimeval_t *a, systimeval_t *b)
{
	b->tv_secs -= a->tv_secs;
	b->tv_micro -= a->tv_micro;
	if ((LONG)b->tv_micro < 0) {
		b->tv_micro += 1000000;
		b->tv_secs -= 1;
	}
}
