/* $Id$ */
/* 
 * Copyright (C) 2010-2011 Teluu Inc. (http://www.teluu.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pjmedia-codec/ffmpeg_vid_codecs.h>
#include <pjmedia-codec/h263_packetizer.h>
#include <pjmedia-codec/h264_packetizer.h>
#include <pjmedia/errno.h>
#include <pjmedia/vid_codec_util.h>
#include <pj/assert.h>
#include <pj/list.h>
#include <pj/log.h>
#include <pj/math.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <pj/os.h>


/*
 * Only build this file if PJMEDIA_HAS_FFMPEG_VID_CODEC != 0 and 
 * PJMEDIA_HAS_VIDEO != 0
 */
#if defined(PJMEDIA_HAS_FFMPEG_VID_CODEC) && \
            PJMEDIA_HAS_FFMPEG_VID_CODEC != 0 && \
    defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)

#define THIS_FILE   "ffmpeg_vid_codecs.c"

#define LIBAVCODEC_VER_AT_LEAST(major,minor)  (LIBAVCODEC_VERSION_MAJOR > major || \
     					       (LIBAVCODEC_VERSION_MAJOR == major && \
					        LIBAVCODEC_VERSION_MINOR >= minor))

#include "../pjmedia/ffmpeg_util.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#if LIBAVCODEC_VER_AT_LEAST(53,20)
  /* Needed by 264 so far, on libavcodec 53.20 */
# include <libavutil/opt.h>
#endif


/* Various compatibility */

#if LIBAVCODEC_VER_AT_LEAST(53,20)
#  define AVCODEC_OPEN(ctx,c)		avcodec_open2(ctx,c,NULL)
#else
#  define AVCODEC_OPEN(ctx,c)		avcodec_open(ctx,c)
#endif

#if LIBAVCODEC_VER_AT_LEAST(53,61)
/* Not sure when AVCodec::encode2 is introduced. It appears in 
 * libavcodec 53.61 where some codecs actually still use AVCodec::encode
 * (e.g: H263, H264).
 */
#  define AVCODEC_HAS_ENCODE(c)		(c->encode || c->encode2)
#  define AV_OPT_SET(obj,name,val,opt)	(av_opt_set(obj,name,val,opt)==0)
#  define AV_OPT_SET_INT(obj,name,val)	(av_opt_set_int(obj,name,val,0)==0)
#else
#  define AVCODEC_HAS_ENCODE(c)		(c->encode)
#  define AV_OPT_SET(obj,name,val,opt)	(av_set_string3(obj,name,val,opt,NULL)==0)
#  define AV_OPT_SET_INT(obj,name,val)	(av_set_int(obj,name,val)!=NULL)
#endif
#define AVCODEC_HAS_DECODE(c)		(c->decode)


/* Prototypes for FFMPEG codecs factory */
static pj_status_t ffmpeg_test_alloc( pjmedia_vid_codec_factory *factory, 
				      const pjmedia_vid_codec_info *id );
static pj_status_t ffmpeg_default_attr( pjmedia_vid_codec_factory *factory, 
				        const pjmedia_vid_codec_info *info, 
				        pjmedia_vid_codec_param *attr );
static pj_status_t ffmpeg_enum_codecs( pjmedia_vid_codec_factory *factory, 
				       unsigned *count, 
				       pjmedia_vid_codec_info codecs[]);
static pj_status_t ffmpeg_alloc_codec( pjmedia_vid_codec_factory *factory, 
				       const pjmedia_vid_codec_info *info, 
				       pjmedia_vid_codec **p_codec);
static pj_status_t ffmpeg_dealloc_codec( pjmedia_vid_codec_factory *factory, 
				         pjmedia_vid_codec *codec );

/* Prototypes for FFMPEG codecs implementation. */
static pj_status_t  ffmpeg_codec_init( pjmedia_vid_codec *codec, 
				       pj_pool_t *pool );
static pj_status_t  ffmpeg_codec_open( pjmedia_vid_codec *codec, 
				       pjmedia_vid_codec_param *attr );
static pj_status_t  ffmpeg_codec_close( pjmedia_vid_codec *codec );
static pj_status_t  ffmpeg_codec_modify(pjmedia_vid_codec *codec, 
				        const pjmedia_vid_codec_param *attr );
static pj_status_t  ffmpeg_codec_get_param(pjmedia_vid_codec *codec,
					   pjmedia_vid_codec_param *param);
static pj_status_t ffmpeg_codec_encode_begin(pjmedia_vid_codec *codec,
					     const pjmedia_vid_encode_opt *opt,
                                             const pjmedia_frame *input,
					     unsigned out_size,
					     pjmedia_frame *output,
					     pj_bool_t *has_more);
static pj_status_t ffmpeg_codec_encode_more(pjmedia_vid_codec *codec,
					    unsigned out_size,
					    pjmedia_frame *output,
					    pj_bool_t *has_more);
static pj_status_t ffmpeg_codec_decode( pjmedia_vid_codec *codec,
					pj_size_t pkt_count,
					pjmedia_frame packets[],
					unsigned out_size,
					pjmedia_frame *output);

/* Definition for FFMPEG codecs operations. */
static pjmedia_vid_codec_op ffmpeg_op = 
{
    &ffmpeg_codec_init,
    &ffmpeg_codec_open,
    &ffmpeg_codec_close,
    &ffmpeg_codec_modify,
    &ffmpeg_codec_get_param,
    &ffmpeg_codec_encode_begin,
    &ffmpeg_codec_encode_more,
    &ffmpeg_codec_decode,
    NULL
};

/* Definition for FFMPEG codecs factory operations. */
static pjmedia_vid_codec_factory_op ffmpeg_factory_op =
{
    &ffmpeg_test_alloc,
    &ffmpeg_default_attr,
    &ffmpeg_enum_codecs,
    &ffmpeg_alloc_codec,
    &ffmpeg_dealloc_codec
};


/* FFMPEG codecs factory */
static struct ffmpeg_factory {
    pjmedia_vid_codec_factory    base;
    pjmedia_vid_codec_mgr	*mgr;
    pj_pool_factory             *pf;
    pj_pool_t		        *pool;
    pj_mutex_t		        *mutex;
} ffmpeg_factory;


typedef struct ffmpeg_codec_desc ffmpeg_codec_desc;


/* FFMPEG codecs private data. */
typedef struct ffmpeg_private
{
    const ffmpeg_codec_desc	    *desc;
    pjmedia_vid_codec_param	     param;	/**< Codec param	    */
    pj_pool_t			    *pool;	/**< Pool for each instance */

    /* Format info and apply format param */
    const pjmedia_video_format_info *enc_vfi;
    pjmedia_video_apply_fmt_param    enc_vafp;
    const pjmedia_video_format_info *dec_vfi;
    pjmedia_video_apply_fmt_param    dec_vafp;

    /* Buffers, only needed for multi-packets */
    pj_bool_t			     whole;
    void			    *enc_buf;
    unsigned			     enc_buf_size;
    pj_bool_t			     enc_buf_is_keyframe;
    unsigned			     enc_frame_len;
    unsigned     		     enc_processed;
    void			    *dec_buf;
    unsigned			     dec_buf_size;
    pj_timestamp		     last_dec_keyframe_ts; 

    /* The ffmpeg codec states. */
    AVCodec			    *enc;
    AVCodec			    *dec;
    AVCodecContext		    *enc_ctx;
    AVCodecContext		    *dec_ctx;

    /* The ffmpeg decoder cannot set the output format, so format conversion
     * may be needed for post-decoding.
     */
    enum PixelFormat		     expected_dec_fmt;
						/**< Expected output format of 
						     ffmpeg decoder	    */

    void			    *data;	/**< Codec specific data    */		    
} ffmpeg_private;


/* Shortcuts for packetize & unpacketize function declaration,
 * as it has long params and is reused many times!
 */
#define FUNC_PACKETIZE(name) \
    pj_status_t(name)(ffmpeg_private *ff, pj_uint8_t *bits, \
		      pj_size_t bits_len, unsigned *bits_pos, \
		      const pj_uint8_t **payload, pj_size_t *payload_len)

#define FUNC_UNPACKETIZE(name) \
    pj_status_t(name)(ffmpeg_private *ff, const pj_uint8_t *payload, \
		      pj_size_t payload_len, pj_uint8_t *bits, \
		      pj_size_t bits_len, unsigned *bits_pos)

