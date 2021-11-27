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

#define ASTMM_LIBC ASTMM_IGNORE
#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/logger.h"

/*!
 * \brief DEBUG_CHAOS returns failure randomly
 *
 * DEBUG_CHAOS_RETURN(failure); can be used to fake
 * failure of functions such as memory allocation,
 * for the purposes of testing failure handling.
 */
#ifdef DEBUG_CHAOS
#ifndef DEBUG_CHAOS_ALLOC_CHANCE
#define DEBUG_CHAOS_ALLOC_CHANCE 100000
#endif
/* Could #define DEBUG_CHAOS_ENABLE ast_fully_booted */
#ifndef DEBUG_CHAOS_ENABLE
#define DEBUG_CHAOS_ENABLE 1
#endif
#define DEBUG_CHAOS_RETURN(CHANCE, FAILURE) \
	do { \
		if ((DEBUG_CHAOS_ENABLE) && (ast_random() % CHANCE == 0)) { \
			return FAILURE; \
		} \
	} while (0)
#else
#define DEBUG_CHAOS_RETURN(c,f)
#endif

#if defined(STANDALONE) || defined(STANDALONE2)
#define ast_log_safe ast_log
#endif

#if defined(MALLOC_DEBUG) && !defined(STANDALONE) && !defined(STANDALONE2)
#define __AST_DEBUG_MALLOC
#endif

#define MALLOC_FAILURE_MSG \
	ast_log_safe(LOG_ERROR, "Memory Allocation Failure in function %s at line %d of %s\n", func, lineno, file)

#if defined(__AST_DEBUG_MALLOC)

#include "asterisk/paths.h"	/* use ast_config_AST_LOG_DIR */
#include <stddef.h>
#include <time.h>

#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/strings.h"
#include "asterisk/unaligned.h"
#include "asterisk/backtrace.h"

/*!
 * The larger the number the faster memory can be freed.
 * However, more memory then is used for the regions[] hash
 * table.
 */
#define SOME_PRIME 1567

enum func_type {
	FUNC_CALLOC = 1,
	FUNC_MALLOC,
	FUNC_REALLOC,
	FUNC_STRDUP,
	FUNC_STRNDUP,
	FUNC_VASPRINTF,
	FUNC_ASPRINTF
};

#define FENCE_MAGIC		0xfeedbabe	/*!< Allocated memory high/low fence overwrite check. */
#define FREED_MAGIC		0xdeaddead	/*!< Freed memory wipe filler. */
#define MALLOC_FILLER	0x55		/*!< Malloced memory filler.  Must not be zero. */

static FILE *mmlog;

struct ast_region {
	AST_LIST_ENTRY(ast_region) node;
	struct ast_bt *bt;
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
	 * \note Padding between fence and data[] is irrelevant because
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

enum summary_opts {
	/*! No summary at exit. */
	SUMMARY_OFF,
	/*! Bit set if summary by line at exit. */
	SUMMARY_BY_LINE = (1 << 0),
	/*! Bit set if summary by function at exit. */
	SUMMARY_BY_FUNC = (1 << 1),
	/*! Bit set if summary by file at exit. */
	SUMMARY_BY_FILE = (1 << 2),
};

/*! Summary options of unfreed regions at exit. */
static enum summary_opts atexit_summary;
/*! Nonzero if the unfreed regions are listed at exit. */
static int atexit_list;
/*! Nonzero if the memory allocation backtrace is enabled. */
static int backtrace_enabled;

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

static void print_backtrace(struct ast_bt *bt, struct ast_cli_args *a)
{
	int i = 0;
	struct ast_vector_string *strings;

	if (!bt) {
		return;
	}

	if ((strings = ast_bt_get_symbols(bt->addresses, bt->num_frames))) {
		if (a) {
			ast_cli(a->fd, "Memory allocation backtrace:\n");
		} else {
			astmm_log("Memory allocation backtrace:\n");
		}
		for (i = 3; i < AST_VECTOR_SIZE(strings) - 2; i++) {
			if (a) {
				ast_cli(a->fd, "#%d: %s\n", i - 3, AST_VECTOR_GET(strings, i));
			} else {
				astmm_log("#%d: %s\n", i - 3, AST_VECTOR_GET(strings, i));
			}
		}
		ast_bt_free_symbols(strings);
	}
}

/*!
 * \internal
 *
 * \note If DO_CRASH is not defined then the function returns.
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

	DEBUG_CHAOS_RETURN(DEBUG_CHAOS_ALLOC_CHANCE, NULL);

	if (!(reg = malloc(size + sizeof(*reg) + sizeof(*fence)))) {
		astmm_log("Memory Allocation Failure - '%d' bytes at %s %s() line %d\n",
			(int) size, file, func, lineno);
		return NULL;
	}

	reg->len = size;
	reg->cache = cache;
	reg->lineno = lineno;
	reg->which = which;
	reg->bt = backtrace_enabled ? ast_bt_create() : NULL;
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
	AST_LIST_NEXT(reg, node) = regions[hash];
	regions[hash] = reg;
	ast_mutex_unlock(&reglock);

	return reg->data;
}

/*!
 * \internal
 * \brief Wipe the region payload data with a known value.
 *
 * \param reg Region block to be wiped.
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
			print_backtrace(reg->bt, NULL);
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
		old->bt = ast_bt_destroy(old->bt);
		free(old);
	}
}

/*!
 * \internal
 * \brief Remove a region from the active regions.
 *
 * \param ptr Region payload data pointer.
 *
 * \return region on success.
 * \retval NULL if not found.
 */
static struct ast_region *region_remove(void *ptr)
{
	int hash;
	struct ast_region *reg;
	struct ast_region *prev = NULL;

