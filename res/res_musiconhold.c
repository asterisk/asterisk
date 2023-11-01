/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
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
 * \brief Routines implementing music on hold
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*! \li \ref res_musiconhold.c uses the configuration file \ref musiconhold.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page musiconhold.conf musiconhold.conf
 * \verbinclude musiconhold.conf.sample
 */

/*** MODULEINFO
	<conflict>win32</conflict>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <ctype.h>
#include <signal.h>
#include <sys/time.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef SOLARIS
#include <thread.h>
#endif

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/say.h"
#include "asterisk/musiconhold.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/stringfields.h"
#include "asterisk/linkedlists.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/paths.h"
#include "asterisk/astobj2.h"
#include "asterisk/timing.h"
#include "asterisk/time.h"
#include "asterisk/poll-compat.h"

#define INITIAL_NUM_FILES   8
#define HANDLE_REF	1
#define DONT_UNREF	0

/*** DOCUMENTATION
	<application name="MusicOnHold" language="en_US">
		<synopsis>
			Play Music On Hold indefinitely.
		</synopsis>
		<syntax>
			<parameter name="class" required="true" />
			<parameter name="duration" />
		</syntax>
		<description>
			<para>Plays hold music specified by class. If omitted, the default music
			source for the channel will be used. Change the default class with
			Set(CHANNEL(musicclass)=...). If duration is given, hold music will be played
			specified number of seconds. If duration is omitted, music plays indefinitely.
			Returns <literal>0</literal> when done, <literal>-1</literal> on hangup.</para>
			<para>This application does not automatically answer and should be preceeded by
			an application such as Answer() or Progress().</para>
		</description>
	</application>
	<application name="StartMusicOnHold" language="en_US">
		<synopsis>
			Play Music On Hold.
		</synopsis>
		<syntax>
			<parameter name="class" required="true" />
		</syntax>
		<description>
			<para>Starts playing music on hold, uses default music class for channel.
			Starts playing music specified by class. If omitted, the default music
			source for the channel will be used. Always returns <literal>0</literal>.</para>
		</description>
	</application>
	<application name="StopMusicOnHold" language="en_US">
		<synopsis>
			Stop playing Music On Hold.
		</synopsis>
		<syntax />
		<description>
			<para>Stops playing music on hold.</para>
		</description>
	</application>
 ***/

static const char play_moh[] = "MusicOnHold";
static const char start_moh[] = "StartMusicOnHold";
static const char stop_moh[] = "StopMusicOnHold";

static int respawn_time = 20;

struct moh_files_state {
	/*! Holds a reference to the MOH class. */
	struct mohclass *class;
	struct ast_format *origwfmt;
	struct ast_format *mohwfmt;
	int announcement;
	int samples;
	int sample_queue;
	int pos;
	int save_pos;
	int save_total;
	char name[MAX_MUSICCLASS];
	char save_pos_filename[PATH_MAX];
};

#define MOH_QUIET		(1 << 0)
#define MOH_SINGLE		(1 << 1)
#define MOH_CUSTOM		(1 << 2)
#define MOH_RANDOMIZE		(1 << 3)
#define MOH_SORTALPHA		(1 << 4)
#define MOH_RANDSTART		(MOH_RANDOMIZE | MOH_SORTALPHA) /*!< Sorted but start at random position */
#define MOH_SORTMODE		(3 << 3)

#define MOH_CACHERTCLASSES	(1 << 5)	/*!< Should we use a separate instance of MOH for each user or not */
#define MOH_ANNOUNCEMENT	(1 << 6)	/*!< Do we play announcement files between songs on this channel? */
#define MOH_PREFERCHANNELCLASS	(1 << 7)	/*!< Should queue moh override channel moh */

#define MOH_LOOPLAST (1 << 8) /*!< Whether to loop the last file in the music class when we reach the end, rather than starting over */

/* Custom astobj2 flag */
#define MOH_NOTDELETED          (1 << 30)       /*!< Find only records that aren't deleted? */
#define MOH_REALTIME          (1 << 31)       /*!< Find only records that are realtime */

static struct ast_flags global_flags[1] = {{0}};        /*!< global MOH_ flags */

enum kill_methods {
	KILL_METHOD_PROCESS_GROUP = 0,
	KILL_METHOD_PROCESS
};

struct mohclass {
	char name[MAX_MUSICCLASS];
	char dir[256];
	char args[256];
	char announcement[256];
	char mode[80];
	char digit;
	/*! An immutable vector of filenames in "files" mode */
	struct ast_vector_string *files;
	unsigned int flags;
	/*! The format from the MOH source, not applicable to "files" mode */
	struct ast_format *format;
	/*! The pid of the external application delivering MOH */
	int pid;
	time_t start;
	pthread_t thread;
	/*! Millisecond delay between kill attempts */
	size_t kill_delay;
	/*! Kill method */
	enum kill_methods kill_method;
	/*! Source of audio */
	int srcfd;
	/*! Generic timer */
	struct ast_timer *timer;
	/*! Created on the fly, from RT engine */
	unsigned int realtime:1;
	unsigned int delete:1;
	AST_LIST_HEAD_NOLOCK(, mohdata) members;
	AST_LIST_ENTRY(mohclass) list;
	/*!< Play the moh if the channel answered */
	int answeredonly;
};

struct mohdata {
	int pipe[2];
	struct ast_format *origwfmt;
	struct mohclass *parent;
	struct ast_frame f;
	AST_LIST_ENTRY(mohdata) list;
};

static struct ao2_container *mohclasses;

#define LOCAL_MPG_123 "/usr/local/bin/mpg123"
#define MPG_123 "/usr/bin/mpg123"
#define MAX_MP3S 256

static void moh_parse_options(struct ast_variable *var, struct mohclass *mohclass);
static int reload(void);

#define mohclass_ref(class,string)   (ao2_t_ref((class), +1, (string)), class)

#ifndef AST_DEVMODE
#define mohclass_unref(class,string) ({ ao2_t_ref((class), -1, (string)); (struct mohclass *) NULL; })
#else
#define mohclass_unref(class,string) _mohclass_unref(class, string, __FILE__,__LINE__,__PRETTY_FUNCTION__)
static struct mohclass *_mohclass_unref(struct mohclass *class, const char *tag, const char *file, int line, const char *funcname)
{
	struct mohclass *dup = ao2_callback(mohclasses, OBJ_POINTER, ao2_match_by_addr, class);

	if (dup) {
		if (__ao2_ref(dup, -1, tag, file, line, funcname) == 2) {
			ast_log(LOG_WARNING, "Attempt to unref mohclass %p (%s) when only 1 ref remained, and class is still in a container! (at %s:%d (%s))\n",
				class, class->name, file, line, funcname);
		} else {
			ao2_ref(class, -1);
		}
	} else {
		__ao2_ref(class, -1, tag, file, line, funcname);
	}
	return NULL;
}
#endif

static void moh_post_start(struct ast_channel *chan, const char *moh_class_name)
{
	struct stasis_message *message;
	struct ast_json *json_object;

	ast_verb(3, "Started music on hold, class '%s', on channel '%s'\n",
		moh_class_name, ast_channel_name(chan));

	json_object = ast_json_pack("{s: s}", "class", moh_class_name);
	if (!json_object) {
		return;
	}

	message = ast_channel_blob_create_from_cache(ast_channel_uniqueid(chan),
		ast_channel_moh_start_type(), json_object);
	if (message) {
		/* A channel snapshot must have been in the cache. */
		ast_assert(((struct ast_channel_blob *) stasis_message_data(message))->snapshot != NULL);

		stasis_publish(ast_channel_topic(chan), message);
	}
	ao2_cleanup(message);
	ast_json_unref(json_object);
}

static void moh_post_stop(struct ast_channel *chan)
{
	struct stasis_message *message;

	ast_verb(3, "Stopped music on hold on %s\n", ast_channel_name(chan));

	message = ast_channel_blob_create_from_cache(ast_channel_uniqueid(chan),
		ast_channel_moh_stop_type(), NULL);
	if (message) {
		/* A channel snapshot must have been in the cache. */
		ast_assert(((struct ast_channel_blob *) stasis_message_data(message))->snapshot != NULL);

		stasis_publish(ast_channel_topic(chan), message);
	}
	ao2_cleanup(message);
}

static void moh_files_release(struct ast_channel *chan, void *data)
{
	struct moh_files_state *state;

	if (!chan || !ast_channel_music_state(chan)) {
		return;
	}

	state = ast_channel_music_state(chan);

	if (ast_channel_stream(chan)) {
		ast_closestream(ast_channel_stream(chan));
		ast_channel_stream_set(chan, NULL);
	}

	moh_post_stop(chan);

	ao2_ref(state->mohwfmt, -1);
	state->mohwfmt = NULL; /* make sure to clear this format before restoring the original format */
	if (state->origwfmt && ast_set_write_format(chan, state->origwfmt)) {
		ast_log(LOG_WARNING, "Unable to restore channel '%s' to format '%s'\n", ast_channel_name(chan),
			ast_format_get_name(state->origwfmt));
	}
	ao2_cleanup(state->origwfmt);
	state->origwfmt = NULL;

	state->save_pos = state->pos;
	state->announcement = 0;

	state->class = mohclass_unref(state->class, "Unreffing channel's music class upon deactivation of generator");
}

