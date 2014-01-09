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
 * \brief Functions implementing astobj2 objects.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "asterisk/dlinkedlists.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#define REF_FILE "/tmp/refs"

#if defined(TEST_FRAMEWORK)
/* We are building with the test framework enabled so enable AO2 debug tests as well. */
#define AO2_DEBUG 1
#endif	/* defined(TEST_FRAMEWORK) */

/*!
 * astobj2 objects are always preceded by this data structure,
 * which contains a reference counter,
 * option flags and a pointer to a destructor.
 * The refcount is used to decide when it is time to
 * invoke the destructor.
 * The magic number is used for consistency check.
 */
struct __priv_data {
	int ref_counter;
	ao2_destructor_fn destructor_fn;
	/*! User data size for stats */
	size_t data_size;
	/*! The ao2 object option flags */
	uint32_t options;
	/*! magic number.  This is used to verify that a pointer passed in is a
	 *  valid astobj2 */
	uint32_t magic;
};

#define	AO2_MAGIC	0xa570b123

/*!
 * What an astobj2 object looks like: fixed-size private data
 * followed by variable-size user data.
 */
struct astobj2 {
	struct __priv_data priv_data;
	void *user_data[0];
};

struct ao2_lock_priv {
	ast_mutex_t lock;
};

/* AstObj2 with recursive lock. */
struct astobj2_lock {
	struct ao2_lock_priv mutex;
	struct __priv_data priv_data;
	void *user_data[0];
};

struct ao2_rwlock_priv {
	ast_rwlock_t lock;
	/*! Count of the number of threads holding a lock on this object. -1 if it is the write lock. */
	int num_lockers;
};

/* AstObj2 with RW lock. */
struct astobj2_rwlock {
	struct ao2_rwlock_priv rwlock;
	struct __priv_data priv_data;
	void *user_data[0];
};

#if defined(AST_DEVMODE)
#define AO2_DEVMODE_STAT(stat)	stat
#else
#define AO2_DEVMODE_STAT(stat)
#endif	/* defined(AST_DEVMODE) */

#ifdef AO2_DEBUG
struct ao2_stats {
	volatile int total_objects;
	volatile int total_mem;
	volatile int total_containers;
	volatile int total_refs;
	volatile int total_locked;
};

static struct ao2_stats ao2;
#endif

#ifdef HAVE_BKTR
#include <execinfo.h>    /* for backtrace */
#endif

void ao2_bt(void)
{
#ifdef HAVE_BKTR
	int depth;
	int idx;
#define N1	20
	void *addresses[N1];
	char **strings;

	depth = backtrace(addresses, N1);
	strings = ast_bt_get_symbols(addresses, depth);
	ast_verbose("backtrace returned: %d\n", depth);
	for (idx = 0; idx < depth; ++idx) {
		ast_verbose("%d: %p %s\n", idx, addresses[idx], strings[idx]);
	}
	ast_std_free(strings);
#endif
}

#define INTERNAL_OBJ_MUTEX(user_data) \
	((struct astobj2_lock *) (((char *) (user_data)) - sizeof(struct astobj2_lock)))

#define INTERNAL_OBJ_RWLOCK(user_data) \
	((struct astobj2_rwlock *) (((char *) (user_data)) - sizeof(struct astobj2_rwlock)))

/*!
 * \brief convert from a pointer _p to a user-defined object
 *
 * \return the pointer to the astobj2 structure
 */
static inline struct astobj2 *INTERNAL_OBJ(void *user_data)
{
	struct astobj2 *p;

	if (!user_data) {
		ast_log(LOG_ERROR, "user_data is NULL\n");
		return NULL;
	}

	p = (struct astobj2 *) ((char *) user_data - sizeof(*p));
	if (AO2_MAGIC != p->priv_data.magic) {
		if (p->priv_data.magic) {
			ast_log(LOG_ERROR, "bad magic number 0x%x for object %p\n",
				p->priv_data.magic, user_data);
		} else {
			ast_log(LOG_ERROR,
				"bad magic number for object %p. Object is likely destroyed.\n",
				user_data);
		}
		return NULL;
	}

	return p;
}

/*!
 * \brief convert from a pointer _p to an astobj2 object
 *
 * \return the pointer to the user-defined portion.
 */
#define EXTERNAL_OBJ(_p)	((_p) == NULL ? NULL : (_p)->user_data)

int __ao2_lock(void *user_data, enum ao2_lock_req lock_how, const char *file, const char *func, int line, const char *var)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);
	struct astobj2_lock *obj_mutex;
	struct astobj2_rwlock *obj_rwlock;
	int res = 0;

	if (obj == NULL) {
		ast_assert(0);
		return -1;
	}

	switch (obj->priv_data.options & AO2_ALLOC_OPT_LOCK_MASK) {
	case AO2_ALLOC_OPT_LOCK_MUTEX:
		obj_mutex = INTERNAL_OBJ_MUTEX(user_data);
		res = __ast_pthread_mutex_lock(file, line, func, var, &obj_mutex->mutex.lock);
#ifdef AO2_DEBUG
		if (!res) {
			ast_atomic_fetchadd_int(&ao2.total_locked, 1);
		}
#endif
		break;
	case AO2_ALLOC_OPT_LOCK_RWLOCK:
		obj_rwlock = INTERNAL_OBJ_RWLOCK(user_data);
		switch (lock_how) {
		case AO2_LOCK_REQ_MUTEX:
		case AO2_LOCK_REQ_WRLOCK:
			res = __ast_rwlock_wrlock(file, line, func, &obj_rwlock->rwlock.lock, var);
			if (!res) {
				ast_atomic_fetchadd_int(&obj_rwlock->rwlock.num_lockers, -1);
#ifdef AO2_DEBUG
				ast_atomic_fetchadd_int(&ao2.total_locked, 1);
#endif
			}
			break;
		case AO2_LOCK_REQ_RDLOCK:
			res = __ast_rwlock_rdlock(file, line, func, &obj_rwlock->rwlock.lock, var);
			if (!res) {
				ast_atomic_fetchadd_int(&obj_rwlock->rwlock.num_lockers, +1);
#ifdef AO2_DEBUG
				ast_atomic_fetchadd_int(&ao2.total_locked, 1);
#endif
			}
			break;
		}
		break;
	case AO2_ALLOC_OPT_LOCK_NOLOCK:
		/* The ao2 object has no lock. */
		break;
	default:
		ast_log(__LOG_ERROR, file, line, func, "Invalid lock option on ao2 object %p\n",
			user_data);
		return -1;
	}

	return res;
}

int __ao2_unlock(void *user_data, const char *file, const char *func, int line, const char *var)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);
	struct astobj2_lock *obj_mutex;
	struct astobj2_rwlock *obj_rwlock;
	int res = 0;
	int current_value;

	if (obj == NULL) {
		ast_assert(0);
		return -1;
	}

	switch (obj->priv_data.options & AO2_ALLOC_OPT_LOCK_MASK) {
	case AO2_ALLOC_OPT_LOCK_MUTEX:
		obj_mutex = INTERNAL_OBJ_MUTEX(user_data);
		res = __ast_pthread_mutex_unlock(file, line, func, var, &obj_mutex->mutex.lock);
#ifdef AO2_DEBUG
		if (!res) {
			ast_atomic_fetchadd_int(&ao2.total_locked, -1);
		}
#endif
		break;
	case AO2_ALLOC_OPT_LOCK_RWLOCK:
		obj_rwlock = INTERNAL_OBJ_RWLOCK(user_data);

		current_value = ast_atomic_fetchadd_int(&obj_rwlock->rwlock.num_lockers, -1) - 1;
		if (current_value < 0) {
			/* It was a WRLOCK that we are unlocking.  Fix the count. */
			ast_atomic_fetchadd_int(&obj_rwlock->rwlock.num_lockers, -current_value);
		}
		res = __ast_rwlock_unlock(file, line, func, &obj_rwlock->rwlock.lock, var);
#ifdef AO2_DEBUG
		if (!res) {
			ast_atomic_fetchadd_int(&ao2.total_locked, -1);
		}
#endif
		break;
	case AO2_ALLOC_OPT_LOCK_NOLOCK:
		/* The ao2 object has no lock. */
		break;
	default:
		ast_log(__LOG_ERROR, file, line, func, "Invalid lock option on ao2 object %p\n",
			user_data);
		res = -1;
		break;
	}
	return res;
}

int __ao2_trylock(void *user_data, enum ao2_lock_req lock_how, const char *file, const char *func, int line, const char *var)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);
	struct astobj2_lock *obj_mutex;
	struct astobj2_rwlock *obj_rwlock;
	int res = 0;

	if (obj == NULL) {
		ast_assert(0);
		return -1;
	}

	switch (obj->priv_data.options & AO2_ALLOC_OPT_LOCK_MASK) {
	case AO2_ALLOC_OPT_LOCK_MUTEX:
		obj_mutex = INTERNAL_OBJ_MUTEX(user_data);
		res = __ast_pthread_mutex_trylock(file, line, func, var, &obj_mutex->mutex.lock);
#ifdef AO2_DEBUG
		if (!res) {
			ast_atomic_fetchadd_int(&ao2.total_locked, 1);
		}
#endif
		break;
	case AO2_ALLOC_OPT_LOCK_RWLOCK:
		obj_rwlock = INTERNAL_OBJ_RWLOCK(user_data);
		switch (lock_how) {
		case AO2_LOCK_REQ_MUTEX:
		case AO2_LOCK_REQ_WRLOCK:
			res = __ast_rwlock_trywrlock(file, line, func, &obj_rwlock->rwlock.lock, var);
			if (!res) {
				ast_atomic_fetchadd_int(&obj_rwlock->rwlock.num_lockers, -1);
#ifdef AO2_DEBUG
				ast_atomic_fetchadd_int(&ao2.total_locked, 1);
#endif
			}
			break;
		case AO2_LOCK_REQ_RDLOCK:
			res = __ast_rwlock_tryrdlock(file, line, func, &obj_rwlock->rwlock.lock, var);
			if (!res) {
				ast_atomic_fetchadd_int(&obj_rwlock->rwlock.num_lockers, +1);
#ifdef AO2_DEBUG
				ast_atomic_fetchadd_int(&ao2.total_locked, 1);
#endif
			}
			break;
		}
		break;
	case AO2_ALLOC_OPT_LOCK_NOLOCK:
		/* The ao2 object has no lock. */
		return 0;
	default:
		ast_log(__LOG_ERROR, file, line, func, "Invalid lock option on ao2 object %p\n",
			user_data);
		return -1;
	}


	return res;
}

/*!
 * \internal
 * \brief Adjust an object's lock to the requested level.
 *
 * \param user_data An ao2 object to adjust lock level.
 * \param lock_how What level to adjust lock.
 * \param keep_stronger TRUE if keep original lock level if it is stronger.
 *
 * \pre The ao2 object is already locked.
 *
 * \details
 * An ao2 object with a RWLOCK will have its lock level adjusted
 * to the specified level if it is not already there.  An ao2
 * object with a different type of lock is not affected.
 *
 * \return Original lock level.
 */
static enum ao2_lock_req adjust_lock(void *user_data, enum ao2_lock_req lock_how, int keep_stronger)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);
	struct astobj2_rwlock *obj_rwlock;
	enum ao2_lock_req orig_lock;

	switch (obj->priv_data.options & AO2_ALLOC_OPT_LOCK_MASK) {
	case AO2_ALLOC_OPT_LOCK_RWLOCK:
		obj_rwlock = INTERNAL_OBJ_RWLOCK(user_data);
		if (obj_rwlock->rwlock.num_lockers < 0) {
			orig_lock = AO2_LOCK_REQ_WRLOCK;
		} else {
			orig_lock = AO2_LOCK_REQ_RDLOCK;
		}
		switch (lock_how) {
		case AO2_LOCK_REQ_MUTEX:
			lock_how = AO2_LOCK_REQ_WRLOCK;
			/* Fall through */
		case AO2_LOCK_REQ_WRLOCK:
			if (lock_how != orig_lock) {
				/* Switch from read lock to write lock. */
				ao2_unlock(user_data);
				ao2_wrlock(user_data);
			}
			break;
		case AO2_LOCK_REQ_RDLOCK:
			if (!keep_stronger && lock_how != orig_lock) {
				/* Switch from write lock to read lock. */
				ao2_unlock(user_data);
				ao2_rdlock(user_data);
			}
			break;
		}
		break;
	default:
		ast_log(LOG_ERROR, "Invalid lock option on ao2 object %p\n", user_data);
		/* Fall through */
	case AO2_ALLOC_OPT_LOCK_NOLOCK:
	case AO2_ALLOC_OPT_LOCK_MUTEX:
		orig_lock = AO2_LOCK_REQ_MUTEX;
		break;
	}

	return orig_lock;
}

