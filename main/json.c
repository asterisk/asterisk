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
 * see its docs at http://www.digip.org/jansson/doc/2.4/apiref.html.
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
#include "asterisk/channel.h"
#include "asterisk/callerid.h"

#include <jansson.h>
#include <time.h>

/*! \brief Magic number, for safety checks. */
#define JSON_MAGIC 0x1541992

/*! \brief Internal structure for allocated memory blocks */
struct json_mem {
	/*! Magic number, for safety checks */
	uint32_t magic;
	/*! Mutext for locking this memory block */
	ast_mutex_t mutex;
	/*! Linked list pointer for the free list */
	AST_LIST_ENTRY(json_mem) list;
	/*! Data section of the allocation; void pointer for proper alignment */
	void *data[];
};

/*! \brief Free a \ref json_mem block. */
static void json_mem_free(struct json_mem *mem)
{
	mem->magic = 0;
	ast_mutex_destroy(&mem->mutex);
	ast_free(mem);
}

/*!
 * \brief Get the \ref json_mem block for a pointer allocated via
 * ast_json_malloc().
 *
 * This function properly handles Jansson singletons (null, true, false), and
 * \c NULL.
 *
 * \param p Pointer, usually to a \c json_t or \ref ast_json.
 * \return \ref json_mem object with extra allocation info.
 */
static inline struct json_mem *to_json_mem(void *p)
{
	struct json_mem *mem;
	/* Avoid ref'ing the singleton values */
	if (p == NULL || p == json_null() || p == json_true() ||
		p == json_false()) {
		return NULL;
	}
	mem = (struct json_mem *)((char *) (p) - sizeof(*mem));
	ast_assert(mem->magic == JSON_MAGIC);
	return mem;
}

/*!
 * \brief Lock an \ref ast_json instance.
 *
 * If \a json is an immutable singleton (null, true, false), this function
 * safely ignores it and returns \c NULL. Otherwise, \a json must have been
 * allocates using ast_json_malloc().
 *
 * \param json JSON instance to lock.
 * \return \ref Corresponding \ref json_mem block.
 * \return \c NULL if \a json was not allocated.
 */
static struct json_mem *json_mem_lock(struct ast_json *json)
{
	struct json_mem *mem = to_json_mem(json);
	if (!mem) {
		return NULL;
	}
	ast_mutex_lock(&mem->mutex);
	return mem;
}

/*!
 * \brief Unlock a \ref json_mem instance.
 *
 * \param mem \ref json_mem, usually returned from json_mem_lock().
 */
static void json_mem_unlock(struct json_mem *mem)
{
	if (!mem) {
		return;
	}
	ast_mutex_unlock(&mem->mutex);
}

/*!
 * \brief Scoped lock for a \ref ast_json instance.
 *
 * \param json JSON instance to lock.
 */
#define SCOPED_JSON_LOCK(json)				\
	RAII_VAR(struct json_mem *, __mem_ ## __LINE__, \
		json_mem_lock(json), json_mem_unlock)

void *ast_json_malloc(size_t size)
{
	struct json_mem *mem = ast_malloc(size + sizeof(*mem));
	if (!mem) {
		return NULL;
	}
	mem->magic = JSON_MAGIC;
	ast_mutex_init(&mem->mutex);
	return mem->data;
}

AST_THREADSTORAGE(json_free_list_ts);

/*!
 * \brief Struct for a linked list of \ref json_mem.
 */
AST_LIST_HEAD_NOLOCK(json_mem_list, json_mem);

/*!
 * \brief Thread local list of \ref json_mem blocks to free at the end of an
 * unref.
 */
static struct json_mem_list *json_free_list(void)
{
	return ast_threadstorage_get(&json_free_list_ts,
		sizeof(struct json_mem_list));
}

void ast_json_free(void *p)
{
	struct json_mem *mem;
	struct json_mem_list *free_list;
	mem = to_json_mem(p);

	if (!mem) {
		return;
	}

	/* Since the unref is holding a lock in mem, we can't free it
	 * immediately. Store it off on a thread local list to be freed by
	 * ast_json_unref().
	 */
	free_list = json_free_list();
	if (!free_list) {
		ast_log(LOG_ERROR, "Error allocating free list\n");
		ast_assert(0);
		/* It's not ideal to free the memory immediately, but that's the
		 * best we can do if the threadlocal allocation fails */
		json_mem_free(mem);
		return;
	}

	AST_LIST_INSERT_HEAD(free_list, mem, list);
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
	/* Jansson refcounting is non-atomic; lock it. */
	SCOPED_JSON_LOCK(json);
	json_incref((json_t *)json);
	return json;
}