static int ast_moh_files_next(struct ast_channel *chan)
{
	struct moh_files_state *state = ast_channel_music_state(chan);
	struct ast_vector_string *files;
	int tries;
	size_t file_count;

	/* Discontinue a stream if it is running already */
	if (ast_channel_stream(chan)) {
		ast_closestream(ast_channel_stream(chan));
		ast_channel_stream_set(chan, NULL);
	}

	if (ast_test_flag(state->class, MOH_ANNOUNCEMENT) && state->announcement == 0) {
		state->announcement = 1;
		if (ast_openstream_full(chan, state->class->announcement, ast_channel_language(chan), 1)) {
			ast_debug(1, "%s Opened announcement '%s'\n", ast_channel_name(chan), state->class->announcement);
			return 0;
		}
	} else {
		state->announcement = 0;
	}

	ao2_lock(state->class);
	files = ao2_bump(state->class->files);
	ao2_unlock(state->class);

	file_count = AST_VECTOR_SIZE(files);
	if (!file_count) {
		ast_log(LOG_WARNING, "No files available for class '%s'\n", state->class->name);
		ao2_ref(files, -1);
		return -1;
	}

	if (state->pos == 0 && ast_strlen_zero(state->save_pos_filename)) {
		/* First time so lets play the file. */
		state->save_pos = -1;
	} else if (state->save_pos >= 0 && state->save_pos < file_count && !strcmp(AST_VECTOR_GET(files, state->save_pos), state->save_pos_filename)) {
		/* If a specific file has been saved confirm it still exists and that it is still valid */
		state->pos = state->save_pos;
		state->save_pos = -1;
	} else if (ast_test_flag(state->class, MOH_SORTMODE) == MOH_RANDOMIZE) {
		/* Get a random file and ensure we can open it */
		for (tries = 0; tries < 20; tries++) {
			state->pos = ast_random() % file_count;
			if (ast_fileexists(AST_VECTOR_GET(files, state->pos), NULL, NULL) > 0) {
				break;
			}
		}
		state->save_pos = -1;
		state->samples = 0;
	} else {
		/* This is easy, just increment our position and make sure we don't exceed the total file count */
		state->pos++;
		if (ast_test_flag(state->class, MOH_LOOPLAST)) {
			state->pos = MIN(file_count - 1, state->pos);
		} else {
			state->pos %= file_count;
		}
		state->save_pos = -1;
		state->samples = 0;
	}

	for (tries = 0; tries < file_count; ++tries) {
		if (ast_openstream_full(chan, AST_VECTOR_GET(files, state->pos), ast_channel_language(chan), 1)) {
			break;
		}

		ast_log(LOG_WARNING, "Unable to open file '%s': %s\n", AST_VECTOR_GET(files, state->pos), strerror(errno));
		state->pos++;
		state->pos %= file_count;
	}

	if (tries == file_count) {
		ao2_ref(files, -1);
		return -1;
	}

	/* Record the pointer to the filename for position resuming later */
	ast_copy_string(state->save_pos_filename, AST_VECTOR_GET(files, state->pos), sizeof(state->save_pos_filename));

	ast_debug(1, "%s Opened file %d '%s'\n", ast_channel_name(chan), state->pos, state->save_pos_filename);

	if (state->samples) {
		size_t loc;
		/* seek *SHOULD* be good since it's from a known location */
		ast_seekstream(ast_channel_stream(chan), state->samples, SEEK_SET);
		/* if the seek failed then recover because if there is not a valid read,
		 * moh_files_generate will return -1 and MOH will stop */
		loc = ast_tellstream(ast_channel_stream(chan));
		if (state->samples > loc && loc) {
			/* seek one sample from the end for one guaranteed valid read */
			ast_seekstream(ast_channel_stream(chan), 1, SEEK_END);
		}
	}

	ao2_ref(files, -1);
	return 0;
}

static struct ast_frame *moh_files_readframe(struct ast_channel *chan)
{
	struct ast_frame *f;

	f = ast_readframe(ast_channel_stream(chan));
	if (!f) {
		/* Either there was no file stream setup or we reached EOF. */
		if (!ast_moh_files_next(chan)) {
			/*
			 * Either we resetup the previously saved file stream position
			 * or we started a new file stream.
			 */
			f = ast_readframe(ast_channel_stream(chan));
			if (!f) {
				/*
				 * We can get here if we were very unlucky because the
				 * resetup file stream was saved at EOF when MOH was
				 * previously stopped.
				 */
				if (!ast_moh_files_next(chan)) {
					f = ast_readframe(ast_channel_stream(chan));
				}
			}
		}
	}

	return f;
}

static void moh_files_write_format_change(struct ast_channel *chan, void *data)
{
	struct moh_files_state *state = ast_channel_music_state(chan);

	/* In order to prevent a recursive call to this function as a result
	 * of setting the moh write format back on the channel. Clear
	 * the moh write format before setting the write format on the channel.*/
	if (state->origwfmt) {
		struct ast_format *tmp;

		tmp = ao2_bump(ast_channel_writeformat(chan));
		ao2_replace(state->origwfmt, NULL);
		if (state->mohwfmt) {
			ast_set_write_format(chan, state->mohwfmt);
		}
		state->origwfmt = tmp;
	}
}

static int moh_files_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	struct moh_files_state *state;
	struct ast_frame *f = NULL;
	int res = 0, sample_queue = 0;

	ast_channel_lock(chan);
	state = ast_channel_music_state(chan);
	state->sample_queue += samples;
	/* save the sample queue value for un-locked access */
	sample_queue = state->sample_queue;
	ast_channel_unlock(chan);

	while (sample_queue > 0) {
		ast_channel_lock(chan);
		f = moh_files_readframe(chan);
		if (!f) {
			ast_channel_unlock(chan);
			return -1;
		}

		/* Only track our offset within the current file if we are not in the
		 * the middle of an announcement */
		if (!state->announcement) {
			state->samples += f->samples;
		}

		state->sample_queue -= f->samples;
		if (ast_format_cmp(f->subclass.format, state->mohwfmt) == AST_FORMAT_CMP_NOT_EQUAL) {
			ao2_replace(state->mohwfmt, f->subclass.format);
		}

		/* We need to be sure that we unlock
		 * the channel prior to calling
		 * ast_write, but after our references to state
		 * as it refers to chan->music_state. Update
		 * sample_queue for our loop
		 * Otherwise, the recursive locking that occurs
		 * can cause deadlocks when using indirect
		 * channels, like local channels
		 */
		sample_queue = state->sample_queue;
		ast_channel_unlock(chan);

		res = ast_write(chan, f);
		ast_frfree(f);
		if (res < 0) {
			ast_log(LOG_WARNING, "Failed to write frame to '%s': %s\n", ast_channel_name(chan), strerror(errno));
			return -1;
		}
	}
	return res;
}

static void *moh_files_alloc(struct ast_channel *chan, void *params)
{
	struct moh_files_state *state;
	struct mohclass *class = params;
	size_t file_count;

	state = ast_channel_music_state(chan);
	if (!state && (state = ast_calloc(1, sizeof(*state)))) {
		ast_channel_music_state_set(chan, state);
		ast_module_ref(ast_module_info->self);
	} else {
		if (!state) {
			return NULL;
		}
		if (state->class) {
			mohclass_unref(state->class, "Uh Oh. Restarting MOH with an active class");
			ast_log(LOG_WARNING, "Uh Oh. Restarting MOH with an active class\n");
		}
	}

	ao2_lock(class);
	file_count = AST_VECTOR_SIZE(class->files);
	ao2_unlock(class);

	/* Resume MOH from where we left off last time or start from scratch? */
	if (state->save_total != file_count || strcmp(state->name, class->name) != 0) {
		/* Start MOH from scratch. */
		ao2_cleanup(state->origwfmt);
		ao2_cleanup(state->mohwfmt);
		memset(state, 0, sizeof(*state));
		if (ast_test_flag(class, MOH_RANDOMIZE) && file_count) {
			state->pos = ast_random() % file_count;
		}
	}

	state->class = mohclass_ref(class, "Reffing music class for channel");
	/* it's possible state is not a new allocation, don't leak old refs */
	ao2_replace(state->origwfmt, ast_channel_writeformat(chan));
	ao2_replace(state->mohwfmt, ast_channel_writeformat(chan));
	/* For comparison on restart of MOH (see above) */
	ast_copy_string(state->name, class->name, sizeof(state->name));
	state->save_total = file_count;

	moh_post_start(chan, class->name);

	return state;
}

