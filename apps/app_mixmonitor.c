/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Anthony Minessale II
 * Copyright (C) 2005 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * Based on app_muxmon.c provided by
 * Anthony Minessale II <anthmct@yahoo.com>
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
 * \brief MixMonitor() - Record a call and mix the audio during the recording
 * \ingroup applications
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Kevin P. Fleming <kpfleming@digium.com>
 *
 * \note Based on app_muxmon.c provided by
 * Anthony Minessale II <anthmct@yahoo.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/paths.h"	/* use ast_config_AST_MONITOR_DIR */
#include "asterisk/stringfields.h"
#include "asterisk/file.h"
#include "asterisk/audiohook.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/autochan.h"
#include "asterisk/manager.h"
#include "asterisk/callerid.h"
#include "asterisk/mod_format.h"
#include "asterisk/linkedlists.h"
#include "asterisk/test.h"
#include "asterisk/mixmonitor.h"
#include "asterisk/format_cache.h"
#include "asterisk/beep.h"

/*** DOCUMENTATION
	<application name="MixMonitor" language="en_US">
		<synopsis>
			Record a call and mix the audio during the recording.  Use of StopMixMonitor is required
			to guarantee the audio file is available for processing during dialplan execution.
		</synopsis>
		<syntax>
			<parameter name="file" required="true" argsep=".">
				<argument name="filename" required="true">
					<para>If <replaceable>filename</replaceable> is an absolute path, uses that path, otherwise
					creates the file in the configured monitoring directory from <filename>asterisk.conf.</filename></para>
				</argument>
				<argument name="extension" required="true" />
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="a">
						<para>Append to the file instead of overwriting it.</para>
					</option>
					<option name="b">
						<para>Only save audio to the file while the channel is bridged.</para>
						<note><para>If you utilize this option inside a Local channel, you must make sure the Local
						channel is not optimized away. To do this, be sure to call your Local channel with the
						<literal>/n</literal> option. For example: Dial(Local/start@mycontext/n)</para></note>
					</option>
					<option name="B">
						<para>Play a periodic beep while this call is being recorded.</para>
						<argument name="interval"><para>Interval, in seconds. Default is 15.</para></argument>
					</option>
					<option name="v">
						<para>Adjust the <emphasis>heard</emphasis> volume by a factor of <replaceable>x</replaceable>
						(range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
					<option name="V">
						<para>Adjust the <emphasis>spoken</emphasis> volume by a factor
						of <replaceable>x</replaceable> (range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
					<option name="W">
						<para>Adjust both, <emphasis>heard and spoken</emphasis> volumes by a factor
						of <replaceable>x</replaceable> (range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
					<option name="r">
						<argument name="file" required="true" />
						<para>Use the specified file to record the <emphasis>receive</emphasis> audio feed.
						Like with the basic filename argument, if an absolute path isn't given, it will create
						the file in the configured monitoring directory.</para>
					</option>
					<option name="t">
						<argument name="file" required="true" />
						<para>Use the specified file to record the <emphasis>transmit</emphasis> audio feed.
						Like with the basic filename argument, if an absolute path isn't given, it will create
						the file in the configured monitoring directory.</para>
					</option>
					<option name="i">
						<argument name="chanvar" required="true" />
						<para>Stores the MixMonitor's ID on this channel variable.</para>
					</option>
					<option name="p">
						<para>Play a beep on the channel that starts the recording.</para>
					</option>
					<option name="P">
						<para>Play a beep on the channel that stops the recording.</para>
					</option>
					<option name="m">
						<argument name="mailbox" required="true" />
						<para>Create a copy of the recording as a voicemail in the indicated <emphasis>mailbox</emphasis>(es)
						separated by commas eg. m(1111@default,2222@default,...).  Folders can be optionally specified using
						the syntax: mailbox@context/folder</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="command">
				<para>Will be executed when the recording is over.</para>
				<para>Any strings matching <literal>^{X}</literal> will be unescaped to <variable>X</variable>.</para>
				<para>All variables will be evaluated at the time MixMonitor is called.</para>
			</parameter>
		</syntax>
		<description>
			<para>Records the audio on the current channel to the specified file.</para>
			<para>This application does not automatically answer and should be preceeded by
			an application such as Answer or Progress().</para>
			<note><para>MixMonitor runs as an audiohook.</para></note>
			<variablelist>
				<variable name="MIXMONITOR_FILENAME">
					<para>Will contain the filename used to record.</para>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">Monitor</ref>
			<ref type="application">StopMixMonitor</ref>
			<ref type="application">PauseMonitor</ref>
			<ref type="application">UnpauseMonitor</ref>
			<ref type="function">AUDIOHOOK_INHERIT</ref>
		</see-also>
	</application>
	<application name="StopMixMonitor" language="en_US">
		<synopsis>
			Stop recording a call through MixMonitor, and free the recording's file handle.
		</synopsis>
		<syntax>
			<parameter name="MixMonitorID" required="false">
				<para>If a valid ID is provided, then this command will stop only that specific
				MixMonitor.</para>
			</parameter>
		</syntax>
		<description>
			<para>Stops the audio recording that was started with a call to <literal>MixMonitor()</literal>
			on the current channel.</para>
		</description>
		<see-also>
			<ref type="application">MixMonitor</ref>
		</see-also>
	</application>
	<manager name="MixMonitorMute" language="en_US">
		<synopsis>
			Mute / unMute a Mixmonitor recording.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Used to specify the channel to mute.</para>
			</parameter>
			<parameter name="Direction">
				<para>Which part of the recording to mute:  read, write or both (from channel, to channel or both channels).</para>
			</parameter>
			<parameter name="State">
				<para>Turn mute on or off : 1 to turn on, 0 to turn off.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action may be used to mute a MixMonitor recording.</para>
		</description>
	</manager>
	<manager name="MixMonitor" language="en_US">
		<synopsis>
			Record a call and mix the audio during the recording.  Use of StopMixMonitor is required
			to guarantee the audio file is available for processing during dialplan execution.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Used to specify the channel to record.</para>
			</parameter>
			<parameter name="File">
				<para>Is the name of the file created in the monitor spool directory.
				Defaults to the same name as the channel (with slashes replaced with dashes).
				This argument is optional if you specify to record unidirectional audio with
				either the r(filename) or t(filename) options in the options field. If
				neither MIXMONITOR_FILENAME or this parameter is set, the mixed stream won't
				be recorded.</para>
			</parameter>
			<parameter name="options">
				<para>Options that apply to the MixMonitor in the same way as they
				would apply if invoked from the MixMonitor application. For a list of
				available options, see the documentation for the mixmonitor application. </para>
			</parameter>
			<parameter name="Command">
				<para>Will be executed when the recording is over.
				Any strings matching <literal>^{X}</literal> will be unescaped to <variable>X</variable>.
				All variables will be evaluated at the time MixMonitor is called.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action records the audio on the current channel to the specified file.</para>
			<variablelist>
				<variable name="MIXMONITOR_FILENAME">
					<para>Will contain the filename used to record the mixed stream.</para>
				</variable>
			</variablelist>
		</description>
	</manager>
	<manager name="StopMixMonitor" language="en_US">
		<synopsis>
			Stop recording a call through MixMonitor, and free the recording's file handle.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>The name of the channel monitored.</para>
			</parameter>
			<parameter name="MixMonitorID" required="false">
				<para>If a valid ID is provided, then this command will stop only that specific
				MixMonitor.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action stops the audio recording that was started with the <literal>MixMonitor</literal>
			action on the current channel.</para>
		</description>
	</manager>
	<function name="MIXMONITOR" language="en_US">
		<synopsis>
			Retrieve data pertaining to specific instances of MixMonitor on a channel.
		</synopsis>
		<syntax>
			<parameter name="id" required="true">
				<para>The unique ID of the MixMonitor instance. The unique ID can be retrieved through the channel
				variable used as an argument to the <replaceable>i</replaceable> option to MixMonitor.</para>
			</parameter>
			<parameter name="key" required="true">
				<para>The piece of data to retrieve from the MixMonitor.</para>
				<enumlist>
					<enum name="filename" />
				</enumlist>
			</parameter>
		</syntax>
	</function>

 ***/

