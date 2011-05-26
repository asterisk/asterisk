/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
 *
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

/*! \file
  \brief String fields in structures

  This file contains objects and macros used to manage string
  fields in structures without requiring them to be allocated
  as fixed-size buffers or requiring individual allocations for
  for each field.

  Using this functionality is quite simple. An example structure
  with three fields is defined like this:
  
  \code
  struct sample_fields {
	  int x1;
	  AST_DECLARE_STRING_FIELDS(
		  AST_STRING_FIELD(foo);
		  AST_STRING_FIELD(bar);
		  AST_STRING_FIELD(blah);
	  );
	  long x2;
  };
  \endcode
  
  When an instance of this structure is allocated (either statically or
  dynamically), the fields and the pool of storage for them must be
  initialized:
  
  \code
  struct sample_fields *x;
  
  x = ast_calloc(1, sizeof(*x));
  if (x == NULL || ast_string_field_init(x, 252)) {
	if (x)
		ast_free(x);
	x = NULL;
  	... handle error
  }
  \endcode

  Fields will default to pointing to an empty string, and will revert to
  that when ast_string_field_set() is called with a NULL argument.
  A string field will \b never contain NULL.

  ast_string_field_init(x, 0) will reset fields to the
  initial value while keeping the pool allocated.
  
  Reading the fields is much like using 'const char * const' fields in the
  structure: you cannot write to the field or to the memory it points to.

  Writing to the fields must be done using the wrapper macros listed below;
  and assignments are always by value (i.e. strings are copied):
  * ast_string_field_set() stores a simple value;
  * ast_string_field_build() builds the string using a printf-style format;
  * ast_string_field_build_va() is the varargs version of the above (for
    portability reasons it uses two vararg arguments);
  * variants of these function allow passing a pointer to the field
    as an argument.

  \code
  ast_string_field_set(x, foo, "infinite loop");
  ast_string_field_set(x, foo, NULL); // set to an empty string
  ast_string_field_ptr_set(x, &x->bar, "right way");

  ast_string_field_build(x, blah, "%d %s", zipcode, city);
  ast_string_field_ptr_build(x, &x->blah, "%d %s", zipcode, city);

  ast_string_field_build_va(x, bar, fmt, args1, args2)
  ast_string_field_ptr_build_va(x, &x->bar, fmt, args1, args2)
  \endcode

  When the structure instance is no longer needed, the fields
  and their storage pool must be freed:
  
  \code
  ast_string_field_free_memory(x);
  ast_free(x);
  \endcode

  This completes the API description.
*/

#ifndef _ASTERISK_STRINGFIELDS_H
#define _ASTERISK_STRINGFIELDS_H

#include "asterisk/inline_api.h"

/*!
  \internal
  \brief An opaque type for managed string fields in structures

  Don't declare instances of this type directly; use the AST_STRING_FIELD()
  macro instead.

  In addition to the string itself, the amount of space allocated for the
  field is stored in the two bytes immediately preceding it.
*/
typedef const char * ast_string_field;

/*!
  \internal
  \brief A constant empty string used for fields that have no other value
*/
extern const char *__ast_string_field_empty;

/*!
  \internal
  \brief Structure used to hold a pool of space for string fields
*/
struct ast_string_field_pool {
	struct ast_string_field_pool *prev;	/*!< pointer to the previous pool, if any */
	size_t size;				/*!< the total size of the pool */
	size_t used;				/*!< the space used in the pool */
	size_t active;				/*!< the amount of space actively in use by fields */
	char base[0];				/*!< storage space for the fields */
};

/*!
  \internal
  \brief Structure used to manage the storage for a set of string fields.
*/
struct ast_string_field_mgr {
	ast_string_field last_alloc;			/*!< the last field allocated */
 	struct ast_string_field_pool *embedded_pool;	/*!< pointer to the embedded pool, if any */
#if defined(__AST_DEBUG_MALLOC)
	const char *owner_file;				/*!< filename of owner */
	const char *owner_func;				/*!< function name of owner */
	int owner_line;					/*!< line number of owner */
#endif
};

/*!
  \internal
  \brief Attempt to 'grow' an already allocated field to a larger size
  \param mgr Pointer to the pool manager structure
  \param needed Amount of space needed for this field
  \param ptr Pointer to a field within the structure
  \return 0 on success, non-zero on failure

  This function will attempt to increase the amount of space allocated to
  an existing field to the amount requested; this is only possible if the
  field was the last field allocated from the current storage pool and
  the pool has enough space available. If so, the additional space will be
  allocated to this field and the field's address will not be changed.
*/
int __ast_string_field_ptr_grow(struct ast_string_field_mgr *mgr,
				struct ast_string_field_pool **pool_head, size_t needed,
				const ast_string_field *ptr);

