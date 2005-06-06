/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Channel Variables
 * 
 * Copyright (C) 2002-2005, Mark Spencer
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */



#ifdef __AST_DEBUG_MALLOC

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION("$Revision$")

#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/lock.h"

#define SOME_PRIME 563

#define FUNC_CALLOC		1
#define FUNC_MALLOC		2
#define FUNC_REALLOC	3
#define FUNC_STRDUP		4
#define FUNC_STRNDUP	5
#define FUNC_VASPRINTF	6

/* Undefine all our macros */
#undef malloc
#undef calloc
#undef realloc
#undef strdup
#undef strndup
#undef free
#undef vasprintf

static FILE *mmlog;

static struct ast_region {
	struct ast_region *next;
	char file[40];
	char func[40];
	int lineno;
	int which;
	size_t len;
	unsigned char data[0];
} *regions[SOME_PRIME];

#define HASH(a) \
	(((unsigned long)(a)) % SOME_PRIME)
	
AST_MUTEX_DEFINE_STATIC(reglock);
AST_MUTEX_DEFINE_STATIC(showmemorylock);

static inline void *__ast_alloc_region(size_t size, int which, const char *file, int lineno, const char *func)
{
	struct ast_region *reg;
	void *ptr=NULL;
	int hash;
	reg = malloc(size + sizeof(struct ast_region));
	ast_mutex_lock(&reglock);
	if (reg) {
		strncpy(reg->file, file, sizeof(reg->file) - 1);
		reg->file[sizeof(reg->file) - 1] = '\0';
		strncpy(reg->func, func, sizeof(reg->func) - 1);
		reg->func[sizeof(reg->func) - 1] = '\0';
		reg->lineno = lineno;
		reg->len = size;
		reg->which = which;
		ptr = reg->data;
		hash = HASH(ptr);
		reg->next = regions[hash];
		regions[hash] = reg;
	}
	ast_mutex_unlock(&reglock);
	if (!reg) {
		fprintf(stderr, "Out of memory :(\n");
		if (mmlog) {
			fprintf(mmlog, "%ld - Out of memory\n", time(NULL));
			fflush(mmlog);
		}
	}
	return ptr;
}

static inline size_t __ast_sizeof_region(void *ptr)
{
	int hash = HASH(ptr);
	struct ast_region *reg;
	size_t len = 0;
	
	ast_mutex_lock(&reglock);
	reg = regions[hash];
	while(reg) {
		if (reg->data == ptr) {
			len = reg->len;
			break;
		}
		reg = reg->next;
	}
	ast_mutex_unlock(&reglock);
	return len;
}

static void __ast_free_region(void *ptr, const char *file, int lineno, const char *func)
{
	int hash = HASH(ptr);
	struct ast_region *reg, *prev = NULL;
	ast_mutex_lock(&reglock);
	reg = regions[hash];
	while(reg) {
		if (reg->data == ptr) {
			if (prev)
				prev->next = reg->next;
			else
				regions[hash] = reg->next;

			break;
		}
		prev = reg;
		reg = reg->next;
	}
	ast_mutex_unlock(&reglock);
	if (reg) {
		free(reg);
	} else {
		fprintf(stderr, "WARNING: Freeing unused memory at %p, in %s of %s, line %d\n",
			ptr, func, file, lineno);
		if (mmlog) {
			fprintf(mmlog, "%ld - WARNING: Freeing unused memory at %p, in %s of %s, line %d\n", time(NULL),
			ptr, func, file, lineno);
			fflush(mmlog);
		}
	}
}

void *__ast_calloc(size_t nmemb, size_t size, const char *file, int lineno, const char *func) 
{
	void *ptr;
	ptr = __ast_alloc_region(size * nmemb, FUNC_CALLOC, file, lineno, func);
	if (ptr) 
		memset(ptr, 0, size * nmemb);
	return ptr;
}

void *__ast_malloc(size_t size, const char *file, int lineno, const char *func) 
{
	return __ast_alloc_region(size, FUNC_MALLOC, file, lineno, func);
}

void __ast_free(void *ptr, const char *file, int lineno, const char *func) 
{
	__ast_free_region(ptr, file, lineno, func);
}

