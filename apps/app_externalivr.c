/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * External IVR application interface
 * 
 * Copyright (C) 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * Portions taken from the file-based music-on-hold work
 * created by Anthony Minessale II in res_musiconhold.c
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/linkedlists.h"

static char *tdesc = "External IVR Interface Application";

static char *app = "ExternalIVR";

static char *synopsis = "Interfaces with an external IVR application";

static char *descrip = 
"  ExternalIVR(command[|arg[|arg...]]): Forks an process to run the supplied command,\n"
"and starts a generator on the channel. The generator's play list is\n"
"controlled by the external application, which can add and clear entries\n"
"via simple commands issued over its stdout. The external application\n"
"will receive all DTMF events received on the channel, and notification\n"
"if the channel is hung up. The application will not be forcibly terminated\n"
"when the channel is hung up.\n"
"See doc/README.externalivr for a protocol specification.\n";

struct playlist_entry {
	AST_LIST_ENTRY(playlist_entry) list;
	char filename[1];
};

struct localuser {
	struct ast_channel *chan;
	struct localuser *next;
	AST_LIST_HEAD(playlist, playlist_entry) playlist;
	int list_cleared;
};

LOCAL_USER_DECL;

struct gen_state {
	struct localuser *u;
	struct ast_filestream *stream;
	int sample_queue;
	int playing_silence;
};

static void *gen_alloc(struct ast_channel *chan, void *params)
{
	struct localuser *u = params;
	struct gen_state *state;

	state = calloc(1, sizeof(*state));

	if (!state)
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
	free(data);
}

/* caller has the playlist locked */
static int gen_nextfile(struct gen_state *state)
{
	struct playlist_entry *entry;
	struct localuser *u = state->u;
	char *file_to_stream;
	
	state->u->list_cleared = 0;
	state->playing_silence = 0;
	gen_closestream(state);

	while (!state->stream) {
		if (AST_LIST_FIRST(&u->playlist))
			entry = AST_LIST_REMOVE_HEAD(&u->playlist, list);
		else
			entry = NULL;

		if (entry) {
			file_to_stream = ast_strdupa(entry->filename);
			free(entry);
		} else {
			file_to_stream = "silence-10";
			state->playing_silence = 1;
		}

		if (!(state->stream = ast_openstream_full(u->chan, file_to_stream, u->chan->language, 1))) {
			ast_log(LOG_WARNING, "File '%s' could not be opened for channel '%s': %s\n", file_to_stream, u->chan->name, strerror(errno));
			if (!state->playing_silence)
				continue;
			else
				break;
		}
	}

	return (!state->stream);
}

static struct ast_frame *gen_readframe(struct gen_state *state)
{
	struct ast_frame *f = NULL;
	
	if (state->u->list_cleared ||
	    (state->playing_silence && AST_LIST_FIRST(&state->u->playlist))) {
		gen_closestream(state);
		AST_LIST_LOCK(&state->u->playlist);
		gen_nextfile(state);
		AST_LIST_UNLOCK(&state->u->playlist);
	}

