/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Bridge PJPROJECT logging to Asterisk logging.
 * \author David M. Lee, II <dlee@digium.com>
 *
 * PJPROJECT logging doesn't exactly match Asterisk logging, but mapping the two is
 * not too bad. PJPROJECT log levels are identified by a single int. Limits are
 * not specified by PJPROJECT, but their implementation used 1 through 6.
 *
 * The mapping is as follows:
 *  - 0: LOG_ERROR
 *  - 1: LOG_ERROR
 *  - 2: LOG_WARNING
 *  - 3 and above: equivalent to ast_debug(level, ...) for res_pjproject.so
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_pjproject" language="en_US">
		<synopsis>pjproject common configuration</synopsis>
		<configFile name="pjproject.conf">
			<configObject name="startup">
				<synopsis>Asterisk startup time options for PJPROJECT</synopsis>
				<description>
					<note><para>The id of this object, as well as its type, must be
					'startup' or it won't be found.</para></note>
				</description>
				<configOption name="type">
					<synopsis>Must be of type 'startup'.</synopsis>
				</configOption>
				<configOption name="log_level" default="2">
					<synopsis>Initial maximum pjproject logging level to log.</synopsis>
					<description>
						<para>Valid values are: 0-6, and default</para>
					<note><para>
						This option is needed very early in the startup process
						so it can only be read from config files because the
						modules for other methods have not been loaded yet.
					</para></note>
					</description>
				</configOption>
			</configObject>
			<configObject name="log_mappings">
				<synopsis>PJPROJECT to Asterisk Log Level Mapping</synopsis>
				<description><para>Warnings and errors in the pjproject libraries are generally handled
					by Asterisk.  In many cases, Asterisk wouldn't even consider them to
					be warnings or errors so the messages emitted by pjproject directly
					are either superfluous or misleading.  The 'log_mappings'
					object allows mapping the pjproject levels to Asterisk levels, or nothing.
					</para>
					<note><para>The id of this object, as well as its type, must be
					'log_mappings' or it won't be found.</para></note>
				</description>
				<configOption name="type">
					<synopsis>Must be of type 'log_mappings'.</synopsis>
				</configOption>
				<configOption name="asterisk_error" default="0,1">
					<synopsis>A comma separated list of pjproject log levels to map to Asterisk LOG_ERROR.</synopsis>
				</configOption>
				<configOption name="asterisk_warning" default="2">
					<synopsis>A comma separated list of pjproject log levels to map to Asterisk LOG_WARNING.</synopsis>
				</configOption>
				<configOption name="asterisk_notice" default="">
					<synopsis>A comma separated list of pjproject log levels to map to Asterisk LOG_NOTICE.</synopsis>
				</configOption>
				<configOption name="asterisk_debug" default="3,4,5,6">
					<synopsis>A comma separated list of pjproject log levels to map to Asterisk LOG_DEBUG.</synopsis>
				</configOption>
				<configOption name="asterisk_verbose" default="">
					<synopsis>A comma separated list of pjproject log levels to map to Asterisk LOG_VERBOSE.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

#include "asterisk.h"

#include <stdarg.h>
#include <pjlib.h>
#include <pjsip.h>
#include <pj/log.h>

#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/res_pjproject.h"
#include "asterisk/vector.h"
#include "asterisk/sorcery.h"
#include "asterisk/test.h"
#include "asterisk/netsock2.h"

static struct ast_sorcery *pjproject_sorcery;
static pj_log_func *log_cb_orig;
static unsigned decor_orig;

static AST_VECTOR(buildopts, char *) buildopts;

/*! Protection from other log intercept instances.  There can be only one at a time. */
AST_MUTEX_DEFINE_STATIC(pjproject_log_intercept_lock);

struct pjproject_log_intercept_data {
	pthread_t thread;
	int fd;
};

static struct pjproject_log_intercept_data pjproject_log_intercept = {
	.thread = AST_PTHREADT_NULL,
	.fd = -1,
};

