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
  \page Stringfields String Fields
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
  * ast_string_field_build_va() is the varargs version of the above;
  * variants of these function allow passing a pointer to the field
    as an argument.

  \code
  ast_string_field_set(x, foo, "infinite loop");
  ast_string_field_set(x, foo, NULL); // set to an empty string
  ast_string_field_ptr_set(x, &x->bar, "right way");

  ast_string_field_build(x, blah, "%d %s", zipcode, city);
  ast_string_field_ptr_build(x, &x->blah, "%d %s", zipcode, city);

  ast_string_field_build_va(x, bar, fmt, args)
  ast_string_field_ptr_build_va(x, &x->bar, fmt, args)
  \endcode

  When the structure instance is no longer needed, the fields
  and their storage pool must be freed:

  \code
  ast_string_field_free_memory(x);
  ast_free(x);
  \endcode

  A new feature "Extended String Fields" has been added in 13.9.0.

  An extended field is one that is declared outside the AST_DECLARE_STRING_FIELDS
  block but still inside the parent structure.  It's most useful for extending
  structures where adding a new string field to an existing AST_DECLARE_STRING_FIELDS
  block would break ABI compatibility.

  Example:

  \code
  struct original_structure_version {
      AST_DECLARE_STRING_FIELDS(
          AST_STRING_FIELD(foo);
          AST_STRING_FIELD(bar);
      );
      int x1;
      int x2;
  };
  \endcode

  Adding "blah" to the existing string fields breaks ABI compatibility because it changes
  the offsets of x1 and x2.

  \code
  struct new_structure_version {
      AST_DECLARE_STRING_FIELDS(
          AST_STRING_FIELD(foo);
          AST_STRING_FIELD(bar);
          AST_STRING_FIELD(blah);
      );
      int x1;
      int x2;
  };
  \endcode

  However, adding "blah" as an extended string field to the end of the structure doesn't break
  ABI compatibility but still allows the use of the existing pool.

  \code
  struct new_structure_version {
      AST_DECLARE_STRING_FIELDS(
          AST_STRING_FIELD(foo);
          AST_STRING_FIELD(bar);
      );
      int x1;
      int x2;
      AST_STRING_FIELD_EXTENDED(blah);
  };
  \endcode

  The only additional step required is to call ast_string_field_init_extended so the
  pool knows about the new field.  It must be called AFTER ast_string_field_init or
  ast_calloc_with_stringfields.  Although ast_calloc_with_stringfields is used in the
  sample below, it's not necessary for extended string fields.

  \code

  struct new_structure_version *x = ast_calloc_with_stringfields(1, struct new_structure_version, 252);
  if (!x) {
      return;
  }

  ast_string_field_init_extended(x, blah);
  \endcode

  The new field can now be treated just like any other string field and it's storage will
  be released with the rest of the string fields.

  \code
  ast_string_field_set(x, foo, "infinite loop");
  ast_stringfield_free_memory(x);
  ast_free(x);
  \endcode

  This completes the API description.
*/

#ifndef _ASTERISK_STRINGFIELDS_H
#define _ASTERISK_STRINGFIELDS_H

#include "asterisk/inline_api.h"
#include "asterisk/vector.h"

/*!
  \internal
  \brief An opaque type for managed string fields in structures

  Don't declare instances of this type directly; use the AST_STRING_FIELD()
  macro instead.

  In addition to the string itself, the amount of space allocated for the
  field is stored in the two bytes immediately preceding it.
*/
typedef const char * ast_string_field;

/* the type of storage used to track how many bytes were allocated for a field */

typedef uint16_t ast_string_field_allocation;

/*!
  \internal
  \brief A constant empty string used for fields that have no other value
*/
extern const char *__ast_string_field_empty;

/*!
  \internal
  \brief Structure used to hold a pool of space for string fields
  \note base is aligned so base+used can stay aligned by incrementing used with
        aligned numbers only
*/
struct ast_string_field_pool {
	struct ast_string_field_pool *prev;	/*!< pointer to the previous pool, if any */
	size_t size;				/*!< the total size of the pool */
	size_t used;				/*!< the space used in the pool */
	size_t active;				/*!< the amount of space actively in use by fields */
	char base[0] __attribute__((aligned(__alignof__(ast_string_field_allocation)))); /*!< storage space for the fields */
};

/*!
  \internal
  \brief The definition for the string field vector used for compare and copy
  \since 13.9.0
*/
AST_VECTOR(ast_string_field_vector, const char **);

/*!
  \internal
  \brief Structure used to manage the storage for a set of string fields.
*/
struct ast_string_field_mgr {
	ast_string_field last_alloc;			/*!< the last field allocated */
	struct ast_string_field_pool *embedded_pool;	/*!< pointer to the embedded pool, if any */
	struct ast_string_field_vector string_fields;	/*!< field vector for compare and copy */
};

