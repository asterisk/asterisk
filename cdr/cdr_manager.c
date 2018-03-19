/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2005
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
 * \brief Asterisk Call Manager CDR records.
 *
 * See also
 * \arg \ref AstCDR
 * \arg \ref AstAMI
 * \arg \ref Config_ami
 * \ingroup cdr_drivers
 */

/*! \li \ref cdr_manager.c uses the configuration file \ref cdr_manager.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page cdr_manager.conf cdr_manager.conf
 * \verbinclude cdr_manager.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<managerEvent language="en_US" name="Cdr">
		<managerEventInstance class="EVENT_FLAG_CDR">
			<synopsis>Raised when a CDR is generated.</synopsis>
			<syntax>
				<parameter name="AccountCode">
					<para>The account code of the Party A channel.</para>
				</parameter>
				<parameter name="Source">
					<para>The Caller ID number associated with the Party A in the CDR.</para>
				</parameter>
				<parameter name="Destination">
					<para>The dialplan extension the Party A was executing.</para>
				</parameter>
				<parameter name="DestinationContext">
					<para>The dialplan context the Party A was executing.</para>
				</parameter>
				<parameter name="CallerID">
					<para>The Caller ID name associated with the Party A in the CDR.</para>
				</parameter>
				<parameter name="Channel">
					<para>The channel name of the Party A.</para>
				</parameter>
				<parameter name="DestinationChannel">
					<para>The channel name of the Party B.</para>
				</parameter>
				<parameter name="LastApplication">
					<para>The last dialplan application the Party A executed.</para>
				</parameter>
				<parameter name="LastData">
					<para>
						The parameters passed to the last dialplan application the
						Party A executed.
					</para>
				</parameter>
				<parameter name="StartTime">
					<para>The time the CDR was created.</para>
				</parameter>
				<parameter name="AnswerTime">
					<para>
						The earliest of either the time when Party A answered, or
						the start time of this CDR.
					</para>
				</parameter>
				<parameter name="EndTime">
					<para>
						The time when the CDR was finished. This occurs when the
						Party A hangs up or when the bridge between Party A and
						Party B is broken.
					</para>
				</parameter>
				<parameter name="Duration">
					<para>The time, in seconds, of <replaceable>EndTime</replaceable> - <replaceable>StartTime</replaceable>.</para>
				</parameter>
				<parameter name="BillableSeconds">
					<para>The time, in seconds, of <replaceable>AnswerTime</replaceable> - <replaceable>StartTime</replaceable>.</para>
				</parameter>
				<parameter name="Disposition">
					<para>The final known disposition of the CDR.</para>
					<enumlist>
						<enum name="NO ANSWER">
							<para>The channel was not answered. This is the default disposition.</para>
						</enum>
						<enum name="FAILED">
							<para>The channel attempted to dial but the call failed.</para>
							<note>
								<para>The congestion setting in <filename>cdr.conf</filename> can result
								in the <literal>AST_CAUSE_CONGESTION</literal> hang up cause or the
								<literal>CONGESTION</literal> dial status to map to this disposition.
								</para>
							</note>
						</enum>
						<enum name="BUSY">
							<para>The channel attempted to dial but the remote party was busy.</para>
						</enum>
						<enum name="ANSWERED">
							<para>The channel was answered. The hang up cause will no longer
							impact the disposition of the CDR.</para>
						</enum>
						<enum name="CONGESTION">
							<para>The channel attempted to dial but the remote party was congested.</para>
						</enum>
					</enumlist>
				</parameter>
				<parameter name="AMAFlags">
					<para>A flag that informs a billing system how to treat the CDR.</para>
					<enumlist>
						<enum name="OMIT">
							<para>This CDR should be ignored.</para>
						</enum>
						<enum name="BILLING">
							<para>This CDR contains valid billing data.</para>
						</enum>
						<enum name="DOCUMENTATION">
							<para>This CDR is for documentation purposes.</para>
						</enum>
					</enumlist>
				</parameter>
				<parameter name="UniqueID">
					<para>A unique identifier for the Party A channel.</para>
				</parameter>
				<parameter name="UserField">
					<para>
						A user defined field set on the channels. If set on both the Party A
						and Party B channel, the userfields of both are concatenated and
						separated by a <literal>;</literal>.
					</para>
				</parameter>
			</syntax>
			<description>
				<para>
					The <replaceable>Cdr</replaceable> event is only raised when the
					<filename>cdr_manager</filename> backend is loaded and registered with
					the CDR engine.
				</para>
				<note>
					<para>
						This event can contain additional fields depending on the configuration
						provided by <filename>cdr_manager.conf</filename>.
					</para>
				</note>
			</description>
		</managerEventInstance>
	</managerEvent>
 ***/

#include "asterisk.h"

#include <time.h>

#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"

#define DATE_FORMAT 	"%Y-%m-%d %T"
#define CONF_FILE	"cdr_manager.conf"
#define CUSTOM_FIELDS_BUF_SIZE 1024

static const char name[] = "cdr_manager";

static int enablecdr = 0;

static struct ast_str *customfields;
AST_RWLOCK_DEFINE_STATIC(customfields_lock);

static int manager_log(struct ast_cdr *cdr);

