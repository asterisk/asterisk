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

#include <stdlib.h>

#include "asterisk/astobj2.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/linkedlists.h"

/*!
 * astobj2 objects are always prepended this data structure,
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
#define AO2_DEBUG 1
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
    strings = backtrace_symbols(addresses,c);
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

/*!
 * \brief convert from a pointer _p to an astobj2 object
 *
 * \return the pointer to the user-defined portion.
 */
#define EXTERNAL_OBJ(_p)	((_p) == NULL ? NULL : (_p)->user_data)

int ao2_lock(void *user_data)
{
	struct astobj2 *p = INTERNAL_OBJ(user_data);

	if (p == NULL)
		return -1;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_locked, 1);
#endif

	return ast_mutex_lock(&p->priv_data.lock);
}

int ao2_unlock(void *user_data)
{
	struct astobj2 *p = INTERNAL_OBJ(user_data);

	if (p == NULL)
		return -1;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_locked, -1);
#endif

	return ast_mutex_unlock(&p->priv_data.lock);
}

/*
 * The argument is a pointer to the user portion.
 */
int ao2_ref(void *user_data, const int delta)
{
	int current_value;
	int ret;
	struct astobj2 *obj = INTERNAL_OBJ(user_data);

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
		if (obj->priv_data.destructor_fn != NULL) 
			obj->priv_data.destructor_fn(user_data);

		ast_mutex_destroy(&obj->priv_data.lock);
#ifdef AO2_DEBUG
		ast_atomic_fetchadd_int(&ao2.total_mem, - obj->priv_data.data_size);
		ast_atomic_fetchadd_int(&ao2.total_objects, -1);
#endif
		/* for safety, zero-out the astobj2 header and also the
		 * first word of the user-data, which we make sure is always
		 * allocated. */
		bzero(obj, sizeof(struct astobj2 *) + sizeof(void *) );
		free(obj);
	}

	return ret;
}

/*
 * We always alloc at least the size of a void *,
 * for debugging purposes.
 */
