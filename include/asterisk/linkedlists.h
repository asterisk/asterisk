/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
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

#ifndef ASTERISK_LINKEDLISTS_H
#define ASTERISK_LINKEDLISTS_H

#include "asterisk/lock.h"

/*!
  \file linkedlists.h
  \brief A set of macros to manage forward-linked lists.
*/

/*!
  \brief Attempts to lock a list.
  \param head This is a pointer to the list head structure

  This macro attempts to place an exclusive lock in the
  list head structure pointed to by head.
  Returns 0 on success, non-zero on failure
*/
#define AST_LIST_LOCK(head)						\
	ast_mutex_lock(&(head)->lock) 
	
/*!
  \brief Attempts to unlock a list.
  \param head This is a pointer to the list head structure

  This macro attempts to remove an exclusive lock from the
  list head structure pointed to by head. If the list
  was not locked by this thread, this macro has no effect.
*/
#define AST_LIST_UNLOCK(head) 						\
	ast_mutex_unlock(&(head)->lock)

/*!
  \brief Defines a structure to be used to hold a list of specified type.
  \param name This will be the name of the defined structure.
  \param type This is the type of each list entry.

  This macro creates a structure definition that can be used
  to hold a list of the entries of type \a type. It does not actually
  declare (allocate) a structure; to do that, either follow this
  macro with the desired name of the instance you wish to declare,
  or use the specified \a name to declare instances elsewhere.

  Example usage:
  \code
  static AST_LIST_HEAD(entry_list, entry) entries;
  \endcode

  This would define \c struct \c entry_list, and declare an instance of it named
  \a entries, all intended to hold a list of type \c struct \c entry.
*/
#define AST_LIST_HEAD(name, type)					\
struct name {								\
	struct type *first;						\
	struct type *last;						\
	ast_mutex_t lock;						\
}

/*!
  \brief Defines a structure to be used to hold a list of specified type (with no lock).
  \param name This will be the name of the defined structure.
  \param type This is the type of each list entry.

  This macro creates a structure definition that can be used
  to hold a list of the entries of type \a type. It does not actually
  declare (allocate) a structure; to do that, either follow this
  macro with the desired name of the instance you wish to declare,
  or use the specified \a name to declare instances elsewhere.

  Example usage:
  \code
  static AST_LIST_HEAD_NOLOCK(entry_list, entry) entries;
  \endcode

  This would define \c struct \c entry_list, and declare an instance of it named
  \a entries, all intended to hold a list of type \c struct \c entry.
*/
#define AST_LIST_HEAD_NOLOCK(name, type)				\
struct name {								\
	struct type *first;						\
	struct type *last;						\
}

/*!
  \brief Defines initial values for a declaration of AST_LIST_HEAD
*/
#define AST_LIST_HEAD_INIT_VALUE	{		\
	.first = NULL,					\
	.last = NULL,					\
	.lock = AST_MUTEX_INIT_VALUE,			\
	}

/*!
  \brief Defines initial values for a declaration of AST_LIST_HEAD_NOLOCK
*/
#define AST_LIST_HEAD_NOLOCK_INIT_VALUE	{	\
	.first = NULL,					\
	.last = NULL,					\
	}

/*!
  \brief Defines a structure to be used to hold a list of specified type, statically initialized.
  \param name This will be the name of the defined structure.
  \param type This is the type of each list entry.

  This macro creates a structure definition that can be used
  to hold a list of the entries of type \a type, and allocates an instance
  of it, initialized to be empty.

  Example usage:
  \code
  static AST_LIST_HEAD_STATIC(entry_list, entry);
  \endcode

  This would define \c struct \c entry_list, intended to hold a list of
  type \c struct \c entry.
*/
#define AST_LIST_HEAD_STATIC(name, type)				\
struct name {								\
	struct type *first;						\
	struct type *last;						\
	ast_mutex_t lock;						\
} name = AST_LIST_HEAD_INIT_VALUE

/*!
  \brief Defines a structure to be used to hold a list of specified type, statically initialized.

  This is the same as AST_LIST_HEAD_STATIC, except without the lock included.
*/
#define AST_LIST_HEAD_NOLOCK_STATIC(name, type)				\
struct name {								\
	struct type *first;						\
	struct type *last;						\
} name = AST_LIST_HEAD_NOLOCK_INIT_VALUE

/*!
  \brief Initializes a list head structure with a specified first entry.
  \param head This is a pointer to the list head structure
  \param entry pointer to the list entry that will become the head of the list

  This macro initializes a list head structure by setting the head
  entry to the supplied value and recreating the embedded lock.
*/
#define AST_LIST_HEAD_SET(head, entry) do {				\
	(head)->first = (entry);					\
	(head)->last = (entry);						\
	ast_mutex_init(&(head)->lock);					\
} while (0)

/*!
  \brief Initializes a list head structure with a specified first entry.
  \param head This is a pointer to the list head structure
  \param entry pointer to the list entry that will become the head of the list

  This macro initializes a list head structure by setting the head
  entry to the supplied value.
*/
#define AST_LIST_HEAD_SET_NOLOCK(head, entry) do {			\
	(head)->first = (entry);					\
	(head)->last = (entry);						\
} while (0)

