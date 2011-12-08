/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright 2007-2008, Sergio Fadda, Luigi Rizzo
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
 * Video codecs support for console_video.c
 * $Revision$
 */

#include "asterisk.h"
#include "console_video.h"
#include "asterisk/frame.h"
#include "asterisk/utils.h"	/* ast_calloc() */

struct video_out_desc;
struct video_dec_desc;
struct fbuf_t;

/*
 * Each codec is defined by a number of callbacks
 */
/*! \brief initialize the encoder */
typedef int (*encoder_init_f)(AVCodecContext *v);

/*! \brief actually call the encoder */
typedef int (*encoder_encode_f)(struct video_out_desc *v);

/*! \brief encapsulate the bistream in RTP frames */
typedef struct ast_frame *(*encoder_encap_f)(struct fbuf_t *, int mtu,
		struct ast_frame **tail);

/*! \brief inizialize the decoder */
typedef int (*decoder_init_f)(AVCodecContext *enc_ctx);

/*! \brief extract the bitstream from RTP frames and store in the fbuf.
 * return 0 if ok, 1 on error
 */
typedef int (*decoder_decap_f)(struct fbuf_t *b, uint8_t *data, int len);

/*! \brief actually call the decoder */
typedef int (*decoder_decode_f)(struct video_dec_desc *v, struct fbuf_t *b);

struct video_codec_desc {
	const char		*name;		/* format name */
	int			format;		/* AST_FORMAT_* */
	encoder_init_f		enc_init;
	encoder_encap_f		enc_encap;
	encoder_encode_f	enc_run;
	decoder_init_f		dec_init;
	decoder_decap_f		dec_decap;
	decoder_decode_f	dec_run;
};

/*
 * Descriptor for the incoming stream, with multiple buffers for the bitstream
 * extracted from the RTP packets, RTP reassembly info, and a frame buffer
 * for the decoded frame (buf).
 * The descriptor is allocated as the first frame comes in.
 *
 * Incoming payload is stored in one of the dec_in[] buffers, which are
 * emptied by the video thread. These buffers are organized in a circular
 * queue, with dec_in_cur being the buffer in use by the incoming stream,
 * and dec_in_dpy is the one being displayed. When the pointers need to
 * be changed, we synchronize the access to them with dec_lock.
 * When the list is full dec_in_cur = NULL (we cannot store new data),
 * when the list is empty dec_in_dpy = NULL (we cannot display frames).
 */
struct video_dec_desc {
	struct video_codec_desc *d_callbacks;	/* decoder callbacks */
	AVCodecContext          *dec_ctx;	/* information about the codec in the stream */
	AVCodec                 *codec;		/* reference to the codec */
	AVFrame                 *d_frame;	/* place to store the decoded frame */
	AVCodecParserContext    *parser;
	uint16_t 		next_seq;	/* must be 16 bit */
	int                     discard;	/* flag for discard status */
#define N_DEC_IN	3	/* number of incoming buffers */
	struct fbuf_t		*dec_in_cur;	/* buffer being filled in */
	struct fbuf_t		*dec_in_dpy;	/* buffer to display */
	struct fbuf_t dec_in[N_DEC_IN];	/* incoming bitstream, allocated/extended in fbuf_append() */
	struct fbuf_t dec_out;	/* decoded frame, no buffer (data is in AVFrame) */
};

#ifdef debugging_only

/* some debugging code to check the bitstream:
 * declare a bit buffer, initialize it, and fetch data from it.
 */
struct bitbuf {
	const uint8_t *base;
	int	bitsize;	/* total size in bits */
	int	ofs;	/* next bit to read */
};

static struct bitbuf bitbuf_init(const uint8_t *base, int bitsize, int start_ofs)
{
	struct bitbuf a;
	a.base = base;
	a.bitsize = bitsize;
	a.ofs = start_ofs;
	return a;
}

static int bitbuf_left(struct bitbuf *b)
{
	return b->bitsize - b->ofs;
}

static uint32_t getbits(struct bitbuf *b, int n)
{
	int i, ofs;
	const uint8_t *d;
	uint8_t mask;
	uint32_t retval = 0;
	if (n> 31) {
		ast_log(LOG_WARNING, "too many bits %d, max 32\n", n);
		return 0;
	}
	if (n + b->ofs > b->bitsize) {
		ast_log(LOG_WARNING, "bitbuf overflow %d of %d\n", n + b->ofs, b->bitsize);
		n = b->bitsize - b->ofs;
	}
	ofs = 7 - b->ofs % 8;	/* start from msb */
	mask = 1 << ofs;
	d = b->base + b->ofs / 8;	/* current byte */
	for (i=0 ; i < n; i++) {
		retval += retval + (*d & mask ? 1 : 0);	/* shift in new byte */
		b->ofs++;
		mask >>= 1;
		if (mask == 0) {
			d++;
			mask = 0x80;
		}
	}
	return retval;
}

