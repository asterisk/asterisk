/*
 * astobj2_hash - Hash table implementation for astobj2.
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
 * \brief Hash table functions implementing astobj2 containers.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 */

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "astobj2_private.h"
#include "astobj2_container_private.h"
#include "asterisk/dlinkedlists.h"
#include "asterisk/utils.h"

/*!
 * A structure to create a linked list of entries,
 * used within a bucket.
 */
struct hash_bucket_node {
	/*!
	 * \brief Items common to all container nodes.
	 * \note Must be first in the specific node struct.
	 */
	struct ao2_container_node common;
	/*! Next node links in the list. */
	AST_DLLIST_ENTRY(hash_bucket_node) links;
	/*! Hash bucket holding the node. */
	int my_bucket;
};

struct hash_bucket {
	/*! List of objects held in the bucket. */
	AST_DLLIST_HEAD_NOLOCK(, hash_bucket_node) list;
#if defined(AO2_DEBUG)
	/*! Number of elements currently in the bucket. */
	int elements;
	/*! Maximum number of elements in the bucket. */
	int max_elements;
#endif	/* defined(AO2_DEBUG) */
};

/*!
 * A hash container in addition to values common to all
 * container types, stores the hash callback function, the
 * number of hash buckets, and the hash bucket heads.
 */
struct ao2_container_hash {
	/*!
	 * \brief Items common to all containers.
	 * \note Must be first in the specific container struct.
	 */
	struct ao2_container common;
	ao2_hash_fn *hash_fn;
	/*! Number of hash buckets in this container. */
	int n_buckets;
	/*! Hash bucket array of n_buckets.  Variable size. */
	struct hash_bucket buckets[0];
};

/*! Traversal state to restart a hash container traversal. */
struct hash_traversal_state {
	/*! Active sort function in the traversal if not NULL. */
	ao2_sort_fn *sort_fn;
	/*! Saved comparison callback arg pointer. */
	void *arg;
	/*! Starting hash bucket */
	int bucket_start;
	/*! Stopping hash bucket */
	int bucket_last;
	/*! Saved search flags to control traversing the container. */
	enum search_flags flags;
	/*! TRUE if it is a descending search */
	unsigned int descending:1;
};

struct hash_traversal_state_check {
	/*
	 * If we have a division by zero compile error here then there
	 * is not enough room for the state.  Increase AO2_TRAVERSAL_STATE_SIZE.
	 */
	char check[1 / (AO2_TRAVERSAL_STATE_SIZE / sizeof(struct hash_traversal_state))];
};

/*!
 * \internal
 * \brief Create an empty copy of this container.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 *
 * \retval empty-clone-container on success.
 * \retval NULL on error.
 */
static struct ao2_container *hash_ao2_alloc_empty_clone(struct ao2_container_hash *self)
{
	if (!is_ao2_object(self)) {
		return NULL;
	}

	return ao2_t_container_alloc_hash(ao2_options_get(self), self->common.options, self->n_buckets,
		self->hash_fn, self->common.sort_fn, self->common.cmp_fn, "Clone hash container");
}

/*!
 * \internal
 * \brief Create an empty copy of this container. (Debug version)
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param tag used for debugging.
 * \param file Debug file name invoked from
 * \param line Debug line invoked from
 * \param func Debug function name invoked from
 * \param ref_debug TRUE if to output a debug reference message.
 *
 * \retval empty-clone-container on success.
 * \retval NULL on error.
 */
static struct ao2_container *hash_ao2_alloc_empty_clone_debug(struct ao2_container_hash *self, const char *tag, const char *file, int line, const char *func, int ref_debug)
{
	if (!is_ao2_object(self)) {
		return NULL;
	}

	return __ao2_container_alloc_hash_debug(ao2_options_get(self), self->common.options,
		self->n_buckets, self->hash_fn, self->common.sort_fn, self->common.cmp_fn,
		tag, file, line, func, ref_debug);
}

/*!
 * \internal
 * \brief Destroy a hash container list node.
 * \since 12.0.0
 *
 * \param v_doomed Container node to destroy.
 *
 * \details
 * The container node unlinks itself from the container as part
 * of its destruction.  The node must be destroyed while the
 * container is already locked.
 *
 * \note The container must be locked when the node is
 * unreferenced.
 *
 * \return Nothing
 */
