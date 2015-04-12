/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief PBX channel monitoring
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/
 
#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include <sys/stat.h>
#include <libgen.h>

#include "asterisk/paths.h"	/* use ast_config_AST_MONITOR_DIR */
#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#define AST_API_MODULE
#include "asterisk/monitor.h"
#undef AST_API_MODULE
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/beep.h"

/*** DOCUMENTATION
	<application name="Monitor" language="en_US">
		<synopsis>
			Monitor a channel.
		</synopsis>
		<syntax>
			<parameter name="file_format" argsep=":">
				<argument name="file_format" required="true">
					<para>optional, if not set, defaults to <literal>wav</literal></para>
				</argument>
				<argument name="urlbase" />
			</parameter>
			<parameter name="fname_base">
				<para>if set, changes the filename used to the one specified.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="m">
						<para>when the recording ends mix the two leg files into one and
						delete the two leg files. If the variable <variable>MONITOR_EXEC</variable>
						is set, the application referenced in it will be executed instead of
						soxmix/sox and the raw leg files will NOT be deleted automatically.
						soxmix/sox or <variable>MONITOR_EXEC</variable> is handed 3 arguments,
						the two leg files and a target mixed file name which is the same as
						the leg file names only without the in/out designator.</para>
						<para>If <variable>MONITOR_EXEC_ARGS</variable> is set, the contents
						will be passed on as additional arguments to <variable>MONITOR_EXEC</variable>.
						Both <variable>MONITOR_EXEC</variable> and the Mix flag can be set from the
						administrator interface.</para>
					</option>
					<option name="b">
						<para>Don't begin recording unless a call is bridged to another channel.</para>
					</option>
					<option name="B">
						<para>Play a periodic beep while this call is being recorded.</para>
						<argument name="interval"><para>Interval, in seconds. Default is 15.</para></argument>
					</option>
					<option name="i">
						<para>Skip recording of input stream (disables <literal>m</literal> option).</para>
					</option>
					<option name="o">
						<para>Skip recording of output stream (disables <literal>m</literal> option).</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Used to start monitoring a channel. The channel's input and output
			voice packets are logged to files until the channel hangs up or
			monitoring is stopped by the StopMonitor application.</para>
			<para>By default, files are stored to <filename>/var/spool/asterisk/monitor/</filename>.
			Returns <literal>-1</literal> if monitor files can't be opened or if the channel is
			already monitored, otherwise <literal>0</literal>.</para>
		</description>
		<see-also>
			<ref type="application">StopMonitor</ref>
		</see-also>
	</application>
	<application name="StopMonitor" language="en_US">
		<synopsis>
			Stop monitoring a channel.
		</synopsis>
		<syntax />
		<description>
			<para>Stops monitoring a channel. Has no effect if the channel is not monitored.</para>
		</description>
	</application>
	<application name="ChangeMonitor" language="en_US">
		<synopsis>
			Change monitoring filename of a channel.
		</synopsis>
		<syntax>
			<parameter name="filename_base" required="true">
				<para>The new filename base to use for monitoring this channel.</para>
			</parameter>
		</syntax>
		<description>
			<para>Changes monitoring filename of a channel. Has no effect if the
			channel is not monitored.</para>
		</description>
	</application>
	<application name="PauseMonitor" language="en_US">
		<synopsis>
			Pause monitoring of a channel.
		</synopsis>
		<syntax />
		<description>
			<para>Pauses monitoring of a channel until it is re-enabled by a call to UnpauseMonitor.</para>
		</description>
		<see-also>
			<ref type="application">UnpauseMonitor</ref>
		</see-also>
	</application>
	<application name="UnpauseMonitor" language="en_US">
		<synopsis>
			Unpause monitoring of a channel.
		</synopsis>
		<syntax />
		<description>
			<para>Unpauses monitoring of a channel on which monitoring had
			previously been paused with PauseMonitor.</para>
		</description>
		<see-also>
			<ref type="application">PauseMonitor</ref>
		</see-also>
	</application>
	<manager name="Monitor" language="en_US">
		<synopsis>
			Monitor a channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Used to specify the channel to record.</para>
			</parameter>
			<parameter name="File">
				<para>Is the name of the file created in the monitor spool directory.
				Defaults to the same name as the channel (with slashes replaced with dashes).</para>
			</parameter>
			<parameter name="Format">
				<para>Is the audio recording format. Defaults to <literal>wav</literal>.</para>
			</parameter>
			<parameter name="Mix">
				<para>Boolean parameter as to whether to mix the input and output channels
				together after the recording is finished.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action may be used to record the audio on a
			specified channel.</para>
		</description>
	</manager>
	<manager name="StopMonitor" language="en_US">
		<synopsis>
			Stop monitoring a channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>The name of the channel monitored.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action may be used to end a previously started 'Monitor' action.</para>
		</description>
	</manager>
	<manager name="ChangeMonitor" language="en_US">
		<synopsis>
			Change monitoring filename of a channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Used to specify the channel to record.</para>
			</parameter>
			<parameter name="File" required="true">
				<para>Is the new name of the file created in the
				monitor spool directory.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action may be used to change the file
			started by a previous 'Monitor' action.</para>
		</description>
	</manager>
	<manager name="PauseMonitor" language="en_US">
		<synopsis>
			Pause monitoring of a channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Used to specify the channel to record.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action may be used to temporarily stop the
			recording of a channel.</para>
		</description>
	</manager>
	<manager name="UnpauseMonitor" language="en_US">
		<synopsis>
			Unpause monitoring of a channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Used to specify the channel to record.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action may be used to re-enable recording
			of a channel after calling PauseMonitor.</para>
		</description>
	</manager>

 ***/

