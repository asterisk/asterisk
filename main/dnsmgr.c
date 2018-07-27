/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005-2006, Kevin P. Fleming
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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
 * \brief Background DNS update manager
 *
 * \author Kevin P. Fleming <kpfleming@digium.com>
 *
 * \bug There is a minor race condition.  In the event that an IP address
 * of a dnsmgr managed host changes, there is the potential for the consumer
 * of that address to access the in_addr data at the same time that the dnsmgr
 * thread is in the middle of updating it to the new address.
 */


/*! \li \ref dnsmgr.c uses the configuration file \ref dnsmgr.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page dnsmgr.conf dnsmgr.conf
 * \verbinclude dnsmgr.conf.sample
 */


/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include <regex.h>
#include <signal.h>

#include "asterisk/module.h"
#include "asterisk/dnsmgr.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include "asterisk/sched.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/acl.h"

static struct ast_sched_context *sched;
static int refresh_sched = -1;
static pthread_t refresh_thread = AST_PTHREADT_NULL;

struct ast_dnsmgr_entry {
	/*! where we will store the resulting IP address and port number */
	struct ast_sockaddr *result;
	/*! SRV record to lookup, if provided. Composed of service, protocol, and domain name: _Service._Proto.Name */
	char *service;
	/*! Address family to filter DNS responses. */
	unsigned int family;
	/*! Set to 1 if the entry changes */
	unsigned int changed:1;
	/*! Data to pass back to update_func */
	void *data;
	/*! The callback function to execute on address update */
	dns_update_func update_func;
	ast_mutex_t lock;
	AST_RWLIST_ENTRY(ast_dnsmgr_entry) list;
	/*! just 1 here, but we use calloc to allocate the correct size */
	char name[1];
};

static AST_RWLIST_HEAD_STATIC(entry_list, ast_dnsmgr_entry);

AST_MUTEX_DEFINE_STATIC(refresh_lock);

#define REFRESH_DEFAULT 300

static int enabled;
static int refresh_interval;

struct refresh_info {
	struct entry_list *entries;
	int verbose;
	unsigned int regex_present:1;
	regex_t filter;
};

static struct refresh_info master_refresh_info = {
	.entries = &entry_list,
	.verbose = 0,
};

struct ast_dnsmgr_entry *ast_dnsmgr_get_family(const char *name, struct ast_sockaddr *result, const char *service, unsigned int family)
{
	struct ast_dnsmgr_entry *entry;
	int total_size = sizeof(*entry) + strlen(name) + (service ? strlen(service) + 1 : 0);

	if (!result || ast_strlen_zero(name) || !(entry = ast_calloc(1, total_size))) {
		return NULL;
	}

	entry->result = result;
	ast_mutex_init(&entry->lock);
	strcpy(entry->name, name);
	if (service) {
		entry->service = ((char *) entry) + sizeof(*entry) + strlen(name);
		strcpy(entry->service, service);
	}
	entry->family = family;

	AST_RWLIST_WRLOCK(&entry_list);
	AST_RWLIST_INSERT_HEAD(&entry_list, entry, list);
	AST_RWLIST_UNLOCK(&entry_list);

	return entry;
}

struct ast_dnsmgr_entry *ast_dnsmgr_get(const char *name, struct ast_sockaddr *result, const char *service)
{
	return ast_dnsmgr_get_family(name, result, service, 0);
}

void ast_dnsmgr_release(struct ast_dnsmgr_entry *entry)
{
	if (!entry) {
		return;
	}

	AST_RWLIST_WRLOCK(&entry_list);
	AST_RWLIST_REMOVE(&entry_list, entry, list);
	AST_RWLIST_UNLOCK(&entry_list);
	ast_debug(6, "removing dns manager for '%s'\n", entry->name);

	ast_mutex_destroy(&entry->lock);
	ast_free(entry);
}

static int internal_dnsmgr_lookup(const char *name, struct ast_sockaddr *result, struct ast_dnsmgr_entry **dnsmgr, const char *service, dns_update_func func, void *data)
{
	unsigned int family;

	if (ast_strlen_zero(name) || !result || !dnsmgr) {
		return -1;
	}

	if (*dnsmgr && !strcasecmp((*dnsmgr)->name, name)) {
		return 0;
	}

	/* Lookup address family filter. */
	family = result->ss.ss_family;

	/*
	 * If it's actually an IP address and not a name, there's no
	 * need for a managed lookup.
	 */
	if (ast_sockaddr_parse(result, name, PARSE_PORT_FORBID)) {
		return 0;
	}

	ast_debug(6, "doing dnsmgr_lookup for '%s'\n", name);

	/* do a lookup now but add a manager so it will automagically get updated in the background */
	ast_get_ip_or_srv(result, name, service);

	/* if dnsmgr is not enable don't bother adding an entry */
	if (!enabled) {
		return 0;
	}

	ast_debug(6, "adding dns manager for '%s'\n", name);
	*dnsmgr = ast_dnsmgr_get_family(name, result, service, family);
	(*dnsmgr)->update_func = func;
	(*dnsmgr)->data = data;
	return !*dnsmgr;
}

