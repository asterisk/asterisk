/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief STUN Network Monitor
 *
 * \author David Vossel <dvossel@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/module.h"
#include "asterisk/sched.h"
#include "asterisk/config.h"
#include "asterisk/stun.h"
#include "asterisk/netsock2.h"
#include "asterisk/lock.h"
#include "asterisk/acl.h"
#include "asterisk/cli.h"
#include "asterisk/json.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_system.h"
#include "asterisk/astobj2.h"

#include <fcntl.h>

#define DEFAULT_MONITOR_REFRESH 30	/*!< Default refresh period in seconds */
#define DEFAULT_RETRIES 3		/*!< retries shown in stun show status
					     matching static retries in stun.c */

static const char stun_conf_file[] = "res_stun_monitor.conf";
static struct ast_sched_context *sched;

static struct {
	/*! STUN monitor protection lock. */
	ast_mutex_t lock;
	/*! Current perceived external address. */
	struct sockaddr_in external_addr;
	/*! STUN server host name. */
	const char *server_hostname;
	/*! Port of STUN server to use */
	unsigned int stun_port;
	/*! Number of seconds between polls to the STUN server for the external address. */
	unsigned int refresh;
	/*! Monitoring STUN socket. */
	int stun_sock;
	/*! TRUE if the STUN monitor is enabled. */
	unsigned int monitor_enabled:1;
	/*! TRUE if the perceived external address is valid/known. */
	unsigned int external_addr_known:1;
	/*! TRUE if we have already griped about a STUN poll failing. */
	unsigned int stun_poll_failed_gripe:1;
} args;

static void stun_close_sock(void)
{
	if (0 <= args.stun_sock) {
		close(args.stun_sock);
		args.stun_sock = -1;
	}
}

/* \brief called by scheduler to send STUN request */
static int stun_monitor_request(const void *blarg)
{
	int res;
	struct sockaddr_in answer;
	static const struct sockaddr_in no_addr = { 0, };

	ast_mutex_lock(&args.lock);
	if (!args.monitor_enabled) {
		goto monitor_request_cleanup;
	}

	if (args.stun_sock < 0) {
		struct ast_sockaddr stun_addr;

		/* STUN socket not open.  Refresh the server DNS address resolution. */
		if (!args.server_hostname) {
			/* No STUN hostname? */
			goto monitor_request_cleanup;
		}

		/* Lookup STUN address. */
		memset(&stun_addr, 0, sizeof(stun_addr));
		stun_addr.ss.ss_family = AF_INET;
		if (ast_get_ip(&stun_addr, args.server_hostname)) {
			/* Lookup failed. */
			ast_log(LOG_WARNING, "Unable to lookup STUN server '%s'\n",
				args.server_hostname);
			goto monitor_request_cleanup;
		}
		ast_sockaddr_set_port(&stun_addr, args.stun_port);

		/* open socket binding */
		args.stun_sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (args.stun_sock < 0) {
			ast_log(LOG_WARNING, "Unable to create STUN socket: %s\n", strerror(errno));
			goto monitor_request_cleanup;
		}
		if (ast_connect(args.stun_sock, &stun_addr)) {
			ast_log(LOG_WARNING, "STUN Failed to connect to %s: %s\n",
				ast_sockaddr_stringify(&stun_addr), strerror(errno));
			stun_close_sock();
			goto monitor_request_cleanup;
		}
	}

	res = ast_stun_request(args.stun_sock, NULL, NULL, &answer);
	if (res) {
		/*
		 * STUN request timed out or errored.
		 *
		 * Refresh the server DNS address resolution next time around.
		 */
		if (!args.stun_poll_failed_gripe) {
			args.stun_poll_failed_gripe = 1;
			ast_log(LOG_WARNING, "STUN poll %s. Re-evaluating STUN server address.\n",
				res < 0 ? "failed" : "got no response");
		}
		stun_close_sock();
	} else {
		args.stun_poll_failed_gripe = 0;
		if (memcmp(&no_addr, &answer, sizeof(no_addr))
			&& memcmp(&args.external_addr, &answer, sizeof(args.external_addr))) {
			const char *newaddr = ast_strdupa(ast_inet_ntoa(answer.sin_addr));
			int newport = ntohs(answer.sin_port);

			ast_log(LOG_NOTICE, "Old external address/port %s:%d now seen as %s:%d.\n",
				ast_inet_ntoa(args.external_addr.sin_addr),
				ntohs(args.external_addr.sin_port), newaddr, newport);

			args.external_addr = answer;

			if (args.external_addr_known) {
				RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
				RAII_VAR(struct ast_json_payload *, json_payload, NULL, ao2_cleanup);
				RAII_VAR(struct ast_json *, json_object, NULL, ast_json_unref);

				if (!ast_network_change_type()) {
					goto publish_failure;
				}

				/* This json_object doesn't actually contain anything yet. We have to reference something
				 * for stasis, and this is useful for if we want to ever add data for any reason. */
				json_object = ast_json_object_create();
				if (!json_object) {
					goto publish_failure;
				}

				if (!(json_payload = ast_json_payload_create(json_object))) {
					goto publish_failure;
				}

				msg = stasis_message_create(ast_network_change_type(), json_payload);

publish_failure:
				if (msg) {
					stasis_publish(ast_system_topic(), msg);
				} else {
					ast_log(LOG_ERROR, "Failed to issue network change message.\n");
				}
			} else {
				/* this was the first external address we found, do not alert listeners
				 * until this address changes to something else. */
				args.external_addr_known = 1;
			}
		}
	}

monitor_request_cleanup:
	/* always refresh this scheduler item.  It will be removed elsewhere when
	 * it is supposed to go away */
	res = args.refresh * 1000;
	ast_mutex_unlock(&args.lock);

	return res;
}

