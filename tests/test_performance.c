/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Inc
 *
 * Joshua Colp <jcolp@sangoma.com>
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

/*!
 * \file
 * \brief Performance Experimentation Tests
 *
 * \author Joshua Colp <jcolp@sangoma.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/time.h"
#include "asterisk/astobj2.h"
#include "asterisk/vector.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/hashtab.h"
#include "asterisk/linkedlists.h"
#include <stdlib.h>

/*! \brief The default number of keys to generate for performance tests */
#define DEFAULT_KEY_COUNT 250

/*! \brief The default duration of the performance test in seconds */
#define DEFAULT_DURATION_SEC 10

/*! \brief The length of each test key */
#define TEST_KEY_LENGTH 32

/*! \brief The number of attempts for random strings before we give up */
#define RANDOM_STRING_ATTEMPTS 100

/*!
 * The following tests cover the most common usage of containers in Asterisk - a key lookup
 * based on a string which then resolves to an underlying object. The CLI commands assume that
 * the underlying container implementations are working properly and return the requested
 * object. These tests also only test single threaded performance. They do not test concurrent
 * access or thread safety.
 */

/*!
 * \brief Frees the memory used by a vector of random strings
 * \param vec The vector of random strings to free
 */
static void free_random_strings(struct ast_vector_string *vec)
{
	AST_VECTOR_RESET(vec, ast_free);
	AST_VECTOR_FREE(vec);
	ast_free(vec);
}

/*!
 * \brief Generates a vector of random strings to use as keys
 * \param count The number of strings to generate
 * \return A vector of random strings, or NULL on failure
 */
static struct ast_vector_string *generate_random_strings(int count)
{
	struct ast_vector_string *vec;
	struct ao2_container *seen;
	int i;

	vec = ast_malloc(sizeof(*vec));
	if (!vec) {
		return NULL;
	}

	/* Preallocate the vector to avoid reallocations during insertion */
	if (AST_VECTOR_INIT(vec, count)) {
		ast_free(vec);
		return NULL;
	}

	/*
	 * To eliminate collissions keep track of seen strings and retry if a collision occurs,
	 * since this is just for keeping track of seen we don't care about optimizing the bucket
	 * size of the container.
	 */
	seen = ast_str_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, count);
	if (!seen) {
		free_random_strings(vec);
		return NULL;
	}

	for (i = 0; i < count; i++) {
		char buf[TEST_KEY_LENGTH];
		unsigned int attempts = 0;
		char *dup;

		/* Generate a random string and retry if it collides with a previously seen string */
		while (1) {
			char *existing;

			ast_generate_random_string(buf, sizeof(buf));

			existing = ao2_find(seen, buf, OBJ_SEARCH_KEY);
			/* We don't care about the existing string, just that it's not NULL */
			ao2_cleanup(existing);
			if (!existing) {
				break;
			}

			/* To prevent infinite loops in case of collisions, limit the number of attempts */
			attempts++;
			if (attempts > RANDOM_STRING_ATTEMPTS) {
				ao2_ref(seen, -1);
				free_random_strings(vec);
				return NULL;
			}
		}

		/* The vector takes pointers to strings, so we need to duplicate the buffer */
		dup = ast_strdup(buf);
		if (!dup) {
			ao2_ref(seen, -1);
			free_random_strings(vec);
			return NULL;
		}

		ast_str_container_add(seen, dup);

		/* This can't fail as the AST_VECTOR_INIT ensured enough space */
		AST_VECTOR_APPEND(vec, dup);
	}

	ao2_ref(seen, -1);

	return vec;
}

/*!
 * \brief Compares two strings for sorting
 * \param a The first string to compare
 * \param b The second string to compare
 * \return The result of the comparison
 */
static int vector_string_cmp(const void *_a, const void *_b)
{
	const char *a = *(const char **)_a;
	const char *b = *(const char **)_b;
	return strcmp(a, b);
}