static void hash_ao2_node_destructor(void *v_doomed)
{
	struct hash_bucket_node *doomed = v_doomed;

	if (doomed->common.is_linked) {
		struct ao2_container_hash *my_container;
		struct hash_bucket *bucket;

		/*
		 * Promote to write lock if not already there.  Since
		 * adjust_lock() can potentially release and block waiting for a
		 * write lock, care must be taken to ensure that node references
		 * are released before releasing the container references.
		 *
		 * Node references held by an iterator can only be held while
		 * the iterator also holds a reference to the container.  These
		 * node references must be unreferenced before the container can
		 * be unreferenced to ensure that the node will not get a
		 * negative reference and the destructor called twice for the
		 * same node.
		 */
		my_container = (struct ao2_container_hash *) doomed->common.my_container;
		ast_assert(is_ao2_object(my_container));

		__adjust_lock(my_container, AO2_LOCK_REQ_WRLOCK, 1);

#if defined(AO2_DEBUG)
		if (!my_container->common.destroying
			&& ao2_container_check(doomed->common.my_container, OBJ_NOLOCK)) {
			ast_log(LOG_ERROR, "Container integrity failed before node deletion.\n");
		}
#endif	/* defined(AO2_DEBUG) */
		bucket = &my_container->buckets[doomed->my_bucket];
		AST_DLLIST_REMOVE(&bucket->list, doomed, links);
		AO2_DEVMODE_STAT(--my_container->common.nodes);
	}

	/*
	 * We could have an object in the node if the container is being
	 * destroyed or the node had not been linked in yet.
	 */
	if (doomed->common.obj) {
		__container_unlink_node(&doomed->common, AO2_UNLINK_NODE_UNLINK_OBJECT);
	}
}

/*!
 * \internal
 * \brief Create a new container node.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param obj_new Object to put into the node.
 * \param tag used for debugging.
 * \param file Debug file name invoked from
 * \param line Debug line invoked from
 * \param func Debug function name invoked from
 *
 * \retval initialized-node on success.
 * \retval NULL on error.
 */
static struct hash_bucket_node *hash_ao2_new_node(struct ao2_container_hash *self, void *obj_new, const char *tag, const char *file, int line, const char *func)
{
	struct hash_bucket_node *node;
	int i;

	node = __ao2_alloc(sizeof(*node), hash_ao2_node_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!node) {
		return NULL;
	}

	i = abs(self->hash_fn(obj_new, OBJ_SEARCH_OBJECT));
	i %= self->n_buckets;

	if (tag) {
		__ao2_ref_debug(obj_new, +1, tag, file, line, func);
	} else {
		ao2_t_ref(obj_new, +1, "Container node creation");
	}
	node->common.obj = obj_new;
	node->common.my_container = (struct ao2_container *) self;
	node->my_bucket = i;

	return node;
}

/*!
 * \internal
 * \brief Insert a node into this container.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param node Container node to insert into the container.
 *
 * \return enum ao2_container_insert value.
 */
static enum ao2_container_insert hash_ao2_insert_node(struct ao2_container_hash *self,
	struct hash_bucket_node *node)
{
	int cmp;
	struct hash_bucket *bucket;
	struct hash_bucket_node *cur;
	ao2_sort_fn *sort_fn;
	uint32_t options;

	bucket = &self->buckets[node->my_bucket];
	sort_fn = self->common.sort_fn;
	options = self->common.options;

