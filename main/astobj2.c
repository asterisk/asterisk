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

/*
 * Function implementing astobj2 objects.
 */
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#define REF_FILE "/tmp/refs"

/*!
 * astobj2 objects are always preceded by this data structure,
 * which contains a lock, a reference counter,
 * the flags and a pointer to a destructor.
 * The refcount is used to decide when it is time to
 * invoke the destructor.
 * The magic number is used for consistency check.
 * XXX the lock is not always needed, and its initialization may be
 * expensive. Consider making it external.
 */
struct __priv_data {
	ast_mutex_t lock;
	int ref_counter;
	ao2_destructor_fn destructor_fn;
	/*! for stats */
	size_t data_size;
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

#ifdef AST_DEVMODE
/* #define AO2_DEBUG 1 */
#endif

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

#ifndef HAVE_BKTR	/* backtrace support */
void ao2_bt(void) {}
#else
#include <execinfo.h>    /* for backtrace */

void ao2_bt(void)
{
	int c, i;
#define N1	20
	void *addresses[N1];
	char **strings;

	c = backtrace(addresses, N1);
	strings = ast_bt_get_symbols(addresses,c);
	ast_verbose("backtrace returned: %d\n", c);
	for(i = 0; i < c; i++) {
		ast_verbose("%d: %p %s\n", i, addresses[i], strings[i]);
	}
	free(strings);
}
#endif

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
	if (AO2_MAGIC != (p->priv_data.magic) ) {
		ast_log(LOG_ERROR, "bad magic number 0x%x for %p\n", p->priv_data.magic, p);
		p = NULL;
	}

	return p;
}

enum ao2_callback_type {
	DEFAULT,
	WITH_DATA,
};

/*!
 * \brief convert from a pointer _p to an astobj2 object
 *
 * \return the pointer to the user-defined portion.
 */
#define EXTERNAL_OBJ(_p)	((_p) == NULL ? NULL : (_p)->user_data)

/* the underlying functions common to debug and non-debug versions */

static int internal_ao2_ref(void *user_data, const int delta);
static struct ao2_container *internal_ao2_container_alloc(struct ao2_container *c, const uint n_buckets, ao2_hash_fn *hash_fn,
							  ao2_callback_fn *cmp_fn);
static struct bucket_entry *internal_ao2_link(struct ao2_container *c, void *user_data, int flags, const char *file, int line, const char *func);
static void *internal_ao2_callback(struct ao2_container *c,
				   const enum search_flags flags, void *cb_fn, void *arg, void *data, enum ao2_callback_type type,
				   char *tag, char *file, int line, const char *funcname);
static void *internal_ao2_iterator_next(struct ao2_iterator *a, struct bucket_entry **q);

int __ao2_lock(void *user_data, const char *file, const char *func, int line, const char *var)
{
	struct astobj2 *p = INTERNAL_OBJ(user_data);

	if (p == NULL)
		return -1;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_locked, 1);
#endif

	return __ast_pthread_mutex_lock(file, line, func, var, &p->priv_data.lock);
}

int __ao2_unlock(void *user_data, const char *file, const char *func, int line, const char *var)
{
	struct astobj2 *p = INTERNAL_OBJ(user_data);

	if (p == NULL)
		return -1;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_locked, -1);
#endif

	return __ast_pthread_mutex_unlock(file, line, func, var, &p->priv_data.lock);
}

int __ao2_trylock(void *user_data, const char *file, const char *func, int line, const char *var)
{
	struct astobj2 *p = INTERNAL_OBJ(user_data);
	int ret;
	
	if (p == NULL)
		return -1;
	ret = __ast_pthread_mutex_trylock(file, line, func, var, &p->priv_data.lock);

#ifdef AO2_DEBUG
	if (!ret)
		ast_atomic_fetchadd_int(&ao2.total_locked, 1);
#endif
	return ret;
}

void *ao2_object_get_lockaddr(void *obj)
{
	struct astobj2 *p = INTERNAL_OBJ(obj);
	
	if (p == NULL)
		return NULL;

	return &p->priv_data.lock;
}

/*
 * The argument is a pointer to the user portion.
 */