void *ao2_object_get_lockaddr(void *user_data)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);
	struct astobj2_lock *obj_mutex;

	if (obj == NULL) {
		ast_assert(0);
		return NULL;
	}

	switch (obj->priv_data.options & AO2_ALLOC_OPT_LOCK_MASK) {
	case AO2_ALLOC_OPT_LOCK_MUTEX:
		obj_mutex = INTERNAL_OBJ_MUTEX(user_data);
		return &obj_mutex->mutex.lock;
	default:
		break;
	}

	return NULL;
}

static int internal_ao2_ref(void *user_data, int delta, const char *file, int line, const char *func)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);
	struct astobj2_lock *obj_mutex;
	struct astobj2_rwlock *obj_rwlock;
	int current_value;
	int ret;

	if (obj == NULL) {
		ast_assert(0);
		return -1;
	}

	/* if delta is 0, just return the refcount */
	if (delta == 0) {
		return obj->priv_data.ref_counter;
	}

	/* we modify with an atomic operation the reference counter */
	ret = ast_atomic_fetchadd_int(&obj->priv_data.ref_counter, delta);
	current_value = ret + delta;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_refs, delta);
#endif

	if (0 < current_value) {
		/* The object still lives. */
		return ret;
	}

	/* this case must never happen */
	if (current_value < 0) {
		ast_log(__LOG_ERROR, file, line, func,
			"Invalid refcount %d on ao2 object %p\n", current_value, user_data);
	}

	/* last reference, destroy the object */
	if (obj->priv_data.destructor_fn != NULL) {
		obj->priv_data.destructor_fn(user_data);
	}

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_mem, - obj->priv_data.data_size);
	ast_atomic_fetchadd_int(&ao2.total_objects, -1);
#endif

	/* In case someone uses an object after it's been freed */
	obj->priv_data.magic = 0;

	switch (obj->priv_data.options & AO2_ALLOC_OPT_LOCK_MASK) {
	case AO2_ALLOC_OPT_LOCK_MUTEX:
		obj_mutex = INTERNAL_OBJ_MUTEX(user_data);
		ast_mutex_destroy(&obj_mutex->mutex.lock);

		ast_free(obj_mutex);
		break;
	case AO2_ALLOC_OPT_LOCK_RWLOCK:
		obj_rwlock = INTERNAL_OBJ_RWLOCK(user_data);
		ast_rwlock_destroy(&obj_rwlock->rwlock.lock);

		ast_free(obj_rwlock);
		break;
	case AO2_ALLOC_OPT_LOCK_NOLOCK:
		ast_free(obj);
		break;
	default:
		ast_log(__LOG_ERROR, file, line, func,
			"Invalid lock option on ao2 object %p\n", user_data);
		break;
	}

	return ret;
}

int __ao2_ref_debug(void *user_data, int delta, const char *tag, const char *file, int line, const char *func)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);

	if (obj == NULL) {
		ast_log_backtrace();
		ast_assert(0);
		return -1;
	}

	if (delta != 0) {
		FILE *refo = fopen(REF_FILE, "a");
		if (refo) {
			fprintf(refo, "%p %s%d   %s:%d:%s (%s) [@%d]\n", user_data, (delta < 0 ? "" : "+"),
				delta, file, line, func, tag, obj->priv_data.ref_counter);
			fclose(refo);
		}
	}
	if (obj->priv_data.ref_counter + delta == 0 && obj->priv_data.destructor_fn != NULL) { /* this isn't protected with lock; just for o/p */
		FILE *refo = fopen(REF_FILE, "a");
		if (refo) {
			fprintf(refo, "%p **call destructor** %s:%d:%s (%s)\n", user_data, file, line, func, tag);
			fclose(refo);
		}
	}
	return internal_ao2_ref(user_data, delta, file, line, func);
}

int __ao2_ref(void *user_data, int delta)
{
	return internal_ao2_ref(user_data, delta, __FILE__, __LINE__, __FUNCTION__);
}

void __ao2_cleanup_debug(void *obj, const char *file, int line, const char *function)
{
	if (obj) {
		__ao2_ref_debug(obj, -1, "ao2_cleanup", file, line, function);
	}
}

void __ao2_cleanup(void *obj)
{
	if (obj) {
		ao2_ref(obj, -1);
	}
}

static void *internal_ao2_alloc(size_t data_size, ao2_destructor_fn destructor_fn, unsigned int options, const char *file, int line, const char *func)
{
	/* allocation */
	struct astobj2 *obj;
	struct astobj2_lock *obj_mutex;
	struct astobj2_rwlock *obj_rwlock;

	switch (options & AO2_ALLOC_OPT_LOCK_MASK) {
	case AO2_ALLOC_OPT_LOCK_MUTEX:
#if defined(__AST_DEBUG_MALLOC)
		obj_mutex = __ast_calloc(1, sizeof(*obj_mutex) + data_size, file, line, func);
#else
		obj_mutex = ast_calloc(1, sizeof(*obj_mutex) + data_size);
#endif
		if (obj_mutex == NULL) {
			return NULL;
		}

		ast_mutex_init(&obj_mutex->mutex.lock);
		obj = (struct astobj2 *) &obj_mutex->priv_data;
		break;
	case AO2_ALLOC_OPT_LOCK_RWLOCK:
#if defined(__AST_DEBUG_MALLOC)
		obj_rwlock = __ast_calloc(1, sizeof(*obj_rwlock) + data_size, file, line, func);
#else
		obj_rwlock = ast_calloc(1, sizeof(*obj_rwlock) + data_size);
#endif
		if (obj_rwlock == NULL) {
			return NULL;
		}

		ast_rwlock_init(&obj_rwlock->rwlock.lock);
		obj = (struct astobj2 *) &obj_rwlock->priv_data;
		break;
	case AO2_ALLOC_OPT_LOCK_NOLOCK:
#if defined(__AST_DEBUG_MALLOC)
		obj = __ast_calloc(1, sizeof(*obj) + data_size, file, line, func);
#else
		obj = ast_calloc(1, sizeof(*obj) + data_size);
#endif
		if (obj == NULL) {
			return NULL;
		}
		break;
	default:
		/* Invalid option value. */
		ast_log(__LOG_DEBUG, file, line, func, "Invalid lock option requested\n");
		return NULL;
	}

	/* Initialize common ao2 values. */
	obj->priv_data.ref_counter = 1;
	obj->priv_data.destructor_fn = destructor_fn;	/* can be NULL */
	obj->priv_data.data_size = data_size;
	obj->priv_data.options = options;
	obj->priv_data.magic = AO2_MAGIC;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_objects, 1);
	ast_atomic_fetchadd_int(&ao2.total_mem, data_size);
	ast_atomic_fetchadd_int(&ao2.total_refs, 1);
#endif

	/* return a pointer to the user data */
	return EXTERNAL_OBJ(obj);
}

void *__ao2_alloc_debug(size_t data_size, ao2_destructor_fn destructor_fn, unsigned int options, const char *tag,
	const char *file, int line, const char *func, int ref_debug)
{
	/* allocation */
	void *obj;
	FILE *refo;

	if ((obj = internal_ao2_alloc(data_size, destructor_fn, options, file, line, func)) == NULL) {
		return NULL;
	}

	if (ref_debug && (refo = fopen(REF_FILE, "a"))) {
		fprintf(refo, "%p =1   %s:%d:%s (%s)\n", obj, file, line, func, tag);
		fclose(refo);
	}

	/* return a pointer to the user data */
	return obj;
}

void *__ao2_alloc(size_t data_size, ao2_destructor_fn destructor_fn, unsigned int options)
{
	return internal_ao2_alloc(data_size, destructor_fn, options, __FILE__, __LINE__, __FUNCTION__);
}


void __ao2_global_obj_release(struct ao2_global_obj *holder, const char *tag, const char *file, int line, const char *func, const char *name)
{
	if (!holder) {
		/* For sanity */
		ast_log(LOG_ERROR, "Must be called with a global object!\n");
		ast_assert(0);
		return;
	}
	if (__ast_rwlock_wrlock(file, line, func, &holder->lock, name)) {
		/* Could not get the write lock. */
		ast_assert(0);
		return;
	}

	/* Release the held ao2 object. */
	if (holder->obj) {
		if (tag) {
			__ao2_ref_debug(holder->obj, -1, tag, file, line, func);
		} else {
			__ao2_ref(holder->obj, -1);
		}
		holder->obj = NULL;
	}

	__ast_rwlock_unlock(file, line, func, &holder->lock, name);
}

void *__ao2_global_obj_replace(struct ao2_global_obj *holder, void *obj, const char *tag, const char *file, int line, const char *func, const char *name)
{
	void *obj_old;

	if (!holder) {
		/* For sanity */
		ast_log(LOG_ERROR, "Must be called with a global object!\n");
		ast_assert(0);
		return NULL;
	}
	if (__ast_rwlock_wrlock(file, line, func, &holder->lock, name)) {
		/* Could not get the write lock. */
		ast_assert(0);
		return NULL;
	}

	if (obj) {
		if (tag) {
			__ao2_ref_debug(obj, +1, tag, file, line, func);
		} else {
			__ao2_ref(obj, +1);
		}
	}
	obj_old = holder->obj;
	holder->obj = obj;

	__ast_rwlock_unlock(file, line, func, &holder->lock, name);

	return obj_old;
}

int __ao2_global_obj_replace_unref(struct ao2_global_obj *holder, void *obj, const char *tag, const char *file, int line, const char *func, const char *name)
{
	void *obj_old;

	obj_old = __ao2_global_obj_replace(holder, obj, tag, file, line, func, name);
	if (obj_old) {
		if (tag) {
			__ao2_ref_debug(obj_old, -1, tag, file, line, func);
		} else {
			__ao2_ref(obj_old, -1);
		}
		return 1;
	}
	return 0;
}

void *__ao2_global_obj_ref(struct ao2_global_obj *holder, const char *tag, const char *file, int line, const char *func, const char *name)
{
	void *obj;

	if (!holder) {
		/* For sanity */
		ast_log(LOG_ERROR, "Must be called with a global object!\n");
		ast_assert(0);
		return NULL;
	}

	if (__ast_rwlock_rdlock(file, line, func, &holder->lock, name)) {
		/* Could not get the read lock. */
		ast_assert(0);
		return NULL;
	}

	obj = holder->obj;
	if (obj) {
		if (tag) {
			__ao2_ref_debug(obj, +1, tag, file, line, func);
		} else {
			__ao2_ref(obj, +1);
		}
	}

	__ast_rwlock_unlock(file, line, func, &holder->lock, name);

	return obj;
}

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

enum ao2_container_rtti {
	/*! This is a hash container */
	AO2_CONTAINER_RTTI_HASH,
	/*! This is a red-black tree container */
	AO2_CONTAINER_RTTI_RBTREE,
};

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
 *
 * \return Nothing
 */
typedef void (*ao2_container_destroy_fn)(struct ao2_container *self);

/*!
 * \brief Create an empty copy of this container.
 *
 * \param self Container to operate upon.
 *
 * \retval empty-container on success.
 * \retval NULL on error.
 */
typedef struct ao2_container *(*ao2_container_alloc_empty_clone_fn)(struct ao2_container *self);

/*!
 * \brief Create an empty copy of this container. (Debug version)
 *
 * \param self Container to operate upon.
 * \param tag used for debugging.
 * \param file Debug file name invoked from
 * \param line Debug line invoked from
 * \param func Debug function name invoked from
 * \param ref_debug TRUE if to output a debug reference message.
 *
 * \retval empty-container on success.
 * \retval NULL on error.
 */
typedef struct ao2_container *(*ao2_container_alloc_empty_clone_debug_fn)(struct ao2_container *self, const char *tag, const char *file, int line, const char *func, int ref_debug);

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
 * \retval initialized-node on success.
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
 * \retval node-ptr of found node (Reffed).
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
 * \retval node-ptr of found node (Reffed).
 * \retval NULL when no node found.
 */