struct log_mappings {
	/*! Sorcery object details */
	SORCERY_OBJECT(details);
	/*! These are all comma-separated lists of pjproject log levels */
	AST_DECLARE_STRING_FIELDS(
		/*! pjproject log levels mapped to Asterisk ERROR */
		AST_STRING_FIELD(asterisk_error);
		/*! pjproject log levels mapped to Asterisk WARNING */
		AST_STRING_FIELD(asterisk_warning);
		/*! pjproject log levels mapped to Asterisk NOTICE */
		AST_STRING_FIELD(asterisk_notice);
		/*! pjproject log levels mapped to Asterisk VERBOSE */
		AST_STRING_FIELD(asterisk_verbose);
		/*! pjproject log levels mapped to Asterisk DEBUG */
		AST_STRING_FIELD(asterisk_debug);
	);
};

static struct log_mappings *default_log_mappings;

static struct log_mappings *get_log_mappings(void)
{
	struct log_mappings *mappings;

	mappings = ast_sorcery_retrieve_by_id(pjproject_sorcery, "log_mappings", "log_mappings");
	if (!mappings) {
		return ao2_bump(default_log_mappings);
	}

	return mappings;
}

#define __LOG_SUPPRESS -1

static int get_log_level(int pj_level)
{
	int mapped_level;
	unsigned char l;
	struct log_mappings *mappings;

	mappings = get_log_mappings();
	if (!mappings) {
		return __LOG_ERROR;
	}

	l = '0' + fmin(pj_level, 9);

	if (strchr(mappings->asterisk_error, l)) {
		mapped_level = __LOG_ERROR;
	} else if (strchr(mappings->asterisk_warning, l)) {
		mapped_level = __LOG_WARNING;
	} else if (strchr(mappings->asterisk_notice, l)) {
		mapped_level = __LOG_NOTICE;
	} else if (strchr(mappings->asterisk_verbose, l)) {
		mapped_level = __LOG_VERBOSE;
	} else if (strchr(mappings->asterisk_debug, l)) {
		mapped_level = __LOG_DEBUG;
	} else {
		mapped_level = __LOG_SUPPRESS;
	}

	ao2_ref(mappings, -1);
	return mapped_level;
}

static void log_forwarder(int level, const char *data, int len)
{
	int ast_level;
	/* PJPROJECT doesn't provide much in the way of source info */
	const char * log_source = "pjproject";
	int log_line = 0;
	const char *log_func = "<?>";

	if (pjproject_log_intercept.fd != -1
		&& pjproject_log_intercept.thread == pthread_self()) {
		/*
		 * We are handling a CLI command intercepting PJPROJECT
		 * log output.
		 */
		ast_cli(pjproject_log_intercept.fd, "%s\n", data);
		return;
	}

	ast_level = get_log_level(level);

	if (ast_level == __LOG_SUPPRESS) {
		return;
	}

	/* PJPROJECT uses indention to indicate function call depth. We'll prepend
	 * log statements with a tab so they'll have a better shot at lining
	 * up */
	ast_log(ast_level, log_source, log_line, log_func, "\t%s\n", data);
}

static void capture_buildopts_cb(int level, const char *data, int len)
{
	char *dup;

	if (strstr(data, "Teluu") || strstr(data, "Dumping")) {
		return;
	}

	dup = ast_strdup(ast_skip_blanks(data));
	if (dup && AST_VECTOR_ADD_SORTED(&buildopts, dup, strcmp)) {
		ast_free(dup);
	}
}

#pragma GCC diagnostic ignored "-Wformat-nonliteral"
int ast_pjproject_get_buildopt(char *option, char *format_string, ...)
{
	int res = 0;
	char *format_temp;
	int i;

	format_temp = ast_alloca(strlen(option) + strlen(" : ") + strlen(format_string) + 1);
	sprintf(format_temp, "%s : %s", option, format_string);

	for (i = 0; i < AST_VECTOR_SIZE(&buildopts); i++) {
		va_list arg_ptr;
		va_start(arg_ptr, format_string);
		res = vsscanf(AST_VECTOR_GET(&buildopts, i), format_temp, arg_ptr);
		va_end(arg_ptr);
		if (res) {
			break;
		}
	}

	return res;
}
#pragma GCC diagnostic warning "-Wformat-nonliteral"

void ast_pjproject_log_intercept_begin(int fd)
{
	/* Protect from other CLI instances trying to do this at the same time. */
	ast_mutex_lock(&pjproject_log_intercept_lock);

	pjproject_log_intercept.thread = pthread_self();
	pjproject_log_intercept.fd = fd;
}

