/* astobj2 - replacement containers for asterisk data structures.
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
 * \brief Functions implementing astobj2 objects.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 */

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "astobj2_private.h"
#include "astobj2_container_private.h"
#include "asterisk/cli.h"

/*!
 * return the number of elements in the container
 */
int ao2_container_count(struct ao2_container *c)
{
	return ast_atomic_fetchadd_int(&c->elements, 0);
}

int __container_unlink_node_debug(struct ao2_container_node *node, uint32_t flags,
	const char *tag, const char *file, int line, const char *func)
{
	struct ao2_container *container = node->my_container;

	if (container == NULL && (flags & AO2_UNLINK_NODE_DEC_COUNT)) {
		return 0;
	}

	if ((flags & AO2_UNLINK_NODE_UNLINK_OBJECT)
		&& !(flags & AO2_UNLINK_NODE_NOUNREF_OBJECT)) {
		if (tag) {
			__ao2_ref_debug(node->obj, -1, tag, file, line, func);
		} else {
			ao2_t_ref(node->obj, -1, "Remove obj from container");
		}
	}

	node->obj = NULL;

	if (flags & AO2_UNLINK_NODE_DEC_COUNT) {
		ast_atomic_fetchadd_int(&container->elements, -1);
#if defined(AO2_DEBUG)
		{
			int empty = container->nodes - container->elements;

			if (container->max_empty_nodes < empty) {
				container->max_empty_nodes = empty;
			}
			if (container->v_table->unlink_stat) {
				container->v_table->unlink_stat(container, node);
			}
		}
#endif	/* defined(AO2_DEBUG) */
	}

	if (flags & AO2_UNLINK_NODE_UNREF_NODE) {
		/* Remove node from container */
		__ao2_ref(node, -1);
	}

	return 1;
}

/*!
 * \internal
 * \brief Link an object into this container.  (internal)
 *
 * \param self Container to operate upon.
 * \param obj_new Object to insert into the container.
 * \param flags search_flags to control linking the object.  (OBJ_NOLOCK)
 * \param tag used for debugging.
 * \param file Debug file name invoked from
 * \param line Debug line invoked from
 * \param func Debug function name invoked from
 *
 * \retval 0 on errors.
 * \retval 1 on success.
 */
static int internal_ao2_link(struct ao2_container *self, void *obj_new, int flags, const char *tag, const char *file, int line, const char *func)
{
	int res;
	enum ao2_lock_req orig_lock;
	struct ao2_container_node *node;

	if (!is_ao2_object(obj_new) || !is_ao2_object(self)
		|| !self->v_table || !self->v_table->new_node || !self->v_table->insert) {
		/* Sanity checks. */
		ast_assert(0);
		return 0;
	}

	if (flags & OBJ_NOLOCK) {
		orig_lock = __adjust_lock(self, AO2_LOCK_REQ_WRLOCK, 1);
	} else {
		ao2_wrlock(self);
		orig_lock = AO2_LOCK_REQ_MUTEX;
	}

	res = 0;
	node = self->v_table->new_node(self, obj_new, tag, file, line, func);
	if (node) {
#if defined(AO2_DEBUG)
		if (ao2_container_check(self, OBJ_NOLOCK)) {
			ast_log(LOG_ERROR, "Container integrity failed before insert.\n");
		}
#endif	/* defined(AO2_DEBUG) */

		/* Insert the new node. */
		switch (self->v_table->insert(self, node)) {
		case AO2_CONTAINER_INSERT_NODE_INSERTED:
			node->is_linked = 1;
			ast_atomic_fetchadd_int(&self->elements, 1);
#if defined(AO2_DEBUG)
			AO2_DEVMODE_STAT(++self->nodes);
			if (self->v_table->link_stat) {
				self->v_table->link_stat(self, node);
			}
#endif	/* defined(AO2_DEBUG) */
			/* Fall through */
		case AO2_CONTAINER_INSERT_NODE_OBJ_REPLACED:
#if defined(AO2_DEBUG)
			if (ao2_container_check(self, OBJ_NOLOCK)) {
				ast_log(LOG_ERROR, "Container integrity failed after insert or replace.\n");
			}
#endif	/* defined(AO2_DEBUG) */
			res = 1;
			break;
		case AO2_CONTAINER_INSERT_NODE_REJECTED:
			__ao2_ref(node, -1);
			break;
		}
	}

	if (flags & OBJ_NOLOCK) {
		__adjust_lock(self, orig_lock, 0);
	} else {
		ao2_unlock(self);
	}

	return res;
}

