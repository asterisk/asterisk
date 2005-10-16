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

/*
 *
 * Generic File Format Support.
 * 
 */

#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/frame.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/sched.h"
#include "asterisk/options.h"
#include "asterisk/translate.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"

struct ast_format {
	/* Name of format */
	char name[80];
	/* Extensions (separated by | if more than one) 
	   this format can read.  First is assumed for writing (e.g. .mp3) */
	char exts[80];
	/* Format of frames it uses/provides (one only) */
	int format;
	/* Open an input stream, and start playback */
	struct ast_filestream * (*open)(FILE * f);
	/* Open an output stream, of a given file descriptor and comment it appropriately if applicable */
	struct ast_filestream * (*rewrite)(FILE *f, const char *comment);
	/* Write a frame to a channel */
	int (*write)(struct ast_filestream *, struct ast_frame *);
	/* seek num samples into file, whence(think normal seek) */
	int (*seek)(struct ast_filestream *, long offset, int whence);
	/* trunc file to current position */
	int (*trunc)(struct ast_filestream *fs);
	/* tell current position */
	long (*tell)(struct ast_filestream *fs);
	/* Read the next frame from the filestream (if available) and report when to get next one
		(in samples) */
	struct ast_frame * (*read)(struct ast_filestream *, int *whennext);
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
	int flags;
	mode_t mode;
	char *filename;
	char *realfilename;
	/* Video file stream */
	struct ast_filestream *vfs;
	/* Transparently translate from another format -- just once */
	struct ast_trans_pvt *trans;
	struct ast_tranlator_pvt *tr;
	int lastwriteformat;
	int lasttimeout;
	struct ast_channel *owner;
};

AST_MUTEX_DEFINE_STATIC(formatlock);

static struct ast_format *formats = NULL;

int ast_format_register(const char *name, const char *exts, int format,
						struct ast_filestream * (*open)(FILE *f),
						struct ast_filestream * (*rewrite)(FILE *f, const char *comment),
						int (*write)(struct ast_filestream *, struct ast_frame *),
						int (*seek)(struct ast_filestream *, long sample_offset, int whence),
						int (*trunc)(struct ast_filestream *),
						long (*tell)(struct ast_filestream *),
						struct ast_frame * (*read)(struct ast_filestream *, int *whennext),
						void (*close)(struct ast_filestream *),
						char * (*getcomment)(struct ast_filestream *))
{
	struct ast_format *tmp;
	if (ast_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return -1;
	}
	tmp = formats;
	while(tmp) {
		if (!strcasecmp(name, tmp->name)) {
			ast_mutex_unlock(&formatlock);
			ast_log(LOG_WARNING, "Tried to register '%s' format, already registered\n", name);
			return -1;
		}
		tmp = tmp->next;
	}
	tmp = malloc(sizeof(struct ast_format));
	if (!tmp) {
		ast_log(LOG_WARNING, "Out of memory\n");
		ast_mutex_unlock(&formatlock);
		return -1;
	}
	ast_copy_string(tmp->name, name, sizeof(tmp->name));
	ast_copy_string(tmp->exts, exts, sizeof(tmp->exts));
	tmp->open = open;
	tmp->rewrite = rewrite;
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
	ast_mutex_unlock(&formatlock);
	if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Registered file format %s, extension(s) %s\n", name, exts);
	return 0;
}

int ast_format_unregister(const char *name)
{
	struct ast_format *tmp, *tmpl = NULL;
	if (ast_mutex_lock(&formatlock)) {
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
			ast_mutex_unlock(&formatlock);
			if (option_verbose > 1)
				ast_verbose( VERBOSE_PREFIX_2 "Unregistered format %s\n", name);
			return 0;
		}
		tmpl = tmp;
		tmp = tmp->next;
	}
	ast_log(LOG_WARNING, "Tried to unregister format %s, already unregistered\n", name);
	return -1;
}

int ast_stopstream(struct ast_channel *tmp)
{
	/* Stop a running stream if there is one */
	if (tmp->vstream)
		ast_closestream(tmp->vstream);
	if (tmp->stream) {
		ast_closestream(tmp->stream);
		if (tmp->oldwriteformat && ast_set_write_format(tmp, tmp->oldwriteformat))
			ast_log(LOG_WARNING, "Unable to restore format back to %d\n", tmp->oldwriteformat);
	}
	return 0;
}