void ast_pjproject_log_intercept_end(void)
{
	pjproject_log_intercept.fd = -1;
	pjproject_log_intercept.thread = AST_PTHREADT_NULL;

	ast_mutex_unlock(&pjproject_log_intercept_lock);
}

static char *handle_pjproject_show_buildopts(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjproject show buildopts";
		e->usage =
			"Usage: pjproject show buildopts\n"
			"       Show the compile time config of the pjproject that Asterisk is\n"
			"       running against.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "PJPROJECT compile time config currently running against:\n");

	for (i = 0; i < AST_VECTOR_SIZE(&buildopts); i++) {
		ast_cli(a->fd, "%s\n", AST_VECTOR_GET(&buildopts, i));
	}

	return CLI_SUCCESS;
}

static void mapping_destroy(void *object)
{
	struct log_mappings *mappings = object;

	ast_string_field_free_memory(mappings);
}

static void *mapping_alloc(const char *name)
{
	struct log_mappings *mappings = ast_sorcery_generic_alloc(sizeof(*mappings), mapping_destroy);
	if (!mappings) {
		return NULL;
	}
	ast_string_field_init(mappings, 128);

	return mappings;
}

static char *handle_pjproject_show_log_mappings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_variable *objset;
	struct ast_variable *i;
	struct log_mappings *mappings;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjproject show log mappings";
		e->usage =
			"Usage: pjproject show log mappings\n"
			"       Show pjproject to Asterisk log mappings\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "PJPROJECT to Asterisk log mappings:\n");
	ast_cli(a->fd, "Asterisk Level   : PJPROJECT log levels\n");

	mappings = get_log_mappings();
	if (!mappings) {
		ast_log(LOG_ERROR, "Unable to retrieve pjproject log_mappings\n");
		return CLI_SUCCESS;
	}

	objset = ast_sorcery_objectset_create(pjproject_sorcery, mappings);
	if (!objset) {
		ao2_ref(mappings, -1);
		return CLI_SUCCESS;
	}

	for (i = objset; i; i = i->next) {
		ast_cli(a->fd, "%-16s : %s\n", i->name, i->value);
	}
	ast_variables_destroy(objset);

	ao2_ref(mappings, -1);
	return CLI_SUCCESS;
}

struct max_pjproject_log_level_check {
	/*!
	 * Compile time sanity check to determine if
	 * MAX_PJ_LOG_MAX_LEVEL matches CLI syntax.
	 */
	char check[1 / (6 == MAX_PJ_LOG_MAX_LEVEL)];
};

static char *handle_pjproject_set_log_level(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int level_new;
	int level_old;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjproject set log level {default|0|1|2|3|4|5|6}";
		e->usage =
			"Usage: pjproject set log level {default|<level>}\n"
			"\n"
			"       Set the maximum active pjproject logging level.\n"
			"       See pjproject.conf.sample for additional information\n"
			"       about the various levels pjproject uses.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[4], "default")) {
		level_new = DEFAULT_PJ_LOG_MAX_LEVEL;
	} else {
		if (sscanf(a->argv[4], "%30d", &level_new) != 1
			|| level_new < 0 || MAX_PJ_LOG_MAX_LEVEL < level_new) {
			return CLI_SHOWUSAGE;
		}
	}

	/* Update pjproject logging level */
	if (ast_pjproject_max_log_level < level_new) {
		level_new = ast_pjproject_max_log_level;
		ast_cli(a->fd,
			"Asterisk built or linked with pjproject PJ_LOG_MAX_LEVEL=%d.\n"
			"Lowering request to the max supported level.\n",
			ast_pjproject_max_log_level);
	}
	level_old = ast_option_pjproject_log_level;
	if (level_old == level_new) {
		ast_cli(a->fd, "pjproject log level is still %d.\n", level_old);
	} else {
		ast_cli(a->fd, "pjproject log level was %d and is now %d.\n",
			level_old, level_new);
		ast_option_pjproject_log_level = level_new;
		pj_log_set_level(level_new);
	}

	return CLI_SUCCESS;
}

static char *handle_pjproject_show_log_level(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "pjproject show log level";
		e->usage =
			"Usage: pjproject show log level\n"
			"\n"
			"       Show the current maximum active pjproject logging level.\n"
			"       See pjproject.conf.sample for additional information\n"
			"       about the various levels pjproject uses.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "pjproject log level is %d.%s\n",
		ast_option_pjproject_log_level,
		ast_option_pjproject_log_level == DEFAULT_PJ_LOG_MAX_LEVEL ? " (default)" : "");

	return CLI_SUCCESS;
}

