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
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/module.h"
#include "asterisk/acl.h"
#include "asterisk/manager.h"
#include "res_pjsip/include/res_pjsip_private.h"

/*** DOCUMENTATION
	<configInfo name="res_pjsip_endpoint_identifier_ip" language="en_US">
		<synopsis>Module that identifies endpoints</synopsis>
		<configFile name="pjsip.conf">
			<configObject name="identify">
				<synopsis>Identifies endpoints via some criteria.</synopsis>
				<description>
					<para>This module provides alternatives to matching inbound requests to
					a configured endpoint. At least one of the matching mechanisms
					must be provided, or the object configuration will be invalid.</para>
					<para>If multiple criteria are provided, an inbound request will
					be matched if it matches <emphasis>any</emphasis> of the criteria.</para>
					<para>The matching mechanisms are provided by the following
					configuration options:</para>
					<enumlist>
						<enum name="match"><para>Match by source IP address.</para></enum>
						<enum name="match_header"><para>Match by SIP header.</para></enum>
					</enumlist>
				</description>
				<configOption name="endpoint">
					<synopsis>Name of Endpoint</synopsis>
				</configOption>
				<configOption name="match">
					<synopsis>IP addresses or networks to match against.</synopsis>
					<description><para>
						The value is a comma-delimited list of IP addresses. IP addresses may
						have a subnet mask appended. The subnet mask may be written in either
						CIDR or dot-decimal notation. Separate the IP address and subnet
						mask with a slash ('/').
					</para></description>
				</configOption>
				<configOption name="srv_lookups" default="yes">
					<synopsis>Perform SRV lookups for provided hostnames.</synopsis>
					<description><para>When enabled, <replaceable>srv_lookups</replaceable> will
					perform SRV lookups for _sip._udp, _sip._tcp, and _sips._tcp of the given
					hostnames to determine additional addresses that traffic may originate from.
					</para></description>
				</configOption>
				<configOption name="match_header">
					<synopsis>Header/value pair to match against.</synopsis>
					<description><para>A SIP header who value is used to match against. SIP
					requests containing the header, along with the specified value, will be
					mapped to the specified endpoint. The header must be specified with a
					<literal>:</literal>, as in <literal>match_header = SIPHeader: value</literal>.
					</para></description>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'identify'.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

/*! \brief The number of buckets for storing hosts for resolution */
#define HOSTS_BUCKETS 53

/*! \brief Structure for an IP identification matching object */
struct ip_identify_match {
	/*! \brief Sorcery object details */
	SORCERY_OBJECT(details);
	/*! \brief Stringfields */
	AST_DECLARE_STRING_FIELDS(
		/*! The name of the endpoint */
		AST_STRING_FIELD(endpoint_name);
		/*! If matching by header, the header/value to match against */
		AST_STRING_FIELD(match_header);
	);
	/*! \brief Networks or addresses that should match this */
	struct ast_ha *matches;
	/*! \brief Perform SRV resolution of hostnames */
	unsigned int srv_lookups;
	/*! \brief Hosts to be resolved after applying configuration */
	struct ao2_container *hosts;
};

