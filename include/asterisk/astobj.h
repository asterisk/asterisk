/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * Object Model for Asterisk
 */

#ifndef _ASTERISK_ASTOBJ_H
#define _ASTERISK_ASTOBJ_H

#include <string.h>

#include "asterisk/lock.h"
#include "asterisk/compiler.h"

/*! \file
 * \brief A set of macros implementing objects and containers.
 * Macros are used for maximum performance, to support multiple inheritance,
 * and to be easily integrated into existing structures without additional
 * malloc calls, etc.
 *
 * These macros expect to operate on two different object types, ASTOBJs and
 * ASTOBJ_CONTAINERs.  These are not actual types, as any struct can be
 * converted into an ASTOBJ compatible object or container using the supplied
 * macros.
 *
 * <b>Sample Usage:</b>
 * \code
 * struct sample_object {
 *    ASTOBJ_COMPONENTS(struct sample_object);
 * };
 *
 * struct sample_container {
 *    ASTOBJ_CONTAINER_COMPONENTS(struct sample_object);
 * } super_container;
 *
 * void sample_object_destroy(struct sample_object *obj)
 * {
 *    free(obj);
 * }
 *
 * int init_stuff()
 * {
 *    struct sample_object *obj1;
 *    struct sample_object *found_obj;
 *
 *    obj1 = malloc(sizeof(struct sample_object));
 *
 *    ASTOBJ_CONTAINER_INIT(&super_container);
 *
 *    ASTOBJ_INIT(obj1);
 *    ASTOBJ_WRLOCK(obj1);
 *    ast_copy_string(obj1->name, "obj1", sizeof(obj1->name));
 *    ASTOBJ_UNLOCK(obj1);
 *
 *    ASTOBJ_CONTAINER_LINK(&super_container, obj1);
 *
 *    found_obj = ASTOBJ_CONTAINER_FIND(&super_container, "obj1");
 *
 *    if(found_obj) {
 *       printf("Found object: %s", found_obj->name); 
 *       ASTOBJ_UNREF(found_obj,sample_object_destroy);
 *    }
 *
 *    ASTOBJ_CONTAINER_DESTROYALL(&super_container,sample_object_destroy);
 *    ASTOBJ_CONTAINER_DESTROY(&super_container);
 * 
 *    return 0;
 * }
 * \endcode
 */

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define ASTOBJ_DEFAULT_NAMELEN 	80
#define ASTOBJ_DEFAULT_BUCKETS	256
#define ASTOBJ_DEFAULT_HASH		ast_strhash

#define ASTOBJ_FLAG_MARKED	(1 << 0)		/* Object has been marked for future operation */

/* C++ is simply a syntactic crutch for those who cannot think for themselves
   in an object oriented way. */

/*! \brief Lock an ASTOBJ for reading.
 */
#define ASTOBJ_RDLOCK(object) ast_mutex_lock(&(object)->_lock)

/*! \brief Lock an ASTOBJ for writing.
 */
#define ASTOBJ_WRLOCK(object) ast_mutex_lock(&(object)->_lock)

/*! \brief Unlock a locked object. */
#define ASTOBJ_UNLOCK(object) ast_mutex_unlock(&(object)->_lock)

#ifdef ASTOBJ_CONTAINER_HASHMODEL 
#define __ASTOBJ_HASH(type,hashes) \
	type *next[hashes] 
#else 
#define __ASTOBJ_HASH(type,hashes) \
	type *next[1] 
#endif	

/*! \brief Add ASTOBJ components to a struct (without locking support).
 *
 * \param type The datatype of the object.
 * \param namelen The length to make the name char array.
 * \param hashes The number of containers the object can be present in.
 *
 * This macro adds components to a struct to make it an ASTOBJ.  This macro
 * differs from ASTOBJ_COMPONENTS_FULL in that it does not create a mutex for
 * locking.
 *
 * <b>Sample Usage:</b>
 * \code
 * struct sample_struct {
 *    ASTOBJ_COMPONENTS_NOLOCK_FULL(struct sample_struct,1,1);
 * };
 * \endcode
 */
#define ASTOBJ_COMPONENTS_NOLOCK_FULL(type,namelen,hashes) \
	char name[namelen]; \
	unsigned int refcount; \
	unsigned int objflags; \
	__ASTOBJ_HASH(type,hashes)
	