typedef struct ao2_container_node *(*ao2_container_find_next_fn)(struct ao2_container *self, void *v_state, struct ao2_container_node *prev);

/*!
 * \brief Cleanup the container traversal state.
 *
 * \param v_state Traversal state to cleanup.
 *
 * \return Nothing
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
 * \retval node on success.
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
 *
 * \return Nothing
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
 *
 * \return Nothing
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

/*! Container virtual methods template. */
struct ao2_container_methods {
	/*! Run Time Type Identification */
	enum ao2_container_rtti type;
	/*! Destroy this container. */
	ao2_container_destroy_fn destroy;
	/*! \brief Create an empty copy of this container. */
	ao2_container_alloc_empty_clone_fn alloc_empty_clone;
	/*! \brief Create an empty copy of this container. (Debug version) */
	ao2_container_alloc_empty_clone_debug_fn alloc_empty_clone_debug;
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
#if defined(AST_DEVMODE)
	/*! Display container contents. (Method for debug purposes) */
	ao2_container_display dump;
	/*! Display container debug statistics. (Method for debug purposes) */
	ao2_container_statistics stats;
	/*! Perform an integrity check on the container. (Method for debug purposes) */
	ao2_container_integrity integrity;
#endif	/* defined(AST_DEVMODE) */
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
#if defined(AST_DEVMODE)
	/*! Number of nodes in the container. */
	int nodes;
	/*! Maximum number of empty nodes in the container. (nodes - elements) */
	int max_empty_nodes;
#endif	/* defined(AST_DEVMODE) */
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
 * return the number of elements in the container
 */
int ao2_container_count(struct ao2_container *c)
{
	return ast_atomic_fetchadd_int(&c->elements, 0);
}

#if defined(AST_DEVMODE)
static void hash_ao2_link_node_stat(struct ao2_container *hash, struct ao2_container_node *hash_node);
static void hash_ao2_unlink_node_stat(struct ao2_container *hash, struct ao2_container_node *hash_node);
#endif	/* defined(AST_DEVMODE) */

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

	if (!INTERNAL_OBJ(obj_new) || !INTERNAL_OBJ(self)
		|| !self->v_table || !self->v_table->new_node || !self->v_table->insert) {
		/* Sanity checks. */
		ast_assert(0);
		return 0;
	}

	if (flags & OBJ_NOLOCK) {
		orig_lock = adjust_lock(self, AO2_LOCK_REQ_WRLOCK, 1);
	} else {
		ao2_wrlock(self);
		orig_lock = AO2_LOCK_REQ_MUTEX;
	}

	res = 0;
	node = self->v_table->new_node(self, obj_new, tag, file, line, func);
	if (node) {
#if defined(AO2_DEBUG) && defined(AST_DEVMODE)
		switch (self->v_table->type) {
		case AO2_CONTAINER_RTTI_HASH:
			if (!self->sort_fn) {
				/*
				 * XXX chan_iax2 plays games with the hash function so we cannot
				 * routinely do an integrity check on this type of container.
				 * chan_iax2 should be changed to not abuse the hash function.
				 */
				break;
			}
			/* Fall through. */
		case AO2_CONTAINER_RTTI_RBTREE:
			if (ao2_container_check(self, OBJ_NOLOCK)) {
				ast_log(LOG_ERROR, "Container integrity failed before insert.\n");
			}
			break;
		}
#endif	/* defined(AO2_DEBUG) && defined(AST_DEVMODE) */
		/* Insert the new node. */
		switch (self->v_table->insert(self, node)) {
		case AO2_CONTAINER_INSERT_NODE_INSERTED:
			node->is_linked = 1;
			ast_atomic_fetchadd_int(&self->elements, 1);
#if defined(AST_DEVMODE)
			AO2_DEVMODE_STAT(++self->nodes);
			switch (self->v_table->type) {
			case AO2_CONTAINER_RTTI_HASH:
				hash_ao2_link_node_stat(self, node);
				break;
			case AO2_CONTAINER_RTTI_RBTREE:
				break;
			}
#endif	/* defined(AST_DEVMODE) */

			res = 1;
			break;
		case AO2_CONTAINER_INSERT_NODE_OBJ_REPLACED:
			res = 1;
			/* Fall through */
		case AO2_CONTAINER_INSERT_NODE_REJECTED:
			__ao2_ref(node, -1);
			break;
		}
#if defined(AO2_DEBUG) && defined(AST_DEVMODE)
		if (res) {
			switch (self->v_table->type) {
			case AO2_CONTAINER_RTTI_HASH:
				if (!self->sort_fn) {
					/*
					 * XXX chan_iax2 plays games with the hash function so we cannot
					 * routinely do an integrity check on this type of container.
					 * chan_iax2 should be changed to not abuse the hash function.
					 */
					break;
				}
				/* Fall through. */
			case AO2_CONTAINER_RTTI_RBTREE:
				if (ao2_container_check(self, OBJ_NOLOCK)) {
					ast_log(LOG_ERROR, "Container integrity failed after insert.\n");
				}
				break;
			}
		}
#endif	/* defined(AO2_DEBUG) && defined(AST_DEVMODE) */
	}

	if (flags & OBJ_NOLOCK) {
		adjust_lock(self, orig_lock, 0);
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
	if (!INTERNAL_OBJ(user_data)) {
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
	if (!INTERNAL_OBJ(user_data)) {
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

/*! Allow enough room for container specific traversal state structs */
#define AO2_TRAVERSAL_STATE_SIZE	100

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

	if (!INTERNAL_OBJ(self) || !self->v_table || !self->v_table->traverse_first
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
			orig_lock = adjust_lock(self, AO2_LOCK_REQ_WRLOCK, 1);
		} else {
			orig_lock = adjust_lock(self, AO2_LOCK_REQ_RDLOCK, 1);
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
				/* update number of elements */
				ast_atomic_fetchadd_int(&self->elements, -1);
#if defined(AST_DEVMODE)
				{
					int empty = self->nodes - self->elements;

					if (self->max_empty_nodes < empty) {
						self->max_empty_nodes = empty;
					}
				}
				switch (self->v_table->type) {
				case AO2_CONTAINER_RTTI_HASH:
					hash_ao2_unlink_node_stat(self, node);
					break;
				case AO2_CONTAINER_RTTI_RBTREE:
					break;
				}
#endif	/* defined(AST_DEVMODE) */

				/*
				 * - When unlinking and not returning the result, OBJ_NODATA is
				 * set, the ref from the container must be decremented.
				 *
				 * - When unlinking with a multi_container the ref from the
				 * original container must be decremented.  This is because the
				 * result is returned in a new container that already holds its
				 * own ref for the object.
				 *
				 * If the ref from the original container is not accounted for
				 * here a memory leak occurs.
				 */
				if (multi_container || (flags & OBJ_NODATA)) {
					if (tag) {
						__ao2_ref_debug(node->obj, -1, tag, file, line, func);
					} else {
						ao2_t_ref(node->obj, -1, "Unlink container obj reference.");
					}
				}
				node->obj = NULL;

				/* Unref the node from the container. */
				__ao2_ref(node, -1);
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
		adjust_lock(self, orig_lock, 0);
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
			orig_lock = adjust_lock(iter->c, AO2_LOCK_REQ_RDLOCK, 1);
		} else {
			orig_lock = AO2_LOCK_REQ_MUTEX;
			ao2_rdlock(iter->c);
		}

		__ao2_ref(iter->last_node, -1);
		iter->last_node = NULL;

		if (iter->flags & AO2_ITERATOR_DONTLOCK) {
			adjust_lock(iter->c, orig_lock, 0);
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

	if (!INTERNAL_OBJ(iter->c) || !iter->c->v_table || !iter->c->v_table->iterator_next) {
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
			orig_lock = adjust_lock(iter->c, AO2_LOCK_REQ_WRLOCK, 1);
		} else {
			orig_lock = adjust_lock(iter->c, AO2_LOCK_REQ_RDLOCK, 1);
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
			/* update number of elements */
			ast_atomic_fetchadd_int(&iter->c->elements, -1);
#if defined(AST_DEVMODE)
			{
				int empty = iter->c->nodes - iter->c->elements;

				if (iter->c->max_empty_nodes < empty) {
					iter->c->max_empty_nodes = empty;
				}
			}
			switch (iter->c->v_table->type) {
			case AO2_CONTAINER_RTTI_HASH:
				hash_ao2_unlink_node_stat(iter->c, node);
				break;
			case AO2_CONTAINER_RTTI_RBTREE:
				break;
			}
#endif	/* defined(AST_DEVMODE) */

			/* Transfer the object ref from the container to the returned object. */
			node->obj = NULL;

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
		adjust_lock(iter->c, orig_lock, 0);
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

static void container_destruct(void *_c)
{
	struct ao2_container *c = _c;

	/* Unlink any stored objects in the container. */
	c->destroying = 1;
	__ao2_callback(c, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);

	/* Perform any extra container cleanup. */
	if (c->v_table && c->v_table->destroy) {
		c->v_table->destroy(c);
	}

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_containers, -1);
#endif
}

static void container_destruct_debug(void *_c)
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

#ifdef AO2_DEBUG
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
	if (!INTERNAL_OBJ(orig) || !orig->v_table || !orig->v_table->alloc_empty_clone) {
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
	if (!INTERNAL_OBJ(orig) || !orig->v_table || !orig->v_table->alloc_empty_clone_debug) {
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
	if (!INTERNAL_OBJ(self) || !self->v_table) {
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
#if defined(AST_DEVMODE)
	if (self->v_table->dump) {
		self->v_table->dump(self, where, prnt, prnt_obj);
	} else
#endif	/* defined(AST_DEVMODE) */
	{
		prnt(where, "Container dump not available.\n");
	}
	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(self);
	}
}

void ao2_container_stats(struct ao2_container *self, enum search_flags flags, const char *name, void *where, ao2_prnt_fn *prnt)
{
	if (!INTERNAL_OBJ(self) || !self->v_table) {
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
#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */
	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(self);
	}
}

int ao2_container_check(struct ao2_container *self, enum search_flags flags)
{
	int res = 0;

	if (!INTERNAL_OBJ(self) || !self->v_table) {
		/* Sanity checks. */
		ast_assert(0);
		return -1;
	}
#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */
	return res;
}

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
#if defined(AST_DEVMODE)
	/*! Number of elements currently in the bucket. */
	int elements;
	/*! Maximum number of elements in the bucket. */
	int max_elements;
#endif	/* defined(AST_DEVMODE) */
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
	struct astobj2 *orig_obj;
	unsigned int ao2_options;

	/* Get container ao2 options. */
	orig_obj = INTERNAL_OBJ(self);
	if (!orig_obj) {
		return NULL;
	}
	ao2_options = orig_obj->priv_data.options;

	return ao2_t_container_alloc_hash(ao2_options, self->common.options, self->n_buckets,
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
	struct astobj2 *orig_obj;
	unsigned int ao2_options;

	/* Get container ao2 options. */
	orig_obj = INTERNAL_OBJ(self);
	if (!orig_obj) {
		return NULL;
	}
	ao2_options = orig_obj->priv_data.options;

	return __ao2_container_alloc_hash_debug(ao2_options, self->common.options,
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
		adjust_lock(my_container, AO2_LOCK_REQ_WRLOCK, 1);

#if defined(AO2_DEBUG) && defined(AST_DEVMODE)
		/*
		 * XXX chan_iax2 plays games with the hash function so we cannot
		 * routinely do an integrity check on this type of container.
		 * chan_iax2 should be changed to not abuse the hash function.
		 */
		if (!my_container->common.destroying
			&& my_container->common.sort_fn
			&& ao2_container_check(doomed->common.my_container, OBJ_NOLOCK)) {
			ast_log(LOG_ERROR, "Container integrity failed before node deletion.\n");
		}
#endif	/* defined(AO2_DEBUG) && defined(AST_DEVMODE) */
		bucket = &my_container->buckets[doomed->my_bucket];
		AST_DLLIST_REMOVE(&bucket->list, doomed, links);
		AO2_DEVMODE_STAT(--my_container->common.nodes);
	}

	/*
	 * We could have an object in the node if the container is being
	 * destroyed or the node had not been linked in yet.
	 */
	if (doomed->common.obj) {
		ao2_t_ref(doomed->common.obj, -1, "Container node destruction");
		doomed->common.obj = NULL;
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
static enum ao2_container_insert hash_ao2_insert_node(struct ao2_container_hash *self, struct hash_bucket_node *node)
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
					return AO2_CONTAINER_INSERT_NODE_OBJ_REPLACED;
				}
			}
			AST_DLLIST_TRAVERSE_SAFE_END;
		}
		AST_DLLIST_INSERT_TAIL(&bucket->list, node, links);
	}
	return AO2_CONTAINER_INSERT_NODE_INSERTED;
}

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

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

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

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

/*! Hash container virtual method table. */
static const struct ao2_container_methods v_table_hash = {
	.type = AO2_CONTAINER_RTTI_HASH,
	.alloc_empty_clone = (ao2_container_alloc_empty_clone_fn) hash_ao2_alloc_empty_clone,
	.alloc_empty_clone_debug =
		(ao2_container_alloc_empty_clone_debug_fn) hash_ao2_alloc_empty_clone_debug,
	.new_node = (ao2_container_new_node_fn) hash_ao2_new_node,
	.insert = (ao2_container_insert_fn) hash_ao2_insert_node,
	.traverse_first = (ao2_container_find_first_fn) hash_ao2_find_first,
	.traverse_next = (ao2_container_find_next_fn) hash_ao2_find_next,
	.iterator_next = (ao2_iterator_next_fn) hash_ao2_iterator_next,
	.destroy = (ao2_container_destroy_fn) hash_ao2_destroy,
#if defined(AST_DEVMODE)
	.dump = (ao2_container_display) hash_ao2_dump,
	.stats = (ao2_container_statistics) hash_ao2_stats,
	.integrity = (ao2_container_integrity) hash_ao2_integrity,
#endif	/* defined(AST_DEVMODE) */
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
#endif

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

/*!
 * A structure to hold the object held by the container and
 * where it is located in it.
 *
 * A red-black tree has the following properties:
 *
 * 1) Every node is either black or red.
 *
 * 2) The root is black.
 *
 * 3) If a node has a NULL child, that "child" is considered
 * black.
 *
 * 4) If a node is red, then both of its children are black.
 *
 * 5) Every path from a node to a descendant NULL child has the
 * same number of black nodes.  (Including the black NULL
 * child.)
 */
struct rbtree_node {
	/*!
	 * \brief Items common to all container nodes.
	 * \note Must be first in the specific node struct.
	 */
	struct ao2_container_node common;
	/*! Parent node of this node. NULL if this is the root node. */
	struct rbtree_node *parent;
	/*! Left child node of this node.  NULL if does not have this child. */
	struct rbtree_node *left;
	/*! Right child node of this node.  NULL if does not have this child. */
	struct rbtree_node *right;
	/*! TRUE if the node is red. */
	unsigned int is_red:1;
};

/*!
 * A rbtree container in addition to values common to all
 * container types, stores the pointer to the root node of the
 * tree.
 */
struct ao2_container_rbtree {
	/*!
	 * \brief Items common to all containers.
	 * \note Must be first in the specific container struct.
	 */
	struct ao2_container common;
	/*! Root node of the tree.  NULL if the tree is empty. */
	struct rbtree_node *root;
#if defined(AST_DEVMODE)
	struct {
		/*! Fixup insert left cases 1-3 */
		int fixup_insert_left[3];
		/*! Fixup insert right cases 1-3 */
		int fixup_insert_right[3];
		/*! Fixup delete left cases 1-4 */
		int fixup_delete_left[4];
		/*! Fixup delete right cases 1-4 */
		int fixup_delete_right[4];
		/*! Deletion of node with number of children (0-2). */
		int delete_children[3];
	} stats;
#endif	/* defined(AST_DEVMODE) */
};

/*!
 * \internal
 * \brief Get the most left node in the tree.
 * \since 12.0.0
 *
 * \param node Starting node to find the most left node.
 *
 * \return Left most node.  Never NULL.
 */
static struct rbtree_node *rb_node_most_left(struct rbtree_node *node)
{
	while (node->left) {
		node = node->left;
	}

	return node;
}

/*!
 * \internal
 * \brief Get the most right node in the tree.
 * \since 12.0.0
 *
 * \param node Starting node to find the most right node.
 *
 * \return Right most node.  Never NULL.
 */
static struct rbtree_node *rb_node_most_right(struct rbtree_node *node)
{
	while (node->right) {
		node = node->right;
	}

	return node;
}

/*!
 * \internal
 * \brief Get the next node in ascending sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the next node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_next(struct rbtree_node *node)
{
	if (node->right) {
		return rb_node_most_left(node->right);
	}

	/* Find the parent that the node is a left child of. */
	while (node->parent) {
		if (node->parent->left == node) {
			/* We are the left child.  The parent is the next node. */
			return node->parent;
		}
		node = node->parent;
	}
	return NULL;
}

/*!
 * \internal
 * \brief Get the next node in descending sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the previous node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_prev(struct rbtree_node *node)
{
	if (node->left) {
		return rb_node_most_right(node->left);
	}

	/* Find the parent that the node is a right child of. */
	while (node->parent) {
		if (node->parent->right == node) {
			/* We are the right child.  The parent is the previous node. */
			return node->parent;
		}
		node = node->parent;
	}
	return NULL;
}

/*!
 * \internal
 * \brief Get the next node in pre-order sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the next node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_pre(struct rbtree_node *node)
{
	/* Visit the children if the node has any. */
	if (node->left) {
		return node->left;
	}
	if (node->right) {
		return node->right;
	}

	/* Time to go back up. */
	for (;;) {
		if (!node->parent) {
			return NULL;
		}
		if (node->parent->left == node && node->parent->right) {
			/*
			 * We came up the left child and there's a right child.  Visit
			 * it.
			 */
			return node->parent->right;
		}
		node = node->parent;
	}
}

/*!
 * \internal
 * \brief Get the next node in post-order sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the next node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_post(struct rbtree_node *node)
{
	/* This node's children have already been visited. */
	for (;;) {
		if (!node->parent) {
			return NULL;
		}
		if (node->parent->left == node) {
			/* We came up the left child. */
			node = node->parent;

			/*
			 * Find the right child's left most childless node.
			 */
			while (node->right) {
				node = rb_node_most_left(node->right);
			}

			/*
			 * This node's left child has already been visited or it doesn't
			 * have any children.
			 */
			return node;
		}

		/*
		 * We came up the right child.
		 *
		 * This node's children have already been visited.  Time to
		 * visit the parent.
		 */
		return node->parent;
	}
}