static void check_h261(struct fbuf_t *b)
{
	struct bitbuf a = bitbuf_init(b->data, b->used * 8, 0);
	uint32_t x, y;
	
	x = getbits(&a, 20);	/* PSC, 0000 0000 0000 0001 0000 */
	if (x != 0x10) {
		ast_log(LOG_WARNING, "bad PSC 0x%x\n", x);
		return;
	}
	x = getbits(&a, 5);	/* temporal reference */
	y = getbits(&a, 6);	/* ptype */
	if (0)
	ast_log(LOG_WARNING, "size %d TR %d PTY spl %d doc %d freeze %d %sCIF hi %d\n",
		b->used,
		x,
		(y & 0x20) ? 1 : 0,
		(y & 0x10) ? 1 : 0,
		(y & 0x8) ? 1 : 0,
		(y & 0x4) ? "" : "Q",
		(y & 0x2) ? 1:0);
	while ( (x = getbits(&a, 1)) == 1)
		ast_log(LOG_WARNING, "PSPARE 0x%x\n", getbits(&a, 8));
	// ast_log(LOG_WARNING, "PSPARE 0 - start GOB LAYER\n");
	while ( (x = bitbuf_left(&a)) > 0) {
		// ast_log(LOG_WARNING, "GBSC %d bits left\n", x);
		x = getbits(&a, 16); /* GBSC 0000 0000 0000 0001 */
		if (x != 0x1) {
			ast_log(LOG_WARNING, "bad GBSC 0x%x\n", x);
			break;
		}
		x = getbits(&a, 4);	/* group number */
		y = getbits(&a, 5);	/* gquant */
		if (x == 0) {
			ast_log(LOG_WARNING, "  bad GN %d\n", x);
			break;
		}
		while ( (x = getbits(&a, 1)) == 1)
			ast_log(LOG_WARNING, "GSPARE 0x%x\n", getbits(&a, 8));
		while ( (x = bitbuf_left(&a)) > 0) { /* MB layer */
			break;
		}
	}
}

void dump_buf(struct fbuf_t *b);
void dump_buf(struct fbuf_t *b)
{
	int i, x, last2lines;
	char buf[80];

	last2lines = (b->used - 16) & ~0xf;
	ast_log(LOG_WARNING, "buf size %d of %d\n", b->used, b->size);
	for (i = 0; i < b->used; i++) {
		x = i & 0xf;
		if ( x == 0) {	/* new line */
			if (i != 0)
				ast_log(LOG_WARNING, "%s\n", buf);
			memset(buf, '\0', sizeof(buf));
			sprintf(buf, "%04x: ", i);
		}
		sprintf(buf + 6 + x*3, "%02x ", b->data[i]);
		if (i > 31 && i < last2lines)
			i = last2lines - 1;
	}
	if (buf[0])
		ast_log(LOG_WARNING, "%s\n", buf);
}
#endif /* debugging_only */

/*!
 * Build an ast_frame for a given chunk of data, and link it into
 * the queue, with possibly 'head' bytes at the beginning to
 * fill in some fields later.
 */
static struct ast_frame *create_video_frame(uint8_t *start, uint8_t *end,
	               int format, int head, struct ast_frame *prev)
{
	int len = end-start;
	uint8_t *data;
	struct ast_frame *f;

	data = ast_calloc(1, len+head);
	f = ast_calloc(1, sizeof(*f));
	if (f == NULL || data == NULL) {
		ast_log(LOG_WARNING, "--- frame error f %p data %p len %d format %d\n",
				f, data, len, format);
		if (f)
			ast_free(f);
		if (data)
			ast_free(data);
		return NULL;
	}
	memcpy(data+head, start, len);
	f->data.ptr = data;
	f->mallocd = AST_MALLOCD_DATA | AST_MALLOCD_HDR;
	//f->has_timing_info = 1;
	//f->ts = ast_tvdiff_ms(ast_tvnow(), out->ts);
	f->datalen = len+head;
	f->frametype = AST_FRAME_VIDEO;
	f->subclass = format;
	f->samples = 0;
	f->offset = 0;
	f->src = "Console";
	f->delivery.tv_sec = 0;
	f->delivery.tv_usec = 0;
	f->seqno = 0;
	AST_LIST_NEXT(f, frame_list) = NULL;

	if (prev)
	        AST_LIST_NEXT(prev, frame_list) = f;

	return f;
}


/*
 * Append a chunk of data to a buffer taking care of bit alignment
 * Return 0 on success, != 0 on failure
 */
static int fbuf_append(struct fbuf_t *b, uint8_t *src, int len,
	int sbit, int ebit)
{
	/*
	 * Allocate buffer. ffmpeg wants an extra FF_INPUT_BUFFER_PADDING_SIZE,
	 * and also wants 0 as a buffer terminator to prevent trouble.
	 */
	int need = len + FF_INPUT_BUFFER_PADDING_SIZE;
	int i;
	uint8_t *dst, mask;

	if (b->data == NULL) {
		b->size = need;
		b->used = 0;
		b->ebit = 0;
		b->data = ast_calloc(1, b->size);
	} else if (b->used + need > b->size) {
		b->size = b->used + need;
		b->data = ast_realloc(b->data, b->size);
	}
	if (b->data == NULL) {
		ast_log(LOG_WARNING, "alloc failure for %d, discard\n",
			b->size);
		return 1;
	}
	if (b->used == 0 && b->ebit != 0) {
		ast_log(LOG_WARNING, "ebit not reset at start\n");
		b->ebit = 0;
	}
	dst = b->data + b->used;
	i = b->ebit + sbit;	/* bits to ignore around */
	if (i == 0) {	/* easy case, just append */
		/* do everything in the common block */
	} else if (i == 8) { /* easy too, just handle the overlap byte */
		mask = (1 << b->ebit) - 1;
		/* update the last byte in the buffer */
		dst[-1] &= ~mask;	/* clear bits to ignore */
		dst[-1] |= (*src & mask);	/* append new bits */
		src += 1;	/* skip and prepare for common block */
		len --;
	} else {	/* must shift the new block, not done yet */
		ast_log(LOG_WARNING, "must handle shift %d %d at %d\n",
			b->ebit, sbit, b->used);
		return 1;
	}
	memcpy(dst, src, len);
	b->used += len;
	b->ebit = ebit;
	b->data[b->used] = 0;	/* padding */
	return 0;
}

