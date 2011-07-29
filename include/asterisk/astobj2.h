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

#ifndef _ASTERISK_ASTOBJ2_H
#define _ASTERISK_ASTOBJ2_H

#include "asterisk/compat.h"
#include "asterisk/linkedlists.h"

/*! \file
 * \ref AstObj2
 *
 * \page AstObj2 Object Model implementing objects and containers.

This module implements an abstraction for objects (with locks and
reference counts), and containers for these user-defined objects,
also supporting locking, reference counting and callbacks.

The internal implementation of objects and containers is opaque to the user,
so we can use different data structures as needs arise.

\section AstObj2_UsageObjects USAGE - OBJECTS

An ao2 object is a block of memory that the user code can access,
and for which the system keeps track (with a bit of help from the
programmer) of the number of references around.  When an object has
no more references (refcount == 0), it is destroyed, by first
invoking whatever 'destructor' function the programmer specifies
(it can be NULL if none is necessary), and then freeing the memory.
This way objects can be shared without worrying who is in charge
of freeing them.
As an additional feature, ao2 objects are associated to individual
locks.

Creating an object requires the size of the object and
and a pointer to the destructor function:

    struct foo *o;

    o = ao2_alloc(sizeof(struct foo), my_destructor_fn);

The value returned points to the user-visible portion of the objects
(user-data), but is also used as an identifier for all object-related
operations such as refcount and lock manipulations.

On return from ao2_alloc():

 - the object has a refcount = 1;
 - the memory for the object is allocated dynamically and zeroed;
 - we cannot realloc() the object itself;
 - we cannot call free(o) to dispose of the object. Rather, we
   tell the system that we do not need the reference anymore:

    ao2_ref(o, -1)

  causing the destructor to be called (and then memory freed) when
  the refcount goes to 0.

- ao2_ref(o, +1) can be used to modify the refcount on the
  object in case we want to pass it around.

- ao2_lock(obj), ao2_unlock(obj), ao2_trylock(obj) can be used
  to manipulate the lock associated with the object.


\section AstObj2_UsageContainers USAGE - CONTAINERS

An ao2 container is an abstract data structure where we can store
ao2 objects, search them (hopefully in an efficient way), and iterate
or apply a callback function to them. A container is just an ao2 object
itself.

A container must first be allocated, specifying the initial
parameters. At the moment, this is done as follows:

    <b>Sample Usage:</b>
    \code

    struct ao2_container *c;

    c = ao2_container_alloc(MAX_BUCKETS, my_hash_fn, my_cmp_fn);
    \endcode

where

- MAX_BUCKETS is the number of buckets in the hash table,
- my_hash_fn() is the (user-supplied) function that returns a
  hash key for the object (further reduced modulo MAX_BUCKETS
  by the container's code);
- my_cmp_fn() is the default comparison function used when doing
  searches on the container,

A container knows little or nothing about the objects it stores,
other than the fact that they have been created by ao2_alloc().
All knowledge of the (user-defined) internals of the objects
is left to the (user-supplied) functions passed as arguments
to ao2_container_alloc().

If we want to insert an object in a container, we should
initialize its fields -- especially, those used by my_hash_fn() --
to compute the bucket to use.
Once done, we can link an object to a container with

    ao2_link(c, o);

The function returns NULL in case of errors (and the object
is not inserted in the container). Other values mean success
(we are not supposed to use the value as a pointer to anything).
Linking an object to a container increases its refcount by 1
automatically.

\note While an object o is in a container, we expect that
my_hash_fn(o) will always return the same value. The function
does not lock the object to be computed, so modifications of
those fields that affect the computation of the hash should
be done by extracting the object from the container, and
reinserting it after the change (this is not terribly expensive).

\note A container with a single buckets is effectively a linked
list. However there is no ordering among elements.

- \ref AstObj2_Containers
- \ref astobj2.h All documentation for functions and data structures

 */

