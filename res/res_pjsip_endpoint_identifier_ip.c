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
					must be provided, or the object configuration is invalid.</para>
					<para>The matching mechanisms are provided by the following
					configuration options:</para>
					<enumlist>
						<enum name="match"><para>Match by source IP address.</para></enum>
						<enum name="match_header"><para>Match by SIP header.</para></enum>
					</enumlist>
					<note><para>If multiple matching criteria are provided then an inbound
					request will be matched to the endpoint if it matches
					<emphasis>any</emphasis> of the criteria.</para></note>
				</description>
				<configOption name="endpoint">
					<synopsis>Name of endpoint identified</synopsis>
				</configOption>
				<configOption name="match">
					<synopsis>IP addresses or networks to match against.</synopsis>
					<description>
						<para>The value is a comma-delimited list of IP addresses or
						hostnames.</para>
						<para>IP addresses may have a subnet mask appended. The subnet
						mask may be written in either CIDR or dotted-decimal
						notation. Separate the IP address and subnet mask with a slash
						('/'). A source port can also be specified by adding a colon (':')
						after the address but before the subnet mask, e.g.
						3.2.1.0:5061/24. To specify a source port for an IPv6 address, the
						address itself must be enclosed in square brackets
						('[2001:db8:0::1]:5060')</para>
						<para>When a hostname is used, the behavior depends on whether
						<replaceable>srv_lookups</replaceable> is enabled and/or a source
						port is provided. If <replaceable>srv_lookups</replaceable> is
						enabled and a source port is not provided, Asterisk will perform
						an SRV lookup on the provided hostname, adding all of the A and
						AAAA records that are resolved.</para>
						<para>If the SRV lookup fails,
						<replaceable>srv_lookups</replaceable> is disabled, or a source
						port is specified when the hostname is configured, Asterisk will
						resolve the hostname and add all A and AAAA records that are
						resolved.</para>
					</description>
				</configOption>
				<configOption name="srv_lookups" default="yes">
					<synopsis>Perform SRV lookups for provided hostnames.</synopsis>
					<description>
						<para>When enabled, <replaceable>srv_lookups</replaceable> will
						perform SRV lookups for _sip._udp, _sip._tcp, and _sips._tcp of
						the given hostnames to determine additional addresses that traffic
						may originate from.
						</para>
					</description>
				</configOption>
				<configOption name="match_header">
					<synopsis>Header/value pair to match against.</synopsis>
					<description>
						<para>A SIP header whose value is used to match against.  SIP
						requests containing the header, along with the specified value,
						will be mapped to the specified endpoint.  The header must be
						specified with a <literal>:</literal>, as in
						<literal>match_header = SIPHeader: value</literal>.
						</para>
						<para>The specified SIP header value can be a regular
						expression if the value is of the form
						/<replaceable>regex</replaceable>/.
						</para>
						<note><para>Use of a regex is expensive so be sure you need
						to use a regex to match your endpoint.
						</para></note>
					</description>
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
		/*! SIP header name of the match_header string */
		AST_STRING_FIELD(match_header_name);
		/*! SIP header value of the match_header string */
		AST_STRING_FIELD(match_header_value);
	);
	/*! Compiled match_header regular expression when is_regex is non-zero */
	regex_t regex_buf;
	/*! \brief Networks or addresses that should match this */
	struct ast_ha *matches;
	/*! \brief Hosts to be resolved when applying configuration */
	struct ao2_container *hosts;
	/*! \brief Perform SRV resolution of hostnames */
	unsigned int srv_lookups;
	/*! Non-zero if match_header has a regular expression (i.e., regex_buf is valid) */
	unsigned int is_regex:1;
};

