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

/*! \file
 *
 * \brief Asterisk JSON abstraction layer.
 *
 * This is a very thin wrapper around the Jansson API. For more details on it, see its
 * docs at http://www.digip.org/jansson/doc/2.4/apiref.html.

 * \author David M. Lee, II <dlee@digium.com>
 */

/*!@{*/

/*!
 * \brief Specifies custom allocators instead of the standard ast_malloc() and ast_free().
 *
 * This is used by the unit tests to do JSON specific memory leak detection. Since it
 * affects all users of the JSON library, shouldn't normally be used.
 */
void ast_json_set_alloc_funcs(void *(*malloc_fn)(size_t), void (*free_fn)(void*));

/*!
 * \struct ast_json
 * \brief Abstract JSON element (object, array, string, int)
 */
struct ast_json;

/*!
 * \brief Increase refcount on \a value.
 * \return The given \a value.
 */
struct ast_json *ast_json_ref(struct ast_json *value);

/*!
 * \brief Decrease refcount on \a value. If refcount reaches zero, \a value is freed.
 */
void ast_json_unref(struct ast_json *value);

/*!@}*/

/*!@{*/

/*!
 * \brief Valid types of a JSON element.
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
 * \brief Return the type of \a value.
 */
enum ast_json_type ast_json_typeof(const struct ast_json *value);

/*!@}*/

/*!@{*/

/*!
 * \brief Gets the JSON true value.
 */
struct ast_json *ast_json_true(void);

/*!
 * \brief Gets the JSON false value.
 */
struct ast_json *ast_json_false(void);

/*!
 * \brief Returns JSON false if \a value is 0, JSON true otherwise.
 */
struct ast_json *ast_json_boolean(int value);

/*!
 * \brief Gets the JSON null value.
 */
struct ast_json *ast_json_null(void);

/*!
 * \brief Returns true (non-zero) if \a value is the JSON true value, false (zero)
 * otherwise.
 */
int ast_json_is_true(const struct ast_json *value);

/*!
 * \brief Returns true (non-zero) if \a value is the JSON false value, false (zero)
 * otherwise.
 */
int ast_json_is_false(const struct ast_json *value);

/*!
 * \brief Returns true (non-zero) if \a value is the JSON null value, false (zero)
 * otherwise.
 */
int ast_json_is_null(const struct ast_json *value);

/*!@}*/

/*!@{*/

/*!
 * \brief Constructs a JSON string from \a value.
 *
 * \a value must be a valid ASCII or UTF-8 encoded string.
 *
 * \return Newly constructed string element, or \c NULL on failure.
 */
struct ast_json *ast_json_string_create(const char *value);

/*!
 * \brief Returns the string value of \a string, or \c NULL if it's not a JSON string.
 */
const char *ast_json_string_get(const struct ast_json *string);

/*!
 * \brief Changes the string value of \a string to \a value.
 *
 * \return 0 on success, or -1 on error.
 */
int ast_json_string_set(struct ast_json *string, const char *value);

/*!
 * \brief Create a JSON string, printf style.
 *
 * \return Newly allocated string, or \c NULL if allocation fails.
 */
struct ast_json *ast_json_stringf(const char *format, ...) __attribute__((format(printf, 1, 2)));

/*!
 * \brief Create a JSON string, vprintf style.
 *
 * \return Newly allocated string, or \c NULL if allocation fails.
 */
struct ast_json *ast_json_vstringf(const char *format, va_list args) __attribute__((format(printf, 1, 0)));

/*!@}*/

/*!@{*/

/*!
 * \brief Create a JSON integer with the give \a value.
 *
 * \return Newly allocated integer, or \c NULL if allocation fails.
 */
struct ast_json *ast_json_integer_create(intmax_t value);

/*!
 * \brief Get the value from \a integer.
 *
 * \return integer value, or 0 for non-integers.
 */
intmax_t ast_json_integer_get(const struct ast_json *integer);

/*!
 * \brief Sets the value of \a integer.
 *
 * \return 0 on success, -1 on failure
 */
int ast_json_integer_set(struct ast_json *integer, intmax_t value);

/*!@}*/

/*!@{*/

/*!
 * \brief Create a empty JSON array.
 *
 * \return Newly allocated array, or \c NULL if allocation fails.
 */
struct ast_json *ast_json_array_create(void);

/*!
 * \brief Returns the size of \a array, or 0 if argument is not an array.
 */
size_t ast_json_array_size(const struct ast_json *array);

/*!
 * \brief Returns the element in the \a index position from the \a array.
 *
 * The returned element is a borrowed reference; use ast_json_ref() to safely keep a
 * pointer to it.
 *
 * \return The specified element, or \c NULL if \a array not an array or \a index is out
 * of bounds.
 */