int __ao2_ref_debug(void *user_data, const int delta, char *tag, char *file, int line, const char *funcname)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);
	
	if (obj == NULL)
		return -1;

	if (delta != 0) {
		FILE *refo = fopen(REF_FILE,"a");
		fprintf(refo, "%p %s%d   %s:%d:%s (%s) [@%d]\n", user_data, (delta<0? "":"+"), delta, file, line, funcname, tag, obj ? obj->priv_data.ref_counter : -1);
		fclose(refo);
	}
	if (obj->priv_data.ref_counter + delta == 0 && obj->priv_data.destructor_fn != NULL) { /* this isn't protected with lock; just for o/p */
			FILE *refo = fopen(REF_FILE,"a"); 	 
			fprintf(refo, "%p **call destructor** %s:%d:%s (%s)\n", user_data, file, line, funcname, tag); 	 
			fclose(refo);
	}
	return internal_ao2_ref(user_data, delta);
}

int __ao2_ref(void *user_data, const int delta)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);

	if (obj == NULL)
		return -1;

	return internal_ao2_ref(user_data, delta);
}

static int internal_ao2_ref(void *user_data, const int delta)
{
	struct astobj2 *obj = INTERNAL_OBJ(user_data);
	int current_value;
	int ret;

	if (obj == NULL)
		return -1;

	/* if delta is 0, just return the refcount */
	if (delta == 0)
		return (obj->priv_data.ref_counter);

	/* we modify with an atomic operation the reference counter */
	ret = ast_atomic_fetchadd_int(&obj->priv_data.ref_counter, delta);
	current_value = ret + delta;

#ifdef AO2_DEBUG	
	ast_atomic_fetchadd_int(&ao2.total_refs, delta);
#endif

	/* this case must never happen */
	if (current_value < 0)
		ast_log(LOG_ERROR, "refcount %d on object %p\n", current_value, user_data);

	if (current_value <= 0) { /* last reference, destroy the object */
		if (obj->priv_data.destructor_fn != NULL) {
			obj->priv_data.destructor_fn(user_data);
		}

		ast_mutex_destroy(&obj->priv_data.lock);
#ifdef AO2_DEBUG
		ast_atomic_fetchadd_int(&ao2.total_mem, - obj->priv_data.data_size);
		ast_atomic_fetchadd_int(&ao2.total_objects, -1);
#endif
		/* for safety, zero-out the astobj2 header and also the
		 * first word of the user-data, which we make sure is always
		 * allocated. */
		memset(obj, '\0', sizeof(struct astobj2 *) + sizeof(void *) );
		ast_free(obj);
	}

	return ret;
}

/*
 * We always alloc at least the size of a void *,
 * for debugging purposes.
 */
static void *internal_ao2_alloc(size_t data_size, ao2_destructor_fn destructor_fn, const char *file, int line, const char *funcname)
{
	/* allocation */
	struct astobj2 *obj;

	if (data_size < sizeof(void *))
		data_size = sizeof(void *);

#if defined(__AST_DEBUG_MALLOC)
	obj = __ast_calloc(1, sizeof(*obj) + data_size, file, line, funcname);
#else
	obj = ast_calloc(1, sizeof(*obj) + data_size);
#endif

	if (obj == NULL)
		return NULL;

	ast_mutex_init(&obj->priv_data.lock);
	obj->priv_data.magic = AO2_MAGIC;
	obj->priv_data.data_size = data_size;
	obj->priv_data.ref_counter = 1;
	obj->priv_data.destructor_fn = destructor_fn;	/* can be NULL */

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_objects, 1);
	ast_atomic_fetchadd_int(&ao2.total_mem, data_size);
	ast_atomic_fetchadd_int(&ao2.total_refs, 1);
#endif

	/* return a pointer to the user data */
	return EXTERNAL_OBJ(obj);
}

void *__ao2_alloc_debug(size_t data_size, ao2_destructor_fn destructor_fn, char *tag,
			const char *file, int line, const char *funcname, int ref_debug)
{
	/* allocation */
	void *obj;
	FILE *refo = ref_debug ? fopen(REF_FILE,"a") : NULL;

	if ((obj = internal_ao2_alloc(data_size, destructor_fn, file, line, funcname)) == NULL) {
		fclose(refo);
		return NULL;
	}

	if (refo) {
		fprintf(refo, "%p =1   %s:%d:%s (%s)\n", obj, file, line, funcname, tag);
		fclose(refo);
	}

	/* return a pointer to the user data */
	return obj;
}