static struct ast_cli_entry pjproject_cli[] = {
	AST_CLI_DEFINE(handle_pjproject_set_log_level, "Set the maximum active pjproject logging level"),
	AST_CLI_DEFINE(handle_pjproject_show_buildopts, "Show the compiled config of the pjproject in use"),
	AST_CLI_DEFINE(handle_pjproject_show_log_mappings, "Show pjproject to Asterisk log mappings"),
	AST_CLI_DEFINE(handle_pjproject_show_log_level, "Show the maximum active pjproject logging level"),
};

void ast_pjproject_caching_pool_init(pj_caching_pool *cp,
	const pj_pool_factory_policy *policy, pj_size_t max_capacity)
{
	/* Passing a max_capacity of zero disables caching pools */
	pj_caching_pool_init(cp, policy, ast_option_pjproject_cache_pools ? max_capacity : 0);
}

void ast_pjproject_caching_pool_destroy(pj_caching_pool *cp)
{
	pj_caching_pool_destroy(cp);
}

int ast_sockaddr_to_pj_sockaddr(const struct ast_sockaddr *addr, pj_sockaddr *pjaddr)
{
	if (addr->ss.ss_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *) &addr->ss;
		pjaddr->ipv4.sin_family = pj_AF_INET();
#ifdef HAVE_PJPROJECT_BUNDLED
		pjaddr->ipv4.sin_addr = sin->sin_addr;
#else
		pjaddr->ipv4.sin_addr.s_addr = sin->sin_addr.s_addr;
#endif
		pjaddr->ipv4.sin_port   = sin->sin_port;
	} else if (addr->ss.ss_family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *) &addr->ss;
		pjaddr->ipv6.sin6_family   = pj_AF_INET6();
		pjaddr->ipv6.sin6_port     = sin->sin6_port;
		pjaddr->ipv6.sin6_flowinfo = sin->sin6_flowinfo;
		pjaddr->ipv6.sin6_scope_id = sin->sin6_scope_id;
		memcpy(&pjaddr->ipv6.sin6_addr, &sin->sin6_addr, sizeof(pjaddr->ipv6.sin6_addr));
	} else {
		memset(pjaddr, 0, sizeof(*pjaddr));
		return -1;
	}
	return 0;
}

int ast_sockaddr_from_pj_sockaddr(struct ast_sockaddr *addr, const pj_sockaddr *pjaddr)
{
	if (pjaddr->addr.sa_family == pj_AF_INET()) {
		struct sockaddr_in *sin = (struct sockaddr_in *) &addr->ss;
		sin->sin_family = AF_INET;
#ifdef HAVE_PJPROJECT_BUNDLED
		sin->sin_addr = pjaddr->ipv4.sin_addr;
#else
		sin->sin_addr.s_addr = pjaddr->ipv4.sin_addr.s_addr;
#endif
		sin->sin_port   = pjaddr->ipv4.sin_port;
		addr->len = sizeof(struct sockaddr_in);
	} else if (pjaddr->addr.sa_family == pj_AF_INET6()) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *) &addr->ss;
		sin->sin6_family   = AF_INET6;
		sin->sin6_port     = pjaddr->ipv6.sin6_port;
		sin->sin6_flowinfo = pjaddr->ipv6.sin6_flowinfo;
		sin->sin6_scope_id = pjaddr->ipv6.sin6_scope_id;
		memcpy(&sin->sin6_addr, &pjaddr->ipv6.sin6_addr, sizeof(sin->sin6_addr));
		addr->len = sizeof(struct sockaddr_in6);
	} else {
		memset(addr, 0, sizeof(*addr));
		return -1;
	}
	return 0;
}

#ifdef TEST_FRAMEWORK
static void fill_with_garbage(void *x, ssize_t len)
{
	unsigned char *w = x;
	while (len > 0) {
		int r = ast_random();
		memcpy(w, &r, len > sizeof(r) ? sizeof(r) : len);
		w += sizeof(r);
		len -= sizeof(r);
	}
}

