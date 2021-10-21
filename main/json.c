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
 * This is a very thin wrapper around the Jansson API. For more details on it,
 * see its docs at http://www.digip.org/jansson/doc/2.11/apiref.html.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend>jansson</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/json.h"
#include "asterisk/localtime.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/channel.h"
#include "asterisk/callerid.h"

#include <jansson.h>
#include <time.h>

void *ast_json_malloc(size_t size)
{
	return ast_malloc(size);
}

void ast_json_free(void *p)
{
	ast_free(p);
}

void ast_json_set_alloc_funcs(void *(*malloc_fn)(size_t), void (*free_fn)(void*))
{
	json_set_alloc_funcs(malloc_fn, free_fn);
}

void ast_json_reset_alloc_funcs(void)
{
	json_set_alloc_funcs(ast_json_malloc, ast_json_free);
}

struct ast_json *ast_json_ref(struct ast_json *json)
{
	json_incref((json_t *)json);
	return json;
}

void ast_json_unref(struct ast_json *json)
{
	json_decref((json_t *) json);
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

const char *ast_json_typename(enum ast_json_type type)
{
	switch (type) {
	case AST_JSON_OBJECT: return "object";
	case AST_JSON_ARRAY: return "array";
	case AST_JSON_STRING: return "string";
	case AST_JSON_INTEGER: return "integer";
	case AST_JSON_REAL: return "real";
	case AST_JSON_TRUE: return "boolean";
	case AST_JSON_FALSE: return "boolean";
	case AST_JSON_NULL: return "null";
	}
	ast_assert(0);
	return "?";
}

/* Ported from libjansson utf.c:utf8_check_first() */
static size_t json_utf8_check_first(char byte)
{
	unsigned char ch = (unsigned char) byte;

	if (ch < 0x80) {
		return 1;
	}

	if (0x80 <= ch && ch <= 0xBF) {
		/* second, third or fourth byte of a multi-byte
		   sequence, i.e. a "continuation byte" */
		return 0;
	} else if (ch == 0xC0 || ch == 0xC1) {
		/* overlong encoding of an ASCII byte */
		return 0;
	} else if (0xC2 <= ch && ch <= 0xDF) {
		/* 2-byte sequence */
		return 2;
	} else if (0xE0 <= ch && ch <= 0xEF) {
		/* 3-byte sequence */
		return 3;
	} else if (0xF0 <= ch && ch <= 0xF4) {
		/* 4-byte sequence */
		return 4;
	} else { /* ch >= 0xF5 */
		/* Restricted (start of 4-, 5- or 6-byte sequence) or invalid
		   UTF-8 */
		return 0;
	}
}

/* Ported from libjansson utf.c:utf8_check_full() */
static size_t json_utf8_check_full(const char *str, size_t len)
{
	size_t pos;
	int32_t value;
	unsigned char ch = (unsigned char) str[0];

	if (len == 2) {
		value = ch & 0x1F;
	} else if (len == 3) {
		value = ch & 0xF;
	} else if (len == 4) {
		value = ch & 0x7;
	} else {
		return 0;
	}

	for (pos = 1; pos < len; ++pos) {
		ch = (unsigned char) str[pos];
		if (ch < 0x80 || ch > 0xBF) {
			/* not a continuation byte */
			return 0;
		}

		value = (value << 6) + (ch & 0x3F);
	}

	if (value > 0x10FFFF) {
		/* not in Unicode range */
		return 0;
	} else if (0xD800 <= value && value <= 0xDFFF) {
		/* invalid code point (UTF-16 surrogate halves) */
		return 0;
	} else if ((len == 2 && value < 0x80)
		|| (len == 3 && value < 0x800)
		|| (len == 4 && value < 0x10000)) {
		/* overlong encoding */
		return 0;
	}

	return 1;
}

int ast_json_utf8_check_len(const char *str, size_t len)
{
	size_t pos;
	size_t count;
	int res = 1;

	if (!str) {
		return 0;
	}

	/*
	 * Since the json library does not make the check function
	 * public we recreate/copy the function in our interface
	 * module.
	 *
	 * Loop ported from libjansson utf.c:utf8_check_string()
	 */
	for (pos = 0; pos < len; pos += count) {
		count = json_utf8_check_first(str[pos]);
		if (count == 0) {
			res = 0;
			break;
		} else if (count > 1) {
			if (count > len - pos) {
				/* UTF-8 needs more than we have left in the string. */
				res = 0;
				break;
			}

			if (!json_utf8_check_full(&str[pos], count)) {
				res = 0;
				break;
			}
		}
	}

	if (!res) {
		ast_debug(1, "String '%.*s' is not UTF-8 for json conversion\n", (int) len, str);
	}
	return res;
}

int ast_json_utf8_check(const char *str)
{
	return str ? ast_json_utf8_check_len(str, strlen(str)) : 0;
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
	return (struct ast_json *)json_boolean(value);
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
	json_t *ret = NULL;

	if (format) {
		/* json_pack was not introduced until jansson-2.0 so Asterisk could never
		 * be compiled against older versions.  The version check can never match
		 * anything older than 2.12. */
#if defined(HAVE_JANSSON_BUNDLED) || JANSSON_MAJOR_VERSION > 2 || JANSSON_MINOR_VERSION > 11
		ret = json_vsprintf(format, args);
#else
		char *str = NULL;
		int err = ast_vasprintf(&str, format, args);

		if (err >= 0) {
			ret = json_string(str);
			ast_free(str);
		}
#endif
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

struct ast_json *ast_json_real_create(double value)
{
	return (struct ast_json *)json_real(value);
}

double ast_json_real_get(const struct ast_json *real)
{
	return json_real_value((json_t *)real);
}

int ast_json_real_set(struct ast_json *real, double value)
{
	return json_real_set((json_t *)real, value);
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
	return json_object_update_existing((json_t *)object, (json_t *)other);
}
int ast_json_object_update_missing(struct ast_json *object, struct ast_json *other)
{
	return json_object_update_missing((json_t *)object, (json_t *)other);
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
	json_error_t error;
	struct ast_json *r = NULL;
	if (format) {
		r = (struct ast_json *)json_vpack_ex(&error, 0, format, ap);
		if (!r && !ast_strlen_zero(error.text)) {
			ast_log(LOG_ERROR,
				"Error building JSON from '%s': %s.\n",
				format, error.text);
			ast_log_backtrace();
		}
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
		"name", AST_JSON_UTF8_VALIDATE(name),
		"number", AST_JSON_UTF8_VALIDATE(number));
}

struct ast_json *ast_json_dialplan_cep_app(
		const char *context, const char *exten, int priority, const char *app_name, const char *app_data)
{
	return ast_json_pack("{s: s?, s: s?, s: o, s: s?, s: s?}",
		"context", context,
		"exten", exten,
		"priority", priority != -1 ? ast_json_integer_create(priority) : ast_json_null(),
		"app_name", app_name,
		"app_data", app_data
		);
}

struct ast_json *ast_json_dialplan_cep(const char *context, const char *exten, int priority)
{
	return ast_json_dialplan_cep_app(context, exten, priority, "", "");
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

int ast_json_init(void)
{
	json_t *version_check;

	/* Setup to use Asterisk custom allocators */
	ast_json_reset_alloc_funcs();

	/* We depend on functionality of jansson-2.11 but don't actually use
	 * any symbols.  If we link at runtime to less than 2.11 this json_pack
	 * will return NULL. */
	version_check = json_pack("{s: o?, s: o*}",
		"JSON", NULL,
		"Bourne", NULL);
	if (!version_check) {
		ast_log(LOG_ERROR, "There was a problem finding jansson 2.11 runtime libraries.\n"
			"Please rebuild Asterisk using ./configure --with-jansson-bundled.\n");
		return -1;
	}

	json_decref(version_check);

	return 0;
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

static struct ast_json *json_party_number(struct ast_party_number *number)
{
	if (!number->valid) {
		return NULL;
	}
	return ast_json_pack("{s: s, s: i, s: i, s: s}",
		"number", AST_JSON_UTF8_VALIDATE(number->str),
		"plan", number->plan,
		"presentation", number->presentation,
		"presentation_txt", ast_describe_caller_presentation(number->presentation));
}

static struct ast_json *json_party_name(struct ast_party_name *name)
{
	if (!name->valid) {
		return NULL;
	}
	return ast_json_pack("{s: s, s: s, s: i, s: s}",
		"name", AST_JSON_UTF8_VALIDATE(name->str),
		"character_set", ast_party_name_charset_describe(name->char_set),
		"presentation", name->presentation,
		"presentation_txt", ast_describe_caller_presentation(name->presentation));
}

static struct ast_json *json_party_subaddress(struct ast_party_subaddress *subaddress)
{
	if (!subaddress->valid) {
		return NULL;
	}
	return ast_json_pack("{s: s, s: i, s: b}",
		"subaddress", AST_JSON_UTF8_VALIDATE(subaddress->str),
		"type", subaddress->type,
		"odd", subaddress->odd_even_indicator);
}

struct ast_json *ast_json_party_id(struct ast_party_id *party)
{
	int pres = ast_party_id_presentation(party);

	/* Combined party presentation */
	return ast_json_pack("{s: i, s: s, s: o*, s: o*, s: o*}",
		"presentation", pres,
		"presentation_txt", ast_describe_caller_presentation(pres),
		"number", json_party_number(&party->number),
		"name", json_party_name(&party->name),
		"subaddress", json_party_subaddress(&party->subaddress));
}

enum ast_json_to_ast_vars_code ast_json_to_ast_variables(struct ast_json *json_variables, struct ast_variable **variables)
{
	struct ast_json_iter *it_json_var;
	struct ast_variable *tail = NULL;

	*variables = NULL;

	for (it_json_var = ast_json_object_iter(json_variables); it_json_var;
		it_json_var = ast_json_object_iter_next(json_variables, it_json_var)) {
		struct ast_variable *new_var;
		const char *key = ast_json_object_iter_key(it_json_var);
		const char *value;
		struct ast_json *json_value;

		if (ast_strlen_zero(key)) {
			continue;
		}

		json_value = ast_json_object_iter_value(it_json_var);
		if (ast_json_typeof(json_value) != AST_JSON_STRING) {
			/* Error: Only strings allowed */
			ast_variables_destroy(*variables);
			*variables = NULL;
			return AST_JSON_TO_AST_VARS_CODE_INVALID_TYPE;
		}
		value = ast_json_string_get(json_value);
		/* Should never be NULL.  Otherwise, how could it be a string type? */
		ast_assert(value != NULL);
		if (!value) {
			/* To be safe. */
			continue;
		}
		new_var = ast_variable_new(key, value, "");
		if (!new_var) {
			/* Error: OOM */
			ast_variables_destroy(*variables);
			*variables = NULL;
			return AST_JSON_TO_AST_VARS_CODE_OOM;
		}

		tail = ast_variable_list_append_hint(variables, tail, new_var);
	}

	return AST_JSON_TO_AST_VARS_CODE_SUCCESS;
}

struct ast_json *ast_json_channel_vars(struct varshead *channelvars)
{
	struct ast_json *ret;
	struct ast_var_t *var;

	ret = ast_json_object_create();
	AST_LIST_TRAVERSE(channelvars, var, entries) {
		ast_json_object_set(ret, var->name, ast_json_string_create(var->value));
	}

	return ret;
}

struct ast_json *ast_json_object_create_vars(const struct ast_variable *variables, const char *excludes)
{
	const struct ast_variable *i;
	struct ast_json *obj;

	obj = ast_json_object_create();
	if (!obj) {
		return NULL;
	}

	for (i = variables; i; i = i->next) {
		if (!excludes || !ast_in_delimited_string(i->name, excludes, ',')) {
			ast_json_object_set(obj, i->name, ast_json_string_create(i->value));
		}
	}

	return obj;
}
