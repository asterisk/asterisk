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
 * \brief Doubly-Linked List Tests
 *
 * \author\verbatim Steve Murphy <murf@digium.com> \endverbatim
 * 
 * This module will run some DLL tests at load time
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"


#include "asterisk/dlinkedlists.h"

/* Tests for DLLists! We really should, and here is a nice place to do it in asterisk */

struct test1
{
	char name[10];
	AST_DLLIST_ENTRY(test1) list;
};

struct test_container
{
	AST_DLLIST_HEAD(entries, test1) entries;
    int count;
};

static void print_list(struct test_container *x, char *expect)
{
	struct test1 *t1;
	char buff[1000];
	buff[0] = 0;
	AST_DLLIST_TRAVERSE(&x->entries, t1, list) {
		strcat(buff,t1->name);
		if (t1 != AST_DLLIST_LAST(&x->entries))
			strcat(buff," <=> ");
	}
	
	ast_log(LOG_NOTICE,"Got: %s  [expect %s]\n", buff, expect);
}

static void print_list_backwards(struct test_container *x, char *expect)
{
	struct test1 *t1;
	char buff[1000];
	buff[0] = 0;
	AST_DLLIST_TRAVERSE_BACKWARDS(&x->entries, t1, list) {
		strcat(buff,t1->name);
		if (t1 != AST_DLLIST_FIRST(&x->entries))
			strcat(buff," <=> ");
	}
	
	ast_log(LOG_NOTICE,"Got: %s  [expect %s]\n", buff, expect);
}

static struct test_container *make_cont(void)
{
	struct test_container *t = ast_calloc(sizeof(struct test_container),1);
	return t;
}

static struct test1 *make_test1(char *name)
{
	struct test1 *t1 = ast_calloc(sizeof(struct test1),1);
	strcpy(t1->name, name);
	return t1;
}

static void destroy_test_container(struct test_container *x)
{
	/* remove all the test1's */
	struct test1 *t1;
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(&x->entries, t1, list) {
		AST_DLLIST_REMOVE_CURRENT(list);
		free(t1);
	}
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_END;
	free(x);
}

/* Macros to test:
AST_DLLIST_LOCK(head)
AST_RWDLLIST_WRLOCK(head)
AST_RWDLLIST_WRLOCK(head) 
AST_RWDLLIST_RDLOCK(head)
AST_DLLIST_TRYLOCK(head)
AST_RWDLLIST_TRYWRLOCK(head)
AST_RWDLLIST_TRYRDLOCK(head)
AST_DLLIST_UNLOCK(head)
AST_RWDLLIST_UNLOCK(head)

AST_DLLIST_HEAD(name, type)
AST_RWDLLIST_HEAD(name, type)
AST_DLLIST_HEAD_NOLOCK(name, type)
AST_DLLIST_HEAD_STATIC(name, type)
AST_RWDLLIST_HEAD_STATIC(name, type)
AST_DLLIST_HEAD_NOLOCK_STATIC(name, type)
AST_DLLIST_HEAD_SET(head, entry)
AST_RWDLLIST_HEAD_SET(head, entry)
AST_DLLIST_HEAD_SET_NOLOCK(head, entry)
AST_DLLIST_HEAD_INIT(head)
AST_RWDLLIST_HEAD_INIT(head)
AST_DLLIST_HEAD_INIT_NOLOCK(head)

AST_RWDLLIST_HEAD_DESTROY(head)

AST_DLLIST_ENTRY(type)

--- the above not going to be dealt with here ---

AST_DLLIST_INSERT_HEAD(head, elm, field)
AST_DLLIST_TRAVERSE(head,var,field)
AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(head, var, field)
AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_END
AST_DLLIST_FIRST(head)
AST_DLLIST_LAST(head)
AST_DLLIST_NEXT(elm, field)
AST_DLLIST_PREV(elm, field)
AST_DLLIST_EMPTY(head)
AST_DLLIST_TRAVERSE_BACKWARDS(head,var,field)
AST_DLLIST_INSERT_AFTER(head, listelm, elm, field)
AST_DLLIST_INSERT_TAIL(head, elm, field)
AST_DLLIST_REMOVE_HEAD(head, field)
AST_DLLIST_REMOVE(head, elm, field)
AST_DLLIST_TRAVERSE_SAFE_BEGIN(head, var, field)
AST_DLLIST_TRAVERSE_SAFE_END
AST_DLLIST_REMOVE_CURRENT(field)
AST_DLLIST_MOVE_CURRENT(newhead, field)
AST_DLLIST_INSERT_BEFORE_CURRENT(elm, field) 


AST_DLLIST_MOVE_CURRENT_BACKWARDS(newhead, field)
AST_DLLIST_INSERT_BEFORE_CURRENT_BACKWARDS(elm, field)
AST_DLLIST_HEAD_DESTROY(head)

AST_DLLIST_APPEND_DLLIST(head, list, field)

*/

