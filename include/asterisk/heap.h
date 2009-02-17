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

/*!
 * \file
 * \brief Max Heap data structure
 * \author Russell Bryant <russell@digium.com>
 */

#ifndef __AST_HEAP_H__
#define __AST_HEAP_H__

/*!
 * \brief A max heap.
 *
 * \note Thread-safety is left to the user of the API.  The heap API provides
 *       no locking of its own.  If the heap will be accessed by multiple threads,
 *       then a lock must be used to ensure that only a single operation is
 *       done on the heap at a time.  For the sake of convenience, a lock is
 *       provided for the user of the API to use if another lock is not already
 *       available to protect the heap.
 */
struct ast_heap;

/*!
 * \brief Function type for comparing nodes in a heap
 *
 * \param elm1 the first element
 * \param elm2 the second element
 *
 * \retval negative if elm1 < elm2
 * \retval 0 if elm1 == elm2
 * \retval positive if elm1 > elm2
 *
 * \note This implementation is of a max heap.  However, if a min heap is
 *       desired, simply swap the return values of this function.
 */
typedef int (*ast_heap_cmp_fn)(void *elm1, void *elm2);

/*!
 * \brief Create a max heap
 *
 * \param init_height The initial height of the heap to allocate space for.
 *        To start out, there will be room for (2 ^ init_height) - 1 entries.
 *        However, the heap will grow as needed.
 * \param cmp_fn The function that should be used to compare elements in the heap.
 * \param index_offset This parameter is optional, but must be provided to be able
 *        to use ast_heap_remove().  This is the number of bytes into the element
 *        where an ssize_t has been made available for the heap's internal
 *        use.  The heap will use this field to keep track of the element's current
 *        position in the heap.  The offsetof() macro is useful for providing a
 *        proper value for this argument.  If ast_heap_remove() will not be used,
 *        then a negative value can be provided to indicate that no field for an
 *        offset has been allocated.
 *
 * Example Usage:
 *
 * \code
 *
 * struct myobj {
 *    int foo;
 *    int bar;
 *    char stuff[8];
 *    char things[8];
 *    ssize_t __heap_index;
 * };
 *
 * ...
 *
 * static int myobj_cmp(void *obj1, void *obj2);
 *
 * ...
 *
 * struct ast_heap *h;
 *
 * h = ast_heap_create(8, myobj_cmp, offsetof(struct myobj, __heap_index));
 *
 * \endcode
 *
 * \return An instance of a max heap
 */
struct ast_heap *ast_heap_create(unsigned int init_height, ast_heap_cmp_fn cmp_fn,
		ssize_t index_offset);

/*!
 * \brief Destroy a max heap
 *
 * \param h the heap to destroy
 *
 * \return NULL for convenience
 */
struct ast_heap *ast_heap_destroy(struct ast_heap *h);

/*!
 * \brief Push an element on to a heap
 *
 * \param h the heap being added to
 * \param elm the element being put on the heap
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_heap_push(struct ast_heap *h, void *elm);

/*!
 * \brief Pop the max element off of the heap
 *
 * \param h the heap
 *
 * \return this will return the element on the top of the heap, which has the
 *         largest value according to the element comparison function that was
 *         provided when the heap was created.  The element will be removed before
 *         being returned.
 */
void *ast_heap_pop(struct ast_heap *h);

/*!
 * \brief Remove a specific element from a heap
 *
 * \param h the heap to remove from
 * \param elm the element to remove
 *
 * \return elm, if the removal was successful, or NULL if it failed
 *
 * \note the index_offset parameter to ast_heap_create() is required to be able
 *       to use this function.
 */
void *ast_heap_remove(struct ast_heap *h, void *elm);

/*!
 * \brief Peek at an element on a heap
 *
 * \param h the heap
 * \param index index of the element to return.  The first element is at index 1,
 *        and the last element is at the index == the size of the heap.
 *
 * \return an element at the specified index on the heap.  This element will \b not
 *         be removed before being returned.
 *
 * \note If this function is being used in combination with ast_heap_size() for
 *       purposes of traversing the heap, the heap must be locked for the entire
 *       duration of the traversal.
 */
void *ast_heap_peek(struct ast_heap *h, unsigned int index);

/*!
 * \brief Get the current size of a heap
 *
 * \param h the heap
 *
 * \return the number of elements currently in the heap
 */
size_t ast_heap_size(struct ast_heap *h);

/*!
 * \brief Write-Lock a heap
 *
 * \arg h the heap
 *
 * A lock is provided for convenience.  It can be assumed that none of the
 * ast_heap API calls are thread safe.  This lock does not have to be used
 * if another one is already available to protect the heap.
 *
 * \return see the documentation for pthread_rwlock_wrlock()
 */
int ast_heap_wrlock(struct ast_heap *h);

/*!
 * \brief Read-Lock a heap
 *
 * \arg h the heap
 *
 * A lock is provided for convenience.  It can be assumed that none of the
 * ast_heap API calls are thread safe.  This lock does not have to be used
 * if another one is already available to protect the heap.
 *
 * \return see the documentation for pthread_rwlock_rdlock()
 */
int ast_heap_rdlock(struct ast_heap *h);

/*!
 * \brief Unlock a heap
 *
 * \arg h the heap
 *
 * \return see the documentation for pthread_rwlock_unlock()
 */
int ast_heap_unlock(struct ast_heap *h);

/*!
 * \brief Verify that a heap has been properly constructed
 *
 * \param h a heap
 *
 * \retval 0 success
 * \retval non-zero failure
 *
 * \note This function is mostly for debugging purposes.  It traverses an existing
 *       heap and verifies that every node is properly placed relative to its children.
 */
int ast_heap_verify(struct ast_heap *h);

#endif /* __AST_HEAP_H__ */