void *__ao2_alloc(size_t data_size, ao2_destructor_fn destructor_fn)
{
	return internal_ao2_alloc(data_size, destructor_fn, __FILE__, __LINE__, __FUNCTION__);
}


/* internal callback to destroy a container. */
static void container_destruct(void *c);

/* internal callback to destroy a container. */
static void container_destruct_debug(void *c);

/* each bucket in the container is a tailq. */
AST_LIST_HEAD_NOLOCK(bucket, bucket_entry);

/*!
 * A container; stores the hash and callback functions, information on
 * the size, the hash bucket heads, and a version number, starting at 0
 * (for a newly created, empty container)
 * and incremented every time an object is inserted or deleted.
 * The assumption is that an object is never moved in a container,
 * but removed and readded with the new number.
 * The version number is especially useful when implementing iterators.
 * In fact, we can associate a unique, monotonically increasing number to
 * each object, which means that, within an iterator, we can store the
 * version number of the current object, and easily look for the next one,
 * which is the next one in the list with a higher number.
 * Since all objects have a version >0, we can use 0 as a marker for
 * 'we need the first object in the bucket'.
 *
 * \todo Linking and unlink objects is typically expensive, as it
 * involves a malloc() of a small object which is very inefficient.
 * To optimize this, we allocate larger arrays of bucket_entry's
 * when we run out of them, and then manage our own freelist.
 * This will be more efficient as we can do the freelist management while
 * we hold the lock (that we need anyways).
 */
struct ao2_container {
	ao2_hash_fn *hash_fn;
	ao2_callback_fn *cmp_fn;
	int n_buckets;
	/*! Number of elements in the container */
	int elements;
	/*! described above */
	int version;
	/*! variable size */
	struct bucket buckets[0];
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

/*
 * A container is just an object, after all!
 */
static struct ao2_container *internal_ao2_container_alloc(struct ao2_container *c, const unsigned int n_buckets, ao2_hash_fn *hash_fn,
							  ao2_callback_fn *cmp_fn)
{
	/* XXX maybe consistency check on arguments ? */
	/* compute the container size */

	if (!c)
		return NULL;
	
	c->version = 1;	/* 0 is a reserved value here */
	c->n_buckets = hash_fn ? n_buckets : 1;
	c->hash_fn = hash_fn ? hash_fn : hash_zero;
	c->cmp_fn = cmp_fn;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_containers, 1);
#endif

	return c;
}

struct ao2_container *__ao2_container_alloc_debug(const unsigned int n_buckets, ao2_hash_fn *hash_fn,
						  ao2_callback_fn *cmp_fn, char *tag, char *file, int line,
						  const char *funcname, int ref_debug)
{
	/* XXX maybe consistency check on arguments ? */
	/* compute the container size */
	const unsigned int num_buckets = hash_fn ? n_buckets : 1;
	size_t container_size = sizeof(struct ao2_container) + num_buckets * sizeof(struct bucket);
	struct ao2_container *c = __ao2_alloc_debug(container_size, container_destruct_debug, tag, file, line, funcname, ref_debug);

	return internal_ao2_container_alloc(c, num_buckets, hash_fn, cmp_fn);
}

struct ao2_container *__ao2_container_alloc(const unsigned int n_buckets, ao2_hash_fn *hash_fn,
					    ao2_callback_fn *cmp_fn)
{
	/* XXX maybe consistency check on arguments ? */
	/* compute the container size */

	const unsigned int num_buckets = hash_fn ? n_buckets : 1;
	size_t container_size = sizeof(struct ao2_container) + num_buckets * sizeof(struct bucket);
	struct ao2_container *c = __ao2_alloc(container_size, container_destruct);

	return internal_ao2_container_alloc(c, num_buckets, hash_fn, cmp_fn);
}

/*!
 * return the number of elements in the container
 */
int ao2_container_count(struct ao2_container *c)
{
	return c->elements;
}

/*!
 * A structure to create a linked list of entries,
 * used within a bucket.
 * XXX \todo this should be private to the container code
 */
struct bucket_entry {
	AST_LIST_ENTRY(bucket_entry) entry;
	int version;
	struct astobj2 *astobj;		/* pointer to internal data */
}; 

/*
 * link an object to a container
 */