/*
 * Here starts the glue code for the various supported video codecs.
 * For each of them, we need to provide routines for initialization,
 * calling the encoder, encapsulating the bitstream in ast_frames,
 * extracting payload from ast_frames, and calling the decoder.
 */

/*--- h263+ support --- */

/*! \brief initialization of h263p */
static int h263p_enc_init(AVCodecContext *enc_ctx)
{
	/* modes supported are
	- Unrestricted Motion Vector (annex D)
	- Advanced Prediction (annex F)
	- Advanced Intra Coding (annex I)
	- Deblocking Filter (annex J)
	- Slice Structure (annex K)
	- Alternative Inter VLC (annex S)
	- Modified Quantization (annex T)
	*/
	enc_ctx->flags |=CODEC_FLAG_H263P_UMV; /* annex D */
	enc_ctx->flags |=CODEC_FLAG_AC_PRED; /* annex f ? */
	enc_ctx->flags |=CODEC_FLAG_H263P_SLICE_STRUCT; /* annex k */
	enc_ctx->flags |= CODEC_FLAG_H263P_AIC; /* annex I */

	return 0;
}


/*
 * Create RTP/H.263 fragments to avoid IP fragmentation. We fragment on a
 * PSC or a GBSC, but if we don't find a suitable place just break somewhere.
 * Everything is byte-aligned.
 */
static struct ast_frame *h263p_encap(struct fbuf_t *b, int mtu,
	struct ast_frame **tail)
{
	struct ast_frame *cur = NULL, *first = NULL;
	uint8_t *d = b->data;
	int len = b->used;
	int l = len; /* size of the current fragment. If 0, must look for a psc */

	for (;len > 0; len -= l, d += l) {
		uint8_t *data;
		struct ast_frame *f;
		int i, h;

		if (len >= 3 && d[0] == 0 && d[1] == 0 && d[2] >= 0x80) {
			/* we are starting a new block, so look for a PSC. */
			for (i = 3; i < len - 3; i++) {
				if (d[i] == 0 && d[i+1] == 0 && d[i+2] >= 0x80) {
					l = i;
					break;
				}
			}
		}
		if (l > mtu || l > len) { /* psc not found, split */
			l = MIN(len, mtu);
		}
		if (l < 1 || l > mtu) {
			ast_log(LOG_WARNING, "--- frame error l %d\n", l);
			break;
		}
		
		if (d[0] == 0 && d[1] == 0) { /* we start with a psc */
			h = 0;
		} else { /* no psc, create a header */
			h = 2;
		}

		f = create_video_frame(d, d+l, AST_FORMAT_H263_PLUS, h, cur);
		if (!f)
			break;

		data = f->data.ptr;
		if (h == 0) {	/* we start with a psc */
			data[0] |= 0x04;	// set P == 1, and we are done
		} else {	/* no psc, create a header */
			data[0] = data[1] = 0;	// P == 0
		}

		if (!cur)
			first = f;
		cur = f;
	}

	if (cur)
		cur->subclass |= 1; // RTP Marker

	*tail = cur;	/* end of the list */
	return first;
}

/*! \brief extract the bitstreem from the RTP payload.
 * This is format dependent.
 * For h263+, the format is defined in RFC 2429
 * and basically has a fixed 2-byte header as follows:
 * 5 bits	RR	reserved, shall be 0
 * 1 bit	P	indicate a start/end condition,
 *			in which case the payload should be prepended
 *			by two zero-valued bytes.
 * 1 bit	V	there is an additional VRC header after this header
 * 6 bits	PLEN	length in bytes of extra picture header
 * 3 bits	PEBIT	how many bits to be ignored in the last byte
 *
 * XXX the code below is not complete.
 */
static int h263p_decap(struct fbuf_t *b, uint8_t *data, int len)
{
	int PLEN;

	if (len < 2) {
		ast_log(LOG_WARNING, "invalid framesize %d\n", len);
		return 1;
	}
	PLEN = ( (data[0] & 1) << 5 ) | ( (data[1] & 0xf8) >> 3);

	if (PLEN > 0) {
		data += PLEN;
		len -= PLEN;
	}
	if (data[0] & 4)	/* bit P */
		data[0] = data[1] = 0;
	else {
		data += 2;
		len -= 2;
	}
	return fbuf_append(b, data, len, 0, 0);	/* ignore trail bits */
}


/*
 * generic encoder, used by the various protocols supported here.
 * We assume that the buffer is empty at the beginning.
 */
static int ffmpeg_encode(struct video_out_desc *v)
{
	struct fbuf_t *b = &v->enc_out;
	int i;

	b->used = avcodec_encode_video(v->enc_ctx, b->data, b->size, v->enc_in_frame);
	i = avcodec_encode_video(v->enc_ctx, b->data + b->used, b->size - b->used, NULL); /* delayed frames ? */
	if (i > 0) {
		ast_log(LOG_WARNING, "have %d more bytes\n", i);
		b->used += i;
	}
	return 0;
}

/*
 * Generic decoder, which is used by h263p, h263 and h261 as it simply
 * invokes ffmpeg's decoder.
 * av_parser_parse should merge a randomly chopped up stream into
 * proper frames. After that, if we have a valid frame, we decode it
 * until the entire frame is processed.
 */
static int ffmpeg_decode(struct video_dec_desc *v, struct fbuf_t *b)
{
	uint8_t *src = b->data;
	int srclen = b->used;
	int full_frame = 0;

	if (srclen == 0)	/* no data */
		return 0;
	while (srclen) {
		uint8_t *data;
		int datalen, ret;
		int len = av_parser_parse(v->parser, v->dec_ctx, &data, &datalen, src, srclen, 0, 0);

		src += len;
		srclen -= len;
		/* The parser might return something it cannot decode, so it skips
		 * the block returning no data
		 */
		if (data == NULL || datalen == 0)
			continue;
		ret = avcodec_decode_video(v->dec_ctx, v->d_frame, &full_frame, data, datalen);
		if (full_frame == 1)	/* full frame */
			break;
		if (ret < 0) {
			ast_log(LOG_NOTICE, "Error decoding\n");
			break;
		}
	}
	if (srclen != 0)	/* update b with leftover data */
		memmove(b->data, src, srclen);
	b->used = srclen;
	b->ebit = 0;
	return full_frame;
}