/*
\note DEBUGGING REF COUNTS BIBLE:
An interface to help debug refcounting is provided
in this package. It is dependent on the REF_DEBUG macro being
defined in a source file, before the #include of astobj2.h,
and in using variants of the normal ao2_xxxx functions
that are named ao2_t_xxxx instead, with an extra argument, a string,
that will be printed out into /tmp/refs when the refcount for an
object is changed.

  these ao2_t_xxxx variants are provided:

ao2_t_alloc(arg1, arg2, arg3)
ao2_t_ref(arg1,arg2,arg3)
ao2_t_container_alloc(arg1,arg2,arg3,arg4)
ao2_t_link(arg1, arg2, arg3)
ao2_t_unlink(arg1, arg2, arg3)
ao2_t_callback(arg1,arg2,arg3,arg4,arg5)
ao2_t_find(arg1,arg2,arg3,arg4)
ao2_t_iterator_next(arg1, arg2)

If you study each argument list, you will see that these functions all have
one extra argument that their ao2_xxx counterpart. The last argument in
each case is supposed to be a string pointer, a "tag", that should contain
enough of an explanation, that you can pair operations that increment the
ref count, with operations that are meant to decrement the refcount.

Each of these calls will generate at least one line of output in /tmp/refs.
These lines look like this:
...
0x8756f00 =1   chan_sip.c:22240:load_module (allocate users)
0x86e3408 =1   chan_sip.c:22241:load_module (allocate peers)
0x86dd380 =1   chan_sip.c:22242:load_module (allocate peers_by_ip)
0x822d020 =1   chan_sip.c:22243:load_module (allocate dialogs)
0x8930fd8 =1   chan_sip.c:20025:build_peer (allocate a peer struct)
0x8930fd8 +1   chan_sip.c:21467:reload_config (link peer into peer table) [@1]
0x8930fd8 -1   chan_sip.c:2370:unref_peer (unref_peer: from reload_config) [@2]
0x89318b0 =1   chan_sip.c:20025:build_peer (allocate a peer struct)
0x89318b0 +1   chan_sip.c:21467:reload_config (link peer into peer table) [@1]
0x89318b0 -1   chan_sip.c:2370:unref_peer (unref_peer: from reload_config) [@2]
0x8930218 =1   chan_sip.c:20025:build_peer (allocate a peer struct)
0x8930218 +1   chan_sip.c:21539:reload_config (link peer into peers table) [@1]
0x868c040 -1   chan_sip.c:2424:dialog_unlink_all (unset the relatedpeer->call field in tandem with relatedpeer field itself) [@2]
0x868c040 -1   chan_sip.c:2443:dialog_unlink_all (Let's unbump the count in the unlink so the poor pvt can disappear if it is time) [@1]
0x868c040 **call destructor** chan_sip.c:2443:dialog_unlink_all (Let's unbump the count in the unlink so the poor pvt can disappear if it is time)
0x8cc07e8 -1   chan_sip.c:2370:unref_peer (unsetting a dialog relatedpeer field in sip_destroy) [@3]
0x8cc07e8 +1   chan_sip.c:3876:find_peer (ao2_find in peers table) [@2]
0x8cc07e8 -1   chan_sip.c:2370:unref_peer (unref_peer, from sip_devicestate, release ref from find_peer) [@3]
...

The first column is the object address.
The second column reflects how the operation affected the ref count
    for that object. Creation sets the ref count to 1 (=1).
    increment or decrement and amount are specified (-1/+1).
The remainder of the line specifies where in the file the call was made,
    and the function name, and the tag supplied in the function call.

The **call destructor** is specified when the the destroy routine is
run for an object. It does not affect the ref count, but is important
in debugging, because it is possible to have the astobj2 system run it
multiple times on the same object, commonly fatal to asterisk.

Sometimes you have some helper functions to do object ref/unref
operations. Using these normally hides the place where these
functions were called. To get the location where these functions
were called to appear in /tmp/refs, you can do this sort of thing:

#ifdef REF_DEBUG
#define dialog_ref(arg1,arg2) dialog_ref_debug((arg1),(arg2), __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define dialog_unref(arg1,arg2) dialog_unref_debug((arg1),(arg2), __FILE__, __LINE__, __PRETTY_FUNCTION__)
static struct sip_pvt *dialog_ref_debug(struct sip_pvt *p, char *tag, const char *file, int line, const char *func)
{
	if (p)
		ao2_ref_debug(p, 1, tag, file, line, func);
	else
		ast_log(LOG_ERROR, "Attempt to Ref a null pointer\n");
	return p;
}

static struct sip_pvt *dialog_unref_debug(struct sip_pvt *p, char *tag, const char *file, int line, const char *func)
{
	if (p)
		ao2_ref_debug(p, -1, tag, file, line, func);
	return NULL;
}
#else
static struct sip_pvt *dialog_ref(struct sip_pvt *p, char *tag)
{
	if (p)
		ao2_ref(p, 1);
	else
		ast_log(LOG_ERROR, "Attempt to Ref a null pointer\n");
	return p;
}

static struct sip_pvt *dialog_unref(struct sip_pvt *p, char *tag)
{
	if (p)
		ao2_ref(p, -1);
	return NULL;
}
#endif

In the above code, note that the "normal" helper funcs call ao2_ref() as
normal, and the "helper" functions call ao2_ref_debug directly with the
file, function, and line number info provided. You might find this
well worth the effort to help track these function calls in the code.

To find out why objects are not destroyed (a common bug), you can
edit the source file to use the ao2_t_* variants, add the #define REF_DEBUG 1
before the #include "asterisk/astobj2.h" line, and add a descriptive
tag to each call. Recompile, and run Asterisk, exit asterisk with
"stop gracefully", which should result in every object being destroyed.
Then, you can "sort -k 1 /tmp/refs > x1" to get a sorted list of
all the objects, or you can use "util/refcounter" to scan the file
for you and output any problems it finds.

The above may seem astronomically more work than it is worth to debug
reference counts, which may be true in "simple" situations, but for
more complex situations, it is easily worth 100 times this effort to
help find problems.

To debug, pair all calls so that each call that increments the
refcount is paired with a corresponding call that decrements the
count for the same reason. Hopefully, you will be left with one
or more unpaired calls. This is where you start your search!

For instance, here is an example of this for a dialog object in
chan_sip, that was not getting destroyed, after I moved the lines around
to pair operations:

   0x83787a0 =1   chan_sip.c:5733:sip_alloc (allocate a dialog(pvt) struct)
   0x83787a0 -1   chan_sip.c:19173:sip_poke_peer (unref dialog at end of sip_poke_peer, obtained from sip_alloc, just before it goes out of scope) [@4]

   0x83787a0 +1   chan_sip.c:5854:sip_alloc (link pvt into dialogs table) [@1]
   0x83787a0 -1   chan_sip.c:19150:sip_poke_peer (About to change the callid -- remove the old name) [@3]
   0x83787a0 +1   chan_sip.c:19152:sip_poke_peer (Linking in under new name) [@2]
   0x83787a0 -1   chan_sip.c:2399:dialog_unlink_all (unlinking dialog via ao2_unlink) [@5]

   0x83787a0 +1   chan_sip.c:19130:sip_poke_peer (copy sip alloc from p to peer->call) [@2]


   0x83787a0 +1   chan_sip.c:2996:__sip_reliable_xmit (__sip_reliable_xmit: setting pkt->owner) [@3]
   0x83787a0 -1   chan_sip.c:2425:dialog_unlink_all (remove all current packets in this dialog, and the pointer to the dialog too as part of __sip_destroy) [@4]

   0x83787a0 +1   chan_sip.c:22356:unload_module (iterate thru dialogs) [@4]
   0x83787a0 -1   chan_sip.c:22359:unload_module (toss dialog ptr from iterator_next) [@5]


   0x83787a0 +1   chan_sip.c:22373:unload_module (iterate thru dialogs) [@3]
   0x83787a0 -1   chan_sip.c:22375:unload_module (throw away iterator result) [@2]

   0x83787a0 +1   chan_sip.c:2397:dialog_unlink_all (Let's bump the count in the unlink so it doesn't accidentally become dead before we are done) [@4]
   0x83787a0 -1   chan_sip.c:2436:dialog_unlink_all (Let's unbump the count in the unlink so the poor pvt can disappear if it is time) [@3]

As you can see, only one unbalanced operation is in the list, a ref count increment when
the peer->call was set, but no corresponding decrement was made...

Hopefully this helps you narrow your search and find those bugs.

THE ART OF REFERENCE COUNTING
(by Steve Murphy)
SOME TIPS for complicated code, and ref counting:

1. Theoretically, passing a refcounted object pointer into a function
call is an act of copying the reference, and could be refcounted.
But, upon examination, this sort of refcounting will explode the amount
of code you have to enter, and for no tangible benefit, beyond
creating more possible failure points/bugs. It will even
complicate your code and make debugging harder, slow down your program
doing useless increments and decrements of the ref counts.

2. It is better to track places where a ref counted pointer
is copied into a structure or stored. Make sure to decrement the refcount
of any previous pointer that might have been there, if setting
this field might erase a previous pointer. ao2_find and iterate_next
internally increment the ref count when they return a pointer, so
you need to decrement the count before the pointer goes out of scope.

3. Any time you decrement a ref count, it may be possible that the
object will be destroyed (freed) immediately by that call. If you
are destroying a series of fields in a refcounted object, and
any of the unref calls might possibly result in immediate destruction,
you can first increment the count to prevent such behavior, then
after the last test, decrement the pointer to allow the object
to be destroyed, if the refcount would be zero.

Example:

	dialog_ref(dialog, "Let's bump the count in the unlink so it doesn't accidentally become dead before we are done");

	ao2_t_unlink(dialogs, dialog, "unlinking dialog via ao2_unlink");

	*//* Unlink us from the owner (channel) if we have one *//*
	if (dialog->owner) {
		if (lockowner)
			ast_channel_lock(dialog->owner);
		ast_debug(1, "Detaching from channel %s\n", dialog->owner->name);
		dialog->owner->tech_pvt = dialog_unref(dialog->owner->tech_pvt, "resetting channel dialog ptr in unlink_all");
		if (lockowner)
			ast_channel_unlock(dialog->owner);
	}
	if (dialog->registry) {
		if (dialog->registry->call == dialog)
			dialog->registry->call = dialog_unref(dialog->registry->call, "nulling out the registry's call dialog field in unlink_all");
		dialog->registry = registry_unref(dialog->registry, "delete dialog->registry");
	}
    ...
 	dialog_unref(dialog, "Let's unbump the count in the unlink so the poor pvt can disappear if it is time");

In the above code, the ao2_t_unlink could end up destroying the dialog
object; if this happens, then the subsequent usages of the dialog
pointer could result in a core dump. So, we 'bump' the
count upwards before beginning, and then decrementing the count when
we are finished. This is analogous to 'locking' or 'protecting' operations
for a short while.

4. One of the most insidious problems I've run into when converting
code to do ref counted automatic destruction, is in the destruction
routines. Where a "destroy" routine had previously been called to
get rid of an object in non-refcounted code, the new regime demands
that you tear that "destroy" routine into two pieces, one that will
tear down the links and 'unref' them, and the other to actually free
and reset fields. A destroy routine that does any reference deletion
for its own object, will never be called. Another insidious problem
occurs in mutually referenced structures. As an example, a dialog contains
a pointer to a peer, and a peer contains a pointer to a dialog. Watch
out that the destruction of one doesn't depend on the destruction of the
other, as in this case a dependency loop will result in neither being
destroyed!

Given the above, you should be ready to do a good job!

murf

*/