/*! \brief Destructor function for a matching object */
static void ip_identify_destroy(void *obj)
{
	struct ip_identify_match *identify = obj;

	ast_string_field_free_memory(identify);
	ast_free_ha(identify->matches);
	ao2_cleanup(identify->hosts);
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

/*! \brief Comparator function for matching an object by header */
static int header_identify_match_check(void *obj, void *arg, int flags)
{
	struct ip_identify_match *identify = obj;
	struct pjsip_rx_data *rdata = arg;
	pjsip_generic_string_hdr *header;
	pj_str_t pj_header_name;
	pj_str_t pj_header_value;
	char *c_header;
	char *c_value;

	if (ast_strlen_zero(identify->match_header)) {
		return 0;
	}

	c_header = ast_strdupa(identify->match_header);
	c_value = strchr(c_header, ':');
	if (!c_value) {
		ast_log(LOG_WARNING, "Identify '%s' has invalid header_match: No ':' separator found!\n",
			ast_sorcery_object_get_id(identify));
		return 0;
	}
	*c_value = '\0';
	c_value++;
	c_value = ast_strip(c_value);

	pj_header_name = pj_str(c_header);
	header = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &pj_header_name, NULL);
	if (!header) {
		ast_debug(3, "SIP message does not contain header '%s'\n", c_header);
		return 0;
	}

	pj_header_value = pj_str(c_value);
	if (pj_strcmp(&pj_header_value, &header->hvalue)) {
		ast_debug(3, "SIP message contains header '%s' but value '%.*s' does not match value '%s' for endpoint '%s'\n",
			c_header,
			(int) pj_strlen(&header->hvalue), pj_strbuf(&header->hvalue),
			c_value,
			identify->endpoint_name);
		return 0;
	}

	return CMP_MATCH | CMP_STOP;
}

/*! \brief Comparator function for matching an object by IP address */
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

	match = ao2_callback(candidates, 0, ip_identify_match_check, &addr);
	if (!match) {
		ast_debug(3, "Identify checks by IP address failed to find match: '%s' did not match any identify section rules\n",
				ast_sockaddr_stringify(&addr));
		match = ao2_callback(candidates, 0, header_identify_match_check, rdata);
		if (!match) {
			return NULL;
		}
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

/*! \brief Helper function which performs a host lookup and adds result to identify match */
static int ip_identify_match_host_lookup(struct ip_identify_match *identify, const char *host)
{
	struct ast_sockaddr *addrs;
	int num_addrs = 0, error = 0, i;
	int results = 0;

	num_addrs = ast_sockaddr_resolve(&addrs, host, PARSE_PORT_FORBID, AST_AF_UNSPEC);
	if (!num_addrs) {
		return -1;
	}

	for (i = 0; i < num_addrs; ++i) {
		/* Check if the address is already in the list, if so don't bother adding it again */
		if (identify->matches && (ast_apply_ha(identify->matches, &addrs[i]) != AST_SENSE_ALLOW)) {
			continue;
		}

		/* We deny what we actually want to match because there is an implicit permit all rule for ACLs */
		identify->matches = ast_append_ha("d", ast_sockaddr_stringify_addr(&addrs[i]), identify->matches, &error);

		if (!identify->matches || error) {
			results = -1;
			break;
		}

		results += 1;
	}

	ast_free(addrs);

	return results;
}

/*! \brief Helper function which performs an SRV lookup and then resolves the hostname */
static int ip_identify_match_srv_lookup(struct ip_identify_match *identify, const char *prefix, const char *host)
{
	char service[NI_MAXHOST];
	struct srv_context *context = NULL;
	int srv_ret;
	const char *srvhost;
	unsigned short srvport;
	int results = 0;

	snprintf(service, sizeof(service), "%s.%s", prefix, host);

	while (!(srv_ret = ast_srv_lookup(&context, service, &srvhost, &srvport))) {
		int hosts;

		/* In the case of the SRV lookup we don't care if it fails, we will output a log message
		 * when we fallback to a normal lookup.
		 */
		hosts = ip_identify_match_host_lookup(identify, srvhost);
		if (hosts == -1) {
			results = -1;
			break;
		} else {
			results += hosts;
		}
	}

	ast_srv_cleanup(&context);

	return results;
}

/*! \brief Custom handler for match field */
static int ip_identify_match_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ip_identify_match *identify = obj;
	char *input_string = ast_strdupa(var->value);
	char *current_string;

	if (ast_strlen_zero(var->value)) {
		return 0;
	}

	while ((current_string = ast_strip(strsep(&input_string, ",")))) {
		char *mask = strrchr(current_string, '/');
		int error = 0;

		if (ast_strlen_zero(current_string)) {
			continue;
		}

		if (mask) {
			identify->matches = ast_append_ha("d", current_string, identify->matches, &error);

			if (!identify->matches || error) {
				ast_log(LOG_ERROR, "Failed to add address '%s' to ip endpoint identifier '%s'\n",
					current_string, ast_sorcery_object_get_id(obj));
				return -1;
			}

			continue;
		}

		if (!identify->hosts) {
			identify->hosts = ast_str_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, HOSTS_BUCKETS);
			if (!identify->hosts) {
				ast_log(LOG_ERROR, "Failed to create container to store hosts on ip endpoint identifier '%s'\n",
					ast_sorcery_object_get_id(obj));
				return -1;
			}
		}

		error = ast_str_container_add(identify->hosts, current_string);
		if (error) {
			ast_log(LOG_ERROR, "Failed to store host '%s' for resolution on ip endpoint identifier '%s'\n",
				current_string, ast_sorcery_object_get_id(obj));
			return -1;
		}
	}

	return 0;
}

