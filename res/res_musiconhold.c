/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Routines implementing music on hold
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <../astconf.h>
#include <asterisk/options.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/say.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/musiconhold.h>
#include <asterisk/config.h>
#include <asterisk/utils.h>
#include <stdlib.h>
#include <asterisk/cli.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef ZAPATA_MOH
#ifdef __linux__
#include <linux/zaptel.h>
#else
#include <zaptel.h>
#endif /* __linux__ */
#endif
#include <unistd.h>
#include <sys/ioctl.h>
#define MAX_MOHFILES 512
#define MAX_MOHFILE_LEN 128

static char *app0 = "MusicOnHold";
static char *app1 = "WaitMusicOnHold";
static char *app2 = "SetMusicOnHold";

static char *synopsis0 = "Play Music On Hold indefinitely";
static char *synopsis1 = "Wait, playing Music On Hold";
static char *synopsis2 = "Set default Music On Hold class";

static char *descrip0 = "MusicOnHold(class): "
"Plays hold music specified by class.  If omitted, the default\n"
"music source for the channel will be used. Set the default \n"
"class with the SetMusicOnHold() application.\n"
"Returns -1 on hangup.\n"
"Never returns otherwise.\n";

static char *descrip1 = "WaitMusicOnHold(delay): "
"Plays hold music specified number of seconds.  Returns 0 when\n"
"done, or -1 on hangup.  If no hold music is available, the delay will\n"
"still occur with no sound.\n";

static char *descrip2 = "SetMusicOnHold(class): "
"Sets the default class for music on hold for a given channel.  When\n"
"music on hold is activated, this class will be used to select which\n"
"music is played.\n";

static int respawn_time = 20;

struct moh_files_state {
	struct mohclass *class;
	struct ast_filestream *stream;
	int origwfmt;
	int samples;
	int sample_queue;
	unsigned char pos;
	unsigned char save_pos;
};

struct mohclass {
	char class[80];
	char dir[256];
	char miscargs[256];
	char filearray[MAX_MOHFILES][MAX_MOHFILE_LEN];
	int total_files;
	int destroyme;
	int pid;		/* PID of mpg123 */
	int quiet;
	int single;
	int custom;
	int randomize;
	time_t start;
	pthread_t thread;
	struct mohdata *members;
	/* Source of audio */
	int srcfd;
	/* FD for timing source */
	int pseudofd;
	struct mohclass *next;
};

struct mohdata {
	int pipe[2];
	int origwfmt;
	struct mohclass *parent;
	struct mohdata *next;
};

static struct mohclass *mohclasses;

AST_MUTEX_DEFINE_STATIC(moh_lock);

#define LOCAL_MPG_123 "/usr/local/bin/mpg123"
#define MPG_123 "/usr/bin/mpg123"
#define MAX_MP3S 256

static void moh_files_release(struct ast_channel *chan, void *data)
{
	struct moh_files_state *state = chan->music_state;

	if (chan && state) {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Stopped music on hold on %s\n", chan->name);

		if (state->origwfmt && ast_set_write_format(chan, state->origwfmt)) {
			ast_log(LOG_WARNING, "Unable to restore channel '%s' to format '%d'\n", chan->name, state->origwfmt);
		}
		state->save_pos = state->pos + 1;
	}
}


static int ast_moh_files_next(struct ast_channel *chan) {
	struct moh_files_state *state = chan->music_state;

	if(state->save_pos) {
		state->pos = state->save_pos - 1;
		state->save_pos = 0;
	} else {
		state->samples = 0;
		if (chan->stream) {
			ast_closestream(chan->stream);
			chan->stream = NULL;
			state->pos++;
		}

		if (state->class->randomize) {
			srand(time(NULL)+getpid()+strlen(chan->name)-state->class->total_files);
			state->pos = rand();
		}
	}

	state->pos = state->pos % state->class->total_files;
	
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Unable to set '%s' to linear format (write)\n", chan->name);
		return -1;
	}
	if (!ast_openstream_full(chan, state->class->filearray[state->pos], chan->language, 1)) {
		ast_log(LOG_WARNING, "Unable to open file '%s': %s\n", state->class->filearray[state->pos], strerror(errno));
		state->pos++;
		return -1;
	}

	if (option_verbose > 2)
		ast_log(LOG_NOTICE, "%s Opened file %d '%s'\n", chan->name, state->pos, state->class->filearray[state->pos]);


	if (state->samples)
		ast_seekstream(chan->stream, state->samples, SEEK_SET);

	return state->pos;
}


