/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Memory Management
 *
 * \author Mark Spencer <markster@digium.com>
 */

#include "asterisk.h"

#ifdef __AST_DEBUG_MALLOC

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/paths.h"	/* use ast_config_AST_LOG_DIR */
#include <stddef.h>
#include <time.h>

#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/strings.h"
#include "asterisk/unaligned.h"

#define SOME_PRIME 563

enum func_type {
	FUNC_CALLOC = 1,
	FUNC_MALLOC,
	FUNC_REALLOC,
	FUNC_STRDUP,
	FUNC_STRNDUP,
	FUNC_VASPRINTF,
	FUNC_ASPRINTF
};

/* Undefine all our macros */
#undef malloc
#undef calloc
#undef realloc
#undef strdup
#undef strndup
#undef free
#undef vasprintf
#undef asprintf

#define FENCE_MAGIC 0xdeadbeef

static FILE *mmlog;

/* NOTE: Be EXTREMELY careful with modifying this structure; the total size of this structure
   must result in 'automatic' alignment so that the 'fence' field lands exactly at the end of
   the structure in memory (and thus immediately before the allocated region the fence is
   supposed to be used to monitor). In other words, we cannot allow the compiler to insert
   any padding between this structure and anything following it, so add up the sizes of all the
   fields and compare to sizeof(struct ast_region)... if they don't match, then the compiler
   is padding the structure and either the fields need to be rearranged to eliminate internal
   padding, or a dummy field will need to be inserted before the 'fence' field to push it to
   the end of the actual space it will consume. Note that this must be checked for both 32-bit
   and 64-bit platforms, as the sizes of pointers and 'size_t' differ on these platforms.
*/

static struct ast_region {
	struct ast_region *next;
	size_t len;
	char file[64];
	char func[40];
	unsigned int lineno;
	enum func_type which;
	unsigned int cache;		/* region was allocated as part of a cache pool */
	unsigned int fence;
	unsigned char data[0];
} *regions[SOME_PRIME];

#define HASH(a) \
	(((unsigned long)(a)) % SOME_PRIME)

/*! Tracking this mutex will cause infinite recursion, as the mutex tracking
 *  code allocates memory */
AST_MUTEX_DEFINE_STATIC_NOTRACKING(reglock);

#define astmm_log(...)                               \
	do {                                         \
		fprintf(stderr, __VA_ARGS__);        \
		if (mmlog) {                         \
			fprintf(mmlog, __VA_ARGS__); \
			fflush(mmlog);               \
		}                                    \
	} while (0)

static inline void *__ast_alloc_region(size_t size, const enum func_type which, const char *file, int lineno, const char *func, unsigned int cache)
{
	struct ast_region *reg;
	void *ptr = NULL;
	unsigned int *fence;
	int hash;

	if (!(reg = malloc(size + sizeof(*reg) + sizeof(*fence)))) {
		astmm_log("Memory Allocation Failure - '%d' bytes in function %s "
			  "at line %d of %s\n", (int) size, func, lineno, file);
		return NULL;
	}

	ast_copy_string(reg->file, file, sizeof(reg->file));
	ast_copy_string(reg->func, func, sizeof(reg->func));
	reg->lineno = lineno;
	reg->len = size;
	reg->which = which;
	reg->cache = cache;
	ptr = reg->data;
	hash = HASH(ptr);
	reg->fence = FENCE_MAGIC;
	fence = (ptr + reg->len);
	put_unaligned_uint32(fence, FENCE_MAGIC);

	ast_mutex_lock(&reglock);
	reg->next = regions[hash];
	regions[hash] = reg;
	ast_mutex_unlock(&reglock);

	return ptr;
}

static inline size_t __ast_sizeof_region(void *ptr)
{
	int hash = HASH(ptr);
	struct ast_region *reg;
	size_t len = 0;
	
	ast_mutex_lock(&reglock);
	for (reg = regions[hash]; reg; reg = reg->next) {
		if (reg->data == ptr) {
			len = reg->len;
			break;
		}
	}
	ast_mutex_unlock(&reglock);

	return len;
}

static void __ast_free_region(void *ptr, const char *file, int lineno, const char *func)
{
	int hash;
	struct ast_region *reg, *prev = NULL;
	unsigned int *fence;

	if (!ptr)
		return;

	hash = HASH(ptr);

	ast_mutex_lock(&reglock);
	for (reg = regions[hash]; reg; reg = reg->next) {
		if (reg->data == ptr) {
			if (prev)
				prev->next = reg->next;
			else
				regions[hash] = reg->next;
			break;
		}
		prev = reg;
	}
	ast_mutex_unlock(&reglock);

	if (reg) {
		fence = (unsigned int *)(reg->data + reg->len);
		if (reg->fence != FENCE_MAGIC) {
			astmm_log("WARNING: Low fence violation at %p, in %s of %s, "
				"line %d\n", reg->data, reg->func, reg->file, reg->lineno);
		}
		if (get_unaligned_uint32(fence) != FENCE_MAGIC) {
			astmm_log("WARNING: High fence violation at %p, in %s of %s, "
				"line %d\n", reg->data, reg->func, reg->file, reg->lineno);
		}
		free(reg);
	} else {
		astmm_log("WARNING: Freeing unused memory at %p, in %s of %s, line %d\n",	
			ptr, func, file, lineno);
	}
}