	hash = HASH(ptr);

	ast_mutex_lock(&reglock);
	for (reg = regions[hash]; reg; reg = AST_LIST_NEXT(reg, node)) {
		if (reg->data == ptr) {
			if (prev) {
				AST_LIST_NEXT(prev, node) = AST_LIST_NEXT(reg, node);
			} else {
				regions[hash] = AST_LIST_NEXT(reg, node);
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
		print_backtrace(reg->bt, NULL);
		my_do_crash();
	}
	fence = (unsigned int *) (reg->data + reg->len);
	if (get_unaligned_uint32(fence) != FENCE_MAGIC) {
		astmm_log("WARNING: High fence violation of %p allocated at %s %s() line %d\n",
			reg->data, reg->file, reg->func, reg->lineno);
		print_backtrace(reg->bt, NULL);
		my_do_crash();
	}
}

/*!
 * \internal
 * \brief Check the fences of all regions currently allocated.
 */
static void regions_check_all_fences(void)
{
	int idx;
	struct ast_region *reg;

	ast_mutex_lock(&reglock);
	for (idx = 0; idx < ARRAY_LEN(regions); ++idx) {
		for (reg = regions[idx]; reg; reg = AST_LIST_NEXT(reg, node)) {
			region_check_fences(reg);
		}
	}
	ast_mutex_unlock(&reglock);
}

void __ast_free(void *ptr, const char *file, int lineno, const char *func)
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

void *__ast_repl_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func)
{
	void *ptr;

	ptr = __ast_alloc_region(size * nmemb, FUNC_CALLOC, file, lineno, func, 0);
	if (ptr) {
		memset(ptr, 0, size * nmemb);
	}

	return ptr;
}

static void *__ast_repl_calloc_cache(size_t nmemb, size_t size, const char *file, int lineno, const char *func)
{
	void *ptr;

	ptr = __ast_alloc_region(size * nmemb, FUNC_CALLOC, file, lineno, func, 1);
	if (ptr) {
		memset(ptr, 0, size * nmemb);
	}

	return ptr;
}

void *__ast_repl_malloc(size_t size, const char *file, int lineno, const char *func)
{
	void *ptr;

	ptr = __ast_alloc_region(size, FUNC_MALLOC, file, lineno, func, 0);
	if (ptr) {
		/* Make sure that the malloced memory is not zero. */
		memset(ptr, MALLOC_FILLER, size);
	}

	return ptr;
}

/*!
 * \note reglock must be locked before calling.
 */
static struct ast_region *region_find(void *ptr)
{
	int hash;
	struct ast_region *reg;

	hash = HASH(ptr);
	for (reg = regions[hash]; reg; reg = AST_LIST_NEXT(reg, node)) {
		if (reg->data == ptr) {
			break;
		}
	}

	return reg;
}

void *__ast_repl_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func)
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
		__ast_free(ptr, file, lineno, func);
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
			__ast_free(ptr, file, lineno, func);
		} else {
			/* Make sure that the malloced memory is not zero. */
			memset(new_mem, MALLOC_FILLER, size);
		}
	}

	return new_mem;
}

char *__ast_repl_strdup(const char *s, const char *file, int lineno, const char *func)
{
	size_t len;
	void *ptr;

	len = strlen(s) + 1;
	if ((ptr = __ast_alloc_region(len, FUNC_STRDUP, file, lineno, func, 0))) {
		strcpy(ptr, s);
	}

	return ptr;
}

char *__ast_repl_strndup(const char *s, size_t n, const char *file, int lineno, const char *func)
{
	size_t len;
	char *ptr;

	len = strnlen(s, n);
	if ((ptr = __ast_alloc_region(len + 1, FUNC_STRNDUP, file, lineno, func, 0))) {
		memcpy(ptr, s, len);
		ptr[len] = '\0';
	}

	return ptr;
}

int __ast_repl_asprintf(const char *file, int lineno, const char *func, char **strp, const char *fmt, ...)
{
	int size;
	va_list ap, ap2;
	char s;
	void *ptr;

	va_start(ap, fmt);
	va_copy(ap2, ap);
	size = vsnprintf(&s, 1, fmt, ap2);
	va_end(ap2);
	ptr = __ast_alloc_region(size + 1, FUNC_ASPRINTF, file, lineno, func, 0);
	if (!ptr) {
		/* As with stdlib *strp is undefined if allocation fails. */
		va_end(ap);
		return -1;
	}
	vsnprintf(ptr, size + 1, fmt, ap);
	va_end(ap);
	*strp = ptr;

	return size;
}

