/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Frame manipulation routines
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/frame.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/term.h>
#include <asterisk/utils.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include "asterisk.h"

#ifdef TRACE_FRAMES
static int headers = 0;
static struct ast_frame *headerlist = NULL;
static ast_mutex_t framelock = AST_MUTEX_INITIALIZER;
#endif

#define SMOOTHER_SIZE 8000

struct ast_smoother {
	int size;
	int format;
	int readdata;
	int optimizablestream;
	int flags;
	float samplesperbyte;
	struct ast_frame f;
	struct timeval delivery;
	char data[SMOOTHER_SIZE];
	char framedata[SMOOTHER_SIZE + AST_FRIENDLY_OFFSET];
	struct ast_frame *opt;
	int len;
};

void ast_smoother_reset(struct ast_smoother *s, int size)
{
	memset(s, 0, sizeof(struct ast_smoother));
	s->size = size;
}

struct ast_smoother *ast_smoother_new(int size)
{
	struct ast_smoother *s;
	if (size < 1)
		return NULL;
	s = malloc(sizeof(struct ast_smoother));
	if (s)
		ast_smoother_reset(s, size);
	return s;
}

int ast_smoother_get_flags(struct ast_smoother *s)
{
	return s->flags;
}

void ast_smoother_set_flags(struct ast_smoother *s, int flags)
{
	s->flags = flags;
}

int ast_smoother_feed(struct ast_smoother *s, struct ast_frame *f)
{
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Huh?  Can't smooth a non-voice frame!\n");
		return -1;
	}
	if (!s->format) {
		s->format = f->subclass;
		s->samplesperbyte = (float)f->samples / (float)f->datalen;
	} else if (s->format != f->subclass) {
		ast_log(LOG_WARNING, "Smoother was working on %d format frames, now trying to feed %d?\n", s->format, f->subclass);
		return -1;
	}
	if (s->len + f->datalen > SMOOTHER_SIZE) {
		ast_log(LOG_WARNING, "Out of smoother space\n");
		return -1;
	}
	if (((f->datalen == s->size) || ((f->datalen < 10) && (s->flags & AST_SMOOTHER_FLAG_G729)))
				 && !s->opt && (f->offset >= AST_MIN_OFFSET)) {
		if (!s->len) {
			/* Optimize by sending the frame we just got
			   on the next read, thus eliminating the douple
			   copy */
			s->opt = f;
			return 0;
		} else {
			s->optimizablestream++;
			if (s->optimizablestream > 10) {
				/* For the past 10 rounds, we have input and output
				   frames of the correct size for this smoother, yet
				   we were unable to optimize because there was still
				   some cruft left over.  Lets just drop the cruft so
				   we can move to a fully optimized path */
				s->len = 0;
				s->opt = f;
				return 0;
			}
		}
	} else 
		s->optimizablestream = 0;
	if (s->flags & AST_SMOOTHER_FLAG_G729) {
		if (s->len % 10) {
			ast_log(LOG_NOTICE, "Dropping extra frame of G.729 since we already have a VAD frame at the end\n");
			return 0;
		}
	}
	memcpy(s->data + s->len, f->data, f->datalen);
	/* If either side is empty, reset the delivery time */
	if (!s->len || (!f->delivery.tv_sec && !f->delivery.tv_usec) ||
			(!s->delivery.tv_sec && !s->delivery.tv_usec))
		s->delivery = f->delivery;
	s->len += f->datalen;
	return 0;
}

struct ast_frame *ast_smoother_read(struct ast_smoother *s)
{
	struct ast_frame *opt;
	int len;
	/* IF we have an optimization frame, send it */
	if (s->opt) {
		opt = s->opt;
		s->opt = NULL;
		return opt;
	}