AST_MUTEX_DEFINE_STATIC(monitorlock);

#define LOCK_IF_NEEDED(lock, needed) do { \
	if (needed) \
		ast_channel_lock(lock); \
	} while(0)

#define UNLOCK_IF_NEEDED(lock, needed) do { \
	if (needed) \
		ast_channel_unlock(lock); \
	} while (0)

static unsigned long seq = 0;

/*! 
 * \brief Change state of monitored channel 
 * \param chan 
 * \param state monitor state
 * \retval 0 on success.
 * \retval -1 on failure.
*/
static int ast_monitor_set_state(struct ast_channel *chan, int state)
{
	LOCK_IF_NEEDED(chan, 1);
	if (!ast_channel_monitor(chan)) {
		UNLOCK_IF_NEEDED(chan, 1);
		return -1;
	}
	ast_channel_monitor(chan)->state = state;
	UNLOCK_IF_NEEDED(chan, 1);
	return 0;
}

/*! \brief Start monitoring a channel
 * \param chan ast_channel struct to record
 * \param format_spec file format to use for recording
 * \param fname_base filename base to record to
 * \param need_lock whether to lock the channel mutex
 * \param stream_action whether to record the input and/or output streams.  X_REC_IN | X_REC_OUT is most often used
 * Creates the file to record, if no format is specified it assumes WAV
 * It also sets channel variable __MONITORED=yes
 * \retval 0 on success
 * \retval -1 on failure
 */