int ast_writestream(struct ast_filestream *fs, struct ast_frame *f)
{
	struct ast_frame *trf;
	int res = -1;
	int alt=0;
	if (f->frametype == AST_FRAME_VIDEO) {
		if (fs->fmt->format < AST_FORMAT_MAX_AUDIO) {
			/* This is the audio portion.  Call the video one... */
			if (!fs->vfs && fs->filename) {
				/* XXX Support other video formats XXX */
				const char *type = "h263";
				fs->vfs = ast_writefile(fs->filename, type, NULL, fs->flags, 0, fs->mode);
				ast_log(LOG_DEBUG, "Opened video output file\n");
			}
			if (fs->vfs)
				return ast_writestream(fs->vfs, f);
			/* Ignore */
			return 0;				
		} else {
			/* Might / might not have mark set */
			alt = 1;
		}
	} else if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Tried to write non-voice frame\n");
		return -1;
	}
	if (((fs->fmt->format | alt) & f->subclass) == f->subclass) {
		res =  fs->fmt->write(fs, f);
		if (res < 0) 
			ast_log(LOG_WARNING, "Natural write failed\n");
		if (res > 0)
			ast_log(LOG_WARNING, "Huh??\n");
		return res;
	} else {
		/* XXX If they try to send us a type of frame that isn't the normal frame, and isn't
		       the one we've setup a translator for, we do the "wrong thing" XXX */
		if (fs->trans && (f->subclass != fs->lastwriteformat)) {
			ast_translator_free_path(fs->trans);
			fs->trans = NULL;
		}
		if (!fs->trans) 
			fs->trans = ast_translator_build_path(fs->fmt->format, f->subclass);
		if (!fs->trans)
			ast_log(LOG_WARNING, "Unable to translate to format %s, source format %s\n", fs->fmt->name, ast_getformatname(f->subclass));
		else {
			fs->lastwriteformat = f->subclass;
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

static int copy(const char *infile, const char *outfile)
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

static char *build_filename(const char *filename, const char *ext)
{
	char *fn, type[16];
	int fnsize = 0;

	if (!strcmp(ext, "wav49")) {
		ast_copy_string(type, "WAV", sizeof(type));
	} else {
		ast_copy_string(type, ext, sizeof(type));
	}

	if (filename[0] == '/') {
		fnsize = strlen(filename) + strlen(type) + 2;
		fn = malloc(fnsize);
		if (fn)
			snprintf(fn, fnsize, "%s.%s", filename, type);
	} else {
		char tmp[AST_CONFIG_MAX_PATH] = "";

		snprintf(tmp, sizeof(tmp), "%s/%s", ast_config_AST_VAR_DIR, "sounds");
		fnsize = strlen(tmp) + strlen(filename) + strlen(type) + 3;
		fn = malloc(fnsize);
		if (fn)
			snprintf(fn, fnsize, "%s/%s.%s", tmp, filename, type);
	}

	return fn;
}

static int exts_compare(const char *exts, const char *type)
{
	char *stringp = NULL, *ext;
	char tmp[256];

	ast_copy_string(tmp, exts, sizeof(tmp));
	stringp = tmp;
	while ((ext = strsep(&stringp, "|"))) {
		if (!strcmp(ext, type)) {
			return 1;
		}
	}

	return 0;
}

#define ACTION_EXISTS 1
#define ACTION_DELETE 2
#define ACTION_RENAME 3
#define ACTION_OPEN   4
#define ACTION_COPY   5

static int ast_filehelper(const char *filename, const char *filename2, const char *fmt, int action)
{
	struct stat st;
	struct ast_format *f;
	struct ast_filestream *s;
	int res=0, ret = 0;
	char *ext=NULL, *exts, *fn, *nfn;
	FILE *bfile;
	struct ast_channel *chan = (struct ast_channel *)filename2;
	
	/* Start with negative response */
	if (action == ACTION_EXISTS)
		res = 0;
	else
		res = -1;
	if (action == ACTION_OPEN)
		ret = -1;
	/* Check for a specific format */
	if (ast_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		if (action == ACTION_EXISTS)
			return 0;
		else
			return -1;
	}
	f = formats;
	while(f) {
		if (!fmt || exts_compare(f->exts, fmt)) {
			char *stringp=NULL;
			exts = ast_strdupa(f->exts);
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
							if ((ret < 0) && ((chan->writeformat & f->format) ||
										((f->format >= AST_FORMAT_MAX_AUDIO) && fmt))) {
								bfile = fopen(fn, "r");
								if (bfile) {
									ret = 1;
									s = f->open(bfile);
									if (s) {
										s->lasttimeout = -1;
										s->fmt = f;
										s->trans = NULL;
										s->filename = NULL;
										if (s->fmt->format < AST_FORMAT_MAX_AUDIO)
											chan->stream = s;
										else
											chan->vstream = s;
									} else {
										fclose(bfile);
										ast_log(LOG_WARNING, "Unable to open file on %s\n", fn);
										ret = -1;
									}
								} else{
									ast_log(LOG_WARNING, "Couldn't open file %s\n", fn);
									ret = -1;
								}
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
			
		}
		f = f->next;
	}
	ast_mutex_unlock(&formatlock);
	if ((action == ACTION_EXISTS) || (action == ACTION_OPEN))
		res = ret ? ret : -1;
	return res;
}
struct ast_filestream *ast_openstream(struct ast_channel *chan, const char *filename, const char *preflang)
{
	return ast_openstream_full(chan, filename, preflang, 0);
}

struct ast_filestream *ast_openstream_full(struct ast_channel *chan, const char *filename, const char *preflang, int asis)
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
	int fmts = -1;
	char filename2[256]="";
	char filename3[256];
	char *endpart;
	int res;

	if (!asis) {
		/* do this first, otherwise we detect the wrong writeformat */
		ast_stopstream(chan);
		if (chan->generator)
			ast_deactivate_generator(chan);
	}
	if (preflang && !ast_strlen_zero(preflang)) {
		ast_copy_string(filename3, filename, sizeof(filename3));
		endpart = strrchr(filename3, '/');
		if (endpart) {
			*endpart = '\0';
			endpart++;
			snprintf(filename2, sizeof(filename2), "%s/%s/%s", filename3, preflang, endpart);
		} else
			snprintf(filename2, sizeof(filename2), "%s/%s", preflang, filename);
		fmts = ast_fileexists(filename2, NULL, NULL);
	}
	if (fmts < 1) {
		ast_copy_string(filename2, filename, sizeof(filename2));
		fmts = ast_fileexists(filename2, NULL, NULL);
	}
	if (fmts < 1) {
		ast_log(LOG_WARNING, "File %s does not exist in any format\n", filename);
		return NULL;
	}
	chan->oldwriteformat = chan->writeformat;
	/* Set the channel to a format we can work with */
	res = ast_set_write_format(chan, fmts);
	
 	res = ast_filehelper(filename2, (char *)chan, NULL, ACTION_OPEN);
	if (res >= 0)
		return chan->stream;
	return NULL;
}

struct ast_filestream *ast_openvstream(struct ast_channel *chan, const char *filename, const char *preflang)
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
	/* XXX H.263 only XXX */
	char *fmt = "h263";
	if (preflang && !ast_strlen_zero(preflang)) {
		snprintf(filename2, sizeof(filename2), "%s/%s", preflang, filename);
		fmts = ast_fileexists(filename2, fmt, NULL);
		if (fmts < 1) {
			ast_copy_string(lang2, preflang, sizeof(lang2));
			snprintf(filename2, sizeof(filename2), "%s/%s", lang2, filename);
			fmts = ast_fileexists(filename2, fmt, NULL);
		}
	}
	if (fmts < 1) {
		ast_copy_string(filename2, filename, sizeof(filename2));
		fmts = ast_fileexists(filename2, fmt, NULL);
	}
	if (fmts < 1) {
		return NULL;
	}
 	fd = ast_filehelper(filename2, (char *)chan, fmt, ACTION_OPEN);
	if (fd >= 0)
		return chan->vstream;
	ast_log(LOG_WARNING, "File %s has video but couldn't be opened\n", filename);
	return NULL;
}

struct ast_frame *ast_readframe(struct ast_filestream *s)
{
	struct ast_frame *f = NULL;
	int whennext = 0;	
	if (s && s->fmt)
		f = s->fmt->read(s, &whennext);
	return f;
}

static int ast_readaudio_callback(void *data)
{
	struct ast_filestream *s = data;
	struct ast_frame *fr;
	int whennext = 0;

	while(!whennext) {
		fr = s->fmt->read(s, &whennext);
		if (fr) {
			if (ast_write(s->owner, fr)) {
				ast_log(LOG_WARNING, "Failed to write frame\n");
				s->owner->streamid = -1;
#ifdef ZAPTEL_OPTIMIZATIONS
				ast_settimeout(s->owner, 0, NULL, NULL);
#endif			
				return 0;
			}
		} else {
			/* Stream has finished */
			s->owner->streamid = -1;
#ifdef ZAPTEL_OPTIMIZATIONS
			ast_settimeout(s->owner, 0, NULL, NULL);
#endif			
			return 0;
		}
	}
	if (whennext != s->lasttimeout) {
#ifdef ZAPTEL_OPTIMIZATIONS
		if (s->owner->timingfd > -1)
			ast_settimeout(s->owner, whennext, ast_readaudio_callback, s);
		else
#endif		
			s->owner->streamid = ast_sched_add(s->owner->sched, whennext/8, ast_readaudio_callback, s);
		s->lasttimeout = whennext;
		return 0;
	}
	return 1;
}

static int ast_readvideo_callback(void *data)
{
	struct ast_filestream *s = data;
	struct ast_frame *fr;
	int whennext = 0;

	while(!whennext) {
		fr = s->fmt->read(s, &whennext);
		if (fr) {
			if (ast_write(s->owner, fr)) {
				ast_log(LOG_WARNING, "Failed to write frame\n");
				s->owner->vstreamid = -1;
				return 0;
			}
		} else {
			/* Stream has finished */
			s->owner->vstreamid = -1;
			return 0;
		}
	}
	if (whennext != s->lasttimeout) {
		s->owner->vstreamid = ast_sched_add(s->owner->sched, whennext/8, ast_readvideo_callback, s);
		s->lasttimeout = whennext;
		return 0;
	}
	return 1;
}

int ast_applystream(struct ast_channel *chan, struct ast_filestream *s)
{
	s->owner = chan;
	return 0;
}

int ast_playstream(struct ast_filestream *s)
{
	if (s->fmt->format < AST_FORMAT_MAX_AUDIO)
		ast_readaudio_callback(s);
	else
		ast_readvideo_callback(s);
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
	char *cmd = NULL;
	size_t size = 0;
	/* Stop a running stream if there is one */
	if (f->owner) {
		if (f->fmt->format < AST_FORMAT_MAX_AUDIO) {
			f->owner->stream = NULL;
			if (f->owner->streamid > -1)
				ast_sched_del(f->owner->sched, f->owner->streamid);
			f->owner->streamid = -1;
#ifdef ZAPTEL_OPTIMIZATIONS
			ast_settimeout(f->owner, 0, NULL, NULL);
#endif			
		} else {
			f->owner->vstream = NULL;
			if (f->owner->vstreamid > -1)
				ast_sched_del(f->owner->sched, f->owner->vstreamid);
			f->owner->vstreamid = -1;
		}
	}
	/* destroy the translator on exit */
	if (f->trans) {
		ast_translator_free_path(f->trans);
		f->trans = NULL;
	}

	if (f->realfilename && f->filename) {
			size = strlen(f->filename) + strlen(f->realfilename) + 15;
			cmd = alloca(size);
			memset(cmd,0,size);
			snprintf(cmd,size,"/bin/mv -f %s %s",f->filename,f->realfilename);
			ast_safe_system(cmd);
	}

	if (f->filename) {
		free(f->filename);
		f->filename = NULL;
	}
	if (f->realfilename) {
		free(f->realfilename);
		f->realfilename = NULL;
	}
	f->fmt->close(f);
	return 0;
}


int ast_fileexists(const char *filename, const char *fmt, const char *preflang)
{
	char filename2[256];
	char tmp[256];
	char *postfix;
	char *prefix;
	char *c;
	char lang2[MAX_LANGUAGE];
	int res = -1;
	if (preflang && !ast_strlen_zero(preflang)) {
		/* Insert the language between the last two parts of the path */
		ast_copy_string(tmp, filename, sizeof(tmp));
		c = strrchr(tmp, '/');
		if (c) {
			*c = '\0';
			postfix = c+1;
			prefix = tmp;
			snprintf(filename2, sizeof(filename2), "%s/%s/%s", prefix, preflang, postfix);
		} else {
			postfix = tmp;
			prefix="";
			snprintf(filename2, sizeof(filename2), "%s/%s", preflang, postfix);
		}
		res = ast_filehelper(filename2, NULL, fmt, ACTION_EXISTS);
		if (res < 1) {
			char *stringp=NULL;
			ast_copy_string(lang2, preflang, sizeof(lang2));
			stringp=lang2;
			strsep(&stringp, "_");
			/* If language is a specific locality of a language (like es_MX), strip the locality and try again */
			if (strcmp(lang2, preflang)) {
				if (ast_strlen_zero(prefix)) {
					snprintf(filename2, sizeof(filename2), "%s/%s", lang2, postfix);
				} else {
					snprintf(filename2, sizeof(filename2), "%s/%s/%s", prefix, lang2, postfix);
				}
				res = ast_filehelper(filename2, NULL, fmt, ACTION_EXISTS);
			}
		}
	}

	/* Fallback to no language (usually winds up being American English) */
	if (res < 1) {
		res = ast_filehelper(filename, NULL, fmt, ACTION_EXISTS);
	}
	return res;
}

int ast_filedelete(const char *filename, const char *fmt)
{
	return ast_filehelper(filename, NULL, fmt, ACTION_DELETE);
}

int ast_filerename(const char *filename, const char *filename2, const char *fmt)
{
	return ast_filehelper(filename, filename2, fmt, ACTION_RENAME);
}

int ast_filecopy(const char *filename, const char *filename2, const char *fmt)
{
	return ast_filehelper(filename, filename2, fmt, ACTION_COPY);
}

int ast_streamfile(struct ast_channel *chan, const char *filename, const char *preflang)
{
	struct ast_filestream *fs;
	struct ast_filestream *vfs;

	fs = ast_openstream(chan, filename, preflang);
	vfs = ast_openvstream(chan, filename, preflang);
	if (vfs)
		ast_log(LOG_DEBUG, "Ooh, found a video stream, too\n");
	if (fs){
		if (ast_applystream(chan, fs))
			return -1;
		if (vfs && ast_applystream(chan, vfs))
			return -1;
		if (ast_playstream(fs))
			return -1;
		if (vfs && ast_playstream(vfs))
			return -1;
#if 1
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Playing '%s' (language '%s')\n", filename, preflang ? preflang : "default");
#endif
		return 0;
	}
	ast_log(LOG_WARNING, "Unable to open %s (format %s): %s\n", filename, ast_getformatname(chan->nativeformats), strerror(errno));
	return -1;
}

struct ast_filestream *ast_readfile(const char *filename, const char *type, const char *comment, int flags, int check, mode_t mode)
{
	FILE *bfile;
	struct ast_format *f;
	struct ast_filestream *fs = NULL;
	char *fn;

	if (ast_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return NULL;
	}

	for (f = formats; f && !fs; f = f->next) {
		if (!exts_compare(f->exts, type))
			continue;

		fn = build_filename(filename, type);
		bfile = fopen(fn, "r");
		if (bfile) {
			errno = 0;

			if (!(fs = f->open(bfile))) {
				ast_log(LOG_WARNING, "Unable to open %s\n", fn);
				fclose(bfile);
				free(fn);
				continue;
			}

			fs->trans = NULL;
			fs->fmt = f;
			fs->flags = flags;
			fs->mode = mode;
			fs->filename = strdup(filename);
			fs->vfs = NULL;
		} else if (errno != EEXIST)
			ast_log(LOG_WARNING, "Unable to open file %s: %s\n", fn, strerror(errno));
		free(fn);
	}

	ast_mutex_unlock(&formatlock);
	if (!fs) 
		ast_log(LOG_WARNING, "No such format '%s'\n", type);

	return fs;
}

struct ast_filestream *ast_writefile(const char *filename, const char *type, const char *comment, int flags, int check, mode_t mode)
{
	int fd, myflags = 0;
	FILE *bfile;
	struct ast_format *f;
	struct ast_filestream *fs = NULL;
	char *fn, *orig_fn = NULL;
	char *buf = NULL;
	size_t size = 0;

	if (ast_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return NULL;
	}

	/* set the O_TRUNC flag if and only if there is no O_APPEND specified */
	if (flags & O_APPEND) { 
		/* We really can't use O_APPEND as it will break WAV header updates */
		flags &= ~O_APPEND;
	} else {
		myflags = O_TRUNC;
	}
	
	myflags |= O_WRONLY | O_CREAT;

	for (f = formats; f && !fs; f = f->next) {
		if (!exts_compare(f->exts, type))
			continue;

		fn = build_filename(filename, type);
		fd = open(fn, flags | myflags, mode);
		if (fd > -1) {
			/* fdopen() the resulting file stream */
			bfile = fdopen(fd, ((flags | myflags) & O_RDWR) ? "w+" : "w");
			if (!bfile) {
				ast_log(LOG_WARNING, "Whoa, fdopen failed: %s!\n", strerror(errno));
				close(fd);
				fd = -1;
			}
		}
		
		if (option_cache_record_files && fd >= 0) {
			char *c;

			fclose(bfile);
			/*
			  We touch orig_fn just as a place-holder so other things (like vmail) see the file is there.
			  What we are really doing is writing to record_cache_dir until we are done then we will mv the file into place.
			*/
			orig_fn = ast_strdupa(fn);
			for (c = fn; *c; c++)
				if (*c == '/')
					*c = '_';

			size = strlen(fn) + strlen(record_cache_dir) + 2;
			buf = alloca(size);
			memset(buf, 0, size);
			snprintf(buf, size, "%s/%s", record_cache_dir, fn);
			free(fn);
			fn = buf;
			fd = open(fn, flags | myflags, mode);
			if (fd > -1) {
				/* fdopen() the resulting file stream */
				bfile = fdopen(fd, ((flags | myflags) & O_RDWR) ? "w+" : "w");
				if (!bfile) {
					ast_log(LOG_WARNING, "Whoa, fdopen failed: %s!\n", strerror(errno));
					close(fd);
					fd = -1;
				}
			}
		}
		if (fd >= 0) {
			errno = 0;

			if ((fs = f->rewrite(bfile, comment))) {
				fs->trans = NULL;
				fs->fmt = f;
				fs->flags = flags;
				fs->mode = mode;
				if (orig_fn) {
					fs->realfilename = strdup(orig_fn);
					fs->filename = strdup(fn);
				} else {
					fs->realfilename = NULL;
					fs->filename = strdup(filename);
				}
				fs->vfs = NULL;
			} else {
				ast_log(LOG_WARNING, "Unable to rewrite %s\n", fn);
				close(fd);
				if (orig_fn) {
					unlink(fn);
					unlink(orig_fn);
				}
			}
		} else if (errno != EEXIST) {
			ast_log(LOG_WARNING, "Unable to open file %s: %s\n", fn, strerror(errno));
			if (orig_fn)
				unlink(orig_fn);
		}
		/* if buf != NULL then fn is already free and pointing to it */
		if (!buf)
			free(fn);
	}

	ast_mutex_unlock(&formatlock);
	if (!fs)
		ast_log(LOG_WARNING, "No such format '%s'\n", type);

	return fs;
}

int ast_waitstream(struct ast_channel *c, const char *breakon)
{
	/* XXX Maybe I should just front-end ast_waitstream_full ? XXX */
	int res;
	struct ast_frame *fr;
	if (!breakon) breakon = "";
	while(c->stream) {
		res = ast_sched_wait(c->sched);
		if ((res < 0) && !c->timingfunc) {
			ast_stopstream(c);
			break;
		}
		if (res < 0)
			res = 1000;
		res = ast_waitfor(c, res);
		if (res < 0) {
			ast_log(LOG_WARNING, "Select failed (%s)\n", strerror(errno));
			return res;
		} else if (res > 0) {
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
				case AST_CONTROL_VIDUPDATE:
					/* Unimportant */
					break;
				default:
					ast_log(LOG_WARNING, "Unexpected control subclass '%d'\n", fr->subclass);
				}
			}
			/* Ignore */
			ast_frfree(fr);
		}
		ast_sched_runq(c->sched);
	}
	return (c->_softhangup ? -1 : 0);
}

