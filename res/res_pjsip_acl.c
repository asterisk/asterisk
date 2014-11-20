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
#include "asterisk/logger.h"
#include "asterisk/sorcery.h"
#include "asterisk/acl.h"

/*** DOCUMENTATION
	<configInfo name="res_pjsip_acl" language="en_US">
		<synopsis>SIP ACL module</synopsis>
		<description><para>
			<emphasis>ACL</emphasis>
			</para><para>
			The ACL module used by <literal>res_pjsip</literal>. This module is
			independent of <literal>endpoints</literal> and operates on all inbound
			SIP communication using res_pjsip.
			</para><para>
			There are two main ways of defining your ACL with the options
			provided. You can use the <literal>permit</literal> and <literal>deny</literal> options
			which act on <emphasis>IP</emphasis> addresses, or the <literal>contactpermit</literal>
			and <literal>contactdeny</literal> options which act on <emphasis>Contact header</emphasis>
			addresses in incoming REGISTER requests. You can combine the various options to
			create a mixed ACL.
			</para><para>
			Additionally, instead of defining an ACL with options, you can reference IP or
			Contact header ACLs from the file <filename>acl.conf</filename> by using the <literal>acl</literal>
			or <literal>contactacl</literal> options.
		</para></description>
		<configFile name="pjsip.conf">
			<configObject name="acl">
				<synopsis>Access Control List</synopsis>
				<configOption name="acl">
					<synopsis>List of IP ACL section names in acl.conf</synopsis>
					<description><para>
						This matches sections configured in <literal>acl.conf</literal>. The value is
						defined as a list of comma-delimited section names.
					</para></description>
				</configOption>
				<configOption name="contact_acl">
					<synopsis>List of Contact ACL section names in acl.conf</synopsis>
					<description><para>
						This matches sections configured in <literal>acl.conf</literal>. The value is
						defined as a list of comma-delimited section names.
					</para></description>
				</configOption>
				<configOption name="contact_deny">
					<synopsis>List of Contact header addresses to deny</synopsis>
					<description><para>
						The value is a comma-delimited list of IP addresses. IP addresses may
						have a subnet mask appended. The subnet mask may be written in either
						CIDR or dotted-decimal notation. Separate the IP address and subnet
						mask with a slash ('/')
					</para></description>
				</configOption>
				<configOption name="contact_permit">
					<synopsis>List of Contact header addresses to permit</synopsis>
					<description><para>
						The value is a comma-delimited list of IP addresses. IP addresses may
						have a subnet mask appended. The subnet mask may be written in either
						CIDR or dotted-decimal notation. Separate the IP address and subnet
						mask with a slash ('/')
					</para></description>
				</configOption>
				<configOption name="deny">
					<synopsis>List of IP addresses to deny access from</synopsis>
					<description><para>
						The value is a comma-delimited list of IP addresses. IP addresses may
						have a subnet mask appended. The subnet mask may be written in either
						CIDR or dotted-decimal notation. Separate the IP address and subnet
						mask with a slash ('/')
					</para></description>
				</configOption>
				<configOption name="permit">
					<synopsis>List of IP addresses to permit access from</synopsis>
					<description><para>
						The value is a comma-delimited list of IP addresses. IP addresses may
						have a subnet mask appended. The subnet mask may be written in either
						CIDR or dotted-decimal notation. Separate the IP address and subnet
						mask with a slash ('/')
					</para></description>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'acl'.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

static int apply_acl(pjsip_rx_data *rdata, struct ast_acl_list *acl)
{
	struct ast_sockaddr addr;

	if (ast_acl_list_is_empty(acl)) {
		return 0;
	}

	memset(&addr, 0, sizeof(addr));
	ast_sockaddr_parse(&addr, rdata->pkt_info.src_name, PARSE_PORT_FORBID);
	ast_sockaddr_set_port(&addr, rdata->pkt_info.src_port);

	if (ast_apply_acl(acl, &addr, "SIP ACL: ") != AST_SENSE_ALLOW) {
		ast_log(LOG_WARNING, "Incoming SIP message from %s did not pass ACL test\n", ast_sockaddr_stringify(&addr));
		return 1;
	}
	return 0;
}

static int extract_contact_addr(pjsip_contact_hdr *contact, struct ast_sockaddr **addrs)
{
	pjsip_sip_uri *sip_uri;
	char host[256];

	if (!contact || contact->star) {
		return 0;
	}
	if (!PJSIP_URI_SCHEME_IS_SIP(contact->uri) && !PJSIP_URI_SCHEME_IS_SIPS(contact->uri)) {
		return 0;
	}
	sip_uri = pjsip_uri_get_uri(contact->uri);
	ast_copy_pj_str(host, &sip_uri->host, sizeof(host));
	return ast_sockaddr_resolve(addrs, host, PARSE_PORT_FORBID, AST_AF_UNSPEC);
}

static int apply_contact_acl(pjsip_rx_data *rdata, struct ast_acl_list *contact_acl)
{
	int num_contact_addrs;
	int forbidden = 0;
	struct ast_sockaddr *contact_addrs;
	int i;
	pjsip_contact_hdr *contact = (pjsip_contact_hdr *)&rdata->msg_info.msg->hdr;

	if (ast_acl_list_is_empty(contact_acl)) {
		return 0;
	}

	while ((contact = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, contact->next))) {
		num_contact_addrs = extract_contact_addr(contact, &contact_addrs);
		if (num_contact_addrs <= 0) {
			continue;
		}
		for (i = 0; i < num_contact_addrs; ++i) {
			if (ast_apply_acl(contact_acl, &contact_addrs[i], "SIP Contact ACL: ") != AST_SENSE_ALLOW) {
				ast_log(LOG_WARNING, "Incoming SIP message from %s did not pass ACL test\n", ast_sockaddr_stringify(&contact_addrs[i]));
				forbidden = 1;
				break;
			}
		}
		ast_free(contact_addrs);
		if (forbidden) {
			/* No use checking other contacts if we already have failed ACL check */
			break;
		}
	}

	return forbidden;
}

