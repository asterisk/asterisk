/*** MODULEINFO
	 <depend>amr_nb</depend>
	 <depend>amr_wb_decoder</depend>
	 <depend>amr_wb_encoder</depend>
***/

#include "asterisk.h"

/* version 1.0 */
/* based on codecs/codec_opus.c */

#include "asterisk/codec.h"             /* for AST_MEDIA_TYPE_AUDIO */
#include "asterisk/format.h"            /* for ast_format_get_attribute_data */
#include "asterisk/frame.h"             /* for ast_frame, etc */
#include "asterisk/linkedlists.h"       /* for AST_LIST_NEXT, etc */
#include "asterisk/logger.h"            /* for ast_log, ast_debug, etc */
#include "asterisk/module.h"
#include "asterisk/translate.h"         /* for ast_trans_pvt, etc */

#include <opencore-amrnb/interf_dec.h>
#include <opencore-amrnb/interf_enc.h>
#include <opencore-amrwb/dec_if.h>
#include <vo-amrwbenc/enc_if.h>

#include "asterisk/amr.h"

#define BUFFER_SAMPLES 16000 /* 1000 milliseconds */

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_amr.h"

struct amr_coder_pvt {
	void *state; /* May be encoder or decoder */
	unsigned int frames;
	int16_t buf[BUFFER_SAMPLES];
};

static int lintoamr_new(struct ast_trans_pvt *pvt)
{
	struct amr_coder_pvt *apvt = pvt->pvt;
	const unsigned int sample_rate = pvt->t->src_codec.sample_rate;

	struct amr_attr *attr = pvt->explicit_dst ? ast_format_get_attribute_data(pvt->explicit_dst) : NULL;
	const int dtx = attr ? attr->vad : 0;

	if (8000 == sample_rate) {
		apvt->state = Encoder_Interface_init(dtx);
	} else if (16000 == sample_rate) {
		apvt->state = E_IF_init();
	}

	if (NULL == apvt->state) {
		ast_log(LOG_ERROR, "Error creating the AMR encoder for %d\n", sample_rate);
		return -1;
	}

	apvt->frames = 0;
	ast_debug(3, "Created encoder (%d -> AMR) %p (Format %p)\n", sample_rate, apvt, pvt->explicit_dst);

	return 0;
}

static int amrtolin_new(struct ast_trans_pvt *pvt)
{
	struct amr_coder_pvt *apvt = pvt->pvt;
	const unsigned int sample_rate = pvt->t->dst_codec.sample_rate;

	if (8000 == sample_rate) {
		apvt->state = Decoder_Interface_init();
	} else if (16000 == sample_rate) {
		apvt->state = D_IF_init();
	}

	if (NULL == apvt->state) {
		ast_log(LOG_ERROR, "Error creating the AMR decoder for %d\n", sample_rate);
		return -1;
	}

	apvt->frames = 0;
	ast_debug(3, "Created decoder (AMR -> %d) %p\n", sample_rate, apvt);

	return 0;
}

static int lintoamr_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct amr_coder_pvt *apvt = pvt->pvt;

	/* XXX We should look at how old the rest of our stream is, and if it
	   is too old, then we should overwrite it entirely, otherwise we can
	   get artifacts of earlier talk that do not belong */
	memcpy(apvt->buf + pvt->samples, f->data.ptr, f->datalen);
	pvt->samples += f->samples;

	return 0;
}

static struct ast_frame *lintoamr_frameout(struct ast_trans_pvt *pvt)
{
	struct amr_coder_pvt *apvt = pvt->pvt;
	const unsigned int sample_rate = pvt->t->src_codec.sample_rate;
	const unsigned int frame_size = sample_rate / 50;
	struct ast_frame *result = NULL;
	struct ast_frame *last = NULL;
	int samples = 0; /* output samples */

	struct amr_attr *attr = ast_format_get_attribute_data(pvt->f.subclass.format);
	const int dtx = attr ? attr->vad : 0;
	const int mode = attr ? attr->mode_current : 0;
	const int aligned = attr ? attr->octet_align : 0;