	if (options & AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN) {
		if (sort_fn) {
			AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(&bucket->list, cur, links) {
				cmp = sort_fn(cur->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
				if (cmp > 0) {
					continue;
				}
				if (cmp < 0) {
					AST_DLLIST_INSERT_AFTER_CURRENT(node, links);
					return AO2_CONTAINER_INSERT_NODE_INSERTED;
				}
				switch (options & AO2_CONTAINER_ALLOC_OPT_DUPS_MASK) {
				default:
				case AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW:
					break;
				case AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT:
					/* Reject all objects with the same key. */
					return AO2_CONTAINER_INSERT_NODE_REJECTED;
				case AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT:
					if (cur->common.obj == node->common.obj) {
						/* Reject inserting the same object */
						return AO2_CONTAINER_INSERT_NODE_REJECTED;
					}
					break;
				case AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE:
					SWAP(cur->common.obj, node->common.obj);
					ao2_t_ref(node, -1, "Discard the new node.");
					return AO2_CONTAINER_INSERT_NODE_OBJ_REPLACED;
				}
			}
			AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_END;
		}
		AST_DLLIST_INSERT_HEAD(&bucket->list, node, links);
	} else {
		if (sort_fn) {
			AST_DLLIST_TRAVERSE_SAFE_BEGIN(&bucket->list, cur, links) {
				cmp = sort_fn(cur->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
				if (cmp < 0) {
					continue;
				}
				if (cmp > 0) {
					AST_DLLIST_INSERT_BEFORE_CURRENT(node, links);
					return AO2_CONTAINER_INSERT_NODE_INSERTED;
				}
				switch (options & AO2_CONTAINER_ALLOC_OPT_DUPS_MASK) {
				default:
				case AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW:
					break;
				case AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT:
					/* Reject all objects with the same key. */
					return AO2_CONTAINER_INSERT_NODE_REJECTED;
				case AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT:
					if (cur->common.obj == node->common.obj) {
						/* Reject inserting the same object */
						return AO2_CONTAINER_INSERT_NODE_REJECTED;
					}
					break;
				case AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE:
					SWAP(cur->common.obj, node->common.obj);
					ao2_t_ref(node, -1, "Discard the new node.");
					return AO2_CONTAINER_INSERT_NODE_OBJ_REPLACED;
				}
			}
			AST_DLLIST_TRAVERSE_SAFE_END;
		}
		AST_DLLIST_INSERT_TAIL(&bucket->list, node, links);
	}
	return AO2_CONTAINER_INSERT_NODE_INSERTED;
}

/*!
 * \internal
 * \brief Find the first hash container node in a traversal.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param flags search_flags to control traversing the container
 * \param arg Comparison callback arg parameter.
 * \param state Traversal state to restart hash container traversal.
 *
 * \retval node-ptr of found node (Reffed).
 * \retval NULL when no node found.
 */
static struct hash_bucket_node *hash_ao2_find_first(struct ao2_container_hash *self, enum search_flags flags, void *arg, struct hash_traversal_state *state)
{
	struct hash_bucket_node *node;
	int bucket_cur;
	int cmp;

	memset(state, 0, sizeof(*state));
	state->arg = arg;
	state->flags = flags;

	/* Determine traversal order. */
	switch (flags & OBJ_ORDER_MASK) {
	case OBJ_ORDER_POST:
	case OBJ_ORDER_DESCENDING:
		state->descending = 1;
		break;
	case OBJ_ORDER_PRE:
	case OBJ_ORDER_ASCENDING:
	default:
		break;
	}

	/*
	 * If lookup by pointer or search key, run the hash and optional
	 * sort functions.  Otherwise, traverse the whole container.
	 */
	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
	case OBJ_SEARCH_KEY:
		/* we know hash can handle this case */
		bucket_cur = abs(self->hash_fn(arg, flags & OBJ_SEARCH_MASK));
		bucket_cur %= self->n_buckets;
		state->sort_fn = self->common.sort_fn;
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* scan all buckets for partial key matches */
		bucket_cur = -1;
		state->sort_fn = self->common.sort_fn;
		break;
	default:
		/* don't know, let's scan all buckets */
		bucket_cur = -1;
		state->sort_fn = NULL;
		break;
	}

	if (state->descending) {
		/*
		 * Determine the search boundaries of a descending traversal.
		 *
		 * bucket_cur downto state->bucket_last
		 */
		if (bucket_cur < 0) {
			bucket_cur = self->n_buckets - 1;
			state->bucket_last = 0;
		} else {
			state->bucket_last = bucket_cur;
		}
		state->bucket_start = bucket_cur;

		/* For each bucket */
		for (; state->bucket_last <= bucket_cur; --bucket_cur) {
			/* For each node in the bucket. */
			for (node = AST_DLLIST_LAST(&self->buckets[bucket_cur].list);
				node;
				node = AST_DLLIST_PREV(node, links)) {
				if (!node->common.obj) {
					/* Node is empty */
					continue;
				}

				if (state->sort_fn) {
					/* Filter node through the sort_fn */
					cmp = state->sort_fn(node->common.obj, arg, flags & OBJ_SEARCH_MASK);
					if (cmp > 0) {
						continue;
					}
					if (cmp < 0) {
						/* No more nodes in this bucket are possible to match. */
						break;
					}
				}

				/* We have the first traversal node */
				__ao2_ref(node, +1);
				return node;
			}
		}
	} else {
		/*
		 * Determine the search boundaries of an ascending traversal.
		 *
		 * bucket_cur to state->bucket_last-1
		 */
		if (bucket_cur < 0) {
			bucket_cur = 0;
			state->bucket_last = self->n_buckets;
		} else {
			state->bucket_last = bucket_cur + 1;
		}
		state->bucket_start = bucket_cur;

		/* For each bucket */
		for (; bucket_cur < state->bucket_last; ++bucket_cur) {
			/* For each node in the bucket. */
			for (node = AST_DLLIST_FIRST(&self->buckets[bucket_cur].list);
				node;
				node = AST_DLLIST_NEXT(node, links)) {
				if (!node->common.obj) {
					/* Node is empty */
					continue;
				}

				if (state->sort_fn) {
					/* Filter node through the sort_fn */
					cmp = state->sort_fn(node->common.obj, arg, flags & OBJ_SEARCH_MASK);
					if (cmp < 0) {
						continue;
					}
					if (cmp > 0) {
						/* No more nodes in this bucket are possible to match. */
						break;
					}
				}

				/* We have the first traversal node */
				__ao2_ref(node, +1);
				return node;
			}
		}
	}

	return NULL;
}

/*!
 * \internal
 * \brief Find the next hash container node in a traversal.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param state Traversal state to restart hash container traversal.
 * \param prev Previous node returned by the traversal search functions.
 *    The ref ownership is passed back to this function.
 *
 * \retval node-ptr of found node (Reffed).
 * \retval NULL when no node found.
 */
static struct hash_bucket_node *hash_ao2_find_next(struct ao2_container_hash *self, struct hash_traversal_state *state, struct hash_bucket_node *prev)
{
	struct hash_bucket_node *node;
	void *arg;
	enum search_flags flags;
	int bucket_cur;
	int cmp;