/*!
 * \internal
 * \brief Get the next non-empty node in ascending sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the next node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_next_full(struct rbtree_node *node)
{
	for (;;) {
		node = rb_node_next(node);
		if (!node || node->common.obj) {
			return node;
		}
	}
}

/*!
 * \internal
 * \brief Get the next non-empty node in descending sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the previous node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_prev_full(struct rbtree_node *node)
{
	for (;;) {
		node = rb_node_prev(node);
		if (!node || node->common.obj) {
			return node;
		}
	}
}

enum equal_node_bias {
	/*! Bias search toward first matching node in the container. */
	BIAS_FIRST,
	/*! Bias search toward any matching node. */
	BIAS_EQUAL,
	/*! Bias search toward last matching node in the container. */
	BIAS_LAST,
};

enum empty_node_direction {
	GO_LEFT,
	GO_RIGHT,
};

/*!
 * \internal
 * \brief Determine which way to go from an empty node.
 * \since 12.0.0
 *
 * \param empty Empty node to determine which side obj_right goes on.
 * \param sort_fn Sort comparison function for non-empty nodes.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_SEARCH_OBJECT - if set, 'obj_right', is an object.
 *   OBJ_SEARCH_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_SEARCH_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 * \param bias How to bias search direction for duplicates
 *
 * \return enum empty_node_direction to proceed.
 */
static enum empty_node_direction rb_find_empty_direction(struct rbtree_node *empty, ao2_sort_fn *sort_fn, void *obj_right, enum search_flags flags, enum equal_node_bias bias)
{
	int cmp;
	struct rbtree_node *cur;
	struct rbtree_node *right_most;

	/* Try for a quick definite go left. */
	if (!empty->left) {
		/* The empty node has no left child. */
		return GO_RIGHT;
	}
	right_most = rb_node_most_right(empty->left);
	if (right_most->common.obj) {
		cmp = sort_fn(right_most->common.obj, obj_right, flags);
		if (cmp < 0) {
			return GO_RIGHT;
		}
		if (cmp == 0 && bias == BIAS_LAST) {
			return GO_RIGHT;
		}
		return GO_LEFT;
	}

	/* Try for a quick definite go right. */
	if (!empty->right) {
		/* The empty node has no right child. */
		return GO_LEFT;
	}
	cur = rb_node_most_left(empty->right);
	if (cur->common.obj) {
		cmp = sort_fn(cur->common.obj, obj_right, flags);
		if (cmp > 0) {
			return GO_LEFT;
		}
		if (cmp == 0 && bias == BIAS_FIRST) {
			return GO_LEFT;
		}
		return GO_RIGHT;
	}

	/*
	 * Have to scan the previous nodes from the right_most node of
	 * the left subtree for the first non-empty node to determine
	 * direction.
	 */
	cur = right_most;
	for (;;) {
		/* Find previous node. */
		if (cur->left) {
			cur = rb_node_most_right(cur->left);
		} else {
			/* Find the parent that the node is a right child of. */
			for (;;) {
				if (cur->parent == empty) {
					/* The left side of the empty node is all empty nodes. */
					return GO_RIGHT;
				}
				if (cur->parent->right == cur) {
					/* We are the right child.  The parent is the previous node. */
					cur = cur->parent;
					break;
				}
				cur = cur->parent;
			}
		}

		if (cur->common.obj) {
			cmp = sort_fn(cur->common.obj, obj_right, flags);
			if (cmp < 0) {
				return GO_RIGHT;
			}
			if (cmp == 0 && bias == BIAS_LAST) {
				return GO_RIGHT;
			}
			return GO_LEFT;
		}
	}
}

/*!
 * \internal
 * \brief Tree node rotation left.
 * \since 12.0.0
 *
 * \param self Container holding node.
 * \param node Node to perform a left rotation with.
 *
 *        p                         p
 *        |     Left rotation       |
 *        N        --->             Ch
 *       / \                       / \
 *      a  Ch                     N   c
 *        / \                    / \
 *       b   c                  a   b
 *
 * N = node
 * Ch = child
 * p = parent
 * a,b,c = other nodes that are unaffected by the rotation.
 *
 * \note It is assumed that the node's right child exists.
 *
 * \return Nothing
 */
static void rb_rotate_left(struct ao2_container_rbtree *self, struct rbtree_node *node)
{
	struct rbtree_node *child;	/*!< Node's right child. */

	child = node->right;

	/* Link the node's parent to the child. */
	if (!node->parent) {
		/* Node is the root so we get a new root node. */
		self->root = child;
	} else if (node->parent->left == node) {
		/* Node is a left child. */
		node->parent->left = child;
	} else {
		/* Node is a right child. */
		node->parent->right = child;
	}
	child->parent = node->parent;

	/* Link node's right subtree to the child's left subtree. */
	node->right = child->left;
	if (node->right) {
		node->right->parent = node;
	}

	/* Link the node to the child's left. */
	node->parent = child;
	child->left = node;
}

/*!
 * \internal
 * \brief Tree node rotation right.
 * \since 12.0.0
 *
 * \param self Container holding node.
 * \param node Node to perform a right rotation with.
 *
 *        p                         p
 *        |     Right rotation      |
 *        Ch                        N
 *       / \       <---            / \
 *      a  N                      Ch  c
 *        / \                    / \
 *       b   c                  a   b
 *
 * N = node
 * Ch = child
 * p = parent
 * a,b,c = other nodes that are unaffected by the rotation.
 *
 * \note It is assumed that the node's left child exists.
 *
 * \return Nothing
 */