/*! \brief Add ASTOBJ components to a struct (without locking support).
 *
 * \param type The datatype of the object.
 *
 * This macro works like #ASTOBJ_COMPONENTS_NOLOCK_FULL() except it only accepts a
 * type and uses default values for namelen and hashes.
 * 
 * <b>Sample Usage:</b>
 * \code
 * struct sample_struct_componets {
 *    ASTOBJ_COMPONENTS_NOLOCK(struct sample_struct);
 * };
 * \endcode
 */
#define ASTOBJ_COMPONENTS_NOLOCK(type) \
	ASTOBJ_COMPONENTS_NOLOCK_FULL(type,ASTOBJ_DEFAULT_NAMELEN,1)

/*! \brief Add ASTOBJ components to a struct (with locking support).
 *
 * \param type The datatype of the object.
 *
 * This macro works like #ASTOBJ_COMPONENTS_NOLOCK() except it includes locking
 * support.
 *
 * <b>Sample Usage:</b>
 * \code
 * struct sample_struct {
 *    ASTOBJ_COMPONENTS(struct sample_struct);
 * };
 * \endcode
 */
#define ASTOBJ_COMPONENTS(type) \
	ASTOBJ_COMPONENTS_NOLOCK(type); \
	ast_mutex_t _lock; 
	
/*! \brief Add ASTOBJ components to a struct (with locking support).
 *
 * \param type The datatype of the object.
 * \param namelen The length to make the name char array.
 * \param hashes The number of containers the object can be present in.
 *
 * This macro adds components to a struct to make it an ASTOBJ and includes
 * support for locking.
 *
 * <b>Sample Usage:</b>
 * \code
 * struct sample_struct {
 *    ASTOBJ_COMPONENTS_FULL(struct sample_struct,1,1);
 * };
 * \endcode
 */
#define ASTOBJ_COMPONENTS_FULL(type,namelen,hashes) \
	ASTOBJ_COMPONENTS_NOLOCK_FULL(type,namelen,hashes); \
	ast_mutex_t _lock; 

/*! \brief Increment an object reference count.
 * \param object A pointer to the object to operate on.
 * \return The object.
 */
#define ASTOBJ_REF(object) \
	({ \
		ASTOBJ_WRLOCK(object); \
		(object)->refcount++; \
		ASTOBJ_UNLOCK(object); \
		(object); \
	})
	
/*! \brief Decrement the reference count on an object.
 *
 * \param object A pointer the object to operate on.
 * \param destructor The destructor to call if the object is no longer referenced.  It will be passed the pointer as an argument.
 *
 * This macro unreferences an object and calls the specfied destructor if the
 * object is no longer referenced.  The destructor should free the object if it
 * was dynamically allocated.
 */
