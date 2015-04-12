/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Built-in supported codecs
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/codec.h"
#include "asterisk/format.h"
#include "asterisk/format_cache.h"
#include "asterisk/frame.h"

enum frame_type {
	TYPE_HIGH,     /* 0x0 */
	TYPE_LOW,      /* 0x1 */
	TYPE_SILENCE,  /* 0x2 */
	TYPE_DONTSEND  /* 0x3 */
};

#define TYPE_MASK 0x3

static int g723_len(unsigned char buf)
{
	enum frame_type type = buf & TYPE_MASK;

	switch(type) {
	case TYPE_DONTSEND:
		return 0;
		break;
	case TYPE_SILENCE:
		return 4;
		break;
	case TYPE_HIGH:
		return 24;
		break;
	case TYPE_LOW:
		return 20;
		break;
	default:
		ast_log(LOG_WARNING, "Badly encoded frame (%u)\n", type);
	}
	return -1;
}

static int g723_samples(struct ast_frame *frame)
{
	unsigned char *buf = frame->data.ptr;
	int pos = 0, samples = 0, res;

	while(pos < frame->datalen) {
		res = g723_len(buf[pos]);
		if (res <= 0)
			break;
		samples += 240;
		pos += res;
	}

	return samples;
}

static int g723_length(unsigned int samples)
{
	return (samples / 240) * 20;
}

static struct ast_codec g723 = {
	.name = "g723",
	.description = "G.723.1",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 30,
	.maximum_ms = 300,
	.default_ms = 30,
	.minimum_bytes = 20,
	.samples_count = g723_samples,
	.get_length = g723_length,
};

static int none_samples(struct ast_frame *frame)
{
	return frame->datalen;
}

static int none_length(unsigned int samples) {
	return samples;
}

static struct ast_codec none = {
	.name = "none",
	.description = "<Null> codec",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000, /* This must have some sample rate to prevent divide by 0 */
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
	.minimum_bytes = 20,
	.samples_count = none_samples,
	.get_length = none_length,
};

static int ulaw_samples(struct ast_frame *frame)
{
	return frame->datalen;
}

static int ulaw_length(unsigned int samples)
{
	return samples;
}

static struct ast_codec ulaw = {
	.name = "ulaw",
	.description = "G.711 u-law",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
	.minimum_bytes = 80,
	.samples_count = ulaw_samples,
	.get_length = ulaw_length,
	.smooth = 1,
};

static struct ast_codec alaw = {
	.name = "alaw",
	.description = "G.711 a-law",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
	.minimum_bytes = 80,
	.samples_count = ulaw_samples,
	.get_length = ulaw_length,
	.smooth = 1,
};

static int gsm_samples(struct ast_frame *frame)
{
	return 160 * (frame->datalen / 33);
}

static int gsm_length(unsigned int samples)
{
	return (samples / 160) * 33;
}

static struct ast_codec gsm = {
	.name = "gsm",
	.description = "GSM",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 20,
	.maximum_ms = 300,
	.default_ms = 20,
	.minimum_bytes = 33,
	.samples_count = gsm_samples,
	.get_length = gsm_length,
	.smooth = 1,
};

static int g726_samples(struct ast_frame *frame)
{
	return frame->datalen * 2;
}

static int g726_length(unsigned int samples)
{
	return samples / 2;
}

static struct ast_codec g726rfc3551 = {
	.name = "g726",
	.description = "G.726 RFC3551",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 300,
	.default_ms = 20,
	.minimum_bytes = 40,
	.samples_count = g726_samples,
	.get_length = g726_length,
	.smooth = 1,
};

static struct ast_codec g726aal2 = {
	.name = "g726aal2",
	.description = "G.726 AAL2",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 300,
	.default_ms = 20,
	.minimum_bytes = 40,
	.samples_count = g726_samples,
	.get_length = g726_length,
	.smooth = 1,
};

static struct ast_codec adpcm = {
	.name = "adpcm",
	.description = "Dialogic ADPCM",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 300,
	.default_ms = 20,
	.minimum_bytes = 40,
	.samples_count = g726_samples,
	.get_length = g726_length,
	.smooth = 1,
};

static int slin_samples(struct ast_frame *frame)
{
	return frame->datalen / 2;
}

static int slin_length(unsigned int samples)
{
	return samples * 2;
}

