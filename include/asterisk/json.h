/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

#ifndef _ASTERISK_JSON_H
#define _ASTERISK_JSON_H

#include "asterisk/netsock2.h"

/*! \file
 *
 * \brief Asterisk JSON abstraction layer.
 * \since 12.0.0
 *
 * This is a very thin wrapper around the Jansson API. For more details on it,
 * see its docs at http://www.digip.org/jansson/doc/2.11/apiref.html.
 *
 * Rather than provide the multiple ways of doing things that the Jansson API
 * does, the Asterisk wrapper is always reference-stealing, and always NULL
 * safe.
 *
 * And by always, I mean that the reference is stolen even if the function
 * fails. This avoids lots of conditional logic, and also avoids having to track
 * zillions of local variables when building complex JSON objects. You can
 * instead chain \c ast_json_* calls together safely and only worry about
 * cleaning up the root object.
 *
 * In the cases where you have a need to introduce intermediate objects, just
 * wrap them with json_ref() when passing them to other \c ast_json_*()
 * functions.
 *
 * \par Example code
 *
 * \code
 *	// Example of how to use the Asterisk JSON API
 *	static struct ast_json *foo(void) {
 *		RAII_VAR(struct ast_json *, array, NULL, ast_json_unref);
 *		RAII_VAR(struct ast_json *, obj, NULL, ast_json_unref);
 *		int i, res;
 *
 *		array = ast_json_array_create();
 *		if (!array) { return NULL; }
 *
 *		for (i = 0; i < 10; ++i) {
 *			// NULL safety and object stealing means calls can
 *			// be chained together directly.
 *			res = ast_json_array_append(array,
 *				ast_json_integer_create(i));
 *			if (res != 0) { return NULL; }
 *		}
 *
 *		obj = ast_json_object_create();
 *		if (!obj) { return NULL; }
 *
 *		// If you already have an object reference, ast_json_ref()
 *		// can be used inline to bump the ref before passing it along
 *		// to a ref-stealing call
 *		res = ast_json_object_set(obj, "foo", ast_json_ref(array));
 *		if (!res) { return NULL; }
 *
 *		return obj;
 *	}
 * \endcode
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*!@{*/

/*!
 * \brief Primarily used to cast when packing to an "I" type.
 */
typedef AST_JSON_INT_T ast_json_int_t;

/*!
 * \brief Initialize the JSON library.
 */
int ast_json_init(void);

/*!
 * \brief Set custom allocators instead of the standard ast_malloc() and ast_free().
 * \since 12.0.0
 *
 * This is used by the unit tests to do JSON specific memory leak detection. Since it
 * affects all users of the JSON library, shouldn't normally be used.
 *
 * \param malloc_fn Custom allocation function.
 * \param free_fn Matching free function.
 */
void ast_json_set_alloc_funcs(void *(*malloc_fn)(size_t), void (*free_fn)(void*));

/*!
 * \brief Change alloc funcs back to the resource module defaults.
 * \since 12.0.0
 *
 * If you use ast_json_set_alloc_funcs() to temporarily change the allocator functions
 * (i.e., from in a unit test), this function sets them back to ast_malloc() and
 * ast_free().
 */
void ast_json_reset_alloc_funcs(void);

/*!
 * \brief Asterisk's custom JSON allocator. Exposed for use by unit tests.
 * \since 12.0.0
 * \internal
 */
void *ast_json_malloc(size_t size);

/*!
 * \brief Asterisk's custom JSON allocator. Exposed for use by unit tests.
 * \since 12.0.0
 * \internal
 */
void ast_json_free(void *p);

/*!
 * \struct ast_json
 * \brief Abstract JSON element (object, array, string, int, ...).
 * \since 12.0.0
 */
struct ast_json;

/*!
 * \brief Increase refcount on \a value.
 * \since 12.0.0
 *
 * \param value JSON value to reference.
 * \return The given \a value.
 */
struct ast_json *ast_json_ref(struct ast_json *value);

/*!
 * \brief Decrease refcount on \a value. If refcount reaches zero, \a value is freed.
 * \since 12.0.0
 *
 * \note It is safe to pass \c NULL to this function.
 */
void ast_json_unref(struct ast_json *value);

/*!@}*/

