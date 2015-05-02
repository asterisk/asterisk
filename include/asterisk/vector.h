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
#define AST_RWVECTOR(name, type) \
	struct name {            \
		type *elems;         \
		size_t max;          \
		size_t current;      \
		ast_rwlock_t lock;   \
	}

/*!
 * \brief Write locks a vector.
 * \param vec This is a pointer to the vector structure
 *
 * This macro attempts to place an exclusive write lock in the
 * vector.
 * \retval 0 on success
 * \retval non-zero on failure
 */
#define AST_RWVECTOR_WRLOCK(vec) \
	ast_rwlock_wrlock(&(vec)->lock)

/*!
 * \brief Read locks a vector.
 * \param vec This is a pointer to the vector structure
 *
 * This macro attempts to place a read lock in the
 * vector.
 * \retval 0 on success
 * \retval non-zero on failure
 */
#define AST_RWVECTOR_RDLOCK(vec) \
	ast_rwlock_rdlock(&(vec)->lock)

/*!
 * \brief Attempts to unlock a read/write based vector.
 * \param vec This is a pointer to the vector structure
 *
 * This macro attempts to remove a read or write lock from the
 * vector. If the vector was not locked by this thread, this
 * macro has no effect.
 */
#define AST_RWVECTOR_UNLOCK(vec) \
	ast_rwlock_unlock(&(vec)->lock)

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

#define AST_RWVECTOR_INIT(vec, size) ({ \
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

#define AST_RWVECTOR_FREE(vec) do { \
	AST_VECTOR_FREE(vec); \
	ast_rwlock_destroy(&(vec)->lock); \
} while(0)

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
	int res = 0;								\
	do {									\
		if ((vec)->current + 1 > (vec)->max) {				\
			size_t new_max = (vec)->max ? 2 * (vec)->max : 1;	\
			typeof((vec)->elems) new_elems = ast_realloc(		\
				(vec)->elems, new_max * sizeof(*new_elems));	\
			if (new_elems) {					\
				(vec)->elems = new_elems;			\
				(vec)->max = new_max;				\
			} else {						\
				res = -1;					\
				break;						\
			}							\
		}								\
		(vec)->elems[(vec)->current++] = (elem);			\
	} while (0);								\
	res;									\
})

#define AST_RWVECTOR_APPEND(vec, elem) ({ \
	int res = -1; \
	if (!AST_RWVECTOR_WRLOCK(vec)) { \
		res = AST_VECTOR_APPEND(vec, elem); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
	res; \
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
#define AST_VECTOR_REPLACE(vec, idx, elem) ({					\
 	int res = 0;												\
 	do {														\
 		if (((idx) + 1) > (vec)->max) {							\
 			size_t new_max = ((idx) + 1) * 2;					\
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
 		(vec)->elems[(idx)] = (elem);							\
 		if (((idx) + 1) > (vec)->current) {						\
 			(vec)->current = (idx) + 1;							\
 		}														\
 	} while(0);													\
 	res;														\
})

#define AST_RWVECTOR_REPLACE(vec, idx, elem) ({ \
	int res = -1; \
	if (!AST_RWVECTOR_WRLOCK(vec)) { \
		res = AST_VECTOR_REPLACE(vec, idx, elem); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
	res; \
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
#define AST_VECTOR_INSERT(vec, idx, elem) ({ \
	int res = 0; \
	size_t __move; \
	do { \
		if ((vec)->current + 1 > (vec)->max) { \
			size_t new_max = (vec)->max ? 2 * (vec)->max : 1; \
			typeof((vec)->elems) new_elems = ast_realloc( \
				(vec)->elems, new_max * sizeof(*new_elems)); \
			if (new_elems) { \
				(vec)->elems = new_elems; \
				(vec)->max = new_max; \
			} else { \
				res = -1; \
				break; \
			} \
		} \
		__move = ((vec)->current - 1) * sizeof(typeof((vec)->elems[0])); \
		memmove(&(vec)->elems[(idx) + 1], &(vec)->elems[(idx)], __move); \
		(vec)->elems[(idx)] = (elem); \
		(vec)->current++; \
	} while (0); \
	res; \
})

