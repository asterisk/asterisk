/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008 - 2009, Digium, Inc.
 *
 * Steve Murphy <murf@digium.com>
 * who freely borrowed code from the cdr equivalents
 *     (see cdr/cdr_manager.c)
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
 * \brief Asterisk Channel Event records.
 *
 * See also
 * \arg \ref AstCDR
 * \arg \ref AstAMI
 * \arg \ref Config_ami
 * \ingroup cel_drivers
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<managerEvent language="en_US" name="CEL">
		<managerEventInstance class="EVENT_FLAG_CEL">
			<synopsis>Raised when a Channel Event Log is generated for a channel.</synopsis>
			<syntax>
				<parameter name="EventName">
					<para>
						The name of the CEL event being raised. This can include
						both the system defined CEL events, as well as user defined
						events.
					</para>
					<note>
						<para>All events listed here may not be raised, depending
						on the configuration in <filename>cel.conf</filename>.</para>
					</note>
					<enumlist>
						<enum name="CHAN_START">
							<para>A channel was created.</para>
						</enum>
						<enum name="CHAN_END">
							<para>A channel was terminated.</para>
						</enum>
						<enum name="ANSWER">
							<para>A channel answered.</para>
						</enum>
						<enum name="HANGUP">
							<para>A channel was hung up.</para>
						</enum>
						<enum name="BRIDGE_ENTER">
							<para>A channel entered a bridge.</para>
						</enum>
						<enum name="BRIDGE_EXIT">
							<para>A channel left a bridge.</para>
						</enum>
						<enum name="APP_START">
							<para>A channel entered into a tracked application.</para>
						</enum>
						<enum name="APP_END">
							<para>A channel left a tracked application.</para>
						</enum>
						<enum name="PARK_START">
							<para>A channel was parked.</para>
						</enum>
						<enum name="PARK_END">
							<para>A channel was unparked.</para>
						</enum>
						<enum name="BLINDTRANSFER">
							<para>A channel initiated a blind transfer.</para>
						</enum>
						<enum name="ATTENDEDTRANSFER">
							<para>A channel initiated an attended transfer.</para>
						</enum>
						<enum name="PICKUP">
							<para>A channel initated a call pickup.</para>
						</enum>
						<enum name="FORWARD">
							<para>A channel is being forwarded to another destination.</para>
						</enum>
						<enum name="LINKEDID_END">
							<para>The linked ID associated with this channel is being retired.</para>
						</enum>
						<enum name="LOCAL_OPTIMIZE">
							<para>A Local channel optimization has occurred.</para>
						</enum>
						<enum name="USER_DEFINED">
							<para>A user defined type.</para>
							<note>
								<para>
									This event is only present if <literal>show_user_defined</literal>
									in <filename>cel.conf</filename> is <literal>True</literal>. Otherwise,
									the user defined event will be placed directly in the
									<replaceable>EventName</replaceable> field.
								</para>
							</note>
						</enum>
					</enumlist>
				</parameter>
				<parameter name="AccountCode">
					<para>The channel's account code.</para>
				</parameter>
				<parameter name="CallerIDnum">
					<para>The Caller ID number.</para>
				</parameter>
				<parameter name="CallerIDname">
					<para>The Caller ID name.</para>
				</parameter>
				<parameter name="CallerIDani">
					<para>The Caller ID Automatic Number Identification.</para>
				</parameter>
				<parameter name="CallerIDrdnis">
					<para>The Caller ID Redirected Dialed Number Identification Service.</para>
				</parameter>
				<parameter name="CallerIDdnid">
					<para>The Caller ID Dialed Number Identifier.</para>
				</parameter>
				<parameter name="Exten">
					<para>The dialplan extension the channel is currently executing in.</para>
				</parameter>
				<parameter name="Context">
					<para>The dialplan context the channel is currently executing in.</para>
				</parameter>
				<parameter name="Application">
					<para>The dialplan application the channel is currently executing.</para>
				</parameter>
				<parameter name="AppData">
					<para>The arguments passed to the dialplan <replaceable>Application</replaceable>.</para>
				</parameter>
				<parameter name="EventTime">
					<para>The time the CEL event occurred.</para>
				</parameter>
				<parameter name="AMAFlags">
					<para>A flag that informs a billing system how to treat the CEL.</para>
					<enumlist>
						<enum name="OMIT">
							<para>This event should be ignored.</para>
						</enum>
						<enum name="BILLING">
							<para>This event contains valid billing data.</para>
						</enum>
						<enum name="DOCUMENTATION">
							<para>This event is for documentation purposes.</para>
						</enum>
					</enumlist>
				</parameter>
				<parameter name="UniqueID">
					<para>The unique ID of the channel.</para>
				</parameter>
				<parameter name="LinkedID">
					<para>The linked ID of the channel, which ties this event to other related channel's events.</para>
				</parameter>
				<parameter name="UserField">
					<para>
						A user defined field set on a channel, containing arbitrary
						application specific data.
					</para>
				</parameter>
				<parameter name="Peer">
					<para>
						If this channel is in a bridge, the channel that it is in
						a bridge with.
					</para>
				</parameter>
				<parameter name="PeerAccount">
					<para>
						If this channel is in a bridge, the accountcode of the
						channel it is in a bridge with.
					</para>
				</parameter>
				<parameter name="Extra">
					<para>
						Some events will have event specific data that accompanies the CEL record.
						This extra data is JSON encoded, and is dependent on the event in
						question.
					</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
 ***/

