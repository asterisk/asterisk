/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Object Model for Asterisk
 * 
 * Copyright (C) 2004-2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_ASTOBJ_H
#define _ASTERISK_ASTOBJ_H

#include <string.h>

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

#define ASTOBJ_FLAG_DELME 	(1 << 0)		/* Object has been deleted, remove on last unref */
#define ASTOBJ_FLAG_MARKED	(1 << 1)		/* Object has been marked for possible deletion */

/* C++ is simply a syntactic crutch for those who cannot think for themselves
   in an object oriented way. */

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
	ast_mutex_t lock; \
	ASTOBJ_COMPONENTS_NOLOCK(type)
	
#define ASTOBJ_COMPONENTS_FULL(type,namelen,hashes) \
	ast_mutex_t lock; \
	ASTOBJ_COMPONENTS_NOLOCK_FULL(type,namelen,hashes)

#define ASTOBJ_REF(object) \
	do { \
		ast_mutex_lock(&(object)->lock); \
		(object)->refcount++; \
		ast_mutex_unlock(&(object)->lock); \
	} while(0)
	
#define ASTOBJ_UNREF(object,destructor) \
	do { \
		int destroyme; \
		ast_mutex_lock(&(object)->lock); \
		if ((object)->refcount > 0) \
			(object)->refcount--; \
		else \
			ast_log(LOG_WARNING, "Unreferencing unreferenced (object)!\n"); \
		destroyme = (!(object)->refcount) && ((object)->objflags & ASTOBJ_FLAG_DELME); \
		ast_mutex_unlock(&(object)->lock); \
		if (destroyme) \
			destructor((object)); \
	} while(0)

#define ASTOBJ_MARK(object) \
	(object)->objflags |= ASTOBJ_FLAG_MARKED;
	
#define ASTOBJ_UNMARK(object) \
	(object)->objflags &= ~ASTOBJ_FLAG_MARKED;

#define ASTOBJ_DESTROY(object,destructor) \
	do { \
		int destroyme; \
		ast_mutex_lock(&(object)->lock); \
		destroyme = (!(object)->refcount); \
		(object)->objflags |= ASTOBJ_FLAG_DELME; \
		ast_mutex_unlock(&(object)->lock); \
		if (destroyme) \
			destructor((object)); \
	} while(0)
	
#define ASTOBJ_INIT(object) \
	do { \
		ast_mutex_init(&(object)->lock); \
		object->name[0] = '\0'; \
	} while(0)

/* Containers for objects -- current implementation is linked lists, but
   should be able to be converted to hashes relatively easily */

#ifdef ASTOBJ_CONTAINER_HASHMODEL
#error "Hash model for object containers not yet implemented!"
#else
/* Linked lists */
#define ASTOBJ_CONTAINER_COMPONENTS_NOLOCK_FULL(type,hashes,buckets) \
	type *head

#define ASTOBJ_CONTAINER_INIT_FULL(container,hashes,buckets) \
	do { \
		ast_mutex_init(&(container)->lock); \
	} while(0)
	
#define ASTOBJ_CONTAINER_RELEASE_FULL(container,hashes,buckets) \
	do { \
		ast_mutex_destroy(&(container)->lock); \
	} while(0)

#define ASTOBJ_CONTAINER_TRAVERSE(container,iterator,eval) \
	do { \
		ast_mutex_lock(&((container)->lock)); \
		(iterator) = (container)->head; \
		while((iterator)) { \
			ast_mutex_lock(&(iterator)->lock); \
			eval; \
			ast_mutex_unlock(&(iterator)->lock); \
			(iterator) = (iterator)->next[0]; \
		} \
		ast_mutex_unlock(&(container)->lock); \
	} while(0)