	/* Make sure we have enough data */
	if (s->len < s->size) {
		/* Or, if this is a G.729 frame with VAD on it, send it immediately anyway */
		if (!((s->flags & AST_SMOOTHER_FLAG_G729) && (s->size % 10)))
			return NULL;
	}
	len = s->size;
	if (len > s->len)
		len = s->len;
	/* Make frame */
	s->f.frametype = AST_FRAME_VOICE;
	s->f.subclass = s->format;
	s->f.data = s->framedata + AST_FRIENDLY_OFFSET;
	s->f.offset = AST_FRIENDLY_OFFSET;
	s->f.datalen = len;
	/* Samples will be improper given VAD, but with VAD the concept really doesn't even exist */
	s->f.samples = len * s->samplesperbyte;
	s->f.delivery = s->delivery;
	/* Fill Data */
	memcpy(s->f.data, s->data, len);
	s->len -= len;
	/* Move remaining data to the front if applicable */
	if (s->len) {
		/* In principle this should all be fine because if we are sending
		   G.729 VAD, the next timestamp will take over anyawy */
		memmove(s->data, s->data + len, s->len);
		if (s->delivery.tv_sec || s->delivery.tv_usec) {
			/* If we have delivery time, increment it, otherwise, leave it at 0 */
			s->delivery.tv_sec += (len * s->samplesperbyte) / 8000.0;
			s->delivery.tv_usec += (((int)(len * s->samplesperbyte)) % 8000) * 125;
			if (s->delivery.tv_usec > 1000000) {
				s->delivery.tv_usec -= 1000000;
				s->delivery.tv_sec += 1;
			}
		}
	}
	/* Return frame */
	return &s->f;
}

void ast_smoother_free(struct ast_smoother *s)
{
	free(s);
}

static struct ast_frame *ast_frame_header_new(void)
{
	struct ast_frame *f;
	f = malloc(sizeof(struct ast_frame));
	if (f)
		memset(f, 0, sizeof(struct ast_frame));
#ifdef TRACE_FRAMES
	if (f) {
		headers++;
		f->prev = NULL;
		ast_mutex_lock(&framelock);
		f->next = headerlist;
		if (headerlist)
			headerlist->prev = f;
		headerlist = f;
		ast_mutex_unlock(&framelock);
	}
#endif	
	return f;
}

/*
 * Important: I should be made more efficient.  Frame headers should
 * most definitely be cached
 */

void ast_frfree(struct ast_frame *fr)
{
	if (fr->mallocd & AST_MALLOCD_DATA) {
		if (fr->data) 
			free(fr->data - fr->offset);
	}
	if (fr->mallocd & AST_MALLOCD_SRC) {
		if (fr->src)
			free(fr->src);
	}
	if (fr->mallocd & AST_MALLOCD_HDR) {
#ifdef TRACE_FRAMES
		headers--;
		ast_mutex_lock(&framelock);
		if (fr->next)
			fr->next->prev = fr->prev;
		if (fr->prev)
			fr->prev->next = fr->next;
		else
			headerlist = fr->next;
		ast_mutex_unlock(&framelock);
#endif			
		free(fr);
	}
}

struct ast_frame *ast_frisolate(struct ast_frame *fr)
{
	struct ast_frame *out;
	if (!(fr->mallocd & AST_MALLOCD_HDR)) {
		/* Allocate a new header if needed */
		out = ast_frame_header_new();
		if (!out) {
			ast_log(LOG_WARNING, "Out of memory\n");
			return NULL;
		}
		out->frametype = fr->frametype;
		out->subclass = fr->subclass;
		out->datalen = 0;
		out->samples = fr->samples;
		out->offset = 0;
		out->src = NULL;
		out->data = NULL;
	} else {
		out = fr;
	}
	if (!(fr->mallocd & AST_MALLOCD_SRC)) {
		if (fr->src)
			out->src = strdup(fr->src);
	} else
		out->src = fr->src;
	if (!(fr->mallocd & AST_MALLOCD_DATA))  {
		out->data = malloc(fr->datalen + AST_FRIENDLY_OFFSET);
		if (!out->data) {
			free(out);
			ast_log(LOG_WARNING, "Out of memory\n");
			return NULL;
		}
		out->data += AST_FRIENDLY_OFFSET;
		out->offset = AST_FRIENDLY_OFFSET;
		out->datalen = fr->datalen;
		memcpy(out->data, fr->data, fr->datalen);
	}
	out->mallocd = AST_MALLOCD_HDR | AST_MALLOCD_SRC | AST_MALLOCD_DATA;
	return out;
}

struct ast_frame *ast_frdup(struct ast_frame *f)
{
	struct ast_frame *out;
	int len, srclen = 0;
	void *buf;
	/* Start with standard stuff */
	len = sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET + f->datalen;
	/* If we have a source, add space for it */
	if (f->src)
		srclen = strlen(f->src);
	if (srclen > 0)
		len += srclen + 1;
	buf = malloc(len);
	if (!buf)
		return NULL;
	out = buf;
	/* Set us as having malloc'd header only, so it will eventually
	   get freed. */
	out->frametype = f->frametype;
	out->subclass = f->subclass;
	out->datalen = f->datalen;
	out->samples = f->samples;
	out->delivery = f->delivery;
	out->mallocd = AST_MALLOCD_HDR;
	out->offset = AST_FRIENDLY_OFFSET;
	out->data = buf + sizeof(struct ast_frame) + AST_FRIENDLY_OFFSET;
	if (srclen > 0) {
		out->src = out->data + f->datalen;
		/* Must have space since we allocated for it */
		strcpy(out->src, f->src);
	} else
		out->src = NULL;
	out->prev = NULL;
	out->next = NULL;
	memcpy(out->data, f->data, out->datalen);	
	return out;
}

