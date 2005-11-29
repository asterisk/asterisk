/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Generic File Format Support.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/frame.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/sched.h>
#include <asterisk/options.h>
#include <asterisk/translate.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include "asterisk.h"

struct ast_format {
	/* Name of format */
	char name[80];
	/* Extensions (separated by | if more than one) 
	   this format can read.  First is assumed for writing (e.g. .mp3) */
	char exts[80];
	/* Format of frames it uses/provides (one only) */
	int format;
	/* Open an input stream, and start playback */
	struct ast_filestream * (*open)(int fd);
	/* Open an output stream, of a given file descriptor and comment it appropriately if applicable */
	struct ast_filestream * (*rewrite)(int fd, char *comment);
	/* Apply a reading filestream to a channel */
	int (*apply)(struct ast_channel *, struct ast_filestream *);
	/* Write a frame to a channel */
	int (*write)(struct ast_filestream *, struct ast_frame *);
	/* Read the next frame from the filestream (if available) */
	struct ast_frame * (*read)(struct ast_filestream *);
	/* Close file, and destroy filestream structure */
	void (*close)(struct ast_filestream *);
	/* Retrieve file comment */
	char * (*getcomment)(struct ast_filestream *);
	/* Link */
	struct ast_format *next;
};

struct ast_filestream {
	/* Everybody reserves a block of AST_RESERVED_POINTERS pointers for us */
	struct ast_format *fmt;
	/* Transparently translate from another format -- just once */
	struct ast_trans_pvt *trans;
	struct ast_tranlator_pvt *tr;
};

static pthread_mutex_t formatlock = PTHREAD_MUTEX_INITIALIZER;

static struct ast_format *formats = NULL;

int ast_format_register(char *name, char *exts, int format,
						struct ast_filestream * (*open)(int fd),
						struct ast_filestream * (*rewrite)(int fd, char *comment),
						int (*apply)(struct ast_channel *, struct ast_filestream *),
						int (*write)(struct ast_filestream *, struct ast_frame *),
						struct ast_frame * (*read)(struct ast_filestream *),
						void (*close)(struct ast_filestream *),
						char * (*getcomment)(struct ast_filestream *))
{
	struct ast_format *tmp;
	if (pthread_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return -1;
	}
	tmp = formats;
	while(tmp) {
		if (!strcasecmp(name, tmp->name)) {
			pthread_mutex_unlock(&formatlock);
			ast_log(LOG_WARNING, "Tried to register '%s' format, already registered\n", name);
			return -1;
		}
		tmp = tmp->next;
	}
	tmp = malloc(sizeof(struct ast_format));
	if (!tmp) {
		ast_log(LOG_WARNING, "Out of memory\n");
		pthread_mutex_unlock(&formatlock);
		return -1;
	}
	strncpy(tmp->name, name, sizeof(tmp->name));
	strncpy(tmp->exts, exts, sizeof(tmp->exts));
	tmp->open = open;
	tmp->rewrite = rewrite;
	tmp->apply = apply;
	tmp->read = read;
	tmp->write = write;
	tmp->close = close;
	tmp->format = format;
	tmp->getcomment = getcomment;
	tmp->next = formats;
	formats = tmp;
	pthread_mutex_unlock(&formatlock);
	if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Registered file format %s, extension(s) %s\n", name, exts);
	return 0;
}

int ast_format_unregister(char *name)
{
	struct ast_format *tmp, *tmpl = NULL;
	if (pthread_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return -1;
	}
	tmp = formats;
	while(tmp) {
		if (!strcasecmp(name, tmp->name)) {
			if (tmpl) 
				tmpl->next = tmp->next;
			else
				formats = tmp->next;
			free(tmp);
			pthread_mutex_unlock(&formatlock);
			if (option_verbose > 1)
				ast_verbose( VERBOSE_PREFIX_2 "Unregistered format %s\n", name);
			return 0;
		}
		tmp = tmp->next;
	}
	ast_log(LOG_WARNING, "Tried to unregister format %s, already unregistered\n", name);
	return -1;
}

int ast_stopstream(struct ast_channel *tmp)
{
	if (tmp->trans)
		tmp = tmp->trans;
	/* Stop a running stream if there is one */
	if (!tmp->stream) 
		return 0;
	tmp->stream->fmt->close(tmp->stream);
	if (tmp->master) {
		ast_translator_destroy(tmp);
	}
	return 0;
}

int ast_closestream(struct ast_filestream *f)
{
	if (f->trans) {
		ast_translator_free_path(f->trans);
	}
	/* Stop a running stream if there is one */
	f->fmt->close(f);
	return 0;
}