static struct video_codec_desc h263p_codec = {
	.name = "h263p",
	.format = AST_FORMAT_H263_PLUS,
	.enc_init = h263p_enc_init,
	.enc_encap = h263p_encap,
	.enc_run = ffmpeg_encode,
	.dec_init = NULL,
	.dec_decap = h263p_decap,
	.dec_run = ffmpeg_decode
};

/*--- Plain h263 support --------*/

static int h263_enc_init(AVCodecContext *enc_ctx)
{
	/* XXX check whether these are supported */
	enc_ctx->flags |= CODEC_FLAG_H263P_UMV;
	enc_ctx->flags |= CODEC_FLAG_H263P_AIC;
	enc_ctx->flags |= CODEC_FLAG_H263P_SLICE_STRUCT;
	enc_ctx->flags |= CODEC_FLAG_AC_PRED;

	return 0;
}

/*
 * h263 encapsulation is specified in RFC2190. There are three modes
 * defined (A, B, C), with 4, 8 and 12 bytes of header, respectively.
 * The header is made as follows
 *     0.....................|.......................|.............|....31
 *	F:1 P:1 SBIT:3 EBIT:3 SRC:3 I:1 U:1 S:1 A:1 R:4 DBQ:2 TRB:3 TR:8
 * FP = 0- mode A, (only one word of header)
 * FP = 10 mode B, and also means this is an I or P frame
 * FP = 11 mode C, and also means this is a PB frame.
 * SBIT, EBIT nuber of bits to ignore at beginning (msbits) and end (lsbits)
 * SRC  bits 6,7,8 from the h263 PTYPE field
 * I = 0 intra-coded, 1 = inter-coded (bit 9 from PTYPE)
 * U = 1 for Unrestricted Motion Vector (bit 10 from PTYPE)
 * S = 1 for Syntax Based Arith coding (bit 11 from PTYPE)
 * A = 1 for Advanced Prediction (bit 12 from PTYPE)
 * R = reserved, must be 0
 * DBQ = differential quantization, DBQUANT from h263, 0 unless we are using
 *	PB frames
 * TRB = temporal reference for bframes, also 0 unless this is a PB frame
 * TR = temporal reference for P frames, also 0 unless PB frame.
 *
 * Mode B and mode C description omitted.
 *
 * An RTP frame can start with a PSC 0000 0000 0000 0000 1000 0
 * or with a GBSC, which also has the first 17 bits as a PSC.
 * Note - PSC are byte-aligned, GOB not necessarily. PSC start with
 *	PSC:22 0000 0000 0000 0000 1000 00 	picture start code
 *	TR:8   .... ....			temporal reference
 *      PTYPE:13 or more 			ptype...
 * If we don't fragment a GOB SBIT and EBIT = 0.
 * reference, 8 bit) 
 * 
 * The assumption below is that we start with a PSC.
 */
static struct ast_frame *h263_encap(struct fbuf_t *b, int mtu,
		struct ast_frame **tail)
{
	uint8_t *d = b->data;
	int start = 0, i, len = b->used;
	struct ast_frame *f, *cur = NULL, *first = NULL;
	const int pheader_len = 4;	/* Use RFC-2190 Mode A */
	uint8_t h263_hdr[12];	/* worst case, room for a type c header */
	uint8_t *h = h263_hdr;	/* shorthand */

#define H263_MIN_LEN	6
	if (len < H263_MIN_LEN)	/* unreasonably small */
		return NULL;

	memset(h263_hdr, '\0', sizeof(h263_hdr));
	/* Now set the header bytes. Only type A by now,
	 * and h[0] = h[2] = h[3] = 0 by default.
	 * PTYPE starts 30 bits in the picture, so the first useful
	 * bit for us is bit 36 i.e. within d[4] (0 is the msbit).
	 * SRC = d[4] & 0x1c goes into data[1] & 0xe0
	 * I   = d[4] & 0x02 goes into data[1] & 0x10
	 * U   = d[4] & 0x01 goes into data[1] & 0x08
	 * S   = d[5] & 0x80 goes into data[1] & 0x04
	 * A   = d[5] & 0x40 goes into data[1] & 0x02
	 * R   = 0           goes into data[1] & 0x01
	 * Optimizing it, we have
	 */
	h[1] = ( (d[4] & 0x1f) << 3 ) |	/* SRC, I, U */
		( (d[5] & 0xc0) >> 5 );		/* S, A, R */