#define FUNC_FMT_MATCH(name) \
    pj_status_t(name)(pj_pool_t *pool, \
		      pjmedia_sdp_media *offer, unsigned o_fmt_idx, \
		      pjmedia_sdp_media *answer, unsigned a_fmt_idx, \
		      unsigned option)


/* Type definition of codec specific functions */
typedef FUNC_PACKETIZE(*func_packetize);
typedef FUNC_UNPACKETIZE(*func_unpacketize);
typedef pj_status_t (*func_preopen)	(ffmpeg_private *ff);
typedef pj_status_t (*func_postopen)	(ffmpeg_private *ff);
typedef FUNC_FMT_MATCH(*func_sdp_fmt_match);


/* FFMPEG codec info */
struct ffmpeg_codec_desc
{
    /* Predefined info */
    pjmedia_vid_codec_info       info;
    pjmedia_format_id		 base_fmt_id;	/**< Some codecs may be exactly
						     same or compatible with
						     another codec, base format
						     will tell the initializer
						     to copy this codec desc
						     from its base format   */
    pjmedia_rect_size            size;
    pjmedia_ratio                fps;
    pj_uint32_t			 avg_bps;
    pj_uint32_t			 max_bps;
    func_packetize		 packetize;
    func_unpacketize		 unpacketize;
    func_preopen		 preopen;
    func_preopen		 postopen;
    func_sdp_fmt_match		 sdp_fmt_match;
    pjmedia_codec_fmtp		 dec_fmtp;

    /* Init time defined info */
    pj_bool_t			 enabled;
    AVCodec                     *enc;
    AVCodec                     *dec;
};


#if PJMEDIA_HAS_FFMPEG_CODEC_H264 && !LIBAVCODEC_VER_AT_LEAST(53,20)
#   error "Must use libavcodec version 53.20 or later to enable FFMPEG H264"
#endif

/* H264 constants */
#define PROFILE_H264_BASELINE		66
#define PROFILE_H264_MAIN		77

/* Codec specific functions */
#if PJMEDIA_HAS_FFMPEG_CODEC_H264
static pj_status_t h264_preopen(ffmpeg_private *ff);
static pj_status_t h264_postopen(ffmpeg_private *ff);
static FUNC_PACKETIZE(h264_packetize);
static FUNC_UNPACKETIZE(h264_unpacketize);
#endif

static pj_status_t h263_preopen(ffmpeg_private *ff);
static FUNC_PACKETIZE(h263_packetize);
static FUNC_UNPACKETIZE(h263_unpacketize);


/* Internal codec info */
static ffmpeg_codec_desc codec_desc[] =
{
#if PJMEDIA_HAS_FFMPEG_CODEC_H264
    {
	{PJMEDIA_FORMAT_H264, PJMEDIA_RTP_PT_H264, {"H264",4},
	 {"Constrained Baseline (level=30, pack=1)", 39}},
	0,
	{720, 480},	{15, 1},	256000, 256000,
	&h264_packetize, &h264_unpacketize, &h264_preopen, &h264_postopen,
	&pjmedia_vid_codec_h264_match_sdp,
	/* Leading space for better compatibility (strange indeed!) */
	{2, { {{"profile-level-id",16},    {"42e01e",6}}, 
	      {{" packetization-mode",19},  {"1",1}}, } },
    },
#endif

#if PJMEDIA_HAS_FFMPEG_CODEC_H263P
    {
	{PJMEDIA_FORMAT_H263P, PJMEDIA_RTP_PT_H263P, {"H263-1998",9}},
	PJMEDIA_FORMAT_H263,
	{352, 288},	{15, 1},	256000, 256000,
	&h263_packetize, &h263_unpacketize, &h263_preopen, NULL, NULL,
	{2, { {{"CIF",3},   {"1",1}}, 
	      {{"QCIF",4},  {"1",1}}, } },
    },
#endif

    {
	{PJMEDIA_FORMAT_H263,	PJMEDIA_RTP_PT_H263,	{"H263",4}},
    },
    {
	{PJMEDIA_FORMAT_H261,	PJMEDIA_RTP_PT_H261,	{"H261",4}},
    },
    {
	{PJMEDIA_FORMAT_MJPEG,	PJMEDIA_RTP_PT_JPEG,	{"JPEG",4}},
	PJMEDIA_FORMAT_MJPEG, {640, 480}, {25, 1},
    },
    {
	{PJMEDIA_FORMAT_MPEG4,	0,			{"MP4V",4}},
	PJMEDIA_FORMAT_MPEG4, {640, 480}, {25, 1},
    },
};

#if PJMEDIA_HAS_FFMPEG_CODEC_H264

typedef struct h264_data
{
    pjmedia_vid_codec_h264_fmtp	 fmtp;
    pjmedia_h264_packetizer	*pktz;
} h264_data;


static pj_status_t h264_preopen(ffmpeg_private *ff)
{
    h264_data *data;
    pjmedia_h264_packetizer_cfg pktz_cfg;
    pj_status_t status;

    data = PJ_POOL_ZALLOC_T(ff->pool, h264_data);
    ff->data = data;

    /* Parse remote fmtp */
    status = pjmedia_vid_codec_h264_parse_fmtp(&ff->param.enc_fmtp,
					       &data->fmtp);
    if (status != PJ_SUCCESS)
	return status;

    /* Create packetizer */
    pktz_cfg.mtu = ff->param.enc_mtu;
#if 0
    if (data->fmtp.packetization_mode == 0)
	pktz_cfg.mode = PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL;
    else if (data->fmtp.packetization_mode == 1)
	pktz_cfg.mode = PJMEDIA_H264_PACKETIZER_MODE_NON_INTERLEAVED;
    else
	return PJ_ENOTSUP;
#else
    if (data->fmtp.packetization_mode!=
				PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL &&
	data->fmtp.packetization_mode!=
				PJMEDIA_H264_PACKETIZER_MODE_NON_INTERLEAVED)
    {
	return PJ_ENOTSUP;
    }
    /* Better always send in single NAL mode for better compatibility */
    pktz_cfg.mode = PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL;
#endif

    status = pjmedia_h264_packetizer_create(ff->pool, &pktz_cfg, &data->pktz);
    if (status != PJ_SUCCESS)
	return status;

    /* Apply SDP fmtp to format in codec param */
    if (!ff->param.ignore_fmtp) {
	status = pjmedia_vid_codec_h264_apply_fmtp(&ff->param);
	if (status != PJ_SUCCESS)
	    return status;
    }

    if (ff->param.dir & PJMEDIA_DIR_ENCODING) {
	pjmedia_video_format_detail *vfd;
	AVCodecContext *ctx = ff->enc_ctx;
	const char *profile = NULL;

	vfd = pjmedia_format_get_video_format_detail(&ff->param.enc_fmt, 
						     PJ_TRUE);

	/* Override generic params after applying SDP fmtp */
	ctx->width = vfd->size.w;
	ctx->height = vfd->size.h;
	ctx->time_base.num = vfd->fps.denum;
	ctx->time_base.den = vfd->fps.num;

	/* Apply profile. */
	ctx->profile  = data->fmtp.profile_idc;
	switch (ctx->profile) {
	case PROFILE_H264_BASELINE:
	    profile = "baseline";
	    break;
	case PROFILE_H264_MAIN:
	    profile = "main";
	    break;
	default:
	    break;
	}
	if (profile && !AV_OPT_SET(ctx->priv_data, "profile", profile, 0))
	{
	    PJ_LOG(3, (THIS_FILE, "Failed to set H264 profile to '%s'",
		       profile));
	}

	/* Apply profile constraint bits. */
	//PJ_TODO(set_h264_constraint_bits_properly_in_ffmpeg);
	if (data->fmtp.profile_iop) {
#if defined(FF_PROFILE_H264_CONSTRAINED)
	    ctx->profile |= FF_PROFILE_H264_CONSTRAINED;
#endif
	}

	/* Apply profile level. */
	ctx->level    = data->fmtp.level;

	/* Limit NAL unit size as we prefer single NAL unit packetization */
	if (!AV_OPT_SET_INT(ctx->priv_data, "slice-max-size", ff->param.enc_mtu))
	{
	    PJ_LOG(3, (THIS_FILE, "Failed to set H264 max NAL size to %d",
		       ff->param.enc_mtu));
	}

	/* Apply intra-refresh */
	if (!AV_OPT_SET_INT(ctx->priv_data, "intra-refresh", 1))
	{
	    PJ_LOG(3, (THIS_FILE, "Failed to set x264 intra-refresh"));
	}

	/* Misc x264 settings (performance, quality, latency, etc).
	 * Let's just use the x264 predefined preset & tune.
	 */
	if (!AV_OPT_SET(ctx->priv_data, "preset", "veryfast", 0)) {
	    PJ_LOG(3, (THIS_FILE, "Failed to set x264 preset 'veryfast'"));
	}
	if (!AV_OPT_SET(ctx->priv_data, "tune", "animation+zerolatency", 0)) {
	    PJ_LOG(3, (THIS_FILE, "Failed to set x264 tune 'zerolatency'"));
	}
    }

    if (ff->param.dir & PJMEDIA_DIR_DECODING) {
	AVCodecContext *ctx = ff->dec_ctx;

	/* Apply the "sprop-parameter-sets" fmtp from remote SDP to
	 * extradata of ffmpeg codec context.
	 */
	if (data->fmtp.sprop_param_sets_len) {
	    ctx->extradata_size = data->fmtp.sprop_param_sets_len;
	    ctx->extradata = data->fmtp.sprop_param_sets;
	}
    }

    return PJ_SUCCESS;
}

