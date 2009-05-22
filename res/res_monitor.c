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
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>
#include <libgen.h>

#include "asterisk/paths.h"	/* use ast_config_AST_MONITOR_DIR */
#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/manager.h"
#include "asterisk/cli.h"
#define AST_API_MODULE
#include "asterisk/monitor.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include "asterisk/options.h"

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

static char *monitor_synopsis = "Monitor a channel";

static char *monitor_descrip = "  Monitor([file_format[:urlbase],[fname_base],[options]]):\n"
"Used to start monitoring a channel. The channel's input and output\n"
"voice packets are logged to files until the channel hangs up or\n"
"monitoring is stopped by the StopMonitor application.\n"
"  file_format		optional, if not set, defaults to \"wav\"\n"
"  fname_base		if set, changes the filename used to the one specified.\n"
"  options:\n"
"    m   - when the recording ends mix the two leg files into one and\n"
"          delete the two leg files.  If the variable MONITOR_EXEC is set, the\n"
"          application referenced in it will be executed instead of\n"
#ifdef HAVE_SOXMIX
"          soxmix and the raw leg files will NOT be deleted automatically.\n"
"          soxmix or MONITOR_EXEC is handed 3 arguments, the two leg files\n"
#else
"          sox and the raw leg files will NOT be deleted automatically.\n"
"          sox or MONITOR_EXEC is handed 3 arguments, the two leg files\n"
#endif
"          and a target mixed file name which is the same as the leg file names\n"
"          only without the in/out designator.\n"
"          If MONITOR_EXEC_ARGS is set, the contents will be passed on as\n"
"          additional arguments to MONITOR_EXEC\n"
"          Both MONITOR_EXEC and the Mix flag can be set from the\n"
"          administrator interface\n"
"\n"
"    b   - Don't begin recording unless a call is bridged to another channel\n"
"    i   - Skip recording of input stream (disables m option)\n"
"    o   - Skip recording of output stream (disables m option)\n"
"\nBy default, files are stored to /var/spool/asterisk/monitor/.\n"
"\nReturns -1 if monitor files can't be opened or if the channel is already\n"
"monitored, otherwise 0.\n"
;

static char *stopmonitor_synopsis = "Stop monitoring a channel";

static char *stopmonitor_descrip = "  StopMonitor():\n"
	"Stops monitoring a channel. Has no effect if the channel is not monitored\n";

static char *changemonitor_synopsis = "Change monitoring filename of a channel";

static char *changemonitor_descrip = "  ChangeMonitor(filename_base):\n"
	"Changes monitoring filename of a channel. Has no effect if the channel is not monitored.\n"
	"The argument is the new filename base to use for monitoring this channel.\n";

static char *pausemonitor_synopsis = "Pause monitoring of a channel";

static char *pausemonitor_descrip = "  PauseMonitor():\n"
	"Pauses monitoring of a channel until it is re-enabled by a call to UnpauseMonitor.\n";

static char *unpausemonitor_synopsis = "Unpause monitoring of a channel";