static struct ast_codec slin8 = {
	.name = "slin",
	.description = "16 bit Signed Linear PCM",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 70,
	.default_ms = 20,
	.minimum_bytes = 160,
	.samples_count = slin_samples,
	.get_length = slin_length,
	.smooth = 1,
};

static struct ast_codec slin12 = {
	.name = "slin",
	.description = "16 bit Signed Linear PCM (12kHz)",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 12000,
	.minimum_ms = 10,
	.maximum_ms = 70,
	.default_ms = 20,
	.minimum_bytes = 240,
	.samples_count = slin_samples,
	.get_length = slin_length,
	.smooth = 1,
};

static struct ast_codec slin16 = {
	.name = "slin",
	.description = "16 bit Signed Linear PCM (16kHz)",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 16000,
	.minimum_ms = 10,
	.maximum_ms = 70,
	.default_ms = 20,
	.minimum_bytes = 320,
	.samples_count = slin_samples,
	.get_length = slin_length,
	.smooth = 1,
};

static struct ast_codec slin24 = {
	.name = "slin",
	.description = "16 bit Signed Linear PCM (24kHz)",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 24000,
	.minimum_ms = 10,
	.maximum_ms = 70,
	.default_ms = 20,
	.minimum_bytes = 480,
	.samples_count = slin_samples,
	.get_length = slin_length,
	.smooth = 1,
};

static struct ast_codec slin32 = {
	.name = "slin",
	.description = "16 bit Signed Linear PCM (32kHz)",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 32000,
	.minimum_ms = 10,
	.maximum_ms = 70,
	.default_ms = 20,
	.minimum_bytes = 640,
	.samples_count = slin_samples,
	.get_length = slin_length,
	.smooth = 1,
};

static struct ast_codec slin44 = {
	.name = "slin",
	.description = "16 bit Signed Linear PCM (44kHz)",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 44100,
	.minimum_ms = 10,
	.maximum_ms = 70,
	.default_ms = 20,
	.minimum_bytes = 882,
	.samples_count = slin_samples,
	.get_length = slin_length,
	.smooth = 1,
};

static struct ast_codec slin48 = {
	.name = "slin",
	.description = "16 bit Signed Linear PCM (48kHz)",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 48000,
	.minimum_ms = 10,
	.maximum_ms = 70,
	.default_ms = 20,
	.minimum_bytes = 960,
	.samples_count = slin_samples,
	.get_length = slin_length,
	.smooth = 1,
};

static struct ast_codec slin96 = {
	.name = "slin",
	.description = "16 bit Signed Linear PCM (96kHz)",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 96000,
	.minimum_ms = 10,
	.maximum_ms = 70,
	.default_ms = 20,
	.minimum_bytes = 1920,
	.samples_count = slin_samples,
	.get_length = slin_length,
	.smooth = 1,
};

static struct ast_codec slin192 = {
	.name = "slin",
	.description = "16 bit Signed Linear PCM (192kHz)",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 192000,
	.minimum_ms = 10,
	.maximum_ms = 70,
	.default_ms = 20,
	.minimum_bytes = 3840,
	.samples_count = slin_samples,
	.get_length = slin_length,
	.smooth = 1,
};

static int lpc10_samples(struct ast_frame *frame)
{
	int samples = 22 * 8;

	/* assumes that the RTP packet contains one LPC10 frame */
	samples += (((char *)(frame->data.ptr))[7] & 0x1) * 8;

	return samples;
}

static struct ast_codec lpc10 = {
	.name = "lpc10",
	.description = "LPC10",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 20,
	.maximum_ms = 20,
	.default_ms = 20,
	.minimum_bytes = 7,
	.samples_count = lpc10_samples,
	.smooth = 1,
};

static int g729_samples(struct ast_frame *frame)
{
	return frame->datalen * 8;
}

static int g729_length(unsigned int samples)
{
	return samples / 8;
}

static struct ast_codec g729a = {
	.name = "g729",
	.description = "G.729A",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 230,
	.default_ms = 20,
	.minimum_bytes = 10,
	.samples_count = g729_samples,
	.get_length = g729_length,
	.smooth = 1,
};

static unsigned char get_n_bits_at(unsigned char *data, int n, int bit)
{
	int byte = bit / 8;       /* byte containing first bit */
	int rem = 8 - (bit % 8);  /* remaining bits in first byte */
	unsigned char ret = 0;

	if (n <= 0 || n > 8)
		return 0;

	if (rem < n) {
		ret = (data[byte] << (n - rem));
		ret |= (data[byte + 1] >> (8 - n + rem));
	} else {
		ret = (data[byte] >> (rem - n));
	}

	return (ret & (0xff >> (8 - n)));
}