void *ao2_alloc(size_t data_size, ao2_destructor_fn destructor_fn)
{
	/* allocation */
	struct astobj2 *obj;

	if (data_size < sizeof(void *))
		data_size = sizeof(void *);

	obj = calloc(1, sizeof(*obj) + data_size);

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

/* internal callback to destroy a container. */
static void container_destruct(void *c);

/* each bucket in the container is a tailq. */
AST_LIST_HEAD_NOLOCK(bucket, bucket_list);

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
 * To optimize this, we allocate larger arrays of bucket_list's
 * when we run out of them, and then manage our own freelist.
 * This will be more efficient as we can do the freelist management while
 * we hold the lock (that we need anyways).
 */
struct ao2_container {
	ao2_hash_fn hash_fn;
	ao2_callback_fn cmp_fn;
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
struct ao2_container *
ao2_container_alloc(const uint n_buckets, ao2_hash_fn hash_fn,
		ao2_callback_fn cmp_fn)
{
	/* XXX maybe consistency check on arguments ? */
	/* compute the container size */
	size_t container_size = sizeof(struct ao2_container) + n_buckets * sizeof(struct bucket);

	struct ao2_container *c = ao2_alloc(container_size, container_destruct);

	if (!c)
		return NULL;
	
	c->version = 1;	/* 0 is a reserved value here */
	c->n_buckets = n_buckets;
	c->hash_fn = hash_fn ? hash_fn : hash_zero;
	c->cmp_fn = cmp_fn;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_containers, 1);
#endif

	return c;
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
struct bucket_list {
	AST_LIST_ENTRY(bucket_list) entry;
	int version;
	struct astobj2 *astobj;		/* pointer to internal data */
}; 

/*
 * link an object to a container
 */
void *__ao2_link(struct ao2_container *c, void *user_data, int iax2_hack)
{
	int i;
	/* create a new list entry */
	struct bucket_list *p;
	struct astobj2 *obj = INTERNAL_OBJ(user_data);
	
	if (!obj)
		return NULL;

	if (INTERNAL_OBJ(c) == NULL)
		return NULL;

	p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;

	i = c->hash_fn(user_data, OBJ_POINTER);

	ao2_lock(c);
	i %= c->n_buckets;
	p->astobj = obj;
	p->version = ast_atomic_fetchadd_int(&c->version, 1);
	if (iax2_hack)
		AST_LIST_INSERT_HEAD(&c->buckets[i], p, entry);
	else
		AST_LIST_INSERT_TAIL(&c->buckets[i], p, entry);
	ast_atomic_fetchadd_int(&c->elements, 1);
	ao2_ref(user_data, +1);
	ao2_unlock(c);
	
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
 * and destroy the associated * ao2_bucket_list structure.
 */
void *ao2_unlink(struct ao2_container *c, void *user_data)
{
	if (INTERNAL_OBJ(user_data) == NULL)	/* safety check on the argument */
		return NULL;

	ao2_callback(c, OBJ_UNLINK | OBJ_POINTER | OBJ_NODATA, ao2_match_by_addr, user_data);

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
 * Browse the container using different stategies accoding the flags.
 * \return Is a pointer to an object or to a list of object if OBJ_MULTIPLE is 
 * specified.
 */
void *ao2_callback(struct ao2_container *c,
	const enum search_flags flags,
	ao2_callback_fn cb_fn, void *arg)
{
	int i, last;	/* search boundaries */
	void *ret = NULL;

	if (INTERNAL_OBJ(c) == NULL)	/* safety check on the argument */
		return NULL;

	if ((flags & (OBJ_MULTIPLE | OBJ_NODATA)) == OBJ_MULTIPLE) {
		ast_log(LOG_WARNING, "multiple data return not implemented yet (flags %x)\n", flags);
		return NULL;
	}

	/* override the match function if necessary */
#if 0
	/* Removing this slightly changes the meaning of OBJ_POINTER, but makes it
	 * do what I want it to.  I'd like to hint to ao2_callback that the arg is
	 * of the same object type, so it can be passed to the hash function.
	 * However, I don't want to imply that this is the object being searched for. */
	if (flags & OBJ_POINTER)
		cb_fn = match_by_addr;
	else
#endif
	if (cb_fn == NULL)	/* if NULL, match everything */
		cb_fn = cb_true;
	/*
	 * XXX this can be optimized.
	 * If we have a hash function and lookup by pointer,
	 * run the hash function. Otherwise, scan the whole container
	 * (this only for the time being. We need to optimize this.)
	 */
	if ((flags & OBJ_POINTER))	/* we know hash can handle this case */
		i = c->hash_fn(arg, flags & OBJ_POINTER) % c->n_buckets;
	else			/* don't know, let's scan all buckets */
		i = -1;		/* XXX this must be fixed later. */

	/* determine the search boundaries: i..last-1 */
	if (i < 0) {
		i = 0;
		last = c->n_buckets;
	} else {
		last = i + 1;
	}

	ao2_lock(c);	/* avoid modifications to the content */

	for (; i < last ; i++) {
		/* scan the list with prev-cur pointers */
		struct bucket_list *cur;

		AST_LIST_TRAVERSE_SAFE_BEGIN(&c->buckets[i], cur, entry) {
			int match = cb_fn(EXTERNAL_OBJ(cur->astobj), arg, flags) & (CMP_MATCH | CMP_STOP);

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
				ao2_ref(ret, 1);
			}

			if (flags & OBJ_UNLINK) {	/* must unlink */
				struct bucket_list *x = cur;

				/* we are going to modify the container, so update version */
				ast_atomic_fetchadd_int(&c->version, 1);
				AST_LIST_REMOVE_CURRENT(&c->buckets[i], entry);
				/* update number of elements and version */
				ast_atomic_fetchadd_int(&c->elements, -1);
				ao2_ref(EXTERNAL_OBJ(x->astobj), -1);
				free(x);	/* free the link record */
			}

			if ((match & CMP_STOP) || (flags & OBJ_MULTIPLE) == 0) {
				/* We found the only match we need */
				i = last;	/* force exit from outer loop */
				break;
			}
			if (!(flags & OBJ_NODATA)) {
#if 0	/* XXX to be completed */
				/*
				 * This is the multiple-return case. We need to link
				 * the object in a list. The refcount is already increased.
				 */
#endif
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
	}
	ao2_unlock(c);
	return ret;
}

/*!
 * the find function just invokes the default callback with some reasonable flags.
 */
void *ao2_find(struct ao2_container *c, void *arg, enum search_flags flags)
{
	return ao2_callback(c, flags, c->cmp_fn, arg);
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
	
	return a;
}

/*
 * move to the next element in the container.
 */
void * ao2_iterator_next(struct ao2_iterator *a)
{
	int lim;
	struct bucket_list *p = NULL;
	void *ret = NULL;

	if (INTERNAL_OBJ(a->c) == NULL)
		return NULL;

	if (!(a->flags & F_AO2I_DONTLOCK))
		ao2_lock(a->c);

	/* optimization. If the container is unchanged and
	 * we have a pointer, try follow it
	 */
	if (a->c->version == a->c_version && (p = a->obj) ) {
		if ( (p = AST_LIST_NEXT(p, entry)) )
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
		a->version = p->version;
		a->obj = p;
		a->c_version = a->c->version;
		ret = EXTERNAL_OBJ(p->astobj);
		/* inc refcount of returned object */
		ao2_ref(ret, 1);
	}

	if (!(a->flags & F_AO2I_DONTLOCK))
		ao2_unlock(a->c);

	return ret;
}

/* callback for destroying container.
 * we can make it simple as we know what it does
 */
static int cd_cb(void *obj, void *arg, int flag)
{
	ao2_ref(obj, -1);
	return 0;
}
	
static void container_destruct(void *_c)
{
	struct ao2_container *c = _c;

	ao2_callback(c, OBJ_UNLINK, cd_cb, NULL);

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_containers, -1);
#endif
}

#ifdef AO2_DEBUG
static int print_cb(void *obj, void *arg, int flag)
{
	int *fd = arg;
	char *s = (char *)obj;

	ast_cli(*fd, "string <%s>\n", s);
	return 0;
}

/*
 * Print stats
 */
static int handle_astobj2_stats(int fd, int argc, char *argv[])
{
	ast_cli(fd, "Objects    : %d\n", ao2.total_objects);
	ast_cli(fd, "Containers : %d\n", ao2.total_containers);
	ast_cli(fd, "Memory     : %d\n", ao2.total_mem);
	ast_cli(fd, "Locked     : %d\n", ao2.total_locked);
	ast_cli(fd, "Refs       : %d\n", ao2.total_refs);
	return 0;
}

/*
 * This is testing code for astobj
 */
static int handle_astobj2_test(int fd, int argc, char *argv[])
{
	struct ao2_container *c1;
	int i, lim;
	char *obj;
	static int prof_id = -1;

	if (prof_id == -1)
		prof_id = ast_add_profile("ao2_alloc", 0);

	ast_cli(fd, "argc %d argv %s %s %s\n", argc, argv[0], argv[1], argv[2]);
	lim = atoi(argv[2]);
	ast_cli(fd, "called astobj_test\n");

	handle_astobj2_stats(fd, 0, NULL);
	/*
	 * allocate a container with no default callback, and no hash function.
	 * No hash means everything goes in the same bucket.
	 */
	c1 = ao2_container_alloc(100, NULL /* no callback */, NULL /* no hash */);
	ast_cli(fd, "container allocated as %p\n", c1);

	/*
	 * fill the container with objects.
	 * ao2_alloc() gives us a reference which we pass to the
	 * container when we do the insert.
	 */
	for (i = 0; i < lim; i++) {
		ast_mark(prof_id, 1 /* start */);
		obj = ao2_alloc(80, NULL);
		ast_mark(prof_id, 0 /* stop */);
		ast_cli(fd, "object %d allocated as %p\n", i, obj);
		sprintf(obj, "-- this is obj %d --", i);
		ao2_link(c1, obj);
	}
	ast_cli(fd, "testing callbacks\n");
	ao2_callback(c1, 0, print_cb, &fd);

	ast_cli(fd, "testing iterators, remove every second object\n");
	{
		struct ao2_iterator ai;
		int x = 0;

		ai = ao2_iterator_init(c1, 0);
		while ( (obj = ao2_iterator_next(&ai)) ) {
			ast_cli(fd, "iterator on <%s>\n", obj);
			if (x++ & 1)
				ao2_unlink(c1, obj);
			ao2_ref(obj, -1);
		}
		ast_cli(fd, "testing iterators again\n");
		ai = ao2_iterator_init(c1, 0);
		while ( (obj = ao2_iterator_next(&ai)) ) {
			ast_cli(fd, "iterator on <%s>\n", obj);
			ao2_ref(obj, -1);
		}
	}
	ast_cli(fd, "testing callbacks again\n");
	ao2_callback(c1, 0, print_cb, &fd);

	ast_verbose("now you should see an error message:\n");
	ao2_ref(&i, -1);	/* i is not a valid object so we print an error here */

	ast_cli(fd, "destroy container\n");
	ao2_ref(c1, -1);	/* destroy container */
	handle_astobj2_stats(fd, 0, NULL);
	return 0;
}

static struct ast_cli_entry cli_astobj2[] = {
	{ { "astobj2", "stats", NULL },
	handle_astobj2_stats, "Print astobj2 statistics", },
	{ { "astobj2", "test", NULL } , handle_astobj2_test, "Test astobj2", },
};
#endif /* AO2_DEBUG */

int astobj2_init(void)
{
#ifdef AO2_DEBUG
	ast_cli_register_multiple(cli_astobj2, ARRAY_LEN(cli_astobj2));
#endif

	return 0;
}