static int moh_digit_match(void *obj, void *arg, int flags)
{
	char *digit = arg;
	struct mohclass *class = obj;

	return (*digit == class->digit) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \note This function should be called with the mohclasses list locked */
static struct mohclass *get_mohbydigit(char digit)
{
	return ao2_t_callback(mohclasses, 0, moh_digit_match, &digit, "digit callback");
}

static void moh_handle_digit(struct ast_channel *chan, char digit)
{
	struct mohclass *class;
	const char *classname = NULL;

	if ((class = get_mohbydigit(digit))) {
		classname = ast_strdupa(class->name);
		class = mohclass_unref(class, "Unreffing ao2_find from finding by digit");
		ast_channel_musicclass_set(chan, classname);
		ast_moh_stop(chan);
		ast_moh_start(chan, classname, NULL);
	}
}

static struct ast_generator moh_file_stream = {
	.alloc    = moh_files_alloc,
	.release  = moh_files_release,
	.generate = moh_files_generator,
	.digit    = moh_handle_digit,
	.write_format_change = moh_files_write_format_change,
};

static int spawn_mp3(struct mohclass *class)
{
	int fds[2];
	int files = 0;
	char fns[MAX_MP3S][80];
	char *argv[MAX_MP3S + 50];
	char xargs[256];
	char *argptr;
	int argc = 0;
	DIR *dir = NULL;
	struct dirent *de;


	if (!strcasecmp(class->dir, "nodir")) {
		files = 1;
	} else {
		dir = opendir(class->dir);
		if (!dir && strncasecmp(class->dir, "http://", 7)) {
			ast_log(LOG_WARNING, "%s is not a valid directory\n", class->dir);
			return -1;
		}
	}

	if (!ast_test_flag(class, MOH_CUSTOM)) {
		argv[argc++] = "mpg123";
		argv[argc++] = "-q";
		argv[argc++] = "-s";
		argv[argc++] = "--mono";
		argv[argc++] = "-r";
		argv[argc++] = "8000";

		if (!ast_test_flag(class, MOH_SINGLE)) {
			argv[argc++] = "-b";
			argv[argc++] = "2048";
		}

		argv[argc++] = "-f";

		if (ast_test_flag(class, MOH_QUIET))
			argv[argc++] = "4096";
		else
			argv[argc++] = "8192";

		/* Look for extra arguments and add them to the list */
		ast_copy_string(xargs, class->args, sizeof(xargs));
		argptr = xargs;
		while (!ast_strlen_zero(argptr)) {
			argv[argc++] = argptr;
			strsep(&argptr, ",");
		}
	} else  {
		/* Format arguments for argv vector */
		ast_copy_string(xargs, class->args, sizeof(xargs));
		argptr = xargs;
		while (!ast_strlen_zero(argptr)) {
			argv[argc++] = argptr;
			strsep(&argptr, " ");
		}
	}

	if (!strncasecmp(class->dir, "http://", 7)) {
		ast_copy_string(fns[files], class->dir, sizeof(fns[files]));
		argv[argc++] = fns[files];
		files++;
	} else if (dir) {
		while ((de = readdir(dir)) && (files < MAX_MP3S)) {
			if ((strlen(de->d_name) > 3) &&
			    ((ast_test_flag(class, MOH_CUSTOM) &&
			      (!strcasecmp(de->d_name + strlen(de->d_name) - 4, ".raw") ||
			       !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".sln"))) ||
			     !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".mp3"))) {
				ast_copy_string(fns[files], de->d_name, sizeof(fns[files]));
				argv[argc++] = fns[files];
				files++;
			}
		}
	}
	argv[argc] = NULL;
	if (dir) {
		closedir(dir);
	}
	if (pipe(fds)) {
		ast_log(LOG_WARNING, "Pipe failed\n");
		return -1;
	}
	if (!files) {
		ast_log(LOG_WARNING, "Found no files in '%s'\n", class->dir);
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if (!strncasecmp(class->dir, "http://", 7) && time(NULL) - class->start < respawn_time) {
		sleep(respawn_time - (time(NULL) - class->start));
	}

	time(&class->start);
	class->pid = ast_safe_fork(0);
	if (class->pid < 0) {
		close(fds[0]);
		close(fds[1]);
		ast_log(LOG_WARNING, "Fork failed: %s\n", strerror(errno));
		return -1;
	}
	if (!class->pid) {
		if (ast_opt_high_priority)
			ast_set_priority(0);

		close(fds[0]);
		/* Stdout goes to pipe */
		dup2(fds[1], STDOUT_FILENO);

		/* Close everything else */
		ast_close_fds_above_n(STDERR_FILENO);

		/* Child */
		if (strncasecmp(class->dir, "http://", 7) && strcasecmp(class->dir, "nodir") && chdir(class->dir) < 0) {
			ast_log(LOG_WARNING, "chdir() failed: %s\n", strerror(errno));
			_exit(1);
		}
		setpgid(0, getpid());
		if (ast_test_flag(class, MOH_CUSTOM)) {
			execv(argv[0], argv);
		} else {
			/* Default install is /usr/local/bin */
			execv(LOCAL_MPG_123, argv);
			/* Many places have it in /usr/bin */
			execv(MPG_123, argv);
			/* Check PATH as a last-ditch effort */
			execvp("mpg123", argv);
		}
		/* Can't use logger, since log FDs are closed */
		fprintf(stderr, "MOH: exec failed: %s\n", strerror(errno));
		close(fds[1]);
		_exit(1);
	} else {
		/* Parent */
		close(fds[1]);
	}
	return fds[0];
}

static int killer(pid_t pid, int signum, enum kill_methods kill_method)
{
	switch (kill_method) {
	case KILL_METHOD_PROCESS_GROUP:
		return killpg(pid, signum);
	case KILL_METHOD_PROCESS:
		return kill(pid, signum);
	}

	return -1;
}

static void killpid(int pid, size_t delay, enum kill_methods kill_method)
{
	if (killer(pid, SIGHUP, kill_method) < 0) {
		if (errno == ESRCH) {
			return;
		}
		ast_log(LOG_WARNING, "Unable to send a SIGHUP to MOH process '%d'?!!: %s\n", pid, strerror(errno));
	} else {
		ast_debug(1, "Sent HUP to pid %d%s\n", pid,
			kill_method == KILL_METHOD_PROCESS_GROUP ? " and all children" : " only");
	}
	usleep(delay);
	if (killer(pid, SIGTERM, kill_method) < 0) {
		if (errno == ESRCH) {
			return;
		}
		ast_log(LOG_WARNING, "Unable to terminate MOH process '%d'?!!: %s\n", pid, strerror(errno));
	} else {
		ast_debug(1, "Sent TERM to pid %d%s\n", pid,
			kill_method == KILL_METHOD_PROCESS_GROUP ? " and all children" : " only");
	}
	usleep(delay);
	if (killer(pid, SIGKILL, kill_method) < 0) {
		if (errno == ESRCH) {
			return;
		}
		ast_log(LOG_WARNING, "Unable to kill MOH process '%d'?!!: %s\n", pid, strerror(errno));
	} else {
		ast_debug(1, "Sent KILL to pid %d%s\n", pid,
			kill_method == KILL_METHOD_PROCESS_GROUP ? " and all children" : " only");
	}
}

static void *monmp3thread(void *data)
{
#define	MOH_MS_INTERVAL		100

	struct mohclass *class = data;
	struct mohdata *moh;
	short sbuf[8192];
	int res = 0, res2;
	int len;
	struct timeval deadline, tv_tmp;

	deadline.tv_sec = 0;
	deadline.tv_usec = 0;
	for(;/* ever */;) {
		pthread_testcancel();
		/* Spawn mp3 player if it's not there */
		if (class->srcfd < 0) {
			if ((class->srcfd = spawn_mp3(class)) < 0) {
				ast_log(LOG_WARNING, "Unable to spawn mp3player\n");
				/* Try again later */
				sleep(500);
				continue;
			}
		}
		if (class->timer) {
			struct pollfd pfd = { .fd = ast_timer_fd(class->timer), .events = POLLIN | POLLPRI, };

#ifdef SOLARIS
			thr_yield();
#endif
			/* Pause some amount of time */
			if (ast_poll(&pfd, 1, -1) > 0) {
				if (ast_timer_ack(class->timer, 1) < 0) {
					ast_log(LOG_ERROR, "Failed to acknowledge timer for mp3player\n");
					return NULL;
				}
				/* 25 samples per second => 40ms framerate => 320 samples */
				res = 320; /* 320/40 = 8 samples/ms */
			} else {
				ast_log(LOG_WARNING, "poll() failed: %s\n", strerror(errno));
				res = 0;
			}
			pthread_testcancel();
		} else {
			long delta;
			/* Reliable sleep */
			tv_tmp = ast_tvnow();
			if (ast_tvzero(deadline))
				deadline = tv_tmp;
			delta = ast_tvdiff_ms(tv_tmp, deadline);
			if (delta < MOH_MS_INTERVAL) {	/* too early */
				deadline = ast_tvadd(deadline, ast_samp2tv(MOH_MS_INTERVAL, 1000));	/* next deadline */
				usleep(1000 * (MOH_MS_INTERVAL - delta));
				pthread_testcancel();
			} else {
				ast_log(LOG_NOTICE, "Request to schedule in the past?!?!\n");
				deadline = tv_tmp;
			}
			/* 10 samples per second (MOH_MS_INTERVAL) => 100ms framerate => 800 samples */
			res = 8 * MOH_MS_INTERVAL; /* 800/100 = 8 samples/ms */
		}
		/* For non-8000Hz formats, we need to alter the resolution */
		res = res * ast_format_get_sample_rate(class->format) / 8000;

		if ((strncasecmp(class->dir, "http://", 7) && strcasecmp(class->dir, "nodir")) && AST_LIST_EMPTY(&class->members))
			continue;
		/* Read mp3 audio */
		len = ast_format_determine_length(class->format, res);

		if ((res2 = read(class->srcfd, sbuf, len)) != len) {
			if (!res2) {
				close(class->srcfd);
				class->srcfd = -1;
				pthread_testcancel();
				if (class->pid > 1) {
					killpid(class->pid, class->kill_delay, class->kill_method);
					class->pid = 0;
				}
			} else {
				ast_debug(1, "Read %d bytes of audio while expecting %d\n", res2, len);
			}
			continue;
		}

		pthread_testcancel();

		ao2_lock(class);
		AST_LIST_TRAVERSE(&class->members, moh, list) {
			/* Write data */
			if ((res = write(moh->pipe[1], sbuf, res2)) != res2) {
				ast_debug(1, "Only wrote %d of %d bytes to pipe\n", res, res2);
			}
		}
		ao2_unlock(class);
	}
	return NULL;
}