	/* now look for the next PSC or GOB header. First try to hit
	 * a '0' byte then look around for the 0000 0000 0000 0000 1 pattern
	 * which is both in the PSC and the GBSC.
	 */
	for (i = H263_MIN_LEN, start = 0; start < len; start = i, i += 3) {
		//ast_log(LOG_WARNING, "search at %d of %d/%d\n", i, start, len);
		for (; i < len ; i++) {
			uint8_t x, rpos, lpos;
			int rpos_i;	/* index corresponding to rpos */
			if (d[i] != 0)		/* cannot be in a GBSC */
				continue;
			if (i > len - 1)
				break;
			x = d[i+1];
			if (x == 0)	/* next is equally good */
				continue;
			/* see if around us we can make 16 '0' bits for the GBSC.
			 * Look for the first bit set on the right, and then
			 * see if we have enough 0 on the left.
			 * We are guaranteed to end before rpos == 0
			 */
			for (rpos = 0x80, rpos_i = 8; rpos; rpos >>= 1, rpos_i--)
				if (x & rpos)	/* found the '1' bit in GBSC */
					break;
			x = d[i-1];		/* now look behind */
			for (lpos = rpos; lpos ; lpos >>= 1)
				if (x & lpos)	/* too early, not a GBSC */
					break;
			if (lpos)		/* as i said... */
				continue;
			/* now we have a GBSC starting somewhere in d[i-1],
			 * but it might be not byte-aligned
			 */
			if (rpos == 0x80) {	/* lucky case */
				i = i - 1;
			} else {	/* XXX to be completed */
				ast_log(LOG_WARNING, "unaligned GBSC 0x%x %d\n",
					rpos, rpos_i);
			}
			break;
		}
		/* This frame is up to offset i (not inclusive).
		 * We do not split it yet even if larger than MTU.
		 */
		f = create_video_frame(d + start, d+i, AST_FORMAT_H263,
				pheader_len, cur);

		if (!f)
			break;
		memmove(f->data.ptr, h, 4);	/* copy the h263 header */
		/* XXX to do: if not aligned, fix sbit and ebit,
		 * then move i back by 1 for the next frame
		 */
		if (!cur)
			first = f;
		cur = f;
	}

	if (cur)
		cur->subclass |= 1;	// RTP Marker

	*tail = cur;
	return first;
}

/* XXX We only drop the header here, but maybe we need more. */
static int h263_decap(struct fbuf_t *b, uint8_t *data, int len)
{
	if (len < 4) {
		ast_log(LOG_WARNING, "invalid framesize %d\n", len);
		return 1;	/* error */
	}

	if ( (data[0] & 0x80) == 0) {
		len -= 4;
		data += 4;
	} else {
		ast_log(LOG_WARNING, "unsupported mode 0x%x\n",
			data[0]);
		return 1;
	}
	return fbuf_append(b, data, len, 0, 0);	/* XXX no bit alignment support yet */
}

static struct video_codec_desc h263_codec = {
	.name = "h263",
	.format = AST_FORMAT_H263,
	.enc_init = h263_enc_init,
	.enc_encap = h263_encap,
	.enc_run = ffmpeg_encode,
	.dec_init = NULL,
	.dec_decap = h263_decap,
	.dec_run = ffmpeg_decode
						
};

/*---- h261 support -----*/
static int h261_enc_init(AVCodecContext *enc_ctx)
{
	/* It is important to set rtp_payload_size = 0, otherwise
	 * ffmpeg in h261 mode will produce output that it cannot parse.
	 * Also try to send I frames more frequently than with other codecs.
	 */
	enc_ctx->rtp_payload_size = 0; /* important - ffmpeg fails otherwise */

	return 0;
}

/*
 * The encapsulation of H261 is defined in RFC4587 which obsoletes RFC2032
 * The bitstream is preceded by a 32-bit header word:
 *  SBIT:3 EBIT:3 I:1 V:1 GOBN:4 MBAP:5 QUANT:5 HMVD:5 VMVD:5
 * SBIT and EBIT are the bits to be ignored at beginning and end,
 * I=1 if the stream has only INTRA frames - cannot change during the stream.
 * V=0 if motion vector is not used. Cannot change.
 * GOBN is the GOB number in effect at the start of packet, 0 if we
 *	start with a GOB header
 * QUANT is the quantizer in effect, 0 if we start with GOB header
 * HMVD  reference horizontal motion vector. 10000 is forbidden
 * VMVD  reference vertical motion vector, as above.
 * Packetization should occur at GOB boundaries, and if not possible
 * with MacroBlock fragmentation. However it is likely that blocks
 * are not bit-aligned so we must take care of this.
 */
static struct ast_frame *h261_encap(struct fbuf_t *b, int mtu,
		struct ast_frame **tail)
{
	uint8_t *d = b->data;
	int start = 0, i, len = b->used;
	struct ast_frame *f, *cur = NULL, *first = NULL;
	const int pheader_len = 4;
	uint8_t h261_hdr[4];
	uint8_t *h = h261_hdr;	/* shorthand */
	int sbit = 0, ebit = 0;

#define H261_MIN_LEN 10
	if (len < H261_MIN_LEN)	/* unreasonably small */
		return NULL;

	memset(h261_hdr, '\0', sizeof(h261_hdr));