static void rb_rotate_right(struct ao2_container_rbtree *self, struct rbtree_node *node)
{
	struct rbtree_node *child;	/*!< Node's left child. */

	child = node->left;

	/* Link the node's parent to the child. */
	if (!node->parent) {
		/* Node is the root so we get a new root node. */
		self->root = child;
	} else if (node->parent->right == node) {
		/* Node is a right child. */
		node->parent->right = child;
	} else {
		/* Node is a left child. */
		node->parent->left = child;
	}
	child->parent = node->parent;

	/* Link node's left subtree to the child's right subtree. */
	node->left = child->right;
	if (node->left) {
		node->left->parent = node;
	}

	/* Link the node to the child's right. */
	node->parent = child;
	child->right = node;
}

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
static struct ao2_container *rb_ao2_alloc_empty_clone(struct ao2_container_rbtree *self)
{
	struct astobj2 *orig_obj;
	unsigned int ao2_options;

	/* Get container ao2 options. */
	orig_obj = INTERNAL_OBJ(self);
	if (!orig_obj) {
		return NULL;
	}
	ao2_options = orig_obj->priv_data.options;

	return ao2_t_container_alloc_rbtree(ao2_options, self->common.options,
		self->common.sort_fn, self->common.cmp_fn, "Clone rbtree container");
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
static struct ao2_container *rb_ao2_alloc_empty_clone_debug(struct ao2_container_rbtree *self, const char *tag, const char *file, int line, const char *func, int ref_debug)
{
	struct astobj2 *orig_obj;
	unsigned int ao2_options;

	/* Get container ao2 options. */
	orig_obj = INTERNAL_OBJ(self);
	if (!orig_obj) {
		return NULL;
	}
	ao2_options = orig_obj->priv_data.options;

	return __ao2_container_alloc_rbtree_debug(ao2_options, self->common.options,
		self->common.sort_fn, self->common.cmp_fn, tag, file, line, func, ref_debug);
}

/*!
 * \internal
 * \brief Fixup the rbtree after deleting a node.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param child Child of the node just deleted from the container.
 *
 * \note The child must be a dummy black node if there really
 * was no child of the deleted node.  Otherwise, the caller must
 * pass in the parent node and which child was deleted.  In
 * addition, the fixup routine would be more complicated.
 *
 * \return Nothing
 */
static void rb_delete_fixup(struct ao2_container_rbtree *self, struct rbtree_node *child)
{
	struct rbtree_node *sibling;

	while (self->root != child && !child->is_red) {
		if (child->parent->left == child) {
			/* Child is a left child. */
			sibling = child->parent->right;
			ast_assert(sibling != NULL);
			if (sibling->is_red) {
				/* Case 1: The child's sibling is red. */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_left[0]);
				sibling->is_red = 0;
				child->parent->is_red = 1;
				rb_rotate_left(self, child->parent);
				sibling = child->parent->right;
				ast_assert(sibling != NULL);
			}
			/*
			 * The sibling is black.  A black node must have two children,
			 * or one red child, or no children.
			 */
			if ((!sibling->left || !sibling->left->is_red)
				&& (!sibling->right || !sibling->right->is_red)) {
				/*
				 * Case 2: The sibling is black and both of its children are black.
				 *
				 * This case handles the two black children or no children
				 * possibilities of a black node.
				 */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_left[1]);
				sibling->is_red = 1;
				child = child->parent;
			} else {
				/* At this point the sibling has at least one red child. */
				if (!sibling->right || !sibling->right->is_red) {
					/*
					 * Case 3: The sibling is black, its left child is red, and its
					 * right child is black.
					 */
					AO2_DEVMODE_STAT(++self->stats.fixup_delete_left[2]);
					ast_assert(sibling->left != NULL);
					ast_assert(sibling->left->is_red);
					sibling->left->is_red = 0;
					sibling->is_red = 1;
					rb_rotate_right(self, sibling);
					sibling = child->parent->right;
					ast_assert(sibling != NULL);
				}
				/* Case 4: The sibling is black and its right child is red. */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_left[3]);
				sibling->is_red = child->parent->is_red;
				child->parent->is_red = 0;
				if (sibling->right) {
					sibling->right->is_red = 0;
				}
				rb_rotate_left(self, child->parent);
				child = self->root;
			}
		} else {
			/* Child is a right child. */
			sibling = child->parent->left;
			ast_assert(sibling != NULL);
			if (sibling->is_red) {
				/* Case 1: The child's sibling is red. */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_right[0]);
				sibling->is_red = 0;
				child->parent->is_red = 1;
				rb_rotate_right(self, child->parent);
				sibling = child->parent->left;
				ast_assert(sibling != NULL);
			}
			/*
			 * The sibling is black.  A black node must have two children,
			 * or one red child, or no children.
			 */
			if ((!sibling->right || !sibling->right->is_red)
				&& (!sibling->left || !sibling->left->is_red)) {
				/*
				 * Case 2: The sibling is black and both of its children are black.
				 *
				 * This case handles the two black children or no children
				 * possibilities of a black node.
				 */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_right[1]);
				sibling->is_red = 1;
				child = child->parent;
			} else {
				/* At this point the sibling has at least one red child. */
				if (!sibling->left || !sibling->left->is_red) {
					/*
					 * Case 3: The sibling is black, its right child is red, and its
					 * left child is black.
					 */
					AO2_DEVMODE_STAT(++self->stats.fixup_delete_right[2]);
					ast_assert(sibling->right != NULL);
					ast_assert(sibling->right->is_red);
					sibling->right->is_red = 0;
					sibling->is_red = 1;
					rb_rotate_left(self, sibling);
					sibling = child->parent->left;
					ast_assert(sibling != NULL);
				}
				/* Case 4: The sibling is black and its left child is red. */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_right[3]);
				sibling->is_red = child->parent->is_red;
				child->parent->is_red = 0;
				if (sibling->left) {
					sibling->left->is_red = 0;
				}
				rb_rotate_right(self, child->parent);
				child = self->root;
			}
		}
	}

	/*
	 * Case 2 could leave the child node red and it needs to leave
	 * with it black.
	 *
	 * Case 4 sets the child node to the root which of course must
	 * be black.
	 */
	child->is_red = 0;
}

/*!
 * \internal
 * \brief Delete the doomed node from this container.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param doomed Container node to delete from the container.
 *
 * \return Nothing
 */
static void rb_delete_node(struct ao2_container_rbtree *self, struct rbtree_node *doomed)
{
	struct rbtree_node *child;
	int need_fixup;

	if (doomed->left && doomed->right) {
		struct rbtree_node *next;
		int is_red;

		/*
		 * The doomed node has two children.
		 *
		 * Find the next child node and swap it with the doomed node in
		 * the tree.
		 */
		AO2_DEVMODE_STAT(++self->stats.delete_children[2]);
		next = rb_node_most_left(doomed->right);
		SWAP(doomed->parent, next->parent);
		SWAP(doomed->left, next->left);
		SWAP(doomed->right, next->right);
		is_red = doomed->is_red;
		doomed->is_red = next->is_red;
		next->is_red = is_red;

		/* Link back in the next node. */
		if (!next->parent) {
			/* Doomed was the root so we get a new root node. */
			self->root = next;
		} else if (next->parent->left == doomed) {
			/* Doomed was the left child. */
			next->parent->left = next;
		} else {
			/* Doomed was the right child. */
			next->parent->right = next;
		}
		next->left->parent = next;
		if (next->right == next) {
			/* The next node was the right child of doomed. */
			next->right = doomed;
			doomed->parent = next;
		} else {
			next->right->parent = next;
			doomed->parent->left = doomed;
		}

		/* The doomed node has no left child now. */
		ast_assert(doomed->left == NULL);

		/*
		 * We don't have to link the right child back in with doomed
		 * since we are going to link it with doomed's parent anyway.
		 */
		child = doomed->right;
	} else {
		/* Doomed has at most one child. */
		child = doomed->left;
		if (!child) {
			child = doomed->right;
		}
	}
	if (child) {
		AO2_DEVMODE_STAT(++self->stats.delete_children[1]);
	} else {
		AO2_DEVMODE_STAT(++self->stats.delete_children[0]);
	}

	need_fixup = (!doomed->is_red && !self->common.destroying);
	if (need_fixup && !child) {
		/*
		 * Use the doomed node as a place holder node for the
		 * nonexistent child so we also don't have to pass to the fixup
		 * routine the parent and which child the deleted node came
		 * from.
		 */
		rb_delete_fixup(self, doomed);
		ast_assert(doomed->left == NULL);
		ast_assert(doomed->right == NULL);
		ast_assert(!doomed->is_red);
	}

	/* Link the child in place of doomed. */
	if (!doomed->parent) {
		/* Doomed was the root so we get a new root node. */
		self->root = child;
	} else if (doomed->parent->left == doomed) {
		/* Doomed was the left child. */
		doomed->parent->left = child;
	} else {
		/* Doomed was the right child. */
		doomed->parent->right = child;
	}
	if (child) {
		child->parent = doomed->parent;
		if (need_fixup) {
			rb_delete_fixup(self, child);
		}
	}

	AO2_DEVMODE_STAT(--self->common.nodes);
}

/*!
 * \internal
 * \brief Destroy a rbtree container node.
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
static void rb_ao2_node_destructor(void *v_doomed)
{
	struct rbtree_node *doomed = v_doomed;

	if (doomed->common.is_linked) {
		struct ao2_container_rbtree *my_container;

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
		my_container = (struct ao2_container_rbtree *) doomed->common.my_container;
		adjust_lock(my_container, AO2_LOCK_REQ_WRLOCK, 1);

#if defined(AO2_DEBUG) && defined(AST_DEVMODE)
		if (!my_container->common.destroying
			&& ao2_container_check(doomed->common.my_container, OBJ_NOLOCK)) {
			ast_log(LOG_ERROR, "Container integrity failed before node deletion.\n");
		}
#endif	/* defined(AO2_DEBUG) && defined(AST_DEVMODE) */
		rb_delete_node(my_container, doomed);
#if defined(AO2_DEBUG) && defined(AST_DEVMODE)
		if (!my_container->common.destroying
			&& ao2_container_check(doomed->common.my_container, OBJ_NOLOCK)) {
			ast_log(LOG_ERROR, "Container integrity failed after node deletion.\n");
		}