int __ao2_link_debug(struct ao2_container *c, void *obj_new, int flags, const char *tag, const char *file, int line, const char *func)
{
	return internal_ao2_link(c, obj_new, flags, tag, file, line, func);
}

int __ao2_link(struct ao2_container *c, void *obj_new, int flags)
{
	return internal_ao2_link(c, obj_new, flags, NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__);
}

/*!
 * \brief another convenience function is a callback that matches on address
 */
int ao2_match_by_addr(void *user_data, void *arg, int flags)
{
	return (user_data == arg) ? (CMP_MATCH | CMP_STOP) : 0;
}

/*
 * Unlink an object from the container
 * and destroy the associated * bucket_entry structure.
 */
void *__ao2_unlink_debug(struct ao2_container *c, void *user_data, int flags,
	const char *tag, const char *file, int line, const char *func)
{
	if (!is_ao2_object(user_data)) {
		/* Sanity checks. */
		ast_assert(0);
		return NULL;
	}

	flags &= ~OBJ_SEARCH_MASK;
	flags |= (OBJ_UNLINK | OBJ_SEARCH_OBJECT | OBJ_NODATA);
	__ao2_callback_debug(c, flags, ao2_match_by_addr, user_data, tag, file, line, func);

	return NULL;
}

void *__ao2_unlink(struct ao2_container *c, void *user_data, int flags)
{
	if (!is_ao2_object(user_data)) {
		/* Sanity checks. */
		ast_assert(0);
		return NULL;
	}

	flags &= ~OBJ_SEARCH_MASK;
	flags |= (OBJ_UNLINK | OBJ_SEARCH_OBJECT | OBJ_NODATA);
	__ao2_callback(c, flags, ao2_match_by_addr, user_data);

	return NULL;
}

/*!
 * \brief special callback that matches all
 */
static int cb_true(void *user_data, void *arg, int flags)
{
	return CMP_MATCH;
}

/*!
 * \brief similar to cb_true, but is an ao2_callback_data_fn instead
 */
static int cb_true_data(void *user_data, void *arg, void *data, int flags)
{
	return CMP_MATCH;
}

/*!
 * \internal
 * \brief Traverse the container.  (internal)
 *
 * \param self Container to operate upon.
 * \param flags search_flags to control traversing the container
 * \param cb_fn Comparison callback function.
 * \param arg Comparison callback arg parameter.
 * \param data Data comparison callback data parameter.
 * \param type Type of comparison callback cb_fn.
 * \param tag used for debugging.
 * \param file Debug file name invoked from
 * \param line Debug line invoked from
 * \param func Debug function name invoked from
 *
 * \retval NULL on failure or no matching object found.
 *
 * \retval object found if OBJ_MULTIPLE is not set in the flags
 * parameter.
 *
 * \retval ao2_iterator pointer if OBJ_MULTIPLE is set in the
 * flags parameter.  The iterator must be destroyed with
 * ao2_iterator_destroy() when the caller no longer needs it.
 */
