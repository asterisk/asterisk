/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Object Model for Asterisk
 * 
 * Copyright (C) 2004 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_ASTOBJ_H
#define _ASTERISK_ASTOBJ_H

#include <string.h>
#include "asterisk/lock.h"

/*!
  \file astobj.h
  \brief A set of macros implementing the asterisk object and container.  Macros
         are used for maximum performance, to support multiple inheritance, and
		 to be easily integrated into existing structures without additional 
		 malloc calls, etc.
*/

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define ASTOBJ_DEFAULT_NAMELEN 	80
#define ASTOBJ_DEFAULT_BUCKETS	256
#define ASTOBJ_DEFAULT_HASH		ast_strhash

#define ASTOBJ_FLAG_MARKED	(1 << 0)		/* Object has been marked for future operation */

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 96)
#define __builtin_expect(exp, c) (exp)
#endif

/* C++ is simply a syntactic crutch for those who cannot think for themselves
   in an object oriented way. */

#define ASTOBJ_RDLOCK(object) ast_mutex_lock(&(object)->_lock)
#define ASTOBJ_WRLOCK(object) ast_mutex_lock(&(object)->_lock)
#define ASTOBJ_UNLOCK(object) ast_mutex_unlock(&(object)->_lock)

#ifdef ASTOBJ_CONTAINER_HASHMODEL 
#define __ASTOBJ_HASH(type,hashes) \
	type *next[hashes] 
#else 
#define __ASTOBJ_HASH(type,hashes) \
	type *next[1] 
#endif	

#define ASTOBJ_COMPONENTS_NOLOCK_FULL(type,namelen,hashes) \
	char name[namelen]; \
	int refcount; \
	int objflags; \
	__ASTOBJ_HASH(type,hashes)
	
#define ASTOBJ_COMPONENTS_NOLOCK(type) \
	ASTOBJ_COMPONENTS_NOLOCK_FULL(type,ASTOBJ_DEFAULT_NAMELEN,1)

#define ASTOBJ_COMPONENTS(type) \
	ASTOBJ_COMPONENTS_NOLOCK(type); \
	ast_mutex_t _lock; 
	
#define ASTOBJ_COMPONENTS_FULL(type,namelen,hashes) \
	ASTOBJ_COMPONENTS_NOLOCK_FULL(type,namelen,hashes); \
	ast_mutex_t _lock; 

#define ASTOBJ_REF(object) \
	({ \
		ASTOBJ_WRLOCK(object); \
		(object)->refcount++; \
		ASTOBJ_UNLOCK(object); \
		(object); \
	})
	
#define ASTOBJ_UNREF(object,destructor) \
	do { \
		int newcount = 0; \
		ASTOBJ_WRLOCK(object); \
		if (__builtin_expect((object)->refcount, 1)) \
			newcount = --((object)->refcount); \
		else \
			ast_log(LOG_WARNING, "Unreferencing unreferenced (object)!\n"); \
		ASTOBJ_UNLOCK(object); \
		if (newcount == 0) { \
			ast_mutex_destroy(&(object)->_lock); \
			destructor((object)); \
		} \
		(object) = NULL; \
	} while(0)

#define ASTOBJ_MARK(object) \
	do { \
		ASTOBJ_WRLOCK(object); \
		(object)->objflags |= ASTOBJ_FLAG_MARKED; \
		ASTOBJ_UNLOCK(object); \
	} while(0)
	
#define ASTOBJ_UNMARK(object) \
	do { \
		ASTOBJ_WRLOCK(object); \
		(object)->objflags &= ~ASTOBJ_FLAG_MARKED; \
		ASTOBJ_UNLOCK(object); \
	} while(0)

#define ASTOBJ_INIT(object) \
	do { \
		ast_mutex_init(&(object)->_lock); \
		object->name[0] = '\0'; \
		object->refcount = 1; \
	} while(0)

/* Containers for objects -- current implementation is linked lists, but
   should be able to be converted to hashes relatively easily */