struct ast_frame *ast_fr_fdread(int fd)
{
	char buf[65536];
	int res;
	int ttl = sizeof(struct ast_frame);
	struct ast_frame *f = (struct ast_frame *)buf;
	/* Read a frame directly from there.  They're always in the
	   right format. */
	
	while(ttl) {
		res = read(fd, buf, ttl);
		if (res < 0) {
			ast_log(LOG_WARNING, "Bad read on %d: %s\n", fd, strerror(errno));
			return NULL;
		}
		ttl -= res;
	}
	
	/* read the frame header */
	f->mallocd = 0;
	/* Re-write data position */
	f->data = buf + sizeof(struct ast_frame);
	f->offset = 0;
	/* Forget about being mallocd */
	f->mallocd = 0;
	/* Re-write the source */
	f->src = __FUNCTION__;
	if (f->datalen > sizeof(buf) - sizeof(struct ast_frame)) {
		/* Really bad read */
		ast_log(LOG_WARNING, "Strange read (%d bytes)\n", f->datalen);
		return NULL;
	}
	if (f->datalen) {
		if ((res = read(fd, f->data, f->datalen)) != f->datalen) {
			/* Bad read */
			ast_log(LOG_WARNING, "How very strange, expected %d, got %d\n", f->datalen, res);
			return NULL;
		}
	}
	if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP)) {
		return NULL;
	}
	return ast_frisolate(f);
}

/* Some convenient routines for sending frames to/from stream or datagram
   sockets, pipes, etc (maybe even files) */

