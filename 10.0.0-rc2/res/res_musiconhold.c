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
 * \arg See also \ref Config_moh
 * 
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<conflict>win32</conflict>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/signal.h>
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
#include "asterisk/manager.h"
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
			specified number of seconds. If duration is ommited, music plays indefinitely.
			Returns <literal>0</literal> when done, <literal>-1</literal> on hangup.</para>
			<para>This application does not automatically answer and should be preceeded by
			an application such as Answer() or Progress().</para>
		</description>
	</application>
	<application name="WaitMusicOnHold" language="en_US">
		<synopsis>
			Wait, playing Music On Hold.
		</synopsis>
		<syntax>
			<parameter name="delay" required="true" />
		</syntax>
		<description>
			<para> !!! DEPRECATED. Use MusicOnHold instead !!!</para>
			<para>Plays hold music specified number of seconds. Returns <literal>0</literal> when done,
			or <literal>-1</literal> on hangup. If no hold music is available, the delay will still occur
			with no sound.</para>
			<para> !!! DEPRECATED. Use MusicOnHold instead !!!</para>
		</description>
	</application>
	<application name="SetMusicOnHold" language="en_US">
		<synopsis>
			Set default Music On Hold class.
		</synopsis>
		<syntax>
			<parameter name="class" required="yes" />
		</syntax>
		<description>
			<para>!!! DEPRECATED. USe Set(CHANNEL(musicclass)=...) instead !!!</para>
			<para>Sets the default class for music on hold for a given channel.
			When music on hold is activated, this class will be used to select which
			music is played.</para>
			<para>!!! DEPRECATED. USe Set(CHANNEL(musicclass)=...) instead !!!</para>
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
static const char wait_moh[] = "WaitMusicOnHold";
static const char set_moh[] = "SetMusicOnHold";
static const char start_moh[] = "StartMusicOnHold";
static const char stop_moh[] = "StopMusicOnHold";

static int respawn_time = 20;

struct moh_files_state {
	/*! Holds a reference to the MOH class. */
	struct mohclass *class;
	char name[MAX_MUSICCLASS];
	struct ast_format origwfmt;
	struct ast_format mohwfmt;
	int samples;
	int sample_queue;
	int pos;
	int save_pos;
	int save_total;
	char *save_pos_filename;
};

#define MOH_QUIET		(1 << 0)
#define MOH_SINGLE		(1 << 1)
#define MOH_CUSTOM		(1 << 2)
#define MOH_RANDOMIZE		(1 << 3)
#define MOH_SORTALPHA		(1 << 4)

#define MOH_CACHERTCLASSES      (1 << 5)        /*!< Should we use a separate instance of MOH for each user or not */

/* Custom astobj2 flag */
#define MOH_NOTDELETED          (1 << 30)       /*!< Find only records that aren't deleted? */

static struct ast_flags global_flags[1] = {{0}};        /*!< global MOH_ flags */

struct mohclass {
	char name[MAX_MUSICCLASS];
	char dir[256];
	char args[256];
	char mode[80];
	char digit;
	/*! A dynamically sized array to hold the list of filenames in "files" mode */
	char **filearray;
	/*! The current size of the filearray */
	int allowed_files;
	/*! The current number of files loaded into the filearray */
	int total_files;
	unsigned int flags;
	/*! The format from the MOH source, not applicable to "files" mode */
	struct ast_format format;
	/*! The pid of the external application delivering MOH */
	int pid;
	time_t start;
	pthread_t thread;
	/*! Source of audio */
	int srcfd;
	/*! Generic timer */
	struct ast_timer *timer;
	/*! Created on the fly, from RT engine */
	unsigned int realtime:1;
	unsigned int delete:1;
	AST_LIST_HEAD_NOLOCK(, mohdata) members;
	AST_LIST_ENTRY(mohclass) list;
};

struct mohdata {
	int pipe[2];
	struct ast_format origwfmt;
	struct mohclass *parent;
	struct ast_frame f;
	AST_LIST_ENTRY(mohdata) list;
};

static struct ao2_container *mohclasses;

#define LOCAL_MPG_123 "/usr/local/bin/mpg123"
#define MPG_123 "/usr/bin/mpg123"
#define MAX_MP3S 256

static int reload(void);

#define mohclass_ref(class,string)   (ao2_t_ref((class), +1, (string)), class)

#ifndef REF_DEBUG
#define mohclass_unref(class,string) (ao2_t_ref((class), -1, (string)), (struct mohclass *) NULL)
#else
#define mohclass_unref(class,string) _mohclass_unref(class, string, __FILE__,__LINE__,__PRETTY_FUNCTION__)
static struct mohclass *_mohclass_unref(struct mohclass *class, const char *tag, const char *file, int line, const char *funcname)
{
	struct mohclass *dup;
	if ((dup = ao2_find(mohclasses, class, OBJ_POINTER))) {
		if (__ao2_ref_debug(dup, -1, (char *) tag, (char *) file, line, funcname) == 2) {
			FILE *ref = fopen("/tmp/refs", "a");
			if (ref) {
				fprintf(ref, "%p =1   %s:%d:%s (%s) BAD ATTEMPT!\n", class, file, line, funcname, tag);
				fclose(ref);
			}
			ast_log(LOG_WARNING, "Attempt to unref mohclass %p (%s) when only 1 ref remained, and class is still in a container! (at %s:%d (%s))\n",
				class, class->name, file, line, funcname);
		} else {
			ao2_ref(class, -1);
		}
	} else {
		ao2_t_ref(class, -1, (char *) tag);
	}
	return NULL;
}
#endif

