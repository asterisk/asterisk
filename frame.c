/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Frame manipulation routines
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/frame.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/cli.h>
#include <asterisk/term.h>
#include <asterisk/utils.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "asterisk.h"

#ifdef TRACE_FRAMES
static int headers = 0;
static struct ast_frame *headerlist = NULL;
AST_MUTEX_DEFINE_STATIC(framelock);
#endif

#define SMOOTHER_SIZE 8000

struct ast_format_list {
	int visible; /* Can we see this entry */
	int bits; /* bitmask value */
	char *name; /* short name */
	char *desc; /* Description */
};

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
		if (s->opt->offset < AST_FRIENDLY_OFFSET)
			ast_log(LOG_WARNING, "Returning a frame of inappropriate offset (%d).",
							s->opt->offset);
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
	f->src = (char *)__FUNCTION__;
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

static struct ast_format_list AST_FORMAT_LIST[] = {
	{ 1, AST_FORMAT_G723_1 , "g723" , "G.723.1"},
	{ 1, AST_FORMAT_GSM, "gsm" , "GSM"},
	{ 1, AST_FORMAT_ULAW, "ulaw", "G.711 u-law" },
	{ 1, AST_FORMAT_ALAW, "alaw", "G.711 A-law" },
	{ 1, AST_FORMAT_G726, "g726", "G.726" },
	{ 1, AST_FORMAT_ADPCM, "adpcm" , "ADPCM"},
	{ 1, AST_FORMAT_SLINEAR, "slin",  "16 bit Signed Linear PCM"},
	{ 1, AST_FORMAT_LPC10, "lpc10", "LPC10" },
	{ 1, AST_FORMAT_G729A, "g729", "G.729A" },
	{ 1, AST_FORMAT_SPEEX, "speex", "SpeeX" },
	{ 1, AST_FORMAT_ILBC, "ilbc", "iLBC"},
	{ 0, 0, "nothing", "undefined" },
	{ 0, 0, "nothing", "undefined" },
	{ 0, 0, "nothing", "undefined" },
	{ 0, 0, "nothing", "undefined" },
	{ 0, AST_FORMAT_MAX_AUDIO, "maxaudio", "Maximum audio format" },
	{ 1, AST_FORMAT_JPEG, "jpeg", "JPEG image"},
	{ 1, AST_FORMAT_PNG, "png", "PNG image"},
	{ 1, AST_FORMAT_H261, "h261", "H.261 Video" },
	{ 1, AST_FORMAT_H263, "h263", "H.263 Video" },
	{ 0, 0, "nothing", "undefined" },
	{ 0, 0, "nothing", "undefined" },
	{ 0, 0, "nothing", "undefined" },
	{ 0, 0, "nothing", "undefined" },
	{ 0, AST_FORMAT_MAX_VIDEO, "maxvideo", "Maximum video format" },
};

struct ast_format_list *ast_get_format_list_index(int index) 
{
	return &AST_FORMAT_LIST[index];
}

struct ast_format_list *ast_get_format_list(size_t *size) 
{
	*size = (sizeof(AST_FORMAT_LIST) / sizeof(struct ast_format_list));
	return AST_FORMAT_LIST;
}

char* ast_getformatname(int format)
{
	int x = 0;
	char *ret = "unknown";
	for (x = 0 ; x < sizeof(AST_FORMAT_LIST) / sizeof(struct ast_format_list) ; x++) {
		if(AST_FORMAT_LIST[x].visible && AST_FORMAT_LIST[x].bits == format) {
			ret = AST_FORMAT_LIST[x].name;
			break;
		}
	}
	return ret;
}

char *ast_getformatname_multiple(char *buf, size_t size, int format) {

	int x = 0;
	unsigned len;
	char *end = buf;
	char *start = buf;
	if (!size) return buf;
	snprintf(end, size, "0x%x (", format);
	len = strlen(end);
	end += len;
	size -= len;
	start = end;
	for (x = 0 ; x < sizeof(AST_FORMAT_LIST) / sizeof(struct ast_format_list) ; x++) {
		if (AST_FORMAT_LIST[x].visible && (AST_FORMAT_LIST[x].bits & format)) {
			snprintf(end, size,"%s|",AST_FORMAT_LIST[x].name);
			len = strlen(end);
			end += len;
			size -= len;
		}
	}
	if (start == end)
		snprintf(start, size, "nothing)");
	else if (size > 1)
		*(end -1) = ')';
	return buf;
}