static void *internal_ao2_traverse(struct ao2_container *self, enum search_flags flags,
	void *cb_fn, void *arg, void *data, enum ao2_callback_type type,
	const char *tag, const char *file, int line, const char *func)
{
	void *ret;
	ao2_callback_fn *cb_default = NULL;
	ao2_callback_data_fn *cb_withdata = NULL;
	struct ao2_container_node *node;
	void *traversal_state;

	enum ao2_lock_req orig_lock;
	struct ao2_container *multi_container = NULL;
	struct ao2_iterator *multi_iterator = NULL;

	if (!is_ao2_object(self) || !self->v_table || !self->v_table->traverse_first
		|| !self->v_table->traverse_next) {
		/* Sanity checks. */
		ast_assert(0);
		return NULL;
	}

	/*
	 * This logic is used so we can support OBJ_MULTIPLE with OBJ_NODATA
	 * turned off.  This if statement checks for the special condition
	 * where multiple items may need to be returned.
	 */
	if ((flags & (OBJ_MULTIPLE | OBJ_NODATA)) == OBJ_MULTIPLE) {
		/* we need to return an ao2_iterator with the results,
		 * as there could be more than one. the iterator will
		 * hold the only reference to a container that has all the
		 * matching objects linked into it, so when the iterator
		 * is destroyed, the container will be automatically
		 * destroyed as well.
		 */
		multi_container = ao2_t_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL,
			NULL, "OBJ_MULTIPLE return container creation");
		if (!multi_container) {
			return NULL;
		}
		if (!(multi_iterator = ast_calloc(1, sizeof(*multi_iterator)))) {
			ao2_t_ref(multi_container, -1, "OBJ_MULTIPLE interator creation failed.");
			return NULL;
		}
	}

	if (!cb_fn) {
		/* Match everything if no callback match function provided. */
		if (type == AO2_CALLBACK_WITH_DATA) {
			cb_withdata = cb_true_data;
		} else {
			cb_default = cb_true;
		}
	} else {
		/*
		 * We do this here to avoid the per object casting penalty (even
		 * though that is probably optimized away anyway).
		 */
		if (type == AO2_CALLBACK_WITH_DATA) {
			cb_withdata = cb_fn;
		} else {
			cb_default = cb_fn;
		}
	}

	/* avoid modifications to the content */
	if (flags & OBJ_NOLOCK) {
		if (flags & OBJ_UNLINK) {
			orig_lock = __adjust_lock(self, AO2_LOCK_REQ_WRLOCK, 1);
		} else {
			orig_lock = __adjust_lock(self, AO2_LOCK_REQ_RDLOCK, 1);
		}
	} else {
		orig_lock = AO2_LOCK_REQ_MUTEX;
		if (flags & OBJ_UNLINK) {
			ao2_wrlock(self);
		} else {
			ao2_rdlock(self);
		}
	}

	/* Create a buffer for the traversal state. */
	traversal_state = alloca(AO2_TRAVERSAL_STATE_SIZE);

	ret = NULL;
	for (node = self->v_table->traverse_first(self, flags, arg, traversal_state);
		node;
		node = self->v_table->traverse_next(self, traversal_state, node)) {
		int match;

		/* Visit the current node. */
		match = (CMP_MATCH | CMP_STOP);
		if (type == AO2_CALLBACK_WITH_DATA) {
			match &= cb_withdata(node->obj, arg, data, flags);
		} else {
			match &= cb_default(node->obj, arg, flags);
		}
		if (match == 0) {
			/* no match, no stop, continue */
			continue;
		}
		if (match == CMP_STOP) {
			/* no match but stop, we are done */
			break;
		}

		/*
		 * CMP_MATCH is set here
		 *
		 * we found the object, performing operations according to flags
		 */
		if (node->obj) {
			/* The object is still in the container. */
			if (!(flags & OBJ_NODATA)) {
				/*
				 * We are returning the object, record the value.  It is
				 * important to handle this case before the unlink.
				 */
				if (multi_container) {
					/*
					 * Link the object into the container that will hold the
					 * results.
					 */
					if (tag) {
						__ao2_link_debug(multi_container, node->obj, flags,
							tag, file, line, func);
					} else {
						__ao2_link(multi_container, node->obj, flags);
					}
				} else {
					ret = node->obj;
					/* Returning a single object. */
					if (!(flags & OBJ_UNLINK)) {
						/*
						 * Bump the ref count since we are not going to unlink and
						 * transfer the container's object ref to the returned object.
						 */
						if (tag) {
							__ao2_ref_debug(ret, 1, tag, file, line, func);
						} else {
							ao2_t_ref(ret, 1, "Traversal found object");
						}
					}
				}
			}

			if (flags & OBJ_UNLINK) {
				int ulflag = AO2_UNLINK_NODE_UNREF_NODE | AO2_UNLINK_NODE_DEC_COUNT;
				if (multi_container || (flags & OBJ_NODATA)) {
					ulflag |= AO2_UNLINK_NODE_UNLINK_OBJECT;
				}
				__container_unlink_node_debug(node, ulflag, tag, file, line, func);
			}
		}

		if ((match & CMP_STOP) || !(flags & OBJ_MULTIPLE)) {
			/* We found our only (or last) match, so we are done */
			break;
		}
	}
	if (self->v_table->traverse_cleanup) {
		self->v_table->traverse_cleanup(traversal_state);
	}
	if (node) {
		/* Unref the node from self->v_table->traverse_first/traverse_next() */
		__ao2_ref(node, -1);
	}

	if (flags & OBJ_NOLOCK) {
		__adjust_lock(self, orig_lock, 0);
	} else {
		ao2_unlock(self);
	}

	/* if multi_container was created, we are returning multiple objects */
	if (multi_container) {
		*multi_iterator = ao2_iterator_init(multi_container,
			AO2_ITERATOR_UNLINK | AO2_ITERATOR_MALLOCD);
		ao2_t_ref(multi_container, -1,
			"OBJ_MULTIPLE for multiple objects traversal complete.");
		return multi_iterator;
	} else {
		return ret;
	}
}