static int speex_get_wb_sz_at(unsigned char *data, int len, int bit)
{
	static const int SpeexWBSubModeSz[] = {
		4, 36, 112, 192,
		352, 0, 0, 0 };
	int off = bit;
	unsigned char c;

	/* skip up to two wideband frames */
	if (((len * 8 - off) >= 5) &&
		get_n_bits_at(data, 1, off)) {
		c = get_n_bits_at(data, 3, off + 1);
		off += SpeexWBSubModeSz[c];

		if (((len * 8 - off) >= 5) &&
			get_n_bits_at(data, 1, off)) {
			c = get_n_bits_at(data, 3, off + 1);
			off += SpeexWBSubModeSz[c];

			if (((len * 8 - off) >= 5) &&
				get_n_bits_at(data, 1, off)) {
				ast_log(LOG_WARNING, "Encountered corrupt speex frame; too many wideband frames in a row.\n");
				return -1;
			}
		}

	}
	return off - bit;
}

static int speex_samples(unsigned char *data, int len)
{
	static const int SpeexSubModeSz[] = {
		5, 43, 119, 160,
		220, 300, 364, 492,
		79, 0, 0, 0,
		0, 0, 0, 0 };
	static const int SpeexInBandSz[] = {
		1, 1, 4, 4,
		4, 4, 4, 4,
		8, 8, 16, 16,
		32, 32, 64, 64 };
	int bit = 0;
	int cnt = 0;
	int off;
	unsigned char c;

	while ((len * 8 - bit) >= 5) {
		/* skip wideband frames */
		off = speex_get_wb_sz_at(data, len, bit);
		if (off < 0)  {
			ast_log(LOG_WARNING, "Had error while reading wideband frames for speex samples\n");
			break;
		}
		bit += off;

		if ((len * 8 - bit) < 5)
			break;

		/* get control bits */
		c = get_n_bits_at(data, 5, bit);
		bit += 5;

		if (c == 15) {
			/* terminator */
			break;
		} else if (c == 14) {
			/* in-band signal; next 4 bits contain signal id */
			c = get_n_bits_at(data, 4, bit);
			bit += 4;
			bit += SpeexInBandSz[c];
		} else if (c == 13) {
			/* user in-band; next 4 bits contain msg len */
			c = get_n_bits_at(data, 4, bit);
			bit += 4;
			/* after which it's 5-bit signal id + c bytes of data */
			bit += 5 + c * 8;
		} else if (c > 8) {
			/* unknown */
			ast_log(LOG_WARNING, "Unknown speex control frame %d\n", c);
			break;
		} else {
			/* skip number bits for submode (less the 5 control bits) */
			bit += SpeexSubModeSz[c] - 5;
			cnt += 160; /* new frame */
		}
	}
	return cnt;
}

static int speex8_samples(struct ast_frame *frame)
{
	return speex_samples(frame->data.ptr, frame->datalen);
}

static struct ast_codec speex8 = {
	.name = "speex",
	.description = "SpeeX",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 60,
	.default_ms = 20,
	.minimum_bytes = 10,
	.samples_count = speex8_samples,
};

static int speex16_samples(struct ast_frame *frame)
{
	return 2 * speex_samples(frame->data.ptr, frame->datalen);
}

static struct ast_codec speex16 = {
	.name = "speex",
	.description = "SpeeX 16khz",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 16000,
	.minimum_ms = 10,
	.maximum_ms = 60,
	.default_ms = 20,
	.minimum_bytes = 10,
	.samples_count = speex16_samples,
};

static int speex32_samples(struct ast_frame *frame)
{
	return 4 * speex_samples(frame->data.ptr, frame->datalen);
}

static struct ast_codec speex32 = {
	.name = "speex",
	.description = "SpeeX 32khz",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 32000,
	.minimum_ms = 10,
	.maximum_ms = 60,
	.default_ms = 20,
	.minimum_bytes = 10,
	.samples_count = speex32_samples,
};

static int ilbc_samples(struct ast_frame *frame)
{
	return 240 * (frame->datalen / 50);
}

static struct ast_codec ilbc = {
	.name = "ilbc",
	.description = "iLBC",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 30,
	.maximum_ms = 30,
	.default_ms = 30,
	.minimum_bytes = 50,
	.samples_count = ilbc_samples,
	.smooth = 1,
};

