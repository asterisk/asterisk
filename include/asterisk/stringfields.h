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
  
  Using this functionality is quite simple... an example structure
  with three fields is defined like this:
  
  \code
  struct sample_fields {
	  int x1;
	  AST_DECLARE_STRING_FIELDS(
		  AST_STRING_FIELD(name);
		  AST_STRING_FIELD(address);
		  AST_STRING_FIELD(password);
	  );
	  long x2;
  };
  \endcode
  
  When an instance of this structure is allocated, the fields
  (and the pool of storage for them) must be initialized:
  
  \code
  struct sample_fields *sample;
  
  sample = calloc(1, sizeof(*sample));
  if (sample) {
	  if (!ast_string_field_init(sample)) {
		  free(sample);
		  sample = NULL;
	  }
  }
  
  if (!sample) {
  ...
  }
  \endcode
  
  Fields will default to pointing to an empty string, and will
  revert to that when ast_string_field_free() is called. This means
  that a string field will \b never contain NULL.
  
  Using the fields is much like using regular 'char *' fields
  in the structure, except that writing into them must be done
  using wrapper macros defined in this file.
  
  Storing simple values into fields can be done using ast_string_field_set();
  more complex values (using printf-style format strings) can be stored
  using ast_string_field_build().
  
  When the structure instance is no longer needed, the fields
  and their storage pool must be freed:
  
  \code
  ast_string_field_free_all(sample);
  free(sample);
  \endcode
*/

#ifndef _ASTERISK_STRINGFIELDS_H
#define _ASTERISK_STRINGFIELDS_H

#include <string.h>
#include <stdarg.h>
#include <stddef.h>

#include "asterisk/inline_api.h"
#include "asterisk/compiler.h"
#include "asterisk/compat.h"

/*!
  \internal
  \brief An opaque type for managed string fields in structures

  Don't declare instances of this type directly; use the AST_STRING_FIELD()
  macro instead.
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
	char base[0];				/*!< storage space for the fields */
};

/*!
  \internal
  \brief Structure used to manage the storage for a set of string fields
*/
struct ast_string_field_mgr {
	struct ast_string_field_pool *pool;	/*!< the address of the pool's structure */
	size_t size;				/*!< the total size of the current pool */
	size_t space;				/*!< the space available in the current pool */
	size_t used;				/*!< the space used in the current pool */
};

/*!
  \internal
  \brief Initialize a field pool manager and fields
  \param mgr Pointer to the pool manager structure
  \param size Amount of storage to allocate
  \param fields Pointer to the first entry of the field array
  \param num_fields Number of fields in the array
  \return 0 on failure, non-zero on success
*/
int __ast_string_field_init(struct ast_string_field_mgr *mgr, size_t size,
			    ast_string_field *fields, int num_fields);

/*!
  \internal
  \brief Allocate space for a field
  \param mgr Pointer to the pool manager structure
  \param needed Amount of space needed for this field
  \param fields Pointer to the first entry of the field array
  \param num_fields Number of fields in the array
  \return NULL on failure, an address for the field on success

  This function will allocate the requested amount of space from
  the field pool. If the requested amount of space is not available,
  an additional pool will be allocated.
*/
ast_string_field __ast_string_field_alloc_space(struct ast_string_field_mgr *mgr, size_t needed,
						ast_string_field *fields, int num_fields);

/*!
  \internal
  \brief Set a field to a complex (built) value
  \param mgr Pointer to the pool manager structure
  \param fields Pointer to the first entry of the field array
  \param num_fields Number of fields in the array
  \param index Index position of the field within the structure
  \param format printf-style format string
  \return nothing
*/
void __ast_string_field_index_build(struct ast_string_field_mgr *mgr,
				    ast_string_field *fields, int num_fields,
				    int index, const char *format, ...);

/*!
  The default amount of storage to be allocated for a field pool.
*/
#define AST_STRING_FIELD_DEFAULT_POOL 512

/*!
  \brief Declare a string field
  \param name The field name
*/
#define AST_STRING_FIELD(name) const ast_string_field name;

/*!
  \brief Declare the fields needed in a structure
  \param field_list The list of fields to declare, using AST_STRING_FIELD() for each one
*/
#define AST_DECLARE_STRING_FIELDS(field_list) \
	ast_string_field __begin_field[0]; \
	field_list \
	ast_string_field __end_field[0]; \
	struct ast_string_field_mgr __field_mgr;