	while (pvt->samples >= frame_size) {
		struct ast_frame *current;
		const int force_speech = 0; /* ignored by underlying API anyway */
		const short *speech = apvt->buf + samples;
		unsigned char *out = pvt->outbuf.uc + 1;
		int status = -1; /* result value; either error or output bytes */

		if (8000 == sample_rate) {
			status = Encoder_Interface_Encode(apvt->state, mode, speech, out, force_speech);
		} else if (16000 == sample_rate) {
			status = E_IF_encode(apvt->state, mode, speech, out, dtx);
		}

		samples += frame_size;
		pvt->samples -= frame_size;

		if (status <= 0) {
			ast_log(LOG_ERROR, "Error encoding the AMR frame\n");
			current = NULL;
		} else if (((out[0] >> 3) & 0x0f) == 15) { /* NO_DATA (FT=15) */
			current = NULL; /* in case of silence do DTX */
		} else if (aligned) {
			pvt->outbuf.uc[0] = (15 << 4); /* Change-Mode Request (CMR): no */
			/* add one byte, because we added the CMR byte */
			current = ast_trans_frameout(pvt, status + 1, frame_size);
		} else {
			const int another = ((out[0] >> 7) & 0x01);
			const int type    = ((out[0] >> 3) & 0x0f);
			const int quality = ((out[0] >> 2) & 0x01);
			unsigned int i;

			/* to shift in place, clear bits beyond end and at start */
			out[0] = 0;
			out[status] = 0;
			/* shift in place, 6 bits */
			for (i = 0; i < status; i++) {
				out[i] = ((out[i] << 6) | (out[i + 1] >> 2));
			}
			/* restore first two bytes: [ CMR |F| FT |Q] */
			out[0] |= ((type << 7) | (quality << 6));
			pvt->outbuf.uc[0] = ((15 << 4) | (another << 3) | (type >> 1)); /* CMR: no */

			if (8000 == sample_rate) {
				/* https://tools.ietf.org/html/rfc4867#section-3.6 */
				const int octets[16] = { 14, 15, 16, 18, 20, 22, 27, 32, 7 };

				status = octets[type];
			} else if (16000 == sample_rate) {
				/* 3GPP TS 26.201, Table A.1b, plus CMR (4 bits) and F (1 bit) / 8 */
				const int octets[16] = { 18, 24, 33, 37, 41, 47, 51, 59, 61, 7 };

				status = octets[type];
			}

			current = ast_trans_frameout(pvt, status, frame_size);
		}

		if (!current) {
			continue;
		} else if (last) {
			AST_LIST_NEXT(last, frame_list) = current;
		} else {
			result = current;
		}
		last = current;
	}

	/* Move the data at the end of the buffer to the front */
	if (samples) {
		memmove(apvt->buf, apvt->buf + samples, pvt->samples * 2);
	}

	return result;
}

static int amrtolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct amr_coder_pvt *apvt = pvt->pvt;
	const unsigned int sample_rate = pvt->t->dst_codec.sample_rate;
	const unsigned int frame_size = sample_rate / 50;

	struct amr_attr *attr = ast_format_get_attribute_data(f->subclass.format);
	const int aligned = attr ? attr->octet_align : 0;
	const unsigned char mode_next = *(unsigned char *) f->data.ptr >> 4;
	const int bad_frame = 0; /* ignored by underlying API anyway */
	unsigned char temp[f->datalen];
	unsigned char *in = f->data.ptr;

	if (attr) {
		if (8000 == sample_rate && mode_next <= 7) {
			attr->mode_current = mode_next;
		} else if (16000 == sample_rate && mode_next <= 8) {
			attr->mode_current = mode_next;
		}
	}

	/* 
	 * Decoders expect the "MIME storage format" (RFC 4867 chapter 5) which is
	 * octet aligned. On the other hand, the "RTP payload format" (chapter 4)
	 * is prefixed with a change-mode request (CMR; 1 byte in octet-aligned
	 * mode). Therefore, we do +1 to jump over the first byte.
	 */

	if (aligned) {
		in++;
	} else {
		const int another = ((in[0] >> 3) & 0x01);
		const int type    = ((in[0] << 1 | in[1] >> 7) & 0x0f);
		const int quality = ((in[1] >> 6) & 0x01);
		unsigned int i;

		/* shift in place, 2 bits */
		for (i = 1; i < (f->datalen - 1); i++) {
			temp[i] = ((in[i] << 2) | (in[i + 1] >> 6));
		}
		temp[f->datalen - 1] = in[f->datalen - 1] << 2;
		/* restore first byte: [F| FT |Q] */
		temp[0] = ((another << 7) | (type << 3) | (quality << 2));
		in = temp;
	}

	if ((apvt->frames == 0) && (in[0] & 0x80)) {
		apvt->frames = 1;
		ast_log(LOG_WARNING, "multiple frames per packet were not tested\n");
	}

	if (8000 == sample_rate) {
		Decoder_Interface_Decode(apvt->state, in, pvt->outbuf.i16 + pvt->datalen, bad_frame);
	} else if (16000 == sample_rate) {
		D_IF_decode(apvt->state, in, pvt->outbuf.i16 + pvt->datalen, bad_frame);
	}

	pvt->samples += frame_size;
	pvt->datalen += frame_size * 2;

	return 0;
}