static pj_status_t h264_postopen(ffmpeg_private *ff)
{
    h264_data *data = (h264_data*)ff->data;
    PJ_UNUSED_ARG(data);
    return PJ_SUCCESS;
}

static FUNC_PACKETIZE(h264_packetize)
{
    h264_data *data = (h264_data*)ff->data;
    return pjmedia_h264_packetize(data->pktz, bits, bits_len, bits_pos,
				  payload, payload_len);
}

static FUNC_UNPACKETIZE(h264_unpacketize)
{
    h264_data *data = (h264_data*)ff->data;
    return pjmedia_h264_unpacketize(data->pktz, payload, payload_len,
				    bits, bits_len, bits_pos);
}

#endif /* PJMEDIA_HAS_FFMPEG_CODEC_H264 */


#if PJMEDIA_HAS_FFMPEG_CODEC_H263P

typedef struct h263_data
{
    pjmedia_h263_packetizer	*pktz;
} h263_data;

/* H263 pre-open */
static pj_status_t h263_preopen(ffmpeg_private *ff)
{
    h263_data *data;
    pjmedia_h263_packetizer_cfg pktz_cfg;
    pj_status_t status;

    data = PJ_POOL_ZALLOC_T(ff->pool, h263_data);
    ff->data = data;

    /* Create packetizer */
    pktz_cfg.mtu = ff->param.enc_mtu;
    pktz_cfg.mode = PJMEDIA_H263_PACKETIZER_MODE_RFC4629;
    status = pjmedia_h263_packetizer_create(ff->pool, &pktz_cfg, &data->pktz);
    if (status != PJ_SUCCESS)
	return status;

    /* Apply fmtp settings to codec param */
    if (!ff->param.ignore_fmtp) {
	status = pjmedia_vid_codec_h263_apply_fmtp(&ff->param);
    }

    /* Override generic params after applying SDP fmtp */
    if (ff->param.dir & PJMEDIA_DIR_ENCODING) {
	pjmedia_video_format_detail *vfd;
	AVCodecContext *ctx = ff->enc_ctx;

	vfd = pjmedia_format_get_video_format_detail(&ff->param.enc_fmt, 
						     PJ_TRUE);

	/* Override generic params after applying SDP fmtp */
	ctx->width = vfd->size.w;
	ctx->height = vfd->size.h;
	ctx->time_base.num = vfd->fps.denum;
	ctx->time_base.den = vfd->fps.num;
    }

    return status;
}

static FUNC_PACKETIZE(h263_packetize)
{
    h263_data *data = (h263_data*)ff->data;
    return pjmedia_h263_packetize(data->pktz, bits, bits_len, bits_pos,
				  payload, payload_len);
}

static FUNC_UNPACKETIZE(h263_unpacketize)
{
    h263_data *data = (h263_data*)ff->data;
    return pjmedia_h263_unpacketize(data->pktz, payload, payload_len,
				    bits, bits_len, bits_pos);
}

#endif /* PJMEDIA_HAS_FFMPEG_CODEC_H263P */


static const ffmpeg_codec_desc* find_codec_desc_by_info(
			const pjmedia_vid_codec_info *info)
{
    int i;

    for (i=0; i<PJ_ARRAY_SIZE(codec_desc); ++i) {
	ffmpeg_codec_desc *desc = &codec_desc[i];

	if (desc->enabled &&
	    (desc->info.fmt_id == info->fmt_id) &&
            ((desc->info.dir & info->dir) == info->dir) &&
	    (desc->info.pt == info->pt) &&
	    (desc->info.packings & info->packings))
        {
            return desc;
        }
    }

    return NULL;
}


static int find_codec_idx_by_fmt_id(pjmedia_format_id fmt_id)
{
    int i;
    for (i=0; i<PJ_ARRAY_SIZE(codec_desc); ++i) {
	if (codec_desc[i].info.fmt_id == fmt_id)
	    return i;
    }

    return -1;
}


/*
 * Initialize and register FFMPEG codec factory to pjmedia endpoint.
 */