#define ASTOBJ_CONTAINER_FIND_FULL(container,iterator,data,field,hashfunc,hashoffset,comparefunc) \
	do { \
		int res; \
		ast_mutex_lock(&((container)->lock)); \
		(iterator) = (container)->head; \
		while((iterator)) { \
			ast_mutex_lock(&(iterator)->lock); \
			res = (comparefunc((iterator)->field,(data))); \
			if (!res) \
				ASTOBJ_REF((iterator)); \
			ast_mutex_unlock(&(iterator)->lock); \
			if (!res) \
				break; \
			(iterator) = (iterator)->next[0]; \
		} \
		ast_mutex_unlock(&(container)->lock); \
	} while(0)

#define ASTOBJ_CONTAINER_DESTROYALL(container,iterator,destructor) \
	do { \
		ast_mutex_lock(&((container)->lock)); \
		(iterator) = (container)->head; \
		while((iterator)) { \
			(container)->head = (iterator)->next[0]; \
			ASTOBJ_DESTROY(iterator,destructor); \
			(iterator) = (container)->head; \
		} \
		ast_mutex_unlock(&(container)->lock); \
	} while(0)

#define ASTOBJ_CONTAINER_UNLINK_FULL(container,iterator,data,field,hashfunc,hashoffset,comparefunc) \
	do { \
		int res=-1; \
		ast_mutex_lock(&((container)->lock)); \
		(iterator) = (container)->head; \
		if ((iterator)) { \
			ast_mutex_lock(&(iterator)->lock); \
			res = (comparefunc((iterator)->field,(data))); \
			if (!res && ((iterator)->refcount < 1)) \
				ast_log(LOG_WARNING, "Unlink called with refcount < 1!\n"); \
			ast_mutex_unlock(&(iterator)->lock); \
			if (!res) \
				(container)->head = (iterator)->next[0]; \
			else while((iterator)->next[0]) { \
				ast_mutex_lock(&(iterator)->next[0]->lock); \
				res = (comparefunc((iterator)->next[0]->field,(data))); \
				if (!res && ((iterator)->next[0]->refcount < 1)) \
					ast_log(LOG_WARNING, "Unlink called with refcount < 1!\n"); \
				ast_mutex_unlock(&(iterator)->next[0]->lock); \
				if (!res) { \
					(iterator)->next[0] = (iterator)->next[0]->next[0]; \
					break; \
				} \
				(iterator) = (iterator)->next[0]; \
			} \
		} \
		ast_mutex_unlock(&(container)->lock); \
	} while(0)

#define ASTOBJ_CONTAINER_FIND_UNLINK_FULL(container,reiterator,iterator,data,field,hashfunc,hashoffset,comparefunc) \
	do { \
		int res=-1; \
		(reiterator) = NULL; \
		ast_mutex_lock(&((container)->lock)); \
		(iterator) = (container)->head; \
		if ((iterator)) { \
			ast_mutex_lock(&(iterator)->lock); \
			res = (comparefunc((iterator)->field,(data))); \
			if (!res && ((iterator)->refcount < 1)) \
				ast_log(LOG_WARNING, "Unlink called with refcount < 1!\n"); \
			ast_mutex_unlock(&(iterator)->lock); \
			if (!res) {\
				(reiterator) = (container)->head; \
				(container)->head = (iterator)->next[0]; \
			} else while((iterator)->next[0]) { \
				ast_mutex_lock(&(iterator)->next[0]->lock); \
				res = (comparefunc((iterator)->next[0]->field,(data))); \
				ast_mutex_unlock(&(iterator)->next[0]->lock); \
				if (!res) { \
					(reiterator) = (iterator)->next[0]; \
					(iterator)->next[0] = (iterator)->next[0]->next[0]; \
					break; \
				} \
				(iterator) = (iterator)->next[0]; \
			} \
		} \
		ast_mutex_unlock(&(container)->lock); \
	} while(0)