static struct ast_codec_alias_table {
	char *alias;
	char *realname;

} ast_codec_alias_table[] = {
	{"slinear","slin"},
	{"g723.1","g723"},
};

static char *ast_expand_codec_alias(char *in) {
	int x = 0;

	for (x = 0; x < sizeof(ast_codec_alias_table) / sizeof(struct ast_codec_alias_table) ; x++) {
		if(!strcmp(in,ast_codec_alias_table[x].alias))
			return ast_codec_alias_table[x].realname;
	}
	return in;
}

int ast_getformatbyname(char *name)
{
	int x = 0, all = 0, format = 0;

	all = strcasecmp(name, "all") ? 0 : 1;
	for (x = 0 ; x < sizeof(AST_FORMAT_LIST) / sizeof(struct ast_format_list) ; x++) {
		if(AST_FORMAT_LIST[x].visible && (all || 
										  !strcasecmp(AST_FORMAT_LIST[x].name,name) ||
										  !strcasecmp(AST_FORMAT_LIST[x].name,ast_expand_codec_alias(name)))) {
			format |= AST_FORMAT_LIST[x].bits;
			if(!all)
				break;
		}
	}

	return format;
}

char *ast_codec2str(int codec) {
	int x = 0;
	char *ret = "unknown";
	for (x = 0 ; x < sizeof(AST_FORMAT_LIST) / sizeof(struct ast_format_list) ; x++) {
		if(AST_FORMAT_LIST[x].visible && AST_FORMAT_LIST[x].bits == codec) {
			ret = AST_FORMAT_LIST[x].desc;
			break;
		}
	}
	return ret;
}