static int play_moh_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	char *class;
	int timeout = -1;
	int res;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(class);
		AST_APP_ARG(duration);
	);

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.duration)) {
		if (sscanf(args.duration, "%30d", &timeout) == 1) {
			timeout *= 1000;
		} else {
			ast_log(LOG_WARNING, "Invalid MusicOnHold duration '%s'. Will wait indefinitely.\n", args.duration);
		}
	}

	class = S_OR(args.class, NULL);
	if (ast_moh_start(chan, class, NULL)) {
		ast_log(LOG_WARNING, "Unable to start music on hold class '%s' on channel %s\n", class, ast_channel_name(chan));
		return 0;
	}

	if (timeout > 0)
		res = ast_safe_sleep(chan, timeout);
	else {
		while (!(res = ast_safe_sleep(chan, 10000)));
	}

	ast_moh_stop(chan);

	return res;
}

static int start_moh_exec(struct ast_channel *chan, const char *data)
{
	char *parse;
	char *class;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(class);
	);

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	class = S_OR(args.class, NULL);
	if (ast_moh_start(chan, class, NULL))
		ast_log(LOG_WARNING, "Unable to start music on hold class '%s' on channel %s\n", class, ast_channel_name(chan));

	return 0;
}

static int stop_moh_exec(struct ast_channel *chan, const char *data)
{
	ast_moh_stop(chan);

	return 0;
}

#define get_mohbyname(a,b,c)	_get_mohbyname(a,b,c,__FILE__,__LINE__,__PRETTY_FUNCTION__)

static struct mohclass *_get_mohbyname(const char *name, int warn, int flags, const char *file, int lineno, const char *funcname)
{
	struct mohclass *moh = NULL;
	struct mohclass tmp_class = {
		.flags = 0,
	};

	ast_copy_string(tmp_class.name, name, sizeof(tmp_class.name));

	moh = __ao2_find(mohclasses, &tmp_class, flags,
		"get_mohbyname", file, lineno, funcname);

	if (!moh && warn) {
		ast_log(LOG_WARNING, "Music on Hold class '%s' not found in memory. Verify your configuration.\n", name);
	}

	return moh;
}

static struct mohdata *mohalloc(struct mohclass *cl)
{
	struct mohdata *moh;

	if (!(moh = ast_calloc(1, sizeof(*moh))))
		return NULL;

	if (ast_pipe_nonblock(moh->pipe)) {
		ast_log(LOG_WARNING, "Failed to create pipe: %s\n", strerror(errno));
		ast_free(moh);
		return NULL;
	}

	moh->f.frametype = AST_FRAME_VOICE;
	moh->f.subclass.format = cl->format;
	moh->f.offset = AST_FRIENDLY_OFFSET;

	moh->parent = mohclass_ref(cl, "Reffing music class for mohdata parent");

	ao2_lock(cl);
	AST_LIST_INSERT_HEAD(&cl->members, moh, list);
	ao2_unlock(cl);

	return moh;
}

static void moh_release(struct ast_channel *chan, void *data)
{
	struct mohdata *moh = data;
	struct mohclass *class = moh->parent;
	struct ast_format *oldwfmt;

	ao2_lock(class);
	AST_LIST_REMOVE(&moh->parent->members, moh, list);
	ao2_unlock(class);

	close(moh->pipe[0]);
	close(moh->pipe[1]);

	oldwfmt = moh->origwfmt;

	moh->parent = class = mohclass_unref(class, "unreffing moh->parent upon deactivation of generator");

	ast_free(moh);

	if (chan) {
		struct moh_files_state *state;

		state = ast_channel_music_state(chan);
		if (state && state->class) {
			state->class = mohclass_unref(state->class, "Unreffing channel's music class upon deactivation of generator");
		}
		if (oldwfmt && ast_set_write_format(chan, oldwfmt)) {
			ast_log(LOG_WARNING, "Unable to restore channel '%s' to format %s\n",
					ast_channel_name(chan), ast_format_get_name(oldwfmt));
		}

		moh_post_stop(chan);
	}

	ao2_cleanup(oldwfmt);
}

static void *moh_alloc(struct ast_channel *chan, void *params)
{
	struct mohdata *res;
	struct mohclass *class = params;
	struct moh_files_state *state;

	/* Initiating music_state for current channel. Channel should know name of moh class */
	state = ast_channel_music_state(chan);
	if (!state && (state = ast_calloc(1, sizeof(*state)))) {
		ast_channel_music_state_set(chan, state);
		ast_module_ref(ast_module_info->self);
	} else {
		if (!state) {
			return NULL;
		}
		if (state->class) {
			mohclass_unref(state->class, "Uh Oh. Restarting MOH with an active class");
			ast_log(LOG_WARNING, "Uh Oh. Restarting MOH with an active class\n");
		}
		ao2_cleanup(state->origwfmt);
		ao2_cleanup(state->mohwfmt);
		memset(state, 0, sizeof(*state));
	}

	if ((res = mohalloc(class))) {
		res->origwfmt = ao2_bump(ast_channel_writeformat(chan));
		if (ast_set_write_format(chan, class->format)) {
			ast_log(LOG_WARNING, "Unable to set channel '%s' to format '%s'\n", ast_channel_name(chan),
				ast_format_get_name(class->format));
			moh_release(NULL, res);
			res = NULL;
		} else {
			state->class = mohclass_ref(class, "Placing reference into state container");
			moh_post_start(chan, class->name);
		}
	}
	return res;
}

static int moh_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	struct mohdata *moh = data;
	short buf[1280 + AST_FRIENDLY_OFFSET / 2];
	int res;

	len = ast_format_determine_length(moh->parent->format, samples);

	if (len > sizeof(buf) - AST_FRIENDLY_OFFSET) {
		ast_log(LOG_WARNING, "Only doing %d of %d requested bytes on %s\n", (int)sizeof(buf), len, ast_channel_name(chan));
		len = sizeof(buf) - AST_FRIENDLY_OFFSET;
	}
	res = read(moh->pipe[0], buf + AST_FRIENDLY_OFFSET/2, len);
	if (res <= 0)
		return 0;

	moh->f.datalen = res;
	moh->f.data.ptr = buf + AST_FRIENDLY_OFFSET / 2;
	moh->f.samples = ast_codec_samples_count(&moh->f);

	if (ast_write(chan, &moh->f) < 0) {
		ast_log(LOG_WARNING, "Failed to write frame to '%s': %s\n", ast_channel_name(chan), strerror(errno));
		return -1;
	}

	return 0;
}

static struct ast_generator mohgen = {
	.alloc    = moh_alloc,
	.release  = moh_release,
	.generate = moh_generate,
	.digit    = moh_handle_digit,
};

static void moh_file_vector_destructor(void *obj)
{
	struct ast_vector_string *files = obj;
	AST_VECTOR_RESET(files, ast_free);
	AST_VECTOR_FREE(files);
}