PJ_DEF(pj_status_t) pjmedia_codec_ffmpeg_vid_init(pjmedia_vid_codec_mgr *mgr,
                                                  pj_pool_factory *pf)
{
    pj_pool_t *pool;
    AVCodec *c;
    pj_status_t status;
    unsigned i;

    if (ffmpeg_factory.pool != NULL) {
	/* Already initialized. */
	return PJ_SUCCESS;
    }

    if (!mgr) mgr = pjmedia_vid_codec_mgr_instance();
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    /* Create FFMPEG codec factory. */
    ffmpeg_factory.base.op = &ffmpeg_factory_op;
    ffmpeg_factory.base.factory_data = NULL;
    ffmpeg_factory.mgr = mgr;
    ffmpeg_factory.pf = pf;

    pool = pj_pool_create(pf, "ffmpeg codec factory", 256, 256, NULL);
    if (!pool)
	return PJ_ENOMEM;

    /* Create mutex. */
    status = pj_mutex_create_simple(pool, "ffmpeg codec factory", 
				    &ffmpeg_factory.mutex);
    if (status != PJ_SUCCESS)
	goto on_error;

    pjmedia_ffmpeg_add_ref();
#if !LIBAVCODEC_VER_AT_LEAST(53,20)
    /* avcodec_init() dissappeared between version 53.20 and 54.15, not sure
     * exactly when 
     */
    avcodec_init();
#endif
    avcodec_register_all();

    /* Enum FFMPEG codecs */
    for (c=av_codec_next(NULL); c; c=av_codec_next(c)) {
        ffmpeg_codec_desc *desc;
	pjmedia_format_id fmt_id;
	int codec_info_idx;
        
#if LIBAVCODEC_VERSION_MAJOR <= 52
#   define AVMEDIA_TYPE_VIDEO	CODEC_TYPE_VIDEO
#endif
        if (c->type != AVMEDIA_TYPE_VIDEO)
            continue;

        /* Video encoder and decoder are usually implemented in separate
         * AVCodec instances. While the codec attributes (e.g: raw formats,
	 * supported fps) are in the encoder.
         */

	//PJ_LOG(3, (THIS_FILE, "%s", c->name));
	status = CodecID_to_pjmedia_format_id(c->id, &fmt_id);
	/* Skip if format ID is unknown */
	if (status != PJ_SUCCESS)
	    continue;

	codec_info_idx = find_codec_idx_by_fmt_id(fmt_id);
	/* Skip if codec is unwanted by this wrapper (not listed in 
	 * the codec info array)
	 */
	if (codec_info_idx < 0)
	    continue;

	desc = &codec_desc[codec_info_idx];

	/* Skip duplicated codec implementation */
	if ((AVCODEC_HAS_ENCODE(c) && (desc->info.dir & PJMEDIA_DIR_ENCODING))
	    ||
	    (AVCODEC_HAS_DECODE(c) && (desc->info.dir & PJMEDIA_DIR_DECODING)))
	{
	    continue;
	}

	/* Get raw/decoded format ids in the encoder */
	if (c->pix_fmts && AVCODEC_HAS_ENCODE(c)) {
	    pjmedia_format_id raw_fmt[PJMEDIA_VID_CODEC_MAX_DEC_FMT_CNT];
	    unsigned raw_fmt_cnt = 0;
	    unsigned raw_fmt_cnt_should_be = 0;
	    const enum PixelFormat *p = c->pix_fmts;

	    for(;(p && *p != -1) &&
		 (raw_fmt_cnt < PJMEDIA_VID_CODEC_MAX_DEC_FMT_CNT);
		 ++p)
	    {
		pjmedia_format_id fmt_id;

		raw_fmt_cnt_should_be++;
		status = PixelFormat_to_pjmedia_format_id(*p, &fmt_id);
		if (status != PJ_SUCCESS) {
		    PJ_LOG(6, (THIS_FILE, "Unrecognized ffmpeg pixel "
			       "format %d", *p));
		    continue;
		}
		
		//raw_fmt[raw_fmt_cnt++] = fmt_id;
		/* Disable some formats due to H.264 error:
		 * x264 [error]: baseline profile doesn't support 4:4:4
		 */
		if (desc->info.pt != PJMEDIA_RTP_PT_H264 ||
		    fmt_id != PJMEDIA_FORMAT_RGB24)
		{
		    raw_fmt[raw_fmt_cnt++] = fmt_id;
		}
	    }

	    if (raw_fmt_cnt == 0) {
		PJ_LOG(5, (THIS_FILE, "No recognized raw format "
				      "for codec [%s/%s], codec ignored",
				      c->name, c->long_name));
		/* Skip this encoder */
		continue;
	    }

	    if (raw_fmt_cnt < raw_fmt_cnt_should_be) {
		PJ_LOG(6, (THIS_FILE, "Codec [%s/%s] have %d raw formats, "
				      "recognized only %d raw formats",
				      c->name, c->long_name,
				      raw_fmt_cnt_should_be, raw_fmt_cnt));
	    }

	    desc->info.dec_fmt_id_cnt = raw_fmt_cnt;
	    pj_memcpy(desc->info.dec_fmt_id, raw_fmt, 
		      sizeof(raw_fmt[0])*raw_fmt_cnt);
	}

	/* Get supported framerates */
	if (c->supported_framerates) {
	    const AVRational *fr = c->supported_framerates;
	    while ((fr->num != 0 || fr->den != 0) && 
		   desc->info.fps_cnt < PJMEDIA_VID_CODEC_MAX_FPS_CNT)
	    {
		desc->info.fps[desc->info.fps_cnt].num = fr->num;
		desc->info.fps[desc->info.fps_cnt].denum = fr->den;
		++desc->info.fps_cnt;
		++fr;
	    }
	}

	/* Get ffmpeg encoder instance */
	if (AVCODEC_HAS_ENCODE(c) && !desc->enc) {
            desc->info.dir |= PJMEDIA_DIR_ENCODING;
            desc->enc = c;
        }
	
	/* Get ffmpeg decoder instance */
        if (AVCODEC_HAS_DECODE(c) && !desc->dec) {
            desc->info.dir |= PJMEDIA_DIR_DECODING;
            desc->dec = c;
        }

	/* Enable this codec when any ffmpeg codec instance are recognized
	 * and the supported raw formats info has been collected.
	 */
	if ((desc->dec || desc->enc) && desc->info.dec_fmt_id_cnt)
	{
	    desc->enabled = PJ_TRUE;
	}

	/* Normalize default value of clock rate */
	if (desc->info.clock_rate == 0)
	    desc->info.clock_rate = 90000;

	/* Set supported packings */
	desc->info.packings |= PJMEDIA_VID_PACKING_WHOLE;
	if (desc->packetize && desc->unpacketize)
	    desc->info.packings |= PJMEDIA_VID_PACKING_PACKETS;

    }

    /* Review all codecs for applying base format, registering format match for
     * SDP negotiation, etc.
     */
    for (i = 0; i < PJ_ARRAY_SIZE(codec_desc); ++i) {
	ffmpeg_codec_desc *desc = &codec_desc[i];

	/* Init encoder/decoder description from base format */
	if (desc->base_fmt_id && (!desc->dec || !desc->enc)) {
	    ffmpeg_codec_desc *base_desc = NULL;
	    int base_desc_idx;
	    pjmedia_dir copied_dir = PJMEDIA_DIR_NONE;

	    base_desc_idx = find_codec_idx_by_fmt_id(desc->base_fmt_id);
	    if (base_desc_idx != -1)
		base_desc = &codec_desc[base_desc_idx];
	    if (!base_desc || !base_desc->enabled)
		continue;

	    /* Copy description from base codec */
	    if (!desc->info.dec_fmt_id_cnt) {
		desc->info.dec_fmt_id_cnt = base_desc->info.dec_fmt_id_cnt;
		pj_memcpy(desc->info.dec_fmt_id, base_desc->info.dec_fmt_id, 
			  sizeof(pjmedia_format_id)*desc->info.dec_fmt_id_cnt);
	    }
	    if (!desc->info.fps_cnt) {
		desc->info.fps_cnt = base_desc->info.fps_cnt;
		pj_memcpy(desc->info.fps, base_desc->info.fps, 
			  sizeof(desc->info.fps[0])*desc->info.fps_cnt);
	    }
	    if (!desc->info.clock_rate) {
		desc->info.clock_rate = base_desc->info.clock_rate;
	    }
	    if (!desc->dec && base_desc->dec) {
		copied_dir |= PJMEDIA_DIR_DECODING;
		desc->dec = base_desc->dec;
	    }
	    if (!desc->enc && base_desc->enc) {
		copied_dir |= PJMEDIA_DIR_ENCODING;
		desc->enc = base_desc->enc;
	    }

	    desc->info.dir |= copied_dir;
	    desc->enabled = (desc->info.dir != PJMEDIA_DIR_NONE);

	    /* Set supported packings */
	    desc->info.packings |= PJMEDIA_VID_PACKING_WHOLE;
	    if (desc->packetize && desc->unpacketize)
		desc->info.packings |= PJMEDIA_VID_PACKING_PACKETS;

	    if (copied_dir != PJMEDIA_DIR_NONE) {
		const char *dir_name[] = {NULL, "encoder", "decoder", "codec"};
		PJ_LOG(5, (THIS_FILE, "The %.*s %s is using base codec (%.*s)",
			   desc->info.encoding_name.slen,
			   desc->info.encoding_name.ptr,
			   dir_name[copied_dir],
			   base_desc->info.encoding_name.slen,
			   base_desc->info.encoding_name.ptr));
	    }
        }

	/* Registering format match for SDP negotiation */
	if (desc->sdp_fmt_match) {
	    status = pjmedia_sdp_neg_register_fmt_match_cb(
						&desc->info.encoding_name,
						desc->sdp_fmt_match);
	    pj_assert(status == PJ_SUCCESS);
	}

	/* Print warning about missing encoder/decoder */
	if (!desc->enc) {
	    PJ_LOG(4, (THIS_FILE, "Cannot find %.*s encoder in ffmpeg library",
		       desc->info.encoding_name.slen,
		       desc->info.encoding_name.ptr));
	}
	if (!desc->dec) {
	    PJ_LOG(4, (THIS_FILE, "Cannot find %.*s decoder in ffmpeg library",
		       desc->info.encoding_name.slen,
		       desc->info.encoding_name.ptr));
	}
    }

    /* Register codec factory to codec manager. */
    status = pjmedia_vid_codec_mgr_register_factory(mgr, 
						    &ffmpeg_factory.base);
    if (status != PJ_SUCCESS)
	goto on_error;

    ffmpeg_factory.pool = pool;

    /* Done. */
    return PJ_SUCCESS;

on_error:
    pj_pool_release(pool);
    return status;
}

/*
 * Unregister FFMPEG codecs factory from pjmedia endpoint.
 */
PJ_DEF(pj_status_t) pjmedia_codec_ffmpeg_vid_deinit(void)
{
    pj_status_t status = PJ_SUCCESS;

    if (ffmpeg_factory.pool == NULL) {
	/* Already deinitialized */
	return PJ_SUCCESS;
    }

    pj_mutex_lock(ffmpeg_factory.mutex);

    /* Unregister FFMPEG codecs factory. */
    status = pjmedia_vid_codec_mgr_unregister_factory(ffmpeg_factory.mgr,
						      &ffmpeg_factory.base);

    /* Destroy mutex. */
    pj_mutex_destroy(ffmpeg_factory.mutex);

    /* Destroy pool. */
    pj_pool_release(ffmpeg_factory.pool);
    ffmpeg_factory.pool = NULL;

    pjmedia_ffmpeg_dec_ref();

    return status;
}