/*!
  \internal
  \brief Allocate space for a field
  \param mgr Pointer to the pool manager structure
  \param needed Amount of space needed for this field
  \param fields Pointer to the first entry of the field array
  \return NULL on failure, an address for the field on success.

  This function will allocate the requested amount of space from
  the field pool. If the requested amount of space is not available,
  an additional pool will be allocated.
*/
ast_string_field __ast_string_field_alloc_space(struct ast_string_field_mgr *mgr,
						struct ast_string_field_pool **pool_head, size_t needed);

/*!
  \internal
  \brief Set a field to a complex (built) value
  \param mgr Pointer to the pool manager structure
  \param pool_head Pointer to the current pool
  \param ptr Pointer to a field within the structure
  \param format printf-style format string
  \return nothing
*/
void __ast_string_field_ptr_build(struct ast_string_field_mgr *mgr,
				  struct ast_string_field_pool **pool_head,
				  ast_string_field *ptr, const char *format, ...) __attribute__((format(printf, 4, 5)));

/*!
  \internal
  \brief Set a field to a complex (built) value
  \param mgr Pointer to the pool manager structure
  \param pool_head Pointer to the current pool
  \param ptr Pointer to a field within the structure
  \param format printf-style format string
  \param args va_list of the args for the format_string
  \param args_again a copy of the first va_list for the sake of bsd not having a copy routine
  \return nothing
*/
void __ast_string_field_ptr_build_va(struct ast_string_field_mgr *mgr,
				     struct ast_string_field_pool **pool_head,
				     ast_string_field *ptr, const char *format, va_list ap) __attribute__((format(printf, 4, 0)));

/*!
  \brief Declare a string field
  \param name The field name
*/
#define AST_STRING_FIELD(name) const ast_string_field name

/*!
  \brief Declare the fields needed in a structure
  \param field_list The list of fields to declare, using AST_STRING_FIELD() for each one.
  Internally, string fields are stored as a pointer to the head of the pool,
  followed by individual string fields, and then a struct ast_string_field_mgr
  which describes the space allocated.
  We split the two variables so they can be used as markers around the
  field_list, and this allows us to determine how many entries are in
  the field, and play with them.
  In particular, for writing to the fields, we rely on __field_mgr_pool to be
  a non-const pointer, so we know it has the same size as ast_string_field,
  and we can use it to locate the fields.
*/
#define AST_DECLARE_STRING_FIELDS(field_list) \
	struct ast_string_field_pool *__field_mgr_pool;	\
	field_list					\
	struct ast_string_field_mgr __field_mgr