int __ast_repl_vasprintf(char **strp, const char *fmt, va_list ap, const char *file, int lineno, const char *func)
{
	int size;
	va_list ap2;
	char s;
	void *ptr;

	va_copy(ap2, ap);
	size = vsnprintf(&s, 1, fmt, ap2);
	va_end(ap2);
	ptr = __ast_alloc_region(size + 1, FUNC_VASPRINTF, file, lineno, func, 0);
	if (!ptr) {
		/* As with stdlib *strp is undefined if allocation fails. */
		return -1;
	}
	vsnprintf(ptr, size + 1, fmt, ap);
	*strp = ptr;

	return size;
}

/*!
 * \internal
 * \brief Count the number of bytes in the specified freed region.
 *
 * \param freed Already freed region blocks storage.
 *
 * \note reglock must be locked before calling.
 *
 * \return Number of bytes in freed region.
 */
static size_t freed_regions_size(struct ast_freed_regions *freed)
{
	size_t total_len = 0;
	int idx;
	struct ast_region *old;

	for (idx = 0; idx < ARRAY_LEN(freed->regions); ++idx) {
		old = freed->regions[idx];
		if (old) {
			total_len += old->len;
		}
	}

	return total_len;
}

static char *handle_memory_atexit_list(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "memory atexit list {on|off}";
		e->usage =
			"Usage: memory atexit list {on|off}\n"
			"       Enable dumping a list of still allocated memory segments at exit.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (ast_true(a->argv[3])) {
		atexit_list = 1;
	} else if (ast_false(a->argv[3])) {
		atexit_list = 0;
	} else {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "The atexit list is: %s\n", atexit_list ? "On" : "Off");

	return CLI_SUCCESS;
}

static char *handle_memory_atexit_summary(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char buf[80];

	switch (cmd) {
	case CLI_INIT:
		e->command = "memory atexit summary {off|byline|byfunc|byfile}";
		e->usage =
			"Usage: memory atexit summary {off|byline|byfunc|byfile}\n"
			"       Summary of still allocated memory segments at exit options.\n"
			"       off - Disable at exit summary.\n"
			"       byline - Enable at exit summary by file line number.\n"
			"       byfunc - Enable at exit summary by function name.\n"
			"       byfile - Enable at exit summary by file.\n"
			"\n"
			"       Note: byline, byfunc, and byfile are cumulative enables.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (ast_false(a->argv[3])) {
		atexit_summary = SUMMARY_OFF;
	} else if (!strcasecmp(a->argv[3], "byline")) {
		atexit_summary |= SUMMARY_BY_LINE;
	} else if (!strcasecmp(a->argv[3], "byfunc")) {
		atexit_summary |= SUMMARY_BY_FUNC;
	} else if (!strcasecmp(a->argv[3], "byfile")) {
		atexit_summary |= SUMMARY_BY_FILE;
	} else {
		return CLI_SHOWUSAGE;
	}

	if (atexit_summary) {
		buf[0] = '\0';
		if (atexit_summary & SUMMARY_BY_LINE) {
			strcat(buf, "byline");
		}
		if (atexit_summary & SUMMARY_BY_FUNC) {
			if (buf[0]) {
				strcat(buf, " | ");
			}
			strcat(buf, "byfunc");
		}
		if (atexit_summary & SUMMARY_BY_FILE) {
			if (buf[0]) {
				strcat(buf, " | ");
			}
			strcat(buf, "byfile");
		}
	} else {
		strcpy(buf, "Off");
	}
	ast_cli(a->fd, "The atexit summary is: %s\n", buf);

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief Common summary output at the end of the memory show commands.
 *
 * \param fd CLI output file descriptor.
 * \param whales_len Accumulated size of free large allocations.
 * \param minnows_len Accumulated size of free small allocations.
 * \param total_len Accumulated size of all current allocations.
 * \param selected_len Accumulated size of the selected allocations.
 * \param cache_len Accumulated size of the allocations that are part of a cache.
 * \param count Number of selected allocations.
 */
static void print_memory_show_common_stats(int fd,
	unsigned int whales_len,
	unsigned int minnows_len,
	unsigned int total_len,
	unsigned int selected_len,
	unsigned int cache_len,
	unsigned int count)
{
	if (cache_len) {
		ast_cli(fd, "%10u bytes allocated (%u in caches) in %u selected allocations\n\n",
			selected_len, cache_len, count);
	} else {
		ast_cli(fd, "%10u bytes allocated in %u selected allocations\n\n",
			selected_len, count);
	}

	ast_cli(fd, "%10u bytes in all allocations\n", total_len);
	ast_cli(fd, "%10u bytes in deferred free large allocations\n", whales_len);
	ast_cli(fd, "%10u bytes in deferred free small allocations\n", minnows_len);
	ast_cli(fd, "%10u bytes in deferred free allocations\n",
		whales_len + minnows_len);
	ast_cli(fd, "%10u bytes in all allocations and deferred free allocations\n",
		total_len + whales_len + minnows_len);
}

static char *handle_memory_show_allocations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *fn = NULL;
	struct ast_region *reg;
	unsigned int idx;
	unsigned int whales_len;
	unsigned int minnows_len;
	unsigned int total_len = 0;
	unsigned int selected_len = 0;
	unsigned int cache_len = 0;
	unsigned int count = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "memory show allocations";
		e->usage =
			"Usage: memory show allocations [<file>|anomalies]\n"
			"       Dumps a list of segments of allocated memory.\n"
			"       Defaults to listing all memory allocations.\n"
			"       <file> - Restricts output to memory allocated by the file.\n"
			"       anomalies - Only check for fence violations.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 4) {
		fn = a->argv[3];
	} else if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	/* Look for historical misspelled option as well. */
	if (fn && (!strcasecmp(fn, "anomalies") || !strcasecmp(fn, "anomolies"))) {
		regions_check_all_fences();
		ast_cli(a->fd, "Anomaly check complete.\n");
		return CLI_SUCCESS;
	}

	ast_mutex_lock(&reglock);
	for (idx = 0; idx < ARRAY_LEN(regions); ++idx) {
		for (reg = regions[idx]; reg; reg = AST_LIST_NEXT(reg, node)) {
			total_len += reg->len;
			if (fn && strcasecmp(fn, reg->file)) {
				continue;
			}

			region_check_fences(reg);

			ast_cli(a->fd, "%10u bytes allocated%s by %20s() line %5u of %s\n",
				(unsigned int) reg->len, reg->cache ? " (cache)" : "",
				reg->func, reg->lineno, reg->file);
			if (reg->bt && !ast_strlen_zero(fn)) {
				print_backtrace(reg->bt, a);
			}

			selected_len += reg->len;
			if (reg->cache) {
				cache_len += reg->len;
			}
			++count;
		}
	}

	whales_len = freed_regions_size(&whales);
	minnows_len = freed_regions_size(&minnows);
	ast_mutex_unlock(&reglock);

	print_memory_show_common_stats(a->fd,
		whales_len, minnows_len, total_len,
		selected_len, cache_len, count);

	return CLI_SUCCESS;
}

