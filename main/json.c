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

/*! \file
 *
 * \brief JSON abstraction layer.
 *
 * This is a very thin wrapper around the Jansson API. For more details on it, see its
 * docs at http://www.digip.org/jansson/doc/2.4/apiref.html.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend>jansson</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/json.h"
#include "asterisk/localtime.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"

#include <jansson.h>
#include <time.h>

/*!
 * \brief Function wrapper around ast_malloc macro.
 */
static void *json_malloc(size_t size)
{
	return ast_malloc(size);
}

/*!
 * \brief Function wrapper around ast_free macro.
 */
static void json_free(void *p)
{
	ast_free(p);
}

void ast_json_set_alloc_funcs(void *(*malloc_fn)(size_t), void (*free_fn)(void*))
{
	json_set_alloc_funcs(malloc_fn, free_fn);
}

void ast_json_reset_alloc_funcs(void)
{
	json_set_alloc_funcs(json_malloc, json_free);
}

struct ast_json *ast_json_ref(struct ast_json *json)
{
	json_incref((json_t *)json);
	return json;
}

void ast_json_unref(struct ast_json *json)
{
	json_decref((json_t *)json);
}

enum ast_json_type ast_json_typeof(const struct ast_json *json)
{
	int r = json_typeof((json_t*)json);
	switch(r) {
	case JSON_OBJECT: return AST_JSON_OBJECT;
	case JSON_ARRAY: return AST_JSON_ARRAY;
	case JSON_STRING: return AST_JSON_STRING;
	case JSON_INTEGER: return AST_JSON_INTEGER;
	case JSON_REAL: return AST_JSON_REAL;
	case JSON_TRUE: return AST_JSON_TRUE;
	case JSON_FALSE: return AST_JSON_FALSE;
	case JSON_NULL: return AST_JSON_NULL;
	}
	ast_assert(0); /* Unexpect return from json_typeof */
	return r;
}

struct ast_json *ast_json_true(void)
{
	return (struct ast_json *)json_true();
}

struct ast_json *ast_json_false(void)
{
	return (struct ast_json *)json_false();
}

struct ast_json *ast_json_boolean(int value)
{
#if JANSSON_VERSION_HEX >= 0x020400
	return (struct ast_json *)json_boolean(value);
#else
	return value ? ast_json_true() : ast_json_false();
#endif
}

struct ast_json *ast_json_null(void)
{
	return (struct ast_json *)json_null();
}

int ast_json_is_true(const struct ast_json *json)
{
	return json_is_true((const json_t *)json);
}

int ast_json_is_false(const struct ast_json *json)
{
	return json_is_false((const json_t *)json);
}

int ast_json_is_null(const struct ast_json *json)
{
	return json_is_null((const json_t *)json);
}

struct ast_json *ast_json_string_create(const char *value)
{
	return (struct ast_json *)json_string(value);
}

const char *ast_json_string_get(const struct ast_json *string)
{
	return json_string_value((json_t *)string);
}

int ast_json_string_set(struct ast_json *string, const char *value)
{
	return json_string_set((json_t *)string, value);
}

struct ast_json *ast_json_stringf(const char *format, ...)
{
	struct ast_json *ret;
	va_list args;
	va_start(args, format);
	ret = ast_json_vstringf(format, args);
	va_end(args);
	return ret;
}

struct ast_json *ast_json_vstringf(const char *format, va_list args)
{
	char *str = NULL;
	json_t *ret = NULL;

	if (format) {
		int err = vasprintf(&str, format, args);
		if (err > 0) {
			ret = json_string(str);
			free(str);
		}
	}
	return (struct ast_json *)ret;
}

struct ast_json *ast_json_integer_create(intmax_t value)
{
	return (struct ast_json *)json_integer(value);
}

intmax_t ast_json_integer_get(const struct ast_json *integer)
{
	return json_integer_value((json_t *)integer);
}

int ast_json_integer_set(struct ast_json *integer, intmax_t value)
{
	return json_integer_set((json_t *)integer, value);
}


int ast_json_equal(const struct ast_json *lhs, const struct ast_json *rhs)
{
	return json_equal((json_t *)lhs, (json_t *)rhs);
}