#define get_volfactor(x) x ? ((x > 0) ? (1 << x) : ((1 << abs(x)) * -1)) : 0

static const char * const app = "MixMonitor";

static const char * const stop_app = "StopMixMonitor";

static const char * const mixmonitor_spy_type = "MixMonitor";

/*!
 * \internal
 * \brief This struct is a list item holds data needed to find a vm_recipient within voicemail
 */
struct vm_recipient {
	char mailbox[AST_MAX_CONTEXT];
	char context[AST_MAX_EXTENSION];
	char folder[80];
	AST_LIST_ENTRY(vm_recipient) list;
};

struct mixmonitor {
	struct ast_audiohook audiohook;
	char *filename;
	char *filename_read;
	char *filename_write;
	char *post_process;
	char *name;
	ast_callid callid;
	unsigned int flags;
	struct ast_autochan *autochan;
	struct mixmonitor_ds *mixmonitor_ds;

	/* the below string fields describe data used for creating voicemails from the recording */
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(call_context);
		AST_STRING_FIELD(call_macrocontext);
		AST_STRING_FIELD(call_extension);
		AST_STRING_FIELD(call_callerchan);
		AST_STRING_FIELD(call_callerid);
	);
	int call_priority;

	/* FUTURE DEVELOPMENT NOTICE
	 * recipient_list will need locks if we make it editable after the monitor is started */
	AST_LIST_HEAD_NOLOCK(, vm_recipient) recipient_list;
};

enum mixmonitor_flags {
	MUXFLAG_APPEND = (1 << 1),
	MUXFLAG_BRIDGED = (1 << 2),
	MUXFLAG_VOLUME = (1 << 3),
	MUXFLAG_READVOLUME = (1 << 4),
	MUXFLAG_WRITEVOLUME = (1 << 5),
	MUXFLAG_READ = (1 << 6),
	MUXFLAG_WRITE = (1 << 7),
	MUXFLAG_COMBINED = (1 << 8),
	MUXFLAG_UID = (1 << 9),
	MUXFLAG_VMRECIPIENTS = (1 << 10),
	MUXFLAG_BEEP = (1 << 11),
	MUXFLAG_BEEP_START = (1 << 12),
	MUXFLAG_BEEP_STOP = (1 << 13)
};

enum mixmonitor_args {
	OPT_ARG_READVOLUME = 0,
	OPT_ARG_WRITEVOLUME,
	OPT_ARG_VOLUME,
	OPT_ARG_WRITENAME,
	OPT_ARG_READNAME,
	OPT_ARG_UID,
	OPT_ARG_VMRECIPIENTS,
	OPT_ARG_BEEP_INTERVAL,
	OPT_ARG_ARRAY_SIZE,	/* Always last element of the enum */
};

AST_APP_OPTIONS(mixmonitor_opts, {
	AST_APP_OPTION('a', MUXFLAG_APPEND),
	AST_APP_OPTION('b', MUXFLAG_BRIDGED),
	AST_APP_OPTION_ARG('B', MUXFLAG_BEEP, OPT_ARG_BEEP_INTERVAL),
	AST_APP_OPTION('p', MUXFLAG_BEEP_START),
	AST_APP_OPTION('P', MUXFLAG_BEEP_STOP),
	AST_APP_OPTION_ARG('v', MUXFLAG_READVOLUME, OPT_ARG_READVOLUME),
	AST_APP_OPTION_ARG('V', MUXFLAG_WRITEVOLUME, OPT_ARG_WRITEVOLUME),
	AST_APP_OPTION_ARG('W', MUXFLAG_VOLUME, OPT_ARG_VOLUME),
	AST_APP_OPTION_ARG('r', MUXFLAG_READ, OPT_ARG_READNAME),
	AST_APP_OPTION_ARG('t', MUXFLAG_WRITE, OPT_ARG_WRITENAME),
	AST_APP_OPTION_ARG('i', MUXFLAG_UID, OPT_ARG_UID),
	AST_APP_OPTION_ARG('m', MUXFLAG_VMRECIPIENTS, OPT_ARG_VMRECIPIENTS),
});

struct mixmonitor_ds {
	unsigned int destruction_ok;
	ast_cond_t destruction_condition;
	ast_mutex_t lock;

	/* The filestream is held in the datastore so it can be stopped
	 * immediately during stop_mixmonitor or channel destruction. */
	int fs_quit;

	struct ast_filestream *fs;
	struct ast_filestream *fs_read;
	struct ast_filestream *fs_write;

	struct ast_audiohook *audiohook;

	unsigned int samp_rate;
	char *filename;
	char *beep_id;
};

/*!
 * \internal
 * \pre mixmonitor_ds must be locked before calling this function
 */
