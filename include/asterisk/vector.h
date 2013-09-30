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

/*! \brief Define a vector structure */
#define ast_vector(type)			\
	struct {				\
		type *elems;			\
		size_t max;			\
		size_t current;			\
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
#define ast_vector_init(vec, size) ({					\
	size_t __size = (size);						\
	size_t alloc_size = __size * sizeof(*(vec).elems);		\
	(vec).elems = alloc_size ? ast_malloc(alloc_size) : NULL;	\
	(vec).current = 0;						\
	if ((vec).elems) {						\
		(vec).max = __size;					\
	} else {							\
		(vec).max = 0;						\
	}								\
	alloc_size == 0 || (vec).elems != NULL ? 0 : -1;		\
})

/*!
 * \brief Deallocates this vector.
 *
 * If any code to free the elements of this vector need to be run, that should
 * be done prior to this call.
 *
 * \param vec Vector to deallocate.
 */
#define ast_vector_free(vec) do {		\
	ast_free((vec).elems);			\
	(vec).elems = NULL;			\
	(vec).max = 0;				\
	(vec).current = 0;			\
} while (0)

/*!
 * \brief Append an element to a vector, growing the vector if needed.
 *
 * \param vec Vector to append to.
 * \param elem Element to append.
 *
 * \return 0 on success.
 * \return Non-zero on failure.
 */
#define ast_vector_append(vec, elem) ({					\
	int res = 0;							\
									\
	if ((vec).current + 1 > (vec).max) {				\
		size_t new_max = (vec).max ? 2 * (vec).max : 1;		\
		typeof((vec).elems) new_elems = ast_realloc(		\
			(vec).elems, new_max * sizeof(*new_elems));	\
		if (new_elems) {					\
			(vec).elems = new_elems;				\
			(vec).max = new_max;				\
		} else {						\
			res = -1;					\
		}							\
	}								\
									\
	if (res == 0) {							\
		(vec).elems[(vec).current++] = (elem);			\
	}								\
	res;								\
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
#define ast_vector_remove_unordered(vec, idx) ({		\
	typeof((vec).elems[0]) res;				\
	size_t __idx = (idx);					\
	ast_assert(__idx < (vec).current);			\
	res = (vec).elems[__idx];				\
	(vec).elems[__idx] = (vec).elems[--(vec).current];	\
	res;							\
})


/*!
 * \brief Remove an element from a vector that matches the given comparison
 *
 * \param vec Vector to remove from.
 * \param value Value to pass into comparator.
 * \param cmp Comparator function/macros (called as \c cmp(elem, value))
 * \return 0 if element was removed.
 * \return Non-zero if element was not in the vector.
 */
#define ast_vector_remove_cmp_unordered(vec, value, cmp) ({		\
	int res = -1;							\
	size_t idx;							\
	typeof(value) __value = (value);				\
	for (idx = 0; idx < (vec).current; ++idx) {			\
		if (cmp((vec).elems[idx], __value)) {			\
			ast_vector_remove_unordered((vec), idx);	\
			res = 0;					\
			break;						\
		}							\
	}								\
	res;								\
})

/*! \brief Default comparator for ast_vector_remove_elem_unordered() */
#define AST_VECTOR_DEFAULT_CMP(a, b) ((a) == (b))

/*!
 * \brief Remove an element from a vector.
 *
 * \param vec Vector to remove from.
 * \param elem Element to remove
 * \return 0 if element was removed.
 * \return Non-zero if element was not in the vector.
 */
#define ast_vector_remove_elem_unordered(vec, elem) ({	\
	ast_vector_remove_cmp_unordered((vec), (elem),	\
		AST_VECTOR_DEFAULT_CMP);		\
})

/*!
 * \brief Get the number of elements in a vector.
 *
 * \param vec Vector to query.
 * \return Number of elements in the vector.
 */
#define ast_vector_size(vec) (vec).current

/*!
 * \brief Get an element from a vector.
 *
 * \param vec Vector to query.
 * \param idx Index of the element to get.
 */
#define ast_vector_get(vec, idx) ({		\
	size_t __idx = (idx);			\
	ast_assert(__idx < (vec).current);	\
	(vec).elems[__idx];			\
})

#endif /* _ASTERISK_VECTOR_H */
