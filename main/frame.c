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

/*! \file
 *
 * \brief Frame and codec manipulation routines
 *
 * \author Mark Spencer <markster@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/format_cache.h"
#include "asterisk/channel.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"
#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"
#include "asterisk/linkedlists.h"
#include "asterisk/translate.h"
#include "asterisk/dsp.h"
#include "asterisk/file.h"

#if !defined(LOW_MEMORY)
static void frame_cache_cleanup(void *data);

/*! \brief A per-thread cache of frame headers */
AST_THREADSTORAGE_CUSTOM(frame_cache, NULL, frame_cache_cleanup);

/*!
 * \brief Maximum ast_frame cache size
 *
 * In most cases where the frame header cache will be useful, the size
 * of the cache will stay very small.  However, it is not always the case that
 * the same thread that allocates the frame will be the one freeing them, so
 * sometimes a thread will never have any frames in its cache, or the cache
 * will never be pulled from.  For the latter case, we limit the maximum size.
 */
#define FRAME_CACHE_MAX_SIZE	10

/*! \brief This is just so ast_frames, a list head struct for holding a list of
 *  ast_frame structures, is defined. */
AST_LIST_HEAD_NOLOCK(ast_frames, ast_frame);

struct ast_frame_cache {
	struct ast_frames list;
	size_t size;
};
#endif

struct ast_frame ast_null_frame = { AST_FRAME_NULL, };

static struct ast_frame *ast_frame_header_new(void)
{
	struct ast_frame *f;

#if !defined(LOW_MEMORY)
	struct ast_frame_cache *frames;

	if ((frames = ast_threadstorage_get(&frame_cache, sizeof(*frames)))) {
		if ((f = AST_LIST_REMOVE_HEAD(&frames->list, frame_list))) {
			size_t mallocd_len = f->mallocd_hdr_len;

			memset(f, 0, sizeof(*f));
			f->mallocd_hdr_len = mallocd_len;
			frames->size--;
			return f;
		}
	}
	if (!(f = ast_calloc_cache(1, sizeof(*f))))
		return NULL;
#else
	if (!(f = ast_calloc(1, sizeof(*f))))
		return NULL;
#endif

	f->mallocd_hdr_len = sizeof(*f);

	return f;
}

#if !defined(LOW_MEMORY)
static void frame_cache_cleanup(void *data)
{
	struct ast_frame_cache *frames = data;
	struct ast_frame *f;

	while ((f = AST_LIST_REMOVE_HEAD(&frames->list, frame_list)))
		ast_free(f);

	ast_free(frames);
}
#endif

static void __frame_free(struct ast_frame *fr, int cache)
{
	if (!fr->mallocd)
		return;

#if !defined(LOW_MEMORY)
	if (fr->mallocd == AST_MALLOCD_HDR
		&& cache
		&& ast_opt_cache_media_frames) {
		/* Cool, only the header is malloc'd, let's just cache those for now
		 * to keep things simple... */
		struct ast_frame_cache *frames;

		frames = ast_threadstorage_get(&frame_cache, sizeof(*frames));
		if (frames && frames->size < FRAME_CACHE_MAX_SIZE) {
			if (fr->frametype == AST_FRAME_VOICE
				|| fr->frametype == AST_FRAME_VIDEO
				|| fr->frametype == AST_FRAME_IMAGE) {
				ao2_cleanup(fr->subclass.format);
			}

			AST_LIST_INSERT_HEAD(&frames->list, fr, frame_list);
			frames->size++;
			return;
		}
	}
#endif

	if (fr->mallocd & AST_MALLOCD_DATA) {
		if (fr->data.ptr) {
			ast_free(fr->data.ptr - fr->offset);
		}
	}
	if (fr->mallocd & AST_MALLOCD_SRC) {
		ast_free((void *) fr->src);
	}
	if (fr->mallocd & AST_MALLOCD_HDR) {
		if (fr->frametype == AST_FRAME_VOICE
			|| fr->frametype == AST_FRAME_VIDEO
			|| fr->frametype == AST_FRAME_IMAGE) {
			ao2_cleanup(fr->subclass.format);
		}

		ast_free(fr);
	} else {
		fr->mallocd = 0;
	}
}