static struct ast_vector_string *moh_file_vector_alloc(int initial_capacity)
{
	struct ast_vector_string *files = ao2_alloc_options(
		sizeof(struct ast_vector_string),
		moh_file_vector_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (files) {
		AST_VECTOR_INIT(files, initial_capacity);
	}
	return files;
}

static void moh_parse_options(struct ast_variable *var, struct mohclass *mohclass)
{
	struct ast_vector_string *playlist_entries = NULL;

	for (; var; var = var->next) {
		if (!strcasecmp(var->name, "name")) {
			ast_copy_string(mohclass->name, var->value, sizeof(mohclass->name));
		} else if (!strcasecmp(var->name, "mode")) {
			ast_copy_string(mohclass->mode, var->value, sizeof(mohclass->mode));
		} else if (!strcasecmp(var->name, "entry")) {
			if (ast_begins_with(var->value, "/") || strstr(var->value, "://")) {
				char *dup;

				if (!playlist_entries) {
					playlist_entries = moh_file_vector_alloc(16);
					if (!playlist_entries) {
						continue;
					}
				}

				dup = ast_strdup(var->value);
				if (!dup) {
					continue;
				}

				if (ast_begins_with(dup, "/")) {
					char *last_pos_dot = strrchr(dup, '.');
					char *last_pos_slash = strrchr(dup, '/');
					if (last_pos_dot && last_pos_dot > last_pos_slash) {
						ast_log(LOG_WARNING, "The playlist entry '%s' may include an extension, which could prevent it from playing.\n",
							dup);
					}
				}

				AST_VECTOR_APPEND(playlist_entries, dup);
			} else {
				ast_log(LOG_ERROR, "Playlist entries must be a URL or an absolute path, '%s' provided.\n", var->value);
			}
		} else if (!strcasecmp(var->name, "directory")) {
			ast_copy_string(mohclass->dir, var->value, sizeof(mohclass->dir));
		} else if (!strcasecmp(var->name, "application")) {
			ast_copy_string(mohclass->args, var->value, sizeof(mohclass->args));
		} else if (!strcasecmp(var->name, "announcement")) {
			ast_copy_string(mohclass->announcement, var->value, sizeof(mohclass->announcement));
			ast_set_flag(mohclass, MOH_ANNOUNCEMENT);
		} else if (!strcasecmp(var->name, "digit") && (isdigit(*var->value) || strchr("*#", *var->value))) {
			mohclass->digit = *var->value;
		} else if (!strcasecmp(var->name, "random")) {
			static int deprecation_warning = 0;
			if (!deprecation_warning) {
				ast_log(LOG_WARNING, "Music on hold 'random' setting is deprecated in 14.  Please use 'sort=random' instead.\n");
				deprecation_warning = 1;
			}
			ast_set2_flag(mohclass, ast_true(var->value), MOH_RANDOMIZE);
		} else if (!strcasecmp(var->name, "sort")) {
			if (!strcasecmp(var->value, "random")) {
				ast_set_flag(mohclass, MOH_RANDOMIZE);
			} else if (!strcasecmp(var->value, "alpha")) {
				ast_set_flag(mohclass, MOH_SORTALPHA);
			} else if (!strcasecmp(var->value, "randstart")) {
				ast_set_flag(mohclass, MOH_RANDSTART);
			}
		} else if (!strcasecmp(var->name, "loop_last")) {
			if (ast_true(var->value)) {
				ast_set_flag(mohclass, MOH_LOOPLAST);
			} else {
				ast_clear_flag(mohclass, MOH_LOOPLAST);
			}
		} else if (!strcasecmp(var->name, "format") && !ast_strlen_zero(var->value)) {
			ao2_cleanup(mohclass->format);
			mohclass->format = ast_format_cache_get(var->value);
			if (!mohclass->format) {
				ast_log(LOG_WARNING, "Unknown format '%s' -- defaulting to SLIN\n", var->value);
				mohclass->format = ao2_bump(ast_format_slin);
			}
		} else if (!strcasecmp(var->name, "kill_escalation_delay")) {
			if (sscanf(var->value, "%zu", &mohclass->kill_delay) == 1) {
				mohclass->kill_delay *= 1000;
			} else {
				ast_log(LOG_WARNING, "kill_escalation_delay '%s' is invalid.  Setting to 100ms\n", var->value);
				mohclass->kill_delay = 100000;
			}
		} else if (!strcasecmp(var->name, "kill_method")) {
			if (!strcasecmp(var->value, "process")) {
				mohclass->kill_method = KILL_METHOD_PROCESS;
			} else if (!strcasecmp(var->value, "process_group")) {
				mohclass->kill_method = KILL_METHOD_PROCESS_GROUP;
			} else {
				ast_log(LOG_WARNING, "kill_method '%s' is invalid.  Setting to 'process_group'\n", var->value);
				mohclass->kill_method = KILL_METHOD_PROCESS_GROUP;
			}
		} else if (!strcasecmp(var->name, "answeredonly")) {
			mohclass->answeredonly = ast_true(var->value) ? 1: 0;
		}
	}

	if (playlist_entries) {
		/* If we aren't in playlist mode, drop any list we may have already built */
		if (strcasecmp(mohclass->mode, "playlist")) {
			ast_log(LOG_NOTICE, "Ignoring playlist entries because we are in '%s' mode.\n",
				mohclass->mode);
			ao2_ref(playlist_entries, -1);
			return;
		}

		AST_VECTOR_COMPACT(playlist_entries);

		/* We don't need to lock here because we are the thread that
		 * created this mohclass and we haven't published it yet */
		ao2_ref(mohclass->files, -1);
		mohclass->files = playlist_entries;
	}
}

static int on_moh_file(const char *directory, const char *filename, void *obj)
{
	struct ast_vector_string *files = obj;
	char *full_path;
	char *extension;

	/* Skip files that starts with a dot */
	if (*filename == '.') {
		ast_debug(4, "Skipping '%s/%s' because it starts with a dot\n",
			directory, filename);
		return 0;
	}

	/* We can't do anything with files that don't have an extension,
	 * so check that first and punt if we can't find something */
	extension = strrchr(filename, '.');
	if (!extension) {
		ast_debug(4, "Skipping '%s/%s' because it doesn't have an extension\n",
			directory, filename);
		return 0;
	}

	/* The extension needs at least two characters (after the .) to be useful */
	if (strlen(extension) < 3) {
		ast_debug(4, "Skipping '%s/%s' because it doesn't have at least a two "
			"character extension\n", directory, filename);
		return 0;
	}

	/* Build the full path (excluding the extension) */
	if (ast_asprintf(&full_path, "%s/%.*s",
			directory,
			(int) (extension - filename), filename) < 0) {
		/* If we don't have enough memory to build this path, there is no
		 * point in continuing */
		return 1;
	}

	/* If the file is present in multiple formats, ensure we only put it
	 * into the list once. Pretty sure this is O(n^2). */
	if (AST_VECTOR_GET_CMP(files, &full_path[0], !strcmp)) {
		ast_free(full_path);
		return 0;
	}

	if (AST_VECTOR_APPEND(files, full_path)) {
		/* AST_VECTOR_APPEND() can only fail on allocation failure, so
		 * we stop iterating */
		ast_free(full_path);
		return 1;
	}

	return 0;
}

static int moh_filename_strcasecmp(const void *a, const void *b)
{
	const char **s1 = (const char **) a;
	const char **s2 = (const char **) b;
	return strcasecmp(*s1, *s2);
}

static int moh_scan_files(struct mohclass *class) {

	char dir_path[PATH_MAX - sizeof(class->dir)];
	struct ast_vector_string *files;

	if (class->dir[0] != '/') {
		snprintf(dir_path, sizeof(dir_path), "%s/%s", ast_config_AST_DATA_DIR, class->dir);
	} else {
		ast_copy_string(dir_path, class->dir, sizeof(dir_path));
	}

	ast_debug(4, "Scanning '%s' for files for class '%s'\n", dir_path, class->name);

	/* 16 seems like a reasonable default */
	files = moh_file_vector_alloc(16);
	if (!files) {
		return -1;
	}

	if (ast_file_read_dir(dir_path, on_moh_file, files)) {
		ao2_ref(files, -1);
		return -1;
	}

	if (ast_test_flag(class, MOH_SORTALPHA)) {
		AST_VECTOR_SORT(files, moh_filename_strcasecmp);
	}

	AST_VECTOR_COMPACT(files);

	ao2_lock(class);
	ao2_ref(class->files, -1);
	class->files = files;
	ao2_unlock(class);

	return AST_VECTOR_SIZE(files);
}

static int init_files_class(struct mohclass *class)
{
	int res;

	res = moh_scan_files(class);

	if (res < 0) {
		return -1;
	}

	if (!res) {
		ast_verb(3, "Files not found in %s for moh class:%s\n",
			class->dir, class->name);
		return -1;
	}

	return 0;
}

static void moh_rescan_files(void) {
	struct ao2_iterator i;
	struct mohclass *c;

	i = ao2_iterator_init(mohclasses, 0);

	while ((c = ao2_iterator_next(&i))) {
		if (!strcasecmp(c->mode, "files")) {
			moh_scan_files(c);
		}
		ao2_ref(c, -1);
	}

	ao2_iterator_destroy(&i);
}

static int moh_diff(struct mohclass *old, struct mohclass *new)
{
	if (!old || !new) {
		return -1;
	}

	if (strcmp(old->dir, new->dir)) {
		return -1;
	} else if (strcmp(old->mode, new->mode)) {
		return -1;
	} else if (strcmp(old->args, new->args)) {
		return -1;
	} else if (old->flags != new->flags) {
		return -1;
	}

	return 0;
}

static int init_app_class(struct mohclass *class)
{
	if (!strcasecmp(class->mode, "custom")) {
		ast_set_flag(class, MOH_CUSTOM);
	} else if (!strcasecmp(class->mode, "mp3nb")) {
		ast_set_flag(class, MOH_SINGLE);
	} else if (!strcasecmp(class->mode, "quietmp3nb")) {
		ast_set_flag(class, MOH_SINGLE | MOH_QUIET);
	} else if (!strcasecmp(class->mode, "quietmp3")) {
		ast_set_flag(class, MOH_QUIET);
	}

	class->srcfd = -1;

	if (!(class->timer = ast_timer_open())) {
		ast_log(LOG_WARNING, "Unable to create timer: %s\n", strerror(errno));
		return -1;
	}
	if (class->timer && ast_timer_set_rate(class->timer, 25)) {
		ast_log(LOG_WARNING, "Unable to set 40ms frame rate: %s\n", strerror(errno));
		ast_timer_close(class->timer);
		class->timer = NULL;
	}

	if (ast_pthread_create_background(&class->thread, NULL, monmp3thread, class)) {
		ast_log(LOG_WARNING, "Unable to create moh thread...\n");
		if (class->timer) {
			ast_timer_close(class->timer);
			class->timer = NULL;
		}
		return -1;
	}

	return 0;
}

/*!
 * \note This function owns the reference it gets to moh if unref is true
 */
#define moh_register(moh, reload, unref) _moh_register(moh, reload, unref, __FILE__, __LINE__, __PRETTY_FUNCTION__)
static int _moh_register(struct mohclass *moh, int reload, int unref, const char *file, int line, const char *funcname)
{
	struct mohclass *mohclass = NULL;

	mohclass = _get_mohbyname(moh->name, 0, MOH_NOTDELETED, file, line, funcname);

	if (mohclass && !moh_diff(mohclass, moh)) {
		ast_log(LOG_WARNING, "Music on Hold class '%s' already exists\n", moh->name);
		mohclass = mohclass_unref(mohclass, "unreffing mohclass we just found by name");
		if (unref) {
			moh = mohclass_unref(moh, "unreffing potential new moh class (it is a duplicate)");
		}
		return -1;
	} else if (mohclass) {
		/* Found a class, but it's different from the one being registered */
		mohclass = mohclass_unref(mohclass, "unreffing mohclass we just found by name");
	}

	time(&moh->start);
	moh->start -= respawn_time;

	if (!strcasecmp(moh->mode, "files")) {
		if (init_files_class(moh)) {
			if (unref) {
				moh = mohclass_unref(moh, "unreffing potential new moh class (init_files_class failed)");
			}
			return -1;
		}
	} else if (!strcasecmp(moh->mode, "playlist")) {
		size_t file_count;

		ao2_lock(moh);
		file_count = AST_VECTOR_SIZE(moh->files);
		ao2_unlock(moh);

		if (!file_count) {
			if (unref) {
				moh = mohclass_unref(moh, "unreffing potential new moh class (no playlist entries)");
			}
			return -1;
		}
	} else if (!strcasecmp(moh->mode, "mp3") || !strcasecmp(moh->mode, "mp3nb") ||
			!strcasecmp(moh->mode, "quietmp3") || !strcasecmp(moh->mode, "quietmp3nb") ||
			!strcasecmp(moh->mode, "httpmp3") || !strcasecmp(moh->mode, "custom")) {
		if (init_app_class(moh)) {
			if (unref) {
				moh = mohclass_unref(moh, "unreffing potential new moh class (init_app_class_failed)");
			}
			return -1;
		}
	} else {
		ast_log(LOG_WARNING, "Don't know how to do a mode '%s' music on hold\n", moh->mode);
		if (unref) {
			moh = mohclass_unref(moh, "unreffing potential new moh class (unknown mode)");
		}
		return -1;
	}

	ao2_t_link(mohclasses, moh, "Adding class to container");

	if (unref) {
		moh = mohclass_unref(moh, "Unreffing new moh class because we just added it to the container");
	}

	return 0;
}

#define moh_unregister(a) _moh_unregister(a,__FILE__,__LINE__,__PRETTY_FUNCTION__)
static int _moh_unregister(struct mohclass *moh, const char *file, int line, const char *funcname)
{
	ao2_t_unlink(mohclasses, moh, "Removing class from container");
	return 0;
}

static void local_ast_moh_cleanup(struct ast_channel *chan)
{
	struct moh_files_state *state = ast_channel_music_state(chan);

	if (state) {
		ast_channel_music_state_set(chan, NULL);
		if (state->class) {
			/* This should never happen.  We likely just leaked some resource. */
			state->class =
				mohclass_unref(state->class, "Uh Oh. Cleaning up MOH with an active class");
			ast_log(LOG_WARNING, "Uh Oh. Cleaning up MOH with an active class\n");
		}
		ao2_cleanup(state->origwfmt);
		ao2_cleanup(state->mohwfmt);
		ast_free(state);
		/* Only held a module reference if we had a music state */
		ast_module_unref(ast_module_info->self);
	}
}

/*! \brief Support routing for 'moh unregister class' CLI
 * This is in charge of generating all strings that match a prefix in the
 * given position. As many functions of this kind, each invokation has
 * O(state) time complexity so be careful in using it.
 */
static char *complete_mohclass_realtime(const char *line, const char *word, int pos, int state)
{
	int which=0;
	struct mohclass *cur;
	char *c = NULL;
	int wordlen = strlen(word);
	struct ao2_iterator i;

	if (pos != 3) {
		return NULL;
	}

	i = ao2_iterator_init(mohclasses, 0);
	while ((cur = ao2_t_iterator_next(&i, "iterate thru mohclasses"))) {
		if (cur->realtime && !strncasecmp(cur->name, word, wordlen) && ++which > state) {
			c = ast_strdup(cur->name);
			mohclass_unref(cur, "drop ref in iterator loop break");
			break;
		}
		mohclass_unref(cur, "drop ref in iterator loop");
	}
	ao2_iterator_destroy(&i);

	return c;
}

static char *handle_cli_moh_unregister_class(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct mohclass *cur;
	int len;
	int found = 0;
	struct ao2_iterator i;

	switch (cmd) {
		case CLI_INIT:
			e->command = "moh unregister class";
			e->usage =
				"Usage: moh unregister class <class>\n"
				"       Unregisters a realtime moh class.\n";
			return NULL;
		case CLI_GENERATE:
			return complete_mohclass_realtime(a->line, a->word, a->pos, a->n);
	}

	if (a->argc != 4)
		return CLI_SHOWUSAGE;

	len = strlen(a->argv[3]);

	i = ao2_iterator_init(mohclasses, 0);
	while ((cur = ao2_t_iterator_next(&i, "iterate thru mohclasses"))) {
		if (cur->realtime && len == strlen(cur->name) && !strncasecmp(cur->name, a->argv[3], len)) {
			found = 1;
			break;
		}
		mohclass_unref(cur, "drop ref in iterator loop");
	}
	ao2_iterator_destroy(&i);

	if (found) {
		moh_unregister(cur);
		mohclass_unref(cur, "drop ref after unregister");
	} else {
		ast_cli(a->fd, "No such realtime moh class '%s'\n", a->argv[3]);
	}

	return CLI_SUCCESS;
}



static void moh_class_destructor(void *obj);

#define moh_class_malloc()	_moh_class_malloc(__FILE__,__LINE__,__PRETTY_FUNCTION__)

static struct mohclass *_moh_class_malloc(const char *file, int line, const char *funcname)
{
	struct mohclass *class;

