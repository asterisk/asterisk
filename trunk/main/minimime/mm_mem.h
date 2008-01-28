#ifndef __MEM_H
#define __MEM_H

#ifdef __HAVE_LEAK_DETECTION

#define NAMEOF(v)       #v
#define xmalloc(x) MM_malloc(x, __FILE__, __LINE__)
#define xfree(x) MM_free(x, __FILE__, __LINE__, NAMEOF(x))
#define xstrdup(x) MM_strdup(x, __FILE__, __LINE__)
#define xrealloc(x, y) MM_realloc(x, y, __FILE__, __LINE__)

TAILQ_HEAD(MM_chunks, MM_mem_chunk);

struct MM_mem_chunk {
	void *address;
	const char *filename;
	uint32_t line;
	size_t size;
	
	TAILQ_ENTRY(MM_mem_chunk) next;
};

void *MM_malloc(size_t, char *, int);
void *MM_realloc(void *, size_t, char *, int);
void MM_free(void *, char *, int, char *);
char *MM_strdup(const char *, char *, int);
void MM_leakd_init(void);
void MM_leakd_printallocated(void);
void MM_leakd_flush(void);

#endif /* __HAVE_LEAK_DETECTION */
#endif /* ! HAVE_MEM_H */
