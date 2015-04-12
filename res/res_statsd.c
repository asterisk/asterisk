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

/*!
 * \brief Support for publishing to a statsd server.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_statsd" language="en_US">
		<synopsis>Statsd client.</synopsis>
		<configFile name="statsd.conf">
			<configObject name="global">
				<synopsis>Global configuration settings</synopsis>
				<configOption name="enabled">
					<synopsis>Enable/disable the statsd module</synopsis>
				</configOption>
				<configOption name="server">
					<synopsis>Address of the statsd server</synopsis>
				</configOption>
				<configOption name="prefix">
					<synopsis>Prefix to prepend to every metric</synopsis>
				</configOption>
				<configOption name="add_newline">
					<synopsis>Append a newline to every event. This is useful if you want to fake out a server using netcat (nc -lu 8125)</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/config_options.h"
#include "asterisk/module.h"
#include "asterisk/netsock2.h"

#define AST_API_MODULE
#include "asterisk/statsd.h"

#define DEFAULT_STATSD_PORT 8125

#define MAX_PREFIX 40

/*! Socket for sending statd messages */
static int socket_fd = -1;

/*! \brief Global configuration options for statsd client. */
struct conf_global_options {
	/*! Enabled by default, disabled if false. */
	int enabled;
	/*! Disabled by default, appends newlines to all messages when enabled. */
	int add_newline;
	/*! Statsd server address[:port]. */
	struct ast_sockaddr statsd_server;
	/*! Prefix to put on every stat. */
	char prefix[MAX_PREFIX + 1];
};

/*! \brief All configuration options for statsd client. */
struct conf {
	/*! The general section configuration options. */
	struct conf_global_options *global;
};

/*! \brief Locking container for safe configuration access. */
static AO2_GLOBAL_OBJ_STATIC(confs);

static void conf_server(const struct conf *cfg, struct ast_sockaddr *addr)
{
	*addr = cfg->global->statsd_server;
	if (ast_sockaddr_port(addr) == 0) {
		ast_sockaddr_set_port(addr, DEFAULT_STATSD_PORT);
	}
}

void AST_OPTIONAL_API_NAME(ast_statsd_log_full)(const char *metric_name,
	const char *metric_type, intmax_t value, double sample_rate)
{
	RAII_VAR(struct conf *, cfg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, msg, NULL, ast_free);
	size_t len;
	struct ast_sockaddr statsd_server;

	if (socket_fd == -1) {
		return;
	}

	cfg = ao2_global_obj_ref(confs);
	conf_server(cfg, &statsd_server);

	/* Rates <= 0.0 never get logged.
	 * Rates >= 1.0 always get logged.
	 * All others leave it to chance.
	 */
	if (sample_rate <= 0.0 ||
		(sample_rate < 1.0 && sample_rate < ast_random_double())) {
		return;
	}

	cfg = ao2_global_obj_ref(confs);

	msg = ast_str_create(40);
	if (!msg) {
		return;
	}

	if (!ast_strlen_zero(cfg->global->prefix)) {
		ast_str_append(&msg, 0, "%s.", cfg->global->prefix);
	}

	ast_str_append(&msg, 0, "%s:%jd|%s", metric_name, value, metric_type);

	if (sample_rate < 1.0) {
		ast_str_append(&msg, 0, "|@%.2f", sample_rate);
	}

	if (cfg->global->add_newline) {
		ast_str_append(&msg, 0, "\n");
	}

	len = ast_str_strlen(msg);

	ast_debug(6, "send: %s\n", ast_str_buffer(msg));
	ast_sendto(socket_fd, ast_str_buffer(msg), len, 0, &statsd_server);
}

void AST_OPTIONAL_API_NAME(ast_statsd_log)(const char *metric_name,
	const char *metric_type, intmax_t value)
{
	ast_statsd_log_full(metric_name, metric_type, value, 1.0);
}

void AST_OPTIONAL_API_NAME(ast_statsd_log_sample)(const char *metric_name,
	intmax_t value, double sample_rate)
{
	ast_statsd_log_full(metric_name, AST_STATSD_COUNTER, value,
		sample_rate);
}

/*! \brief Mapping of the statsd conf struct's globals to the
 *         general context in the config file. */
