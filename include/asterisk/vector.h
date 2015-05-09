/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

#ifndef _ASTERISK_VECTOR_H
#define _ASTERISK_VECTOR_H

#include "asterisk/lock.h"

/*! \file
 *
 * \brief Vector container support.
 *
 * A vector is a variable length array, with properties that can be useful when
 * order doesn't matter.
 *  - Appends are asymptotically constant time.
 *  - Unordered removes are constant time.
 *  - Search is linear time
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

/*!
 * \brief Define a vector structure
 *
 * \param name Optional vector struct name.
 * \param type Vector element type.
 */
#define AST_VECTOR(name, type)			\
	struct name {				\
		type *elems;			\
		size_t max;			\
		size_t current;			\
	}

/*!
 * \brief Define a vector structure with a read/write lock
 *
 * \param name Optional vector struct name.
 * \param type Vector element type.
 */
#define AST_VECTOR_RW(name, type) \
	struct name {            \
		type *elems;         \
		size_t max;          \
		size_t current;      \
		ast_rwlock_t lock;   \
	}

/*!
 * \brief Initialize a vector
 *
 * If \a size is 0, then no space will be allocated until the vector is
 * appended to.
 *
 * \param vec Vector to initialize.
 * \param size Initial size of the vector.
 *
 * \return 0 on success.
 * \return Non-zero on failure.
 */
#define AST_VECTOR_INIT(vec, size) ({					\
	size_t __size = (size);						\
	size_t alloc_size = __size * sizeof(*((vec)->elems));		\
	(vec)->elems = alloc_size ? ast_calloc(1, alloc_size) : NULL;	\
	(vec)->current = 0;						\
	if ((vec)->elems) {						\
		(vec)->max = __size;					\
	} else {							\
		(vec)->max = 0;						\
	}								\
	(alloc_size == 0 || (vec)->elems != NULL) ? 0 : -1;		\
})

/*!
 * \brief Initialize a vector with a read/write lock
 *
 * If \a size is 0, then no space will be allocated until the vector is
 * appended to.
 *
 * \param vec Vector to initialize.
 * \param size Initial size of the vector.
 *
 * \return 0 on success.
 * \return Non-zero on failure.
 */
#define AST_VECTOR_RW_INIT(vec, size) ({ \
	int res = -1; \
	if (AST_VECTOR_INIT(vec, size) == 0) { \
		res = ast_rwlock_init(&(vec)->lock); \
	} \
	res; \
})

/*!
 * \brief Deallocates this vector.
 *
 * If any code to free the elements of this vector need to be run, that should
 * be done prior to this call.
 *
 * \param vec Vector to deallocate.
 */
#define AST_VECTOR_FREE(vec) do {		\
	ast_free((vec)->elems);			\
	(vec)->elems = NULL;			\
	(vec)->max = 0;				\
	(vec)->current = 0;			\
} while (0)

/*!
 * \brief Deallocates this vector pointer.
 *
 * If any code to free the elements of this vector need to be run, that should
 * be done prior to this call.
 *
 * \param vec Pointer to a malloc'd vector structure.
 */
#define AST_VECTOR_PTR_FREE(vec) do { \
	AST_VECTOR_FREE(vec); \
	ast_free(vec); \
} while (0)

/*!
 * \brief Deallocates this locked vector
 *
 * If any code to free the elements of this vector need to be run, that should
 * be done prior to this call.
 *
 * \param vec Vector to deallocate.
 */
#define AST_VECTOR_RW_FREE(vec) do { \
	AST_VECTOR_FREE(vec); \
	ast_rwlock_destroy(&(vec)->lock); \
} while(0)

/*!
 * \brief Deallocates this locked vector pointer.
 *
 * If any code to free the elements of this vector need to be run, that should
 * be done prior to this call.
 *
 * \param vec Pointer to a malloc'd vector structure.
 */
#define AST_VECTOR_RW_PTR_FREE(vec) do { \
	AST_VECTOR_RW_FREE(vec); \
	ast_free(vec); \
} while(0)

/*!
 * \internal
 */
#define __make_room(idx, vec) ({ \
	int res = 0;								\
	do {														\
		if ((idx) >= (vec)->max) {								\
			size_t new_max = ((idx) + 1) * 2;				\
			typeof((vec)->elems) new_elems = ast_calloc(1,		\
				new_max * sizeof(*new_elems));					\
			if (new_elems) {									\
				memcpy(new_elems, (vec)->elems,					\
					(vec)->current * sizeof(*new_elems)); 		\
				ast_free((vec)->elems);							\
				(vec)->elems = new_elems;						\
				(vec)->max = new_max;							\
			} else {											\
				res = -1;										\
				break;											\
			}													\
		}														\
	} while(0);													\
	res;														\
})