	/* Similar to the code in h263_encap, but the marker there is longer.
	 * Start a few bytes within the bitstream to avoid hitting the marker
	 * twice. Note we might access the buffer at len, but this is ok because
	 * the caller has it oversized.
	 */
	for (i = H261_MIN_LEN, start = 0; start < len - 1; start = i, i += 4) {
#if 0	/* test - disable packetization */
		i = len;	/* wrong... */
#else
		int found = 0, found_ebit = 0;	/* last GBSC position found */
		for (; i < len ; i++) {
			uint8_t x, rpos, lpos;
			if (d[i] != 0)		/* cannot be in a GBSC */
				continue;
			x = d[i+1];
			if (x == 0)	/* next is equally good */
				continue;
			/* See if around us we find 15 '0' bits for the GBSC.
			 * Look for the first bit set on the right, and then
			 * see if we have enough 0 on the left.
			 * We are guaranteed to end before rpos == 0
			 */
			for (rpos = 0x80, ebit = 7; rpos; ebit--, rpos >>= 1)
				if (x & rpos)	/* found the '1' bit in GBSC */
					break;
			x = d[i-1];		/* now look behind */
			for (lpos = (rpos >> 1); lpos ; lpos >>= 1)
				if (x & lpos)	/* too early, not a GBSC */
					break;
			if (lpos)		/* as i said... */
				continue;
			/* now we have a GBSC starting somewhere in d[i-1],
			 * but it might be not byte-aligned. Just remember it.
			 */
			if (i - start > mtu) /* too large, stop now */
				break;
			found_ebit = ebit;
			found = i;
			i += 4;	/* continue forward */
		}
		if (i >= len) {	/* trim if we went too forward */
			i = len;
			ebit = 0;	/* hopefully... should ask the bitstream ? */
		}
		if (i - start > mtu && found) {
			/* use the previous GBSC, hope is within the mtu */
			i = found;
			ebit = found_ebit;
		}
#endif /* test */
		if (i - start < 4)	/* XXX too short ? */
			continue;
		/* This frame is up to offset i (not inclusive).
		 * We do not split it yet even if larger than MTU.
		 */
		f = create_video_frame(d + start, d+i, AST_FORMAT_H261,
				pheader_len, cur);

		if (!f)
			break;
		/* recompute header with I=0, V=1 */
		h[0] = ( (sbit & 7) << 5 ) | ( (ebit & 7) << 2 ) | 1;
		memmove(f->data.ptr, h, 4);	/* copy the h261 header */
		if (ebit)	/* not aligned, restart from previous byte */
			i--;
		sbit = (8 - ebit) & 7;
		ebit = 0;
		if (!cur)
			first = f;
		cur = f;
	}
	if (cur)
		cur->subclass |= 1;	// RTP Marker

	*tail = cur;
	return first;
}

/*
 * Pieces might be unaligned so we really need to put them together.
 */
static int h261_decap(struct fbuf_t *b, uint8_t *data, int len)
{
	int ebit, sbit;

	if (len < 8) {
		ast_log(LOG_WARNING, "invalid framesize %d\n", len);
		return 1;
	}
	sbit = (data[0] >> 5) & 7;
	ebit = (data[0] >> 2) & 7;
	len -= 4;
	data += 4;
	return fbuf_append(b, data, len, sbit, ebit);
}

static struct video_codec_desc h261_codec = {
	.name = "h261",
	.format = AST_FORMAT_H261,
	.enc_init = h261_enc_init,
	.enc_encap = h261_encap,
	.enc_run = ffmpeg_encode,
	.dec_init = NULL,
	.dec_decap = h261_decap,
	.dec_run = ffmpeg_decode
};

/* mpeg4 support */
static int mpeg4_enc_init(AVCodecContext *enc_ctx)
{
#if 0
	//enc_ctx->flags |= CODEC_FLAG_LOW_DELAY; /*don't use b frames ?*/
	enc_ctx->flags |= CODEC_FLAG_AC_PRED;
	enc_ctx->flags |= CODEC_FLAG_H263P_UMV;
	enc_ctx->flags |= CODEC_FLAG_QPEL;
	enc_ctx->flags |= CODEC_FLAG_4MV;
	enc_ctx->flags |= CODEC_FLAG_GMC;
	enc_ctx->flags |= CODEC_FLAG_LOOP_FILTER;
	enc_ctx->flags |= CODEC_FLAG_H263P_SLICE_STRUCT;
#endif
	enc_ctx->rtp_payload_size = 0; /* important - ffmpeg fails otherwise */
	return 0;
}

/* simplistic encapsulation - just split frames in mtu-size units */
static struct ast_frame *mpeg4_encap(struct fbuf_t *b, int mtu,
	struct ast_frame **tail)
{
	struct ast_frame *f, *cur = NULL, *first = NULL;
	uint8_t *d = b->data;
	uint8_t *end = d + b->used;
	int len;

	for (;d < end; d += len, cur = f) {
		len = MIN(mtu, end - d);
		f = create_video_frame(d, d + len, AST_FORMAT_MP4_VIDEO, 0, cur);
		if (!f)
			break;
		if (!first)
			first = f;
	}
	if (cur)
		cur->subclass |= 1;
	*tail = cur;
	return first;
}

static int mpeg4_decap(struct fbuf_t *b, uint8_t *data, int len)
{
	return fbuf_append(b, data, len, 0, 0);
}

static int mpeg4_decode(struct video_dec_desc *v, struct fbuf_t *b)
{
	int full_frame = 0, datalen = b->used;
	int ret = avcodec_decode_video(v->dec_ctx, v->d_frame, &full_frame,
		b->data, datalen);
	if (ret < 0) {
		ast_log(LOG_NOTICE, "Error decoding\n");
		ret = datalen; /* assume we used everything. */
	}
	datalen -= ret;
	if (datalen > 0)	/* update b with leftover bytes */
		memmove(b->data, b->data + ret, datalen);
	b->used = datalen;
	b->ebit = 0;
	return full_frame;
}

static struct video_codec_desc mpeg4_codec = {
	.name = "mpeg4",
	.format = AST_FORMAT_MP4_VIDEO,
	.enc_init = mpeg4_enc_init,
	.enc_encap = mpeg4_encap,
	.enc_run = ffmpeg_encode,
	.dec_init = NULL,
	.dec_decap = mpeg4_decap,
	.dec_run = mpeg4_decode
};

static int h264_enc_init(AVCodecContext *enc_ctx)
{
	enc_ctx->flags |= CODEC_FLAG_TRUNCATED;
	//enc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
	//enc_ctx->flags2 |= CODEC_FLAG2_FASTPSKIP;
	/* TODO: Maybe we need to add some other flags */
	enc_ctx->rtp_mode = 0;
	enc_ctx->rtp_payload_size = 0;
	enc_ctx->bit_rate_tolerance = enc_ctx->bit_rate;
	return 0;
}

