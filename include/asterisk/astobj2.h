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
#include "asterisk/lock.h"
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
a pointer to the destructor function:

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
re-inserting it after the change (this is not terribly expensive).

\note A container with a single buckets is effectively a linked
list. However there is no ordering among elements.

- \ref AstObj2_Containers
- \ref astobj2.h All documentation for functions and data structures

 */

/*
\note DEBUGGING REF COUNTS BIBLE:
An interface to help debug refcounting is provided
in this package. It is dependent on the REF_DEBUG macro being
defined via menuselect and in using variants of the normal ao2_xxxx
function that are named ao2_t_xxxx instead, with an extra argument,
a string that will be printed out into the refs log file when the
refcount for an object is changed.

  these ao2_t_xxx variants are provided:

ao2_t_alloc(arg1, arg2, arg3)
ao2_t_ref(arg1,arg2,arg3)
ao2_t_container_alloc(arg1,arg2,arg3,arg4)
ao2_t_link(arg1, arg2, arg3)
ao2_t_unlink(arg1, arg2, arg3)
ao2_t_callback(arg1,arg2,arg3,arg4,arg5)
ao2_t_find(arg1,arg2,arg3,arg4)
ao2_t_iterator_next(arg1, arg2)

If you study each argument list, you will see that these functions all have
one extra argument than their ao2_xxx counterpart. The last argument in
each case is supposed to be a string pointer, a "tag", that should contain
enough of an explanation, that you can pair operations that increment the
ref count, with operations that are meant to decrement the refcount.

Each of these calls will generate at least one line of output in in the refs
log files. These lines look like this:
...
0x8756f00,+1,1234,chan_sip.c,22240,load_module,**constructor**,allocate users
0x86e3408,+1,1234,chan_sip.c,22241,load_module,**constructor**,allocate peers
0x86dd380,+1,1234,chan_sip.c,22242,load_module,**constructor**,allocate peers_by_ip
0x822d020,+1,1234,chan_sip.c,22243,load_module,**constructor**,allocate dialogs
0x8930fd8,+1,1234,chan_sip.c,20025,build_peer,**constructor**,allocate a peer struct
0x8930fd8,+1,1234,chan_sip.c,21467,reload_config,1,link peer into peer table
0x8930fd8,-1,1234,chan_sip.c,2370,unref_peer,2,unref_peer: from reload_config
0x89318b0,1,5678,chan_sip.c,20025,build_peer,**constructor**,allocate a peer struct
0x89318b0,+1,5678,chan_sip.c,21467,reload_config,1,link peer into peer table
0x89318b0,-1,1234,chan_sip.c,2370,unref_peer,2,unref_peer: from reload_config
0x8930218,+1,1234,chan_sip.c,20025,build_peer,**constructor**,allocate a peer struct
0x8930218,+1,1234,chan_sip.c,21539,reload_config,1,link peer into peers table
0x868c040,-1,1234,chan_sip.c,2424,dialog_unlink_all,2,unset the relatedpeer->call field in tandem with relatedpeer field itself
0x868c040,-1,1234,chan_sip.c,2443,dialog_unlink_all,1,Let's unbump the count in the unlink so the poor pvt can disappear if it is time
0x868c040,-1,1234,chan_sip.c,2443,dialog_unlink_all,**destructor**,Let's unbump the count in the unlink so the poor pvt can disappear if it is time
0x8cc07e8,-1,1234,chan_sip.c,2370,unref_peer,3,unsetting a dialog relatedpeer field in sip_destroy
0x8cc07e8,+1,1234,chan_sip.c,3876,find_peer,2,ao2_find in peers table
0x8cc07e8,-1,1234,chan_sip.c,2370,unref_peer,3,unref_peer, from sip_devicestate, release ref from find_peer
...

This uses a comma delineated format. The columns in the format are as
follows:
- The first column is the object address.
- The second column reflects how the operation affected the ref count
    for that object. A change in the ref count is reflected either as
    an increment (+) or decrement (-), as well as the amount it changed
    by.
- The third column is the ID of the thread that modified the reference
  count.
- The fourth column is the source file that the change in reference was
  issued from.
- The fifth column is the line number of the source file that the ref
  change was issued from.
- The sixth column is the name of the function that the ref change was
  issued from.
- The seventh column indicates either (a) construction of the object via
  the special tag **constructor**; (b) destruction of the object via
  the special tag **destructor**; (c) the previous reference count
  prior to this reference change.
- The eighth column is a special tag added by the developer to provide
  context for the ref change. Note that any subsequent columns are
  considered to be part of this tag.

Sometimes you have some helper functions to do object ref/unref
operations. Using these normally hides the place where these
functions were called. To get the location where these functions
were called to appear in /refs, you can do this sort of thing:

#ifdef REF_DEBUG
#define dialog_ref(arg1,arg2) dialog_ref_debug((arg1),(arg2), __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define dialog_unref(arg1,arg2) dialog_unref_debug((arg1),(arg2), __FILE__, __LINE__, __PRETTY_FUNCTION__)
static struct sip_pvt *dialog_ref_debug(struct sip_pvt *p, const char *tag, const char *file, int line, const char *func)
{
	if (p) {
		ao2_ref_debug(p, 1, tag, file, line, func);
	} else {
		ast_log(LOG_ERROR, "Attempt to Ref a null pointer\n");
	}
	return p;
}

static struct sip_pvt *dialog_unref_debug(struct sip_pvt *p, const char *tag, const char *file, int line, const char *func)
{
	if (p) {
		ao2_ref_debug(p, -1, tag, file, line, func);
	}
	return NULL;
}
#else
static struct sip_pvt *dialog_ref(struct sip_pvt *p, const char *tag)
{
	if (p) {
		ao2_ref(p, 1);
	} else {
		ast_log(LOG_ERROR, "Attempt to Ref a null pointer\n");
	}
	return p;
}

static struct sip_pvt *dialog_unref(struct sip_pvt *p, const char *tag)
{
	if (p) {
		ao2_ref(p, -1);
	}
	return NULL;
}
#endif

In the above code, note that the "normal" helper funcs call ao2_ref() as
normal, and the "helper" functions call ao2_ref_debug directly with the
file, function, and line number info provided. You might find this
well worth the effort to help track these function calls in the code.

To find out why objects are not destroyed (a common bug), you can
edit the source file to use the ao2_t_* variants, enable REF_DEBUG
in menuselect, and add a descriptive tag to each call. Recompile,
and run Asterisk, exit asterisk with "core stop gracefully", which should
result in every object being destroyed.

Then, you can "sort -k 1 {AST_LOG_DIR}/refs > x1" to get a sorted list of
all the objects, or you can use "contrib/script/refcounter.py" to scan
the file for you and output any problems it finds.

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

   0x83787a0,+1,1234,chan_sip.c,5733,sip_alloc,**constructor**,(allocate a dialog(pvt) struct)
   0x83787a0,-1,1234,chan_sip.c,19173,sip_poke_peer,4,(unref dialog at end of sip_poke_peer, obtained from sip_alloc, just before it goes out of scope)

   0x83787a0,+1,1234,chan_sip.c,5854,sip_alloc,1,(link pvt into dialogs table)
   0x83787a0,-1,1234,chan_sip.c,19150,sip_poke_peer,3,(About to change the callid -- remove the old name)
   0x83787a0,+1,1234,chan_sip.c,19152,sip_poke_peer,2,(Linking in under new name)
   0x83787a0,-1,1234,chan_sip.c,2399,dialog_unlink_all,5,(unlinking dialog via ao2_unlink)

   0x83787a0,+1,1234,chan_sip.c,19130,sip_poke_peer,2,(copy sip alloc from p to peer->call)


   0x83787a0,+1,1234,chan_sip.c,2996,__sip_reliable_xmit,3,(__sip_reliable_xmit: setting pkt->owner)
   0x83787a0,-1,1234,chan_sip.c,2425,dialog_unlink_all,4,(remove all current packets in this dialog, and the pointer to the dialog too as part of __sip_destroy)

   0x83787a0,+1,1234,chan_sip.c,22356,unload_module,4,(iterate thru dialogs)
   0x83787a0,-1,1234,chan_sip.c,22359,unload_module,5,(toss dialog ptr from iterator_next)


   0x83787a0,+1,1234,chan_sip.c,22373,unload_module,3,(iterate thru dialogs)
   0x83787a0,-1,1234,chan_sip.c,22375,unload_module,2,(throw away iterator result)

   0x83787a0,+1,1234,chan_sip.c,2397,dialog_unlink_all,4,(Let's bump the count in the unlink so it doesn't accidentally become dead before we are done)
   0x83787a0,-1,1234,chan_sip.c,2436,dialog_unlink_all,3,(Let's unbump the count in the unlink so the poor pvt can disappear if it is time)

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
		if (lockowner) {
			ast_channel_lock(dialog->owner);
		}
		ast_debug(1, "Detaching from channel %s\n", dialog->owner->name);
		dialog->owner->tech_pvt = dialog_unref(dialog->owner->tech_pvt, "resetting channel dialog ptr in unlink_all");
		if (lockowner) {
			ast_channel_unlock(dialog->owner);
		}
	}
	if (dialog->registry) {
		if (dialog->registry->call == dialog) {
			dialog->registry->call = dialog_unref(dialog->registry->call, "nulling out the registry's call dialog field in unlink_all");
		}
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



/*!
 * \brief Typedef for an object destructor.
 *
 * \param vdoomed Object to destroy.
 *
 * \details
 * This is called just before freeing the memory for the object.
 * It is passed a pointer to the user-defined data of the
 * object.
 *
 * \return Nothing
 */