static void mixmonitor_ds_close_fs(struct mixmonitor_ds *mixmonitor_ds)
{
	unsigned char quitting = 0;

	if (mixmonitor_ds->fs) {
		quitting = 1;
		ast_closestream(mixmonitor_ds->fs);
		mixmonitor_ds->fs = NULL;
		ast_verb(2, "MixMonitor close filestream (mixed)\n");
	}

	if (mixmonitor_ds->fs_read) {
		quitting = 1;
		ast_closestream(mixmonitor_ds->fs_read);
		mixmonitor_ds->fs_read = NULL;
		ast_verb(2, "MixMonitor close filestream (read)\n");
	}

	if (mixmonitor_ds->fs_write) {
		quitting = 1;
		ast_closestream(mixmonitor_ds->fs_write);
		mixmonitor_ds->fs_write = NULL;
		ast_verb(2, "MixMonitor close filestream (write)\n");
	}

	if (quitting) {
		mixmonitor_ds->fs_quit = 1;
	}
}

static void mixmonitor_ds_destroy(void *data)
{
	struct mixmonitor_ds *mixmonitor_ds = data;

	ast_mutex_lock(&mixmonitor_ds->lock);
	mixmonitor_ds->audiohook = NULL;
	mixmonitor_ds->destruction_ok = 1;
	ast_free(mixmonitor_ds->filename);
	ast_free(mixmonitor_ds->beep_id);
	ast_cond_signal(&mixmonitor_ds->destruction_condition);
	ast_mutex_unlock(&mixmonitor_ds->lock);
}

static const struct ast_datastore_info mixmonitor_ds_info = {
	.type = "mixmonitor",
	.destroy = mixmonitor_ds_destroy,
};

static void destroy_monitor_audiohook(struct mixmonitor *mixmonitor)
{
	if (mixmonitor->mixmonitor_ds) {
		ast_mutex_lock(&mixmonitor->mixmonitor_ds->lock);
		mixmonitor->mixmonitor_ds->audiohook = NULL;
		ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);
	}
	/* kill the audiohook.*/
	ast_audiohook_lock(&mixmonitor->audiohook);
	ast_audiohook_detach(&mixmonitor->audiohook);
	ast_audiohook_unlock(&mixmonitor->audiohook);
	ast_audiohook_destroy(&mixmonitor->audiohook);
}

static int startmon(struct ast_channel *chan, struct ast_audiohook *audiohook)
{
	if (!chan) {
		return -1;
	}

	return ast_audiohook_attach(chan, audiohook);
}

/*!
 * \internal
 * \brief adds recipients to a mixmonitor's recipient list
 * \param mixmonitor mixmonitor being affected
 * \param vm_recipients string containing the desired recipients to add
 */
static void add_vm_recipients_from_string(struct mixmonitor *mixmonitor, const char *vm_recipients)
{
	/* recipients are in a single string with a format format resembling "mailbox@context/INBOX,mailbox2@context2,mailbox3@context3/Work" */
	char *cur_mailbox = ast_strdupa(vm_recipients);
	char *cur_context;
	char *cur_folder;
	char *next;
	int elements_processed = 0;

	while (!ast_strlen_zero(cur_mailbox)) {
		ast_debug(3, "attempting to add next element %d from %s\n", elements_processed, cur_mailbox);
		if ((next = strchr(cur_mailbox, ',')) || (next = strchr(cur_mailbox, '&'))) {
			*(next++) = '\0';
		}

		if ((cur_folder = strchr(cur_mailbox, '/'))) {
			*(cur_folder++) = '\0';
		} else {
			cur_folder = "INBOX";
		}

		if ((cur_context = strchr(cur_mailbox, '@'))) {
			*(cur_context++) = '\0';
		} else {
			cur_context = "default";
		}

		if (!ast_strlen_zero(cur_mailbox) && !ast_strlen_zero(cur_context)) {
			struct vm_recipient *recipient;
			if (!(recipient = ast_malloc(sizeof(*recipient)))) {
				ast_log(LOG_ERROR, "Failed to allocate recipient. Aborting function.\n");
				return;
			}
			ast_copy_string(recipient->context, cur_context, sizeof(recipient->context));
			ast_copy_string(recipient->mailbox, cur_mailbox, sizeof(recipient->mailbox));
			ast_copy_string(recipient->folder, cur_folder, sizeof(recipient->folder));

			/* Add to list */
			ast_verb(4, "Adding %s@%s to recipient list\n", recipient->mailbox, recipient->context);
			AST_LIST_INSERT_HEAD(&mixmonitor->recipient_list, recipient, list);
		} else {
			ast_log(LOG_ERROR, "Failed to properly parse extension and/or context from element %d of recipient string: %s\n", elements_processed, vm_recipients);
		}

		cur_mailbox = next;
		elements_processed++;
	}
}

static void clear_mixmonitor_recipient_list(struct mixmonitor *mixmonitor)
{
	struct vm_recipient *current;
	while ((current = AST_LIST_REMOVE_HEAD(&mixmonitor->recipient_list, list))) {
		/* Clear list element data */
		ast_free(current);
	}
}

#define SAMPLES_PER_FRAME 160

static void mixmonitor_free(struct mixmonitor *mixmonitor)
{
	if (mixmonitor) {
		if (mixmonitor->mixmonitor_ds) {
			ast_mutex_destroy(&mixmonitor->mixmonitor_ds->lock);
			ast_cond_destroy(&mixmonitor->mixmonitor_ds->destruction_condition);
			ast_free(mixmonitor->mixmonitor_ds);
		}

		ast_free(mixmonitor->name);
		ast_free(mixmonitor->post_process);
		ast_free(mixmonitor->filename);
		ast_free(mixmonitor->filename_write);
		ast_free(mixmonitor->filename_read);

		/* Free everything in the recipient list */
		clear_mixmonitor_recipient_list(mixmonitor);

		/* clean stringfields */
		ast_string_field_free_memory(mixmonitor);

		ast_free(mixmonitor);
	}
}

/*!
 * \internal
 * \brief Copies the mixmonitor to all voicemail recipients
 * \param mixmonitor The mixmonitor that needs to forward its file to recipients
 * \param ext Format of the file that was saved
 */
