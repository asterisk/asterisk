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

/* This reduces the size of lock structures within astobj2 objects when
 * DEBUG_THREADS is not defined. */
#define DEBUG_THREADS_LOOSE_ABI

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "astobj2_private.h"
#include "astobj2_container_private.h"
#include "asterisk/cli.h"
#include "asterisk/paths.h"

/* Use ast_log_safe in place of ast_log. */
#define ast_log ast_log_safe

static FILE *ref_log;

/*!
 * astobj2 objects are always preceded by this data structure,
 * which contains a reference counter,
 * option flags and a pointer to a destructor.
 * The refcount is used to decide when it is time to
 * invoke the destructor.
 * The magic number is used for consistency check.
 */
struct __priv_data {
	ao2_destructor_fn destructor_fn;
	/*! This field is used for astobj2 and ao2_weakproxy objects to reference each other */
	void *weakptr;
#if defined(AO2_DEBUG)
	/*! User data size for stats */
	size_t data_size;
#endif
	/*! Number of references held for this object */
	int32_t ref_counter;
	/*!
	 * \brief The ao2 object option flags.
	 *
	 * \note This field is constant after object creation.  It shares
	 *       a uint32_t with \ref lockused and \ref magic.
	 */
	uint32_t options:2;
	/*!
	 * \brief Set to 1 when the lock is used if refdebug is enabled.
	 *
	 * \note This bit-field may be modified after object creation.  It
	 *       shares a uint32_t with \ref options and \ref magic.
	 */
	uint32_t lockused:1;
	/*!
	 * \brief Magic number.
	 *
	 * This is used to verify that a pointer is a valid astobj2 or ao2_weak
	 * reference.
	 *
	 * \note This field is constant after object creation.  It shares
	 *       a uint32_t with \ref options and \ref lockused.
	 *
	 * \warning Stealing bits for any additional writable fields would cause
	 *          reentrancy issues if using bitfields.  If any additional
	 *          writable bits are required in the future we will need to put
	 *          all bitfields into a single 'uint32_t flags' field and use
	 *          atomic operations from \file lock.h to perform writes.
	 */
	uint32_t magic:29;
};

#define	AO2_MAGIC	0x1a70b123
#define	AO2_WEAK	0x1a70b122
#define IS_AO2_MAGIC_BAD(p) (AO2_MAGIC != (p->priv_data.magic | 1))

/*!
 * What an astobj2 object looks like: fixed-size private data
 * followed by variable-size user data.
 */
struct astobj2 {
	struct __priv_data priv_data;
	void *user_data[0];
};