static struct ast_frame *moh_files_readframe(struct ast_channel *chan) {
	struct ast_frame *f = NULL;
	
	if (!chan->stream || !(f = ast_readframe(chan->stream))) {
		if (ast_moh_files_next(chan) > -1)
			f = ast_readframe(chan->stream);
	}

	return f;
}

static int moh_files_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	struct moh_files_state *state = chan->music_state;
	struct ast_frame *f = NULL;
	int res = 0;
	state->sample_queue += samples;

	while(state->sample_queue > 0) {
		if ((f = moh_files_readframe(chan))) {
			state->samples += f->samples;
			res = ast_write(chan, f);
			ast_frfree(f);
			if(res < 0) {
				ast_log(LOG_WARNING, "Failed to write frame to '%s': %s\n", chan->name, strerror(errno));
				return -1;
			}
			state->sample_queue -= f->samples;
		} else
			return -1;	
	}
	return res;
}


static void *moh_files_alloc(struct ast_channel *chan, void *params)
{
	struct moh_files_state *state;
	struct mohclass *class = params;
	int allocated = 0;

	if ((!chan->music_state) && ((state = malloc(sizeof(struct moh_files_state))))) {
		chan->music_state = state;
		allocated = 1;
	} else 
		state = chan->music_state;


	if(state) {
		if (allocated || state->class != class) {
			/* initialize */
			memset(state, 0, sizeof(struct moh_files_state));
			state->class = class;
		}

		state->origwfmt = chan->writeformat;

		if (ast_set_write_format(chan, AST_FORMAT_SLINEAR)) {
			ast_log(LOG_WARNING, "Unable to set '%s' to linear format (write)\n", chan->name);
			free(chan->music_state);
			chan->music_state = NULL;
		} else {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Started music on hold, class '%s', on %s\n", (char *)params, chan->name);
		}


	}
	
	return chan->music_state;
}

static struct ast_generator moh_file_stream = 
{
	alloc: moh_files_alloc,
	release: moh_files_release,
	generate: moh_files_generator,
};

static int spawn_mp3(struct mohclass *class)
{
	int fds[2];
	int files=0;
	char fns[MAX_MP3S][80];
	char *argv[MAX_MP3S + 50];
	char xargs[256];
	char *argptr;
	int argc = 0;
	DIR *dir;
	struct dirent *de;

	
	dir = opendir(class->dir);
	if (!dir && !strstr(class->dir,"http://") && !strstr(class->dir,"HTTP://")) {
		ast_log(LOG_WARNING, "%s is not a valid directory\n", class->dir);
		return -1;
	}

	if (!class->custom) {
		argv[argc++] = "mpg123";
		argv[argc++] = "-q";
		argv[argc++] = "-s";
		argv[argc++] = "--mono";
		argv[argc++] = "-r";
		argv[argc++] = "8000";
		
		if (!class->single) {
			argv[argc++] = "-b";
			argv[argc++] = "2048";
		}
		
		argv[argc++] = "-f";
		
		if (class->quiet)
			argv[argc++] = "4096";
		else
			argv[argc++] = "8192";
		
		/* Look for extra arguments and add them to the list */
		strncpy(xargs, class->miscargs, sizeof(xargs) - 1);
		argptr = xargs;
		while(argptr && !ast_strlen_zero(argptr)) {
			argv[argc++] = argptr;
			argptr = strchr(argptr, ',');
			if (argptr) {
				*argptr = '\0';
				argptr++;
			}
		}
	} else  {
		/* Format arguments for argv vector */
		strncpy(xargs, class->miscargs, sizeof(xargs) - 1);
		argptr = xargs;
		while(argptr && !ast_strlen_zero(argptr)) {
			argv[argc++] = argptr;
			argptr = strchr(argptr, ' ');
			if (argptr) {
				*argptr = '\0';
				argptr++;
			}
		}
	}

	files = 0;
	if (strstr(class->dir,"http://") || strstr(class->dir,"HTTP://")) {
		strncpy(fns[files], class->dir, sizeof(fns[files]) - 1);
		argv[argc++] = fns[files];
		files++;
	} else {
		while((de = readdir(dir)) && (files < MAX_MP3S)) {
			if ((strlen(de->d_name) > 3) && 
				((class->custom && 
				  (!strcasecmp(de->d_name + strlen(de->d_name) - 4, ".raw") || 
				   !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".sln")))
				 || !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".mp3"))) {
				strncpy(fns[files], de->d_name, sizeof(fns[files]) - 1);
				argv[argc++] = fns[files];
				files++;
			}
		}
	}
	argv[argc] = NULL;
	closedir(dir);
	
	if (pipe(fds)) {	
		ast_log(LOG_WARNING, "Pipe failed\n");
		return -1;
	}