/*!@{*/

/*!
 * \brief Valid types of a JSON element.
 * \since 12.0.0
 */
enum ast_json_type
{
	AST_JSON_OBJECT,
	AST_JSON_ARRAY,
	AST_JSON_STRING,
	AST_JSON_INTEGER,
	AST_JSON_REAL,
	AST_JSON_TRUE,
	AST_JSON_FALSE,
	AST_JSON_NULL,
};

/*!
 * \brief Get the type of \a value.
 * \since 12.0.0
 * \param value Value to query.
 * \return Type of \a value.
 */
enum ast_json_type ast_json_typeof(const struct ast_json *value);

/*!
 * \brief Get the string name for the given type.
 * \since 12.0.0
 * \param type Type to convert to string.
 * \return Simple string for the type name (object, array, string, etc.)
 * \retval "?" for invalid types.
 */
const char *ast_json_typename(enum ast_json_type type);

/*!@}*/

/*!@{*/

/*!
 * \brief Check the string of the given length for UTF-8 format.
 * \since 13.12.0
 *
 * \param str String to check.
 * \param len Length of string to check.
 *
 * \retval 0 if not UTF-8 encoded or str is NULL.
 * \retval 1 if UTF-8 encoded.
 */
int ast_json_utf8_check_len(const char *str, size_t len);

/*!
 * \brief Check the nul terminated string for UTF-8 format.
 * \since 13.12.0
 *
 * \param str String to check.
 *
 * \retval 0 if not UTF-8 encoded or str is NULL.
 * \retval 1 if UTF-8 encoded.
 */
int ast_json_utf8_check(const char *str);

/*!
 * \brief Check str for UTF-8 and replace with an empty string if fails the check.
 *
 * \note The convenience macro is normally used with ast_json_pack()
 * or a function wrapper that calls ast_json_vpack().
 */
#define AST_JSON_UTF8_VALIDATE(str) (ast_json_utf8_check(str) ? (str) : "")

/*!@}*/

/*!@{*/

/*!
 * \brief Get the JSON true value.
 * \since 12.0.0
 *
 * The returned value is a singleton, and does not need to be
 * ast_json_unref()'ed.
 *
 * \return JSON true.
 */
struct ast_json *ast_json_true(void);

/*!
 * \brief Get the JSON false value.
 * \since 12.0.0
 *
 * The returned value is a singleton, and does not need to be
 * ast_json_unref()'ed.
 *
 * \return JSON false.
 */
struct ast_json *ast_json_false(void);

/*!
 * \brief Get the JSON boolean corresponding to \a value.
 * \since 12.0.0
 * \return JSON true if value is true (non-zero).
 * \return JSON false if value is false (zero).
 */
struct ast_json *ast_json_boolean(int value);

/*!
 * \brief Get the JSON null value.
 * \since 12.0.0
 *
 * The returned value is a singleton, and does not need to be
 * ast_json_unref()'ed.
 *
 * \return JSON null.
 */
struct ast_json *ast_json_null(void);

/*!
 * \brief Check if \a value is JSON array.
 * \since 12.0.0
 * \retval True (non-zero) if \a value == \ref ast_json_array().
 * \retval False (zero) otherwise..
 */
int ast_json_is_array(const struct ast_json *value);

/*!
 * \brief Check if \a value is JSON object.
 * \since 12.0.0
 * \retval True (non-zero) if \a value == \ref ast_json_object().
 * \retval False (zero) otherwise..
 */
int ast_json_is_object(const struct ast_json *value);

/*!
 * \brief Check if \a value is JSON true.
 * \since 12.0.0
 * \retval True (non-zero) if \a value == \ref ast_json_true().
 * \retval False (zero) otherwise..
 */
int ast_json_is_true(const struct ast_json *value);

/*!
 * \brief Check if \a value is JSON false.
 * \since 12.0.0
 * \retval True (non-zero) if \a value == \ref ast_json_false().
 * \retval False (zero) otherwise.
 */
int ast_json_is_false(const struct ast_json *value);

/*!
 * \brief Check if \a value is JSON null.
 * \since 12.0.0
 * \retval True (non-zero) if \a value == \ref ast_json_false().
 * \retval False (zero) otherwise.
 */
