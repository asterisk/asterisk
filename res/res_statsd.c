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
 * \brief Support for publishing to a StatsD server.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_statsd" language="en_US">
		<synopsis>StatsD client</synopsis>
		<description>
			<para>The <literal>res_statsd</literal> module provides an API that
			allows Asterisk and its modules to send statistics to a StatsD
			server. It only provides a means to communicate with a StatsD server
			and does not send any metrics of its own.</para>
			<para>An example module, <literal>res_chan_stats</literal>, is
			provided which uses the API exposed by this module to send channel
			statistics to the configured StatsD server.</para>
			<para>More information about StatsD can be found at
			https://github.com/statsd/statsd</para>
		</description>
		<configFile name="statsd.conf">
			<configObject name="global">
				<synopsis>Global configuration settings</synopsis>
				<configOption name="enabled">
					<synopsis>Enable/disable the StatsD module</synopsis>
				</configOption>
				<configOption name="server">
					<synopsis>Address of the StatsD server</synopsis>
				</configOption>
				<configOption name="prefix">
					<synopsis>Prefix to prepend to every metric</synopsis>
				</configOption>
				<configOption name="add_newline">
					<synopsis>Append a newline to every event. This is useful if
					you want to fake out a server using netcat
					(nc -lu 8125)</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

#include "asterisk.h"

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

void AST_OPTIONAL_API_NAME(ast_statsd_log_string)(const char *metric_name,
	const char *metric_type, const char *value, double sample_rate)
{
	struct conf *cfg;
	struct ast_str *msg;
	size_t len;
	struct ast_sockaddr statsd_server;

	if (socket_fd == -1) {
		return;
	}

	/* Rates <= 0.0 never get logged.
	 * Rates >= 1.0 always get logged.
	 * All others leave it to chance.
	 */
	if (sample_rate <= 0.0 ||
		(sample_rate < 1.0 && sample_rate < ast_random_double())) {
		return;
	}

	cfg = ao2_global_obj_ref(confs);
	conf_server(cfg, &statsd_server);

	msg = ast_str_create(40);
	if (!msg) {
		ao2_cleanup(cfg);
		return;
	}

	if (!ast_strlen_zero(cfg->global->prefix)) {
		ast_str_append(&msg, 0, "%s.", cfg->global->prefix);
	}

	ast_str_append(&msg, 0, "%s:%s|%s", metric_name, value, metric_type);

	if (sample_rate < 1.0) {
		ast_str_append(&msg, 0, "|@%.2f", sample_rate);
	}

	if (cfg->global->add_newline) {
		ast_str_append(&msg, 0, "\n");
	}

	len = ast_str_strlen(msg);

	ast_debug(6, "Sending statistic %s to StatsD server\n", ast_str_buffer(msg));
	ast_sendto(socket_fd, ast_str_buffer(msg), len, 0, &statsd_server);

	ao2_cleanup(cfg);
	ast_free(msg);
}

void AST_OPTIONAL_API_NAME(ast_statsd_log_full)(const char *metric_name,
	const char *metric_type, intmax_t value, double sample_rate)
{
	char char_value[30];
	snprintf(char_value, sizeof(char_value), "%jd", value);

	ast_statsd_log_string(metric_name, metric_type, char_value, sample_rate);

}

AST_THREADSTORAGE(statsd_buf);

void AST_OPTIONAL_API_NAME(ast_statsd_log_string_va)(const char *metric_name,
	const char *metric_type, const char *value, double sample_rate, ...)
{
	struct ast_str *buf;
	va_list ap;
	int res;

	buf = ast_str_thread_get(&statsd_buf, 128);
	if (!buf) {
		return;
	}

	va_start(ap, sample_rate);
	res = ast_str_set_va(&buf, 0, metric_name, ap);
	va_end(ap);

	if (res == AST_DYNSTR_BUILD_FAILED) {
		return;
	}

	ast_statsd_log_string(ast_str_buffer(buf), metric_type, value, sample_rate);
}

