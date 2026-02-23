#include <stdint.h>
#include <stdbool.h>

typedef struct list list_t;
typedef struct list_item list_item_t;

struct list
{
	list_item_t *begin;
	list_item_t *end;
	uint32_t count;
};

struct list_item
{
	list_item_t *next;
	list_item_t *prev;
	void *value;
};

bool list_init(list_t *list);
void list_cleanup(list_t *list, bool cleanup_values);

bool list_push_back(list_t *list, void *value);
bool list_pop_back(list_t *list, bool cleanup_value);

void *list_front(list_t *list);
void *list_back(list_t *list);
