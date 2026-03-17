#include <memory.h>
#include <stdlib.h>

#include "system.h"
#include "assert.h"

#include "list.h"

bool list_init(list_t *list)
{
	list_item_t *item = (list_item_t *)malloc(sizeof(list_item_t));
	if (!item) {
		return false;
	}

	memset(item, 0, sizeof(*item));
	list->begin = item;
	list->end = item;
	list->count = 0;
	return true;
}

void list_cleanup(list_t *list, bool cleanup_values)
{
	list_item_t *item = list->begin;
	list_item_t *next;
	while (item) {
		if (cleanup_values && item->value) {
			free(item->value);
		}

		next = item->next;
		free(item);
		item = next;
	}

	list->begin = NULL;
	list->end = NULL;
	list->count = 0;
}

bool list_push_back(list_t *list, void *value)
{
	list_item_t *end = list->end;
	if (!end) {
		return NULL;
	}
	list_item_t *item = (list_item_t *)malloc(sizeof(list_item_t));
	if (!item) {
		return false;
	}

	item->list = list;
	item->value = value;
	item->next = NULL;
	item->prev = end;
	end->next = item;
	list->end = item;
	return true;
}

bool list_pop_back(list_t *list, bool cleanup_value)
{
	if (!list->count) {
		return false;
	}

	list_item_t *end = list->end;
	if (!end) {
		return false;
	}

	list_item_t *last = end->prev;
	if (!last) {
		return false;
	}

	if (cleanup_value) {
		free(last->value);
	}
	
	last->list = NULL;
	last->value = NULL;
	last->next = NULL;
	list->end = last;
	free(end);
	return true;
}

void *list_front(list_t *list)
{
	if (!list->count) {
		return NULL;
	}
	if (!list->begin) {
		return NULL;
	}
	return list->begin->value;
}

void *list_back(list_t *list)
{
	if (!list->count) {
		return NULL;
	}

	list_item_t *end = list->end;
	if (!end) {
		return NULL;
	}

	assert(end->prev != NULL);
	return end->prev->value;
}

list_item_t *list_at(list_t *list, int index)
{
	bool reverse = index < 0;
	list_item_t *item = reverse ? list->end : list->begin;
	for (int i = reverse ? -index : index; i && item; i--) {
		item = reverse ? item->prev : item->next;
	}
	return item;
}

bool list_erase(list_item_t *item, bool cleanup_value)
{
	assert(item != NULL);
	list_t *list = item->list;
	assert(list != NULL);
	if (list->end == item) {
		return false;
	}
	if (list->begin == item) {
		list->begin = item->next;
	}
	if (item->prev) {
		item->prev->next = item->next;
	}
	if (item->next) {
		item->next->prev = item->prev;
	}
	if (cleanup_value) {
		free(item->value);
	}
	free(item);
	return true;
}