int ast_writestream(struct ast_filestream *fs, struct ast_frame *f)
{
	struct ast_frame_chain *fc, *f2;
	int res = -1;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Tried to write non-voice frame\n");
		return -1;
	}
	if ((fs->fmt->format & f->subclass) == f->subclass) {
		res =  fs->fmt->write(fs, f);
		if (res < 0) 
			ast_log(LOG_WARNING, "Natural write failed\n");
		if (res > 0)
			ast_log(LOG_WARNING, "Huh??\n");
		return res;
	} else {
		/* XXX If they try to send us a type of frame that isn't the normal frame, and isn't
		       the one we've setup a translator for, we do the "wrong thing" XXX */
		if (!fs->trans) 
			fs->trans = ast_translator_build_path(f->subclass, fs->fmt->format);
		if (!fs->trans)
			ast_log(LOG_WARNING, "Unable to translate to format %s, source format %d\n", fs->fmt->name, f->subclass);
		else {
			res = 0;
			/* Build a chain of translated frames */
			fc = ast_translate(fs->trans, f);
			f2 = fc;
			while(f2) {
				res = fs->fmt->write(fs, f2->fr);
				if (res) {
					ast_log(LOG_WARNING, "Translated frame write failed\n");
					break;
				}
				f2 = f2->next;
			}
			if (fc)
				ast_frchain(fc);
		}
		return res;
	}
}

static char *build_filename(char *filename, char *ext)
{
	char *fn;
	fn = malloc(strlen(AST_SOUNDS) + strlen(filename) + strlen(ext) + 10);
	if (fn) {
		if (filename[0] == '/') 
			sprintf(fn, "%s.%s", filename, ext);
		else
			sprintf(fn, "%s/%s.%s", AST_SOUNDS, filename, ext);
	}
	return fn;
	
}

#define ACTION_EXISTS 1
#define ACTION_DELETE 2
#define ACTION_RENAME 3
#define ACTION_OPEN   4

static int ast_filehelper(char *filename, char *filename2, char *fmt, int action)
{
	struct stat st;
	struct ast_format *f;
	struct ast_filestream *s;
	int res=0, ret = 0;
	char *ext=NULL, *exts, *fn, *nfn;
	struct ast_channel *trans = (struct ast_channel *)filename2;
	
	/* Start with negative response */
	if (action == ACTION_EXISTS)
		res = 0;
	else
		res = -1;
	if (action == ACTION_OPEN)
		ret = -1;
	/* Check for a specific format */
	if (pthread_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		if (action == ACTION_EXISTS)
			return 0;
		else
			return -1;
	}
	f = formats;
	while(f) {
		if (!fmt || !strcasecmp(f->name, fmt)) {
			exts = strdup(f->exts);
			/* Try each kind of extension */
			ext = strtok(exts, "|");
			do {
				fn = build_filename(filename, ext);
				if (fn) {
					res = stat(fn, &st);
					if (!res) {
						switch(action) {
						case ACTION_EXISTS:
							ret |= f->format;
							break;
						case ACTION_DELETE:
							res = unlink(fn);
							if (res)
								ast_log(LOG_WARNING, "unlink(%s) failed: %s\n", fn, strerror(errno));
							break;
						case ACTION_RENAME:
							nfn = build_filename(filename2, ext);
							if (nfn) {
								res = rename(fn, nfn);
								if (res)
									ast_log(LOG_WARNING, "rename(%s,%s) failed: %s\n", fn, nfn, strerror(errno));
								free(nfn);
							} else
								ast_log(LOG_WARNING, "Out of memory\n");
							break;
						case ACTION_OPEN:
							if ((ret < 0) && ((trans->format & f->format) /* == trans->format */)) {
								ret = open(fn, O_RDONLY);
								if (ret >= 0) {
									s = f->open(ret);
									if (s) {
										s->fmt = f;
										s->trans = NULL;
										trans->stream = s;
										if (f->apply(trans, s)) {
											f->close(s);
											trans->stream = NULL;
											ast_log(LOG_WARNING, "Unable to apply stream to channel %s\n", trans->name);
											close(ret);
											ret = 0;
										}
									} else {
										close(ret);
										ast_log(LOG_WARNING, "Unable to open fd on %s\n", filename);
									}
								} else
									ast_log(LOG_WARNING, "Couldn't open file %s\n", fn);
							}
							break;
						default:
							ast_log(LOG_WARNING, "Unknown helper %d\n", action);
						}
						/* Conveniently this logic is the same for all */
						if (res)
							break;
					}
					free(fn);
				}
				ext = strtok(NULL, "|");
			} while(ext);
			free(exts);
		}
		f = f->next;
	}
	pthread_mutex_unlock(&formatlock);
	if ((action == ACTION_EXISTS) || (action == ACTION_OPEN))
		res = ret ? ret : -1;
	return res;
}

