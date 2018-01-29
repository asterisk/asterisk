/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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
 * \brief AMI wrapper for external MWI.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

/*** MODULEINFO
	<depend>res_mwi_external</depend>
	<support_level>core</support_level>
 ***/


/*** DOCUMENTATION
	<manager name="MWIGet" language="en_US">
		<synopsis>
			Get selected mailboxes with message counts.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Mailbox" required="true">
				<para>Mailbox ID in the form of
				/<replaceable>regex</replaceable>/ for all mailboxes matching the regular
				expression.  Otherwise it is for a specific mailbox.</para>
			</parameter>
		</syntax>
		<description>
			<para>Get a list of mailboxes with their message counts.</para>
		</description>
	</manager>
	<managerEvent language="en_US" name="MWIGet">
		<managerEventInstance class="EVENT_FLAG_REPORTING">
			<synopsis>
				Raised in response to a MWIGet command.
			</synopsis>
			<syntax>
				<parameter name="ActionID" required="false"/>
				<parameter name="Mailbox">
					<para>Specific mailbox ID.</para>
				</parameter>
				<parameter name="OldMessages">
					<para>The number of old messages in the mailbox.</para>
				</parameter>
				<parameter name="NewMessages">
					<para>The number of new messages in the mailbox.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="manager">MWIGet</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="MWIGetComplete">
		<managerEventInstance class="EVENT_FLAG_REPORTING">
			<synopsis>
				Raised in response to a MWIGet command.
			</synopsis>
			<syntax>
				<parameter name="ActionID" required="false"/>
				<parameter name="EventList" />
				<parameter name="ListItems">
					<para>The number of mailboxes reported.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="manager">MWIGet</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<manager name="MWIDelete" language="en_US">
		<synopsis>
			Delete selected mailboxes.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<xi:include xpointer="xpointer(/docs/manager[@name='MWIGet']/syntax/parameter[@name='Mailbox'])" />
		</syntax>
		<description>
			<para>Delete the specified mailboxes.</para>
		</description>
	</manager>
	<manager name="MWIUpdate" language="en_US">
		<synopsis>
			Update the mailbox message counts.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Mailbox" required="true">
				<para>Specific mailbox ID.</para>
			</parameter>
			<parameter name="OldMessages">
				<para>The number of old messages in the mailbox.  Defaults
				to zero if missing.</para>
			</parameter>
			<parameter name="NewMessages">
				<para>The number of new messages in the mailbox.  Defaults
				to zero if missing.</para>
			</parameter>
		</syntax>
		<description>
			<para>Update the mailbox message counts.</para>
		</description>
	</manager>
 ***/


#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/res_mwi_external.h"
#include "asterisk/manager.h"

/* ------------------------------------------------------------------- */

/*!
 * \internal
 * \brief Get the requested mailboxes.
 * \since 12.1.0
 *
 * \param s AMI session.
 * \param m AMI message.
 *
 * \retval 0 to keep AMI connection.
 * \retval -1 to disconnect AMI connection.
 */
static int mwi_mailbox_get(struct mansession *s, const struct message *m)
{
	char id_text[256];
	const char *id;
	const char *mailbox_id = astman_get_header(m, "Mailbox");
	const struct ast_mwi_mailbox_object *mailbox;
	struct ao2_container *mailboxes;
	unsigned count;
	struct ao2_iterator iter;

	if (ast_strlen_zero(mailbox_id)) {
		astman_send_error(s, m, "Missing mailbox parameter in request");
		return 0;
	}

	if (*mailbox_id == '/') {
		struct ast_str *regex_string;

		regex_string = ast_str_create(strlen(mailbox_id) + 1);
		if (!regex_string) {
			astman_send_error(s, m, "Memory Allocation Failure");
			return 0;
		}

		/* Make "/regex/" into "regex" */
		if (ast_regex_string_to_regex_pattern(mailbox_id, &regex_string) != 0) {
			astman_send_error_va(s, m, "Mailbox regex format invalid in: %s", mailbox_id);
			ast_free(regex_string);
			return 0;
		}

		mailboxes = ast_mwi_mailbox_get_by_regex(ast_str_buffer(regex_string));
		ast_free(regex_string);
	} else {
		mailboxes = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
		if (mailboxes) {
			mailbox = ast_mwi_mailbox_get(mailbox_id);
			if (mailbox) {
				if (!ao2_link(mailboxes, (void *) mailbox)) {
					ao2_ref(mailboxes, -1);
					mailboxes = NULL;
				}
				ast_mwi_mailbox_unref(mailbox);
			}
		}
	}
	if (!mailboxes) {
		astman_send_error(s, m, "Mailbox container creation failure");
		return 0;
	}

	astman_send_listack(s, m, "Mailboxes will follow", "start");

	id = astman_get_header(m, "ActionID");
	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	} else {
		id_text[0] = '\0';
	}

	/* Output mailbox list. */
	count = 0;
	iter = ao2_iterator_init(mailboxes, AO2_ITERATOR_UNLINK);
	for (; (mailbox = ao2_iterator_next(&iter)); ast_mwi_mailbox_unref(mailbox)) {
		++count;
		astman_append(s,
			"Event: MWIGet\r\n"
			"Mailbox: %s\r\n"
			"OldMessages: %u\r\n"
			"NewMessages: %u\r\n"
			"%s"
			"\r\n",
			ast_mwi_mailbox_get_id(mailbox),
			ast_mwi_mailbox_get_msgs_old(mailbox),
			ast_mwi_mailbox_get_msgs_new(mailbox),
			id_text);
	}
	ao2_iterator_destroy(&iter);
	ao2_ref(mailboxes, -1);

	astman_send_list_complete_start(s, m, "MWIGetComplete", count);
	astman_send_list_complete_end(s);

	return 0;
}