int ast_json_is_null(const struct ast_json *value);

/*!@}*/

/*!@{*/

/*!
 * \brief Construct a JSON string from \a value.
 * \since 12.0.0
 *
 * The given \a value must be a valid ASCII or UTF-8 encoded string.
 *
 * \param value Value of new JSON string.
 * \return Newly constructed string element.
 * \retval NULL on error.
 */
struct ast_json *ast_json_string_create(const char *value);

/*!
 * \brief Get the value of a JSON string.
 * \since 12.0.0
 * \param string JSON string.
 * \return Value of the string.
 * \retval NULL on error.
 */
const char *ast_json_string_get(const struct ast_json *string);

/*!
 * \brief Change the value of a JSON string.
 * \since 12.0.0
 *
 * The given \a value must be a valid ASCII or UTF-8 encoded string.
 *
 * \param string JSON string to modify.
 * \param value New value to store in \a string.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_string_set(struct ast_json *string, const char *value);

/*!
 * \brief Create a JSON string, printf style.
 * \since 12.0.0
 *
 * The formatted value must be a valid ASCII or UTF-8 encoded string.
 *
 * \param format \c printf style format string.
 * \return Newly allocated string.
 * \retval NULL on error.
 */
struct ast_json *ast_json_stringf(const char *format, ...) __attribute__((format(printf, 1, 2)));

/*!
 * \brief Create a JSON string, vprintf style.
 * \since 12.0.0
 *
 * The formatted value must be a valid ASCII or UTF-8 encoded string.
 *
 * \param format \c printf style format string.
 * \param args
 *
 * \return Newly allocated string.
 * \retval NULL on error.
 */
struct ast_json *ast_json_vstringf(const char *format, va_list args) __attribute__((format(printf, 1, 0)));

/*!@}*/

/*!@{*/

/*!
 * \brief Create a JSON integer.
 * \since 12.0.0
 * \param value Value of the new JSON integer.
 * \return Newly allocated integer.
 * \retval NULL on error.
 */
struct ast_json *ast_json_integer_create(intmax_t value);

/*!
 * \brief Get the value from a JSON integer.
 * \since 12.0.0
 * \param integer JSON integer.
 * \return Value of a JSON integer.
 * \retval 0 if \a integer is not a JSON integer.
 */
intmax_t ast_json_integer_get(const struct ast_json *integer);

/*!
 * \brief Set the value of a JSON integer.
 * \since 12.0.0
 * \param integer JSON integer to modify.
 * \param value New value for \a integer.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_integer_set(struct ast_json *integer, intmax_t value);

/*!
 * \brief Create a JSON real number.
 * \since 12.0.0
 * \param value Value of the new JSON real number.
 * \return Newly allocated real number.
 * \retval NULL on error.
 */
struct ast_json *ast_json_real_create(double value);

/*!
 * \brief Get the value from a JSON real number.
 * \since 12.0.0
 * \param real JSON real number.
 * \return Value of a JSON real number.
 * \retval 0 if \a real is not a JSON real number.
 */
double ast_json_real_get(const struct ast_json *real);

/*!
 * \brief Set the value of a JSON real number.
 * \since 12.0.0
 * \param real JSON real number to modify.
 * \param value New value for \a real.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_real_set(struct ast_json *real, double value);

/*!@}*/

/*!@{*/

/*!
 * \brief Create a empty JSON array.
 * \since 12.0.0
 * \return Newly allocated array.
 * \retval NULL on error.
 */
struct ast_json *ast_json_array_create(void);

/*!
 * \brief Get the size of a JSON array.
 * \since 12.0.0
 * \param array JSON array.
 * \return Size of \a array.
 * \retval 0 if array is not a JSON array.
 */
size_t ast_json_array_size(const struct ast_json *array);

/*!
 * \brief Get an element from an array.
 * \since 12.0.0
 *
 * The returned element is a borrowed reference; use ast_json_ref() to safely keep a
 * pointer to it.
 *
 * \param array JSON array.
 * \param index Zero-based index into \a array.
 * \return The specified element.
 * \retval NULL if \a array not an array.
 *              if \a index is out of bounds.
 */
struct ast_json *ast_json_array_get(const struct ast_json *array, size_t index);