int ast_fileexists(char *filename, char *fmt)
{
	return ast_filehelper(filename, NULL, fmt, ACTION_EXISTS);
}

int ast_filedelete(char *filename, char *fmt)
{
	return ast_filehelper(filename, NULL, fmt, ACTION_DELETE);
}

int ast_filerename(char *filename, char *filename2, char *fmt)
{
	return ast_filehelper(filename, filename2, fmt, ACTION_RENAME);
}

int ast_streamfile(struct ast_channel *chan, char *filename)
{
	/* This is a fairly complex routine.  Essentially we should do 
	   the following:
	   
	   1) Find which file handlers produce our type of format.
	   2) Look for a filename which it can handle.
	   3) If we find one, then great.  
	   4) If not, see what files are there
	   5) See what we can actually support
	   6) Choose the one with the least costly translator path and
	       set it up.
		   
	*/
	int fd = -1;
	struct ast_channel *trans;
	int fmts;
	ast_stopstream(chan);
	fmts = ast_fileexists(filename, NULL);
	if (fmts < 1) {
		ast_log(LOG_WARNING, "File %s does not exist in any format\n", filename);
		return -1;
	}
	if (fmts & chan->format) {
		/* No translation necessary -- we have a file in a format our channel can 
		   handle */
		trans = chan;
	} else {
		/* Find the best */
		fmts = ast_translator_best_choice(chan->format, fmts);
		if (fmts < 1) {
			ast_log(LOG_WARNING, "Unable to find a translator method\n");
			return -1;
		}
		trans = ast_translator_create(chan, fmts, AST_DIRECTION_OUT);
		if (!trans) {
			ast_log(LOG_WARNING, "Unable to create translator\n");
			return -1;
		}
	}
 	fd = ast_filehelper(filename, (char *)trans, NULL, ACTION_OPEN);
	if (fd >= 0) {
#if 0
		ast_verbose(VERBOSE_PREFIX_3 "Playing '%s'\n", filename);
#endif
		return 0;
	}
	ast_log(LOG_WARNING, "Unable to open %s (format %d): %s\n", filename, chan->format, strerror(errno));
	if (chan != trans)
		ast_translator_destroy(trans);	
	return -1;
}


struct ast_filestream *ast_writefile(char *filename, char *type, char *comment, int flags, int check, mode_t mode)
{
	int fd;
	struct ast_format *f;
	struct ast_filestream *fs=NULL;
	char *fn;
	char *ext;
	if (pthread_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return NULL;
	}
	f = formats;
	while(f) {
		if (!strcasecmp(f->name, type)) {
			/* XXX Implement check XXX */
			ext = strdup(f->exts);
			ext = strtok(ext, "|");
			fn = build_filename(filename, ext);
			fd = open(fn, flags | O_WRONLY | O_CREAT, mode);
			if (fd >= 0) {
				errno = 0;
				if ((fs = f->rewrite(fd, comment))) {
					fs->trans = NULL;
					fs->fmt = f;
				} else {
					ast_log(LOG_WARNING, "Unable to rewrite %s\n", fn);
					close(fd);
					unlink(fn);
				}
			} else if (errno != EEXIST)
				ast_log(LOG_WARNING, "Unable to open file %s: %s\n", fn, strerror(errno));
			free(fn);
			free(ext);
			break;
		}
		f = f->next;
	}
	pthread_mutex_unlock(&formatlock);
	if (!f) 
		ast_log(LOG_WARNING, "No such format '%s'\n", type);
	return fs;
}

char ast_waitstream(struct ast_channel *c, char *breakon)
{
	int res;
	struct ast_frame *fr;
	if (c->trans)
		c=c->trans;
	while(c->stream) {
		res = ast_sched_wait(c->sched);
		if (res < 0) {
			/* Okay, stop :) */
			return 0;
		}
		res = ast_waitfor(c, res);
		if (res < 0) {
			ast_log(LOG_WARNING, "Select failed (%s)\n", strerror(errno));
			return res;
		} else
		if (res > 0) {
			fr = ast_read(c);
			if (!fr) {
#if 0
				ast_log(LOG_DEBUG, "Got hung up\n");
#endif
				return -1;
			}
			
			switch(fr->frametype) {
			case AST_FRAME_DTMF:
				res = fr->subclass;
				ast_frfree(fr);
				if (strchr(breakon, res))
					return res;
				break;
			case AST_FRAME_CONTROL:
				switch(fr->subclass) {
				case AST_CONTROL_HANGUP:
					ast_frfree(fr);
					return -1;
				default:
					ast_log(LOG_WARNING, "Unexpected control subclass '%d'\n", fr->subclass);
				}
			default:
				/* Ignore */
				ast_frfree(fr);
			}
		} else
			ast_sched_runq(c->sched);
	
		
	}
	return 0;
}