/*!
 * \brief Append an element to a vector, growing the vector if needed.
 *
 * \param vec Vector to append to.
 * \param elem Element to append.
 *
 * \return 0 on success.
 * \return Non-zero on failure.
 */
#define AST_VECTOR_APPEND(vec, elem) ({						\
	int res = 0;											\
	do {													\
		if (__make_room((vec)->current, vec) != 0) { 		\
			res = -1;										\
			break;											\
		} 													\
		(vec)->elems[(vec)->current++] = (elem);			\
	} while (0);											\
	res;													\
})

/*!
 * \brief Replace an element at a specific position in a vector, growing the vector if needed.
 *
 * \param vec Vector to replace into.
 * \param idx Position to replace.
 * \param elem Element to replace.
 *
 * \return 0 on success.
 * \return Non-zero on failure.
 *
 * \warning This macro will overwrite anything already present at the position provided.
 *
 * \warning Use of this macro with the expectation that the element will remain at the provided
 * index means you can not use the UNORDERED assortment of macros. These macros alter the ordering
 * of the vector itself.
 */
#define AST_VECTOR_REPLACE(vec, idx, elem) ({		\
	int res = 0;									\
	do {											\
		if (__make_room((idx), vec) != 0) {			\
			res = -1;								\
			break;									\
		}											\
		(vec)->elems[(idx)] = (elem);				\
		if (((idx) + 1) > (vec)->current) {			\
			(vec)->current = (idx) + 1;				\
		}											\
	} while(0);										\
	res;											\
})

/*!
 * \brief Insert an element at a specific position in a vector, growing the vector if needed.
 *
 * \param vec Vector to insert into.
 * \param idx Position to insert at.
 * \param elem Element to insert.
 *
 * \return 0 on success.
 * \return Non-zero on failure.
 *
 * \warning This macro will shift existing elements right to make room for the new element.
 *
 * \warning Use of this macro with the expectation that the element will remain at the provided
 * index means you can not use the UNORDERED assortment of macros. These macros alter the ordering
 * of the vector itself.
 */
#define AST_VECTOR_INSERT_AT(vec, idx, elem) ({ \
	int res = 0; \
	size_t __move; \
	do { \
		if (__make_room(((idx) > (vec)->current ? (idx) : (vec)->current), vec) != 0) {							\
			res = -1;										\
			break;											\
		}														\
		if ((vec)->current > 0 && (idx) < (vec)->current) { \
			__move = ((vec)->current - (idx)) * sizeof(typeof((vec)->elems[0])); \
			memmove(&(vec)->elems[(idx) + 1], &(vec)->elems[(idx)], __move); \
		} \
		(vec)->elems[(idx)] = (elem); \
		(vec)->current = ((idx) > (vec)->current ? (idx) : (vec)->current) + 1; \
	} while (0); \
	res; \
})

/*!
 * \brief Insert an element into a sorted vector
 *
 * \param vec Sorted vector to insert into.
 * \param elem Element to insert.
 * \param comparison_fn The comparison_fn_t compatible compare function.
 *
 * \return 0 on success.
 * \return Non-zero on failure.
 *
 * \warning Use of this macro on an unsorted vector will produce unpredictable results
 */
#define AST_VECTOR_INSERT_SORTED(vec, elem, compare_fn) ({ \
	int res = 0; \
	size_t __idx = (vec)->current - 1; \
	do { \
		if (__make_room((vec)->current, vec) != 0) { \
			res = -1; \
			break; \
		} \
		while (__idx >= 0 && (compare_fn(&elem, &(vec)->elems[__idx]) < 0)) { \
			(vec)->elems[__idx + 1] = (vec)->elems[__idx]; \
			__idx--; \
		} \
		(vec)->elems[__idx + 1] = elem; \
		(vec)->current++; \
	} while (0); \
	res; \
})

/*!
 * \brief Remove an element from a vector by index.
 *
 * Note that elements in the vector may be reordered, so that the remove can
 * happen in constant time.
 *
 * \param vec Vector to remove from.
 * \param idx Index of the element to remove.
 * \param preserve_order Preserve the vector order.
 *
 * \return The element that was removed.
 */
