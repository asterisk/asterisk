/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Routines implementing music on hold
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/say.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/musiconhold.h>
#include <asterisk/config.h>
#include <asterisk/utils.h>
#include <stdlib.h>
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

#include <pthread.h>

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

struct mohclass {
	char class[80];
	char dir[256];
	char miscargs[256];
	int destroyme;
	int pid;		/* PID of mpg123 */
	int quiet;
	int single;
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

static int spawn_mp3(struct mohclass *class)
{
	int fds[2];
	int files;
	char fns[MAX_MP3S][80];
	char *argv[MAX_MP3S + 50];
	char xargs[256];
	char *argptr;
	int argc = 0;
	DIR *dir;
	struct dirent *de;
	dir = opendir(class->dir);
	if (!dir) {
		ast_log(LOG_WARNING, "%s is not a valid directory\n", class->dir);
		return -1;
 	}
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
	
	if (class->quiet) {
		argv[argc++] = "4096";
	} else
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

	files = 0;
	while((de = readdir(dir)) && (files < MAX_MP3S)) {
		if ((strlen(de->d_name) > 3) && !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".mp3")) {
			strncpy(fns[files], de->d_name, sizeof(fns[files]));
			argv[argc++] = fns[files];
			files++;
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
		for (x=3;x<8192;x++)
			close(x);
		/* Child */
		chdir(class->dir);
		/* Default install is /usr/local/bin */
		execv(LOCAL_MPG_123, argv);
		/* Many places have it in /usr/bin */
		execv(MPG_123, argv);
		/* Check PATH as a last-ditch effort */
		execvp("mpg123", argv);
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
		if (class->srcfd < 0)  {
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
	strncpy(chan->musicclass, data, sizeof(chan->musicclass));
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
	ast_mutex_lock(&moh_lock);
	class = get_mohbyname(params);
	if (class)
		res = mohalloc(class);
	else {
		if (strcasecmp(params, "default"))
			ast_log(LOG_WARNING, "No class: %s\n", (char *)params);
		res = NULL;
	}
	ast_mutex_unlock(&moh_lock);
	if (res) {
		res->origwfmt = chan->writeformat;
		if (ast_set_write_format(chan, AST_FORMAT_SLINEAR)) {
			ast_log(LOG_WARNING, "Unable to set '%s' to signed linear format\n", chan->name);
			moh_release(NULL, res);
			res = NULL;
		}
#if 0
		/* Allow writes to interrupt */
		chan->writeinterrupt = 1;
#endif		
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

	len = samples * 2;
	if (len > sizeof(buf) - AST_FRIENDLY_OFFSET) {
		ast_log(LOG_WARNING, "Only doing %d of %d requested bytes on %s\n", sizeof(buf), len, chan->name);
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

	strncpy(moh->class, classname, sizeof(moh->class) - 1);
	if (miscargs)
		strncpy(moh->miscargs, miscargs, sizeof(moh->miscargs) - 1);
	if (!strcasecmp(mode, "mp3") || !strcasecmp(mode, "mp3nb") || !strcasecmp(mode, "quietmp3") || !strcasecmp(mode, "quietmp3nb") || !strcasecmp(mode, "httpmp3")) {
		if (!strcasecmp(mode, "mp3nb") || !strcasecmp(mode, "quietmp3nb"))
			moh->single = 1;
		if (!strcasecmp(mode, "quietmp3") || !strcasecmp(mode, "quietmp3nb"))
			moh->quiet = 1;
		strncpy(moh->dir, param, sizeof(moh->dir) - 1);
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
		if (pthread_create(&moh->thread, NULL, monmp3thread, moh)) {
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

int ast_moh_start(struct ast_channel *chan, char *class)
{
	if (!class || ast_strlen_zero(class))
		class = chan->musicclass;
	if (!class || ast_strlen_zero(class))
		class = "default";
	return ast_activate_generator(chan, &mohgen, class);
}

void ast_moh_stop(struct ast_channel *chan)
{
	ast_deactivate_generator(chan);
}

static void load_moh_classes(void)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	char *data;
	char *args;
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
				moh_register(var->name, var->value, data,args);
			}
			var = var->next;
		}
		ast_destroy(cfg);
	}
}

static void ast_moh_destroy(void)
{
	struct mohclass *moh;
	char buff[8192];
	int bytes, tbytes=0, stime = 0;
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Destroying any remaining musiconhold processes\n");
	ast_mutex_lock(&moh_lock);
	moh = mohclasses;
	while(moh) {
		if (moh->pid) {
			ast_log(LOG_DEBUG, "killing %d!\n", moh->pid);
			stime = time(NULL);
			kill(moh->pid, SIGKILL);
			while ((bytes = read(moh->srcfd, buff, 8192)) && time(NULL) < stime + 5) {
				tbytes = tbytes + bytes;
			}
			ast_log(LOG_DEBUG, "mpg123 pid %d and child died after %d bytes read\n", moh->pid, tbytes);
			close(moh->srcfd);
			moh->pid = 0;
		}
		moh = moh->next;
	}
	ast_mutex_unlock(&moh_lock);
}

int load_module(void)
{
	int res;
	load_moh_classes();
	res = ast_register_application(app0, moh0_exec, synopsis0, descrip0);
	ast_register_atexit(ast_moh_destroy);
	if (!res)
		res = ast_register_application(app1, moh1_exec, synopsis1, descrip1);
	if (!res)
		res = ast_register_application(app2, moh2_exec, synopsis2, descrip2);
	return res;
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
