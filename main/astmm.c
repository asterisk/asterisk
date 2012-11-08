/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2012, Digium, Inc.
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
 * \author Richard Mudgett <rmudgett@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#if defined(__AST_DEBUG_MALLOC)

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

#define FENCE_MAGIC		0xdeadbeef	/*!< Allocated memory high/low fence overwrite check. */
#define FREED_MAGIC		0xdeaddead	/*!< Freed memory wipe filler. */
#define MALLOC_FILLER	0x55		/*!< Malloced memory filler.  Must not be zero. */

static FILE *mmlog;

struct ast_region {
	struct ast_region *next;
	size_t len;
	unsigned int cache;		/* region was allocated as part of a cache pool */
	unsigned int lineno;
	enum func_type which;
	char file[64];
	char func[40];

	/*!
	 * \brief Lower guard fence.
	 *
	 * \note Must be right before data[].
	 *
	 * \note Padding between fence and data[] is irrelevent because
	 * data[] is used to fill in the lower fence check value and not
	 * the fence member.  The fence member is to ensure that there
	 * is space reserved for the fence check value.
	 */
	unsigned int fence;
	/*!
	 * \brief Location of the requested malloc block to return.
	 *
	 * \note Must have the same alignment that malloc returns.
	 * i.e., It is suitably aligned for any kind of varible.
	 */
	unsigned char data[0] __attribute__((aligned));
};

/*! Hash table of lists of active allocated memory regions. */
static struct ast_region *regions[SOME_PRIME];

/*! Number of freed regions to keep around to delay actually freeing them. */
#define FREED_MAX_COUNT		1500

/*! Maximum size of a minnow block */
#define MINNOWS_MAX_SIZE	50

struct ast_freed_regions {
	/*! Memory regions that have been freed. */
	struct ast_region *regions[FREED_MAX_COUNT];
	/*! Next index into freed regions[] to use. */
	int index;
};

/*! Large memory blocks that have been freed. */
static struct ast_freed_regions whales;
/*! Small memory blocks that have been freed. */
static struct ast_freed_regions minnows;

#define HASH(a)		(((unsigned long)(a)) % ARRAY_LEN(regions))

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

/*!
 * \internal
 *
 * \note If DO_CRASH is not defined then the function returns.
 *
 * \return Nothing
 */
static void my_do_crash(void)
{
	/*
	 * Give the logger a chance to get the message out, just in case
	 * we abort(), or Asterisk crashes due to whatever problem just
	 * happened.
	 */
	usleep(1);
	ast_do_crash();
}

static void *__ast_alloc_region(size_t size, const enum func_type which, const char *file, int lineno, const char *func, unsigned int cache)
{
	struct ast_region *reg;
	unsigned int *fence;
	int hash;

	if (!(reg = malloc(size + sizeof(*reg) + sizeof(*fence)))) {
		astmm_log("Memory Allocation Failure - '%d' bytes at %s %s() line %d\n",
			(int) size, file, func, lineno);
		return NULL;
	}

	reg->len = size;
	reg->cache = cache;
	reg->lineno = lineno;
	reg->which = which;
	ast_copy_string(reg->file, file, sizeof(reg->file));
	ast_copy_string(reg->func, func, sizeof(reg->func));

	/*
	 * Init lower fence.
	 *
	 * We use the bytes just preceeding reg->data and not reg->fence
	 * because there is likely to be padding between reg->fence and
	 * reg->data for reg->data alignment.
	 */
	fence = (unsigned int *) (reg->data - sizeof(*fence));
	*fence = FENCE_MAGIC;

	/* Init higher fence. */
	fence = (unsigned int *) (reg->data + reg->len);
	put_unaligned_uint32(fence, FENCE_MAGIC);

	hash = HASH(reg->data);
	ast_mutex_lock(&reglock);
	reg->next = regions[hash];
	regions[hash] = reg;
	ast_mutex_unlock(&reglock);

	return reg->data;
}

/*!
 * \internal
 * \brief Wipe the region payload data with a known value.
 *
 * \param reg Region block to be wiped.
 *
 * \return Nothing
 */
static void region_data_wipe(struct ast_region *reg)
{
	void *end;
	unsigned int *pos;

	/*
	 * Wipe the lower fence, the payload, and whatever amount of the
	 * higher fence that falls into alignment with the payload.
	 */
	end = reg->data + reg->len;
	for (pos = &reg->fence; (void *) pos <= end; ++pos) {
		*pos = FREED_MAGIC;
	}
}

/*!
 * \internal
 * \brief Check the region payload data for memory corruption.
 *
 * \param reg Region block to be checked.
 *
 * \return Nothing
 */
static void region_data_check(struct ast_region *reg)
{
	void *end;
	unsigned int *pos;

	/*
	 * Check the lower fence, the payload, and whatever amount of
	 * the higher fence that falls into alignment with the payload.
	 */
	end = reg->data + reg->len;
	for (pos = &reg->fence; (void *) pos <= end; ++pos) {
		if (*pos != FREED_MAGIC) {
			astmm_log("WARNING: Memory corrupted after free of %p allocated at %s %s() line %d\n",
				reg->data, reg->file, reg->func, reg->lineno);
			my_do_crash();
			break;
		}
	}
}