#define AST_VECTOR_REMOVE(vec, idx, preserve_ordered) ({ \
	typeof((vec)->elems[0]) res; \
	size_t __idx = (idx); \
	ast_assert(__idx < (vec)->current); \
	res = (vec)->elems[__idx]; \
	if ((preserve_ordered)) { \
		size_t __move; \
		__move = ((vec)->current - (__idx) - 1) * sizeof(typeof((vec)->elems[0])); \
		memmove(&(vec)->elems[__idx], &(vec)->elems[__idx + 1], __move); \
	(vec)->current--; \
	} else { \
		(vec)->elems[__idx] = (vec)->elems[--(vec)->current];	\
	}; \
	res;							\
})

/*!
 * \brief Remove an element from an unordered vector by index.
 *
 * Note that elements in the vector may be reordered, so that the remove can
 * happen in constant time.
 *
 * \param vec Vector to remove from.
 * \param idx Index of the element to remove.
 * \return The element that was removed.
 */
#define AST_VECTOR_REMOVE_UNORDERED(vec, idx) \
	AST_VECTOR_REMOVE(vec, idx, 0)

/*!
 * \brief Remove an element from a vector by index while maintaining order.
 *
 * \param vec Vector to remove from.
 * \param idx Index of the element to remove.
 * \return The element that was removed.
 */
#define AST_VECTOR_REMOVE_ORDERED(vec, idx) \
	AST_VECTOR_REMOVE(vec, idx, 1)

/*!
 * \brief Remove an element from a vector that matches the given comparison
 *
 * \param vec Vector to remove from.
 * \param value Value to pass into comparator.
 * \param cmp Comparator function/macros (called as \c cmp(elem, value))
 * \param cleanup How to cleanup a removed element macro/function.
 *
 * \return 0 if element was removed.
 * \return Non-zero if element was not in the vector.
 */
#define AST_VECTOR_REMOVE_CMP_UNORDERED(vec, value, cmp, cleanup) ({	\
	int res = -1;							\
	size_t idx;							\
	typeof(value) __value = (value);				\
	for (idx = 0; idx < (vec)->current; ++idx) {			\
		if (cmp((vec)->elems[idx], __value)) {			\
			cleanup((vec)->elems[idx]);			\
			AST_VECTOR_REMOVE_UNORDERED((vec), idx);	\
			res = 0;					\
			break;						\
		}							\
	}								\
	res;								\
})

/*!
 * \brief Remove an element from a vector that matches the given comparison while maintaining order
 *
 * \param vec Vector to remove from.
 * \param value Value to pass into comparator.
 * \param cmp Comparator function/macros (called as \c cmp(elem, value))
 * \param cleanup How to cleanup a removed element macro/function.
 *
 * \return 0 if element was removed.
 * \return Non-zero if element was not in the vector.
 */
#define AST_VECTOR_REMOVE_CMP_ORDERED(vec, value, cmp, cleanup) ({	\
	int res = -1;							\
	size_t idx;							\
	typeof(value) __value = (value);				\
	for (idx = 0; idx < (vec)->current; ++idx) {			\
		if (cmp((vec)->elems[idx], __value)) {			\
			cleanup((vec)->elems[idx]);			\
			AST_VECTOR_REMOVE_ORDERED((vec), idx);	\
			res = 0;					\
			break;						\
		}							\
	}								\
	res;								\
})

/*!
 * \brief Default comparator for AST_VECTOR_REMOVE_ELEM_UNORDERED()
 *
 * \param elem Element to compare against
 * \param value Value to compare with the vector element.
 *
 * \return 0 if element does not match.
 * \return Non-zero if element matches.
 */
#define AST_VECTOR_ELEM_DEFAULT_CMP(elem, value) ((elem) == (value))

/*!
 * \brief Vector element cleanup that does nothing.
 *
 * \param elem Element to cleanup
 *
 * \return Nothing
 */
#define AST_VECTOR_ELEM_CLEANUP_NOOP(elem)

/*!
 * \brief Remove an element from a vector.
 *
 * \param vec Vector to remove from.
 * \param elem Element to remove
 * \param cleanup How to cleanup a removed element macro/function.
 *
 * \return 0 if element was removed.
 * \return Non-zero if element was not in the vector.
 */
#define AST_VECTOR_REMOVE_ELEM_UNORDERED(vec, elem, cleanup) ({	\
	AST_VECTOR_REMOVE_CMP_UNORDERED((vec), (elem),		\
		AST_VECTOR_ELEM_DEFAULT_CMP, cleanup);		\
})

/*!
 * \brief Remove an element from a vector while maintaining order.
 *
 * \param vec Vector to remove from.
 * \param elem Element to remove
 * \param cleanup How to cleanup a removed element macro/function.
 *
 * \return 0 if element was removed.
 * \return Non-zero if element was not in the vector.
 */