int AST_OPTIONAL_API_NAME(ast_monitor_start)(struct ast_channel *chan, const char *format_spec,
					     const char *fname_base, int need_lock, int stream_action,
					     const char *beep_id)
{
	int res = 0;
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	LOCK_IF_NEEDED(chan, need_lock);

	if (!(ast_channel_monitor(chan))) {
		struct ast_channel_monitor *monitor;
		char *channel_name, *p;

		/* Create monitoring directory if needed */
		ast_mkdir(ast_config_AST_MONITOR_DIR, 0777);

		if (!(monitor = ast_calloc(1, sizeof(*monitor)))) {
			UNLOCK_IF_NEEDED(chan, need_lock);
			return -1;
		}

		if (!ast_strlen_zero(beep_id)) {
			ast_copy_string(monitor->beep_id, beep_id, sizeof(monitor->beep_id));
		}

		/* Determine file names */
		if (!ast_strlen_zero(fname_base)) {
			int directory = strchr(fname_base, '/') ? 1 : 0;
			const char *absolute = *fname_base == '/' ? "" : ast_config_AST_MONITOR_DIR;
			const char *absolute_suffix = *fname_base == '/' ? "" : "/";

			snprintf(monitor->read_filename, FILENAME_MAX, "%s%s%s-in",
						absolute, absolute_suffix, fname_base);
			snprintf(monitor->write_filename, FILENAME_MAX, "%s%s%s-out",
						absolute, absolute_suffix, fname_base);
			snprintf(monitor->filename_base, FILENAME_MAX, "%s%s%s",
					 	absolute, absolute_suffix, fname_base);

			/* try creating the directory just in case it doesn't exist */
			if (directory) {
				char *name = ast_strdupa(monitor->filename_base);
				ast_mkdir(dirname(name), 0777);
			}
		} else {
			ast_mutex_lock(&monitorlock);
			snprintf(monitor->read_filename, FILENAME_MAX, "%s/audio-in-%lu",
						ast_config_AST_MONITOR_DIR, seq);
			snprintf(monitor->write_filename, FILENAME_MAX, "%s/audio-out-%lu",
						ast_config_AST_MONITOR_DIR, seq);
			seq++;
			ast_mutex_unlock(&monitorlock);

			/* Replace all '/' chars from the channel name with '-' chars. */
			channel_name = ast_strdupa(ast_channel_name(chan));
			for (p = channel_name; (p = strchr(p, '/')); ) {
				*p = '-';
			}

			snprintf(monitor->filename_base, FILENAME_MAX, "%s/%d-%s",
					 ast_config_AST_MONITOR_DIR, (int)time(NULL), channel_name);
			monitor->filename_changed = 1;
		}

		monitor->stop = ast_monitor_stop;

		/* Determine file format */
		if (!ast_strlen_zero(format_spec)) {
			monitor->format = ast_strdup(format_spec);
		} else {
			monitor->format = ast_strdup("wav");
		}
		
		/* open files */
		if (stream_action & X_REC_IN) {
			if (ast_fileexists(monitor->read_filename, NULL, NULL) > 0)
				ast_filedelete(monitor->read_filename, NULL);
			if (!(monitor->read_stream = ast_writefile(monitor->read_filename,
							monitor->format, NULL,
							O_CREAT|O_TRUNC|O_WRONLY, 0, AST_FILE_MODE))) {
				ast_log(LOG_WARNING, "Could not create file %s\n",
							monitor->read_filename);
				ast_free(monitor);
				UNLOCK_IF_NEEDED(chan, need_lock);
				return -1;
			}
		} else
			monitor->read_stream = NULL;

		if (stream_action & X_REC_OUT) {
			if (ast_fileexists(monitor->write_filename, NULL, NULL) > 0) {
				ast_filedelete(monitor->write_filename, NULL);
			}
			if (!(monitor->write_stream = ast_writefile(monitor->write_filename,
							monitor->format, NULL,
							O_CREAT|O_TRUNC|O_WRONLY, 0, AST_FILE_MODE))) {
				ast_log(LOG_WARNING, "Could not create file %s\n",
							monitor->write_filename);
				if (monitor->read_stream) {
					ast_closestream(monitor->read_stream);
				}
				ast_free(monitor);
				UNLOCK_IF_NEEDED(chan, need_lock);
				return -1;
			}
		} else
			monitor->write_stream = NULL;

		ast_channel_insmpl_set(chan, 0);
		ast_channel_outsmpl_set(chan, 0);
		ast_channel_monitor_set(chan, monitor);
		ast_monitor_set_state(chan, AST_MONITOR_RUNNING);
		/* so we know this call has been monitored in case we need to bill for it or something */
		pbx_builtin_setvar_helper(chan, "__MONITORED","true");

		message = ast_channel_blob_create_from_cache(ast_channel_uniqueid(chan),
				ast_channel_monitor_start_type(),
				NULL);
		if (message) {
			stasis_publish(ast_channel_topic(chan), message);
		}
	} else {
		ast_debug(1,"Cannot start monitoring %s, already monitored\n", ast_channel_name(chan));
		res = -1;
	}

	UNLOCK_IF_NEEDED(chan, need_lock);

	return res;
}