static char *unpausemonitor_descrip = "  UnpauseMonitor():\n"
	"Unpauses monitoring of a channel on which monitoring had\n"
	"previously been paused with PauseMonitor.\n";

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
	if (!chan->monitor) {
		UNLOCK_IF_NEEDED(chan, 1);
		return -1;
	}
	chan->monitor->state = state;
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
int ast_monitor_start(	struct ast_channel *chan, const char *format_spec,
		const char *fname_base, int need_lock, int stream_action)
{
	int res = 0;

	LOCK_IF_NEEDED(chan, need_lock);

	if (!(chan->monitor)) {
		struct ast_channel_monitor *monitor;
		char *channel_name, *p;

		/* Create monitoring directory if needed */
		ast_mkdir(ast_config_AST_MONITOR_DIR, 0777);

		if (!(monitor = ast_calloc(1, sizeof(*monitor)))) {
			UNLOCK_IF_NEEDED(chan, need_lock);
			return -1;
		}

		/* Determine file names */
		if (!ast_strlen_zero(fname_base)) {
			int directory = strchr(fname_base, '/') ? 1 : 0;
			const char *absolute = *fname_base == '/' ? "" : "/";
			/* try creating the directory just in case it doesn't exist */
			if (directory) {
				char *name = ast_strdupa(fname_base);
				ast_mkdir(dirname(name), 0777);
			}
			snprintf(monitor->read_filename, FILENAME_MAX, "%s%s%s-in",
						directory ? "" : ast_config_AST_MONITOR_DIR, absolute, fname_base);
			snprintf(monitor->write_filename, FILENAME_MAX, "%s%s%s-out",
						directory ? "" : ast_config_AST_MONITOR_DIR, absolute, fname_base);
			ast_copy_string(monitor->filename_base, fname_base, sizeof(monitor->filename_base));
		} else {
			ast_mutex_lock(&monitorlock);
			snprintf(monitor->read_filename, FILENAME_MAX, "%s/audio-in-%ld",
						ast_config_AST_MONITOR_DIR, seq);
			snprintf(monitor->write_filename, FILENAME_MAX, "%s/audio-out-%ld",
						ast_config_AST_MONITOR_DIR, seq);
			seq++;
			ast_mutex_unlock(&monitorlock);

			channel_name = ast_strdupa(chan->name);
			while ((p = strchr(channel_name, '/'))) {
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
				ast_closestream(monitor->read_stream);
				ast_free(monitor);
				UNLOCK_IF_NEEDED(chan, need_lock);
				return -1;
			}
		} else
			monitor->write_stream = NULL;

		chan->monitor = monitor;
		ast_monitor_set_state(chan, AST_MONITOR_RUNNING);
		/* so we know this call has been monitored in case we need to bill for it or something */
		pbx_builtin_setvar_helper(chan, "__MONITORED","true");

		manager_event(EVENT_FLAG_CALL, "MonitorStart",
        	                "Channel: %s\r\n"
                	        "Uniqueid: %s\r\n",                        
	                        chan->name,
        	                chan->uniqueid                        
                	        );
	} else {
		ast_debug(1,"Cannot start monitoring %s, already monitored\n", chan->name);
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
int ast_monitor_stop(struct ast_channel *chan, int need_lock)
{
	int delfiles = 0;

	LOCK_IF_NEEDED(chan, need_lock);

	if (chan->monitor) {
		char filename[ FILENAME_MAX ];

		if (chan->monitor->read_stream) {
			ast_closestream(chan->monitor->read_stream);
		}
		if (chan->monitor->write_stream) {
			ast_closestream(chan->monitor->write_stream);
		}

		if (chan->monitor->filename_changed && !ast_strlen_zero(chan->monitor->filename_base)) {
			if (ast_fileexists(chan->monitor->read_filename,NULL,NULL) > 0) {
				snprintf(filename, FILENAME_MAX, "%s-in", chan->monitor->filename_base);
				if (ast_fileexists(filename, NULL, NULL) > 0) {
					ast_filedelete(filename, NULL);
				}
				ast_filerename(chan->monitor->read_filename, filename, chan->monitor->format);
			} else {
				ast_log(LOG_WARNING, "File %s not found\n", chan->monitor->read_filename);
			}

			if (ast_fileexists(chan->monitor->write_filename,NULL,NULL) > 0) {
				snprintf(filename, FILENAME_MAX, "%s-out", chan->monitor->filename_base);
				if (ast_fileexists(filename, NULL, NULL) > 0) {
					ast_filedelete(filename, NULL);
				}
				ast_filerename(chan->monitor->write_filename, filename, chan->monitor->format);
			} else {
				ast_log(LOG_WARNING, "File %s not found\n", chan->monitor->write_filename);
			}
		}

		if (chan->monitor->joinfiles && !ast_strlen_zero(chan->monitor->filename_base)) {
			char tmp[1024];
			char tmp2[1024];
			const char *format = !strcasecmp(chan->monitor->format,"wav49") ? "WAV" : chan->monitor->format;
			char *name = chan->monitor->filename_base;
			int directory = strchr(name, '/') ? 1 : 0;
			const char *dir = directory ? "" : ast_config_AST_MONITOR_DIR;
			const char *execute, *execute_args;
			const char *absolute = *name == '/' ? "" : "/";

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
			
			snprintf(tmp, sizeof(tmp), "%s \"%s%s%s-in.%s\" \"%s%s%s-out.%s\" \"%s%s%s.%s\" %s &", execute, dir, absolute, name, format, dir, absolute, name, format, dir, absolute, name, format,execute_args);
			if (delfiles) {
				snprintf(tmp2,sizeof(tmp2), "( %s& rm -f \"%s%s%s-\"* ) &",tmp, dir, absolute, name); /* remove legs when done mixing */
				ast_copy_string(tmp, tmp2, sizeof(tmp));
			}
			ast_debug(1,"monitor executing %s\n",tmp);
			if (ast_safe_system(tmp) == -1)
				ast_log(LOG_WARNING, "Execute of %s failed.\n",tmp);
		}
		
		ast_free(chan->monitor->format);
		ast_free(chan->monitor);
		chan->monitor = NULL;

		manager_event(EVENT_FLAG_CALL, "MonitorStop",
        	                "Channel: %s\r\n"
	                        "Uniqueid: %s\r\n",
	                        chan->name,
	                        chan->uniqueid
	                        );
	}

	UNLOCK_IF_NEEDED(chan, need_lock);

	return 0;
}


/*! \brief Pause monitoring of channel */
int ast_monitor_pause(struct ast_channel *chan)
{
	return ast_monitor_set_state(chan, AST_MONITOR_PAUSED);
}

/*! \brief Unpause monitoring of channel */
int ast_monitor_unpause(struct ast_channel *chan)
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
int ast_monitor_change_fname(struct ast_channel *chan, const char *fname_base, int need_lock)
{
	if (ast_strlen_zero(fname_base)) {
		ast_log(LOG_WARNING, "Cannot change monitor filename of channel %s to null\n", chan->name);
		return -1;
	}

	LOCK_IF_NEEDED(chan, need_lock);

	if (chan->monitor) {
		int directory = strchr(fname_base, '/') ? 1 : 0;
		const char *absolute = *fname_base == '/' ? "" : "/";
		char tmpstring[sizeof(chan->monitor->filename_base)] = "";

		/* before continuing, see if we're trying to rename the file to itself... */
		snprintf(tmpstring, sizeof(tmpstring), "%s%s%s", directory ? "" : ast_config_AST_MONITOR_DIR, absolute, fname_base);
		if (!strcmp(tmpstring, chan->monitor->filename_base)) {
			if (option_debug > 2)
				ast_log(LOG_DEBUG, "No need to rename monitor filename to itself\n");
			UNLOCK_IF_NEEDED(chan, need_lock);
			return 0;
		}

		/* try creating the directory just in case it doesn't exist */
		if (directory) {
			char *name = ast_strdupa(fname_base);
			ast_mkdir(dirname(name), 0777);
		}

		ast_copy_string(chan->monitor->filename_base, tmpstring, sizeof(chan->monitor->filename_base));
		chan->monitor->filename_changed = 1;
	} else {
		ast_log(LOG_WARNING, "Cannot change monitor filename of channel %s to %s, monitoring not started\n", chan->name, fname_base);
	}

	UNLOCK_IF_NEEDED(chan, need_lock);

	return 0;
}

 
/*!
 * \brief Start monitor
 * \param chan
 * \param data arguments passed fname|options
 * \retval 0 on success.
 * \retval -1 on failure.
*/
static int start_monitor_exec(struct ast_channel *chan, const char *data)
{
	char *arg = NULL;
	char *options = NULL;
	char *delay = NULL;
	char *urlprefix = NULL;
	char tmp[256];
	int stream_action = X_REC_IN | X_REC_OUT;
	int joinfiles = 0;
	int waitforbridge = 0;
	int res = 0;
	char *parse;
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
		if (strchr(args.options, 'm'))
			stream_action |= X_JOIN;
		if (strchr(args.options, 'b'))
			waitforbridge = 1;
		if (strchr(args.options, 'i'))
			stream_action &= ~X_REC_IN;
		if (strchr(args.options, 'o'))
			stream_action &= ~X_REC_OUT;
	}

	arg = strchr(args.format, ':');
	if (arg) {
		*arg++ = 0;
		urlprefix = arg;
	}

	if (urlprefix) {
		snprintf(tmp, sizeof(tmp), "%s/%s.%s", urlprefix, args.fname_base,
			((strcmp(args.format, "gsm")) ? "wav" : "gsm"));
		if (!chan->cdr && !(chan->cdr = ast_cdr_alloc()))
			return -1;
		ast_cdr_setuserfield(chan, tmp);
	}
	if (waitforbridge) {
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

	res = ast_monitor_start(chan, args.format, args.fname_base, 1, stream_action);
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

static const char start_monitor_action_help[] =
"Description: The 'Monitor' action may be used to record the audio on a\n"
"  specified channel.  The following parameters may be used to control\n"
"  this:\n"
"  Channel     - Required.  Used to specify the channel to record.\n"
"  File        - Optional.  Is the name of the file created in the\n"
"                monitor spool directory.  Defaults to the same name\n"
"                as the channel (with slashes replaced with dashes).\n"
"  Format      - Optional.  Is the audio recording format.  Defaults\n"
"                to \"wav\".\n"
"  Mix         - Optional.  Boolean parameter as to whether to mix\n"
"                the input and output channels together after the\n"
"                recording is finished.\n";

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
		return 0;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	if (ast_strlen_zero(fname)) {
		/* No filename base specified, default to channel name as per CLI */
		ast_channel_lock(c);
		fname = ast_strdupa(c->name);
		ast_channel_unlock(c);
		/* Channels have the format technology/channel_name - have to replace that /  */
		if ((d = strchr(fname, '/'))) {
			*d = '-';
		}
	}

	if (ast_monitor_start(c, format, fname, 1, X_REC_IN | X_REC_OUT)) {
		if (ast_monitor_change_fname(c, fname, 1)) {
			astman_send_error(s, m, "Could not start monitoring channel");
			c = ast_channel_unref(c);
			return 0;
		}
	}

	if (ast_true(mix)) {
		ast_channel_lock(c);
		ast_monitor_setjoinfiles(c, 1);
		ast_channel_unlock(c);
	}

	c = ast_channel_unref(c);

	astman_send_ack(s, m, "Started monitoring channel");

	return 0;
}

static const char stop_monitor_action_help[] =
"Description: The 'StopMonitor' action may be used to end a previously\n"
"  started 'Monitor' action.  The only parameter is 'Channel', the name\n"
"  of the channel monitored.\n";

/*! \brief Stop monitoring a channel by manager connection */
static int stop_monitor_action(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	int res;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	res = ast_monitor_stop(c, 1);

	c = ast_channel_unref(c);

	if (res) {
		astman_send_error(s, m, "Could not stop monitoring channel");
		return 0;
	}

	astman_send_ack(s, m, "Stopped monitoring channel");

	return 0;
}

static const char change_monitor_action_help[] =
"Description: The 'ChangeMonitor' action may be used to change the file\n"
"  started by a previous 'Monitor' action.  The following parameters may\n"
"  be used to control this:\n"
"  Channel     - Required.  Used to specify the channel to record.\n"
"  File        - Required.  Is the new name of the file created in the\n"
"                monitor spool directory.\n";

/*! \brief Change filename of a monitored channel by manager connection */
static int change_monitor_action(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *fname = astman_get_header(m, "File");

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (ast_strlen_zero(fname)) {
		astman_send_error(s, m, "No filename specified");
		return 0;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	if (ast_monitor_change_fname(c, fname, 1)) {
		c = ast_channel_unref(c);
		astman_send_error(s, m, "Could not change monitored filename of channel");
		return 0;
	}

	c = ast_channel_unref(c);

	astman_send_ack(s, m, "Changed monitor filename");

	return 0;
}

void ast_monitor_setjoinfiles(struct ast_channel *chan, int turnon)
{
	if (chan->monitor)
		chan->monitor->joinfiles = turnon;
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
		return -1;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return -1;
	}

	if (action == MONITOR_ACTION_PAUSE) {
		ast_monitor_pause(c);
	} else {
		ast_monitor_unpause(c);
	}

	c = ast_channel_unref(c);

	astman_send_ack(s, m, (action == MONITOR_ACTION_PAUSE ? "Paused monitoring of the channel" : "Unpaused monitoring of the channel"));

	return 0;
}

static const char pause_monitor_action_help[] =
	"Description: The 'PauseMonitor' action may be used to temporarily stop the\n"
	" recording of a channel.  The following parameters may\n"
	" be used to control this:\n"
	"  Channel     - Required.  Used to specify the channel to record.\n";

static int pause_monitor_action(struct mansession *s, const struct message *m)
{
	return do_pause_or_unpause(s, m, MONITOR_ACTION_PAUSE);
}

static const char unpause_monitor_action_help[] =
	"Description: The 'UnpauseMonitor' action may be used to re-enable recording\n"
	"  of a channel after calling PauseMonitor.  The following parameters may\n"
	"  be used to control this:\n"
	"  Channel     - Required.  Used to specify the channel to record.\n";

static int unpause_monitor_action(struct mansession *s, const struct message *m)
{
	return do_pause_or_unpause(s, m, MONITOR_ACTION_UNPAUSE);
}
	

static int load_module(void)
{
	ast_register_application("Monitor", start_monitor_exec, monitor_synopsis, monitor_descrip);
	ast_register_application("StopMonitor", stop_monitor_exec, stopmonitor_synopsis, stopmonitor_descrip);
	ast_register_application("ChangeMonitor", change_monitor_exec, changemonitor_synopsis, changemonitor_descrip);
	ast_register_application("PauseMonitor", pause_monitor_exec, pausemonitor_synopsis, pausemonitor_descrip);
	ast_register_application("UnpauseMonitor", unpause_monitor_exec, unpausemonitor_synopsis, unpausemonitor_descrip);
	ast_manager_register2("Monitor", EVENT_FLAG_CALL, start_monitor_action, monitor_synopsis, start_monitor_action_help);
	ast_manager_register2("StopMonitor", EVENT_FLAG_CALL, stop_monitor_action, stopmonitor_synopsis, stop_monitor_action_help);
	ast_manager_register2("ChangeMonitor", EVENT_FLAG_CALL, change_monitor_action, changemonitor_synopsis, change_monitor_action_help);
	ast_manager_register2("PauseMonitor", EVENT_FLAG_CALL, pause_monitor_action, pausemonitor_synopsis, pause_monitor_action_help);
	ast_manager_register2("UnpauseMonitor", EVENT_FLAG_CALL, unpause_monitor_action, unpausemonitor_synopsis, unpause_monitor_action_help);

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
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Call Monitoring Resource",
		.load = load_module,
		.unload = unload_module,
		);