#define AST_VECTOR_REMOVE_ELEM_ORDERED(vec, elem, cleanup) ({	\
	AST_VECTOR_REMOVE_CMP_ORDERED((vec), (elem),		\
		AST_VECTOR_ELEM_DEFAULT_CMP, cleanup);		\
})

/*!
 * \brief Get the number of elements in a vector.
 *
 * \param vec Vector to query.
 * \return Number of elements in the vector.
 */
#define AST_VECTOR_SIZE(vec) (vec)->current

/*!
 * \brief Reset vector.
 *
 * \param vec Vector to reset.
 * \param callback A cleanup callback or AST_VECTOR_ELEM_CLEANUP_NOOP.
 */
#define AST_VECTOR_RESET(vec, cleanup) ({ \
	size_t idx; \
	for (idx = 0; idx < (vec)->current; ++idx) { \
		cleanup((vec)->elems[idx]); \
	} \
	(vec)->current = 0; \
})

/*!
 * \brief Sort vector.
 *
 * \param vec Vector to sort.
 * \param comparison_fn The comparison_fn_t compatible compare function.
 */
#define AST_VECTOR_SORT(vec, comparison_fn) \
	qsort((vec)->elems, (vec)->current, sizeof(*((vec)->elems)), comparison_fn)

/*!
 * \brief Get an address of element in a vector.
 *
 * \param vec Vector to query.
 * \param idx Index of the element to get address of.
 */
#define AST_VECTOR_GET_ADDR(vec, idx) ({	\
	size_t __idx = (idx);			\
	ast_assert(__idx < (vec)->current);	\
	&(vec)->elems[__idx];			\
})

/*!
 * \brief Get an element from a vector.
 *
 * \param vec Vector to query.
 * \param idx Index of the element to get.
 */
#define AST_VECTOR_GET(vec, idx) ({		\
	size_t __idx = (idx);			\
	ast_assert(__idx < (vec)->current);	\
	(vec)->elems[__idx];			\
})

/*!
 * \brief Get an element from a vector that matches the given comparison
 *
 * \param vec Vector to get from.
 * \param value Value to pass into comparator.
 * \param cmp Comparator function/macros (called as \c cmp(elem, value))
 *
 * \return a pointer to the element that was found or NULL
 */
#define AST_VECTOR_GET_CMP(vec, value, cmp) ({ \
	void *res = NULL; \
	size_t idx; \
	typeof(value) __value = (value); \
	for (idx = 0; idx < (vec)->current; ++idx) { \
		if (cmp((vec)->elems[idx], __value)) { \
			res = &(vec)->elems[idx]; \
			break; \
		} \
	} \
	res; \
})

/*!
 * \brief Default callback for AST_VECTOR_CALLBACK()
 *
 * \param elem Element to compare against
 * \param value Value to compare with the vector element.
 *
 * \return CMP_MATCH always.
 */
#define AST_VECTOR_MATCH_ALL(element) (CMP_MATCH)


/*!
 * \brief Execute a callback on every element in a vector returning the first matched
 *
 * \param vec Vector to operate on.
 * \param callback A callback that takes at least 1 argument (the element)
 * plus number of optional arguments
 * \param default_value A default value to return if no elements matched
 *
 * \return the first element matched before CMP_STOP was returned
 * or the end of the vector was reached. Otherwise, default_value
 */
#define AST_VECTOR_CALLBACK(vec, callback, default_value, ...) ({ \
	size_t idx; \
	typeof((vec)->elems[0]) res = default_value;				\
	for (idx = 0; idx < (vec)->current; idx++) { \
		int rc = callback((vec)->elems[idx], ##__VA_ARGS__);	\
		if (rc & CMP_MATCH) { \
			res = (vec)->elems[idx]; \
			break; \
		}\
		if (rc & CMP_STOP) { \
			break; \
		}\
	} \
	res; \
})