void ast_frame_free(struct ast_frame *frame, int cache)
{
	struct ast_frame *next;

	while (frame) {
		next = AST_LIST_NEXT(frame, frame_list);
		__frame_free(frame, cache);
		frame = next;
	}
}

void ast_frame_dtor(struct ast_frame *f)
{
	ast_frfree(f);
}

/*!
 * \brief 'isolates' a frame by duplicating non-malloc'ed components
 * (header, src, data).
 * On return all components are malloc'ed
 */
struct ast_frame *ast_frisolate(struct ast_frame *fr)
{
	struct ast_frame *out;
	void *newdata;

	/* if none of the existing frame is malloc'd, let ast_frdup() do it
	   since it is more efficient
	*/
	if (fr->mallocd == 0) {
		return ast_frdup(fr);
	}

	/* if everything is already malloc'd, we are done */
	if ((fr->mallocd & (AST_MALLOCD_HDR | AST_MALLOCD_SRC | AST_MALLOCD_DATA)) ==
	    (AST_MALLOCD_HDR | AST_MALLOCD_SRC | AST_MALLOCD_DATA)) {
		return fr;
	}

	if (!(fr->mallocd & AST_MALLOCD_HDR)) {
		/* Allocate a new header if needed */
		if (!(out = ast_frame_header_new())) {
			return NULL;
		}
		out->frametype = fr->frametype;
		out->subclass = fr->subclass;
		if ((fr->frametype == AST_FRAME_VOICE) || (fr->frametype == AST_FRAME_VIDEO) ||
			(fr->frametype == AST_FRAME_IMAGE)) {
			ao2_bump(out->subclass.format);
		}
		out->datalen = fr->datalen;
		out->samples = fr->samples;
		out->mallocd = AST_MALLOCD_HDR;
		out->offset = fr->offset;
		/* Copy the timing data */
		ast_copy_flags(out, fr, AST_FLAGS_ALL);
		if (ast_test_flag(fr, AST_FRFLAG_HAS_TIMING_INFO)) {
			out->ts = fr->ts;
			out->len = fr->len;
			out->seqno = fr->seqno;
		}
		out->stream_num = fr->stream_num;
	} else {
		out = fr;
	}

	if (fr->src) {
		/* The original frame has a source string */
		if (!(fr->mallocd & AST_MALLOCD_SRC)) {
			/*
			 * The original frame has a non-malloced source string.
			 *
			 * Duplicate the string and put it into the isolated frame
			 * which may also be the original frame.
			 */
			newdata = ast_strdup(fr->src);
			if (!newdata) {
				if (out != fr) {
					ast_frame_free(out, 0);
				}
				return NULL;
			}
			out->src = newdata;
			out->mallocd |= AST_MALLOCD_SRC;
		} else if (out != fr) {
			/* Steal the source string from the original frame. */
			out->src = fr->src;
			fr->src = NULL;
			fr->mallocd &= ~AST_MALLOCD_SRC;
			out->mallocd |= AST_MALLOCD_SRC;
		}
	}

	if (!(fr->mallocd & AST_MALLOCD_DATA))  {
		/* The original frame has a non-malloced data buffer. */
		if (!fr->datalen && fr->frametype != AST_FRAME_TEXT) {
			/* Actually it's just an int so we can simply copy it. */
			out->data.uint32 = fr->data.uint32;
			return out;
		}
		/*
		 * Duplicate the data buffer and put it into the isolated frame
		 * which may also be the original frame.
		 */
		newdata = ast_malloc(fr->datalen + AST_FRIENDLY_OFFSET);
		if (!newdata) {
			if (out != fr) {
				ast_frame_free(out, 0);
			}
			return NULL;
		}
		newdata += AST_FRIENDLY_OFFSET;
		out->offset = AST_FRIENDLY_OFFSET;
		memcpy(newdata, fr->data.ptr, fr->datalen);
		out->data.ptr = newdata;
		out->mallocd |= AST_MALLOCD_DATA;
	} else if (out != fr) {
		/* Steal the data buffer from the original frame. */
		out->data = fr->data;
		memset(&fr->data, 0, sizeof(fr->data));
		fr->mallocd &= ~AST_MALLOCD_DATA;
		out->mallocd |= AST_MALLOCD_DATA;
	}

