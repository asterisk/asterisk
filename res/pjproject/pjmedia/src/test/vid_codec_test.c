/* $Id$ */
/* 
 * Copyright (C) 2011 Teluu Inc. (http://www.teluu.com)
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
#include "test.h"
#include <pjmedia-codec/ffmpeg_vid_codecs.h>
#include <pjmedia-videodev/videodev.h>
#include <pjmedia/vid_codec.h>
#include <pjmedia/port.h>


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#define THIS_FILE "vid_codec.c"

/* 
 * Capture device setting: 
 *   -1 = colorbar, 
 *   -2 = any non-colorbar capture device (first found)
 *    x = specified capture device id
 */
#define CAPTURE_DEV	    -1


typedef struct codec_port_data_t
{
    pjmedia_vid_codec   *codec;
    pjmedia_vid_port    *rdr_port;
    pj_uint8_t          *enc_buf;
    pj_size_t            enc_buf_size;
    pj_uint8_t          *pack_buf;
    pj_size_t            pack_buf_size;
} codec_port_data_t;

static pj_status_t codec_on_event(pjmedia_event *event,
                                  void *user_data)
{
    codec_port_data_t *port_data = (codec_port_data_t*)user_data;

    if (event->type == PJMEDIA_EVENT_FMT_CHANGED) {
	pjmedia_vid_codec *codec = port_data->codec;
	pjmedia_vid_codec_param codec_param;
	pj_status_t status;

	status = pjmedia_vid_codec_get_param(codec, &codec_param);
	if (status != PJ_SUCCESS)
	    return status;

	status = pjmedia_vid_dev_stream_set_cap(
			pjmedia_vid_port_get_stream(port_data->rdr_port),
			PJMEDIA_VID_DEV_CAP_FORMAT,
			&codec_param.dec_fmt);
	if (status != PJ_SUCCESS)
	    return status;
    }

    return PJ_SUCCESS;
}

static pj_status_t codec_put_frame(pjmedia_port *port,
			           pjmedia_frame *frame)
{
    enum { MAX_PACKETS = 50 };
    codec_port_data_t *port_data = (codec_port_data_t*)port->port_data.pdata;
    pj_status_t status;
    pjmedia_vid_codec *codec = port_data->codec;
    unsigned enc_cnt = 0;
    pj_uint8_t *enc_buf;
    unsigned enc_size_left;
    pjmedia_frame enc_frames[MAX_PACKETS];
    pj_bool_t has_more = PJ_FALSE;

    enc_buf = port_data->enc_buf;
    enc_size_left = port_data->enc_buf_size;

    /*
     * Encode
     */
    enc_frames[enc_cnt].buf = enc_buf;
    enc_frames[enc_cnt].size = enc_size_left;

    status = pjmedia_vid_codec_encode_begin(codec, NULL, frame, enc_size_left,
                                            &enc_frames[enc_cnt], &has_more);
    if (status != PJ_SUCCESS) goto on_error;

    enc_buf += enc_frames[enc_cnt].size;
    enc_size_left -= enc_frames[enc_cnt].size;

    ++enc_cnt;
    while (has_more) {
	enc_frames[enc_cnt].buf = enc_buf;
	enc_frames[enc_cnt].size = enc_size_left;

	status = pjmedia_vid_codec_encode_more(codec, enc_size_left,
						&enc_frames[enc_cnt],
						&has_more);
	if (status != PJ_SUCCESS)
	    break;

	enc_buf += enc_frames[enc_cnt].size;
	enc_size_left -= enc_frames[enc_cnt].size;

	++enc_cnt;

	if (enc_cnt >= MAX_PACKETS) {
	    assert(!"Too many packets!");
	    break;
	}
    }

    /*
     * Decode
     */
    status = pjmedia_vid_codec_decode(codec, enc_cnt, enc_frames,
				      frame->size, frame);
    if (status != PJ_SUCCESS) goto on_error;

    /* Display */
    status = pjmedia_port_put_frame(
			pjmedia_vid_port_get_passive_port(port_data->rdr_port),
			frame);
    if (status != PJ_SUCCESS) goto on_error;

    return PJ_SUCCESS;

on_error:
    pj_perror(3, THIS_FILE, status, "codec_put_frame() error");
    return status;
}

