/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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
 * \brief String fields
 *
 * \author Kevin P. Fleming <kpfleming@digium.com>
 */

#include "asterisk.h"

#include "asterisk/stringfields.h"
#include "asterisk/utils.h"

/* this is a little complex... string fields are stored with their
   allocated size in the bytes preceding the string; even the
   constant 'empty' string has to be this way, so the code that
   checks to see if there is enough room for a new string doesn't
   have to have any special case checks
*/

static const struct {
	ast_string_field_allocation allocation;
	char string[1];
} __ast_string_field_empty_buffer;

ast_string_field __ast_string_field_empty = __ast_string_field_empty_buffer.string;

#define ALLOCATOR_OVERHEAD 48

static size_t optimal_alloc_size(size_t size)
{
	unsigned int count;

	size += ALLOCATOR_OVERHEAD;

	for (count = 1; size; size >>= 1, count++);

	return (1 << count) - ALLOCATOR_OVERHEAD;
}

/*! \brief add a new block to the pool.
 * We can only allocate from the topmost pool, so the
 * fields in *mgr reflect the size of that only.
 */
static int add_string_pool(struct ast_string_field_mgr *mgr, struct ast_string_field_pool **pool_head,
	size_t size, const char *file, int lineno, const char *func)
{
	struct ast_string_field_pool *pool;
	size_t alloc_size = optimal_alloc_size(sizeof(*pool) + size);

	pool = __ast_calloc(1, alloc_size, file, lineno, func);
	if (!pool) {
		return -1;
	}

	pool->prev = *pool_head;
	pool->size = alloc_size - sizeof(*pool);
	*pool_head = pool;
	mgr->last_alloc = NULL;

	return 0;
}

static void reset_field(const char **p)
{
	*p = __ast_string_field_empty;
}

/*!
 * \brief Internal cleanup function
 * \internal
 * \param mgr
 * \param pool_head
 * \param cleanup_type
 * 	     0: Reset all string fields and free all pools except the last or embedded pool.
 * 	        Keep the internal management structures so the structure can be reused.
 * 	    -1: Reset all string fields and free all pools except the embedded pool.
 * 	        Free the internal management structures.
 * \param file
 * \param lineno
 * \param func
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int __ast_string_field_free_memory(struct ast_string_field_mgr *mgr,
	struct ast_string_field_pool **pool_head, enum ast_stringfield_cleanup_type cleanup_type,
	const char *file, int lineno, const char *func)
{
	struct ast_string_field_pool *cur = NULL;
	struct ast_string_field_pool *preserve = NULL;

	/* reset all the fields regardless of cleanup type */
	AST_VECTOR_CALLBACK_VOID(&mgr->string_fields, reset_field);

	switch (cleanup_type) {
	case AST_STRINGFIELD_DESTROY:
		AST_VECTOR_FREE(&mgr->string_fields);

		if (mgr->embedded_pool) { /* ALWAYS preserve the embedded pool if there is one */
			preserve = mgr->embedded_pool;
			preserve->used = preserve->active = 0;
		}

		break;
	case AST_STRINGFIELD_RESET:
		/* Preserve the embedded pool if there is one, otherwise the last pool */
		if (mgr->embedded_pool) {
			preserve = mgr->embedded_pool;
		} else {
			if (*pool_head == NULL) {
				ast_log(LOG_WARNING, "trying to reset empty pool\n");
				return -1;
			}
			preserve = *pool_head;
		}
		preserve->used = preserve->active = 0;
		break;
	default:
		return -1;
	}

	cur = *pool_head;
	while (cur) {
		struct ast_string_field_pool *prev = cur->prev;

		if (cur != preserve) {
			ast_free(cur);
		}
		cur = prev;
	}

	*pool_head = preserve;
	if (preserve) {
		preserve->prev = NULL;
	}

	return 0;
}