struct ast_json *ast_json_array_get(const struct ast_json *array, size_t index);

/*!
 * \brief Changes the \a index element in \a array to \a value.
 *
 * The array steals the \a value reference; use ast_json_ref() to safely keep a pointer
 * to it.
 *
 * \return 0 on success, or -1 on failure.
 */
int ast_json_array_set(struct ast_json *array, size_t index, struct ast_json *value);

/*!
 * \brief Appends \a value to \a array.
 *
 * The array steals the \a value reference; use ast_json_ref() to safely keep a pointer
 * to it.
 *
 * \return 0 on success, or -1 on failure.
 */
int ast_json_array_append(struct ast_json *array, struct ast_json *value);

/*!
 * \brief Inserts \a value into \a array at position \a index.
 *
 * The array steals the \a value reference; use ast_json_ref() to safely keep a pointer
 * to it.
 *
 * \return 0 on success, or -1 on failure.
 */
int ast_json_array_insert(struct ast_json *array, size_t index, struct ast_json *value);

/*!
 * \brief Removes the element at position \a index from \a array.
 *
 * \return 0 on success, or -1 on failure.
 */
int ast_json_array_remove(struct ast_json *array, size_t index);

/*!
 * \brief Removes all elements from \a array.
 *
 * \return 0 on success, or -1 on failure.
 */
int ast_json_array_clear(struct ast_json *array);

/*!
 * \brief Appends all elements from \a tail to \a array.
 *
 * The \a tail argument is left alone, so ast_json_unref() it when you are done with it.
 *
 * \return 0 on success, or -1 on failure.
 */
int ast_json_array_extend(struct ast_json *array, struct ast_json *tail);

/*!@}*/

/*!@{*/

/*!
 * \brief Create a new JSON object.
 *
 * \return Newly allocated object, or \c NULL if allocation fails.
 */
struct ast_json *ast_json_object_create(void);

/*!
 * \brief Returns the size of \a object, or 0 if it's not a JSON object.
 */
size_t ast_json_object_size(struct ast_json *object);

/*!
 * \brief Returns the element from \a object with the given \a key, or \c NULL on error.
 */
struct ast_json *ast_json_object_get(struct ast_json *object, const char *key);

/*!
 * \brief Sets the value of \a key to \a value in \a object.
 *
 * The object steals the \a value reference; use ast_json_ref() to safely keep a pointer
 * to it.
 *
 * \return 0 on success, or -1 on error.
 */
int ast_json_object_set(struct ast_json *object, const char *key, struct ast_json *value);

/*!
 * \brief Deletes \a key from \a object.
 *
 * \return 0 on success, or -1 if key does not exist.
 */
int ast_json_object_del(struct ast_json *object, const char *key);

/*!
 * \brief Deletes all elements from \a object.
 *
 * \return 0 on success, or -1 on error.
 */
int ast_json_object_clear(struct ast_json *object);

/*!
 * \brief Updates the elements of \a object with all of the elements \a other, overwriting
 * existing keys and adding new ones.
 *
 * The \a other argument is left alone, so ast_json_unref() it when you are done with it.
 *
 * \return 0 on success, or -1 on error.
 */
int ast_json_object_update(struct ast_json *object, struct ast_json *other);

/*!
 * \brief Updates the elements of \a object with the elements of \a other, only
 * overwriting existing keys.
 *
 * The \a other argument is left alone, so ast_json_unref() it when you are done with it.
 *
 * \return 0 on success, or -1 on error.
 */
int ast_json_object_update_existing(struct ast_json *object, struct ast_json *other);

/*!
 * \brief Updates the elements of \a object with the elements of \a other, only adding new
 * ones, not changing existing values.
 *
 * The \a other argument is left alone, so ast_json_unref() it when you are done with it.
 *
 * \return 0 on success, or -1 on error.
 */
int ast_json_object_update_missing(struct ast_json *object, struct ast_json *other);

/*!
 * \struct ast_json_iter
 * \brief Iterator for JSON object key/values. Note that iteration order is not specified,
 * and may change as keys are added to and removed from the object.
 */
struct ast_json_iter;

/*!
 * \brief Returns an iterator pointing to the first element in \a object, or \c NULL if
 * \a object is \c NULL or empty.
 */
struct ast_json_iter *ast_json_object_iter(struct ast_json *object);

/*!
 * \brief Returns an iterator pointing to a specified \a key in \a object. Iterating
 * forward from this iterator may not to cover all elements in \a object.
 */
struct ast_json_iter *ast_json_object_iter_at(struct ast_json *object, const char *key);

/*!
 * \brief Returns an iterator pointing to the next key-value pair after \a iter, or
 * \c NULL if at the end.
 */
struct ast_json_iter *ast_json_object_iter_next(struct ast_json *object, struct ast_json_iter *iter);