	class = __ao2_alloc(sizeof(*class), moh_class_destructor, AO2_ALLOC_OPT_LOCK_MUTEX,
		"Allocating new moh class", file, line, funcname);
	if (class) {
		class->format = ao2_bump(ast_format_slin);
		class->srcfd = -1;
		class->kill_delay = 100000;

		/* We create an empty one by default */
		class->files = moh_file_vector_alloc(0);
		if (!class->files) {
			ao2_ref(class, -1);
			return NULL;
		}
	}

	return class;
}

static struct ast_variable *load_realtime_musiconhold(const char *name)
{
	struct ast_variable *var = ast_load_realtime("musiconhold", "name", name, SENTINEL);

	if (var) {
		const char *mode = ast_variable_find_in_list(var, "mode");
		if (ast_strings_equal(mode, "playlist")) {
			struct ast_config *entries = ast_load_realtime_multientry("musiconhold_entry", "position >=", "0", "name", name, SENTINEL);
			char *category = NULL;
			size_t entry_count = 0;

			/* entries is NULL if there are no results */
			if (entries) {
				while ((category = ast_category_browse(entries, category))) {
					const char *entry = ast_variable_retrieve(entries, category, "entry");

					if (entry) {
						struct ast_variable *dup = ast_variable_new("entry", entry, "");
						if (dup) {
							entry_count++;
							ast_variable_list_append(&var, dup);
						}
					}
				}
				ast_config_destroy(entries);
			}

			if (entry_count == 0) {
				/* Behave as though this class doesn't exist */
				ast_variables_destroy(var);
				var = NULL;
			}
		}
	}

