/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Generic File Format Support.
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"

/*
 * The following variable controls the layout of localized sound files.
 * If 0, use the historical layout with prefix just before the filename
 * (i.e. digits/en/1.gsm , digits/it/1.gsm or default to digits/1.gsm),
 * if 1 put the prefix at the beginning of the filename
 * (i.e. en/digits/1.gsm, it/digits/1.gsm or default to digits/1.gsm).
 * The latter permits a language to be entirely in one directory.
 */
int ast_language_is_prefix;

static AST_LIST_HEAD_STATIC(formats, ast_format);

int __ast_format_register(const struct ast_format *f, struct ast_module *mod)
{
	struct ast_format *tmp;

	if (AST_LIST_LOCK(&formats)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return -1;
	}
	AST_LIST_TRAVERSE(&formats, tmp, list) {
		if (!strcasecmp(f->name, tmp->name)) {
			AST_LIST_UNLOCK(&formats);
			ast_log(LOG_WARNING, "Tried to register '%s' format, already registered\n", f->name);
			return -1;
		}
	}
	if (!(tmp = ast_calloc(1, sizeof(*tmp)))) {
		AST_LIST_UNLOCK(&formats);
		return -1;
	}
	*tmp = *f;
	tmp->module = mod;
	if (tmp->buf_size) {
		/*
		 * Align buf_size properly, rounding up to the machine-specific
		 * alignment for pointers.
		 */
		struct _test_align { void *a, *b; } p;
		int align = (char *)&p.b - (char *)&p.a;
		tmp->buf_size = ((f->buf_size + align - 1)/align)*align;
	}
	
	memset(&tmp->list, 0, sizeof(tmp->list));

	AST_LIST_INSERT_HEAD(&formats, tmp, list);
	AST_LIST_UNLOCK(&formats);
	if (option_verbose > 1)
		ast_verbose( VERBOSE_PREFIX_2 "Registered file format %s, extension(s) %s\n", f->name, f->exts);

	return 0;
}

