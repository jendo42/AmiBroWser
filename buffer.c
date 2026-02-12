#include <memory.h>
#include <stdlib.h>
#include <assert.h>

#include "buffer.h"
#include "log.h"

LOG_FACILITY(Buffer, LL_DEBUG);

void buffer_init(buffer_t* buffer, uint16_t size, uint16_t preallocCount)
{
	LOG_DEBUG("Init (%p, %u, %u)", buffer, (ULONG)size, (ULONG)preallocCount);
	buffer->count = 0;
	buffer->size = size;
	buffer->capacity = size * preallocCount;
	buffer->data = malloc(buffer->capacity);
	if (!buffer->data) {
		buffer->capacity = 0;
	}
}

void buffer_clear(buffer_t *buffer)
{
	assert(buffer_resize(buffer, buffer->size, 0) == true);
}

bool buffer_resize(buffer_t* buffer, uint16_t size, uint16_t count)
{
	LOG_TRACE("Resize (%p, %u, %u)", buffer, (ULONG)size, (ULONG)count);
	const uint32_t MinimumGrowSize = 16;
	uint32_t newCapacity = size * count;
	if (newCapacity > buffer->capacity || !buffer->data) {
		uint32_t diff = newCapacity - buffer->capacity;
		if (diff < MinimumGrowSize) {
			newCapacity += MinimumGrowSize - diff;
		}
		void *data = realloc(buffer->data, newCapacity);
		if (!data) {
			// not enough memory
			return false;
		}
		buffer->data = data;
		buffer->capacity = newCapacity;
	}
	buffer->size = size;
	buffer->count = count;
	return true;
}

bool buffer_resizec(buffer_t* buffer, uint16_t count)
{
	return buffer_resize(buffer, buffer->size, count);
}

bool buffer_check(buffer_t* buffer)
{
	return buffer->data && buffer->capacity;
}

void buffer_cleanup(buffer_t* buffer)
{
	LOG_DEBUG("Cleanup (%p)", buffer);
	if (buffer->data) {
		free(buffer->data);
	}
	memset(buffer, 0, sizeof(*buffer));
}

void *buffer_emplace(buffer_t* buffer)
{
	LOG_TRACE("Emplace (%p)", buffer);
	uint32_t end = buffer_end(buffer);
	if (buffer_resizec(buffer, buffer->count + 1)) {
		return (char *)buffer->data + end;
	}
	return NULL;
}

bool buffer_pop(buffer_t* buffer)
{
	LOG_TRACE("Pop (%p)", buffer);
	uint16_t count = buffer->count;
	if (count) {
		buffer_resizec(buffer, count - 1);
		return true;
	}

	return false;
}

void *buffer_top(buffer_t* buffer)
{
	LOG_TRACE("Top (%p)", buffer);
	uint16_t count = buffer->count;
	uint16_t size = buffer->size;
	if (buffer_check(buffer) && count) {
		return (char *)buffer->data + size * (count - 1);
	}
	return NULL;
}

void *buffer_at(buffer_t* buffer, uint16_t i)
{
	LOG_TRACE("At (%p, %u)", buffer, (ULONG)i);
	uint16_t count = buffer->count;
	uint16_t size = buffer->size;
	if (buffer_check(buffer) && i < count) {
		return (char *)buffer->data + size * i;
	}
	return NULL;
}

bool buffer_append(buffer_t *buffer, const void *data, uint16_t count)
{
	uint32_t end = buffer_end(buffer);
	if (!buffer_resizec(buffer, buffer->count + count)) {
		return false;
	}

	memcpy((char *)buffer->data + end, data, buffer->size * count);
	return true;
}

uint32_t buffer_end(buffer_t *buffer)
{
	return (uint32_t)buffer->count * buffer->size;
}