void *__ao2_callback_debug(struct ao2_container *c, enum search_flags flags,
	ao2_callback_fn *cb_fn, void *arg, const char *tag, const char *file, int line,
	const char *func)
{
	return internal_ao2_traverse(c, flags, cb_fn, arg, NULL, AO2_CALLBACK_DEFAULT, tag, file, line, func);
}

void *__ao2_callback(struct ao2_container *c, enum search_flags flags,
	ao2_callback_fn *cb_fn, void *arg)
{
	return internal_ao2_traverse(c, flags, cb_fn, arg, NULL, AO2_CALLBACK_DEFAULT, NULL, NULL, 0, NULL);
}

void *__ao2_callback_data_debug(struct ao2_container *c, enum search_flags flags,
	ao2_callback_data_fn *cb_fn, void *arg, void *data, const char *tag, const char *file,
	int line, const char *func)
{
	return internal_ao2_traverse(c, flags, cb_fn, arg, data, AO2_CALLBACK_WITH_DATA, tag, file, line, func);
}

void *__ao2_callback_data(struct ao2_container *c, enum search_flags flags,
	ao2_callback_data_fn *cb_fn, void *arg, void *data)
{
	return internal_ao2_traverse(c, flags, cb_fn, arg, data, AO2_CALLBACK_WITH_DATA, NULL, NULL, 0, NULL);
}

/*!
 * the find function just invokes the default callback with some reasonable flags.
 */
void *__ao2_find_debug(struct ao2_container *c, const void *arg, enum search_flags flags,
	const char *tag, const char *file, int line, const char *func)
{
	void *arged = (void *) arg;/* Done to avoid compiler const warning */

	if (!c) {
		/* Sanity checks. */
		ast_assert(0);
		return NULL;
	}
	return __ao2_callback_debug(c, flags, c->cmp_fn, arged, tag, file, line, func);
}

void *__ao2_find(struct ao2_container *c, const void *arg, enum search_flags flags)
{
	void *arged = (void *) arg;/* Done to avoid compiler const warning */

	if (!c) {
		/* Sanity checks. */
		ast_assert(0);
		return NULL;
	}
	return __ao2_callback(c, flags, c->cmp_fn, arged);
}

/*!
 * initialize an iterator so we start from the first object
 */
struct ao2_iterator ao2_iterator_init(struct ao2_container *c, int flags)
{
	struct ao2_iterator a = {
		.c = c,
		.flags = flags
	};

	ao2_t_ref(c, +1, "Init iterator with container.");

	return a;
}

void ao2_iterator_restart(struct ao2_iterator *iter)
{
	/* Release the last container node reference if we have one. */
	if (iter->last_node) {
		enum ao2_lock_req orig_lock;

		/*
		 * Do a read lock in case the container node unref does not
		 * destroy the node.  If the container node is destroyed then
		 * the lock will be upgraded to a write lock.
		 */
		if (iter->flags & AO2_ITERATOR_DONTLOCK) {
			orig_lock = __adjust_lock(iter->c, AO2_LOCK_REQ_RDLOCK, 1);
		} else {
			orig_lock = AO2_LOCK_REQ_MUTEX;
			ao2_rdlock(iter->c);
		}

		__ao2_ref(iter->last_node, -1);
		iter->last_node = NULL;

		if (iter->flags & AO2_ITERATOR_DONTLOCK) {
			__adjust_lock(iter->c, orig_lock, 0);
		} else {
			ao2_unlock(iter->c);
		}
	}

	/* The iteration is no longer complete. */
	iter->complete = 0;
}

void ao2_iterator_destroy(struct ao2_iterator *iter)
{
	/* Release any last container node reference. */
	ao2_iterator_restart(iter);

	/* Release the iterated container reference. */
	ao2_t_ref(iter->c, -1, "Unref iterator in ao2_iterator_destroy");
	iter->c = NULL;

	/* Free the malloced iterator. */
	if (iter->flags & AO2_ITERATOR_MALLOCD) {
		ast_free(iter);
	}
}

void ao2_iterator_cleanup(struct ao2_iterator *iter)
{
	if (iter) {
		ao2_iterator_destroy(iter);
	}
}

/*
 * move to the next element in the container.
 */
