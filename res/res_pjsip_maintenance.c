/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Aurora Innovation
 *
 * Daniel Donoghue
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
 * \brief PJSIP Endpoint Maintenance Mode
 *
 * Provides a runtime toggle to place individual PJSIP endpoints into
 * maintenance mode.  While an endpoint is in maintenance mode:
 *
 * - New \b inbound INVITE and SUBSCRIBE requests are rejected with
 *   "503 Service Unavailable" and a Retry-After: 300 header.
 * - \b Outbound originations (Dial, ARI originate) are refused before
 *   any SIP session or Asterisk channel is created.
 * - Active in-progress dialogs (BYE, re-INVITE, UPDATE, etc.) are
 *   completely unaffected.
 * - Existing presence/BLF subscriptions are left to expire naturally.
 *
 * CLI:
 *   pjsip set maintenance <on|off> <endpoint|all>
 *   pjsip show maintenance [endpoint]
 *
 * AMI actions: PJSIPSetMaintenance, PJSIPShowMaintenance
 *
 * \ingroup res_pjsip
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<manager name="PJSIPSetMaintenance" language="en_US">
		<since>
			<version>20.19.0</version>
			<version>22.9.0</version>
			<version>23.3.0</version>
		</since>
		<synopsis>
			Enable or disable maintenance mode for a PJSIP endpoint.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Endpoint" required="true">
				<para>The PJSIP endpoint name, or <literal>all</literal> to
				toggle maintenance mode for every configured endpoint.</para>
			</parameter>
			<parameter name="State" required="true">
				<para>Desired maintenance state.</para>
				<enumlist>
					<enum name="on" />
					<enum name="off" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Enables or disables maintenance mode for the specified PJSIP
			endpoint.  While in maintenance mode, new inbound INVITE and
			SUBSCRIBE dialogs are rejected with 503 Service Unavailable, and
			outbound originations via Dial() or ARI are refused before any
			SIP session or channel is created.  In-progress dialogs are unaffected.</para>
			<para>A <literal>PJSIPMaintenanceStatus</literal> event is emitted
			when the state changes.</para>
		</description>
	</manager>
	<manager name="PJSIPShowMaintenance" language="en_US">
		<since>
			<version>20.19.0</version>
			<version>22.9.0</version>
			<version>23.3.0</version>
		</since>
		<synopsis>
			Show maintenance mode status for PJSIP endpoints.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Endpoint" required="false">
				<para>If specified, show the status for this endpoint only.
				If omitted, list all endpoints currently in maintenance
				mode.</para>
			</parameter>
		</syntax>
		<description>
			<para>Emits one <literal>PJSIPMaintenanceStatus</literal> event
			per result, followed by a
			<literal>PJSIPMaintenanceStatusComplete</literal> event.</para>
		</description>
	</manager>
	<managerEvent language="en_US" name="PJSIPMaintenanceStatus">
		<managerEventInstance class="EVENT_FLAG_SYSTEM">
			<since>
				<version>20.19.0</version>
				<version>22.9.0</version>
				<version>23.3.0</version>
			</since>
			<synopsis>
				Reports the maintenance mode state of a PJSIP endpoint.
			</synopsis>
			<syntax>
				<parameter name="Endpoint">
					<para>The PJSIP endpoint name.</para>
				</parameter>
				<parameter name="Status">
					<para>Current maintenance state.</para>
					<enumlist>
						<enum name="enabled" />
						<enum name="disabled" />
					</enumlist>
				</parameter>
			</syntax>
			<description>
				<para>Emitted when an endpoint enters or leaves maintenance
				mode, and as a list entry in response to
				<literal>PJSIPShowMaintenance</literal>.</para>
			</description>
		</managerEventInstance>
	</managerEvent>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/manager.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/sorcery.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"

enum {
	MAINT_HASH_BUCKETS = 53,
};

/*! Endpoints currently in maintenance mode.
 *  Protected by the container's own internal RWLOCK.
 *  No other locks are ever held simultaneously with this container.
 */
static struct ao2_container *maintenance_set;

/*!
 * \internal
 * \brief Add an endpoint to the maintenance set.
 * \retval  1  Added successfully.
 * \retval  0  Already in maintenance (no-op).
 * \retval -1  Allocation failure.
 */