/*! \brief
 * Typedef for an object destructor. This is called just before freeing
 * the memory for the object. It is passed a pointer to the user-defined
 * data of the object.
 */
typedef void (*ao2_destructor_fn)(void *);


/*! \brief
 * Allocate and initialize an object.
 *
 * \param data_size The sizeof() of the user-defined structure.
 * \param destructor_fn The destructor function (can be NULL)
 * \param debug_msg
 * \return A pointer to user-data.
 *
 * Allocates a struct astobj2 with sufficient space for the
 * user-defined structure.
 * \note
 * - storage is zeroed; XXX maybe we want a flag to enable/disable this.
 * - the refcount of the object just created is 1
 * - the returned pointer cannot be free()'d or realloc()'ed;
 *   rather, we just call ao2_ref(o, -1);
 *
 * @{
 */

#if defined(REF_DEBUG)

#define ao2_t_alloc(data_size, destructor_fn, debug_msg) __ao2_alloc_debug((data_size), (destructor_fn), (debug_msg),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)
#define ao2_alloc(data_size, destructor_fn)              __ao2_alloc_debug((data_size), (destructor_fn), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)

#elif defined(__AST_DEBUG_MALLOC)

#define ao2_t_alloc(data_size, destructor_fn, debug_msg) __ao2_alloc_debug((data_size), (destructor_fn), (debug_msg),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)
#define ao2_alloc(data_size, destructor_fn)              __ao2_alloc_debug((data_size), (destructor_fn), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)