int ast_dnsmgr_lookup(const char *name, struct ast_sockaddr *result, struct ast_dnsmgr_entry **dnsmgr, const char *service)
{
	return internal_dnsmgr_lookup(name, result, dnsmgr, service, NULL, NULL);
}

int ast_dnsmgr_lookup_cb(const char *name, struct ast_sockaddr *result, struct ast_dnsmgr_entry **dnsmgr, const char *service, dns_update_func func, void *data)
{
	return internal_dnsmgr_lookup(name, result, dnsmgr, service, func, data);
}

/*
 * Refresh a dnsmgr entry
 */
static int dnsmgr_refresh(struct ast_dnsmgr_entry *entry, int verbose)
{
	struct ast_sockaddr tmp = { .len = 0, };
	int changed = 0;

	ast_mutex_lock(&entry->lock);

	ast_debug(6, "refreshing '%s'\n", entry->name);

	tmp.ss.ss_family = entry->family;
	if (!ast_get_ip_or_srv(&tmp, entry->name, entry->service)) {
		if (!ast_sockaddr_port(&tmp)) {
			ast_sockaddr_set_port(&tmp, ast_sockaddr_port(entry->result));
		}
		if (ast_sockaddr_cmp(&tmp, entry->result)) {
			const char *old_addr = ast_strdupa(ast_sockaddr_stringify(entry->result));
			const char *new_addr = ast_strdupa(ast_sockaddr_stringify(&tmp));

			if (entry->update_func) {
				entry->update_func(entry->result, &tmp, entry->data);
			} else {
				ast_log(LOG_NOTICE, "dnssrv: host '%s' changed from %s to %s\n",
						entry->name, old_addr, new_addr);

				ast_sockaddr_copy(entry->result, &tmp);
				changed = entry->changed = 1;
			}
		}
	}

	ast_mutex_unlock(&entry->lock);

	return changed;
}

int ast_dnsmgr_refresh(struct ast_dnsmgr_entry *entry)
{
	return dnsmgr_refresh(entry, 0);
}

/*
 * Check if dnsmgr entry has changed from since last call to this function
 */
int ast_dnsmgr_changed(struct ast_dnsmgr_entry *entry)
{
	int changed;

	ast_mutex_lock(&entry->lock);

	changed = entry->changed;
	entry->changed = 0;

	ast_mutex_unlock(&entry->lock);

	return changed;
}

static void *do_refresh(void *data)
{
	for (;;) {
		pthread_testcancel();
		usleep((ast_sched_wait(sched)*1000));
		pthread_testcancel();
		ast_sched_runq(sched);
	}
	return NULL;
}

static int refresh_list(const void *data)
{
	struct refresh_info *info = (struct refresh_info *)data;
	struct ast_dnsmgr_entry *entry;

	/* if a refresh or reload is already in progress, exit now */
	if (ast_mutex_trylock(&refresh_lock)) {
		if (info->verbose) {
			ast_log(LOG_WARNING, "DNS Manager refresh already in progress.\n");
		}
		return -1;
	}

	ast_debug(6, "Refreshing DNS lookups.\n");
	AST_RWLIST_RDLOCK(info->entries);
	AST_RWLIST_TRAVERSE(info->entries, entry, list) {
		if (info->regex_present && regexec(&info->filter, entry->name, 0, NULL, 0)) {
			continue;
		}

		dnsmgr_refresh(entry, info->verbose);
	}
	AST_RWLIST_UNLOCK(info->entries);

	ast_mutex_unlock(&refresh_lock);

	/* automatically reschedule based on the interval */
	return refresh_interval * 1000;
}

void dnsmgr_start_refresh(void)
{
	if (refresh_sched > -1) {
		AST_SCHED_DEL(sched, refresh_sched);
		refresh_sched = ast_sched_add_variable(sched, 100, refresh_list, &master_refresh_info, 1);
	}
}

static int do_reload(int loading);

static char *handle_cli_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "dnsmgr reload";
		e->usage =
			"Usage: dnsmgr reload\n"
			"       Reloads the DNS manager configuration.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc > 2) {
		return CLI_SHOWUSAGE;
	}

	do_reload(0);
	return CLI_SUCCESS;
}

static char *handle_cli_refresh(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct refresh_info info = {
		.entries = &entry_list,
		.verbose = 1,
	};
	switch (cmd) {
	case CLI_INIT:
		e->command = "dnsmgr refresh";
		e->usage =
			"Usage: dnsmgr refresh [pattern]\n"
			"       Peforms an immediate refresh of the managed DNS entries.\n"
			"       Optional regular expression pattern is used to filter the entries to refresh.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (!enabled) {
		ast_cli(a->fd, "DNS Manager is disabled.\n");
		return 0;
	}

	if (a->argc > 3) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 3) {
		if (regcomp(&info.filter, a->argv[2], REG_EXTENDED | REG_NOSUB)) {
			return CLI_SHOWUSAGE;
		} else {
			info.regex_present = 1;
		}
	}

	refresh_list(&info);

	if (info.regex_present) {
		regfree(&info.filter);
	}

	return CLI_SUCCESS;
}