/*!
 * \brief Finds the next prime number greater than or equal to n
 * \param n The number to find the next prime for
 * \return The next prime number
 */
static int next_prime(int n)
{
	/*
	 * The lowest prime number is 2. It's also the only even prime number, that fact
	 * is of absolutely no relevance to this module but I put it here just for fun
	 */
	if (n <= 2) {
		return 2;
	}

	/* If it's divisible by 2, increment to the next odd number so we can find the next prime easier */
	if (n % 2 == 0) {
		n++;
	}

	/* Loop until we find a prime number */
	while (!ast_is_prime(n)) {
		n += 2;
	}

	return n;
}

/*!
 * \brief Handles the CLI command for testing ao2_container hash string lookups
 * \param e The CLI entry
 * \param cmd The command to execute
 * \param a The CLI arguments
 * \return NULL on success, or an error message on failure
 */
static char *handle_cli_ao2_string_hash(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;
	struct ast_vector_string *strings;
	struct timeval start;
	unsigned int count = 0;
	int key_count = DEFAULT_KEY_COUNT;
	int duration_sec = DEFAULT_DURATION_SEC;
	int duration_ms;
	int buckets = 0;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "performance test ao2_string_hash";
		e->usage =
			"Usage: performance test ao2_string_hash [<num_keys>] [<duration_sec>] [<buckets>]\n"
			"       Test the performance of ao2_container hash string key lookups.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3) {
		if (sscanf(a->argv[3], "%30d", &key_count) <= 0 || key_count <= 0) {
			ast_cli(a->fd, "Invalid value for num_keys: '%s'\n", a->argv[3]);
			return CLI_FAILURE;
		}
	}
	if (a->argc > 4) {
		if (sscanf(a->argv[4], "%30d", &duration_sec) <= 0 || duration_sec <= 0) {
			ast_cli(a->fd, "Invalid value for duration_sec: '%s'\n", a->argv[4]);
			return CLI_FAILURE;
		}
	}
	if (a->argc > 5) {
		if (sscanf(a->argv[5], "%30d", &buckets) <= 0 || buckets <= 0) {
			ast_cli(a->fd, "Invalid value for buckets: '%s'\n", a->argv[5]);
			return CLI_FAILURE;
		}
	}
	duration_ms = duration_sec * 1000;
	if (!buckets) {
		buckets = next_prime(key_count);
	}

	strings = generate_random_strings(key_count);
	if (!strings) {
		ast_cli(a->fd, "Failed to generate random strings\n");
		return CLI_FAILURE;
	}

	container = ast_str_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, buckets);
	if (!container) {
		ast_cli(a->fd, "Failed to allocate ao2 container\n");
		free_random_strings(strings);
		return CLI_FAILURE;
	}

	for (i = 0; i < AST_VECTOR_SIZE(strings); i++) {
		if (ast_str_container_add(container, AST_VECTOR_GET(strings, i))) {
			ast_cli(a->fd, "Failed to add string to container\n");
			ao2_ref(container, -1);
			free_random_strings(strings);
			return CLI_FAILURE;
		}
	}

	ast_cli(a->fd, "Beginning ao2_string_hash performance test (%d keys, %d seconds, buckets %d)\n",
		key_count, duration_sec, buckets);

	start = ast_tvnow();
	for (;;) {
		int idx = ast_random() % AST_VECTOR_SIZE(strings);

		ao2_cleanup(ao2_find(container, AST_VECTOR_GET(strings, idx), OBJ_SEARCH_KEY));
		count++;

		if (ast_tvdiff_ms(ast_tvnow(), start) >= duration_ms) {
			break;
		}
	}

	ast_cli(a->fd, "ao2_string_hash: %u lookups in %d seconds (%u lookups/sec)\n", count, duration_sec, count / duration_sec);

	ao2_ref(container, -1);
	free_random_strings(strings);

	return CLI_SUCCESS;
}