/*! \brief Apply handler for identify type */
static int ip_identify_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ip_identify_match *identify = obj;
	char *current_string;
	struct ao2_iterator i;

	if (!identify->hosts) {
		return 0;
	}

	i = ao2_iterator_init(identify->hosts, 0);
	while ((current_string = ao2_iterator_next(&i))) {
		struct ast_sockaddr address;
		int results = 0;

		/* If the provided string is not an IP address perform SRV resolution on it */
		if (identify->srv_lookups && !ast_sockaddr_parse(&address, current_string, 0)) {
			results = ip_identify_match_srv_lookup(identify, "_sip._udp", current_string);
			if (results != -1) {
				results += ip_identify_match_srv_lookup(identify, "_sip._tcp", current_string);
			}
			if (results != -1) {
				results += ip_identify_match_srv_lookup(identify, "_sips._tcp", current_string);
			}
		}

		/* If SRV falls fall back to a normal lookup on the host itself */
		if (!results) {
			results = ip_identify_match_host_lookup(identify, current_string);
		}

		if (results == 0) {
			ast_log(LOG_ERROR, "Address '%s' provided on ip endpoint identifier '%s' did not resolve to any address\n",
				current_string, ast_sorcery_object_get_id(obj));
		} else if (results == -1) {
			ast_log(LOG_ERROR, "An error occurred when adding resolution results of '%s' on '%s'\n",
				current_string, ast_sorcery_object_get_id(obj));
			ao2_ref(current_string, -1);
			ao2_iterator_destroy(&i);
			return -1;
		}

		ao2_ref(current_string, -1);
	}
	ao2_iterator_destroy(&i);

	ao2_ref(identify->hosts, -1);
	identify->hosts = NULL;

	return 0;
}

static int match_to_str(const void *obj, const intptr_t *args, char **buf)
{
	RAII_VAR(struct ast_str *, str, ast_str_create(MAX_OBJECT_FIELD), ast_free);
	const struct ip_identify_match *identify = obj;

	ast_ha_join(identify->matches, &str);
	*buf = ast_strdup(ast_str_buffer(str));
	return 0;
}

static int match_to_var_list(const void *obj, struct ast_variable **fields)
{
	char str[MAX_OBJECT_FIELD];
	const struct ip_identify_match *identify = obj;
	struct ast_variable *head = NULL;
	struct ast_ha *ha = identify->matches;

	for (; ha; ha = ha->next) {
		const char *addr = ast_strdupa(ast_sockaddr_stringify_addr(&ha->addr));
		snprintf(str, MAX_OBJECT_FIELD, "%s%s/%s", ha->sense == AST_SENSE_ALLOW ? "!" : "",
			addr, ast_sockaddr_stringify_addr(&ha->netmask));

		ast_variable_list_append(&head, ast_variable_new("match", str, ""));

	}

	if (head) {
		*fields = head;
	}

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

	return strcmp(identify->endpoint_name, endpoint_name) ? 0 : CMP_MATCH;
}