/*!
 * \brief Get audio format.
 * \param format recording format.
 * The file format extensions that Asterisk uses are not all the same as that
 * which soxmix expects.  This function ensures that the format used as the
 * extension on the filename is something soxmix will understand.
 */
static const char *get_soxmix_format(const char *format)
{
	const char *res = format;

	if (!strcasecmp(format,"ulaw"))
		res = "ul";
	if (!strcasecmp(format,"alaw"))
		res = "al";
	
	return res;
}

/*! 
 * \brief Stop monitoring channel 
 * \param chan 
 * \param need_lock
 * Stop the recording, close any open streams, mix in/out channels if required
 * \return Always 0
*/
int AST_OPTIONAL_API_NAME(ast_monitor_stop)(struct ast_channel *chan, int need_lock)
{
	int delfiles = 0;
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	LOCK_IF_NEEDED(chan, need_lock);

	if (ast_channel_monitor(chan)) {
		char filename[ FILENAME_MAX ];

		if (ast_channel_monitor(chan)->read_stream) {
			ast_closestream(ast_channel_monitor(chan)->read_stream);
		}
		if (ast_channel_monitor(chan)->write_stream) {
			ast_closestream(ast_channel_monitor(chan)->write_stream);
		}

		if (ast_channel_monitor(chan)->filename_changed && !ast_strlen_zero(ast_channel_monitor(chan)->filename_base)) {
			if (ast_fileexists(ast_channel_monitor(chan)->read_filename,NULL,NULL) > 0) {
				snprintf(filename, FILENAME_MAX, "%s-in", ast_channel_monitor(chan)->filename_base);
				if (ast_fileexists(filename, NULL, NULL) > 0) {
					ast_filedelete(filename, NULL);
				}
				ast_filerename(ast_channel_monitor(chan)->read_filename, filename, ast_channel_monitor(chan)->format);
			} else {
				ast_log(LOG_WARNING, "File %s not found\n", ast_channel_monitor(chan)->read_filename);
			}

			if (ast_fileexists(ast_channel_monitor(chan)->write_filename,NULL,NULL) > 0) {
				snprintf(filename, FILENAME_MAX, "%s-out", ast_channel_monitor(chan)->filename_base);
				if (ast_fileexists(filename, NULL, NULL) > 0) {
					ast_filedelete(filename, NULL);
				}
				ast_filerename(ast_channel_monitor(chan)->write_filename, filename, ast_channel_monitor(chan)->format);
			} else {
				ast_log(LOG_WARNING, "File %s not found\n", ast_channel_monitor(chan)->write_filename);
			}
		}

		if (ast_channel_monitor(chan)->joinfiles && !ast_strlen_zero(ast_channel_monitor(chan)->filename_base)) {
			char tmp[1024];
			char tmp2[1024];
			const char *format = !strcasecmp(ast_channel_monitor(chan)->format,"wav49") ? "WAV" : ast_channel_monitor(chan)->format;
			char *fname_base = ast_channel_monitor(chan)->filename_base;
			const char *execute, *execute_args;
			/* at this point, fname_base really is the full path */

			/* Set the execute application */
			execute = pbx_builtin_getvar_helper(chan, "MONITOR_EXEC");
			if (ast_strlen_zero(execute)) {
#ifdef HAVE_SOXMIX
				execute = "nice -n 19 soxmix";
#else
				execute = "nice -n 19 sox -m";
#endif
				format = get_soxmix_format(format);
				delfiles = 1;
			} 
			execute_args = pbx_builtin_getvar_helper(chan, "MONITOR_EXEC_ARGS");
			if (ast_strlen_zero(execute_args)) {
				execute_args = "";
			}
			
			snprintf(tmp, sizeof(tmp), "%s \"%s-in.%s\" \"%s-out.%s\" \"%s.%s\" %s &",
				execute, fname_base, format, fname_base, format, fname_base, format,execute_args);
			if (delfiles) {
				snprintf(tmp2,sizeof(tmp2), "( %s& rm -f \"%s-\"* ) &",tmp, fname_base); /* remove legs when done mixing */
				ast_copy_string(tmp, tmp2, sizeof(tmp));
			}
			ast_debug(1,"monitor executing %s\n",tmp);
			if (ast_safe_system(tmp) == -1)
				ast_log(LOG_WARNING, "Execute of %s failed.\n",tmp);
		}

		if (!ast_strlen_zero(ast_channel_monitor(chan)->beep_id)) {
			ast_beep_stop(chan, ast_channel_monitor(chan)->beep_id);
		}

		ast_free(ast_channel_monitor(chan)->format);
		ast_free(ast_channel_monitor(chan));
		ast_channel_monitor_set(chan, NULL);

		message = ast_channel_blob_create_from_cache(ast_channel_uniqueid(chan),
				ast_channel_monitor_stop_type(),
				NULL);
		if (message) {
			stasis_publish(ast_channel_topic(chan), message);
		}
		pbx_builtin_setvar_helper(chan, "MONITORED", NULL);
	}
	pbx_builtin_setvar_helper(chan, "AUTO_MONITOR", NULL);

	UNLOCK_IF_NEEDED(chan, need_lock);

	return 0;
}