AST_TEST_DEFINE(ast_sockaddr_to_pj_sockaddr_test)
{
	char *candidates[] = {
		"127.0.0.1:5555",
		"[::]:4444",
		"192.168.0.100:0",
		"[fec0::1:80]:0",
		"[fec0::1]:80",
		NULL,
	}, **candidate = candidates;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_sockaddr_to_pj_sockaddr_test";
		info->category = "/res/res_pjproject/";
		info->summary = "Validate conversions from an ast_sockaddr to a pj_sockaddr";
		info->description = "This test converts an ast_sockaddr to a pj_sockaddr and validates\n"
			"that the two evaluate to the same string when formatted.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	while (*candidate) {
		struct ast_sockaddr addr = {{0,}};
		pj_sockaddr pjaddr;
		char buffer[512];

		fill_with_garbage(&pjaddr, sizeof(pj_sockaddr));

		if (!ast_sockaddr_parse(&addr, *candidate, 0)) {
			ast_test_status_update(test, "Failed to parse candidate IP: %s\n", *candidate);
			return AST_TEST_FAIL;
		}

		if (ast_sockaddr_to_pj_sockaddr(&addr, &pjaddr)) {
			ast_test_status_update(test, "Failed to convert ast_sockaddr to pj_sockaddr: %s\n", *candidate);
			return AST_TEST_FAIL;
		}

		pj_sockaddr_print(&pjaddr, buffer, sizeof(buffer), 1 | 2);

		if (strcmp(*candidate, buffer)) {
			ast_test_status_update(test, "Converted sockaddrs do not match: \"%s\" and \"%s\"\n",
				*candidate,
				buffer);
			return AST_TEST_FAIL;
		}

		candidate++;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(ast_sockaddr_from_pj_sockaddr_test)
{
	char *candidates[] = {
		"127.0.0.1:5555",
		"[::]:4444",
		"192.168.0.100:0",
		"[fec0::1:80]:0",
		"[fec0::1]:80",
		NULL,
	}, **candidate = candidates;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_sockaddr_from_pj_sockaddr_test";
		info->category = "/res/res_pjproject/";
		info->summary = "Validate conversions from a pj_sockaddr to an ast_sockaddr";
		info->description = "This test converts a pj_sockaddr to an ast_sockaddr and validates\n"
			"that the two evaluate to the same string when formatted.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	while (*candidate) {
		struct ast_sockaddr addr = {{0,}};
		pj_sockaddr pjaddr;
		pj_str_t t;
		char buffer[512];

		fill_with_garbage(&addr, sizeof(addr));

		pj_strset(&t, *candidate, strlen(*candidate));

		if (pj_sockaddr_parse(pj_AF_UNSPEC(), 0, &t, &pjaddr) != PJ_SUCCESS) {
			ast_test_status_update(test, "Failed to parse candidate IP: %s\n", *candidate);
			return AST_TEST_FAIL;
		}

		if (ast_sockaddr_from_pj_sockaddr(&addr, &pjaddr)) {
			ast_test_status_update(test, "Failed to convert pj_sockaddr to ast_sockaddr: %s\n", *candidate);
			return AST_TEST_FAIL;
		}

		snprintf(buffer, sizeof(buffer), "%s", ast_sockaddr_stringify(&addr));

		if (strcmp(*candidate, buffer)) {
			ast_test_status_update(test, "Converted sockaddrs do not match: \"%s\" and \"%s\"\n",
				*candidate,
				buffer);
			return AST_TEST_FAIL;
		}

		candidate++;
	}

	return AST_TEST_PASS;
}
#endif