static int maint_set_add(const char *endpoint_name)
{
	char *entry;

	entry = ao2_find(maintenance_set, endpoint_name, OBJ_SEARCH_KEY);
	if (entry) {
		ao2_ref(entry, -1);
		return 0; /* already in maintenance */
	}
	return ast_str_container_add(maintenance_set, endpoint_name) ? -1 : 1;
}

/*!
 * \internal
 * \brief Remove an endpoint from the maintenance set.
 * \retval 1  Removed successfully.
 * \retval 0  Was not in maintenance (no-op).
 */
static int maint_set_remove(const char *endpoint_name)
{
	char *entry;

	entry = ao2_find(maintenance_set, endpoint_name, OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (!entry) {
		return 0;
	}
	ao2_ref(entry, -1);
	return 1;
}

/*!
 * \internal
 * \brief Apply a maintenance state change to the maintenance set.
 *
 * Does not validate endpoint existence; callers are responsible for that.
 * Callers are also responsible for emitting log messages and AMI events.
 *
 * \retval  1  State changed.
 * \retval  0  Already in requested state (no-op).
 * \retval -1  Allocation failure (enable path only).
 */
static int apply_maintenance_state(const char *endpoint_name, int enable)
{
	return enable ? maint_set_add(endpoint_name) : maint_set_remove(endpoint_name);
}

/* Session supplement: block outgoing session creation when endpoint is in maintenance. */

/*!
 * \internal
 * \brief Session supplement session_create callback: block outgoing sessions to
 *        endpoints currently in maintenance mode.
 * \retval  1  Endpoint is in maintenance; session creation blocked.
 * \retval  0  Endpoint is not in maintenance; session creation allowed.
 */
static int maint_session_create(struct ast_sip_endpoint *endpoint,
	struct ast_sip_contact *contact, const char *location,
	const char *request_user, struct ast_stream_topology *req_topology)
{
	const char *endpoint_name = ast_sorcery_object_get_id(endpoint);
	char *entry = ao2_find(maintenance_set, endpoint_name, OBJ_SEARCH_KEY);

	if (entry) {
		ao2_ref(entry, -1);
		ast_debug(3, "PJSIP: Refusing outbound call to endpoint '%s': maintenance mode active\n",
			endpoint_name);
		return 1;
	}
	return 0;
}

static struct ast_sip_session_supplement maintenance_session_supplement = {
	.session_create = maint_session_create,
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_FIRST,
};

/* Inbound request hook for maintenance_pjsip_mod.
 *
 * For endpoints in maintenance mode, blocks new INVITE, SUBSCRIBE,
 * REGISTER, and OPTIONS requests with 503 + Retry-After: 300.
 * In-dialog INVITE, SUBSCRIBE, and OPTIONS are passed through 
 * unmodified.  SUBSCRIBE and REGISTER with Expires: 0 are also passed
 * through, allowing un-subscribe and de-registration. */

static pj_bool_t maintenance_on_rx_request(pjsip_rx_data *rdata)
{
	pjsip_msg *msg = rdata->msg_info.msg;
	const pjsip_method *method = &msg->line.req.method;
	pjsip_to_hdr *to;
	pjsip_expires_hdr *expires_hdr;
	struct ast_sip_endpoint *endpoint;
	char *entry;
	pjsip_hdr hdr_list;
	pjsip_generic_int_hdr *retry_after;
	static const pj_str_t str_retry_after = { "Retry-After", 11 };
	int is_invite;
	int is_subscribe;
	int is_register;
	int is_options;

	is_invite   = pjsip_method_cmp(method, pjsip_get_invite_method())   == 0;
	is_subscribe = pjsip_method_cmp(method, pjsip_get_subscribe_method()) == 0;
	is_register = pjsip_method_cmp(method, pjsip_get_register_method()) == 0;
	is_options  = pjsip_method_cmp(method, pjsip_get_options_method())  == 0;

	if (!is_invite && !is_subscribe && !is_register && !is_options) {
		return PJ_FALSE;
	}

	/* In-dialog INVITE, SUBSCRIBE, or OPTIONS; pass through. */
	if (is_invite || is_subscribe || is_options) {
		to = rdata->msg_info.to;
		if (to->tag.slen > 0) {
			return PJ_FALSE;
		}
	}

	/* SUBSCRIBE or REGISTER with Expires: 0: allow un-subscribe / de-register. */
	if (is_subscribe || is_register) {
		expires_hdr = pjsip_msg_find_hdr(msg, PJSIP_H_EXPIRES, NULL);
		if (expires_hdr && expires_hdr->ivalue == 0) {
			return PJ_FALSE;
		}
	}

	endpoint = ast_pjsip_rdata_get_endpoint(rdata);
	if (!endpoint) {
		return PJ_FALSE;
	}

	entry = ao2_find(maintenance_set, ast_sorcery_object_get_id(endpoint), OBJ_SEARCH_KEY);
	if (!entry) {
		ao2_ref(endpoint, -1);
		return PJ_FALSE;
	}
	ao2_ref(entry, -1);

	ast_log(LOG_NOTICE, "PJSIP: Endpoint '%s' is in maintenance mode; rejecting new %.*s from %s\n",
		ast_sorcery_object_get_id(endpoint),
		(int)method->name.slen, method->name.ptr,
		rdata->pkt_info.src_name);

	ao2_ref(endpoint, -1);

	pj_list_init(&hdr_list);
	retry_after = pjsip_generic_int_hdr_create(rdata->tp_info.pool,
		&str_retry_after, 300);
	if (retry_after) {
		pj_list_push_back(&hdr_list, retry_after);
	}

	pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 503, NULL,
		retry_after ? &hdr_list : NULL, NULL);

	return PJ_TRUE;
}