/*!
 * \brief Compares two strings for sorting in the RBTree
 * \param lhs The first string to compare
 * \param rhs The second string to compare
 * \param flags The search flags
 * \return The result of the comparison
 */
static int rbtree_str_sort(const void *lhs, const void *rhs, int flags)
{
	if ((flags & OBJ_SEARCH_MASK) == OBJ_SEARCH_PARTIAL_KEY) {
		return strncmp(lhs, rhs, strlen(rhs));
	} else {
		return strcmp(lhs, rhs);
	}
}

/*!
 * \brief Compares two strings for finding in the RBTree
 * \param lhs The first string to compare
 * \param rhs The second string to compare
 * \param flags The search flags
 * \return The result of the comparison
 */
static int rbtree_str_cmp(void *lhs, void *rhs, int flags)
{
	int cmp = 0;

	if ((flags & OBJ_SEARCH_MASK) == OBJ_SEARCH_PARTIAL_KEY) {
		cmp = strncmp(lhs, rhs, strlen(rhs));
	} else {
		cmp = strcmp(lhs, rhs);
	}

	return cmp ? 0 : CMP_MATCH;
}

/*!
 * \brief Handles the CLI command for testing ao2_container rbtree string lookups
 * \param e The CLI entry
 * \param cmd The command to execute
 * \param a The CLI arguments
 * \return NULL on success, or an error message on failure
 */
static char *handle_cli_ao2_string_rbtree(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;
	struct ast_vector_string *strings;
	struct timeval start;
	unsigned int count = 0;
	int key_count = DEFAULT_KEY_COUNT;
	int duration_sec = DEFAULT_DURATION_SEC;
	int duration_ms;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "performance test ao2_string_rbtree";
		e->usage =
			"Usage: performance test ao2_string_rbtree [<num_keys>] [<duration_sec>]\n"
			"       Test the performance of ao2_container rbtree string key lookups.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3) {
		if (sscanf(a->argv[3], "%30d", &key_count) <= 0 || key_count <= 0) {
			ast_cli(a->fd, "Invalid value for num_keys: '%s'\n", a->argv[3]);
			return CLI_FAILURE;
		}
	}
	if (a->argc > 4) {
		if (sscanf(a->argv[4], "%30d", &duration_sec) <= 0 || duration_sec <= 0) {
			ast_cli(a->fd, "Invalid value for duration_sec: '%s'\n", a->argv[4]);
			return CLI_FAILURE;
		}
	}
	duration_ms = duration_sec * 1000;

	strings = generate_random_strings(key_count);
	if (!strings) {
		ast_cli(a->fd, "Failed to generate random strings\n");
		return CLI_FAILURE;
	}

	container = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, rbtree_str_sort, rbtree_str_cmp);
	if (!container) {
		ast_cli(a->fd, "Failed to allocate ao2 rbtree container\n");
		free_random_strings(strings);
		return CLI_FAILURE;
	}

	for (i = 0; i < AST_VECTOR_SIZE(strings); i++) {
		const char *key = AST_VECTOR_GET(strings, i);
		char *obj = ao2_alloc_options(strlen(key) + 1, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);

		if (!obj) {
			ast_cli(a->fd, "Failed to allocate object for string\n");
			ao2_ref(container, -1);
			free_random_strings(strings);
			return CLI_FAILURE;
		}
		strcpy(obj, key);
		ao2_link(container, obj);
		ao2_ref(obj, -1);
	}

	ast_cli(a->fd, "Beginning ao2_string_rbtree performance test (%d keys, %d seconds)\n",
		key_count, duration_sec);

	start = ast_tvnow();
	for (;;) {
		int idx = ast_random() % AST_VECTOR_SIZE(strings);

		ao2_cleanup(ao2_find(container, AST_VECTOR_GET(strings, idx), OBJ_SEARCH_KEY));
		count++;

		if (ast_tvdiff_ms(ast_tvnow(), start) >= duration_ms) {
			break;
		}
	}

	ast_cli(a->fd, "ao2_string_rbtree: %u lookups in %d seconds (%u lookups/sec)\n", count, duration_sec, count / duration_sec);

	ao2_ref(container, -1);
	free_random_strings(strings);

	return CLI_SUCCESS;
}

