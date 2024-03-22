/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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
	<depend>res_pjsip_pubsub</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_pubsub.h"
#include "asterisk/res_pjsip_presence_xml.h"
#include "asterisk/res_pjsip_body_generator_types.h"
#include "asterisk/module.h"
#include "asterisk/strings.h"

#define FEATURE_TYPE "application"
#define FEATURE_SUBTYPE "x-as-feature-event+xml"

static void *features_allocate_body(void *data)
{
	struct ast_str **features_str;

	features_str = ast_malloc(sizeof(*features_str));
	if (!features_str) {
		return NULL;
	}
	*features_str = ast_str_create(128);
	if (!*features_str) {
		ast_free(features_str);
		return NULL;
	}
	return features_str;
}

/* Also used in res_pjsip_features.c */
enum forward_type {
	FORWARD_ALWAYS,
	FORWARD_BUSY,
	FORWARD_NOANSWER,
};

static const char *forward_type_str(enum forward_type fwdtype)
{
	switch (fwdtype) {
	case FORWARD_ALWAYS:
		return "forwardImmediate";
	case FORWARD_BUSY:
		return "forwardBusy";
	case FORWARD_NOANSWER:
		return "forwardNoAns";
	}
	return "";
}

static int generate_forward_body(struct ast_str **bodytext, struct ast_sip_device_feature_sync_data *sync_data, enum forward_type fwdtype, const char *forward_to)
{
	char *buf;
	int len;
	int active;
	struct ast_xml_doc *doc;
	struct ast_xml_node *root, *tmpnode;
	const char *fwd_str = forward_type_str(fwdtype);

	ast_assert(!ast_strlen_zero(fwd_str));

	ast_debug(1, "ForwardingEvent update required (%s)\n", fwd_str);
	doc = ast_xml_new();
	if (!doc) {
		ast_log(LOG_ERROR, "Could not create new XML document\n");
		return -1;
	}

	root = ast_xml_new_node("ForwardingEvent");
	if (!root) {
		goto cleanup;
	}

	ast_xml_set_root(doc, root);
	/* If the namespace is missing altogether, Polycom phones will just crash and reboot when they get the NOTIFY... not good! */
	/* Furthermore, if the phone doesn't like the namespace, it will terminate parsing of the as-feature-event document. */
	ast_xml_set_attribute(root, "xmlns", "http://www.ecma-international.org/standards/ecma-323/csta/ed3");

	/*
	 * The phone is expecting something like this:
	 *
	 * <?xml version="1.0" encoding="ISO-8859-1"?>
	 * <ForwardingEvent xmlns="http://www.ecmainternational.org/standards/ecma-323/csta/ed3">
	 *    <device>5559430902</device>
	 *    <forwardingType>forwardImmediate</forwardingType>
	 *    <forwardStatus>false</forwardStatus>
	 * </ForwardingEvent>
	 *
	 * <?xml version="1.0" encoding="ISO-8859-1"?>
	 * <ForwardingEvent xmlns="http://www.ecmainternational.org/standards/ecma-323/csta/ed3">
	 *    <device>5559430902</device>
	 *    <forwardingType>forwardNoAns</forwardingType>
	 *    <forwardStatus>false</forwardStatus>
	 *    <forwardTo></forwardTo>
	 *    <ringCount></ringCount>
	 * </ForwardingEvent>
	 *
	 * ringcount, if present, can range from 1-100
	 * Currently this isn't something we send to the phone, since the forward time is handled server side anyways.
	 */

	tmpnode = ast_xml_new_node("device");
	if (!tmpnode) {
		goto cleanup;
	}
	/* The Broadworks spec says device is mandatory, but the actual value is used by neither server nor client. So if we don't have one, make something up. */
	ast_xml_set_text(tmpnode, S_OR(sync_data->deviceid, "123456"));
	if (!sync_data->deviceid[0]) {
		ast_debug(2, "This was the first NOTIFY with body data for this endpoint\n");
	}
	ast_xml_add_child(root, tmpnode);

	tmpnode = ast_xml_new_node("forwardingType");
	if (!tmpnode) {
		goto cleanup;
	}
	ast_xml_set_text(tmpnode, fwd_str);
	ast_xml_add_child(root, tmpnode);

	active = !ast_strlen_zero(forward_to) ? 1 : 0;

	tmpnode = ast_xml_new_node("forwardStatus");
	if (!tmpnode) {
		goto cleanup;
	}
	ast_xml_set_text(tmpnode, active ? "true" : "false");
	ast_xml_add_child(root, tmpnode);

	if (active) { /* Forwarding is active. */
		tmpnode = ast_xml_new_node("forwardTo");
		if (!tmpnode) {
			goto cleanup;
		}
		ast_xml_set_text(tmpnode, forward_to);
		ast_xml_add_child(root, tmpnode);
	} /* else, forwarding is not active. No additional info needed. */

	/* Finalize. */
	ast_xml_doc_dump_memory(doc, &buf, &len);
	ast_xml_close(doc);
	if (len <= 0) {
		ast_log(LOG_WARNING, "XML document has length %d?\n", len);
	}
	if (buf) {
		ast_str_append(bodytext, 0, "%s", buf);
		ast_xml_free_text(buf);
	}
	return 0;
cleanup:
	ast_xml_close(doc);
	ast_log(LOG_ERROR, "Could not create new XML root node\n");
	return -1;
}