static struct pjsip_module maintenance_pjsip_mod = {
	.name     = { "Maintenance Module", 18 },
	/*
	 * Run after endpoint identification (endpoint_mod,
	 * PJSIP_MOD_PRIORITY_TSX_LAYER - 3) so that
	 * ast_pjsip_rdata_get_endpoint() returns the identified endpoint,
	 * but before the request authenticator
	 * (PJSIP_MOD_PRIORITY_APPLICATION - 2) so that a maintenance
	 * endpoint receives 503 rather than a 401 challenge.
	 */
	.priority = PJSIP_MOD_PRIORITY_APPLICATION - 3,
	.on_rx_request = maintenance_on_rx_request,
};

/* Sorcery observer - clean up stale entries when an endpoint is deleted. */

static void maint_endpoint_deleted(const void *obj)
{
	maint_set_remove(ast_sorcery_object_get_id(obj));
}

static const struct ast_sorcery_observer endpoint_observer = {
	.deleted = maint_endpoint_deleted,
};

/* CLI helpers */

/*!
 * \internal
 * \brief Tab-complete a PJSIP endpoint name.
 */
static char *cli_complete_endpoint(const char *word)
{
	int wordlen = strlen(word);
	struct ao2_container *endpoints;
	struct ast_sip_endpoint *endpoint;
	struct ao2_iterator i;

	endpoints = ast_sorcery_retrieve_by_prefix(ast_sip_get_sorcery(),
		"endpoint", word, wordlen);
	if (!endpoints) {
		return NULL;
	}

	i = ao2_iterator_init(endpoints, 0);
	while ((endpoint = ao2_iterator_next(&i))) {
		ast_cli_completion_add(ast_strdup(ast_sorcery_object_get_id(endpoint)));
		ao2_cleanup(endpoint);
	}
	ao2_iterator_destroy(&i);
	ao2_ref(endpoints, -1);

	return NULL;
}

/* CLI: pjsip set maintenance <on|off> <endpoint|all> */

