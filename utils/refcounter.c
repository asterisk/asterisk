/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Steve Murphy
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
 *  \brief A program to read in the /tmp/refs file generated
 *         by astobj2 code when the REF_DEBUG macro is defined.
 *         It will read in the file line by line, and
 *         sort the data out by object, and check to see
 *         if the refcounts balance to zero, and the object
 *         was destroyed just once. Any problems that are 
 *         found are reported to stdout and the objects
 *         ref count history is printed out. If all is well,
 *         this program reads in the /tmp/refs file and 
 *         generates no output. No news is good news.
 *  The contents of the /tmp/refs file looks like this:
 *
0x84fd718 -1   astobj2.c:926:cd_cb_debug (deref object via container destroy) [@1]
0x84fd718 =1   chan_sip.c:19760:build_user (allocate a user struct)
0x84fd718 +1   chan_sip.c:21558:reload_config (link user into users table) [@1]
0x84fd718 -1   chan_sip.c:2376:unref_user (Unref the result of build_user. Now, the table link is the only one left.) [@2]
0x84fd718 **call destructor** astobj2.c:926:cd_cb_debug (deref object via container destroy)
 *
 *
 *  \author Steve Murphy <murf@digium.com>
 */

/*** MODULEINFO
	<support_level>extended</support_level>
	<defaultenabled>no</defaultenabled>
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

struct rc_hist
{
	char *desc;
	struct rc_hist *next;
};

struct rc_obj /* short for refcounted object */
{
	unsigned int addr;
	unsigned int count;  /* this plus addr makes each entry unique, starts at 1 */
	int last_count; /* count 1 objects will record how many other objects had the same addr */
	int destroy_count;
	int total_refcount;
	struct rc_hist *hist;
	struct rc_hist *last;
};

void pbx_substitute_variables_helper_full(struct ast_channel *c, struct varshead *headp, const char *cp1, char *cp2, int cp2_size, size_t *used);
void pbx_substitute_variables_helper_full(struct ast_channel *c, struct varshead *headp, const char *cp1, char *cp2, int cp2_size, size_t *used)
{
}

static unsigned int hashtab_hash_rc(const void *obj)
{
	const struct rc_obj *rc = obj;
	return rc->addr + rc->count; /* it's addr will make a FINE hash */
}

static int hashtab_compare_rc(const void *a, const void *b)
{
	const struct rc_obj *rca = a;
	const struct rc_obj *rcb = b;
	if (rca->addr == rcb->addr && rca->count == rcb->count)
		return 0;
	else
		return 1;
}


static struct rc_obj *alloc_obj(unsigned int addr, unsigned int count)
{
	struct rc_obj *x = calloc(1,sizeof(struct rc_obj));
	x->addr = addr;
	x->count = count;
	x->last_count = 1;
	x->total_refcount = 1;
	return x;
}

static void add_to_hist(char *buffer, struct rc_obj *obj)
{
	struct rc_hist *y = calloc(1,sizeof(struct rc_hist));
	y->desc = strdup(buffer);
	if (obj->last) {
		obj->last->next = y;
		obj->last = y;
	} else {
		obj->hist = obj->last = y;
	}
}



int main(int argc,char **argv)
{
	char linebuffer[300];
	FILE *ifile = fopen("/tmp/refs", "r");
	char *t;
	unsigned int un;
	struct rc_obj *curr_obj, *count1_obj;
	struct rc_obj lookup;
	struct ast_hashtab_iter *it;
	struct ast_hashtab *objhash;
	
	if (!ifile) {
		printf("Sorry, Cannot open /tmp/refs!\n");
		exit(10);
	}
	
	objhash = ast_hashtab_create(9000, hashtab_compare_rc, ast_hashtab_resize_java, ast_hashtab_newsize_java, hashtab_hash_rc, 1);
	
	while (fgets(linebuffer, sizeof(linebuffer), ifile)) {
		/* collect data about the entry */
		un = strtoul(linebuffer, &t, 16);
		lookup.addr = un;
		lookup.count = 1;

		count1_obj = ast_hashtab_lookup(objhash, &lookup);
		
		if (count1_obj) {
			/* there IS a count1 obj, so let's see which one we REALLY want */
			if (*(t+1) == '=') {
				/* start a new object! */
				curr_obj = alloc_obj(un, ++count1_obj->last_count);
				/* put it in the hashtable */
				ast_hashtab_insert_safe(objhash, curr_obj);
			} else {
				if (count1_obj->last_count > 1) {
					lookup.count = count1_obj->last_count;
					curr_obj = ast_hashtab_lookup(objhash, &lookup);
				} else {
					curr_obj = count1_obj;
				}
				
			}

		} else {
			/* NO obj at ALL? -- better make one! */
			if (*(t+1) != '=') {
				printf("BAD: object %x appears without previous allocation marker!\n", un);
			}
			curr_obj = count1_obj = alloc_obj(un, 1);
			/* put it in the hashtable */
			ast_hashtab_insert_safe(objhash, curr_obj);
			
		}
		
		if (*(t+1) == '+' || *(t+1) == '-' ) {
			curr_obj->total_refcount += strtol(t+1, NULL, 10);
		} else if (*(t+1) == '*') {
			curr_obj->destroy_count++;
		}
		
		add_to_hist(linebuffer, curr_obj);
	}
	fclose(ifile);
	
	/* traverse the objects and check for problems */
	it = ast_hashtab_start_traversal(objhash);
	while ((curr_obj = ast_hashtab_next(it))) {
		if (curr_obj->total_refcount != 0 || curr_obj->destroy_count != 1) {
			struct rc_hist *h;
			if (curr_obj->total_refcount != 0)
				printf("Problem: net Refcount not zero for object %x\n", curr_obj->addr);
			if (curr_obj->destroy_count > 1 )
				printf("Problem: Object %x destroyed more than once!\n", curr_obj->addr);
			printf("Object %x history:\n", curr_obj->addr);
			for(h=curr_obj->hist;h;h=h->next) {
				printf("   %s", h->desc);
			}
			printf("==============\n");
		}
	}
	ast_hashtab_end_traversal(it);
	return 0;
}


/* stub routines to satisfy linking with asterisk subcomponents */

#ifndef LOW_MEMORY
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

#ifndef LOW_MEMORY
void ast_register_file_version(const char *file, const char *version)
{
}

void ast_unregister_file_version(const char *file)
{

}

#undef ast_mark

int64_t ast_mark(int x, int start1_stop0)
{
	return 0;
}
#endif

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

void __ast_verbose(const char *file, int line, const char *func, int level, const char *fmt, ...)
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