void *__ast_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func) 
{
	void *ptr;

	if ((ptr = __ast_alloc_region(size * nmemb, FUNC_CALLOC, file, lineno, func, 0))) 
		memset(ptr, 0, size * nmemb);

	return ptr;
}

void *__ast_calloc_cache(size_t nmemb, size_t size, const char *file, int lineno, const char *func) 
{
	void *ptr;

	if ((ptr = __ast_alloc_region(size * nmemb, FUNC_CALLOC, file, lineno, func, 1))) 
		memset(ptr, 0, size * nmemb);

	return ptr;
}

void *__ast_malloc(size_t size, const char *file, int lineno, const char *func) 
{
	return __ast_alloc_region(size, FUNC_MALLOC, file, lineno, func, 0);
}

void __ast_free(void *ptr, const char *file, int lineno, const char *func) 
{
	__ast_free_region(ptr, file, lineno, func);
}

void *__ast_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func) 
{
	void *tmp;
	size_t len = 0;

	if (ptr && !(len = __ast_sizeof_region(ptr))) {
		astmm_log("WARNING: Realloc of unalloced memory at %p, in %s of %s, "
			"line %d\n", ptr, func, file, lineno);
		return NULL;
	}

	if (!(tmp = __ast_alloc_region(size, FUNC_REALLOC, file, lineno, func, 0)))
		return NULL;

	if (len > size)
		len = size;
	if (ptr) {
		memcpy(tmp, ptr, len);
		__ast_free_region(ptr, file, lineno, func);
	}
	
	return tmp;
}

char *__ast_strdup(const char *s, const char *file, int lineno, const char *func) 
{
	size_t len;
	void *ptr;

	if (!s)
		return NULL;

	len = strlen(s) + 1;
	if ((ptr = __ast_alloc_region(len, FUNC_STRDUP, file, lineno, func, 0)))
		strcpy(ptr, s);

	return ptr;
}

char *__ast_strndup(const char *s, size_t n, const char *file, int lineno, const char *func) 
{
	size_t len;
	void *ptr;

	if (!s)
		return NULL;

	len = strlen(s) + 1;
	if (len > n)
		len = n;
	if ((ptr = __ast_alloc_region(len, FUNC_STRNDUP, file, lineno, func, 0)))
		strcpy(ptr, s);

	return ptr;
}

int __ast_asprintf(const char *file, int lineno, const char *func, char **strp, const char *fmt, ...)
{
	int size;
	va_list ap, ap2;
	char s;

	*strp = NULL;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	size = vsnprintf(&s, 1, fmt, ap2);
	va_end(ap2);
	if (!(*strp = __ast_alloc_region(size + 1, FUNC_ASPRINTF, file, lineno, func, 0))) {
		va_end(ap);
		return -1;
	}
	vsnprintf(*strp, size + 1, fmt, ap);
	va_end(ap);

	return size;
}

int __ast_vasprintf(char **strp, const char *fmt, va_list ap, const char *file, int lineno, const char *func) 
{
	int size;
	va_list ap2;
	char s;

	*strp = NULL;
	va_copy(ap2, ap);
	size = vsnprintf(&s, 1, fmt, ap2);
	va_end(ap2);
	if (!(*strp = __ast_alloc_region(size + 1, FUNC_VASPRINTF, file, lineno, func, 0))) {
		va_end(ap);
		return -1;
	}
	vsnprintf(*strp, size + 1, fmt, ap);

	return size;
}