struct ast_json *ast_json_array_create(void)
{
	return (struct ast_json *)json_array();
}
size_t ast_json_array_size(const struct ast_json *array)
{
	return json_array_size((json_t *)array);
}
struct ast_json *ast_json_array_get(const struct ast_json *array, size_t index)
{
	return (struct ast_json *)json_array_get((json_t *)array, index);
}
int ast_json_array_set(struct ast_json *array, size_t index, struct ast_json *value)
{
	return json_array_set_new((json_t *)array, index, (json_t *)value);
}
int ast_json_array_append(struct ast_json *array, struct ast_json *value)
{
	return json_array_append_new((json_t *)array, (json_t *)value);
}
int ast_json_array_insert(struct ast_json *array, size_t index, struct ast_json *value)
{
	return json_array_insert_new((json_t *)array, index, (json_t *)value);
}
int ast_json_array_remove(struct ast_json *array, size_t index)
{
	return json_array_remove((json_t *)array, index);
}
int ast_json_array_clear(struct ast_json *array)
{
	return json_array_clear((json_t *)array);
}
int ast_json_array_extend(struct ast_json *array, struct ast_json *tail)
{
	return json_array_extend((json_t *)array, (json_t *)tail);
}

struct ast_json *ast_json_object_create(void)
{
	return (struct ast_json *)json_object();
}
size_t ast_json_object_size(struct ast_json *object)
{
	return json_object_size((json_t *)object);
}
struct ast_json *ast_json_object_get(struct ast_json *object, const char *key)
{
	if (!key) {
		return NULL;
	}
	return (struct ast_json *)json_object_get((json_t *)object, key);
}
int ast_json_object_set(struct ast_json *object, const char *key, struct ast_json *value)
{
	return json_object_set_new((json_t *)object, key, (json_t *)value);
}
int ast_json_object_del(struct ast_json *object, const char *key)
{
	return json_object_del((json_t *)object, key);
}
int ast_json_object_clear(struct ast_json *object)
{
	return json_object_clear((json_t *)object);
}
int ast_json_object_update(struct ast_json *object, struct ast_json *other)
{
	return json_object_update((json_t *)object, (json_t *)other);
}
int ast_json_object_update_existing(struct ast_json *object, struct ast_json *other)
{
#if JANSSON_VERSION_HEX >= 0x020300
	return json_object_update_existing((json_t *)object, (json_t *)other);
#else
	struct ast_json_iter *iter = ast_json_object_iter(other);
	int ret = 0;

	if (object == NULL || other == NULL) {
		return -1;
	}

	while (iter != NULL && ret == 0) {
		const char *key = ast_json_object_iter_key(iter);
		if (ast_json_object_get(object, key) != NULL) {
			ret = ast_json_object_set(object, key, ast_json_object_iter_value(iter));
		}
		iter = ast_json_object_iter_next(other, iter);
	}
	return ret;
#endif
}
int ast_json_object_update_missing(struct ast_json *object, struct ast_json *other)
{
#if JANSSON_VERSION_HEX >= 0x020300
	return json_object_update_missing((json_t *)object, (json_t *)other);
#else
	struct ast_json_iter *iter = ast_json_object_iter(other);
	int ret = 0;

	if (object == NULL || other == NULL) {
		return -1;
	}

	while (iter != NULL && ret == 0) {
		const char *key = ast_json_object_iter_key(iter);
		if (ast_json_object_get(object, key) == NULL) {
			ret = ast_json_object_set(object, key, ast_json_object_iter_value(iter));
		}
		iter = ast_json_object_iter_next(other, iter);
	}
	return ret;
#endif
}

struct ast_json_iter *ast_json_object_iter(struct ast_json *object)
{
	return json_object_iter((json_t *)object);
}
struct ast_json_iter *ast_json_object_iter_at(struct ast_json *object, const char *key)
{
	return json_object_iter_at((json_t *)object, key);
}
struct ast_json_iter *ast_json_object_iter_next(struct ast_json *object, struct ast_json_iter *iter)
{
	return json_object_iter_next((json_t *)object, iter);
}
const char *ast_json_object_iter_key(struct ast_json_iter *iter)
{
	return json_object_iter_key(iter);
}
struct ast_json *ast_json_object_iter_value(struct ast_json_iter *iter)
{
	return (struct ast_json *)json_object_iter_value(iter);
}
int ast_json_object_iter_set(struct ast_json *object, struct ast_json_iter *iter, struct ast_json *value)
{
	return json_object_iter_set_new((json_t *)object, iter, (json_t *)value);
}