/*!
 * \brief Change an element in an array.
 * \since 12.0.0
 *
 * \note The \a array steals the \a value reference even if it returns error;
 * use ast_json_ref() to safely keep a pointer to it.
 *
 * \param array JSON array to modify.
 * \param index Zero-based index into array.
 * \param value New JSON value to store in \a array at \a index.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_array_set(struct ast_json *array, size_t index, struct ast_json *value);

/*!
 * \brief Append to an array.
 * \since 12.0.0
 *
 * \note The \a array steals the \a value reference even if it returns error;
 * use ast_json_ref() to safely keep a pointer to it.
 *
 * \param array JSON array to modify.
 * \param value New JSON value to store at the end of \a array.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_array_append(struct ast_json *array, struct ast_json *value);

/*!
 * \brief Insert into an array.
 * \since 12.0.0
 *
 * \note The \a array steals the \a value reference even if it returns error;
 * use ast_json_ref() to safely keep a pointer to it.
 *
 * \param array JSON array to modify.
 * \param index Zero-based index into array.
 * \param value New JSON value to store in \a array at \a index.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_array_insert(struct ast_json *array, size_t index, struct ast_json *value);

/*!
 * \brief Remove an element from an array.
 * \since 12.0.0
 * \param array JSON array to modify.
 * \param index Zero-based index into array.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_array_remove(struct ast_json *array, size_t index);

/*!
 * \brief Remove all elements from an array.
 * \since 12.0.0
 * \param array JSON array to clear.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_array_clear(struct ast_json *array);

/*!
 * \brief Append all elements from \a tail to \a array.
 * \since 12.0.0
 *
 * The \a tail argument is not changed, so ast_json_unref() it when you are done with it.
 *
 * \param array JSON array to modify.
 * \param tail JSON array with contents to append to \a array.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_array_extend(struct ast_json *array, struct ast_json *tail);

/*!@}*/

/*!@{*/

/*!
 * \brief Create a new JSON object.
 * \since 12.0.0
 * \return Newly allocated object.
 * \retval NULL on error.
 */
struct ast_json *ast_json_object_create(void);

/*!
 * \brief Create a new JSON object using the given variables
 * \param variables A list of Asterisk variables
 * \param excludes Comma separated string of variable names to exclude (optional)
 * \return Newly allocated object.
 * \retval NULL on error.
 */
struct ast_json *ast_json_object_create_vars(const struct ast_variable *variables, const char *excludes);

/*!
 * \brief Get size of JSON object.
 * \since 12.0.0
 * \param object JSON object.
 * \return Size of \a object.
 * \retval Zero of \a object is not a JSON object.
 */
size_t ast_json_object_size(struct ast_json *object);

/*!
 * \brief Get a field from a JSON object.
 * \since 12.0.0
 *
 * The returned element is a borrowed reference; use ast_json_ref() to safely keep a
 * pointer to it.
 *
 * \param object JSON object.
 * \param key Key of field to look up.
 * \return Value with given \a key.
 * \retval NULL on error.
 */
struct ast_json *ast_json_object_get(struct ast_json *object, const char *key);

/*!
 * \brief Get a string field from a JSON object.
 * \since 16.3.0
 *
 * \param object JSON object.
 * \param key Key of string field to look up.
 * \return String value of given \a key.
 * \retval NULL on error, or key value is not a string.
 */
#define ast_json_object_string_get(object, key) ast_json_string_get(ast_json_object_get(object, key))

/*!
 * \brief Get an integer field from a JSON object.
 * \param object JSON object.
 * \param key Key of integer field to look up.
 * \return Value of a JSON integer.
 * \retval 0 if \a integer is not a JSON integer.
 */
#define ast_json_object_integer_get(object, key) ast_json_integer_get(ast_json_object_get(object, key))

/*!
 * \brief Get a double field from a JSON object.
 * \param object JSON object.
 * \param key Key of double field to look up.
 * \return Value of a JSON double.
 * \retval 0 if \a real is not a JSON real number.
 */
#define ast_json_object_real_get(object, key) ast_json_real_get(ast_json_object_get(object, key))