	arg = state->arg;
	flags = state->flags;
	bucket_cur = prev->my_bucket;
	node = prev;

	/*
	 * This function is structured the same as hash_ao2_find_first()
	 * intentionally.  We are resuming the search loops from
	 * hash_ao2_find_first() in order to find the next node.  The
	 * search loops must be resumed where hash_ao2_find_first()
	 * returned with the first node.
	 */
	if (state->descending) {
		goto hash_descending_resume;

		/* For each bucket */
		for (; state->bucket_last <= bucket_cur; --bucket_cur) {
			/* For each node in the bucket. */
			for (node = AST_DLLIST_LAST(&self->buckets[bucket_cur].list);
				node;
				node = AST_DLLIST_PREV(node, links)) {
				if (!node->common.obj) {
					/* Node is empty */
					continue;
				}

				if (state->sort_fn) {
					/* Filter node through the sort_fn */
					cmp = state->sort_fn(node->common.obj, arg, flags & OBJ_SEARCH_MASK);
					if (cmp > 0) {
						continue;
					}
					if (cmp < 0) {
						/* No more nodes in this bucket are possible to match. */
						break;
					}
				}

				/* We have the next traversal node */
				__ao2_ref(node, +1);

				/*
				 * Dereferencing the prev node may result in our next node
				 * object being removed by another thread.  This could happen if
				 * the container uses RW locks and the container was read
				 * locked.
				 */
				__ao2_ref(prev, -1);
				if (node->common.obj) {
					return node;
				}
				prev = node;

hash_descending_resume:;
			}
		}
	} else {
		goto hash_ascending_resume;

		/* For each bucket */
		for (; bucket_cur < state->bucket_last; ++bucket_cur) {
			/* For each node in the bucket. */
			for (node = AST_DLLIST_FIRST(&self->buckets[bucket_cur].list);
				node;
				node = AST_DLLIST_NEXT(node, links)) {
				if (!node->common.obj) {
					/* Node is empty */
					continue;
				}

				if (state->sort_fn) {
					/* Filter node through the sort_fn */
					cmp = state->sort_fn(node->common.obj, arg, flags & OBJ_SEARCH_MASK);
					if (cmp < 0) {
						continue;
					}
					if (cmp > 0) {
						/* No more nodes in this bucket are possible to match. */
						break;
					}
				}

				/* We have the next traversal node */
				__ao2_ref(node, +1);

				/*
				 * Dereferencing the prev node may result in our next node
				 * object being removed by another thread.  This could happen if
				 * the container uses RW locks and the container was read
				 * locked.
				 */
				__ao2_ref(prev, -1);
				if (node->common.obj) {
					return node;
				}
				prev = node;

hash_ascending_resume:;
			}
		}
	}