static char *handle_memory_show_summary(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define my_max(a, b) ((a) >= (b) ? (a) : (b))

	const char *fn = NULL;
	int idx;
	int cmp;
	struct ast_region *reg;
	unsigned int whales_len;
	unsigned int minnows_len;
	unsigned int total_len = 0;
	unsigned int selected_len = 0;
	unsigned int cache_len = 0;
	unsigned int count = 0;
	struct file_summary {
		struct file_summary *next;
		unsigned int len;
		unsigned int cache_len;
		unsigned int count;
		unsigned int lineno;
		char name[my_max(sizeof(reg->file), sizeof(reg->func))];
	} *list = NULL, *cur, **prev;

	switch (cmd) {
	case CLI_INIT:
		e->command = "memory show summary";
		e->usage =
			"Usage: memory show summary [<file>]\n"
			"       Summarizes heap memory allocations by file, or optionally\n"
			"       by line if a file is specified.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 4) {
		fn = a->argv[3];
	} else if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ast_mutex_lock(&reglock);
	for (idx = 0; idx < ARRAY_LEN(regions); ++idx) {
		for (reg = regions[idx]; reg; reg = AST_LIST_NEXT(reg, node)) {
			total_len += reg->len;
			if (fn) {
				if (strcasecmp(fn, reg->file)) {
					continue;
				}

				/* Sort list by func/lineno.  Find existing or place to insert. */
				for (prev = &list; (cur = *prev); prev = &cur->next) {
					cmp = strcmp(cur->name, reg->func);
					if (cmp < 0) {
						continue;
					}
					if (cmp > 0) {
						/* Insert before current */
						cur = NULL;
						break;
					}
					cmp = cur->lineno - reg->lineno;
					if (cmp < 0) {
						continue;
					}
					if (cmp > 0) {
						/* Insert before current */
						cur = NULL;
					}
					break;
				}
			} else {
				/* Sort list by filename.  Find existing or place to insert. */
				for (prev = &list; (cur = *prev); prev = &cur->next) {
					cmp = strcmp(cur->name, reg->file);
					if (cmp < 0) {
						continue;
					}
					if (cmp > 0) {
						/* Insert before current */
						cur = NULL;
					}
					break;
				}
			}

			if (!cur) {
				cur = ast_alloca(sizeof(*cur));
				memset(cur, 0, sizeof(*cur));
				cur->lineno = reg->lineno;
				ast_copy_string(cur->name, fn ? reg->func : reg->file, sizeof(cur->name));

				cur->next = *prev;
				*prev = cur;
			}

			cur->len += reg->len;
			if (reg->cache) {
				cur->cache_len += reg->len;
			}
			++cur->count;
		}
	}

	whales_len = freed_regions_size(&whales);
	minnows_len = freed_regions_size(&minnows);
	ast_mutex_unlock(&reglock);

	/* Dump the whole list */
	for (cur = list; cur; cur = cur->next) {
		selected_len += cur->len;
		cache_len += cur->cache_len;
		count += cur->count;
		if (cur->cache_len) {
			if (fn) {
				ast_cli(a->fd, "%10u bytes (%10u cache) in %10u allocations by %20s() line %5u of %s\n",
					cur->len, cur->cache_len, cur->count, cur->name, cur->lineno, fn);
			} else {
				ast_cli(a->fd, "%10u bytes (%10u cache) in %10u allocations in file %s\n",
					cur->len, cur->cache_len, cur->count, cur->name);
			}
		} else {
			if (fn) {
				ast_cli(a->fd, "%10u bytes in %10u allocations by %20s() line %5u of %s\n",
					cur->len, cur->count, cur->name, cur->lineno, fn);
			} else {
				ast_cli(a->fd, "%10u bytes in %10u allocations in file %s\n",
					cur->len, cur->count, cur->name);
			}
		}
	}

	print_memory_show_common_stats(a->fd,
		whales_len, minnows_len, total_len,
		selected_len, cache_len, count);

	return CLI_SUCCESS;
}