/*!
 * \internal
 * \brief Flush the circular array of freed regions.
 *
 * \param freed Already freed region blocks storage.
 *
 * \return Nothing
 */
static void freed_regions_flush(struct ast_freed_regions *freed)
{
	int idx;
	struct ast_region *old;

	ast_mutex_lock(&reglock);
	for (idx = 0; idx < ARRAY_LEN(freed->regions); ++idx) {
		old = freed->regions[idx];
		freed->regions[idx] = NULL;
		if (old) {
			region_data_check(old);
			free(old);
		}
	}
	freed->index = 0;
	ast_mutex_unlock(&reglock);
}

/*!
 * \internal
 * \brief Delay freeing a region block.
 *
 * \param freed Already freed region blocks storage.
 * \param reg Region block to be freed.
 *
 * \return Nothing
 */
static void region_free(struct ast_freed_regions *freed, struct ast_region *reg)
{
	struct ast_region *old;

	region_data_wipe(reg);

	ast_mutex_lock(&reglock);
	old = freed->regions[freed->index];
	freed->regions[freed->index] = reg;

	++freed->index;
	if (ARRAY_LEN(freed->regions) <= freed->index) {
		freed->index = 0;
	}
	ast_mutex_unlock(&reglock);

	if (old) {
		region_data_check(old);
		free(old);
	}
}

/*!
 * \internal
 * \brief Remove a region from the active regions.
 *
 * \param ptr Region payload data pointer.
 *
 * \retval region on success.
 * \retval NULL if not found.
 */
static struct ast_region *region_remove(void *ptr)
{
	int hash;
	struct ast_region *reg;
	struct ast_region *prev = NULL;

	hash = HASH(ptr);

	ast_mutex_lock(&reglock);
	for (reg = regions[hash]; reg; reg = reg->next) {
		if (reg->data == ptr) {
			if (prev) {
				prev->next = reg->next;
			} else {
				regions[hash] = reg->next;
			}
			break;
		}
		prev = reg;
	}
	ast_mutex_unlock(&reglock);

	return reg;
}

/*!
 * \internal
 * \brief Check the fences of a region.
 *
 * \param reg Region block to check.
 *
 * \return Nothing
 */
static void region_check_fences(struct ast_region *reg)
{
	unsigned int *fence;

	/*
	 * We use the bytes just preceeding reg->data and not reg->fence
	 * because there is likely to be padding between reg->fence and
	 * reg->data for reg->data alignment.
	 */
	fence = (unsigned int *) (reg->data - sizeof(*fence));
	if (*fence != FENCE_MAGIC) {
		astmm_log("WARNING: Low fence violation of %p allocated at %s %s() line %d\n",
			reg->data, reg->file, reg->func, reg->lineno);
		my_do_crash();
	}
	fence = (unsigned int *) (reg->data + reg->len);
	if (get_unaligned_uint32(fence) != FENCE_MAGIC) {
		astmm_log("WARNING: High fence violation of %p allocated at %s %s() line %d\n",
			reg->data, reg->file, reg->func, reg->lineno);
		my_do_crash();
	}
}

static void __ast_free_region(void *ptr, const char *file, int lineno, const char *func)
{
	struct ast_region *reg;

	if (!ptr) {
		return;
	}

	reg = region_remove(ptr);
	if (reg) {
		region_check_fences(reg);

		if (reg->len <= MINNOWS_MAX_SIZE) {
			region_free(&minnows, reg);
		} else {
			region_free(&whales, reg);
		}
	} else {
		/*
		 * This memory region is not registered.  It could be because of
		 * a double free or the memory block was not allocated by the
		 * malloc debug code.
		 */
		astmm_log("WARNING: Freeing unregistered memory %p by %s %s() line %d\n",
			ptr, file, func, lineno);
		my_do_crash();
	}
}

void *__ast_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func)
{
	void *ptr;

	ptr = __ast_alloc_region(size * nmemb, FUNC_CALLOC, file, lineno, func, 0);
	if (ptr) {
		memset(ptr, 0, size * nmemb);
	}

	return ptr;
}

void *__ast_calloc_cache(size_t nmemb, size_t size, const char *file, int lineno, const char *func)
{
	void *ptr;

	ptr = __ast_alloc_region(size * nmemb, FUNC_CALLOC, file, lineno, func, 1);
	if (ptr) {
		memset(ptr, 0, size * nmemb);
	}

	return ptr;
}

void *__ast_malloc(size_t size, const char *file, int lineno, const char *func)
{
	void *ptr;

	ptr = __ast_alloc_region(size, FUNC_MALLOC, file, lineno, func, 0);
	if (ptr) {
		/* Make sure that the malloced memory is not zero. */
		memset(ptr, MALLOC_FILLER, size);
	}

	return ptr;
}

void __ast_free(void *ptr, const char *file, int lineno, const char *func)
{
	__ast_free_region(ptr, file, lineno, func);
}