/*! \brief Pause monitoring of channel */
int AST_OPTIONAL_API_NAME(ast_monitor_pause)(struct ast_channel *chan)
{
	return ast_monitor_set_state(chan, AST_MONITOR_PAUSED);
}

/*! \brief Unpause monitoring of channel */
int AST_OPTIONAL_API_NAME(ast_monitor_unpause)(struct ast_channel *chan)
{
	return ast_monitor_set_state(chan, AST_MONITOR_RUNNING);
}

/*! \brief Wrapper for ast_monitor_pause */
static int pause_monitor_exec(struct ast_channel *chan, const char *data)
{
	return ast_monitor_pause(chan);
}

/*! \brief Wrapper for ast_monitor_unpause */
static int unpause_monitor_exec(struct ast_channel *chan, const char *data)
{
	return ast_monitor_unpause(chan);
}

/*! 
 * \brief Change monitored filename of channel 
 * \param chan
 * \param fname_base new filename
 * \param need_lock
 * \retval 0 on success.
 * \retval -1 on failure.
*/
int AST_OPTIONAL_API_NAME(ast_monitor_change_fname)(struct ast_channel *chan, const char *fname_base, int need_lock)
{
	if (ast_strlen_zero(fname_base)) {
		ast_log(LOG_WARNING, "Cannot change monitor filename of channel %s to null\n", ast_channel_name(chan));
		return -1;
	}

	LOCK_IF_NEEDED(chan, need_lock);

	if (ast_channel_monitor(chan)) {
		int directory = strchr(fname_base, '/') ? 1 : 0;
		const char *absolute = *fname_base == '/' ? "" : ast_config_AST_MONITOR_DIR;
		const char *absolute_suffix = *fname_base == '/' ? "" : "/";
		char tmpstring[sizeof(ast_channel_monitor(chan)->filename_base)] = "";
		int i, fd[2] = { -1, -1 }, doexit = 0;

		/* before continuing, see if we're trying to rename the file to itself... */
		snprintf(tmpstring, sizeof(tmpstring), "%s%s%s", absolute, absolute_suffix, fname_base);

		/* try creating the directory just in case it doesn't exist */
		if (directory) {
			char *name = ast_strdupa(tmpstring);
			ast_mkdir(dirname(name), 0777);
		}

		/*!
		 * \note We cannot just compare filenames, due to symlinks, relative
		 * paths, and other possible filesystem issues.  We could use
		 * realpath(3), but its use is discouraged.  However, if we try to
		 * create the same file from two different paths, the second will
		 * fail, and so we have our notification that the filenames point to
		 * the same path.
		 *
		 * Remember, also, that we're using the basename of the file (i.e.
		 * the file without the format suffix), so it does not already exist
		 * and we aren't interfering with the recording itself.
		 */
		ast_debug(2, "comparing tmpstring %s to filename_base %s\n", tmpstring, ast_channel_monitor(chan)->filename_base);
		
		if ((fd[0] = open(tmpstring, O_CREAT | O_WRONLY, 0644)) < 0 ||
			(fd[1] = open(ast_channel_monitor(chan)->filename_base, O_CREAT | O_EXCL | O_WRONLY, 0644)) < 0) {
			if (fd[0] < 0) {
				ast_log(LOG_ERROR, "Unable to compare filenames: %s\n", strerror(errno));
			} else {
				ast_debug(2, "No need to rename monitor filename to itself\n");
			}
			doexit = 1;
		}

		/* Cleanup temporary files */
		for (i = 0; i < 2; i++) {
			if (fd[i] >= 0) {
				while (close(fd[i]) < 0 && errno == EINTR);
			}
		}
		unlink(tmpstring);
		/* if previous monitor file existed in a subdirectory, the directory will not be removed */
		unlink(ast_channel_monitor(chan)->filename_base);

		if (doexit) {
			UNLOCK_IF_NEEDED(chan, need_lock);
			return 0;
		}

		ast_copy_string(ast_channel_monitor(chan)->filename_base, tmpstring, sizeof(ast_channel_monitor(chan)->filename_base));
		ast_channel_monitor(chan)->filename_changed = 1;
	} else {
		ast_log(LOG_WARNING, "Cannot change monitor filename of channel %s to %s, monitoring not started\n", ast_channel_name(chan), fname_base);
	}

	UNLOCK_IF_NEEDED(chan, need_lock);

	return 0;
}