/*! \brief Destructor function for a matching object */
static void ip_identify_destroy(void *obj)
{
	struct ip_identify_match *identify = obj;

	ast_string_field_free_memory(identify);
	ast_free_ha(identify->matches);
	ao2_cleanup(identify->hosts);
	if (identify->is_regex) {
		regfree(&identify->regex_buf);
	}
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
	pjsip_hdr *header;
	pj_str_t pj_header_name;
	int header_present;

	if (ast_strlen_zero(identify->match_header)) {
		return 0;
	}

	pj_header_name = pj_str((void *) identify->match_header_name);

	/* Check all headers of the given name for a match. */
	header_present = 0;
	for (header = NULL;
		(header = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &pj_header_name, header));
		header = header->next) {
		char *pos;
		int len;
		char buf[PATH_MAX];

		header_present = 1;

		/* Print header line to buf */
		len = pjsip_hdr_print_on(header, buf, sizeof(buf) - 1);
		if (len < 0) {
			/* Buffer not large enough or no header vptr! */
			ast_assert(0);
			continue;
		}
		buf[len] = '\0';

		/* Remove header name from pj_buf and trim blanks. */
		pos = strchr(buf, ':');
		if (!pos) {
			/* No header name?  Bug in PJPROJECT if so. */
			ast_assert(0);
			continue;
		}
		pos = ast_strip(pos + 1);

		/* Does header value match what we are looking for? */
		if (identify->is_regex) {
			if (!regexec(&identify->regex_buf, pos, 0, NULL, 0)) {
				return CMP_MATCH;
			}
		} else if (!strcmp(identify->match_header_value, pos)) {
			return CMP_MATCH;
		}

		ast_debug(3, "Identify '%s': SIP message has '%s' header but value '%s' does not match '%s'.\n",
			ast_sorcery_object_get_id(identify),
			identify->match_header_name,
			pos,
			identify->match_header_value);
	}
	if (!header_present) {
		ast_debug(3, "Identify '%s': SIP message does not have '%s' header.\n",
			ast_sorcery_object_get_id(identify),
			identify->match_header_name);
	}
	return 0;
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
		return CMP_MATCH;
	} else {
		ast_debug(3, "Source address %s does not match identify '%s'\n",
				ast_sockaddr_stringify(addr),
				ast_sorcery_object_get_id(identify));
		return 0;
	}
}

static struct ast_sip_endpoint *common_identify(ao2_callback_fn *identify_match_cb, void *arg)
{
	RAII_VAR(struct ao2_container *, candidates, NULL, ao2_cleanup);
	struct ip_identify_match *match;
	struct ast_sip_endpoint *endpoint;

	/* If no possibilities exist return early to save some time */
	candidates = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "identify",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!candidates || !ao2_container_count(candidates)) {
		ast_debug(3, "No identify sections to match against\n");
		return NULL;
	}

	match = ao2_callback(candidates, 0, identify_match_cb, arg);
	if (!match) {
		return NULL;
	}

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
		match->endpoint_name);
	if (endpoint) {
		ast_debug(3, "Identify '%s' SIP message matched to endpoint %s\n",
			ast_sorcery_object_get_id(match), match->endpoint_name);
	} else {
		ast_log(LOG_WARNING, "Identify '%s' points to endpoint '%s' but endpoint could not be found\n",
			ast_sorcery_object_get_id(match), match->endpoint_name);
	}

	ao2_ref(match, -1);
	return endpoint;
}

static struct ast_sip_endpoint *ip_identify(pjsip_rx_data *rdata)
{
	struct ast_sockaddr addr = { { 0, } };

	ast_sockaddr_parse(&addr, rdata->pkt_info.src_name, PARSE_PORT_FORBID);
	ast_sockaddr_set_port(&addr, rdata->pkt_info.src_port);

	return common_identify(ip_identify_match_check, &addr);
}

static struct ast_sip_endpoint_identifier ip_identifier = {
	.identify_endpoint = ip_identify,
};

static struct ast_sip_endpoint *header_identify(pjsip_rx_data *rdata)
{
	return common_identify(header_identify_match_check, rdata);
}

static struct ast_sip_endpoint_identifier header_identifier = {
	.identify_endpoint = header_identify,
};