#else

#define ao2_t_alloc(data_size, destructor_fn, debug_msg) __ao2_alloc((data_size), (destructor_fn))
#define ao2_alloc(data_size, destructor_fn)              __ao2_alloc((data_size), (destructor_fn))

#endif

void *__ao2_alloc_debug(const size_t data_size, ao2_destructor_fn destructor_fn, char *tag,
			const char *file, int line, const char *funcname, int ref_debug);
void *__ao2_alloc(const size_t data_size, ao2_destructor_fn destructor_fn);

/*! @} */

/*! \brief
 * Reference/unreference an object and return the old refcount.
 *
 * \param o A pointer to the object
 * \param delta Value to add to the reference counter.
 * \param tag used for debugging
 * \return The value of the reference counter before the operation.
 *
 * Increase/decrease the reference counter according
 * the value of delta.
 *
 * If the refcount goes to zero, the object is destroyed.
 *
 * \note The object must not be locked by the caller of this function, as
 *       it is invalid to try to unlock it after releasing the reference.
 *
 * \note if we know the pointer to an object, it is because we
 * have a reference count to it, so the only case when the object
 * can go away is when we release our reference, and it is
 * the last one in existence.
 *
 * @{
 */

#ifdef REF_DEBUG

#define ao2_t_ref(o,delta,tag) __ao2_ref_debug((o), (delta), (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_ref(o,delta)       __ao2_ref_debug((o), (delta), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_ref(o,delta,tag) __ao2_ref((o), (delta))
#define ao2_ref(o,delta)       __ao2_ref((o), (delta))

#endif

int __ao2_ref_debug(void *o, int delta, char *tag, char *file, int line, const char *funcname);
int __ao2_ref(void *o, int delta);

/*! @} */

/*! \brief
 * Lock an object.
 *
 * \param a A pointer to the object we want to lock.
 * \return 0 on success, other values on error.
 */
int __ao2_lock(void *a, const char *file, const char *func, int line, const char *var);
#define ao2_lock(a) __ao2_lock(a, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)

/*! \brief
 * Unlock an object.
 *
 * \param a A pointer to the object we want unlock.
 * \return 0 on success, other values on error.
 */
int __ao2_unlock(void *a, const char *file, const char *func, int line, const char *var);
#define ao2_unlock(a) __ao2_unlock(a, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)

/*! \brief
 * Try locking-- (don't block if fail)
 *
 * \param a A pointer to the object we want to lock.
 * \return 0 on success, other values on error.
 */
int __ao2_trylock(void *a, const char *file, const char *func, int line, const char *var);
#define ao2_trylock(a) __ao2_trylock(a, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)

/*!
 * \brief Return the lock address of an object
 *
 * \param[in] obj A pointer to the object we want.
 * \return the address of the lock, else NULL.
 *
 * This function comes in handy mainly for debugging locking
 * situations, where the locking trace code reports the
 * lock address, this allows you to correlate against
 * object address, to match objects to reported locks.
 *
 * \since 1.6.1
 */
void *ao2_object_get_lockaddr(void *obj);

/*!
 \page AstObj2_Containers AstObj2 Containers

Containers are data structures meant to store several objects,
and perform various operations on them.
Internally, objects are stored in lists, hash tables or other
data structures depending on the needs.

\note NOTA BENE: at the moment the only container we support is the
	hash table and its degenerate form, the list.

Operations on container include:

  -  c = \b ao2_container_alloc(size, hash_fn, cmp_fn)
	allocate a container with desired size and default compare
	and hash function
         -The compare function returns an int, which
         can be 0 for not found, CMP_STOP to stop end a traversal,
         or CMP_MATCH if they are equal
         -The hash function returns an int. The hash function
         takes two argument, the object pointer and a flags field,

  -  \b ao2_find(c, arg, flags)
	returns zero or more element matching a given criteria
	(specified as arg). 'c' is the container pointer. Flags
    can be:
	OBJ_UNLINK - to remove the object, once found, from the container.
	OBJ_NODATA - don't return the object if found (no ref count change)
	OBJ_MULTIPLE - don't stop at first match
	OBJ_POINTER	- if set, 'arg' is an object pointer, and a hashtable
                  search will be done. If not, a traversal is done.

  -  \b ao2_callback(c, flags, fn, arg)
	apply fn(obj, arg) to all objects in the container.
	Similar to find. fn() can tell when to stop, and
	do anything with the object including unlinking it.
	  - c is the container;
      - flags can be
	     OBJ_UNLINK   - to remove the object, once found, from the container.
	     OBJ_NODATA   - don't return the object if found (no ref count change)
	     OBJ_MULTIPLE - don't stop at first match
	     OBJ_POINTER  - if set, 'arg' is an object pointer, and a hashtable
                        search will be done. If not, a traversal is done through
                        all the hashtable 'buckets'..
      - fn is a func that returns int, and takes 3 args:
        (void *obj, void *arg, int flags);
          obj is an object
          arg is the same as arg passed into ao2_callback
          flags is the same as flags passed into ao2_callback
         fn returns:
           0: no match, keep going
           CMP_STOP: stop search, no match
           CMP_MATCH: This object is matched.

	Note that the entire operation is run with the container
	locked, so noone else can change its content while we work on it.
	However, we pay this with the fact that doing
	anything blocking in the callback keeps the container
	blocked.
	The mechanism is very flexible because the callback function fn()
	can do basically anything e.g. counting, deleting records, etc.
	possibly using arg to store the results.

  -  \b iterate on a container
	this is done with the following sequence

\code

	    struct ao2_container *c = ... // our container
	    struct ao2_iterator i;
	    void *o;

	    i = ao2_iterator_init(c, flags);

	    while ((o = ao2_iterator_next(&i))) {
		... do something on o ...
		ao2_ref(o, -1);
	    }

	    ao2_iterator_destroy(&i);
\endcode

	The difference with the callback is that the control
	on how to iterate is left to us.

    - \b ao2_ref(c, -1)
	dropping a reference to a container destroys it, very simple!

Containers are ao2 objects themselves, and this is why their
implementation is simple too.

Before declaring containers, we need to declare the types of the
arguments passed to the constructor - in turn, this requires
to define callback and hash functions and their arguments.

- \ref AstObj2
- \ref astobj2.h
 */