static const char* dump_codec_info(const pjmedia_vid_codec_info *info)
{
    static char str[80];
    unsigned i;
    char *p = str;

    /* Raw format ids */
    for (i=0; (i<info->dec_fmt_id_cnt) && (p-str+5<sizeof(str)); ++i) {
        pj_memcpy(p, &info->dec_fmt_id[i], 4);
        p += 4;
        *p++ = ' ';
    }
    *p = '\0';

    return str;
}

static int enum_codecs()
{
    unsigned i, cnt;
    pjmedia_vid_codec_info info[PJMEDIA_CODEC_MGR_MAX_CODECS];
    pj_status_t status;

    PJ_LOG(3, (THIS_FILE, "  codec enums"));
    cnt = PJ_ARRAY_SIZE(info);
    status = pjmedia_vid_codec_mgr_enum_codecs(NULL, &cnt, info, NULL);
    if (status != PJ_SUCCESS)
        return 100;

    for (i = 0; i < cnt; ++i) {
        PJ_LOG(3, (THIS_FILE, "  %-16.*s %c%c %s",
                   info[i].encoding_name.slen, info[i].encoding_name.ptr,
                   (info[i].dir & PJMEDIA_DIR_ENCODING? 'E' : ' '),
                   (info[i].dir & PJMEDIA_DIR_DECODING? 'D' : ' '),
                   dump_codec_info(&info[i])));
    }

    return PJ_SUCCESS;
}