/*! \brief Helper function which performs a host lookup and adds result to identify match */
static int ip_identify_match_host_lookup(struct ip_identify_match *identify, const char *host)
{
	struct ast_sockaddr *addrs;
	int num_addrs = 0, error = 0, i;
	int results = 0;

	num_addrs = ast_sockaddr_resolve(&addrs, host, 0, AST_AF_UNSPEC);
	if (!num_addrs) {
		return -1;
	}

	for (i = 0; i < num_addrs; ++i) {
		/* Check if the address is already in the list, if so don't add it again */
		if (identify->matches && (ast_apply_ha(identify->matches, &addrs[i]) != AST_SENSE_ALLOW)) {
			continue;
		}

		/* We deny what we actually want to match because there is an implicit permit all rule for ACLs */
		identify->matches = ast_append_ha_with_port("d", ast_sockaddr_stringify(&addrs[i]), identify->matches, &error);

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
static int ip_identify_match_srv_lookup(struct ip_identify_match *identify, const char *prefix, const char *host, int results)
{
	char service[NI_MAXHOST];
	struct srv_context *context = NULL;
	int srv_ret;
	const char *srvhost;
	unsigned short srvport;

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
		char *mask;
		struct ast_sockaddr address;
		int error = 0;

		if (ast_strlen_zero(current_string)) {
			continue;
		}

		mask = strrchr(current_string, '/');

		/* If it looks like a netmask is present, or we can immediately parse as an IP,
		 * hand things off to the ACL */
		if (mask || ast_sockaddr_parse(&address, current_string, 0)) {
			identify->matches = ast_append_ha_with_port("d", current_string, identify->matches, &error);

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

	/* Validate the identify object configuration */
	if (ast_strlen_zero(identify->endpoint_name)) {
		ast_log(LOG_ERROR, "Identify '%s' missing required endpoint name.\n",
			ast_sorcery_object_get_id(identify));
		return -1;
	}
	if (ast_strlen_zero(identify->match_header) /* No header to match */
		/* and no static IP addresses with a mask */
		&& !identify->matches
		/* and no addresses to resolve */
		&& (!identify->hosts || !ao2_container_count(identify->hosts))) {
		ast_log(LOG_ERROR, "Identify '%s' is not configured to match anything.\n",
			ast_sorcery_object_get_id(identify));
		return -1;
	}

	if (!ast_strlen_zero(identify->match_header)) {
		char *c_header;
		char *c_value;
		int len;

		/* Split the header name and value */
		c_header = ast_strdupa(identify->match_header);
		c_value = strchr(c_header, ':');
		if (!c_value) {
			ast_log(LOG_ERROR, "Identify '%s' missing ':' separator in match_header '%s'.\n",
				ast_sorcery_object_get_id(identify), identify->match_header);
			return -1;
		}
		*c_value = '\0';
		c_value = ast_strip(c_value + 1);
		c_header = ast_strip(c_header);

		if (ast_strlen_zero(c_header)) {
			ast_log(LOG_ERROR, "Identify '%s' has no SIP header to match in match_header '%s'.\n",
				ast_sorcery_object_get_id(identify), identify->match_header);
			return -1;
		}

		if (!strcmp(c_value, "//")) {
			/* An empty regex is the same as an empty literal string. */
			c_value = "";
		}

		if (ast_string_field_set(identify, match_header_name, c_header)
			|| ast_string_field_set(identify, match_header_value, c_value)) {
			return -1;
		}

		len = strlen(c_value);
		if (2 < len && c_value[0] == '/' && c_value[len - 1] == '/') {
			/* Make "/regex/" into "regex" */
			c_value[len - 1] = '\0';
			++c_value;

			if (regcomp(&identify->regex_buf, c_value, REG_EXTENDED | REG_NOSUB)) {
				ast_log(LOG_ERROR, "Identify '%s' failed to compile match_header regex '%s'.\n",
					ast_sorcery_object_get_id(identify), c_value);
				return -1;
			}
			identify->is_regex = 1;
		}
	}

	if (!identify->hosts) {
		/* No match addresses to resolve */
		return 0;
	}

	/* Resolve the match addresses now */
	i = ao2_iterator_init(identify->hosts, 0);
	while ((current_string = ao2_iterator_next(&i))) {
		int results = 0;
		char *colon = strrchr(current_string, ':');

		/* We skip SRV lookup if a colon is present, assuming a port was specified */
		if (!colon) {
			/* No port, and we know this is not an IP address, so perform SRV resolution on it */
			if (identify->srv_lookups) {
				results = ip_identify_match_srv_lookup(identify, "_sip._udp", current_string,
					results);
				if (results != -1) {
					results = ip_identify_match_srv_lookup(identify, "_sip._tcp",
						current_string, results);
				}
				if (results != -1) {
					results = ip_identify_match_srv_lookup(identify, "_sips._tcp",
						current_string, results);
				}
			}
		}

		/* If SRV fails fall back to a normal lookup on the host itself */
		if (!results) {
			results = ip_identify_match_host_lookup(identify, current_string);
		}

		if (results == 0) {
			ast_log(LOG_WARNING, "Identify '%s' provided address '%s' did not resolve to any address\n",
				ast_sorcery_object_get_id(identify), current_string);
		} else if (results == -1) {
			ast_log(LOG_ERROR, "Identify '%s' failed when adding resolution results of '%s'\n",
				ast_sorcery_object_get_id(identify), current_string);
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

static void match_to_var_list_append(struct ast_variable **head, struct ast_ha *ha)
{
	char str[MAX_OBJECT_FIELD];
	const char *addr;

	if (ast_sockaddr_port(&ha->addr)) {
		addr = ast_strdupa(ast_sockaddr_stringify(&ha->addr));
	} else {
		addr = ast_strdupa(ast_sockaddr_stringify_addr(&ha->addr));
	}

	snprintf(str, MAX_OBJECT_FIELD, "%s%s/%s", ha->sense == AST_SENSE_ALLOW ? "!" : "",
			 addr, ast_sockaddr_stringify_addr(&ha->netmask));

	ast_variable_list_append(head, ast_variable_new("match", str, ""));
}

static int match_to_var_list(const void *obj, struct ast_variable **fields)
{
	const struct ip_identify_match *identify = obj;
	struct ast_variable *head = NULL;
	struct ast_ha *ha = identify->matches;

	for (; ha; ha = ha->next) {
		match_to_var_list_append(&head, ha);
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

static int send_identify_ami_event(void *obj, void *arg, void *data, int flags)
{
	struct ip_identify_match *identify = obj;
	const char *endpoint_name = arg;
	struct ast_sip_ami *ami = data;
	struct ast_str *buf;

	/* Build AMI event */
	buf = ast_sip_create_ami_event("IdentifyDetail", ami);
	if (!buf) {
		return CMP_STOP;
	}
	if (sip_identify_to_ami(identify, &buf)) {
		ast_free(buf);
		return CMP_STOP;
	}
	ast_str_append(&buf, 0, "EndpointName: %s\r\n", endpoint_name);

	/* Send AMI event */
	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	++ami->count;

	ast_free(buf);
	return 0;
}

static int format_ami_endpoint_identify(const struct ast_sip_endpoint *endpoint,
					struct ast_sip_ami *ami)
{
	struct ao2_container *identifies;
	struct ast_variable fields = {
		.name = "endpoint",
		.value = ast_sorcery_object_get_id(endpoint),
	};

	identifies = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "identify",
		AST_RETRIEVE_FLAG_MULTIPLE, &fields);
	if (!identifies) {
		return -1;
	}

	/* Build and send any found identify object's AMI IdentifyDetail event. */
	ao2_callback_data(identifies, OBJ_MULTIPLE | OBJ_NODATA,
		send_identify_ami_event,
		(void *) ast_sorcery_object_get_id(endpoint),
		ami);

	ao2_ref(identifies, -1);
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
			const char *addr;

			if (ast_sockaddr_port(&match->addr)) {
				addr = ast_sockaddr_stringify(&match->addr);
			} else {
				addr = ast_sockaddr_stringify_addr(&match->addr);
			}

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
	ast_sip_register_endpoint_identifier_with_name(&header_identifier, "header");
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
	ast_sip_unregister_endpoint_identifier(&header_identifier);
	ast_sip_unregister_endpoint_identifier(&ip_identifier);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP IP endpoint identifier",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.reload = reload_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 4,
	.requires = "res_pjsip",
);