static char *handle_memory_backtrace(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "memory backtrace {on|off}";
		e->usage =
			"Usage: memory backtrace {on|off}\n"
			"       Enable dumping an allocation backtrace with memory diagnostics.\n"
			"       Note that saving the backtrace data for each allocation\n"
			"       can be CPU intensive.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	if (ast_true(a->argv[2])) {
		backtrace_enabled = 1;
	} else if (ast_false(a->argv[2])) {
		backtrace_enabled = 0;
	} else {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "The memory backtrace is: %s\n", backtrace_enabled ? "On" : "Off");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_memory[] = {
	AST_CLI_DEFINE(handle_memory_atexit_list, "Enable memory allocations not freed at exit list."),
	AST_CLI_DEFINE(handle_memory_atexit_summary, "Enable memory allocations not freed at exit summary."),
	AST_CLI_DEFINE(handle_memory_show_allocations, "Display outstanding memory allocations"),
	AST_CLI_DEFINE(handle_memory_show_summary, "Summarize outstanding memory allocations"),
	AST_CLI_DEFINE(handle_memory_backtrace, "Enable dumping an allocation backtrace with memory diagnostics."),
};

AST_LIST_HEAD_NOLOCK(region_list, ast_region);

/*!
 * \internal
 * \brief Convert the allocated regions hash table to a list.
 *
 * \param list Fill list with the allocated regions.
 *
 * \details
 * Take all allocated regions from the regions[] and put them
 * into the list.
 *
 * \note reglock must be locked before calling.
 *
 * \note This function is destructive to the regions[] lists.
 *
 * \return Length of list created.
 */
static size_t mm_atexit_hash_list(struct region_list *list)
{
	struct ast_region *reg;
	size_t total_length;
	int idx;

	total_length = 0;
	for (idx = 0; idx < ARRAY_LEN(regions); ++idx) {
		while ((reg = regions[idx])) {
			regions[idx] = AST_LIST_NEXT(reg, node);
			AST_LIST_NEXT(reg, node) = NULL;
			AST_LIST_INSERT_HEAD(list, reg, node);
			++total_length;
		}
	}
	return total_length;
}

/*!
 * \internal
 * \brief Put the regions list into the allocated regions hash table.
 *
 * \param list List to put into the allocated regions hash table.
 *
 * \note reglock must be locked before calling.
 */
static void mm_atexit_hash_restore(struct region_list *list)
{
	struct ast_region *reg;
	int hash;

	while ((reg = AST_LIST_REMOVE_HEAD(list, node))) {
		hash = HASH(reg->data);
		AST_LIST_NEXT(reg, node) = regions[hash];
		regions[hash] = reg;
	}
}

/*!
 * \internal
 * \brief Sort regions comparision.
 *
 * \param left Region to compare.
 * \param right Region to compare.
 *
 * \retval <0 if left < right
 * \retval =0 if left == right
 * \retval >0 if left > right
 */