/*!
 * \brief Default flags for JSON encoding.
 */
static size_t dump_flags(enum ast_json_encoding_format format)
{
	return format == AST_JSON_PRETTY ?
		JSON_INDENT(2) | JSON_PRESERVE_ORDER : JSON_COMPACT;
}

char *ast_json_dump_string_format(struct ast_json *root, enum ast_json_encoding_format format)
{
	return json_dumps((json_t *)root, dump_flags(format));
}

static int write_to_ast_str(const char *buffer, size_t size, void *data)
{
	struct ast_str **dst = data;
	size_t str_size = ast_str_size(*dst);
	size_t remaining = str_size - ast_str_strlen(*dst);
	int ret;

	/* While ast_str_append will grow the ast_str, it won't report
	 * allocation errors. Fortunately, it's not that hard.
	 */

	/* Remaining needs to be big enough for buffer, plus null char */
	while (remaining < size + 1) {
		/* doubling the size of the buffer gives us 'amortized
		 * constant' time.
		 * See http://stackoverflow.com/a/249695/115478 for info.
		 */
		str_size *= 2;
		remaining = str_size - ast_str_strlen(*dst);
	}

	ret = ast_str_make_space(dst, str_size);
	if (ret == -1) {
		/* Could not alloc; fail */
		return -1;
	}

	ast_str_append_substr(dst, -1, buffer, size);
	return 0;
}

int ast_json_dump_str_format(struct ast_json *root, struct ast_str **dst, enum ast_json_encoding_format format)
{
	return json_dump_callback((json_t *)root, write_to_ast_str, dst, dump_flags(format));
}


int ast_json_dump_file_format(struct ast_json *root, FILE *output, enum ast_json_encoding_format format)
{
	if (!root || !output) {
		return -1;
	}
	return json_dumpf((json_t *)root, output, dump_flags(format));
}
int ast_json_dump_new_file_format(struct ast_json *root, const char *path, enum ast_json_encoding_format format)
{
	if (!root || !path) {
		return -1;
	}
	return json_dump_file((json_t *)root, path, dump_flags(format));
}

/*!
 * \brief Copy Jansson error struct to ours.
 */
static void copy_error(struct ast_json_error *error, const json_error_t *jansson_error)
{
	if (error && jansson_error) {
		error->line = jansson_error->line;
		error->column = jansson_error->column;
		error->position = jansson_error->position;
		ast_copy_string(error->text, jansson_error->text, sizeof(error->text));
		ast_copy_string(error->source, jansson_error->source, sizeof(error->source));
	}

}

static void parse_error(struct ast_json_error *error, const char *text, const char *source)
{
	if (error != NULL) {
		error->line = 0;
		error->column = 0;
		error->position = 0;
		strncpy(error->text, text, sizeof(error->text));
		strncpy(error->source, source, sizeof(error->text));
	}
}

struct ast_json *ast_json_load_string(const char *input, struct ast_json_error *error)
{
	json_error_t jansson_error = {};
	struct ast_json *r = NULL;
	if (input != NULL) {
		r = (struct ast_json *)json_loads(input, 0, &jansson_error);
		copy_error(error, &jansson_error);
	} else {
		parse_error(error, "NULL input string", "<null>");
	}
	return r;
}

struct ast_json *ast_json_load_str(const struct ast_str *input, struct ast_json_error *error)
{
	return ast_json_load_string(ast_str_buffer(input), error);
}