#if 0
	printf("%d files total, %d args total\n", files, argc);
	{
		int x;
		for (x=0;argv[x];x++)
			printf("arg%d: %s\n", x, argv[x]);
	}
#endif	
	if (!files) {
		ast_log(LOG_WARNING, "Found no files in '%s'\n", class->dir);
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if (time(NULL) - class->start < respawn_time) {
		sleep(respawn_time - (time(NULL) - class->start));
	}
	time(&class->start);
	class->pid = fork();
	if (class->pid < 0) {
		close(fds[0]);
		close(fds[1]);
		ast_log(LOG_WARNING, "Fork failed: %s\n", strerror(errno));
		return -1;
	}
	if (!class->pid) {
		int x;
		close(fds[0]);
		/* Stdout goes to pipe */
		dup2(fds[1], STDOUT_FILENO);
		/* Close unused file descriptors */
		for (x=3;x<8192;x++) {
			if (-1 != fcntl(x, F_GETFL)) {
				close(x);
			}
		}
        /* Child */
		chdir(class->dir);
		if(class->custom) {
			execv(argv[0], argv);
        } else {
            /* Default install is /usr/local/bin */
            execv(LOCAL_MPG_123, argv);
            /* Many places have it in /usr/bin */
            execv(MPG_123, argv);
            /* Check PATH as a last-ditch effort */
            execvp("mpg123", argv);
        }
		ast_log(LOG_WARNING, "Exec failed: %s\n", strerror(errno));
		close(fds[1]);
		exit(1);
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
	char buf[8192];
	short sbuf[8192];
	int res, res2;
	struct timeval tv;
	struct timeval tv_tmp;
	long error_sec, error_usec;
	long delay;

	tv_tmp.tv_sec = 0;
	tv_tmp.tv_usec = 0;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	error_sec = 0;
	error_usec = 0;
	for(;/* ever */;) {
		/* Spawn mp3 player if it's not there */
		if (class->srcfd < 0) {
			if ((class->srcfd = spawn_mp3(class)) < 0) {
				ast_log(LOG_WARNING, "unable to spawn mp3player\n");
				/* Try again later */
				sleep(500);
			}
		}
		if (class->pseudofd > -1) {
			/* Pause some amount of time */
			res = read(class->pseudofd, buf, sizeof(buf));
		} else {
			/* Reliable sleep */
			if (gettimeofday(&tv_tmp, NULL) < 0) {
				ast_log(LOG_NOTICE, "gettimeofday() failed!\n");
				return NULL;
			}
			if (((unsigned long)(tv.tv_sec) > 0)&&((unsigned long)(tv.tv_usec) > 0)) {
				if ((unsigned long)(tv_tmp.tv_usec) < (unsigned long)(tv.tv_usec)) {
					tv_tmp.tv_usec += 1000000;
					tv_tmp.tv_sec -= 1;
				}
				error_sec = (unsigned long)(tv_tmp.tv_sec) - (unsigned long)(tv.tv_sec);
				error_usec = (unsigned long)(tv_tmp.tv_usec) - (unsigned long)(tv.tv_usec);
			} else {
				error_sec = 0;
				error_usec = 0;
			}
			if (error_sec * 1000 + error_usec / 1000 < MOH_MS_INTERVAL) {
				tv.tv_sec = tv_tmp.tv_sec + (MOH_MS_INTERVAL/1000 - error_sec);
				tv.tv_usec = tv_tmp.tv_usec + ((MOH_MS_INTERVAL % 1000) * 1000 - error_usec);
				delay = (MOH_MS_INTERVAL/1000 - error_sec) * 1000 +
							((MOH_MS_INTERVAL % 1000) * 1000 - error_usec) / 1000;
			} else {
				ast_log(LOG_NOTICE, "Request to schedule in the past?!?!\n");
				tv.tv_sec = tv_tmp.tv_sec;
				tv.tv_usec = tv_tmp.tv_usec;
				delay = 0;
			}
			if (tv.tv_usec > 1000000) {
				tv.tv_sec++;
				tv.tv_usec-= 1000000;
			}
			if (delay > 0)
				usleep(delay * 1000);
			res = 800;		/* 800 samples */
		}
		if (!class->members)
			continue;
		/* Read mp3 audio */
		if ((res2 = read(class->srcfd, sbuf, res * 2)) != res * 2) {
			if (!res2) {
				close(class->srcfd);
				class->srcfd = -1;
				if (class->pid) {
					kill(class->pid, SIGKILL);
					class->pid = 0;
				}
			} else
				ast_log(LOG_DEBUG, "Read %d bytes of audio while expecting %d\n", res2, res * 2);
			continue;
		}
		ast_mutex_lock(&moh_lock);
		moh = class->members;
		while(moh) {
			/* Write data */
			if ((res = write(moh->pipe[1], sbuf, res2)) != res2) 
				if (option_debug)
					ast_log(LOG_DEBUG, "Only wrote %d of %d bytes to pipe\n", res, res2);
			moh = moh->next;
		}
		ast_mutex_unlock(&moh_lock);
	}
	return NULL;
}

static int moh0_exec(struct ast_channel *chan, void *data)
{
	if (ast_moh_start(chan, data)) {
		ast_log(LOG_WARNING, "Unable to start music on hold (class '%s') on channel %s\n", (char *)data, chan->name);
		return -1;
	}
	while(!ast_safe_sleep(chan, 10000));
	return -1;
}

static int moh1_exec(struct ast_channel *chan, void *data)
{
	int res;
	if (!data || !atoi(data)) {
		ast_log(LOG_WARNING, "WaitMusicOnHold requires an argument (number of seconds to wait)\n");
		return -1;
	}
	if (ast_moh_start(chan, NULL)) {
		ast_log(LOG_WARNING, "Unable to start music on hold (class '%s') on channel %s\n", (char *)data, chan->name);
		return -1;
	}
	res = ast_safe_sleep(chan, atoi(data) * 1000);
	ast_moh_stop(chan);
	return res;
}

static int moh2_exec(struct ast_channel *chan, void *data)
{
	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SetMusicOnHold requires an argument (class)\n");
		return -1;
	}
	strncpy(chan->musicclass, data, sizeof(chan->musicclass) - 1);
	return 0;
}