#define ASTOBJ_CONTAINER_RDLOCK(container) ast_mutex_lock(&(container)->_lock)
#define ASTOBJ_CONTAINER_WRLOCK(container) ast_mutex_lock(&(container)->_lock)
#define ASTOBJ_CONTAINER_UNLOCK(container) ast_mutex_unlock(&(container)->_lock)

#ifdef ASTOBJ_CONTAINER_HASHMODEL
#error "Hash model for object containers not yet implemented!"
#else
/* Linked lists */
#define ASTOBJ_CONTAINER_COMPONENTS_NOLOCK_FULL(type,hashes,buckets) \
	type *head

#define ASTOBJ_CONTAINER_INIT_FULL(container,hashes,buckets) \
	do { \
		ast_mutex_init(&(container)->_lock); \
	} while(0)
	
#define ASTOBJ_CONTAINER_DESTROY_FULL(container,hashes,buckets) \
	do { \
		ast_mutex_destroy(&(container)->_lock); \
	} while(0)

#define ASTOBJ_CONTAINER_TRAVERSE(container,continue,eval) \
	do { \
		typeof((container)->head) iterator; \
		typeof((container)->head) next; \
		ASTOBJ_CONTAINER_RDLOCK(container); \
		next = (container)->head; \
		while((continue) && (iterator = next)) { \
			next = iterator->next[0]; \
			eval; \
		} \
		ASTOBJ_CONTAINER_UNLOCK(container); \
	} while(0)

#define ASTOBJ_CONTAINER_FIND(container,namestr) \
	({ \
		typeof((container)->head) found = NULL; \
		ASTOBJ_CONTAINER_TRAVERSE(container, !found, do { \
			if (!(strcasecmp(iterator->name, (namestr)))) \
				found = ASTOBJ_REF(iterator); \
		} while (0)); \
		found; \
	})

#define ASTOBJ_CONTAINER_FIND_FULL(container,data,field,hashfunc,hashoffset,comparefunc) \
	({ \
		typeof((container)->head) found = NULL; \
		ASTOBJ_CONTAINER_TRAVERSE(container, !found, do { \
			ASTOBJ_RDLOCK(iterator); \
			if (!(comparefunc(iterator->field, (data)))) { \
				found = ASTOBJ_REF(iterator); \
			} \
			ASTOBJ_UNLOCK(iterator); \
		} while (0)); \
		found; \
	})

#define ASTOBJ_CONTAINER_DESTROYALL(container,destructor) \
	do { \
		typeof((container)->head) iterator; \
		ASTOBJ_CONTAINER_WRLOCK(container); \
		while((iterator = (container)->head)) { \
			(container)->head = (iterator)->next[0]; \
			ASTOBJ_UNREF(iterator,destructor); \
		} \
		ASTOBJ_CONTAINER_UNLOCK(container); \
	} while(0)

#define ASTOBJ_CONTAINER_UNLINK(container,obj) \
	({ \
		typeof((container)->head) found = NULL; \
		typeof((container)->head) prev = NULL; \
		ASTOBJ_CONTAINER_TRAVERSE(container, !found, do { \
			if (iterator == obj) { \
				found = iterator; \
				found->next[0] = NULL; \
				ASTOBJ_CONTAINER_WRLOCK(container); \
				if (prev) \
					prev->next[0] = next; \
				else \
					(container)->head = next; \
				ASTOBJ_CONTAINER_UNLOCK(container); \
			} \
			prev = iterator; \
		} while (0)); \
		found; \
	})

#define ASTOBJ_CONTAINER_FIND_UNLINK(container,namestr) \
	({ \
		typeof((container)->head) found = NULL; \
		typeof((container)->head) prev = NULL; \
		ASTOBJ_CONTAINER_TRAVERSE(container, !found, do { \
			if (!(strcasecmp(iterator->name, (namestr)))) { \
				found = iterator; \
				found->next[0] = NULL; \
				ASTOBJ_CONTAINER_WRLOCK(container); \
				if (prev) \
					prev->next[0] = next; \
				else \
					(container)->head = next; \
				ASTOBJ_CONTAINER_UNLOCK(container); \
			} \
			prev = iterator; \
		} while (0)); \
		found; \
	})