static struct ast_codec g722 = {
	.name = "g722",
	.description = "G722",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 16000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
	.minimum_bytes = 80,
	.samples_count = g726_samples,
	.get_length = g726_length,
	.smooth = 1,
};

static int siren7_samples(struct ast_frame *frame)
{
	return frame->datalen * (16000 / 4000);
}

static int siren7_length(unsigned int samples)
{
	return samples / (16000 / 4000);
}

static struct ast_codec siren7 = {
	.name = "siren7",
	.description = "ITU G.722.1 (Siren7, licensed from Polycom)",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 16000,
	.minimum_ms = 20,
	.maximum_ms = 80,
	.default_ms = 20,
	.minimum_bytes = 80,
	.samples_count = siren7_samples,
	.get_length = siren7_length,
};

static int siren14_samples(struct ast_frame *frame)
{
	return (int) frame->datalen * ((float) 32000 / 6000);
}

static int siren14_length(unsigned int samples)
{
	return (int) samples / ((float) 32000 / 6000);;
}

static struct ast_codec siren14 = {
	.name = "siren14",
	.description = "ITU G.722.1 Annex C, (Siren14, licensed from Polycom)",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 32000,
	.minimum_ms = 20,
	.maximum_ms = 80,
	.default_ms = 20,
	.minimum_bytes = 120,
	.samples_count = siren14_samples,
	.get_length = siren14_length,
};

static struct ast_codec testlaw = {
	.name = "testlaw",
	.description = "G.711 test-law",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 8000,
	.minimum_ms = 10,
	.maximum_ms = 150,
	.default_ms = 20,
	.minimum_bytes = 80,
	.samples_count = ulaw_samples,
	.get_length = ulaw_length,
	.smooth = 1,
};

static int g719_samples(struct ast_frame *frame)
{
	return (int) frame->datalen * ((float) 48000 / 8000);
}

static int g719_length(unsigned int samples)
{
	return (int) samples / ((float) 48000 / 8000);
}

static struct ast_codec g719 = {
	.name = "g719",
	.description = "ITU G.719",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 48000,
	.minimum_ms = 20,
	.maximum_ms = 80,
	.default_ms = 20,
	.minimum_bytes = 160,
	.samples_count = g719_samples,
	.get_length = g719_length,
};

static struct ast_codec opus = {
	.name = "opus",
	.description = "Opus Codec",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = 48000,
	.minimum_ms = 20,
	.maximum_ms = 60,
	.default_ms = 20,
	.minimum_bytes = 10,
};

static struct ast_codec jpeg = {
	.name = "jpeg",
	.description = "JPEG image",
	.type = AST_MEDIA_TYPE_IMAGE,
};

static struct ast_codec png = {
	.name = "png",
	.description = "PNG Image",
	.type = AST_MEDIA_TYPE_IMAGE,
};

static struct ast_codec h261 = {
	.name = "h261",
	.description = "H.261 video",
	.type = AST_MEDIA_TYPE_VIDEO,
};

static struct ast_codec h263 = {
	.name = "h263",
	.description = "H.263 video",
	.type = AST_MEDIA_TYPE_VIDEO,
};

static struct ast_codec h263p = {
	.name = "h263p",
	.description = "H.263+ video",
	.type = AST_MEDIA_TYPE_VIDEO,
};

static struct ast_codec h264 = {
	.name = "h264",
	.description = "H.264 video",
	.type = AST_MEDIA_TYPE_VIDEO,
};

static struct ast_codec mpeg4 = {
	.name = "mpeg4",
	.description = "MPEG4 video",
	.type = AST_MEDIA_TYPE_VIDEO,
};

static struct ast_codec vp8 = {
	.name = "vp8",
	.description = "VP8 video",
	.type = AST_MEDIA_TYPE_VIDEO,
};

static struct ast_codec t140red = {
	.name = "red",
	.description = "T.140 Realtime Text with redundancy",
	.type = AST_MEDIA_TYPE_TEXT,
};

static struct ast_codec t140 = {
	.name = "t140",
	.description = "Passthrough T.140 Realtime Text",
	.type = AST_MEDIA_TYPE_TEXT,
};