/*! \brief
 * Type of a generic callback function
 * \param obj  pointer to the (user-defined part) of an object.
 * \param arg callback argument from ao2_callback()
 * \param flags flags from ao2_callback()
 *
 * The return values are a combination of enum _cb_results.
 * Callback functions are used to search or manipulate objects in a container.
 */
typedef int (ao2_callback_fn)(void *obj, void *arg, int flags);

/*! \brief
 * Type of a generic callback function
 * \param obj pointer to the (user-defined part) of an object.
 * \param arg callback argument from ao2_callback()
 * \param data arbitrary data from ao2_callback()
 * \param flags flags from ao2_callback()
 *
 * The return values are a combination of enum _cb_results.
 * Callback functions are used to search or manipulate objects in a container.
 */
typedef int (ao2_callback_data_fn)(void *obj, void *arg, void *data, int flags);

/*! \brief a very common callback is one that matches by address. */
ao2_callback_fn ao2_match_by_addr;

/*! \brief
 * A callback function will return a combination of CMP_MATCH and CMP_STOP.
 * The latter will terminate the search in a container.
 */
enum _cb_results {
	CMP_MATCH	= 0x1,	/*!< the object matches the request */
	CMP_STOP	= 0x2,	/*!< stop the search now */
};

/*! \brief
 * Flags passed to ao2_callback() and ao2_hash_fn() to modify its behaviour.
 */
enum search_flags {
	/*! Unlink the object for which the callback function
	 *  returned CMP_MATCH.
	 */
	OBJ_UNLINK	 = (1 << 0),
	/*! On match, don't return the object hence do not increase
	 *  its refcount.
	 */
	OBJ_NODATA	 = (1 << 1),
	/*! Don't stop at the first match in ao2_callback() unless the result of
	 *  of the callback function == (CMP_STOP | CMP_MATCH).
	 */
	OBJ_MULTIPLE = (1 << 2),
	/*! obj is an object of the same type as the one being searched for,
	 *  so use the object's hash function for optimized searching.
	 *  The search function is unaffected (i.e. use the one passed as
	 *  argument, or match_by_addr if none specified).
	 */
	OBJ_POINTER	 = (1 << 3),
	/*! 
	 * \brief Continue if a match is not found in the hashed out bucket
	 *
	 * This flag is to be used in combination with OBJ_POINTER.  This tells
	 * the ao2_callback() core to keep searching through the rest of the
	 * buckets if a match is not found in the starting bucket defined by
	 * the hash value on the argument.
	 */
	OBJ_CONTINUE     = (1 << 4),
	/*! 
	 * \brief By using this flag, the ao2_container being searched will _NOT_
	 * be locked.  Only use this flag if the ao2_container is being protected
	 * by another mechanism other that the internal ao2_lock.
	 */
	OBJ_NOLOCK     = (1 << 5),
	/*!
	 * \brief The data is hashable, but is not an object.
	 *
	 * This can be used when you want to be able to pass custom data
	 * to a hash function that is not a full object, but perhaps just
	 * a string.
	 *
	 * \note OBJ_KEY and OBJ_POINTER are mutually exclusive options.
	 */
	OBJ_KEY        = (1 << 6),
};

/*!
 * Type of a generic function to generate a hash value from an object.
 * flags is ignored at the moment. Eventually, it will include the
 * value of OBJ_POINTER passed to ao2_callback().
 */
typedef int (ao2_hash_fn)(const void *obj, const int flags);

/*! \name Object Containers
 * Here start declarations of containers.
 */
/*@{ */
struct ao2_container;

/*! \brief
 * Allocate and initialize a container
 * with the desired number of buckets.
 *
 * We allocate space for a struct astobj_container, struct container
 * and the buckets[] array.
 *
 * \param arg1 Number of buckets for hash
 * \param arg2 Pointer to a function computing a hash value.
 * \param arg3 Pointer to a function comparating key-value
 * 			with a string. (can be NULL)
 * \param arg4
 *
 * \return A pointer to a struct container.
 *
 * \note Destructor is set implicitly.
 */

#if defined(REF_DEBUG)