	/* No more nodes in the container left to traverse. */
	__ao2_ref(prev, -1);
	return NULL;
}

/*!
 * \internal
 * \brief Find the next non-empty iteration node in the container.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param node Previous node returned by the iterator.
 * \param flags search_flags to control iterating the container.
 *   Only AO2_ITERATOR_DESCENDING is useful by the method.
 *
 * \note The container is already locked.
 *
 * \retval node on success.
 * \retval NULL on error or no more nodes in the container.
 */
static struct hash_bucket_node *hash_ao2_iterator_next(struct ao2_container_hash *self, struct hash_bucket_node *node, enum ao2_iterator_flags flags)
{
	int cur_bucket;

	if (flags & AO2_ITERATOR_DESCENDING) {
		if (node) {
			cur_bucket = node->my_bucket;

			/* Find next non-empty node. */
			for (;;) {
				node = AST_DLLIST_PREV(node, links);
				if (!node) {
					break;
				}
				if (node->common.obj) {
					/* Found a non-empty node. */
					return node;
				}
			}
		} else {
			/* Find first non-empty node. */
			cur_bucket = self->n_buckets;
		}

		/* Find a non-empty node in the remaining buckets */
		while (0 <= --cur_bucket) {
			node = AST_DLLIST_LAST(&self->buckets[cur_bucket].list);
			while (node) {
				if (node->common.obj) {
					/* Found a non-empty node. */
					return node;
				}
				node = AST_DLLIST_PREV(node, links);
			}
		}
	} else {
		if (node) {
			cur_bucket = node->my_bucket;

			/* Find next non-empty node. */
			for (;;) {
				node = AST_DLLIST_NEXT(node, links);
				if (!node) {
					break;
				}
				if (node->common.obj) {
					/* Found a non-empty node. */
					return node;
				}
			}
		} else {
			/* Find first non-empty node. */
			cur_bucket = -1;
		}

		/* Find a non-empty node in the remaining buckets */
		while (++cur_bucket < self->n_buckets) {
			node = AST_DLLIST_FIRST(&self->buckets[cur_bucket].list);
			while (node) {
				if (node->common.obj) {
					/* Found a non-empty node. */
					return node;
				}
				node = AST_DLLIST_NEXT(node, links);
			}
		}
	}