struct ast_json *ast_json_load_buf(const char *buffer, size_t buflen, struct ast_json_error *error)
{
	json_error_t jansson_error = {};
	struct ast_json *r = (struct ast_json *)json_loadb(buffer, buflen, 0, &jansson_error);
	copy_error(error, &jansson_error);
	return r;
}
struct ast_json *ast_json_load_file(FILE *input, struct ast_json_error *error)
{
	json_error_t jansson_error = {};
	struct ast_json *r = NULL;
	if (input != NULL) {
		r = (struct ast_json *)json_loadf(input, 0, &jansson_error);
		copy_error(error, &jansson_error);
	} else {
		parse_error(error, "NULL input file", "<null>");
	}
	return r;
}
struct ast_json *ast_json_load_new_file(const char *path, struct ast_json_error *error)
{
	json_error_t jansson_error = {};
	struct ast_json *r = (struct ast_json *)json_load_file(path, 0, &jansson_error);
	copy_error(error, &jansson_error);
	return r;
}

struct ast_json *ast_json_pack(char const *format, ...)
{
	struct ast_json *ret;
	va_list args;
	va_start(args, format);
	ret = ast_json_vpack(format, args);
	va_end(args);
	return ret;
}
struct ast_json *ast_json_vpack(char const *format, va_list ap)
{
	struct ast_json *r = NULL;
	if (format) {
		r = (struct ast_json *)json_vpack_ex(NULL, 0, format, ap);
	}
	return r;
}

struct ast_json *ast_json_copy(const struct ast_json *value)
{
	return (struct ast_json *)json_copy((json_t *)value);
}
struct ast_json *ast_json_deep_copy(const struct ast_json *value)
{
	return (struct ast_json *)json_deep_copy((json_t *)value);
}

struct ast_json *ast_json_name_number(const char *name, const char *number)
{
	return ast_json_pack("{s: s, s: s}",
			     "name", name,
			     "number", number);
}

struct ast_json *ast_json_dialplan_cep(const char *context, const char *exten, int priority)
{
	return ast_json_pack("{s: o, s: o, s: o}",
			     "context", context ? ast_json_string_create(context) : ast_json_null(),
			     "exten", exten ? ast_json_string_create(exten) : ast_json_null(),
			     "priority", priority != -1 ? ast_json_integer_create(priority) : ast_json_null());
}

struct ast_json *ast_json_timeval(const struct timeval tv, const char *zone)
{
	char buf[AST_ISO8601_LEN];
	struct ast_tm tm = {};

	ast_localtime(&tv, &tm, zone);

	ast_strftime(buf, sizeof(buf),AST_ISO8601_FORMAT, &tm);

	return ast_json_string_create(buf);
}

struct ast_json *ast_json_ipaddr(const struct ast_sockaddr *addr, enum ast_transport transport_type)
{
	struct ast_str *string = ast_str_alloca(64);

	if (!string) {
		return NULL;
	}

	ast_str_set(&string, 0, (ast_sockaddr_is_ipv4(addr) ||
		ast_sockaddr_is_ipv4_mapped(addr)) ? "IPV4/" : "IPV6/");

	if (transport_type) {
		char *transport_string = NULL;

		/* NOTE: None will be applied if multiple transport types are specified in transport_type */
		switch(transport_type) {
		case AST_TRANSPORT_UDP:
			transport_string = "UDP";
			break;
		case AST_TRANSPORT_TCP:
			transport_string = "TCP";
			break;
		case AST_TRANSPORT_TLS:
			transport_string = "TLS";
			break;
		case AST_TRANSPORT_WS:
			transport_string = "WS";
			break;
		case AST_TRANSPORT_WSS:
			transport_string = "WSS";
			break;
		}

		if (transport_string) {
			ast_str_append(&string, 0, "%s/", transport_string);
		}
	}

	ast_str_append(&string, 0, "%s", ast_sockaddr_stringify_addr(addr));
	ast_str_append(&string, 0, "/%s", ast_sockaddr_stringify_port(addr));

	return ast_json_string_create(ast_str_buffer(string));
}

void ast_json_init(void)
{
	/* Setup to use Asterisk custom allocators */
	ast_json_reset_alloc_funcs();
}

static void json_payload_destructor(void *obj)
{
	struct ast_json_payload *payload = obj;
	ast_json_unref(payload->json);
}

struct ast_json_payload *ast_json_payload_create(struct ast_json *json)
{
	struct ast_json_payload *payload;

	if (!(payload = ao2_alloc(sizeof(*payload), json_payload_destructor))) {
		return NULL;
	}

	ast_json_ref(json);
	payload->json = json;

	return payload;
}