static struct bucket_entry *internal_ao2_link(struct ao2_container *c, void *user_data, int flags, const char *file, int line, const char *func)
{
	int i;
	/* create a new list entry */
	struct bucket_entry *p;
	struct astobj2 *obj = INTERNAL_OBJ(user_data);

	if (obj == NULL)
		return NULL;

	if (INTERNAL_OBJ(c) == NULL)
		return NULL;

	p = ast_calloc(1, sizeof(*p));
	if (!p)
		return NULL;

	i = abs(c->hash_fn(user_data, OBJ_POINTER));

	if (!(flags & OBJ_NOLOCK)) {
		ao2_lock(c);
	}
	i %= c->n_buckets;
	p->astobj = obj;
	p->version = ast_atomic_fetchadd_int(&c->version, 1);
	AST_LIST_INSERT_TAIL(&c->buckets[i], p, entry);
	ast_atomic_fetchadd_int(&c->elements, 1);

	/* the last two operations (ao2_ref, ao2_unlock) must be done by the calling func */
	return p;
}

void *__ao2_link_debug(struct ao2_container *c, void *user_data, int flags, char *tag, char *file, int line, const char *funcname)
{
	struct bucket_entry *p = internal_ao2_link(c, user_data, flags, file, line, funcname);

	if (p) {
		__ao2_ref_debug(user_data, +1, tag, file, line, funcname);
		if (!(flags & OBJ_NOLOCK)) {
			ao2_unlock(c);
		}
	}
	return p;
}