/*!
 * \brief Handles the CLI command for testing ao2_container list string lookups
 * \param e The CLI entry
 * \param cmd The command to execute
 * \param a The CLI arguments
 * \return NULL on success, or an error message on failure
 */
static char *handle_cli_ao2_string_list(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;
	struct ast_vector_string *strings;
	struct timeval start;
	unsigned int count = 0;
	int key_count = DEFAULT_KEY_COUNT;
	int duration_sec = DEFAULT_DURATION_SEC;
	int duration_ms;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "performance test ao2_string_list";
		e->usage =
			"Usage: performance test ao2_string_list [<num_keys>] [<duration_sec>]\n"
			"       Test the performance of ao2_container list string key lookups.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3) {
		if (sscanf(a->argv[3], "%30d", &key_count) <= 0 || key_count <= 0) {
			ast_cli(a->fd, "Invalid value for num_keys: '%s'\n", a->argv[3]);
			return CLI_FAILURE;
		}
	}
	if (a->argc > 4) {
		if (sscanf(a->argv[4], "%30d", &duration_sec) <= 0 || duration_sec <= 0) {
			ast_cli(a->fd, "Invalid value for duration_sec: '%s'\n", a->argv[4]);
			return CLI_FAILURE;
		}
	}
	duration_ms = duration_sec * 1000;

	strings = generate_random_strings(key_count);
	if (!strings) {
		ast_cli(a->fd, "Failed to generate random strings\n");
		return CLI_FAILURE;
	}

	container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, rbtree_str_sort, rbtree_str_cmp);
	if (!container) {
		ast_cli(a->fd, "Failed to allocate ao2 list container\n");
		free_random_strings(strings);
		return CLI_FAILURE;
	}

	for (i = 0; i < AST_VECTOR_SIZE(strings); i++) {
		const char *key = AST_VECTOR_GET(strings, i);
		char *obj = ao2_alloc_options(strlen(key) + 1, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);

		if (!obj) {
			ast_cli(a->fd, "Failed to allocate object for string\n");
			ao2_ref(container, -1);
			free_random_strings(strings);
			return CLI_FAILURE;
		}
		strcpy(obj, key);
		ao2_link(container, obj);
		ao2_ref(obj, -1);
	}

	ast_cli(a->fd, "Beginning ao2_string_list performance test (%d keys, %d seconds)\n",
		key_count, duration_sec);

	start = ast_tvnow();
	for (;;) {
		int idx = ast_random() % AST_VECTOR_SIZE(strings);

		ao2_cleanup(ao2_find(container, AST_VECTOR_GET(strings, idx), OBJ_SEARCH_KEY));
		count++;

		if (ast_tvdiff_ms(ast_tvnow(), start) >= duration_ms) {
			break;
		}
	}

	ast_cli(a->fd, "ao2_string_list: %u lookups in %d seconds (%u lookups/sec)\n", count, duration_sec, count / duration_sec);

	ao2_ref(container, -1);
	free_random_strings(strings);

	return CLI_SUCCESS;
}

/*!
 * \brief Frees a string object placed into a hashtab
 * \param obj The string object to free
 */
static void hashtab_free_string(void *obj)
{
	ast_free(obj);
}

/*!
 * \brief Handles the CLI command for testing hashtab string lookups
 * \param e The CLI entry
 * \param cmd The command to execute
 * \param a The CLI arguments
 * \return NULL on success, or an error message on failure
 */
