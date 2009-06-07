/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * Portions taken from the file-based music-on-hold work
 * created by Anthony Minessale II in res_musiconhold.c
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
 * \brief External IVR application interface
 *
 * \author Kevin P. Fleming <kpfleming@digium.com>
 *
 * \note Portions taken from the file-based music-on-hold work
 * created by Anthony Minessale II in res_musiconhold.c
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <signal.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/linkedlists.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/tcptls.h"
#include "asterisk/astobj2.h"

/*** DOCUMENTATION
	<application name="ExternalIVR" language="en_US">
		<synopsis>
			Interfaces with an external IVR application.
		</synopsis>
		<syntax>
			<parameter name="command|ivr://host" required="true" hasparams="true">
				<argument name="arg1" />
				<argument name="arg2" multiple="yes" />
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="n">
						<para>Tells ExternalIVR() not to answer the channel.</para>
					</option>
					<option name="i">
						<para>Tells ExternalIVR() not to send a hangup and exit when the
						channel receives a hangup, instead it sends an <literal>I</literal>
						informative message meaning that the external application MUST hang
						up the call with an <literal>H</literal> command.</para>
					</option>
					<option name="d">
						<para>Tells ExternalIVR() to run on a channel that has been hung up
						and will not look for hangups. The external application must exit with
						an <literal>E</literal> command.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Either forks a process to run given command or makes a socket to connect
			to given host and starts a generator on the channel. The generator's play list
			is controlled by the external application, which can add and clear entries via
			simple commands issued over its stdout. The external application will receive
			all DTMF events received on the channel, and notification if the channel is
			hung up. The received on the channel, and notification if the channel is hung
			up. The application will not be forcibly terminated when the channel is hung up.
			See <filename>doc/externalivr.txt</filename> for a protocol specification.</para>
		</description>
	</application>
 ***/

static const char app[] = "ExternalIVR";

/* XXX the parser in gcc 2.95 gets confused if you don't put a space between 'name' and the comma */
#define ast_chan_log(level, channel, format, ...) ast_log(level, "%s: " format, channel->name , ## __VA_ARGS__)

enum {
	noanswer = (1 << 0),
	ignore_hangup = (1 << 1),
	run_dead = (1 << 2),
} options_flags;

AST_APP_OPTIONS(app_opts, {
	AST_APP_OPTION('n', noanswer),
	AST_APP_OPTION('i', ignore_hangup),
	AST_APP_OPTION('d', run_dead),
});

struct playlist_entry {
	AST_LIST_ENTRY(playlist_entry) list;
	char filename[1];
};

struct ivr_localuser {
	struct ast_channel *chan;
	AST_LIST_HEAD(playlist, playlist_entry) playlist;
	AST_LIST_HEAD(finishlist, playlist_entry) finishlist;
	int abort_current_sound;
	int playing_silence;
	int option_autoclear;
	int gen_active;
};


struct gen_state {
	struct ivr_localuser *u;
	struct ast_filestream *stream;
	struct playlist_entry *current;
	int sample_queue;
};

static int eivr_comm(struct ast_channel *chan, struct ivr_localuser *u, 
	int eivr_events_fd, int eivr_commands_fd, int eivr_errors_fd, 
	const struct ast_str *args, const struct ast_flags flags);

int eivr_connect_socket(struct ast_channel *chan, const char *host, int port);

static void send_eivr_event(FILE *handle, const char event, const char *data,
	const struct ast_channel *chan)
{
	struct ast_str *tmp = ast_str_create(12);

	ast_str_append(&tmp, 0, "%c,%10d", event, (int)time(NULL));
	if (data) {
		ast_str_append(&tmp, 0, ",%s", data);
	}

	fprintf(handle, "%s\n", ast_str_buffer(tmp));
	ast_debug(1, "sent '%s'\n", ast_str_buffer(tmp));
}

static void *gen_alloc(struct ast_channel *chan, void *params)
{
	struct ivr_localuser *u = params;
	struct gen_state *state;

	if (!(state = ast_calloc(1, sizeof(*state))))
		return NULL;

	state->u = u;

	return state;
}

