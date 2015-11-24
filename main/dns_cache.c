/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "asterisk/_private.h"
#include "asterisk/sched.h"
#include "asterisk/strings.h"
#include "asterisk/test.h"
#include "asterisk/dns_cache.h"

/*! Start timeout for negative entries */
#define DEFAULT_NEGATIVE_TTL 60

/*! Maximum number of times to extend the timeout */
#define MAX_NEGATIVE_TTL 10

/*! Check for stale items every 5 minutes */
#define CHECK_EXPIRE_TIMEOUT 300000

#define MAX_CACHE_ITEMS 256
#define DNS_CACHE_BUCKETS 64

#define DNS_TEST_EVENT_NOTIFY(state) \
	ast_test_suite_event_notify( \
		state, "Name: %s\r\nNumAttempts: %u\r\nTtl: %lu", \
		item->name, item->num_attempts, item->ttl)

/*!
 * \internal
 * \brief A dns cache item
 */
struct dns_cache_item {
	/*! The time at which this item expires */
	time_t ttl;
	/*! The number of times this item has attempted resolution */
	unsigned int num_attempts;
	/*! The domain name */
	char name[0];
};

/*!
 * \internal
 * \brief Scheduling context for expired entries
 */
static struct ast_sched_context *sched;

/*!
 * \internal
 * \brief Schedule id for expiring cache entries
 *
 * There will exist a single scheduled task that runs in the background around
 * once a minute. This task is responsible for checking the expiration of each
 * entry and removing the entry if it has expired.
 */
static int sched_id = -1;

/*!
 * \internal
 * \brief Scheduler lock used when starting/stopping the task
 */
static ast_mutex_t sched_lock;

/*!
 * \internal
 * \brief The dns cache container
 */
static struct ao2_container *dns_cache;

static int dns_cache_hash_fn(const void *obj, int flags)
{
	const struct dns_cache_item *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->name;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

static int dns_cache_cmp_fn(void *obj, void *arg, int flags)
{
	const struct dns_cache_item *object_left = obj;
	const struct dns_cache_item *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* Not supported by container. */
		ast_assert(0);
		return 0;
	default:
		cmp = 0;
		break;
	}

	return cmp ? 0 : CMP_MATCH;
}

static int dns_cache_scheduler_stop(int del)
{
	SCOPED_MUTEX(lock, &sched_lock);

	if (sched_id < 0 || ao2_container_count(dns_cache)) {
		return 1;
	}

	if (del) {
		AST_SCHED_DEL(sched, sched_id);
	}

	sched_id = -1;
	return 0;
}

static int dns_cache_check_expire_entry(void *obj, void *arg, int flags)
{
	struct dns_cache_item *item = obj;
	return time(NULL) > item->ttl ? CMP_MATCH : 0;
}

static int dns_cache_check_expire(const void *obj)
{
	/* Remove items that have expired */
	ao2_callback(dns_cache, OBJ_MULTIPLE | OBJ_NODATA | OBJ_UNLINK,
		     dns_cache_check_expire_entry, NULL);

	return dns_cache_scheduler_stop(0);
}

static int dns_cache_scheduler_start(void)
{
	SCOPED_MUTEX(lock, &sched_lock);

	if (sched_id != -1 || !ao2_container_count(dns_cache)) {
		return 0;
	}

	sched_id = ast_sched_add(sched, CHECK_EXPIRE_TIMEOUT,
				 dns_cache_check_expire, NULL);
	if (sched_id < 0) {
		ast_log(LOG_ERROR, "Unable to start expiration scheduler\n");
		return -1;
	}
	return 0;
}

static struct dns_cache_item *dns_cache_item_create(const char *name)
{
	int size = strlen(name) + 1;
	struct dns_cache_item *item = ao2_alloc(sizeof(*item) + size, NULL);

	if (!item) {
		ast_log(LOG_ERROR, "Unable to create dns cache entry\n");
		return NULL;
	}

	item->num_attempts = 0;
	ast_copy_string(item->name, name, size);

	if (!ao2_link(dns_cache, item)) {
		ast_log(LOG_ERROR, "Unable to link dns cache entry\n");
		ao2_ref(item, -1);
		return NULL;
	}

	return item;
}

void ast_dns_cache_add_or_update(const char *name)
{
	RAII_VAR(struct dns_cache_item *, item, NULL, ao2_cleanup);

	if (ao2_container_count(dns_cache) > MAX_CACHE_ITEMS) {
		ast_log(LOG_WARNING, "Maximum number of DNS cache items reached\n");
		return;
	}

	if (!(item = ao2_find(dns_cache, name, OBJ_SEARCH_KEY))
	    && !(item = dns_cache_item_create(name))) {
		return;
	}

	ao2_lock(item);
	if (item->num_attempts < MAX_NEGATIVE_TTL) {
		item->ttl = time(NULL) + DEFAULT_NEGATIVE_TTL *	(1 << item->num_attempts);
	}

	++item->num_attempts;
	ao2_unlock(item);

	/* Starts if cache is not empty */
	dns_cache_scheduler_start();

	DNS_TEST_EVENT_NOTIFY("DNS_CACHE_UPDATE");
}