static int format_ami_endpoint_identify(const struct ast_sip_endpoint *endpoint,
					struct ast_sip_ami *ami)
{
	RAII_VAR(struct ao2_container *, identifies, NULL, ao2_cleanup);
	RAII_VAR(struct ip_identify_match *, identify, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, buf, NULL, ast_free);

	identifies = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "identify",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!identifies) {
		return -1;
	}

	identify = ao2_callback(identifies, 0, find_identify_by_endpoint,
		(void *) ast_sorcery_object_get_id(endpoint));
	if (!identify) {
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
	ami->count++;

	return 0;
}

struct ast_sip_endpoint_formatter endpoint_identify_formatter = {
	.format_ami = format_ami_endpoint_identify
};

static int cli_iterator(void *container, ao2_callback_fn callback, void *args)
{
	const struct ast_sip_endpoint *endpoint = container;
	struct ao2_container *identifies;

	struct ast_variable fields = {
		.name = "endpoint",
		.value = ast_sorcery_object_get_id(endpoint),
		.next = NULL,
	};

	identifies = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "identify",
		AST_RETRIEVE_FLAG_MULTIPLE, &fields);
	if (!identifies) {
		return -1;
	}

	ao2_callback(identifies, OBJ_NODATA, callback, args);
	ao2_cleanup(identifies);

	return 0;
}

static struct ao2_container *cli_get_container(const char *regex)
{
	RAII_VAR(struct ao2_container *, container, NULL, ao2_cleanup);
	struct ao2_container *s_container;

	container =  ast_sorcery_retrieve_by_regex(ast_sip_get_sorcery(), "identify", regex);
	if (!container) {
		return NULL;
	}

	s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, ast_sorcery_object_id_compare);
	if (!s_container) {
		return NULL;
	}

	if (ao2_container_dup(s_container, container, 0)) {
		ao2_ref(s_container, -1);
		return NULL;
	}

	return s_container;
}

static void *cli_retrieve_by_id(const char *id)
{
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "identify", id);
}

static int cli_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_MAX_WIDTH - indent - 22;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <Identify/Endpoint%*.*s>\n",
		indent, "Identify", filler, filler, CLI_HEADER_FILLER);

	if (context->recurse) {
		context->indent_level++;
		indent = CLI_INDENT_TO_SPACES(context->indent_level);
		filler = CLI_LAST_TABSTOP - indent - 24;

		ast_str_append(&context->output_buffer, 0,
			"%*s:  <criteria%*.*s>\n",
			indent, "Match", filler, filler, CLI_HEADER_FILLER);

		context->indent_level--;
	}

	return 0;
}

static int cli_print_body(void *obj, void *arg, int flags)
{
	RAII_VAR(struct ast_str *, str, ast_str_create(MAX_OBJECT_FIELD), ast_free);
	struct ip_identify_match *ident = obj;
	struct ast_sip_cli_context *context = arg;
	struct ast_ha *match;
	int indent;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0, "%*s:  %s/%s\n",
		CLI_INDENT_TO_SPACES(context->indent_level), "Identify",
		ast_sorcery_object_get_id(ident), ident->endpoint_name);

	if (context->recurse) {
		context->indent_level++;
		indent = CLI_INDENT_TO_SPACES(context->indent_level);

		for (match = ident->matches; match; match = match->next) {
			const char *addr = ast_sockaddr_stringify_addr(&match->addr);

			ast_str_append(&context->output_buffer, 0, "%*s: %s%s/%d\n",
				indent,
				"Match",
				match->sense == AST_SENSE_ALLOW ? "!" : "",
				addr, ast_sockaddr_cidr_bits(&match->netmask));
		}

		if (!ast_strlen_zero(ident->match_header)) {
			ast_str_append(&context->output_buffer, 0, "%*s: %s\n",
				indent,
				"Match",
				ident->match_header);
		}

		context->indent_level--;

		if (context->indent_level == 0) {
			ast_str_append(&context->output_buffer, 0, "\n");
		}
	}

	if (context->show_details
		|| (context->show_details_only_level_0 && context->indent_level == 0)) {
		ast_str_append(&context->output_buffer, 0, "\n");
		ast_sip_cli_print_sorcery_objectset(ident, context, 0);
	}

	return 0;
}