#define ASTOBJ_UNREF(object,destructor) \
	do { \
		int newcount = 0; \
		ASTOBJ_WRLOCK(object); \
		if (__builtin_expect((object)->refcount > 0, 1)) \
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

/*! \brief Mark an ASTOBJ by adding the #ASTOBJ_FLAG_MARKED flag to its objflags mask. 
 * \param object A pointer to the object to operate on.
 *
 * This macro "marks" an object.  Marked objects can later be unlinked from a container using
 * #ASTOBJ_CONTAINER_PRUNE_MARKED().
 * 
 */
#define ASTOBJ_MARK(object) \
	do { \
		ASTOBJ_WRLOCK(object); \
		(object)->objflags |= ASTOBJ_FLAG_MARKED; \
		ASTOBJ_UNLOCK(object); \
	} while(0)
	
/*! \brief Unmark an ASTOBJ by subtracting the #ASTOBJ_FLAG_MARKED flag from its objflags mask.
 * \param object A pointer to the object to operate on.
 */
#define ASTOBJ_UNMARK(object) \
	do { \
		ASTOBJ_WRLOCK(object); \
		(object)->objflags &= ~ASTOBJ_FLAG_MARKED; \
		ASTOBJ_UNLOCK(object); \
	} while(0)

/*! \brief Initialize an object.
 * \param object A pointer to the object to operate on.
 *
 * \note This should only be used on objects that support locking (objects
 * created with #ASTOBJ_COMPONENTS() or #ASTOBJ_COMPONENTS_FULL())
 */
#define ASTOBJ_INIT(object) \
	do { \
		ast_mutex_init(&(object)->_lock); \
		object->name[0] = '\0'; \
		object->refcount = 1; \
	} while(0)

/* Containers for objects -- current implementation is linked lists, but
   should be able to be converted to hashes relatively easily */

/*! \brief Lock an ASTOBJ_CONTAINER for reading.
 */
#define ASTOBJ_CONTAINER_RDLOCK(container) ast_mutex_lock(&(container)->_lock)

/*! \brief Lock an ASTOBJ_CONTAINER for writing. 
 */
#define ASTOBJ_CONTAINER_WRLOCK(container) ast_mutex_lock(&(container)->_lock)

/*! \brief Unlock an ASTOBJ_CONTAINER. */
#define ASTOBJ_CONTAINER_UNLOCK(container) ast_mutex_unlock(&(container)->_lock)

#ifdef ASTOBJ_CONTAINER_HASHMODEL
#error "Hash model for object containers not yet implemented!"
#else
/* Linked lists */

/*! \brief Create a container for ASTOBJs (without locking support).
 *
 * \param type The type of objects the container will hold.
 * \param hashes Currently unused.
 * \param buckets Currently unused.
 *
 * This macro is used to create a container for ASTOBJs without locking
 * support.
 *
 * <b>Sample Usage:</b>
 * \code
 * struct sample_struct_nolock_container {
 *    ASTOBJ_CONTAINER_COMPONENTS_NOLOCK_FULL(struct sample_struct,1,1);
 * };
 * \endcode
 */
#define ASTOBJ_CONTAINER_COMPONENTS_NOLOCK_FULL(type,hashes,buckets) \
	type *head

/*! \brief Initialize a container.
 *
 * \param container A pointer to the container to initialize.
 * \param hashes Currently unused.
 * \param buckets Currently unused.
 *
 * This macro initializes a container.  It should only be used on containers
 * that support locking.
 * 
 * <b>Sample Usage:</b>
 * \code
 * struct sample_struct_container {
 *    ASTOBJ_CONTAINER_COMPONENTS_FULL(struct sample_struct,1,1);
 * } container;
 *
 * int func()
 * {
 *    ASTOBJ_CONTAINER_INIT_FULL(&container,1,1);
 * }
 * \endcode
 */
#define ASTOBJ_CONTAINER_INIT_FULL(container,hashes,buckets) \
	do { \
		ast_mutex_init(&(container)->_lock); \
	} while(0)
	
/*! \brief Destroy a container.
 *
 * \param container A pointer to the container to destroy.
 * \param hashes Currently unused.
 * \param buckets Currently unused.
 *
 * This macro frees up resources used by a container.  It does not operate on
 * the objects in the container.  To unlink the objects from the container use
 * #ASTOBJ_CONTAINER_DESTROYALL().
 *
 * \note This macro should only be used on containers with locking support.
 */
#define ASTOBJ_CONTAINER_DESTROY_FULL(container,hashes,buckets) \
	do { \
		ast_mutex_destroy(&(container)->_lock); \
	} while(0)

/*! \brief Iterate through the objects in a container.
 *
 * \param container A pointer to the container to traverse.
 * \param continue A condition to allow the traversal to continue.
 * \param eval A statement to evaluate in the iteration loop.
 *
 * This is macro is a little complicated, but it may help to think of it as a
 * loop.  Basically it iterates through the specfied containter as long as the
 * condition is met.  Two variables, iterator and next, are provided for use in
 * your \p eval statement.  See the sample code for an example.
 *
 * <b>Sample Usage:</b>
 * \code
 * ASTOBJ_CONTAINER_TRAVERSE(&sample_container,1, {
 *    ASTOBJ_RDLOCK(iterator);
 *    printf("Currently iterating over '%s'\n", iterator->name);
 *    ASTOBJ_UNLOCK(iterator);
 * } );
 * \endcode
 *
 * \code
 * ASTOBJ_CONTAINER_TRAVERSE(&sample_container,1, sample_func(iterator));
 * \endcode
 */
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

/*! \brief Find an object in a container.
 *
 * \param container A pointer to the container to search.
 * \param namestr The name to search for.
 *
 * Use this function to find an object with the specfied name in a container.
 *
 * \note When the returned object is no longer in use, #ASTOBJ_UNREF() should
 * be used to free the additional reference created by this macro.
 *
 * \return A new reference to the object located or NULL if nothing is found.
 */
#define ASTOBJ_CONTAINER_FIND(container,namestr) \
	({ \
		typeof((container)->head) found = NULL; \
		ASTOBJ_CONTAINER_TRAVERSE(container, !found, do { \
			if (!(strcasecmp(iterator->name, (namestr)))) \
				found = ASTOBJ_REF(iterator); \
		} while (0)); \
		found; \
	})

/*! \brief Find an object in a container.
 * 
 * \param container A pointer to the container to search.
 * \param data The data to search for.
 * \param field The field/member of the container's objects to search.
 * \param hashfunc The hash function to use, currently not implemented.
 * \param hashoffset The hash offset to use, currently not implemented.
 * \param comparefunc The function used to compare the field and data values.
 *
 * This macro iterates through a container passing the specified field and data
 * elements to the specified comparefunc.  The function should return 0 when a match is found.
 * 
 * \note When the returned object is no longer in use, #ASTOBJ_UNREF() should
 * be used to free the additional reference created by this macro.
 *
 * \return A pointer to the object located or NULL if nothing is found.
 */
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

/*! \brief Empty a container.
 *
 * \param container A pointer to the container to operate on.
 * \param destructor A destructor function to call on each object.
 *
 * This macro loops through a container removing all the items from it using
 * #ASTOBJ_UNREF().  This does not destroy the container itself, use
 * #ASTOBJ_CONTAINER_DESTROY() for that.
 *
 * \note If any object in the container is only referenced by the container,
 * the destructor will be called for that object once it has been removed.
 */
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

/*! \brief Remove an object from a container.
 *
 * \param container A pointer to the container to operate on.
 * \param obj A pointer to the object to remove.
 *
 * This macro iterates through a container and removes the specfied object if
 * it exists in the container.
 *
 * \note This macro does not destroy any objects, it simply unlinks
 * them from the list.  No destructors are called.
 *
 * \return The container's reference to the removed object or NULL if no
 * matching object was found.
 */
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

/*! \brief Find and remove an object from a container.
 * 
 * \param container A pointer to the container to operate on.
 * \param namestr The name of the object to remove.
 *
 * This macro iterates through a container and removes the first object with
 * the specfied name from the container.
 *
 * \note This macro does not destroy any objects, it simply unlinks
 * them.  No destructors are called.
 *
 * \return The container's reference to the removed object or NULL if no
 * matching object was found.
 */
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

/*! \brief Find and remove an object in a container.
 * 
 * \param container A pointer to the container to search.
 * \param data The data to search for.
 * \param field The field/member of the container's objects to search.
 * \param hashfunc The hash function to use, currently not implemented.
 * \param hashoffset The hash offset to use, currently not implemented.
 * \param comparefunc The function used to compare the field and data values.
 *
 * This macro iterates through a container passing the specified field and data
 * elements to the specified comparefunc.  The function should return 0 when a match is found.
 * If a match is found it is removed from the list. 
 *
 * \note This macro does not destroy any objects, it simply unlinks
 * them.  No destructors are called.
 *
 * \return The container's reference to the removed object or NULL if no match
 * was found.
 */
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

/*! \brief Prune marked objects from a container.
 *
 * \param container A pointer to the container to prune.
 * \param destructor A destructor function to call on each marked object.
 * 
 * This macro iterates through the specfied container and prunes any marked
 * objects executing the specfied destructor if necessary.
 */
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

/*! \brief Add an object to a container.
 *
 * \param container A pointer to the container to operate on.
 * \param newobj A pointer to the object to be added.
 * \param data Currently unused.
 * \param field Currently unused.
 * \param hashfunc Currently unused.
 * \param hashoffset Currently unused.
 * \param comparefunc Currently unused.
 *
 * Currently this function adds an object to the head of the list.  One day it
 * will support adding objects atthe position specified using the various
 * options this macro offers.
 */
#define ASTOBJ_CONTAINER_LINK_FULL(container,newobj,data,field,hashfunc,hashoffset,comparefunc) \
	do { \
		ASTOBJ_CONTAINER_WRLOCK(container); \
		(newobj)->next[0] = (container)->head; \
		(container)->head = ASTOBJ_REF(newobj); \
		ASTOBJ_CONTAINER_UNLOCK(container); \
	} while(0)

#endif /* List model */

/* Common to hash and linked list models */

/*! \brief Create a container for ASTOBJs (without locking support).
 *
 * \param type The type of objects the container will hold.
 *
 * This macro is used to create a container for ASTOBJs without locking
 * support.
 *
 * <b>Sample Usage:</b>
 * \code
 * struct sample_struct_nolock_container {
 *    ASTOBJ_CONTAINER_COMPONENTS_NOLOCK(struct sample_struct);
 * };
 * \endcode
 */
#define ASTOBJ_CONTAINER_COMPONENTS_NOLOCK(type) \
	ASTOBJ_CONTAINER_COMPONENTS_NOLOCK_FULL(type,1,ASTOBJ_DEFAULT_BUCKETS)


/*! \brief Create a container for ASTOBJs (with locking support).
 *
 * \param type The type of objects the container will hold.
 *
 * This macro is used to create a container for ASTOBJs with locking support.
 *
 * <b>Sample Usage:</b>
 * \code
 * struct sample_struct_container {
 *    ASTOBJ_CONTAINER_COMPONENTS(struct sample_struct);
 * };
 * \endcode
 */
#define ASTOBJ_CONTAINER_COMPONENTS(type) \
	ast_mutex_t _lock; \
	ASTOBJ_CONTAINER_COMPONENTS_NOLOCK(type)

/*! \brief Initialize a container.
 *
 * \param container A pointer to the container to initialize.
 *
 * This macro initializes a container.  It should only be used on containers
 * that support locking.
 * 
 * <b>Sample Usage:</b>
 * \code
 * struct sample_struct_container {
 *    ASTOBJ_CONTAINER_COMPONENTS(struct sample_struct);
 * } container;
 *
 * int func()
 * {
 *    ASTOBJ_CONTAINER_INIT(&container);
 * }
 * \endcode
 */
#define ASTOBJ_CONTAINER_INIT(container) \
	ASTOBJ_CONTAINER_INIT_FULL(container,1,ASTOBJ_DEFAULT_BUCKETS)

/*! \brief Destroy a container.
 *
 * \param container A pointer to the container to destory.
 *
 * This macro frees up resources used by a container.  It does not operate on
 * the objects in the container.  To unlink the objects from the container use
 * #ASTOBJ_CONTAINER_DESTROYALL().
 *
 * \note This macro should only be used on containers with locking support.
 */
#define ASTOBJ_CONTAINER_DESTROY(container) \
	ASTOBJ_CONTAINER_DESTROY_FULL(container,1,ASTOBJ_DEFAULT_BUCKETS)

/*! \brief Add an object to a container.
 *
 * \param container A pointer to the container to operate on.
 * \param newobj A pointer to the object to be added.
 *
 * Currently this macro adds an object to the head of a container.  One day it
 * should add an object in alphabetical order.
 */
#define ASTOBJ_CONTAINER_LINK(container,newobj) \
	ASTOBJ_CONTAINER_LINK_FULL(container,newobj,(newobj)->name,name,ASTOBJ_DEFAULT_HASH,0,strcasecmp)

/*! \brief Mark all the objects in a container.
 * \param container A pointer to the container to operate on.
 */
#define ASTOBJ_CONTAINER_MARKALL(container) \
	ASTOBJ_CONTAINER_TRAVERSE(container, 1, ASTOBJ_MARK(iterator))

/*! \brief Unmark all the objects in a container.
 * \param container A pointer to the container to operate on.
 */
#define ASTOBJ_CONTAINER_UNMARKALL(container) \
	ASTOBJ_CONTAINER_TRAVERSE(container, 1, ASTOBJ_UNMARK(iterator))

/*! \brief Dump information about an object into a string.
 *
 * \param s A pointer to the string buffer to use.
 * \param slen The length of s.
 * \param obj A pointer to the object to dump.
 *
 * This macro dumps a text representation of the name, objectflags, and
 * refcount fields of an object to the specfied string buffer.
 */
#define ASTOBJ_DUMP(s,slen,obj) \
	snprintf((s),(slen),"name: %s\nobjflags: %d\nrefcount: %d\n\n", (obj)->name, (obj)->objflags, (obj)->refcount);

/*! \brief Dump information about all the objects in a container to a file descriptor.
 *
 * \param fd The file descriptor to write to.
 * \param s A string buffer, same as #ASTOBJ_DUMP().
 * \param slen The length of s, same as #ASTOBJ_DUMP().
 * \param container A pointer to the container to dump.
 *
 * This macro dumps a text representation of the name, objectflags, and
 * refcount fields of all the objects in a container to the specified file
 * descriptor.
 */
#define ASTOBJ_CONTAINER_DUMP(fd,s,slen,container) \
	ASTOBJ_CONTAINER_TRAVERSE(container, 1, do { ASTOBJ_DUMP(s,slen,iterator); ast_cli(fd, s); } while(0))

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_ASTOBJ_H */