#define ao2_t_container_alloc(arg1,arg2,arg3,arg4) __ao2_container_alloc_debug((arg1), (arg2), (arg3), (arg4),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)
#define ao2_container_alloc(arg1,arg2,arg3)        __ao2_container_alloc_debug((arg1), (arg2), (arg3), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)

#elif defined(__AST_DEBUG_MALLOC)

#define ao2_t_container_alloc(arg1,arg2,arg3,arg4) __ao2_container_alloc_debug((arg1), (arg2), (arg3), (arg4),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)
#define ao2_container_alloc(arg1,arg2,arg3)        __ao2_container_alloc_debug((arg1), (arg2), (arg3), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)

#else

#define ao2_t_container_alloc(arg1,arg2,arg3,arg4) __ao2_container_alloc((arg1), (arg2), (arg3))
#define ao2_container_alloc(arg1,arg2,arg3)        __ao2_container_alloc((arg1), (arg2), (arg3))

#endif

struct ao2_container *__ao2_container_alloc(const unsigned int n_buckets,
					    ao2_hash_fn *hash_fn, ao2_callback_fn *cmp_fn);
struct ao2_container *__ao2_container_alloc_debug(const unsigned int n_buckets,
						  ao2_hash_fn *hash_fn, ao2_callback_fn *cmp_fn,
						  char *tag, char *file, int line, const char *funcname,
						  int ref_debug);

/*! \brief
 * Returns the number of elements in a container.
 */
int ao2_container_count(struct ao2_container *c);

/*@} */

/*! \name Object Management
 * Here we have functions to manage objects.
 *
 * We can use the functions below on any kind of
 * object defined by the user.
 */
/*@{ */

/*!
 * \brief Add an object to a container.
 *
 * \param arg1 the container to operate on.
 * \param arg2 the object to be added.
 * \param arg3 used for debuging.
 *
 * \retval NULL on errors.
 * \retval newobj on success.
 *
 * This function inserts an object in a container according its key.
 *
 * \note Remember to set the key before calling this function.
 *
 * \note This function automatically increases the reference count to account
 *       for the reference that the container now holds to the object.
 */
#ifdef REF_DEBUG

#define ao2_t_link(arg1, arg2, arg3) __ao2_link_debug((arg1), (arg2), 0, (arg3),  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_link(arg1, arg2)         __ao2_link_debug((arg1), (arg2), 0, "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_link_nolock(arg1, arg2)  __ao2_link_debug((arg1), (arg2), OBJ_NOLOCK, "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_link(arg1, arg2, arg3) __ao2_link((arg1), (arg2), 0)
#define ao2_link(arg1, arg2)         __ao2_link((arg1), (arg2), 0)
#define ao2_link_nolock(arg1, arg2)  __ao2_link((arg1), (arg2), OBJ_NOLOCK)

#endif

void *__ao2_link_debug(struct ao2_container *c, void *new_obj, int flags, char *tag, char *file, int line, const char *funcname);
void *__ao2_link(struct ao2_container *c, void *newobj, int flags);

/*!
 * \brief Remove an object from a container
 *
 * \param arg1 the container
 * \param arg2 the object to unlink
 * \param arg3 tag for debugging
 *
 * \retval NULL, always
 *
 * \note The object requested to be unlinked must be valid.  However, if it turns
 *       out that it is not in the container, this function is still safe to
 *       be called.
 *
 * \note If the object gets unlinked from the container, the container's
 *       reference to the object will be automatically released. (The
 *       refcount will be decremented).
 */
#ifdef REF_DEBUG

#define ao2_t_unlink(arg1, arg2, arg3) __ao2_unlink_debug((arg1), (arg2), 0, (arg3),  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_unlink(arg1, arg2)         __ao2_unlink_debug((arg1), (arg2), 0, "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_unlink_nolock(arg1, arg2)  __ao2_unlink_debug((arg1), (arg2), OBJ_NOLOCK, "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_unlink(arg1, arg2, arg3) __ao2_unlink((arg1), (arg2), 0)
#define ao2_unlink(arg1, arg2)         __ao2_unlink((arg1), (arg2), 0)
#define ao2_unlink_nolock(arg1, arg2)  __ao2_unlink((arg1), (arg2), OBJ_NOLOCK)

#endif

void *__ao2_unlink_debug(struct ao2_container *c, void *obj, int flags, char *tag, char *file, int line, const char *funcname);
void *__ao2_unlink(struct ao2_container *c, void *obj, int flags);


/*@} */

