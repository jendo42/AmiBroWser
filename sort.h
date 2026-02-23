#pragma once

// when to use insertion sort
#define SORT_THRESHOLD 12

// Comparer function for `quicksort`
typedef int __regargs (*sort_comparer_t)(const void *a, const void *b, void *u);

void quicksort(void **base, int count, sort_comparer_t compar, void *user);