/* 
 * Check if factory can allocate the specified codec. 
 */
static pj_status_t ffmpeg_test_alloc( pjmedia_vid_codec_factory *factory, 
				      const pjmedia_vid_codec_info *info )
{
    const ffmpeg_codec_desc *desc;

    PJ_ASSERT_RETURN(factory==&ffmpeg_factory.base, PJ_EINVAL);
    PJ_ASSERT_RETURN(info, PJ_EINVAL);

    desc = find_codec_desc_by_info(info);
    if (!desc) {
        return PJMEDIA_CODEC_EUNSUP;
    }

    return PJ_SUCCESS;
}

/*
 * Generate default attribute.
 */
static pj_status_t ffmpeg_default_attr( pjmedia_vid_codec_factory *factory, 
				        const pjmedia_vid_codec_info *info, 
				        pjmedia_vid_codec_param *attr )
{
    const ffmpeg_codec_desc *desc;
    unsigned i;

    PJ_ASSERT_RETURN(factory==&ffmpeg_factory.base, PJ_EINVAL);
    PJ_ASSERT_RETURN(info && attr, PJ_EINVAL);

    desc = find_codec_desc_by_info(info);
    if (!desc) {
        return PJMEDIA_CODEC_EUNSUP;
    }

    pj_bzero(attr, sizeof(pjmedia_vid_codec_param));

    /* Scan the requested packings and use the lowest number */
    attr->packing = 0;
    for (i=0; i<15; ++i) {
	unsigned packing = (1 << i);
	if ((desc->info.packings & info->packings) & packing) {
	    attr->packing = (pjmedia_vid_packing)packing;
	    break;
	}
    }
    if (attr->packing == 0) {
	/* No supported packing in info */
	return PJMEDIA_CODEC_EUNSUP;
    }

    /* Direction */
    attr->dir = desc->info.dir;

    /* Encoded format */
    pjmedia_format_init_video(&attr->enc_fmt, desc->info.fmt_id,
                              desc->size.w, desc->size.h,
			      desc->fps.num, desc->fps.denum);

    /* Decoded format */
    pjmedia_format_init_video(&attr->dec_fmt, desc->info.dec_fmt_id[0],
                              desc->size.w, desc->size.h,
			      desc->fps.num, desc->fps.denum);

    /* Decoding fmtp */
    attr->dec_fmtp = desc->dec_fmtp;

    /* Bitrate */
    attr->enc_fmt.det.vid.avg_bps = desc->avg_bps;
    attr->enc_fmt.det.vid.max_bps = desc->max_bps;

    /* Encoding MTU */
    attr->enc_mtu = PJMEDIA_MAX_VID_PAYLOAD_SIZE;

    return PJ_SUCCESS;
}

/*
 * Enum codecs supported by this factory.
 */
static pj_status_t ffmpeg_enum_codecs( pjmedia_vid_codec_factory *factory,
				       unsigned *count, 
				       pjmedia_vid_codec_info codecs[])
{
    unsigned i, max_cnt;

    PJ_ASSERT_RETURN(codecs && *count > 0, PJ_EINVAL);
    PJ_ASSERT_RETURN(factory == &ffmpeg_factory.base, PJ_EINVAL);

    max_cnt = PJ_MIN(*count, PJ_ARRAY_SIZE(codec_desc));
    *count = 0;

    for (i=0; i<max_cnt; ++i) {
	if (codec_desc[i].enabled) {
	    pj_memcpy(&codecs[*count], &codec_desc[i].info, 
		      sizeof(pjmedia_vid_codec_info));
	    (*count)++;
	}
    }

    return PJ_SUCCESS;
}

/*
 * Allocate a new codec instance.
 */
static pj_status_t ffmpeg_alloc_codec( pjmedia_vid_codec_factory *factory, 
				       const pjmedia_vid_codec_info *info,
				       pjmedia_vid_codec **p_codec)
{
    ffmpeg_private *ff;
    const ffmpeg_codec_desc *desc;
    pjmedia_vid_codec *codec;
    pj_pool_t *pool = NULL;
    pj_status_t status = PJ_SUCCESS;

    PJ_ASSERT_RETURN(factory && info && p_codec, PJ_EINVAL);
    PJ_ASSERT_RETURN(factory == &ffmpeg_factory.base, PJ_EINVAL);

    desc = find_codec_desc_by_info(info);
    if (!desc) {
        return PJMEDIA_CODEC_EUNSUP;
    }

    /* Create pool for codec instance */
    pool = pj_pool_create(ffmpeg_factory.pf, "ffmpeg codec", 512, 512, NULL);
    codec = PJ_POOL_ZALLOC_T(pool, pjmedia_vid_codec);
    if (!codec) {
        status = PJ_ENOMEM;
        goto on_error;
    }
    codec->op = &ffmpeg_op;
    codec->factory = factory;
    ff = PJ_POOL_ZALLOC_T(pool, ffmpeg_private);
    if (!ff) {
        status = PJ_ENOMEM;
        goto on_error;
    }
    codec->codec_data = ff;
    ff->pool = pool;
    ff->enc = desc->enc;
    ff->dec = desc->dec;
    ff->desc = desc;

    *p_codec = codec;
    return PJ_SUCCESS;

on_error:
    if (pool)
        pj_pool_release(pool);
    return status;
}

/*
 * Free codec.
 */
static pj_status_t ffmpeg_dealloc_codec( pjmedia_vid_codec_factory *factory, 
				         pjmedia_vid_codec *codec )
{
    ffmpeg_private *ff;
    pj_pool_t *pool;

    PJ_ASSERT_RETURN(factory && codec, PJ_EINVAL);
    PJ_ASSERT_RETURN(factory == &ffmpeg_factory.base, PJ_EINVAL);

    /* Close codec, if it's not closed. */
    ff = (ffmpeg_private*) codec->codec_data;
    pool = ff->pool;
    codec->codec_data = NULL;
    pj_pool_release(pool);

    return PJ_SUCCESS;
}

/*
 * Init codec.
 */
static pj_status_t ffmpeg_codec_init( pjmedia_vid_codec *codec, 
				      pj_pool_t *pool )
{
    PJ_UNUSED_ARG(codec);
    PJ_UNUSED_ARG(pool);
    return PJ_SUCCESS;
}

static void print_ffmpeg_err(int err)
{
#if LIBAVCODEC_VER_AT_LEAST(52,72)
    char errbuf[512];
    if (av_strerror(err, errbuf, sizeof(errbuf)) >= 0)
        PJ_LOG(5, (THIS_FILE, "ffmpeg err %d: %s", err, errbuf));
#else
    PJ_LOG(5, (THIS_FILE, "ffmpeg err %d", err));
#endif

}