static void gen_closestream(struct gen_state *state)
{
	if (!state->stream)
		return;

	ast_closestream(state->stream);
	state->u->chan->stream = NULL;
	state->stream = NULL;
}

static void gen_release(struct ast_channel *chan, void *data)
{
	struct gen_state *state = data;

	gen_closestream(state);
	ast_free(data);
}

/* caller has the playlist locked */
static int gen_nextfile(struct gen_state *state)
{
	struct ivr_localuser *u = state->u;
	char *file_to_stream;

	u->abort_current_sound = 0;
	u->playing_silence = 0;
	gen_closestream(state);

	while (!state->stream) {
		state->current = AST_LIST_REMOVE_HEAD(&u->playlist, list);
		if (state->current) {
			file_to_stream = state->current->filename;
		} else {
			file_to_stream = "silence/10";
			u->playing_silence = 1;
		}

		if (!(state->stream = ast_openstream_full(u->chan, file_to_stream, u->chan->language, 1))) {
			ast_chan_log(LOG_WARNING, u->chan, "File '%s' could not be opened: %s\n", file_to_stream, strerror(errno));
			if (!u->playing_silence) {
				continue;
			} else {
				break;
			}
		}
	}

	return (!state->stream);
}

static struct ast_frame *gen_readframe(struct gen_state *state)
{
	struct ast_frame *f = NULL;
	struct ivr_localuser *u = state->u;

	if (u->abort_current_sound ||
		(u->playing_silence && AST_LIST_FIRST(&u->playlist))) {
		gen_closestream(state);
		AST_LIST_LOCK(&u->playlist);
		gen_nextfile(state);
		AST_LIST_UNLOCK(&u->playlist);
	}

	if (!(state->stream && (f = ast_readframe(state->stream)))) {
		if (state->current) {
			AST_LIST_LOCK(&u->finishlist);
			AST_LIST_INSERT_TAIL(&u->finishlist, state->current, list);
			AST_LIST_UNLOCK(&u->finishlist);
			state->current = NULL;
		}
		if (!gen_nextfile(state))
			f = ast_readframe(state->stream);
	}

	return f;
}

static int gen_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	struct gen_state *state = data;
	struct ast_frame *f = NULL;
	int res = 0;

	state->sample_queue += samples;

	while (state->sample_queue > 0) {
		if (!(f = gen_readframe(state)))
			return -1;

		res = ast_write(chan, f);
		ast_frfree(f);
		if (res < 0) {
			ast_chan_log(LOG_WARNING, chan, "Failed to write frame: %s\n", strerror(errno));
			return -1;
		}
		state->sample_queue -= f->samples;
	}

	return res;
}

static struct ast_generator gen =
{
	alloc: gen_alloc,
	release: gen_release,
	generate: gen_generate,
};

static void ast_eivr_getvariable(struct ast_channel *chan, char *data, char *outbuf, int outbuflen)
{
	/* original input data: "G,var1,var2," */
	/* data passed as "data":  "var1,var2" */

	char *inbuf, *variable;
	const char *value;
	int j;
	struct ast_str *newstring = ast_str_alloca(outbuflen); 

	outbuf[0] = '\0';

	for (j = 1, inbuf = data; ; j++) {
		variable = strsep(&inbuf, ",");
		if (variable == NULL) {
			int outstrlen = strlen(outbuf);
			if (outstrlen && outbuf[outstrlen - 1] == ',') {
				outbuf[outstrlen - 1] = 0;
			}
			break;
		}
		
		ast_channel_lock(chan);
		if (!(value = pbx_builtin_getvar_helper(chan, variable))) {
			value = "";
		}

		ast_str_append(&newstring, 0, "%s=%s,", variable, value);
		ast_channel_unlock(chan);
		ast_copy_string(outbuf, ast_str_buffer(newstring), outbuflen);
	}
}