	if (!var) {
		ast_log(LOG_WARNING,
			"Music on Hold class '%s' not found in memory/database. "
			"Verify your configuration.\n",
			name);
	}
	return var;
}

static int local_ast_moh_start(struct ast_channel *chan, const char *mclass, const char *interpclass)
{
	struct mohclass *mohclass = NULL;
	struct moh_files_state *state = ast_channel_music_state(chan);
	struct ast_variable *var = NULL;
	int res = 0;
	int i;
	int realtime_possible = ast_check_realtime("musiconhold");
	int warn_if_not_in_memory = !realtime_possible;
	const char *classes[] = {NULL, NULL, interpclass, "default"};

	if (ast_test_flag(global_flags, MOH_PREFERCHANNELCLASS)) {
		classes[0] = ast_channel_musicclass(chan);
		classes[1] = mclass;
	} else {
		classes[0] = mclass;
		classes[1] = ast_channel_musicclass(chan);
	}

	/* The following is the order of preference for which class to use:
	 * 1) The channels explicitly set musicclass, which should *only* be
	 *    set by a call to Set(CHANNEL(musicclass)=whatever) in the dialplan.
	 *    Unless preferchannelclass in musiconhold.conf is false
	 * 2) The mclass argument. If a channel is calling ast_moh_start() as the
	 *    result of receiving a HOLD control frame, this should be the
	 *    payload that came with the frame.
	 * 3) The channels explicitly set musicclass, which should *only* be
	 *    set by a call to Set(CHANNEL(musicclass)=whatever) in the dialplan.
	 * 4) The interpclass argument. This would be from the mohinterpret
	 *    option from channel drivers. This is the same as the old musicclass
	 *    option.
	 * 5) The default class.
	 */

	for (i = 0; i < ARRAY_LEN(classes); ++i) {
		if (!ast_strlen_zero(classes[i])) {
			mohclass = get_mohbyname(classes[i], warn_if_not_in_memory, 0);
			if (!mohclass && realtime_possible) {
				var = load_realtime_musiconhold(classes[i]);
			}
			if (mohclass || var) {
				break;
			}
		}
	}

	/* If no moh class found in memory, then check RT. Note that the logic used
	 * above guarantees that if var is non-NULL, then mohclass must be NULL.
	 */
	if (var) {
		if ((mohclass = moh_class_malloc())) {
			mohclass->realtime = 1;

			moh_parse_options(var, mohclass);
			ast_variables_destroy(var);

			if (ast_strlen_zero(mohclass->dir)) {
				if (!strcasecmp(mohclass->mode, "custom") || !strcasecmp(mohclass->mode, "playlist")) {
					strcpy(mohclass->dir, "nodir");
				} else {
					ast_log(LOG_WARNING, "A directory must be specified for class '%s'!\n", mohclass->name);
					mohclass = mohclass_unref(mohclass, "unreffing potential mohclass (no directory specified)");
					return -1;
				}
			}
			if (ast_strlen_zero(mohclass->mode)) {
				ast_log(LOG_WARNING, "A mode must be specified for class '%s'!\n", mohclass->name);
				mohclass = mohclass_unref(mohclass, "unreffing potential mohclass (no mode specified)");
				return -1;
			}
			if (ast_strlen_zero(mohclass->args) && !strcasecmp(mohclass->mode, "custom")) {
				ast_log(LOG_WARNING, "An application must be specified for class '%s'!\n", mohclass->name);
				mohclass = mohclass_unref(mohclass, "unreffing potential mohclass (no app specified for custom mode");
				return -1;
			}

			if (ast_test_flag(global_flags, MOH_CACHERTCLASSES)) {
				/* CACHERTCLASSES enabled, let's add this class to default tree */
				if (state && state->class) {
					/* Class already exist for this channel */
					ast_log(LOG_NOTICE, "This channel already has a MOH class attached (%s)!\n", state->class->name);
				}
				/* We don't want moh_register to unref the mohclass because we do it at the end of this function as well.
				 * If we allowed moh_register to unref the mohclass,too, then the count would be off by one. The result would
				 * be that the destructor would be called when the generator on the channel is deactivated. The container then
				 * has a pointer to a freed mohclass, so any operations involving the mohclass container would result in reading
				 * invalid memory.
				 */
				if (moh_register(mohclass, 0, DONT_UNREF) == -1) {
					mohclass = mohclass_unref(mohclass, "unreffing mohclass failed to register");
					return -1;
				}
			} else {
				/* We don't register RT moh class, so let's init it manually */

				time(&mohclass->start);
				mohclass->start -= respawn_time;

				if (!strcasecmp(mohclass->mode, "files")) {
					/*
					 * XXX moh_scan_files returns -1 if it is unable to open the
					 * configured directory or there is a memory allocation
					 * failure. Otherwise it returns the number of files for this music
					 * class. This check is only checking if the number of files is zero
					 * and it ignores the -1 case. To avoid a behavior change we keep this
					 * as-is, but we should address what the 'correct' behavior should be.
					 */
					if (!moh_scan_files(mohclass)) {
						mohclass = mohclass_unref(mohclass, "unreffing potential mohclass (moh_scan_files failed)");
						return -1;
					}
					if (strchr(mohclass->args, 'r')) {
						static int deprecation_warning = 0;
						if (!deprecation_warning) {
							ast_log(LOG_WARNING, "Music on hold 'application=r' setting is deprecated in 14.  Please use 'sort=random' instead.\n");
							deprecation_warning = 1;
						}
						ast_set_flag(mohclass, MOH_RANDOMIZE);
					}
				} else if (!strcasecmp(mohclass->mode, "playlist")) {
					size_t file_count;

					ao2_lock(mohclass);
					file_count = AST_VECTOR_SIZE(mohclass->files);
					ao2_unlock(mohclass);

					if (!file_count) {
						mohclass = mohclass_unref(mohclass, "unreffing potential mohclass (no playlist entries)");
						return -1;
					}
				} else if (!strcasecmp(mohclass->mode, "mp3") || !strcasecmp(mohclass->mode, "mp3nb") || !strcasecmp(mohclass->mode, "quietmp3") || !strcasecmp(mohclass->mode, "quietmp3nb") || !strcasecmp(mohclass->mode, "httpmp3") || !strcasecmp(mohclass->mode, "custom")) {

					if (!strcasecmp(mohclass->mode, "custom"))
						ast_set_flag(mohclass, MOH_CUSTOM);
					else if (!strcasecmp(mohclass->mode, "mp3nb"))
						ast_set_flag(mohclass, MOH_SINGLE);
					else if (!strcasecmp(mohclass->mode, "quietmp3nb"))
						ast_set_flag(mohclass, MOH_SINGLE | MOH_QUIET);
					else if (!strcasecmp(mohclass->mode, "quietmp3"))
						ast_set_flag(mohclass, MOH_QUIET);

					mohclass->srcfd = -1;
					if (!(mohclass->timer = ast_timer_open())) {
						ast_log(LOG_WARNING, "Unable to create timer: %s\n", strerror(errno));
					}
					if (mohclass->timer && ast_timer_set_rate(mohclass->timer, 25)) {
						ast_log(LOG_WARNING, "Unable to set 40ms frame rate: %s\n", strerror(errno));
						ast_timer_close(mohclass->timer);
						mohclass->timer = NULL;
					}

					/* Let's check if this channel already had a moh class before */
					if (state && state->class) {
						/* Class already exist for this channel */
						ast_log(LOG_NOTICE, "This channel already has a MOH class attached (%s)!\n", state->class->name);
						if (state->class->realtime && !ast_test_flag(global_flags, MOH_CACHERTCLASSES) && !strcasecmp(mohclass->name, state->class->name)) {
							/* we found RT class with the same name, seems like we should continue playing existing one */
							mohclass = mohclass_unref(mohclass, "unreffing potential mohclass (channel already has one)");
							mohclass = mohclass_ref(state->class, "using existing class from state");
						}
					} else {
						if (ast_pthread_create_background(&mohclass->thread, NULL, monmp3thread, mohclass)) {
							ast_log(LOG_WARNING, "Unable to create moh...\n");
							if (mohclass->timer) {
								ast_timer_close(mohclass->timer);
								mohclass->timer = NULL;
							}
							mohclass = mohclass_unref(mohclass, "Unreffing potential mohclass (failed to create background thread)");
							return -1;
						}
					}
				} else {
					ast_log(LOG_WARNING, "Don't know how to do a mode '%s' music on hold\n", mohclass->mode);
					mohclass = mohclass_unref(mohclass, "unreffing potential mohclass (unknown mode)");
					return -1;
				}
			}
		} else {
			ast_variables_destroy(var);
			var = NULL;
		}
	}

	if (!mohclass) {
		return -1;
	}

	if (mohclass->answeredonly && (ast_channel_state(chan) != AST_STATE_UP)) {
		ast_verb(3, "The channel '%s' is not answered yet. Ignore the moh request.\n", ast_channel_name(chan));
		return -1;
	}

	/* If we are using a cached realtime class with files, re-scan the files */
	if (!var && ast_test_flag(global_flags, MOH_CACHERTCLASSES) && mohclass->realtime && !strcasecmp(mohclass->mode, "files")) {
		/*
		 * XXX moh_scan_files returns -1 if it is unable to open the configured directory
		 * or there is a memory allocation failure. Otherwise it returns the number of
		 * files for this music class. This check is only checking if the number of files
		 * is zero and it ignores the -1 case. To avoid a behavior change we keep this
		 * as-is, but we should address what the 'correct' behavior should be.
		 */
		if (!moh_scan_files(mohclass)) {
			mohclass = mohclass_unref(mohclass, "unreffing potential mohclass (moh_scan_files failed)");
			return -1;
		}
	}

	if (!state || !state->class || strcmp(mohclass->name, state->class->name)) {
		size_t file_count;

		ao2_lock(mohclass);
		file_count = AST_VECTOR_SIZE(mohclass->files);
		ao2_unlock(mohclass);

		if (file_count) {
			res = ast_activate_generator(chan, &moh_file_stream, mohclass);
		} else {
			res = ast_activate_generator(chan, &mohgen, mohclass);
		}
	}
	if (!res) {
		ast_channel_lock(chan);
		ast_channel_latest_musicclass_set(chan, mohclass->name);
		ast_set_flag(ast_channel_flags(chan), AST_FLAG_MOH);
		ast_channel_unlock(chan);
	}

	mohclass = mohclass_unref(mohclass, "unreffing local reference to mohclass in local_ast_moh_start");

	return res;
}

static void local_ast_moh_stop(struct ast_channel *chan)
{
	ast_deactivate_generator(chan);

	ast_channel_lock(chan);
	ast_clear_flag(ast_channel_flags(chan), AST_FLAG_MOH);
	if (ast_channel_music_state(chan)) {
		if (ast_channel_stream(chan)) {
			ast_closestream(ast_channel_stream(chan));
			ast_channel_stream_set(chan, NULL);
		}
	}
	ast_channel_unlock(chan);
}

static void moh_class_destructor(void *obj)
{
	struct mohclass *class = obj;
	struct mohdata *member;
	pthread_t tid = 0;

	ast_debug(1, "Destroying MOH class '%s'\n", class->name);

	ao2_lock(class);
	while ((member = AST_LIST_REMOVE_HEAD(&class->members, list))) {
		ast_free(member);
	}
	ao2_cleanup(class->files);
	ao2_unlock(class);

	/* Kill the thread first, so it cannot restart the child process while the
	 * class is being destroyed */
	if (class->thread != AST_PTHREADT_NULL && class->thread != 0) {
		tid = class->thread;
		class->thread = AST_PTHREADT_NULL;
		pthread_cancel(tid);
		/* We'll collect the exit status later, after we ensure all the readers
		 * are dead. */
	}

	if (class->pid > 1) {
		char buff[8192];
		int bytes, tbytes = 0, stime = 0;

		ast_debug(1, "killing %d!\n", class->pid);

		stime = time(NULL) + 2;
		killpid(class->pid, class->kill_delay, class->kill_method);

		while ((ast_wait_for_input(class->srcfd, 100) > 0) &&
				(bytes = read(class->srcfd, buff, 8192)) && time(NULL) < stime) {
			tbytes = tbytes + bytes;
		}

		ast_debug(1, "mpg123 pid %d and child died after %d bytes read\n",
			class->pid, tbytes);

		class->pid = 0;
		close(class->srcfd);
		class->srcfd = -1;
	}

	if (class->timer) {
		ast_timer_close(class->timer);
		class->timer = NULL;
	}

	ao2_cleanup(class->format);

	/* Finally, collect the exit status of the monitor thread */
	if (tid > 0) {
		pthread_join(tid, NULL);
	}

}

static int moh_class_mark(void *obj, void *arg, int flags)
{
	struct mohclass *class = obj;

	if ( ((flags & MOH_REALTIME) && class->realtime) || !(flags & MOH_REALTIME) ) {
		class->delete = 1;
	}

	return 0;
}

static int moh_classes_delete_marked(void *obj, void *arg, int flags)
{
	struct mohclass *class = obj;

	return class->delete ? CMP_MATCH : 0;
}

static int load_moh_classes(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct mohclass *class;
	char *cat;
	int numclasses = 0;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	cfg = ast_config_load("musiconhold.conf", config_flags);

	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		if (ast_check_realtime("musiconhold") && reload) {
			ao2_t_callback(mohclasses, OBJ_NODATA | MOH_REALTIME, moh_class_mark, NULL, "Mark realtime classes for deletion");
			ao2_t_callback(mohclasses, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, moh_classes_delete_marked, NULL, "Purge marked classes");
		}
		moh_rescan_files();
		return 0;
	}

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		if (ast_check_realtime("musiconhold") && reload) {
			ao2_t_callback(mohclasses, OBJ_NODATA, moh_class_mark, NULL, "Mark deleted classes");
			ao2_t_callback(mohclasses, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, moh_classes_delete_marked, NULL, "Purge marked classes");
		}
		return 0;
	}

	if (reload) {
		ao2_t_callback(mohclasses, OBJ_NODATA, moh_class_mark, NULL, "Mark deleted classes");
	}

	ast_clear_flag(global_flags, AST_FLAGS_ALL);
	ast_set2_flag(global_flags, 1, MOH_PREFERCHANNELCLASS);

	cat = ast_category_browse(cfg, NULL);
	for (; cat; cat = ast_category_browse(cfg, cat)) {
		/* Setup common options from [general] section */
		if (!strcasecmp(cat, "general")) {
			for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
				if (!strcasecmp(var->name, "cachertclasses")) {
					ast_set2_flag(global_flags, ast_true(var->value), MOH_CACHERTCLASSES);
				} else if (!strcasecmp(var->name, "preferchannelclass")) {
					ast_set2_flag(global_flags, ast_true(var->value), MOH_PREFERCHANNELCLASS);
				} else {
					ast_log(LOG_WARNING, "Unknown option '%s' in [general] section of musiconhold.conf\n", var->name);
				}
			}
			continue;
		}

		if (!(class = moh_class_malloc())) {
			break;
		}

		moh_parse_options(ast_variable_browse(cfg, cat), class);
		/* For compatibility with the past, we overwrite any name=name
		 * with the context [name]. */
		ast_copy_string(class->name, cat, sizeof(class->name));

		if (ast_strlen_zero(class->dir)) {
			if (!strcasecmp(class->mode, "custom") || !strcasecmp(class->mode, "playlist")) {
				strcpy(class->dir, "nodir");
			} else {
				ast_log(LOG_WARNING, "A directory must be specified for class '%s'!\n", class->name);
				class = mohclass_unref(class, "unreffing potential mohclass (no directory)");
				continue;
			}
		}
		if (ast_strlen_zero(class->mode)) {
			ast_log(LOG_WARNING, "A mode must be specified for class '%s'!\n", class->name);
			class = mohclass_unref(class, "unreffing potential mohclass (no mode)");
			continue;
		}
		if (ast_strlen_zero(class->args) && !strcasecmp(class->mode, "custom")) {
			ast_log(LOG_WARNING, "An application must be specified for class '%s'!\n", class->name);
			class = mohclass_unref(class, "unreffing potential mohclass (no app for custom mode)");
			continue;
		}

		/* Don't leak a class when it's already registered */
		if (!moh_register(class, reload, HANDLE_REF)) {
			numclasses++;
		}
	}

	ast_config_destroy(cfg);

	ao2_t_callback(mohclasses, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE,
			moh_classes_delete_marked, NULL, "Purge marked classes");

	return numclasses;
}