static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.name = "global",
	.item_offset = offsetof(struct conf, global),
	.category = "^general$",
	.category_match = ACO_WHITELIST
};

static struct aco_type *global_options[] = ACO_TYPES(&global_option);

/*! \brief Disposes of the statsd conf object */
static void conf_destructor(void *obj)
{
    struct conf *cfg = obj;
    ao2_cleanup(cfg->global);
}

/*! \brief Creates the statis http conf object. */
static void *conf_alloc(void)
{
    struct conf *cfg;

    if (!(cfg = ao2_alloc(sizeof(*cfg), conf_destructor))) {
        return NULL;
    }

    if (!(cfg->global = ao2_alloc(sizeof(*cfg->global), NULL))) {
        ao2_ref(cfg, -1);
        return NULL;
    }
    return cfg;
}

/*! \brief The conf file that's processed for the module. */
static struct aco_file conf_file = {
	/*! The config file name. */
	.filename = "statsd.conf",
	/*! The mapping object types to be processed. */
	.types = ACO_TYPES(&global_option),
};

CONFIG_INFO_STANDARD(cfg_info, confs, conf_alloc,
		     .files = ACO_FILES(&conf_file));

/*! \brief Helper function to check if module is enabled. */
static char is_enabled(void)
{
	RAII_VAR(struct conf *, cfg, ao2_global_obj_ref(confs), ao2_cleanup);
	return cfg->global->enabled;
}

static int statsd_init(void)
{
	RAII_VAR(struct conf *, cfg, ao2_global_obj_ref(confs), ao2_cleanup);
	char *server;
	struct ast_sockaddr statsd_server;

	ast_assert(is_enabled());

	ast_debug(3, "Configuring statsd client.\n");

	if (socket_fd == -1) {
		ast_debug(3, "Creating statsd socket.\n");
		socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (socket_fd == -1) {
			perror("Error creating statsd socket");
			return -1;
		}
	}

	conf_server(cfg, &statsd_server);
	server = ast_sockaddr_stringify_fmt(&statsd_server,
		AST_SOCKADDR_STR_DEFAULT);
	ast_debug(3, "  statsd server = %s.\n", server);
	ast_debug(3, "  add newline = %s\n", AST_YESNO(cfg->global->add_newline));
	ast_debug(3, "  prefix = %s\n", cfg->global->prefix);

	return 0;
}

static void statsd_shutdown(void)
{
	ast_debug(3, "Shutting down statsd client.\n");
	if (socket_fd != -1) {
		close(socket_fd);
		socket_fd = -1;
	}
}

static int load_module(void)
{
	if (aco_info_init(&cfg_info)) {
		aco_info_destroy(&cfg_info);
		return AST_MODULE_LOAD_DECLINE;
	}

	aco_option_register(&cfg_info, "enabled", ACO_EXACT, global_options,
		"no", OPT_BOOL_T, 1,
		FLDSET(struct conf_global_options, enabled));

	aco_option_register(&cfg_info, "add_newline", ACO_EXACT, global_options,
		"no", OPT_BOOL_T, 1,
		FLDSET(struct conf_global_options, add_newline));

	aco_option_register(&cfg_info, "server", ACO_EXACT, global_options,
		"127.0.0.1", OPT_SOCKADDR_T, 0,
		FLDSET(struct conf_global_options, statsd_server));

	aco_option_register(&cfg_info, "prefix", ACO_EXACT, global_options,
		"", OPT_CHAR_ARRAY_T, 0,
		CHARFLDSET(struct conf_global_options, prefix));

	if (aco_process_config(&cfg_info, 0)) {
		aco_info_destroy(&cfg_info);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!is_enabled()) {
		return AST_MODULE_LOAD_SUCCESS;
	}

	if (statsd_init() != 0) {
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	statsd_shutdown();
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(confs);
	return 0;
}

static int reload_module(void)
{
	if (aco_process_config(&cfg_info, 1)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (is_enabled()) {
		return statsd_init();
	} else {
		statsd_shutdown();
		return AST_MODULE_LOAD_SUCCESS;
	}
}

/* The priority of this module is set to be as low as possible, since it could
 * be used by any other sort of module.
 */
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Statsd client support",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = 0,
	);