static void *internal_ao2_iterator_next(struct ao2_iterator *iter, const char *tag, const char *file, int line, const char *func)
{
	enum ao2_lock_req orig_lock;
	struct ao2_container_node *node;
	void *ret;

	if (!is_ao2_object(iter->c) || !iter->c->v_table || !iter->c->v_table->iterator_next) {
		/* Sanity checks. */
		ast_assert(0);
		return NULL;
	}

	if (iter->complete) {
		/* Don't return any more objects. */
		return NULL;
	}

	if (iter->flags & AO2_ITERATOR_DONTLOCK) {
		if (iter->flags & AO2_ITERATOR_UNLINK) {
			orig_lock = __adjust_lock(iter->c, AO2_LOCK_REQ_WRLOCK, 1);
		} else {
			orig_lock = __adjust_lock(iter->c, AO2_LOCK_REQ_RDLOCK, 1);
		}
	} else {
		orig_lock = AO2_LOCK_REQ_MUTEX;
		if (iter->flags & AO2_ITERATOR_UNLINK) {
			ao2_wrlock(iter->c);
		} else {
			ao2_rdlock(iter->c);
		}
	}

	node = iter->c->v_table->iterator_next(iter->c, iter->last_node, iter->flags);
	if (node) {
		ret = node->obj;

		if (iter->flags & AO2_ITERATOR_UNLINK) {
			/* Transfer the object ref from the container to the returned object. */
			__container_unlink_node_debug(node, AO2_UNLINK_NODE_DEC_COUNT, tag, file, line, func);

			/* Transfer the container's node ref to the iterator. */
		} else {
			/* Bump ref of returned object */
			if (tag) {
				__ao2_ref_debug(ret, +1, tag, file, line, func);
			} else {
				ao2_t_ref(ret, +1, "Next iterator object.");
			}

			/* Bump the container's node ref for the iterator. */
			__ao2_ref(node, +1);
		}
	} else {
		/* The iteration has completed. */
		iter->complete = 1;
		ret = NULL;
	}

	/* Replace the iterator's node */
	if (iter->last_node) {
		__ao2_ref(iter->last_node, -1);
	}
	iter->last_node = node;

	if (iter->flags & AO2_ITERATOR_DONTLOCK) {
		__adjust_lock(iter->c, orig_lock, 0);
	} else {
		ao2_unlock(iter->c);
	}

	return ret;
}

void *__ao2_iterator_next_debug(struct ao2_iterator *iter, const char *tag, const char *file, int line, const char *func)
{
	return internal_ao2_iterator_next(iter, tag, file, line, func);
}

void *__ao2_iterator_next(struct ao2_iterator *iter)
{
	return internal_ao2_iterator_next(iter, NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__);
}

int ao2_iterator_count(struct ao2_iterator *iter)
{
	return ao2_container_count(iter->c);
}

void container_destruct(void *_c)
{
	struct ao2_container *c = _c;

	/* Unlink any stored objects in the container. */
	c->destroying = 1;
	__ao2_callback(c, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);

	/* Perform any extra container cleanup. */
	if (c->v_table && c->v_table->destroy) {
		c->v_table->destroy(c);
	}

#if defined(AO2_DEBUG)
	ast_atomic_fetchadd_int(&ao2.total_containers, -1);
#endif
}

void container_destruct_debug(void *_c)
{
	struct ao2_container *c = _c;

	/* Unlink any stored objects in the container. */
	c->destroying = 1;
	__ao2_callback_debug(c, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL,
		"container_destruct_debug called", __FILE__, __LINE__, __PRETTY_FUNCTION__);

	/* Perform any extra container cleanup. */
	if (c->v_table && c->v_table->destroy) {
		c->v_table->destroy(c);
	}

#if defined(AO2_DEBUG)
	ast_atomic_fetchadd_int(&ao2.total_containers, -1);
#endif
}

/*!
 * \internal
 * \brief Put obj into the arg container.
 * \since 11.0
 *
 * \param obj  pointer to the (user-defined part) of an object.
 * \param arg callback argument from ao2_callback()
 * \param flags flags from ao2_callback()
 *
 * \retval 0 on success.
 * \retval CMP_STOP|CMP_MATCH on error.
 */
static int dup_obj_cb(void *obj, void *arg, int flags)
{
	struct ao2_container *dest = arg;

	return __ao2_link(dest, obj, OBJ_NOLOCK) ? 0 : (CMP_MATCH | CMP_STOP);
}