static struct mohclass *get_mohbyname(char *name)
{
	struct mohclass *moh;
	moh = mohclasses;
	while(moh) {
		if (!strcasecmp(name, moh->class))
			return moh;
		moh = moh->next;
	}
	return NULL;
}

static struct mohdata *mohalloc(struct mohclass *cl)
{
	struct mohdata *moh;
	long flags;
	moh = malloc(sizeof(struct mohdata));
	if (!moh)
		return NULL;
	memset(moh, 0, sizeof(struct mohdata));
	if (pipe(moh->pipe)) {
		ast_log(LOG_WARNING, "Failed to create pipe: %s\n", strerror(errno));
		free(moh);
		return NULL;
	}
	/* Make entirely non-blocking */
	flags = fcntl(moh->pipe[0], F_GETFL);
	fcntl(moh->pipe[0], F_SETFL, flags | O_NONBLOCK);
	flags = fcntl(moh->pipe[1], F_GETFL);
	fcntl(moh->pipe[1], F_SETFL, flags | O_NONBLOCK);
	moh->parent = cl;
	moh->next = cl->members;
	cl->members = moh;
	return moh;
}

static void moh_release(struct ast_channel *chan, void *data)
{
	struct mohdata *moh = data, *prev, *cur;
	int oldwfmt;
	ast_mutex_lock(&moh_lock);
	/* Unlink */
	prev = NULL;
	cur = moh->parent->members;
	while(cur) {
		if (cur == moh) {
			if (prev)
				prev->next = cur->next;
			else
				moh->parent->members = cur->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	ast_mutex_unlock(&moh_lock);
	close(moh->pipe[0]);
	close(moh->pipe[1]);
	oldwfmt = moh->origwfmt;
	free(moh);
	if (chan) {
		if (oldwfmt && ast_set_write_format(chan, oldwfmt)) 
			ast_log(LOG_WARNING, "Unable to restore channel '%s' to format %s\n", chan->name, ast_getformatname(oldwfmt));
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Stopped music on hold on %s\n", chan->name);
	}
}

static void *moh_alloc(struct ast_channel *chan, void *params)
{
	struct mohdata *res;
	struct mohclass *class;
	class = params;
	if (class)
		res = mohalloc(class);
	else {
		if (strcasecmp(params, "default"))
			ast_log(LOG_WARNING, "No class: %s\n", (char *)params);
		res = NULL;
	}
	if (res) {
		res->origwfmt = chan->writeformat;
		if (ast_set_write_format(chan, AST_FORMAT_SLINEAR)) {
			ast_log(LOG_WARNING, "Unable to set '%s' to signed linear format\n", chan->name);
			moh_release(NULL, res);
			res = NULL;
		}
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Started music on hold, class '%s', on %s\n", (char *)params, chan->name);
	}
	return res;
}

static int moh_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	struct ast_frame f;
	struct mohdata *moh = data;
	short buf[1280 + AST_FRIENDLY_OFFSET / 2];
	int res;
	if(!moh->parent->pid)
		return - 1;
	len = samples * 2;
	if (len > sizeof(buf) - AST_FRIENDLY_OFFSET) {
		ast_log(LOG_WARNING, "Only doing %d of %d requested bytes on %s\n", (int)sizeof(buf), (int)len, chan->name);
		len = sizeof(buf) - AST_FRIENDLY_OFFSET;
	}
	res = read(moh->pipe[0], buf + AST_FRIENDLY_OFFSET/2, len);
#if 0
	if (res != len) {
		ast_log(LOG_WARNING, "Read only %d of %d bytes: %s\n", res, len, strerror(errno));
	}
#endif
	if (res > 0) {
		memset(&f, 0, sizeof(f));
		f.frametype = AST_FRAME_VOICE;
		f.subclass = AST_FORMAT_SLINEAR;
		f.mallocd = 0;
		f.datalen = res;
		f.samples = res / 2;
		f.data = buf + AST_FRIENDLY_OFFSET / 2;
		f.offset = AST_FRIENDLY_OFFSET;
		if (ast_write(chan, &f)< 0) {
			ast_log(LOG_WARNING, "Failed to write frame to '%s': %s\n", chan->name, strerror(errno));
			return -1;
		}
	}
	return 0;
}

static struct ast_generator mohgen = 
{
	alloc: moh_alloc,
	release: moh_release,
	generate: moh_generate,
};

static int moh_scan_files(struct mohclass *class) {

	DIR *files_DIR;
	struct dirent *files_dirent;
	char path[512];
	char filepath[MAX_MOHFILE_LEN];
	char *ext;
	struct stat statbuf;
	int dirnamelen;
	int i;
	
	files_DIR = opendir(class->dir);
	if (!files_DIR) {
		ast_log(LOG_WARNING, "Cannot open dir %s or dir does not exist", class->dir);
		return -1;
	}

	class->total_files = 0;
	dirnamelen = strlen(class->dir) + 2;
	getcwd(path, 512);
	chdir(class->dir);
	memset(class->filearray, 0, MAX_MOHFILES*MAX_MOHFILE_LEN);

	while ((files_dirent = readdir(files_DIR))) {
		if ((strlen(files_dirent->d_name) < 4) || ((strlen(files_dirent->d_name) + dirnamelen) >= MAX_MOHFILE_LEN))
			continue;

		snprintf(filepath, MAX_MOHFILE_LEN, "%s/%s", class->dir, files_dirent->d_name);

		if (stat(filepath, &statbuf))
			continue;

		if (!S_ISREG(statbuf.st_mode))
			continue;

		if ((ext = strrchr(filepath, '.'))) {
			*ext = '\0';
			ext++;
		}

		/* check to see if this file's format can be opened */
		if (ast_fileexists(filepath, ext, NULL) == -1)
			continue;

		/* if the file is present in multiple formats, ensure we only put it into the list once */
		for (i = 0; i < class->total_files; i++)
			if (!strcmp(filepath, class->filearray[i]))
				break;

		if (i == class->total_files)
			strcpy(class->filearray[class->total_files++], filepath);
	}

	closedir(files_DIR);
	chdir(path);
	return class->total_files;
}

static int moh_register(char *classname, char *mode, char *param, char *miscargs)
{
	struct mohclass *moh;
#ifdef ZAPATA_MOH
	int x;
#endif
	ast_mutex_lock(&moh_lock);
	moh = get_mohbyname(classname);
	ast_mutex_unlock(&moh_lock);
	if (moh) {
		ast_log(LOG_WARNING, "Music on Hold '%s' already exists\n", classname);
		return -1;
	}
	moh = malloc(sizeof(struct mohclass));
	if (!moh)
		return -1;
	memset(moh, 0, sizeof(struct mohclass));
	time(&moh->start);
	moh->start -= respawn_time;
	strncpy(moh->class, classname, sizeof(moh->class) - 1);
	if (miscargs) {
		strncpy(moh->miscargs, miscargs, sizeof(moh->miscargs) - 1);
		if (strchr(miscargs,'r'))
			moh->randomize=1;
	}
	if (!strcasecmp(mode, "files")) {
		if (param)
			strncpy(moh->dir, param, sizeof(moh->dir) - 1);
		if (!moh_scan_files(moh)) {
			free(moh);
			return -1;
		}
	} else if (!strcasecmp(mode, "mp3") || !strcasecmp(mode, "mp3nb") || !strcasecmp(mode, "quietmp3") || !strcasecmp(mode, "quietmp3nb") || !strcasecmp(mode, "httpmp3") || !strcasecmp(mode, "custom")) {

		if (param)
			strncpy(moh->dir, param, sizeof(moh->dir) - 1);

		if (!strcasecmp(mode, "custom"))
			moh->custom = 1;
		else if (!strcasecmp(mode, "mp3nb") || !strcasecmp(mode, "quietmp3nb"))
			moh->single = 1;
		else if (!strcasecmp(mode, "quietmp3") || !strcasecmp(mode, "quietmp3nb"))
			moh->quiet = 1;
		
		moh->srcfd = -1;
#ifdef ZAPATA_MOH
		/* It's an MP3 Moh -- Open /dev/zap/pseudo for timing...  Is
		   there a better, yet reliable way to do this? */
		moh->pseudofd = open("/dev/zap/pseudo", O_RDONLY);
		if (moh->pseudofd < 0) {
			ast_log(LOG_WARNING, "Unable to open pseudo channel for timing...  Sound may be choppy.\n");
		} else {
			x = 320;
			ioctl(moh->pseudofd, ZT_SET_BLOCKSIZE, &x);
		}
#else
		moh->pseudofd = -1;
#endif
		if (ast_pthread_create(&moh->thread, NULL, monmp3thread, moh)) {
			ast_log(LOG_WARNING, "Unable to create moh...\n");
			if (moh->pseudofd > -1)
				close(moh->pseudofd);
			free(moh);
			return -1;
		}
	} else {
		ast_log(LOG_WARNING, "Don't know how to do a mode '%s' music on hold\n", mode);
		free(moh);
		return -1;
	}
	ast_mutex_lock(&moh_lock);
	moh->next = mohclasses;
	mohclasses = moh;
	ast_mutex_unlock(&moh_lock);
	return 0;
}

static void local_ast_moh_cleanup(struct ast_channel *chan)
{
	if(chan->music_state) {
		free(chan->music_state);
		chan->music_state = NULL;
	}
}

static int local_ast_moh_start(struct ast_channel *chan, char *class)
{
	struct mohclass *mohclass;

	if (!class || ast_strlen_zero(class))
		class = chan->musicclass;
	if (!class || ast_strlen_zero(class))
		class = "default";
	ast_mutex_lock(&moh_lock);
	mohclass = get_mohbyname(class);
	ast_mutex_unlock(&moh_lock);

	if (!mohclass) {
		ast_log(LOG_WARNING, "No class: %s\n", (char *)class);
		return -1;
	}

	ast_set_flag(chan, AST_FLAG_MOH);
	if (mohclass->total_files) {
		return ast_activate_generator(chan, &moh_file_stream, mohclass);
	} else
		return ast_activate_generator(chan, &mohgen, mohclass);
}

static void local_ast_moh_stop(struct ast_channel *chan)
{
	ast_clear_flag(chan, AST_FLAG_MOH);
	ast_deactivate_generator(chan);

	if(chan->music_state) {
		if(chan->stream) {
			ast_closestream(chan->stream);
			chan->stream = NULL;
		}
	}
}

static int load_moh_classes(void)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	char *data;
	char *args;
	int x = 0;
	cfg = ast_load("musiconhold.conf");
	if (cfg) {
		var = ast_variable_browse(cfg, "classes");
		while(var) {
			data = strchr(var->value, ':');
			if (data) {
				*data = '\0';
				data++;
				args = strchr(data, ',');
				if (args) {
					*args = '\0';
					args++;
				}
				if(!(get_mohbyname(var->name))) {
					moh_register(var->name, var->value, data, args);
					x++;
				}
			}
			var = var->next;
		}
		var = ast_variable_browse(cfg, "moh_files");
		while(var) {
			if(!(get_mohbyname(var->name))) {
				args = strchr(var->value, ',');
				if (args) {
					*args = '\0';
					args++;
				}
				moh_register(var->name, "files", var->value, args);
				x++;
			}
			var = var->next;
		}

		ast_destroy(cfg);
	}
	return x;
}

