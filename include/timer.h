#pragma once

#include <stdbool.h>
#include <devices/timer.h>

typedef struct timeval systimeval_t;
typedef struct systimer systimer_t;
struct systimer {
	struct MsgPort *port;
	struct timerequest *req;
	bool open;
};

bool timer_init(systimer_t *timer);
void timer_cleanup(systimer_t *timer);
bool timer_gettime(systimer_t *timer, systimeval_t *tv);
void timer_diff(const systimeval_t *a, systimeval_t *b);