static void copy_to_voicemail(struct mixmonitor *mixmonitor, const char *ext, const char *filename)
{
	struct vm_recipient *recipient = NULL;
	struct ast_vm_recording_data recording_data;
	if (ast_string_field_init(&recording_data, 512)) {
		ast_log(LOG_ERROR, "Failed to string_field_init, skipping copy_to_voicemail\n");
		return;
	}

	/* Copy strings to stringfields that will be used for all recipients */
	ast_string_field_set(&recording_data, recording_file, filename);
	ast_string_field_set(&recording_data, recording_ext, ext);
	ast_string_field_set(&recording_data, call_context, mixmonitor->call_context);
	ast_string_field_set(&recording_data, call_macrocontext, mixmonitor->call_macrocontext);
	ast_string_field_set(&recording_data, call_extension, mixmonitor->call_extension);
	ast_string_field_set(&recording_data, call_callerchan, mixmonitor->call_callerchan);
	ast_string_field_set(&recording_data, call_callerid, mixmonitor->call_callerid);
	/* and call_priority gets copied too */
	recording_data.call_priority = mixmonitor->call_priority;

	AST_LIST_TRAVERSE(&mixmonitor->recipient_list, recipient, list) {
		/* context, mailbox, and folder need to be set per recipient */
		ast_string_field_set(&recording_data, context, recipient->context);
		ast_string_field_set(&recording_data, mailbox, recipient->mailbox);
		ast_string_field_set(&recording_data, folder, recipient->folder);

		ast_verb(4, "MixMonitor attempting to send voicemail copy to %s@%s\n", recording_data.mailbox,
			recording_data.context);
		ast_app_copy_recording_to_vm(&recording_data);
	}

	/* Free the string fields for recording_data before exiting the function. */
	ast_string_field_free_memory(&recording_data);
}

static void mixmonitor_save_prep(struct mixmonitor *mixmonitor, char *filename, struct ast_filestream **fs, unsigned int *oflags, int *errflag, char **ext)
{
	/* Initialize the file if not already done so */
	char *last_slash = NULL;
	if (!ast_strlen_zero(filename)) {
		if (!*fs && !*errflag && !mixmonitor->mixmonitor_ds->fs_quit) {
			*oflags = O_CREAT | O_WRONLY;
			*oflags |= ast_test_flag(mixmonitor, MUXFLAG_APPEND) ? O_APPEND : O_TRUNC;

			last_slash = strrchr(filename, '/');

			if ((*ext = strrchr(filename, '.')) && (*ext > last_slash)) {
				**ext = '\0';
				*ext = *ext + 1;
			} else {
				*ext = "raw";
			}

			if (!(*fs = ast_writefile(filename, *ext, NULL, *oflags, 0, 0666))) {
				ast_log(LOG_ERROR, "Cannot open %s.%s\n", filename, *ext);
				*errflag = 1;
			} else {
				struct ast_filestream *tmp = *fs;
				mixmonitor->mixmonitor_ds->samp_rate = MAX(mixmonitor->mixmonitor_ds->samp_rate, ast_format_get_sample_rate(tmp->fmt->format));
			}
		}
	}
}

static void *mixmonitor_thread(void *obj)
{
	struct mixmonitor *mixmonitor = obj;
	char *fs_ext = "";
	char *fs_read_ext = "";
	char *fs_write_ext = "";

	struct ast_filestream **fs = NULL;
	struct ast_filestream **fs_read = NULL;
	struct ast_filestream **fs_write = NULL;

	unsigned int oflags;
	int errflag = 0;
	struct ast_format *format_slin;

	/* Keep callid association before any log messages */
	if (mixmonitor->callid) {
		ast_callid_threadassoc_add(mixmonitor->callid);
	}

	ast_verb(2, "Begin MixMonitor Recording %s\n", mixmonitor->name);

	fs = &mixmonitor->mixmonitor_ds->fs;
	fs_read = &mixmonitor->mixmonitor_ds->fs_read;
	fs_write = &mixmonitor->mixmonitor_ds->fs_write;

	ast_mutex_lock(&mixmonitor->mixmonitor_ds->lock);
	mixmonitor_save_prep(mixmonitor, mixmonitor->filename, fs, &oflags, &errflag, &fs_ext);
	mixmonitor_save_prep(mixmonitor, mixmonitor->filename_read, fs_read, &oflags, &errflag, &fs_read_ext);
	mixmonitor_save_prep(mixmonitor, mixmonitor->filename_write, fs_write, &oflags, &errflag, &fs_write_ext);

	format_slin = ast_format_cache_get_slin_by_rate(mixmonitor->mixmonitor_ds->samp_rate);

	ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);

	/* The audiohook must enter and exit the loop locked */
	ast_audiohook_lock(&mixmonitor->audiohook);
	while (mixmonitor->audiohook.status == AST_AUDIOHOOK_STATUS_RUNNING && !mixmonitor->mixmonitor_ds->fs_quit) {
		struct ast_frame *fr = NULL;
		struct ast_frame *fr_read = NULL;
		struct ast_frame *fr_write = NULL;

		if (!(fr = ast_audiohook_read_frame_all(&mixmonitor->audiohook, SAMPLES_PER_FRAME, format_slin,
						&fr_read, &fr_write))) {
			ast_audiohook_trigger_wait(&mixmonitor->audiohook);

			if (mixmonitor->audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING) {
				break;
			}
			continue;
		}

		/* audiohook lock is not required for the next block.
		 * Unlock it, but remember to lock it before looping or exiting */
		ast_audiohook_unlock(&mixmonitor->audiohook);

		if (!ast_test_flag(mixmonitor, MUXFLAG_BRIDGED)
			|| (mixmonitor->autochan->chan
				&& ast_channel_is_bridged(mixmonitor->autochan->chan))) {
			ast_mutex_lock(&mixmonitor->mixmonitor_ds->lock);

			/* Write out the frame(s) */
			if ((*fs_read) && (fr_read)) {
				struct ast_frame *cur;

				for (cur = fr_read; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
					ast_writestream(*fs_read, cur);
				}
			}

			if ((*fs_write) && (fr_write)) {
				struct ast_frame *cur;

				for (cur = fr_write; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
					ast_writestream(*fs_write, cur);
				}
			}

			if ((*fs) && (fr)) {
				struct ast_frame *cur;

				for (cur = fr; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
					ast_writestream(*fs, cur);
				}
			}
			ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);
		}
		/* All done! free it. */
		if (fr) {
			ast_frame_free(fr, 0);
		}
		if (fr_read) {
			ast_frame_free(fr_read, 0);
		}
		if (fr_write) {
			ast_frame_free(fr_write, 0);
		}

		fr = NULL;
		fr_write = NULL;
		fr_read = NULL;

		ast_audiohook_lock(&mixmonitor->audiohook);
	}

	ast_audiohook_unlock(&mixmonitor->audiohook);

	ast_channel_lock(mixmonitor->autochan->chan);
	if (ast_test_flag(mixmonitor, MUXFLAG_BEEP_STOP)) {
		ast_stream_and_wait(mixmonitor->autochan->chan, "beep", "");
	}
	ast_channel_unlock(mixmonitor->autochan->chan);

	ast_autochan_destroy(mixmonitor->autochan);

	/* Datastore cleanup.  close the filestream and wait for ds destruction */
	ast_mutex_lock(&mixmonitor->mixmonitor_ds->lock);
	mixmonitor_ds_close_fs(mixmonitor->mixmonitor_ds);
	if (!mixmonitor->mixmonitor_ds->destruction_ok) {
		ast_cond_wait(&mixmonitor->mixmonitor_ds->destruction_condition, &mixmonitor->mixmonitor_ds->lock);
	}
	ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);

	/* kill the audiohook */
	destroy_monitor_audiohook(mixmonitor);

	if (mixmonitor->post_process) {
		ast_verb(2, "Executing [%s]\n", mixmonitor->post_process);
		ast_safe_system(mixmonitor->post_process);
	}

	ast_verb(2, "End MixMonitor Recording %s\n", mixmonitor->name);
	ast_test_suite_event_notify("MIXMONITOR_END", "File: %s\r\n", mixmonitor->filename);

	if (!AST_LIST_EMPTY(&mixmonitor->recipient_list)) {
		if (ast_strlen_zero(fs_ext)) {
			ast_log(LOG_ERROR, "No file extension set for Mixmonitor %s. Skipping copy to voicemail.\n",
				mixmonitor -> name);
		} else {
			ast_verb(3, "Copying recordings for Mixmonitor %s to voicemail recipients\n", mixmonitor->name);
			copy_to_voicemail(mixmonitor, fs_ext, mixmonitor->filename);
		}
		if (!ast_strlen_zero(fs_read_ext)) {
			ast_verb(3, "Copying read recording for Mixmonitor %s to voicemail recipients\n", mixmonitor->name);
			copy_to_voicemail(mixmonitor, fs_read_ext, mixmonitor->filename_read);
		}
		if (!ast_strlen_zero(fs_write_ext)) {
			ast_verb(3, "Copying write recording for Mixmonitor %s to voicemail recipients\n", mixmonitor->name);
			copy_to_voicemail(mixmonitor, fs_write_ext, mixmonitor->filename_write);
		}
	} else {
		ast_debug(3, "No recipients to forward monitor to, moving on.\n");
	}

	mixmonitor_free(mixmonitor);

	ast_module_unref(ast_module_info->self);
	return NULL;
}