/*!
  \internal
  \brief Attempt to 'grow' an already allocated field to a larger size
  \param mgr Pointer to the pool manager structure
  \param pool_head Pointer to the current pool
  \param needed Amount of space needed for this field
  \param ptr Pointer to a field within the structure
  \retval zero on success
  \retval non-zero on failure

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
  \param pool_head Pointer to the current pool
  \param needed Amount of space needed for this field
  \param file, lineno, func
  \retval NULL on failure
  \return an address for the field on success.

  This function will allocate the requested amount of space from
  the field pool. If the requested amount of space is not available,
  an additional pool will be allocated.
*/
ast_string_field __ast_string_field_alloc_space(struct ast_string_field_mgr *mgr,
	struct ast_string_field_pool **pool_head, size_t needed,
	const char *file, int lineno, const char *func);

/*!
  \internal
  \brief Set a field to a complex (built) value
  \param mgr Pointer to the pool manager structure
  \param pool_head Pointer to the current pool
  \param ptr Pointer to a field within the structure
  \param format printf-style format string
  \param file, lineno, func
*/
void __ast_string_field_ptr_build(const char *file, int lineno, const char *func,
	struct ast_string_field_mgr *mgr, struct ast_string_field_pool **pool_head,
	ast_string_field *ptr, const char *format, ...) __attribute__((format(printf, 7, 8)));

/*!
  \internal
  \brief Set a field to a complex (built) value
  \param mgr Pointer to the pool manager structure
  \param pool_head Pointer to the current pool
  \param ptr Pointer to a field within the structure
  \param format printf-style format string
  \param ap va_list of the args for the format_string
  \param file, lineno, func
*/
void __ast_string_field_ptr_build_va(struct ast_string_field_mgr *mgr,
	struct ast_string_field_pool **pool_head,
	ast_string_field *ptr, const char *format, va_list ap,
	const char *file, int lineno, const char *func) __attribute__((format(printf, 4, 0)));

/*!
  \brief Declare a string field
  \param name The field name
*/
#define AST_STRING_FIELD(name) const ast_string_field name

/*!
  \brief Declare an extended string field
  \since 13.9.0

  \param name The field name
*/
#define AST_STRING_FIELD_EXTENDED(name) AST_STRING_FIELD(name)

enum ast_stringfield_cleanup_type {
	/*!
	 * Reset all string fields and free all extra pools that may have been created
	 * The allocation or structure can be reused as is.
	 */
	AST_STRINGFIELD_RESET = 0,
	/*!
	 * Reset all string fields and free all pools.
	 * If the pointer was returned by ast_calloc_with_stringfields, it can NOT be reused
	 * and should be immediately freed.  Otherwise, you must call ast_string_field_init
	 * again if you want to reuse it.
	 */
	AST_STRINGFIELD_DESTROY = -1,
};

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
	Use AST_STRINGFIELD_RESET to reset fields to the default value,
	and release all but the most recent pool.
	AST_STRINGFIELD_DESTROY (used internally) means free all pools which is
	equivalent to calling ast_string_field_free_memory.

  \retval zero on success
  \retval non-zero on failure
*/

#define ast_string_field_init(x, size) \
({ \
	int __res__ = -1; \
	if (((void *)(x)) != (void *)NULL) { \
		__res__ = __ast_string_field_init(&(x)->__field_mgr, &(x)->__field_mgr_pool, size, __FILE__, __LINE__, __PRETTY_FUNCTION__); \
	} \
	__res__ ; \
})

/*!
 * \brief free all memory - to be called before destroying the object
 *
 * \param x
 *
 */
#define ast_string_field_free_memory(x)	 \
({ \
	int __res__ = -1; \
	if (((void *)(x)) != (void *)NULL) { \
		__res__ = __ast_string_field_free_memory(&(x)->__field_mgr, &(x)->__field_mgr_pool, \
			AST_STRINGFIELD_DESTROY, __FILE__, __LINE__, __PRETTY_FUNCTION__); \
	} \
	__res__; \
})

int __ast_string_field_free_memory(struct ast_string_field_mgr *mgr,
	struct ast_string_field_pool **pool_head, enum ast_stringfield_cleanup_type cleanup_type,
	const char *file, int lineno, const char *func);

/*!
 * \brief Initialize an extended string field
 * \since 13.9.0
 *
 * \param x Pointer to a structure containing the field
 * \param field The extended field to initialize
 * \retval zero on success
 * \retval non-zero on error
 *
 * \note
 * This macro must be called on ALL fields defined with AST_STRING_FIELD_EXTENDED after
 * ast_string_field_init has been called.
 */