static char *handle_cli_hashtab_string(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_hashtab *tab;
	struct ast_vector_string *strings;
	struct timeval start;
	unsigned int count = 0;
	int key_count = DEFAULT_KEY_COUNT;
	int duration_sec = DEFAULT_DURATION_SEC;
	int duration_ms;
	int buckets = 0;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "performance test hashtab_string";
		e->usage =
			"Usage: performance test hashtab_string [<num_keys>] [<duration_sec>] [<buckets>]\n"
			"       Test the performance of hashtab string lookups.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3) {
		if (sscanf(a->argv[3], "%30d", &key_count) <= 0 || key_count <= 0) {
			ast_cli(a->fd, "Invalid value for num_keys: '%s'\n", a->argv[3]);
			return CLI_FAILURE;
		}
	}
	if (a->argc > 4) {
		if (sscanf(a->argv[4], "%30d", &duration_sec) <= 0 || duration_sec <= 0) {
			ast_cli(a->fd, "Invalid value for duration_sec: '%s'\n", a->argv[4]);
			return CLI_FAILURE;
		}
	}
	if (a->argc > 5) {
		if (sscanf(a->argv[5], "%30d", &buckets) <= 0 || buckets <= 0) {
			ast_cli(a->fd, "Invalid value for buckets: '%s'\n", a->argv[5]);
			return CLI_FAILURE;
		}
	}
	duration_ms = duration_sec * 1000;
	if (!buckets) {
		buckets = next_prime(key_count);
	}

	strings = generate_random_strings(key_count);
	if (!strings) {
		ast_cli(a->fd, "Failed to generate random strings\n");
		return CLI_FAILURE;
	}

	tab = ast_hashtab_create(buckets, ast_hashtab_compare_strings, NULL, NULL, ast_hashtab_hash_string, 0);
	if (!tab) {
		ast_cli(a->fd, "Failed to allocate hashtab\n");
		free_random_strings(strings);
		return CLI_FAILURE;
	}

	for (i = 0; i < AST_VECTOR_SIZE(strings); i++) {
		char *dup = ast_strdup(AST_VECTOR_GET(strings, i));

		if (!dup || !ast_hashtab_insert_immediate(tab, dup)) {
			ast_cli(a->fd, "Failed to insert string into hashtab\n");
			ast_free(dup);
			ast_hashtab_destroy(tab, hashtab_free_string);
			free_random_strings(strings);
			return CLI_FAILURE;
		}
	}

	ast_cli(a->fd, "Beginning hashtab_string performance test (%d keys, %d seconds, buckets %d)\n",
		key_count, duration_sec, buckets);

	start = ast_tvnow();
	for (;;) {
		int idx = ast_random() % AST_VECTOR_SIZE(strings);

		ast_hashtab_lookup(tab, AST_VECTOR_GET(strings, idx));
		count++;

		if (ast_tvdiff_ms(ast_tvnow(), start) >= duration_ms) {
			break;
		}
	}

	ast_cli(a->fd, "hashtab_string: %u lookups in %d seconds (%u lookups/sec)\n", count, duration_sec, count / duration_sec);

	ast_hashtab_destroy(tab, hashtab_free_string);
	free_random_strings(strings);

	return CLI_SUCCESS;
}

/*!
 * \brief A node in the linked list for string lookups
 */
struct list_node {
	char *str;
	AST_LIST_ENTRY(list_node) list;
};

/*!
 * \brief Handle the 'performance test list_string' command
 * \param e The CLI entry
 * \param cmd The command to execute
 * \param a The CLI arguments
 * \return CLI_SUCCESS on success, CLI_FAILURE on error
 */