static void dll_tests(void)
{
	struct test_container *tc;
	struct test1 *a;
	struct test1 *b;
	struct test1 *c;
	struct test1 *d;
	struct test1 *e;
	
	ast_log(LOG_NOTICE,"Test AST_DLLIST_INSERT_HEAD, AST_DLLIST_TRAVERSE, AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN, AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_END\n");
	tc = make_cont();
	a = make_test1("A");
	b = make_test1("B");
	c = make_test1("C");
	d = make_test1("D");
	AST_DLLIST_INSERT_HEAD(&tc->entries, d, list);
	AST_DLLIST_INSERT_HEAD(&tc->entries, c, list);
	AST_DLLIST_INSERT_HEAD(&tc->entries, b, list);
	AST_DLLIST_INSERT_HEAD(&tc->entries, a, list);
	print_list(tc, "A <=> B <=> C <=> D");

	destroy_test_container(tc);
	
	tc = make_cont();

	if (AST_DLLIST_EMPTY(&tc->entries))
		ast_log(LOG_NOTICE,"Test AST_DLLIST_EMPTY....OK\n");
	else
		ast_log(LOG_NOTICE,"Test AST_DLLIST_EMPTY....PROBLEM!!\n");


	a = make_test1("A");
	b = make_test1("B");
	c = make_test1("C");
	d = make_test1("D");
	
	ast_log(LOG_NOTICE,"Test AST_DLLIST_INSERT_TAIL\n");
	AST_DLLIST_INSERT_TAIL(&tc->entries, a, list);
	AST_DLLIST_INSERT_TAIL(&tc->entries, b, list);
	AST_DLLIST_INSERT_TAIL(&tc->entries, c, list);
	AST_DLLIST_INSERT_TAIL(&tc->entries, d, list);
	print_list(tc, "A <=> B <=> C <=> D");

	if (AST_DLLIST_FIRST(&tc->entries) == a)
		ast_log(LOG_NOTICE,"Test AST_DLLIST_FIRST....OK\n");
	else
		ast_log(LOG_NOTICE,"Test AST_DLLIST_FIRST....PROBLEM\n");

	if (AST_DLLIST_LAST(&tc->entries) == d)
		ast_log(LOG_NOTICE,"Test AST_DLLIST_LAST....OK\n");
	else
		ast_log(LOG_NOTICE,"Test AST_DLLIST_LAST....PROBLEM\n");

	if (AST_DLLIST_NEXT(a,list) == b)
		ast_log(LOG_NOTICE,"Test AST_DLLIST_NEXT....OK\n");
	else
		ast_log(LOG_NOTICE,"Test AST_DLLIST_NEXT....PROBLEM\n");

	if (AST_DLLIST_PREV(d,list) == c)
		ast_log(LOG_NOTICE,"Test AST_DLLIST_PREV....OK\n");
	else
		ast_log(LOG_NOTICE,"Test AST_DLLIST_PREV....PROBLEM\n");

	destroy_test_container(tc);

	tc = make_cont();

	a = make_test1("A");
	b = make_test1("B");
	c = make_test1("C");
	d = make_test1("D");

	ast_log(LOG_NOTICE,"Test AST_DLLIST_INSERT_AFTER, AST_DLLIST_TRAVERSE_BACKWARDS\n");
	AST_DLLIST_INSERT_HEAD(&tc->entries, a, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, a, b, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, b, c, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, c, d, list);
	print_list_backwards(tc, "D <=> C <=> B <=> A");

	ast_log(LOG_NOTICE,"Test AST_DLLIST_REMOVE_HEAD\n");
	AST_DLLIST_REMOVE_HEAD(&tc->entries, list);
	print_list_backwards(tc, "D <=> C <=> B");
	ast_log(LOG_NOTICE,"Test AST_DLLIST_REMOVE_HEAD\n");
	AST_DLLIST_REMOVE_HEAD(&tc->entries, list);
	print_list_backwards(tc, "D <=> C");
	ast_log(LOG_NOTICE,"Test AST_DLLIST_REMOVE_HEAD\n");
	AST_DLLIST_REMOVE_HEAD(&tc->entries, list);
	print_list_backwards(tc, "D");
	AST_DLLIST_REMOVE_HEAD(&tc->entries, list);

	if (AST_DLLIST_EMPTY(&tc->entries))
		ast_log(LOG_NOTICE,"Test AST_DLLIST_REMOVE_HEAD....OK\n");
	else
		ast_log(LOG_NOTICE,"Test AST_DLLIST_REMOVE_HEAD....PROBLEM!!\n");

	AST_DLLIST_INSERT_HEAD(&tc->entries, a, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, a, b, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, b, c, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, c, d, list);

	ast_log(LOG_NOTICE,"Test AST_DLLIST_REMOVE\n");
	AST_DLLIST_REMOVE(&tc->entries, c, list);
	print_list(tc, "A <=> B <=> D");
	AST_DLLIST_REMOVE(&tc->entries, a, list);
	print_list(tc, "B <=> D");
	AST_DLLIST_REMOVE(&tc->entries, d, list);
	print_list(tc, "B");
	AST_DLLIST_REMOVE(&tc->entries, b, list);
	
	if (AST_DLLIST_EMPTY(&tc->entries))
		ast_log(LOG_NOTICE,"Test AST_DLLIST_REMOVE....OK\n");
	else
		ast_log(LOG_NOTICE,"Test AST_DLLIST_REMOVE....PROBLEM!!\n");

	AST_DLLIST_INSERT_HEAD(&tc->entries, a, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, a, b, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, b, c, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, c, d, list);

	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&tc->entries, e, list) {
		AST_DLLIST_REMOVE_CURRENT(list);
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	if (AST_DLLIST_EMPTY(&tc->entries))
		ast_log(LOG_NOTICE,"Test AST_DLLIST_REMOVE_CURRENT... OK\n");
	else
		ast_log(LOG_NOTICE,"Test AST_DLLIST_REMOVE_CURRENT... PROBLEM\n");
	
	ast_log(LOG_NOTICE,"Test AST_DLLIST_MOVE_CURRENT, AST_DLLIST_INSERT_BEFORE_CURRENT\n");
	AST_DLLIST_INSERT_HEAD(&tc->entries, a, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, a, b, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, b, c, list);
	AST_DLLIST_TRAVERSE_SAFE_BEGIN(&tc->entries, e, list) {
		if (e == a) {
			AST_DLLIST_INSERT_BEFORE_CURRENT(d, list);  /* D A B C */
		}
		
		if (e == b) {
			AST_DLLIST_MOVE_CURRENT(&tc->entries, list); /* D A C B */
		}
		
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	print_list(tc, "D <=> A <=> C <=> B");
	
	destroy_test_container(tc);

	tc = make_cont();

	a = make_test1("A");
	b = make_test1("B");
	c = make_test1("C");
	d = make_test1("D");

	ast_log(LOG_NOTICE,"Test: AST_DLLIST_MOVE_CURRENT_BACKWARDS and AST_DLLIST_INSERT_BEFORE_CURRENT_BACKWARDS\n");
	AST_DLLIST_INSERT_HEAD(&tc->entries, a, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, a, b, list);
	AST_DLLIST_INSERT_AFTER(&tc->entries, b, c, list);
	AST_DLLIST_TRAVERSE_BACKWARDS_SAFE_BEGIN(&tc->entries, e, list) {
		if (e == c && AST_DLLIST_FIRST(&tc->entries) != c) {
			AST_DLLIST_MOVE_CURRENT_BACKWARDS(&tc->entries, list); /* C A B */
			print_list(tc, "C <=> A <=> B");
		}

		if (e == b) {
			AST_DLLIST_REMOVE_CURRENT(list);  /* C A */
			print_list(tc, "C <=> A");
		}
		if (e == a) {
			AST_DLLIST_INSERT_BEFORE_CURRENT_BACKWARDS(d, list); /* C A D */
			print_list(tc, "C <=> A <=> D");
		}
		
	}
	AST_DLLIST_TRAVERSE_SAFE_END;
	print_list(tc, "C <=> A <=> D");

}

static int unload_module(void)
{
	return 0;
}

static int load_module(void)
{
	dll_tests();
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Test Doubly-Linked Lists");
