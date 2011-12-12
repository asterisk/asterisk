/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Steve Murphy
 *
 * Steve Murphy <murf@digium.com>
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
 *  \brief A program to thoroughly thrash a hash table, testing
 *         out locking safety, and making sure all functionality
 *         is functioning. Run with 5 or more threads to get that
 *         fully intense firestorm of activity. If your
 *         hash tables don't crash, lock up, or go weird, it must
 *         be good code! Even features some global counters
 *         that will get slightly behind because they aren't lock-protected.
 *
 *  \author Steve Murphy <murf@digium.com>
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include "asterisk/lock.h"
#include "asterisk/hashtab.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
int testno = 1;

/* stuff we need to make this work with the hashtab stuff */
#if !defined(LOW_MEMORY)
int64_t ast_mark(int prof_id, int x)
{
	return 0;
}
#endif

void pbx_substitute_variables_helper_full(struct ast_channel *c, struct varshead *headp, const char *cp1, char *cp2, int cp2_size, size_t *used);
void pbx_substitute_variables_helper_full(struct ast_channel *c, struct varshead *headp, const char *cp1, char *cp2, int cp2_size, size_t *used)
{
}

struct ht_element 
{
	char *key;
	char *val;
};

static int hashtab_compare_strings_nocase(const void *a, const void *b)
{
	const struct ht_element *ae = a, *be = b;
	return ast_hashtab_compare_strings_nocase(ae->key, be->key);
}

#if 0
static int hashtab_compare_strings(const void *a, const void *b)
{
	const struct ht_element *ae = a, *be = b;
	return ast_hashtab_compare_strings(ae->key, be->key);
}

static unsigned int hashtab_hash_string(const void *obj)
{
	const struct ht_element *o = obj;
	return ast_hashtab_hash_string((const void *)o->key);
}
#endif

static unsigned int hashtab_hash_string_nocase(const void *obj)
{
	const struct ht_element *o = obj;
	return ast_hashtab_hash_string_nocase((const void*)o->key);
}

/* random numbers */

static int my_rand(int incl_low, int incl_high, unsigned int *seedp)
{
	if (incl_high == 0)
		return 0;
	
	return incl_low + (rand_r(seedp) % incl_high);
}




/* the testing routines */

static int glob_highwater = 0;
struct ast_hashtab *glob_hashtab = 0;
unsigned int glob_seed = 0;
int els_removed = 0;
int els_added = 0;
int els_lookedup = 0;
int els_found = 0;
int els_traversals = 0;

/* all the operations to perform on the hashtab */

static void add_element(void)
{
	char keybuf[100];
	struct ht_element *x = malloc(sizeof(struct ht_element));
	sprintf(keybuf,"key%08d", glob_highwater++);
	x->key = strdup(keybuf);
	x->val = strdup("interesting data");
	ast_hashtab_insert_immediate(glob_hashtab, x);
	els_added++;
}

static void traverse_elements(void)
{
	struct ht_element *el;
	int c=0;
	struct ast_hashtab_iter *it = ast_hashtab_start_write_traversal(glob_hashtab);
#ifdef DEBUG
	printf("Traverse hashtab\n");
#endif
	while ((el = ast_hashtab_next(it))) {
		c++;
	}
	ast_hashtab_end_traversal(it);
	els_traversals++; /* unprotected, sometimes off, but, not really important, either */
}

static void * del_element(unsigned int *seedp)
{
	char keybuf[100];
	struct ht_element *el, lookup;
	int x;
	
	/* pick a random element from 0 to highwater-1 */
	x = my_rand(0,glob_highwater-1,seedp);
	sprintf(keybuf, "key%08d", x);
#ifdef DEBUG
	printf("Removing %s", keybuf);
#endif
	lookup.key = keybuf;
	el = ast_hashtab_remove_object_via_lookup(glob_hashtab, &lookup);
	
	if (el) {
#ifdef DEBUG
		printf("...YES (el=%x)\n", (unsigned long)el);
#endif
		free(el->key);
		free(el->val);
		free(el);
		els_removed++;
	} else {
#ifdef DEBUG
		printf("...NO.\n");
#endif
		return 0;
	}
	
	
	return el;
}

static int lookup_element(unsigned int *seedp)
{
	char keybuf[100];
	struct ht_element *el, lookup;
	int x;
	
	/* pick a random element from 0 to highwater-1 */
	x = my_rand(0,glob_highwater-1,seedp);
	sprintf(keybuf, "key%08d", x);
	lookup.key = keybuf;
	el = ast_hashtab_lookup(glob_hashtab, &lookup);
	els_lookedup++;
	if (el)
		els_found++;
	if (el)
		return 1;
	return 0;
}