#endif	/* defined(AO2_DEBUG) && defined(AST_DEVMODE) */
	}

	/*
	 * We could have an object in the node if the container is being
	 * destroyed or the node had not been linked in yet.
	 */
	if (doomed->common.obj) {
		ao2_t_ref(doomed->common.obj, -1, "Container node destruction");
		doomed->common.obj = NULL;
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
static struct rbtree_node *rb_ao2_new_node(struct ao2_container_rbtree *self, void *obj_new, const char *tag, const char *file, int line, const char *func)
{
	struct rbtree_node *node;

	node = __ao2_alloc(sizeof(*node), rb_ao2_node_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!node) {
		return NULL;
	}

	if (tag) {
		__ao2_ref_debug(obj_new, +1, tag, file, line, func);
	} else {
		ao2_t_ref(obj_new, +1, "Container node creation");
	}
	node->common.obj = obj_new;
	node->common.my_container = (struct ao2_container *) self;

	return node;
}

/*!
 * \internal
 * \brief Fixup the rbtree after inserting a node.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param node Container node just inserted into the container.
 *
 * \note The just inserted node is red.
 *
 * \return Nothing
 */
static void rb_insert_fixup(struct ao2_container_rbtree *self, struct rbtree_node *node)
{
	struct rbtree_node *g_parent;	/* Grand parent node. */

	while (node->parent && node->parent->is_red) {
		g_parent = node->parent->parent;

		/* The grand parent must exist if the parent is red. */
		ast_assert(g_parent != NULL);

		if (node->parent == g_parent->left) {
			/* The parent is a left child. */
			if (g_parent->right && g_parent->right->is_red) {
				/* Case 1: Push the black down from the grand parent node. */
				AO2_DEVMODE_STAT(++self->stats.fixup_insert_left[0]);
				g_parent->right->is_red = 0;
				g_parent->left->is_red = 0;
				g_parent->is_red = 1;

				node = g_parent;
			} else {
				/* The uncle node is black. */
				if (node->parent->right == node) {
					/*
					 * Case 2: The node is a right child.
					 *
					 * Which node is the grand parent does not change.
					 */
					AO2_DEVMODE_STAT(++self->stats.fixup_insert_left[1]);
					node = node->parent;
					rb_rotate_left(self, node);
				}
				/* Case 3: The node is a left child. */
				AO2_DEVMODE_STAT(++self->stats.fixup_insert_left[2]);
				node->parent->is_red = 0;
				g_parent->is_red = 1;
				rb_rotate_right(self, g_parent);
			}
		} else {
			/* The parent is a right child. */
			if (g_parent->left && g_parent->left->is_red) {
				/* Case 1: Push the black down from the grand parent node. */
				AO2_DEVMODE_STAT(++self->stats.fixup_insert_right[0]);
				g_parent->left->is_red = 0;
				g_parent->right->is_red = 0;
				g_parent->is_red = 1;

				node = g_parent;
			} else {
				/* The uncle node is black. */
				if (node->parent->left == node) {
					/*
					 * Case 2: The node is a left child.
					 *
					 * Which node is the grand parent does not change.
					 */
					AO2_DEVMODE_STAT(++self->stats.fixup_insert_right[1]);
					node = node->parent;
					rb_rotate_right(self, node);
				}
				/* Case 3: The node is a right child. */
				AO2_DEVMODE_STAT(++self->stats.fixup_insert_right[2]);
				node->parent->is_red = 0;
				g_parent->is_red = 1;
				rb_rotate_left(self, g_parent);
			}
		}
	}

	/*
	 * The root could be red here because:
	 * 1) We just inserted the root node in an empty tree.
	 *
	 * 2) Case 1 could leave the root red if the grand parent were
	 * the root.
	 */
	self->root->is_red = 0;
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
static enum ao2_container_insert rb_ao2_insert_node(struct ao2_container_rbtree *self, struct rbtree_node *node)
{
	int cmp;
	struct rbtree_node *cur;
	struct rbtree_node *next;
	ao2_sort_fn *sort_fn;
	uint32_t options;
	enum equal_node_bias bias;

	if (!self->root) {
		/* The tree is empty. */
		self->root = node;
		return AO2_CONTAINER_INSERT_NODE_INSERTED;
	}

	sort_fn = self->common.sort_fn;
	options = self->common.options;
	switch (options & AO2_CONTAINER_ALLOC_OPT_DUPS_MASK) {
	default:
	case AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW:
		if (options & AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN) {
			bias = BIAS_FIRST;
		} else {
			bias = BIAS_LAST;
		}
		break;
	case AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT:
	case AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT:
	case AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE:
		bias = BIAS_EQUAL;
		break;
	}

	/*
	 * New nodes are always colored red when initially inserted into
	 * the tree.  (Except for the root which is always black.)
	 */
	node->is_red = 1;

	/* Find node where normal insert would put a new node. */
	cur = self->root;
	for (;;) {
		if (!cur->common.obj) {
			/* Which direction do we go to insert this node? */
			if (rb_find_empty_direction(cur, sort_fn, node->common.obj, OBJ_SEARCH_OBJECT, bias)
				== GO_LEFT) {
				if (cur->left) {
					cur = cur->left;
					continue;
				}

				/* Node becomes a left child */
				cur->left = node;
				node->parent = cur;
				rb_insert_fixup(self, node);
				return AO2_CONTAINER_INSERT_NODE_INSERTED;
			}
			if (cur->right) {
				cur = cur->right;
				continue;
			}

			/* Node becomes a right child */
			cur->right = node;
			node->parent = cur;
			rb_insert_fixup(self, node);
			return AO2_CONTAINER_INSERT_NODE_INSERTED;
		}
		cmp = sort_fn(cur->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
		if (cmp > 0) {
			if (cur->left) {
				cur = cur->left;
				continue;
			}

			/* Node becomes a left child */
			cur->left = node;
			node->parent = cur;
			rb_insert_fixup(self, node);
			return AO2_CONTAINER_INSERT_NODE_INSERTED;
		} else if (cmp < 0) {
			if (cur->right) {
				cur = cur->right;
				continue;
			}

			/* Node becomes a right child */
			cur->right = node;
			node->parent = cur;
			rb_insert_fixup(self, node);
			return AO2_CONTAINER_INSERT_NODE_INSERTED;
		}
		switch (bias) {
		case BIAS_FIRST:
			/* Duplicate nodes unconditionally accepted. */
			if (cur->left) {
				cur = cur->left;
				continue;
			}

			/* Node becomes a left child */
			cur->left = node;
			node->parent = cur;
			rb_insert_fixup(self, node);
			return AO2_CONTAINER_INSERT_NODE_INSERTED;
		case BIAS_EQUAL:
			break;
		case BIAS_LAST:
			/* Duplicate nodes unconditionally accepted. */
			if (cur->right) {
				cur = cur->right;
				continue;
			}

			/* Node becomes a right child */
			cur->right = node;
			node->parent = cur;
			rb_insert_fixup(self, node);
			return AO2_CONTAINER_INSERT_NODE_INSERTED;
		}

		break;
	}

	/* Node is a dupliate */
	switch (options & AO2_CONTAINER_ALLOC_OPT_DUPS_MASK) {
	default:
	case AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW:
		ast_assert(0);/* Case already handled by BIAS_FIRST/BIAS_LAST. */
		return AO2_CONTAINER_INSERT_NODE_REJECTED;
	case AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT:
		/* Reject all objects with the same key. */
		return AO2_CONTAINER_INSERT_NODE_REJECTED;
	case AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT:
		if (cur->common.obj == node->common.obj) {
			/* Reject inserting the same object */
			return AO2_CONTAINER_INSERT_NODE_REJECTED;
		}
		next = cur;
		if (options & AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN) {
			/* Search to end of duplicates for the same object. */
			for (;;) {
				next = rb_node_next_full(next);
				if (!next) {
					break;
				}
				if (next->common.obj == node->common.obj) {
					/* Reject inserting the same object */
					return AO2_CONTAINER_INSERT_NODE_REJECTED;
				}
				cmp = sort_fn(next->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
				if (cmp) {
					break;
				}
			}

			/* Find first duplicate node. */
			for (;;) {
				next = rb_node_prev_full(cur);
				if (!next) {
					break;
				}
				if (next->common.obj == node->common.obj) {
					/* Reject inserting the same object */
					return AO2_CONTAINER_INSERT_NODE_REJECTED;
				}
				cmp = sort_fn(next->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
				if (cmp) {
					break;
				}
				cur = next;
			}
			if (!cur->left) {
				/* Node becomes a left child */
				cur->left = node;
			} else {
				/* Node becomes a right child */
				cur = rb_node_most_right(cur->left);
				cur->right = node;
			}
		} else {
			/* Search to beginning of duplicates for the same object. */
			for (;;) {
				next = rb_node_prev_full(next);
				if (!next) {
					break;
				}
				if (next->common.obj == node->common.obj) {
					/* Reject inserting the same object */
					return AO2_CONTAINER_INSERT_NODE_REJECTED;
				}
				cmp = sort_fn(next->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
				if (cmp) {
					break;
				}
			}

			/* Find last duplicate node. */
			for (;;) {
				next = rb_node_next_full(cur);
				if (!next) {
					break;
				}
				if (next->common.obj == node->common.obj) {
					/* Reject inserting the same object */
					return AO2_CONTAINER_INSERT_NODE_REJECTED;
				}
				cmp = sort_fn(next->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
				if (cmp) {
					break;
				}
				cur = next;
			}
			if (!cur->right) {
				/* Node becomes a right child */
				cur->right = node;
			} else {
				/* Node becomes a left child */
				cur = rb_node_most_left(cur->right);
				cur->left = node;
			}
		}
		break;
	case AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE:
		SWAP(cur->common.obj, node->common.obj);
		return AO2_CONTAINER_INSERT_NODE_OBJ_REPLACED;
	}

	/* Complete inserting duplicate node. */
	node->parent = cur;
	rb_insert_fixup(self, node);
	return AO2_CONTAINER_INSERT_NODE_INSERTED;
}

/*! Traversal state to restart a rbtree container traversal. */
struct rbtree_traversal_state {
	/*! Active sort function in the traversal if not NULL. */
	ao2_sort_fn *sort_fn;
	/*! Saved comparison callback arg pointer. */
	void *arg;
	/*! Saved search flags to control traversing the container. */
	enum search_flags flags;
};

struct rbtree_traversal_state_check {
	/*
	 * If we have a division by zero compile error here then there
	 * is not enough room for the state.  Increase AO2_TRAVERSAL_STATE_SIZE.
	 */
	char check[1 / (AO2_TRAVERSAL_STATE_SIZE / sizeof(struct rbtree_traversal_state))];
};

/*!
 * \internal
 * \brief Find the next rbtree container node in a traversal.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param state Traversal state to restart rbtree container traversal.
 * \param prev Previous node returned by the traversal search functions.
 *    The ref ownership is passed back to this function.
 *
 * \retval node-ptr of found node (Reffed).
 * \retval NULL when no node found.
 */
static struct rbtree_node *rb_ao2_find_next(struct ao2_container_rbtree *self, struct rbtree_traversal_state *state, struct rbtree_node *prev)
{
	struct rbtree_node *node;
	void *arg;
	enum search_flags flags;
	int cmp;

	arg = state->arg;
	flags = state->flags;

	node = prev;
	for (;;) {
		/* Find next node in traversal order. */
		switch (flags & OBJ_ORDER_MASK) {
		default:
		case OBJ_ORDER_ASCENDING:
			node = rb_node_next(node);
			break;
		case OBJ_ORDER_DESCENDING:
			node = rb_node_prev(node);
			break;
		case OBJ_ORDER_PRE:
			node = rb_node_pre(node);
			break;
		case OBJ_ORDER_POST:
			node = rb_node_post(node);
			break;
		}
		if (!node) {
			/* No more nodes left to traverse. */
			break;
		}
		if (!node->common.obj) {
			/* Node is empty */
			continue;
		}

		if (state->sort_fn) {
			/* Filter node through the sort_fn */
			cmp = state->sort_fn(node->common.obj, arg, flags & OBJ_SEARCH_MASK);
			if (cmp) {
				/* No more nodes in this container are possible to match. */
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
	}

	/* No more nodes in the container left to traverse. */
	__ao2_ref(prev, -1);
	return NULL;
}

/*!
 * \internal
 * \brief Find an initial matching node.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_SEARCH_OBJECT - if set, 'obj_right', is an object.
 *   OBJ_SEARCH_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_SEARCH_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 * \param bias How to bias search direction for duplicates
 *
 * \retval node on success.
 * \retval NULL if not found.
 */
static struct rbtree_node *rb_find_initial(struct ao2_container_rbtree *self, void *obj_right, enum search_flags flags, enum equal_node_bias bias)
{
	int cmp;
	enum search_flags sort_flags;
	struct rbtree_node *node;
	struct rbtree_node *next = NULL;
	ao2_sort_fn *sort_fn;

	sort_flags = flags & OBJ_SEARCH_MASK;
	sort_fn = self->common.sort_fn;

	/* Find node where normal search would find it. */
	node = self->root;
	if (!node) {
		return NULL;
	}
	for (;;) {
		if (!node->common.obj) {
			/* Which direction do we go to find the node? */
			if (rb_find_empty_direction(node, sort_fn, obj_right, sort_flags, bias)
				== GO_LEFT) {
				next = node->left;
			} else {
				next = node->right;
			}
			if (!next) {
				switch (bias) {
				case BIAS_FIRST:
					/* Check successor node for match. */
					next = rb_node_next_full(node);
					break;
				case BIAS_EQUAL:
					break;
				case BIAS_LAST:
					/* Check previous node for match. */
					next = rb_node_prev_full(node);
					break;
				}
				if (next) {
					cmp = sort_fn(next->common.obj, obj_right, sort_flags);
					if (cmp == 0) {
						/* Found the first/last matching node. */
						return next;
					}
					next = NULL;
				}

				/* No match found. */
				return next;
			}
		} else {
			cmp = sort_fn(node->common.obj, obj_right, sort_flags);
			if (cmp > 0) {
				next = node->left;
			} else if (cmp < 0) {
				next = node->right;
			} else {
				switch (bias) {
				case BIAS_FIRST:
					next = node->left;
					break;
				case BIAS_EQUAL:
					return node;
				case BIAS_LAST:
					next = node->right;
					break;
				}
				if (!next) {
					/* Found the first/last matching node. */
					return node;
				}
			}
			if (!next) {
				switch (bias) {
				case BIAS_FIRST:
					if (cmp < 0) {
						/* Check successor node for match. */
						next = rb_node_next_full(node);
					}
					break;
				case BIAS_EQUAL:
					break;
				case BIAS_LAST:
					if (cmp > 0) {
						/* Check previous node for match. */
						next = rb_node_prev_full(node);
					}
					break;
				}
				if (next) {
					cmp = sort_fn(next->common.obj, obj_right, sort_flags);
					if (cmp == 0) {
						/* Found the first/last matching node. */
						return next;
					}
				}

				/* No match found. */
				return NULL;
			}
		}
		node = next;
	}
}

/*!
 * \internal
 * \brief Find the first rbtree container node in a traversal.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param flags search_flags to control traversing the container
 * \param arg Comparison callback arg parameter.
 * \param state Traversal state to restart rbtree container traversal.
 *
 * \retval node-ptr of found node (Reffed).
 * \retval NULL when no node found.
 */
static struct rbtree_node *rb_ao2_find_first(struct ao2_container_rbtree *self, enum search_flags flags, void *arg, struct rbtree_traversal_state *state)
{
	struct rbtree_node *node;
	enum equal_node_bias bias;

	if (self->common.destroying) {
		/* Force traversal to be post order for tree destruction. */
		flags = OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE | OBJ_ORDER_POST;
	}

	memset(state, 0, sizeof(*state));
	state->arg = arg;
	state->flags = flags;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
	case OBJ_SEARCH_KEY:
	case OBJ_SEARCH_PARTIAL_KEY:
		/* We are asked to do a directed search. */
		state->sort_fn = self->common.sort_fn;
		break;
	default:
		/* Don't know, let's visit all nodes */
		state->sort_fn = NULL;
		break;
	}

	if (!self->root) {
		/* Tree is empty. */
		return NULL;
	}

	/* Find first traversal node. */
	switch (flags & OBJ_ORDER_MASK) {
	default:
	case OBJ_ORDER_ASCENDING:
		if (!state->sort_fn) {
			/* Find left most child. */
			node = rb_node_most_left(self->root);
			if (!node->common.obj) {
				node = rb_node_next_full(node);
				if (!node) {
					return NULL;
				}
			}
			break;
		}

		/* Search for initial node. */
		switch (self->common.options & AO2_CONTAINER_ALLOC_OPT_DUPS_MASK) {
		case AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE:
			if ((flags & OBJ_SEARCH_MASK) != OBJ_SEARCH_PARTIAL_KEY) {
				/* There are no duplicates allowed. */
				bias = BIAS_EQUAL;
				break;
			}
			/* Fall through */
		default:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT:
			/* Find first duplicate node. */
			bias = BIAS_FIRST;
			break;
		}
		node = rb_find_initial(self, arg, flags, bias);
		if (!node) {
			return NULL;
		}
		break;
	case OBJ_ORDER_DESCENDING:
		if (!state->sort_fn) {
			/* Find right most child. */
			node = rb_node_most_right(self->root);
			if (!node->common.obj) {
				node = rb_node_prev_full(node);
				if (!node) {
					return NULL;
				}
			}
			break;
		}

		/* Search for initial node. */
		switch (self->common.options & AO2_CONTAINER_ALLOC_OPT_DUPS_MASK) {
		case AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE:
			if ((flags & OBJ_SEARCH_MASK) != OBJ_SEARCH_PARTIAL_KEY) {
				/* There are no duplicates allowed. */
				bias = BIAS_EQUAL;
				break;
			}
			/* Fall through */
		default:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT:
			/* Find last duplicate node. */
			bias = BIAS_LAST;
			break;
		}
		node = rb_find_initial(self, arg, flags, bias);
		if (!node) {
			return NULL;
		}
		break;
	case OBJ_ORDER_PRE:
		/* This is a tree structure traversal so we must visit all nodes. */
		state->sort_fn = NULL;

		node = self->root;

		/* Find a non-empty node. */
		while (!node->common.obj) {
			node = rb_node_pre(node);
			if (!node) {
				return NULL;
			}
		}
		break;
	case OBJ_ORDER_POST:
		/* This is a tree structure traversal so we must visit all nodes. */
		state->sort_fn = NULL;

		/* Find the left most childless node. */
		node = self->root;
		for (;;) {
			node = rb_node_most_left(node);
			if (!node->right) {
				/* This node has no children. */
				break;
			}
			node = node->right;
		}

		/* Find a non-empty node. */
		while (!node->common.obj) {
			node = rb_node_post(node);
			if (!node) {
				return NULL;
			}
		}
		break;
	}

	/* We have the first traversal node */
	__ao2_ref(node, +1);
	return node;
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
static struct rbtree_node *rb_ao2_iterator_next(struct ao2_container_rbtree *self, struct rbtree_node *node, enum ao2_iterator_flags flags)
{
	if (flags & AO2_ITERATOR_DESCENDING) {
		if (!node) {
			/* Find right most node. */
			if (!self->root) {
				return NULL;
			}
			node = rb_node_most_right(self->root);
			if (node->common.obj) {
				/* Found a non-empty node. */
				return node;
			}
		}
		/* Find next non-empty node. */
		node = rb_node_prev_full(node);
	} else {
		if (!node) {
			/* Find left most node. */
			if (!self->root) {
				return NULL;
			}
			node = rb_node_most_left(self->root);
			if (node->common.obj) {
				/* Found a non-empty node. */
				return node;
			}
		}
		/* Find next non-empty node. */
		node = rb_node_next_full(node);
	}

	return node;
}

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
static void rb_ao2_destroy(struct ao2_container_rbtree *self)
{
	/* Check that the container no longer has any nodes */
	if (self->root) {
		ast_log(LOG_ERROR, "Node ref leak.  Red-Black tree container still has nodes!\n");
		ast_assert(0);
	}
}

#if defined(AST_DEVMODE)
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
static void rb_ao2_dump(struct ao2_container_rbtree *self, void *where, ao2_prnt_fn *prnt, ao2_prnt_obj_fn *prnt_obj)
{
#define FORMAT  "%16s, %16s, %16s, %16s, %5s, %16s, %s\n"
#define FORMAT2 "%16p, %16p, %16p, %16p, %5s, %16p, "

	struct rbtree_node *node;

	prnt(where, FORMAT, "Node", "Parent", "Left", "Right", "Color", "Obj", "Key");
	for (node = self->root; node; node = rb_node_pre(node)) {
		prnt(where, FORMAT2,
			node,
			node->parent,
			node->left,
			node->right,
			node->is_red ? "Red" : "Black",
			node->common.obj);
		if (node->common.obj && prnt_obj) {
			prnt_obj(node->common.obj, where, prnt);
		}
		prnt(where, "\n");
	}

#undef FORMAT
#undef FORMAT2
}
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
static void rb_ao2_stats(struct ao2_container_rbtree *self, void *where, ao2_prnt_fn *prnt)
{
	int idx;

	for (idx = 0; idx < ARRAY_LEN(self->stats.fixup_insert_left); ++idx) {
		prnt(where, "Number of left insert fixups case %d: %d\n", idx + 1,
			self->stats.fixup_insert_left[idx]);
	}
	for (idx = 0; idx < ARRAY_LEN(self->stats.fixup_insert_right); ++idx) {
		prnt(where, "Number of right insert fixups case %d: %d\n", idx + 1,
			self->stats.fixup_insert_right[idx]);
	}

	for (idx = 0; idx < ARRAY_LEN(self->stats.delete_children); ++idx) {
		prnt(where, "Number of nodes deleted with %d children: %d\n", idx,
			self->stats.delete_children[idx]);
	}
	for (idx = 0; idx < ARRAY_LEN(self->stats.fixup_delete_left); ++idx) {
		prnt(where, "Number of left delete fixups case %d: %d\n", idx + 1,
			self->stats.fixup_delete_left[idx]);
	}
	for (idx = 0; idx < ARRAY_LEN(self->stats.fixup_delete_right); ++idx) {
		prnt(where, "Number of right delete fixups case %d: %d\n", idx + 1,
			self->stats.fixup_delete_right[idx]);
	}
}
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
/*!
 * \internal
 * \brief Check the black height of the given node.
 * \since 12.0.0
 *
 * \param node Node to check black height.
 *
 * \retval black-height of node on success.
 * \retval -1 on error.  Node black height did not balance.
 */
static int rb_check_black_height(struct rbtree_node *node)
{
	int height_left;
	int height_right;

	if (!node) {
		/* A NULL child is a black node. */
		return 0;
	}

	height_left = rb_check_black_height(node->left);
	if (height_left < 0) {
		return -1;
	}
	height_right = rb_check_black_height(node->right);
	if (height_right < 0) {
		return -1;
	}
	if (height_left != height_right) {
		ast_log(LOG_ERROR,
			"Tree node black height of children does not match! L:%d != R:%d\n",
			height_left, height_right);
		return -1;
	}
	if (!node->is_red) {
		/* The node itself is black. */
		++height_left;
	}
	return height_left;
}

#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
static int rb_ao2_integrity(struct ao2_container_rbtree *self)
{
	int res;
	int count_node;
	int count_obj;
	void *obj_last;
	struct rbtree_node *node;

	res = 0;

	count_node = 0;
	count_obj = 0;

	/*
	 * See the properties listed at struct rbtree_node definition.
	 *
	 * The rbtree properties 1 and 3 are not testable.
	 *
	 * Property 1 is not testable because we are not rebalancing at
	 * this time so all nodes are either red or black.
	 *
	 * Property 3 is not testable because it is the definition of a
	 * NULL child.
	 */
	if (self->root) {
		/* Check tree links. */
		if (self->root->parent) {
			if (self->root->parent == self->root) {
				ast_log(LOG_ERROR, "Tree root parent pointer points to itself!\n");
			} else {
				ast_log(LOG_ERROR, "Tree root is not a root node!\n");
			}
			return -1;
		}
		if (self->root->is_red) {
			/* Violation rbtree property 2. */
			ast_log(LOG_ERROR, "Tree root is red!\n");
			res = -1;
		}
		node = self->root;
		do {
			if (node->left) {
				if (node->left == node) {
					ast_log(LOG_ERROR, "Tree node's left pointer points to itself!\n");
					return -1;
				}
				if (node->left->parent != node) {
					ast_log(LOG_ERROR, "Tree node's left child does not link back!\n");
					return -1;
				}
			}
			if (node->right) {
				if (node->right == node) {
					ast_log(LOG_ERROR, "Tree node's right pointer points to itself!\n");
					return -1;
				}
				if (node->right->parent != node) {
					ast_log(LOG_ERROR, "Tree node's right child does not link back!\n");
					return -1;
				}
			}

			/* Check red/black node flags. */
			if (node->is_red) {
				/* A red node must have two black children or no children. */
				if (node->left && node->right) {
					/* Node has two children. */
					if (node->left->is_red) {
						/* Violation rbtree property 4. */
						ast_log(LOG_ERROR, "Tree node is red and its left child is red!\n");
						res = -1;
					}
					if (node->right->is_red) {
						/* Violation rbtree property 4. */
						ast_log(LOG_ERROR, "Tree node is red and its right child is red!\n");
						res = -1;
					}
				} else if (node->left || node->right) {
					/*
					 * Violation rbtree property 4 if the child is red.
					 * Violation rbtree property 5 if the child is black.
					 */
					ast_log(LOG_ERROR, "Tree node is red and it only has one child!\n");
					res = -1;
				}
			} else {
				/*
				 * A black node must have two children, or one red child, or no
				 * children.  If the black node has two children and only one of
				 * them is red, that red child must have two children.
				 */
				if (node->left && node->right) {
					/* Node has two children. */
					if (node->left->is_red != node->right->is_red) {
						/* The children are not the same color. */
						struct rbtree_node *red;

						if (node->left->is_red) {
							red = node->left;
						} else {
							red = node->right;
						}
						if (!red->left || !red->right) {
							/* Violation rbtree property 5. */
							ast_log(LOG_ERROR,
								"Tree node is black and the red child does not have two children!\n");
							res = -1;
						}
					}
				} else if ((node->left && !node->left->is_red)
					|| (node->right && !node->right->is_red)) {
					/* Violation rbtree property 5. */
					ast_log(LOG_ERROR, "Tree node is black and its only child is black!\n");
					res = -1;
				}
			}

			/* Count nodes and objects. */
			++count_node;
			if (node->common.obj) {
				++count_obj;
			}

			node = rb_node_pre(node);
		} while (node);

		/* Check node key sort order. */
		obj_last = NULL;
		for (node = rb_node_most_left(self->root); node; node = rb_node_next(node)) {
			if (!node->common.obj) {
				/* Node is empty. */
				continue;
			}

			if (obj_last) {
				if (self->common.sort_fn(obj_last, node->common.obj, OBJ_SEARCH_OBJECT) > 0) {
					ast_log(LOG_ERROR, "Tree nodes are out of sorted order!\n");
					return -1;
				}
			}
			obj_last = node->common.obj;
		}

		/* Completely check property 5 */
		if (!res && rb_check_black_height(self->root) < 0) {
			/* Violation rbtree property 5. */
			res = -1;
		}
	}

	/* Check total obj count. */
	if (count_obj != ao2_container_count(&self->common)) {
		ast_log(LOG_ERROR, "Total object count does not match ao2_container_count()!\n");
		return -1;
	}

	/* Check total node count. */
	if (count_node != self->common.nodes) {
		ast_log(LOG_ERROR, "Total node count of %d does not match stat of %d!\n",
			count_node, self->common.nodes);
		return -1;
	}

	return res;
}
#endif	/* defined(AST_DEVMODE) */

/*! rbtree container virtual method table. */
static const struct ao2_container_methods v_table_rbtree = {
	.type = AO2_CONTAINER_RTTI_RBTREE,
	.alloc_empty_clone = (ao2_container_alloc_empty_clone_fn) rb_ao2_alloc_empty_clone,
	.alloc_empty_clone_debug =
		(ao2_container_alloc_empty_clone_debug_fn) rb_ao2_alloc_empty_clone_debug,
	.new_node = (ao2_container_new_node_fn) rb_ao2_new_node,
	.insert = (ao2_container_insert_fn) rb_ao2_insert_node,
	.traverse_first = (ao2_container_find_first_fn) rb_ao2_find_first,
	.traverse_next = (ao2_container_find_next_fn) rb_ao2_find_next,
	.iterator_next = (ao2_iterator_next_fn) rb_ao2_iterator_next,
	.destroy = (ao2_container_destroy_fn) rb_ao2_destroy,
#if defined(AST_DEVMODE)
	.dump = (ao2_container_display) rb_ao2_dump,
	.stats = (ao2_container_statistics) rb_ao2_stats,
	.integrity = (ao2_container_integrity) rb_ao2_integrity,
#endif	/* defined(AST_DEVMODE) */
};

/*!
 * \brief Initialize a rbtree container.
 *
 * \param self Container to initialize.
 * \param options Container behaviour options (See enum ao2_container_opts)
 * \param sort_fn Pointer to a sort function.
 * \param cmp_fn Pointer to a compare function used by ao2_find.
 *
 * \return A pointer to a struct container.
 */
static struct ao2_container *rb_ao2_container_init(struct ao2_container_rbtree *self,
	unsigned int options, ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn)
{
	if (!self) {
		return NULL;
	}

	self->common.v_table = &v_table_rbtree;
	self->common.sort_fn = sort_fn;
	self->common.cmp_fn = cmp_fn;
	self->common.options = options;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_containers, 1);
#endif

	return (struct ao2_container *) self;
}

struct ao2_container *__ao2_container_alloc_rbtree(unsigned int ao2_options, unsigned int container_options,
	ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn)
{
	struct ao2_container_rbtree *self;

	if (!sort_fn) {
		/* Sanity checks. */
		ast_log(LOG_ERROR, "Missing sort_fn()!\n");
		return NULL;
	}

	self = ao2_t_alloc_options(sizeof(*self), container_destruct, ao2_options,
		"New rbtree container");
	return rb_ao2_container_init(self, container_options, sort_fn, cmp_fn);
}

struct ao2_container *__ao2_container_alloc_rbtree_debug(unsigned int ao2_options, unsigned int container_options,
	ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn,
	const char *tag, const char *file, int line, const char *func, int ref_debug)
{
	struct ao2_container_rbtree *self;

	if (!sort_fn) {
		/* Sanity checks. */
		ast_log(__LOG_ERROR, file, line, func, "Missing sort_fn()!\n");
		return NULL;
	}

	self = __ao2_alloc_debug(sizeof(*self),
		ref_debug ? container_destruct_debug : container_destruct, ao2_options,
		tag, file, line, func, ref_debug);
	return rb_ao2_container_init(self, container_options, sort_fn, cmp_fn);
}

#ifdef AO2_DEBUG
static int print_cb(void *obj, void *arg, int flag)
{
	struct ast_cli_args *a = (struct ast_cli_args *) arg;
	char *s = (char *)obj;

	ast_cli(a->fd, "string <%s>\n", s);
	return 0;
}

/*
 * Print stats
 */
static char *handle_astobj2_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "astobj2 show stats";
		e->usage = "Usage: astobj2 show stats\n"
			   "       Show astobj2 show stats\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	ast_cli(a->fd, "Objects    : %d\n", ao2.total_objects);
	ast_cli(a->fd, "Containers : %d\n", ao2.total_containers);
	ast_cli(a->fd, "Memory     : %d\n", ao2.total_mem);
	ast_cli(a->fd, "Locked     : %d\n", ao2.total_locked);
	ast_cli(a->fd, "Refs       : %d\n", ao2.total_refs);
	return CLI_SUCCESS;
}

/*
 * This is testing code for astobj
 */
static char *handle_astobj2_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *c1;
	struct ao2_container *c2;
	int i, lim;
	char *obj;
	static int prof_id = -1;
	struct ast_cli_args fake_args = { a->fd, 0, NULL };

	switch (cmd) {
	case CLI_INIT:
		e->command = "astobj2 test";
		e->usage = "Usage: astobj2 test <num>\n"
			   "       Runs astobj2 test. Creates 'num' objects,\n"
			   "       and test iterators, callbacks and maybe other stuff\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	if (prof_id == -1) {
		prof_id = ast_add_profile("ao2_alloc", 0);
	}

	ast_cli(a->fd, "argc %d argv %s %s %s\n", a->argc, a->argv[0], a->argv[1], a->argv[2]);
	lim = atoi(a->argv[2]);
	ast_cli(a->fd, "called astobj_test\n");

	handle_astobj2_stats(e, CLI_HANDLER, &fake_args);
	/*
	 * Allocate a list container.
	 */
	c1 = ao2_t_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL /* no sort */,
		NULL /* no callback */, "test");
	ast_cli(a->fd, "container allocated as %p\n", c1);

	/*
	 * fill the container with objects.
	 * ao2_alloc() gives us a reference which we pass to the
	 * container when we do the insert.
	 */
	for (i = 0; i < lim; i++) {
		ast_mark(prof_id, 1 /* start */);
		obj = ao2_t_alloc(80, NULL,"test");
		ast_mark(prof_id, 0 /* stop */);
		ast_cli(a->fd, "object %d allocated as %p\n", i, obj);
		sprintf(obj, "-- this is obj %d --", i);
		ao2_link(c1, obj);
		/* At this point, the refcount on obj is 2 due to the allocation
		 * and linking. We can go ahead and reduce the refcount by 1
		 * right here so that when the container is unreffed later, the
		 * objects will be freed
		 */
		ao2_t_ref(obj, -1, "test");
	}

	ast_cli(a->fd, "testing callbacks\n");
	ao2_t_callback(c1, 0, print_cb, a, "test callback");

	ast_cli(a->fd, "testing container cloning\n");
	c2 = ao2_container_clone(c1, 0);
	if (ao2_container_count(c1) != ao2_container_count(c2)) {
		ast_cli(a->fd, "Cloned container does not have the same number of objects!\n");
	}
	ao2_t_callback(c2, 0, print_cb, a, "test callback");

	ast_cli(a->fd, "testing iterators, remove every second object\n");
	{
		struct ao2_iterator ai;
		int x = 0;

		ai = ao2_iterator_init(c1, 0);
		while ( (obj = ao2_t_iterator_next(&ai,"test")) ) {
			ast_cli(a->fd, "iterator on <%s>\n", obj);
			if (x++ & 1)
				ao2_t_unlink(c1, obj,"test");
			ao2_t_ref(obj, -1,"test");
		}
		ao2_iterator_destroy(&ai);
		ast_cli(a->fd, "testing iterators again\n");
		ai = ao2_iterator_init(c1, 0);
		while ( (obj = ao2_t_iterator_next(&ai,"test")) ) {
			ast_cli(a->fd, "iterator on <%s>\n", obj);
			ao2_t_ref(obj, -1,"test");
		}
		ao2_iterator_destroy(&ai);
	}

	ast_cli(a->fd, "testing callbacks again\n");
	ao2_t_callback(c1, 0, print_cb, a, "test callback");

	ast_verbose("now you should see an error and possible assertion failure messages:\n");
	ao2_t_ref(&i, -1, "");	/* i is not a valid object so we print an error here */

	ast_cli(a->fd, "destroy container\n");
	ao2_t_ref(c1, -1, "");	/* destroy container */
	ao2_t_ref(c2, -1, "");	/* destroy container */
	handle_astobj2_stats(e, CLI_HANDLER, &fake_args);
	return CLI_SUCCESS;
}
#endif /* AO2_DEBUG */

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
static void ao2_reg_destructor(void *v_doomed)
{
	struct ao2_reg_container *doomed = v_doomed;

	if (doomed->registered) {
		ao2_t_ref(doomed->registered, -1, "Releasing registered container.");
	}
}
#endif	/* defined(AST_DEVMODE) */

int ao2_container_register(const char *name, struct ao2_container *self, ao2_prnt_obj_fn *prnt_obj)
{
	int res = 0;
#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */
	return res;
}

void ao2_container_unregister(const char *name)
{
#if defined(AST_DEVMODE)
	ao2_t_find(reg_containers, name, OBJ_UNLINK | OBJ_NODATA | OBJ_SEARCH_KEY,
		"Unregister container");
#endif	/* defined(AST_DEVMODE) */
}

#if defined(AST_DEVMODE)
static int ao2_complete_reg_cb(void *obj, void *arg, void *data, int flags)
{
	struct ao2_reg_match *which = data;

	/* ao2_reg_sort_cb() has already filtered the search to matching keys */
	return (which->find_nth < ++which->count) ? (CMP_MATCH | CMP_STOP) : 0;
}
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

#if defined(AST_DEVMODE)
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
#endif	/* defined(AST_DEVMODE) */

#if defined(AO2_DEBUG) || defined(AST_DEVMODE)
static struct ast_cli_entry cli_astobj2[] = {
#if defined(AO2_DEBUG)
	AST_CLI_DEFINE(handle_astobj2_stats, "Print astobj2 statistics"),
	AST_CLI_DEFINE(handle_astobj2_test, "Test astobj2"),
#endif /* defined(AO2_DEBUG) */
#if defined(AST_DEVMODE)
	AST_CLI_DEFINE(handle_cli_astobj2_container_dump, "Show container contents"),
	AST_CLI_DEFINE(handle_cli_astobj2_container_stats, "Show container statistics"),
	AST_CLI_DEFINE(handle_cli_astobj2_container_check, "Perform a container integrity check"),
#endif	/* defined(AST_DEVMODE) */
};
#endif	/* defined(AO2_DEBUG) || defined(AST_DEVMODE) */

#if defined(AO2_DEBUG) || defined(AST_DEVMODE)
static void astobj2_cleanup(void)
{
#if defined(AST_DEVMODE)
	ao2_t_ref(reg_containers, -1, "Releasing container registration container");
	reg_containers = NULL;
#endif
	ast_cli_unregister_multiple(cli_astobj2, ARRAY_LEN(cli_astobj2));
}
#endif

int astobj2_init(void)
{
#if defined(AST_DEVMODE)
	reg_containers = ao2_t_container_alloc_list(AO2_ALLOC_OPT_LOCK_RWLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, ao2_reg_sort_cb, NULL,
		"Container registration container.");
	if (!reg_containers) {
		return -1;
	}
#endif	/* defined(AST_DEVMODE) */
#if defined(AO2_DEBUG) || defined(AST_DEVMODE)
	ast_cli_register_multiple(cli_astobj2, ARRAY_LEN(cli_astobj2));
	ast_register_atexit(astobj2_cleanup);
#endif	/* defined(AO2_DEBUG) || defined(AST_DEVMODE) */

	return 0;
}