/*!
  \brief Declare a forward link structure inside a list entry.
  \param type This is the type of each list entry.

  This macro declares a structure to be used to link list entries together.
  It must be used inside the definition of the structure named in
  \a type, as follows:

  \code
  struct list_entry {
  	...
  	AST_LIST_ENTRY(list_entry) list;
  }
  \endcode

  The field name \a list here is arbitrary, and can be anything you wish.
*/
#define AST_LIST_ENTRY(type)						\
struct {								\
	struct type *next;						\
}
 
/*!
  \brief Returns the first entry contained in a list.
  \param head This is a pointer to the list head structure
 */
#define	AST_LIST_FIRST(head)	((head)->first)

/*!
  \brief Returns the last entry contained in a list.
  \param head This is a pointer to the list tail structure
 */
#define	AST_LIST_LAST(head)	((head)->last)

/*!
  \brief Returns the next entry in the list after the given entry.
  \param elm This is a pointer to the current entry.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.
*/
#define AST_LIST_NEXT(elm, field)	((elm)->field.next)

/*!
  \brief Checks whether the specified list contains any entries.
  \param head This is a pointer to the list head structure

  Returns non-zero if the list has entries, zero if not.
 */
#define	AST_LIST_EMPTY(head)	(AST_LIST_FIRST(head) == NULL)

/*!
  \brief Loops over (traverses) the entries in a list.
  \param head This is a pointer to the list head structure
  \param var This is the name of the variable that will hold a pointer to the
  current list entry on each iteration. It must be declared before calling
  this macro.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  This macro is use to loop over (traverse) the entries in a list. It uses a
  \a for loop, and supplies the enclosed code with a pointer to each list
  entry as it loops. It is typically used as follows:
  \code
  static AST_LIST_HEAD(entry_list, list_entry) entries;
  ...
  struct list_entry {
  	...
  	AST_LIST_ENTRY(list_entry) list;
  }
  ...
  struct list_entry *current;
  ...
  AST_LIST_TRAVERSE(&entries, current, list) {
     (do something with current here)
  }
  \endcode
  \warning If you modify the forward-link pointer contained in the \a current entry while
  inside the loop, the behavior will be unpredictable. At a minimum, the following
  macros will modify the forward-link pointer, and should not be used inside
  AST_LIST_TRAVERSE() against the entry pointed to by the \a current pointer without
  careful consideration of their consequences:
  \li AST_LIST_NEXT() (when used as an lvalue)
  \li AST_LIST_INSERT_AFTER()
  \li AST_LIST_INSERT_HEAD()
  \li AST_LIST_INSERT_TAIL()
*/
#define AST_LIST_TRAVERSE(head,var,field) 				\
	for((var) = (head)->first; (var); (var) = (var)->field.next)

/*!
  \brief Loops safely over (traverses) the entries in a list.
  \param head This is a pointer to the list head structure
  \param var This is the name of the variable that will hold a pointer to the
  current list entry on each iteration. It must be declared before calling
  this macro.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  This macro is used to safely loop over (traverse) the entries in a list. It
  uses a \a for loop, and supplies the enclosed code with a pointer to each list
  entry as it loops. It is typically used as follows:

  \code
  static AST_LIST_HEAD(entry_list, list_entry) entries;
  ...
  struct list_entry {
  	...
  	AST_LIST_ENTRY(list_entry) list;
  }
  ...
  struct list_entry *current;
  ...
  AST_LIST_TRAVERSE_SAFE_BEGIN(&entries, current, list) {
     (do something with current here)
  }
  AST_LIST_TRAVERSE_SAFE_END;
  \endcode

  It differs from AST_LIST_TRAVERSE() in that the code inside the loop can modify
  (or even free, after calling AST_LIST_REMOVE_CURRENT()) the entry pointed to by
  the \a current pointer without affecting the loop traversal.
*/
#define AST_LIST_TRAVERSE_SAFE_BEGIN(head, var, field) {				\
	typeof((head)->first) __list_next;						\
	typeof((head)->first) __list_prev = NULL;					\
	typeof((head)->first) __new_prev = NULL;					\
	for ((var) = (head)->first, __new_prev = (var),					\
	      __list_next = (var) ? (var)->field.next : NULL;				\
	     (var);									\
	     __list_prev = __new_prev, (var) = __list_next,				\
	     __new_prev = (var),							\
	     __list_next = (var) ? (var)->field.next : NULL				\
	    )

/*!
  \brief Removes the \a current entry from a list during a traversal.
  \param head This is a pointer to the list head structure
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  \note This macro can \b only be used inside an AST_LIST_TRAVERSE_SAFE_BEGIN()
  block; it is used to unlink the current entry from the list without affecting
  the list traversal (and without having to re-traverse the list to modify the
  previous entry, if any).
 */
#define AST_LIST_REMOVE_CURRENT(head, field)						\
	__new_prev = __list_prev;							\
	if (__list_prev)								\
		__list_prev->field.next = __list_next;					\
	else										\
		(head)->first = __list_next;						\
	if (!__list_next)								\
		(head)->last = __list_prev;