static void *hashtest(void *data)
{
	int my_els_removed = 0;
	int my_els_added = 0;
	int my_els_lookedup = 0;
	int my_els_found = 0;
	int my_els_traversals = 0;
	int my_testno = testno++;
	int its;
	
	/* data will be a random number == use as a seed for random numbers */
	unsigned long seed = (unsigned long)data;

	printf("hashtest thread created... test beginning\n");
	
	/* main test routine-- a global hashtab exists, pound it like crazy  */
	for(its=0;its<100000;its++)
	{
		void *seed2 = &seed;
		int op = my_rand(0,100, seed2);
		if (op<60) {
			my_els_lookedup++;
#ifdef DEBUG
			printf("%d[%d]: LOOKUP\n", my_testno, its);
#endif			
			if ((my_els_lookedup%1000)==0) {
				printf(".");
				fflush(stdout);
			}
			if (lookup_element(seed2))
				my_els_found++;
		} else if (op < 61) { /* make this 61 and it'll take 15 minutes to run */
#ifdef DEBUG
			printf("%d[%d]: TRAVERSE\n", my_testno, its);
#endif
			traverse_elements();
			my_els_traversals++;
			
		} else if (op < 80) {
#ifdef DEBUG
			printf("%d[%d]: REMOVE\n", my_testno, its);
#endif
			if (del_element(seed2))
				my_els_removed++;
		} else {
			my_els_added++;
#ifdef DEBUG
			printf("%d[%d]: ADD\n", my_testno, its);
#endif
			add_element();
		}
	}
	printf("\nhashtest thread %d exiting.... lookups=%d/%d, added=%d, removed=%d, traversals=%d;\n",
		   my_testno, my_els_found, my_els_lookedup, my_els_added, my_els_removed, my_els_traversals);
	printf("\ntotals..................... lookups=%d/%d, added=%d, removed=%d, traversals=%d;\n",
		   els_found, els_lookedup, els_added, els_removed,els_traversals);
	pthread_exit(0);
}

static void run_hashtest(int numthr)
{
	pthread_t thr[numthr];
	void *thrres[numthr];
	int i, biggest, resize_cnt, numobjs, numbuckets;
	
	/* init a single global hashtab, then... */
	glob_hashtab = ast_hashtab_create(180000, hashtab_compare_strings_nocase, ast_hashtab_resize_java, ast_hashtab_newsize_java, hashtab_hash_string_nocase, 1);
	printf("starting with %d elements in the hashtable...\n", ast_hashtab_capacity(glob_hashtab));
	/* set a random seed  */
	glob_seed = (unsigned int)time(0);
	srand(glob_seed);
	
	/* create threads, each running hashtest */
	for(i=0;i<numthr;i++)
	{
		unsigned long z = rand();
		
		printf("starting hashtest thread %d....\n",i+1);
		if (ast_pthread_create(&thr[i], NULL, hashtest, (void*)z)) {
			printf("Sorry, couldn't create thread #%d\n", i+1);
		}
		printf("hashtest thread spawned.... \n");
	}
	/* collect threads, each running hashtest */
	for(i=0;i<numthr;i++)
	{
		printf("waiting for thread %d....\n", i+1);
		if (pthread_join(thr[i], &thrres[i])) {
			printf("Sorry, couldn't join thread #%d\n", i+1);
		}
		printf("hashtest thread %d done.... \n",i+1);
	}
	/* user has to kill/intr the process to stop the test? */
	ast_hashtab_get_stats(glob_hashtab, &biggest, &resize_cnt, &numobjs, &numbuckets);
	printf("Some stats: longest bucket chain: %d;  number of resizes: %d; number of objects: %d;  capacity: %d\n",
			biggest, resize_cnt, numobjs, numbuckets);
}


int main(int argc,char **argv)
{
	if (argc < 2 || argc > 2 || atoi(argv[1]) < 1)
	{
		printf("Usage: hashtest <number of threads>\n");
		exit(1);
	}
	
	/* one arg == number of threads to create */
	run_hashtest(atoi(argv[1]));
	
	return 0;
}
#if !defined(LOW_MEMORY)
int  ast_add_profile(const char *x, uint64_t scale)
{
	return 0;
}
#endif

int ast_loader_register(int (*updater)(void))
{
	return 1;
}

int ast_loader_unregister(int (*updater)(void))
{
	return 1;
}
void ast_module_register(const struct ast_module_info *x)
{
}

void ast_module_unregister(const struct ast_module_info *x)
{
}


void ast_register_file_version(const char *file, const char *version);
void ast_register_file_version(const char *file, const char *version)
{
}

void ast_unregister_file_version(const char *file);
void ast_unregister_file_version(const char *file)
{

}

void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
	va_list vars;
	va_start(vars,fmt);
	printf("LOG: lev:%d file:%s  line:%d func: %s  ",
		   level, file, line, function);
	vprintf(fmt, vars);
	fflush(stdout);
	va_end(vars);
}

void __ast_verbose(const char *file, int line, const char *func, const char *fmt, ...)
{
        va_list vars;
        va_start(vars,fmt);

        printf("VERBOSE: ");
        vprintf(fmt, vars);
        fflush(stdout);
        va_end(vars);
}

void ast_register_thread(char *name)
{

}

void ast_unregister_thread(void *id)
{
}

#ifdef HAVE_BKTR
struct ast_bt *ast_bt_create(void);
struct ast_bt *ast_bt_create(void) 
{
	return NULL;
}

int ast_bt_get_addresses(struct ast_bt *bt);
int ast_bt_get_addresses(struct ast_bt *bt)
{
	return 0;
}

char **ast_bt_get_symbols(void **addresses, size_t num_frames);
char **ast_bt_get_symbols(void **addresses, size_t num_frames)
{
	char **foo = calloc(num_frames, sizeof(char *) + 1);
	if (foo) {
		int i;
		for (i = 0; i < num_frames; i++) {
			foo[i] = (char *) foo + sizeof(char *) * num_frames;
		}
	}
	return foo;
}

void *ast_bt_destroy(struct ast_bt *bt);
void *ast_bt_destroy(struct ast_bt *bt)
{
	return NULL;
}
#endif
