#pragma once

#include <stdbool.h>

#include <proto/intuition.h>
#include "buffer.h"

void requester_init();
void requester_cleanup();

// Iterates over the `text`, replaces `\n` by `\0` (the input is modified!) and generates linked list of `IntuiText` into `lines`.
// NOTE: maybe there is better way to draw the multi-line text, conserving more memory
int requester_text2lines(char *text, buffer_t *lines);
bool requester_text(const char *message, char *buffer, int len);
bool requester_message(struct Window *window, const char *positive, const char *negative, const char *format, ...);