typedef void (*ao2_destructor_fn)(void *vdoomed);

/*! \brief Options available when allocating an ao2 object. */
enum ao2_alloc_opts {
	/*! The ao2 object has a recursive mutex lock associated with it. */
	AO2_ALLOC_OPT_LOCK_MUTEX = (0 << 0),
	/*! The ao2 object has a non-recursive read/write lock associated with it. */
	AO2_ALLOC_OPT_LOCK_RWLOCK = (1 << 0),
	/*! The ao2 object has no lock associated with it. */
	AO2_ALLOC_OPT_LOCK_NOLOCK = (2 << 0),
	/*! The ao2 object locking option field mask. */
	AO2_ALLOC_OPT_LOCK_MASK = (3 << 0),
};

/*!
 * \brief Allocate and initialize an object.
 *
 * \param data_size The sizeof() of the user-defined structure.
 * \param destructor_fn The destructor function (can be NULL)
 * \param options The ao2 object options (See enum ao2_alloc_opts)
 * \param debug_msg An ao2 object debug tracing message.
 * \return A pointer to user-data.
 *
 * \details
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

#define ao2_t_alloc_options(data_size, destructor_fn, options, debug_msg) \
	__ao2_alloc_debug((data_size), (destructor_fn), (options), (debug_msg),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)
#define ao2_alloc_options(data_size, destructor_fn, options) \
	__ao2_alloc_debug((data_size), (destructor_fn), (options), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)

#define ao2_t_alloc(data_size, destructor_fn, debug_msg) \
	__ao2_alloc_debug((data_size), (destructor_fn), AO2_ALLOC_OPT_LOCK_MUTEX, (debug_msg),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)
#define ao2_alloc(data_size, destructor_fn) \
	__ao2_alloc_debug((data_size), (destructor_fn), AO2_ALLOC_OPT_LOCK_MUTEX, "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)

#elif defined(__AST_DEBUG_MALLOC)

#define ao2_t_alloc_options(data_size, destructor_fn, options, debug_msg) \
	__ao2_alloc_debug((data_size), (destructor_fn), (options), (debug_msg),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)
#define ao2_alloc_options(data_size, destructor_fn, options) \
	__ao2_alloc_debug((data_size), (destructor_fn), (options), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)

#define ao2_t_alloc(data_size, destructor_fn, debug_msg) \
	__ao2_alloc_debug((data_size), (destructor_fn), AO2_ALLOC_OPT_LOCK_MUTEX, (debug_msg),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)
#define ao2_alloc(data_size, destructor_fn) \
	__ao2_alloc_debug((data_size), (destructor_fn), AO2_ALLOC_OPT_LOCK_MUTEX, "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)

#else

#define ao2_t_alloc_options(data_size, destructor_fn, options, debug_msg) \
	__ao2_alloc((data_size), (destructor_fn), (options))
#define ao2_alloc_options(data_size, destructor_fn, options) \
	__ao2_alloc((data_size), (destructor_fn), (options))

#define ao2_t_alloc(data_size, destructor_fn, debug_msg) \
	__ao2_alloc((data_size), (destructor_fn), AO2_ALLOC_OPT_LOCK_MUTEX)
#define ao2_alloc(data_size, destructor_fn) \
	__ao2_alloc((data_size), (destructor_fn), AO2_ALLOC_OPT_LOCK_MUTEX)

#endif

void *__ao2_alloc_debug(size_t data_size, ao2_destructor_fn destructor_fn, unsigned int options, const char *tag,
	const char *file, int line, const char *func, int ref_debug) attribute_warn_unused_result;
void *__ao2_alloc(size_t data_size, ao2_destructor_fn destructor_fn, unsigned int options) attribute_warn_unused_result;

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

/*!
 * \brief Retrieve the ao2 options used to create the object.
 * \param obj pointer to the (user-defined part) of an object.
 * \return options from enum ao2_alloc_opts.
 */
unsigned int ao2_options_get(void *obj);

/*!
 * \since 12
 * \brief Bump refcount on an AO2 object by one, returning the object.
 *
 * This is useful for inlining a ref bump, and you don't care about the ref
 * count. Also \c NULL safe, for even more convenience.
 *
 * \param obj AO2 object to bump the refcount on.
 * \retval The given \a obj pointer.
 */