int ast_dns_cache_check(const char *name)
{
	int res;
	struct dns_cache_item *item;

	if (ast_strlen_zero(name)) {
		return 0;
	}

	if (!(item = ao2_find(dns_cache, name, OBJ_SEARCH_KEY))) {
		return -1;
	}

	ao2_lock(item);
	res = time(NULL) > item->ttl;
	ao2_unlock(item);

	if (!res) {
		DNS_TEST_EVENT_NOTIFY("DNS_CACHE_HIT");
	}

	ao2_ref(item, -1);

	return res;
}

void ast_dns_cache_delete(const char *name)
{
	ao2_find(dns_cache, name, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);

	/* Stops if cache is empty */
	dns_cache_scheduler_stop(1);
}

static void dns_cache_delete_all(void)
{
	ao2_callback(dns_cache, OBJ_MULTIPLE | OBJ_UNLINK | OBJ_NODATA, NULL, NULL);

	dns_cache_scheduler_stop(1);
}

static int cli_dns_cache_show_item(void *obj, void *arg, int flags)
{
	struct ast_cli_args *a = arg;
	struct dns_cache_item *item = obj;
	time_t now = time(NULL);
	time_t diff = 0;

	if (now < item->ttl) {
		diff = item->ttl - now;
	}

	ast_cli(a->fd, "%-50.50s %-10lu %-10u\n", item->name, diff, item->num_attempts);
	return 0;
}

static char *cli_dns_cache_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define DNS_CACHE_FORMAT  "%-50.50s %-10.10s %-10.10s\n"
	switch (cmd) {
	case CLI_INIT:
		e->command = "dns cache show";
		e->usage =
			"Usage: dns cache show\n"
			"       Displays the DNS cache.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 4) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, DNS_CACHE_FORMAT, "Domain name", "TTL (sec)", "# Attempts");
	ast_cli(a->fd, DNS_CACHE_FORMAT, "-----------", "---------", "----------");
	ao2_callback(dns_cache, OBJ_MULTIPLE | OBJ_NODATA,
		     cli_dns_cache_show_item, a);

	return CLI_SUCCESS;
#undef DNS_CACHE_FORMAT
}

static char *cli_dns_cache_complete(const char *word, int state)
{
	char *res = NULL;
	int wordlen = strlen(word);
	int which = 0;
	struct dns_cache_item *item;
	struct ao2_iterator i = ao2_iterator_init(dns_cache, 0);

	while ((item = ao2_iterator_next(&i)) && !res) {
		if (!strncasecmp(word, item->name, wordlen) && ++which > state) {
			res = ast_strdup(item->name);
		}
		ao2_ref(item, -1);
	}
	ao2_iterator_destroy(&i);

	return res;
}

static char *cli_dns_cache_delete(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct dns_cache_item *item;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dns cache delete";
		e->usage =
			"Usage: dns cache delete [all]|<name>\n"
			"       Remove item(s) from the DNS cache.\n";
		return NULL;
	case CLI_GENERATE:
		return a->pos == 3 ? cli_dns_cache_complete(a->word, a->n) : NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[3], "all")) {
		dns_cache_delete_all();
		return CLI_SUCCESS;
	}

	if (!(item = ao2_find(dns_cache, a->argv[3], OBJ_SEARCH_KEY | OBJ_UNLINK))) {
		ast_cli(a->fd, "'%s' not found in the DNS cache.\n", a->argv[3]);
		return CLI_FAILURE;
	}

	ao2_ref(item, -1);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_dns_cache[] = {
	AST_CLI_DEFINE(cli_dns_cache_show, "Show items in the DNS cache"),
	AST_CLI_DEFINE(cli_dns_cache_delete, "Delete an item in the DNS cache")
};

static void dns_cache_destroy(void)
{
	ast_cli_unregister_multiple(
		cli_dns_cache, ARRAY_LEN(cli_dns_cache));

	dns_cache_delete_all();
	ao2_cleanup(dns_cache);

	if (sched) {
		ast_sched_context_destroy(sched);
	}
}

int ast_dns_cache_create(void)
{
	dns_cache = ao2_container_alloc(
		DNS_CACHE_BUCKETS, dns_cache_hash_fn,
		dns_cache_cmp_fn);

	if (!dns_cache) {
		ast_log(LOG_ERROR, "Unable to create DNS cache\n");
		return -1;
	}

	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Unable to create DNS cache scheduler context\n");
		dns_cache_destroy();
		return -1;
	}

	if (ast_sched_start_thread(sched)) {
		ast_log(LOG_ERROR, "Unable to start DNS cache scheduler thread\n");
		dns_cache_destroy();
		return -1;
	}

	if (ast_cli_register_multiple(
		    cli_dns_cache, ARRAY_LEN(cli_dns_cache))) {
		ast_log(LOG_ERROR, "Unable to register DNS cache cli commands\n");
		dns_cache_destroy();
		return -1;
	}

	ast_register_cleanup(dns_cache_destroy);

	return 0;
}