static void moh_files_release(struct ast_channel *chan, void *data)
{
	struct moh_files_state *state;

	if (!chan || !chan->music_state) {
		return;
	}

	state = chan->music_state;

	if (chan->stream) {
		ast_closestream(chan->stream);
		chan->stream = NULL;
	}
	
	if (option_verbose > 2) {
		ast_verbose(VERBOSE_PREFIX_3 "Stopped music on hold on %s\n", chan->name);
	}

	ast_format_clear(&state->mohwfmt); /* make sure to clear this format before restoring the original format. */
	if (state->origwfmt.id && ast_set_write_format(chan, &state->origwfmt)) {
		ast_log(LOG_WARNING, "Unable to restore channel '%s' to format '%s'\n", chan->name, ast_getformatname(&state->origwfmt));
	}

	state->save_pos = state->pos;

	state->class = mohclass_unref(state->class, "Unreffing channel's music class upon deactivation of generator");
}

static int ast_moh_files_next(struct ast_channel *chan) 
{
	struct moh_files_state *state = chan->music_state;
	int tries;

	/* Discontinue a stream if it is running already */
	if (chan->stream) {
		ast_closestream(chan->stream);
		chan->stream = NULL;
	}

	if (!state->class->total_files) {
		ast_log(LOG_WARNING, "No files available for class '%s'\n", state->class->name);
		return -1;
	}

	if (state->pos == 0 && state->save_pos_filename == NULL) {
		/* First time so lets play the file. */
		state->save_pos = -1;
	} else if (state->save_pos >= 0 && state->save_pos < state->class->total_files && state->class->filearray[state->save_pos] == state->save_pos_filename) {
		/* If a specific file has been saved confirm it still exists and that it is still valid */
		state->pos = state->save_pos;
		state->save_pos = -1;
	} else if (ast_test_flag(state->class, MOH_RANDOMIZE)) {
		/* Get a random file and ensure we can open it */
		for (tries = 0; tries < 20; tries++) {
			state->pos = ast_random() % state->class->total_files;
			if (ast_fileexists(state->class->filearray[state->pos], NULL, NULL) > 0) {
				break;
			}
		}
		state->save_pos = -1;
		state->samples = 0;
	} else {
		/* This is easy, just increment our position and make sure we don't exceed the total file count */
		state->pos++;
		state->pos %= state->class->total_files;
		state->save_pos = -1;
		state->samples = 0;
	}

	for (tries = 0; tries < state->class->total_files; ++tries) {
		if (ast_openstream_full(chan, state->class->filearray[state->pos], chan->language, 1)) {
			break;
		}

		ast_log(LOG_WARNING, "Unable to open file '%s': %s\n", state->class->filearray[state->pos], strerror(errno));
		state->pos++;
		state->pos %= state->class->total_files;
	}

	if (tries == state->class->total_files) {
		return -1;
	}

	/* Record the pointer to the filename for position resuming later */
	state->save_pos_filename = state->class->filearray[state->pos];

	ast_debug(1, "%s Opened file %d '%s'\n", chan->name, state->pos, state->class->filearray[state->pos]);

	if (state->samples) {
		size_t loc;
		/* seek *SHOULD* be good since it's from a known location */
		ast_seekstream(chan->stream, state->samples, SEEK_SET);
		/* if the seek failed then recover because if there is not a valid read,
		 * moh_files_generate will return -1 and MOH will stop */
		loc = ast_tellstream(chan->stream);
		if (state->samples > loc && loc) {
			/* seek one sample from the end for one guaranteed valid read */
			ast_seekstream(chan->stream, 1, SEEK_END);
		}
	}

	return 0;
}

static struct ast_frame *moh_files_readframe(struct ast_channel *chan)
{
	struct ast_frame *f = NULL;

	if (!(chan->stream && (f = ast_readframe(chan->stream)))) {
		if (!ast_moh_files_next(chan))
			f = ast_readframe(chan->stream);
	}

	return f;
}

static void moh_files_write_format_change(struct ast_channel *chan, void *data)
{
	struct moh_files_state *state = chan->music_state;

	/* In order to prevent a recursive call to this function as a result
	 * of setting the moh write format back on the channel. Clear
	 * the moh write format before setting the write format on the channel.*/
	if (&state->origwfmt.id) {
		struct ast_format tmp;

		ast_format_copy(&tmp, &chan->writeformat);
		if (state->mohwfmt.id) {
			ast_format_clear(&state->origwfmt);
			ast_set_write_format(chan, &state->mohwfmt);
		}
		ast_format_copy(&state->origwfmt, &tmp);
	}
}