/*!
 * \brief Internal initialization function
 * \internal
 * \param mgr
 * \param pool_head
 * \param needed
 * \param file
 * \param lineno
 * \param func
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int __ast_string_field_init(struct ast_string_field_mgr *mgr, struct ast_string_field_pool **pool_head,
	int needed, const char *file, int lineno, const char *func)
{
	const char **p = (const char **) pool_head + 1;
	size_t initial_vector_size = ((size_t) (((char *)mgr) - ((char *)p))) / sizeof(*p);

	if (needed <= 0) {
		return __ast_string_field_free_memory(mgr, pool_head, needed, file, lineno, func);
	}

	mgr->last_alloc = NULL;

	if (AST_VECTOR_INIT(&mgr->string_fields, initial_vector_size)) {
		return -1;
	}

	while ((struct ast_string_field_mgr *) p != mgr) {
		AST_VECTOR_APPEND(&mgr->string_fields, p);
		*p++ = __ast_string_field_empty;
	}

	*pool_head = NULL;
	mgr->embedded_pool = NULL;
	if (add_string_pool(mgr, pool_head, needed, file, lineno, func)) {
		AST_VECTOR_FREE(&mgr->string_fields);
		return -1;
	}

	return 0;
}

ast_string_field __ast_string_field_alloc_space(struct ast_string_field_mgr *mgr,
	struct ast_string_field_pool **pool_head, size_t needed,
	const char *file, int lineno, const char *func)
{
	char *result = NULL;
	size_t space = (*pool_head)->size - (*pool_head)->used;
	size_t to_alloc;

	/* Make room for ast_string_field_allocation and make it a multiple of that. */
	to_alloc = ast_make_room_for(needed, ast_string_field_allocation);
	ast_assert(to_alloc % ast_alignof(ast_string_field_allocation) == 0);

	if (__builtin_expect(to_alloc > space, 0)) {
		size_t new_size = (*pool_head)->size;

		while (new_size < to_alloc) {
			new_size *= 2;
		}

		if (add_string_pool(mgr, pool_head, new_size, file, lineno, func)) {
			return NULL;
		}
	}

	/* pool->base is always aligned (gcc aligned attribute). We ensure that
	 * to_alloc is also a multiple of ast_alignof(ast_string_field_allocation)
	 * causing result to always be aligned as well; which in turn fixes that
	 * AST_STRING_FIELD_ALLOCATION(result) is aligned. */
	result = (*pool_head)->base + (*pool_head)->used;
	(*pool_head)->used += to_alloc;
	(*pool_head)->active += needed;
	result += ast_alignof(ast_string_field_allocation);
	AST_STRING_FIELD_ALLOCATION(result) = needed;
	mgr->last_alloc = result;

	return result;
}

int __ast_string_field_ptr_grow(struct ast_string_field_mgr *mgr,
	struct ast_string_field_pool **pool_head, size_t needed, const ast_string_field *ptr)
{
	ssize_t grow = needed - AST_STRING_FIELD_ALLOCATION(*ptr);
	size_t space = (*pool_head)->size - (*pool_head)->used;

	if (*ptr != mgr->last_alloc) {
		return 1;
	}

	if (space < grow) {
		return 1;
	}

	(*pool_head)->used += grow;
	(*pool_head)->active += grow;
	AST_STRING_FIELD_ALLOCATION(*ptr) += grow;

	return 0;
}

void __ast_string_field_release_active(struct ast_string_field_pool *pool_head,
	const ast_string_field ptr)
{
	struct ast_string_field_pool *pool, *prev;

	if (ptr == __ast_string_field_empty) {
		return;
	}

	for (pool = pool_head, prev = NULL; pool; prev = pool, pool = pool->prev) {
		if ((ptr >= pool->base) && (ptr <= (pool->base + pool->size))) {
			pool->active -= AST_STRING_FIELD_ALLOCATION(ptr);
			if (pool->active == 0) {
				if (prev) {
					prev->prev = pool->prev;
					ast_free(pool);
				} else {
					pool->used = 0;
				}
			}
			break;
		}
	}
}

void __ast_string_field_ptr_build_va(struct ast_string_field_mgr *mgr,
	struct ast_string_field_pool **pool_head, ast_string_field *ptr,
	const char *format, va_list ap,
	const char *file, int lineno, const char *func)
{
	size_t needed;
	size_t available;
	size_t space = (*pool_head)->size - (*pool_head)->used;
	int res;
	ssize_t grow;
	char *target;
	va_list ap2;

	/* if the field already has space allocated, try to reuse it;
	   otherwise, try to use the empty space at the end of the current
	   pool
	*/
	if (*ptr != __ast_string_field_empty) {
		target = (char *) *ptr;
		available = AST_STRING_FIELD_ALLOCATION(*ptr);
		if (*ptr == mgr->last_alloc) {
			available += space;
		}
	} else {
		/* pool->used is always a multiple of ast_alignof(ast_string_field_allocation)
		 * so we don't need to re-align anything here.
		 */
		target = (*pool_head)->base + (*pool_head)->used + ast_alignof(ast_string_field_allocation);
		if (space > ast_alignof(ast_string_field_allocation)) {
			available = space - ast_alignof(ast_string_field_allocation);
		} else {
			available = 0;
		}
	}

	va_copy(ap2, ap);
	res = vsnprintf(target, available, format, ap2);
	va_end(ap2);

	if (res < 0) {
		/* Are we out of memory? */
		return;
	}
	if (res == 0) {
		__ast_string_field_release_active(*pool_head, *ptr);
		*ptr = __ast_string_field_empty;
		return;
	}
	needed = (size_t)res + 1; /* NUL byte */

	if (needed > available) {
		/* the allocation could not be satisfied using the field's current allocation
		   (if it has one), or the space available in the pool (if it does not). allocate
		   space for it, adding a new string pool if necessary.
		*/
		target = (char *) __ast_string_field_alloc_space(mgr, pool_head, needed, file, lineno, func);
		if (!target) {
			return;
		}
		vsprintf(target, format, ap);
		va_end(ap); /* XXX va_end without va_start? */
		__ast_string_field_release_active(*pool_head, *ptr);
		*ptr = target;
	} else if (*ptr != target) {
		/* the allocation was satisfied using available space in the pool, but not
		   using the space already allocated to the field
		*/
		__ast_string_field_release_active(*pool_head, *ptr);
		mgr->last_alloc = *ptr = target;
	        ast_assert(needed < (ast_string_field_allocation)-1);
		AST_STRING_FIELD_ALLOCATION(target) = (ast_string_field_allocation)needed;
		(*pool_head)->used += ast_make_room_for(needed, ast_string_field_allocation);
		(*pool_head)->active += needed;
	} else if ((grow = (needed - AST_STRING_FIELD_ALLOCATION(*ptr))) > 0) {
		/* the allocation was satisfied by using available space in the pool *and*
		   the field was the last allocated field from the pool, so it grew
		*/
		AST_STRING_FIELD_ALLOCATION(*ptr) += grow;
		(*pool_head)->used += ast_align_for(grow, ast_string_field_allocation);
		(*pool_head)->active += grow;
	}
}