static int h264_dec_init(AVCodecContext *dec_ctx)
{
	dec_ctx->flags |= CODEC_FLAG_TRUNCATED;

	return 0;
}

/*
 * The structure of a generic H.264 stream is:
 * - 0..n 0-byte(s), unused, optional. one zero-byte is always present
 *   in the first NAL before the start code prefix.
 * - start code prefix (3 bytes): 0x000001
 *   (the first bytestream has a 
 *   like these 0x00000001!)
 * - NAL header byte ( F[1] | NRI[2] | Type[5] ) where type != 0
 * - byte-stream
 * - 0..n 0-byte(s) (padding, unused).
 * Segmentation in RTP only needs to be done on start code prefixes.
 * If fragments are too long... we don't support it yet.
 * - encapsulate (or fragment) the byte-stream (with NAL header included)
 */
static struct ast_frame *h264_encap(struct fbuf_t *b, int mtu,
	struct ast_frame **tail)
{
	struct ast_frame *f = NULL, *cur = NULL, *first = NULL;
	uint8_t *d, *start = b->data;
	uint8_t *end = start + b->used;

	/* Search the first start code prefix - ITU-T H.264 sec. B.2,
	 * and move start right after that, on the NAL header byte.
	 */
#define HAVE_NAL(x) (x[-4] == 0 && x[-3] == 0 && x[-2] == 0 && x[-1] == 1)
	for (start += 4; start < end; start++) {
		int ty = start[0] & 0x1f;
		if (HAVE_NAL(start) && ty != 0 && ty != 31)
			break;
	}
	/* if not found, or too short, we just skip the next loop and are done. */

	/* Here follows the main loop to create frames. Search subsequent start
	 * codes, and then possibly fragment the unit into smaller fragments.
	 */
   for (;start < end - 4; start = d) {
	int size;		/* size of current block */
	uint8_t hdr[2];		/* add-on header when fragmenting */
	int ty = 0;

	/* now search next nal */
	for (d = start + 4; d < end; d++) {
		ty = d[0] & 0x1f;
		if (HAVE_NAL(d))
			break;	/* found NAL */
	}
	/* have a block to send. d past the start code unless we overflow */
	if (d >= end) {	/* NAL not found */
		d = end + 4;
	} else if (ty == 0 || ty == 31) { /* found but invalid type, skip */
		ast_log(LOG_WARNING, "skip invalid nal type %d at %d of %d\n",
			ty, d - (uint8_t *)b->data, b->used);
		continue;
	}

	size = d - start - 4;	/* don't count the end */

	if (size < mtu) {	// test - don't fragment
		// Single NAL Unit
		f = create_video_frame(start, d - 4, AST_FORMAT_H264, 0, cur);
		if (!f)
			break;
		if (!first)
			first = f;

		cur = f;
		continue;
	}

	// Fragmented Unit (Mode A: no DON, very weak)
	hdr[0] = (*start & 0xe0) | 28;	/* mark as a fragmentation unit */
	hdr[1] = (*start++ & 0x1f) | 0x80 ;	/* keep type and set START bit */
	size--;		/* skip the NAL header */
	while (size) {
		uint8_t *data;
		int frag_size = MIN(size, mtu);

		f = create_video_frame(start, start+frag_size, AST_FORMAT_H264, 2, cur);
		if (!f)
			break;
		size -= frag_size;	/* skip this data block */
		start += frag_size;

		data = f->data.ptr;
		data[0] = hdr[0];
		data[1] = hdr[1] | (size == 0 ? 0x40 : 0);	/* end bit if we are done */
		hdr[1] &= ~0x80;	/* clear start bit for subsequent frames */
		if (!first)
			first = f;
		cur = f;
	}
    }

	if (cur)
		cur->subclass |= 1;     // RTP Marker

	*tail = cur;

	return first;
}

static int h264_decap(struct fbuf_t *b, uint8_t *data, int len)
{
	/* Start Code Prefix (Annex B in specification) */
	uint8_t scp[] = { 0x00, 0x00, 0x00, 0x01 };
	int retval = 0;
	int type, ofs = 0;

	if (len < 2) {
		ast_log(LOG_WARNING, "--- invalid len %d\n", len);
		return 1;
	}
	/* first of all, check if the packet has F == 0 */
	if (data[0] & 0x80) {
		ast_log(LOG_WARNING, "--- forbidden packet; nal: %02x\n",
			data[0]);
		return 1;
	}

	type = data[0] & 0x1f;
	switch (type) {
	case 0:
	case 31:
		ast_log(LOG_WARNING, "--- invalid type: %d\n", type);
		return 1;
	case 24:
	case 25:
	case 26:
	case 27:
	case 29:
		ast_log(LOG_WARNING, "--- encapsulation not supported : %d\n", type);
		return 1;
	case 28:	/* FU-A Unit */
		if (data[1] & 0x80) { // S == 1, import F and NRI from next
			data[1] &= 0x1f;	/* preserve type */
			data[1] |= (data[0] & 0xe0);	/* import F & NRI */
			retval = fbuf_append(b, scp, sizeof(scp), 0, 0);
			ofs = 1;
		} else {
			ofs = 2;
		}
		break;
	default:	/* From 1 to 23 (Single NAL Unit) */
		retval = fbuf_append(b, scp, sizeof(scp), 0, 0);
	}
	if (!retval)
		retval = fbuf_append(b, data + ofs, len - ofs, 0, 0);
	if (retval)
		ast_log(LOG_WARNING, "result %d\n", retval);
	return retval;
}