static char *handle_cli_pjsip_set_maintenance(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{
	struct ast_sip_endpoint *endpoint;
	struct ao2_container *all_endpoints;
	struct ao2_iterator it;
	const char *endpoint_name;
	int enable;
	int rc;
	int count;
	int failed;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip set maintenance";
		e->usage =
			"Usage: pjsip set maintenance <on|off> <endpoint|all>\n"
			"       Place a PJSIP endpoint into or out of maintenance mode.\n"
			"       Use 'all' to toggle maintenance mode for every endpoint.\n"
			"       While in maintenance mode new inbound INVITE and SUBSCRIBE\n"
			"       requests to that endpoint are rejected with 503, and outbound\n"
			"       originations are refused.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			static const char * const opts[] = { "on", "off", NULL };
			return ast_cli_complete(a->word, opts, a->n);
		}
		if (a->pos == 4) {
			if (!strncasecmp("all", a->word, strlen(a->word))) {
				ast_cli_completion_add(ast_strdup("all"));
			}
			return cli_complete_endpoint(a->word);
		}
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (!strcasecmp(a->argv[3], "on")) {
		enable = 1;
	} else if (!strcasecmp(a->argv[3], "off")) {
		enable = 0;
	} else {
		return CLI_SHOWUSAGE;
	}

	endpoint_name = a->argv[4];

	if (!strcasecmp(endpoint_name, "all")) {
		all_endpoints = ast_sip_get_endpoints();
		if (!all_endpoints) {
			ast_cli(a->fd, "Failed to retrieve endpoint list\n");
			return CLI_SUCCESS;
		}
		count = 0;
		failed = 0;
		it = ao2_iterator_init(all_endpoints, 0);
		while ((endpoint = ao2_iterator_next(&it))) {
			rc = apply_maintenance_state(ast_sorcery_object_get_id(endpoint), enable);
			if (rc > 0) {
				count++;
			} else if (rc < 0) {
				failed++;
			}
			ao2_ref(endpoint, -1);
		}
		ao2_iterator_destroy(&it);
		ao2_ref(all_endpoints, -1);
		if (count > 0) {
			manager_event(EVENT_FLAG_SYSTEM, "PJSIPMaintenanceStatus",
				"Endpoint: all\r\n"
				"Status: %s\r\n",
				enable ? "enabled" : "disabled");
			ast_log(LOG_NOTICE, "PJSIP: Maintenance mode %s for all endpoints "
				"(%d endpoint%s affected)\n",
				enable ? "enabled" : "disabled",
				count, count == 1 ? "" : "s");
		}
		ast_cli(a->fd, "Maintenance mode %s for %d endpoint%s%s\n",
			enable ? "ENABLED" : "DISABLED",
			count, count == 1 ? "" : "s",
			failed ? " (some failed)" : "");
		return CLI_SUCCESS;
	}

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", endpoint_name);
	if (!endpoint) {
		ast_cli(a->fd, "Endpoint '%s' not found\n", endpoint_name);
		return CLI_SUCCESS;
	}
	ao2_ref(endpoint, -1);

	rc = apply_maintenance_state(endpoint_name, enable);
	if (rc > 0) {
		manager_event(EVENT_FLAG_SYSTEM, "PJSIPMaintenanceStatus",
			"Endpoint: %s\r\n"
			"Status: %s\r\n",
			endpoint_name, enable ? "enabled" : "disabled");
		ast_log(LOG_NOTICE, "PJSIP: Maintenance mode %s for endpoint '%s'\n",
			enable ? "enabled" : "disabled", endpoint_name);
		ast_cli(a->fd, "Maintenance mode %s for endpoint '%s'\n",
			enable ? "ENABLED" : "DISABLED", endpoint_name);
	} else if (rc == 0 && enable) {
		ast_cli(a->fd, "Endpoint '%s' is already in maintenance mode\n", endpoint_name);
	} else if (rc == 0) {
		ast_cli(a->fd, "Endpoint '%s' was not in maintenance mode\n", endpoint_name);
	} else {
		ast_cli(a->fd, "Failed to %s maintenance mode for endpoint '%s'\n",
			enable ? "enable" : "disable", endpoint_name);
	}

	return CLI_SUCCESS;
}

/* CLI: pjsip show maintenance [endpoint] */

/*! \brief ao2_callback used to print one maintenance entry to the CLI */
static int cli_maint_entry_cb(void *obj, void *arg, int flags)
{
	const char *name = obj;
	int fd = *(int *)arg;
	ast_cli(fd, "  %-40s  ON\n", name);
	return 0;
}