/*!
 * \brief Returns key from \a iter.
 */
const char *ast_json_object_iter_key(struct ast_json_iter *iter);

/*!
 * \brief Returns value from \a iter.
 *
 * The returned element is a borrowed reference; use ast_json_ref() to safely
 * keep a pointer to it.
 */
struct ast_json *ast_json_object_iter_value(struct ast_json_iter *iter);

/*!
 * \brief Sets the value in \a object at \a iter to \a value.
 *
 * The array steals the value reference; use ast_json_ref() to safely keep a
 * pointer to it.
 *
 * \return 0 on success, or -1 on error.
 */
int ast_json_object_iter_set(struct ast_json *object, struct ast_json_iter *iter, struct ast_json *value);

/*!@}*/

/*!@{*/

/*!
 * \brief Returns JSON representation of \a root, or \c NULL on error.
 *
 * Returned string must be freed by calling ast_free().
 */
char *ast_json_dump_string(struct ast_json *root);

/*!
 * \brief Writes JSON representation of \a root to \a dst.
 *
 * If \a dst is too small, it will be grown as needed.
 *
 * \return 0 on success, or -1 on error. On error, the contents of \a dst are
 * undefined.
 */
int ast_json_dump_str(struct ast_json *root, struct ast_str **dst);

/*!
 * \brief Writes JSON representation of \a root to \a output.
 *
 * \return 0 on success, or -1 on error.
 */
int ast_json_dump_file(struct ast_json *root, FILE *output);

/*!
 * \brief Writes JSON representation of \a root to a file at \a path.
 *
 * \return 0 on success, or -1 on error.
 */
int ast_json_dump_new_file(struct ast_json *root, const char *path);

#define AST_JSON_ERROR_TEXT_LENGTH    160
#define AST_JSON_ERROR_SOURCE_LENGTH   80

/*!
 * \brief JSON parsing error information.
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
	/*! Source of the error (filename or <string>) */
	char source[AST_JSON_ERROR_TEXT_LENGTH];
};

/*!
 * \brief Parses null terminated \a input string into a JSON object or array.
 *
 * \param[out] error Filled with information on error.
 *
 * \return Parsed JSON element, or \c NULL on error.
 */
struct ast_json *ast_json_load_string(const char *input, struct ast_json_error *error);

/*!
 * \brief Parses null terminated \a input ast_str into a JSON object or array.
 *
 * \param[out] error Filled with information on error.
 *
 * \return Parsed JSON element, or \c NULL on error.
 */
struct ast_json *ast_json_load_str(const struct ast_str *input, struct ast_json_error *error);

/*!
 * \brief Parses \a buffer with length \a buflen into a JSON object or array.
 *
 * \param[out] error Filled with information on error.
 *
 * \return Parsed JSON element, or \c NULL on error.
 */
struct ast_json *ast_json_load_buf(const char *buffer, size_t buflen, struct ast_json_error *error);

/*!
 * \brief Parses \a input into JSON object or array.
 *
 * \param[out] error Filled with information on error.
 *
 * \return Parsed JSON element, or \c NULL on error.
 */
struct ast_json *ast_json_load_file(FILE *input, struct ast_json_error *error);

/*!
 * \brief Parses file at \a path into JSON object or array.
 *
 * \param[out] error Filled with information on error.
 *
 * \return Parsed JSON element, or \c NULL on error.
 */
struct ast_json *ast_json_load_new_file(const char *path, struct ast_json_error *error);

/*!
 * \brief Helper for creating complex JSON values.
 *
 * See original Jansson docs at http://www.digip.org/jansson/doc/2.4/apiref.html#apiref-pack
 * for more details.
 */
struct ast_json *ast_json_pack(char const *format, ...);

/*!
 * \brief Helper for creating complex JSON values simply.
 *
 * See original Jansson docs at http://www.digip.org/jansson/doc/2.4/apiref.html#apiref-pack
 * for more details.
 */
struct ast_json *ast_json_vpack(char const *format, va_list ap);

/*!@}*/

/*!@{*/

/*!
 * \brief Compares two JSON objects.
 *
 * Two JSON objects are equal if they are of the same type, and their contents are equal.
 *
 * \return true (non-zero) if \a lhs and \a rhs are equal, and false (zero) otherwise.
 */
int ast_json_equal(const struct ast_json *lhs, const struct ast_json *rhs);

/*!
 * \brief Returns shallow copy (does not copy child elements) of \a value, or \c NULL
 * on error.
 */
struct ast_json *ast_json_copy(const struct ast_json *value);

/*!
 * \brief Returns deep copy (copies child elements) of \a value, or \c NULL on error.
 */
struct ast_json *ast_json_deep_copy(const struct ast_json *value);

/*!@}*/

#endif /* _ASTERISK_JSON_H */