static char *handle_cli_list_string(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	AST_LIST_HEAD_NOLOCK(list_head, list_node) head;
	struct ast_vector_string *strings;
	struct list_node *node;
	struct timeval start;
	unsigned int count = 0;
	int key_count = DEFAULT_KEY_COUNT;
	int duration_sec = DEFAULT_DURATION_SEC;
	int duration_ms;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "performance test list_string";
		e->usage =
			"Usage: performance test list_string [<num_keys>] [<duration_sec>]\n"
			"       Test the performance of linked list string key lookups.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3) {
		if (sscanf(a->argv[3], "%30d", &key_count) <= 0 || key_count <= 0) {
			ast_cli(a->fd, "Invalid value for num_keys: '%s'\n", a->argv[3]);
			return CLI_FAILURE;
		}
	}
	if (a->argc > 4) {
		if (sscanf(a->argv[4], "%30d", &duration_sec) <= 0 || duration_sec <= 0) {
			ast_cli(a->fd, "Invalid value for duration_sec: '%s'\n", a->argv[4]);
			return CLI_FAILURE;
		}
	}
	duration_ms = duration_sec * 1000;

	strings = generate_random_strings(key_count);
	if (!strings) {
		ast_cli(a->fd, "Failed to generate random strings\n");
		return CLI_FAILURE;
	}

	AST_LIST_HEAD_INIT_NOLOCK(&head);

	for (i = 0; i < AST_VECTOR_SIZE(strings); i++) {
		node = ast_calloc(1, sizeof(*node));
		if (!node) {
			ast_cli(a->fd, "Failed to allocate list node\n");
			goto cleanup;
		}

		node->str = ast_strdup(AST_VECTOR_GET(strings, i));
		if (!node->str) {
			ast_free(node);
			ast_cli(a->fd, "Failed to duplicate list node string\n");
			goto cleanup;
		}

		AST_LIST_INSERT_TAIL(&head, node, list);
	}

	ast_cli(a->fd, "Beginning list_string performance test (%d keys, %d seconds)\n",
		key_count, duration_sec);

	start = ast_tvnow();
	for (;;) {
		int idx = ast_random() % AST_VECTOR_SIZE(strings);

		AST_LIST_TRAVERSE(&head, node, list) {
			if (!strcmp(node->str, AST_VECTOR_GET(strings, idx))) {
				break;
			}
		}
		count++;

		if (ast_tvdiff_ms(ast_tvnow(), start) >= duration_ms) {
			break;
		}
	}

	ast_cli(a->fd, "list_string: %u lookups in %d seconds (%u lookups/sec)\n", count, duration_sec, count / duration_sec);

cleanup:
	while ((node = AST_LIST_REMOVE_HEAD(&head, list))) {
		ast_free(node->str);
		ast_free(node);
	}

	free_random_strings(strings);

	return CLI_SUCCESS;
}

/*!
 * \brief Handles the CLI command for testing vector string binary search lookups
 * \param e The CLI entry
 * \param cmd The command to execute
 * \param a The CLI arguments
 * \return NULL on success, or an error message on failure
 */