/*! \brief
 * ao2_callback() is a generic function that applies cb_fn() to all objects
 * in a container, as described below.
 *
 * \param c A pointer to the container to operate on.
 * \param flags A set of flags specifying the operation to perform,
	partially used by the container code, but also passed to
	the callback.
     - If OBJ_NODATA is set, ao2_callback will return NULL. No refcounts
       of any of the traversed objects will be incremented.
       On the converse, if it is NOT set (the default), The ref count
       of each object for which CMP_MATCH was set will be incremented,
       and you will have no way of knowing which those are, until
       the multiple-object-return functionality is implemented.
     - If OBJ_POINTER is set, the traversed items will be restricted
       to the objects in the bucket that the object key hashes to.
 * \param cb_fn A function pointer, that will be called on all
    objects, to see if they match. This function returns CMP_MATCH
    if the object is matches the criteria; CMP_STOP if the traversal
    should immediately stop, or both (via bitwise ORing), if you find a
    match and want to end the traversal, and 0 if the object is not a match,
    but the traversal should continue. This is the function that is applied
    to each object traversed. Its arguments are:
        (void *obj, void *arg, int flags), where:
          obj is an object
          arg is the same as arg passed into ao2_callback
          flags is the same as flags passed into ao2_callback (flags are
           also used by ao2_callback).
 * \param arg passed to the callback.
 * \param tag used for debuging.
 * \return when OBJ_MULTIPLE is not included in the flags parameter,
 *         the return value will be either the object found or NULL if no
 *         no matching object was found. if OBJ_MULTIPLE is included,
 *         the return value will be a pointer to an ao2_iterator object,
 *         which must be destroyed with ao2_iterator_destroy() when the
 *         caller no longer needs it.
 *
 * If the function returns any objects, their refcount is incremented,
 * and the caller is in charge of decrementing them once done.
 *
 * Typically, ao2_callback() is used for two purposes:
 * - to perform some action (including removal from the container) on one
 *   or more objects; in this case, cb_fn() can modify the object itself,
 *   and to perform deletion should set CMP_MATCH on the matching objects,
 *   and have OBJ_UNLINK set in flags.
 * - to look for a specific object in a container; in this case, cb_fn()
 *   should not modify the object, but just return a combination of
 *   CMP_MATCH and CMP_STOP on the desired object.
 * Other usages are also possible, of course.

 * This function searches through a container and performs operations
 * on objects according on flags passed.
 * XXX describe better
 * The comparison is done calling the compare function set implicitly.
 * The p pointer can be a pointer to an object or to a key,
 * we can say this looking at flags value.
 * If p points to an object we will search for the object pointed
 * by this value, otherwise we serch for a key value.
 * If the key is not unique we only find the first matching valued.
 *
 * The use of flags argument is the follow:
 *
 *	OBJ_UNLINK 		unlinks the object found
 *	OBJ_NODATA		on match, do return an object
 *				Callbacks use OBJ_NODATA as a default
 *				functions such as find() do
 *	OBJ_MULTIPLE		return multiple matches
 *				Default is no.
 *	OBJ_POINTER 		the pointer is an object pointer
 *
 * \note When the returned object is no longer in use, ao2_ref() should
 * be used to free the additional reference possibly created by this function.
 *
 * @{
 */
#ifdef REF_DEBUG

#define ao2_t_callback(c,flags,cb_fn,arg,tag) __ao2_callback_debug((c), (flags), (cb_fn), (arg), (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_callback(c,flags,cb_fn,arg)       __ao2_callback_debug((c), (flags), (cb_fn), (arg), "", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_callback(c,flags,cb_fn,arg,tag) __ao2_callback((c), (flags), (cb_fn), (arg))
#define ao2_callback(c,flags,cb_fn,arg)       __ao2_callback((c), (flags), (cb_fn), (arg))

#endif

void *__ao2_callback_debug(struct ao2_container *c, enum search_flags flags, ao2_callback_fn *cb_fn,
			   void *arg, char *tag, char *file, int line, const char *funcname);
void *__ao2_callback(struct ao2_container *c, enum search_flags flags, ao2_callback_fn *cb_fn, void *arg);

/*! @} */

/*! \brief
 * ao2_callback_data() is a generic function that applies cb_fn() to all objects
 * in a container.  It is functionally identical to ao2_callback() except that
 * instead of taking an ao2_callback_fn *, it takes an ao2_callback_data_fn *, and
 * allows the caller to pass in arbitrary data.
 *
 * This call would be used instead of ao2_callback() when the caller needs to pass
 * OBJ_POINTER as part of the flags argument (which in turn requires passing in a
 * prototype ao2 object for 'arg') and also needs access to other non-global data
 * to complete it's comparison or task.
 *
 * See the documentation for ao2_callback() for argument descriptions.
 *
 * \see ao2_callback()
 */
#ifdef REF_DEBUG

#define ao2_t_callback_data(arg1,arg2,arg3,arg4,arg5,arg6) __ao2_callback_data_debug((arg1), (arg2), (arg3), (arg4), (arg5), (arg6), __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_callback_data(arg1,arg2,arg3,arg4,arg5)        __ao2_callback_data_debug((arg1), (arg2), (arg3), (arg4), (arg5), "", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_callback_data(arg1,arg2,arg3,arg4,arg5,arg6) __ao2_callback_data((arg1), (arg2), (arg3), (arg4), (arg5))
#define ao2_callback_data(arg1,arg2,arg3,arg4,arg5)        __ao2_callback_data((arg1), (arg2), (arg3), (arg4), (arg5))

#endif

void *__ao2_callback_data_debug(struct ao2_container *c, enum search_flags flags,
				ao2_callback_data_fn *cb_fn, void *arg, void *data, char *tag,
				char *file, int line, const char *funcname);
void *__ao2_callback_data(struct ao2_container *c, enum search_flags flags,
			  ao2_callback_data_fn *cb_fn, void *arg, void *data);

/*! ao2_find() is a short hand for ao2_callback(c, flags, c->cmp_fn, arg)
 * XXX possibly change order of arguments ?
 */
#ifdef REF_DEBUG

#define ao2_t_find(arg1,arg2,arg3,arg4) __ao2_find_debug((arg1), (arg2), (arg3), (arg4), __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_find(arg1,arg2,arg3)        __ao2_find_debug((arg1), (arg2), (arg3), "", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_find(arg1,arg2,arg3,arg4) __ao2_find((arg1), (arg2), (arg3))
#define ao2_find(arg1,arg2,arg3)        __ao2_find((arg1), (arg2), (arg3))

#endif

void *__ao2_find_debug(struct ao2_container *c, const void *arg, enum search_flags flags, char *tag,
		       char *file, int line, const char *funcname);