static int moh_files_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	struct moh_files_state *state = chan->music_state;
	struct ast_frame *f = NULL;
	int res = 0;

	state->sample_queue += samples;

	while (state->sample_queue > 0) {
		ast_channel_lock(chan);
		if ((f = moh_files_readframe(chan))) {
			/* We need to be sure that we unlock
			 * the channel prior to calling
			 * ast_write. Otherwise, the recursive locking
			 * that occurs can cause deadlocks when using
			 * indirect channels, like local channels
			 */
			ast_channel_unlock(chan);
			state->samples += f->samples;
			state->sample_queue -= f->samples;
			if (ast_format_cmp(&f->subclass.format, &state->mohwfmt) == AST_FORMAT_CMP_NOT_EQUAL) {
				ast_format_copy(&state->mohwfmt, &f->subclass.format);
			}
			res = ast_write(chan, f);
			ast_frfree(f);
			if (res < 0) {
				ast_log(LOG_WARNING, "Failed to write frame to '%s': %s\n", chan->name, strerror(errno));
				return -1;
			}
		} else {
			ast_channel_unlock(chan);
			return -1;	
		}
	}
	return res;
}

static void *moh_files_alloc(struct ast_channel *chan, void *params)
{
	struct moh_files_state *state;
	struct mohclass *class = params;

	if (!chan->music_state && (state = ast_calloc(1, sizeof(*state)))) {
		chan->music_state = state;
		ast_module_ref(ast_module_info->self);
	} else {
		state = chan->music_state;
		if (!state) {
			return NULL;
		}
		if (state->class) {
			mohclass_unref(state->class, "Uh Oh. Restarting MOH with an active class");
			ast_log(LOG_WARNING, "Uh Oh. Restarting MOH with an active class\n");
		}
	}

	/* LOGIC: Comparing an unrefcounted pointer is a really bad idea, because
	 * malloc may allocate a different class to the same memory block.  This
	 * might only happen when two reloads are generated in a short period of
	 * time, but it's still important to protect against.
	 * PROG: Compare the quick operation first, to save CPU. */
	if (state->save_total != class->total_files || strcmp(state->name, class->name) != 0) {
		memset(state, 0, sizeof(*state));
		if (ast_test_flag(class, MOH_RANDOMIZE) && class->total_files) {
			state->pos = ast_random() % class->total_files;
		}
	}

	state->class = mohclass_ref(class, "Reffing music class for channel");
	ast_format_copy(&state->origwfmt, &chan->writeformat);
	ast_format_copy(&state->mohwfmt, &chan->writeformat);

	/* For comparison on restart of MOH (see above) */
	ast_copy_string(state->name, class->name, sizeof(state->name));
	state->save_total = class->total_files;

	ast_verb(3, "Started music on hold, class '%s', on %s\n", class->name, chan->name);
	
	return chan->music_state;
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
		ast_string_field_set(chan,musicclass,classname);
		ast_moh_stop(chan);
		ast_moh_start(chan, classname, NULL);
	}
}