/*!
 * \internal
 * \brief Delete the requested mailboxes.
 * \since 12.1.0
 *
 * \param s AMI session.
 * \param m AMI message.
 *
 * \retval 0 to keep AMI connection.
 * \retval -1 to disconnect AMI connection.
 */
static int mwi_mailbox_delete(struct mansession *s, const struct message *m)
{
	const char *mailbox_id = astman_get_header(m, "Mailbox");

	if (ast_strlen_zero(mailbox_id)) {
		astman_send_error(s, m, "Missing mailbox parameter in request");
		return 0;
	}

	if (*mailbox_id == '/') {
		struct ast_str *regex_string;

		regex_string = ast_str_create(strlen(mailbox_id) + 1);
		if (!regex_string) {
			astman_send_error(s, m, "Memory Allocation Failure");
			return 0;
		}

		/* Make "/regex/" into "regex" */
		if (ast_regex_string_to_regex_pattern(mailbox_id, &regex_string) != 0) {
			astman_send_error_va(s, m, "Mailbox regex format invalid in: %s", mailbox_id);
			ast_free(regex_string);
			return 0;
		}

		ast_mwi_mailbox_delete_by_regex(ast_str_buffer(regex_string));
		ast_free(regex_string);
	} else {
		ast_mwi_mailbox_delete(mailbox_id);
	}

	astman_send_ack(s, m, NULL);
	return 0;
}

/*!
 * \internal
 * \brief Update the specified mailbox.
 * \since 12.1.0
 *
 * \param s AMI session.
 * \param m AMI message.
 *
 * \retval 0 to keep AMI connection.
 * \retval -1 to disconnect AMI connection.
 */
static int mwi_mailbox_update(struct mansession *s, const struct message *m)
{
	const char *mailbox_id = astman_get_header(m, "Mailbox");
	const char *msgs_old = astman_get_header(m, "OldMessages");
	const char *msgs_new = astman_get_header(m, "NewMessages");
	struct ast_mwi_mailbox_object *mailbox;
	unsigned int num_old;
	unsigned int num_new;

	if (ast_strlen_zero(mailbox_id)) {
		astman_send_error(s, m, "Missing mailbox parameter in request");
		return 0;
	}

	num_old = 0;
	if (!ast_strlen_zero(msgs_old)) {
		if (sscanf(msgs_old, "%u", &num_old) != 1) {
			astman_send_error_va(s, m, "Invalid OldMessages: %s", msgs_old);
			return 0;
		}
	}

	num_new = 0;
	if (!ast_strlen_zero(msgs_new)) {
		if (sscanf(msgs_new, "%u", &num_new) != 1) {
			astman_send_error_va(s, m, "Invalid NewMessages: %s", msgs_new);
			return 0;
		}
	}

	mailbox = ast_mwi_mailbox_alloc(mailbox_id);
	if (!mailbox) {
		astman_send_error(s, m, "Mailbox object creation failure");
		return 0;
	}

	/* Update external mailbox. */
	ast_mwi_mailbox_set_msgs_old(mailbox, num_old);
	ast_mwi_mailbox_set_msgs_new(mailbox, num_new);
	if (ast_mwi_mailbox_update(mailbox)) {
		astman_send_error(s, m, "Update attempt failed");
	} else {
		astman_send_ack(s, m, NULL);
	}
	ast_mwi_mailbox_unref(mailbox);

	return 0;
}

static int unload_module(void)
{
	ast_manager_unregister("MWIGet");
	ast_manager_unregister("MWIDelete");
	ast_manager_unregister("MWIUpdate");

	return 0;
}

static int load_module(void)
{
	int res;

	res = 0;
	res |= ast_manager_register_xml("MWIGet", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, mwi_mailbox_get);
	res |= ast_manager_register_xml("MWIDelete", EVENT_FLAG_CALL, mwi_mailbox_delete);
	res |= ast_manager_register_xml("MWIUpdate", EVENT_FLAG_CALL, mwi_mailbox_update);
	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "AMI support for external MWI",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 5,
	.requires = "res_mwi_external",
);