/*!
 * \internal
 * \brief Stops the STUN monitor thread.
 *
 * \note do not hold the args->lock while calling this
 *
 * \return Nothing
 */
static void stun_stop_monitor(void)
{
	ast_mutex_lock(&args.lock);
	args.monitor_enabled = 0;
	ast_free((char *) args.server_hostname);
	args.server_hostname = NULL;
	stun_close_sock();
	ast_mutex_unlock(&args.lock);

	if (sched) {
		ast_sched_context_destroy(sched);
		sched = NULL;
		ast_log(LOG_NOTICE, "STUN monitor stopped\n");
	}
}

/*!
 * \internal
 * \brief Starts the STUN monitor thread.
 *
 * \note The args->lock MUST be held when calling this function
 *
 * \return Nothing
 */
static int stun_start_monitor(void)
{
	/* if scheduler thread is not started, make sure to start it now */
	if (sched) {
		return 0; /* already started */
	}

	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Failed to create stun monitor scheduler context\n");
		return -1;
	}

	if (ast_sched_start_thread(sched)) {
		ast_sched_context_destroy(sched);
		sched = NULL;
		stun_close_sock();
		return -1;
	}

	if (ast_sched_add_variable(sched, (args.refresh * 1000), stun_monitor_request, NULL, 1) < 0) {
		ast_log(LOG_ERROR, "Unable to schedule STUN network monitor \n");
		ast_sched_context_destroy(sched);
		sched = NULL;
		return -1;
	}

	ast_log(LOG_NOTICE, "STUN monitor started\n");

	return 0;
}

/*!
 * \internal
 * \brief Parse and setup the stunaddr parameter.
 *
 * \param value Configuration parameter variable value.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_stunaddr(const char *value)
{
	char *val;
	char *host_str;
	char *port_str;
	unsigned int port;
	struct ast_sockaddr stun_addr;

	if (ast_strlen_zero(value)) {
		/* Setting to an empty value disables STUN monitoring. */
		args.monitor_enabled = 0;
		return 0;
	}

	val = ast_strdupa(value);
	if (!ast_sockaddr_split_hostport(val, &host_str, &port_str, 0)
		|| ast_strlen_zero(host_str)) {
		return -1;
	}

	/* Determine STUN port */
	if (ast_strlen_zero(port_str)
		|| 1 != sscanf(port_str, "%30u", &port)) {
		port = STANDARD_STUN_PORT;
	}

	host_str = ast_strdup(host_str);
	if (!host_str) {
		return -1;
	}

	/* Lookup STUN address. */
	memset(&stun_addr, 0, sizeof(stun_addr));
	stun_addr.ss.ss_family = AF_INET;
	if (ast_get_ip(&stun_addr, host_str)) {
		ast_log(LOG_WARNING, "Unable to lookup STUN server '%s'\n", host_str);
		ast_free(host_str);
		return -1;
	}

	/* Save STUN server information. */
	ast_free((char *) args.server_hostname);
	args.server_hostname = host_str;
	args.stun_port = port;

	/* Enable STUN monitor */
	args.monitor_enabled = 1;
	return 0;
}