static void ast_eivr_setvariable(struct ast_channel *chan, char *data)
{
	char *value;

	char *inbuf = ast_strdupa(data), *variable;

	for (variable = strsep(&inbuf, ","); variable; variable = strsep(&inbuf, ",")) {
		ast_debug(1, "Setting up a variable: %s\n", variable);
		/* variable contains "varname=value" */
		value = strchr(variable, '=');
		if (!value) {
			value = "";
		} else {
			*value++ = '\0';
		}
		pbx_builtin_setvar_helper(chan, variable, value);
	}
}

static struct playlist_entry *make_entry(const char *filename)
{
	struct playlist_entry *entry;

	if (!(entry = ast_calloc(1, sizeof(*entry) + strlen(filename) + 10))) /* XXX why 10 ? */
		return NULL;

	strcpy(entry->filename, filename);

	return entry;
}

static int app_exec(struct ast_channel *chan, const char *data)
{
	struct ast_flags flags = { 0, };
	char *opts[0];
	struct playlist_entry *entry;
	int child_stdin[2] = { 0, 0 };
	int child_stdout[2] = { 0, 0 };
	int child_stderr[2] = { 0, 0 };
	int res = -1;
	int pid;

	char hostname[1024];
	char *port_str = NULL;
	int port = 0;
	struct ast_tcptls_session_instance *ser = NULL;

	struct ivr_localuser foo = {
		.playlist = AST_LIST_HEAD_INIT_VALUE,
		.finishlist = AST_LIST_HEAD_INIT_VALUE,
		.gen_active = 0,
	};
	struct ivr_localuser *u = &foo;

	char *buf;
	int j;
	char *s, **app_args, *e; 
	struct ast_str *pipe_delim_args = ast_str_create(100);

	AST_DECLARE_APP_ARGS(eivr_args,
		AST_APP_ARG(cmd)[32];
	);
	AST_DECLARE_APP_ARGS(application_args,
		AST_APP_ARG(cmd)[32];
	);

	u->abort_current_sound = 0;
	u->chan = chan;

	buf = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(eivr_args, buf);

	if ((s = strchr(eivr_args.cmd[0], '('))) {
		s[0] = ',';
		if (( e = strrchr(s, ')')) ) {
			*e = '\0';
		} else {
			ast_log(LOG_ERROR, "Parse error, no closing paren?\n");
		}
		AST_STANDARD_APP_ARGS(application_args, eivr_args.cmd[0]);
		app_args = application_args.argv;

		/* Put the application + the arguments in a | delimited list */
		ast_str_reset(pipe_delim_args);
		for (j = 0; application_args.cmd[j] != NULL; j++) {
			ast_str_append(&pipe_delim_args, 0, "%s%s", j == 0 ? "" : ",", application_args.cmd[j]);
		}

		/* Parse the ExternalIVR() arguments */
		if (option_debug)
			ast_debug(1, "Parsing options from: [%s]\n", eivr_args.cmd[1]);
		ast_app_parse_options(app_opts, &flags, opts, eivr_args.cmd[1]);
		if (option_debug) {
			if (ast_test_flag(&flags, noanswer))
				ast_debug(1, "noanswer is set\n");
			if (ast_test_flag(&flags, ignore_hangup))
				ast_debug(1, "ignore_hangup is set\n");
			if (ast_test_flag(&flags, run_dead))
				ast_debug(1, "run_dead is set\n");
		}

	} else {
		app_args = eivr_args.argv;
		for (j = 0; eivr_args.cmd[j] != NULL; j++) {
			ast_str_append(&pipe_delim_args, 0, "%s%s", j == 0 ? "" : "|", eivr_args.cmd[j]);
		}
	}
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ExternalIVR requires a command to execute\n");
		return -1;
	}

	if (!(ast_test_flag(&flags, noanswer))) {
		ast_chan_log(LOG_WARNING, chan, "Answering channel and starting generator\n");
		if (chan->_state != AST_STATE_UP) {
			if (ast_test_flag(&flags, run_dead)) {
				ast_chan_log(LOG_WARNING, chan, "Running ExternalIVR with 'd'ead flag on non-hungup channel isn't supported\n");
				goto exit;
			}
			ast_answer(chan);
		}
		if (ast_activate_generator(chan, &gen, u) < 0) {
			ast_chan_log(LOG_WARNING, chan, "Failed to activate generator\n");
			goto exit;
		} else {
			u->gen_active = 1;
		}
	}

	if (!strncmp(app_args[0], "ivr://", 6)) {
		struct ast_tcptls_session_args ivr_desc = {
			.accept_fd = -1,
			.name = "IVR",
		};
		struct ast_hostent hp;

		/*communicate through socket to server*/
		ast_debug(1, "Parsing hostname:port for socket connect from \"%s\"\n", app_args[0]);
		ast_copy_string(hostname, app_args[0] + 6, sizeof(hostname));
		if ((port_str = strchr(hostname, ':')) != NULL) {
			port_str[0] = 0;
			port_str += 1;
			port = atoi(port_str);
		}
		if (!port) {
			port = 2949;  /* default port, if one is not provided */
		}

		ast_gethostbyname(hostname, &hp);
		ivr_desc.local_address.sin_family = AF_INET;
		ivr_desc.local_address.sin_port = htons(port);
		memcpy(&ivr_desc.local_address.sin_addr.s_addr, hp.hp.h_addr, hp.hp.h_length);
		ser = ast_tcptls_client_start(&ivr_desc);

		if (!ser) {
			goto exit;
		}
		res = eivr_comm(chan, u, ser->fd, ser->fd, -1, pipe_delim_args, flags);

	} else {
	
		if (pipe(child_stdin)) {
			ast_chan_log(LOG_WARNING, chan, "Could not create pipe for child input: %s\n", strerror(errno));
			goto exit;
		}
		if (pipe(child_stdout)) {
			ast_chan_log(LOG_WARNING, chan, "Could not create pipe for child output: %s\n", strerror(errno));
			goto exit;
		}
		if (pipe(child_stderr)) {
			ast_chan_log(LOG_WARNING, chan, "Could not create pipe for child errors: %s\n", strerror(errno));
			goto exit;
		}
	
		pid = ast_safe_fork(0);
		if (pid < 0) {
			ast_log(LOG_WARNING, "Failed to fork(): %s\n", strerror(errno));
			goto exit;
		}
	
		if (!pid) {
			/* child process */
			if (ast_opt_high_priority)
				ast_set_priority(0);
	
			dup2(child_stdin[0], STDIN_FILENO);
			dup2(child_stdout[1], STDOUT_FILENO);
			dup2(child_stderr[1], STDERR_FILENO);
			ast_close_fds_above_n(STDERR_FILENO);
			execv(app_args[0], app_args);
			fprintf(stderr, "Failed to execute '%s': %s\n", app_args[0], strerror(errno));
			_exit(1);
		} else {
			/* parent process */
			close(child_stdin[0]);
			child_stdin[0] = 0;
			close(child_stdout[1]);
			child_stdout[1] = 0;
			close(child_stderr[1]);
			child_stderr[1] = 0;
			res = eivr_comm(chan, u, child_stdin[1], child_stdout[0], child_stderr[0], pipe_delim_args, flags);
		}
	}

	exit:
	if (u->gen_active)
		ast_deactivate_generator(chan);

	if (child_stdin[0])
		close(child_stdin[0]);

	if (child_stdin[1])
		close(child_stdin[1]);

	if (child_stdout[0])
		close(child_stdout[0]);

	if (child_stdout[1])
		close(child_stdout[1]);

	if (child_stderr[0])
		close(child_stderr[0]);

	if (child_stderr[1])
		close(child_stderr[1]);
	if (ser) {
		ao2_ref(ser, -1);
	}
	while ((entry = AST_LIST_REMOVE_HEAD(&u->playlist, list)))
		ast_free(entry);

	return res;
}