	return out;
}

struct ast_frame *ast_frdup(const struct ast_frame *f)
{
	struct ast_frame *out = NULL;
	int len, srclen = 0;
	void *buf = NULL;

#if !defined(LOW_MEMORY)
	struct ast_frame_cache *frames;
#endif

	/* Start with standard stuff */
	len = sizeof(*out) + AST_FRIENDLY_OFFSET + f->datalen;
	/* If we have a source, add space for it */
	/*
	 * XXX Watch out here - if we receive a src which is not terminated
	 * properly, we can be easily attacked. Should limit the size we deal with.
	 */
	if (f->src)
		srclen = strlen(f->src);
	if (srclen > 0)
		len += srclen + 1;

#if !defined(LOW_MEMORY)
	if ((frames = ast_threadstorage_get(&frame_cache, sizeof(*frames)))) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&frames->list, out, frame_list) {
			if (out->mallocd_hdr_len >= len) {
				size_t mallocd_len = out->mallocd_hdr_len;

				AST_LIST_REMOVE_CURRENT(frame_list);
				memset(out, 0, sizeof(*out));
				out->mallocd_hdr_len = mallocd_len;
				buf = out;
				frames->size--;
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}
#endif

	if (!buf) {
		if (!(buf = ast_calloc_cache(1, len)))
			return NULL;
		out = buf;
		out->mallocd_hdr_len = len;
	}

	out->frametype = f->frametype;
	out->subclass = f->subclass;
	if ((f->frametype == AST_FRAME_VOICE) || (f->frametype == AST_FRAME_VIDEO) ||
		(f->frametype == AST_FRAME_IMAGE)) {
		ao2_bump(out->subclass.format);
	}
	out->datalen = f->datalen;
	out->samples = f->samples;
	out->delivery = f->delivery;
	/* Even though this new frame was allocated from the heap, we can't mark it
	 * with AST_MALLOCD_HDR, AST_MALLOCD_DATA and AST_MALLOCD_SRC, because that
	 * would cause ast_frfree() to attempt to individually free each of those
	 * under the assumption that they were separately allocated. Since this frame
	 * was allocated in a single allocation, we'll only mark it as if the header
	 * was heap-allocated; this will result in the entire frame being properly freed.
	 */
	out->mallocd = AST_MALLOCD_HDR;
	out->offset = AST_FRIENDLY_OFFSET;
	/* Make sure that empty text frames have a valid data.ptr */
	if (out->datalen || f->frametype == AST_FRAME_TEXT) {
		out->data.ptr = buf + sizeof(*out) + AST_FRIENDLY_OFFSET;
		memcpy(out->data.ptr, f->data.ptr, out->datalen);
	} else {
		out->data.uint32 = f->data.uint32;
	}
	if (srclen > 0) {
		/* This may seem a little strange, but it's to avoid a gcc (4.2.4) compiler warning */
		char *src;
		out->src = buf + sizeof(*out) + AST_FRIENDLY_OFFSET + f->datalen;
		src = (char *) out->src;
		/* Must have space since we allocated for it */
		strcpy(src, f->src);
	}
	ast_copy_flags(out, f, AST_FLAGS_ALL);
	out->ts = f->ts;
	out->len = f->len;
	out->seqno = f->seqno;
	out->stream_num = f->stream_num;
	return out;
}

void ast_swapcopy_samples(void *dst, const void *src, int samples)
{
	int i;
	unsigned short *dst_s = dst;
	const unsigned short *src_s = src;

	for (i = 0; i < samples; i++)
		dst_s[i] = (src_s[i]<<8) | (src_s[i]>>8);
}