/*!
  \brief Closes a safe loop traversal block.
 */
#define AST_LIST_TRAVERSE_SAFE_END  }

/*!
  \brief Initializes a list head structure.
  \param head This is a pointer to the list head structure

  This macro initializes a list head structure by setting the head
  entry to \a NULL (empty list) and recreating the embedded lock.
*/
#define AST_LIST_HEAD_INIT(head) {					\
	(head)->first = NULL;						\
	(head)->last = NULL;						\
	ast_mutex_init(&(head)->lock);					\
}

/*!
  \brief Destroys a list head structure.
  \param head This is a pointer to the list head structure

  This macro destroys a list head structure by setting the head
  entry to \a NULL (empty list) and destroying the embedded lock.
  It does not free the structure from memory.
*/
#define AST_LIST_HEAD_DESTROY(head) {					\
	(head)->first = NULL;						\
	(head)->last = NULL;						\
	ast_mutex_destroy(&(head)->lock);				\
}

/*!
  \brief Initializes a list head structure.
  \param head This is a pointer to the list head structure

  This macro initializes a list head structure by setting the head
  entry to \a NULL (empty list). There is no embedded lock handling
  with this macro.
*/
#define AST_LIST_HEAD_INIT_NOLOCK(head) {				\
	(head)->first = NULL;						\
	(head)->last = NULL;						\
}

/*!
  \brief Inserts a list entry after a given entry.
  \param head This is a pointer to the list head structure
  \param listelm This is a pointer to the entry after which the new entry should
  be inserted.
  \param elm This is a pointer to the entry to be inserted.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.
 */
#define AST_LIST_INSERT_AFTER(head, listelm, elm, field) do {		\
	(elm)->field.next = (listelm)->field.next;			\
	(listelm)->field.next = (elm);					\
	if ((head)->last == (listelm))					\
		(head)->last = (elm);					\
} while (0)

/*!
  \brief Inserts a list entry at the head of a list.
  \param head This is a pointer to the list head structure
  \param elm This is a pointer to the entry to be inserted.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.
 */
#define AST_LIST_INSERT_HEAD(head, elm, field) do {			\
		(elm)->field.next = (head)->first;			\
		(head)->first = (elm);					\
		if (!(head)->last)					\
			(head)->last = (elm);				\
} while (0)

/*!
  \brief Appends a list entry to the tail of a list.
  \param head This is a pointer to the list head structure
  \param elm This is a pointer to the entry to be appended.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  Note: The link field in the appended entry is \b not modified, so if it is
  actually the head of a list itself, the entire list will be appended
  temporarily (until the next AST_LIST_INSERT_TAIL is performed).
 */
#define AST_LIST_INSERT_TAIL(head, elm, field) do {			\
      if (!(head)->first) {						\
		(head)->first = (elm);					\
		(head)->last = (elm);					\
      } else {								\
		(head)->last->field.next = (elm);			\
		(head)->last = (elm);					\
      }									\
} while (0)

/*!
  \brief Appends a whole list to the tail of a list.
  \param head This is a pointer to the list head structure
  \param list This is a pointer to the list to be appended.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.
 */
#define AST_LIST_APPEND_LIST(head, list, field) do {			\
      if (!(head)->first) {						\
		(head)->first = (list)->first;				\
		(head)->last = (list)->last;				\
      } else {								\
		(head)->last->field.next = (list)->first;		\
		(head)->last = (list)->last;				\
      }									\
} while (0)

/*!
  \brief Removes and returns the head entry from a list.
  \param head This is a pointer to the list head structure
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.

  Removes the head entry from the list, and returns a pointer to it.
  This macro is safe to call on an empty list.
 */
#define AST_LIST_REMOVE_HEAD(head, field) ({				\
		typeof((head)->first) cur = (head)->first;		\
		if (cur) {						\
			(head)->first = cur->field.next;		\
			cur->field.next = NULL;				\
			if ((head)->last == cur)			\
				(head)->last = NULL;			\
		}							\
		cur;							\
	})

/*!
  \brief Removes a specific entry from a list.
  \param head This is a pointer to the list head structure
  \param elm This is a pointer to the entry to be removed.
  \param field This is the name of the field (declared using AST_LIST_ENTRY())
  used to link entries of this list together.
  \warning The removed entry is \b not freed nor modified in any way.
 */
#define AST_LIST_REMOVE(head, elm, field) do {			        \
	if ((head)->first == (elm)) {					\
		(head)->first = (elm)->field.next;			\
		if ((head)->last == (elm))			\
			(head)->last = NULL;			\
	} else {								\
		typeof(elm) curelm = (head)->first;			\
		while (curelm && (curelm->field.next != (elm)))			\
			curelm = curelm->field.next;			\
		if (curelm) { \
			curelm->field.next = (elm)->field.next;			\
			if ((head)->last == (elm))				\
				(head)->last = curelm;				\
		} \
	}								\
        (elm)->field.next = NULL;                                       \
} while (0)

#endif /* _ASTERISK_LINKEDLISTS_H */