static int setup_mixmonitor_ds(struct mixmonitor *mixmonitor, struct ast_channel *chan, char **datastore_id, const char *beep_id)
{
	struct ast_datastore *datastore = NULL;
	struct mixmonitor_ds *mixmonitor_ds;

	if (!(mixmonitor_ds = ast_calloc(1, sizeof(*mixmonitor_ds)))) {
		return -1;
	}

	if (ast_asprintf(datastore_id, "%p", mixmonitor_ds) == -1) {
		ast_log(LOG_ERROR, "Failed to allocate memory for MixMonitor ID.\n");
	}

	ast_mutex_init(&mixmonitor_ds->lock);
	ast_cond_init(&mixmonitor_ds->destruction_condition, NULL);

	if (!(datastore = ast_datastore_alloc(&mixmonitor_ds_info, *datastore_id))) {
		ast_mutex_destroy(&mixmonitor_ds->lock);
		ast_cond_destroy(&mixmonitor_ds->destruction_condition);
		ast_free(mixmonitor_ds);
		return -1;
	}

	ast_channel_lock(mixmonitor->autochan->chan);
	if (ast_test_flag(mixmonitor, MUXFLAG_BEEP_START)) {
		ast_stream_and_wait(mixmonitor->autochan->chan, "beep", "");
	}
	ast_channel_unlock(mixmonitor->autochan->chan);

	mixmonitor_ds->samp_rate = 8000;
	mixmonitor_ds->audiohook = &mixmonitor->audiohook;
	mixmonitor_ds->filename = ast_strdup(mixmonitor->filename);
	if (!ast_strlen_zero(beep_id)) {
		mixmonitor_ds->beep_id = ast_strdup(beep_id);
	}
	datastore->data = mixmonitor_ds;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	mixmonitor->mixmonitor_ds = mixmonitor_ds;
	return 0;
}

static int launch_monitor_thread(struct ast_channel *chan, const char *filename,
				  unsigned int flags, int readvol, int writevol,
				  const char *post_process, const char *filename_write,
				  char *filename_read, const char *uid_channel_var,
				  const char *recipients, const char *beep_id)
{
	pthread_t thread;
	struct mixmonitor *mixmonitor;
	char postprocess2[1024] = "";
	char *datastore_id = NULL;

	postprocess2[0] = 0;
	/* If a post process system command is given attach it to the structure */
	if (!ast_strlen_zero(post_process)) {
		char *p1, *p2;

		p1 = ast_strdupa(post_process);
		for (p2 = p1; *p2; p2++) {
			if (*p2 == '^' && *(p2+1) == '{') {
				*p2 = '$';
			}
		}
		ast_channel_lock(chan);
		pbx_substitute_variables_helper(chan, p1, postprocess2, sizeof(postprocess2) - 1);
		ast_channel_unlock(chan);
	}

	/* Pre-allocate mixmonitor structure and spy */
	if (!(mixmonitor = ast_calloc(1, sizeof(*mixmonitor)))) {
		return -1;
	}

	/* Now that the struct has been calloced, go ahead and initialize the string fields. */
	if (ast_string_field_init(mixmonitor, 512)) {
		mixmonitor_free(mixmonitor);
		return -1;
	}

	/* Setup the actual spy before creating our thread */
	if (ast_audiohook_init(&mixmonitor->audiohook, AST_AUDIOHOOK_TYPE_SPY, mixmonitor_spy_type, 0)) {
		mixmonitor_free(mixmonitor);
		return -1;
	}

	/* Copy over flags and channel name */
	mixmonitor->flags = flags;
	if (!(mixmonitor->autochan = ast_autochan_setup(chan))) {
		mixmonitor_free(mixmonitor);
		return -1;
	}

	if (!ast_strlen_zero(filename)) {
		mixmonitor->filename = ast_strdup(filename);
	}

	if (!ast_strlen_zero(filename_write)) {
		mixmonitor->filename_write = ast_strdup(filename_write);
	}

	if (!ast_strlen_zero(filename_read)) {
		mixmonitor->filename_read = ast_strdup(filename_read);
	}

	if (setup_mixmonitor_ds(mixmonitor, chan, &datastore_id, beep_id)) {
		ast_autochan_destroy(mixmonitor->autochan);
		mixmonitor_free(mixmonitor);
		ast_free(datastore_id);
		return -1;
	}

	if (!ast_strlen_zero(uid_channel_var)) {
		if (datastore_id) {
			pbx_builtin_setvar_helper(chan, uid_channel_var, datastore_id);
		}
	}
	ast_free(datastore_id);

	mixmonitor->name = ast_strdup(ast_channel_name(chan));

	if (!ast_strlen_zero(postprocess2)) {
		mixmonitor->post_process = ast_strdup(postprocess2);
	}

	if (!ast_strlen_zero(recipients)) {
		char callerid[256];
		struct ast_party_connected_line *connected;

		ast_channel_lock(chan);

		/* We use the connected line of the invoking channel for caller ID. */

		connected = ast_channel_connected(chan);
		ast_debug(3, "Connected Line CID = %d - %s : %d - %s\n", connected->id.name.valid,
			connected->id.name.str, connected->id.number.valid,
			connected->id.number.str);
		ast_callerid_merge(callerid, sizeof(callerid),
			S_COR(connected->id.name.valid, connected->id.name.str, NULL),
			S_COR(connected->id.number.valid, connected->id.number.str, NULL),
			"Unknown");

		ast_string_field_set(mixmonitor, call_context, ast_channel_context(chan));
		ast_string_field_set(mixmonitor, call_macrocontext, ast_channel_macrocontext(chan));
		ast_string_field_set(mixmonitor, call_extension, ast_channel_exten(chan));
		ast_string_field_set(mixmonitor, call_callerchan, ast_channel_name(chan));
		ast_string_field_set(mixmonitor, call_callerid, callerid);
		mixmonitor->call_priority = ast_channel_priority(chan);

		ast_channel_unlock(chan);

		add_vm_recipients_from_string(mixmonitor, recipients);
	}

	ast_set_flag(&mixmonitor->audiohook, AST_AUDIOHOOK_TRIGGER_SYNC);

	if (readvol)
		mixmonitor->audiohook.options.read_volume = readvol;
	if (writevol)
		mixmonitor->audiohook.options.write_volume = writevol;

	if (startmon(chan, &mixmonitor->audiohook)) {
		ast_log(LOG_WARNING, "Unable to add '%s' spy to channel '%s'\n",
			mixmonitor_spy_type, ast_channel_name(chan));
		ast_audiohook_destroy(&mixmonitor->audiohook);
		mixmonitor_free(mixmonitor);
		return -1;
	}

	/* reference be released at mixmonitor destruction */
	mixmonitor->callid = ast_read_threadstorage_callid();

	return ast_pthread_create_detached_background(&thread, NULL, mixmonitor_thread, mixmonitor);
}

