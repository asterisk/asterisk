/*m
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
#include "astconf.h"

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
	/* play filestream on a channel */
	int (*play)(struct ast_filestream *);
	/* Write a frame to a channel */
	int (*write)(struct ast_filestream *, struct ast_frame *);
	/* seek num samples into file, whence(think normal seek) */
	int (*seek)(struct ast_filestream *, long offset, int whence);
	/* trunc file to current position */
	int (*trunc)(struct ast_filestream *fs);
	/* tell current position */
	long (*tell)(struct ast_filestream *fs);
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

static pthread_mutex_t formatlock = AST_MUTEX_INITIALIZER;

static struct ast_format *formats = NULL;

int ast_format_register(char *name, char *exts, int format,
						struct ast_filestream * (*open)(int fd),
						struct ast_filestream * (*rewrite)(int fd, char *comment),
						int (*apply)(struct ast_channel *, struct ast_filestream *),
						int (*play)(struct ast_filestream *),
						int (*write)(struct ast_filestream *, struct ast_frame *),
						int (*seek)(struct ast_filestream *, long sample_offset, int whence),
						int (*trunc)(struct ast_filestream *),
						long (*tell)(struct ast_filestream *),
						struct ast_frame * (*read)(struct ast_filestream *),
						void (*close)(struct ast_filestream *),
						char * (*getcomment)(struct ast_filestream *))
{
	struct ast_format *tmp;
	if (ast_pthread_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return -1;
	}
	tmp = formats;
	while(tmp) {
		if (!strcasecmp(name, tmp->name)) {
			ast_pthread_mutex_unlock(&formatlock);
			ast_log(LOG_WARNING, "Tried to register '%s' format, already registered\n", name);
			return -1;
		}
		tmp = tmp->next;
	}
	tmp = malloc(sizeof(struct ast_format));
	if (!tmp) {
		ast_log(LOG_WARNING, "Out of memory\n");
		ast_pthread_mutex_unlock(&formatlock);
		return -1;
	}
	strncpy(tmp->name, name, sizeof(tmp->name)-1);
	strncpy(tmp->exts, exts, sizeof(tmp->exts)-1);
	tmp->open = open;
	tmp->rewrite = rewrite;
	tmp->apply = apply;
	tmp->play = play;
	tmp->read = read;
	tmp->write = write;
	tmp->seek = seek;
	tmp->trunc = trunc;
	tmp->tell = tell;
	tmp->close = close;
	tmp->format = format;
	tmp->getcomment = getcomment;
	tmp->next = formats;
	formats = tmp;
	ast_pthread_mutex_unlock(&formatlock);
	if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Registered file format %s, extension(s) %s\n", name, exts);
	return 0;
}

int ast_format_unregister(char *name)
{
	struct ast_format *tmp, *tmpl = NULL;
	if (ast_pthread_mutex_lock(&formatlock)) {
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
			ast_pthread_mutex_unlock(&formatlock);
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
	/* Stop a running stream if there is one */
	if (!tmp->stream) 
		return 0;
	tmp->stream->fmt->close(tmp->stream);
	if (tmp->oldwriteformat && ast_set_write_format(tmp, tmp->oldwriteformat))
		ast_log(LOG_WARNING, "Unable to restore format back to %d\n", tmp->oldwriteformat);
	return 0;
}

int ast_writestream(struct ast_filestream *fs, struct ast_frame *f)
{
	struct ast_frame *trf;
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
			fs->trans = ast_translator_build_path(fs->fmt->format, f->subclass);
		if (!fs->trans)
			ast_log(LOG_WARNING, "Unable to translate to format %s, source format %d\n", fs->fmt->name, f->subclass);
		else {
			res = 0;
			/* Get the translated frame but don't consume the original in case they're using it on another stream */
			trf = ast_translate(fs->trans, f, 0);
			if (trf) {
				res = fs->fmt->write(fs, trf);
				if (res) 
					ast_log(LOG_WARNING, "Translated frame write failed\n");
			} else
				res = 0;
		}
		return res;
	}
}

static int copy(char *infile, char *outfile)
{
	int ifd;
	int ofd;
	int res;
	int len;
	char buf[4096];
	if ((ifd = open(infile, O_RDONLY)) < 0) {
		ast_log(LOG_WARNING, "Unable to open %s in read-only mode\n", infile);
		return -1;
	}
	if ((ofd = open(outfile, O_WRONLY | O_TRUNC | O_CREAT, 0600)) < 0) {
		ast_log(LOG_WARNING, "Unable to open %s in write-only mode\n", outfile);
		close(ifd);
		return -1;
	}
	do {
		len = read(ifd, buf, sizeof(buf));
		if (len < 0) {
			ast_log(LOG_WARNING, "Read failed on %s: %s\n", infile, strerror(errno));
			close(ifd);
			close(ofd);
			unlink(outfile);
		}
		if (len) {
			res = write(ofd, buf, len);
			if (res != len) {
				ast_log(LOG_WARNING, "Write failed on %s (%d of %d): %s\n", outfile, res, len, strerror(errno));
				close(ifd);
				close(ofd);
				unlink(outfile);
			}
		}
	} while(len);
	close(ifd);
	close(ofd);
	return 0;
}