static int mm_atexit_cmp(struct ast_region *left, struct ast_region *right)
{
	int cmp;
	ptrdiff_t cmp_ptr;
	ssize_t cmp_size;

	/* Sort by filename. */
	cmp = strcmp(left->file, right->file);
	if (cmp) {
		return cmp;
	}

	/* Sort by line number. */
	cmp = left->lineno - right->lineno;
	if (cmp) {
		return cmp;
	}

	/* Sort by allocated size. */
	cmp_size = left->len - right->len;
	if (cmp_size) {
		if (cmp_size < 0) {
			return -1;
		}
		return 1;
	}

	/* Sort by allocated pointers just because. */
	cmp_ptr = left->data - right->data;
	if (cmp_ptr) {
		if (cmp_ptr < 0) {
			return -1;
		}
		return 1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Merge the given sorted sublists into sorted order onto the end of the list.
 *
 * \param list Merge sublists onto this list.
 * \param sub1 First sublist to merge.
 * \param sub2 Second sublist to merge.
 */
static void mm_atexit_list_merge(struct region_list *list, struct region_list *sub1, struct region_list *sub2)
{
	struct ast_region *reg;

	for (;;) {
		if (AST_LIST_EMPTY(sub1)) {
			/* The remaining sublist goes onto the list. */
			AST_LIST_APPEND_LIST(list, sub2, node);
			break;
		}
		if (AST_LIST_EMPTY(sub2)) {
			/* The remaining sublist goes onto the list. */
			AST_LIST_APPEND_LIST(list, sub1, node);
			break;
		}

		if (mm_atexit_cmp(AST_LIST_FIRST(sub1), AST_LIST_FIRST(sub2)) <= 0) {
			reg = AST_LIST_REMOVE_HEAD(sub1, node);
		} else {
			reg = AST_LIST_REMOVE_HEAD(sub2, node);
		}
		AST_LIST_INSERT_TAIL(list, reg, node);
	}
}

/*!
 * \internal
 * \brief Take sublists off of the given list.
 *
 * \param list Source list to remove sublists from the beginning of list.
 * \param sub Array of sublists to fill. (Lists are empty on entry.)
 * \param num_lists Number of lists to remove from the source list.
 * \param size Size of the sublists to remove.
 * \param remaining Remaining number of elements on the source list.
 */
static void mm_atexit_list_split(struct region_list *list, struct region_list sub[], size_t num_lists, size_t size, size_t *remaining)
{
	int idx;

	for (idx = 0; idx < num_lists; ++idx) {
		size_t count;

		if (*remaining < size) {
			/* The remaining source list goes onto the sublist. */
			AST_LIST_APPEND_LIST(&sub[idx], list, node);
			*remaining = 0;
			break;
		}

		/* Take a sublist off the beginning of the source list. */
		*remaining -= size;
		for (count = size; count--;) {
			struct ast_region *reg;

			reg = AST_LIST_REMOVE_HEAD(list, node);
			AST_LIST_INSERT_TAIL(&sub[idx], reg, node);
		}
	}
}

/*!
 * \internal
 * \brief Sort the regions list using mergesort.
 *
 * \param list Allocated regions list to sort.
 * \param length Length of the list.
 */
static void mm_atexit_list_sort(struct region_list *list, size_t length)
{
	/*! Semi-sorted merged list. */
	struct region_list merged = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	/*! Sublists to merge. (Can only merge two sublists at this time.) */
	struct region_list sub[2] = {
		AST_LIST_HEAD_NOLOCK_INIT_VALUE,
		AST_LIST_HEAD_NOLOCK_INIT_VALUE
	};
	/*! Sublist size. */
	size_t size = 1;
	/*! Remaining elements in the list. */
	size_t remaining;
	/*! Number of sublist merge passes to process the list. */
	int passes;

	for (;;) {
		remaining = length;

		passes = 0;
		while (!AST_LIST_EMPTY(list)) {
			mm_atexit_list_split(list, sub, ARRAY_LEN(sub), size, &remaining);
			mm_atexit_list_merge(&merged, &sub[0], &sub[1]);
			++passes;
		}
		AST_LIST_APPEND_LIST(list, &merged, node);
		if (passes <= 1) {
			/* The list is now sorted. */
			break;
		}

		/* Double the sublist size to remove for next round. */
		size <<= 1;
	}
}

/*!
 * \internal
 * \brief List all regions currently allocated.
 *
 * \param alloced regions list.
 */
static void mm_atexit_regions_list(struct region_list *alloced)
{
	struct ast_region *reg;

	AST_LIST_TRAVERSE(alloced, reg, node) {
		astmm_log("%s %s() line %u: %u bytes%s at %p\n",
			reg->file, reg->func, reg->lineno,
			(unsigned int) reg->len, reg->cache ? " (cache)" : "", reg->data);
	}
}

/*!
 * \internal
 * \brief Summarize all regions currently allocated.
 *
 * \param alloced Sorted regions list.
 */
static void mm_atexit_regions_summary(struct region_list *alloced)
{
	struct ast_region *reg;
	struct ast_region *next;
	struct {
		unsigned int count;
		unsigned int len;
		unsigned int cache_len;
	} by_line, by_func, by_file, total;

	by_line.count = 0;
	by_line.len = 0;
	by_line.cache_len = 0;

	by_func.count = 0;
	by_func.len = 0;
	by_func.cache_len = 0;

	by_file.count = 0;
	by_file.len = 0;
	by_file.cache_len = 0;

	total.count = 0;
	total.len = 0;
	total.cache_len = 0;

	AST_LIST_TRAVERSE(alloced, reg, node) {
		next = AST_LIST_NEXT(reg, node);

		++by_line.count;
		by_line.len += reg->len;
		if (reg->cache) {
			by_line.cache_len += reg->len;
		}
		if (next && !strcmp(reg->file, next->file) && reg->lineno == next->lineno) {
			continue;
		}
		if (atexit_summary & SUMMARY_BY_LINE) {
			if (by_line.cache_len) {
				astmm_log("%10u bytes (%u in caches) in %u allocations. %s %s() line %u\n",
					by_line.len, by_line.cache_len, by_line.count, reg->file, reg->func, reg->lineno);
			} else {
				astmm_log("%10u bytes in %5u allocations. %s %s() line %u\n",
					by_line.len, by_line.count, reg->file, reg->func, reg->lineno);
			}
		}

		by_func.count += by_line.count;
		by_func.len += by_line.len;
		by_func.cache_len += by_line.cache_len;
		by_line.count = 0;
		by_line.len = 0;
		by_line.cache_len = 0;
		if (next && !strcmp(reg->file, next->file) && !strcmp(reg->func, next->func)) {
			continue;
		}
		if (atexit_summary & SUMMARY_BY_FUNC) {
			if (by_func.cache_len) {
				astmm_log("%10u bytes (%u in caches) in %u allocations. %s %s()\n",
					by_func.len, by_func.cache_len, by_func.count, reg->file, reg->func);
			} else {
				astmm_log("%10u bytes in %5u allocations. %s %s()\n",
					by_func.len, by_func.count, reg->file, reg->func);
			}
		}

		by_file.count += by_func.count;
		by_file.len += by_func.len;
		by_file.cache_len += by_func.cache_len;
		by_func.count = 0;
		by_func.len = 0;
		by_func.cache_len = 0;
		if (next && !strcmp(reg->file, next->file)) {
			continue;
		}
		if (atexit_summary & SUMMARY_BY_FILE) {
			if (by_file.cache_len) {
				astmm_log("%10u bytes (%u in caches) in %u allocations. %s\n",
					by_file.len, by_file.cache_len, by_file.count, reg->file);
			} else {
				astmm_log("%10u bytes in %5u allocations. %s\n",
					by_file.len, by_file.count, reg->file);
			}
		}

		total.count += by_file.count;
		total.len += by_file.len;
		total.cache_len += by_file.cache_len;
		by_file.count = 0;
		by_file.len = 0;
		by_file.cache_len = 0;
	}

	if (total.cache_len) {
		astmm_log("%u bytes (%u in caches) in %u allocations.\n",
			total.len, total.cache_len, total.count);
	} else {
		astmm_log("%u bytes in %u allocations.\n", total.len, total.count);
	}
}

/*!
 * \internal
 * \brief Dump the memory allocations atexit.
 *
 * \note reglock must be locked before calling.
 */
static void mm_atexit_dump(void)
{
	struct region_list alloced_atexit = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
	size_t length;

	length = mm_atexit_hash_list(&alloced_atexit);
	if (!length) {
		/* Wow!  This is amazing! */
		astmm_log("Exiting with all memory freed.\n");
		return;
	}

	mm_atexit_list_sort(&alloced_atexit, length);

	astmm_log("Exiting with the following memory not freed:\n");
	if (atexit_list) {
		mm_atexit_regions_list(&alloced_atexit);
	}
	if (atexit_summary) {
		mm_atexit_regions_summary(&alloced_atexit);
	}

	/*
	 * Put the alloced list back into regions[].
	 *
	 * We have do this because we can get called before all other
	 * threads have terminated.
	 */
	mm_atexit_hash_restore(&alloced_atexit);
}

/*!
 * \internal
 */
static void mm_atexit_final(void)
{
	FILE *log;

	/* Only wait if we want atexit allocation dumps. */
	if (atexit_list || atexit_summary) {
		fprintf(stderr, "Waiting 10 seconds to let other threads die.\n");
		sleep(10);
	}

	regions_check_all_fences();

	/* Flush all delayed memory free circular arrays. */
	freed_regions_flush(&whales);
	freed_regions_flush(&minnows);

	/* Perform atexit allocation dumps. */
	if (atexit_list || atexit_summary) {
		ast_mutex_lock(&reglock);
		mm_atexit_dump();
		ast_mutex_unlock(&reglock);
	}

	/* Close the log file. */
	log = mmlog;
	mmlog = NULL;
	if (log) {
		fclose(log);
	}
}

void load_astmm_phase_1(void)
{
	atexit(mm_atexit_final);
}

/*!
 * \internal
 */
static void mm_atexit_ast(void)
{
	ast_cli_unregister_multiple(cli_memory, ARRAY_LEN(cli_memory));
}

void load_astmm_phase_2(void)
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

	ast_register_cleanup(mm_atexit_ast);
}

#else	/* !defined(__AST_DEBUG_MALLOC) */

void load_astmm_phase_1(void)
{
}

void load_astmm_phase_2(void)
{
}

void *__ast_repl_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func)
{
	DEBUG_CHAOS_RETURN(DEBUG_CHAOS_ALLOC_CHANCE, NULL);

	return calloc(nmemb, size);
}