/* a note on filename_parse: creates directory structure and assigns absolute path from relative paths for filenames */
/* requires immediate copying of string from return to retain data since otherwise it will immediately lose scope */
static char *filename_parse(char *filename, char *buffer, size_t len)
{
	char *slash;
	if (ast_strlen_zero(filename)) {
		ast_log(LOG_WARNING, "No file name was provided for a file save option.\n");
	} else if (filename[0] != '/') {
		char *build;
		build = ast_alloca(strlen(ast_config_AST_MONITOR_DIR) + strlen(filename) + 3);
		sprintf(build, "%s/%s", ast_config_AST_MONITOR_DIR, filename);
		filename = build;
	}

	ast_copy_string(buffer, filename, len);

	if ((slash = strrchr(filename, '/'))) {
		*slash = '\0';
	}
	ast_mkdir(filename, 0777);

	return buffer;
}

static int mixmonitor_exec(struct ast_channel *chan, const char *data)
{
	int x, readvol = 0, writevol = 0;
	char *filename_read = NULL;
	char *filename_write = NULL;
	char filename_buffer[1024] = "";
	char *uid_channel_var = NULL;
	char beep_id[64] = "";

	struct ast_flags flags = { 0 };
	char *recipients = NULL;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(options);
		AST_APP_ARG(post_process);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MixMonitor requires an argument (filename or ,t(filename) and/or r(filename)\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.options) {
		char *opts[OPT_ARG_ARRAY_SIZE] = { NULL, };

		ast_app_parse_options(mixmonitor_opts, &flags, opts, args.options);

		if (ast_test_flag(&flags, MUXFLAG_READVOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_READVOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the heard volume ('v') option.\n");
			} else if ((sscanf(opts[OPT_ARG_READVOLUME], "%2d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Heard volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_READVOLUME]);
			} else {
				readvol = get_volfactor(x);
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_WRITEVOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_WRITEVOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the spoken volume ('V') option.\n");
			} else if ((sscanf(opts[OPT_ARG_WRITEVOLUME], "%2d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Spoken volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_WRITEVOLUME]);
			} else {
				writevol = get_volfactor(x);
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_VOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_VOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the combined volume ('W') option.\n");
			} else if ((sscanf(opts[OPT_ARG_VOLUME], "%2d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Combined volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_VOLUME]);
			} else {
				readvol = writevol = get_volfactor(x);
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_VMRECIPIENTS)) {
			if (ast_strlen_zero(opts[OPT_ARG_VMRECIPIENTS])) {
				ast_log(LOG_WARNING, "No voicemail recipients were specified for the vm copy ('m') option.\n");
			} else {
				recipients = ast_strdupa(opts[OPT_ARG_VMRECIPIENTS]);
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_WRITE)) {
			filename_write = ast_strdupa(filename_parse(opts[OPT_ARG_WRITENAME], filename_buffer, sizeof(filename_buffer)));
		}

		if (ast_test_flag(&flags, MUXFLAG_READ)) {
			filename_read = ast_strdupa(filename_parse(opts[OPT_ARG_READNAME], filename_buffer, sizeof(filename_buffer)));
		}

		if (ast_test_flag(&flags, MUXFLAG_UID)) {
			uid_channel_var = opts[OPT_ARG_UID];
		}

		if (ast_test_flag(&flags, MUXFLAG_BEEP)) {
			const char *interval_str = S_OR(opts[OPT_ARG_BEEP_INTERVAL], "15");
			unsigned int interval = 15;

			if (sscanf(interval_str, "%30u", &interval) != 1) {
				ast_log(LOG_WARNING, "Invalid interval '%s' for periodic beep. Using default of %u\n",
						interval_str, interval);
			}

			if (ast_beep_start(chan, interval, beep_id, sizeof(beep_id))) {
				ast_log(LOG_WARNING, "Unable to enable periodic beep, please ensure func_periodic_hook is loaded.\n");
				return -1;
			}
		}
	}
	/* If there are no file writing arguments/options for the mix monitor, send a warning message and return -1 */

	if (!ast_test_flag(&flags, MUXFLAG_WRITE) && !ast_test_flag(&flags, MUXFLAG_READ) && ast_strlen_zero(args.filename)) {
		ast_log(LOG_WARNING, "MixMonitor requires an argument (filename)\n");
		return -1;
	}

	/* If filename exists, try to create directories for it */
	if (!(ast_strlen_zero(args.filename))) {
		args.filename = ast_strdupa(filename_parse(args.filename, filename_buffer, sizeof(filename_buffer)));
	}

	pbx_builtin_setvar_helper(chan, "MIXMONITOR_FILENAME", args.filename);

	/* If launch_monitor_thread works, the module reference must not be released until it is finished. */
	ast_module_ref(ast_module_info->self);
	if (launch_monitor_thread(chan,
			args.filename,
			flags.flags,
			readvol,
			writevol,
			args.post_process,
			filename_write,
			filename_read,
			uid_channel_var,
			recipients,
			beep_id)) {
		ast_module_unref(ast_module_info->self);
	}

	return 0;
}

static int stop_mixmonitor_full(struct ast_channel *chan, const char *data)
{
	struct ast_datastore *datastore = NULL;
	char *parse = "";
	struct mixmonitor_ds *mixmonitor_ds;
	const char *beep_id = NULL;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(mixmonid);
	);

	if (!ast_strlen_zero(data)) {
		parse = ast_strdupa(data);
	}

	AST_STANDARD_APP_ARGS(args, parse);

	ast_channel_lock(chan);

	datastore = ast_channel_datastore_find(chan, &mixmonitor_ds_info,
		S_OR(args.mixmonid, NULL));
	if (!datastore) {
		ast_channel_unlock(chan);
		return -1;
	}
	mixmonitor_ds = datastore->data;

	ast_mutex_lock(&mixmonitor_ds->lock);

	/* closing the filestream here guarantees the file is available to the dialplan
	 * after calling StopMixMonitor */
	mixmonitor_ds_close_fs(mixmonitor_ds);

	/* The mixmonitor thread may be waiting on the audiohook trigger.
	 * In order to exit from the mixmonitor loop before waiting on channel
	 * destruction, poke the audiohook trigger. */
	if (mixmonitor_ds->audiohook) {
		if (mixmonitor_ds->audiohook->status != AST_AUDIOHOOK_STATUS_DONE) {
			ast_audiohook_update_status(mixmonitor_ds->audiohook, AST_AUDIOHOOK_STATUS_SHUTDOWN);
		}
		ast_audiohook_lock(mixmonitor_ds->audiohook);
		ast_cond_signal(&mixmonitor_ds->audiohook->trigger);
		ast_audiohook_unlock(mixmonitor_ds->audiohook);
		mixmonitor_ds->audiohook = NULL;
	}

	if (!ast_strlen_zero(mixmonitor_ds->beep_id)) {
		beep_id = ast_strdupa(mixmonitor_ds->beep_id);
	}

	ast_mutex_unlock(&mixmonitor_ds->lock);

	/* Remove the datastore so the monitor thread can exit */
	if (!ast_channel_datastore_remove(chan, datastore)) {
		ast_datastore_free(datastore);
	}

	ast_channel_unlock(chan);

	if (!ast_strlen_zero(beep_id)) {
		ast_beep_stop(chan, beep_id);
	}

	return 0;
}

static int stop_mixmonitor_exec(struct ast_channel *chan, const char *data)
{
	stop_mixmonitor_full(chan, data);
	return 0;
}

static char *handle_cli_mixmonitor(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;
	struct ast_datastore *datastore = NULL;
	struct mixmonitor_ds *mixmonitor_ds = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mixmonitor {start|stop|list}";
		e->usage =
			"Usage: mixmonitor start <chan_name> [args]\n"
			"         The optional arguments are passed to the MixMonitor application.\n"
			"       mixmonitor stop <chan_name> [args]\n"
			"         The optional arguments are passed to the StopMixMonitor application.\n"
			"       mixmonitor list <chan_name>\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 2);
	}

	if (a->argc < 3) {
		return CLI_SHOWUSAGE;
	}

	if (!(chan = ast_channel_get_by_name_prefix(a->argv[2], strlen(a->argv[2])))) {
		ast_cli(a->fd, "No channel matching '%s' found.\n", a->argv[2]);
		/* Technically this is a failure, but we don't want 2 errors printing out */
		return CLI_SUCCESS;
	}

	if (!strcasecmp(a->argv[1], "start")) {
		mixmonitor_exec(chan, (a->argc >= 4) ? a->argv[3] : "");
	} else if (!strcasecmp(a->argv[1], "stop")){
		stop_mixmonitor_exec(chan, (a->argc >= 4) ? a->argv[3] : "");
	} else if (!strcasecmp(a->argv[1], "list")) {
		ast_cli(a->fd, "MixMonitor ID\tFile\tReceive File\tTransmit File\n");
		ast_cli(a->fd, "=========================================================================\n");
		ast_channel_lock(chan);
		AST_LIST_TRAVERSE(ast_channel_datastores(chan), datastore, entry) {
			if (datastore->info == &mixmonitor_ds_info) {
				char *filename = "";
				char *filename_read = "";
				char *filename_write = "";

				mixmonitor_ds = datastore->data;
				if (mixmonitor_ds->fs) {
					filename = mixmonitor_ds->fs->filename;
				}
				if (mixmonitor_ds->fs_read) {
					filename_read = mixmonitor_ds->fs_read->filename;
				}
				if (mixmonitor_ds->fs_write) {
					filename_write = mixmonitor_ds->fs_write->filename;
				}
				ast_cli(a->fd, "%p\t%s\t%s\t%s\n", mixmonitor_ds, filename, filename_read, filename_write);
			}
		}
		ast_channel_unlock(chan);
	} else {
		chan = ast_channel_unref(chan);
		return CLI_SHOWUSAGE;
	}

	chan = ast_channel_unref(chan);

	return CLI_SUCCESS;
}

/*! \brief  Mute / unmute  a MixMonitor channel */
static int manager_mute_mixmonitor(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m, "ActionID");
	const char *state = astman_get_header(m, "State");
	const char *direction = astman_get_header(m,"Direction");
	int clearmute = 1;
	enum ast_audiohook_flags flag;

	if (ast_strlen_zero(direction)) {
		astman_send_error(s, m, "No direction specified. Must be read, write or both");
		return AMI_SUCCESS;
	}

	if (!strcasecmp(direction, "read")) {
		flag = AST_AUDIOHOOK_MUTE_READ;
	} else  if (!strcasecmp(direction, "write")) {
		flag = AST_AUDIOHOOK_MUTE_WRITE;
	} else  if (!strcasecmp(direction, "both")) {
		flag = AST_AUDIOHOOK_MUTE_READ | AST_AUDIOHOOK_MUTE_WRITE;
	} else {
		astman_send_error(s, m, "Invalid direction specified. Must be read, write or both");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(state)) {
		astman_send_error(s, m, "No state specified");
		return AMI_SUCCESS;
	}

	clearmute = ast_false(state);

	c = ast_channel_get_by_name(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	if (ast_audiohook_set_mute(c, mixmonitor_spy_type, flag, clearmute)) {
		ast_channel_unref(c);
		astman_send_error(s, m, "Cannot set mute flag");
		return AMI_SUCCESS;
	}

	astman_append(s, "Response: Success\r\n");

	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}

	astman_append(s, "\r\n");

	ast_channel_unref(c);

	return AMI_SUCCESS;
}

static int start_mixmonitor_callback(struct ast_channel *chan, const char *filename, const char *options)
{
	char args[PATH_MAX];

	if (ast_strlen_zero(options)) {
		snprintf(args, sizeof(args), "%s", filename);
	} else {
		snprintf(args, sizeof(args), "%s,%s", filename, options);
	}

	return mixmonitor_exec(chan, args);
}

static int stop_mixmonitor_callback(struct ast_channel *chan, const char *mixmonitor_id)
{
	return stop_mixmonitor_full(chan, mixmonitor_id);
}

static int manager_mixmonitor(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m, "ActionID");
	const char *file = astman_get_header(m, "File");
	const char *options = astman_get_header(m, "Options");
	const char *command = astman_get_header(m, "Command");
	char *opts[OPT_ARG_ARRAY_SIZE] = { NULL, };
	struct ast_flags flags = { 0 };
	char *uid_channel_var = NULL;
	const char *mixmonitor_id = NULL;
	int res;
	char args[PATH_MAX];

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	c = ast_channel_get_by_name(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	if (!ast_strlen_zero(options)) {
		ast_app_parse_options(mixmonitor_opts, &flags, opts, ast_strdupa(options));
	}

	snprintf(args, sizeof(args), "%s,%s,%s", file, options, command);

	res = mixmonitor_exec(c, args);

	if (ast_test_flag(&flags, MUXFLAG_UID)) {
		uid_channel_var = opts[OPT_ARG_UID];
		ast_channel_lock(c);
		mixmonitor_id = pbx_builtin_getvar_helper(c, uid_channel_var);
		mixmonitor_id = ast_strdupa(S_OR(mixmonitor_id, ""));
		ast_channel_unlock(c);
	}

	if (res) {
		ast_channel_unref(c);
		astman_send_error(s, m, "Could not start monitoring channel");
		return AMI_SUCCESS;
	}

	astman_append(s, "Response: Success\r\n");

	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}

	if (!ast_strlen_zero(mixmonitor_id)) {
		astman_append(s, "MixMonitorID: %s\r\n", mixmonitor_id);
	}

	astman_append(s, "\r\n");

	ast_channel_unref(c);

	return AMI_SUCCESS;
}

static int manager_stop_mixmonitor(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m, "ActionID");
	const char *mixmonitor_id = astman_get_header(m, "MixMonitorID");
	int res;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	c = ast_channel_get_by_name(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	res = stop_mixmonitor_full(c, mixmonitor_id);
	if (res) {
		ast_channel_unref(c);
		astman_send_error(s, m, "Could not stop monitoring channel");
		return AMI_SUCCESS;
	}

	astman_append(s, "Response: Success\r\n");

	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}

	astman_append(s, "\r\n");

	ast_channel_unref(c);

	return AMI_SUCCESS;
}

static int func_mixmonitor_read(struct ast_channel *chan, const char *cmd, char *data,
		char *buf, size_t len)
{
	struct ast_datastore *datastore;
	struct mixmonitor_ds *ds_data;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(id);
		AST_APP_ARG(key);
	);

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.id) || ast_strlen_zero(args.key)) {
		ast_log(LOG_WARNING, "Not enough arguments provided to %s. "
				"An ID and key must be provided\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &mixmonitor_ds_info, args.id);
	ast_channel_unlock(chan);

	if (!datastore) {
		ast_log(LOG_WARNING, "Could not find MixMonitor with ID %s\n", args.id);
		return -1;
	}

	ds_data = datastore->data;

	if (!strcasecmp(args.key, "filename")) {
		ast_copy_string(buf, ds_data->filename, len);
	} else {
		ast_log(LOG_WARNING, "Unrecognized %s option %s\n", cmd, args.key);
		return -1;
	}
	return 0;
}

static struct ast_custom_function mixmonitor_function = {
	.name = "MIXMONITOR",
	.read = func_mixmonitor_read,
};

static struct ast_cli_entry cli_mixmonitor[] = {
	AST_CLI_DEFINE(handle_cli_mixmonitor, "Execute a MixMonitor command")
};

static int set_mixmonitor_methods(void)
{
	struct ast_mixmonitor_methods mixmonitor_methods = {
		.start = start_mixmonitor_callback,
		.stop = stop_mixmonitor_callback,
	};

	return ast_set_mixmonitor_methods(&mixmonitor_methods);
}

static int clear_mixmonitor_methods(void)
{
	return ast_clear_mixmonitor_methods();
}

static int unload_module(void)
{
	int res;

	ast_cli_unregister_multiple(cli_mixmonitor, ARRAY_LEN(cli_mixmonitor));
	res = ast_unregister_application(stop_app);
	res |= ast_unregister_application(app);
	res |= ast_manager_unregister("MixMonitorMute");
	res |= ast_manager_unregister("MixMonitor");
	res |= ast_manager_unregister("StopMixMonitor");
	res |= ast_custom_function_unregister(&mixmonitor_function);
	res |= clear_mixmonitor_methods();

	return res;
}

static int load_module(void)
{
	int res;

	ast_cli_register_multiple(cli_mixmonitor, ARRAY_LEN(cli_mixmonitor));
	res = ast_register_application_xml(app, mixmonitor_exec);
	res |= ast_register_application_xml(stop_app, stop_mixmonitor_exec);
	res |= ast_manager_register_xml("MixMonitorMute", EVENT_FLAG_SYSTEM | EVENT_FLAG_CALL, manager_mute_mixmonitor);
	res |= ast_manager_register_xml("MixMonitor", EVENT_FLAG_SYSTEM, manager_mixmonitor);
	res |= ast_manager_register_xml("StopMixMonitor", EVENT_FLAG_SYSTEM | EVENT_FLAG_CALL, manager_stop_mixmonitor);
	res |= ast_custom_function_register(&mixmonitor_function);
	res |= set_mixmonitor_methods();

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Mixed Audio Monitoring Application");