void ast_frame_subclass2str(struct ast_frame *f, char *subclass, size_t slen, char *moreinfo, size_t mlen)
{
	switch(f->frametype) {
	case AST_FRAME_DTMF_BEGIN:
		if (slen > 1) {
			subclass[0] = f->subclass.integer;
			subclass[1] = '\0';
		}
		break;
	case AST_FRAME_DTMF_END:
		if (slen > 1) {
			subclass[0] = f->subclass.integer;
			subclass[1] = '\0';
		}
		break;
	case AST_FRAME_CONTROL:
		switch (f->subclass.integer) {
		case AST_CONTROL_HANGUP:
			ast_copy_string(subclass, "Hangup", slen);
			break;
		case AST_CONTROL_RING:
			ast_copy_string(subclass, "Ring", slen);
			break;
		case AST_CONTROL_RINGING:
			ast_copy_string(subclass, "Ringing", slen);
			break;
		case AST_CONTROL_ANSWER:
			ast_copy_string(subclass, "Answer", slen);
			break;
		case AST_CONTROL_BUSY:
			ast_copy_string(subclass, "Busy", slen);
			break;
		case AST_CONTROL_TAKEOFFHOOK:
			ast_copy_string(subclass, "Take Off Hook", slen);
			break;
		case AST_CONTROL_OFFHOOK:
			ast_copy_string(subclass, "Line Off Hook", slen);
			break;
		case AST_CONTROL_CONGESTION:
			ast_copy_string(subclass, "Congestion", slen);
			break;
		case AST_CONTROL_FLASH:
			ast_copy_string(subclass, "Flash", slen);
			break;
		case AST_CONTROL_WINK:
			ast_copy_string(subclass, "Wink", slen);
			break;
		case AST_CONTROL_OPTION:
			ast_copy_string(subclass, "Option", slen);
			break;
		case AST_CONTROL_RADIO_KEY:
			ast_copy_string(subclass, "Key Radio", slen);
			break;
		case AST_CONTROL_RADIO_UNKEY:
			ast_copy_string(subclass, "Unkey Radio", slen);
			break;
		case AST_CONTROL_HOLD:
			ast_copy_string(subclass, "Hold", slen);
			break;
		case AST_CONTROL_UNHOLD:
			ast_copy_string(subclass, "Unhold", slen);
			break;
		case AST_CONTROL_T38_PARAMETERS: {
			char *message = "Unknown";
			if (f->datalen != sizeof(struct ast_control_t38_parameters)) {
				message = "Invalid";
			} else {
				struct ast_control_t38_parameters *parameters = f->data.ptr;
				enum ast_control_t38 state = parameters->request_response;
				if (state == AST_T38_REQUEST_NEGOTIATE)
					message = "Negotiation Requested";
				else if (state == AST_T38_REQUEST_TERMINATE)
					message = "Negotiation Request Terminated";
				else if (state == AST_T38_NEGOTIATED)
					message = "Negotiated";
				else if (state == AST_T38_TERMINATED)
					message = "Terminated";
				else if (state == AST_T38_REFUSED)
					message = "Refused";
			}
			snprintf(subclass, slen, "T38_Parameters/%s", message);
			break;
		}
		case -1:
			ast_copy_string(subclass, "Stop generators", slen);
			break;
		default:
			snprintf(subclass, slen, "Unknown control '%d'", f->subclass.integer);
		}
		break;
	case AST_FRAME_NULL:
		ast_copy_string(subclass, "N/A", slen);
		break;
	case AST_FRAME_IAX:
		/* Should never happen */
		snprintf(subclass, slen, "IAX Frametype %d", f->subclass.integer);
		break;
	case AST_FRAME_BRIDGE_ACTION:
		/* Should never happen */
		snprintf(subclass, slen, "Bridge Frametype %d", f->subclass.integer);
		break;
	case AST_FRAME_BRIDGE_ACTION_SYNC:
		/* Should never happen */
		snprintf(subclass, slen, "Synchronous Bridge Frametype %d", f->subclass.integer);
		break;
	case AST_FRAME_TEXT:
		ast_copy_string(subclass, "N/A", slen);
		if (moreinfo) {
			ast_copy_string(moreinfo, f->data.ptr, mlen);
		}
		break;
	case AST_FRAME_IMAGE:
		snprintf(subclass, slen, "Image format %s\n", ast_format_get_name(f->subclass.format));
		break;
	case AST_FRAME_HTML:
		switch (f->subclass.integer) {
		case AST_HTML_URL:
			ast_copy_string(subclass, "URL", slen);
			if (moreinfo) {
				ast_copy_string(moreinfo, f->data.ptr, mlen);
			}
			break;
		case AST_HTML_DATA:
			ast_copy_string(subclass, "Data", slen);
			break;
		case AST_HTML_BEGIN:
			ast_copy_string(subclass, "Begin", slen);
			break;
		case AST_HTML_END:
			ast_copy_string(subclass, "End", slen);
			break;
		case AST_HTML_LDCOMPLETE:
			ast_copy_string(subclass, "Load Complete", slen);
			break;
		case AST_HTML_NOSUPPORT:
			ast_copy_string(subclass, "No Support", slen);
			break;
		case AST_HTML_LINKURL:
			ast_copy_string(subclass, "Link URL", slen);
			if (moreinfo) {
				ast_copy_string(moreinfo, f->data.ptr, mlen);
			}
			break;
		case AST_HTML_UNLINK:
			ast_copy_string(subclass, "Unlink", slen);
			break;
		case AST_HTML_LINKREJECT:
			ast_copy_string(subclass, "Link Reject", slen);
			break;
		default:
			snprintf(subclass, slen, "Unknown HTML frame '%d'\n", f->subclass.integer);
			break;
		}
		break;
	case AST_FRAME_MODEM:
		switch (f->subclass.integer) {
		case AST_MODEM_T38:
			ast_copy_string(subclass, "T.38", slen);
			break;
		case AST_MODEM_V150:
			ast_copy_string(subclass, "V.150", slen);
			break;
		default:
			snprintf(subclass, slen, "Unknown MODEM frame '%d'\n", f->subclass.integer);
			break;
		}
		break;
	case AST_FRAME_RTCP:
		ast_copy_string(subclass, "RTCP", slen);
	default:
		ast_copy_string(subclass, "Unknown Subclass", slen);
		break;
	}
}