int ast_waitstream_fr(struct ast_channel *c, const char *breakon, const char *forward, const char *rewind, int ms)
{
	int res;
	struct ast_frame *fr;

	if (!breakon)
			breakon = "";
	if (!forward)
			forward = "";
	if (!rewind)
			rewind = "";
	
	while(c->stream) {
		res = ast_sched_wait(c->sched);
		if ((res < 0) && !c->timingfunc) {
			ast_stopstream(c);
			break;
		}
		if (res < 0)
			res = 1000;
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
				if (strchr(forward,res)) {
					ast_stream_fastforward(c->stream, ms);
				} else if (strchr(rewind,res)) {
					ast_stream_rewind(c->stream, ms);
				} else if (strchr(breakon, res)) {
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

int ast_waitstream_full(struct ast_channel *c, const char *breakon, int audiofd, int cmdfd)
{
	int res;
	int ms;
	int outfd;
	struct ast_frame *fr;
	struct ast_channel *rchan;

	if (!breakon)
		breakon = "";
	
	while(c->stream) {
		ms = ast_sched_wait(c->sched);
		if ((ms < 0) && !c->timingfunc) {
			ast_stopstream(c);
			break;
		}
		if (ms < 0)
			ms = 1000;
		rchan = ast_waitfor_nandfds(&c, 1, &cmdfd, (cmdfd > -1) ? 1 : 0, NULL, &outfd, &ms);
		if (!rchan && (outfd < 0) && (ms)) {
			/* Continue */
			if (errno == EINTR)
				continue;
			ast_log(LOG_WARNING, "Wait failed (%s)\n", strerror(errno));
			return -1;
		} else if (outfd > -1) {
			/* The FD we were watching has something waiting */
			return 1;
		} else if (rchan) {
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
			case AST_FRAME_VOICE:
				/* Write audio if appropriate */
				if (audiofd > -1)
					write(audiofd, fr->data, fr->datalen);
			}
			/* Ignore */
			ast_frfree(fr);
		}
		ast_sched_runq(c->sched);
	}
	return (c->_softhangup ? -1 : 0);
}

int ast_waitstream_exten(struct ast_channel *c, const char *context)
{
	/* Waitstream, with return in the case of a valid 1 digit extension */
	/* in the current or specified context being pressed */
	/* XXX Maybe I should just front-end ast_waitstream_full ? XXX */
	int res;
	struct ast_frame *fr;
	char exten[AST_MAX_EXTENSION];

	if (!context) context = c->context;
	while(c->stream) {
		res = ast_sched_wait(c->sched);
		if ((res < 0) && !c->timingfunc) {
			ast_stopstream(c);
			break;
		}
		if (res < 0)
			res = 1000;
		res = ast_waitfor(c, res);
		if (res < 0) {
			ast_log(LOG_WARNING, "Select failed (%s)\n", strerror(errno));
			return res;
		} else if (res > 0) {
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
				snprintf(exten, sizeof(exten), "%c", res);
				if (ast_exists_extension(c, context, exten, 1, c->cid.cid_num)) {
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
		}
		ast_sched_runq(c->sched);
	}
	return (c->_softhangup ? -1 : 0);
}

static int show_file_formats(int fd, int argc, char *argv[])
{
#define FORMAT "%-10s %-10s %-20s\n"
#define FORMAT2 "%-10s %-10s %-20s\n"
	struct ast_format *f;
	int count_fmt = 0;

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, FORMAT, "Format", "Name", "Extensions");
	        
	if (ast_mutex_lock(&formatlock)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return -1;
	}

	f = formats;
	while(f) {
		ast_cli(fd, FORMAT2, ast_getformatname(f->format), f->name, f->exts);
		f = f->next;
		count_fmt++;
	};
	ast_mutex_unlock(&formatlock);
	ast_cli(fd, "%d file formats registered.\n", count_fmt);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
	
}

struct ast_cli_entry show_file =
{
	{ "show", "file", "formats" },
	show_file_formats,
	"Displays file formats",
	"Usage: show file formats\n"
	"       displays currently registered file formats (if any)\n"
};

int ast_file_init(void)
{
	ast_cli_register(&show_file);
	return 0;
}