int ast_fr_fdwrite(int fd, struct ast_frame *frame)
{
	/* Write the frame exactly */
	if (write(fd, frame, sizeof(struct ast_frame)) != sizeof(struct ast_frame)) {
		ast_log(LOG_WARNING, "Write error: %s\n", strerror(errno));
		return -1;
	}
	if (write(fd, frame->data, frame->datalen) != frame->datalen) {
		ast_log(LOG_WARNING, "Write error: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int ast_fr_fdhangup(int fd)
{
	struct ast_frame hangup = {
		AST_FRAME_CONTROL,
		AST_CONTROL_HANGUP
	};
	return ast_fr_fdwrite(fd, &hangup);
}

char* ast_getformatname(int format)
{
	if (format == AST_FORMAT_G723_1) 
		return "G723";
	else if (format == AST_FORMAT_GSM)
		return "GSM";
	else if (format == AST_FORMAT_ULAW)
		return "ULAW";
	else if (format == AST_FORMAT_ALAW)
		return "ALAW";
	else if (format == AST_FORMAT_G726)
		return "G726";
	else if (format == AST_FORMAT_SLINEAR)
		return "SLINR";
	else if (format == AST_FORMAT_LPC10)
		return "LPC10";
	else if (format == AST_FORMAT_ADPCM)
		return "ADPCM";
	else if (format == AST_FORMAT_G729A)
		return "G729A";
	else if (format == AST_FORMAT_SPEEX)
		return "SPEEX";
	else if (format == AST_FORMAT_ILBC)
		return "ILBC";
	else if (format == AST_FORMAT_JPEG)
		return "JPEG";
	else if (format == AST_FORMAT_PNG)
		return "PNG";
	else if (format == AST_FORMAT_H261)
		return "H261";
	else if (format == AST_FORMAT_H263)
		return "H263";
	return "UNKN";
}

char* ast_getformatname_multiple(char *buf, unsigned n, int format) {
	unsigned u=1;
	unsigned len;
	char *b = buf;
	char *start = buf;
	if (!n) return buf;
	snprintf(b,n,"0x%x(",format);
	len = strlen(b);
	b += len;
	n -= len;
	start = b;
	while (u) {
		if (u&format) {
			snprintf(b,n,"%s|",ast_getformatname(u));
			len = strlen(b);
			b += len;
			n -= len;
		}
		u *= 2;
	}
	if (start==b)
		snprintf(start,n,"EMPTY)");
	else if (n>1)
		b[-1]=')';
	return buf;
}

int ast_getformatbyname(char *name)
{
	if (!strcasecmp(name, "g723.1")) 
		return AST_FORMAT_G723_1;
	else if (!strcasecmp(name, "gsm"))
		return AST_FORMAT_GSM;
	else if (!strcasecmp(name, "ulaw"))
		return AST_FORMAT_ULAW;
	else if (!strcasecmp(name, "alaw"))
		return AST_FORMAT_ALAW;
	else if (!strcasecmp(name, "g726"))
		return AST_FORMAT_G726;
	else if (!strcasecmp(name, "slinear"))
		return AST_FORMAT_SLINEAR;
	else if (!strcasecmp(name, "lpc10"))
		return AST_FORMAT_LPC10;
	else if (!strcasecmp(name, "adpcm"))
		return AST_FORMAT_ADPCM;
	else if (!strcasecmp(name, "g729"))
		return AST_FORMAT_G729A;
	else if (!strcasecmp(name, "speex"))
		return AST_FORMAT_SPEEX;
	else if (!strcasecmp(name, "ilbc"))
		return AST_FORMAT_ILBC;
	else if (!strcasecmp(name, "h261"))
		return AST_FORMAT_H261;
	else if (!strcasecmp(name, "h263"))
		return AST_FORMAT_H263;
	else if (!strcasecmp(name, "all"))
		return 0x7FFFFFFF;
	return 0;
}

char *ast_codec2str(int codec) {
	static char codecs[25][30] = {
		/* Audio formats */
		"G.723.1",                    /*  0 */
		"GSM",                        /*  1 */
		"G.711 u-law",                /*  2 */
		"G.711 A-law",                /*  3 */
		"G.726",                      /*  4 */
		"ADPCM",                      /*  5 */
		"16 bit Signed Linear PCM",   /*  6 */
		"LPC10",                      /*  7 */
		"G.729A audio",               /*  8 */
		"SpeeX",                      /*  9 */
		"iLBC",                       /* 10 */
		"undefined",                  /* 11 */
		"undefined",                  /* 12 */
		"undefined",                  /* 13 */
		"undefined",                  /* 14 */
		"Maximum audio format",       /* 15 */
        /* Image formats */
		"JPEG image",                 /* 16 */
		"PNG image",                  /* 17 */
		"H.261 Video",                /* 18 */
		"H.263 Video",                /* 19 */
		"undefined",                  /* 20 */
		"undefined",                  /* 21 */
		"undefined",                  /* 22 */
		"undefined",                  /* 23 */
        "Maximum video format",       /* 24 */
		};
	if ((codec >= 0) && (codec <= 24))
		return codecs[codec];
	else
		return "unknown";
}

static int show_codecs(int fd, int argc, char *argv[])
{
	int i, found=0;

	if ((argc < 2) || (argc > 3))
		return RESULT_SHOWUSAGE;

	if (getenv("I_AM_NOT_AN_IDIOT") == NULL)
		ast_cli(fd, "Disclaimer: this command is for informational purposes only.\n"
				"\tIt does not indicate anything about your configuration.\n");

	if ((argc == 2) || (!strcasecmp(argv[1],"audio"))) {
		found = 1;
		for (i=0;i<11;i++)
			ast_cli(fd, "%11u (1 << %2d)  %s\n",1 << i,i,ast_codec2str(i));
	}

	if ((argc == 2) || (!strcasecmp(argv[1],"image"))) {
		found = 1;
		for (i=16;i<18;i++)
			ast_cli(fd, "%11u (1 << %2d)  %s\n",1 << i,i,ast_codec2str(i));
	}

	if ((argc == 2) || (!strcasecmp(argv[1],"video"))) {
		found = 1;
		for (i=18;i<20;i++)
			ast_cli(fd, "%11u (1 << %2d)  %s\n",1 << i,i,ast_codec2str(i));
	}

	if (! found)
		return RESULT_SHOWUSAGE;
	else
		return RESULT_SUCCESS;
}

static char frame_show_codecs_usage[] =
"Usage: show [audio|video|image] codecs\n"
"       Displays codec mapping\n";

struct ast_cli_entry cli_show_codecs =
{ { "show", "codecs", NULL }, show_codecs, "Shows codecs", frame_show_codecs_usage };
struct ast_cli_entry cli_show_codecs_audio =
{ { "show", "audio", "codecs", NULL }, show_codecs, "Shows audio codecs", frame_show_codecs_usage };
struct ast_cli_entry cli_show_codecs_video =
{ { "show", "video", "codecs", NULL }, show_codecs, "Shows video codecs", frame_show_codecs_usage };
struct ast_cli_entry cli_show_codecs_image =
{ { "show", "image", "codecs", NULL }, show_codecs, "Shows image codecs", frame_show_codecs_usage };

static int show_codec_n(int fd, int argc, char *argv[])
{
	int codec, i, found=0;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	if (sscanf(argv[2],"%d",&codec) != 1)
		return RESULT_SHOWUSAGE;

	for (i=0;i<32;i++)
		if (codec & (1 << i)) {
			found = 1;
			ast_cli(fd, "%11u (1 << %2d)  %s\n",1 << i,i,ast_codec2str(i));
		}

	if (! found)
		ast_cli(fd, "Codec %d not found\n", codec);

	return RESULT_SUCCESS;
}

static char frame_show_codec_n_usage[] =
"Usage: show codec <number>\n"
"       Displays codec mapping\n";

struct ast_cli_entry cli_show_codec_n =
{ { "show", "codec", NULL }, show_codec_n, "Shows a specific codec", frame_show_codec_n_usage };

void ast_frame_dump(char *name, struct ast_frame *f, char *prefix)
{
	char *n = "unknown";
	char ftype[40] = "Unknown Frametype";
	char cft[80];
	char subclass[40] = "Unknown Subclass";
	char csub[80];
	char moreinfo[40] = "";
	char cn[40];
	char cp[40];
	char cmn[40];
	if (name)
		n = name;
	if (!f) {
		ast_verbose("%s [ %s (NULL) ] [%s]\n", 
			term_color(cp, prefix, COLOR_BRMAGENTA, COLOR_BLACK, sizeof(cp)),
			term_color(cft, "HANGUP", COLOR_BRRED, COLOR_BLACK, sizeof(cft)), 
			term_color(cn, n, COLOR_YELLOW, COLOR_BLACK, sizeof(cn)));
		return;
	}
	/* XXX We should probably print one each of voice and video when the format changes XXX */
	if (f->frametype == AST_FRAME_VOICE)
		return;
	if (f->frametype == AST_FRAME_VIDEO)
		return;
	switch(f->frametype) {
	case AST_FRAME_DTMF:
		strcpy(ftype, "DTMF");
		subclass[0] = f->subclass;
		subclass[1] = '\0';
		break;
	case AST_FRAME_CONTROL:
		strcpy(ftype, "Control");
		switch(f->subclass) {
		case AST_CONTROL_HANGUP:
			strcpy(subclass, "Hangup");
			break;
		case AST_CONTROL_RING:
			strcpy(subclass, "Ring");
			break;
		case AST_CONTROL_RINGING:
			strcpy(subclass, "Ringing");
			break;
		case AST_CONTROL_ANSWER:
			strcpy(subclass, "Answer");
			break;
		case AST_CONTROL_BUSY:
			strcpy(subclass, "Busy");
			break;
		case AST_CONTROL_TAKEOFFHOOK:
			strcpy(subclass, "Take Off Hook");
			break;
		case AST_CONTROL_OFFHOOK:
			strcpy(subclass, "Line Off Hook");
			break;
		case AST_CONTROL_CONGESTION:
			strcpy(subclass, "Congestion");
			break;
		case AST_CONTROL_FLASH:
			strcpy(subclass, "Flash");
			break;
		case AST_CONTROL_WINK:
			strcpy(subclass, "Wink");
			break;
		case AST_CONTROL_OPTION:
			strcpy(subclass, "Option");
			break;
		case AST_CONTROL_RADIO_KEY:
			strcpy(subclass, "Key Radio");
			break;
		case AST_CONTROL_RADIO_UNKEY:
			strcpy(subclass, "Unkey Radio");
			break;
		default:
			snprintf(subclass, sizeof(subclass), "Unknown control '%d'", f->subclass);
		}
	case AST_FRAME_NULL:
		strcpy(ftype, "Null Frame");
		strcpy(subclass, "N/A");
		break;
	case AST_FRAME_IAX:
		/* Should never happen */
		strcpy(ftype, "IAX Specific");
		snprintf(subclass, sizeof(subclass), "IAX Frametype %d", f->subclass);
		break;
	case AST_FRAME_TEXT:
		strcpy(ftype, "Text");
		strcpy(subclass, "N/A");
		strncpy(moreinfo, f->data, sizeof(moreinfo) - 1);
		break;
	case AST_FRAME_IMAGE:
		strcpy(ftype, "Image");
		snprintf(subclass, sizeof(subclass), "Image format %s\n", ast_getformatname(f->subclass));
		break;
	case AST_FRAME_HTML:
		strcpy(ftype, "HTML");
		switch(f->subclass) {
		case AST_HTML_URL:
			strcpy(subclass, "URL");
			strncpy(moreinfo, f->data, sizeof(moreinfo) - 1);
			break;
		case AST_HTML_DATA:
			strcpy(subclass, "Data");
			break;
		case AST_HTML_BEGIN:
			strcpy(subclass, "Begin");
			break;
		case AST_HTML_END:
			strcpy(subclass, "End");
			break;
		case AST_HTML_LDCOMPLETE:
			strcpy(subclass, "Load Complete");
			break;
		case AST_HTML_NOSUPPORT:
			strcpy(subclass, "No Support");
			break;
		case AST_HTML_LINKURL:
			strcpy(subclass, "Link URL");
			strncpy(moreinfo, f->data, sizeof(moreinfo) - 1);
			break;
		case AST_HTML_UNLINK:
			strcpy(subclass, "Unlink");
			break;
		case AST_HTML_LINKREJECT:
			strcpy(subclass, "Link Reject");
			break;
		default:
			snprintf(subclass, sizeof(subclass), "Unknown HTML frame '%d'\n", f->subclass);
			break;
		}
		break;
	default:
		snprintf(ftype, sizeof(ftype), "Unknown Frametype '%d'", f->frametype);
	}
	if (!ast_strlen_zero(moreinfo))
		ast_verbose("%s [ TYPE: %s (%d) SUBCLASS: %s (%d) '%s' ] [%s]\n",  
			term_color(cp, prefix, COLOR_BRMAGENTA, COLOR_BLACK, sizeof(cp)),
			term_color(cft, ftype, COLOR_BRRED, COLOR_BLACK, sizeof(cft)),
			f->frametype, 
			term_color(csub, subclass, COLOR_BRCYAN, COLOR_BLACK, sizeof(csub)),
			f->subclass, 
			term_color(cmn, moreinfo, COLOR_BRGREEN, COLOR_BLACK, sizeof(cmn)),
			term_color(cn, n, COLOR_YELLOW, COLOR_BLACK, sizeof(cn)));
	else
		ast_verbose("%s [ TYPE: %s (%d) SUBCLASS: %s (%d) ] [%s]\n",  
			term_color(cp, prefix, COLOR_BRMAGENTA, COLOR_BLACK, sizeof(cp)),
			term_color(cft, ftype, COLOR_BRRED, COLOR_BLACK, sizeof(cft)),
			f->frametype, 
			term_color(csub, subclass, COLOR_BRCYAN, COLOR_BLACK, sizeof(csub)),
			f->subclass, 
			term_color(cn, n, COLOR_YELLOW, COLOR_BLACK, sizeof(cn)));

}


#ifdef TRACE_FRAMES
static int show_frame_stats(int fd, int argc, char *argv[])
{
	struct ast_frame *f;
	int x=1;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, "     Framer Statistics     \n");
	ast_cli(fd, "---------------------------\n");
	ast_cli(fd, "Total allocated headers: %d\n", headers);
	ast_cli(fd, "Queue Dump:\n");
	ast_mutex_lock(&framelock);
	for (f=headerlist; f; f = f->next) {
		ast_cli(fd, "%d.  Type %d, subclass %d from %s\n", x++, f->frametype, f->subclass, f->src ? f->src : "<Unknown>");
	}
	ast_mutex_unlock(&framelock);
	return RESULT_SUCCESS;
}

static char frame_stats_usage[] =
"Usage: show frame stats\n"
"       Displays debugging statistics from framer\n";

struct ast_cli_entry cli_frame_stats =
{ { "show", "frame", "stats", NULL }, show_frame_stats, "Shows frame statistics", frame_stats_usage };
#endif

int init_framer(void)
{
#ifdef TRACE_FRAMES
	ast_cli_register(&cli_frame_stats);
#endif
	ast_cli_register(&cli_show_codecs);
	ast_cli_register(&cli_show_codecs_audio);
	ast_cli_register(&cli_show_codecs_video);
	ast_cli_register(&cli_show_codecs_image);
	ast_cli_register(&cli_show_codec_n);
	return 0;	
}