int ao2_container_dup(struct ao2_container *dest, struct ao2_container *src, enum search_flags flags)
{
	void *obj;
	int res = 0;

	if (!(flags & OBJ_NOLOCK)) {
		ao2_rdlock(src);
		ao2_wrlock(dest);
	}
	obj = __ao2_callback(src, OBJ_NOLOCK, dup_obj_cb, dest);
	if (obj) {
		/* Failed to put this obj into the dest container. */
		ao2_t_ref(obj, -1, "Failed to put this object into the dest container.");

		/* Remove all items from the dest container. */
		__ao2_callback(dest, OBJ_NOLOCK | OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL,
			NULL);
		res = -1;
	}
	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(dest);
		ao2_unlock(src);
	}

	return res;
}

struct ao2_container *__ao2_container_clone(struct ao2_container *orig, enum search_flags flags)
{
	struct ao2_container *clone;
	int failed;

	/* Create the clone container with the same properties as the original. */
	if (!is_ao2_object(orig) || !orig->v_table || !orig->v_table->alloc_empty_clone) {
		/* Sanity checks. */
		ast_assert(0);
		return NULL;
	}
	clone = orig->v_table->alloc_empty_clone(orig);
	if (!clone) {
		return NULL;
	}

	if (flags & OBJ_NOLOCK) {
		ao2_wrlock(clone);
	}
	failed = ao2_container_dup(clone, orig, flags);
	if (flags & OBJ_NOLOCK) {
		ao2_unlock(clone);
	}
	if (failed) {
		/* Object copy into the clone container failed. */
		ao2_t_ref(clone, -1, "Clone creation failed.");
		clone = NULL;
	}
	return clone;
}

struct ao2_container *__ao2_container_clone_debug(struct ao2_container *orig, enum search_flags flags, const char *tag, const char *file, int line, const char *func, int ref_debug)
{
	struct ao2_container *clone;
	int failed;

	/* Create the clone container with the same properties as the original. */
	if (!is_ao2_object(orig) || !orig->v_table || !orig->v_table->alloc_empty_clone_debug) {
		/* Sanity checks. */
		ast_assert(0);
		return NULL;
	}
	clone = orig->v_table->alloc_empty_clone_debug(orig, tag, file, line, func, ref_debug);
	if (!clone) {
		return NULL;
	}

	if (flags & OBJ_NOLOCK) {
		ao2_wrlock(clone);
	}
	failed = ao2_container_dup(clone, orig, flags);
	if (flags & OBJ_NOLOCK) {
		ao2_unlock(clone);
	}
	if (failed) {
		/* Object copy into the clone container failed. */
		if (ref_debug) {
			__ao2_ref_debug(clone, -1, tag, file, line, func);
		} else {
			ao2_t_ref(clone, -1, "Clone creation failed.");
		}
		clone = NULL;
	}
	return clone;
}

void ao2_container_dump(struct ao2_container *self, enum search_flags flags, const char *name, void *where, ao2_prnt_fn *prnt, ao2_prnt_obj_fn *prnt_obj)
{
	if (!is_ao2_object(self) || !self->v_table) {
		prnt(where, "Invalid container\n");
		ast_assert(0);
		return;
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_rdlock(self);
	}
	if (name) {
		prnt(where, "Container name: %s\n", name);
	}
#if defined(AO2_DEBUG)
	if (self->v_table->dump) {
		self->v_table->dump(self, where, prnt, prnt_obj);
	} else
#endif	/* defined(AO2_DEBUG) */
	{
		prnt(where, "Container dump not available.\n");
	}
	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(self);
	}
}

void ao2_container_stats(struct ao2_container *self, enum search_flags flags, const char *name, void *where, ao2_prnt_fn *prnt)
{
	if (!is_ao2_object(self) || !self->v_table) {
		prnt(where, "Invalid container\n");
		ast_assert(0);
		return;
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_rdlock(self);
	}
	if (name) {
		prnt(where, "Container name: %s\n", name);
	}
	prnt(where, "Number of objects: %d\n", self->elements);
#if defined(AO2_DEBUG)
	prnt(where, "Number of nodes: %d\n", self->nodes);
	prnt(where, "Number of empty nodes: %d\n", self->nodes - self->elements);
	/*
	 * XXX
	 * If the max_empty_nodes count gets out of single digits you
	 * likely have a code path where ao2_iterator_destroy() is not
	 * called.
	 *
	 * Empty nodes do not harm the container but they do make
	 * container operations less efficient.
	 */
	prnt(where, "Maximum empty nodes: %d\n", self->max_empty_nodes);
	if (self->v_table->stats) {
		self->v_table->stats(self, where, prnt);
	}
#endif	/* defined(AO2_DEBUG) */
	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(self);
	}
}

