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
	<depend>res_sip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_sip.h"
#include "asterisk/module.h"
#include "asterisk/acl.h"

/*** DOCUMENTATION
	<configInfo name="res_sip_endpoint_identifier_ip" language="en_US">
		<synopsis>Module that identifies endpoints via source IP address</synopsis>
		<configFile name="res_sip.conf">
			<configObject name="identify">
				<configOption name="endpoint">
					<synopsis>Name of Endpoint</synopsis>
				</configOption>
				<configOption name="match">
					<synopsis>IP addresses or networks to match against</synopsis>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'identify'.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

/*! \brief Structure for an IP identification matching object */
struct ip_identify_match {
	/*! \brief Sorcery object details */
	SORCERY_OBJECT(details);
	/*! \brief Stringfields */
	AST_DECLARE_STRING_FIELDS(
		/*! The name of the endpoint */
		AST_STRING_FIELD(endpoint_name);
	);
	/*! \brief Networks or addresses that should match this */
	struct ast_ha *matches;
};

/*! \brief Destructor function for a matching object */
static void ip_identify_destroy(void *obj)
{
	struct ip_identify_match *identify = obj;

	ast_string_field_free_memory(identify);
	ast_free_ha(identify->matches);
}

/*! \brief Allocator function for a matching object */
static void *ip_identify_alloc(const char *name)
{
	struct ip_identify_match *identify = ao2_alloc(sizeof(*identify), ip_identify_destroy);

	if (!identify || ast_string_field_init(identify, 256)) {
		ao2_cleanup(identify);
		return NULL;
	}

	return identify;
}

/*! \brief Comparator function for a matching object */
static int ip_identify_match_check(void *obj, void *arg, int flags)
{
	struct ip_identify_match *identify = obj;
	struct ast_sockaddr *addr = arg;

	return (ast_apply_ha(identify->matches, addr) != AST_SENSE_ALLOW) ? CMP_MATCH | CMP_STOP : 0;
}

static struct ast_sip_endpoint *ip_identify(pjsip_rx_data *rdata)
{
	struct ast_sockaddr addr = { { 0, } };
	RAII_VAR(struct ao2_container *, candidates, NULL, ao2_cleanup);
	RAII_VAR(struct ip_identify_match *, match, NULL, ao2_cleanup);

	/* If no possibilities exist return early to save some time */
	if (!(candidates = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "identify", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL)) ||
		!ao2_container_count(candidates)) {
		return NULL;
	}

	ast_sockaddr_parse(&addr, rdata->pkt_info.src_name, PARSE_PORT_FORBID);
	ast_sockaddr_set_port(&addr, rdata->pkt_info.src_port);

	if (!(match = ao2_callback(candidates, 0, ip_identify_match_check, &addr))) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", match->endpoint_name);
}

static struct ast_sip_endpoint_identifier ip_identifier = {
	.identify_endpoint = ip_identify,
};

/*! \brief Custom handler for match field */
static int ip_identify_match_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ip_identify_match *identify = obj;
	int error = 0;

	/* We deny what we actually want to match because there is an implicit permit all rule for ACLs */
	if (!(identify->matches = ast_append_ha("d", var->value, identify->matches, &error))) {
		return -1;
	}

	return error;
}

static int load_module(void)
{
	ast_sorcery_apply_default(ast_sip_get_sorcery(), "identify", "config", "res_sip.conf,criteria=type=identify");

	if (ast_sorcery_object_register(ast_sip_get_sorcery(), "identify", ip_identify_alloc, NULL, NULL)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "identify", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "identify", "endpoint", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ip_identify_match, endpoint_name));
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "identify", "match", "", ip_identify_match_handler, NULL, 0, 0);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "identify");

	ast_sip_register_endpoint_identifier(&ip_identifier);

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "identify");
	return 0;
}

static int unload_module(void)
{
	ast_sip_unregister_endpoint_identifier(&ip_identifier);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SIP IP endpoint identifier",
		.load = load_module,
		.reload = reload_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