static void ast_moh_destroy(void)
{
	ast_verb(2, "Destroying musiconhold processes\n");
	if (mohclasses) {
		ao2_t_callback(mohclasses, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL, "Destroy callback");
		ao2_ref(mohclasses, -1);
		mohclasses = NULL;
	}
}

static char *handle_cli_moh_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "moh reload";
		e->usage =
			"Usage: moh reload\n"
			"       Reloads the MusicOnHold module.\n"
			"       Alias for 'module reload res_musiconhold.so'\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	/* The module loader will prevent concurrent reloads from occurring, so we delegate */
	ast_module_reload("res_musiconhold");

	return CLI_SUCCESS;
}

static char *handle_cli_moh_show_files(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct mohclass *class;
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "moh show files";
		e->usage =
			"Usage: moh show files\n"
			"       Lists all loaded file-based MusicOnHold classes and their\n"
			"       files.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	i = ao2_iterator_init(mohclasses, 0);
	for (; (class = ao2_t_iterator_next(&i, "Show files iterator")); mohclass_unref(class, "Unref iterator in moh show files")) {
		struct ast_vector_string *files;

		ao2_lock(class);
		files = ao2_bump(class->files);
		ao2_unlock(class);

		if (AST_VECTOR_SIZE(files)) {
			int x;
			ast_cli(a->fd, "Class: %s\n", class->name);
			for (x = 0; x < AST_VECTOR_SIZE(files); x++) {
				ast_cli(a->fd, "\tFile: %s\n", AST_VECTOR_GET(files, x));
			}
		}

		ao2_ref(files, -1);
	}
	ao2_iterator_destroy(&i);

	return CLI_SUCCESS;
}

static char *handle_cli_moh_show_classes(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct mohclass *class;
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "moh show classes";
		e->usage =
			"Usage: moh show classes\n"
			"       Lists all MusicOnHold classes.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	i = ao2_iterator_init(mohclasses, 0);
	for (; (class = ao2_t_iterator_next(&i, "Show classes iterator")); mohclass_unref(class, "Unref iterator in moh show classes")) {
		ast_cli(a->fd, "Class: %s\n", class->name);
		ast_cli(a->fd, "\tMode: %s\n", S_OR(class->mode, "<none>"));
		ast_cli(a->fd, "\tDirectory: %s\n", S_OR(class->dir, "<none>"));
		if (ast_test_flag(class, MOH_ANNOUNCEMENT)) {
			ast_cli(a->fd, "\tAnnouncement: %s\n", S_OR(class->announcement, "<none>"));
		}
		if (ast_test_flag(class, MOH_CUSTOM)) {
			ast_cli(a->fd, "\tApplication: %s\n", S_OR(class->args, "<none>"));
			ast_cli(a->fd, "\tKill Escalation Delay: %zu ms\n", class->kill_delay / 1000);
			ast_cli(a->fd, "\tKill Method: %s\n",
				class->kill_method == KILL_METHOD_PROCESS ? "process" : "process_group");
		}
		if (strcasecmp(class->mode, "files")) {
			ast_cli(a->fd, "\tFormat: %s\n", ast_format_get_name(class->format));
		}
	}
	ao2_iterator_destroy(&i);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_moh[] = {
	AST_CLI_DEFINE(handle_cli_moh_reload,       	"Reload MusicOnHold"),
	AST_CLI_DEFINE(handle_cli_moh_show_classes, 	"List MusicOnHold classes"),
	AST_CLI_DEFINE(handle_cli_moh_show_files,   	"List MusicOnHold file-based classes"),
	AST_CLI_DEFINE(handle_cli_moh_unregister_class, "Unregister realtime MusicOnHold class")
};

static int moh_class_hash(const void *obj, const int flags)
{
	const struct mohclass *class = obj;

	return ast_str_case_hash(class->name);
}

static int moh_class_cmp(void *obj, void *arg, int flags)
{
	struct mohclass *class = obj, *class2 = arg;

	return strcasecmp(class->name, class2->name) ? 0 :
		(flags & MOH_NOTDELETED) && (class->delete || class2->delete) ? 0 :
		CMP_MATCH | CMP_STOP;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	int res;

	mohclasses = ao2_t_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, 53,
		moh_class_hash, NULL, moh_class_cmp, "Moh class container");
	if (!mohclasses) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!load_moh_classes(0) && ast_check_realtime("musiconhold") == 0) { 	/* No music classes configured, so skip it */
		ast_log(LOG_WARNING, "No music on hold classes configured, "
				"disabling music on hold.\n");
	} else {
		ast_install_music_functions(local_ast_moh_start, local_ast_moh_stop,
				local_ast_moh_cleanup);
	}

	res = ast_register_application_xml(play_moh, play_moh_exec);
	ast_register_atexit(ast_moh_destroy);
	ast_cli_register_multiple(cli_moh, ARRAY_LEN(cli_moh));
	if (!res)
		res = ast_register_application_xml(start_moh, start_moh_exec);
	if (!res)
		res = ast_register_application_xml(stop_moh, stop_moh_exec);

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	if (load_moh_classes(1)) {
		ast_install_music_functions(local_ast_moh_start, local_ast_moh_stop,
				local_ast_moh_cleanup);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int moh_class_inuse(void *obj, void *arg, int flags)
{
	struct mohclass *class = obj;

	return AST_LIST_EMPTY(&class->members) ? 0 : CMP_MATCH | CMP_STOP;
}

static int unload_module(void)
{
	int res = 0;
	struct mohclass *class = NULL;

	/* XXX This check shouldn't be required if module ref counting was being used
	 * properly ... */
	if ((class = ao2_t_callback(mohclasses, 0, moh_class_inuse, NULL, "Module unload callback"))) {
		class = mohclass_unref(class, "unref of class from module unload callback");
		res = -1;
	}

	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to unload res_musiconhold due to active MOH channels\n");
		return res;
	}

	ast_uninstall_music_functions();

	ast_moh_destroy();
	res = ast_unregister_application(play_moh);
	res |= ast_unregister_application(start_moh);
	res |= ast_unregister_application(stop_moh);
	ast_cli_unregister_multiple(cli_moh, ARRAY_LEN(cli_moh));
	ast_unregister_atexit(ast_moh_destroy);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Music On Hold Resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