/*!
 * \note reglock must be locked before calling.
 */
static struct ast_region *region_find(void *ptr)
{
	int hash;
	struct ast_region *reg;

	hash = HASH(ptr);
	for (reg = regions[hash]; reg; reg = reg->next) {
		if (reg->data == ptr) {
			break;
		}
	}

	return reg;
}

void *__ast_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func)
{
	size_t len;
	struct ast_region *found;
	void *new_mem;

	if (ptr) {
		ast_mutex_lock(&reglock);
		found = region_find(ptr);
		if (!found) {
			ast_mutex_unlock(&reglock);
			astmm_log("WARNING: Realloc of unregistered memory %p by %s %s() line %d\n",
				ptr, file, func, lineno);
			my_do_crash();
			return NULL;
		}
		len = found->len;
		ast_mutex_unlock(&reglock);
	} else {
		found = NULL;
		len = 0;
	}

	if (!size) {
		__ast_free_region(ptr, file, lineno, func);
		return NULL;
	}

	new_mem = __ast_alloc_region(size, FUNC_REALLOC, file, lineno, func, 0);
	if (new_mem) {
		if (found) {
			/* Copy the old data to the new malloced memory. */
			if (size <= len) {
				memcpy(new_mem, ptr, size);
			} else {
				memcpy(new_mem, ptr, len);
				/* Make sure that the added memory is not zero. */
				memset(new_mem + len, MALLOC_FILLER, size - len);
			}
			__ast_free_region(ptr, file, lineno, func);
		} else {
			/* Make sure that the malloced memory is not zero. */
			memset(new_mem, MALLOC_FILLER, size);
		}
	}

	return new_mem;
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
	char *ptr;

	if (!s) {
		return NULL;
	}

	len = strnlen(s, n);
	if ((ptr = __ast_alloc_region(len + 1, FUNC_STRNDUP, file, lineno, func, 0))) {
		memcpy(ptr, s, len);
		ptr[len] = '\0';
	}

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

	switch (cmd) {
	case CLI_INIT:
		e->command = "memory show allocations";
		e->usage =
			"Usage: memory show allocations [<file>|anomolies]\n"
			"       Dumps a list of segments of allocated memory.\n"
			"       Defaults to listing all memory allocations.\n"
			"       <file> - Restricts output to memory allocated by the file.\n"
			"       anomolies - Only check for fence violations.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}


	if (a->argc > 3)
		fn = a->argv[3];

	ast_mutex_lock(&reglock);
	for (x = 0; x < ARRAY_LEN(regions); x++) {
		for (reg = regions[x]; reg; reg = reg->next) {
			if (!fn || !strcasecmp(fn, reg->file) || !strcasecmp(fn, "anomolies")) {
				region_check_fences(reg);
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
			"       by function, if a file is specified.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3)
		fn = a->argv[3];

	ast_mutex_lock(&reglock);
	for (x = 0; x < ARRAY_LEN(regions); x++) {
		for (reg = regions[x]; reg; reg = reg->next) {
			if (fn && strcasecmp(fn, reg->file))
				continue;

			for (cur = list; cur; cur = cur->next) {
				if ((!fn && !strcmp(cur->fn, reg->file)) || (fn && !strcmp(cur->fn, reg->func)))
					break;
			}
			if (!cur) {
				cur = ast_alloca(sizeof(*cur));
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

/*!
 * \internal
 * \return Nothing
 */
static void mm_atexit_final(void)
{
	FILE *log;

	/* Flush all delayed memory free circular arrays. */
	freed_regions_flush(&whales);
	freed_regions_flush(&minnows);

	/* Close the log file. */
	log = mmlog;
	mmlog = NULL;
	if (log) {
		fclose(log);
	}
}

/*!
 * \brief Initialize malloc debug phase 1.
 *
 * \note Must be called first thing in main().
 *
 * \return Nothing
 */
void __ast_mm_init_phase_1(void)
{
	atexit(mm_atexit_final);
}

/*!
 * \internal
 * \return Nothing
 */
static void mm_atexit_ast(void)
{
	ast_cli_unregister_multiple(cli_memory, ARRAY_LEN(cli_memory));
}

/*!
 * \brief Initialize malloc debug phase 2.
 *
 * \return Nothing
 */
void __ast_mm_init_phase_2(void)
{
	char filename[PATH_MAX];

	ast_cli_register_multiple(cli_memory, ARRAY_LEN(cli_memory));

	snprintf(filename, sizeof(filename), "%s/mmlog", ast_config_AST_LOG_DIR);

	ast_verb(1, "Asterisk Malloc Debugger Started (see %s))\n", filename);

	mmlog = fopen(filename, "a+");
	if (mmlog) {
		fprintf(mmlog, "%ld - New session\n", (long) time(NULL));
		fflush(mmlog);
	} else {
		ast_log(LOG_ERROR, "Could not open malloc debug log file: %s\n", filename);
	}

	ast_register_atexit(mm_atexit_ast);
}

#endif	/* defined(__AST_DEBUG_MALLOC) */