void ast_frame_type2str(enum ast_frame_type frame_type, char *ftype, size_t len)
{
	switch (frame_type) {
	case AST_FRAME_DTMF_BEGIN:
		ast_copy_string(ftype, "DTMF Begin", len);
		break;
	case AST_FRAME_DTMF_END:
		ast_copy_string(ftype, "DTMF End", len);
		break;
	case AST_FRAME_CONTROL:
		ast_copy_string(ftype, "Control", len);
		break;
	case AST_FRAME_NULL:
		ast_copy_string(ftype, "Null Frame", len);
		break;
	case AST_FRAME_IAX:
		/* Should never happen */
		ast_copy_string(ftype, "IAX Specific", len);
		break;
	case AST_FRAME_BRIDGE_ACTION:
		/* Should never happen */
		ast_copy_string(ftype, "Bridge Specific", len);
		break;
	case AST_FRAME_BRIDGE_ACTION_SYNC:
		/* Should never happen */
		ast_copy_string(ftype, "Bridge Specific", len);
		break;
	case AST_FRAME_TEXT:
		ast_copy_string(ftype, "Text", len);
		break;
	case AST_FRAME_TEXT_DATA:
		ast_copy_string(ftype, "Text Data", len);
		break;
	case AST_FRAME_IMAGE:
		ast_copy_string(ftype, "Image", len);
		break;
	case AST_FRAME_HTML:
		ast_copy_string(ftype, "HTML", len);
		break;
	case AST_FRAME_MODEM:
		ast_copy_string(ftype, "Modem", len);
		break;
	case AST_FRAME_VOICE:
		ast_copy_string(ftype, "Voice", len);
		break;
	case AST_FRAME_VIDEO:
		ast_copy_string(ftype, "Video", len);
		break;
	case AST_FRAME_RTCP:
		ast_copy_string(ftype, "RTCP", len);
		break;
	default:
		snprintf(ftype, len, "Unknown Frametype '%u'", frame_type);
		break;
	}
}