static void lintoamr_destroy(struct ast_trans_pvt *pvt)
{
	struct amr_coder_pvt *apvt = pvt->pvt;
	const unsigned int sample_rate = pvt->t->src_codec.sample_rate;

	if (!apvt || !apvt->state) {
		return;
	}

	if (8000 == sample_rate) {
		Encoder_Interface_exit(apvt->state);
	} else if (16000 == sample_rate) {
		E_IF_exit(apvt->state);
	}
	apvt->state = NULL;

	ast_debug(3, "Destroyed encoder (%d -> AMR) %p\n", sample_rate, apvt);
}

static void amrtolin_destroy(struct ast_trans_pvt *pvt)
{
	struct amr_coder_pvt *apvt = pvt->pvt;
	const unsigned int sample_rate = pvt->t->dst_codec.sample_rate;

	if (!apvt || !apvt->state) {
		return;
	}

	if (8000 == sample_rate) {
		Decoder_Interface_exit(apvt->state);
	} else if (16000 == sample_rate) {
		D_IF_exit(apvt->state);
	}
	apvt->state = NULL;

	ast_debug(3, "Destroyed decoder (AMR -> %d) %p\n", sample_rate, apvt);
}

static struct ast_translator amrtolin = {
	.name = "amrtolin",
	.src_codec = {
		.name = "amr",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "slin",
	.newpvt = amrtolin_new,
	.framein = amrtolin_framein,
	.destroy = amrtolin_destroy,
	.sample = amr_sample,
	.desc_size = sizeof(struct amr_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES / 2,
	/* actually: 50 * channels[6] * redundancy[5] * (mode7[31] + CRC[1] + FT[1] + CMR[1]) */
	.buf_size = BUFFER_SAMPLES,
};

static struct ast_translator lintoamr = {
	.name = "lintoamr",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "amr",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "amr",
	.newpvt = lintoamr_new,
	.framein = lintoamr_framein,
	.frameout = lintoamr_frameout,
	.destroy = lintoamr_destroy,
	.sample = slin8_sample,
	.desc_size = sizeof(struct amr_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES / 2,
	.buf_size = BUFFER_SAMPLES,
};

static struct ast_translator amrtolin16 = {
	.name = "amrtolin16",
	.src_codec = {
		.name = "amrwb",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.format = "slin16",
	.newpvt = amrtolin_new,
	.framein = amrtolin_framein,
	.destroy = amrtolin_destroy,
	.sample = amrwb_sample,
	.desc_size = sizeof(struct amr_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	/* actually: 50 * channels[6] * redundancy[5] * (mode8[60] + CRC[1] + FT[1] + CMR[1]) */
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lin16toamr = {
	.name = "lin16toamr",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.dst_codec = {
		.name = "amrwb",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.format = "amrwb",
	.newpvt = lintoamr_new,
	.framein = lintoamr_framein,
	.frameout = lintoamr_frameout,
	.destroy = lintoamr_destroy,
	.sample = slin16_sample,
	.desc_size = sizeof(struct amr_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&amrtolin);
	res |= ast_unregister_translator(&lintoamr);
	res |= ast_unregister_translator(&amrtolin16);
	res |= ast_unregister_translator(&lin16toamr);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_translator(&amrtolin);
	res |= ast_register_translator(&lintoamr);
	res |= ast_register_translator(&amrtolin16);
	res |= ast_register_translator(&lin16toamr);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "AMR Coder/Decoder");