static char *handle_cli_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int count = 0;
	struct ast_dnsmgr_entry *entry;
	switch (cmd) {
	case CLI_INIT:
		e->command = "dnsmgr status";
		e->usage =
			"Usage: dnsmgr status\n"
			"       Displays the DNS manager status.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 2) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "DNS Manager: %s\n", enabled ? "enabled" : "disabled");
	ast_cli(a->fd, "Refresh Interval: %d seconds\n", refresh_interval);
	AST_RWLIST_RDLOCK(&entry_list);
	AST_RWLIST_TRAVERSE(&entry_list, entry, list)
		count++;
	AST_RWLIST_UNLOCK(&entry_list);
	ast_cli(a->fd, "Number of entries: %d\n", count);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_reload = AST_CLI_DEFINE(handle_cli_reload, "Reloads the DNS manager configuration");
static struct ast_cli_entry cli_refresh = AST_CLI_DEFINE(handle_cli_refresh, "Performs an immediate refresh");
static struct ast_cli_entry cli_status = AST_CLI_DEFINE(handle_cli_status, "Display the DNS manager status");

static int unload_module(void)
{
	ast_cli_unregister(&cli_reload);
	ast_cli_unregister(&cli_status);
	ast_cli_unregister(&cli_refresh);

	/* Destroy refresh thread. */
	ast_mutex_lock(&refresh_lock);
	if (refresh_thread != AST_PTHREADT_NULL) {
		/* wake up the thread so it will exit */
		pthread_cancel(refresh_thread);
		pthread_kill(refresh_thread, SIGURG);
		pthread_join(refresh_thread, NULL);
		refresh_thread = AST_PTHREADT_NULL;
	}
	ast_mutex_unlock(&refresh_lock);

	ast_sched_context_destroy(sched);

	return 0;
}

static int load_module(void)
{
	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Unable to create schedule context.\n");
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_cli_register(&cli_reload);
	ast_cli_register(&cli_status);
	ast_cli_register(&cli_refresh);

	return do_reload(1) ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	return do_reload(0);
}

static int do_reload(int loading)
{
	struct ast_config *config;
	struct ast_variable *v;
	struct ast_flags config_flags = { loading ? 0 : CONFIG_FLAG_FILEUNCHANGED };
	int interval;
	int was_enabled;

	if ((config = ast_config_load2("dnsmgr.conf", "dnsmgr", config_flags)) == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	/* ensure that no refresh cycles run while the reload is in progress */
	ast_mutex_lock(&refresh_lock);

	/* reset defaults in preparation for reading config file */
	refresh_interval = REFRESH_DEFAULT;
	was_enabled = enabled;
	enabled = 0;

	if (config == CONFIG_STATUS_FILEMISSING || config == CONFIG_STATUS_FILEINVALID) {
		ast_mutex_unlock(&refresh_lock);
		return 0;
	}

	AST_SCHED_DEL(sched, refresh_sched);

	for (v = ast_variable_browse(config, "general"); v; v = v->next) {
		if (!strcasecmp(v->name, "enable")) {
			enabled = ast_true(v->value);
		} else if (!strcasecmp(v->name, "refreshinterval")) {
			if (sscanf(v->value, "%30d", &interval) < 1) {
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", v->value);
			} else if (interval < 0) {
				ast_log(LOG_WARNING, "Invalid refresh interval '%d' specified, using default\n", interval);
			} else {
				refresh_interval = interval;
			}
		}
	}
	ast_config_destroy(config);

	if (enabled && refresh_interval) {
		ast_log(LOG_NOTICE, "Managed DNS entries will be refreshed every %d seconds.\n", refresh_interval);
	}

	/* if this reload enabled the manager, create the background thread
	   if it does not exist */
	if (enabled) {
		if (!was_enabled && (refresh_thread == AST_PTHREADT_NULL)) {
			if (ast_pthread_create_background(&refresh_thread, NULL, do_refresh, NULL) < 0) {
				ast_log(LOG_ERROR, "Unable to start refresh thread.\n");
			}
		}
		/* make a background refresh happen right away */
		refresh_sched = ast_sched_add_variable(sched, 100, refresh_list, &master_refresh_info, 1);
	/* if this reload disabled the manager and there is a background thread, kill it */
	} else if (!enabled && was_enabled && (refresh_thread != AST_PTHREADT_NULL)) {
		/* wake up the thread so it will exit */
		pthread_cancel(refresh_thread);
		pthread_kill(refresh_thread, SIGURG);
		pthread_join(refresh_thread, NULL);
		refresh_thread = AST_PTHREADT_NULL;
	}

	ast_mutex_unlock(&refresh_lock);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "DNS Manager",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CORE,
	.requires = "extconfig",
);