static int encode_decode_test(pj_pool_t *pool, const char *codec_id,
                              pjmedia_vid_packing packing)
{
    const pj_str_t port_name = {"codec", 5};

    pjmedia_vid_codec *codec=NULL;
    pjmedia_port codec_port;
    codec_port_data_t codec_port_data;
    pjmedia_vid_codec_param codec_param;
    const pjmedia_vid_codec_info *codec_info;
    const char *packing_name;
    pjmedia_vid_dev_index cap_idx, rdr_idx;
    pjmedia_vid_port *capture=NULL, *renderer=NULL;
    pjmedia_vid_port_param vport_param;
    pjmedia_video_format_detail *vfd;
    char codec_name[5];
    pj_status_t status;
    int rc = 0;

    switch (packing) {
    case PJMEDIA_VID_PACKING_PACKETS:
	packing_name = "framed";
	break;
    case PJMEDIA_VID_PACKING_WHOLE:
	packing_name = "whole";
	break;
    default:
	packing_name = "unknown";
	break;
    }

    PJ_LOG(3, (THIS_FILE, "  encode decode test: codec=%s, packing=%s",
	       codec_id, packing_name));

    /* Lookup codec */
    {
        pj_str_t codec_id_st;
        unsigned info_cnt = 1;

        /* Lookup codec */
        pj_cstr(&codec_id_st, codec_id);
        status = pjmedia_vid_codec_mgr_find_codecs_by_id(NULL, &codec_id_st, 
                                                         &info_cnt, 
                                                         &codec_info, NULL);
        if (status != PJ_SUCCESS) {
            rc = 205; goto on_return;
        }
    }


#if CAPTURE_DEV == -1
    /* Lookup colorbar source */
    status = pjmedia_vid_dev_lookup("Colorbar", "Colorbar generator", &cap_idx);
    if (status != PJ_SUCCESS) {
	rc = 206; goto on_return;
    }
#elif CAPTURE_DEV == -2
    /* Lookup any first non-colorbar source */
    {
	unsigned i, cnt;
	pjmedia_vid_dev_info info;

	cap_idx = -1;
	cnt = pjmedia_vid_dev_count();
	for (i = 0; i < cnt; ++i) {
	    status = pjmedia_vid_dev_get_info(i, &info);
	    if (status != PJ_SUCCESS) {
		rc = 206; goto on_return;
	    }
	    if (info.dir & PJMEDIA_DIR_CAPTURE && 
		pj_ansi_stricmp(info.driver, "Colorbar"))
	    {
		cap_idx = i;
		break;
	    }
	}

	if (cap_idx == -1) {
	    status = PJ_ENOTFOUND;
	    rc = 206; goto on_return;
	}
    }
#else
    cap_idx = CAPTURE_DEV;
#endif

    /* Lookup SDL renderer */
    status = pjmedia_vid_dev_lookup("SDL", "SDL renderer", &rdr_idx);
    if (status != PJ_SUCCESS) {
	rc = 207; goto on_return;
    }

    /* Prepare codec */
    {
        pj_str_t codec_id_st;
        unsigned info_cnt = 1;
        const pjmedia_vid_codec_info *codec_info;

        /* Lookup codec */
        pj_cstr(&codec_id_st, codec_id);
        status = pjmedia_vid_codec_mgr_find_codecs_by_id(NULL, &codec_id_st, 
                                                         &info_cnt, 
                                                         &codec_info, NULL);
        if (status != PJ_SUCCESS) {
            rc = 245; goto on_return;
        }
        status = pjmedia_vid_codec_mgr_get_default_param(NULL, codec_info,
                                                         &codec_param);
        if (status != PJ_SUCCESS) {
            rc = 246; goto on_return;
        }

        codec_param.packing = packing;

        /* Open codec */
        status = pjmedia_vid_codec_mgr_alloc_codec(NULL, codec_info,
                                                   &codec);
        if (status != PJ_SUCCESS) {
	    rc = 250; goto on_return;
        }

        status = pjmedia_vid_codec_init(codec, pool);
        if (status != PJ_SUCCESS) {
	    rc = 251; goto on_return;
        }

        status = pjmedia_vid_codec_open(codec, &codec_param);
        if (status != PJ_SUCCESS) {
	    rc = 252; goto on_return;
        }

	/* After opened, codec will update the param, let's sync encoder & 
	 * decoder format detail.
	 */
	codec_param.dec_fmt.det = codec_param.enc_fmt.det;

	/* Subscribe to codec events */
	pjmedia_event_subscribe(NULL, &codec_on_event, &codec_port_data,
                                codec);
    }

    pjmedia_vid_port_param_default(&vport_param);

    /* Create capture, set it to active (master) */
    status = pjmedia_vid_dev_default_param(pool, cap_idx,
					   &vport_param.vidparam);
    if (status != PJ_SUCCESS) {
	rc = 220; goto on_return;
    }
    pjmedia_format_copy(&vport_param.vidparam.fmt, &codec_param.dec_fmt);
    vport_param.vidparam.dir = PJMEDIA_DIR_CAPTURE;
    vport_param.active = PJ_TRUE;

    if (vport_param.vidparam.fmt.detail_type != PJMEDIA_FORMAT_DETAIL_VIDEO) {
	rc = 221; goto on_return;
    }

    vfd = pjmedia_format_get_video_format_detail(&vport_param.vidparam.fmt,
						 PJ_TRUE);
    if (vfd == NULL) {
	rc = 225; goto on_return;
    }

    status = pjmedia_vid_port_create(pool, &vport_param, &capture);
    if (status != PJ_SUCCESS) {
	rc = 226; goto on_return;
    }

    /* Create renderer, set it to passive (slave)  */
    vport_param.active = PJ_FALSE;
    vport_param.vidparam.dir = PJMEDIA_DIR_RENDER;
    vport_param.vidparam.rend_id = rdr_idx;
    vport_param.vidparam.disp_size = vfd->size;

    status = pjmedia_vid_port_create(pool, &vport_param, &renderer);
    if (status != PJ_SUCCESS) {
	rc = 230; goto on_return;
    }

    /* Init codec port */
    pj_bzero(&codec_port, sizeof(codec_port));
    status = pjmedia_port_info_init2(&codec_port.info, &port_name, 0x1234,
                                     PJMEDIA_DIR_ENCODING, 
                                     &codec_param.dec_fmt);
    if (status != PJ_SUCCESS) {
	rc = 260; goto on_return;
    }

    codec_port_data.codec = codec;
    codec_port_data.rdr_port = renderer;
    codec_port_data.enc_buf_size = codec_param.dec_fmt.det.vid.size.w *
				   codec_param.dec_fmt.det.vid.size.h * 4;
    codec_port_data.enc_buf = pj_pool_alloc(pool, 
					    codec_port_data.enc_buf_size);
    codec_port_data.pack_buf_size = codec_port_data.enc_buf_size;
    codec_port_data.pack_buf = pj_pool_alloc(pool, 
					     codec_port_data.pack_buf_size);

    codec_port.put_frame = &codec_put_frame;
    codec_port.port_data.pdata = &codec_port_data;

    /* Connect capture to codec port */
    status = pjmedia_vid_port_connect(capture,
				      &codec_port,
				      PJ_FALSE);
    if (status != PJ_SUCCESS) {
	rc = 270; goto on_return;
    }

    PJ_LOG(3, (THIS_FILE, "    starting codec test: %s<->%.*s %dx%d",
	pjmedia_fourcc_name(codec_param.dec_fmt.id, codec_name),
	codec_info->encoding_name.slen,
	codec_info->encoding_name.ptr,
        codec_param.dec_fmt.det.vid.size.w,
        codec_param.dec_fmt.det.vid.size.h
        ));

    /* Start streaming.. */
    status = pjmedia_vid_port_start(renderer);
    if (status != PJ_SUCCESS) {
	rc = 275; goto on_return;
    }
    status = pjmedia_vid_port_start(capture);
    if (status != PJ_SUCCESS) {
	rc = 280; goto on_return;
    }

    /* Sleep while the video is being displayed... */
    pj_thread_sleep(10000);

on_return:
    if (status != PJ_SUCCESS) {
        PJ_PERROR(3, (THIS_FILE, status, "  error"));
    }
    if (capture)
        pjmedia_vid_port_stop(capture);
    if (renderer)
        pjmedia_vid_port_stop(renderer);
    if (capture)
	pjmedia_vid_port_destroy(capture);
    if (renderer)
	pjmedia_vid_port_destroy(renderer);
    if (codec) {
	pjmedia_event_unsubscribe(NULL, &codec_on_event, &codec_port_data,
                                  codec);
        pjmedia_vid_codec_close(codec);
        pjmedia_vid_codec_mgr_dealloc_codec(NULL, codec);
    }

    return rc;
}