static void ast_moh_destroy(void)
{
	struct mohclass *moh,*tmp;
	char buff[8192];
	int bytes, tbytes=0, stime = 0, pid = 0;
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Destroying musiconhold processes\n");
	ast_mutex_lock(&moh_lock);
	moh = mohclasses;

	while(moh) {
		if (moh->pid) {
			ast_log(LOG_DEBUG, "killing %d!\n", moh->pid);
			stime = time(NULL);
			pid = moh->pid;
			moh->pid = 0;
			kill(pid, SIGKILL);
			while ((bytes = read(moh->srcfd, buff, 8192)) && time(NULL) < stime + 5) {
				tbytes = tbytes + bytes;
			}
			ast_log(LOG_DEBUG, "mpg123 pid %d and child died after %d bytes read\n", pid, tbytes);
			close(moh->srcfd);
		}
		tmp = moh;
		moh = moh->next;
		free(tmp);
	}
	mohclasses = NULL;
	ast_mutex_unlock(&moh_lock);
}


static void moh_on_off(int on) {
	struct ast_channel *chan = ast_channel_walk_locked(NULL);
	while(chan) {
		if(ast_test_flag(chan, AST_FLAG_MOH)) {
			if(on)
				local_ast_moh_start(chan,NULL);
			else
				ast_deactivate_generator(chan);
		}
		ast_mutex_unlock(&chan->lock);
		chan = ast_channel_walk_locked(chan);
	}
}