	if (!(state->stream && (f = ast_readframe(state->stream)))) {
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
			ast_log(LOG_WARNING, "Failed to write frame to '%s': %s\n", chan->name, strerror(errno));
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

static struct playlist_entry *make_entry(const char *filename)
{
	struct playlist_entry *entry;

	entry = calloc(1, sizeof(*entry) + strlen(filename));

	if (!entry)
		return NULL;

	strcpy(entry->filename, filename);

	return entry;
}

static int app_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u = NULL;
	struct playlist_entry *entry;
	const char *args = data;
	int child_stdin[2] = { 0,0 };
	int child_stdout[2] = { 0,0 };
	int child_stderr[2] = { 0,0 };
	int res = -1;
	int gen_active = 0;
	int pid;
	char *command;
	char *argv[32];
	int argc = 1;
	char *buf;
	FILE *child_commands = NULL;
	FILE *child_errors = NULL;
	FILE *child_events = NULL;

	if (!args || ast_strlen_zero(args)) {
		ast_log(LOG_WARNING, "ExternalIVR requires a command to execute\n");
		goto exit;
	}

	buf = ast_strdupa(data);
	command = strsep(&buf, "|");
	argv[0] = command;
	while ((argc < 31) && (argv[argc++] = strsep(&buf, "|")));
	argv[argc] = NULL;

	LOCAL_USER_ADD(u);

	if (pipe(child_stdin)) {
		ast_log(LOG_WARNING, "Could not create pipe for child input on channel '%s': %s\n", chan->name, strerror(errno));
		goto exit;
	}

	if (pipe(child_stdout)) {
		ast_log(LOG_WARNING, "Could not create pipe for child output on channel '%s': %s\n", chan->name, strerror(errno));
		goto exit;
	}

	if (pipe(child_stderr)) {
		ast_log(LOG_WARNING, "Could not create pipe for child errors on channel '%s': %s\n", chan->name, strerror(errno));
		goto exit;
	}

	u->list_cleared = 0;
	AST_LIST_HEAD_INIT(&u->playlist);

	if (chan->_state != AST_STATE_UP) {
		ast_answer(chan);
	}

	if (ast_activate_generator(chan, &gen, u) < 0) {
		ast_log(LOG_WARNING,"Failed to activate generator on '%s'\n", chan->name);
		goto exit;
	} else
		gen_active = 1;

	pid = fork();
	if (pid < 0) {
		ast_log(LOG_WARNING, "Failed to fork(): %s\n", strerror(errno));
		goto exit;
	}

	if (!pid) {
		/* child process */
		int i;

		dup2(child_stdin[0], STDIN_FILENO);
		dup2(child_stdout[1], STDOUT_FILENO);
		dup2(child_stderr[1], STDERR_FILENO);
		close(child_stdin[1]);
		close(child_stdout[0]);
		close(child_stderr[0]);
		for (i = STDERR_FILENO + 1; i < 1024; i++)
			close(i);
		execv(command, argv);
		fprintf(stderr, "Failed to execute '%s': %s\n", command, strerror(errno));
		exit(1);
	} else {
		/* parent process */
		int child_events_fd = child_stdin[1];
		int child_commands_fd = child_stdout[0];
		int child_errors_fd = child_stderr[0];
		struct ast_frame *f;
		int ms;
		int exception;
		int ready_fd;
		int waitfds[2] = { child_errors_fd, child_commands_fd };
		struct ast_channel *rchan;

		close(child_stdin[0]);
		close(child_stdout[1]);
		close(child_stderr[1]);

		if (!(child_events = fdopen(child_events_fd, "w"))) {
			ast_log(LOG_WARNING, "Could not open stream for child events for channel '%s'\n", chan->name);
			goto exit;
		}

		setvbuf(child_events, NULL, _IONBF, 0);

		if (!(child_commands = fdopen(child_commands_fd, "r"))) {
			ast_log(LOG_WARNING, "Could not open stream for child commands for channel '%s'\n", chan->name);
			goto exit;
		}

		if (!(child_errors = fdopen(child_errors_fd, "r"))) {
			ast_log(LOG_WARNING, "Could not open stream for child errors for channel '%s'\n", chan->name);
			goto exit;
		}

		res = 0;

		while (1) {
			if (ast_test_flag(chan, AST_FLAG_ZOMBIE)) {
				ast_log(LOG_NOTICE, "Channel '%s' is a zombie\n", chan->name);
				res = -1;
				break;
			}

			if (ast_check_hangup(chan)) {
				ast_log(LOG_NOTICE, "Channel '%s' got check_hangup\n", chan->name);
				fprintf(child_events, "H,%10ld\n", time(NULL));
				res = -1;
				break;
			}

			ready_fd = 0;
			ms = 1000;
			errno = 0;
			exception = 0;

			rchan = ast_waitfor_nandfds(&chan, 1, waitfds, 2, &exception, &ready_fd, &ms);

			if (rchan) {
				/* the channel has something */
				f = ast_read(chan);
				if (!f) {
					fprintf(child_events, "H,%10ld\n", time(NULL));
					ast_log(LOG_NOTICE, "Channel '%s' returned no frame\n", chan->name);
					res = -1;
					break;
				}

				if (f->frametype == AST_FRAME_DTMF) {
					fprintf(child_events, "%c,%10ld\n", f->subclass, time(NULL));
				} else if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP)) {
					ast_log(LOG_NOTICE, "Channel '%s' got AST_CONTROL_HANGUP\n", chan->name);
					fprintf(child_events, "H,%10ld\n", time(NULL));
					ast_frfree(f);
					res = -1;
					break;
				}
				ast_frfree(f);
			} else if (ready_fd == child_commands_fd) {
				char input[1024];

				if (exception || feof(child_commands)) {
					ast_log(LOG_WARNING, "Child process went away for channel '%s'\n", chan->name);
					res = -1;
					break;
				}

				if (!fgets(input, sizeof(input), child_commands))
					continue;

				command = ast_strip(input);

				if (strlen(input) < 4)
					continue;

				if (input[0] == 'S') {
					if (ast_fileexists(&input[2], NULL, NULL) == -1) {
						fprintf(child_events, "Z,%10ld\n", time(NULL));
						ast_log(LOG_WARNING, "Unknown file requested '%s' for channel '%s'\n", &input[2], chan->name);
						strcpy(&input[2], "exception");
					}
					AST_LIST_LOCK(&u->playlist);
					while ((entry = AST_LIST_REMOVE_HEAD(&u->playlist, list)))
						free(entry);
					u->list_cleared = 1;
					entry = make_entry(&input[2]);
					if (entry)
						AST_LIST_UNLOCK(&u->playlist);
					AST_LIST_INSERT_TAIL(&u->playlist, entry, list);
				} else if (input[0] == 'A') {
					if (ast_fileexists(&input[2], NULL, NULL) == -1) {
						fprintf(child_events, "Z,%10ld\n", time(NULL));
						ast_log(LOG_WARNING, "Unknown file requested '%s' for channel '%s'\n", &input[2], chan->name);
						strcpy(&input[2], "exception");
					}
					entry = make_entry(&input[2]);
					if (entry) {
						AST_LIST_LOCK(&u->playlist);
						AST_LIST_INSERT_TAIL(&u->playlist, entry, list);
						AST_LIST_UNLOCK(&u->playlist);
					}
				} else if (input[0] == 'H') {
					ast_log(LOG_NOTICE, "Hanging up: %s\n", &input[2]);
					fprintf(child_events, "H,%10ld\n", time(NULL));
					break;
				}
			} else if (ready_fd == child_errors_fd) {
				char input[1024];

				if (exception || feof(child_errors)) {
					ast_log(LOG_WARNING, "Child process went away for channel '%s'\n", chan->name);
					res = -1;
					break;
				}

				if (fgets(input, sizeof(input), child_errors)) {
					command = ast_strip(input);
					ast_log(LOG_NOTICE, "%s\n", command);
				}
			} else if ((ready_fd < 0) && ms) { 
				if (errno == 0 || errno == EINTR)
					continue;

				ast_log(LOG_WARNING, "Wait failed (%s)\n", strerror(errno));
				break;
			}
		}
	}

 exit:
	if (gen_active)
		ast_deactivate_generator(chan);

	if (child_events)
		fclose(child_events);

	if (child_commands)
		fclose(child_commands);

	if (child_errors)
		fclose(child_errors);

	if (child_stdin[0]) {
		close(child_stdin[0]);
		close(child_stdin[1]);
	}

	if (child_stdout[0]) {
		close(child_stdout[0]);
		close(child_stdout[1]);
	}

	if (child_stderr[0]) {
		close(child_stderr[0]);
		close(child_stderr[1]);
	}

	if (u) {
		while ((entry = AST_LIST_REMOVE_HEAD(&u->playlist, list)))
			free(entry);

		LOCAL_USER_REMOVE(u);
	}

	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;

	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, app_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;

	STANDARD_USECOUNT(res);

	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