#define CODEC_REGISTER_AND_CACHE(codec) \
	({ \
		int __res_ ## __LINE__ = 0; \
		struct ast_format *__fmt_ ## __LINE__; \
		struct ast_codec *__codec_ ## __LINE__; \
		res |= __ast_codec_register(&(codec), NULL); \
		__codec_ ## __LINE__ = ast_codec_get((codec).name, (codec).type, (codec).sample_rate); \
		__fmt_ ## __LINE__ = __codec_ ## __LINE__ ? ast_format_create(__codec_ ## __LINE__) : NULL; \
		res |= ast_format_cache_set(__fmt_ ## __LINE__); \
		ao2_ref(__fmt_ ## __LINE__, -1); \
		ao2_ref(__codec_ ## __LINE__, -1); \
		__res_ ## __LINE__; \
	})

#define CODEC_REGISTER_AND_CACHE_NAMED(format_name, codec) \
	({ \
		int __res_ ## __LINE__ = 0; \
		struct ast_format *__fmt_ ## __LINE__; \
		struct ast_codec *__codec_ ## __LINE__; \
		res |= __ast_codec_register(&(codec), NULL); \
		__codec_ ## __LINE__ = ast_codec_get((codec).name, (codec).type, (codec).sample_rate); \
		__fmt_ ## __LINE__ = ast_format_create_named((format_name), __codec_ ## __LINE__); \
		res |= ast_format_cache_set(__fmt_ ## __LINE__); \
		ao2_ref(__fmt_ ## __LINE__, -1); \
		ao2_ref(__codec_ ## __LINE__, -1); \
		__res_ ## __LINE__; \
	})

int ast_codec_builtin_init(void)
{
	int res = 0;

	res |= CODEC_REGISTER_AND_CACHE(g723);
	res |= CODEC_REGISTER_AND_CACHE(ulaw);
	res |= CODEC_REGISTER_AND_CACHE(alaw);
	res |= CODEC_REGISTER_AND_CACHE(gsm);
	res |= CODEC_REGISTER_AND_CACHE(g726rfc3551);
	res |= CODEC_REGISTER_AND_CACHE(g726aal2);
	res |= CODEC_REGISTER_AND_CACHE(adpcm);
	res |= CODEC_REGISTER_AND_CACHE(slin8);
	res |= CODEC_REGISTER_AND_CACHE_NAMED("slin12", slin12);
	res |= CODEC_REGISTER_AND_CACHE_NAMED("slin16", slin16);
	res |= CODEC_REGISTER_AND_CACHE_NAMED("slin24", slin24);
	res |= CODEC_REGISTER_AND_CACHE_NAMED("slin32", slin32);
	res |= CODEC_REGISTER_AND_CACHE_NAMED("slin44", slin44);
	res |= CODEC_REGISTER_AND_CACHE_NAMED("slin48", slin48);
	res |= CODEC_REGISTER_AND_CACHE_NAMED("slin96", slin96);
	res |= CODEC_REGISTER_AND_CACHE_NAMED("slin192", slin192);
	res |= CODEC_REGISTER_AND_CACHE(lpc10);
	res |= CODEC_REGISTER_AND_CACHE(g729a);
	res |= CODEC_REGISTER_AND_CACHE(speex8);
	res |= CODEC_REGISTER_AND_CACHE_NAMED("speex16", speex16);
	res |= CODEC_REGISTER_AND_CACHE_NAMED("speex32", speex32);
	res |= CODEC_REGISTER_AND_CACHE(ilbc);
	res |= CODEC_REGISTER_AND_CACHE(g722);
	res |= CODEC_REGISTER_AND_CACHE(siren7);
	res |= CODEC_REGISTER_AND_CACHE(siren14);
	res |= CODEC_REGISTER_AND_CACHE(testlaw);
	res |= CODEC_REGISTER_AND_CACHE(g719);
	res |= CODEC_REGISTER_AND_CACHE(opus);
	res |= CODEC_REGISTER_AND_CACHE(jpeg);
	res |= CODEC_REGISTER_AND_CACHE(png);
	res |= CODEC_REGISTER_AND_CACHE(h261);
	res |= CODEC_REGISTER_AND_CACHE(h263);
	res |= CODEC_REGISTER_AND_CACHE(h263p);
	res |= CODEC_REGISTER_AND_CACHE(h264);
	res |= CODEC_REGISTER_AND_CACHE(mpeg4);
	res |= CODEC_REGISTER_AND_CACHE(vp8);
	res |= CODEC_REGISTER_AND_CACHE(t140red);
	res |= CODEC_REGISTER_AND_CACHE(t140);
	res |= CODEC_REGISTER_AND_CACHE(none);

	return res;
}
