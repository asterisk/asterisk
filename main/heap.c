/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Max Heap data structure
 *
 * \author Russell Bryant <russell@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/heap.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"

struct ast_heap {
	ast_rwlock_t lock;
	ast_heap_cmp_fn cmp_fn;
	ssize_t index_offset;
	size_t cur_len;
	size_t avail_len;
	void **heap;
};

static inline int left_node(int i)
{
	return 2 * i;
}

static inline int right_node(int i)
{
	return 2 * i + 1;
}

static inline int parent_node(int i)
{
	return i / 2;
}

static inline void *heap_get(struct ast_heap *h, int i)
{
	return h->heap[i - 1];
}

static inline ssize_t get_index(struct ast_heap *h, void *elm)
{
	ssize_t *index;

	if (h->index_offset < 0) {
		return -1;
	}

	index = elm + h->index_offset;

	return *index;
}

static inline void heap_set(struct ast_heap *h, int i, void *elm)
{
	h->heap[i - 1] = elm;

	if (h->index_offset >= 0) {
		ssize_t *index = elm + h->index_offset;
		*index = i;
	}
}

int ast_heap_verify(struct ast_heap *h)
{
	unsigned int i;

	for (i = 1; i <= (h->cur_len / 2); i++) {
		int l = left_node(i);
		int r = right_node(i);

		if (l <= h->cur_len) {
			if (h->cmp_fn(heap_get(h, i), heap_get(h, l)) < 0) {
				return -1;
			}
		}

		if (r <= h->cur_len) {
			if (h->cmp_fn(heap_get(h, i), heap_get(h, r)) < 0) {
				return -1;
			}
		}
	}

	return 0;
}

#ifdef MALLOC_DEBUG
struct ast_heap *_ast_heap_create(unsigned int init_height, ast_heap_cmp_fn cmp_fn,
		ssize_t index_offset, const char *file, int lineno, const char *func)
#else
struct ast_heap *ast_heap_create(unsigned int init_height, ast_heap_cmp_fn cmp_fn,
		ssize_t index_offset)
#endif
{
	struct ast_heap *h;

	if (!cmp_fn) {
		ast_log(LOG_ERROR, "A comparison function must be provided\n");
		return NULL;
	}

	if (!init_height) {
		init_height = 8;
	}

	if (!(h =
#ifdef MALLOC_DEBUG
			__ast_calloc(1, sizeof(*h), file, lineno, func)
#else
			ast_calloc(1, sizeof(*h))
#endif
		)) {
		return NULL;
	}

	h->cmp_fn = cmp_fn;
	h->index_offset = index_offset;
	h->avail_len = (1 << init_height) - 1;

	if (!(h->heap =
#ifdef MALLOC_DEBUG
			__ast_calloc(1, h->avail_len * sizeof(void *), file, lineno, func)
#else
			ast_calloc(1, h->avail_len * sizeof(void *))
#endif
		)) {
		ast_free(h);
		return NULL;
	}

	ast_rwlock_init(&h->lock);

	return h;
}

struct ast_heap *ast_heap_destroy(struct ast_heap *h)
{
	ast_free(h->heap);
	h->heap = NULL;

	ast_rwlock_destroy(&h->lock);

	ast_free(h);

	return NULL;
}

/*!
 * \brief Add a row of additional storage for the heap.
 */
static int grow_heap(struct ast_heap *h
#ifdef MALLOC_DEBUG
, const char *file, int lineno, const char *func
#endif
)
{
	h->avail_len = h->avail_len * 2 + 1;

	if (!(h->heap =
#ifdef MALLOC_DEBUG
			__ast_realloc(h->heap, h->avail_len * sizeof(void *), file, lineno, func)
#else
			ast_realloc(h->heap, h->avail_len * sizeof(void *))
#endif
		)) {
		h->cur_len = h->avail_len = 0;
		return -1;
	}

	return 0;
}

static inline void heap_swap(struct ast_heap *h, int i, int j)
{
	void *tmp;

	tmp = heap_get(h, i);
	heap_set(h, i, heap_get(h, j));
	heap_set(h, j, tmp);
}

static inline void max_heapify(struct ast_heap *h, int i)
{
	for (;;) {
		int l = left_node(i);
		int r = right_node(i);
		int max;

		if (l <= h->cur_len && h->cmp_fn(heap_get(h, l), heap_get(h, i)) > 0) {
			max = l;
		} else {
			max = i;
		}

		if (r <= h->cur_len && h->cmp_fn(heap_get(h, r), heap_get(h, max)) > 0) {
			max = r;
		}

		if (max == i) {
			break;
		}

		heap_swap(h, i, max);

		i = max;
	}
}

static int bubble_up(struct ast_heap *h, int i)
{
	while (i > 1 && h->cmp_fn(heap_get(h, parent_node(i)), heap_get(h, i)) < 0) {
		heap_swap(h, i, parent_node(i));
		i = parent_node(i);
	}

	return i;
}

#ifdef MALLOC_DEBUG
int _ast_heap_push(struct ast_heap *h, void *elm, const char *file, int lineno, const char *func)
#else
int ast_heap_push(struct ast_heap *h, void *elm)
#endif
{
	if (h->cur_len == h->avail_len && grow_heap(h
#ifdef MALLOC_DEBUG
		, file, lineno, func
#endif
		)) {
		return -1;
	}

	heap_set(h, ++(h->cur_len), elm);

	bubble_up(h, h->cur_len);

	return 0;
}

static void *_ast_heap_remove(struct ast_heap *h, unsigned int index)
{
	void *ret;

	if (!index || index > h->cur_len) {
		return NULL;
	}

	ret = heap_get(h, index);
	heap_set(h, index, heap_get(h, (h->cur_len)--));
	index = bubble_up(h, index);
	max_heapify(h, index);

	return ret;
}

void *ast_heap_remove(struct ast_heap *h, void *elm)
{
	ssize_t i = get_index(h, elm);

	if (i == -1) {
		return NULL;
	}

	return _ast_heap_remove(h, i);
}

void *ast_heap_pop(struct ast_heap *h)
{
	return _ast_heap_remove(h, 1);
}

void *ast_heap_peek(struct ast_heap *h, unsigned int index)
{
	if (!h->cur_len || !index || index > h->cur_len) {
		return NULL;
	}

	return heap_get(h, index);
}

size_t ast_heap_size(struct ast_heap *h)
{
	return h->cur_len;
}

int __ast_heap_wrlock(struct ast_heap *h, const char *file, const char *func, int line)
{
	return __ast_rwlock_wrlock(file, line, func, &h->lock, "&h->lock");
}

int __ast_heap_rdlock(struct ast_heap *h, const char *file, const char *func, int line)
{
	return __ast_rwlock_rdlock(file, line, func, &h->lock, "&h->lock");
}

int __ast_heap_unlock(struct ast_heap *h, const char *file, const char *func, int line)
{
	return __ast_rwlock_unlock(file, line, func, &h->lock, "&h->lock");
}