/*!
 * \brief Set a field in a JSON object.
 * \since 12.0.0
 *
 * \note The object steals the \a value reference even if it returns error;
 * use ast_json_ref() to safely keep a pointer to it.
 *
 * \param object JSON object to modify.
 * \param key Key of field to set.
 * \param value JSON value to set for field.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_object_set(struct ast_json *object, const char *key, struct ast_json *value);

/*!
 * \brief Delete a field from a JSON object.
 * \since 12.0.0
 *
 * \param object JSON object to modify.
 * \param key Key of field to delete.
 * \retval 0 on success, or -1 if key does not exist.
 */
int ast_json_object_del(struct ast_json *object, const char *key);

/*!
 * \brief Delete all elements from a JSON object.
 * \since 12.0.0
 * \param object JSON object to clear.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_object_clear(struct ast_json *object);

/*!
 * \brief Update \a object with all of the fields of \a other.
 * \since 12.0.0
 *
 * All of the fields of \a other are copied into \a object, overwriting existing keys.
 * The \a other object is not changed, so ast_json_unref() it when you are done with it.
 *
 * \param object JSON object to modify.
 * \param other JSON object to copy into \a object.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_object_update(struct ast_json *object, struct ast_json *other);

/*!
 * \brief Update existing fields in \a object with the fields of \a other.
 * \since 12.0.0
 *
 * Like ast_json_object_update(), but only existing fields are updated. No new fields
 * will get added. The \a other object is not changed, so ast_json_unref() it when you
 * are done with it.
 *
 * \param object JSON object to modify.
 * \param other JSON object to copy into \a object.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_object_update_existing(struct ast_json *object, struct ast_json *other);

/*!
 * \brief Add new fields to \a object with the fields of \a other.
 * \since 12.0.0
 *
 * Like ast_json_object_update(), but only missing fields are added. No existing fields
 * will be modified. The \a other object is not changed, so ast_json_unref() it when you
 * are done with it.
 *
 * \param object JSON object to modify.
 * \param other JSON object to copy into \a object.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_object_update_missing(struct ast_json *object, struct ast_json *other);

/*!
 * \struct ast_json_iter
 * \brief Iterator for JSON object key/values.
 * \since 12.0.0
 *
 * Note that iteration order is not specified, and may change as fields are added to
 * and removed from the object.
 */
struct ast_json_iter;

/*!
 * \brief Get an iterator pointing to the first field in a JSON object.
 * \since 12.0.0
 *
 * The order of the fields in an object are not specified. However, iterating forward
 * from this iterator will cover all fields in \a object. Adding or removing fields from
 * \a object may invalidate its iterators.
 *
 * \param object JSON object.
 * \return Iterator to the first field in \a object.
 * \retval NULL \a object is empty.
 *              on error.
 */
struct ast_json_iter *ast_json_object_iter(struct ast_json *object);

/*!
 * \brief Get an iterator pointing to a specified \a key in \a object.
 * \since 12.0.0
 *
 * Iterating forward from this iterator may not to cover all elements in \a object.
 *
 * \param object JSON object to iterate.
 * \param key Key of field to lookup.
 * \return Iterator pointing to the field with the given \a key.
 * \retval NULL if \a key does not exist.
 *              on error.
 */
struct ast_json_iter *ast_json_object_iter_at(struct ast_json *object, const char *key);

/*!
 * \brief Get the next iterator.
 * \since 12.0.0
 * \param object JSON object \a iter was obtained from.
 * \param iter JSON object iterator.
 * \return Iterator to next field in \a object.
 * \retval NULL if \a iter was the last field.
 */
struct ast_json_iter *ast_json_object_iter_next(struct ast_json *object, struct ast_json_iter *iter);

/*!
 * \brief Get the key from an iterator.
 * \since 12.0.0
 * \param iter JSON object iterator.
 * \return Key of the field \a iter points to.
 */
const char *ast_json_object_iter_key(struct ast_json_iter *iter);

/*!
 * \brief Get the value from an iterator.
 * \since 12.0.0
 *
 * The returned element is a borrowed reference; use ast_json_ref() to safely
 * keep a pointer to it.
 *
 * \param iter JSON object iterator.
 * \return Value of the field \a iter points to.
 */
struct ast_json *ast_json_object_iter_value(struct ast_json_iter *iter);