static pj_status_t open_ffmpeg_codec(ffmpeg_private *ff,
                                     pj_mutex_t *ff_mutex)
{
    enum PixelFormat pix_fmt;
    pjmedia_video_format_detail *vfd;
    pj_bool_t enc_opened = PJ_FALSE, dec_opened = PJ_FALSE;
    pj_status_t status;

    /* Get decoded pixel format */
    status = pjmedia_format_id_to_PixelFormat(ff->param.dec_fmt.id,
                                              &pix_fmt);
    if (status != PJ_SUCCESS)
        return status;
    ff->expected_dec_fmt = pix_fmt;

    /* Get video format detail for shortcut access to encoded format */
    vfd = pjmedia_format_get_video_format_detail(&ff->param.enc_fmt, 
						 PJ_TRUE);

    /* Allocate ffmpeg codec context */
    if (ff->param.dir & PJMEDIA_DIR_ENCODING) {
#if LIBAVCODEC_VER_AT_LEAST(53,20)
	ff->enc_ctx = avcodec_alloc_context3(ff->enc);
#else
	ff->enc_ctx = avcodec_alloc_context();
#endif
	if (ff->enc_ctx == NULL)
	    goto on_error;
    }
    if (ff->param.dir & PJMEDIA_DIR_DECODING) {
#if LIBAVCODEC_VER_AT_LEAST(53,20)
	ff->dec_ctx = avcodec_alloc_context3(ff->dec);
#else
	ff->dec_ctx = avcodec_alloc_context();
#endif
	if (ff->dec_ctx == NULL)
	    goto on_error;
    }

    /* Init generic encoder params */
    if (ff->param.dir & PJMEDIA_DIR_ENCODING) {
        AVCodecContext *ctx = ff->enc_ctx;

        ctx->pix_fmt = pix_fmt;
	ctx->width = vfd->size.w;
	ctx->height = vfd->size.h;
	ctx->time_base.num = vfd->fps.denum;
	ctx->time_base.den = vfd->fps.num;
	if (vfd->avg_bps) {
	    ctx->bit_rate = vfd->avg_bps;
	    if (vfd->max_bps > vfd->avg_bps)
		ctx->bit_rate_tolerance = vfd->max_bps - vfd->avg_bps;
	}
	ctx->strict_std_compliance = FF_COMPLIANCE_STRICT;
        ctx->workaround_bugs = FF_BUG_AUTODETECT;
        ctx->opaque = ff;

	/* Set no delay, note that this may cause some codec functionals
	 * not working (e.g: rate control).
	 */
#if LIBAVCODEC_VER_AT_LEAST(52,113) && !LIBAVCODEC_VER_AT_LEAST(53,20)
	ctx->rc_lookahead = 0;
#endif
    }

    /* Init generic decoder params */
    if (ff->param.dir & PJMEDIA_DIR_DECODING) {
	AVCodecContext *ctx = ff->dec_ctx;

	/* Width/height may be overriden by ffmpeg after first decoding. */
	ctx->width  = ctx->coded_width  = ff->param.dec_fmt.det.vid.size.w;
	ctx->height = ctx->coded_height = ff->param.dec_fmt.det.vid.size.h;
	ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
        ctx->workaround_bugs = FF_BUG_AUTODETECT;
        ctx->opaque = ff;
    }

    /* Override generic params or apply specific params before opening
     * the codec.
     */
    if (ff->desc->preopen) {
	status = (*ff->desc->preopen)(ff);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

    /* Open encoder */
    if (ff->param.dir & PJMEDIA_DIR_ENCODING) {
	int err;

	pj_mutex_lock(ff_mutex);
	err = AVCODEC_OPEN(ff->enc_ctx, ff->enc);
        pj_mutex_unlock(ff_mutex);
        if (err < 0) {
            print_ffmpeg_err(err);
            status = PJMEDIA_CODEC_EFAILED;
	    goto on_error;
        }
	enc_opened = PJ_TRUE;
    }

    /* Open decoder */
    if (ff->param.dir & PJMEDIA_DIR_DECODING) {
	int err;

	pj_mutex_lock(ff_mutex);
	err = AVCODEC_OPEN(ff->dec_ctx, ff->dec);
        pj_mutex_unlock(ff_mutex);
        if (err < 0) {
            print_ffmpeg_err(err);
            status = PJMEDIA_CODEC_EFAILED;
	    goto on_error;
        }
	dec_opened = PJ_TRUE;
    }

    /* Let the codec apply specific params after the codec opened */
    if (ff->desc->postopen) {
	status = (*ff->desc->postopen)(ff);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

    return PJ_SUCCESS;

on_error:
    if (ff->enc_ctx) {
	if (enc_opened)
	    avcodec_close(ff->enc_ctx);
	av_free(ff->enc_ctx);
	ff->enc_ctx = NULL;
    }
    if (ff->dec_ctx) {
	if (dec_opened)
	    avcodec_close(ff->dec_ctx);
	av_free(ff->dec_ctx);
	ff->dec_ctx = NULL;
    }
    return status;
}

/*
 * Open codec.
 */
static pj_status_t ffmpeg_codec_open( pjmedia_vid_codec *codec, 
				      pjmedia_vid_codec_param *attr )
{
    ffmpeg_private *ff;
    pj_status_t status;
    pj_mutex_t *ff_mutex;

    PJ_ASSERT_RETURN(codec && attr, PJ_EINVAL);
    ff = (ffmpeg_private*)codec->codec_data;

    pj_memcpy(&ff->param, attr, sizeof(*attr));

    /* Normalize encoding MTU in codec param */
    if (attr->enc_mtu > PJMEDIA_MAX_VID_PAYLOAD_SIZE)
	attr->enc_mtu = PJMEDIA_MAX_VID_PAYLOAD_SIZE;

    /* Open the codec */
    ff_mutex = ((struct ffmpeg_factory*)codec->factory)->mutex;
    status = open_ffmpeg_codec(ff, ff_mutex);
    if (status != PJ_SUCCESS)
        goto on_error;

    /* Init format info and apply-param of decoder */
    ff->dec_vfi = pjmedia_get_video_format_info(NULL, ff->param.dec_fmt.id);
    if (!ff->dec_vfi) {
        status = PJ_EINVAL;
        goto on_error;
    }
    pj_bzero(&ff->dec_vafp, sizeof(ff->dec_vafp));
    ff->dec_vafp.size = ff->param.dec_fmt.det.vid.size;
    ff->dec_vafp.buffer = NULL;
    status = (*ff->dec_vfi->apply_fmt)(ff->dec_vfi, &ff->dec_vafp);
    if (status != PJ_SUCCESS) {
        goto on_error;
    }

    /* Init format info and apply-param of encoder */
    ff->enc_vfi = pjmedia_get_video_format_info(NULL, ff->param.dec_fmt.id);
    if (!ff->enc_vfi) {
        status = PJ_EINVAL;
        goto on_error;
    }
    pj_bzero(&ff->enc_vafp, sizeof(ff->enc_vafp));
    ff->enc_vafp.size = ff->param.enc_fmt.det.vid.size;
    ff->enc_vafp.buffer = NULL;
    status = (*ff->enc_vfi->apply_fmt)(ff->enc_vfi, &ff->enc_vafp);
    if (status != PJ_SUCCESS) {
        goto on_error;
    }

    /* Alloc buffers if needed */
    ff->whole = (ff->param.packing == PJMEDIA_VID_PACKING_WHOLE);
    if (!ff->whole) {
	ff->enc_buf_size = ff->enc_vafp.framebytes;
	ff->enc_buf = pj_pool_alloc(ff->pool, ff->enc_buf_size);

	ff->dec_buf_size = ff->dec_vafp.framebytes;
	ff->dec_buf = pj_pool_alloc(ff->pool, ff->dec_buf_size);
    }

    /* Update codec attributes, e.g: encoding format may be changed by
     * SDP fmtp negotiation.
     */
    pj_memcpy(attr, &ff->param, sizeof(*attr));

    return PJ_SUCCESS;

on_error:
    ffmpeg_codec_close(codec);
    return status;
}

/*
 * Close codec.
 */
static pj_status_t ffmpeg_codec_close( pjmedia_vid_codec *codec )
{
    ffmpeg_private *ff;
    pj_mutex_t *ff_mutex;

    PJ_ASSERT_RETURN(codec, PJ_EINVAL);
    ff = (ffmpeg_private*)codec->codec_data;
    ff_mutex = ((struct ffmpeg_factory*)codec->factory)->mutex;

    pj_mutex_lock(ff_mutex);
    if (ff->enc_ctx) {
        avcodec_close(ff->enc_ctx);
        av_free(ff->enc_ctx);
    }
    if (ff->dec_ctx && ff->dec_ctx!=ff->enc_ctx) {
        avcodec_close(ff->dec_ctx);
        av_free(ff->dec_ctx);
    }
    ff->enc_ctx = NULL;
    ff->dec_ctx = NULL;
    pj_mutex_unlock(ff_mutex);

    return PJ_SUCCESS;
}


/*
 * Modify codec settings.
 */
static pj_status_t  ffmpeg_codec_modify( pjmedia_vid_codec *codec, 
				         const pjmedia_vid_codec_param *attr)
{
    ffmpeg_private *ff = (ffmpeg_private*)codec->codec_data;

    PJ_UNUSED_ARG(attr);
    PJ_UNUSED_ARG(ff);

    return PJ_ENOTSUP;
}

static pj_status_t  ffmpeg_codec_get_param(pjmedia_vid_codec *codec,
					   pjmedia_vid_codec_param *param)
{
    ffmpeg_private *ff;

    PJ_ASSERT_RETURN(codec && param, PJ_EINVAL);

    ff = (ffmpeg_private*)codec->codec_data;
    pj_memcpy(param, &ff->param, sizeof(*param));

    return PJ_SUCCESS;
}


static pj_status_t  ffmpeg_packetize ( pjmedia_vid_codec *codec,
                                       pj_uint8_t *bits,
                                       pj_size_t bits_len,
                                       unsigned *bits_pos,
                                       const pj_uint8_t **payload,
                                       pj_size_t *payload_len)
{
    ffmpeg_private *ff = (ffmpeg_private*)codec->codec_data;

    if (ff->desc->packetize) {
	return (*ff->desc->packetize)(ff, bits, bits_len, bits_pos,
                                      payload, payload_len);
    }

    return PJ_ENOTSUP;
}

static pj_status_t  ffmpeg_unpacketize(pjmedia_vid_codec *codec,
                                       const pj_uint8_t *payload,
                                       pj_size_t   payload_len,
                                       pj_uint8_t *bits,
                                       pj_size_t   bits_len,
				       unsigned   *bits_pos)
{
    ffmpeg_private *ff = (ffmpeg_private*)codec->codec_data;

    if (ff->desc->unpacketize) {
        return (*ff->desc->unpacketize)(ff, payload, payload_len,
                                        bits, bits_len, bits_pos);
    }
    
    return PJ_ENOTSUP;
}


/*
 * Encode frames.
 */
static pj_status_t ffmpeg_codec_encode_whole(pjmedia_vid_codec *codec,
					     const pjmedia_vid_encode_opt *opt,
					     const pjmedia_frame *input,
					     unsigned output_buf_len,
					     pjmedia_frame *output)
{
    ffmpeg_private *ff = (ffmpeg_private*)codec->codec_data;
    pj_uint8_t *p = (pj_uint8_t*)input->buf;
    AVFrame avframe;
    AVPacket avpacket;
    int err, got_packet;
    //AVRational src_timebase;
    /* For some reasons (e.g: SSE/MMX usage), the avcodec_encode_video() must
     * have stack aligned to 16 bytes. Let's try to be safe by preparing the
     * 16-bytes aligned stack here, in case it's not managed by the ffmpeg.
     */
    PJ_ALIGN_DATA(pj_uint32_t i[4], 16);

    if ((long)i & 0xF) {
	PJ_LOG(2,(THIS_FILE, "Stack alignment fails"));
    }

    /* Check if encoder has been opened */
    PJ_ASSERT_RETURN(ff->enc_ctx, PJ_EINVALIDOP);

    avcodec_get_frame_defaults(&avframe);

    // Let ffmpeg manage the timestamps
    /*
    src_timebase.num = 1;
    src_timebase.den = ff->desc->info.clock_rate;
    avframe.pts = av_rescale_q(input->timestamp.u64, src_timebase,
			       ff->enc_ctx->time_base);
    */
    
    for (i[0] = 0; i[0] < ff->enc_vfi->plane_cnt; ++i[0]) {
        avframe.data[i[0]] = p;
        avframe.linesize[i[0]] = ff->enc_vafp.strides[i[0]];
        p += ff->enc_vafp.plane_bytes[i[0]];
    }

    /* Force keyframe */
    if (opt && opt->force_keyframe) {
#if LIBAVCODEC_VER_AT_LEAST(53,20)
	avframe.pict_type = AV_PICTURE_TYPE_I;
#else
	avframe.pict_type = FF_I_TYPE;
#endif
    }

    av_init_packet(&avpacket);
    avpacket.data = (pj_uint8_t*)output->buf;
    avpacket.size = output_buf_len;

#if LIBAVCODEC_VER_AT_LEAST(54,15)
    err = avcodec_encode_video2(ff->enc_ctx, &avpacket, &avframe, &got_packet);
    if (!err && got_packet)
	err = avpacket.size;
#else
    PJ_UNUSED_ARG(got_packet);
    err = avcodec_encode_video(ff->enc_ctx, avpacket.data, avpacket.size, &avframe);
#endif

    if (err < 0) {
        print_ffmpeg_err(err);
        return PJMEDIA_CODEC_EFAILED;
    } else {
        output->size = err;
	output->bit_info = 0;
	if (ff->enc_ctx->coded_frame->key_frame)
	    output->bit_info |= PJMEDIA_VID_FRM_KEYFRAME;
    }

    return PJ_SUCCESS;
}

static pj_status_t ffmpeg_codec_encode_begin(pjmedia_vid_codec *codec,
					     const pjmedia_vid_encode_opt *opt,
					     const pjmedia_frame *input,
					     unsigned out_size,
					     pjmedia_frame *output,
					     pj_bool_t *has_more)
{
    ffmpeg_private *ff = (ffmpeg_private*)codec->codec_data;
    pj_status_t status;

    *has_more = PJ_FALSE;

    if (ff->whole) {
	status = ffmpeg_codec_encode_whole(codec, opt, input, out_size,
					   output);
    } else {
	pjmedia_frame whole_frm;
        const pj_uint8_t *payload;
        pj_size_t payload_len;

	pj_bzero(&whole_frm, sizeof(whole_frm));
	whole_frm.buf = ff->enc_buf;
	whole_frm.size = ff->enc_buf_size;
	status = ffmpeg_codec_encode_whole(codec, opt, input,
	                                   whole_frm.size, &whole_frm);
	if (status != PJ_SUCCESS)
	    return status;

	ff->enc_buf_is_keyframe = (whole_frm.bit_info & 
				   PJMEDIA_VID_FRM_KEYFRAME);
	ff->enc_frame_len = (unsigned)whole_frm.size;
	ff->enc_processed = 0;
        status = ffmpeg_packetize(codec, (pj_uint8_t*)whole_frm.buf,
                                  whole_frm.size, &ff->enc_processed,
				  &payload, &payload_len);
        if (status != PJ_SUCCESS)
            return status;

        if (out_size < payload_len)
            return PJMEDIA_CODEC_EFRMTOOSHORT;

        output->type = PJMEDIA_FRAME_TYPE_VIDEO;
        pj_memcpy(output->buf, payload, payload_len);
        output->size = payload_len;

	if (ff->enc_buf_is_keyframe)
	    output->bit_info |= PJMEDIA_VID_FRM_KEYFRAME;

        *has_more = (ff->enc_processed < ff->enc_frame_len);
    }

    return status;
}

static pj_status_t ffmpeg_codec_encode_more(pjmedia_vid_codec *codec,
					    unsigned out_size,
					    pjmedia_frame *output,
					    pj_bool_t *has_more)
{
    ffmpeg_private *ff = (ffmpeg_private*)codec->codec_data;
    const pj_uint8_t *payload;
    pj_size_t payload_len;
    pj_status_t status;

    *has_more = PJ_FALSE;

    if (ff->enc_processed >= ff->enc_frame_len) {
	/* No more frame */
	return PJ_EEOF;
    }

    status = ffmpeg_packetize(codec, (pj_uint8_t*)ff->enc_buf,
                              ff->enc_frame_len, &ff->enc_processed,
                              &payload, &payload_len);
    if (status != PJ_SUCCESS)
        return status;

    if (out_size < payload_len)
        return PJMEDIA_CODEC_EFRMTOOSHORT;

    output->type = PJMEDIA_FRAME_TYPE_VIDEO;
    pj_memcpy(output->buf, payload, payload_len);
    output->size = payload_len;

    if (ff->enc_buf_is_keyframe)
	output->bit_info |= PJMEDIA_VID_FRM_KEYFRAME;

    *has_more = (ff->enc_processed < ff->enc_frame_len);

    return PJ_SUCCESS;
}


static pj_status_t check_decode_result(pjmedia_vid_codec *codec,
				       const pj_timestamp *ts,
				       pj_bool_t got_keyframe)
{
    ffmpeg_private *ff = (ffmpeg_private*)codec->codec_data;
    pjmedia_video_apply_fmt_param *vafp = &ff->dec_vafp;
    pjmedia_event event;

    /* Check for format change.
     * Decoder output format is set by libavcodec, in case it is different
     * to the configured param.
     */
    if (ff->dec_ctx->pix_fmt != ff->expected_dec_fmt ||
	ff->dec_ctx->width != (int)vafp->size.w ||
	ff->dec_ctx->height != (int)vafp->size.h)
    {
	pjmedia_format_id new_fmt_id;
	pj_status_t status;

	/* Get current raw format id from ffmpeg decoder context */
	status = PixelFormat_to_pjmedia_format_id(ff->dec_ctx->pix_fmt, 
						  &new_fmt_id);
	if (status != PJ_SUCCESS)
	    return status;

	/* Update decoder format in param */
		ff->param.dec_fmt.id = new_fmt_id;
	ff->param.dec_fmt.det.vid.size.w = ff->dec_ctx->width;
	ff->param.dec_fmt.det.vid.size.h = ff->dec_ctx->height;
	ff->expected_dec_fmt = ff->dec_ctx->pix_fmt;

	/* Re-init format info and apply-param of decoder */
	ff->dec_vfi = pjmedia_get_video_format_info(NULL, ff->param.dec_fmt.id);
	if (!ff->dec_vfi)
	    return PJ_ENOTSUP;
	pj_bzero(&ff->dec_vafp, sizeof(ff->dec_vafp));
	ff->dec_vafp.size = ff->param.dec_fmt.det.vid.size;
	ff->dec_vafp.buffer = NULL;
	status = (*ff->dec_vfi->apply_fmt)(ff->dec_vfi, &ff->dec_vafp);
	if (status != PJ_SUCCESS)
	    return status;

	/* Realloc buffer if necessary */
	if (ff->dec_vafp.framebytes > ff->dec_buf_size) {
	    PJ_LOG(5,(THIS_FILE, "Reallocating decoding buffer %u --> %u",
		       (unsigned)ff->dec_buf_size,
		       (unsigned)ff->dec_vafp.framebytes));
	    ff->dec_buf_size = ff->dec_vafp.framebytes;
	    ff->dec_buf = pj_pool_alloc(ff->pool, ff->dec_buf_size);
	}

	/* Broadcast format changed event */
	pjmedia_event_init(&event, PJMEDIA_EVENT_FMT_CHANGED, ts, codec);
	event.data.fmt_changed.dir = PJMEDIA_DIR_DECODING;
	pj_memcpy(&event.data.fmt_changed.new_fmt, &ff->param.dec_fmt,
		  sizeof(ff->param.dec_fmt));
	pjmedia_event_publish(NULL, codec, &event, 0);
    }

    /* Check for missing/found keyframe */
    if (got_keyframe) {
	pj_get_timestamp(&ff->last_dec_keyframe_ts);

	/* Broadcast keyframe event */
        pjmedia_event_init(&event, PJMEDIA_EVENT_KEYFRAME_FOUND, ts, codec);
        pjmedia_event_publish(NULL, codec, &event, 0);
    } else if (ff->last_dec_keyframe_ts.u64 == 0) {
	/* Broadcast missing keyframe event */
	pjmedia_event_init(&event, PJMEDIA_EVENT_KEYFRAME_MISSING, ts, codec);
	pjmedia_event_publish(NULL, codec, &event, 0);
    }

    return PJ_SUCCESS;
}

/*
 * Decode frame.
 */
static pj_status_t ffmpeg_codec_decode_whole(pjmedia_vid_codec *codec,
					     const pjmedia_frame *input,
					     unsigned output_buf_len,
					     pjmedia_frame *output)
{
    ffmpeg_private *ff = (ffmpeg_private*)codec->codec_data;
    AVFrame avframe;
    AVPacket avpacket;
    int err, got_picture;

    /* Check if decoder has been opened */
    PJ_ASSERT_RETURN(ff->dec_ctx, PJ_EINVALIDOP);

    /* Reset output frame bit info */
    output->bit_info = 0;

    /* Validate output buffer size */
    // Do this validation later after getting decoding result, where the real
    // decoded size will be assured.
    //if (ff->dec_vafp.framebytes > output_buf_len)
	//return PJ_ETOOSMALL;

    /* Init frame to receive the decoded data, the ffmpeg codec context will
     * automatically provide the decoded buffer (single buffer used for the
     * whole decoding session, and seems to be freed when the codec context
     * closed).
     */
    avcodec_get_frame_defaults(&avframe);

    /* Init packet, the container of the encoded data */
    av_init_packet(&avpacket);
    avpacket.data = (pj_uint8_t*)input->buf;
    avpacket.size = input->size;

    /* ffmpeg warns:
     * - input buffer padding, at least FF_INPUT_BUFFER_PADDING_SIZE
     * - null terminated
     * Normally, encoded buffer is allocated more than needed, so lets just
     * bzero the input buffer end/pad, hope it will be just fine.
     */
    pj_bzero(avpacket.data+avpacket.size, FF_INPUT_BUFFER_PADDING_SIZE);

    output->bit_info = 0;
    output->timestamp = input->timestamp;

#if LIBAVCODEC_VER_AT_LEAST(52,72)
    //avpacket.flags = AV_PKT_FLAG_KEY;
#else
    avpacket.flags = 0;
#endif

#if LIBAVCODEC_VER_AT_LEAST(52,72)
    err = avcodec_decode_video2(ff->dec_ctx, &avframe, 
                                &got_picture, &avpacket);
#else
    err = avcodec_decode_video(ff->dec_ctx, &avframe,
                               &got_picture, avpacket.data, avpacket.size);
#endif
    if (err < 0) {
	pjmedia_event event;

	output->type = PJMEDIA_FRAME_TYPE_NONE;
	output->size = 0;
        print_ffmpeg_err(err);

	/* Broadcast missing keyframe event */
	pjmedia_event_init(&event, PJMEDIA_EVENT_KEYFRAME_MISSING,
			   &input->timestamp, codec);
	pjmedia_event_publish(NULL, codec, &event, 0);

	return PJMEDIA_CODEC_EBADBITSTREAM;
    } else if (got_picture) {
        pjmedia_video_apply_fmt_param *vafp = &ff->dec_vafp;
        pj_uint8_t *q = (pj_uint8_t*)output->buf;
	unsigned i;
	pj_status_t status;

	/* Check decoding result, e.g: see if the format got changed,
	 * keyframe found/missing.
	 */
	status = check_decode_result(codec, &input->timestamp,
				     avframe.key_frame);
	if (status != PJ_SUCCESS)
	    return status;

	/* Check provided buffer size */
	if (vafp->framebytes > output_buf_len)
	    return PJ_ETOOSMALL;

	/* Get the decoded data */
	for (i = 0; i < ff->dec_vfi->plane_cnt; ++i) {
	    pj_uint8_t *p = avframe.data[i];

	    /* The decoded data may contain padding */
	    if (avframe.linesize[i]!=vafp->strides[i]) {
		/* Padding exists, copy line by line */
		pj_uint8_t *q_end;
                    
		q_end = q+vafp->plane_bytes[i];
		while(q < q_end) {
		    pj_memcpy(q, p, vafp->strides[i]);
		    q += vafp->strides[i];
		    p += avframe.linesize[i];
		}
	    } else {
		/* No padding, copy the whole plane */
		pj_memcpy(q, p, vafp->plane_bytes[i]);
		q += vafp->plane_bytes[i];
	    }
	}

	output->type = PJMEDIA_FRAME_TYPE_VIDEO;
        output->size = vafp->framebytes;
    } else {
	output->type = PJMEDIA_FRAME_TYPE_NONE;
	output->size = 0;
    }
    
    return PJ_SUCCESS;
}

static pj_status_t ffmpeg_codec_decode( pjmedia_vid_codec *codec,
					pj_size_t pkt_count,
					pjmedia_frame packets[],
					unsigned out_size,
					pjmedia_frame *output)
{
    ffmpeg_private *ff = (ffmpeg_private*)codec->codec_data;
    pj_status_t status;

    PJ_ASSERT_RETURN(codec && pkt_count > 0 && packets && output,
                     PJ_EINVAL);

    if (ff->whole) {
	pj_assert(pkt_count==1);
	return ffmpeg_codec_decode_whole(codec, &packets[0], out_size, output);
    } else {
	pjmedia_frame whole_frm;
	unsigned whole_len = 0;
	unsigned i;

	for (i=0; i<pkt_count; ++i) {
	    if (whole_len + packets[i].size > ff->dec_buf_size) {
		PJ_LOG(5,(THIS_FILE, "Decoding buffer overflow"));
		break;
	    }

	    status = ffmpeg_unpacketize(codec, packets[i].buf, packets[i].size,
	                                ff->dec_buf, ff->dec_buf_size,
	                                &whole_len);
	    if (status != PJ_SUCCESS) {
		PJ_PERROR(5,(THIS_FILE, status, "Unpacketize error"));
		continue;
	    }
	}

	whole_frm.buf = ff->dec_buf;
	whole_frm.size = whole_len;
	whole_frm.timestamp = output->timestamp = packets[i].timestamp;
	whole_frm.bit_info = 0;

	return ffmpeg_codec_decode_whole(codec, &whole_frm, out_size, output);
    }
}


#ifdef _MSC_VER
#   pragma comment( lib, "avcodec.lib")
#endif

#endif	/* PJMEDIA_HAS_FFMPEG_VID_CODEC */

