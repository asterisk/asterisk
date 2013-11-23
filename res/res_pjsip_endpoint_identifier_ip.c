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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/acl.h"
#include "asterisk/manager.h"
#include "res_pjsip/include/res_pjsip_private.h"

/*** DOCUMENTATION
	<configInfo name="res_pjsip_endpoint_identifier_ip" language="en_US">
		<synopsis>Module that identifies endpoints via source IP address</synopsis>
		<configFile name="pjsip.conf">
			<configObject name="identify">
				<synopsis>Identifies endpoints via source IP address</synopsis>
				<configOption name="endpoint">
					<synopsis>Name of Endpoint</synopsis>
				</configOption>
				<configOption name="match">
					<synopsis>IP addresses or networks to match against</synopsis>
					<description><para>
						The value is a comma-delimited list of IP addresses. IP addresses may
						have a subnet mask appended. The subnet mask may be written in either
						CIDR or dot-decimal notation. Separate the IP address and subnet
						mask with a slash ('/')
					</para></description>
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
	struct ip_identify_match *identify = ast_sorcery_generic_alloc(sizeof(*identify), ip_identify_destroy);

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
	int sense;

	sense = ast_apply_ha(identify->matches, addr);
	if (sense != AST_SENSE_ALLOW) {
		ast_debug(3, "Source address %s matches identify '%s'\n",
				ast_sockaddr_stringify(addr),
				ast_sorcery_object_get_id(identify));
		return CMP_MATCH | CMP_STOP;
	} else {
		ast_debug(3, "Source address %s does not match identify '%s'\n",
				ast_sockaddr_stringify(addr),
				ast_sorcery_object_get_id(identify));
		return 0;
	}
}

static struct ast_sip_endpoint *ip_identify(pjsip_rx_data *rdata)
{
	struct ast_sockaddr addr = { { 0, } };
	RAII_VAR(struct ao2_container *, candidates, NULL, ao2_cleanup);
	RAII_VAR(struct ip_identify_match *, match, NULL, ao2_cleanup);
	struct ast_sip_endpoint *endpoint;

	/* If no possibilities exist return early to save some time */
	if (!(candidates = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "identify", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL)) ||
		!ao2_container_count(candidates)) {
		ast_debug(3, "No identify sections to match against\n");
		return NULL;
	}

	ast_sockaddr_parse(&addr, rdata->pkt_info.src_name, PARSE_PORT_FORBID);
	ast_sockaddr_set_port(&addr, rdata->pkt_info.src_port);

	if (!(match = ao2_callback(candidates, 0, ip_identify_match_check, &addr))) {
		ast_debug(3, "'%s' did not match any identify section rules\n",
				ast_sockaddr_stringify(&addr));
		return NULL;
	}

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", match->endpoint_name);
	if (endpoint) {
		ast_debug(3, "Retrieved endpoint %s\n", ast_sorcery_object_get_id(endpoint));
	} else {
		ast_log(LOG_WARNING, "Identify section '%s' points to endpoint '%s' but endpoint could not be looked up\n",
				ast_sorcery_object_get_id(match), match->endpoint_name);
	}

	return endpoint;
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

static int ip_identify_match_to_str(const void *obj, const intptr_t *args, char **buf)
{
	RAII_VAR(struct ast_str *, str, ast_str_create(MAX_OBJECT_FIELD), ast_free);
	const struct ip_identify_match *identify = obj;

	ast_ha_join(identify->matches, &str);
	*buf = ast_strdup(ast_str_buffer(str));
	return 0;
}

static int sip_identify_to_ami(const struct ip_identify_match *identify,
			       struct ast_str **buf)
{
	return ast_sip_sorcery_object_to_ami(identify, buf);
}

static int find_identify_by_endpoint(void *obj, void *arg, int flags)
{
	struct ip_identify_match *identify = obj;
	const char *endpoint_name = arg;

	return strcmp(identify->endpoint_name, endpoint_name) ? 0 : CMP_MATCH | CMP_STOP;
}

static int format_ami_endpoint_identify(const struct ast_sip_endpoint *endpoint,
					struct ast_sip_ami *ami)
{
	RAII_VAR(struct ao2_container *, identifies, NULL, ao2_cleanup);
	RAII_VAR(struct ip_identify_match *, identify, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, buf, NULL, ast_free);

	if (!(identifies = ast_sorcery_retrieve_by_fields(
		      ast_sip_get_sorcery(), "identify", AST_RETRIEVE_FLAG_MULTIPLE |
		      AST_RETRIEVE_FLAG_ALL, NULL))) {
		return -1;
	}

	if (!(identify = ao2_callback(identifies, OBJ_NOLOCK,
				      find_identify_by_endpoint,
				      (void*)ast_sorcery_object_get_id(endpoint)))) {
		return 1;
	}

	if (!(buf = ast_sip_create_ami_event("IdentifyDetail", ami))) {
		return -1;
	}

	if (sip_identify_to_ami(identify, &buf)) {
		return -1;
	}

	ast_str_append(&buf, 0, "EndpointName: %s\r\n",
		       ast_sorcery_object_get_id(endpoint));

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	return 0;
}

struct ast_sip_endpoint_formatter endpoint_identify_formatter = {
	.format_ami = format_ami_endpoint_identify
};

static int load_module(void)
{
	ast_sorcery_apply_default(ast_sip_get_sorcery(), "identify", "config", "pjsip.conf,criteria=type=identify");

	if (ast_sorcery_object_register(ast_sip_get_sorcery(), "identify", ip_identify_alloc, NULL, NULL)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "identify", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "identify", "endpoint", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ip_identify_match, endpoint_name));
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "identify", "match", "", ip_identify_match_handler, ip_identify_match_to_str, 0, 0);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "identify");

	ast_sip_register_endpoint_identifier(&ip_identifier);
	ast_sip_register_endpoint_formatter(&endpoint_identify_formatter);

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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP IP endpoint identifier",
		.load = load_module,
		.reload = reload_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