void *__ao2_find(struct ao2_container *c, const void *arg, enum search_flags flags);

/*! \brief
 *
 *
 * When we need to walk through a container, we use an
 * ao2_iterator to keep track of the current position.
 *
 * Because the navigation is typically done without holding the
 * lock on the container across the loop, objects can be inserted or deleted
 * or moved while we work. As a consequence, there is no guarantee that
 * we manage to touch all the elements in the container, and it is possible
 * that we touch the same object multiple times.
 *
 * However, within the current hash table container, the following is true:
 *  - It is not possible to miss an object in the container while iterating
 *    unless it gets added after the iteration begins and is added to a bucket
 *    that is before the one the current object is in.  In this case, even if
 *    you locked the container around the entire iteration loop, you still would
 *    not see this object, because it would still be waiting on the container
 *    lock so that it can be added.
 *  - It would be extremely rare to see an object twice.  The only way this can
 *    happen is if an object got unlinked from the container and added again
 *    during the same iteration.  Furthermore, when the object gets added back,
 *    it has to be in the current or later bucket for it to be seen again.
 *
 * An iterator must be first initialized with ao2_iterator_init(),
 * then we can use o = ao2_iterator_next() to move from one
 * element to the next. Remember that the object returned by
 * ao2_iterator_next() has its refcount incremented,
 * and the reference must be explicitly released when done with it.
 *
 * In addition, ao2_iterator_init() will hold a reference to the container
 * being iterated, which will be freed when ao2_iterator_destroy() is called
 * to free up the resources used by the iterator (if any).
 *
 * Example:
 *
 *  \code
 *
 *  struct ao2_container *c = ... // the container we want to iterate on
 *  struct ao2_iterator i;
 *  struct my_obj *o;
 *
 *  i = ao2_iterator_init(c, flags);
 *
 *  while ((o = ao2_iterator_next(&i))) {
 *     ... do something on o ...
 *     ao2_ref(o, -1);
 *  }
 *
 *  ao2_iterator_destroy(&i);
 *
 *  \endcode
 *
 */

/*! \brief
 * The astobj2 iterator
 *
 * \note You are not supposed to know the internals of an iterator!
 * We would like the iterator to be opaque, unfortunately
 * its size needs to be known if we want to store it around
 * without too much trouble.
 * Anyways...
 * The iterator has a pointer to the container, and a flags
 * field specifying various things e.g. whether the container
 * should be locked or not while navigating on it.
 * The iterator "points" to the current object, which is identified
 * by three values:
 *
 * - a bucket number;
 * - the object_id, which is also the container version number
 *   when the object was inserted. This identifies the object
 *   uniquely, however reaching the desired object requires
 *   scanning a list.
 * - a pointer, and a container version when we saved the pointer.
 *   If the container has not changed its version number, then we
 *   can safely follow the pointer to reach the object in constant time.
 *
 * Details are in the implementation of ao2_iterator_next()
 * A freshly-initialized iterator has bucket=0, version=0.
 */
struct ao2_iterator {
	/*! the container */
	struct ao2_container *c;
	/*! operation flags */
	int flags;
	/*! current bucket */
	int bucket;
	/*! container version */
	unsigned int c_version;
	/*! pointer to the current object */
	void *obj;
	/*! container version when the object was created */
	unsigned int version;
};

/*! Flags that can be passed to ao2_iterator_init() to modify the behavior
 * of the iterator.
 */
enum ao2_iterator_flags {
	/*! Prevents ao2_iterator_next() from locking the container
	 * while retrieving the next object from it.
	 */
	AO2_ITERATOR_DONTLOCK = (1 << 0),
	/*! Indicates that the iterator was dynamically allocated by
	 * astobj2 API and should be freed by ao2_iterator_destroy().
	 */
	AO2_ITERATOR_MALLOCD = (1 << 1),
	/*! Indicates that before the iterator returns an object from
	 * the container being iterated, the object should be unlinked
	 * from the container.
	 */
	AO2_ITERATOR_UNLINK = (1 << 2),
};

/*!
 * \brief Create an iterator for a container
 *
 * \param c the container
 * \param flags one or more flags from ao2_iterator_flags
 *
 * \retval the constructed iterator
 *
 * \note This function does \b not take a pointer to an iterator;
 *       rather, it returns an iterator structure that should be
 *       assigned to (overwriting) an existing iterator structure
 *       allocated on the stack or on the heap.
 *
 * This function will take a reference on the container being iterated.
 *
 */
struct ao2_iterator ao2_iterator_init(struct ao2_container *c, int flags);

/*!
 * \brief Destroy a container iterator
 *
 * \param i the iterator to destroy
 *
 * \retval none
 *
 * This function will release the container reference held by the iterator
 * and any other resources it may be holding.
 *
 */
void ao2_iterator_destroy(struct ao2_iterator *i);

#ifdef REF_DEBUG

#define ao2_t_iterator_next(arg1, arg2) __ao2_iterator_next_debug((arg1), (arg2),  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_iterator_next(arg1)         __ao2_iterator_next_debug((arg1), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_iterator_next(arg1, arg2) __ao2_iterator_next((arg1))
#define ao2_iterator_next(arg1)         __ao2_iterator_next((arg1))

#endif

void *__ao2_iterator_next_debug(struct ao2_iterator *a, char *tag, char *file, int line, const char *funcname);
void *__ao2_iterator_next(struct ao2_iterator *a);

/* extra functions */
void ao2_bt(void);	/* backtrace */

#endif /* _ASTERISK_ASTOBJ2_H */