static int show_codecs(int fd, int argc, char *argv[])
{
	int i, found=0;
	char hex[25];
	
	if ((argc < 2) || (argc > 3))
		return RESULT_SHOWUSAGE;

	if (getenv("I_AM_NOT_AN_IDIOT") == NULL)
		ast_cli(fd, "Disclaimer: this command is for informational purposes only.\n"
				"\tIt does not indicate anything about your configuration.\n");

	ast_cli(fd, "%11s %9s %10s   TYPE   %5s   %s\n","INT","BINARY","HEX","NAME","DESC");
	ast_cli(fd, "--------------------------------------------------------------------------------\n");
	if ((argc == 2) || (!strcasecmp(argv[1],"audio"))) {
		found = 1;
		for (i=0;i<11;i++) {
			snprintf(hex,25,"(0x%x)",1<<i);
			ast_cli(fd, "%11u (1 << %2d) %10s  audio   %5s   (%s)\n",1 << i,i,hex,ast_getformatname(1<<i),ast_codec2str(1<<i));
		}
	}

	if ((argc == 2) || (!strcasecmp(argv[1],"image"))) {
		found = 1;
		for (i=16;i<18;i++) {
			snprintf(hex,25,"(0x%x)",1<<i);
			ast_cli(fd, "%11u (1 << %2d) %10s  image   %5s   (%s)\n",1 << i,i,hex,ast_getformatname(1<<i),ast_codec2str(1<<i));
		}
	}

	if ((argc == 2) || (!strcasecmp(argv[1],"video"))) {
		found = 1;
		for (i=18;i<20;i++) {
			snprintf(hex,25,"(0x%x)",1<<i);
			ast_cli(fd, "%11u (1 << %2d) %10s  video   %5s   (%s)\n",1 << i,i,hex,ast_getformatname(1<<i),ast_codec2str(1<<i));
		}
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
			ast_cli(fd, "%11u (1 << %2d)  %s\n",1 << i,i,ast_codec2str(1<<i));
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
	char cn[60];
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
		case -1:
			strcpy(subclass, "Stop generators");
			break;
		default:
			snprintf(subclass, sizeof(subclass), "Unknown control '%d'", f->subclass);
		}
		break;
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

void ast_codec_pref_convert(struct ast_codec_pref *pref, char *buf, size_t size, int right) 
{
	int x = 0, differential = (int) 'A', mem = 0;
	char *from = NULL, *to = NULL;

	if(right) {
		from = pref->order;
		to = buf;
		mem = size;
	} else {
		to = pref->order;
		from = buf;
		mem = 32;
	}

	memset(to, 0, mem);
	for (x = 0; x < 32 ; x++) {
		if(!from[x])
			break;
		to[x] = right ? (from[x] + differential) : (from[x] - differential);
	}
}

int ast_codec_pref_string(struct ast_codec_pref *pref, char *buf, size_t size) 
{
	int x = 0, codec = 0; 
	size_t total_len = 0, slen = 0;
	char *formatname = 0;
	
	memset(buf,0,size);
	total_len = size;
	buf[0] = '(';
	total_len--;
	for(x = 0; x < 32 ; x++) {
		if(total_len <= 0)
			break;
		if(!(codec = ast_codec_pref_index(pref,x)))
			break;
		if((formatname = ast_getformatname(codec))) {
			slen = strlen(formatname);
			if(slen > total_len)
				break;
			strncat(buf,formatname,total_len);
			total_len -= slen;
		}
		if(total_len && x < 31 && ast_codec_pref_index(pref , x + 1)) {
			strncat(buf,"|",total_len);
			total_len--;
		}
	}
	if(total_len) {
		strncat(buf,")",total_len);
		total_len--;
	}

	return size - total_len;
}

int ast_codec_pref_index(struct ast_codec_pref *pref, int index) 
{
	int slot = 0;

	
	if((index >= 0) && (index < sizeof(pref->order))) {
		slot = pref->order[index];
	}

	return slot ? AST_FORMAT_LIST[slot-1].bits : 0;
}

/*--- ast_codec_pref_remove: Remove codec from pref list ---*/
void ast_codec_pref_remove(struct ast_codec_pref *pref, int format)
{
	struct ast_codec_pref oldorder;
	int x=0, y=0;
	size_t size = 0;
	int slot = 0;

	if(!pref->order[0])
		return;

	size = sizeof(AST_FORMAT_LIST) / sizeof(struct ast_format_list);

	memcpy(&oldorder,pref,sizeof(struct ast_codec_pref));
	memset(pref,0,sizeof(struct ast_codec_pref));

	for (x = 0; x < size; x++) {
		slot = oldorder.order[x];
		if(! slot)
			break;
		if(AST_FORMAT_LIST[slot-1].bits != format)
			pref->order[y++] = slot;
	}
	
}

/*--- ast_codec_pref_append: Append codec to list ---*/
int ast_codec_pref_append(struct ast_codec_pref *pref, int format)
{
	size_t size = 0;
	int x = 0, newindex = -1;

	ast_codec_pref_remove(pref, format);
	size = sizeof(AST_FORMAT_LIST) / sizeof(struct ast_format_list);

	for (x = 0; x < size; x++) {
		if(AST_FORMAT_LIST[x].bits == format) {
			newindex = x + 1;
			break;
		}
	}

	if(newindex) {
		for (x = 0; x < size; x++) {
			if(!pref->order[x]) {
				pref->order[x] = newindex;
				break;
			}
		}
	}

	return x;
}


/*--- sip_codec_choose: Pick a codec ---*/
int ast_codec_choose(struct ast_codec_pref *pref, int formats, int find_best)
{
	size_t size = 0;
	int x = 0, ret = 0, slot = 0;

	size = sizeof(AST_FORMAT_LIST) / sizeof(struct ast_format_list);
	for (x = 0; x < size; x++) {
		slot = pref->order[x];

		if(!slot)
			break;
		if ( formats & AST_FORMAT_LIST[slot-1].bits ) {
			ret = AST_FORMAT_LIST[slot-1].bits;
			break;
		}
	}
	if(ret)
		return ret;

   	return find_best ? ast_best_codec(formats) : 0;
}

void ast_parse_allow_disallow(struct ast_codec_pref *pref, int *mask, char *list, int allowing) 
{
	int format_i = 0;
	char *next_format = NULL, *last_format = NULL;

	last_format = ast_strdupa(list);
	while(last_format) {
		if((next_format = strchr(last_format, ','))) {
			*next_format = '\0';
			next_format++;
		}
		if ((format_i = ast_getformatbyname(last_format)) > 0) {
			if (mask) {
				if (allowing)
					(*mask) |= format_i;
				else
					(*mask) &= ~format_i;
			}
			/* can't consider 'all' a prefered codec*/
			if(pref && strcasecmp(last_format, "all")) {
				if(allowing)
					ast_codec_pref_append(pref, format_i);
				else
					ast_codec_pref_remove(pref, format_i);
			} else if(!allowing) /* disallow all must clear your prefs or it makes no sense */
				memset(pref, 0, sizeof(struct ast_codec_pref));
		} else
			ast_log(LOG_WARNING, "Cannot %s unknown format '%s'\n", allowing ? "allow" : "disallow", last_format);

		last_format = next_format;
	}
}