/*!
  \brief Initialize a field pool and fields
  \param x Pointer to a structure containing fields
  \param size Amount of storage to allocate.
	Use 0 to reset fields to the default value,
	and release all but the most recent pool.
	size<0 (used internally) means free all pools.
  \return 0 on success, non-zero on failure
*/
#define ast_string_field_init(x, size) \
	__ast_string_field_init(&(x)->__field_mgr, &(x)->__field_mgr_pool, size, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*! \brief free all memory - to be called before destroying the object */
#define ast_string_field_free_memory(x)	\
	__ast_string_field_init(&(x)->__field_mgr, &(x)->__field_mgr_pool, -1, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \internal
 * \brief internal version of ast_string_field_init
 */
int __ast_string_field_init(struct ast_string_field_mgr *mgr, struct ast_string_field_pool **pool_head,
			    int needed, const char *file, int lineno, const char *func);

/*!
 * \brief Allocate a structure with embedded stringfields in a single allocation
 * \param n Number of structures to allocate (see ast_calloc)
 * \param type The type of structure to allocate
 * \param size The number of bytes of space (minimum) to allocate for stringfields to use
 *
 * This function will allocate memory for one or more structures that use stringfields, and
 * also allocate space for the stringfields and initialize the stringfield management
 * structure embedded in the outer structure.
 *
 * \since 1.8
 */
#define ast_calloc_with_stringfields(n, type, size) \
	__ast_calloc_with_stringfields(n, sizeof(type), offsetof(type, __field_mgr), offsetof(type, __field_mgr_pool), \
				       size, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \internal
 * \brief internal version of ast_calloc_with_stringfields
 */
void * attribute_malloc __ast_calloc_with_stringfields(unsigned int num_structs, size_t struct_size, size_t field_mgr_offset,
						       size_t field_mgr_pool_offset, size_t pool_size, const char *file,
						       int lineno, const char *func);

/*!
  \internal
  \brief Release a field's allocation from a pool
  \param pool_head Pointer to the current pool
  \param ptr Field to be released
  \return nothing

  This function will search the pool list to find the pool that contains
  the allocation for the specified field, then remove the field's allocation
  from that pool's 'active' count. If the pool's active count reaches zero,
  and it is not the current pool, then it will be freed.
 */
void __ast_string_field_release_active(struct ast_string_field_pool *pool_head,
				       const ast_string_field ptr);

/* the type of storage used to track how many bytes were allocated for a field */

typedef uint16_t ast_string_field_allocation;

/*!
  \brief Macro to provide access to the allocation field that lives immediately in front of a string field
  \param x Pointer to the string field
*/
#define AST_STRING_FIELD_ALLOCATION(x) *((ast_string_field_allocation *) (x - sizeof(ast_string_field_allocation)))

/*!
  \brief Set a field to a simple string value
  \param x Pointer to a structure containing fields
  \param ptr Pointer to a field within the structure
  \param data String value to be copied into the field 
  \return nothing
*/
#define ast_string_field_ptr_set(x, ptr, data) do {									\
	const char *__d__ = (data);											\
	size_t __dlen__ = (__d__) ? strlen(__d__) + 1 : 1;								\
	ast_string_field *__p__ = (ast_string_field *) (ptr);								\
	if (__dlen__ == 1) {												\
		__ast_string_field_release_active((x)->__field_mgr_pool, *__p__);					\
		*__p__ = __ast_string_field_empty;									\
	} else if ((__dlen__ <= AST_STRING_FIELD_ALLOCATION(*__p__)) ||							\
		   (!__ast_string_field_ptr_grow(&(x)->__field_mgr, &(x)->__field_mgr_pool, __dlen__, __p__)) ||	\
		   (*__p__ = __ast_string_field_alloc_space(&(x)->__field_mgr, &(x)->__field_mgr_pool, __dlen__))) {	\
		if (*__p__ != (*ptr)) {											\
			__ast_string_field_release_active((x)->__field_mgr_pool, (*ptr));				\
		}													\
		memcpy(* (void **) __p__, __d__, __dlen__);								\
	}														\
	} while (0)

/*!
  \brief Set a field to a simple string value
  \param x Pointer to a structure containing fields
  \param field Name of the field to set
  \param data String value to be copied into the field
  \return nothing
*/
#define ast_string_field_set(x, field, data) do {		\
	ast_string_field_ptr_set(x, &(x)->field, data);		\
	} while (0)

/*!
  \brief Set a field to a complex (built) value
  \param x Pointer to a structure containing fields
  \param ptr Pointer to a field within the structure
  \param fmt printf-style format string
  \param args Arguments for format string
  \return nothing
*/
#define ast_string_field_ptr_build(x, ptr, fmt, args...) \
	__ast_string_field_ptr_build(&(x)->__field_mgr, &(x)->__field_mgr_pool, (ast_string_field *) ptr, fmt, args)

/*!
  \brief Set a field to a complex (built) value
  \param x Pointer to a structure containing fields
  \param field Name of the field to set
  \param fmt printf-style format string
  \param args Arguments for format string
  \return nothing
*/
#define ast_string_field_build(x, field, fmt, args...) \
	__ast_string_field_ptr_build(&(x)->__field_mgr, &(x)->__field_mgr_pool, (ast_string_field *) &(x)->field, fmt, args)

/*!
  \brief Set a field to a complex (built) value with prebuilt va_lists.
  \param x Pointer to a structure containing fields
  \param ptr Pointer to a field within the structure
  \param fmt printf-style format string
  \param args1 Arguments for format string in va_list format
  \param args2 a second copy of the va_list for the sake of bsd, with no va_list copy operation
  \return nothing
*/
#define ast_string_field_ptr_build_va(x, ptr, fmt, args) \
	__ast_string_field_ptr_build_va(&(x)->__field_mgr, &(x)->__field_mgr_pool, (ast_string_field *) ptr, fmt, args)

/*!
  \brief Set a field to a complex (built) value
  \param x Pointer to a structure containing fields
  \param field Name of the field to set
  \param fmt printf-style format string
  \param args1 argument one
  \param args2 argument two
  \return nothing
*/
#define ast_string_field_build_va(x, field, fmt, args) \
	__ast_string_field_ptr_build_va(&(x)->__field_mgr, &(x)->__field_mgr_pool, (ast_string_field *) &(x)->field, fmt, args)

#endif /* _ASTERISK_STRINGFIELDS_H */
