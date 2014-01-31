/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<defaultenabled>yes</defaultenabled>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/netsock2.h"

enum pjsip_logging_mode {
	LOGGING_MODE_DISABLED,    /* No logging is enabled */
	LOGGING_MODE_ENABLED,     /* Logging is enabled */
};

static enum pjsip_logging_mode logging_mode;
static struct ast_sockaddr log_addr;

/*! \brief  Return the first entry from ast_sockaddr_resolve filtered by address family
 *
 * \warning Using this function probably means you have a faulty design.
 * \note This function was taken from the function of the same name in chan_sip.c
 */
static int ast_sockaddr_resolve_first_af(struct ast_sockaddr *addr,
				      const char* name, int flag, int family)
{
	struct ast_sockaddr *addrs;
	int addrs_cnt;

	addrs_cnt = ast_sockaddr_resolve(&addrs, name, flag, family);
	if (addrs_cnt <= 0) {
		return 1;
	}
	if (addrs_cnt > 1) {
		ast_debug(1, "Multiple addresses, using the first one only\n");
	}

	ast_sockaddr_copy(addr, &addrs[0]);

	ast_free(addrs);
	return 0;
}

/*! \brief See if we pass debug IP filter */
static inline int pjsip_log_test_addr(const char *address, int port)
{
	struct ast_sockaddr test_addr;
	if (logging_mode == LOGGING_MODE_DISABLED) {
		return 0;
	}

	/* A null logging address means we'll debug any address */
	if (ast_sockaddr_isnull(&log_addr)) {
		return 1;
	}

	/* A null address was passed in. Just reject it. */
	if (ast_strlen_zero(address)) {
		return 0;
	}

	ast_sockaddr_parse(&test_addr, address, PARSE_PORT_IGNORE);
	ast_sockaddr_set_port(&test_addr, port);

	/* If no port was specified for a debug address, just compare the
	 * addresses, otherwise compare the address and port
	 */
	if (ast_sockaddr_port(&log_addr)) {
		return !ast_sockaddr_cmp(&log_addr, &test_addr);
	} else {
		return !ast_sockaddr_cmp_addr(&log_addr, &test_addr);
	}
}

static pj_status_t logging_on_tx_msg(pjsip_tx_data *tdata)
{
	if (!pjsip_log_test_addr(tdata->tp_info.dst_name, tdata->tp_info.dst_port)) {
		return PJ_SUCCESS;
	}

	ast_verbose("<--- Transmitting SIP %s (%d bytes) to %s:%s:%d --->\n%.*s\n",
		    tdata->msg->type == PJSIP_REQUEST_MSG ? "request" : "response",
		    (int) (tdata->buf.cur - tdata->buf.start),
		    tdata->tp_info.transport->type_name,
		    tdata->tp_info.dst_name,
		    tdata->tp_info.dst_port,
		    (int) (tdata->buf.end - tdata->buf.start), tdata->buf.start);
	return PJ_SUCCESS;
}

static pj_bool_t logging_on_rx_msg(pjsip_rx_data *rdata)
{
	if (!pjsip_log_test_addr(rdata->pkt_info.src_name, rdata->pkt_info.src_port)) {
		return PJ_FALSE;
	}

	ast_verbose("<--- Received SIP %s (%d bytes) from %s:%s:%d --->\n%s\n",
		    rdata->msg_info.msg->type == PJSIP_REQUEST_MSG ? "request" : "response",
		    rdata->msg_info.len,
		    rdata->tp_info.transport->type_name,
		    rdata->pkt_info.src_name,
		    rdata->pkt_info.src_port,
		    rdata->pkt_info.packet);
	return PJ_FALSE;
}

static pjsip_module logging_module = {
	.name = { "Logging Module", 14 },
	.priority = 0,
	.on_rx_request = logging_on_rx_msg,
	.on_rx_response = logging_on_rx_msg,
	.on_tx_request = logging_on_tx_msg,
	.on_tx_response = logging_on_tx_msg,
};

static char *pjsip_enable_logger_host(int fd, const char *arg)
{
	if (ast_sockaddr_resolve_first_af(&log_addr, arg, 0, AST_AF_UNSPEC)) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(fd, "PJSIP Logging Enabled for host: %s\n", ast_sockaddr_stringify_addr(&log_addr));
	logging_mode = LOGGING_MODE_ENABLED;

	return CLI_SUCCESS;
}

static char *pjsip_set_logger(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *what;

	if (cmd == CLI_INIT) {
		e->command = "pjsip set logger {on|off|host}";
		e->usage =
			"Usage: pjsip set logger {on|off}\n"
			"       Enables or disabling logging of SIP packets\n"
			"       read on ports bound to PJSIP transports either\n"
			"       globally or enables logging for an individual\n"
			"       host.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	what = a->argv[e->args - 1];     /* Guaranteed to exist */

	if (a->argc == e->args) {        /* on/off */
		if (!strcasecmp(what, "on")) {
			logging_mode = LOGGING_MODE_ENABLED;
			ast_cli(a->fd, "PJSIP Logging enabled\n");
			ast_sockaddr_setnull(&log_addr);
			return CLI_SUCCESS;
		} else if (!strcasecmp(what, "off")) {
			logging_mode = LOGGING_MODE_DISABLED;
			ast_cli(a->fd, "PJSIP Logging disabled\n");
			return CLI_SUCCESS;
		}
	} else if (a->argc == e->args + 1) {
		if (!strcasecmp(what, "host")) {
			return pjsip_enable_logger_host(a->fd, a->argv[e->args]);
		}
	}

	return CLI_SHOWUSAGE;
}

static struct ast_cli_entry cli_pjsip[] = {
	AST_CLI_DEFINE(pjsip_set_logger, "Enable/Disable PJSIP Logger Output")
};

static void check_debug(void)
{
	RAII_VAR(char *, debug, ast_sip_get_debug(), ast_free);

	if (ast_false(debug)) {
		logging_mode = LOGGING_MODE_DISABLED;
		return;
	}

	logging_mode = LOGGING_MODE_ENABLED;

	if (ast_true(debug)) {
		ast_sockaddr_setnull(&log_addr);
		return;
	}

	/* assume host */
	if (ast_sockaddr_resolve_first_af(&log_addr, debug, 0, AST_AF_UNSPEC)) {
		ast_log(LOG_WARNING, "Could not resolve host %s for debug "
			"logging\n", debug);
	}
}

static void global_reloaded(const char *object_type)
{
	check_debug();
}

static const struct ast_sorcery_observer global_observer = {
	.loaded = global_reloaded
};

static int load_module(void)
{
	if (ast_sorcery_observer_add(ast_sip_get_sorcery(), "global", &global_observer)) {
		ast_log(LOG_WARNING, "Unable to add global observer\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	check_debug();

	ast_sip_register_service(&logging_module);
	ast_cli_register_multiple(cli_pjsip, ARRAY_LEN(cli_pjsip));

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_pjsip, ARRAY_LEN(cli_pjsip));
	ast_sip_unregister_service(&logging_module);

	ast_sorcery_observer_remove(
		ast_sip_get_sorcery(), "global", &global_observer);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Packet Logger",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