/*!
 * \brief Set the value of the field pointed to by an iterator.
 * \since 12.0.0
 *
 * \note The object steals the \p value reference even if it returns error;
 * use ast_json_ref() to safely keep a pointer to it.
 *
 * \param object JSON object \p iter was obtained from.
 * \param iter JSON object iterator.
 * \param value JSON value to store in \p iter's field.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_json_object_iter_set(struct ast_json *object, struct ast_json_iter *iter, struct ast_json *value);

/*!@}*/

/*!@{*/

/*!
 * \brief Encoding format type.
 * \since 12.0.0
 */
enum ast_json_encoding_format
{
	/*! Compact format, low human readability */
	AST_JSON_COMPACT,
	/*! Formatted for human readability */
	AST_JSON_PRETTY,
	/*! Keys sorted alphabetically */
	AST_JSON_SORTED,
};

/*!
 * \brief Encode a JSON value to a compact string.
 * \since 12.0.0
 *
 * Returned string must be freed by calling ast_json_free().
 *
 * \param root JSON value.
 * \return String encoding of \a root.
 * \retval NULL on error.
 */
#define ast_json_dump_string(root) ast_json_dump_string_format(root, AST_JSON_COMPACT)

/*!
 * \brief Encode a JSON value to a string.
 * \since 12.0.0
 *
 * Returned string must be freed by calling ast_json_free().
 *
 * \param root JSON value.
 * \param format encoding format type.
 * \return String encoding of \a root.
 * \retval NULL on error.
 */
char *ast_json_dump_string_format(struct ast_json *root, enum ast_json_encoding_format format);

/*!
 * \brief Encode a JSON value to a string, with its keys sorted.
 *
 * Returned string must be freed by calling ast_json_free().
 *
 * \param root JSON value.
 * \return String encoding of \a root.
 * \retval NULL on error.
 */
#define ast_json_dump_string_sorted(root) ast_json_dump_string_format(root, AST_JSON_SORTED)

#define ast_json_dump_str(root, dst) ast_json_dump_str_format(root, dst, AST_JSON_COMPACT)

/*!
 * \brief Encode a JSON value to an \ref ast_str.
 * \since 12.0.0
 *
 * If \a dst is too small, it will be grown as needed.
 *
 * \param root JSON value.
 * \param dst \ref ast_str to store JSON encoding.
 * \param format encoding format type.
 * \retval 0 on success.
 * \retval -1 on error. The contents of \a dst are undefined.
 */
int ast_json_dump_str_format(struct ast_json *root, struct ast_str **dst, enum ast_json_encoding_format format);

#define ast_json_dump_file(root, output) ast_json_dump_file_format(root, output, AST_JSON_COMPACT)

/*!
 * \brief Encode a JSON value to a \c FILE.
 * \since 12.0.0
 *
 * \param root JSON value.
 * \param output File to write JSON encoding to.
 * \param format encoding format type.
 * \retval 0 on success.
 * \retval -1 on error. The contents of \a output are undefined.
 */
int ast_json_dump_file_format(struct ast_json *root, FILE *output, enum ast_json_encoding_format format);

#define ast_json_dump_new_file(root, path) ast_json_dump_new_file_format(root, path, AST_JSON_COMPACT)

/*!
 * \brief Encode a JSON value to a file at the given location.
 * \since 12.0.0
 *
 * \param root JSON value.
 * \param path Path to file to write JSON encoding to.
 * \param format encoding format type.
 * \retval 0 on success.
 * \retval -1 on error. The contents of \a output are undefined.
 */
int ast_json_dump_new_file_format(struct ast_json *root, const char *path, enum ast_json_encoding_format format);

#define AST_JSON_ERROR_TEXT_LENGTH    160
#define AST_JSON_ERROR_SOURCE_LENGTH   80

/*!
 * \brief JSON parsing error information.
 * \since 12.0.0
 */
struct ast_json_error {
	/*! Line number error occured on */
	int line;
	/*! Character (not byte, can be different for UTF-8) column on which the error occurred. */
	int column;
	/*! Position in bytes from start of input */
	int position;
	/*! Error message */
	char text[AST_JSON_ERROR_TEXT_LENGTH];
	/*! Source of the error (filename or \<string\>) */
	char source[AST_JSON_ERROR_TEXT_LENGTH];
};