static char *handle_cli_pjsip_show_maintenance(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{
	const char *endpoint_name;
	char *entry;
	int fd;
	int count;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip show maintenance";
		e->usage =
			"Usage: pjsip show maintenance [endpoint]\n"
			"       Display endpoints currently in maintenance mode.\n"
			"       If [endpoint] is given, show the status for that endpoint only.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return cli_complete_endpoint(a->word);
		}
		return NULL;
	}

	if (a->argc == 4) {
		endpoint_name = a->argv[3];
		entry = ao2_find(maintenance_set, endpoint_name, OBJ_SEARCH_KEY);
		if (entry) {
			ast_cli(a->fd, "Endpoint '%s' is in maintenance mode\n", endpoint_name);
			ao2_ref(entry, -1);
		} else {
			ast_cli(a->fd, "Endpoint '%s' is NOT in maintenance mode\n", endpoint_name);
		}
		return CLI_SUCCESS;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "\n");
	ast_cli(a->fd, "  %-40s  %s\n", "Endpoint", "State");
	ast_cli(a->fd, "  %-40s  -----\n", "----------------------------------------");
	fd = a->fd;
	ao2_callback(maintenance_set, OBJ_NODATA, cli_maint_entry_cb, &fd);

	count = ao2_container_count(maintenance_set);
	ast_cli(a->fd, "\n  %d endpoint%s in maintenance mode\n\n",
		count, count == 1 ? "" : "s");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_maintenance[] = {
	AST_CLI_DEFINE(handle_cli_pjsip_set_maintenance,  "Set PJSIP endpoint maintenance mode"),
	AST_CLI_DEFINE(handle_cli_pjsip_show_maintenance, "Show PJSIP endpoint maintenance status"),
};

/* AMI: PJSIPSetMaintenance, PJSIPShowMaintenance */

static int ami_set_maintenance(struct mansession *s, const struct message *m)
{
	const char *endpoint_name;
	const char *state_str;
	struct ast_sip_endpoint *endpoint;
	struct ao2_container *all_endpoints;
	struct ao2_iterator it;
	int enable;
	int rc;

	endpoint_name = astman_get_header(m, "Endpoint");
	state_str = astman_get_header(m, "State");

	if (ast_strlen_zero(endpoint_name)) {
		astman_send_error(s, m, "Endpoint parameter missing");
		return 0;
	}
	if (ast_strlen_zero(state_str)) {
		astman_send_error(s, m, "State parameter missing");
		return 0;
	}

	if (!strcasecmp(state_str, "on")) {
		enable = 1;
	} else if (!strcasecmp(state_str, "off")) {
		enable = 0;
	} else {
		astman_send_error(s, m, "State must be 'on' or 'off'");
		return 0;
	}

	if (!strcasecmp(endpoint_name, "all")) {
		int count = 0;

		all_endpoints = ast_sip_get_endpoints();
		if (!all_endpoints) {
			astman_send_error(s, m, "Failed to retrieve endpoint list");
			return 0;
		}
		it = ao2_iterator_init(all_endpoints, 0);
		while ((endpoint = ao2_iterator_next(&it))) {
			if (apply_maintenance_state(ast_sorcery_object_get_id(endpoint), enable) > 0) {
				count++;
			}
			ao2_ref(endpoint, -1);
		}
		ao2_iterator_destroy(&it);
		ao2_ref(all_endpoints, -1);
		if (count > 0) {
			manager_event(EVENT_FLAG_SYSTEM, "PJSIPMaintenanceStatus",
				"Endpoint: all\r\n"
				"Status: %s\r\n",
				enable ? "enabled" : "disabled");
			ast_log(LOG_NOTICE, "PJSIP: Maintenance mode %s for all endpoints "
				"(%d endpoint%s affected)\n",
				enable ? "enabled" : "disabled",
				count, count == 1 ? "" : "s");
		}
		astman_send_ack(s, m,
			enable ? "Maintenance mode enabled for all endpoints"
			       : "Maintenance mode disabled for all endpoints");
		return 0;
	}

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", endpoint_name);
	if (!endpoint) {
		astman_send_error_va(s, m, "Endpoint '%s' not found", endpoint_name);
		return 0;
	}
	ao2_ref(endpoint, -1);

	rc = apply_maintenance_state(endpoint_name, enable);
	if (rc < 0) {
		astman_send_error_va(s, m, "Failed to %s maintenance mode for endpoint '%s'",
			enable ? "enable" : "disable", endpoint_name);
	} else {
		if (rc > 0) {
			manager_event(EVENT_FLAG_SYSTEM, "PJSIPMaintenanceStatus",
				"Endpoint: %s\r\n"
				"Status: %s\r\n",
				endpoint_name, enable ? "enabled" : "disabled");
			ast_log(LOG_NOTICE, "PJSIP: Maintenance mode %s for endpoint '%s'\n",
				enable ? "enabled" : "disabled", endpoint_name);
		}
		astman_send_ack(s, m,
			enable ? "Maintenance mode enabled" : "Maintenance mode disabled");
	}

	return 0;
}