void ast_json_unref(struct ast_json *json)
{
	struct json_mem_list *free_list;
	struct json_mem *mem;

	if (!json) {
		return;
	}

	/* Jansson refcounting is non-atomic; lock it. */
	{
		SCOPED_JSON_LOCK(json);

		json_decref((json_t *) json);
	}

	/* Now free any objects that were ast_json_free()'s while the lock was
	 * held */
	free_list = json_free_list();
	if (!free_list) {
		return;
	}

	while ((mem = AST_LIST_REMOVE_HEAD(free_list, list))) {
		json_mem_free(mem);
	}
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
		int err = ast_vasprintf(&str, format, args);
		if (err > 0) {
			ret = json_string(str);
			ast_free(str);
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
			struct ast_json *value = ast_json_object_iter_value(iter);

			if (!value || ast_json_object_set(object, key, ast_json_ref(value))) {
				ret = -1;
			}
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
			struct ast_json *value = ast_json_object_iter_value(iter);

			if (!value || ast_json_object_set(object, key, ast_json_ref(value))) {
				ret = -1;
			}
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
	/* Jansson's json_dump*, even though it's a read operation, isn't
	 * thread safe for concurrent reads. Locking is necessary.
	 * See http://www.digip.org/jansson/doc/2.4/portability.html#thread-safety. */
	SCOPED_JSON_LOCK(root);
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
	/* Jansson's json_dump*, even though it's a read operation, isn't
	 * thread safe for concurrent reads. Locking is necessary.
	 * See http://www.digip.org/jansson/doc/2.4/portability.html#thread-safety. */
	SCOPED_JSON_LOCK(root);
	return json_dump_callback((json_t *)root, write_to_ast_str, dst, dump_flags(format));
}


int ast_json_dump_file_format(struct ast_json *root, FILE *output, enum ast_json_encoding_format format)
{
	/* Jansson's json_dump*, even though it's a read operation, isn't
	 * thread safe for concurrent reads. Locking is necessary.
	 * See http://www.digip.org/jansson/doc/2.4/portability.html#thread-safety. */
	SCOPED_JSON_LOCK(root);
	if (!root || !output) {
		return -1;
	}
	return json_dumpf((json_t *)root, output, dump_flags(format));
}
int ast_json_dump_new_file_format(struct ast_json *root, const char *path, enum ast_json_encoding_format format)
{
	/* Jansson's json_dump*, even though it's a read operation, isn't
	 * thread safe for concurrent reads. Locking is necessary.
	 * See http://www.digip.org/jansson/doc/2.4/portability.html#thread-safety. */
	SCOPED_JSON_LOCK(root);
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

static struct ast_json *json_party_number(struct ast_party_number *number)
{
	if (!number->valid) {
		return NULL;
	}
	return ast_json_pack("{s: s, s: i, s: i, s: s}",
		"number", number->str,
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
		"name", name->str,
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
		"subaddress", subaddress->str,
		"type", subaddress->type,
		"odd", subaddress->odd_even_indicator);
}

struct ast_json *ast_json_party_id(struct ast_party_id *party)
{
	RAII_VAR(struct ast_json *, json_party_id, NULL, ast_json_unref);
	int pres;

	/* Combined party presentation */
	pres = ast_party_id_presentation(party);
	json_party_id = ast_json_pack("{s: i, s: s}",
		"presentation", pres,
		"presentation_txt", ast_describe_caller_presentation(pres));
	if (!json_party_id) {
		return NULL;
	}

	/* Party number */
	if (party->number.valid && ast_json_object_set(json_party_id, "number", json_party_number(&party->number))) {
		return NULL;
	}

	/* Party name */
	if (party->name.valid && ast_json_object_set(json_party_id, "name", json_party_name(&party->name))) {
		return NULL;
	}

	/* Party subaddress */
	if (party->subaddress.valid && ast_json_object_set(json_party_id, "subaddress", json_party_subaddress(&party->subaddress))) {
		return NULL;
	}

	return ast_json_ref(json_party_id);
}

enum ast_json_to_ast_vars_code ast_json_to_ast_variables(struct ast_json *json_variables, struct ast_variable **variables)
{
	struct ast_json_iter *it_json_var;

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

		ast_variable_list_append(variables, new_var);
	}

	return AST_JSON_TO_AST_VARS_CODE_SUCCESS;
}