static char *handle_cli_vector_bsearch_lookup(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_vector_string *strings;
	struct ast_vector_string vec;
	struct timeval start;
	unsigned int count = 0;
	int key_count = DEFAULT_KEY_COUNT;
	int duration_sec = DEFAULT_DURATION_SEC;
	int duration_ms;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "performance test vector_string_bsearch";
		e->usage =
			"Usage: performance test vector_string_bsearch [<num_keys>] [<duration_sec>]\n"
			"       Test the performance of vector string key binary search lookups.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3) {
		if (sscanf(a->argv[3], "%30d", &key_count) <= 0 || key_count <= 0) {
			ast_cli(a->fd, "Invalid value for num_keys: '%s'\n", a->argv[3]);
			return CLI_FAILURE;
		}
	}
	if (a->argc > 4) {
		if (sscanf(a->argv[4], "%30d", &duration_sec) <= 0 || duration_sec <= 0) {
			ast_cli(a->fd, "Invalid value for duration_sec: '%s'\n", a->argv[4]);
			return CLI_FAILURE;
		}
	}
	duration_ms = duration_sec * 1000;

	strings = generate_random_strings(key_count);
	if (!strings) {
		ast_cli(a->fd, "Failed to generate random strings\n");
		return CLI_FAILURE;
	}

	if (AST_VECTOR_INIT(&vec, key_count)) {
		ast_cli(a->fd, "Failed to initialize vector\n");
		free_random_strings(strings);
		return CLI_FAILURE;
	}

	for (i = 0; i < AST_VECTOR_SIZE(strings); i++) {
		char *dup = ast_strdup(AST_VECTOR_GET(strings, i));

		if (!dup) {
			ast_cli(a->fd, "Failed to add string to vector\n");
			AST_VECTOR_RESET(&vec, ast_free);
			AST_VECTOR_FREE(&vec);
			free_random_strings(strings);
			return CLI_FAILURE;
		}

		/* This can't fail as we're appending to a vector that we know is large enough */
		AST_VECTOR_APPEND(&vec, dup);
	}

	AST_VECTOR_SORT(&vec, vector_string_cmp);

	ast_cli(a->fd, "Beginning vector_string_bsearch performance test (%d keys, %d seconds)\n",
		key_count, duration_sec);

	start = ast_tvnow();
	for (;;) {
		int idx = ast_random() % AST_VECTOR_SIZE(strings);
		const char **key;

		/*
		 * Unlike the other tests this actually stores the result in a variable and uses it
		 * for determining if the count should be increased. This is done to ensure the compiler
		 * doesn't pull some funny business and optimize away the bsearch call.
		 */
		key = AST_VECTOR_BSEARCH(&vec, AST_VECTOR_GET(strings, idx), vector_string_cmp);
		if (key) {
			count++;
		}

		if (ast_tvdiff_ms(ast_tvnow(), start) >= duration_ms) {
			break;
		}
	}

	ast_cli(a->fd, "vector_string_bsearch: %u lookups in %d seconds (%u lookups/sec)\n", count, duration_sec, count / duration_sec);

	AST_VECTOR_RESET(&vec, ast_free);
	AST_VECTOR_FREE(&vec);
	free_random_strings(strings);

	return CLI_SUCCESS;
}

/*!
 * \brief Handles the CLI command for testing vector string linear search lookups
 * \param e The CLI entry
 * \param cmd The command to execute
 * \param a The CLI arguments
 * \return NULL on success, or an error message on failure
 */
static char *handle_cli_vector_linear_lookup(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_vector_string *strings;
	struct ast_vector_string vec;
	struct timeval start;
	unsigned int count = 0;
	const char *target;
	int key_count = DEFAULT_KEY_COUNT;
	int duration_sec = DEFAULT_DURATION_SEC;
	int duration_ms;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "performance test vector_string_linear";
		e->usage =
			"Usage: performance test vector_string_linear [<num_keys>] [<duration_sec>]\n"
			"       Test the performance of vector string linear search lookups.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3) {
		if (sscanf(a->argv[3], "%30d", &key_count) <= 0 || key_count <= 0) {
			ast_cli(a->fd, "Invalid value for num_keys: '%s'\n", a->argv[3]);
			return CLI_FAILURE;
		}
	}
	if (a->argc > 4) {
		if (sscanf(a->argv[4], "%30d", &duration_sec) <= 0 || duration_sec <= 0) {
			ast_cli(a->fd, "Invalid value for duration_sec: '%s'\n", a->argv[4]);
			return CLI_FAILURE;
		}
	}
	duration_ms = duration_sec * 1000;

	strings = generate_random_strings(key_count);
	if (!strings) {
		ast_cli(a->fd, "Failed to generate random strings\n");
		return CLI_FAILURE;
	}

	if (AST_VECTOR_INIT(&vec, key_count)) {
		ast_cli(a->fd, "Failed to initialize vector\n");
		free_random_strings(strings);
		return CLI_FAILURE;
	}

	for (i = 0; i < AST_VECTOR_SIZE(strings); i++) {
		char *dup = ast_strdup(AST_VECTOR_GET(strings, i));

		if (!dup) {
			ast_cli(a->fd, "Failed to add string to vector\n");
			AST_VECTOR_RESET(&vec, ast_free);
			AST_VECTOR_FREE(&vec);
			free_random_strings(strings);
			return CLI_FAILURE;
		}

		/* This can't fail as we're appending to a vector that we know is large enough */
		AST_VECTOR_APPEND(&vec, dup);
	}

	ast_cli(a->fd, "Beginning vector_string_linear performance test (%d keys, %d seconds)\n",
		key_count, duration_sec);

	start = ast_tvnow();
	for (;;) {
		int idx = ast_random() % AST_VECTOR_SIZE(strings);

		target = AST_VECTOR_GET(strings, idx);
		for (i = 0; i < AST_VECTOR_SIZE(&vec); i++) {
			if (!strcmp(AST_VECTOR_GET(&vec, i), target)) {
				break;
			}
		}
		count++;

		if (ast_tvdiff_ms(ast_tvnow(), start) >= duration_ms) {
			break;
		}
	}

	ast_cli(a->fd, "vector_string_linear: %u lookups in %d seconds (%u lookups/sec)\n", count, duration_sec, count / duration_sec);

	AST_VECTOR_RESET(&vec, ast_free);
	AST_VECTOR_FREE(&vec);
	free_random_strings(strings);

	return CLI_SUCCESS;
}

