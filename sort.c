#include "sort.h"

/* Structure for the explicit stack */
typedef struct {
	void **l;
	void **r;
} range_t;

/* * Helper: Insertion Sort
 * Critical optimization: This removes the overhead of the 'compar' function call
 * for the vast majority of your data sorting (small partitions).
 */
static void insertion_sort(void **start, void **end, sort_comparer_t compar, void *user)
{
	register void **i;
	register void **j;
	void *value;

	for (i = start + 1; i <= end; i++) {
		value = *i;
		j = i;

		/* Note: We must pass addresses &value and (j-1) to the callback */
		/* This is the unavoidable cost of the standard callback interface */
		while (j > start && compar(*(j - 1), value, user) > 0) {
			*j = *(j - 1);
			j--;
		}
		*j = value;
	}
}

/* * Optimized Quicksort (Int Array + Callback)
 * - base: Pointer to the first integer
 * - count: Number of integers
 * - compar: Standard 'qsort' style callback 
 * (takes pointers to two integers)
 */
void quicksort(void **base, int count, sort_comparer_t compar, void *user)
{
	if (count < 2) {
		return;
	}

	range_t stack[32];
	register range_t *sp = stack;

	sp->l = base;
	sp->r = base + count - 1;
	sp++;

	while (sp > stack) {
		sp--;
		register void **l = sp->l;
		register void **r = sp->r;

		/* Use Insertion Sort for small chunks */
		if (r - l < SORT_THRESHOLD) {
			insertion_sort(l, r, compar, user);
			continue;
		}

		register void **i = l;
		register void **j = r;

		/* Pivot Selection: Median of Three */
		/* We use the callback to sort the Start, Middle, and End */
		void **mid = l + ((r - l) >> 1);

		if (compar(*mid, *l, user) < 0) { void* t=*mid; *mid=*l; *l=t; }
		if (compar(*r, *l, user) < 0)   { void* t=*r;   *r=*l;   *l=t; }
		if (compar(*r, *mid, user) < 0) { void* t=*r;   *r=*mid; *mid=t; }

		/* Pivot Value (Copy to register D0 if possible) */
		void *pivot = *mid;

		/* Partitioning Loop */
		while (i <= j) {
			/* * PERFORMANCE NOTE:
			 * This is the bottleneck. compar() is an indirect function call (JSR (A0)).
			 * On 68010 this costs ~16 cycles + stack push/pop.
			 * We pass '&pivot' to avoid fetching the pivot from memory repeatedly,
			 * but 'i' changes every time.
			 */
			while (compar(*i, pivot, user) < 0) i++;
			while (compar(*j, pivot, user) > 0) j--;

			if (i <= j) {
				void* tmp = *i;
				*i = *j;
				*j = tmp;
				i++;
				j--;
			}
		}

		/* Push larger side first to keep stack small */
		if (l < j) {
			sp->l = l;
			sp->r = j;
			sp++;
		}
		if (i < r) {
			sp->l = i;
			sp->r = r;
			sp++;
		}
	}
}