static struct ast_generator moh_file_stream = 
{
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
				pthread_testcancel();
			}
		}
		if (class->timer) {
			struct pollfd pfd = { .fd = ast_timer_fd(class->timer), .events = POLLIN, };

#ifdef SOLARIS
			thr_yield();
#endif
			/* Pause some amount of time */
			if (ast_poll(&pfd, 1, -1) > 0) {
				ast_timer_ack(class->timer, 1);
				res = 320;
			} else {
				ast_log(LOG_ERROR, "poll() failed: %s\n", strerror(errno));
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
			res = 8 * MOH_MS_INTERVAL;	/* 8 samples per millisecond */
		}
		if ((strncasecmp(class->dir, "http://", 7) && strcasecmp(class->dir, "nodir")) && AST_LIST_EMPTY(&class->members))
			continue;
		/* Read mp3 audio */
		len = ast_codec_get_len(&class->format, res);

		if ((res2 = read(class->srcfd, sbuf, len)) != len) {
			if (!res2) {
				close(class->srcfd);
				class->srcfd = -1;
				pthread_testcancel();
				if (class->pid > 1) {
					do {
						if (killpg(class->pid, SIGHUP) < 0) {
							if (errno == ESRCH) {
								break;
							}
							ast_log(LOG_WARNING, "Unable to send a SIGHUP to MOH process?!!: %s\n", strerror(errno));
						}
						usleep(100000);
						if (killpg(class->pid, SIGTERM) < 0) {
							if (errno == ESRCH) {
								break;
							}
							ast_log(LOG_WARNING, "Unable to terminate MOH process?!!: %s\n", strerror(errno));
						}
						usleep(100000);
						if (killpg(class->pid, SIGKILL) < 0) {
							if (errno == ESRCH) {
								break;
							}
							ast_log(LOG_WARNING, "Unable to kill MOH process?!!: %s\n", strerror(errno));
						}
					} while (0);
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
		ast_log(LOG_WARNING, "Unable to start music on hold class '%s' on channel %s\n", class, chan->name);
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

static int wait_moh_exec(struct ast_channel *chan, const char *data)
{
	static int deprecation_warning = 0;
	int res;

	if (!deprecation_warning) {
		deprecation_warning = 1;
		ast_log(LOG_WARNING, "WaitMusicOnHold application is deprecated and will be removed. Use MusicOnHold with duration parameter instead\n");
	}

	if (!data || !atoi(data)) {
		ast_log(LOG_WARNING, "WaitMusicOnHold requires an argument (number of seconds to wait)\n");
		return -1;
	}
	if (ast_moh_start(chan, NULL, NULL)) {
		ast_log(LOG_WARNING, "Unable to start music on hold for %d seconds on channel %s\n", atoi(data), chan->name);
		return 0;
	}
	res = ast_safe_sleep(chan, atoi(data) * 1000);
	ast_moh_stop(chan);
	return res;
}

static int set_moh_exec(struct ast_channel *chan, const char *data)
{
	static int deprecation_warning = 0;

	if (!deprecation_warning) {
		deprecation_warning = 1;
		ast_log(LOG_WARNING, "SetMusicOnHold application is deprecated and will be removed. Use Set(CHANNEL(musicclass)=...) instead\n");
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SetMusicOnHold requires an argument (class)\n");
		return -1;
	}
	ast_string_field_set(chan, musicclass, data);
	return 0;
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
		ast_log(LOG_WARNING, "Unable to start music on hold class '%s' on channel %s\n", class, chan->name);

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

#ifdef REF_DEBUG
	moh = __ao2_find_debug(mohclasses, &tmp_class, flags, "get_mohbyname", (char *) file, lineno, funcname);
#else
	moh = __ao2_find(mohclasses, &tmp_class, flags);
#endif

	if (!moh && warn) {
		ast_debug(1, "Music on Hold class '%s' not found in memory\n", name);
	}

	return moh;
}

static struct mohdata *mohalloc(struct mohclass *cl)
{
	struct mohdata *moh;
	long flags;

	if (!(moh = ast_calloc(1, sizeof(*moh))))
		return NULL;

	if (pipe(moh->pipe)) {
		ast_log(LOG_WARNING, "Failed to create pipe: %s\n", strerror(errno));
		ast_free(moh);
		return NULL;
	}

	/* Make entirely non-blocking */
	flags = fcntl(moh->pipe[0], F_GETFL);
	fcntl(moh->pipe[0], F_SETFL, flags | O_NONBLOCK);
	flags = fcntl(moh->pipe[1], F_GETFL);
	fcntl(moh->pipe[1], F_SETFL, flags | O_NONBLOCK);

	moh->f.frametype = AST_FRAME_VOICE;
	ast_format_copy(&moh->f.subclass.format, &cl->format);
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
	struct ast_format oldwfmt;

	ao2_lock(class);
	AST_LIST_REMOVE(&moh->parent->members, moh, list);	
	ao2_unlock(class);
	
	close(moh->pipe[0]);
	close(moh->pipe[1]);

	ast_format_copy(&oldwfmt, &moh->origwfmt);

	moh->parent = class = mohclass_unref(class, "unreffing moh->parent upon deactivation of generator");

	ast_free(moh);

	if (chan) {
		struct moh_files_state *state;

		state = chan->music_state;
		if (state && state->class) {
			state->class = mohclass_unref(state->class, "Unreffing channel's music class upon deactivation of generator");
		}
		if (oldwfmt.id && ast_set_write_format(chan, &oldwfmt)) {
			ast_log(LOG_WARNING, "Unable to restore channel '%s' to format %s\n",
					chan->name, ast_getformatname(&oldwfmt));
		}

		ast_verb(3, "Stopped music on hold on %s\n", chan->name);
	}
}

static void *moh_alloc(struct ast_channel *chan, void *params)
{
	struct mohdata *res;
	struct mohclass *class = params;
	struct moh_files_state *state;

	/* Initiating music_state for current channel. Channel should know name of moh class */
	if (!chan->music_state && (state = ast_calloc(1, sizeof(*state)))) {
		chan->music_state = state;
		ast_module_ref(ast_module_info->self);
	} else {
		state = chan->music_state;
		if (!state) {
			return NULL;
		}
		if (state->class) {
			mohclass_unref(state->class, "Uh Oh. Restarting MOH with an active class");
			ast_log(LOG_WARNING, "Uh Oh. Restarting MOH with an active class\n");
		}
		memset(state, 0, sizeof(*state));
	}

	if ((res = mohalloc(class))) {
		ast_format_copy(&res->origwfmt, &chan->writeformat);
		if (ast_set_write_format(chan, &class->format)) {
			ast_log(LOG_WARNING, "Unable to set channel '%s' to format '%s'\n", chan->name, ast_codec2str(&class->format));
			moh_release(NULL, res);
			res = NULL;
		} else {
			state->class = mohclass_ref(class, "Placing reference into state container");
		}
		ast_verb(3, "Started music on hold, class '%s', on channel '%s'\n", class->name, chan->name);
	}
	return res;
}

static int moh_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	struct mohdata *moh = data;
	short buf[1280 + AST_FRIENDLY_OFFSET / 2];
	int res;

	len = ast_codec_get_len(&moh->parent->format, samples);

	if (len > sizeof(buf) - AST_FRIENDLY_OFFSET) {
		ast_log(LOG_WARNING, "Only doing %d of %d requested bytes on %s\n", (int)sizeof(buf), len, chan->name);
		len = sizeof(buf) - AST_FRIENDLY_OFFSET;
	}
	res = read(moh->pipe[0], buf + AST_FRIENDLY_OFFSET/2, len);
	if (res <= 0)
		return 0;

	moh->f.datalen = res;
	moh->f.data.ptr = buf + AST_FRIENDLY_OFFSET / 2;
	moh->f.samples = ast_codec_get_samples(&moh->f);

	if (ast_write(chan, &moh->f) < 0) {
		ast_log(LOG_WARNING, "Failed to write frame to '%s': %s\n", chan->name, strerror(errno));
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

static int moh_add_file(struct mohclass *class, const char *filepath)
{
	if (!class->allowed_files) {
		if (!(class->filearray = ast_calloc(1, INITIAL_NUM_FILES * sizeof(*class->filearray))))
			return -1;
		class->allowed_files = INITIAL_NUM_FILES;
	} else if (class->total_files == class->allowed_files) {
		if (!(class->filearray = ast_realloc(class->filearray, class->allowed_files * sizeof(*class->filearray) * 2))) {
			class->allowed_files = 0;
			class->total_files = 0;
			return -1;
		}
		class->allowed_files *= 2;
	}

	if (!(class->filearray[class->total_files] = ast_strdup(filepath)))
		return -1;

	class->total_files++;

	return 0;
}

static int moh_sort_compare(const void *i1, const void *i2)
{
	char *s1, *s2;

	s1 = ((char **)i1)[0];
	s2 = ((char **)i2)[0];

	return strcasecmp(s1, s2);
}

static int moh_scan_files(struct mohclass *class) {

	DIR *files_DIR;
	struct dirent *files_dirent;
	char dir_path[PATH_MAX];
	char path[PATH_MAX];
	char filepath[PATH_MAX];
	char *ext;
	struct stat statbuf;
	int i;

	if (class->dir[0] != '/') {
		ast_copy_string(dir_path, ast_config_AST_DATA_DIR, sizeof(dir_path));
		strncat(dir_path, "/", sizeof(dir_path) - 1);
		strncat(dir_path, class->dir, sizeof(dir_path) - 1);
	} else {
		ast_copy_string(dir_path, class->dir, sizeof(dir_path));
	}
	ast_debug(4, "Scanning '%s' for files for class '%s'\n", dir_path, class->name);
	files_DIR = opendir(dir_path);
	if (!files_DIR) {
		ast_log(LOG_WARNING, "Cannot open dir %s or dir does not exist\n", dir_path);
		return -1;
	}

	for (i = 0; i < class->total_files; i++)
		ast_free(class->filearray[i]);

	class->total_files = 0;
	if (!getcwd(path, sizeof(path))) {
		ast_log(LOG_WARNING, "getcwd() failed: %s\n", strerror(errno));
		return -1;
	}
	if (chdir(dir_path) < 0) {
		ast_log(LOG_WARNING, "chdir() failed: %s\n", strerror(errno));
		return -1;
	}
	while ((files_dirent = readdir(files_DIR))) {
		/* The file name must be at least long enough to have the file type extension */
		if ((strlen(files_dirent->d_name) < 4))
			continue;

		/* Skip files that starts with a dot */
		if (files_dirent->d_name[0] == '.')
			continue;

		/* Skip files without extensions... they are not audio */
		if (!strchr(files_dirent->d_name, '.'))
			continue;

		snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, files_dirent->d_name);

		if (stat(filepath, &statbuf))
			continue;

		if (!S_ISREG(statbuf.st_mode))
			continue;

		if ((ext = strrchr(filepath, '.')))
			*ext = '\0';

		/* if the file is present in multiple formats, ensure we only put it into the list once */
		for (i = 0; i < class->total_files; i++)
			if (!strcmp(filepath, class->filearray[i]))
				break;

		if (i == class->total_files) {
			if (moh_add_file(class, filepath))
				break;
		}
	}

	closedir(files_DIR);
	if (chdir(path) < 0) {
		ast_log(LOG_WARNING, "chdir() failed: %s\n", strerror(errno));
		return -1;
	}
	if (ast_test_flag(class, MOH_SORTALPHA))
		qsort(&class->filearray[0], class->total_files, sizeof(char *), moh_sort_compare);
	return class->total_files;
}

static int init_files_class(struct mohclass *class)
{
	int res;

	res = moh_scan_files(class);

	if (res < 0) {
		return -1;
	}

	if (!res) {
		if (option_verbose > 2) {
			ast_verbose(VERBOSE_PREFIX_3 "Files not found in %s for moh class:%s\n",
					class->dir, class->name);
		}
		return -1;
	}

#if 0
	/* XXX This isn't correct.  Args is an application for custom mode. XXX */
	if (strchr(class->args, 'r')) {
		ast_set_flag(class, MOH_RANDOMIZE);
	}
#endif

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
#define moh_register(a,b,c)	_moh_register(a,b,c,__FILE__,__LINE__,__PRETTY_FUNCTION__)
static int _moh_register(struct mohclass *moh, int reload, int unref, const char *file, int line, const char *funcname)
{
	struct mohclass *mohclass = NULL;

	if ((mohclass = _get_mohbyname(moh->name, 0, MOH_NOTDELETED, file, line, funcname)) && !moh_diff(mohclass, moh)) {
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

static void local_ast_moh_cleanup(struct ast_channel *chan)
{
	struct moh_files_state *state = chan->music_state;

	if (state) {
		if (state->class) {
			/* This should never happen.  We likely just leaked some resource. */
			state->class =
				mohclass_unref(state->class, "Uh Oh. Cleaning up MOH with an active class");
			ast_log(LOG_WARNING, "Uh Oh. Cleaning up MOH with an active class\n");
		}
		ast_free(chan->music_state);
		chan->music_state = NULL;
		/* Only held a module reference if we had a music state */
		ast_module_unref(ast_module_info->self);
	}
}

static void moh_class_destructor(void *obj);

#define moh_class_malloc()	_moh_class_malloc(__FILE__,__LINE__,__PRETTY_FUNCTION__)

static struct mohclass *_moh_class_malloc(const char *file, int line, const char *funcname)
{
	struct mohclass *class;

	if ((class =
#ifdef REF_DEBUG
			__ao2_alloc_debug(sizeof(*class), moh_class_destructor, "Allocating new moh class", file, line, funcname, 1)
#elif defined(__AST_DEBUG_MALLOC)
			__ao2_alloc_debug(sizeof(*class), moh_class_destructor, "Allocating new moh class", file, line, funcname, 0)
#else
			ao2_alloc(sizeof(*class), moh_class_destructor)
#endif
		)) {
		ast_format_set(&class->format, AST_FORMAT_SLINEAR, 0);
		class->srcfd = -1;
	}

	return class;
}

static int local_ast_moh_start(struct ast_channel *chan, const char *mclass, const char *interpclass)
{
	struct mohclass *mohclass = NULL;
	struct moh_files_state *state = chan->music_state;
	struct ast_variable *var = NULL;
	int res;
	int realtime_possible = ast_check_realtime("musiconhold");

	/* The following is the order of preference for which class to use:
	 * 1) The channels explicitly set musicclass, which should *only* be
	 *    set by a call to Set(CHANNEL(musicclass)=whatever) in the dialplan.
	 * 2) The mclass argument. If a channel is calling ast_moh_start() as the
	 *    result of receiving a HOLD control frame, this should be the
	 *    payload that came with the frame.
	 * 3) The interpclass argument. This would be from the mohinterpret
	 *    option from channel drivers. This is the same as the old musicclass
	 *    option.
	 * 4) The default class.
	 */
	if (!ast_strlen_zero(chan->musicclass)) {
		mohclass = get_mohbyname(chan->musicclass, 1, 0);
		if (!mohclass && realtime_possible) {
			var = ast_load_realtime("musiconhold", "name", chan->musicclass, SENTINEL);
		}
	}
	if (!mohclass && !var && !ast_strlen_zero(mclass)) {
		mohclass = get_mohbyname(mclass, 1, 0);
		if (!mohclass && realtime_possible) {
			var = ast_load_realtime("musiconhold", "name", mclass, SENTINEL);
		}
	}
	if (!mohclass && !var && !ast_strlen_zero(interpclass)) {
		mohclass = get_mohbyname(interpclass, 1, 0);
		if (!mohclass && realtime_possible) {
			var = ast_load_realtime("musiconhold", "name", interpclass, SENTINEL);
		}
	}

	if (!mohclass && !var) {
		mohclass = get_mohbyname("default", 1, 0);
		if (!mohclass && realtime_possible) {
			var = ast_load_realtime("musiconhold", "name", "default", SENTINEL);
		}
	}

	/* If no moh class found in memory, then check RT. Note that the logic used
	 * above guarantees that if var is non-NULL, then mohclass must be NULL.
	 */
	if (var) {
		struct ast_variable *tmp = NULL;

		if ((mohclass = moh_class_malloc())) {
			mohclass->realtime = 1;
			for (tmp = var; tmp; tmp = tmp->next) {
				if (!strcasecmp(tmp->name, "name"))
					ast_copy_string(mohclass->name, tmp->value, sizeof(mohclass->name));
				else if (!strcasecmp(tmp->name, "mode"))
					ast_copy_string(mohclass->mode, tmp->value, sizeof(mohclass->mode)); 
				else if (!strcasecmp(tmp->name, "directory"))
					ast_copy_string(mohclass->dir, tmp->value, sizeof(mohclass->dir));
				else if (!strcasecmp(tmp->name, "application"))
					ast_copy_string(mohclass->args, tmp->value, sizeof(mohclass->args));
				else if (!strcasecmp(tmp->name, "digit") && (isdigit(*tmp->value) || strchr("*#", *tmp->value)))
					mohclass->digit = *tmp->value;
				else if (!strcasecmp(tmp->name, "random"))
					ast_set2_flag(mohclass, ast_true(tmp->value), MOH_RANDOMIZE);
				else if (!strcasecmp(tmp->name, "sort") && !strcasecmp(tmp->value, "random"))
					ast_set_flag(mohclass, MOH_RANDOMIZE);
				else if (!strcasecmp(tmp->name, "sort") && !strcasecmp(tmp->value, "alpha")) 
					ast_set_flag(mohclass, MOH_SORTALPHA);
				else if (!strcasecmp(tmp->name, "format")) {
					ast_getformatbyname(tmp->value, &mohclass->format);
					if (!mohclass->format.id) {
						ast_log(LOG_WARNING, "Unknown format '%s' -- defaulting to SLIN\n", tmp->value);
						ast_format_set(&mohclass->format, AST_FORMAT_SLINEAR, 0);
					}
				}
			}
			ast_variables_destroy(var);
			if (ast_strlen_zero(mohclass->dir)) {
				if (!strcasecmp(mohclass->mode, "custom")) {
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
					if (state->class->realtime && !ast_test_flag(global_flags, MOH_CACHERTCLASSES) && !strcasecmp(mohclass->name, state->class->name)) {
						/* we found RT class with the same name, seems like we should continue playing existing one */
						/* XXX This code is impossible to reach */
						mohclass = mohclass_unref(mohclass, "unreffing potential mohclass (channel already has a class)");
						mohclass = state->class;
					}
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
				/* We don't register RT moh class, so let's init it manualy */

				time(&mohclass->start);
				mohclass->start -= respawn_time;

				if (!strcasecmp(mohclass->mode, "files")) {
					if (!moh_scan_files(mohclass)) {
						mohclass = mohclass_unref(mohclass, "unreffing potential mohclass (moh_scan_files failed)");
						return -1;
					}
					if (strchr(mohclass->args, 'r'))
						ast_set_flag(mohclass, MOH_RANDOMIZE);
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
							mohclass = state->class;
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
		}
	}

	if (!mohclass) {
		return -1;
	}

	ast_manager_event(chan, EVENT_FLAG_CALL, "MusicOnHold",
		"State: Start\r\n"
		"Channel: %s\r\n"
		"UniqueID: %s\r\n"
		"Class: %s\r\n",
		chan->name, chan->uniqueid,
		mohclass->name);

	ast_set_flag(chan, AST_FLAG_MOH);

	if (mohclass->total_files) {
		res = ast_activate_generator(chan, &moh_file_stream, mohclass);
	} else {
		res = ast_activate_generator(chan, &mohgen, mohclass);
	}

	mohclass = mohclass_unref(mohclass, "unreffing local reference to mohclass in local_ast_moh_start");

	return res;
}

static void local_ast_moh_stop(struct ast_channel *chan)
{
	ast_clear_flag(chan, AST_FLAG_MOH);
	ast_deactivate_generator(chan);

	ast_channel_lock(chan);
	if (chan->music_state) {
		if (chan->stream) {
			ast_closestream(chan->stream);
			chan->stream = NULL;
		}
	}

	ast_manager_event(chan, EVENT_FLAG_CALL, "MusicOnHold",
		"State: Stop\r\n"
		"Channel: %s\r\n"
		"UniqueID: %s\r\n",
		chan->name, chan->uniqueid);
	ast_channel_unlock(chan);
}

static void moh_class_destructor(void *obj)
{
	struct mohclass *class = obj;
	struct mohdata *member;
	pthread_t tid = 0;

	ast_debug(1, "Destroying MOH class '%s'\n", class->name);

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
		int bytes, tbytes = 0, stime = 0, pid = 0;

		ast_debug(1, "killing %d!\n", class->pid);

		stime = time(NULL) + 2;
		pid = class->pid;
		class->pid = 0;

		/* Back when this was just mpg123, SIGKILL was fine.  Now we need
		 * to give the process a reason and time enough to kill off its
		 * children. */
		do {
			if (killpg(pid, SIGHUP) < 0) {
				ast_log(LOG_WARNING, "Unable to send a SIGHUP to MOH process?!!: %s\n", strerror(errno));
			}
			usleep(100000);
			if (killpg(pid, SIGTERM) < 0) {
				if (errno == ESRCH) {
					break;
				}
				ast_log(LOG_WARNING, "Unable to terminate MOH process?!!: %s\n", strerror(errno));
			}
			usleep(100000);
			if (killpg(pid, SIGKILL) < 0) {
				if (errno == ESRCH) {
					break;
				}
				ast_log(LOG_WARNING, "Unable to kill MOH process?!!: %s\n", strerror(errno));
			}
		} while (0);

		while ((ast_wait_for_input(class->srcfd, 100) > 0) && 
				(bytes = read(class->srcfd, buff, 8192)) && time(NULL) < stime) {
			tbytes = tbytes + bytes;
		}

		ast_debug(1, "mpg123 pid %d and child died after %d bytes read\n", pid, tbytes);

		close(class->srcfd);
	}

	while ((member = AST_LIST_REMOVE_HEAD(&class->members, list))) {
		free(member);
	}

	if (class->filearray) {
		int i;
		for (i = 0; i < class->total_files; i++) {
			free(class->filearray[i]);
		}
		free(class->filearray);
		class->filearray = NULL;
	}

	if (class->timer) {
		ast_timer_close(class->timer);
		class->timer = NULL;
	}

	/* Finally, collect the exit status of the monitor thread */
	if (tid > 0) {
		pthread_join(tid, NULL);
	}
}

static int moh_class_mark(void *obj, void *arg, int flags)
{
	struct mohclass *class = obj;

	class->delete = 1;

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

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		if (ast_check_realtime("musiconhold") && reload) {
			ao2_t_callback(mohclasses, OBJ_NODATA, moh_class_mark, NULL, "Mark deleted classes");
			ao2_t_callback(mohclasses, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, moh_classes_delete_marked, NULL, "Purge marked classes");
		}
		return 0;
	}
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		moh_rescan_files();
		return 0;
	}

	if (reload) {
		ao2_t_callback(mohclasses, OBJ_NODATA, moh_class_mark, NULL, "Mark deleted classes");
	}

	ast_clear_flag(global_flags, AST_FLAGS_ALL);

	cat = ast_category_browse(cfg, NULL);
	for (; cat; cat = ast_category_browse(cfg, cat)) {
		/* Setup common options from [general] section */
		if (!strcasecmp(cat, "general")) {
			for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
				if (!strcasecmp(var->name, "cachertclasses")) {
					ast_set2_flag(global_flags, ast_true(var->value), MOH_CACHERTCLASSES);
				} else {
					ast_log(LOG_WARNING, "Unknown option '%s' in [general] section of musiconhold.conf\n", var->name);
				}
			}
		}
		/* These names were deprecated in 1.4 and should not be used until after the next major release. */
		if (!strcasecmp(cat, "classes") || !strcasecmp(cat, "moh_files") || 
				!strcasecmp(cat, "general")) {
			continue;
		}

		if (!(class = moh_class_malloc())) {
			break;
		}

		ast_copy_string(class->name, cat, sizeof(class->name));	
		for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
			if (!strcasecmp(var->name, "mode"))
				ast_copy_string(class->mode, var->value, sizeof(class->mode)); 
			else if (!strcasecmp(var->name, "directory"))
				ast_copy_string(class->dir, var->value, sizeof(class->dir));
			else if (!strcasecmp(var->name, "application"))
				ast_copy_string(class->args, var->value, sizeof(class->args));
			else if (!strcasecmp(var->name, "digit") && (isdigit(*var->value) || strchr("*#", *var->value)))
				class->digit = *var->value;
			else if (!strcasecmp(var->name, "random"))
				ast_set2_flag(class, ast_true(var->value), MOH_RANDOMIZE);
			else if (!strcasecmp(var->name, "sort") && !strcasecmp(var->value, "random"))
				ast_set_flag(class, MOH_RANDOMIZE);
			else if (!strcasecmp(var->name, "sort") && !strcasecmp(var->value, "alpha")) 
				ast_set_flag(class, MOH_SORTALPHA);
			else if (!strcasecmp(var->name, "format")) {
				ast_getformatbyname(var->value, &class->format);
				if (!class->format.id) {
					ast_log(LOG_WARNING, "Unknown format '%s' -- defaulting to SLIN\n", var->value);
					ast_format_set(&class->format, AST_FORMAT_SLINEAR, 0);
				}
			}
		}

		if (ast_strlen_zero(class->dir)) {
			if (!strcasecmp(class->mode, "custom")) {
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
	ao2_t_callback(mohclasses, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL, "Destroy callback");
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

	reload();

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
		int x;

		if (!class->total_files) {
			continue;
		}

		ast_cli(a->fd, "Class: %s\n", class->name);
		for (x = 0; x < class->total_files; x++) {
			ast_cli(a->fd, "\tFile: %s\n", class->filearray[x]);
		}
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
		if (ast_test_flag(class, MOH_CUSTOM)) {
			ast_cli(a->fd, "\tApplication: %s\n", S_OR(class->args, "<none>"));
		}
		if (strcasecmp(class->mode, "files")) {
			ast_cli(a->fd, "\tFormat: %s\n", ast_getformatname(&class->format));
		}
	}
	ao2_iterator_destroy(&i);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_moh[] = {
	AST_CLI_DEFINE(handle_cli_moh_reload,       "Reload MusicOnHold"),
	AST_CLI_DEFINE(handle_cli_moh_show_classes, "List MusicOnHold classes"),
	AST_CLI_DEFINE(handle_cli_moh_show_files,   "List MusicOnHold file-based classes")
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

static int load_module(void)
{
	int res;

	if (!(mohclasses = ao2_t_container_alloc(53, moh_class_hash, moh_class_cmp, "Moh class container"))) {
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
		res = ast_register_application_xml(wait_moh, wait_moh_exec);
	if (!res)
		res = ast_register_application_xml(set_moh, set_moh_exec);
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
	res |= ast_unregister_application(wait_moh);
	res |= ast_unregister_application(set_moh);
	res |= ast_unregister_application(start_moh);
	res |= ast_unregister_application(stop_moh);
	ast_cli_unregister_multiple(cli_moh, ARRAY_LEN(cli_moh));
	ast_unregister_atexit(ast_moh_destroy);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Music On Hold Resource",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