#define ASTOBJ_CONTAINER_FIND_UNLINK_FULL(container,data,field,hashfunc,hashoffset,comparefunc) \
	({ \
		typeof((container)->head) found = NULL; \
		typeof((container)->head) prev = NULL; \
		ASTOBJ_CONTAINER_TRAVERSE(container, !found, do { \
			ASTOBJ_RDLOCK(iterator); \
			if (!(comparefunc(iterator->field, (data)))) { \
				found = iterator; \
				found->next[0] = NULL; \
				ASTOBJ_CONTAINER_WRLOCK(container); \
				if (prev) \
					prev->next[0] = next; \
				else \
					(container)->head = next; \
				ASTOBJ_CONTAINER_UNLOCK(container); \
			} \
			ASTOBJ_UNLOCK(iterator); \
			prev = iterator; \
		} while (0)); \
		found; \
	})

#define ASTOBJ_CONTAINER_PRUNE_MARKED(container,destructor) \
	do { \
		typeof((container)->head) prev = NULL; \
		ASTOBJ_CONTAINER_TRAVERSE(container, 1, do { \
			ASTOBJ_RDLOCK(iterator); \
			if (iterator->objflags & ASTOBJ_FLAG_MARKED) { \
				ASTOBJ_CONTAINER_WRLOCK(container); \
				if (prev) \
					prev->next[0] = next; \
				else \
					(container)->head = next; \
				ASTOBJ_CONTAINER_UNLOCK(container); \
				ASTOBJ_UNLOCK(iterator); \
				ASTOBJ_UNREF(iterator,destructor); \
				continue; \
			} \
			ASTOBJ_UNLOCK(iterator); \
			prev = iterator; \
		} while (0)); \
	} while(0)

#define ASTOBJ_CONTAINER_LINK_FULL(container,newobj,data,field,hashfunc,hashoffset,comparefunc) \
	do { \
		ASTOBJ_CONTAINER_WRLOCK(container); \
		(newobj)->next[0] = (container)->head; \
		(container)->head = ASTOBJ_REF(newobj); \
		ASTOBJ_CONTAINER_UNLOCK(container); \
	} while(0)

#endif /* List model */

/* Common to hash and linked list models */
#define ASTOBJ_CONTAINER_COMPONENTS_NOLOCK(type) \
	ASTOBJ_CONTAINER_COMPONENTS_NOLOCK_FULL(type,1,ASTOBJ_DEFAULT_BUCKETS)

#define ASTOBJ_CONTAINER_COMPONENTS(type) \
	ast_mutex_t _lock; \
	ASTOBJ_CONTAINER_COMPONENTS_NOLOCK(type)

#define ASTOBJ_CONTAINER_INIT(container) \
	ASTOBJ_CONTAINER_INIT_FULL(container,1,ASTOBJ_DEFAULT_BUCKETS)

#define ASTOBJ_CONTAINER_DESTROY(container) \
	ASTOBJ_CONTAINER_DESTROY_FULL(container,1,ASTOBJ_DEFAULT_BUCKETS)

#define ASTOBJ_CONTAINER_LINK(container,newobj) \
	ASTOBJ_CONTAINER_LINK_FULL(container,newobj,(newobj)->name,name,ASTOBJ_DEFAULT_HASH,0,strcasecmp)

#define ASTOBJ_CONTAINER_MARKALL(container) \
	ASTOBJ_CONTAINER_TRAVERSE(container, 1, ASTOBJ_MARK(iterator))

#define ASTOBJ_CONTAINER_UNMARKALL(container) \
	ASTOBJ_CONTAINER_TRAVERSE(container, 1, ASTOBJ_UNMARK(iterator))

#define ASTOBJ_DUMP(s,slen,obj) \
	snprintf((s),(slen),"name: %s\nobjflags: %d\nrefcount: %d\n\n", (obj)->name, (obj)->objflags, (obj)->refcount);

#define ASTOBJ_CONTAINER_DUMP(fd,s,slen,container) \
	ASTOBJ_CONTAINER_TRAVERSE(container, 1, do { ASTOBJ_DUMP(s,slen,iterator); ast_cli(fd, s); } while(0))

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