/*!
 * \brief Parse null terminated string into a JSON object or array.
 * \since 12.0.0
 * \param input String to parse.
 * \param[out] error Filled with information on error.
 * \return Parsed JSON element.
 * \retval NULL on error.
 */
struct ast_json *ast_json_load_string(const char *input, struct ast_json_error *error);

/*!
 * \brief Parse \ref ast_str into a JSON object or array.
 * \since 12.0.0
 * \param input \ref ast_str to parse.
 * \param[out] error Filled with information on error.
 * \return Parsed JSON element.
 * \retval NULL on error.
 */
struct ast_json *ast_json_load_str(const struct ast_str *input, struct ast_json_error *error);

/*!
 * \brief Parse buffer with known length into a JSON object or array.
 * \since 12.0.0
 * \param buffer Buffer to parse.
 * \param buflen Length of \a buffer.
 * \param[out] error Filled with information on error.
 * \return Parsed JSON element.
 * \retval NULL on error.
 */
struct ast_json *ast_json_load_buf(const char *buffer, size_t buflen, struct ast_json_error *error);

/*!
 * \brief Parse a \c FILE into JSON object or array.
 * \since 12.0.0
 * \param input \c FILE to parse.
 * \param[out] error Filled with information on error.
 * \return Parsed JSON element.
 * \retval NULL on error.
 */
struct ast_json *ast_json_load_file(FILE *input, struct ast_json_error *error);

/*!
 * \brief Parse file at \a path into JSON object or array.
 * \since 12.0.0
 * \param path Path of file to parse.
 * \param[out] error Filled with information on error.
 * \return Parsed JSON element.
 * \retval NULL on error.
 */
struct ast_json *ast_json_load_new_file(const char *path, struct ast_json_error *error);

/*!
 * \brief Helper for creating complex JSON values.
 * \since 12.0.0
 *
 * See original Jansson docs at http://www.digip.org/jansson/doc/2.11/apiref.html#apiref-pack
 * for more details.
 */
struct ast_json *ast_json_pack(char const *format, ...);

/*!
 * \brief Helper for creating complex JSON values simply.
 * \since 12.0.0
 *
 * See original Jansson docs at http://www.digip.org/jansson/doc/2.11/apiref.html#apiref-pack
 * for more details.
 */
struct ast_json *ast_json_vpack(char const *format, va_list ap);

/*!@}*/

/*!@{*/

/*!
 * \brief Compare two JSON objects.
 * \since 12.0.0
 *
 * Two JSON objects are equal if they are of the same type, and their contents are equal.
 *
 * \param lhs Value to compare.
 * \param rhs Other value to compare.
 * \retval True (non-zero) if \a lhs and \a rhs are equal.
 * \retval False (zero) if they are not.
 */
int ast_json_equal(const struct ast_json *lhs, const struct ast_json *rhs);

/*!
 * \brief Copy a JSON value, but not its children.
 * \since 12.0.0
 *
 * If \a value is a JSON object or array, its children are shared with the returned copy.
 *
 * \param value JSON value to copy.
 * \return Shallow copy of \a value.
 * \retval NULL on error.
 */
struct ast_json *ast_json_copy(const struct ast_json *value);

/*!
 * \brief Copy a JSON value, and its children.
 * \since 12.0.0
 *
 * If \a value is a JSON object or array, they are also copied.
 *
 * \param value JSON value to copy.
 * \return Deep copy of \a value.
 * \retval NULL on error.
 */
struct ast_json *ast_json_deep_copy(const struct ast_json *value);

/*!@}*/

/*!@{*/

/*!
 * \brief Common JSON rendering functions for common 'objects'.
 */

/*!
 * \brief Simple name/number pair.
 * \param name Name
 * \param number Number
 * \return JSON object with name and number fields
 * \retval NULL on error (non-UTF8 characters, NULL inputs, etc.)
 */
struct ast_json *ast_json_name_number(const char *name, const char *number);

/*!
 * \brief Construct a timeval as JSON.
 *
 * JSON does not define a standard date format (boo), but the de facto standard
 * is to use ISO 8601 formatted string. We build a millisecond resolution string
 * from the \c timeval
 *
 * \param tv \c timeval to encode.
 * \param zone Text string of a standard system zoneinfo file.  If NULL, the system localtime will be used.
 * \return JSON string with ISO 8601 formatted date/time.
 * \retval NULL on error.
 */