#define SIP_SORCERY_ACL_TYPE "acl"

/*!
 * \brief SIP ACL details and configuration.
 */
struct ast_sip_acl {
	SORCERY_OBJECT(details);
	struct ast_acl_list *acl;
	struct ast_acl_list *contact_acl;
};

static int check_acls(void *obj, void *arg, int flags)
{
	struct ast_sip_acl *sip_acl = obj;
	pjsip_rx_data *rdata = arg;

	if (apply_acl(rdata, sip_acl->acl) ||
	    apply_contact_acl(rdata, sip_acl->contact_acl)) {
		return CMP_MATCH | CMP_STOP;
	}
	return 0;
}

static pj_bool_t acl_on_rx_msg(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ao2_container *, acls, ast_sorcery_retrieve_by_fields(
			 ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE,
			 AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL), ao2_cleanup);
	RAII_VAR(struct ast_sip_acl *, matched_acl, NULL, ao2_cleanup);

	if (!acls) {
		ast_log(LOG_ERROR, "Unable to retrieve ACL sorcery data\n");
		return PJ_FALSE;
	}

	if ((matched_acl = ao2_callback(acls, 0, check_acls, rdata))) {
		if (rdata->msg_info.msg->line.req.method.id != PJSIP_ACK_METHOD) {
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 403, NULL, NULL, NULL);
		}
		return PJ_TRUE;
	}

	return PJ_FALSE;
}

static int acl_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_acl *sip_acl = obj;
	int error = 0;
	int ignore;

	if (!strncmp(var->name, "contact_", 8)) {
		ast_append_acl(var->name + 8, var->value, &sip_acl->contact_acl, &error, &ignore);
	} else {
		ast_append_acl(var->name, var->value, &sip_acl->acl, &error, &ignore);
	}

	return error;
}

static pjsip_module acl_module = {
	.name = { "ACL Module", 14 },
	/* This should run after a logger but before anything else */
	.priority = 1,
	.on_rx_request = acl_on_rx_msg,
};

static void acl_destroy(void *obj)
{
	struct ast_sip_acl *sip_acl = obj;
	sip_acl->acl = ast_free_acl_list(sip_acl->acl);
	sip_acl->contact_acl = ast_free_acl_list(sip_acl->contact_acl);
}

static void *acl_alloc(const char *name)
{
	struct ast_sip_acl *sip_acl =
		ast_sorcery_generic_alloc(sizeof(*sip_acl), acl_destroy);

	return sip_acl;
}

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	ast_sorcery_apply_default(ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE,
				  "config", "pjsip.conf,criteria=type=acl");

	if (ast_sorcery_object_register(ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE,
					acl_alloc, NULL, NULL)) {

		ast_log(LOG_ERROR, "Failed to register SIP %s object with sorcery\n",
			SIP_SORCERY_ACL_TYPE);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE, "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE, "permit", "", acl_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE, "deny", "", acl_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE, "acl", "", acl_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE, "contact_permit", "", acl_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE, "contact_deny", "", acl_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE, "contact_acl", "", acl_handler, NULL, NULL, 0, 0);

	ast_sorcery_load_object(ast_sip_get_sorcery(), SIP_SORCERY_ACL_TYPE);

	ast_sip_register_service(&acl_module);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_service(&acl_module);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP ACL Resource",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