static int moh_cli(int fd, int argc, char *argv[]) 
{
	int x = 0;
	moh_on_off(0);
	ast_moh_destroy();
	x = load_moh_classes();
	moh_on_off(1);
	ast_cli(fd,"\n%d class%s reloaded.\n",x,x == 1 ? "" : "es");
	return 0;
}

static int cli_files_show(int fd, int argc, char *argv[])
{
	int i;
	struct mohclass *class;

	ast_mutex_lock(&moh_lock);
	for (class = mohclasses; class; class = class->next) {
		if (!class->total_files)
			continue;

		ast_cli(fd, "Class: %s\n", class->class);
		for (i = 0; i < class->total_files; i++)
			ast_cli(fd, "\tFile: %s\n", class->filearray[i]);
	}
	ast_mutex_unlock(&moh_lock);

	return 0;
}

static struct ast_cli_entry  cli_moh = { { "moh", "reload"}, moh_cli, "Music On Hold", "Music On Hold", NULL};

static struct ast_cli_entry  cli_moh_files_show = { { "moh", "files", "show"}, cli_files_show, "List MOH file-based classes", "Lists all loaded file-based MOH classes and their files", NULL};


int load_module(void)
{
	int res;
	load_moh_classes();
	ast_install_music_functions(local_ast_moh_start, local_ast_moh_stop, local_ast_moh_cleanup);
	res = ast_register_application(app0, moh0_exec, synopsis0, descrip0);
	ast_register_atexit(ast_moh_destroy);
	ast_cli_register(&cli_moh);
	ast_cli_register(&cli_moh_files_show);
	if (!res)
		res = ast_register_application(app1, moh1_exec, synopsis1, descrip1);
	if (!res)
		res = ast_register_application(app2, moh2_exec, synopsis2, descrip2);

	return res;
}

int reload(void)
{
    struct mohclass *moh = mohclasses;
    load_moh_classes();
    while(moh) {
        if (moh->total_files)
            moh_scan_files(moh);
        moh = moh->next;
    }
    return 0;
}


int unload_module(void)
{
	return -1;
}

char *description(void)
{
	return "Music On Hold Resource";
}

int usecount(void)
{
	/* Never allow Music On Hold to be unloaded
	   unresolve needed symbols in the dialer */
#if 0
	int res;
	STANDARD_USECOUNT(res);
	return res;
#else
	return 1;
#endif
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