struct ast_json *ast_json_timeval(const struct timeval tv, const char *zone);

/*!
 * \brief Construct an IP address as JSON
 *
 * XXX some comments describing the need for this here
 *
 * \param addr ast_sockaddr to encode
 * \param transport_type ast_transport to include in the address string if any. Should just be one.
 * \return JSON string containing the IP address with optional transport information
 * \retval NULL on error.
 */
struct ast_json *ast_json_ipaddr(const struct ast_sockaddr *addr, enum ast_transport transport_type);

/*!
 * \brief Construct a context/exten/priority/application/application_data as JSON.
 *
 * If a \c NULL is passed for \c context or \c exten or \c app_name or \c app_data,
 * or -1 for \c priority, the fields is set to ast_json_null().
 *
 * \param context Context name.
 * \param exten Extension.
 * \param priority Dialplan priority.
 * \param app_name Application name.
 * \param app_data Application argument.
 * \return JSON object with \c context, \c exten and \c priority \c app_name \c app_data fields
 */
struct ast_json *ast_json_dialplan_cep_app(
		const char *context, const char *exten, int priority, const char *app_name, const char *app_data);

/*!
 * \brief Construct a context/exten/priority as JSON.
 *
 * If a \c NULL is passed for \c context or \c exten, or -1 for \c priority,
 * the fields is set to ast_json_null().
 *
 * \param context Context name.
 * \param exten Extension.
 * \param priority Dialplan priority.
 * \return JSON object with \c context, \c exten and \c priority fields
 */
struct ast_json *ast_json_dialplan_cep(const char *context, const char *exten, int priority);

struct ast_json_payload {
	struct ast_json *json;
};

/*!
 * \brief Create an ao2 object to pass json blobs as data payloads for stasis
 * \since 12.0.0
 *
 * \param json the ast_json blob we are loading
 *
 * \return pointer to the ast_json_payload created
 * \retval NULL if we fail to alloc it
 */
struct ast_json_payload *ast_json_payload_create(struct ast_json *json);

struct ast_party_id;
/*!
 * \brief Construct an ast_party_id as JSON.
 * \since 12.0.0
 *
 * \param party The party ID to represent as JSON.
 *
 * \return JSON object with \c name, \c number and \c subaddress objects
 * for those that are valid in the party ID
 */
struct ast_json *ast_json_party_id(struct ast_party_id *party);

enum ast_json_to_ast_vars_code {
	/*! \brief Conversion successful */
	AST_JSON_TO_AST_VARS_CODE_SUCCESS,
	/*!
	 * \brief Conversion failed because invalid value type supplied.
	 * \note Only string values allowed.
	 */
	AST_JSON_TO_AST_VARS_CODE_INVALID_TYPE,
	/*! \brief Conversion failed because of allocation failure. (Out Of Memory) */
	AST_JSON_TO_AST_VARS_CODE_OOM,
};

/*!
 * \brief Convert a \c ast_json list of key/value pair tuples into a \c ast_variable list
 * \since 12.5.0
 *
 * \param json_variables The JSON blob containing the variable
 * \param variables An out reference to the variables to populate.
 *
 * \code
 * struct ast_json *json_variables = ast_json_pack("[ { s: s } ]", "foo", "bar");
 * struct ast_variable *variables = NULL;
 * int res;
 *
 * res = ast_json_to_ast_variables(json_variables, &variables);
 * \endcode
 *
 * \return Conversion enum ast_json_to_ast_vars_code status
 */
enum ast_json_to_ast_vars_code ast_json_to_ast_variables(struct ast_json *json_variables, struct ast_variable **variables);

struct varshead;

/*!
 * \brief Construct a JSON object from a \c ast_var_t list
 * \since 14.2.0
 *
 * \param channelvars The list of \c ast_var_t to represent as JSON
 *
 * \return JSON object with variable names as keys and variable values as values
 */
struct ast_json *ast_json_channel_vars(struct varshead *channelvars);

/*!@}*/

#endif /* _ASTERISK_JSON_H */