	/* No more nodes to visit in the container. */
	return NULL;
}

#if defined(AO2_DEBUG)
/*!
 * \internal
 * \brief Increment the hash container linked object statistic.
 * \since 12.0.0
 *
 * \param hash Container to operate upon.
 * \param hash_node Container node linking object to.
 *
 * \return Nothing
 */
static void hash_ao2_link_node_stat(struct ao2_container *hash, struct ao2_container_node *hash_node)
{
	struct ao2_container_hash *self = (struct ao2_container_hash *) hash;
	struct hash_bucket_node *node = (struct hash_bucket_node *) hash_node;
	int i = node->my_bucket;

	++self->buckets[i].elements;
	if (self->buckets[i].max_elements < self->buckets[i].elements) {
		self->buckets[i].max_elements = self->buckets[i].elements;
	}
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
/*!
 * \internal
 * \brief Decrement the hash container linked object statistic.
 * \since 12.0.0
 *
 * \param hash Container to operate upon.
 * \param hash_node Container node unlinking object from.
 *
 * \return Nothing
 */
static void hash_ao2_unlink_node_stat(struct ao2_container *hash, struct ao2_container_node *hash_node)
{
	struct ao2_container_hash *self = (struct ao2_container_hash *) hash;
	struct hash_bucket_node *node = (struct hash_bucket_node *) hash_node;

	--self->buckets[node->my_bucket].elements;
}
#endif	/* defined(AO2_DEBUG) */

/*!
 * \internal
 *
 * \brief Destroy this container.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 *
 * \return Nothing
 */
static void hash_ao2_destroy(struct ao2_container_hash *self)
{
	int idx;

	/* Check that the container no longer has any nodes */
	for (idx = self->n_buckets; idx--;) {
		if (!AST_DLLIST_EMPTY(&self->buckets[idx].list)) {
			ast_log(LOG_ERROR, "Node ref leak.  Hash container still has nodes!\n");
			ast_assert(0);
			break;
		}
	}
}

#if defined(AO2_DEBUG)
/*!
 * \internal
 * \brief Display contents of the specified container.
 * \since 12.0.0
 *
 * \param self Container to dump.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 * \param prnt_obj Callback function to print the given object's key. (NULL if not available)
 *
 * \return Nothing
 */
static void hash_ao2_dump(struct ao2_container_hash *self, void *where, ao2_prnt_fn *prnt, ao2_prnt_obj_fn *prnt_obj)
{
#define FORMAT  "%6s, %16s, %16s, %16s, %16s, %s\n"
#define FORMAT2 "%6d, %16p, %16p, %16p, %16p, "

	int bucket;
	int suppressed_buckets = 0;
	struct hash_bucket_node *node;

	prnt(where, "Number of buckets: %d\n\n", self->n_buckets);

	prnt(where, FORMAT, "Bucket", "Node", "Prev", "Next", "Obj", "Key");
	for (bucket = 0; bucket < self->n_buckets; ++bucket) {
		node = AST_DLLIST_FIRST(&self->buckets[bucket].list);
		if (node) {
			suppressed_buckets = 0;
			do {
				prnt(where, FORMAT2,
					bucket,
					node,
					AST_DLLIST_PREV(node, links),
					AST_DLLIST_NEXT(node, links),
					node->common.obj);
				if (node->common.obj && prnt_obj) {
					prnt_obj(node->common.obj, where, prnt);
				}
				prnt(where, "\n");

				node = AST_DLLIST_NEXT(node, links);
			} while (node);
		} else if (!suppressed_buckets) {
			suppressed_buckets = 1;
			prnt(where, "...\n");
		}
	}

#undef FORMAT
#undef FORMAT2
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
/*!
 * \internal
 * \brief Display statistics of the specified container.
 * \since 12.0.0
 *
 * \param self Container to display statistics.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 *
 * \note The container is already locked for reading.
 *
 * \return Nothing
 */
static void hash_ao2_stats(struct ao2_container_hash *self, void *where, ao2_prnt_fn *prnt)
{
#define FORMAT  "%10.10s %10.10s %10.10s\n"
#define FORMAT2 "%10d %10d %10d\n"

	int bucket;
	int suppressed_buckets = 0;

	prnt(where, "Number of buckets: %d\n\n", self->n_buckets);

	prnt(where, FORMAT, "Bucket", "Objects", "Max");
	for (bucket = 0; bucket < self->n_buckets; ++bucket) {
		if (self->buckets[bucket].max_elements) {
			suppressed_buckets = 0;
			prnt(where, FORMAT2, bucket, self->buckets[bucket].elements,
				self->buckets[bucket].max_elements);
		} else if (!suppressed_buckets) {
			suppressed_buckets = 1;
			prnt(where, "...\n");
		}
	}

#undef FORMAT
#undef FORMAT2
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
/*!
 * \internal
 * \brief Perform an integrity check on the specified container.
 * \since 12.0.0
 *
 * \param self Container to check integrity.
 *
 * \note The container is already locked for reading.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int hash_ao2_integrity(struct ao2_container_hash *self)
{
	int bucket_exp;
	int bucket;
	int count_obj;
	int count_total_obj;
	int count_total_node;
	void *obj_last;
	struct hash_bucket_node *node;
	struct hash_bucket_node *prev;
	struct hash_bucket_node *next;

	count_total_obj = 0;
	count_total_node = 0;

	/* For each bucket in the container. */
	for (bucket = 0; bucket < self->n_buckets; ++bucket) {
		if (!AST_DLLIST_FIRST(&self->buckets[bucket].list)
			&& !AST_DLLIST_LAST(&self->buckets[bucket].list)) {
			/* The bucket list is empty. */
			continue;
		}

		count_obj = 0;
		obj_last = NULL;

		/* Check bucket list links and nodes. */
		node = AST_DLLIST_LAST(&self->buckets[bucket].list);
		if (!node) {
			ast_log(LOG_ERROR, "Bucket %d list tail is NULL when it should not be!\n",
				bucket);
			return -1;
		}
		if (AST_DLLIST_NEXT(node, links)) {
			ast_log(LOG_ERROR, "Bucket %d list tail node is not the last node!\n",
				bucket);
			return -1;
		}
		node = AST_DLLIST_FIRST(&self->buckets[bucket].list);
		if (!node) {
			ast_log(LOG_ERROR, "Bucket %d list head is NULL when it should not be!\n",
				bucket);
			return -1;
		}
		if (AST_DLLIST_PREV(node, links)) {
			ast_log(LOG_ERROR, "Bucket %d list head node is not the first node!\n",
				bucket);
			return -1;
		}
		for (; node; node = next) {
			/* Check backward link. */
			prev = AST_DLLIST_PREV(node, links);
			if (prev) {
				if (prev == node) {
					ast_log(LOG_ERROR, "Bucket %d list node's prev pointer points to itself!\n",
						bucket);
					return -1;
				}
				if (node != AST_DLLIST_NEXT(prev, links)) {
					ast_log(LOG_ERROR, "Bucket %d list node's prev node does not link back!\n",
						bucket);
					return -1;
				}
			} else if (node != AST_DLLIST_FIRST(&self->buckets[bucket].list)) {
				ast_log(LOG_ERROR, "Bucket %d backward list chain is broken!\n",
					bucket);
				return -1;
			}

			/* Check forward link. */
			next = AST_DLLIST_NEXT(node, links);
			if (next) {
				if (next == node) {
					ast_log(LOG_ERROR, "Bucket %d list node's next pointer points to itself!\n",
						bucket);
					return -1;
				}
				if (node != AST_DLLIST_PREV(next, links)) {
					ast_log(LOG_ERROR, "Bucket %d list node's next node does not link back!\n",
						bucket);
					return -1;
				}
			} else if (node != AST_DLLIST_LAST(&self->buckets[bucket].list)) {
				ast_log(LOG_ERROR, "Bucket %d forward list chain is broken!\n",
					bucket);
				return -1;
			}

			if (bucket != node->my_bucket) {
				ast_log(LOG_ERROR, "Bucket %d node claims to be in bucket %d!\n",
					bucket, node->my_bucket);
				return -1;
			}

			++count_total_node;
			if (!node->common.obj) {
				/* Node is empty. */
				continue;
			}
			++count_obj;

			/* Check container hash key for expected bucket. */
			bucket_exp = abs(self->hash_fn(node->common.obj, OBJ_SEARCH_OBJECT));
			bucket_exp %= self->n_buckets;
			if (bucket != bucket_exp) {
				ast_log(LOG_ERROR, "Bucket %d node hashes to bucket %d!\n",
					bucket, bucket_exp);
				return -1;
			}

			/* Check sort if configured. */
			if (self->common.sort_fn) {
				if (obj_last
					&& self->common.sort_fn(obj_last, node->common.obj, OBJ_SEARCH_OBJECT) > 0) {
					ast_log(LOG_ERROR, "Bucket %d nodes out of sorted order!\n",
						bucket);
					return -1;
				}
				obj_last = node->common.obj;
			}
		}

		/* Check bucket obj count statistic. */
		if (count_obj != self->buckets[bucket].elements) {
			ast_log(LOG_ERROR, "Bucket %d object count of %d does not match stat of %d!\n",
				bucket, count_obj, self->buckets[bucket].elements);
			return -1;
		}

		/* Accumulate found object counts. */
		count_total_obj += count_obj;
	}

	/* Check total obj count. */
	if (count_total_obj != ao2_container_count(&self->common)) {
		ast_log(LOG_ERROR,
			"Total object count of %d does not match ao2_container_count() of %d!\n",
			count_total_obj, ao2_container_count(&self->common));
		return -1;
	}

	/* Check total node count. */
	if (count_total_node != self->common.nodes) {
		ast_log(LOG_ERROR, "Total node count of %d does not match stat of %d!\n",
			count_total_node, self->common.nodes);
		return -1;
	}

	return 0;
}
#endif	/* defined(AO2_DEBUG) */

/*! Hash container virtual method table. */
static const struct ao2_container_methods v_table_hash = {
	.alloc_empty_clone = (ao2_container_alloc_empty_clone_fn) hash_ao2_alloc_empty_clone,
	.alloc_empty_clone_debug =
		(ao2_container_alloc_empty_clone_debug_fn) hash_ao2_alloc_empty_clone_debug,
	.new_node = (ao2_container_new_node_fn) hash_ao2_new_node,
	.insert = (ao2_container_insert_fn) hash_ao2_insert_node,
	.traverse_first = (ao2_container_find_first_fn) hash_ao2_find_first,
	.traverse_next = (ao2_container_find_next_fn) hash_ao2_find_next,
	.iterator_next = (ao2_iterator_next_fn) hash_ao2_iterator_next,
	.destroy = (ao2_container_destroy_fn) hash_ao2_destroy,
#if defined(AO2_DEBUG)
	.link_stat = hash_ao2_link_node_stat,
	.unlink_stat = hash_ao2_unlink_node_stat,
	.dump = (ao2_container_display) hash_ao2_dump,
	.stats = (ao2_container_statistics) hash_ao2_stats,
	.integrity = (ao2_container_integrity) hash_ao2_integrity,
#endif	/* defined(AO2_DEBUG) */
};

/*!
 * \brief always zero hash function
 *
 * it is convenient to have a hash function that always returns 0.
 * This is basically used when we want to have a container that is
 * a simple linked list.
 *
 * \returns 0
 */
static int hash_zero(const void *user_obj, const int flags)
{
	return 0;
}

/*!
 * \brief Initialize a hash container with the desired number of buckets.
 *
 * \param self Container to initialize.
 * \param options Container behaviour options (See enum ao2_container_opts)
 * \param n_buckets Number of buckets for hash
 * \param hash_fn Pointer to a function computing a hash value.
 * \param sort_fn Pointer to a sort function.
 * \param cmp_fn Pointer to a compare function used by ao2_find.
 *
 * \return A pointer to a struct container.
 */
static struct ao2_container *hash_ao2_container_init(
	struct ao2_container_hash *self, unsigned int options, unsigned int n_buckets,
	ao2_hash_fn *hash_fn, ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn)
{
	if (!self) {
		return NULL;
	}

	self->common.v_table = &v_table_hash;
	self->common.sort_fn = sort_fn;
	self->common.cmp_fn = cmp_fn;
	self->common.options = options;
	self->hash_fn = hash_fn ? hash_fn : hash_zero;
	self->n_buckets = n_buckets;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_containers, 1);
#endif	/* defined(AO2_DEBUG) */

	return (struct ao2_container *) self;
}

struct ao2_container *__ao2_container_alloc_hash(unsigned int ao2_options,
	unsigned int container_options, unsigned int n_buckets, ao2_hash_fn *hash_fn,
	ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn)
{
	unsigned int num_buckets;
	size_t container_size;
	struct ao2_container_hash *self;

	num_buckets = hash_fn ? n_buckets : 1;
	container_size = sizeof(struct ao2_container_hash) + num_buckets * sizeof(struct hash_bucket);

	self = ao2_t_alloc_options(container_size, container_destruct, ao2_options,
		"New hash container");
	return hash_ao2_container_init(self, container_options, num_buckets,
		hash_fn, sort_fn, cmp_fn);
}

struct ao2_container *__ao2_container_alloc_hash_debug(unsigned int ao2_options,
	unsigned int container_options, unsigned int n_buckets, ao2_hash_fn *hash_fn,
	ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn,
	const char *tag, const char *file, int line, const char *func, int ref_debug)
{
	unsigned int num_buckets;
	size_t container_size;
	struct ao2_container_hash *self;

	num_buckets = hash_fn ? n_buckets : 1;
	container_size = sizeof(struct ao2_container_hash) + num_buckets * sizeof(struct hash_bucket);

	self = __ao2_alloc_debug(container_size,
		ref_debug ? container_destruct_debug : container_destruct, ao2_options,
		tag, file, line, func, ref_debug);
	return hash_ao2_container_init(self, container_options, num_buckets, hash_fn,
		sort_fn, cmp_fn);
}

struct ao2_container *__ao2_container_alloc_list(unsigned int ao2_options,
	unsigned int container_options, ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn)
{
	return __ao2_container_alloc_hash(ao2_options, container_options, 1, NULL, sort_fn,
		cmp_fn);
}

struct ao2_container *__ao2_container_alloc_list_debug(unsigned int ao2_options,
	unsigned int container_options, ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn,
	const char *tag, const char *file, int line, const char *func, int ref_debug)
{
	return __ao2_container_alloc_hash_debug(ao2_options, container_options, 1, NULL,
		sort_fn, cmp_fn, tag, file, line, func, ref_debug);
}