int ao2_container_check(struct ao2_container *self, enum search_flags flags)
{
	int res = 0;

	if (!is_ao2_object(self) || !self->v_table) {
		/* Sanity checks. */
		ast_assert(0);
		return -1;
	}
#if defined(AO2_DEBUG)
	if (!self->v_table->integrity) {
		/* No ingetrigy check available.  Assume container is ok. */
		return 0;
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_rdlock(self);
	}
	res = self->v_table->integrity(self);
	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(self);
	}
#endif	/* defined(AO2_DEBUG) */
	return res;
}

#if defined(AO2_DEBUG)
static struct ao2_container *reg_containers;

struct ao2_reg_container {
	/*! Registered container pointer. */
	struct ao2_container *registered;
	/*! Callback function to print the given object's key. (NULL if not available) */
	ao2_prnt_obj_fn *prnt_obj;
	/*! Name container registered under. */
	char name[1];
};

struct ao2_reg_partial_key {
	/*! Length of partial key match. */
	int len;
	/*! Registration partial key name. */
	const char *name;
};

struct ao2_reg_match {
	/*! The nth match to find. */
	int find_nth;
	/*! Count of the matches already found. */
	int count;
};
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
static int ao2_reg_sort_cb(const void *obj_left, const void *obj_right, int flags)
{
	const struct ao2_reg_container *reg_left = obj_left;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		{
			const struct ao2_reg_container *reg_right = obj_right;

			cmp = strcasecmp(reg_left->name, reg_right->name);
		}
		break;
	case OBJ_SEARCH_KEY:
		{
			const char *name = obj_right;

			cmp = strcasecmp(reg_left->name, name);
		}
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		{
			const struct ao2_reg_partial_key *partial_key = obj_right;

			cmp = strncasecmp(reg_left->name, partial_key->name, partial_key->len);
		}
		break;
	default:
		/* Sort can only work on something with a full or partial key. */
		ast_assert(0);
		cmp = 0;
		break;
	}
	return cmp;
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
static void ao2_reg_destructor(void *v_doomed)
{
	struct ao2_reg_container *doomed = v_doomed;

	if (doomed->registered) {
		ao2_t_ref(doomed->registered, -1, "Releasing registered container.");
	}
}
#endif	/* defined(AO2_DEBUG) */

int ao2_container_register(const char *name, struct ao2_container *self, ao2_prnt_obj_fn *prnt_obj)
{
	int res = 0;
#if defined(AO2_DEBUG)
	struct ao2_reg_container *reg;

	reg = ao2_t_alloc_options(sizeof(*reg) + strlen(name), ao2_reg_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK, "Container registration object.");
	if (!reg) {
		return -1;
	}

	/* Fill in registered entry */
	ao2_t_ref(self, +1, "Registering container.");
	reg->registered = self;
	reg->prnt_obj = prnt_obj;
	strcpy(reg->name, name);/* safe */

	if (!ao2_t_link(reg_containers, reg, "Save registration object.")) {
		res = -1;
	}

	ao2_t_ref(reg, -1, "Done registering container.");
#endif	/* defined(AO2_DEBUG) */
	return res;
}

void ao2_container_unregister(const char *name)
{
#if defined(AO2_DEBUG)
	ao2_t_find(reg_containers, name, OBJ_UNLINK | OBJ_NODATA | OBJ_SEARCH_KEY,
		"Unregister container");
#endif	/* defined(AO2_DEBUG) */
}

