/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Steve Murphy <murf@digium.com>
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
 *  \brief code to implement generic hash tables
 *
 *  \author Steve Murphy <murf@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#define WRAP_LIBC_MALLOC
#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include <ctype.h>

#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"
#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"
#include "asterisk/linkedlists.h"
#include "asterisk/hashtab.h"


#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
static void _ast_hashtab_resize(struct ast_hashtab *tab, const char *file, int lineno, const char *func);
#define ast_hashtab_resize(a)	_ast_hashtab_resize(a,__FILE__, __LINE__, __PRETTY_FUNCTION__)
#else
static void ast_hashtab_resize(struct ast_hashtab *tab);
#endif
static void *ast_hashtab_lookup_internal(struct ast_hashtab *tab, const void *obj, unsigned int h);

/* some standard, default routines for general use */

int ast_hashtab_compare_strings(const void *a, const void *b)
{
	return strcmp(a, b);
}

int ast_hashtab_compare_strings_nocase(const void *a, const void *b)
{
	return strcasecmp(a, b);
}

int ast_hashtab_compare_ints(const void *a, const void *b)
{
	int ai = *((int *) a);
	int bi = *((int *) b);

	if (ai < bi)
		return -1;

	return !(ai == bi);
}

int ast_hashtab_compare_shorts(const void *a, const void *b)
{
	short as = *((short *) a);
	short bs = *((short *) b);

	if (as < bs)
		return -1;

	return !(as == bs);
}

int ast_hashtab_resize_java(struct ast_hashtab *tab)
{
	double loadfactor = (double) tab->hash_tab_elements / (double) tab->hash_tab_size;

	return (loadfactor > 0.75);
}

int ast_hashtab_resize_tight(struct ast_hashtab *tab)
{
	return (tab->hash_tab_elements > tab->hash_tab_size); /* this is quicker than division */
}

int ast_hashtab_resize_none(struct ast_hashtab *tab) /* always return 0 -- no resizing */
{
	return 0;
}

int ast_is_prime(int num)
{
	int tnum, limit;

	if (!(num & 0x1)) /* even number -- not prime */
		return 0;

	/* Loop through ODD numbers starting with 3 */

	tnum = 3;
	limit = num;
	while (tnum < limit) {
		if (!(num % tnum))
			return 0;

		/* really, we only need to check sqrt(num) numbers */
		limit = num / tnum;

		/* we only check odd numbers */
		tnum = tnum + 2;
	}

	/* if we made it through the loop, the number is a prime */
	return 1;
}

int ast_hashtab_newsize_java(struct ast_hashtab *tab)
{
	int i = (tab->hash_tab_size << 1); /* multiply by two */

	while (!ast_is_prime(i))
		i++;

	return i;
}

int ast_hashtab_newsize_tight(struct ast_hashtab *tab)
{
	int x = (tab->hash_tab_size << 1);
	int i = (tab->hash_tab_size + x);

	while (!ast_is_prime(i))
		i++;

	return i;
}

int ast_hashtab_newsize_none(struct ast_hashtab *tab) /* always return current size -- no resizing */
{
	return tab->hash_tab_size;
}

unsigned int ast_hashtab_hash_string(const void *obj)
{
	unsigned char *str = (unsigned char *) obj;
	unsigned int total;

	for (total = 0; *str; str++) {
		unsigned int tmp = total;
		total <<= 1; /* multiply by 2 */
		total += tmp; /* multiply by 3 */
		total <<= 2; /* multiply by 12 */
		total += tmp; /* multiply by 13 */

		total += ((unsigned int)(*str));
	}
	return total;
}

unsigned int ast_hashtab_hash_string_sax(const void *obj) /* from Josh */
{
	const unsigned char *str = obj;
	unsigned int total = 0, c = 0;

	while ((c = *str++))
		total ^= (total << 5) + (total >> 2) + (total << 10) + c;

	return total;
}