enum {
	MON_FLAG_BRIDGED =  (1 << 0),
	MON_FLAG_MIX =      (1 << 1),
	MON_FLAG_DROP_IN =  (1 << 2),
	MON_FLAG_DROP_OUT = (1 << 3),
	MON_FLAG_BEEP =     (1 << 4),
};

enum {
	OPT_ARG_BEEP_INTERVAL,
	OPT_ARG_ARRAY_SIZE,	/* Always last element of the enum */
};

AST_APP_OPTIONS(monitor_opts, {
	AST_APP_OPTION('b', MON_FLAG_BRIDGED),
	AST_APP_OPTION('m', MON_FLAG_MIX),
	AST_APP_OPTION('i', MON_FLAG_DROP_IN),
	AST_APP_OPTION('o', MON_FLAG_DROP_OUT),
	AST_APP_OPTION_ARG('B', MON_FLAG_BEEP, OPT_ARG_BEEP_INTERVAL),
});

/*!
 * \brief Start monitor
 * \param chan
 * \param data arguments passed fname|options
 * \retval 0 on success.
 * \retval -1 on failure.
*/
static int start_monitor_exec(struct ast_channel *chan, const char *data)
{
	char *arg;
	char *options;
	char *delay;
	char *urlprefix = NULL;
	char tmp[256];
	int stream_action = X_REC_IN | X_REC_OUT;
	int joinfiles = 0;
	int res = 0;
	char *parse;
	struct ast_flags flags = { 0 };
	char *opts[OPT_ARG_ARRAY_SIZE] = { NULL, };
	char beep_id[64] = "";
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(format);
		AST_APP_ARG(fname_base);
		AST_APP_ARG(options);
	);
	
	/* Parse arguments. */
	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Monitor requires an argument\n");
		return 0;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options)) {
		ast_app_parse_options(monitor_opts, &flags, opts, args.options);

		if (ast_test_flag(&flags, MON_FLAG_MIX)) {
			stream_action |= X_JOIN;
		}
		if (ast_test_flag(&flags, MON_FLAG_DROP_IN)) {
			stream_action &= ~X_REC_IN;
		}
		if (ast_test_flag(&flags, MON_FLAG_DROP_OUT)) {
			stream_action &= ~X_REC_OUT;
		}
		if (ast_test_flag(&flags, MON_FLAG_BEEP)) {
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

	arg = strchr(args.format, ':');
	if (arg) {
		*arg++ = 0;
		urlprefix = arg;
	}

	if (!ast_strlen_zero(urlprefix) && !ast_strlen_zero(args.fname_base)) {
		snprintf(tmp, sizeof(tmp), "%s/%s.%s", urlprefix, args.fname_base,
			((strcmp(args.format, "gsm")) ? "wav" : "gsm"));
		ast_channel_lock(chan);
		ast_cdr_setuserfield(ast_channel_name(chan), tmp);
		ast_channel_unlock(chan);
	}
	if (ast_test_flag(&flags, MON_FLAG_BRIDGED)) {
		/* We must remove the "b" option if listed.  In principle none of
		   the following could give NULL results, but we check just to
		   be pedantic. Reconstructing with checks for 'm' option does not
		   work if we end up adding more options than 'm' in the future. */
		delay = ast_strdupa(data);
		options = strrchr(delay, ',');
		if (options) {
			arg = strchr(options, 'b');
			if (arg) {
				*arg = 'X';
				pbx_builtin_setvar_helper(chan,"AUTO_MONITOR", delay);
			}
		}
		return 0;
	}

	res = ast_monitor_start(chan, args.format, args.fname_base, 1, stream_action, beep_id);
	if (res < 0)
		res = ast_monitor_change_fname(chan, args.fname_base, 1);

	if (stream_action & X_JOIN) {
		if ((stream_action & X_REC_IN) && (stream_action & X_REC_OUT))
			joinfiles = 1;
		else
			ast_log(LOG_WARNING, "Won't mix streams unless both input and output streams are recorded\n");
	}
	ast_monitor_setjoinfiles(chan, joinfiles);

	return res;
}

/*! \brief Wrapper function \see ast_monitor_stop */
static int stop_monitor_exec(struct ast_channel *chan, const char *data)
{
	return ast_monitor_stop(chan, 1);
}

/*! \brief Wrapper function \see ast_monitor_change_fname */
static int change_monitor_exec(struct ast_channel *chan, const char *data)
{
	return ast_monitor_change_fname(chan, data, 1);
}

/*! \brief Start monitoring a channel by manager connection */
static int start_monitor_action(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *fname = astman_get_header(m, "File");
	const char *format = astman_get_header(m, "Format");
	const char *mix = astman_get_header(m, "Mix");
	char *d;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(fname)) {
		/* No filename specified, default to the channel name. */
		ast_channel_lock(c);
		fname = ast_strdupa(ast_channel_name(c));
		ast_channel_unlock(c);

		/* Replace all '/' chars from the channel name with '-' chars. */
		for (d = (char *) fname; (d = strchr(d, '/')); ) {
			*d = '-';
		}
	}

	if (ast_monitor_start(c, format, fname, 1, X_REC_IN | X_REC_OUT, NULL)) {
		if (ast_monitor_change_fname(c, fname, 1)) {
			astman_send_error(s, m, "Could not start monitoring channel");
			c = ast_channel_unref(c);
			return AMI_SUCCESS;
		}
	}

	if (ast_true(mix)) {
		ast_channel_lock(c);
		ast_monitor_setjoinfiles(c, 1);
		ast_channel_unlock(c);
	}

	c = ast_channel_unref(c);

	astman_send_ack(s, m, "Started monitoring channel");

	return AMI_SUCCESS;
}