static void *__ast_repl_calloc_cache(size_t nmemb, size_t size, const char *file, int lineno, const char *func)
{
	DEBUG_CHAOS_RETURN(DEBUG_CHAOS_ALLOC_CHANCE, NULL);

	return calloc(nmemb, size);
}

void *__ast_repl_malloc(size_t size, const char *file, int lineno, const char *func)
{
	DEBUG_CHAOS_RETURN(DEBUG_CHAOS_ALLOC_CHANCE, NULL);

	return malloc(size);
}

void __ast_free(void *ptr, const char *file, int lineno, const char *func)
{
	free(ptr);
}

void *__ast_repl_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func)
{
	DEBUG_CHAOS_RETURN(DEBUG_CHAOS_ALLOC_CHANCE, NULL);

	return realloc(ptr, size);
}

char *__ast_repl_strdup(const char *s, const char *file, int lineno, const char *func)
{
	DEBUG_CHAOS_RETURN(DEBUG_CHAOS_ALLOC_CHANCE, NULL);

	return strdup(s);
}

char *__ast_repl_strndup(const char *s, size_t n, const char *file, int lineno, const char *func)
{
	DEBUG_CHAOS_RETURN(DEBUG_CHAOS_ALLOC_CHANCE, NULL);

	return strndup(s, n);
}