static struct video_codec_desc h264_codec = {
	.name = "h264",
	.format = AST_FORMAT_H264,
	.enc_init = h264_enc_init,
	.enc_encap = h264_encap,
	.enc_run = ffmpeg_encode,
	.dec_init = h264_dec_init,
	.dec_decap = h264_decap,
	.dec_run = ffmpeg_decode
};

/*
 * Table of translation between asterisk and ffmpeg formats.
 * We need also a field for read and write (encoding and decoding), because
 * e.g. H263+ uses different codec IDs in ffmpeg when encoding or decoding.
 */
struct _cm {    /* map ffmpeg codec types to asterisk formats */
	uint32_t	ast_format;	/* 0 is a terminator */
	enum CodecID	codec;
	enum { CM_RD = 1, CM_WR = 2, CM_RDWR = 3 } rw;  /* read or write or both ? */
	//struct video_codec_desc *codec_desc;
};

static const struct _cm video_formats[] = {
        { AST_FORMAT_H263_PLUS, CODEC_ID_H263,  CM_RD }, /* incoming H263P ? */
        { AST_FORMAT_H263_PLUS, CODEC_ID_H263P, CM_WR },
        { AST_FORMAT_H263,      CODEC_ID_H263,  CM_RD },
        { AST_FORMAT_H263,      CODEC_ID_H263,  CM_WR },
        { AST_FORMAT_H261,      CODEC_ID_H261,  CM_RDWR },
        { AST_FORMAT_H264,      CODEC_ID_H264,  CM_RDWR },
        { AST_FORMAT_MP4_VIDEO, CODEC_ID_MPEG4, CM_RDWR },
        { 0,                    0, 0 },
};
                

/*! \brief map an asterisk format into an ffmpeg one */
static enum CodecID map_video_format(uint32_t ast_format, int rw)
{
	struct _cm *i;

	for (i = video_formats; i->ast_format != 0; i++)
		if (ast_format & i->ast_format && rw & i->rw && rw & i->rw)
			return i->codec;
	return CODEC_ID_NONE;
}

/* pointers to supported codecs. We assume the first one to be non null. */
static const struct video_codec_desc *supported_codecs[] = {
	&h263p_codec,
	&h264_codec,
	&h263_codec,
	&h261_codec,
	&mpeg4_codec,
	NULL
};

/*
 * Map the AST_FORMAT to the library. If not recognised, fail.
 * This is useful in the input path where we get frames.
 */
static struct video_codec_desc *map_video_codec(int fmt)
{
	int i;

	for (i = 0; supported_codecs[i]; i++)
		if (fmt == supported_codecs[i]->format) {
			ast_log(LOG_WARNING, "using %s for format 0x%x\n",
				supported_codecs[i]->name, fmt);
			return supported_codecs[i];
		}
	return NULL;
}

/*! \brief uninitialize the descriptor for remote video stream */
static struct video_dec_desc *dec_uninit(struct video_dec_desc *v)
{
	int i;

	if (v == NULL)		/* not initialized yet */
		return NULL;
	if (v->parser) {
		av_parser_close(v->parser);
		v->parser = NULL;
	}
	if (v->dec_ctx) {
		avcodec_close(v->dec_ctx);
		av_free(v->dec_ctx);
		v->dec_ctx = NULL;
	}
	if (v->d_frame) {
		av_free(v->d_frame);
		v->d_frame = NULL;
	}
	v->codec = NULL;	/* only a reference */
	v->d_callbacks = NULL;		/* forget the decoder */
	v->discard = 1;		/* start in discard mode */
	for (i = 0; i < N_DEC_IN; i++)
		fbuf_free(&v->dec_in[i]);
	fbuf_free(&v->dec_out);
	ast_free(v);
	return NULL;	/* error, in case someone cares */
}

/*
 * initialize ffmpeg resources used for decoding frames from the network.
 */
static struct video_dec_desc *dec_init(uint32_t the_ast_format)
{
	enum CodecID codec;
	struct video_dec_desc *v = ast_calloc(1, sizeof(*v));
	if (v == NULL)
		return NULL;

	v->discard = 1;

	v->d_callbacks = map_video_codec(the_ast_format);
	if (v->d_callbacks == NULL) {
		ast_log(LOG_WARNING, "cannot find video codec, drop input 0x%x\n", the_ast_format);
		return dec_uninit(v);
	}

	codec = map_video_format(v->d_callbacks->format, CM_RD);

	v->codec = avcodec_find_decoder(codec);
	if (!v->codec) {
		ast_log(LOG_WARNING, "Unable to find the decoder for format %d\n", codec);
		return dec_uninit(v);
	}
	/*
	 * Initialize the codec context.
	 */
	v->dec_ctx = avcodec_alloc_context();
	if (!v->dec_ctx) {
		ast_log(LOG_WARNING, "Cannot allocate the decoder context\n");
		return dec_uninit(v);
	}
	/* XXX call dec_init() ? */
	if (avcodec_open(v->dec_ctx, v->codec) < 0) {
		ast_log(LOG_WARNING, "Cannot open the decoder context\n");
		av_free(v->dec_ctx);
		v->dec_ctx = NULL;
		return dec_uninit(v);
	}

	v->parser = av_parser_init(codec);
	if (!v->parser) {
		ast_log(LOG_WARNING, "Cannot initialize the decoder parser\n");
		return dec_uninit(v);
	}

	v->d_frame = avcodec_alloc_frame();
	if (!v->d_frame) {
		ast_log(LOG_WARNING, "Cannot allocate decoding video frame\n");
		return dec_uninit(v);
	}
        v->dec_in_cur = &v->dec_in[0];	/* buffer for incoming frames */
        v->dec_in_dpy = NULL;      /* nothing to display */

	return v;	/* ok */
}
/*------ end codec specific code -----*/
