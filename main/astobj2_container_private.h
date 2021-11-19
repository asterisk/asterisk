/*
 * astobj2 - replacement containers for asterisk data structures.
 *
 * Copyright (C) 2006 Marta Carbone, Luigi Rizzo - Univ. di Pisa, Italy
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
 * \brief Common, private definitions for astobj2 containers.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 */

#ifndef ASTOBJ2_CONTAINER_PRIVATE_H_
#define ASTOBJ2_CONTAINER_PRIVATE_H_

#include "asterisk/astobj2.h"

/*!
 * \internal
 * \brief Enum for internal_ao2_unlink_node.
 */
enum ao2_unlink_node_flags {
	/*! Remove the node from the object's weak link list
	 * OR unref the object if it's a strong reference. */
	AO2_UNLINK_NODE_UNLINK_OBJECT = (1 << 0),
	/*! Modified unlink_object to skip the unref of the object. */
	AO2_UNLINK_NODE_NOUNREF_OBJECT = (1 << 1),
	/*! Unref the node. */
	AO2_UNLINK_NODE_UNREF_NODE = (1 << 2),
	/*! Decrement the container's element count. */
	AO2_UNLINK_NODE_DEC_COUNT = (1 << 3),
};

enum ao2_callback_type {
	AO2_CALLBACK_DEFAULT,
	AO2_CALLBACK_WITH_DATA,
};

enum ao2_container_insert {
	/*! The node was inserted into the container. */
	AO2_CONTAINER_INSERT_NODE_INSERTED,
	/*! The node object replaced an existing node object. */
	AO2_CONTAINER_INSERT_NODE_OBJ_REPLACED,
	/*! The node was rejected (duplicate). */
	AO2_CONTAINER_INSERT_NODE_REJECTED,
};

/*! Allow enough room for container specific traversal state structs */
#define AO2_TRAVERSAL_STATE_SIZE	100

/*!
 * \brief Generic container node.
 *
 * \details This is the base container node type that contains
 * values common to all container nodes.
 */
struct ao2_container_node {
	/*! Stored object in node. */
	void *obj;
	/*! Container holding the node.  (Does not hold a reference.) */
	struct ao2_container *my_container;
	/*! TRUE if the node is linked into the container. */
	unsigned int is_linked:1;
};

/*!
 * \brief Destroy this container.
 *
 * \param self Container to operate upon.
 */
typedef void (*ao2_container_destroy_fn)(struct ao2_container *self);

/*!
 * \brief Create an empty copy of this container.
 *
 * \param self Container to operate upon.
 * \param tag used for debugging.
 * \param file Debug file name invoked from
 * \param line Debug line invoked from
 * \param func Debug function name invoked from
 *
 * \return empty-container on success.
 * \retval NULL on error.
 */
typedef struct ao2_container *(*ao2_container_alloc_empty_clone_fn)(struct ao2_container *self, const char *tag, const char *file, int line, const char *func);

/*!
 * \brief Create a new container node.
 *
 * \param self Container to operate upon.
 * \param obj_new Object to put into the node.
 * \param tag used for debugging.
 * \param file Debug file name invoked from
 * \param line Debug line invoked from
 * \param func Debug function name invoked from
 *
 * \return initialized-node on success.
 * \retval NULL on error.
 */
typedef struct ao2_container_node *(*ao2_container_new_node_fn)(struct ao2_container *self, void *obj_new, const char *tag, const char *file, int line, const char *func);

/*!
 * \brief Insert a node into this container.
 *
 * \param self Container to operate upon.
 * \param node Container node to insert into the container.
 *
 * \return enum ao2_container_insert value.
 */
typedef enum ao2_container_insert (*ao2_container_insert_fn)(struct ao2_container *self, struct ao2_container_node *node);

/*!
 * \brief Find the first container node in a traversal.
 *
 * \param self Container to operate upon.
 * \param flags search_flags to control traversing the container
 * \param arg Comparison callback arg parameter.
 * \param v_state Traversal state to restart container traversal.
 *
 * \return node-ptr of found node (Reffed).
 * \retval NULL when no node found.
 */
typedef struct ao2_container_node *(*ao2_container_find_first_fn)(struct ao2_container *self, enum search_flags flags, void *arg, void *v_state);

/*!
 * \brief Find the next container node in a traversal.
 *
 * \param self Container to operate upon.
 * \param v_state Traversal state to restart container traversal.
 * \param prev Previous node returned by the traversal search functions.
 *    The ref ownership is passed back to this function.
 *
 * \return node-ptr of found node (Reffed).
 * \retval NULL when no node found.
 */
typedef struct ao2_container_node *(*ao2_container_find_next_fn)(struct ao2_container *self, void *v_state, struct ao2_container_node *prev);

/*!
 * \brief Cleanup the container traversal state.
 *
 * \param v_state Traversal state to cleanup.
 */
typedef void (*ao2_container_find_cleanup_fn)(void *v_state);

/*!
 * \brief Find the next non-empty iteration node in the container.
 *
 * \param self Container to operate upon.
 * \param prev Previous node returned by the iterator.
 * \param flags search_flags to control iterating the container.
 *   Only AO2_ITERATOR_DESCENDING is useful by the method.
 *
 * \note The container is already locked.
 *
 * \return node on success.
 * \retval NULL on error or no more nodes in the container.
 */