#define ASTOBJ_CONTAINER_PRUNE_MARKED(container,previ,nexti,iterator,destructor) \
	do { \
		(previ) = NULL; \
		ast_mutex_lock(&((container)->lock)); \
		(iterator) = (container)->head; \
		while((iterator)) { \
			ast_mutex_lock(&(iterator)->lock); \
			(nexti) = (iterator)->next[0]; \
			if ((iterator)->objflags & ASTOBJ_FLAG_MARKED) { \
				if ((previ)) \
					(previ)->next[0] = (nexti); \
				else \
					(container)->head = (nexti); \
				ast_mutex_unlock(&(iterator)->lock); \
				ASTOBJ_DESTROY(iterator,destructor); \
			} else { \
				(previ) = (iterator); \
				ast_mutex_unlock(&(iterator)->lock); \
			} \
			(iterator) = (nexti); \
		} \
		ast_mutex_unlock(&(container)->lock); \
	} while(0)

#define ASTOBJ_CONTAINER_LINK_FULL(container,newobj,data,field,hashfunc,hashoffset,comparefunc) \
	do { \
		ASTOBJ_REF(newobj); \
		ast_mutex_lock(&(container)->lock); \
		(newobj)->next[0] = (container)->head; \
		(container)->head = (newobj); \
		ast_mutex_unlock(&(container)->lock); \
	} while(0)

#endif /* Hash model */

/* Common to hash and linked list models */
#define ASTOBJ_CONTAINER_COMPONENTS_NOLOCK(type) \
	ASTOBJ_CONTAINER_COMPONENTS_NOLOCK_FULL(type,1,ASTOBJ_DEFAULT_BUCKETS)

#define ASTOBJ_CONTAINER_COMPONENTS(type) \
	ast_mutex_t lock; \
	ASTOBJ_CONTAINER_COMPONENTS_NOLOCK(type)

#define ASTOBJ_CONTAINER_INIT(container) \
	ASTOBJ_CONTAINER_INIT_FULL(container,1,ASTOBJ_DEFAULT_BUCKETS)

#define ASTOBJ_CONTAINER_RELEASE(container) \
	ASTOBJ_CONTAINER_RELEASE_FULL(container,1,ASTOBJ_DEFAULT_BUCKETS)

#define ASTOBJ_CONTAINER_FIND(container,iterator,namestr) \
	ASTOBJ_CONTAINER_FIND_FULL(container,iterator,namestr,name,ASTOBJ_DEFAULT_HASH,0,strcasecmp)

#define ASTOBJ_CONTAINER_FIND_UNLINK(container,reiterator,iterator,namestr) \
	ASTOBJ_CONTAINER_FIND_UNLINK_FULL(container,reiterator,iterator,namestr,name,ASTOBJ_DEFAULT_HASH,0,strcasecmp)

#define ASTOBJ_CONTAINER_UNLINK(container,iterator,namestr) \
	ASTOBJ_CONTAINER_UNLINK_FULL(container,iterator,namestr,name,ASTOBJ_DEFAULT_HASH,0,strcasecmp)

#define ASTOBJ_CONTAINER_LINK(container,newobj) \
	ASTOBJ_CONTAINER_LINK_FULL(container,newobj,(newobj)->name,name,ASTOBJ_DEFAULT_HASH,0,strcasecmp)

#define ASTOBJ_CONTAINER_MARKALL(container,iterator) \
	ASTOBJ_CONTAINER_TRAVERSE(container,iterator,(iterator)->objflags |= ASTOBJ_FLAG_MARKED)

#define ASTOBJ_CONTAINER_UNMARKALL(container,iterator) \
	ASTOBJ_CONTAINER_TRAVERSE(container,iterator,(iterator)->objflags &= ~ASTOBJ_FLAG_MARKED)

#define ASTOBJ_DUMP(s,slen,obj) \
	snprintf((s),(slen),"name: %s\nobjflags: %d\nrefcount: %d\n\n", (obj)->name, (obj)->objflags, (obj)->refcount);

#define ASTOBJ_CONTAINER_DUMP(fd,s,slen,container,iterator) \
	ASTOBJ_CONTAINER_TRAVERSE(container,iterator,do { ASTOBJ_DUMP(s,slen,iterator); ast_cli(fd, s); } while(0))

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif



#endif