/*! \brief Stop monitoring a channel by manager connection */
static int stop_monitor_action(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	int res;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	res = ast_monitor_stop(c, 1);

	c = ast_channel_unref(c);

	if (res) {
		astman_send_error(s, m, "Could not stop monitoring channel");
		return AMI_SUCCESS;
	}

	astman_send_ack(s, m, "Stopped monitoring channel");

	return AMI_SUCCESS;
}

/*! \brief Change filename of a monitored channel by manager connection */
static int change_monitor_action(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *fname = astman_get_header(m, "File");

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(fname)) {
		astman_send_error(s, m, "No filename specified");
		return AMI_SUCCESS;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	if (ast_monitor_change_fname(c, fname, 1)) {
		c = ast_channel_unref(c);
		astman_send_error(s, m, "Could not change monitored filename of channel");
		return AMI_SUCCESS;
	}

	c = ast_channel_unref(c);

	astman_send_ack(s, m, "Changed monitor filename");

	return AMI_SUCCESS;
}

void AST_OPTIONAL_API_NAME(ast_monitor_setjoinfiles)(struct ast_channel *chan, int turnon)
{
	if (ast_channel_monitor(chan))
		ast_channel_monitor(chan)->joinfiles = turnon;
}

enum MONITOR_PAUSING_ACTION
{
	MONITOR_ACTION_PAUSE,
	MONITOR_ACTION_UNPAUSE
};