void *__ast_realloc(void *ptr, size_t size, const char *file, int lineno, const char *func) 
{
	void *tmp;
	size_t len=0;
	if (ptr) {
		len = __ast_sizeof_region(ptr);
		if (!len) {
			fprintf(stderr, "WARNING: Realloc of unalloced memory at %p, in %s of %s, line %d\n",
				ptr, func, file, lineno);
			if (mmlog) {
				fprintf(mmlog, "%ld - WARNING: Realloc of unalloced memory at %p, in %s of %s, line %d\n",
					time(NULL), ptr, func, file, lineno);
				fflush(mmlog);
			}
			return NULL;
		}
	}
	tmp = __ast_alloc_region(size, FUNC_REALLOC, file, lineno, func);
	if (tmp) {
		if (len > size)
			len = size;
		if (ptr) {
			memcpy(tmp, ptr, len);
			__ast_free_region(ptr, file, lineno, func);
		}
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
	ptr = __ast_alloc_region(len, FUNC_STRDUP, file, lineno, func);
	if (ptr)
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
	ptr = __ast_alloc_region(len, FUNC_STRNDUP, file, lineno, func);
	if (ptr)
		strcpy(ptr, s);
	return ptr;
}

int __ast_vasprintf(char **strp, const char *fmt, va_list ap, const char *file, int lineno, const char *func) 
{
	int n, size = strlen(fmt) + 1;
	if ((*strp = __ast_alloc_region(size, FUNC_VASPRINTF, file, lineno, func)) == NULL)
		return -1; 
	for (;;) {
		n = vsnprintf(*strp, size, fmt, ap);
		if (n > -1 && n < size)
			return n;
		if (n > -1)	/* glibc 2.1 */
			size = n+1;
		else		/* glibc 2.0 */
			size *= 2;
		if ((*strp = __ast_realloc(*strp, size, file, lineno, func)) == NULL)
			return -1;
	}
}

static int handle_show_memory(int fd, int argc, char *argv[])
{
	char *fn = NULL;
	int x;
	struct ast_region *reg;
	unsigned int len=0;
	int count = 0;
	if (argc >3) 
		fn = argv[3];

	/* try to lock applications list ... */
	ast_mutex_lock(&showmemorylock);

	for (x=0;x<SOME_PRIME;x++) {
		reg = regions[x];
		while(reg) {
			if (!fn || !strcasecmp(fn, reg->file)) {
				ast_cli(fd, "%10d bytes allocated in %20s at line %5d of %s\n", reg->len, reg->func, reg->lineno, reg->file);
				len += reg->len;
				count++;
			}
			reg = reg->next;
		}
	}
	ast_cli(fd, "%d bytes allocated %d units total\n", len, count);
	ast_mutex_unlock(&showmemorylock);
	return RESULT_SUCCESS;
}

struct file_summary {
	char fn[80];
	int len;
	int count;
	struct file_summary *next;
};

static int handle_show_memory_summary(int fd, int argc, char *argv[])
{
	char *fn = NULL;
	int x;
	struct ast_region *reg;
	unsigned int len=0;
	int count = 0;
	struct file_summary *list = NULL, *cur;
	
	if (argc >3) 
		fn = argv[3];

	/* try to lock applications list ... */
	ast_mutex_lock(&reglock);

	for (x=0;x<SOME_PRIME;x++) {
		reg = regions[x];
		while(reg) {
			if (!fn || !strcasecmp(fn, reg->file)) {
				cur = list;
				while(cur) {
					if ((!fn && !strcmp(cur->fn, reg->file)) || (fn && !strcmp(cur->fn, reg->func)))
						break;
					cur = cur->next;
				}
				if (!cur) {
					cur = alloca(sizeof(struct file_summary));
					memset(cur, 0, sizeof(struct file_summary));
					strncpy(cur->fn, fn ? reg->func : reg->file, sizeof(cur->fn) - 1);
					cur->next = list;
					list = cur;
				}
				cur->len += reg->len;
				cur->count++;
			}
			reg = reg->next;
		}
	}
	ast_mutex_unlock(&reglock);
	
	/* Dump the whole list */
	while(list) {
		cur = list;
		len += list->len;
		count += list->count;
		if (fn)
			ast_cli(fd, "%10d bytes in %5d allocations in function '%s' of '%s'\n", list->len, list->count, list->fn, fn);
		else
			ast_cli(fd, "%10d bytes in %5d allocations in file '%s'\n", list->len, list->count, list->fn);
		list = list->next;
#if 0
		free(cur);
#endif		
	}
	ast_cli(fd, "%d bytes allocated %d units total\n", len, count);
	return RESULT_SUCCESS;
}

static char show_memory_help[] = 
"Usage: show memory allocations [<file>]\n"
"       Dumps a list of all segments of allocated memory, optionally\n"
"limited to those from a specific file\n";

static char show_memory_summary_help[] = 
"Usage: show memory summary [<file>]\n"
"       Summarizes heap memory allocations by file, or optionally\n"
"by function, if a file is specified\n";

static struct ast_cli_entry show_memory_allocations_cli = 
	{ { "show", "memory", "allocations", NULL }, 
	handle_show_memory, "Display outstanding memory allocations",
	show_memory_help };

static struct ast_cli_entry show_memory_summary_cli = 
	{ { "show", "memory", "summary", NULL }, 
	handle_show_memory_summary, "Summarize outstanding memory allocations",
	show_memory_summary_help };


void __ast_mm_init(void)
{
	ast_cli_register(&show_memory_allocations_cli);
	ast_cli_register(&show_memory_summary_cli);
	mmlog = fopen("/var/log/asterisk/mmlog", "a+");
	if (option_verbose)
		ast_verbose("Asterisk Malloc Debugger Started (see /var/log/asterisk/mmlog)\n");
	if (mmlog) {
		fprintf(mmlog, "%ld - New session\n", time(NULL));
		fflush(mmlog);
	}
}

#endif