/*! \brief ao2_callback used to emit one PJSIPMaintenanceStatus AMI list entry */
static int ami_maint_entry_cb(void *obj, void *arg, int flags)
{
	const char *name = obj;
	struct ast_sip_ami *ami = arg;
	struct ast_str *buf;

	buf = ast_sip_create_ami_event("PJSIPMaintenanceStatus", ami);
	if (!buf) {
		return 0;
	}
	ast_str_append(&buf, 0, "Endpoint: %s\r\nStatus: enabled\r\n", name);
	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	ast_free(buf);
	++ami->count;

	return 0;
}

static int ami_show_maintenance(struct mansession *s, const struct message *m)
{
	const char *endpoint_name;
	struct ast_sip_ami ami;
	char *entry;
	struct ast_str *buf;

	endpoint_name = astman_get_header(m, "Endpoint");

	ami.s         = s;
	ami.m         = m;
	ami.action_id = astman_get_header(m, "ActionID");
	ami.arg       = NULL;
	ami.count     = 0;

	astman_send_listack(s, m, "Maintenance status events follow", "start");

	if (!ast_strlen_zero(endpoint_name)) {
		buf = ast_sip_create_ami_event("PJSIPMaintenanceStatus", &ami);
		if (buf) {
			entry = ao2_find(maintenance_set, endpoint_name, OBJ_SEARCH_KEY);
			ast_str_append(&buf, 0, "Endpoint: %s\r\nStatus: %s\r\n",
				endpoint_name, entry ? "enabled" : "disabled");
			if (entry) {
				ao2_ref(entry, -1);
			}
			astman_append(s, "%s\r\n", ast_str_buffer(buf));
			ast_free(buf);
		}
		ami.count = 1;
	} else {
		ao2_callback(maintenance_set, OBJ_NODATA, ami_maint_entry_cb, &ami);
	}

	astman_send_list_complete_start(s, m, "PJSIPMaintenanceStatusComplete", ami.count);
	astman_send_list_complete_end(s);

	return 0;
}

/* Module load / unload */

static int load_module(void)
{
	maintenance_set = ast_str_container_alloc_options(AO2_ALLOC_OPT_LOCK_RWLOCK,
		MAINT_HASH_BUCKETS);
	if (!maintenance_set) {
		ast_log(LOG_ERROR, "res_pjsip_maintenance: failed to allocate maintenance set\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_observer_add(ast_sip_get_sorcery(), "endpoint", &endpoint_observer);
	ast_sip_register_service(&maintenance_pjsip_mod);
	ast_sip_session_register_supplement(&maintenance_session_supplement);
	ast_manager_register_xml("PJSIPSetMaintenance",
		EVENT_FLAG_SYSTEM, ami_set_maintenance);
	ast_manager_register_xml("PJSIPShowMaintenance",
		EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, ami_show_maintenance);
	ast_cli_register_multiple(cli_maintenance, ARRAY_LEN(cli_maintenance));

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_maintenance, ARRAY_LEN(cli_maintenance));
	ast_manager_unregister("PJSIPShowMaintenance");
	ast_manager_unregister("PJSIPSetMaintenance");
	ast_sip_session_unregister_supplement(&maintenance_session_supplement);
	ast_sip_unregister_service(&maintenance_pjsip_mod);
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "endpoint", &endpoint_observer);
	ao2_cleanup(maintenance_set);
	maintenance_set = NULL;
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Endpoint Maintenance Mode",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load   = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);