static char *build_filename(char *filename, char *ext)
{
	char *fn;
	char tmp[AST_CONFIG_MAX_PATH];
	snprintf((char *)tmp,sizeof(tmp)-1,"%s/%s",(char *)ast_config_AST_VAR_DIR,"sounds");
	fn = malloc(strlen(tmp) + strlen(filename) + strlen(ext) + 10);
	if (fn) {
		if (filename[0] == '/') 
			sprintf(fn, "%s.%s", filename, ext);
		else
			sprintf(fn, "%s/%s.%s", (char *)tmp, filename, ext);
	}
	return fn;
	
}

#define ACTION_EXISTS 1
#define ACTION_DELETE 2
#define ACTION_RENAME 3
#define ACTION_OPEN   4
#define ACTION_COPY   5

static int ast_filehelper(char *filename, char *filename2, char *fmt, int action)
{
	struct stat st;
	struct ast_format *f;
	struct ast_filestream *s;
	int res=0, ret = 0;
	char *ext=NULL, *exts, *fn, *nfn;
	struct ast_channel *chan = (struct ast_channel *)filename2;
	
	/* Start with negative response */
	if (action == ACTION_EXISTS)
		res = 0;
	else
		res = -1;
	if (action == ACTION_OPEN)
		ret = -1;
	/* Check for a specific format */
	if (ast_pthread_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		if (action == ACTION_EXISTS)
			return 0;
		else
			return -1;
	}
	f = formats;
	while(f) {
		if (!fmt || !strcasecmp(f->name, fmt)) {
			char *stringp=NULL;
			exts = strdup(f->exts);
			/* Try each kind of extension */
			stringp=exts;
			ext = strsep(&stringp, "|");
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
						case ACTION_COPY:
							nfn = build_filename(filename2, ext);
							if (nfn) {
								res = copy(fn, nfn);
								if (res)
									ast_log(LOG_WARNING, "copy(%s,%s) failed: %s\n", fn, nfn, strerror(errno));
								free(nfn);
							} else
								ast_log(LOG_WARNING, "Out of memory\n");
							break;
						case ACTION_OPEN:
							if ((ret < 0) && ((chan->writeformat & f->format))) {
								ret = open(fn, O_RDONLY);
								if (ret >= 0) {
									s = f->open(ret);
									if (s) {
										s->fmt = f;
										s->trans = NULL;
										chan->stream = s;
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
				ext = strsep(&stringp, "|");
			} while(ext);
			free(exts);
		}
		f = f->next;
	}
	ast_pthread_mutex_unlock(&formatlock);
	if ((action == ACTION_EXISTS) || (action == ACTION_OPEN))
		res = ret ? ret : -1;
	return res;
}

struct ast_filestream *ast_openstream(struct ast_channel *chan, char *filename, char *preflang)
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
	int fmts = -1;
	char filename2[256];
	char lang2[MAX_LANGUAGE];
	int res;
	ast_stopstream(chan);
	/* do this first, otherwise we detect the wrong writeformat */
	if (chan->generator)
		ast_deactivate_generator(chan);
	if (preflang && strlen(preflang)) {
		snprintf(filename2, sizeof(filename2), "%s-%s", filename, preflang);
		fmts = ast_fileexists(filename2, NULL, NULL);
		if (fmts < 1) {
			strncpy(lang2, preflang, sizeof(lang2)-1);
			snprintf(filename2, sizeof(filename2), "%s-%s", filename, lang2);
			fmts = ast_fileexists(filename2, NULL, NULL);
		}
	}
	if (fmts < 1) {
		strncpy(filename2, filename, sizeof(filename2)-1);
		fmts = ast_fileexists(filename2, NULL, NULL);
	}
	if (fmts < 1) {
		ast_log(LOG_WARNING, "File %s does not exist in any format\n", filename);
		return NULL;
	}
	chan->oldwriteformat = chan->writeformat;
	/* Set the channel to a format we can work with */
	res = ast_set_write_format(chan, fmts);
	
 	fd = ast_filehelper(filename2, (char *)chan, NULL, ACTION_OPEN);
	if(fd >= 0)
		return chan->stream;
	return NULL;
}

int ast_applystream(struct ast_channel *chan, struct ast_filestream *s)
{
	if(chan->stream->fmt->apply(chan,s)){
		chan->stream->fmt->close(s);
		chan->stream = NULL;
		ast_log(LOG_WARNING, "Unable to apply stream to channel %s\n", chan->name);
		return -1;
	}
	return 0;
}

int ast_playstream(struct ast_filestream *s)
{
	if(s->fmt->play(s)){
		ast_closestream(s);
		ast_log(LOG_WARNING, "Unable to start playing stream\n");
		return -1;
	}
	return 0;
}

int ast_seekstream(struct ast_filestream *fs, long sample_offset, int whence)
{
	return fs->fmt->seek(fs, sample_offset, whence);
}

int ast_truncstream(struct ast_filestream *fs)
{
	return fs->fmt->trunc(fs);
}

long ast_tellstream(struct ast_filestream *fs)
{
	return fs->fmt->tell(fs);
}

int ast_stream_fastforward(struct ast_filestream *fs, long ms)
{
	/* I think this is right, 8000 samples per second, 1000 ms a second so 8
	 * samples per ms  */
	long samples = ms * 8;
	return ast_seekstream(fs, samples, SEEK_CUR);
}

int ast_stream_rewind(struct ast_filestream *fs, long ms)
{
	long samples = ms * 8;
	samples = samples * -1;
	return ast_seekstream(fs, samples, SEEK_CUR);
}

int ast_closestream(struct ast_filestream *f)
{
	/* Stop a running stream if there is one */
	f->fmt->close(f);
	return 0;
}


int ast_fileexists(char *filename, char *fmt, char *preflang)
{
	char filename2[256];
	char tmp[256];
	char *postfix;
	char *prefix;
	char *c;
	char lang2[MAX_LANGUAGE];
	int res = -1;
	if (preflang && strlen(preflang)) {
		/* Insert the language between the last two parts of the path */
		strncpy(tmp, filename, sizeof(tmp) - 1);
		c = strrchr(tmp, '/');
		if (c) {
			*c = '\0';
			postfix = c+1;
			prefix = tmp;
		} else {
			postfix = tmp;
			prefix="";
		}
		snprintf(filename2, sizeof(filename2), "%s/%s/%s", prefix, preflang, postfix);
		res = ast_filehelper(filename2, NULL, fmt, ACTION_EXISTS);
		if (res < 1) {
			char *stringp=NULL;
			strncpy(lang2, preflang, sizeof(lang2)-1);
			stringp=lang2;
			strsep(&stringp, "_");
			if (strcmp(lang2, preflang)) {
				snprintf(filename2, sizeof(filename2), "%s/%s/%s", prefix, lang2, postfix);
				res = ast_filehelper(filename2, NULL, fmt, ACTION_EXISTS);
			}
		}
	}
	if (res < 1) {
		res = ast_filehelper(filename, NULL, fmt, ACTION_EXISTS);
	}
	return res;
}

int ast_filedelete(char *filename, char *fmt)
{
	return ast_filehelper(filename, NULL, fmt, ACTION_DELETE);
}

int ast_filerename(char *filename, char *filename2, char *fmt)
{
	return ast_filehelper(filename, filename2, fmt, ACTION_RENAME);
}

int ast_filecopy(char *filename, char *filename2, char *fmt)
{
	return ast_filehelper(filename, filename2, fmt, ACTION_COPY);
}

int ast_streamfile(struct ast_channel *chan, char *filename, char *preflang)
{
	struct ast_filestream *fs;

	fs = ast_openstream(chan, filename, preflang);
	if(fs){
		if(ast_applystream(chan, fs))
			return -1;
		if(ast_playstream(fs))
			return -1;
#if 1
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Playing '%s'\n", filename);
#endif
		return 0;
	}
	ast_log(LOG_WARNING, "Unable to open %s (format %d): %s\n", filename, chan->nativeformats, strerror(errno));
	return -1;
}


struct ast_filestream *ast_writefile(char *filename, char *type, char *comment, int flags, int check, mode_t mode)
{
	int fd,myflags;
	struct ast_format *f;
	struct ast_filestream *fs=NULL;
	char *fn;
	char *ext;
	if (ast_pthread_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return NULL;
	}
	myflags = 0;
	/* set the O_TRUNC flag if and only if there is no O_APPEND specified */
	//if (!(flags & O_APPEND)) myflags = O_TRUNC;
	f = formats;
	while(f) {
		if (!strcasecmp(f->name, type)) {
			char *stringp=NULL;
			/* XXX Implement check XXX */
			ext = strdup(f->exts);
			stringp=ext;
			ext = strsep(&stringp, "|");
			fn = build_filename(filename, ext);
			fd = open(fn, flags | myflags | O_WRONLY | O_CREAT, mode);
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
	ast_pthread_mutex_unlock(&formatlock);
	if (!f) 
		ast_log(LOG_WARNING, "No such format '%s'\n", type);
	return fs;
}

char ast_waitstream(struct ast_channel *c, char *breakon)
{
	int res;
	struct ast_frame *fr;
	while(c->stream) {
		res = ast_sched_wait(c->sched);
		if (res < 0) {
			ast_closestream(c->stream);
			break;
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
				if (strchr(breakon, res)) {
					ast_frfree(fr);
					return res;
				}
				break;
			case AST_FRAME_CONTROL:
				switch(fr->subclass) {
				case AST_CONTROL_HANGUP:
					ast_frfree(fr);
					return -1;
				case AST_CONTROL_RINGING:
				case AST_CONTROL_ANSWER:
					/* Unimportant */
					break;
				default:
					ast_log(LOG_WARNING, "Unexpected control subclass '%d'\n", fr->subclass);
				}
			}
			/* Ignore */
			ast_frfree(fr);
		} else
			ast_sched_runq(c->sched);
	
		
	}
	return (c->_softhangup ? -1 : 0);
}