static int eivr_comm(struct ast_channel *chan, struct ivr_localuser *u, 
 				int eivr_events_fd, int eivr_commands_fd, int eivr_errors_fd, 
 				const struct ast_str *args, const struct ast_flags flags)
{
	struct playlist_entry *entry;
	struct ast_frame *f;
	int ms;
 	int exception;
 	int ready_fd;
 	int waitfds[2] = { eivr_commands_fd, eivr_errors_fd };
 	struct ast_channel *rchan;
 	char *command;
 	int res = -1;
	int test_available_fd = -1;
	int hangup_info_sent = 0;
  
 	FILE *eivr_commands = NULL;
 	FILE *eivr_errors = NULL;
 	FILE *eivr_events = NULL;

	if (!(eivr_events = fdopen(eivr_events_fd, "w"))) {
		ast_chan_log(LOG_WARNING, chan, "Could not open stream to send events\n");
		goto exit;
	}
	if (!(eivr_commands = fdopen(eivr_commands_fd, "r"))) {
		ast_chan_log(LOG_WARNING, chan, "Could not open stream to receive commands\n");
		goto exit;
	}
	if (eivr_errors_fd > -1) {  /* if opening a socket connection, error stream will not be used */
 		if (!(eivr_errors = fdopen(eivr_errors_fd, "r"))) {
 			ast_chan_log(LOG_WARNING, chan, "Could not open stream to receive errors\n");
 			goto exit;
 		}
	}

	test_available_fd = open("/dev/null", O_RDONLY);
 
 	setvbuf(eivr_events, NULL, _IONBF, 0);
 	setvbuf(eivr_commands, NULL, _IONBF, 0);
 	if (eivr_errors) {
		setvbuf(eivr_errors, NULL, _IONBF, 0);
	}

	res = 0;
 
 	while (1) {
 		if (ast_test_flag(chan, AST_FLAG_ZOMBIE)) {
 			ast_chan_log(LOG_NOTICE, chan, "Is a zombie\n");
 			res = -1;
 			break;
 		}
 		if (!hangup_info_sent && !(ast_test_flag(&flags, run_dead)) && ast_check_hangup(chan)) {
			if (ast_test_flag(&flags, ignore_hangup)) {
				ast_chan_log(LOG_NOTICE, chan, "Got check_hangup, but ignore_hangup set so sending 'I' command\n");
				send_eivr_event(eivr_events, 'I', "HANGUP", chan);
				hangup_info_sent = 1;
			} else {
 				ast_chan_log(LOG_NOTICE, chan, "Got check_hangup\n");
 				send_eivr_event(eivr_events, 'H', NULL, chan);
 				res = -1;
	 			break;
			}
 		}
 
 		ready_fd = 0;
 		ms = 100;
 		errno = 0;
 		exception = 0;
 
 		rchan = ast_waitfor_nandfds(&chan, 1, waitfds, (eivr_errors_fd < 0) ? 1 : 2, &exception, &ready_fd, &ms);
 
 		if (chan->_state == AST_STATE_UP && !AST_LIST_EMPTY(&u->finishlist)) {
 			AST_LIST_LOCK(&u->finishlist);
 			while ((entry = AST_LIST_REMOVE_HEAD(&u->finishlist, list))) {
 				send_eivr_event(eivr_events, 'F', entry->filename, chan);
 				ast_free(entry);
 			}
 			AST_LIST_UNLOCK(&u->finishlist);
 		}
 
 		if (chan->_state == AST_STATE_UP && !(ast_check_hangup(chan)) && rchan) {
 			/* the channel has something */
 			f = ast_read(chan);
 			if (!f) {
 				ast_chan_log(LOG_NOTICE, chan, "Returned no frame\n");
 				send_eivr_event(eivr_events, 'H', NULL, chan);
 				res = -1;
 				break;
 			}
 			if (f->frametype == AST_FRAME_DTMF) {
 				send_eivr_event(eivr_events, f->subclass, NULL, chan);
 				if (u->option_autoclear) {
  					if (!u->abort_current_sound && !u->playing_silence)
 						send_eivr_event(eivr_events, 'T', NULL, chan);
  					AST_LIST_LOCK(&u->playlist);
  					while ((entry = AST_LIST_REMOVE_HEAD(&u->playlist, list))) {
 						send_eivr_event(eivr_events, 'D', entry->filename, chan);
  						ast_free(entry);
  					}
  					if (!u->playing_silence)
  						u->abort_current_sound = 1;
  					AST_LIST_UNLOCK(&u->playlist);
  				}
 			} else if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP)) {
 				ast_chan_log(LOG_NOTICE, chan, "Got AST_CONTROL_HANGUP\n");
 				send_eivr_event(eivr_events, 'H', NULL, chan);
				if (f->data.uint32) {
					chan->hangupcause = f->data.uint32;
				}
 				ast_frfree(f);
 				res = -1;
 				break;
 			}
 			ast_frfree(f);
 		} else if (ready_fd == eivr_commands_fd) {
 			char input[1024];
 
 			if (exception || (dup2(eivr_commands_fd, test_available_fd) == -1) || feof(eivr_commands)) {
 				ast_chan_log(LOG_WARNING, chan, "Child process went away\n");
 				res = -1;
  				break;
  			}
  
 			if (!fgets(input, sizeof(input), eivr_commands))
 				continue;
 
 			command = ast_strip(input);
  
 			if (option_debug)
 				ast_debug(1, "got command '%s'\n", input);
  
 			if (strlen(input) < 4)
 				continue;
  
			if (input[0] == 'P') {
				struct ast_str *tmp = (struct ast_str *) args;
 				send_eivr_event(eivr_events, 'P', ast_str_buffer(tmp), chan);
			} else if ( input[0] == 'T' ) {
				ast_chan_log(LOG_WARNING, chan, "Answering channel if needed and starting generator\n");
				if (chan->_state != AST_STATE_UP) {
					if (ast_test_flag(&flags, run_dead)) {
						ast_chan_log(LOG_WARNING, chan, "Running ExternalIVR with 'd'ead flag on non-hungup channel isn't supported\n");
						send_eivr_event(eivr_events, 'Z', "ANSWER_FAILURE", chan);
						continue;
					}
					ast_answer(chan);
				}
				if (!(u->gen_active)) {
					if (ast_activate_generator(chan, &gen, u) < 0) {
						ast_chan_log(LOG_WARNING, chan, "Failed to activate generator\n");
						send_eivr_event(eivr_events, 'Z', "GENERATOR_FAILURE", chan);
					} else {
						u->gen_active = 1;
					}
				}
 			} else if (input[0] == 'S') {
				if (chan->_state != AST_STATE_UP || ast_check_hangup(chan)) {
					ast_chan_log(LOG_WARNING, chan, "Queue 'S'et called on unanswered channel\n");
					send_eivr_event(eivr_events, 'Z', NULL, chan);
					continue;
				}
 				if (ast_fileexists(&input[2], NULL, u->chan->language) == -1) {
 					ast_chan_log(LOG_WARNING, chan, "Unknown file requested '%s'\n", &input[2]);
 					send_eivr_event(eivr_events, 'Z', NULL, chan);
 					strcpy(&input[2], "exception");
 				}
 				if (!u->abort_current_sound && !u->playing_silence)
 					send_eivr_event(eivr_events, 'T', NULL, chan);
 				AST_LIST_LOCK(&u->playlist);
 				while ((entry = AST_LIST_REMOVE_HEAD(&u->playlist, list))) {
 					send_eivr_event(eivr_events, 'D', entry->filename, chan);
 					ast_free(entry);
 				}
 				if (!u->playing_silence)
 					u->abort_current_sound = 1;
 				entry = make_entry(&input[2]);
 				if (entry)
 					AST_LIST_INSERT_TAIL(&u->playlist, entry, list);
 				AST_LIST_UNLOCK(&u->playlist);
 			} else if (input[0] == 'A') {
				if (chan->_state != AST_STATE_UP || ast_check_hangup(chan)) {
					ast_chan_log(LOG_WARNING, chan, "Queue 'A'ppend called on unanswered channel\n");
					send_eivr_event(eivr_events, 'Z', NULL, chan);
					continue;
				}
 				if (ast_fileexists(&input[2], NULL, u->chan->language) == -1) {
 					ast_chan_log(LOG_WARNING, chan, "Unknown file requested '%s'\n", &input[2]);
 					send_eivr_event(eivr_events, 'Z', NULL, chan);
 					strcpy(&input[2], "exception");
 				}
 				entry = make_entry(&input[2]);
 				if (entry) {
 					AST_LIST_LOCK(&u->playlist);
 					AST_LIST_INSERT_TAIL(&u->playlist, entry, list);
 					AST_LIST_UNLOCK(&u->playlist);
 				}
 			} else if (input[0] == 'G') {
 				/* A get variable message:  "G,variable1,variable2,..." */
 				char response[2048];

 				ast_chan_log(LOG_NOTICE, chan, "Getting a Variable out of the channel: %s\n", &input[2]);
 				ast_eivr_getvariable(chan, &input[2], response, sizeof(response));
 				send_eivr_event(eivr_events, 'G', response, chan);
 			} else if (input[0] == 'V') {
 				/* A set variable message:  "V,variablename=foo" */
 				ast_chan_log(LOG_NOTICE, chan, "Setting a Variable up: %s\n", &input[2]);
 				ast_eivr_setvariable(chan, &input[2]);
 			} else if (input[0] == 'L') {
 				ast_chan_log(LOG_NOTICE, chan, "Log message from EIVR: %s\n", &input[2]);
 			} else if (input[0] == 'X') {
 				ast_chan_log(LOG_NOTICE, chan, "Exiting ExternalIVR: %s\n", &input[2]);
 				/*! \todo add deprecation debug message for X command here */
 				res = 0;
 				break;
			} else if (input[0] == 'E') {
 				ast_chan_log(LOG_NOTICE, chan, "Exiting: %s\n", &input[2]);
 				send_eivr_event(eivr_events, 'E', NULL, chan);
 				res = 0;
 				break;
 			} else if (input[0] == 'H') {
 				ast_chan_log(LOG_NOTICE, chan, "Hanging up: %s\n", &input[2]);
 				send_eivr_event(eivr_events, 'H', NULL, chan);
 				res = -1;
 				break;
 			} else if (input[0] == 'O') {
				if (chan->_state != AST_STATE_UP || ast_check_hangup(chan)) {
					ast_chan_log(LOG_WARNING, chan, "Option called on unanswered channel\n");
					send_eivr_event(eivr_events, 'Z', NULL, chan);
					continue;
				}
 				if (!strcasecmp(&input[2], "autoclear"))
 					u->option_autoclear = 1;
 				else if (!strcasecmp(&input[2], "noautoclear"))
 					u->option_autoclear = 0;
 				else
 					ast_chan_log(LOG_WARNING, chan, "Unknown option requested '%s'\n", &input[2]);
 			}
 		} else if (eivr_errors_fd && ready_fd == eivr_errors_fd) {
 			char input[1024];
  
 			if (exception || feof(eivr_errors)) {
 				ast_chan_log(LOG_WARNING, chan, "Child process went away\n");
 				res = -1;
 				break;
 			}
 			if (fgets(input, sizeof(input), eivr_errors)) {
 				command = ast_strip(input);
 				ast_chan_log(LOG_NOTICE, chan, "stderr: %s\n", command);
 			}
 		} else if ((ready_fd < 0) && ms) { 
 			if (errno == 0 || errno == EINTR)
 				continue;
 
 			ast_chan_log(LOG_WARNING, chan, "Wait failed (%s)\n", strerror(errno));
 			break;
 		}
 	}
  
 
exit:
 
	if (test_available_fd > -1) {
		close(test_available_fd);
	}

	if (eivr_events)
 		fclose(eivr_events);
 
	if (eivr_commands)
		fclose(eivr_commands);

	if (eivr_errors)
		fclose(eivr_errors);
  
  	return res;
 
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, app_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "External IVR Interface Application");