struct ao2_weakproxy_notification {
	ao2_weakproxy_notification_cb cb;
	void *data;
	AST_LIST_ENTRY(ao2_weakproxy_notification) list;
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

struct ao2_lockobj_priv {
	void *lock;
};

/* AstObj2 with locking provided by a separate object. */
struct astobj2_lockobj {
	struct ao2_lockobj_priv lockobj;
	struct __priv_data priv_data;
	void *user_data[0];
};

#ifdef AO2_DEBUG
struct ao2_stats ao2;
#endif

#define INTERNAL_OBJ_MUTEX(user_data) \
	((struct astobj2_lock *) (((char *) (user_data)) - sizeof(struct astobj2_lock)))

#define INTERNAL_OBJ_RWLOCK(user_data) \
	((struct astobj2_rwlock *) (((char *) (user_data)) - sizeof(struct astobj2_rwlock)))

#define INTERNAL_OBJ_LOCKOBJ(user_data) \
	((struct astobj2_lockobj *) (((char *) (user_data)) - sizeof(struct astobj2_lockobj)))

#define INTERNAL_OBJ(user_data) \
	(struct astobj2 *) ((char *) user_data - sizeof(struct astobj2))

/*!
 * \brief convert from a pointer _p to a user-defined object
 *
 * \return the pointer to the astobj2 structure
 */
#define __INTERNAL_OBJ_CHECK(user_data, file, line, func) \
	({ \
		struct astobj2 *p ## __LINE__; \
		if (!user_data \
			|| !(p ## __LINE__ = INTERNAL_OBJ(user_data)) \
			|| IS_AO2_MAGIC_BAD(p ## __LINE__)) { \
			log_bad_ao2(user_data, file, line, func); \
			p ## __LINE__ = NULL; \
		} \
		(p ## __LINE__); \
	})

#define INTERNAL_OBJ_CHECK(user_data) \
	__INTERNAL_OBJ_CHECK(user_data, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief convert from a pointer _p to an astobj2 object
 *
 * \return the pointer to the user-defined portion.
 */
#define EXTERNAL_OBJ(_p)	((_p) == NULL ? NULL : (_p)->user_data)

int internal_is_ao2_object(void *user_data)
{
	struct astobj2 *p;

	if (!user_data) {
		return 0;
	}

	p = INTERNAL_OBJ(user_data);

	return !p || IS_AO2_MAGIC_BAD(p) ? 0 : 1;
}

void log_bad_ao2(void *user_data, const char *file, int line, const char *func)
{
	struct astobj2 *p;
	char bad_magic[100];

	if (!user_data) {
		__ast_assert_failed(0, "user_data is NULL", file, line, func);
		return;
	}

	p = INTERNAL_OBJ(user_data);
	snprintf(bad_magic, sizeof(bad_magic), "bad magic number 0x%x for object %p",
		p->priv_data.magic, user_data);
	__ast_assert_failed(0, bad_magic, file, line, func);
}

int __ao2_lock(void *user_data, enum ao2_lock_req lock_how, const char *file, const char *func, int line, const char *var)
{
	struct astobj2 *obj = __INTERNAL_OBJ_CHECK(user_data, file, line, func);
	struct astobj2_lock *obj_mutex;
	struct astobj2_rwlock *obj_rwlock;
	struct astobj2_lockobj *obj_lockobj;
	int res = 0;

	if (obj == NULL) {
		return -1;
	}

	if (ref_log) {
		obj->priv_data.lockused  = 1;
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
	case AO2_ALLOC_OPT_LOCK_OBJ:
		obj_lockobj = INTERNAL_OBJ_LOCKOBJ(user_data);
		res = __ao2_lock(obj_lockobj->lockobj.lock, lock_how, file, func, line, var);
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
	struct astobj2 *obj = __INTERNAL_OBJ_CHECK(user_data, file, line, func);
	struct astobj2_lock *obj_mutex;
	struct astobj2_rwlock *obj_rwlock;
	struct astobj2_lockobj *obj_lockobj;
	int res = 0;
	int current_value;

	if (obj == NULL) {
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
	case AO2_ALLOC_OPT_LOCK_OBJ:
		obj_lockobj = INTERNAL_OBJ_LOCKOBJ(user_data);
		res = __ao2_unlock(obj_lockobj->lockobj.lock, file, func, line, var);
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
	struct astobj2 *obj = __INTERNAL_OBJ_CHECK(user_data, file, line, func);
	struct astobj2_lock *obj_mutex;
	struct astobj2_rwlock *obj_rwlock;
	struct astobj2_lockobj *obj_lockobj;
	int res = 0;

	if (obj == NULL) {
		return -1;
	}

	if (ref_log) {
		obj->priv_data.lockused  = 1;
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
	case AO2_ALLOC_OPT_LOCK_OBJ:
		obj_lockobj = INTERNAL_OBJ_LOCKOBJ(user_data);
		res = __ao2_trylock(obj_lockobj->lockobj.lock, lock_how, file, func, line, var);
		break;
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
enum ao2_lock_req __adjust_lock(void *user_data, enum ao2_lock_req lock_how, int keep_stronger)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);
	struct astobj2_rwlock *obj_rwlock;
	struct astobj2_lockobj *obj_lockobj;
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
	case AO2_ALLOC_OPT_LOCK_OBJ:
		obj_lockobj = INTERNAL_OBJ_LOCKOBJ(user_data);
		orig_lock = __adjust_lock(obj_lockobj->lockobj.lock, lock_how, keep_stronger);
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
	struct astobj2 *obj;
	struct astobj2_lock *obj_mutex;

	obj = INTERNAL_OBJ_CHECK(user_data);

	if (obj == NULL) {
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

int __ao2_ref(void *user_data, int delta,
	const char *tag, const char *file, int line, const char *func)
{
	struct astobj2 *obj = __INTERNAL_OBJ_CHECK(user_data, file, line, func);
	struct astobj2_lock *obj_mutex;
	struct astobj2_rwlock *obj_rwlock;
	struct astobj2_lockobj *obj_lockobj;
	int32_t current_value;
	int32_t ret;
	struct ao2_weakproxy *weakproxy = NULL;
	const char *lock_state;

	if (obj == NULL) {
		if (ref_log && user_data) {
			fprintf(ref_log, "%p,%d,%d,%s,%d,%s,**invalid**,%s\n",
				user_data, delta, ast_get_tid(), file, line, func, tag ?: "");
			fflush(ref_log);
		}
		return -1;
	}

	/* if delta is 0, just return the refcount */
	if (delta == 0) {
		return obj->priv_data.ref_counter;
	}

	if (delta < 0 && obj->priv_data.magic == AO2_MAGIC && (weakproxy = obj->priv_data.weakptr)) {
		ao2_lock(weakproxy);
	}

	/* we modify with an atomic operation the reference counter */
	ret = ast_atomic_fetch_add(&obj->priv_data.ref_counter, delta, __ATOMIC_RELAXED);
	current_value = ret + delta;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_refs, delta);
#endif

	if (weakproxy) {
		struct ao2_weakproxy cbs;

		if (current_value == 1) {
			/* The only remaining reference is the one owned by the weak object */
			struct astobj2 *internal_weakproxy;

			internal_weakproxy = INTERNAL_OBJ_CHECK(weakproxy);

			/* Unlink the obj from the weak proxy */
			internal_weakproxy->priv_data.weakptr = NULL;
			obj->priv_data.weakptr = NULL;

			/* transfer list to local copy so callbacks are run with weakproxy unlocked. */
			cbs.destroyed_cb = weakproxy->destroyed_cb;
			AST_LIST_HEAD_INIT_NOLOCK(&weakproxy->destroyed_cb);

			/* weak is already unlinked from obj so this won't recurse */
			ao2_ref(user_data, -1);
		}

		ao2_unlock(weakproxy);

		if (current_value == 1) {
			struct ao2_weakproxy_notification *destroyed_cb;

			/* Notify the subscribers that weakproxy now points to NULL. */
			while ((destroyed_cb = AST_LIST_REMOVE_HEAD(&cbs.destroyed_cb, list))) {
				destroyed_cb->cb(weakproxy, destroyed_cb->data);
				ast_free(destroyed_cb);
			}

			ao2_ref(weakproxy, -1);
		}
	}

	if (0 < current_value) {
		/* The object still lives. */
#define EXCESSIVE_REF_COUNT		100000

		if (EXCESSIVE_REF_COUNT <= current_value && ret < EXCESSIVE_REF_COUNT) {
			char excessive_ref_buf[100];

			/* We just reached or went over the excessive ref count trigger */
			snprintf(excessive_ref_buf, sizeof(excessive_ref_buf),
				"Excessive refcount %d reached on ao2 object %p",
				(int)current_value, user_data);
			ast_log(__LOG_ERROR, file, line, func, "%s\n", excessive_ref_buf);

			__ast_assert_failed(0, excessive_ref_buf, file, line, func);
		}

		if (ref_log && tag) {
			fprintf(ref_log, "%p,%s%d,%d,%s,%d,%s,%d,%s\n", user_data,
				(delta < 0 ? "" : "+"), delta, ast_get_tid(),
				file, line, func, (int)ret, tag);
			fflush(ref_log);
		}
		return ret;
	}

	/* this case must never happen */
	if (current_value < 0) {
		ast_log(__LOG_ERROR, file, line, func,
			"Invalid refcount %d on ao2 object %p\n", (int)current_value, user_data);
		if (ref_log) {
			/* Log to ref_log invalid even if (tag == NULL) */
			fprintf(ref_log, "%p,%d,%d,%s,%d,%s,**invalid**,%s\n",
				user_data, delta, ast_get_tid(), file, line, func, tag ?: "");
			fflush(ref_log);
		}
		ast_assert(0);
		/* stop here even if assert doesn't DO_CRASH */
		return -1;
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
		lock_state = obj->priv_data.lockused ? "used" : "unused";
		ast_mutex_destroy(&obj_mutex->mutex.lock);

		ast_free(obj_mutex);
		break;
	case AO2_ALLOC_OPT_LOCK_RWLOCK:
		obj_rwlock = INTERNAL_OBJ_RWLOCK(user_data);
		lock_state = obj->priv_data.lockused ? "used" : "unused";
		ast_rwlock_destroy(&obj_rwlock->rwlock.lock);

		ast_free(obj_rwlock);
		break;
	case AO2_ALLOC_OPT_LOCK_NOLOCK:
		lock_state = "none";
		ast_free(obj);
		break;
	case AO2_ALLOC_OPT_LOCK_OBJ:
		obj_lockobj = INTERNAL_OBJ_LOCKOBJ(user_data);
		lock_state = obj->priv_data.lockused ? "used" : "unused";
		ao2_t_ref(obj_lockobj->lockobj.lock, -1, "release lockobj");

		ast_free(obj_lockobj);
		break;
	default:
		ast_log(__LOG_ERROR, file, line, func,
			"Invalid lock option on ao2 object %p\n", user_data);
		lock_state = "invalid";
		break;
	}

	if (ref_log && tag) {
		fprintf(ref_log, "%p,%d,%d,%s,%d,%s,**destructor**lock-state:%s**,%s\n",
			user_data, delta, ast_get_tid(), file, line, func, lock_state, tag);
		fflush(ref_log);
	}

	return ret;
}

void __ao2_cleanup_debug(void *obj, const char *tag, const char *file, int line, const char *function)
{
	if (obj) {
		__ao2_ref(obj, -1, tag, file, line, function);
	}
}

void __ao2_cleanup(void *obj)
{
	if (obj) {
		ao2_ref(obj, -1);
	}
}

static void *internal_ao2_alloc(size_t data_size, ao2_destructor_fn destructor_fn, unsigned int options,
	void *lockobj, const char *tag, const char *file, int line, const char *func)
{
	/* allocation */
	struct astobj2 *obj;
	struct astobj2_lock *obj_mutex;
	struct astobj2_rwlock *obj_rwlock;
	struct astobj2_lockobj *obj_lockobj;
	size_t overhead;

	switch (options & AO2_ALLOC_OPT_LOCK_MASK) {
	case AO2_ALLOC_OPT_LOCK_MUTEX:
		overhead = sizeof(*obj_mutex);
		obj_mutex = __ast_calloc(1, overhead + data_size, file, line, func);
		if (obj_mutex == NULL) {
			return NULL;
		}

		ast_mutex_init(&obj_mutex->mutex.lock);
		obj = (struct astobj2 *) &obj_mutex->priv_data;
		break;
	case AO2_ALLOC_OPT_LOCK_RWLOCK:
		overhead = sizeof(*obj_rwlock);
		obj_rwlock = __ast_calloc(1, overhead + data_size, file, line, func);
		if (obj_rwlock == NULL) {
			return NULL;
		}

		ast_rwlock_init(&obj_rwlock->rwlock.lock);
		obj = (struct astobj2 *) &obj_rwlock->priv_data;
		break;
	case AO2_ALLOC_OPT_LOCK_NOLOCK:
		overhead = sizeof(*obj);
		obj = __ast_calloc(1, overhead + data_size, file, line, func);
		if (obj == NULL) {
			return NULL;
		}
		break;
	case AO2_ALLOC_OPT_LOCK_OBJ:
		lockobj = ao2_t_bump(lockobj, "set lockobj");
		if (!lockobj) {
			ast_log(__LOG_ERROR, file, line, func, "AO2_ALLOC_OPT_LOCK_OBJ requires a non-NULL lockobj.\n");
			return NULL;
		}

		overhead = sizeof(*obj_lockobj);
		obj_lockobj = __ast_calloc(1, overhead + data_size, file, line, func);
		if (obj_lockobj == NULL) {
			ao2_t_ref(lockobj, -1, "release lockobj for failed alloc");
			return NULL;
		}

		obj_lockobj->lockobj.lock = lockobj;
		obj = (struct astobj2 *) &obj_lockobj->priv_data;
		break;
	default:
		/* Invalid option value. */
		ast_log(__LOG_DEBUG, file, line, func, "Invalid lock option requested\n");
		return NULL;
	}

	/* Initialize common ao2 values. */
	obj->priv_data.destructor_fn = destructor_fn;	/* can be NULL */
	obj->priv_data.ref_counter = 1;
	obj->priv_data.options = options;
	obj->priv_data.magic = AO2_MAGIC;

#ifdef AO2_DEBUG
	obj->priv_data.data_size = data_size;
	ast_atomic_fetchadd_int(&ao2.total_objects, 1);
	ast_atomic_fetchadd_int(&ao2.total_mem, data_size);
	ast_atomic_fetchadd_int(&ao2.total_refs, 1);
#endif

	if (ref_log && tag) {
		fprintf(ref_log, "%p,+1,%d,%s,%d,%s,**constructor**%zu**%zu**,%s\n",
			EXTERNAL_OBJ(obj), ast_get_tid(), file, line, func, overhead, data_size, tag);
		fflush(ref_log);
	}

	/* return a pointer to the user data */
	return EXTERNAL_OBJ(obj);
}

void *__ao2_alloc(size_t data_size, ao2_destructor_fn destructor_fn, unsigned int options,
	const char *tag, const char *file, int line, const char *func)
{
	return internal_ao2_alloc(data_size, destructor_fn, options, NULL, tag, file, line, func);
}

void *__ao2_alloc_with_lockobj(size_t data_size, ao2_destructor_fn destructor_fn, void *lockobj,
	const char *tag, const char *file, int line, const char *func)
{
	return internal_ao2_alloc(data_size, destructor_fn, AO2_ALLOC_OPT_LOCK_OBJ, lockobj,
		tag, file, line, func);
}

unsigned int ao2_options_get(void *obj)
{
	struct astobj2 *orig_obj;

	orig_obj = INTERNAL_OBJ_CHECK(obj);
	if (!orig_obj) {
		return 0;
	}
	return orig_obj->priv_data.options;
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
		__ao2_ref(obj, +1, tag, file, line, func);
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
		__ao2_ref(obj_old, -1, tag, file, line, func);
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
		__ao2_ref(obj, +1, tag, file, line, func);
	}

	__ast_rwlock_unlock(file, line, func, &holder->lock, name);

	return obj;
}


void *__ao2_weakproxy_alloc(size_t data_size, ao2_destructor_fn destructor_fn,
	const char *tag, const char *file, int line, const char *func)
{
	struct ao2_weakproxy *weakproxy;

	if (data_size < sizeof(*weakproxy)) {
		ast_assert(0);
		ast_log(LOG_ERROR, "Requested data_size smaller than minimum.\n");
		return NULL;
	}

	weakproxy = __ao2_alloc(data_size, destructor_fn, AO2_ALLOC_OPT_LOCK_MUTEX,
		tag, file, line, func);

	if (weakproxy) {
		struct astobj2 *weakproxy_internal;

		/* Just created weakproxy, no need to check if it's valid. */
		weakproxy_internal = INTERNAL_OBJ(weakproxy);
		weakproxy_internal->priv_data.magic = AO2_WEAK;
	}

	return weakproxy;
}

int __ao2_weakproxy_set_object(void *weakproxy, void *obj, int flags,
	const char *tag, const char *file, int line, const char *func)
{
	struct astobj2 *weakproxy_internal = __INTERNAL_OBJ_CHECK(weakproxy, file, line, func);
	struct astobj2 *obj_internal = __INTERNAL_OBJ_CHECK(obj, file, line, func);
	int ret = -1;

	if (!weakproxy_internal
		|| weakproxy_internal->priv_data.magic != AO2_WEAK) {
		return -1;
	}

	if (!obj_internal
		|| obj_internal->priv_data.weakptr
		|| obj_internal->priv_data.magic != AO2_MAGIC) {
		return -1;
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_lock(weakproxy);
	}

	if (!weakproxy_internal->priv_data.weakptr) {
		__ao2_ref(obj, +1, tag, file, line, func);
		__ao2_ref(weakproxy, +1, tag, file, line, func);

		weakproxy_internal->priv_data.weakptr = obj;
		obj_internal->priv_data.weakptr = weakproxy;

		ret = 0;
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(weakproxy);
		/* It is possible for obj to be accessed now.  It's allowed
		 * for weakproxy to already be in a container.  Another thread
		 * could have been waiting for a lock on weakproxy to retrieve
		 * the object.
		 */
	}

	return ret;
}

int __ao2_weakproxy_ref_object(void *weakproxy, int delta, int flags,
	const char *tag, const char *file, int line, const char *func)
{
	struct astobj2 *internal = __INTERNAL_OBJ_CHECK(weakproxy, file, line, func);
	int ret = -1;

	if (!internal || internal->priv_data.magic != AO2_WEAK) {
		/* This method is meant to be run on weakproxy objects! */
		return -2;
	}

	/* We have a weak object, grab lock. */
	if (!(flags & OBJ_NOLOCK)) {
		ao2_lock(weakproxy);
	}

	if (internal->priv_data.weakptr) {
		ret = __ao2_ref(internal->priv_data.weakptr, delta, tag, file, line, func);
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(weakproxy);
	}

	return ret;
}

void *__ao2_weakproxy_get_object(void *weakproxy, int flags,
	const char *tag, const char *file, int line, const char *func)
{
	struct astobj2 *internal = __INTERNAL_OBJ_CHECK(weakproxy, file, line, func);
	void *obj;

	if (!internal || internal->priv_data.magic != AO2_WEAK) {
		/* This method is meant to be run on weakproxy objects! */
		return NULL;
	}

	/* We have a weak object, grab reference to object within lock */
	if (!(flags & OBJ_NOLOCK)) {
		ao2_lock(weakproxy);
	}

	obj = internal->priv_data.weakptr;
	if (obj) {
		__ao2_ref(obj, +1, tag, file, line, func);
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(weakproxy);
	}

	return obj;
}

void *__ao2_get_weakproxy(void *obj, const char *tag, const char *file, int line, const char *func)
{
	struct astobj2 *obj_internal = __INTERNAL_OBJ_CHECK(obj, file, line, func);

	if (!obj_internal || obj_internal->priv_data.magic != AO2_MAGIC) {
		/* This method is meant to be run on normal ao2 objects! */
		return NULL;
	}

	if (!obj_internal->priv_data.weakptr) {
		return NULL;
	}

	__ao2_ref(obj_internal->priv_data.weakptr, +1, tag, file, line, func);
	return obj_internal->priv_data.weakptr;
}

int ao2_weakproxy_subscribe(void *weakproxy, ao2_weakproxy_notification_cb cb, void *data, int flags)
{
	struct astobj2 *weakproxy_internal = INTERNAL_OBJ_CHECK(weakproxy);
	int ret = -1;
	int hasobj;

	if (!weakproxy_internal || weakproxy_internal->priv_data.magic != AO2_WEAK) {
		return -1;
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_lock(weakproxy);
	}

	hasobj = weakproxy_internal->priv_data.weakptr != NULL;
	if (hasobj) {
		struct ao2_weakproxy *weak = weakproxy;
		struct ao2_weakproxy_notification *sub = ast_calloc(1, sizeof(*sub));

		if (sub) {
			sub->cb = cb;
			sub->data = data;
			AST_LIST_INSERT_HEAD(&weak->destroyed_cb, sub, list);
			ret = 0;
		}
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(weakproxy);
	}

	if (!hasobj) {
		cb(weakproxy, data);
		ret = 0;
	}

	return ret;
}

int ao2_weakproxy_unsubscribe(void *weakproxy, ao2_weakproxy_notification_cb destroyed_cb, void *data, int flags)
{
	struct astobj2 *internal_weakproxy = INTERNAL_OBJ_CHECK(weakproxy);
	struct ao2_weakproxy *weak;
	struct ao2_weakproxy_notification *sub;
	int ret = 0;

	if (!internal_weakproxy || internal_weakproxy->priv_data.magic != AO2_WEAK || !destroyed_cb) {
		return -1;
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_lock(weakproxy);
	}

	weak = weakproxy;
	AST_LIST_TRAVERSE_SAFE_BEGIN(&weak->destroyed_cb, sub, list) {
		if (sub->cb == destroyed_cb && sub->data == data) {
			AST_LIST_REMOVE_CURRENT(list);
			ast_free(sub);
			ret++;
			if (!(flags & OBJ_MULTIPLE)) {
				break;
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(weakproxy);
	}

	return ret;
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

#if defined(AO2_DEBUG)
static struct ast_cli_entry cli_astobj2[] = {
	AST_CLI_DEFINE(handle_astobj2_stats, "Print astobj2 statistics"),
	AST_CLI_DEFINE(handle_astobj2_test, "Test astobj2"),
};
#endif /* AO2_DEBUG */

static void astobj2_cleanup(void)
{
#if defined(AO2_DEBUG)
	ast_cli_unregister_multiple(cli_astobj2, ARRAY_LEN(cli_astobj2));
#endif

	if (ast_opt_ref_debug) {
		fclose(ref_log);
		ref_log = NULL;
	}
}

int astobj2_init(void)
{
	char ref_filename[1024];

	if (ast_opt_ref_debug) {
		snprintf(ref_filename, sizeof(ref_filename), "%s/refs", ast_config_AST_LOG_DIR);
		ref_log = fopen(ref_filename, "w");
		if (!ref_log) {
			ast_log(LOG_ERROR, "Could not open ref debug log file: %s\n", ref_filename);
		}
	}

	ast_register_cleanup(astobj2_cleanup);

	if (container_init() != 0) {
		fclose(ref_log);
		ref_log = NULL;
		return -1;
	}

#if defined(AO2_DEBUG)
	ast_cli_register_multiple(cli_astobj2, ARRAY_LEN(cli_astobj2));
#endif	/* defined(AO2_DEBUG) */

	return 0;
}