int __ast_repl_asprintf(const char *file, int lineno, const char *func, char **strp, const char *format, ...)
{
	int res;
	va_list ap;

	DEBUG_CHAOS_RETURN(DEBUG_CHAOS_ALLOC_CHANCE, -1);

	va_start(ap, format);
	res = vasprintf(strp, format, ap);
	va_end(ap);

	return res;
}

int __ast_repl_vasprintf(char **strp, const char *format, va_list ap, const char *file, int lineno, const char *func)
{
	DEBUG_CHAOS_RETURN(DEBUG_CHAOS_ALLOC_CHANCE, -1);

	return vasprintf(strp, format, ap);
}

#endif	/* defined(__AST_DEBUG_MALLOC) */

void *__ast_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func)
{
	void *p;

	p = __ast_repl_calloc(nmemb, size, file, lineno, func);
	if (!p) {
		MALLOC_FAILURE_MSG;
	}

	return p;
}

void *__ast_calloc_cache(size_t nmemb, size_t size, const char *file, int lineno, const char *func)
{
	void *p;

	p = __ast_repl_calloc_cache(nmemb, size, file, lineno, func);
	if (!p) {
		MALLOC_FAILURE_MSG;
	}

	return p;

}

void *__ast_malloc(size_t size, const char *file, int lineno, const char *func)
{
	void *p;

	p = __ast_repl_malloc(size, file, lineno, func);
	if (!p) {
		MALLOC_FAILURE_MSG;
	}

	return p;
}

void *__ast_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func)
{
	void *newp;

	newp = __ast_repl_realloc(ptr, size, file, lineno, func);
	if (!newp) {
		MALLOC_FAILURE_MSG;
	}

	return newp;
}

char *__ast_strdup(const char *s, const char *file, int lineno, const char *func)
{
	char *newstr = NULL;

	if (s) {
		newstr = __ast_repl_strdup(s, file, lineno, func);
		if (!newstr) {
			MALLOC_FAILURE_MSG;
		}
	}

	return newstr;
}

char *__ast_strndup(const char *s, size_t n, const char *file, int lineno, const char *func)
{
	char *newstr = NULL;

	if (s) {
		newstr = __ast_repl_strndup(s, n, file, lineno, func);
		if (!newstr) {
			MALLOC_FAILURE_MSG;
		}
	}

	return newstr;
}

int __ast_asprintf(const char *file, int lineno, const char *func, char **strp, const char *format, ...)
{
	int res;
	va_list ap;

	va_start(ap, format);
	res = __ast_repl_vasprintf(strp, format, ap, file, lineno, func);
	if (res < 0) {
		/*
		 * *strp is undefined so set to NULL to ensure it is
		 * initialized to something useful.
		 */
		*strp = NULL;

		MALLOC_FAILURE_MSG;
	}
	va_end(ap);

	return res;
}

int __ast_vasprintf(char **strp, const char *format, va_list ap, const char *file, int lineno, const char *func)
{
	int res;

	res = __ast_repl_vasprintf(strp, format, ap, file, lineno, func);
	if (res < 0) {
		/*
		 * *strp is undefined so set to NULL to ensure it is
		 * initialized to something useful.
		 */
		*strp = NULL;

		MALLOC_FAILURE_MSG;
	}

	return res;
}

void *ast_std_malloc(size_t size)
{
	return malloc(size);
}

void *ast_std_calloc(size_t nmemb, size_t size)
{
	return calloc(nmemb, size);
}

void *ast_std_realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

void ast_std_free(void *ptr)
{
	free(ptr);
}

void ast_free_ptr(void *ptr)
{
	ast_free(ptr);
}