static int features_generate_body_content(void *body, void *data)
{
	char *buf;
	int len;
	struct ast_str **bodytext = body;
	struct ast_xml_doc *doc;
	struct ast_xml_node *root, *tmpnode;
	struct ast_sip_device_feature_sync_data *sync_data = data;

	int updates_made = 0;

	/* This callback is called for *all* NOTIFYs, so we should only add XML to the body if actually necessary. */
	ast_debug(2, "Generating body content for %s/%s\n", FEATURE_TYPE, FEATURE_SUBTYPE);
	if (sync_data->update_needed_dnd) {
		int dnd_active = sync_data->dnd;

		updates_made++;

		ast_debug(1, "Do Not Disturb update required\n");
		doc = ast_xml_new();
		if (!doc) {
			ast_log(LOG_ERROR, "Could not create new XML document\n");
			return -1;
		}

		root = ast_xml_new_node("DoNotDisturbEvent");
		if (!root) {
			ast_xml_close(doc);
			ast_log(LOG_ERROR, "Could not create new XML root node\n");
			return -1;
		}

		ast_xml_set_root(doc, root);
		/* If the namespace is missing altogether, Polycom phones will just crash and reboot when they get the NOTIFY... not good! */
		/* Furthermore, if the phone doesn't like the namespace, it will terminate parsing of the as-feature-event document. */
		ast_xml_set_attribute(root, "xmlns", "http://www.ecma-international.org/standards/ecma-323/csta/ed3");

		/*
		 * The phone is expecting something like this:
		 *
		 * <?xml version="1.0" encoding="ISO-8859-1"?>
		 * <DoNotDisturbEvent xmlns="http://www.ecma-international.org/standards/ecma-323/csta/ed3">
		 * 	  <device>5559430902</device>
		 * 	  <doNotDisturbOn>true</doNotDisturbOn>
		 * </DoNotDisturbEvent>
		 */

		tmpnode = ast_xml_new_node("device");
		/* The Broadworks spec says device is mandatory, but the actual value is used by neither server nor client. So if we don't have one, make something up. */
		ast_xml_set_text(tmpnode, S_OR(sync_data->deviceid, "123456"));
		if (!sync_data->deviceid[0]) {
			ast_debug(2, "This was the first NOTIFY with body data for this endpoint\n");
		}
		ast_xml_add_child(root, tmpnode);

		tmpnode = ast_xml_new_node("doNotDisturbOn");
		ast_xml_set_text(tmpnode, dnd_active ? "true" : "false");
		ast_xml_add_child(root, tmpnode);

		/* Finalize. */
		ast_xml_doc_dump_memory(doc, &buf, &len);
		ast_xml_close(doc);
		if (len <= 0) {
			ast_log(LOG_WARNING, "XML document has length %d?\n", len);
		}
		if (buf) {
			ast_str_append(bodytext, 0, "%s", buf);
			ast_xml_free_text(buf);
		}
	}
	if (sync_data->update_needed_fwd_always) {
		if (generate_forward_body(bodytext, sync_data, FORWARD_ALWAYS, sync_data->fwd_exten_always)) {
			return -1;
		}
		updates_made++;
	}
	if (sync_data->update_needed_fwd_busy) {
		if (generate_forward_body(bodytext, sync_data, FORWARD_BUSY, sync_data->fwd_exten_busy)) {
			return -1;
		}
		updates_made++;
	}
	if (sync_data->update_needed_fwd_noanswer) {
		if (generate_forward_body(bodytext, sync_data, FORWARD_NOANSWER, sync_data->fwd_exten_noanswer)) {
			return -1;
		}
		updates_made++;
	}

	ast_debug(3, "%d update(s) made\n", updates_made);

	/* XXX This only works for the first update that gets sent, because we need to send
	 * a multipart XML document for more than 1.
	 * So in practice, res_pjsip_device_features will never call us to make multiple updates for the same NOTIFY. */
	if (updates_made > 1) {
		ast_log(LOG_WARNING, "%d updates made, processing likely truncated by endpoint\n", updates_made);
	}
	return 0;
}

static void features_to_string(void *body, struct ast_str **str)
{
	struct ast_str **features = body;
	ast_str_set(str, 0, "%s", ast_str_buffer(*features));
}

static void features_destroy_body(void *body)
{
	struct ast_str **features = body;
	ast_free(*features);
	ast_free(features);
}

static struct ast_sip_pubsub_body_generator features_generator = {
	.type = FEATURE_TYPE,
	.subtype = FEATURE_SUBTYPE,
	.body_type = AST_SIP_DEVICE_FEATURE_SYNC_DATA,
	.allocate_body = features_allocate_body,
	.generate_body_content = features_generate_body_content,
	.to_string = features_to_string,
	.destroy_body = features_destroy_body,
};

static int load_module(void)
{
	if (ast_sip_pubsub_register_body_generator(&features_generator)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_pubsub_unregister_body_generator(&features_generator);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Device Feature Synchronization",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "res_pjsip,res_pjsip_pubsub",
);