/*!
 * \brief Handles the CLI command for running all container key lookup performance tests
 * \param e The CLI entry
 * \param cmd The command to execute
 * \param a The CLI arguments
 * \return NULL on success, or an error message on failure
 */
static char *handle_cli_container_key_lookup_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "performance test container_key_lookup_all";
		e->usage =
			"Usage: performance test container_key_lookup_all [<num_keys>] [<duration_sec>] [<buckets>]\n"
			"       Run all performance tests.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	/*
	 * We don't care if any of these fail because each will output a message if this occurs,
	 * as well we just pass in the arguments unaltered as none of the tests examine the
	 * full command and they also follow the same format as this CLI command -
	 * "performance test <name> <arguments>" so there is no weird difference in where the
	 * arguments they care about are.
	 */
	handle_cli_ao2_string_hash(e, cmd, a);
	handle_cli_ao2_string_rbtree(e, cmd, a);
	handle_cli_ao2_string_list(e, cmd, a);
	handle_cli_hashtab_string(e, cmd, a);
	handle_cli_list_string(e, cmd, a);
	handle_cli_vector_bsearch_lookup(e, cmd, a);
	handle_cli_vector_linear_lookup(e, cmd, a);

	return CLI_SUCCESS;
}

/*!
 * \brief The CLI commands for performance experimentation testing
 */
static struct ast_cli_entry cli_performance[] = {
	AST_CLI_DEFINE(handle_cli_container_key_lookup_all, "Run all performance tests"),
	AST_CLI_DEFINE(handle_cli_ao2_string_hash, "Test the performance of ao2_container hash string lookups"),
	AST_CLI_DEFINE(handle_cli_ao2_string_rbtree, "Test the performance of ao2_container rbtree string lookups"),
	AST_CLI_DEFINE(handle_cli_ao2_string_list, "Test the performance of ao2_container list string lookups"),
	AST_CLI_DEFINE(handle_cli_hashtab_string, "Test the performance of hashtab string lookups"),
	AST_CLI_DEFINE(handle_cli_list_string, "Test the performance of linked list string lookups"),
	AST_CLI_DEFINE(handle_cli_vector_bsearch_lookup, "Test the performance of vector string binary search lookups"),
	AST_CLI_DEFINE(handle_cli_vector_linear_lookup, "Test the performance of vector string linear search lookups"),
};

/*!
 * \brief Unloads the performance testing module
 * \return 0 on success
 */
static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_performance, ARRAY_LEN(cli_performance));
	return 0;
}

/*!
 * \brief Loads the performance testing module
 * \return AST_MODULE_LOAD_SUCCESS on success
 */
static int load_module(void)
{
	ast_cli_register_multiple(cli_performance, ARRAY_LEN(cli_performance));
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Performance Test Experimentation Module");
