/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jason Parker <jparker@digium.com>
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

/*! \file
 *
 * \brief Stasis Messages and Data Types for System events
 *
 * \author Jason Parker <jparker@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_system.h"

/*** DOCUMENTATION
	<managerEvent language="en_US" name="Registry">
		<managerEventInstance class="EVENT_FLAG_SYSTEM">
			<synopsis>Raised when an outbound registration completes.</synopsis>
			<syntax>
				<parameter name="ChannelType">
					<para>The type of channel that was registered (or not).</para>
				</parameter>
				<parameter name="Username">
					<para>The username portion of the registration.</para>
				</parameter>
				<parameter name="Domain">
					<para>The address portion of the registration.</para>
				</parameter>
				<parameter name="Status">
					<para>The status of the registration request.</para>
					<enumlist>
						<enum name="Registered"/>
						<enum name="Unregistered"/>
						<enum name="Rejected"/>
						<enum name="Failed"/>
					</enumlist>
				</parameter>
				<parameter name="Cause">
					<para>What caused the rejection of the request, if available.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
 ***/

/*! \brief The \ref stasis topic for system level changes */
static struct stasis_topic *system_topic;

static struct ast_manager_event_blob *system_registry_to_ami(struct stasis_message *message);

STASIS_MESSAGE_TYPE_DEFN(ast_network_change_type);
STASIS_MESSAGE_TYPE_DEFN(ast_system_registry_type,
	.to_ami = system_registry_to_ami,
	);

void ast_system_publish_registry(const char *channeltype, const char *username, const char *domain, const char *status, const char *cause)
{
	RAII_VAR(struct ast_json *, registry, NULL, ast_json_unref);
	RAII_VAR(struct ast_json_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	registry = ast_json_pack("{s: s, s: s, s: s, s: s, s: s, s: s}",
		"type", "registry",
		"channeltype", channeltype,
		"username", username,
		"domain", domain,
		"status", status,
		"cause", S_OR(cause, ""));

	if (!(payload = ast_json_payload_create(registry))) {
		return;
	}

	if (!(message = stasis_message_create(ast_system_registry_type(), payload))) {
		return;
	}

	stasis_publish(ast_system_topic(), message);
}

static struct ast_manager_event_blob *system_registry_to_ami(struct stasis_message *message)
{
	struct ast_json_payload *payload = stasis_message_data(message);
	const char *channeltype;
	const char *username;
	const char *domain;
	const char *status;
	const char *cause;
	RAII_VAR(struct ast_str *, cause_string, ast_str_create(32), ast_free);

	if (!cause_string) {
		return NULL;
	}

	channeltype = ast_json_string_get(ast_json_object_get(payload->json, "channeltype"));
	username = ast_json_string_get(ast_json_object_get(payload->json, "username"));
	domain = ast_json_string_get(ast_json_object_get(payload->json, "domain"));
	status = ast_json_string_get(ast_json_object_get(payload->json, "status"));
	cause = ast_json_string_get(ast_json_object_get(payload->json, "cause"));

	if (!ast_strlen_zero(cause)) {
		ast_str_set(&cause_string, 0, "Cause: %s\r\n", cause);
	}

	return ast_manager_event_blob_create(EVENT_FLAG_SYSTEM, "Registry",
		"ChannelType: %s\r\n"
		"Username: %s\r\n"
		"Domain: %s\r\n"
		"Status: %s\r\n"
		"%s",
		channeltype, username, domain, status, ast_str_buffer(cause_string));
}

struct stasis_topic *ast_system_topic(void)
{
	return system_topic;
}

/*! \brief Cleanup the \ref stasis system level items */
static void stasis_system_cleanup(void)
{
	ao2_cleanup(system_topic);
	system_topic = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(ast_network_change_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_system_registry_type);
}

/*! \brief Initialize the system level items for \ref stasis */
int ast_stasis_system_init(void)
{
	ast_register_cleanup(stasis_system_cleanup);

	system_topic = stasis_topic_create("ast_system");
	if (!system_topic) {
		return 1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(ast_network_change_type) != 0) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(ast_system_registry_type) != 0) {
		return -1;
	}

	return 0;
}