typedef struct ao2_container_node *(*ao2_iterator_next_fn)(struct ao2_container *self, struct ao2_container_node *prev, enum ao2_iterator_flags flags);

/*!
 * \brief Display contents of the specified container.
 *
 * \param self Container to dump.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 * \param prnt_obj Callback function to print the given object's key. (NULL if not available)
 */
typedef void (*ao2_container_display)(struct ao2_container *self, void *where, ao2_prnt_fn *prnt, ao2_prnt_obj_fn *prnt_obj);

/*!
 * \brief Display statistics of the specified container.
 *
 * \param self Container to display statistics.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 *
 * \note The container is already locked for reading.
 */
typedef void (*ao2_container_statistics)(struct ao2_container *self, void *where, ao2_prnt_fn *prnt);

/*!
 * \brief Perform an integrity check on the specified container.
 *
 * \param self Container to check integrity.
 *
 * \note The container is already locked for reading.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
typedef int (*ao2_container_integrity)(struct ao2_container *self);

/*!
 * \internal
 * \brief Increment the container linked object statistic.
 * \since 12.4.0
 *
 * \param container Container to operate upon.
 * \param node Container node linking object to.
 */
typedef void (*ao2_link_node_stat_fn)(struct ao2_container *container, struct ao2_container_node *node);

/*!
 * \internal
 * \brief Decrement the container linked object statistic.
 * \since 12.4.0
 *
 * \param container Container to operate upon.
 * \param node Container node unlinking object from.
 */
typedef void (*ao2_unlink_node_stat_fn)(struct ao2_container *container, struct ao2_container_node *node);

/*! Container virtual methods template. */
struct ao2_container_methods {
	/*! Destroy this container. */
	ao2_container_destroy_fn destroy;
	/*! \brief Create an empty copy of this container. */
	ao2_container_alloc_empty_clone_fn alloc_empty_clone;
	/*! Create a new container node. */
	ao2_container_new_node_fn new_node;
	/*! Insert a node into this container. */
	ao2_container_insert_fn insert;
	/*! Traverse the container, find the first node. */
	ao2_container_find_first_fn traverse_first;
	/*! Traverse the container, find the next node. */
	ao2_container_find_next_fn traverse_next;
	/*! Traverse the container, cleanup state. */
	ao2_container_find_cleanup_fn traverse_cleanup;
	/*! Find the next iteration element in the container. */
	ao2_iterator_next_fn iterator_next;
#if defined(AO2_DEBUG)
	/*! Increment the container linked object statistic. */
	ao2_link_node_stat_fn link_stat;
	/*! Decrement the container linked object statistic. */
	ao2_unlink_node_stat_fn unlink_stat;
	/*! Display container contents. (Method for debug purposes) */
	ao2_container_display dump;
	/*! Display container debug statistics. (Method for debug purposes) */
	ao2_container_statistics stats;
	/*! Perform an integrity check on the container. (Method for debug purposes) */
	ao2_container_integrity integrity;
#endif	/* defined(AO2_DEBUG) */
};

/*!
 * \brief Generic container type.
 *
 * \details This is the base container type that contains values
 * common to all container types.
 *
 * \todo Linking and unlinking container objects is typically
 * expensive, as it involves a malloc()/free() of a small object
 * which is very inefficient.  To optimize this, we can allocate
 * larger arrays of container nodes when we run out of them, and
 * then manage our own freelist.  This will be more efficient as
 * we can do the freelist management while we hold the lock
 * (that we need anyway).
 */
struct ao2_container {
	/*! Container virtual method table. */
	const struct ao2_container_methods *v_table;
	/*! Container sort function if the container is sorted. */
	ao2_sort_fn *sort_fn;
	/*! Container traversal matching function for ao2_find. */
	ao2_callback_fn *cmp_fn;
	/*! The container option flags */
	uint32_t options;
	/*! Number of elements in the container. */
	int elements;
#if defined(AO2_DEBUG)
	/*! Number of nodes in the container. */
	int nodes;
	/*! Maximum number of empty nodes in the container. (nodes - elements) */
	int max_empty_nodes;
#endif	/* defined(AO2_DEBUG) */
	/*!
	 * \brief TRUE if the container is being destroyed.
	 *
	 * \note The destruction traversal should override any requested
	 * search order to do the most efficient order for destruction.
	 *
	 * \note There should not be any empty nodes in the container
	 * during destruction.  If there are then an error needs to be
	 * issued about container node reference leaks.
	 */
	unsigned int destroying:1;
};

/*!
 * \internal
 * \brief Unlink a node from this container.
 *
 * \param node Node to operate upon.
 * \param flags ao2_unlink_node_flags governing behavior.
 *
 * \retval 0 on errors.
 * \retval 1 on success.
 */
#define __container_unlink_node(node, flags) \
	__container_unlink_node_debug(node, flags, NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__)
int __container_unlink_node_debug(struct ao2_container_node *node, uint32_t flags,
	const char *tag, const char *file, int line, const char *func);

void container_destruct(void *_c);
int container_init(void);

#endif /* ASTOBJ2_CONTAINER_PRIVATE_H_ */