static int load_config(int startup)
{
	struct ast_flags config_flags = { 0, };
	struct ast_config *cfg;
	struct ast_variable *v;

	if (!startup) {
		ast_set_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
	}

	cfg = ast_config_load2(stun_conf_file, "res_stun_monitor", config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Unable to load config %s\n", stun_conf_file);
		return -1;
	}
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	/* clean up any previous open socket */
	stun_close_sock();
	args.stun_poll_failed_gripe = 0;

	/* set defaults */
	args.monitor_enabled = 0;
	args.refresh = DEFAULT_MONITOR_REFRESH;

	for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
		if (!strcasecmp(v->name, "stunaddr")) {
			if (setup_stunaddr(v->value)) {
				ast_log(LOG_WARNING, "Invalid STUN server address: %s at line %d\n",
					v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "stunrefresh")) {
			if ((sscanf(v->value, "%30u", &args.refresh) != 1) || !args.refresh) {
				ast_log(LOG_WARNING, "Invalid stunrefresh value '%s', must be an integer > 0 at line %d\n", v->value, v->lineno);
				args.refresh = DEFAULT_MONITOR_REFRESH;
			}
		} else {
			ast_log(LOG_WARNING, "Invalid config option %s at line %d\n",
				v->value, v->lineno);
		}
	}

	ast_config_destroy(cfg);

	return 0;
}

/*! \brief Execute stun show status command */
static void _stun_show_status(int fd)
{
	const char *status;

#define DATALN "%-25s %-5u %-7u %-8d %-7s %-16s %-d\n"
#define HEADER "%-25s %-5s %-7s %-8s %-7s %-16s %-s\n"

	/*! we only have one stun server, but start to play well with more */
	ast_cli(fd, HEADER, "Hostname", "Port", "Period", "Retries", "Status", "ExternAddr", "ExternPort");

	if (args.stun_poll_failed_gripe) {
		status = "FAIL";
	} else if (args.external_addr_known) {
		status = "OK";
	} else {
		status = "INIT";
	}
	ast_cli( fd, DATALN,
		     args.server_hostname,
		     args.stun_port,
		     args.refresh,
		     DEFAULT_RETRIES,
		     status,
		     ast_inet_ntoa(args.external_addr.sin_addr),
		     ntohs(args.external_addr.sin_port)
		   );

#undef HEADER
#undef DATALN
}

static char *handle_cli_stun_show_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "stun show status";
		e->usage =
		    "Usage: stun show status\n"
		    "       List all known STUN servers and statuses.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	_stun_show_status(a->fd);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_stun[] = {
	AST_CLI_DEFINE(handle_cli_stun_show_status, "Show STUN servers and statuses"),
};

static int __reload(int startup)
{
	int res;

	ast_mutex_lock(&args.lock);
	if (!(res = load_config(startup)) && args.monitor_enabled) {
		res = stun_start_monitor();
	}
	ast_mutex_unlock(&args.lock);

	if (res < 0 || !args.monitor_enabled) {
		stun_stop_monitor();
	}

	return res;
}

static int reload(void)
{
	return __reload(0);
}

static int unload_module(void)
{
	stun_stop_monitor();
	ast_mutex_destroy(&args.lock);

	/*! Unregister CLI commands */
	ast_cli_unregister_multiple(cli_stun, ARRAY_LEN(cli_stun));

	return 0;
}

static int load_module(void)
{
	ast_mutex_init(&args.lock);
	args.stun_sock = -1;
	if (__reload(1)) {
		ast_mutex_destroy(&args.lock);
		return AST_MODULE_LOAD_DECLINE;
	}

	/*! Register CLI commands */
	ast_cli_register_multiple(cli_stun, sizeof(cli_stun) / sizeof(struct ast_cli_entry));

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "STUN Network Monitor",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND
	);