void AST_OPTIONAL_API_NAME(ast_statsd_log_full_va)(const char *metric_name,
	const char *metric_type, intmax_t value, double sample_rate, ...)
{
	struct ast_str *buf;
	va_list ap;
	int res;

	buf = ast_str_thread_get(&statsd_buf, 128);
	if (!buf) {
		return;
	}

	va_start(ap, sample_rate);
	res = ast_str_set_va(&buf, 0, metric_name, ap);
	va_end(ap);

	if (res == AST_DYNSTR_BUILD_FAILED) {
		return;
	}

	ast_statsd_log_full(ast_str_buffer(buf), metric_type, value, sample_rate);
}

void AST_OPTIONAL_API_NAME(ast_statsd_log)(const char *metric_name,
	const char *metric_type, intmax_t value)
{
	char char_value[30];
	snprintf(char_value, sizeof(char_value), "%jd", value);

	ast_statsd_log_string(metric_name, metric_type, char_value, 1.0);
}

void AST_OPTIONAL_API_NAME(ast_statsd_log_sample)(const char *metric_name,
	intmax_t value, double sample_rate)
{
	char char_value[30];
	snprintf(char_value, sizeof(char_value), "%jd", value);

	ast_statsd_log_string(metric_name, AST_STATSD_COUNTER, char_value,
		sample_rate);
}

/*! \brief Mapping of the statsd conf struct's globals to the
 *         general context in the config file. */
static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.name = "global",
	.item_offset = offsetof(struct conf, global),
	.category = "general",
	.category_match = ACO_WHITELIST_EXACT,
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

	ast_debug(3, "Configuring StatsD client.\n");

	if (socket_fd == -1) {
		ast_debug(3, "Creating StatsD socket.\n");
		socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (socket_fd == -1) {
			perror("Error creating StatsD socket");
			return -1;
		}
	}

	conf_server(cfg, &statsd_server);
	server = ast_sockaddr_stringify_fmt(&statsd_server,
		AST_SOCKADDR_STR_DEFAULT);
	ast_debug(3, "  StatsD server = %s.\n", server);
	ast_debug(3, "  add newline = %s\n", AST_YESNO(cfg->global->add_newline));
	ast_debug(3, "  prefix = %s\n", cfg->global->prefix);

	return 0;
}

static void statsd_shutdown(void)
{
	ast_debug(3, "Shutting down StatsD client.\n");
	if (socket_fd != -1) {
		close(socket_fd);
		socket_fd = -1;
	}
}

static int unload_module(void)
{
	statsd_shutdown();
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(confs);
	return 0;
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

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		struct conf *cfg;

		ast_log(LOG_NOTICE, "Could not load statsd config; using defaults\n");
		cfg = conf_alloc();
		if (!cfg) {
			aco_info_destroy(&cfg_info);
			return AST_MODULE_LOAD_DECLINE;
		}

		if (aco_set_defaults(&global_option, "general", cfg->global)) {
			ast_log(LOG_ERROR, "Failed to initialize statsd defaults.\n");
			ao2_ref(cfg, -1);
			aco_info_destroy(&cfg_info);
			return AST_MODULE_LOAD_DECLINE;
		}

		ao2_global_obj_replace_unref(confs, cfg);
		ao2_ref(cfg, -1);
	}

	if (!is_enabled()) {
		return AST_MODULE_LOAD_SUCCESS;
	}

	if (statsd_init()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	switch (aco_process_config(&cfg_info, 1)) {
	case ACO_PROCESS_OK:
		break;
	case ACO_PROCESS_UNCHANGED:
		return AST_MODULE_LOAD_SUCCESS;
	case ACO_PROCESS_ERROR:
	default:
		return AST_MODULE_LOAD_DECLINE;
	}

	if (is_enabled()) {
		if (statsd_init()) {
			return AST_MODULE_LOAD_DECLINE;
		}
	} else {
		statsd_shutdown();
	}
	return AST_MODULE_LOAD_SUCCESS;
}

/* The priority of this module is set just after realtime, since it loads
 * configuration and could be used by any other sort of module.
 */
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "StatsD client support",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER + 5,
);
