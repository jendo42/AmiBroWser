#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct buffer buffer_t;

struct buffer
{
	void* data;
	void* user;
	uint32_t capacity;
	uint16_t count;
	uint16_t size;
};

bool buffer_check(buffer_t* buffer);
void buffer_cleanup(buffer_t* buffer);
void buffer_init(buffer_t* buffer, uint16_t size, uint16_t preallocCount);
void buffer_clear(buffer_t* buffer);
bool buffer_resize(buffer_t* buffer, uint16_t size, uint16_t count);
bool buffer_resizec(buffer_t* buffer, uint16_t count);
void* buffer_emplace(buffer_t* buffer);
bool buffer_pop(buffer_t* buffer);
void* buffer_top(buffer_t* buffer);
void* buffer_at(buffer_t* buffer, uint16_t i);
bool buffer_append(buffer_t *buffer, const void *data, uint16_t count);
uint32_t buffer_end(buffer_t *buffer);