#define ao2_t_bump(obj, tag)						\
	({							\
		typeof(obj) __obj_ ## __LINE__ = (obj);		\
		if (__obj_ ## __LINE__) {			\
			ao2_t_ref(__obj_ ## __LINE__, +1, (tag));	\
		}						\
		__obj_ ## __LINE__;				\
	})
#define ao2_bump(obj) \
	ao2_t_bump((obj), "")

int __ao2_ref_debug(void *o, int delta, const char *tag, const char *file, int line, const char *func);
int __ao2_ref(void *o, int delta);

/*!
 * \since 12.4.0
 * \brief Replace one object reference with another cleaning up the original.
 *
 * \param dst Pointer to the object that will be cleaned up.
 * \param src Pointer to the object replacing it.
 */
#define ao2_t_replace(dst, src, tag) \
	{\
		typeof(dst) *__dst_ ## __LINE__ = &dst; \
		typeof(src) __src_ ## __LINE__ = src; \
		if (__src_ ## __LINE__ != *__dst_ ## __LINE__) { \
			if (__src_ ## __LINE__) {\
				ao2_t_ref(__src_ ## __LINE__, +1, (tag)); \
			} \
			if (*__dst_ ## __LINE__) {\
				ao2_t_ref(*__dst_ ## __LINE__, -1, (tag)); \
			} \
			*__dst_ ## __LINE__ = __src_ ## __LINE__; \
		} \
	}
#define ao2_replace(dst, src) \
	ao2_t_replace((dst), (src), "")

/*! @} */

/*! \brief ao2_weakproxy
 *
 * @{
 */
struct ao2_weakproxy_notification;
typedef void (*ao2_weakproxy_notification_cb)(void *weakproxy, void *data);

/*! \brief This struct should be opaque, but it's size is needed. */
struct ao2_weakproxy {
	AST_LIST_HEAD_NOLOCK(, ao2_weakproxy_notification) destroyed_cb;
};

/*! \brief Macro which must be used at the beginning of weakproxy capable objects.
 *
 * \note The primary purpose of user defined fields on weakproxy objects is to hold
 *       immutable container keys for the real object.
 */
#define AO2_WEAKPROXY() struct ao2_weakproxy __weakproxy##__LINE__

/*!
 * \since 14.0.0
 * \brief Allocate an ao2_weakproxy object
 *
 * \param data_size The sizeof() of the user-defined structure.
 * \param destructor_fn The destructor function (can be NULL)
 *
 * \note "struct ao2_weakproxy" must be the first field of any object.
 *       This can be done by using AO2_WEAKPROXY to declare your structure.
 */
void *__ao2_weakproxy_alloc(size_t data_size, ao2_destructor_fn destructor_fn,
	const char *tag, const char *file, int line, const char *func) attribute_warn_unused_result;

#define ao2_weakproxy_alloc(data_size, destructor_fn) \
	__ao2_weakproxy_alloc(data_size, destructor_fn, "", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ao2_t_weakproxy_alloc(data_size, destructor_fn, tag) \
	__ao2_weakproxy_alloc(data_size, destructor_fn, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \since 14.0.0
 * \brief Associate weakproxy with obj.
 *
 * \param weakproxy An object created by ao2_weakproxy_alloc.
 * \param obj An ao2 object not created by ao2_weakproxy_alloc.
 * \param flags OBJ_NOLOCK to avoid locking weakproxy.
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \note obj must be newly created, this procedure is not thread safe
 *       if any other code can reach obj before this procedure ends.
 *
 * \note weakproxy may be previously existing, but must not currently
 *       have an object set.
 *
 * \note The only way to unset an object is for it to be destroyed.
 *       Any call to this function while an object is already set will fail.
 */
int __ao2_weakproxy_set_object(void *weakproxy, void *obj, int flags,
	const char *tag, const char *file, int line, const char *func);

#define ao2_weakproxy_set_object(weakproxy, obj, flags) \
	__ao2_weakproxy_set_object(weakproxy, obj, flags, "", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ao2_t_weakproxy_set_object(weakproxy, obj, flags, tag) \
	__ao2_weakproxy_set_object(weakproxy, obj, flags, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \since 14.0.0
 * \brief Get the object associated with weakproxy.
 *
 * \param weakproxy The weakproxy to read from.
 * \param flags OBJ_NOLOCK to avoid locking weakproxy.
 *
 * \return A reference to the object previously set by ao2_weakproxy_set_object.
 * \retval NULL Either no object was set or the previously set object has been freed.
 */
void *__ao2_weakproxy_get_object(void *weakproxy, int flags,
	const char *tag, const char *file, int line, const char *func) attribute_warn_unused_result;

#define ao2_weakproxy_get_object(weakproxy, flags) \
	__ao2_weakproxy_get_object(weakproxy, flags, "", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ao2_t_weakproxy_get_object(weakproxy, flags, tag) \
	__ao2_weakproxy_get_object(weakproxy, flags, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \since 14.0.0
 * \brief Request notification when weakproxy points to NULL.
 *
 * \param weakproxy The weak object
 * \param cb Procedure to call when no real object is associated
 * \param data Passed to cb
 * \param flags OBJ_NOLOCK to avoid locking weakproxy.
 *
 * \retval 0 Success
 * \retval -1 Failure
 *
 * \note This procedure will allow the same cb / data pair to be added to
 *       the same weakproxy multiple times.
 *
 * \note It is the caller's responsibility to ensure that *data is valid
 *       until after cb() is run or ao2_weakproxy_unsubscribe is called.
 *
 * \note If the weakproxy currently points to NULL the callback will be run immediately,
 *       without being added to the subscriber list.
 */
int ao2_weakproxy_subscribe(void *weakproxy, ao2_weakproxy_notification_cb cb, void *data, int flags);

/*!
 * \since 14.0.0
 * \brief Remove notification of real object destruction.
 *
 * \param weakproxy The weak object
 * \param cb Callback to remove from destroy notification list
 * \param data Data pointer to match
 * \param flags OBJ_NOLOCK to avoid locking weakproxy.
 *              OBJ_MULTIPLE to remove all copies of the same cb / data pair.
 *
 * \return The number of subscriptions removed.
 * \retval 0 cb / data pair not found, nothing removed.
 * \retval -1 Failure due to invalid parameters.
 *
 * \note Unless flags includes OBJ_MULTIPLE, this will only remove a single copy
 *       of the cb / data pair.  If it was subscribed multiple times it must be
 *       unsubscribed as many times.  The OBJ_MULTIPLE flag can be used to remove
 *       matching subscriptions.
 */
int ao2_weakproxy_unsubscribe(void *weakproxy, ao2_weakproxy_notification_cb cb, void *data, int flags);

/*!
 * \since 14.0.0
 * \brief Get the weakproxy attached to obj
 *
 * \param obj The object to retreive a weakproxy from
 *
 * \return The weakproxy object
 */
void *__ao2_get_weakproxy(void *obj,
	const char *tag, const char *file, int line, const char *func) attribute_warn_unused_result;

#define ao2_get_weakproxy(obj) \
	__ao2_get_weakproxy(obj, "", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#define ao2_t_get_weakproxy(obj, tag) \
	__ao2_get_weakproxy(obj, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__)
/*! @} */


/*! \brief Which lock to request. */
enum ao2_lock_req {
	/*! Request the mutex lock be acquired. */
	AO2_LOCK_REQ_MUTEX,
	/*! Request the read lock be acquired. */
	AO2_LOCK_REQ_RDLOCK,
	/*! Request the write lock be acquired. */
	AO2_LOCK_REQ_WRLOCK,
};

/*! \brief
 * Lock an object.
 *
 * \param a A pointer to the object we want to lock.
 * \param lock_how, file, func, line, var
 * \return 0 on success, other values on error.
 */
int __ao2_lock(void *a, enum ao2_lock_req lock_how, const char *file, const char *func, int line, const char *var);
#define ao2_lock(a) __ao2_lock(a, AO2_LOCK_REQ_MUTEX, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)
#define ao2_rdlock(a) __ao2_lock(a, AO2_LOCK_REQ_RDLOCK, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)
#define ao2_wrlock(a) __ao2_lock(a, AO2_LOCK_REQ_WRLOCK, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)

/*! \brief
 * Unlock an object.
 *
 * \param a A pointer to the object we want unlock.
 * \param file, func, line, var
 * \return 0 on success, other values on error.
 */
int __ao2_unlock(void *a, const char *file, const char *func, int line, const char *var);
#define ao2_unlock(a) __ao2_unlock(a, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)

/*! \brief
 * Try locking-- (don't block if fail)
 *
 * \param a A pointer to the object we want to lock.
 * \param lock_how, file, func, line, var
 * \return 0 on success, other values on error.
 */
int __ao2_trylock(void *a, enum ao2_lock_req lock_how, const char *file, const char *func, int line, const char *var);
#define ao2_trylock(a) __ao2_trylock(a, AO2_LOCK_REQ_MUTEX, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)
#define ao2_tryrdlock(a) __ao2_trylock(a, AO2_LOCK_REQ_RDLOCK, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)
#define ao2_trywrlock(a) __ao2_trylock(a, AO2_LOCK_REQ_WRLOCK, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)

/*!
 * \brief Return the mutex lock address of an object
 *
 * \param[in] obj A pointer to the object we want.
 * \return the address of the mutex lock, else NULL.
 *
 * This function comes in handy mainly for debugging locking
 * situations, where the locking trace code reports the
 * lock address, this allows you to correlate against
 * object address, to match objects to reported locks.
 *
 * \since 1.6.1
 */
void *ao2_object_get_lockaddr(void *obj);


/*! Global ao2 object holder structure. */
struct ao2_global_obj {
	/*! Access lock to the held ao2 object. */
	ast_rwlock_t lock;
	/*! Global ao2 object. */
	void *obj;
};

/*!
 * \brief Define a global object holder to be used to hold an ao2 object, statically initialized.
 * \since 11.0
 *
 * \param name This will be the name of the object holder.
 *
 * \details
 * This macro creates a global object holder that can be used to
 * hold an ao2 object accessible using the API.  The structure is
 * allocated and initialized to be empty.
 *
 * Example usage:
 * \code
 * static AO2_GLOBAL_OBJ_STATIC(global_cfg);
 * \endcode
 *
 * This defines global_cfg, intended to hold an ao2 object
 * accessible using an API.
 */
#ifndef HAVE_PTHREAD_RWLOCK_INITIALIZER
#define AO2_GLOBAL_OBJ_STATIC(name)										\
	struct ao2_global_obj name;											\
	static void  __attribute__((constructor)) __init_##name(void)		\
	{																	\
		ast_rwlock_init(&name.lock);									\
		name.obj = NULL;												\
	}																	\
	static void  __attribute__((destructor)) __fini_##name(void)		\
	{																	\
		if (name.obj) {													\
			ao2_ref(name.obj, -1);										\
			name.obj = NULL;											\
		}																\
		ast_rwlock_destroy(&name.lock);									\
	}																	\
	struct __dummy_##name
#else
#define AO2_GLOBAL_OBJ_STATIC(name)										\
	struct ao2_global_obj name = {										\
		.lock = AST_RWLOCK_INIT_VALUE,									\
	}
#endif

/*!
 * \brief Release the ao2 object held in the global holder.
 * \since 11.0
 *
 * \param holder Global ao2 object holder.
 * \param tag used for debugging
 *
 * \return Nothing
 */
#ifdef REF_DEBUG
#define ao2_t_global_obj_release(holder, tag)	\
	__ao2_global_obj_release(&holder, (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#define ao2_global_obj_release(holder)	\
	__ao2_global_obj_release(&holder, "", __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)

#else

#define ao2_t_global_obj_release(holder, tag)	\
	__ao2_global_obj_release(&holder, NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#define ao2_global_obj_release(holder)	\
	__ao2_global_obj_release(&holder, NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#endif

void __ao2_global_obj_release(struct ao2_global_obj *holder, const char *tag, const char *file, int line, const char *func, const char *name);

/*!
 * \brief Replace an ao2 object in the global holder.
 * \since 11.0
 *
 * \param holder Global ao2 object holder.
 * \param obj Object to put into the holder.  Can be NULL.
 * \param tag used for debugging
 *
 * \note This function automatically increases the reference
 * count to account for the reference that the global holder now
 * holds to the object.
 *
 * \retval Reference to previous global ao2 object stored.
 * \retval NULL if no object available.
 */
#ifdef REF_DEBUG
#define ao2_t_global_obj_replace(holder, obj, tag)	\
	__ao2_global_obj_replace(&holder, (obj), (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#define ao2_global_obj_replace(holder, obj)	\
	__ao2_global_obj_replace(&holder, (obj), "", __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)

#else

#define ao2_t_global_obj_replace(holder, obj, tag)	\
	__ao2_global_obj_replace(&holder, (obj), NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#define ao2_global_obj_replace(holder, obj)	\
	__ao2_global_obj_replace(&holder, (obj), NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#endif

void *__ao2_global_obj_replace(struct ao2_global_obj *holder, void *obj, const char *tag, const char *file, int line, const char *func, const char *name) attribute_warn_unused_result;

/*!
 * \brief Replace an ao2 object in the global holder, throwing away any old object.
 * \since 11.0
 *
 * \param holder Global ao2 object holder.
 * \param obj Object to put into the holder.  Can be NULL.
 * \param tag used for debugging
 *
 * \note This function automatically increases the reference
 * count to account for the reference that the global holder now
 * holds to the object.  It also decreases the reference count
 * of any object being replaced.
 *
 * \retval 0 The global object was previously empty
 * \retval 1 The global object was not previously empty
 */
#ifdef REF_DEBUG
#define ao2_t_global_obj_replace_unref(holder, obj, tag)	\
	__ao2_global_obj_replace_unref(&holder, (obj), (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#define ao2_global_obj_replace_unref(holder, obj)	\
	__ao2_global_obj_replace_unref(&holder, (obj), "", __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)

#else

#define ao2_t_global_obj_replace_unref(holder, obj, tag)	\
	__ao2_global_obj_replace_unref(&holder, (obj), NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#define ao2_global_obj_replace_unref(holder, obj)	\
	__ao2_global_obj_replace_unref(&holder, (obj), NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#endif

int __ao2_global_obj_replace_unref(struct ao2_global_obj *holder, void *obj, const char *tag, const char *file, int line, const char *func, const char *name);

/*!
 * \brief Get a reference to the object stored in the global holder.
 * \since 11.0
 *
 * \param holder Global ao2 object holder.
 * \param tag used for debugging
 *
 * \retval Reference to current ao2 object stored in the holder.
 * \retval NULL if no object available.
 */
#ifdef REF_DEBUG
#define ao2_t_global_obj_ref(holder, tag)	\
	__ao2_global_obj_ref(&holder, (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#define ao2_global_obj_ref(holder)	\
	__ao2_global_obj_ref(&holder, "", __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)

#else

#define ao2_t_global_obj_ref(holder, tag)	\
	__ao2_global_obj_ref(&holder, NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#define ao2_global_obj_ref(holder)	\
	__ao2_global_obj_ref(&holder, NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, #holder)
#endif

void *__ao2_global_obj_ref(struct ao2_global_obj *holder, const char *tag, const char *file, int line, const char *func, const char *name) attribute_warn_unused_result;


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
    returns zero or more elements matching a given criteria
    (specified as arg). 'c' is the container pointer. Flags
    can be:
    OBJ_UNLINK - to remove the object, once found, from the container.
    OBJ_NODATA - don't return the object if found (no ref count change)
    OBJ_MULTIPLE - don't stop at first match
    OBJ_SEARCH_OBJECT - if set, 'arg' is an object pointer, and a hash table
                  search will be done. If not, a traversal is done.
    OBJ_SEARCH_KEY - if set, 'arg', is a search key item that is not an object.
              Similar to OBJ_SEARCH_OBJECT and mutually exclusive.
    OBJ_SEARCH_PARTIAL_KEY - if set, 'arg', is a partial search key item that is not an object.
              Similar to OBJ_SEARCH_KEY and mutually exclusive.

  -  \b ao2_callback(c, flags, fn, arg)
    apply fn(obj, arg) to all objects in the container.
    Similar to find. fn() can tell when to stop, and
    do anything with the object including unlinking it.
      - c is the container;
      - flags can be
         OBJ_UNLINK   - to remove the object, once found, from the container.
         OBJ_NODATA   - don't return the object if found (no ref count change)
         OBJ_MULTIPLE - don't stop at first match
         OBJ_SEARCH_OBJECT - if set, 'arg' is an object pointer, and a hash table
                        search will be done. If not, a traversal is done through
                        all the hash table 'buckets'..
         OBJ_SEARCH_KEY - if set, 'arg', is a search key item that is not an object.
                        Similar to OBJ_SEARCH_OBJECT and mutually exclusive.
         OBJ_SEARCH_PARTIAL_KEY - if set, 'arg', is a partial search key item that is not an object.
                        Similar to OBJ_SEARCH_KEY and mutually exclusive.
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
    locked, so nobody else can change its content while we work on it.
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
 * A callback function will return a combination of CMP_MATCH and CMP_STOP.
 * The latter will terminate the search in a container.
 */
enum _cb_results {
	CMP_MATCH	= 0x1,	/*!< the object matches the request */
	CMP_STOP	= 0x2,	/*!< stop the search now */
};

/*!
 * \brief Flags passed to ao2_callback_fn(), ao2_hash_fn(), and ao2_sort_fn() to modify behaviour.
 */
enum search_flags {
	/*!
	 * Unlink the object for which the callback function returned
	 * CMP_MATCH.
	 */
	OBJ_UNLINK = (1 << 0),
	/*!
	 * On match, don't return the object hence do not increase its
	 * refcount.
	 */
	OBJ_NODATA = (1 << 1),
	/*!
	 * Don't stop at the first match in ao2_callback() unless the
	 * result of of the callback function has the CMP_STOP bit set.
	 */
	OBJ_MULTIPLE = (1 << 2),
	/*!
	 * \brief Assume that the ao2_container is already locked.
	 *
	 * \note For ao2_containers that have mutexes, no locking will
	 * be done.
	 *
	 * \note For ao2_containers that have RWLOCKs, the lock will be
	 * promoted to write mode as needed.  The lock will be returned
	 * to the original locked state.
	 *
	 * \note Only use this flag if the ao2_container is manually
	 * locked already.
	 */
	OBJ_NOLOCK = (1 << 4),

	/*!
	 * \brief Search option field mask.
	 *
	 * \todo Eventually OBJ_SEARCH_MASK will shrink to a two bit
	 * field when the codebase is made to use the search field
	 * values as a field instead of independent bits.
	 */
	OBJ_SEARCH_MASK = (0x07 << 5),
	/*! \brief The arg parameter has no meaning to the astobj2 code. */
	OBJ_SEARCH_NONE = (0 << 5),
	/*!
	 * \brief The arg parameter is an object of the same type.
	 *
	 * \details
	 * The arg parameter is an object of the same type as the one
	 * being searched for, so use the object's ao2_hash_fn and/or
	 * ao2_sort_fn functions for optimized searching.
	 *
	 * \note The supplied ao2_callback_fn is called after the
	 * container nodes have been filtered by the ao2_hash_fn and/or
	 * ao2_sort_fn functions.
	 */
	OBJ_SEARCH_OBJECT = (1 << 5),
	/*!
	 * \brief The arg parameter is a search key, but is not an object.
	 *
	 * \details
	 * This can be used when you want to be able to pass custom data
	 * to the container's stored ao2_hash_fn, ao2_sort_fn, and
	 * ao2_find ao2_callback_fn functions that is not a full object,
	 * but perhaps just a string.
	 *
	 * \note The supplied ao2_callback_fn is called after the
	 * container nodes have been filtered by the ao2_hash_fn and/or
	 * ao2_sort_fn functions.
	 */
	OBJ_SEARCH_KEY = (2 << 5),
	/*!
	 * \brief The arg parameter is a partial search key similar to OBJ_SEARCH_KEY.
	 *
	 * \details
	 * The partial key can be used by the ao2_sort_fn to guide the
	 * search to find a contiguous subset of a sorted container.
	 * For example, a sorted container holds: "A", "B", "Bert",
	 * "Beth", "Earnie".  Doing a partial key search with "B" will
	 * find the sorted subset of all held objects starting with "B".
	 *
	 * \note The supplied ao2_callback_fn is called after the
	 * container nodes have been filtered by the ao2_sort_fn
	 * function.
	 */
	OBJ_SEARCH_PARTIAL_KEY = (4 << 5),

	/*! \brief Traverse order option field mask. */
	OBJ_ORDER_MASK = (0x03 << 8),
	/*! \brief Traverse in ascending order (First to last container object) */
	OBJ_ORDER_ASCENDING = (0 << 8),
	/*! \brief Traverse in descending order (Last to first container object) */
	OBJ_ORDER_DESCENDING = (1 << 8),
	/*!
	 * \brief Traverse in pre-order (Node then children, for tree container)
	 *
	 * \note For non-tree containers, it is up to the container type
	 * to make the best interpretation of the order.  For list and
	 * hash containers, this also means ascending order because a
	 * binary tree can degenerate into a list.
	 */
	OBJ_ORDER_PRE = (2 << 8),
	/*!
	 * \brief Traverse in post-order (Children then node, for tree container)
	 *
	 * \note For non-tree containers, it is up to the container type
	 * to make the best interpretation of the order.  For list and
	 * hash containers, this also means descending order because a
	 * binary tree can degenerate into a list.
	 */
	OBJ_ORDER_POST = (3 << 8),
};

/*
 * Deprecated backward compatible flag names.
 *
 * Note: OBJ_POINTER, OBJ_KEY, and OBJ_PARTIAL_KEY are mutually
 * exclusive.
 */
#define OBJ_POINTER		OBJ_SEARCH_OBJECT		/*!< Deprecated name */
#define OBJ_KEY			OBJ_SEARCH_KEY			/*!< Deprecated name */
#define OBJ_PARTIAL_KEY	OBJ_SEARCH_PARTIAL_KEY	/*!< Deprecated name */

/*!
 * \brief Options available when allocating an ao2 container object.
 *
 * \note Each option is open to some interpretation by the
 * container type as long as it makes sense with the option
 * name.
 */
enum ao2_container_opts {
	/*!
	 * \brief Insert objects at the beginning of the container.
	 * (Otherwise it is the opposite; insert at the end.)
	 *
	 * \note If an ao2_sort_fn is provided, the object is inserted
	 * before any objects with duplicate keys.
	 *
	 * \note Hash containers insert the object in the computed hash
	 * bucket in the indicated manner.
	 */
	AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN = (1 << 0),

	/*!
	 * \brief The ao2 container objects with duplicate keys option field mask.
	 */
	AO2_CONTAINER_ALLOC_OPT_DUPS_MASK = (3 << 1),
	/*!
	 * \brief Allow objects with duplicate keys in container.
	 */
	AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW = (0 << 1),
	/*!
	 * \brief Reject objects with duplicate keys in container.
	 *
	 * \note The container must be sorted.  i.e. have an
	 * ao2_sort_fn.
	 */
	AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT = (1 << 1),
	/*!
	 * \brief Reject duplicate objects in container.
	 *
	 * \details Don't link the same object into the container twice.
	 * However, you can link a different object with the same key.
	 *
	 * \note The container must be sorted.  i.e. have an
	 * ao2_sort_fn.
	 *
	 * \note It is assumed that the objects are located where the
	 * search key says they should be located.
	 */
	AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT = (2 << 1),
	/*!
	 * \brief Replace objects with duplicate keys in container.
	 *
	 * \details The existing duplicate object is removed and the new
	 * object takes the old object's place.
	 *
	 * \note The container must be sorted.  i.e. have an
	 * ao2_sort_fn.
	 */
	AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE = (3 << 1),
};

/*!
 * \brief Type of a generic callback function
 * \param obj  pointer to the (user-defined part) of an object.
 * \param arg callback argument from ao2_callback()
 * \param flags flags from ao2_callback()
 *   OBJ_SEARCH_OBJECT - if set, 'arg', is an object.
 *   OBJ_SEARCH_KEY - if set, 'arg', is a search key item that is not an object.
 *   OBJ_SEARCH_PARTIAL_KEY - if set, 'arg', is a partial search key item that is not an object.
 *
 * The return values are a combination of enum _cb_results.
 * Callback functions are used to search or manipulate objects in a container.
 */
typedef int (ao2_callback_fn)(void *obj, void *arg, int flags);

/*! \brief A common ao2_callback is one that matches by address. */
int ao2_match_by_addr(void *obj, void *arg, int flags);

/*!
 * \brief Type of a generic callback function
 * \param obj pointer to the (user-defined part) of an object.
 * \param arg callback argument from ao2_callback()
 * \param data arbitrary data from ao2_callback()
 * \param flags flags from ao2_callback()
 *   OBJ_SEARCH_OBJECT - if set, 'arg', is an object.
 *   OBJ_SEARCH_KEY - if set, 'arg', is a search key item that is not an object.
 *   OBJ_SEARCH_PARTIAL_KEY - if set, 'arg', is a partial search key item that is not an object.
 *
 * The return values are a combination of enum _cb_results.
 * Callback functions are used to search or manipulate objects in a container.
 */
typedef int (ao2_callback_data_fn)(void *obj, void *arg, void *data, int flags);

/*!
 * Type of a generic function to generate a hash value from an object.
 *
 * \param obj pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_SEARCH_OBJECT - if set, 'obj', is an object.
 *   OBJ_SEARCH_KEY - if set, 'obj', is a search key item that is not an object.
 *
 * \note This function must be idempotent.
 *
 * \return Computed hash value.
 */
typedef int (ao2_hash_fn)(const void *obj, int flags);

/*!
 * \brief Type of generic container sort function.
 *
 * \param obj_left pointer to the (user-defined part) of an object.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_SEARCH_OBJECT - if set, 'obj_right', is an object.
 *   OBJ_SEARCH_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_SEARCH_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 *
 * \note This function must be idempotent.
 *
 * \retval <0 if obj_left < obj_right
 * \retval =0 if obj_left == obj_right
 * \retval >0 if obj_left > obj_right
 */
typedef int (ao2_sort_fn)(const void *obj_left, const void *obj_right, int flags);

/*! \name Object Containers
 * Here start declarations of containers.
 */
/*@{ */
struct ao2_container;

/*!
 * \brief Allocate and initialize a hash container with the desired number of buckets.
 *
 * \details
 * We allocate space for a struct astobj_container, struct container
 * and the buckets[] array.
 *
 * \param options Container ao2 object options (See enum ao2_alloc_opts)
 * \param n_buckets Number of buckets for hash
 * \param hash_fn Pointer to a function computing a hash value. (NULL if everyting goes in first bucket.)
 * \param cmp_fn Pointer to a compare function used by ao2_find. (NULL to match everything)
 * \param tag used for debugging.
 *
 * \return A pointer to a struct container.
 *
 * \note Destructor is set implicitly.
 * \note This is legacy container creation that is mapped to the new method.
 */

#define ao2_t_container_alloc_options(options, n_buckets, hash_fn, cmp_fn, tag) \
	ao2_t_container_alloc_hash((options), 0, (n_buckets), (hash_fn), NULL, (cmp_fn), (tag))
#define ao2_container_alloc_options(options, n_buckets, hash_fn, cmp_fn) \
	ao2_container_alloc_hash((options), 0, (n_buckets), (hash_fn), NULL, (cmp_fn))

#define ao2_t_container_alloc(n_buckets, hash_fn, cmp_fn, tag) \
	ao2_t_container_alloc_options(AO2_ALLOC_OPT_LOCK_MUTEX, (n_buckets), (hash_fn), (cmp_fn), (tag))
#define ao2_container_alloc(n_buckets, hash_fn, cmp_fn) \
	ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_MUTEX, (n_buckets), (hash_fn), (cmp_fn))

/*!
 * \brief Allocate and initialize a hash container with the desired number of buckets.
 *
 * \details
 * We allocate space for a struct astobj_container, struct container
 * and the buckets[] array.
 *
 * \param ao2_options Container ao2 object options (See enum ao2_alloc_opts)
 * \param container_options Container behaviour options (See enum ao2_container_opts)
 * \param n_buckets Number of buckets for hash
 * \param hash_fn Pointer to a function computing a hash value. (NULL if everyting goes in first bucket.)
 * \param sort_fn Pointer to a sort function. (NULL to not sort the buckets.)
 * \param cmp_fn Pointer to a compare function used by ao2_find. (NULL to match everything)
 * \param tag used for debugging.
 *
 * \return A pointer to a struct container.
 *
 * \note Destructor is set implicitly.
 */

#if defined(REF_DEBUG)

#define ao2_t_container_alloc_hash(ao2_options, container_options, n_buckets, hash_fn, sort_fn, cmp_fn, tag) \
	__ao2_container_alloc_hash_debug((ao2_options), (container_options), (n_buckets), (hash_fn), (sort_fn), (cmp_fn), (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)
#define ao2_container_alloc_hash(ao2_options, container_options, n_buckets, hash_fn, sort_fn, cmp_fn) \
	__ao2_container_alloc_hash_debug((ao2_options), (container_options), (n_buckets), (hash_fn), (sort_fn), (cmp_fn), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)

#elif defined(__AST_DEBUG_MALLOC)

#define ao2_t_container_alloc_hash(ao2_options, container_options, n_buckets, hash_fn, sort_fn, cmp_fn, tag) \
	__ao2_container_alloc_hash_debug((ao2_options), (container_options), (n_buckets), (hash_fn), (sort_fn), (cmp_fn), (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)
#define ao2_container_alloc_hash(ao2_options, container_options, n_buckets, hash_fn, sort_fn, cmp_fn) \
	__ao2_container_alloc_hash_debug((ao2_options), (container_options), (n_buckets), (hash_fn), (sort_fn), (cmp_fn), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)

#else

#define ao2_t_container_alloc_hash(ao2_options, container_options, n_buckets, hash_fn, sort_fn, cmp_fn, tag) \
	__ao2_container_alloc_hash((ao2_options), (container_options), (n_buckets), (hash_fn), (sort_fn), (cmp_fn))
#define ao2_container_alloc_hash(ao2_options, container_options, n_buckets, hash_fn, sort_fn, cmp_fn) \
	__ao2_container_alloc_hash((ao2_options), (container_options), (n_buckets), (hash_fn), (sort_fn), (cmp_fn))

#endif

struct ao2_container *__ao2_container_alloc_hash(unsigned int ao2_options,
	unsigned int container_options, unsigned int n_buckets, ao2_hash_fn *hash_fn,
	ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn) attribute_warn_unused_result;
struct ao2_container *__ao2_container_alloc_hash_debug(unsigned int ao2_options,
	unsigned int container_options, unsigned int n_buckets, ao2_hash_fn *hash_fn,
	ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn,
	const char *tag, const char *file, int line, const char *func, int ref_debug) attribute_warn_unused_result;

/*!
 * \brief Allocate and initialize a list container.
 *
 * \param ao2_options Container ao2 object options (See enum ao2_alloc_opts)
 * \param container_options Container behaviour options (See enum ao2_container_opts)
 * \param sort_fn Pointer to a sort function. (NULL if list not sorted.)
 * \param cmp_fn Pointer to a compare function used by ao2_find. (NULL to match everything)
 * \param tag used for debugging.
 *
 * \return A pointer to a struct container.
 *
 * \note Destructor is set implicitly.
 * \note Implemented as a degenerate hash table.
 */

#if defined(REF_DEBUG)

#define ao2_t_container_alloc_list(ao2_options, container_options, sort_fn, cmp_fn, tag) \
	__ao2_container_alloc_list_debug((ao2_options), (container_options), (sort_fn), (cmp_fn), (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)
#define ao2_container_alloc_list(ao2_options, container_options, sort_fn, cmp_fn) \
	__ao2_container_alloc_list_debug((ao2_options), (container_options), (sort_fn), (cmp_fn), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)

#elif defined(__AST_DEBUG_MALLOC)

#define ao2_t_container_alloc_list(ao2_options, container_options, sort_fn, cmp_fn, tag) \
	__ao2_container_alloc_list_debug((ao2_options), (container_options), (sort_fn), (cmp_fn), (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)
#define ao2_container_alloc_list(ao2_options, container_options, sort_fn, cmp_fn) \
	__ao2_container_alloc_list_debug((ao2_options), (container_options), (sort_fn), (cmp_fn), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)

#else

#define ao2_t_container_alloc_list(ao2_options, container_options, sort_fn, cmp_fn, tag) \
	__ao2_container_alloc_list((ao2_options), (container_options), (sort_fn), (cmp_fn))
#define ao2_container_alloc_list(ao2_options, container_options, sort_fn, cmp_fn) \
	__ao2_container_alloc_list((ao2_options), (container_options), (sort_fn), (cmp_fn))

#endif

struct ao2_container *__ao2_container_alloc_list(unsigned int ao2_options,
	unsigned int container_options, ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn) attribute_warn_unused_result;
struct ao2_container *__ao2_container_alloc_list_debug(unsigned int ao2_options,
	unsigned int container_options, ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn,
	const char *tag, const char *file, int line, const char *func, int ref_debug) attribute_warn_unused_result;

/*!
 * \brief Allocate and initialize a red-black tree container.
 *
 * \param ao2_options Container ao2 object options (See enum ao2_alloc_opts)
 * \param container_options Container behaviour options (See enum ao2_container_opts)
 * \param sort_fn Pointer to a sort function.
 * \param cmp_fn Pointer to a compare function used by ao2_find. (NULL to match everything)
 * \param tag used for debugging.
 *
 * \return A pointer to a struct container.
 *
 * \note Destructor is set implicitly.
 */

#if defined(REF_DEBUG)

#define ao2_t_container_alloc_rbtree(ao2_options, container_options, sort_fn, cmp_fn, tag) \
	__ao2_container_alloc_rbtree_debug((ao2_options), (container_options), (sort_fn), (cmp_fn), (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)
#define ao2_container_alloc_rbtree(ao2_options, container_options, sort_fn, cmp_fn) \
	__ao2_container_alloc_rbtree_debug((ao2_options), (container_options), (sort_fn), (cmp_fn), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)

#elif defined(__AST_DEBUG_MALLOC)

#define ao2_t_container_alloc_rbtree(ao2_options, container_options, sort_fn, cmp_fn, tag) \
	__ao2_container_alloc_rbtree_debug((ao2_options), (container_options), (sort_fn), (cmp_fn), (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)
#define ao2_container_alloc_rbtree(ao2_options, container_options, sort_fn, cmp_fn) \
	__ao2_container_alloc_rbtree_debug((ao2_options), (container_options), (sort_fn), (cmp_fn), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)

#else

#define ao2_t_container_alloc_rbtree(ao2_options, container_options, sort_fn, cmp_fn, tag) \
	__ao2_container_alloc_rbtree((ao2_options), (container_options), (sort_fn), (cmp_fn))
#define ao2_container_alloc_rbtree(ao2_options, container_options, sort_fn, cmp_fn) \
	__ao2_container_alloc_rbtree((ao2_options), (container_options), (sort_fn), (cmp_fn))

#endif

struct ao2_container *__ao2_container_alloc_rbtree(unsigned int ao2_options, unsigned int container_options,
	ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn) attribute_warn_unused_result;
struct ao2_container *__ao2_container_alloc_rbtree_debug(unsigned int ao2_options, unsigned int container_options,
	ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn,
	const char *tag, const char *file, int line, const char *func, int ref_debug) attribute_warn_unused_result;

/*! \brief
 * Returns the number of elements in a container.
 */
int ao2_container_count(struct ao2_container *c);

/*!
 * \brief Copy all object references in the src container into the dest container.
 * \since 11.0
 *
 * \param dest Container to copy src object references into.
 * \param src Container to copy all object references from.
 * \param flags OBJ_NOLOCK if a lock is already held on both containers.
 *    Otherwise, the src container is locked first.
 *
 * \pre The dest container must be empty.  If the duplication fails, the
 * dest container will be returned empty.
 *
 * \note This can potentially be expensive because a malloc is
 * needed for every object in the src container.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ao2_container_dup(struct ao2_container *dest, struct ao2_container *src, enum search_flags flags);

/*!
 * \brief Create a clone/copy of the given container.
 * \since 11.0
 *
 * \param orig Container to copy all object references from.
 * \param flags OBJ_NOLOCK if a lock is already held on the container.
 *
 * \note This can potentially be expensive because a malloc is
 * needed for every object in the orig container.
 *
 * \retval Clone container on success.
 * \retval NULL on error.
 */
struct ao2_container *__ao2_container_clone(struct ao2_container *orig, enum search_flags flags) attribute_warn_unused_result;
struct ao2_container *__ao2_container_clone_debug(struct ao2_container *orig, enum search_flags flags, const char *tag, const char *file, int line, const char *func, int ref_debug) attribute_warn_unused_result;
#if defined(REF_DEBUG)

#define ao2_t_container_clone(orig, flags, tag)	__ao2_container_clone_debug(orig, flags, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)
#define ao2_container_clone(orig, flags)		__ao2_container_clone_debug(orig, flags, "", __FILE__, __LINE__, __PRETTY_FUNCTION__, 1)

#elif defined(__AST_DEBUG_MALLOC)

#define ao2_t_container_clone(orig, flags, tag)	__ao2_container_clone_debug(orig, flags, tag, __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)
#define ao2_container_clone(orig, flags)		__ao2_container_clone_debug(orig, flags, "", __FILE__, __LINE__, __PRETTY_FUNCTION__, 0)

#else

#define ao2_t_container_clone(orig, flags, tag)	__ao2_container_clone(orig, flags)
#define ao2_container_clone(orig, flags)		__ao2_container_clone(orig, flags)

#endif

/*!
 * \brief Print output.
 * \since 12.0.0
 *
 * \param where User data pointer needed to determine where to put output.
 * \param fmt printf type format string.
 *
 * \return Nothing
 */
typedef void (ao2_prnt_fn)(void *where, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/*!
 * \brief Print object key.
 * \since 12.0.0
 *
 * \param v_obj A pointer to the object we want the key printed.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 *
 * \return Nothing
 */
typedef void (ao2_prnt_obj_fn)(void *v_obj, void *where, ao2_prnt_fn *prnt);

/*!
 * \brief Display contents of the specified container.
 * \since 12.0.0
 *
 * \param self Container to dump.
 * \param flags OBJ_NOLOCK if a lock is already held on the container.
 * \param name Container name.  (NULL if anonymous)
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 * \param prnt_obj Callback function to print the given object's key. (NULL if not available)
 *
 * \return Nothing
 */
void ao2_container_dump(struct ao2_container *self, enum search_flags flags, const char *name, void *where, ao2_prnt_fn *prnt, ao2_prnt_obj_fn *prnt_obj);

/*!
 * \brief Display statistics of the specified container.
 * \since 12.0.0
 *
 * \param self Container to display statistics.
 * \param flags OBJ_NOLOCK if a lock is already held on the container.
 * \param name Container name.  (NULL if anonymous)
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 *
 * \return Nothing
 */
void ao2_container_stats(struct ao2_container *self, enum search_flags flags, const char *name, void *where, ao2_prnt_fn *prnt);

/*!
 * \brief Perform an integrity check on the specified container.
 * \since 12.0.0
 *
 * \param self Container to check integrity.
 * \param flags OBJ_NOLOCK if a lock is already held on the container.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ao2_container_check(struct ao2_container *self, enum search_flags flags);

/*!
 * \brief Register a container for CLI stats and integrity check.
 * \since 12.0.0
 *
 * \param name Name to register the container under.
 * \param self Container to register.
 * \param prnt_obj Callback function to print the given object's key. (NULL if not available)
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ao2_container_register(const char *name, struct ao2_container *self, ao2_prnt_obj_fn *prnt_obj);

/*!
 * \brief Unregister a container for CLI stats and integrity check.
 * \since 12.0.0
 *
 * \param name Name the container is registered under.
 *
 * \return Nothing
 */
void ao2_container_unregister(const char *name);

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
 * \param container The container to operate on.
 * \param obj The object to be added.
 * \param tag used for debugging.
 *
 * \retval 0 on errors.
 * \retval 1 on success.
 *
 * This function inserts an object in a container according its key.
 *
 * \note Remember to set the key before calling this function.
 *
 * \note This function automatically increases the reference count to account
 *       for the reference that the container now holds to the object.
 */
#ifdef REF_DEBUG

#define ao2_t_link(container, obj, tag)					__ao2_link_debug((container), (obj), 0, (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_link(container, obj)						__ao2_link_debug((container), (obj), 0, "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief Add an object to a container.
 *
 * \param container The container to operate on.
 * \param obj The object to be added.
 * \param flags search_flags to control linking the object.  (OBJ_NOLOCK)
 * \param tag used for debugging.
 *
 * \retval 0 on errors.
 * \retval 1 on success.
 *
 * This function inserts an object in a container according its key.
 *
 * \note Remember to set the key before calling this function.
 *
 * \note This function automatically increases the reference count to account
 *       for the reference that the container now holds to the object.
 */
#define ao2_t_link_flags(container, obj, flags, tag)	__ao2_link_debug((container), (obj), (flags), (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_link_flags(container, obj, flags)			__ao2_link_debug((container), (obj), (flags), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_link(container, obj, tag)					__ao2_link((container), (obj), 0)
#define ao2_link(container, obj)						__ao2_link((container), (obj), 0)

#define ao2_t_link_flags(container, obj, flags, tag)	__ao2_link((container), (obj), (flags))
#define ao2_link_flags(container, obj, flags)			__ao2_link((container), (obj), (flags))

#endif

int __ao2_link_debug(struct ao2_container *c, void *obj_new, int flags, const char *tag, const char *file, int line, const char *func);
int __ao2_link(struct ao2_container *c, void *obj_new, int flags);

/*!
 * \brief Remove an object from a container
 *
 * \param container The container to operate on.
 * \param obj The object to unlink.
 * \param tag used for debugging.
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

#define ao2_t_unlink(container, obj, tag)				__ao2_unlink_debug((container), (obj), 0, (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_unlink(container, obj)						__ao2_unlink_debug((container), (obj), 0, "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief Remove an object from a container
 *
 * \param container The container to operate on.
 * \param obj The object to unlink.
 * \param flags search_flags to control unlinking the object.  (OBJ_NOLOCK)
 * \param tag used for debugging.
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

#define ao2_t_unlink_flags(container, obj, flags, tag)	__ao2_unlink_debug((container), (obj), (flags), (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_unlink_flags(container, obj, flags)			__ao2_unlink_debug((container), (obj), (flags), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_unlink(container, obj, tag)				__ao2_unlink((container), (obj), 0)
#define ao2_unlink(container, obj)						__ao2_unlink((container), (obj), 0)

#define ao2_t_unlink_flags(container, obj, flags, tag)	__ao2_unlink((container), (obj), (flags))
#define ao2_unlink_flags(container, obj, flags)			__ao2_unlink((container), (obj), (flags))

#endif

void *__ao2_unlink_debug(struct ao2_container *c, void *obj, int flags, const char *tag, const char *file, int line, const char *func);
void *__ao2_unlink(struct ao2_container *c, void *obj, int flags);


/*@} */

/*! \brief
 * ao2_callback() is a generic function that applies cb_fn() to all objects
 * in a container, as described below.
 *
 * \param c A pointer to the container to operate on.
 * \param flags A set of flags specifying the operation to perform,
 *  partially used by the container code, but also passed to
 *  the callback.
 *   - If OBJ_NODATA is set, ao2_callback will return NULL. No refcounts
 *     of any of the traversed objects will be incremented.
 *     On the converse, if it is NOT set (the default), the ref count
 *     of the first matching object will be incremented and returned.
 *   - If OBJ_MULTIPLE is set, the ref count of all matching objects will
 *     be incremented in an iterator for a temporary container and returned.
 *   - If OBJ_SEARCH_OBJECT is set, the traversed items will be restricted
 *     to the objects in the bucket that the object key hashes to.
 *   - If OBJ_SEARCH_KEY is set, the traversed items will be restricted
 *     to the objects in the bucket that the object key hashes to.
 * \param cb_fn A function pointer, that will be called on all
 *  objects, to see if they match. This function returns CMP_MATCH
 *  if the object is matches the criteria; CMP_STOP if the traversal
 *  should immediately stop, or both (via bitwise ORing), if you find a
 *  match and want to end the traversal, and 0 if the object is not a match,
 *  but the traversal should continue. This is the function that is applied
 *  to each object traversed. Its arguments are:
 *      (void *obj, void *arg, int flags), where:
 *        obj is an object
 *        arg is the same as arg passed into ao2_callback
 *        flags is the same as flags passed into ao2_callback (flags are
 *         also used by ao2_callback).
 * \param arg passed to the callback.
 * \param tag used for debugging.
 *
 * \retval NULL on failure or no matching object found.
 *
 * \retval object found if OBJ_MULTIPLE is not set in the flags
 * parameter.
 *
 * \retval ao2_iterator pointer if OBJ_MULTIPLE is set in the
 * flags parameter.  The iterator must be destroyed with
 * ao2_iterator_destroy() when the caller no longer needs it.
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
 *
 * This function searches through a container and performs operations
 * on objects according on flags passed.
 * XXX describe better
 * The comparison is done calling the compare function set implicitly.
 * The arg pointer can be a pointer to an object or to a key,
 * we can say this looking at flags value.
 * If arg points to an object we will search for the object pointed
 * by this value, otherwise we search for a key value.
 * If the key is not unique we only find the first matching value.
 *
 * The use of flags argument is the follow:
 *
 *      OBJ_UNLINK              unlinks the object found
 *      OBJ_NODATA              on match, do return an object
 *                              Callbacks use OBJ_NODATA as a default
 *                              functions such as find() do
 *      OBJ_MULTIPLE            return multiple matches
 *                              Default is no.
 *      OBJ_SEARCH_OBJECT       the pointer is to an object
 *      OBJ_SEARCH_KEY          the pointer is to a search key
 *      OBJ_SEARCH_PARTIAL_KEY  the pointer is to a partial search key
 *
 * \note When the returned object is no longer in use, ao2_ref() should
 * be used to free the additional reference possibly created by this function.
 *
 * @{
 */
#ifdef REF_DEBUG

#define ao2_t_callback(c, flags, cb_fn, arg, tag) \
	__ao2_callback_debug((c), (flags), (cb_fn), (arg), (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_callback(c, flags, cb_fn, arg) \
	__ao2_callback_debug((c), (flags), (cb_fn), (arg), "", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_callback(c, flags, cb_fn, arg, tag) \
	__ao2_callback((c), (flags), (cb_fn), (arg))
#define ao2_callback(c, flags, cb_fn, arg) \
	__ao2_callback((c), (flags), (cb_fn), (arg))

#endif

void *__ao2_callback_debug(struct ao2_container *c, enum search_flags flags,
	ao2_callback_fn *cb_fn, void *arg, const char *tag, const char *file, int line,
	const char *func);
void *__ao2_callback(struct ao2_container *c, enum search_flags flags, ao2_callback_fn *cb_fn, void *arg);

/*! @} */

/*! \brief
 * ao2_callback_data() is a generic function that applies cb_fn() to all objects
 * in a container.  It is functionally identical to ao2_callback() except that
 * instead of taking an ao2_callback_fn *, it takes an ao2_callback_data_fn *, and
 * allows the caller to pass in arbitrary data.
 *
 * This call would be used instead of ao2_callback() when the caller needs to pass
 * OBJ_SEARCH_OBJECT, OBJ_SEARCH_KEY, or OBJ_SEARCH_PARTIAL_KEY as part of the flags
 * argument (which in turn requires passing in a known pointer type for 'arg') and
 * also needs access to other non-global data to complete it's comparison or task.
 *
 * See the documentation for ao2_callback() for argument descriptions.
 *
 * \see ao2_callback()
 */
#ifdef REF_DEBUG

#define ao2_t_callback_data(container, flags, cb_fn, arg, data, tag) \
	__ao2_callback_data_debug((container), (flags), (cb_fn), (arg), (data), (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_callback_data(container, flags, cb_fn, arg, data) \
	__ao2_callback_data_debug((container), (flags), (cb_fn), (arg), (data), "", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_callback_data(container, flags, cb_fn, arg, data, tag) \
	__ao2_callback_data((container), (flags), (cb_fn), (arg), (data))
#define ao2_callback_data(container, flags, cb_fn, arg, data) \
	__ao2_callback_data((container), (flags), (cb_fn), (arg), (data))

#endif

void *__ao2_callback_data_debug(struct ao2_container *c, enum search_flags flags,
	ao2_callback_data_fn *cb_fn, void *arg, void *data, const char *tag, const char *file,
	int line, const char *func);
void *__ao2_callback_data(struct ao2_container *c, enum search_flags flags,
	ao2_callback_data_fn *cb_fn, void *arg, void *data);

/*! ao2_find() is a short hand for ao2_callback(c, flags, c->cmp_fn, arg)
 * XXX possibly change order of arguments ?
 */
#ifdef REF_DEBUG

#define ao2_t_find(container, arg, flags, tag) \
	__ao2_find_debug((container), (arg), (flags), (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_find(container, arg, flags) \
	__ao2_find_debug((container), (arg), (flags), "", __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_find(container, arg, flags, tag) \
	__ao2_find((container), (arg), (flags))
#define ao2_find(container, arg, flags) \
	__ao2_find((container), (arg), (flags))

#endif

void *__ao2_find_debug(struct ao2_container *c, const void *arg, enum search_flags flags,
	const char *tag, const char *file, int line, const char *func);
void *__ao2_find(struct ao2_container *c, const void *arg, enum search_flags flags);

/*! \brief
 *
 *
 * When we need to walk through a container, we use an
 * ao2_iterator to keep track of the current position.
 *
 * Because the navigation is typically done without holding the
 * lock on the container across the loop, objects can be
 * inserted or deleted or moved while we work.  As a
 * consequence, there is no guarantee that we manage to touch
 * all the elements in the container, and it is possible that we
 * touch the same object multiple times.
 *
 * An iterator must be first initialized with
 * ao2_iterator_init(), then we can use o = ao2_iterator_next()
 * to move from one element to the next.  Remember that the
 * object returned by ao2_iterator_next() has its refcount
 * incremented, and the reference must be explicitly released
 * when done with it.
 *
 * In addition, ao2_iterator_init() will hold a reference to the
 * container being iterated and the last container node found.
 * Thes objects will be unreffed when ao2_iterator_destroy() is
 * called to free up the resources used by the iterator (if
 * any).
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
 *  ao2_iterator_restart(&i);
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

/*!
 * \brief The astobj2 iterator
 *
 * \note You are not supposed to know the internals of an iterator!
 * We would like the iterator to be opaque, unfortunately
 * its size needs to be known if we want to store it around
 * without too much trouble.
 * Anyways...
 * The iterator has a pointer to the container, and a flags
 * field specifying various things e.g. whether the container
 * should be locked or not while navigating on it.
 * The iterator "points" to the current container node.
 *
 * Details are in the implementation of ao2_iterator_next()
 */
struct ao2_iterator {
	/*! The container (Has a reference) */
	struct ao2_container *c;
	/*! Last container node (Has a reference) */
	void *last_node;
	/*! Nonzero if the iteration has completed. */
	int complete;
	/*! operation flags (enum ao2_iterator_flags) */
	int flags;
};

/*! Flags that can be passed to ao2_iterator_init() to modify the behavior
 * of the iterator.
 */
enum ao2_iterator_flags {
	/*!
	 * \brief Assume that the ao2_container is already locked.
	 *
	 * \note For ao2_containers that have mutexes, no locking will
	 * be done.
	 *
	 * \note For ao2_containers that have RWLOCKs, the lock will be
	 * promoted to write mode as needed.  The lock will be returned
	 * to the original locked state.
	 *
	 * \note Only use this flag if the ao2_container is manually
	 * locked already.  You should hold the lock until after
	 * ao2_iterator_destroy().  If you must release the lock then
	 * you must at least hold the lock whenever you call an
	 * ao2_iterator_xxx function with this iterator.
	 */
	AO2_ITERATOR_DONTLOCK = (1 << 0),
	/*!
	 * Indicates that the iterator was dynamically allocated by
	 * astobj2 API and should be freed by ao2_iterator_destroy().
	 */
	AO2_ITERATOR_MALLOCD = (1 << 1),
	/*!
	 * Indicates that before the iterator returns an object from
	 * the container being iterated, the object should be unlinked
	 * from the container.
	 */
	AO2_ITERATOR_UNLINK = (1 << 2),
	/*!
	 * Iterate in descending order (Last to first container object)
	 * (Otherwise ascending order)
	 *
	 * \note Other traversal orders such as pre-order and post-order
	 * do not make sense because they require the container
	 * structure to be static during the traversal.  Iterators just
	 * about guarantee that is not going to happen because the
	 * container is allowed to change by other threads during the
	 * iteration.
	 */
	AO2_ITERATOR_DESCENDING = (1 << 3),
};

/*!
 * \brief Create an iterator for a container
 *
 * \param c the container
 * \param flags one or more flags from ao2_iterator_flags.
 *
 * \retval the constructed iterator
 *
 * \note This function does \b not take a pointer to an iterator;
 *       rather, it returns an iterator structure that should be
 *       assigned to (overwriting) an existing iterator structure
 *       allocated on the stack or on the heap.
 *
 * This function will take a reference on the container being iterated.
 */
struct ao2_iterator ao2_iterator_init(struct ao2_container *c, int flags) attribute_warn_unused_result;

/*!
 * \brief Destroy a container iterator
 *
 * \param iter the iterator to destroy
 *
 * \retval none
 *
 * This function will release the container reference held by the iterator
 * and any other resources it may be holding.
 */
#if defined(TEST_FRAMEWORK)
void ao2_iterator_destroy(struct ao2_iterator *iter) __attribute__((noinline));
#else
void ao2_iterator_destroy(struct ao2_iterator *iter);
#endif	/* defined(TEST_FRAMEWORK) */

#ifdef REF_DEBUG

#define ao2_t_iterator_next(iter, tag) __ao2_iterator_next_debug((iter), (tag),  __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_iterator_next(iter)        __ao2_iterator_next_debug((iter), "",  __FILE__, __LINE__, __PRETTY_FUNCTION__)

#else

#define ao2_t_iterator_next(iter, tag) __ao2_iterator_next((iter))
#define ao2_iterator_next(iter)        __ao2_iterator_next((iter))

#endif

void *__ao2_iterator_next_debug(struct ao2_iterator *iter, const char *tag, const char *file, int line, const char *func) attribute_warn_unused_result;
void *__ao2_iterator_next(struct ao2_iterator *iter) attribute_warn_unused_result;

/*!
 * \brief Restart an iteration.
 *
 * \param iter the iterator to restart
 *
 * \note A restart is not going to have any effect if the
 * iterator was created with the AO2_ITERATOR_UNLINK flag.  Any
 * previous objects returned were removed from the container.
 *
 * \retval none
 */
void ao2_iterator_restart(struct ao2_iterator *iter);

/*! gcc __attribute__(cleanup()) functions
 * \note they must be able to handle NULL parameters because most of the
 * allocation/find functions can fail and we don't want to try to tear
 * down a NULL */
void __ao2_cleanup(void *obj);
void __ao2_cleanup_debug(void *obj, const char *tag, const char *file, int line, const char *function);
#ifdef REF_DEBUG
#define ao2_cleanup(obj) __ao2_cleanup_debug((obj), "", __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ao2_t_cleanup(obj, tag) __ao2_cleanup_debug((obj), (tag), __FILE__, __LINE__, __PRETTY_FUNCTION__)
#else
#define ao2_cleanup(obj) __ao2_cleanup(obj)
#define ao2_t_cleanup(obj, tag) __ao2_cleanup((obj))
#endif
void ao2_iterator_cleanup(struct ao2_iterator *iter);

/*!
 * \brief Get a count of the iterated container objects.
 *
 * \param iter the iterator to query
 *
 * \retval The number of objects in the iterated container
 */
int ao2_iterator_count(struct ao2_iterator *iter);

#endif /* _ASTERISK_ASTOBJ2_H */