void *__ao2_link(struct ao2_container *c, void *user_data, int flags)
{
	struct bucket_entry *p = internal_ao2_link(c, user_data, flags, __FILE__, __LINE__, __PRETTY_FUNCTION__);

	if (p) {
		__ao2_ref(user_data, +1);
		if (!(flags & OBJ_NOLOCK)) {
			ao2_unlock(c);
		}
	}
	return p;
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
void *__ao2_unlink_debug(struct ao2_container *c, void *user_data, int flags, char *tag,
			 char *file, int line, const char *funcname)
{
	if (INTERNAL_OBJ(user_data) == NULL)	/* safety check on the argument */
		return NULL;

	flags |= (OBJ_UNLINK | OBJ_POINTER | OBJ_NODATA);

	__ao2_callback_debug(c, flags, ao2_match_by_addr, user_data, tag, file, line, funcname);

	return NULL;
}

void *__ao2_unlink(struct ao2_container *c, void *user_data, int flags)
{
	if (INTERNAL_OBJ(user_data) == NULL)	/* safety check on the argument */
		return NULL;

	flags |= (OBJ_UNLINK | OBJ_POINTER | OBJ_NODATA);
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
 * Browse the container using different stategies accoding the flags.
 * \return Is a pointer to an object or to a list of object if OBJ_MULTIPLE is 
 * specified.
 * Luckily, for debug purposes, the added args (tag, file, line, funcname)
 * aren't an excessive load to the system, as the callback should not be
 * called as often as, say, the ao2_ref func is called.
 */
static void *internal_ao2_callback(struct ao2_container *c,
				   const enum search_flags flags, void *cb_fn, void *arg, void *data, enum ao2_callback_type type,
				   char *tag, char *file, int line, const char *funcname)
{
	int i, start, last;	/* search boundaries */
	void *ret = NULL;
	ao2_callback_fn *cb_default = NULL;
	ao2_callback_data_fn *cb_withdata = NULL;
	struct ao2_container *multi_container = NULL;
	struct ao2_iterator *multi_iterator = NULL;

	if (INTERNAL_OBJ(c) == NULL)	/* safety check on the argument */
		return NULL;

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
		if (!(multi_container = __ao2_container_alloc(1, NULL, NULL))) {
			return NULL;
		}
		if (!(multi_iterator = ast_calloc(1, sizeof(*multi_iterator)))) {
			ao2_ref(multi_container, -1);
			return NULL;
		}
	}

	/* override the match function if necessary */
	if (cb_fn == NULL) { /* if NULL, match everything */
		if (type == WITH_DATA) {
			cb_withdata = cb_true_data;
		} else {
			cb_default = cb_true;
		}
	} else {
		/* We do this here to avoid the per object casting penalty (even though
		   that is probably optimized away anyway). */
		if (type == WITH_DATA) {
			cb_withdata = cb_fn;
		} else {
			cb_default = cb_fn;
		}
	}

	/*
	 * XXX this can be optimized.
	 * If we have a hash function and lookup by pointer,
	 * run the hash function. Otherwise, scan the whole container
	 * (this only for the time being. We need to optimize this.)
	 */
	if ((flags & OBJ_POINTER))	/* we know hash can handle this case */
		start = i = c->hash_fn(arg, flags & OBJ_POINTER) % c->n_buckets;
	else			/* don't know, let's scan all buckets */
		start = i = -1;		/* XXX this must be fixed later. */

	/* determine the search boundaries: i..last-1 */
	if (i < 0) {
		start = i = 0;
		last = c->n_buckets;
	} else if ((flags & OBJ_CONTINUE)) {
		last = c->n_buckets;
	} else {
		last = i + 1;
	}


	if (!(flags & OBJ_NOLOCK)) {
		ao2_lock(c);	/* avoid modifications to the content */
	}

	for (; i < last ; i++) {
		/* scan the list with prev-cur pointers */
		struct bucket_entry *cur;

		AST_LIST_TRAVERSE_SAFE_BEGIN(&c->buckets[i], cur, entry) {
			int match = (CMP_MATCH | CMP_STOP);

			if (type == WITH_DATA) {
				match &= cb_withdata(EXTERNAL_OBJ(cur->astobj), arg, data, flags);
			} else {
				match &= cb_default(EXTERNAL_OBJ(cur->astobj), arg, flags);
			}

			/* we found the object, performing operations according flags */
			if (match == 0) {	/* no match, no stop, continue */
				continue;
			} else if (match == CMP_STOP) {	/* no match but stop, we are done */
				i = last;
				break;
			}

			/* we have a match (CMP_MATCH) here */
			if (!(flags & OBJ_NODATA)) {	/* if must return the object, record the value */
				/* it is important to handle this case before the unlink */
				ret = EXTERNAL_OBJ(cur->astobj);
				if (!(flags & (OBJ_UNLINK | OBJ_MULTIPLE))) {
					if (tag)
						__ao2_ref_debug(ret, 1, tag, file, line, funcname);
					else
						__ao2_ref(ret, 1);
				}
			}

			/* If we are in OBJ_MULTIPLE mode and OBJ_NODATE is off, 
			 * link the object into the container that will hold the results.
			 */
			if (ret && (multi_container != NULL)) {
				__ao2_link(multi_container, ret, flags);
				ret = NULL;
			}

			if (flags & OBJ_UNLINK) {	/* must unlink */
				/* we are going to modify the container, so update version */
				ast_atomic_fetchadd_int(&c->version, 1);
				AST_LIST_REMOVE_CURRENT(entry);
				/* update number of elements */
				ast_atomic_fetchadd_int(&c->elements, -1);

				/* - When unlinking and not returning the result, (OBJ_NODATA), the ref from the container
				 * must be decremented.
				 * - When unlinking with OBJ_MULTIPLE the ref from the original container
				 * must be decremented regardless if OBJ_NODATA is used. This is because the result is
				 * returned in a new container that already holds its own ref for the object. If the ref
				 * from the original container is not accounted for here a memory leak occurs. */
				if (flags & (OBJ_NODATA | OBJ_MULTIPLE)) {
					if (tag)
						__ao2_ref_debug(EXTERNAL_OBJ(cur->astobj), -1, tag, file, line, funcname);
					else
						__ao2_ref(EXTERNAL_OBJ(cur->astobj), -1);
				}
				ast_free(cur);	/* free the link record */
			}

			if ((match & CMP_STOP) || !(flags & OBJ_MULTIPLE)) {
				/* We found our only (or last) match, so force an exit from
				   the outside loop. */
				i = last;
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;

		if (ret) {
			break;
		}

		if (i == c->n_buckets - 1 && (flags & OBJ_POINTER) && (flags & OBJ_CONTINUE)) {
			/* Move to the beginning to ensure we check every bucket */
			i = -1;
			last = start;
		}
	}

	if (!(flags & OBJ_NOLOCK)) {
		ao2_unlock(c);
	}

	/* if multi_container was created, we are returning multiple objects */
	if (multi_container != NULL) {
		*multi_iterator = ao2_iterator_init(multi_container,
						    AO2_ITERATOR_DONTLOCK | AO2_ITERATOR_UNLINK | AO2_ITERATOR_MALLOCD);
		ao2_ref(multi_container, -1);
		return multi_iterator;
	} else {
		return ret;
	}
}

void *__ao2_callback_debug(struct ao2_container *c,
			   const enum search_flags flags,
			   ao2_callback_fn *cb_fn, void *arg,
			   char *tag, char *file, int line, const char *funcname)
{
	return internal_ao2_callback(c,flags, cb_fn, arg, NULL, DEFAULT, tag, file, line, funcname);
}

void *__ao2_callback(struct ao2_container *c, const enum search_flags flags,
		     ao2_callback_fn *cb_fn, void *arg)
{
	return internal_ao2_callback(c,flags, cb_fn, arg, NULL, DEFAULT, NULL, NULL, 0, NULL);
}

void *__ao2_callback_data_debug(struct ao2_container *c,
				const enum search_flags flags,
				ao2_callback_data_fn *cb_fn, void *arg, void *data,
				char *tag, char *file, int line, const char *funcname)
{
	return internal_ao2_callback(c, flags, cb_fn, arg, data, WITH_DATA, tag, file, line, funcname);
}

void *__ao2_callback_data(struct ao2_container *c, const enum search_flags flags,
			  ao2_callback_data_fn *cb_fn, void *arg, void *data)
{
	return internal_ao2_callback(c, flags, cb_fn, arg, data, WITH_DATA, NULL, NULL, 0, NULL);
}

/*!
 * the find function just invokes the default callback with some reasonable flags.
 */
void *__ao2_find_debug(struct ao2_container *c, void *arg, enum search_flags flags, char *tag, char *file, int line, const char *funcname)
{
	return __ao2_callback_debug(c, flags, c->cmp_fn, arg, tag, file, line, funcname);
}

void *__ao2_find(struct ao2_container *c, void *arg, enum search_flags flags)
{
	return __ao2_callback(c, flags, c->cmp_fn, arg);
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

	ao2_ref(c, +1);
	
	return a;
}

/*!
 * destroy an iterator
 */
void ao2_iterator_destroy(struct ao2_iterator *i)
{
	ao2_ref(i->c, -1);
	if (i->flags & AO2_ITERATOR_MALLOCD) {
		ast_free(i);
	} else {
		i->c = NULL;
	}
}

/*
 * move to the next element in the container.
 */
static void *internal_ao2_iterator_next(struct ao2_iterator *a, struct bucket_entry **q)
{
	int lim;
	struct bucket_entry *p = NULL;
	void *ret = NULL;

	*q = NULL;
	
	if (INTERNAL_OBJ(a->c) == NULL)
		return NULL;

	if (!(a->flags & AO2_ITERATOR_DONTLOCK))
		ao2_lock(a->c);

	/* optimization. If the container is unchanged and
	 * we have a pointer, try follow it
	 */
	if (a->c->version == a->c_version && (p = a->obj)) {
		if ((p = AST_LIST_NEXT(p, entry)))
			goto found;
		/* nope, start from the next bucket */
		a->bucket++;
		a->version = 0;
		a->obj = NULL;
	}

	lim = a->c->n_buckets;

	/* Browse the buckets array, moving to the next
	 * buckets if we don't find the entry in the current one.
	 * Stop when we find an element with version number greater
	 * than the current one (we reset the version to 0 when we
	 * switch buckets).
	 */
	for (; a->bucket < lim; a->bucket++, a->version = 0) {
		/* scan the current bucket */
		AST_LIST_TRAVERSE(&a->c->buckets[a->bucket], p, entry) {
			if (p->version > a->version)
				goto found;
		}
	}

found:
	if (p) {
		ret = EXTERNAL_OBJ(p->astobj);
		if (a->flags & AO2_ITERATOR_UNLINK) {
			/* we are going to modify the container, so update version */
			ast_atomic_fetchadd_int(&a->c->version, 1);
			AST_LIST_REMOVE(&a->c->buckets[a->bucket], p, entry);
			/* update number of elements */
			ast_atomic_fetchadd_int(&a->c->elements, -1);
			a->version = 0;
			a->obj = NULL;
			a->c_version = a->c->version;
			ast_free(p);
		} else {
			a->version = p->version;
			a->obj = p;
			a->c_version = a->c->version;
			/* inc refcount of returned object */
			*q = p;
		}
	}

	return ret;
}

void *__ao2_iterator_next_debug(struct ao2_iterator *a, char *tag, char *file, int line, const char *funcname)
{
	struct bucket_entry *p;
	void *ret = NULL;

	ret = internal_ao2_iterator_next(a, &p);
	
	if (p) {
		/* inc refcount of returned object */
		__ao2_ref_debug(ret, 1, tag, file, line, funcname);
	}

	if (!(a->flags & AO2_ITERATOR_DONTLOCK))
		ao2_unlock(a->c);

	return ret;
}

void *__ao2_iterator_next(struct ao2_iterator *a)
{
	struct bucket_entry *p = NULL;
	void *ret = NULL;

	ret = internal_ao2_iterator_next(a, &p);
	
	if (p) {
		/* inc refcount of returned object */
		__ao2_ref(ret, 1);
	}

	if (!(a->flags & AO2_ITERATOR_DONTLOCK))
		ao2_unlock(a->c);

	return ret;
}

/* callback for destroying container.
 * we can make it simple as we know what it does
 */
static int cd_cb(void *obj, void *arg, int flag)
{
	__ao2_ref(obj, -1);
	return 0;
}
	
static int cd_cb_debug(void *obj, void *arg, int flag)
{
	__ao2_ref_debug(obj, -1, "deref object via container destroy",  __FILE__, __LINE__, __PRETTY_FUNCTION__);
	return 0;
}
	
static void container_destruct(void *_c)
{
	struct ao2_container *c = _c;
	int i;

	__ao2_callback(c, OBJ_UNLINK, cd_cb, NULL);

	for (i = 0; i < c->n_buckets; i++) {
		struct bucket_entry *current;

		while ((current = AST_LIST_REMOVE_HEAD(&c->buckets[i], entry))) {
			ast_free(current);
		}
	}

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_containers, -1);
#endif
}

static void container_destruct_debug(void *_c)
{
	struct ao2_container *c = _c;
	int i;

	__ao2_callback_debug(c, OBJ_UNLINK, cd_cb_debug, NULL, "container_destruct_debug called", __FILE__, __LINE__, __PRETTY_FUNCTION__);

	for (i = 0; i < c->n_buckets; i++) {
		struct bucket_entry *current;

		while ((current = AST_LIST_REMOVE_HEAD(&c->buckets[i], entry))) {
			ast_free(current);
		}
	}

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_containers, -1);
#endif
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
	int i, lim;
	char *obj;
	static int prof_id = -1;
	struct ast_cli_args fake_args = { a->fd, 0, NULL };

	switch (cmd) {
	case CLI_INIT:
		e->command = "astobj2 test";
		e->usage = "Usage: astobj2 test <num>\n"
			   "       Runs astobj2 test. Creates 'num' objects,\n"
			   "       and test iterators, callbacks and may be other stuff\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	if (prof_id == -1)
		prof_id = ast_add_profile("ao2_alloc", 0);

	ast_cli(a->fd, "argc %d argv %s %s %s\n", a->argc, a->argv[0], a->argv[1], a->argv[2]);
	lim = atoi(a->argv[2]);
	ast_cli(a->fd, "called astobj_test\n");

	handle_astobj2_stats(e, CLI_HANDLER, &fake_args);
	/*
	 * allocate a container with no default callback, and no hash function.
	 * No hash means everything goes in the same bucket.
	 */
	c1 = ao2_t_container_alloc(100, NULL /* no callback */, NULL /* no hash */,"test");
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

	ast_verbose("now you should see an error message:\n");
	ao2_t_ref(&i, -1, "");	/* i is not a valid object so we print an error here */

	ast_cli(a->fd, "destroy container\n");
	ao2_t_ref(c1, -1, "");	/* destroy container */
	handle_astobj2_stats(e, CLI_HANDLER, &fake_args);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_astobj2[] = {
	AST_CLI_DEFINE(handle_astobj2_stats, "Print astobj2 statistics"),
	AST_CLI_DEFINE(handle_astobj2_test, "Test astobj2"),
};
#endif /* AO2_DEBUG */

int astobj2_init(void)
{
#ifdef AO2_DEBUG
	ast_cli_register_multiple(cli_astobj2, ARRAY_LEN(cli_astobj2));
#endif

	return 0;
}