#define AST_RWVECTOR_INSERT(vec, idx, elem) ({ \
	int res = -1; \
	if (!AST_RWVECTOR_WRLOCK(vec)) { \
		res = AST_VECTOR_INSERT(vec, idx, elem); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
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
 * \return The element that was removed.
 */
#define AST_VECTOR_REMOVE_UNORDERED(vec, idx) ({		\
	typeof((vec)->elems[0]) res;				\
	size_t __idx = (idx);					\
	ast_assert(__idx < (vec)->current);			\
	res = (vec)->elems[__idx];				\
	(vec)->elems[__idx] = (vec)->elems[--(vec)->current];	\
	res;							\
})

#define AST_RWVECTOR_REMOVE_UNORDERED(vec, idx) ({ \
	int res = -1; \
	if (!AST_RWVECTOR_WRLOCK(vec)) { \
		res = AST_VECTOR_REMOVE_UNORDERED(vec, idx); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
	res; \
})

/*!
 * \brief Remove an element from a vector by index while maintaining order.
 *
 * \param vec Vector to remove from.
 * \param idx Index of the element to remove.
 * \return The element that was removed.
 */
#define AST_VECTOR_REMOVE_ORDERED(vec, idx) ({				\
      	typeof((vec)->elems[0]) res;					\
	size_t __idx = (idx);						\
	size_t __move;							\
	ast_assert(__idx < (vec)->current);				\
	res = (vec)->elems[__idx];					\
	__move = ((vec)->current - (__idx) - 1) * sizeof(typeof((vec)->elems[0])); \
	memmove(&(vec)->elems[__idx], &(vec)->elems[__idx + 1], __move); \
	(vec)->current--;						\
	res;								\
})

#define AST_RWVECTOR_REMOVE_ORDERED(vec, idx) ({ \
	int res = -1; \
	if (!AST_RWVECTOR_WRLOCK(vec)) { \
		res = AST_VECTOR_REMOVE_ORDERED(vec, idx); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
	res; \
})

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

#define AST_RWVECTOR_REMOVE_CMP_UNORDERED(vec, value, cmp, cleanup) ({ \
	int res = -1; \
	if (!AST_RWVECTOR_WRLOCK(vec)) { \
		res = AST_VECTOR_REMOVE_CMP_UNORDERED(vec, value, cmp, cleanup); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
	res; \
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

#define AST_RWVECTOR_REMOVE_CMP_ORDERED(vec, value, cmp, cleanup) ({ \
	int res = -1; \
	if (!AST_RWVECTOR_WRLOCK(vec)) { \
		res = AST_VECTOR_REMOVE_CMP_ORDERED(vec, value, cmp, cleanup); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
	res; \
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

#define AST_RWVECTOR_REMOVE_ELEM_UNORDERED(vec, elem, cleanup) ({ \
	AST_RWVECTOR_REMOVE_CMP_UNORDERED((vec), (elem), \
		AST_VECTOR_ELEM_DEFAULT_CMP, cleanup); \
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

#define AST_RWVECTOR_REMOVE_ELEM_ORDERED(vec, elem, cleanup) ({ \
	AST_RWVECTOR_REMOVE_CMP_ORDERED((vec), (elem), \
		AST_VECTOR_ELEM_DEFAULT_CMP, cleanup); \
})

/*!
 * \brief Get the number of elements in a vector.
 *
 * \param vec Vector to query.
 * \return Number of elements in the vector.
 */