static int do_pause_or_unpause(struct mansession *s, const struct message *m, int action)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	if (action == MONITOR_ACTION_PAUSE) {
		ast_monitor_pause(c);
	} else {
		ast_monitor_unpause(c);
	}

	c = ast_channel_unref(c);

	astman_send_ack(s, m, (action == MONITOR_ACTION_PAUSE ? "Paused monitoring of the channel" : "Unpaused monitoring of the channel"));

	return AMI_SUCCESS;
}

static int pause_monitor_action(struct mansession *s, const struct message *m)
{
	return do_pause_or_unpause(s, m, MONITOR_ACTION_PAUSE);
}

static int unpause_monitor_action(struct mansession *s, const struct message *m)
{
	return do_pause_or_unpause(s, m, MONITOR_ACTION_UNPAUSE);
}

static int load_module(void)
{
	ast_register_application_xml("Monitor", start_monitor_exec);
	ast_register_application_xml("StopMonitor", stop_monitor_exec);
	ast_register_application_xml("ChangeMonitor", change_monitor_exec);
	ast_register_application_xml("PauseMonitor", pause_monitor_exec);
	ast_register_application_xml("UnpauseMonitor", unpause_monitor_exec);
	ast_manager_register_xml("Monitor", EVENT_FLAG_CALL, start_monitor_action);
	ast_manager_register_xml("StopMonitor", EVENT_FLAG_CALL, stop_monitor_action);
	ast_manager_register_xml("ChangeMonitor", EVENT_FLAG_CALL, change_monitor_action);
	ast_manager_register_xml("PauseMonitor", EVENT_FLAG_CALL, pause_monitor_action);
	ast_manager_register_xml("UnpauseMonitor", EVENT_FLAG_CALL, unpause_monitor_action);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_unregister_application("Monitor");
	ast_unregister_application("StopMonitor");
	ast_unregister_application("ChangeMonitor");
	ast_unregister_application("PauseMonitor");
	ast_unregister_application("UnpauseMonitor");
	ast_manager_unregister("Monitor");
	ast_manager_unregister("StopMonitor");
	ast_manager_unregister("ChangeMonitor");
	ast_manager_unregister("PauseMonitor");
	ast_manager_unregister("UnpauseMonitor");

	return 0;
}

/* usecount semantics need to be defined */
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Call Monitoring Resource",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND,
		);