/*
 * A function pointer to callback needs to be within the
 * module in order to avoid problems with an undefined
 * symbol when the module is loaded.
 */
static char *my_cli_traverse_objects(struct ast_cli_entry *e, int cmd,
	struct ast_cli_args *a)
{
	return ast_sip_cli_traverse_objects(e, cmd, a);
}

static struct ast_cli_entry cli_identify[] = {
AST_CLI_DEFINE(my_cli_traverse_objects, "List PJSIP Identifies",
	.command = "pjsip list identifies",
	.usage = "Usage: pjsip list identifies [ like <pattern> ]\n"
	"       List the configured PJSIP Identifies\n"
	"       Optional regular expression pattern is used to filter the list.\n"),
AST_CLI_DEFINE(my_cli_traverse_objects, "Show PJSIP Identifies",
	.command = "pjsip show identifies",
	.usage = "Usage: pjsip show identifies [ like <pattern> ]\n"
	"       Show the configured PJSIP Identifies\n"
	"       Optional regular expression pattern is used to filter the list.\n"),
AST_CLI_DEFINE(my_cli_traverse_objects, "Show PJSIP Identify",
	.command = "pjsip show identify",
	.usage = "Usage: pjsip show identify <id>\n"
	"       Show the configured PJSIP Identify\n"),
};

static struct ast_sip_cli_formatter_entry *cli_formatter;

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	ast_sorcery_apply_config(ast_sip_get_sorcery(), "res_pjsip_endpoint_identifier_ip");
	ast_sorcery_apply_default(ast_sip_get_sorcery(), "identify", "config", "pjsip.conf,criteria=type=identify");

	if (ast_sorcery_object_register(ast_sip_get_sorcery(), "identify", ip_identify_alloc, NULL, ip_identify_apply)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "identify", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "identify", "endpoint", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ip_identify_match, endpoint_name));
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "identify", "match", "", ip_identify_match_handler, match_to_str, match_to_var_list, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "identify", "match_header", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ip_identify_match, match_header));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "identify", "srv_lookups", "yes", OPT_BOOL_T, 1, FLDSET(struct ip_identify_match, srv_lookups));
	ast_sorcery_load_object(ast_sip_get_sorcery(), "identify");

	ast_sip_register_endpoint_identifier_with_name(&ip_identifier, "ip");
	ast_sip_register_endpoint_formatter(&endpoint_identify_formatter);

	cli_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!cli_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for cli formatter\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	cli_formatter->name = "identify";
	cli_formatter->print_header = cli_print_header;
	cli_formatter->print_body = cli_print_body;
	cli_formatter->get_container = cli_get_container;
	cli_formatter->iterate = cli_iterator;
	cli_formatter->get_id = ast_sorcery_object_get_id;
	cli_formatter->retrieve_by_id = cli_retrieve_by_id;

	ast_sip_register_cli_formatter(cli_formatter);
	ast_cli_register_multiple(cli_identify, ARRAY_LEN(cli_identify));

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "identify");

	return 0;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_identify, ARRAY_LEN(cli_identify));
	ast_sip_unregister_cli_formatter(cli_formatter);
	ast_sip_unregister_endpoint_formatter(&endpoint_identify_formatter);
	ast_sip_unregister_endpoint_identifier(&ip_identifier);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP IP endpoint identifier",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.reload = reload_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND - 4,
	       );