#define AST_VECTOR_SIZE(vec) (vec)->current

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
 * \brief Execute a callback on every element in a vector
 *
 * \param vec Vector to operate on.
 * \param callback The ao2_callback style function to execute
 * \param arg Any argument
 *
 * \return the number of elements visited before the end of the vector
 * was reached or CMP_STOP was returned.
 */
#define AST_VECTOR_CALLBACK(vec, callback, arg) ({ \
	size_t idx; \
	for (idx = 0; idx < (vec)->current; idx++) { \
		int rc = callback((vec)->elems[idx], arg, 0);	\
		if (rc == CMP_STOP) { \
			idx++; \
			break; \
		}\
	} \
	idx; \
})

/*!
 * \brief Execute a callback on every element in a vector while holding a read lock
 *
 * \param vec Vector to operate on.
 * \param callback The ao2_callback style function to execute
 * \param arg Any argument
 *
 * \return the number of elements visited before the end of the vector
 * was reached or CMP_STOP was returned.
 */
#define AST_RWVECTOR_CALLBACK_RDLOCK(vec, callback, arg) ({ \
	int res = -1; \
	if (AST_RWVECTOR_RDLOCK(vec) == 0) { \
		res = AST_VECTOR_CALLBACK(vec, callback, arg); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
	res; \
})

/*!
 * \brief Execute a callback on every element in a vector while holding a write lock
 *
 * \param vec Vector to operate on.
 * \param callback The ao2_callback style function to execute
 * \param arg Any argument
 *
 * \return the number of elements visited before the end of the vector
 * was reached or CMP_STOP was returned.
 */
#define AST_RWVECTOR_CALLBACK_WRLOCK(vec, callback, arg) ({ \
	int res = -1; \
	if (AST_RWVECTOR_WRLOCK(vec) == 0) { \
		res = AST_VECTOR_CALLBACK(vec, callback, arg); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
	res; \
})

/*!
 * \brief Execute a callback on every element in a vector
 *
 * \param vec Vector to operate on.
 * \param callback The ao2_callback_data style function to execute
 * \param arg Any argument
 * \param data Any data
 *
 * \return the number of elements visited before the end of the vector
 * was reached or CMP_STOP was returned.
 */
#define AST_VECTOR_CALLBACK_DATA(vec, callback, arg, data) ({ \
	size_t idx; \
	for (idx = 0; idx < (vec)->current; idx++) { \
		int rc = callback((vec)->elems[idx], arg, data, 0);	\
		if (rc == CMP_STOP) { \
			idx++; \
			break; \
		}\
	} \
	idx; \
})

/*!
 * \brief Execute a callback on every element in a vector while holding a read lock
 *
 * \param vec Vector to operate on.
 * \param callback The ao2_callback_data style function to execute
 * \param arg Any argument
 * \param data Any data
 *
 * \return the number of elements visited before the end of the vector
 * was reached or CMP_STOP was returned.
 */
#define AST_RWVECTOR_CALLBACK_DATA_RDLOCK(vec, callback, arg, data) ({ \
	int res = -1; \
	if (AST_RWVECTOR_RDLOCK(vec) == 0) { \
		res = AST_VECTOR_CALLBACK_DATA(vec, callback, arg, data); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
	res; \
})

/*!
 * \brief Execute a callback on every element in a vector while holding a write lock
 *
 * \param vec Vector to operate on.
 * \param callback The ao2_callback_data style function to execute
 * \param arg Any argument
 * \param data Any data
 *
 * \return the number of elements visited before the end of the vector
 * was reached or CMP_STOP was returned.
 */
#define AST_RWVECTOR_CALLBACK_DATA_WRLOCK(vec, callback, arg, data) ({ \
	int res = -1; \
	if (AST_RWVECTOR_WRLOCK(vec) == 0) { \
		res = AST_VECTOR_CALLBACK_DATA(vec, callback, arg, data); \
		AST_RWVECTOR_UNLOCK(vec); \
	} \
	res; \
})

#endif /* _ASTERISK_VECTOR_H */