/*!
 * \brief Execute a callback on every element in a vector returning the matching
 * elements in a new vector
 *
 * This macro basically provides a filtered clone.
 *
 * \param vec Vector to operate on.
 * \param callback A callback that takes at least 1 argument (the element)
 * plus number of optional arguments
 *
 * \return a vector containing the elements matched before CMP_STOP was returned
 * or the end of the vector was reached. The vector may be empty and could be NULL
 * if there was not enough memory to allocate it's control structure.
 *
 * \warning The returned vector must have AST_VECTOR_PTR_FREE()
 * called on it after you've finished with it.
 *
 * \note The type of the returned vector must be traceable to the original vector.
 *
 * The following will resut in "error: assignment from incompatible pointer type"
 * because these declare 2 different structures.
 *
 * \code
 * AST_VECTOR(, char *) vector_1;
 * AST_VECTOR(, char *) *vector_2;
 *
 * vector_2 = AST_VECTOR_CALLBACK_MULTIPLE(&vector_1, callback);
 * \endcode
 *
 * This will work because you're using the type of the first
 * to declare the second:
 *
 * \code
 * AST_VECTOR(mytype, char *) vector_1;
 * struct mytype *vector_2 = NULL;
 *
 * vector_2 = AST_VECTOR_CALLBACK_MULTIPLE(&vector_1, callback);
 * \endcode
 *
 * This will also work because you're declaring both vector_1 and
 * vector_2 from the same definition.
 *
 * \code
 * AST_VECTOR(, char *) vector_1, *vector_2 = NULL;
 *
 * vector_2 = AST_VECTOR_CALLBACK_MULTIPLE(&vector_1, callback);
 * \endcode
 */
#define AST_VECTOR_CALLBACK_MULTIPLE(vec, callback, ...) ({ \
	size_t idx; \
	typeof((vec)) new_vec; \
	do { \
		new_vec = ast_malloc(sizeof(*new_vec)); \
		if (!new_vec) { \
			break; \
		} \
		if (AST_VECTOR_INIT(new_vec, AST_VECTOR_SIZE((vec))) != 0) { \
			ast_free(new_vec); \
			new_vec = NULL; \
			break; \
		} \
		for (idx = 0; idx < (vec)->current; idx++) { \
			int rc = callback((vec)->elems[idx], ##__VA_ARGS__);	\
			if (rc & CMP_MATCH) { \
				AST_VECTOR_APPEND(new_vec, (vec)->elems[idx]); \
			} \
			if (rc & CMP_STOP) { \
				break; \
			}\
		} \
	} while(0); \
	new_vec; \
})

/*!
 * \brief Execute a callback on every element in a vector disregarding callback return
 *
 * \param vec Vector to operate on.
 * \param callback A callback that takes at least 1 argument (the element)
 * plus number of optional arguments
 */
#define AST_VECTOR_CALLBACK_VOID(vec, callback, ...) ({ \
	size_t idx; \
	for (idx = 0; idx < (vec)->current; idx++) { \
		callback((vec)->elems[idx], ##__VA_ARGS__);	\
	} \
})

/*!
 * \brief Obtain read lock on vector
 *
 * \param vec Vector to operate on.
 *
 * \return 0 if success
 * \return Non-zero if error
 */
#define AST_VECTOR_RW_RDLOCK(vec) ast_rwlock_rdlock(&(vec)->lock)

/*!
 * \brief Obtain write lock on vector
 *
 * \param vec Vector to operate on.
 *
 * \return 0 if success
 * \return Non-zero if error
 */
#define AST_VECTOR_RW_WRLOCK(vec) ast_rwlock_wrlock(&(vec)->lock)

/*!
 * \brief Unlock vector
 *
 * \param vec Vector to operate on.
 *
 * \return 0 if success
 * \return Non-zero if error
 */
#define AST_VECTOR_RW_UNLOCK(vec) ast_rwlock_unlock(&(vec)->lock)

/*!
 * \brief Try to obtain read lock on vector failing immediately if unable
 *
 * \param vec Vector to operate on.
 *
 * \return 0 if success
 * \return Non-zero if error
 */
#define AST_VECTOR_RW_RDLOCK_TRY(vec) ast_rwlock_tryrdlock(&(vec)->lock)

/*!
 * \brief Try to obtain write lock on vector failing immediately if unable
 *
 * \param vec Vector to operate on.
 *
 * \return 0 if success
 * \return Non-zero if error
 */
#define AST_VECTOR_RW_WRLOCK_TRY(vec) ast_rwlock_trywrlock(&(vec)->lock)

/*!
 * \brief Try to obtain read lock on vector failing after timeout if unable
 *
 * \param vec Vector to operate on.
 *
 * \return 0 if success
 * \return Non-zero if error
 */
#define AST_VECTOR_RW_RDLOCK_TIMED(vec, timespec) ast_rwlock_timedrdlock(&(vec)->lock, timespec)

/*!
 * \brief Try to obtain write lock on vector failing after timeout if unable
 *
 * \param vec Vector to operate on.
 *
 * \return 0 if success
 * \return Non-zero if error
 */
#define AST_VECTOR_RW_WRLOCK_TIMED(vec, timespec) ast_rwlock_timedwrlock(&(vec)->lock, timespec)

#endif /* _ASTERISK_VECTOR_H */
