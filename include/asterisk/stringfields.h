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
	  if (ast_string_field_init(sample, 256)) {
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
  ast_string_field_free_memory(sample);
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
extern const char __ast_string_field_empty[];

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
	ast_string_field last_alloc;		/*!< the last field allocated */
};

/*!
  \internal
  \brief Initialize a field pool manager and fields
  \param mgr Pointer to the pool manager structure
  \param size Amount of storage to allocate
  \param fields Pointer to the first entry of the field array
  \param num_fields Number of fields in the array
  \return 0 on success, non-zero on failure
*/
int __ast_string_field_init(struct ast_string_field_mgr *mgr, size_t size,
			    ast_string_field *fields, int num_fields);

/*!
  \internal
  \brief Attempt to 'grow' an already allocated field to a larger size
  \param mgr Pointer to the pool manager structure
  \param needed Amount of space needed for this field
  \param fields Pointer to the first entry of the field array
  \param index Index position of the field within the structure
  \return 0 on success, non-zero on failure

  This function will attempt to increase the amount of space allocated to
  an existing field to the amount requested; this is only possible if the
  field was the last field allocated from the current storage pool and
  the pool has enough space available. If so, the additional space will be
  allocated to this field and the field's address will not be changed.
*/
int __ast_string_field_index_grow(struct ast_string_field_mgr *mgr, size_t needed,
				  ast_string_field *fields, int index);

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
  \internal
  \brief Set a field to a complex (built) value
  \param mgr Pointer to the pool manager structure
  \param fields Pointer to the first entry of the field array
  \param num_fields Number of fields in the array
  \param index Index position of the field within the structure
  \param format printf-style format string
  \param args va_list of the args for the format_string
  \param args_again a copy of the first va_list for the sake of bsd not having a copy routine
  \return nothing
*/
void __ast_string_field_index_build_va(struct ast_string_field_mgr *mgr,
				       ast_string_field *fields, int num_fields,
				       int index, const char *format, va_list a1, va_list a2);

/*!
  \brief Declare a string field
  \param name The field name
*/
#define AST_STRING_FIELD(name) const ast_string_field name

/*!
  \brief Declare the fields needed in a structure
  \param field_list The list of fields to declare, using AST_STRING_FIELD() for each one
*/
#define AST_DECLARE_STRING_FIELDS(field_list) \
	ast_string_field __begin_field[0]; \
	field_list \
	ast_string_field __end_field[0]; \
	struct ast_string_field_mgr __field_mgr

/*!
  \brief Get the number of string fields in a structure
  \param x Pointer to a structure containing fields
  \return the number of fields in the structure's definition
*/
#define ast_string_field_count(x) \
	(offsetof(typeof(*(x)), __end_field) - offsetof(typeof(*(x)), __begin_field)) / sizeof(ast_string_field)

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
  \param size Amount of storage to allocate
  \return 0 on success, non-zero on failure
*/
#define ast_string_field_init(x, size) \
	__ast_string_field_init(&(x)->__field_mgr, size, &(x)->__begin_field[0], ast_string_field_count(x))

/*!
  \brief Set a field to a simple string value
  \param x Pointer to a structure containing fields
  \param index Index position of the field within the structure
  \param data String value to be copied into the field
  \return nothing
*/
#define ast_string_field_index_set(x, index, data) do { \
    char *__zz__ = (char*) (x)->__begin_field[index]; \
    size_t __dlen__ = strlen(data) + 1; \
    if ( __dlen__ == 1 ) {\
      (x)->__begin_field[index] = __ast_string_field_empty; \
    } else { \
      if (!__ast_string_field_index_grow(&(x)->__field_mgr, __dlen__, &(x)->__begin_field[0], index)) { \
        memcpy(__zz__, data, __dlen__); \
      } else { \
        if (((x)->__begin_field[index] = __ast_string_field_alloc_space(&(x)->__field_mgr, __dlen__, &(x)->__begin_field[0], ast_string_field_count(x)))) \
          memcpy((char*) (x)->__begin_field[index], data, __dlen__); \
      } \
     } \
   } while (0)