static int load_config(int reload)
{
	char *cat = NULL;
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	int newenablecdr = 0;

	cfg = ast_config_load(CONF_FILE, config_flags);
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file '%s' could not be parsed\n", CONF_FILE);
		return -1;
	}

	if (!cfg) {
		/* Standard configuration */
		ast_log(LOG_WARNING, "Failed to load configuration file. Module not activated.\n");
		if (enablecdr) {
			ast_cdr_backend_suspend(name);
		}
		enablecdr = 0;
		return -1;
	}

	if (reload) {
		ast_rwlock_wrlock(&customfields_lock);
	}

	if (reload && customfields) {
		ast_free(customfields);
		customfields = NULL;
	}

	while ( (cat = ast_category_browse(cfg, cat)) ) {
		if (!strcasecmp(cat, "general")) {
			v = ast_variable_browse(cfg, cat);
			while (v) {
				if (!strcasecmp(v->name, "enabled"))
					newenablecdr = ast_true(v->value);

				v = v->next;
			}
		} else if (!strcasecmp(cat, "mappings")) {
			customfields = ast_str_create(CUSTOM_FIELDS_BUF_SIZE);
			v = ast_variable_browse(cfg, cat);
			while (v) {
				if (customfields && !ast_strlen_zero(v->name) && !ast_strlen_zero(v->value)) {
					if ((ast_str_strlen(customfields) + strlen(v->value) + strlen(v->name) + 14) < ast_str_size(customfields)) {
						ast_str_append(&customfields, -1, "%s: ${CDR(%s)}\r\n", v->value, v->name);
						ast_log(LOG_NOTICE, "Added mapping %s: ${CDR(%s)}\n", v->value, v->name);
					} else {
						ast_log(LOG_WARNING, "No more buffer space to add other custom fields\n");
						break;
					}

				}
				v = v->next;
			}
		}
	}

	if (reload) {
		ast_rwlock_unlock(&customfields_lock);
	}

	ast_config_destroy(cfg);

	if (!newenablecdr) {
		ast_cdr_backend_suspend(name);
	} else if (newenablecdr) {
		ast_cdr_backend_unsuspend(name);
	}
	enablecdr = newenablecdr;

	return 0;
}

static int manager_log(struct ast_cdr *cdr)
{
	struct ast_tm timeresult;
	char strStartTime[80] = "";
	char strAnswerTime[80] = "";
	char strEndTime[80] = "";
	char buf[CUSTOM_FIELDS_BUF_SIZE];

	if (!enablecdr)
		return 0;

	ast_localtime(&cdr->start, &timeresult, NULL);
	ast_strftime(strStartTime, sizeof(strStartTime), DATE_FORMAT, &timeresult);

	if (cdr->answer.tv_sec)	{
		ast_localtime(&cdr->answer, &timeresult, NULL);
		ast_strftime(strAnswerTime, sizeof(strAnswerTime), DATE_FORMAT, &timeresult);
	}

	ast_localtime(&cdr->end, &timeresult, NULL);
	ast_strftime(strEndTime, sizeof(strEndTime), DATE_FORMAT, &timeresult);

	buf[0] = '\0';
	ast_rwlock_rdlock(&customfields_lock);
	if (customfields && ast_str_strlen(customfields)) {
		struct ast_channel *dummy = ast_dummy_channel_alloc();
		if (!dummy) {
			ast_log(LOG_ERROR, "Unable to allocate channel for variable substitution.\n");
			return 0;
		}
		ast_channel_cdr_set(dummy, ast_cdr_dup(cdr));
		pbx_substitute_variables_helper(dummy, ast_str_buffer(customfields), buf, sizeof(buf) - 1);
		ast_channel_unref(dummy);
	}
	ast_rwlock_unlock(&customfields_lock);

	manager_event(EVENT_FLAG_CDR, "Cdr",
	    "AccountCode: %s\r\n"
	    "Source: %s\r\n"
	    "Destination: %s\r\n"
	    "DestinationContext: %s\r\n"
	    "CallerID: %s\r\n"
	    "Channel: %s\r\n"
	    "DestinationChannel: %s\r\n"
	    "LastApplication: %s\r\n"
	    "LastData: %s\r\n"
	    "StartTime: %s\r\n"
	    "AnswerTime: %s\r\n"
	    "EndTime: %s\r\n"
	    "Duration: %ld\r\n"
	    "BillableSeconds: %ld\r\n"
	    "Disposition: %s\r\n"
	    "AMAFlags: %s\r\n"
	    "UniqueID: %s\r\n"
	    "UserField: %s\r\n"
	    "%s",
	    cdr->accountcode, cdr->src, cdr->dst, cdr->dcontext, cdr->clid, cdr->channel,
	    cdr->dstchannel, cdr->lastapp, cdr->lastdata, strStartTime, strAnswerTime, strEndTime,
	    cdr->duration, cdr->billsec, ast_cdr_disp2str(cdr->disposition),
	    ast_channel_amaflags2string(cdr->amaflags), cdr->uniqueid, cdr->userfield,buf);

	return 0;
}

static int unload_module(void)
{
	if (ast_cdr_unregister(name)) {
		return -1;
	}

	if (customfields)
		ast_free(customfields);

	return 0;
}

static int load_module(void)
{
	if (ast_cdr_register(name, "Asterisk Manager Interface CDR Backend", manager_log)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (load_config(0)) {
		ast_cdr_unregister(name);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Asterisk Manager Interface CDR Backend",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cdr",
);