static int load_module(void)
{
	ast_debug(3, "Starting PJPROJECT logging to Asterisk logger\n");

	if (!(pjproject_sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "Failed to open SIP sorcery failed to open\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_apply_default(pjproject_sorcery, "log_mappings", "config", "pjproject.conf,criteria=type=log_mappings");
	if (ast_sorcery_object_register(pjproject_sorcery, "log_mappings", mapping_alloc, NULL, NULL)) {
		ast_log(LOG_WARNING, "Failed to register pjproject log_mappings object with sorcery\n");
		ast_sorcery_unref(pjproject_sorcery);
		pjproject_sorcery = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(pjproject_sorcery, "log_mappings", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(pjproject_sorcery, "log_mappings", "asterisk_debug", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct log_mappings, asterisk_debug));
	ast_sorcery_object_field_register(pjproject_sorcery, "log_mappings", "asterisk_error", "",  OPT_STRINGFIELD_T, 0, STRFLDSET(struct log_mappings, asterisk_error));
	ast_sorcery_object_field_register(pjproject_sorcery, "log_mappings", "asterisk_warning", "",  OPT_STRINGFIELD_T, 0, STRFLDSET(struct log_mappings, asterisk_warning));
	ast_sorcery_object_field_register(pjproject_sorcery, "log_mappings", "asterisk_notice", "",  OPT_STRINGFIELD_T, 0, STRFLDSET(struct log_mappings, asterisk_notice));
	ast_sorcery_object_field_register(pjproject_sorcery, "log_mappings", "asterisk_verbose", "",  OPT_STRINGFIELD_T, 0, STRFLDSET(struct log_mappings, asterisk_verbose));

	default_log_mappings = ast_sorcery_alloc(pjproject_sorcery, "log_mappings", "log_mappings");
	if (!default_log_mappings) {
		ast_log(LOG_ERROR, "Unable to allocate memory for pjproject log_mappings\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_string_field_set(default_log_mappings, asterisk_error, "0,1");
	ast_string_field_set(default_log_mappings, asterisk_warning, "2");
	ast_string_field_set(default_log_mappings, asterisk_debug, "3,4,5,6");

	ast_sorcery_load(pjproject_sorcery);

	AST_PJPROJECT_INIT_LOG_LEVEL();
	pj_init();

	decor_orig = pj_log_get_decor();
	log_cb_orig = pj_log_get_log_func();

	if (AST_VECTOR_INIT(&buildopts, 64)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	/*
	 * On startup, we want to capture the dump once and store it.
	 */
	pj_log_set_log_func(capture_buildopts_cb);
	pj_log_set_decor(0);
	pj_log_set_level(MAX_PJ_LOG_MAX_LEVEL);/* Set level to guarantee the dump output. */
	pj_dump_config();
	pj_log_set_decor(PJ_LOG_HAS_SENDER | PJ_LOG_HAS_INDENT);
	pj_log_set_log_func(log_forwarder);
	if (ast_pjproject_max_log_level < ast_option_pjproject_log_level) {
		ast_log(LOG_WARNING,
			"Asterisk built or linked with pjproject PJ_LOG_MAX_LEVEL=%d which is too low for startup level: %d.\n",
			ast_pjproject_max_log_level, ast_option_pjproject_log_level);
		ast_option_pjproject_log_level = ast_pjproject_max_log_level;
	}
	pj_log_set_level(ast_option_pjproject_log_level);
	if (!AST_VECTOR_SIZE(&buildopts)) {
		ast_log(LOG_NOTICE,
			"Asterisk built or linked with pjproject PJ_LOG_MAX_LEVEL=%d which is too low to get buildopts.\n",
			ast_pjproject_max_log_level);
	}

	ast_cli_register_multiple(pjproject_cli, ARRAY_LEN(pjproject_cli));

	AST_TEST_REGISTER(ast_sockaddr_to_pj_sockaddr_test);
	AST_TEST_REGISTER(ast_sockaddr_from_pj_sockaddr_test);

	return AST_MODULE_LOAD_SUCCESS;
}

#define NOT_EQUALS(a, b) (a != b)

static int unload_module(void)
{
	ast_cli_unregister_multiple(pjproject_cli, ARRAY_LEN(pjproject_cli));
	pj_log_set_log_func(log_cb_orig);
	pj_log_set_decor(decor_orig);

	AST_VECTOR_CALLBACK_VOID(&buildopts, ast_free);
	AST_VECTOR_FREE(&buildopts);

	ast_debug(3, "Stopped PJPROJECT logging to Asterisk logger\n");

	pj_shutdown();

	ao2_cleanup(default_log_mappings);
	default_log_mappings = NULL;

	ast_sorcery_unref(pjproject_sorcery);

	AST_TEST_UNREGISTER(ast_sockaddr_to_pj_sockaddr_test);
	AST_TEST_UNREGISTER(ast_sockaddr_from_pj_sockaddr_test);

	return 0;
}

static int reload_module(void)
{
	if (pjproject_sorcery) {
		ast_sorcery_reload(pjproject_sorcery);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJPROJECT Log and Utility Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 6,
);