static char *handle_memory_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *fn = NULL;
	struct ast_region *reg;
	unsigned int x;
	unsigned int len = 0;
	unsigned int cache_len = 0;
	unsigned int count = 0;
	unsigned int *fence;

	switch (cmd) {
	case CLI_INIT:
		e->command = "memory show allocations";
		e->usage =
			"Usage: memory show allocations [<file>]\n"
			"       Dumps a list of all segments of allocated memory, optionally\n"
			"       limited to those from a specific file\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}


	if (a->argc > 3)
		fn = a->argv[3];

	ast_mutex_lock(&reglock);
	for (x = 0; x < SOME_PRIME; x++) {
		for (reg = regions[x]; reg; reg = reg->next) {
			if (!fn || !strcasecmp(fn, reg->file) || !strcasecmp(fn, "anomolies")) {
				fence = (unsigned int *)(reg->data + reg->len);
				if (reg->fence != FENCE_MAGIC) {
					astmm_log("WARNING: Low fence violation at %p, "
						"in %s of %s, line %d\n", reg->data, 
						reg->func, reg->file, reg->lineno);
				}
				if (get_unaligned_uint32(fence) != FENCE_MAGIC) {
					astmm_log("WARNING: High fence violation at %p, in %s of %s, "
						"line %d\n", reg->data, reg->func, reg->file, reg->lineno);
				}
			}
			if (!fn || !strcasecmp(fn, reg->file)) {
				ast_cli(a->fd, "%10d bytes allocated%s in %20s at line %5d of %s\n", 
					(int) reg->len, reg->cache ? " (cache)" : "", 
					reg->func, reg->lineno, reg->file);
				len += reg->len;
				if (reg->cache)
					cache_len += reg->len;
				count++;
			}
		}
	}
	ast_mutex_unlock(&reglock);
	
	if (cache_len)
		ast_cli(a->fd, "%d bytes allocated (%d in caches) in %d allocations\n", len, cache_len, count);
	else
		ast_cli(a->fd, "%d bytes allocated in %d allocations\n", len, count);
	
	return CLI_SUCCESS;
}

static char *handle_memory_show_summary(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *fn = NULL;
	int x;
	struct ast_region *reg;
	unsigned int len = 0;
	unsigned int cache_len = 0;
	int count = 0;
	struct file_summary {
		char fn[80];
		int len;
		int cache_len;
		int count;
		struct file_summary *next;
	} *list = NULL, *cur;
	
	switch (cmd) {
	case CLI_INIT:
		e->command = "memory show summary";
		e->usage =
			"Usage: memory show summary [<file>]\n"
			"       Summarizes heap memory allocations by file, or optionally\n"
			"by function, if a file is specified\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3) 
		fn = a->argv[3];

	ast_mutex_lock(&reglock);
	for (x = 0; x < SOME_PRIME; x++) {
		for (reg = regions[x]; reg; reg = reg->next) {
			if (fn && strcasecmp(fn, reg->file))
				continue;

			for (cur = list; cur; cur = cur->next) {
				if ((!fn && !strcmp(cur->fn, reg->file)) || (fn && !strcmp(cur->fn, reg->func)))
					break;
			}
			if (!cur) {
				cur = alloca(sizeof(*cur));
				memset(cur, 0, sizeof(*cur));
				ast_copy_string(cur->fn, fn ? reg->func : reg->file, sizeof(cur->fn));
				cur->next = list;
				list = cur;
			}

			cur->len += reg->len;
			if (reg->cache)
				cur->cache_len += reg->len;
			cur->count++;
		}
	}
	ast_mutex_unlock(&reglock);
	
	/* Dump the whole list */
	for (cur = list; cur; cur = cur->next) {
		len += cur->len;
		cache_len += cur->cache_len;
		count += cur->count;
		if (cur->cache_len) {
			if (fn) {
				ast_cli(a->fd, "%10d bytes (%10d cache) in %d allocations in function '%s' of '%s'\n", 
					cur->len, cur->cache_len, cur->count, cur->fn, fn);
			} else {
				ast_cli(a->fd, "%10d bytes (%10d cache) in %d allocations in file '%s'\n", 
					cur->len, cur->cache_len, cur->count, cur->fn);
			}
		} else {
			if (fn) {
				ast_cli(a->fd, "%10d bytes in %d allocations in function '%s' of '%s'\n", 
					cur->len, cur->count, cur->fn, fn);
			} else {
				ast_cli(a->fd, "%10d bytes in %d allocations in file '%s'\n", 
					cur->len, cur->count, cur->fn);
			}
		}
	}

	if (cache_len)
		ast_cli(a->fd, "%d bytes allocated (%d in caches) in %d allocations\n", len, cache_len, count);
	else
		ast_cli(a->fd, "%d bytes allocated in %d allocations\n", len, count);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_memory[] = {
	AST_CLI_DEFINE(handle_memory_show, "Display outstanding memory allocations"),
	AST_CLI_DEFINE(handle_memory_show_summary, "Summarize outstanding memory allocations"),
};

void __ast_mm_init(void)
{
	char filename[PATH_MAX];
	size_t pad = sizeof(struct ast_region) - offsetof(struct ast_region, data);

	if (pad) {
		ast_log(LOG_ERROR, "struct ast_region has %d bytes of padding! This must be eliminated for low-fence checking to work properly!\n", (int) pad);
	}

	ast_cli_register_multiple(cli_memory, ARRAY_LEN(cli_memory));
	
	snprintf(filename, sizeof(filename), "%s/mmlog", ast_config_AST_LOG_DIR);
	
	ast_verb(1, "Asterisk Malloc Debugger Started (see %s))\n", filename);
	
	if ((mmlog = fopen(filename, "a+"))) {
		fprintf(mmlog, "%ld - New session\n", (long)time(NULL));
		fflush(mmlog);
	}
}

#endif