#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"

static const char DATE_FORMAT[] = "%Y-%m-%d %T";

static const char CONF_FILE[] = "cel.conf";

/*! \brief AMI CEL is off by default */
#define CEL_AMI_ENABLED_DEFAULT		0

static int enablecel;

/*! \brief show_user_def is off by default */
#define CEL_SHOW_USERDEF_DEFAULT	0

#define MANAGER_BACKEND_NAME "Manager Event Logging"

/*! TRUE if we should set the EventName header to USER_DEFINED on user events. */
static unsigned char cel_show_user_def;

static void manager_log(struct ast_event *event)
{
	struct ast_tm timeresult;
	char start_time[80] = "";
	char user_defined_header[160];
	const char *event_name;
	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};

	if (!enablecel) {
		return;
	}

	if (ast_cel_fill_record(event, &record)) {
		return;
	}

	ast_localtime(&record.event_time, &timeresult, NULL);
	ast_strftime(start_time, sizeof(start_time), DATE_FORMAT, &timeresult);

	event_name = record.event_name;
	user_defined_header[0] = '\0';
	if (record.event_type == AST_CEL_USER_DEFINED) {
		if (cel_show_user_def) {
			snprintf(user_defined_header, sizeof(user_defined_header),
				"UserDefType: %s\r\n", record.user_defined_name);
		} else {
			event_name = record.user_defined_name;
		}
	}

	manager_event(EVENT_FLAG_CALL, "CEL",
		"EventName: %s\r\n"
		"AccountCode: %s\r\n"
		"CallerIDnum: %s\r\n"
		"CallerIDname: %s\r\n"
		"CallerIDani: %s\r\n"
		"CallerIDrdnis: %s\r\n"
		"CallerIDdnid: %s\r\n"
		"Exten: %s\r\n"
		"Context: %s\r\n"
		"Channel: %s\r\n"
		"Application: %s\r\n"
		"AppData: %s\r\n"
		"EventTime: %s\r\n"
		"AMAFlags: %s\r\n"
		"UniqueID: %s\r\n"
		"LinkedID: %s\r\n"
		"Userfield: %s\r\n"
		"Peer: %s\r\n"
		"PeerAccount: %s\r\n"
		"%s"
		"Extra: %s\r\n",
		event_name,
		record.account_code,
		record.caller_id_num,
		record.caller_id_name,
		record.caller_id_ani,
		record.caller_id_rdnis,
		record.caller_id_dnid,
		record.extension,
		record.context,
		record.channel_name,
		record.application_name,
		record.application_data,
		start_time,
		ast_channel_amaflags2string(record.amaflag),
		record.unique_id,
		record.linked_id,
		record.user_field,
		record.peer,
		record.peer_account,
		user_defined_header,
		record.extra);
}

static int load_config(int reload)
{
	const char *cat = NULL;
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_variable *v;
	int newenablecel = CEL_AMI_ENABLED_DEFAULT;
	int new_cel_show_user_def = CEL_SHOW_USERDEF_DEFAULT;

	cfg = ast_config_load(CONF_FILE, config_flags);
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Configuration file '%s' is invalid. CEL manager Module not activated.\n",
			CONF_FILE);
		enablecel = 0;
		return -1;
	} else if (!cfg) {
		ast_log(LOG_WARNING, "Failed to load configuration file. CEL manager Module not activated.\n");
		enablecel = 0;
		return -1;
	}

	while ((cat = ast_category_browse(cfg, cat))) {
		if (strcasecmp(cat, "manager")) {
			continue;
		}

		for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
			if (!strcasecmp(v->name, "enabled")) {
				newenablecel = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "show_user_defined")) {
				new_cel_show_user_def = ast_true(v->value) ? 1 : 0;
			} else {
				ast_log(LOG_NOTICE, "Unknown option '%s' specified "
						"for cel_manager.\n", v->name);
			}
		}
	}

	ast_config_destroy(cfg);

	cel_show_user_def = new_cel_show_user_def;
	if (enablecel && !newenablecel) {
		ast_cel_backend_unregister(MANAGER_BACKEND_NAME);
	} else if (!enablecel && newenablecel) {
		if (ast_cel_backend_register(MANAGER_BACKEND_NAME, manager_log)) {
			ast_log(LOG_ERROR, "Unable to register Asterisk Call Manager CEL handling\n");
		}
	}
	enablecel = newenablecel;

	return 0;
}

static int unload_module(void)
{
	ast_cel_backend_unregister(MANAGER_BACKEND_NAME);
	return 0;
}

static int load_module(void)
{
	if (load_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Asterisk Manager Interface CEL Backend",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cel",
);