int ast_format_unregister(const char *name)
{
	struct ast_format *tmp;
	int res = -1;

	if (AST_LIST_LOCK(&formats)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return -1;
	}
	AST_LIST_TRAVERSE_SAFE_BEGIN(&formats, tmp, list) {
		if (!strcasecmp(name, tmp->name)) {
			AST_LIST_REMOVE_CURRENT(&formats, list);
			free(tmp);
			res = 0;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&formats);

	if (!res) {
		if (option_verbose > 1)
			ast_verbose( VERBOSE_PREFIX_2 "Unregistered format %s\n", name);
	} else
		ast_log(LOG_WARNING, "Tried to unregister format %s, already unregistered\n", name);

	return res;
}

int ast_stopstream(struct ast_channel *tmp)
{
	ast_channel_lock(tmp);

	/* Stop a running stream if there is one */
	if (tmp->stream) {
		ast_closestream(tmp->stream);
		tmp->stream = NULL;
		if (tmp->oldwriteformat && ast_set_write_format(tmp, tmp->oldwriteformat))
			ast_log(LOG_WARNING, "Unable to restore format back to %d\n", tmp->oldwriteformat);
	}
	/* Stop the video stream too */
	if (tmp->vstream != NULL) {
		ast_closestream(tmp->vstream);
		tmp->vstream = NULL;
	}

	ast_channel_unlock(tmp);

	return 0;
}

int ast_writestream(struct ast_filestream *fs, struct ast_frame *f)
{
	int res = -1;
	int alt = 0;
	if (f->frametype == AST_FRAME_VIDEO) {
		if (fs->fmt->format < AST_FORMAT_MAX_AUDIO) {
			/* This is the audio portion.  Call the video one... */
			if (!fs->vfs && fs->filename) {
				const char *type = ast_getformatname(f->subclass & ~0x1);
				fs->vfs = ast_writefile(fs->filename, type, NULL, fs->flags, 0, fs->mode);
				ast_log(LOG_DEBUG, "Opened video output file\n");
			}
			if (fs->vfs)
				return ast_writestream(fs->vfs, f);
			/* else ignore */
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
		else if (res > 0)
			ast_log(LOG_WARNING, "Huh??\n");
	} else {
		/* XXX If they try to send us a type of frame that isn't the normal frame, and isn't
		       the one we've setup a translator for, we do the "wrong thing" XXX */
		if (fs->trans && f->subclass != fs->lastwriteformat) {
			ast_translator_free_path(fs->trans);
			fs->trans = NULL;
		}
		if (!fs->trans) 
			fs->trans = ast_translator_build_path(fs->fmt->format, f->subclass);
		if (!fs->trans)
			ast_log(LOG_WARNING, "Unable to translate to format %s, source format %s\n",
				fs->fmt->name, ast_getformatname(f->subclass));
		else {
			struct ast_frame *trf;
			fs->lastwriteformat = f->subclass;
			/* Get the translated frame but don't consume the original in case they're using it on another stream */
			if ((trf = ast_translate(fs->trans, f, 0))) {
				struct ast_frame *cur;

				/* the translator may have returned multiple frames, so process them */
				for (cur = trf; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
					if ((res = fs->fmt->write(fs, trf))) {
						ast_log(LOG_WARNING, "Translated frame write failed\n");
						break;
					}
				}
				ast_frfree(trf);
			} else {
				res = 0;
			}
		}
	}
	return res;
}

static int copy(const char *infile, const char *outfile)
{
	int ifd, ofd, len;
	char buf[4096];	/* XXX make it lerger. */

	if ((ifd = open(infile, O_RDONLY)) < 0) {
		ast_log(LOG_WARNING, "Unable to open %s in read-only mode\n", infile);
		return -1;
	}
	if ((ofd = open(outfile, O_WRONLY | O_TRUNC | O_CREAT, 0600)) < 0) {
		ast_log(LOG_WARNING, "Unable to open %s in write-only mode\n", outfile);
		close(ifd);
		return -1;
	}
	while ( (len = read(ifd, buf, sizeof(buf)) ) ) {
		int res;
		if (len < 0) {
			ast_log(LOG_WARNING, "Read failed on %s: %s\n", infile, strerror(errno));
			break;
		}
		/* XXX handle partial writes */
		res = write(ofd, buf, len);
		if (res != len) {
			ast_log(LOG_WARNING, "Write failed on %s (%d of %d): %s\n", outfile, res, len, strerror(errno));
			len = -1; /* error marker */
			break;
		}
	}
	close(ifd);
	close(ofd);
	if (len < 0) {
		unlink(outfile);
		return -1; /* error */
	}
	return 0;	/* success */
}

/*!
 * \brief construct a filename. Absolute pathnames are preserved,
 * relative names are prefixed by the sounds/ directory.
 * The wav49 suffix is replaced by 'WAV'.
 * Returns a malloc'ed string to be freed by the caller.
 */
static char *build_filename(const char *filename, const char *ext)
{
	char *fn = NULL;

	if (!strcmp(ext, "wav49"))
		ext = "WAV";

	if (filename[0] == '/') {
		if (asprintf(&fn, "%s.%s", filename, ext) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
			fn = NULL;
		}
	} else {
		if (asprintf(&fn, "%s/sounds/%s.%s",
			     ast_config_AST_DATA_DIR, filename, ext) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
			fn = NULL;
		}
	}
	return fn;
}

/* compare type against the list 'exts' */
/* XXX need a better algorithm */
static int exts_compare(const char *exts, const char *type)
{
	char tmp[256];
	char *stringp = tmp, *ext;

	ast_copy_string(tmp, exts, sizeof(tmp));
	while ((ext = strsep(&stringp, "|"))) {
		if (!strcmp(ext, type))
			return 1;
	}

	return 0;
}

static void filestream_destructor(void *arg)
{
	char *cmd = NULL;
	size_t size = 0;
	struct ast_filestream *f = arg;

	/* Stop a running stream if there is one */
	if (f->owner) {
		if (f->fmt->format < AST_FORMAT_MAX_AUDIO) {
			f->owner->stream = NULL;
			AST_SCHED_DEL(f->owner->sched, f->owner->streamid);
#ifdef HAVE_DAHDI
			ast_settimeout(f->owner, 0, NULL, NULL);
#endif			
		} else {
			f->owner->vstream = NULL;
			AST_SCHED_DEL(f->owner->sched, f->owner->vstreamid);
		}
	}
	/* destroy the translator on exit */
	if (f->trans)
		ast_translator_free_path(f->trans);

	if (f->realfilename && f->filename) {
			size = strlen(f->filename) + strlen(f->realfilename) + 15;
			cmd = alloca(size);
			memset(cmd,0,size);
			snprintf(cmd,size,"/bin/mv -f %s %s",f->filename,f->realfilename);
			ast_safe_system(cmd);
	}

	if (f->filename)
		free(f->filename);
	if (f->realfilename)
		free(f->realfilename);
	if (f->fmt->close) {
		void (*closefn)(struct ast_filestream *) = f->fmt->close;
		closefn(f);
	}
	if (f->f)
		fclose(f->f);
	if (f->vfs)
		ast_closestream(f->vfs);
	if (f->orig_chan_name)
		free((void *) f->orig_chan_name);
	ast_module_unref(f->fmt->module);
}

static struct ast_filestream *get_filestream(struct ast_format *fmt, FILE *bfile)
{
	struct ast_filestream *s;

	int l = sizeof(*s) + fmt->buf_size + fmt->desc_size;	/* total allocation size */
	if ( (s = ao2_alloc(l, filestream_destructor)) == NULL)
		return NULL;
	s->fmt = fmt;
	s->f = bfile;

	if (fmt->desc_size)
		s->_private = ((char *)(s+1)) + fmt->buf_size;
	if (fmt->buf_size)
		s->buf = (char *)(s+1);
	s->fr.src = fmt->name;
	return s;
}

/*
 * Default implementations of open and rewrite.
 * Only use them if you don't have expensive stuff to do.
 */
enum wrap_fn { WRAP_OPEN, WRAP_REWRITE };

static int fn_wrapper(struct ast_filestream *s, const char *comment, enum wrap_fn mode)
{
	struct ast_format *f = s->fmt;
	int ret = -1;
	int (*openfn)(struct ast_filestream *s);

	if (mode == WRAP_OPEN && (openfn = f->open) && openfn(s))
                ast_log(LOG_WARNING, "Unable to open format %s\n", f->name);
	else if (mode == WRAP_REWRITE && f->rewrite && f->rewrite(s, comment))
                ast_log(LOG_WARNING, "Unable to rewrite format %s\n", f->name);
	else {
		/* preliminary checks succeed. update usecount */
		ast_module_ref(f->module);
		ret = 0;
	}
        return ret;
}

static int rewrite_wrapper(struct ast_filestream *s, const char *comment)
{
	return fn_wrapper(s, comment, WRAP_REWRITE);
}
                
static int open_wrapper(struct ast_filestream *s)
{
	return fn_wrapper(s, NULL, WRAP_OPEN);
}

enum file_action {
	ACTION_EXISTS = 1, /* return matching format if file exists, 0 otherwise */
	ACTION_DELETE,	/* delete file, return 0 on success, -1 on error */
	ACTION_RENAME,	/* rename file. return 0 on success, -1 on error */
	ACTION_OPEN,
	ACTION_COPY	/* copy file. return 0 on success, -1 on error */
};

/*!
 * \brief perform various actions on a file. Second argument
 * arg2 depends on the command:
 *	unused for EXISTS and DELETE
 *	destination file name (const char *) for COPY and RENAME
 * 	struct ast_channel * for OPEN
 * if fmt is NULL, OPEN will return the first matching entry,
 * whereas other functions will run on all matching entries.
 */
static int ast_filehelper(const char *filename, const void *arg2, const char *fmt, const enum file_action action)
{
	struct ast_format *f;
	int res = (action == ACTION_EXISTS) ? 0 : -1;

	if (AST_LIST_LOCK(&formats)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return res;
	}
	/* Check for a specific format */
	AST_LIST_TRAVERSE(&formats, f, list) {
		char *stringp, *ext = NULL;

		if (fmt && !exts_compare(f->exts, fmt))
			continue;

		/* Look for a file matching the supported extensions.
		 * The file must exist, and for OPEN, must match
		 * one of the formats supported by the channel.
		 */
		stringp = ast_strdupa(f->exts);	/* this is in the stack so does not need to be freed */
		while ( (ext = strsep(&stringp, "|")) ) {
			struct stat st;
			char *fn = build_filename(filename, ext);

			if (fn == NULL)
				continue;

			if ( stat(fn, &st) ) { /* file not existent */
				free(fn);
				continue;
			}
			/* for 'OPEN' we need to be sure that the format matches
			 * what the channel can process
			 */
			if (action == ACTION_OPEN) {
				struct ast_channel *chan = (struct ast_channel *)arg2;
				FILE *bfile;
				struct ast_filestream *s;

				if ( !(chan->writeformat & f->format) &&
				     !(f->format >= AST_FORMAT_MAX_AUDIO && fmt)) {
					free(fn);
					continue;	/* not a supported format */
				}
				if ( (bfile = fopen(fn, "r")) == NULL) {
					free(fn);
					continue;	/* cannot open file */
				}
				s = get_filestream(f, bfile);
				if (!s) {
					fclose(bfile);
					free(fn);	/* cannot allocate descriptor */
					continue;
				}
				if (open_wrapper(s)) {
					free(fn);
					ast_closestream(s);
					continue;	/* cannot run open on file */
				}
				/* ok this is good for OPEN */
				res = 1;	/* found */
				s->lasttimeout = -1;
				s->fmt = f;
				s->trans = NULL;
				s->filename = NULL;
				if (s->fmt->format < AST_FORMAT_MAX_AUDIO) {
					if (chan->stream)
						ast_closestream(chan->stream);
					chan->stream = s;
				} else {
					if (chan->vstream)
						ast_closestream(chan->vstream);
					chan->vstream = s;
				}
				free(fn);
				break;
			}
			switch (action) {
			case ACTION_OPEN:
				break;	/* will never get here */

			case ACTION_EXISTS:	/* return the matching format */
				res |= f->format;
				break;

			case ACTION_DELETE:
				if ( (res = unlink(fn)) )
					ast_log(LOG_WARNING, "unlink(%s) failed: %s\n", fn, strerror(errno));
				break;

			case ACTION_RENAME:
			case ACTION_COPY: {
				char *nfn = build_filename((const char *)arg2, ext);
				if (!nfn)
					ast_log(LOG_WARNING, "Out of memory\n");
				else {
					res = action == ACTION_COPY ? copy(fn, nfn) : rename(fn, nfn);
					if (res)
						ast_log(LOG_WARNING, "%s(%s,%s) failed: %s\n",
							action == ACTION_COPY ? "copy" : "rename",
							 fn, nfn, strerror(errno));
					free(nfn);
				}
			    }
				break;

			default:
				ast_log(LOG_WARNING, "Unknown helper %d\n", action);
			}
			free(fn);
		}
	}
	AST_LIST_UNLOCK(&formats);
	return res;
}

static int is_absolute_path(const char *filename)
{
	return filename[0] == '/';
}

static int fileexists_test(const char *filename, const char *fmt, const char *lang,
			   char *buf, int buflen)
{
	if (buf == NULL) {
		return -1;
	}

	if (ast_language_is_prefix && !is_absolute_path(filename)) { /* new layout */
		if (lang) {
			snprintf(buf, buflen, "%s/%s", lang, filename);
		} else {
			snprintf(buf, buflen, "%s", filename);
		}
	} else { /* old layout */
		strcpy(buf, filename);	/* first copy the full string */
		if (lang) {
			/* insert the language and suffix if needed */
			const char *c = strrchr(filename, '/');
			int offset = c ? c - filename + 1 : 0;	/* points right after the last '/' */
			snprintf(buf + offset, buflen - offset, "%s/%s", lang, filename + offset);
		}
	}

	return ast_filehelper(buf, NULL, fmt, ACTION_EXISTS);
}

/*!
 * \brief helper routine to locate a file with a given format
 * and language preference.
 * Try preflang, preflang with stripped '_' suffix, or NULL.
 * In the standard asterisk, language goes just before the last component.
 * In an alternative configuration, the language should be a prefix
 * to the actual filename.
 *
 * The last parameter(s) point to a buffer of sufficient size,
 * which on success is filled with the matching filename.
 */
static int fileexists_core(const char *filename, const char *fmt, const char *preflang,
			   char *buf, int buflen)
{
	int res = -1;
	char *lang = NULL;

	if (buf == NULL) {
		return -1;
	}

	/* We try languages in the following order:
	 *    preflang (may include dialect)
	 *    lang (preflang without dialect - if any)
	 *    <none>
	 *    default (unless the same as preflang or lang without dialect)
	 */

	/* Try preferred language */
	if (!ast_strlen_zero(preflang)) {
		/* try the preflang exactly as it was requested */
		if ((res = fileexists_test(filename, fmt, preflang, buf, buflen)) > 0) {
			return res;
		} else {
			/* try without a dialect */
			char *postfix = NULL;
			postfix = lang = ast_strdupa(preflang);

			strsep(&postfix, "_");
			if (postfix) {
				if ((res = fileexists_test(filename, fmt, lang, buf, buflen)) > 0) {
					return res;
				}
			}
		}
	}

	/* Try without any language */
	if ((res = fileexists_test(filename, fmt, NULL, buf, buflen)) > 0) {
		return res;
	}

	/* Finally try the default language unless it was already tried before */
	if ((ast_strlen_zero(preflang) || strcmp(preflang, DEFAULT_LANGUAGE)) && (ast_strlen_zero(lang) || strcmp(lang, DEFAULT_LANGUAGE))) {
		if ((res = fileexists_test(filename, fmt, DEFAULT_LANGUAGE, buf, buflen)) > 0) {
			return res;
		}
	}

	return 0;
}

struct ast_filestream *ast_openstream(struct ast_channel *chan, const char *filename, const char *preflang)
{
	return ast_openstream_full(chan, filename, preflang, 0);
}

struct ast_filestream *ast_openstream_full(struct ast_channel *chan, const char *filename, const char *preflang, int asis)
{
	/* 
	 * Use fileexists_core() to find a file in a compatible
	 * language and format, set up a suitable translator,
	 * and open the stream.
	 */
	int fmts, res, buflen;
	char *buf;

	if (!asis) {
		/* do this first, otherwise we detect the wrong writeformat */
		ast_stopstream(chan);
		if (chan->generator)
			ast_deactivate_generator(chan);
	}
	if (preflang == NULL)
		preflang = "";
	buflen = strlen(preflang) + strlen(filename) + 4;
	buf = alloca(buflen);
	if (buf == NULL)
		return NULL;
	fmts = fileexists_core(filename, NULL, preflang, buf, buflen);
	if (fmts > 0)
		fmts &= AST_FORMAT_AUDIO_MASK;
	if (fmts < 1) {
		ast_log(LOG_WARNING, "File %s does not exist in any format\n", filename);
		return NULL;
	}
	chan->oldwriteformat = chan->writeformat;
	/* Set the channel to a format we can work with */
	res = ast_set_write_format(chan, fmts);
 	res = ast_filehelper(buf, chan, NULL, ACTION_OPEN);
	if (res >= 0)
		return chan->stream;
	return NULL;
}

struct ast_filestream *ast_openvstream(struct ast_channel *chan, const char *filename, const char *preflang)
{
	/* As above, but for video. But here we don't have translators
	 * so we must enforce a format.
	 */
	unsigned int format;
	char *buf;
	int buflen;

	if (preflang == NULL)
		preflang = "";
	buflen = strlen(preflang) + strlen(filename) + 4;
	buf = alloca(buflen);
	if (buf == NULL)
		return NULL;

	for (format = AST_FORMAT_MAX_AUDIO << 1; format <= AST_FORMAT_MAX_VIDEO; format = format << 1) {
		int fd;
		const char *fmt;

		if (!(chan->nativeformats & format))
			continue;
		fmt = ast_getformatname(format);
		if ( fileexists_core(filename, fmt, preflang, buf, buflen) < 1)	/* no valid format */
			continue;
	 	fd = ast_filehelper(buf, chan, fmt, ACTION_OPEN);
		if (fd >= 0)
			return chan->vstream;
		ast_log(LOG_WARNING, "File %s has video but couldn't be opened\n", filename);
	}
	return NULL;
}

struct ast_frame *ast_readframe(struct ast_filestream *s)
{
	struct ast_frame *f = NULL;
	int whennext = 0;	
	if (s && s->fmt)
		f = s->fmt->read(s, &whennext);
	if (f) {
		ast_set_flag(f, AST_FRFLAG_FROM_FILESTREAM);
		ao2_ref(s, +1);
	}
	return f;
}

enum fsread_res {
	FSREAD_FAILURE,
	FSREAD_SUCCESS_SCHED,
	FSREAD_SUCCESS_NOSCHED,
};

static int ast_fsread_audio(const void *data);

static enum fsread_res ast_readaudio_callback(struct ast_filestream *s)
{
	int whennext = 0;

	while (!whennext) {
		struct ast_frame *fr;
		
		if (s->orig_chan_name && strcasecmp(s->owner->name, s->orig_chan_name))
			goto return_failure;
		
		fr = s->fmt->read(s, &whennext);
		if (fr) {
			ast_set_flag(fr, AST_FRFLAG_FROM_FILESTREAM);
			ao2_ref(s, +1);
		}
		if (!fr /* stream complete */ || ast_write(s->owner, fr) /* error writing */) {
			if (fr) {
				ast_log(LOG_WARNING, "Failed to write frame\n");
				ast_frfree(fr);
			}
			goto return_failure;
		}
		if (fr) {
			ast_frfree(fr);
		}
	}
	if (whennext != s->lasttimeout) {
#ifdef HAVE_DAHDI
		if (s->owner->timingfd > -1) {
			int zap_timer_samples = whennext;
			int rate;
			/* whennext is in samples, but DAHDI timers operate in 8 kHz samples. */
			if ((rate = ast_format_rate(s->fmt->format)) != 8000) {
				float factor;
				factor = ((float) rate) / ((float) 8000.0); 
				zap_timer_samples = (int) ( ((float) zap_timer_samples) / factor );
			}
			ast_settimeout(s->owner, zap_timer_samples, ast_fsread_audio, s);
		} else
#endif		
			s->owner->streamid = ast_sched_add(s->owner->sched, 
				whennext / (ast_format_rate(s->fmt->format) / 1000), 
				ast_fsread_audio, s);
		s->lasttimeout = whennext;
		return FSREAD_SUCCESS_NOSCHED;
	}
	return FSREAD_SUCCESS_SCHED;

return_failure:
	s->owner->streamid = -1;
#ifdef HAVE_DAHDI
	ast_settimeout(s->owner, 0, NULL, NULL);
#endif			
	return FSREAD_FAILURE;
}

static int ast_fsread_audio(const void *data)
{
	struct ast_filestream *fs = (struct ast_filestream *)data;
	enum fsread_res res;

	res = ast_readaudio_callback(fs);

	if (res == FSREAD_SUCCESS_SCHED)
		return 1;
	
	return 0;
}

static int ast_fsread_video(const void *data);

static enum fsread_res ast_readvideo_callback(struct ast_filestream *s)
{
	int whennext = 0;

	while (!whennext) {
		struct ast_frame *fr = s->fmt->read(s, &whennext);
		if (fr) {
			ast_set_flag(fr, AST_FRFLAG_FROM_FILESTREAM);
			ao2_ref(s, +1);
		}
		if (!fr /* stream complete */ || ast_write(s->owner, fr) /* error writing */) {
			if (fr) {
				ast_log(LOG_WARNING, "Failed to write frame\n");
				ast_frfree(fr);
			}
			s->owner->vstreamid = -1;
			return FSREAD_FAILURE;
		}
		if (fr) {
			ast_frfree(fr);
		}
	}

	if (whennext != s->lasttimeout) {
		s->owner->vstreamid = ast_sched_add(s->owner->sched, 
			whennext / (ast_format_rate(s->fmt->format) / 1000), 
			ast_fsread_video, s);
		s->lasttimeout = whennext;
		return FSREAD_SUCCESS_NOSCHED;
	}

	return FSREAD_SUCCESS_SCHED;
}

static int ast_fsread_video(const void *data)
{
	struct ast_filestream *fs = (struct ast_filestream *)data;
	enum fsread_res res;

	res = ast_readvideo_callback(fs);

	if (res == FSREAD_SUCCESS_SCHED)
		return 1;
	
	return 0;
}

int ast_applystream(struct ast_channel *chan, struct ast_filestream *s)
{
	s->owner = chan;
	return 0;
}

int ast_playstream(struct ast_filestream *s)
{
	enum fsread_res res;

	if (s->fmt->format < AST_FORMAT_MAX_AUDIO)
		res = ast_readaudio_callback(s);
	else
		res = ast_readvideo_callback(s);

	return (res == FSREAD_FAILURE) ? -1 : 0;
}

int ast_seekstream(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	return fs->fmt->seek(fs, sample_offset, whence);
}

int ast_truncstream(struct ast_filestream *fs)
{
	return fs->fmt->trunc(fs);
}

off_t ast_tellstream(struct ast_filestream *fs)
{
	return fs->fmt->tell(fs);
}

int ast_stream_fastforward(struct ast_filestream *fs, off_t ms)
{
	return ast_seekstream(fs, ms * DEFAULT_SAMPLES_PER_MS, SEEK_CUR);
}

int ast_stream_rewind(struct ast_filestream *fs, off_t ms)
{
	return ast_seekstream(fs, -ms * DEFAULT_SAMPLES_PER_MS, SEEK_CUR);
}

int ast_closestream(struct ast_filestream *f)
{

	if (ast_test_flag(&f->fr, AST_FRFLAG_FROM_FILESTREAM)) {
		/* If this flag is still set, it essentially means that the reference
		 * count of f is non-zero. We can't destroy this filestream until
		 * whatever is using the filestream's frame has finished.
		 *
		 * Since this was called, however, we need to remove the reference from
		 * when this filestream was first allocated. That way, when the embedded
		 * frame is freed, the refcount will reach 0 and we can finish destroying
		 * this filestream properly.
		 */
		ao2_ref(f, -1);
		return 0;
	}
	
	ao2_ref(f, -1);
	return 0;
}


/*
 * Look the various language-specific places where a file could exist.
 */
int ast_fileexists(const char *filename, const char *fmt, const char *preflang)
{
	char *buf;
	int buflen;

	if (preflang == NULL)
		preflang = "";
	buflen = strlen(preflang) + strlen(filename) + 4;	/* room for everything */
	buf = alloca(buflen);
	if (buf == NULL)
		return 0;
	return fileexists_core(filename, fmt, preflang, buf, buflen);
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
	struct ast_filestream *vfs=NULL;
	char fmt[256];

	fs = ast_openstream(chan, filename, preflang);
	if (fs)
		vfs = ast_openvstream(chan, filename, preflang);
	if (vfs)
		ast_log(LOG_DEBUG, "Ooh, found a video stream, too, format %s\n", ast_getformatname(vfs->fmt->format));
	if (fs){
		int res;
		if (ast_test_flag(chan, AST_FLAG_MASQ_NOSTREAM))
			fs->orig_chan_name = ast_strdup(chan->name);
		if (ast_applystream(chan, fs))
			return -1;
		if (vfs && ast_applystream(chan, vfs))
			return -1;
		res = ast_playstream(fs);
		if (!res && vfs)
			res = ast_playstream(vfs);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "<%s> Playing '%s' (language '%s')\n", chan->name, filename, preflang ? preflang : "default");

		return res;
	}
	ast_log(LOG_WARNING, "Unable to open %s (format %s): %s\n", filename, ast_getformatname_multiple(fmt, sizeof(fmt), chan->nativeformats), strerror(errno));
	return -1;
}

struct ast_filestream *ast_readfile(const char *filename, const char *type, const char *comment, int flags, int check, mode_t mode)
{
	FILE *bfile;
	struct ast_format *f;
	struct ast_filestream *fs = NULL;
	char *fn;

	if (AST_LIST_LOCK(&formats)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return NULL;
	}

	AST_LIST_TRAVERSE(&formats, f, list) {
		fs = NULL;
		if (!exts_compare(f->exts, type))
			continue;

		fn = build_filename(filename, type);
		errno = 0;
		bfile = fopen(fn, "r");
		if (!bfile || (fs = get_filestream(f, bfile)) == NULL ||
		    open_wrapper(fs) ) {
			ast_log(LOG_WARNING, "Unable to open %s\n", fn);
			if (fs) {
				ast_closestream(fs);
			}
			fs = NULL;
			bfile = NULL;
			free(fn);
			continue;
		}
		/* found it */
		fs->trans = NULL;
		fs->fmt = f;
		fs->flags = flags;
		fs->mode = mode;
		fs->filename = strdup(filename);
		fs->vfs = NULL;
		break;
	}

	AST_LIST_UNLOCK(&formats);
	if (!fs) 
		ast_log(LOG_WARNING, "No such format '%s'\n", type);

	return fs;
}

struct ast_filestream *ast_writefile(const char *filename, const char *type, const char *comment, int flags, int check, mode_t mode)
{
	int fd, myflags = 0;
	/* compiler claims this variable can be used before initialization... */
	FILE *bfile = NULL;
	struct ast_format *f;
	struct ast_filestream *fs = NULL;
	char *buf = NULL;
	size_t size = 0;
	int format_found = 0;

	if (AST_LIST_LOCK(&formats)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return NULL;
	}

	/* set the O_TRUNC flag if and only if there is no O_APPEND specified */
	/* We really can't use O_APPEND as it will break WAV header updates */
	if (flags & O_APPEND) { 
		flags &= ~O_APPEND;
	} else {
		myflags = O_TRUNC;
	}
	
	myflags |= O_WRONLY | O_CREAT;

	/* XXX need to fix this - we should just do the fopen,
	 * not open followed by fdopen()
	 */
	AST_LIST_TRAVERSE(&formats, f, list) {
		char *fn, *orig_fn = NULL;
		if (fs)
			break;

		if (!exts_compare(f->exts, type))
			continue;
		else
			format_found = 1;

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
		
		if (ast_opt_cache_record_files && (fd > -1)) {
			char *c;

			fclose(bfile);	/* this also closes fd */
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
			strcpy(buf, record_cache_dir);
			strcat(buf, "/");
			strcat(buf, fn);
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
		if (fd > -1) {
			errno = 0;
			fs = get_filestream(f, bfile);
			if (!fs || rewrite_wrapper(fs, comment)) {
				ast_log(LOG_WARNING, "Unable to rewrite %s\n", fn);
				close(fd);
				if (orig_fn) {
					unlink(fn);
					unlink(orig_fn);
				}
				if (fs) {
					ast_closestream(fs);
					fs = NULL;
				}
				continue;
			}
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
			/* If truncated, we'll be at the beginning; if not truncated, then append */
			f->seek(fs, 0, SEEK_END);
		} else if (errno != EEXIST) {
			ast_log(LOG_WARNING, "Unable to open file %s: %s\n", fn, strerror(errno));
			if (orig_fn)
				unlink(orig_fn);
		}
		/* if buf != NULL then fn is already free and pointing to it */
		if (!buf)
			free(fn);
	}

	AST_LIST_UNLOCK(&formats);

	if (!format_found)
		ast_log(LOG_WARNING, "No such format '%s'\n", type);

	return fs;
}

/*!
 * \brief the core of all waitstream() functions
 */
static int waitstream_core(struct ast_channel *c, const char *breakon,
	const char *forward, const char *rewind, int skip_ms,
	int audiofd, int cmdfd,  const char *context)
{
	const char *orig_chan_name = NULL;
	int err = 0;

	if (!breakon)
		breakon = "";
	if (!forward)
		forward = "";
	if (!rewind)
		rewind = "";

	/* Switch the channel to end DTMF frame only. waitstream_core doesn't care about the start of DTMF. */
	ast_set_flag(c, AST_FLAG_END_DTMF_ONLY);

	if (ast_test_flag(c, AST_FLAG_MASQ_NOSTREAM))
		orig_chan_name = ast_strdupa(c->name);

	while (c->stream) {
		int res;
		int ms;

		if (orig_chan_name && strcasecmp(orig_chan_name, c->name)) {
			ast_stopstream(c);
			err = 1;
			break;
		}

		ms = ast_sched_wait(c->sched);

		if (ms < 0 && !c->timingfunc) {
			ast_stopstream(c);
			break;
		}
		if (ms < 0)
			ms = 1000;
		if (cmdfd < 0) {
			res = ast_waitfor(c, ms);
			if (res < 0) {
				ast_log(LOG_WARNING, "Select failed (%s)\n", strerror(errno));
				ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
				return res;
			}
		} else {
			int outfd;
			struct ast_channel *rchan = ast_waitfor_nandfds(&c, 1, &cmdfd, (cmdfd > -1) ? 1 : 0, NULL, &outfd, &ms);
			if (!rchan && (outfd < 0) && (ms)) {
				/* Continue */
				if (errno == EINTR)
					continue;
				ast_log(LOG_WARNING, "Wait failed (%s)\n", strerror(errno));
				ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
				return -1;
			} else if (outfd > -1) { /* this requires cmdfd set */
				/* The FD we were watching has something waiting */
				ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
				return 1;
			}
			/* if rchan is set, it is 'c' */
			res = rchan ? 1 : 0; /* map into 'res' values */
		}
		if (res > 0) {
			struct ast_frame *fr = ast_read(c);
			if (!fr) {
				ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
				return -1;
			}
			switch(fr->frametype) {
			case AST_FRAME_DTMF_END:
				if (context) {
					const char exten[2] = { fr->subclass, '\0' };
					if (ast_exists_extension(c, context, exten, 1, c->cid.cid_num)) {
						res = fr->subclass;
						ast_frfree(fr);
						ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
						return res;
					}
				} else {
					res = fr->subclass;
					if (strchr(forward,res)) {
						ast_stream_fastforward(c->stream, skip_ms);
					} else if (strchr(rewind,res)) {
						ast_stream_rewind(c->stream, skip_ms);
					} else if (strchr(breakon, res)) {
						ast_frfree(fr);
						ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
						return res;
					}					
				}
				break;
			case AST_FRAME_CONTROL:
				switch(fr->subclass) {
				case AST_CONTROL_HANGUP:
				case AST_CONTROL_BUSY:
				case AST_CONTROL_CONGESTION:
					ast_frfree(fr);
					ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);
					return -1;
				case AST_CONTROL_RINGING:
				case AST_CONTROL_ANSWER:
				case AST_CONTROL_VIDUPDATE:
				case AST_CONTROL_SRCUPDATE:
				case AST_CONTROL_HOLD:
				case AST_CONTROL_UNHOLD:
					/* Unimportant */
					break;
				default:
					ast_log(LOG_WARNING, "Unexpected control subclass '%d'\n", fr->subclass);
				}
				break;
			case AST_FRAME_VOICE:
				/* Write audio if appropriate */
				if (audiofd > -1) {
					if (write(audiofd, fr->data, fr->datalen) < 0) {
						ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
					}
				}
			default:
				/* Ignore all others */
				break;
			}
			ast_frfree(fr);
		}
		ast_sched_runq(c->sched);
	}

	ast_clear_flag(c, AST_FLAG_END_DTMF_ONLY);

	return (err || c->_softhangup) ? -1 : 0;
}

int ast_waitstream_fr(struct ast_channel *c, const char *breakon, const char *forward, const char *rewind, int ms)
{
	return waitstream_core(c, breakon, forward, rewind, ms,
		-1 /* no audiofd */, -1 /* no cmdfd */, NULL /* no context */);
}

int ast_waitstream(struct ast_channel *c, const char *breakon)
{
	return waitstream_core(c, breakon, NULL, NULL, 0, -1, -1, NULL);
}

int ast_waitstream_full(struct ast_channel *c, const char *breakon, int audiofd, int cmdfd)
{
	return waitstream_core(c, breakon, NULL, NULL, 0,
		audiofd, cmdfd, NULL /* no context */);
}

int ast_waitstream_exten(struct ast_channel *c, const char *context)
{
	/* Waitstream, with return in the case of a valid 1 digit extension */
	/* in the current or specified context being pressed */

	if (!context)
		context = c->context;
	return waitstream_core(c, NULL, NULL, NULL, 0,
		-1, -1, context);
}

void ast_filestream_frame_freed(struct ast_frame *fr)
{
	struct ast_filestream *fs;

	ast_clear_flag(fr, AST_FRFLAG_FROM_FILESTREAM);

	fs = (struct ast_filestream *) (((char *) fr) - offsetof(struct ast_filestream, fr));

	ao2_ref(fs, -1);
}

/*
 * if the file name is non-empty, try to play it.
 * Return 0 if success, -1 if error, digit if interrupted by a digit.
 * If digits == "" then we can simply check for non-zero.
 */
int ast_stream_and_wait(struct ast_channel *chan, const char *file,
	const char *language, const char *digits)
{
        int res = 0;
        if (!ast_strlen_zero(file)) {
                res =  ast_streamfile(chan, file, language);
                if (!res)
                        res = ast_waitstream(chan, digits);
        }
        return res;
} 

static int show_file_formats(int fd, int argc, char *argv[])
{
#define FORMAT "%-10s %-10s %-20s\n"
#define FORMAT2 "%-10s %-10s %-20s\n"
	struct ast_format *f;
	int count_fmt = 0;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, FORMAT, "Format", "Name", "Extensions");
	        
	if (AST_LIST_LOCK(&formats)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return -1;
	}

	AST_LIST_TRAVERSE(&formats, f, list) {
		ast_cli(fd, FORMAT2, ast_getformatname(f->format), f->name, f->exts);
		count_fmt++;
	}
	AST_LIST_UNLOCK(&formats);
	ast_cli(fd, "%d file formats registered.\n", count_fmt);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int show_file_formats_deprecated(int fd, int argc, char *argv[])
{
#define FORMAT "%-10s %-10s %-20s\n"
#define FORMAT2 "%-10s %-10s %-20s\n"
	struct ast_format *f;
	int count_fmt = 0;
	
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, FORMAT, "Format", "Name", "Extensions");
	
	if (AST_LIST_LOCK(&formats)) {
		ast_log(LOG_WARNING, "Unable to lock format list\n");
		return -1;
	}
	
	AST_LIST_TRAVERSE(&formats, f, list) {
		ast_cli(fd, FORMAT2, ast_getformatname(f->format), f->name, f->exts);
		count_fmt++;
	}
	AST_LIST_UNLOCK(&formats);
	ast_cli(fd, "%d file formats registered.\n", count_fmt);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

char show_file_formats_usage[] = 
"Usage: core show file formats\n"
"       Displays currently registered file formats (if any)\n";

struct ast_cli_entry cli_show_file_formats_deprecated = {
	{ "show", "file", "formats" },
	show_file_formats_deprecated, NULL,
	NULL };

struct ast_cli_entry cli_file[] = {
	{ { "core", "show", "file", "formats" },
	show_file_formats, "Displays file formats",
	show_file_formats_usage, NULL, &cli_show_file_formats_deprecated },
};

int ast_file_init(void)
{
	ast_cli_register_multiple(cli_file, sizeof(cli_file) / sizeof(struct ast_cli_entry));
	return 0;
}