/*! Dump a frame for debugging purposes */
void ast_frame_dump(const char *name, struct ast_frame *f, char *prefix)
{
	const char noname[] = "unknown";
	char ftype[40] = "Unknown Frametype";
	char cft[80];
	char subclass[40] = "Unknown Subclass";
	char csub[80];
	char moreinfo[40] = "";
	char cn[60];
	char cp[40];
	char cmn[40];

	if (!name) {
		name = noname;
	}

	if (!f) {
		ast_verb(-1, "%s [ %s (NULL) ] [%s]\n",
			term_color(cp, prefix, COLOR_BRMAGENTA, COLOR_BLACK, sizeof(cp)),
			term_color(cft, "HANGUP", COLOR_BRRED, COLOR_BLACK, sizeof(cft)),
			term_color(cn, name, COLOR_YELLOW, COLOR_BLACK, sizeof(cn)));
		return;
	}
	/* XXX We should probably print one each of voice and video when the format changes XXX */
	if (f->frametype == AST_FRAME_VOICE) {
		return;
	}
	if (f->frametype == AST_FRAME_VIDEO) {
		return;
	}
	if (f->frametype == AST_FRAME_RTCP) {
		return;
	}

	ast_frame_type2str(f->frametype, ftype, sizeof(ftype));
	ast_frame_subclass2str(f, subclass, sizeof(subclass), moreinfo, sizeof(moreinfo));

	if (!ast_strlen_zero(moreinfo))
		ast_verb(-1, "%s [ TYPE: %s (%u) SUBCLASS: %s (%d) '%s' ] [%s]\n",
			    term_color(cp, prefix, COLOR_BRMAGENTA, COLOR_BLACK, sizeof(cp)),
			    term_color(cft, ftype, COLOR_BRRED, COLOR_BLACK, sizeof(cft)),
			    f->frametype,
			    term_color(csub, subclass, COLOR_BRCYAN, COLOR_BLACK, sizeof(csub)),
			    f->subclass.integer,
			    term_color(cmn, moreinfo, COLOR_BRGREEN, COLOR_BLACK, sizeof(cmn)),
			    term_color(cn, name, COLOR_YELLOW, COLOR_BLACK, sizeof(cn)));
	else
		ast_verb(-1, "%s [ TYPE: %s (%u) SUBCLASS: %s (%d) ] [%s]\n",
			    term_color(cp, prefix, COLOR_BRMAGENTA, COLOR_BLACK, sizeof(cp)),
			    term_color(cft, ftype, COLOR_BRRED, COLOR_BLACK, sizeof(cft)),
			    f->frametype,
			    term_color(csub, subclass, COLOR_BRCYAN, COLOR_BLACK, sizeof(csub)),
			    f->subclass.integer,
			    term_color(cn, name, COLOR_YELLOW, COLOR_BLACK, sizeof(cn)));
}

int ast_frame_adjust_volume(struct ast_frame *f, int adjustment)
{
	int count;
	short *fdata = f->data.ptr;
	short adjust_value = abs(adjustment);

	if ((f->frametype != AST_FRAME_VOICE) || !(ast_format_cache_is_slinear(f->subclass.format))) {
		return -1;
	}

	if (!adjustment) {
		return 0;
	}

	for (count = 0; count < f->samples; count++) {
		if (adjustment > 0) {
			ast_slinear_saturated_multiply(&fdata[count], &adjust_value);
		} else if (adjustment < 0) {
			ast_slinear_saturated_divide(&fdata[count], &adjust_value);
		}
	}

	return 0;
}

int ast_frame_slinear_sum(struct ast_frame *f1, struct ast_frame *f2)
{
	int count;
	short *data1, *data2;

	if ((f1->frametype != AST_FRAME_VOICE) || (ast_format_cmp(f1->subclass.format, ast_format_slin) != AST_FORMAT_CMP_NOT_EQUAL))
		return -1;

	if ((f2->frametype != AST_FRAME_VOICE) || (ast_format_cmp(f2->subclass.format, ast_format_slin) != AST_FORMAT_CMP_NOT_EQUAL))
		return -1;

	if (f1->samples != f2->samples)
		return -1;

	for (count = 0, data1 = f1->data.ptr, data2 = f2->data.ptr;
	     count < f1->samples;
	     count++, data1++, data2++)
		ast_slinear_saturated_add(data1, data2);

	return 0;
}

int ast_frame_clear(struct ast_frame *frame)
{
	struct ast_frame *next;

	for (next = AST_LIST_NEXT(frame, frame_list);
		 frame;
		 frame = next, next = frame ? AST_LIST_NEXT(frame, frame_list) : NULL) {
		memset(frame->data.ptr, 0, frame->datalen);
	}
	return 0;
}
