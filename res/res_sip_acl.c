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
#include "asterisk/logger.h"
#include "asterisk/sorcery.h"
#include "asterisk/acl.h"

/*** DOCUMENTATION
	<configInfo name="res_sip_acl" language="en_US">
		<synopsis>SIP ACL module</synopsis>
		<description><para>
			<emphasis>ACL</emphasis>
			</para>
			<para>The ACL module used by <literal>res_sip</literal>. This module is
			independent of <literal>endpoints</literal> and operates on all inbound
			SIP communication using res_sip.
			</para><para>
			It should be noted that this module can also reference ACLs from
			<filename>acl.conf</filename>.
			</para><para>
			There are two main ways of creating an access list: <literal>IP-Domain</literal>
			and <literal>Contact Header</literal>. It is possible to create a combined ACL using
			both IP and Contact.
		</para></description>
		<configFile name="res_sip.conf">
			<configObject name="acl">
				<synopsis>Access Control List</synopsis>
				<configOption name="acl">
					<synopsis>Name of IP ACL</synopsis>
					<description><para>
						This matches sections configured in <literal>acl.conf</literal>
					</para></description>
				</configOption>
				<configOption name="contactacl">
					<synopsis>Name of Contact ACL</synopsis>
					<description><para>
						This matches sections configured in <literal>acl.conf</literal>
					</para></description>
				</configOption>
				<configOption name="contactdeny">
					<synopsis>List of Contact Header addresses to Deny</synopsis>
				</configOption>
				<configOption name="contactpermit">
					<synopsis>List of Contact Header addresses to Permit</synopsis>
				</configOption>
				<configOption name="deny">
					<synopsis>List of IP-domains to deny access from</synopsis>
				</configOption>
				<configOption name="permit">
					<synopsis>List of IP-domains to allow access from</synopsis>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'acl'.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

struct sip_acl {
	SORCERY_OBJECT(details);
	struct ast_acl_list *acl;
	struct ast_acl_list *contact_acl;
};

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

	if (!contact) {
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

static int check_acls(void *obj, void *arg, int flags)
{
	struct sip_acl *acl = obj;
	pjsip_rx_data *rdata = arg;

	if (apply_acl(rdata, acl->acl) || apply_contact_acl(rdata, acl->contact_acl)) {
		return CMP_MATCH | CMP_STOP;
	}
	return 0;
}

static pj_bool_t acl_on_rx_msg(pjsip_rx_data *rdata)
{
	int forbidden = 0;
	struct ao2_container *acls = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "acl", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	struct sip_acl *matched_acl;
	if (!acls) {
		ast_log(LOG_ERROR, "Unable to retrieve ACL sorcery data\n");
		return PJ_FALSE;
	}

	matched_acl = ao2_callback(acls, 0, check_acls, rdata);
	if (matched_acl) {
		forbidden = 1;
		ao2_ref(matched_acl, -1);
	}
	ao2_ref(acls, -1);

	if (forbidden) {
		if (rdata->msg_info.msg->line.req.method.id != PJSIP_ACK_METHOD) {
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 403, NULL, NULL, NULL);
		}
		return PJ_TRUE;
	}

	return PJ_FALSE;
}

static pjsip_module acl_module = {
	.name = { "ACL Module", 14 },
	/* This should run after a logger but before anything else */
	.priority = 1,
	.on_rx_request = acl_on_rx_msg,
};

static int acl_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct sip_acl *acl = obj;
	int error;
	int ignore;
	if (!strncmp(var->name, "contact", 7)) {
		ast_append_acl(var->name + 7, var->value, &acl->contact_acl, &error, &ignore);
	} else {
		ast_append_acl(var->name, var->value, &acl->acl, &error, &ignore);
	}
	return error;
}

static void sip_acl_destructor(void *obj)
{
	struct sip_acl *acl = obj;
	acl->acl = ast_free_acl_list(acl->acl);
	acl->contact_acl = ast_free_acl_list(acl->contact_acl);
}

static void *sip_acl_alloc(const char *name)
{
	struct sip_acl *acl = ao2_alloc(sizeof(*acl), sip_acl_destructor);
	if (!acl) {
		return NULL;
	}
	return acl;
}

static int load_acls(void)
{
	ast_sorcery_apply_default(ast_sip_get_sorcery(), "acl", "config", "res_sip.conf,criteria=type=acl");
	if (ast_sorcery_object_register(ast_sip_get_sorcery(), "acl", sip_acl_alloc, NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to register SIP ACL object with sorcery\n");
		return -1;
	}
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "acl", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "acl", "permit", "", acl_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "acl", "deny", "", acl_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "acl", "acl", "", acl_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "acl", "contactpermit", "", acl_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "acl", "contactdeny", "", acl_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "acl", "contactacl", "", acl_handler, NULL, 0, 0);

	/* XXX Is there a more selective way to do this? (i.e. Just reload a specific object type?) */
	ast_sorcery_reload(ast_sip_get_sorcery());
	return 0;
}

static int load_module(void)
{
	if (load_acls()) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_sip_register_service(&acl_module);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_unregister_service(&acl_module);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SIP ACL Resource",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