void __ast_string_field_ptr_build(const char *file, int lineno, const char *func,
	struct ast_string_field_mgr *mgr,
	struct ast_string_field_pool **pool_head, ast_string_field *ptr, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	__ast_string_field_ptr_build_va(mgr, pool_head, ptr, format, ap, file, lineno, func);
	va_end(ap);
}

void *__ast_calloc_with_stringfields(unsigned int num_structs, size_t struct_size,
	size_t field_mgr_offset, size_t field_mgr_pool_offset, size_t pool_size,
	const char *file, int lineno, const char *func)
{
	struct ast_string_field_mgr *mgr;
	struct ast_string_field_pool *pool;
	struct ast_string_field_pool **pool_head;
	size_t pool_size_needed = sizeof(*pool) + pool_size;
	size_t size_to_alloc = optimal_alloc_size(struct_size + pool_size_needed);
	void *allocation;
	const char **p;
	size_t initial_vector_size;

	ast_assert(num_structs == 1);

	allocation = __ast_calloc(num_structs, size_to_alloc, file, lineno, func);
	if (!allocation) {
		return NULL;
	}

	mgr = allocation + field_mgr_offset;

	pool = allocation + struct_size;
	pool_head = allocation + field_mgr_pool_offset;
	p = (const char **) pool_head + 1;
	initial_vector_size = ((size_t) (((char *)mgr) - ((char *)p))) / sizeof(*p);

	if (AST_VECTOR_INIT(&mgr->string_fields, initial_vector_size)) {
		ast_free(allocation);
		return NULL;
	}

	while ((struct ast_string_field_mgr *) p != mgr) {
		AST_VECTOR_APPEND(&mgr->string_fields, p);
		*p++ = __ast_string_field_empty;
	}

	mgr->embedded_pool = pool;
	*pool_head = pool;
	pool->size = size_to_alloc - struct_size - sizeof(*pool);

	return allocation;
}

int __ast_string_fields_cmp(struct ast_string_field_vector *left,
	struct ast_string_field_vector *right)
{
	int i;
	int res = 0;

	ast_assert(AST_VECTOR_SIZE(left) == AST_VECTOR_SIZE(right));

	for (i = 0; i < AST_VECTOR_SIZE(left); i++) {
		if ((res = strcmp(*AST_VECTOR_GET(left, i), *AST_VECTOR_GET(right, i)))) {
			return res;
		}
	}

	return res;
}

int __ast_string_fields_copy(struct ast_string_field_pool *copy_pool,
	struct ast_string_field_mgr *copy_mgr, struct ast_string_field_mgr *orig_mgr,
	const char *file, int lineno, const char *func)
{
	int i;
	struct ast_string_field_vector *dest = &(copy_mgr->string_fields);
	struct ast_string_field_vector *src = &(orig_mgr->string_fields);

	ast_assert(AST_VECTOR_SIZE(dest) == AST_VECTOR_SIZE(src));

	for (i = 0; i < AST_VECTOR_SIZE(dest); i++) {
		__ast_string_field_release_active(copy_pool, *AST_VECTOR_GET(dest, i));
		*AST_VECTOR_GET(dest, i) = __ast_string_field_empty;
	}

	for (i = 0; i < AST_VECTOR_SIZE(dest); i++) {
		if (__ast_string_field_ptr_set_by_fields(copy_pool, *copy_mgr, AST_VECTOR_GET(dest, i),
			*AST_VECTOR_GET(src, i), file, lineno, func)) {
			return -1;
		}
	}

	return 0;
}