#if defined(AO2_DEBUG)
static int ao2_complete_reg_cb(void *obj, void *arg, void *data, int flags)
{
	struct ao2_reg_match *which = data;

	/* ao2_reg_sort_cb() has already filtered the search to matching keys */
	return (which->find_nth < ++which->count) ? (CMP_MATCH | CMP_STOP) : 0;
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
static char *complete_container_names(struct ast_cli_args *a)
{
	struct ao2_reg_partial_key partial_key;
	struct ao2_reg_match which;
	struct ao2_reg_container *reg;
	char *name;

	if (a->pos != 3) {
		return NULL;
	}

	partial_key.len = strlen(a->word);
	partial_key.name = a->word;
	which.find_nth = a->n;
	which.count = 0;
	reg = ao2_t_callback_data(reg_containers, partial_key.len ? OBJ_SEARCH_PARTIAL_KEY : 0,
		ao2_complete_reg_cb, &partial_key, &which, "Find partial registered container");
	if (reg) {
		name = ast_strdup(reg->name);
		ao2_t_ref(reg, -1, "Done with registered container object.");
	} else {
		name = NULL;
	}
	return name;
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
AST_THREADSTORAGE(ao2_out_buf);

/*!
 * \brief Print CLI output.
 * \since 12.0.0
 *
 * \param where User data pointer needed to determine where to put output.
 * \param fmt printf type format string.
 *
 * \return Nothing
 */
static void cli_output(void *where, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void cli_output(void *where, const char *fmt, ...)
{
	int res;
	struct ast_str *buf;
	va_list ap;

	buf = ast_str_thread_get(&ao2_out_buf, 256);
	if (!buf) {
		return;
	}

	va_start(ap, fmt);
	res = ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);

	if (res != AST_DYNSTR_BUILD_FAILED) {
		ast_cli(*(int *) where, "%s", ast_str_buffer(buf));
	}
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
/*! \brief Show container contents - CLI command */
static char *handle_cli_astobj2_container_dump(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *name;
	struct ao2_reg_container *reg;

	switch (cmd) {
	case CLI_INIT:
		e->command = "astobj2 container dump";
		e->usage =
			"Usage: astobj2 container dump <name>\n"
			"	Show contents of the container <name>.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_container_names(a);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	name = a->argv[3];
	reg = ao2_t_find(reg_containers, name, OBJ_SEARCH_KEY, "Find registered container");
	if (reg) {
		ao2_container_dump(reg->registered, 0, name, (void *) &a->fd, cli_output,
			reg->prnt_obj);
		ao2_t_ref(reg, -1, "Done with registered container object.");
	} else {
		ast_cli(a->fd, "Container '%s' not found.\n", name);
	}

	return CLI_SUCCESS;
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
/*! \brief Show container statistics - CLI command */
static char *handle_cli_astobj2_container_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *name;
	struct ao2_reg_container *reg;

	switch (cmd) {
	case CLI_INIT:
		e->command = "astobj2 container stats";
		e->usage =
			"Usage: astobj2 container stats <name>\n"
			"	Show statistics about the specified container <name>.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_container_names(a);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	name = a->argv[3];
	reg = ao2_t_find(reg_containers, name, OBJ_SEARCH_KEY, "Find registered container");
	if (reg) {
		ao2_container_stats(reg->registered, 0, name, (void *) &a->fd, cli_output);
		ao2_t_ref(reg, -1, "Done with registered container object.");
	} else {
		ast_cli(a->fd, "Container '%s' not found.\n", name);
	}

	return CLI_SUCCESS;
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
/*! \brief Show container check results - CLI command */
static char *handle_cli_astobj2_container_check(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *name;
	struct ao2_reg_container *reg;

	switch (cmd) {
	case CLI_INIT:
		e->command = "astobj2 container check";
		e->usage =
			"Usage: astobj2 container check <name>\n"
			"	Perform a container integrity check on <name>.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_container_names(a);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	name = a->argv[3];
	reg = ao2_t_find(reg_containers, name, OBJ_SEARCH_KEY, "Find registered container");
	if (reg) {
		ast_cli(a->fd, "Container check of '%s': %s.\n", name,
			ao2_container_check(reg->registered, 0) ? "failed" : "OK");
		ao2_t_ref(reg, -1, "Done with registered container object.");
	} else {
		ast_cli(a->fd, "Container '%s' not found.\n", name);
	}

	return CLI_SUCCESS;
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
static struct ast_cli_entry cli_astobj2[] = {
	AST_CLI_DEFINE(handle_cli_astobj2_container_dump, "Show container contents"),
	AST_CLI_DEFINE(handle_cli_astobj2_container_stats, "Show container statistics"),
	AST_CLI_DEFINE(handle_cli_astobj2_container_check, "Perform a container integrity check"),
};
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
static void container_cleanup(void)
{
	ao2_t_ref(reg_containers, -1, "Releasing container registration container");
	reg_containers = NULL;

	ast_cli_unregister_multiple(cli_astobj2, ARRAY_LEN(cli_astobj2));
}
#endif	/* defined(AO2_DEBUG) */

int container_init(void)
{
#if defined(AO2_DEBUG)
	reg_containers = ao2_t_container_alloc_list(AO2_ALLOC_OPT_LOCK_RWLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, ao2_reg_sort_cb, NULL,
		"Container registration container.");
	if (!reg_containers) {
		return -1;
	}

	ast_cli_register_multiple(cli_astobj2, ARRAY_LEN(cli_astobj2));
	ast_register_cleanup(container_cleanup);
#endif	/* defined(AO2_DEBUG) */

	return 0;
}