#define ast_string_field_init_extended(x, field) \
({ \
	int __res__ = -1; \
	if (((void *)(x)) != (void *)NULL) { \
		ast_string_field *non_const = (ast_string_field *)&(x)->field; \
		*non_const = __ast_string_field_empty; \
		__res__ = AST_VECTOR_APPEND(&(x)->__field_mgr.string_fields, non_const); \
	} \
	__res__; \
})

/*!
 * \internal
 * \brief internal version of ast_string_field_init
 */
int __ast_string_field_init(struct ast_string_field_mgr *mgr, struct ast_string_field_pool **pool_head,
			    int needed, const char *file, int lineno, const char *func);

/*!
 * \brief Allocate a structure with embedded stringfields in a single allocation
 * \param n Current implementation only allows 1 structure to be allocated
 * \param type The type of structure to allocate
 * \param size The number of bytes of space (minimum) to allocate for stringfields to use
 *             in each structure
 *
 * This function will allocate memory for one or more structures that use stringfields, and
 * also allocate space for the stringfields and initialize the stringfield management
 * structure embedded in the outer structure.
 *
 * \since 1.8
 */
#define ast_calloc_with_stringfields(n, type, size) \
	__ast_calloc_with_stringfields(n, sizeof(type), offsetof(type, __field_mgr), \
		offsetof(type, __field_mgr_pool), size, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \internal
 * \brief internal version of ast_calloc_with_stringfields
 */
void *__ast_calloc_with_stringfields(unsigned int num_structs,
	size_t struct_size, size_t field_mgr_offset, size_t field_mgr_pool_offset, size_t pool_size,
	const char *file, int lineno, const char *func);

/*!
  \internal
  \brief Release a field's allocation from a pool
  \param pool_head Pointer to the current pool
  \param ptr Field to be released

  This function will search the pool list to find the pool that contains
  the allocation for the specified field, then remove the field's allocation
  from that pool's 'active' count. If the pool's active count reaches zero,
  and it is not the current pool, then it will be freed.
 */
void __ast_string_field_release_active(struct ast_string_field_pool *pool_head,
				       const ast_string_field ptr);

/*!
  \brief Macro to provide access to the allocation field that lives immediately in front of a string field
  \param x Pointer to the string field

  Note that x must be a pointer to a byte-sized type -- normally (char *) -- or this calculation
  would break horribly
*/
#define AST_STRING_FIELD_ALLOCATION(x) *((ast_string_field_allocation *) (x - __alignof__(ast_string_field_allocation)))

/*!
  \brief Set a field to a simple string value
  \param x Pointer to a structure containing fields
  \param ptr Pointer to a field within the structure
  \param data String value to be copied into the field
  \retval zero on success
  \retval non-zero on error
*/
#define ast_string_field_ptr_set(x, ptr, data) \
({ \
	int __res__ = -1; \
	if (((void *)(x)) != (void *)NULL) { \
		__res__ = ast_string_field_ptr_set_by_fields((x)->__field_mgr_pool, (x)->__field_mgr, ptr, data); \
	} \
	__res__; \
})

#define __ast_string_field_ptr_set_by_fields(field_mgr_pool, field_mgr, ptr, data, file, lineno, func) \
({                                                                                             \
	int __res__ = 0;                                                                           \
	const char *__d__ = (data);                                                                \
	ast_string_field *__p__ = (ast_string_field *) (ptr);                                      \
	ast_string_field target = *__p__;                                                          \
	if (__d__ == NULL || *__d__ == '\0') {                                                     \
		__ast_string_field_release_active(field_mgr_pool, *__p__);                             \
		*__p__ = __ast_string_field_empty;                                                     \
	} else {                                                                                   \
		size_t __dlen__ = strlen(__d__) + 1;                                                   \
		if ((__dlen__ <= AST_STRING_FIELD_ALLOCATION(*__p__)) ||                               \
			(!__ast_string_field_ptr_grow(&field_mgr, &field_mgr_pool, __dlen__, __p__)) ||    \
			(target = __ast_string_field_alloc_space(&field_mgr, &field_mgr_pool, __dlen__, file, lineno, func))) { \
			if (target != *__p__) {                                                            \
				__ast_string_field_release_active(field_mgr_pool, *__p__);                     \
				*__p__ = target;                                                               \
			}                                                                                  \
			memcpy(* (void **) __p__, __d__, __dlen__);                                        \
		} else {                                                                               \
			__res__ = -1;                                                                      \
		}                                                                                      \
	}                                                                                          \
	__res__;                                                                                   \
})

#define ast_string_field_ptr_set_by_fields(field_mgr_pool, field_mgr, ptr, data) \
	__ast_string_field_ptr_set_by_fields(field_mgr_pool, field_mgr, ptr, data, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
  \brief Set a field to a simple string value
  \param x Pointer to a structure containing fields
  \param field Name of the field to set
  \param data String value to be copied into the field
  \retval zero on success
  \retval non-zero on error
*/
#define ast_string_field_set(x, field, data) \
({ \
	int __res__ = -1; \
	if (((void *)(x)) != (void *)NULL) { \
		__res__ = ast_string_field_ptr_set(x, &(x)->field, data); \
	} \
	__res__; \
})

/*!
  \brief Set a field to a complex (built) value
  \param x Pointer to a structure containing fields
  \param ptr Pointer to a field within the structure
  \param fmt printf-style format string
  \param args Arguments for format string
*/
#define ast_string_field_ptr_build(x, ptr, fmt, args...) \
({ \
	int __res__ = -1; \
	if (((void *)(x)) != (void *)NULL) { \
		__ast_string_field_ptr_build(__FILE__, __LINE__, __PRETTY_FUNCTION__, \
			&(x)->__field_mgr, &(x)->__field_mgr_pool, (ast_string_field *) ptr, fmt, args); \
		__res__ = 0; \
	} \
	__res__; \
})

/*!
  \brief Set a field to a complex (built) value
  \param x Pointer to a structure containing fields
  \param field Name of the field to set
  \param fmt printf-style format string
  \param args Arguments for format string
*/
#define ast_string_field_build(x, field, fmt, args...) \
({ \
	int __res__ = -1; \
	if (((void *)(x)) != (void *)NULL) { \
		__ast_string_field_ptr_build(__FILE__, __LINE__, __PRETTY_FUNCTION__, \
			&(x)->__field_mgr, &(x)->__field_mgr_pool, (ast_string_field *) &(x)->field, fmt, args); \
		__res__ = 0; \
	} \
	__res__; \
})

/*!
  \brief Set a field to a complex (built) value with prebuilt va_lists.
  \param x Pointer to a structure containing fields
  \param ptr Pointer to a field within the structure
  \param fmt printf-style format string
  \param args Arguments for format string in va_list format
*/
#define ast_string_field_ptr_build_va(x, ptr, fmt, args) \
({ \
	int __res__ = -1; \
	if (((void *)(x)) != (void *)NULL) { \
		__ast_string_field_ptr_build_va(&(x)->__field_mgr, &(x)->__field_mgr_pool, (ast_string_field *) ptr, fmt, args, \
			__FILE__, __LINE__, __PRETTY_FUNCTION__); \
		__res__ = 0; \
	} \
	__res__; \
})

/*!
  \brief Set a field to a complex (built) value
  \param x Pointer to a structure containing fields
  \param field Name of the field to set
  \param fmt printf-style format string
  \param args Arguments for format string in va_list format
*/
#define ast_string_field_build_va(x, field, fmt, args) \
({ \
	int __res__ = -1; \
	if (((void *)(x)) != (void *)NULL) { \
		__ast_string_field_ptr_build_va(&(x)->__field_mgr, &(x)->__field_mgr_pool, (ast_string_field *) &(x)->field, fmt, args, \
			__FILE__, __LINE__, __PRETTY_FUNCTION__); \
		__res__ = 0; \
	} \
	__res__; \
})

/*!
  \brief Compare the string fields in two instances of the same structure
  \since 12
  \param instance1 The first instance of the structure to be compared
  \param instance2 The second instance of the structure to be compared
  \retval zero if all string fields are equal (does not compare non-string field data)
  \retval non-zero if the values of the string fields differ
*/
#define ast_string_fields_cmp(instance1, instance2) \
({ \
	int __res__ = -1; \
	if (((void *)(instance1)) != (void *)NULL && ((void *)(instance2)) != (void *)NULL) { \
		__res__ = __ast_string_fields_cmp(&(instance1)->__field_mgr.string_fields, \
			&(instance2)->__field_mgr.string_fields); \
	} \
	__res__; \
})

int __ast_string_fields_cmp(struct ast_string_field_vector *left, struct ast_string_field_vector *right);

/*!
  \brief Copy all string fields from one instance to another of the same structure
  \since 12
  \param copy The instance of the structure to be copied into
  \param orig The instance of the structure to be copied from
  \retval zero on success
  \retval non-zero on error
*/
#define ast_string_fields_copy(copy, orig) \
({ \
	int __res__ = -1; \
	if (((void *)(copy)) != (void *)NULL && ((void *)(orig)) != (void *)NULL) { \
		__res__ = __ast_string_fields_copy(((copy)->__field_mgr_pool), \
			(struct ast_string_field_mgr *)&((copy)->__field_mgr), \
			(struct ast_string_field_mgr *)&((orig)->__field_mgr), \
			__FILE__, __LINE__, __PRETTY_FUNCTION__); \
	} \
	__res__; \
})

int __ast_string_fields_copy(struct ast_string_field_pool *copy_pool,
	struct ast_string_field_mgr *copy_mgr, struct ast_string_field_mgr *orig_mgr,
	const char *file, int lineno, const char *func);

#endif /* _ASTERISK_STRINGFIELDS_H */