unsigned int ast_hashtab_hash_string_nocase(const void *obj)
{
	const unsigned char *str = obj;
	unsigned int total;

	for (total = 0; *str; str++) {
		unsigned int tmp = total;
		unsigned int charval = toupper(*str);

		/* hopefully, the following is faster than multiplication by 7 */
		/* why do I go to this bother? A good compiler will do this
		   anyway, if I say total *= 13 */
		/* BTW, tried *= 7, and it doesn't do as well in spreading things around! */
		total <<= 1; /* multiply by 2 */
		total += tmp; /* multiply by 3 */
		total <<= 2; /* multiply by 12 */
		total += tmp; /* multiply by 13 */

		total += (charval);
	}

	return total;
}

unsigned int ast_hashtab_hash_int(const int x)
{
	return x;
}

unsigned int ast_hashtab_hash_short(const short x)
{
	/* hmmmm.... modulus is best < 65535 !! */
	return x;
}

struct ast_hashtab *
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
_ast_hashtab_create
#else
ast_hashtab_create
#endif
(int initial_buckets,
	int (*compare)(const void *a, const void *b),
	int (*resize)(struct ast_hashtab *),
	int (*newsize)(struct ast_hashtab *tab),
	unsigned int (*hash)(const void *obj),
	int do_locking
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
	, const char *file, int lineno, const char *function
#endif
)
{
	struct ast_hashtab *ht;

#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
	if (!(ht = __ast_calloc(1, sizeof(*ht), file, lineno, function)))
#else
	if (!(ht = ast_calloc(1, sizeof(*ht))))
#endif
		return NULL;

	while (!ast_is_prime(initial_buckets)) /* make sure this is prime */
		initial_buckets++;

#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
	if (!(ht->array = __ast_calloc(initial_buckets, sizeof(*(ht->array)), file, lineno, function))) {
#else
	if (!(ht->array = ast_calloc(initial_buckets, sizeof(*(ht->array))))) {
#endif
		ast_free(ht);
		return NULL;
	}

	ht->hash_tab_size = initial_buckets;
	ht->compare = compare;
	ht->resize = resize;
	ht->newsize = newsize;
	ht->hash = hash;
	ht->do_locking = do_locking;

	if (do_locking)
		ast_rwlock_init(&ht->lock);

	if (!ht->resize)
		ht->resize = ast_hashtab_resize_java;

	if (!ht->newsize)
		ht->newsize = ast_hashtab_newsize_java;

	return ht;
}

#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
struct ast_hashtab *_ast_hashtab_dup(struct ast_hashtab *tab, void *(*obj_dup_func)(const void *obj), const char *file, int lineno, const char *func)
#else
struct ast_hashtab *ast_hashtab_dup(struct ast_hashtab *tab, void *(*obj_dup_func)(const void *obj))
#endif
{
	struct ast_hashtab *ht;
	unsigned int i;

	if (!(ht = ast_calloc(1, sizeof(*ht))))
		return NULL;

	if (!(ht->array =
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
		__ast_calloc(tab->hash_tab_size, sizeof(*(ht->array)), file, lineno, func)
#else
		ast_calloc(tab->hash_tab_size, sizeof(*(ht->array)))
#endif
		)) {
		free(ht);
		return NULL;
	}

	ht->hash_tab_size = tab->hash_tab_size;
	ht->compare = tab->compare;
	ht->resize = tab->resize;
	ht->newsize = tab->newsize;
	ht->hash = tab->hash;
	ht->do_locking = tab->do_locking;

	if (ht->do_locking)
		ast_rwlock_init(&ht->lock);

	/* now, dup the objects in the buckets and get them into the table */
	/* the fast way is to use the existing array index, and not have to hash
	   the objects again */
	for (i = 0; i < ht->hash_tab_size; i++) {
		struct ast_hashtab_bucket *b = tab->array[i];
		while (b) {
			void *newobj = (*obj_dup_func)(b->object);
			if (newobj)
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
				_ast_hashtab_insert_immediate_bucket(ht, newobj, i, file, lineno, func);
#else
				ast_hashtab_insert_immediate_bucket(ht, newobj, i);
#endif
			b = b->next;
		}
	}

	return ht;
}

static void tlist_del_item(struct ast_hashtab_bucket **head, struct ast_hashtab_bucket *item)
{
	/* item had better be in the list! or suffer the weirdness that occurs, later! */
	if (*head == item) { /* first item in the list */
		*head = item->tnext;
		if (item->tnext)
			item->tnext->tprev = NULL;
	} else {
		/* short circuit stuff */
		item->tprev->tnext = item->tnext;
		if (item->tnext)
			item->tnext->tprev = item->tprev;
	}
}

static void tlist_add_head(struct ast_hashtab_bucket **head, struct ast_hashtab_bucket *item)
{
	if (*head) {
		item->tnext = *head;
		item->tprev = NULL;
		(*head)->tprev = item;
		*head = item;
	} else {
		/* the list is empty */
		*head = item;
		item->tprev = NULL;
		item->tnext = NULL;
	}
}

/* user-controlled hashtab locking. Create a hashtab without locking, then call the
   following locking routines yourself to lock the table between threads. */

void ast_hashtab_wrlock(struct ast_hashtab *tab)
{
	ast_rwlock_wrlock(&tab->lock);
}

void ast_hashtab_rdlock(struct ast_hashtab *tab)
{
	ast_rwlock_rdlock(&tab->lock);
}

void ast_hashtab_initlock(struct ast_hashtab *tab)
{
	ast_rwlock_init(&tab->lock);
}

void ast_hashtab_destroylock(struct ast_hashtab *tab)
{
	ast_rwlock_destroy(&tab->lock);
}

void ast_hashtab_unlock(struct ast_hashtab *tab)
{
	ast_rwlock_unlock(&tab->lock);
}

void ast_hashtab_destroy(struct ast_hashtab *tab, void (*objdestroyfunc)(void *obj))
{
	/* this func will free the hash table and all its memory. It
	   doesn't touch the objects stored in it */
	if (tab) {

		if (tab->do_locking)
			ast_rwlock_wrlock(&tab->lock);

		if (tab->array) {
			/* go thru and destroy the buckets */
			struct ast_hashtab_bucket *t;
			int i;

			while (tab->tlist) {
				t = tab->tlist;
				if (t->object && objdestroyfunc) {
					/* I cast this because I'm not going to MOD it, I'm going to DESTROY
					 * it.
					 */
					(*objdestroyfunc)((void *) t->object);
				}

				tlist_del_item(&(tab->tlist), tab->tlist);
				free(t);
			}

			for (i = 0; i < tab->hash_tab_size; i++) {
				/* Not totally necessary, but best to destroy old pointers */
				tab->array[i] = NULL;
			}
			free(tab->array);
		}
		if (tab->do_locking) {
			ast_rwlock_unlock(&tab->lock);
			ast_rwlock_destroy(&tab->lock);
		}
		free(tab);
	}
}

#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
int _ast_hashtab_insert_immediate(struct ast_hashtab *tab, const void *obj, const char *file, int lineno, const char *func)
#else
int ast_hashtab_insert_immediate(struct ast_hashtab *tab, const void *obj)
#endif
{
	unsigned int h;
	int res=0;

	if (!tab || !obj)
		return res;

	if (tab->do_locking)
		ast_rwlock_wrlock(&tab->lock);

	h = (*tab->hash)(obj) % tab->hash_tab_size;

#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
	res = _ast_hashtab_insert_immediate_bucket(tab, obj, h, file, lineno, func);
#else
	res = ast_hashtab_insert_immediate_bucket(tab, obj, h);
#endif

	if (tab->do_locking)
		ast_rwlock_unlock(&tab->lock);

	return res;
}

#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
int _ast_hashtab_insert_immediate_bucket(struct ast_hashtab *tab, const void *obj, unsigned int h, const char *file, int lineno, const char *func)
#else
int ast_hashtab_insert_immediate_bucket(struct ast_hashtab *tab, const void *obj, unsigned int h)
#endif
{
	int c;
	struct ast_hashtab_bucket *b;

	if (!tab || !obj)
		return 0;

	for (c = 0, b = tab->array[h]; b; b= b->next)
		c++;

	if (c + 1 > tab->largest_bucket_size)
		tab->largest_bucket_size = c + 1;

	if (!(b =
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
			__ast_calloc(1, sizeof(*b), file, lineno, func)
#else
			ast_calloc(1, sizeof(*b))
#endif
		)) return 0;

	b->object = obj;
	b->next = tab->array[h];
	tab->array[h] = b;

	if (b->next)
		b->next->prev = b;

	tlist_add_head(&(tab->tlist), b);
	tab->hash_tab_elements++;

	if ((*tab->resize)(tab))
		ast_hashtab_resize(tab);

	return 1;
}

#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
int _ast_hashtab_insert_safe(struct ast_hashtab *tab, const void *obj, const char *file, int lineno, const char *func)
#else
int ast_hashtab_insert_safe(struct ast_hashtab *tab, const void *obj)
#endif
{
	/* check to see if the element is already there; insert only if
	   it is not there. */
	/* will force a resize if the resize func returns 1 */
	/* returns 1 on success, 0 if there's a problem, or it's already there. */
	unsigned int bucket = 0;

	if (tab->do_locking)
		ast_rwlock_wrlock(&tab->lock);

	if (!ast_hashtab_lookup_bucket(tab, obj, &bucket)) {
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
		int ret2 = _ast_hashtab_insert_immediate_bucket(tab, obj, bucket, file, lineno, func);
#else
		int ret2 = ast_hashtab_insert_immediate_bucket(tab, obj, bucket);
#endif

		if (tab->do_locking)
			ast_rwlock_unlock(&tab->lock);

		return ret2;
	}

	if (tab->do_locking)
		ast_rwlock_unlock(&tab->lock);

	return 0;
}

void *ast_hashtab_lookup(struct ast_hashtab *tab, const void *obj)
{
	/* lookup this object in the hash table. return a ptr if found, or NULL if not */
	unsigned int h;
	void *ret;

	if (!tab || !obj)
		return 0;

	if (tab->do_locking)
		ast_rwlock_rdlock(&tab->lock);

	h = (*tab->hash)(obj) % tab->hash_tab_size;

	ret = ast_hashtab_lookup_internal(tab,obj,h);

	if (tab->do_locking)
		ast_rwlock_unlock(&tab->lock);

	return ret;
}


void *ast_hashtab_lookup_with_hash(struct ast_hashtab *tab, const void *obj, unsigned int hashval)
{
	/* lookup this object in the hash table. return a ptr if found, or NULL if not */
	unsigned int h;
	void *ret;

	if (!tab || !obj)
		return 0;

	if (tab->do_locking)
		ast_rwlock_rdlock(&tab->lock);

	h = hashval % tab->hash_tab_size;

	ret = ast_hashtab_lookup_internal(tab,obj,h);

	if (tab->do_locking)
		ast_rwlock_unlock(&tab->lock);

	return ret;
}

void *ast_hashtab_lookup_bucket(struct ast_hashtab *tab, const void *obj, unsigned int *bucket)
{
	/* lookup this object in the hash table. return a ptr if found, or NULL if not */
	unsigned int h;
	void *ret;

	if (!tab || !obj)
		return 0;

	h = (*tab->hash)(obj) % tab->hash_tab_size;

	ret = ast_hashtab_lookup_internal(tab,obj,h);

	*bucket = h;

	return ret;
}

static void *ast_hashtab_lookup_internal(struct ast_hashtab *tab, const void *obj, unsigned int h)
{
	struct ast_hashtab_bucket *b;

	for (b = tab->array[h]; b; b = b->next) {
		if (!(*tab->compare)(obj,b->object)) {
			/* I can't touch obj in this func, but the outside world is welcome to */
			return (void*) b->object;
		}
	}

	return NULL;
}

void ast_hashtab_get_stats(struct ast_hashtab *tab, int *biggest_bucket_size, int *resize_count, int *num_objects, int *num_buckets)
{
	/* returns key stats for the table */
	if (tab->do_locking)
		ast_rwlock_rdlock(&tab->lock);
	*biggest_bucket_size = tab->largest_bucket_size;
	*resize_count = tab->resize_count;
	*num_objects = tab->hash_tab_elements;
	*num_buckets = tab->hash_tab_size;
	if (tab->do_locking)
		ast_rwlock_unlock(&tab->lock);
}

/* this function returns the number of elements stored in the hashtab */
int ast_hashtab_size(struct ast_hashtab *tab)
{
	return tab->hash_tab_elements;
}

/* this function returns the size of the bucket array in the hashtab */
int ast_hashtab_capacity( struct ast_hashtab *tab)
{
	return tab->hash_tab_size;
}

/* the insert operation calls this, and is wrlock'd when it does. */
/* if you want to call it, you should set the wrlock yourself */

#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
static void _ast_hashtab_resize(struct ast_hashtab *tab, const char *file, int lineno, const char *func)
#else
static void ast_hashtab_resize(struct ast_hashtab *tab)
#endif
{
	/* this function is called either internally, when the resize func returns 1, or
	   externally by the user to force a resize of the hash table */
	int newsize = (*tab->newsize)(tab), i, c;
	unsigned int h;
	struct ast_hashtab_bucket *b,*bn;

	/* Since we keep a DLL of all the buckets in tlist,
	   all we have to do is free the array, malloc a new one,
	   and then go thru the tlist array and reassign them into
	   the bucket arrayj.
	*/
	for (i = 0; i < tab->hash_tab_size; i++) { /* don't absolutely have to do this, but
											why leave ptrs laying around */
		tab->array[i] = 0; /* erase old ptrs */
	}
	free(tab->array);
	if (!(tab->array =
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
		__ast_calloc(newsize, sizeof(*(tab->array)), file, lineno, func)
#else
		ast_calloc(newsize, sizeof(*(tab->array)))
#endif
		))
		return;

	/* now sort the buckets into their rightful new slots */
	tab->resize_count++;
	tab->hash_tab_size = newsize;
	tab->largest_bucket_size = 0;

	for (b = tab->tlist; b; b = bn) {
		b->prev = 0;
		bn = b->tnext;
		h = (*tab->hash)(b->object) % tab->hash_tab_size;
		b->next = tab->array[h];
		if (b->next)
			b->next->prev = b;
		tab->array[h] = b;
	}
	/* recalc the largest bucket size */
	for (i = 0; i < tab->hash_tab_size; i++) {
		for (c = 0, b = tab->array[i]; b; b = b->next)
			c++;
		if (c > tab->largest_bucket_size)
			tab->largest_bucket_size = c;
	}
}

#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
struct ast_hashtab_iter *_ast_hashtab_start_traversal(struct ast_hashtab *tab, const char *file, int lineno, const char *func)
#else
struct ast_hashtab_iter *ast_hashtab_start_traversal(struct ast_hashtab *tab)
#endif
{
	/* returns an iterator */
	struct ast_hashtab_iter *it;

	if (!(it =
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
			__ast_calloc(1, sizeof(*it), file, lineno, func)
#else
			ast_calloc(1, sizeof(*it))
#endif
		))
		return NULL;

	it->next = tab->tlist;
	it->tab = tab;
	if (tab->do_locking)
		ast_rwlock_rdlock(&tab->lock);

	return it;
}

/* use this function to get a write lock */
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
struct ast_hashtab_iter *_ast_hashtab_start_write_traversal(struct ast_hashtab *tab, const char *file, int lineno, const char *func)
#else
struct ast_hashtab_iter *ast_hashtab_start_write_traversal(struct ast_hashtab *tab)
#endif
{
	/* returns an iterator */
	struct ast_hashtab_iter *it;

	if (!(it =
#if (defined(MALLOC_DEBUG) && !defined(STANDALONE))
			__ast_calloc(1, sizeof(*it), file, lineno, func)
#else
			ast_calloc(1, sizeof(*it))
#endif
		))
		return NULL;

	it->next = tab->tlist;
	it->tab = tab;
	if (tab->do_locking)
		ast_rwlock_wrlock(&tab->lock);

	return it;
}

void ast_hashtab_end_traversal(struct ast_hashtab_iter *it)
{
	if (it->tab->do_locking)
		ast_rwlock_unlock(&it->tab->lock);
	free(it);
}

void *ast_hashtab_next(struct ast_hashtab_iter *it)
{
	/* returns the next object in the list, advances iter one step */
	struct ast_hashtab_bucket *retval;

	if (it && it->next) { /* there's a next in the bucket list */
		retval = it->next;
		it->next = retval->tnext;
		return (void *) retval->object;
	}

	return NULL;
}

static void *ast_hashtab_remove_object_internal(struct ast_hashtab *tab, struct ast_hashtab_bucket *b, int h)
{
	const void *obj2;

	if (b->prev)
		b->prev->next = b->next;
	else
		tab->array[h] = b->next;

	if (b->next)
		b->next->prev = b->prev;

	tlist_del_item(&(tab->tlist), b);

	obj2 = b->object;
	b->object = b->next = (void*)2;
	free(b); /* free up the hashbucket */
	tab->hash_tab_elements--;
#ifdef DEBUG
	{
		int c2;
		struct ast_hashtab_bucket *b2;
		/* do a little checking */
		for (c2 = 0, b2 = tab->tlist; b2; b2 = b2->tnext) {
			c2++;
		}
		if (c2 != tab->hash_tab_elements) {
			printf("Hey! we didn't delete right! there are %d elements in the list, and we expected %d\n",
				   c2, tab->hash_tab_elements);
		}
		for (c2 = 0, b2 = tab->tlist; b2; b2 = b2->tnext) {
			unsigned int obj3 = (unsigned long) obj2;
			unsigned int b3 = (unsigned long) b;
			if (b2->object == obj2)
				printf("Hey-- you've still got a bucket pointing at ht_element %x\n", obj3);
			if (b2->next == b)
				printf("Hey-- you've still got a bucket with next ptr pointing to deleted bucket %x\n", b3);
			if (b2->prev == b)
				printf("Hey-- you've still got a bucket with prev ptr pointing to deleted bucket %x\n", b3);
			if (b2->tprev == b)
				printf("Hey-- you've still got a bucket with tprev ptr pointing to deleted bucket %x\n", b3);
			if (b2->tnext == b)
				printf("Hey-- you've still got a bucket with tnext ptr pointing to deleted bucket %x\n", b3);
		}
	}
#endif
	return (void *) obj2; /* inside this code, the obj's are untouchable, but outside, they aren't */
}

void *ast_hashtab_remove_object_via_lookup(struct ast_hashtab *tab, void *obj)
{
	/* looks up the object; removes the corresponding bucket */
	const void *obj2;

	if (!tab || !obj)
		return 0;

	if (tab->do_locking)
		ast_rwlock_wrlock(&tab->lock);

	obj2 = ast_hashtab_remove_object_via_lookup_nolock(tab,obj);

	if (tab->do_locking)
		ast_rwlock_unlock(&tab->lock);

	return (void *)obj2;
}

void *ast_hashtab_remove_object_via_lookup_nolock(struct ast_hashtab *tab, void *obj)
{
	/* looks up the object; removes the corresponding bucket */
	unsigned int h;
	struct ast_hashtab_bucket *b;

	if (!tab || !obj)
		return 0;

	h = (*tab->hash)(obj) % tab->hash_tab_size;
	for (b = tab->array[h]; b; b = b->next) {

		if (!(*tab->compare)(obj, b->object)) {
			const void *obj2;

			obj2 = ast_hashtab_remove_object_internal(tab, b, h);

			return (void *) obj2; /* inside this code, the obj's are untouchable, but outside, they aren't */
		}
	}

	return 0;
}

void *ast_hashtab_remove_this_object(struct ast_hashtab *tab, void *obj)
{
	/* looks up the object by hash and then comparing pts in bucket list instead of
	   calling the compare routine; removes the bucket -- a slightly cheaper operation */
	/* looks up the object; removes the corresponding bucket */
	const void *obj2;

	if (!tab || !obj)
		return 0;

	if (tab->do_locking)
		ast_rwlock_wrlock(&tab->lock);

	obj2 = ast_hashtab_remove_this_object_nolock(tab,obj);

	if (tab->do_locking)
		ast_rwlock_unlock(&tab->lock);

	return (void *)obj2;
}

void *ast_hashtab_remove_this_object_nolock(struct ast_hashtab *tab, void *obj)
{
	/* looks up the object by hash and then comparing pts in bucket list instead of
	   calling the compare routine; removes the bucket -- a slightly cheaper operation */
	/* looks up the object; removes the corresponding bucket */
	unsigned int h;
	struct ast_hashtab_bucket *b;

	if (!tab || !obj)
		return 0;

	h = (*tab->hash)(obj) % tab->hash_tab_size;
	for (b = tab->array[h]; b; b = b->next) {

		if (obj == b->object) {
			const void *obj2;
			obj2 = ast_hashtab_remove_object_internal(tab, b, h);

			return (void *) obj2; /* inside this code, the obj's are untouchable, but outside, they aren't */
		}
	}

	return 0;
}