int vid_codec_test(void)
{
    pj_pool_t *pool;
    int rc = 0;
    pj_status_t status;
    int orig_log_level;
    
    orig_log_level = pj_log_get_level();
    pj_log_set_level(3);

    PJ_LOG(3, (THIS_FILE, "Performing video codec tests.."));

    pool = pj_pool_create(mem, "Vid codec test", 256, 256, 0);

    status = pjmedia_vid_dev_subsys_init(mem);
    if (status != PJ_SUCCESS)
        return -10;

#if PJMEDIA_HAS_FFMPEG_VID_CODEC
    status = pjmedia_codec_ffmpeg_vid_init(NULL, mem);
    if (status != PJ_SUCCESS)
        return -20;
#endif

    rc = enum_codecs();
    if (rc != 0)
	goto on_return;

    rc = encode_decode_test(pool, "h263-1998", PJMEDIA_VID_PACKING_WHOLE);
    if (rc != 0)
	goto on_return;

    rc = encode_decode_test(pool, "h263-1998", PJMEDIA_VID_PACKING_PACKETS);
    if (rc != 0)
	goto on_return;

on_return:
#if PJMEDIA_HAS_FFMPEG_VID_CODEC
    pjmedia_codec_ffmpeg_vid_deinit();
#endif
    pjmedia_vid_dev_subsys_shutdown();
    pj_pool_release(pool);
    pj_log_set_level(orig_log_level);

    return rc;
}


#endif /* PJMEDIA_HAS_VIDEO */