#define ast_string_field_index_logset(x, index, data, logstr) do { \
    char *__zz__ = (char*) (x)->__begin_field[index]; \
    size_t __dlen__ = strlen(data) + 1; \
    if ( __dlen__ == 1 ) {\
      (x)->__begin_field[index] = __ast_string_field_empty; \
    } else { \
      if (!__ast_string_field_index_grow(&(x)->__field_mgr, __dlen__, &(x)->__begin_field[0], index)) { \
        ast_verbose("%s: ======replacing '%s' with '%s'\n", logstr, __zz__, data); \
        memcpy(__zz__, data, __dlen__); \
      } else { \
        if (((x)->__begin_field[index] = __ast_string_field_alloc_space(&(x)->__field_mgr, __dlen__, &(x)->__begin_field[0], ast_string_field_count(x)))) \
          ast_verbose("%s: ++++++allocating room for '%s' to replace '%s'\n", logstr, data, __zz__); \
          memcpy((char*) (x)->__begin_field[index], data, __dlen__); \
      } \
     } \
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

#define ast_string_field_logset(x, field, data, logstr) \
	ast_string_field_index_logset(x, ast_string_field_index(x, field), data, logstr)

/*!
  \brief Set a field to a complex (built) value
  \param x Pointer to a structure containing fields
  \param index Index position of the field within the structure
  \param fmt printf-style format string
  \param args Arguments for format string
  \return nothing
*/
#define ast_string_field_index_build(x, index, fmt, args...) \
	__ast_string_field_index_build(&(x)->__field_mgr, &(x)->__begin_field[0], ast_string_field_count(x), index, fmt, args)

/*!
  \brief Set a field to a complex (built) value with prebuilt va_lists.
  \param x Pointer to a structure containing fields
  \param index Index position of the field within the structure
  \param fmt printf-style format string
  \param args1 Arguments for format string in va_list format
  \param args2 a second copy of the va_list for the sake of bsd, with no va_list copy operation
  \return nothing
*/
#define ast_string_field_index_build_va(x, index, fmt, args1, args2) \
	__ast_string_field_index_build_va(&(x)->__field_mgr, &(x)->__begin_field[0], ast_string_field_count(x), index, fmt, args1, args2)

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
  \brief Set a field to a complex (built) value
  \param x Pointer to a structure containing fields
  \param field Name of the field to set
  \param fmt printf-style format string
  \param argslist a va_list of the args
  \return nothing
*/
#define ast_string_field_build_va(x, field, fmt, args1, args2) \
	ast_string_field_index_build_va(x, ast_string_field_index(x, field), fmt, args1, args2)

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
	(x)->__begin_field[index] = __ast_string_field_empty; \
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
  \brief Free the stringfield storage pools attached to a structure
  \param x Pointer to a structure containing fields
  \return nothing

  After calling this macro, fields can no longer be accessed in
  structure; it should only be called immediately before freeing
  the structure itself.
*/
#define ast_string_field_free_memory(x) do { \
	struct ast_string_field_pool *this, *prev; \
	for (this = (x)->__field_mgr.pool; this; this = prev) { \
		prev = this->prev; \
		free(this); \
	} \
	} while(0)

/*!
  \brief Free the stringfields in a structure
  \param x Pointer to a structure containing fields
  \return nothing

  After calling this macro, the most recently allocated pool
  attached to the structure will be available for use by
  stringfields again.
*/
#define ast_string_field_reset_all(x) do { \
	int index; \
	for (index = 0; index < ast_string_field_count(x); index++) \
		ast_string_field_index_free(x, index); \
	(x)->__field_mgr.used = 0; \
	(x)->__field_mgr.space = (x)->__field_mgr.size; \
	} while(0)

#endif /* _ASTERISK_STRINGFIELDS_H */