/*!
  \brief Get the number of string fields in a structure
  \param x Pointer to a structure containing fields
  \return the number of fields in the structure's definition
*/
#define ast_string_field_count(x) \
	(offsetof(typeof(*x), __end_field) - offsetof(typeof(*x), __begin_field)) / sizeof(ast_string_field)

/*!
  \brief Get the index of a field in a structure
  \param x Pointer to a structure containing fields
  \param field Name of the field to locate
  \return the position (index) of the field within the structure's
  array of fields
*/
#define ast_string_field_index(x, field) \
	(offsetof(typeof(*x), field) - offsetof(typeof(*x), __begin_field)) / sizeof(ast_string_field)

/*!
  \brief Initialize a field pool and fields
  \param x Pointer to a structure containing fields
  \return 0 on failure, non-zero on success
*/
#define ast_string_field_init(x) \
	__ast_string_field_init(&x->__field_mgr, AST_STRING_FIELD_DEFAULT_POOL, &x->__begin_field[0], ast_string_field_count(x))

/*!
  \brief Set a field to a simple string value
  \param x Pointer to a structure containing fields
  \param index Index position of the field within the structure
  \param data String value to be copied into the field
  \return nothing
*/
#define ast_string_field_index_set(x, index, data) do { \
	if ((x->__begin_field[index] = __ast_string_field_alloc_space(&x->__field_mgr, strlen(data) + 1, &x->__begin_field[0], ast_string_field_count(x)))) \
		strcpy((char *) x->__begin_field[index], data); \
	} while (0)

/*!
  \brief Set a field to a simple string value
  \param x Pointer to a structure containing fields
  \param field Name of the field to set
  \param data String value to be copied into the field
  \return nothing
*/
#define ast_string_field_set(x, field, data) \
	ast_string_field_index_set(x, ast_string_field_index(x, field), data)

/*!
  \brief Set a field to a complex (built) value
  \param x Pointer to a structure containing fields
  \param index Index position of the field within the structure
  \param fmt printf-style format string
  \param args Arguments for format string
  \return nothing
*/
#define ast_string_field_index_build(x, index, fmt, args...) \
	__ast_string_field_index_build(&x->__field_mgr, &x->__begin_field[0], ast_string_field_count(x), index, fmt, args)

/*!
  \brief Set a field to a complex (built) value
  \param x Pointer to a structure containing fields
  \param field Name of the field to set
  \param fmt printf-style format string
  \param args Arguments for format string
  \return nothing
*/
#define ast_string_field_build(x, field, fmt, args...) \
	ast_string_field_index_build(x, ast_string_field_index(x, field), fmt, args)

/*!
  \brief Free a field's value.
  \param x Pointer to a structure containing fields
  \param index Index position of the field within the structure
  \return nothing

  \note Because of the storage pool used, the memory
  occupied by the field's value is \b not recovered; the field
  pointer is just changed to point to an empty string.
*/
#define ast_string_field_index_free(x, index) do { \
	x->__begin_field[index] = __ast_string_field_empty; \
	} while(0)

/*!
  \brief Free a field's value.
  \param x Pointer to a structure containing fields
  \param field Name of the field to free
  \return nothing

  \note Because of the storage pool used, the memory
  occupied by the field's value is \b not recovered; the field
  pointer is just changed to point to an empty string.
*/
#define ast_string_field_free(x, field) \
	ast_string_field_index_free(x, ast_string_field_index(x, field))

/*!
  \brief Free all fields (and the storage pool) in a structure
  \param x Pointer to a structure containing fields
  \return nothing

  After calling this macro, fields can no longer be accessed in
  structure; it should only be called immediately before freeing
  the structure itself.
*/
#define ast_string_field_free_all(x) do { \
	int index; \
	struct ast_string_field_pool *this, *prev; \
	for (index = 0; index < ast_string_field_count(x); index ++) \
		ast_string_field_index_free(x, index); \
	for (this = x->__field_mgr.pool; this; this = prev) { \
		prev = this->prev; \
		free(this); \
	} \
	} while(0)

#endif /* _ASTERISK_STRINGFIELDS_H */
