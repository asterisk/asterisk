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

#include <pjmedia.h>
#include <pjmedia/converter.h>
#include <pjmedia-codec.h>
#include <pjlib-util.h>
#include <pjlib.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

/**
 * \page page_pjmedia_samples_aviplay_c Samples: Playing AVI File to
 * Video and Sound Devices
 *
 * This is a very simple example to use the @ref PJMEDIA_FILE_PLAY,
 * @ref PJMED_SND_PORT, and @ref PJMEDIA_VID_PORT. In this example, we
 * open the file, video, and sound devices, then connect the file to both
 * video and sound devices to play the contents of the file.
 *
 *
 * This file is pjsip-apps/src/samples/aviplay.c
 *
 * \includelineno aviplay.c
 */


/*
 * aviplay.c
 *
 * PURPOSE:
 *  Play a AVI file to video and sound devices.
 *
 * USAGE:
 *  aviplay FILE.AVI
 */


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


/* For logging purpose. */
#define THIS_FILE   "aviplay.c"

static const char *desc = 
" FILE		    						    \n"
"		    						    \n"
"  aviplay.c	    						    \n"
"		    						    \n"
" PURPOSE	    						    \n"
"		    						    \n"
"  Demonstrate how to play a AVI file.				    \n"
"		    						    \n"
" USAGE		    						    \n"
"		    						    \n"
"  aviplay FILE.AVI						    \n";

struct codec_fmt {
    pj_uint32_t         pjmedia_id;
    const char         *codec_id;
    /* Do we need to convert the decoded frame? */
    pj_bool_t           need_conversion;
    /* If conversion is needed, dst_fmt indicates the destination format */
    pjmedia_format_id   dst_fmt;
} codec_fmts[] = {{PJMEDIA_FORMAT_MJPEG, "mjpeg",
                   PJ_TRUE , PJMEDIA_FORMAT_I420},
                  {PJMEDIA_FORMAT_H263 , "h263" ,
                   PJ_FALSE, 0},
                  {PJMEDIA_FORMAT_MPEG4, "mp4v"}, 
                  {PJMEDIA_FORMAT_H264 , "h264"}
                 };

typedef struct avi_port_t
{
    pjmedia_vid_port   *vid_port;
    pjmedia_snd_port   *snd_port;
    pj_bool_t           is_running;
    pj_bool_t           is_quitting;
} avi_port_t;

typedef struct codec_port_data_t
{
    pjmedia_vid_codec   *codec;
    pjmedia_port        *src_port;
    pj_uint8_t          *enc_buf;
    pj_size_t            enc_buf_size;
    
    pjmedia_converter   *conv;
} codec_port_data_t;

static pj_status_t avi_event_cb(pjmedia_event *event,
                                void *user_data)
{
    avi_port_t *ap = (avi_port_t *)user_data;
    
    switch (event->type) {
    case PJMEDIA_EVENT_WND_CLOSED:
        ap->is_quitting = PJ_TRUE;
        break;
    case PJMEDIA_EVENT_MOUSE_BTN_DOWN:
        if (ap->is_running) {
            pjmedia_vid_port_stop(ap->vid_port);
            if (ap->snd_port)
                pjmedia_aud_stream_stop(
                    pjmedia_snd_port_get_snd_stream(ap->snd_port));
        } else {
            pjmedia_vid_port_start(ap->vid_port);
            if (ap->snd_port)
                pjmedia_aud_stream_start(
                    pjmedia_snd_port_get_snd_stream(ap->snd_port));
        }
        ap->is_running = !ap->is_running;
        break;
    default:
        return PJ_SUCCESS;
    }
    
    /* We handled the event on our own, so return non-PJ_SUCCESS here */
    return -1;
}

static pj_status_t codec_get_frame(pjmedia_port *port,
			           pjmedia_frame *frame)
{
    codec_port_data_t *port_data = (codec_port_data_t*)port->port_data.pdata;
    pjmedia_vid_codec *codec = port_data->codec;
    pjmedia_frame enc_frame;
    pj_status_t status;
    
    enc_frame.buf = port_data->enc_buf;
    enc_frame.size = port_data->enc_buf_size;
    
    if (port_data->conv) {
        pj_size_t frame_size = frame->size;
	
        status = pjmedia_port_get_frame(port_data->src_port, frame);
        if (status != PJ_SUCCESS) goto on_error;
	
        status = pjmedia_vid_codec_decode(codec, 1, frame,
                                          frame->size, &enc_frame);
        if (status != PJ_SUCCESS) goto on_error;
	
        frame->size = frame_size;
        status = pjmedia_converter_convert(port_data->conv, &enc_frame, frame);
        if (status != PJ_SUCCESS) goto on_error;
	
        return PJ_SUCCESS;
    }
    
    status = pjmedia_port_get_frame(port_data->src_port, &enc_frame);
    if (status != PJ_SUCCESS) goto on_error;
    
    status = pjmedia_vid_codec_decode(codec, 1, &enc_frame,
                                      frame->size, frame);
    if (status != PJ_SUCCESS) goto on_error;
    
    return PJ_SUCCESS;
    
on_error:
    pj_perror(3, THIS_FILE, status, "codec_get_frame() error");
    return status;
}

static int aviplay(pj_pool_t *pool, const char *fname)
{
    pjmedia_vid_port *renderer=NULL;
    pjmedia_vid_port_param param;
    const pjmedia_video_format_info *vfi;
    pjmedia_video_format_detail *vfd;
    pjmedia_snd_port *snd_port = NULL;
    pj_status_t status;
    int rc = 0;
    pjmedia_avi_streams *avi_streams;
    pjmedia_avi_stream *vid_stream, *aud_stream;
    pjmedia_port *vid_port = NULL, *aud_port = NULL;
    pjmedia_vid_codec *codec=NULL;
    avi_port_t avi_port;
    
    pj_bzero(&avi_port, sizeof(avi_port));
    
    status = pjmedia_avi_player_create_streams(pool, fname, 0, &avi_streams);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(2,("", status, "    Error playing %s", fname));
	rc = 210; goto on_return;
    }
    
    vid_stream = pjmedia_avi_streams_get_stream_by_media(avi_streams,
                                                         0,
                                                         PJMEDIA_TYPE_VIDEO);
    vid_port = pjmedia_avi_stream_get_port(vid_stream);
    
    if (vid_port) {
        pjmedia_vid_port_param_default(&param);
	
        status = pjmedia_vid_dev_default_param(pool,
                                               PJMEDIA_VID_DEFAULT_RENDER_DEV,
                                               &param.vidparam);
        if (status != PJ_SUCCESS) {
    	    rc = 220; goto on_return;
        }
	
        /* Create renderer, set it to active  */
        param.active = PJ_TRUE;
        param.vidparam.dir = PJMEDIA_DIR_RENDER;
        vfd = pjmedia_format_get_video_format_detail(&vid_port->info.fmt,
                                                     PJ_TRUE);
        pjmedia_format_init_video(&param.vidparam.fmt, 
                                  vid_port->info.fmt.id,
                                  vfd->size.w, vfd->size.h,
                                  vfd->fps.num, vfd->fps.denum);
	
        vfi = pjmedia_get_video_format_info(
                  pjmedia_video_format_mgr_instance(),
                  vid_port->info.fmt.id);
        /* Check whether the frame is encoded */
        if (!vfi || vfi->bpp == 0) {
            /* Yes, prepare codec */
            pj_str_t codec_id_st;
            unsigned info_cnt = 1, i, k;
            const pjmedia_vid_codec_info *codec_info;
            pj_str_t port_name = {"codec", 5};
            pj_uint8_t *enc_buf = NULL;
            pj_size_t enc_buf_size = 0;
            pjmedia_vid_dev_info rdr_info;
            pjmedia_port codec_port;
            codec_port_data_t codec_port_data;
            pjmedia_vid_codec_param codec_param;
            struct codec_fmt *codecp = NULL;
	    
            /* Lookup codec */
            for (i = 0; i < sizeof(codec_fmts)/sizeof(codec_fmts[0]); i++) {
                if (vid_port->info.fmt.id == codec_fmts[i].pjmedia_id) {
                    codecp = &codec_fmts[i];
                    break;
                }
            }
            if (!codecp) {
                rc = 242; goto on_return;
            }
            pj_cstr(&codec_id_st, codecp->codec_id);
            status = pjmedia_vid_codec_mgr_find_codecs_by_id(NULL,
                                                             &codec_id_st, 
                                                             &info_cnt, 
                                                             &codec_info,
                                                             NULL);
            if (status != PJ_SUCCESS) {
                rc = 245; goto on_return;
            }
            status = pjmedia_vid_codec_mgr_get_default_param(NULL, codec_info,
                                                             &codec_param);
            if (status != PJ_SUCCESS) {
                rc = 246; goto on_return;
            }
	    
            pjmedia_format_copy(&codec_param.enc_fmt, &param.vidparam.fmt);

            pjmedia_vid_dev_get_info(param.vidparam.rend_id, &rdr_info);
            for (i=0; i<codec_info->dec_fmt_id_cnt; ++i) {
                for (k=0; k<rdr_info.fmt_cnt; ++k) {
                    if (codec_info->dec_fmt_id[i]==(int)rdr_info.fmt[k].id)
                    {
                        param.vidparam.fmt.id = codec_info->dec_fmt_id[i];
                        i = codec_info->dec_fmt_id_cnt;
                        break;
                    }
                }
            }
	    
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
	    
            pjmedia_format_copy(&codec_param.dec_fmt, &param.vidparam.fmt);
            codec_param.dir = PJMEDIA_DIR_DECODING;
            codec_param.packing = PJMEDIA_VID_PACKING_WHOLE;
            status = pjmedia_vid_codec_open(codec, &codec_param);
            if (status != PJ_SUCCESS) {
                rc = 252; goto on_return;
            }
	    
            /* Alloc encoding buffer */
            enc_buf_size =  codec_param.dec_fmt.det.vid.size.w *
	    codec_param.dec_fmt.det.vid.size.h * 4
	    + 16; /*< padding, just in case */
            enc_buf = pj_pool_alloc(pool,enc_buf_size);
	    
            /* Init codec port */
            pj_bzero(&codec_port, sizeof(codec_port));
            status = pjmedia_port_info_init2(&codec_port.info, &port_name,
                                             0x1234,
                                             PJMEDIA_DIR_ENCODING, 
                                             &codec_param.dec_fmt);
            if (status != PJ_SUCCESS) {
                rc = 260; goto on_return;
            }
            pj_bzero(&codec_port_data, sizeof(codec_port_data));
            codec_port_data.codec = codec;
            codec_port_data.src_port = vid_port;
            codec_port_data.enc_buf = enc_buf;
            codec_port_data.enc_buf_size = enc_buf_size;
	    
            codec_port.get_frame = &codec_get_frame;
            codec_port.port_data.pdata = &codec_port_data;
	    
            /* Check whether we need to convert the decoded frame */
            if (codecp->need_conversion) {
                pjmedia_conversion_param conv_param;
		
                pjmedia_format_copy(&conv_param.src, &param.vidparam.fmt);
                pjmedia_format_copy(&conv_param.dst, &param.vidparam.fmt);
                conv_param.dst.id = codecp->dst_fmt;
                param.vidparam.fmt.id = conv_param.dst.id;
		
                status = pjmedia_converter_create(NULL, pool, &conv_param,
                                                  &codec_port_data.conv);
                if (status != PJ_SUCCESS) {
                    rc = 270; goto on_return;
                }
            }
	    
            status = pjmedia_vid_port_create(pool, &param, &renderer);
            if (status != PJ_SUCCESS) {
                rc = 230; goto on_return;
            }
	    
            status = pjmedia_vid_port_connect(renderer, &codec_port,
                                              PJ_FALSE);
        } else {
            status = pjmedia_vid_port_create(pool, &param, &renderer);
            if (status != PJ_SUCCESS) {
                rc = 230; goto on_return;
            }
	    
            /* Connect avi port to renderer */
            status = pjmedia_vid_port_connect(renderer, vid_port,
                                              PJ_FALSE);
        }
	
        if (status != PJ_SUCCESS) {
            rc = 240; goto on_return;
        }
    }
    
    aud_stream = pjmedia_avi_streams_get_stream_by_media(avi_streams,
                                                         0,
                                                         PJMEDIA_TYPE_AUDIO);
    aud_port = pjmedia_avi_stream_get_port(aud_stream);
    
    if (aud_port) {
        /* Create sound player port. */
        status = pjmedia_snd_port_create_player( 
		 pool,				    /* pool		    */
		 -1,				    /* use default dev.	    */
		 PJMEDIA_PIA_SRATE(&aud_port->info),/* clock rate.	    */
		 PJMEDIA_PIA_CCNT(&aud_port->info), /* # of channels.	    */
		 PJMEDIA_PIA_SPF(&aud_port->info),  /* samples per frame.   */
		 PJMEDIA_PIA_BITS(&aud_port->info), /* bits per sample.	    */
		 0,				    /* options		    */
		 &snd_port			    /* returned port	    */
		 );
        if (status != PJ_SUCCESS) {
            rc = 310; goto on_return;
        }
	
        /* Connect file port to the sound player.
         * Stream playing will commence immediately.
         */
        status = pjmedia_snd_port_connect(snd_port, aud_port);
        if (status != PJ_SUCCESS) {
            rc = 330; goto on_return;
        }
    }
    
    if (vid_port) {
        pjmedia_vid_dev_cb cb;
	
        pj_bzero(&cb, sizeof(cb));
        avi_port.snd_port = snd_port;
        avi_port.vid_port = renderer;
        avi_port.is_running = PJ_TRUE;
        pjmedia_vid_port_set_cb(renderer, &cb, &avi_port);

        /* subscribe events */
        pjmedia_event_subscribe(NULL, &avi_event_cb, &avi_port,
                                renderer);

        if (snd_port) {
            /* Synchronize video rendering and audio playback */
            pjmedia_vid_port_set_clock_src(
                renderer,
                pjmedia_snd_port_get_clock_src(
                    snd_port, PJMEDIA_DIR_PLAYBACK));
        }
                                              
	
        /* Start video streaming.. */
        status = pjmedia_vid_port_start(renderer);
        if (status != PJ_SUCCESS) {
            rc = 270; goto on_return;
        }
    }
    
    while (!avi_port.is_quitting) {
        pj_thread_sleep(100);
    }

on_return:
    if (snd_port) {
        pjmedia_snd_port_disconnect(snd_port);
        /* Without this sleep, Windows/DirectSound will repeteadly
         * play the last frame during destroy.
         */
        pj_thread_sleep(100);
        pjmedia_snd_port_destroy(snd_port);
    }
    if (renderer) {
        pjmedia_event_unsubscribe(NULL, &avi_event_cb, &avi_port,
                                  renderer);
        pjmedia_vid_port_destroy(renderer);
    }
    if (aud_port)
        pjmedia_port_destroy(aud_port);
    if (vid_port)
        pjmedia_port_destroy(vid_port);
    if (codec) {
        pjmedia_vid_codec_close(codec);
        pjmedia_vid_codec_mgr_dealloc_codec(NULL, codec);
    }
    
    return rc;
}


static int main_func(int argc, char *argv[])
{
    pj_caching_pool cp;
    pj_pool_t *pool;
    int rc = 0;
    pj_status_t status = PJ_SUCCESS;
    
    if (argc != 2) {
    	puts("Error: filename required");
	puts(desc);
	return 1;
    }


    /* Must init PJLIB first: */
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    /* Must create a pool factory before we can allocate any memory. */
    pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

    /* Create memory pool for our file player */
    pool = pj_pool_create( &cp.factory,	    /* pool factory	    */
			   "AVI",	    /* pool name.	    */
			   4000,	    /* init size	    */
			   4000,	    /* increment size	    */
			   NULL		    /* callback on error    */
			   );

    pjmedia_video_format_mgr_create(pool, 64, 0, NULL);
    pjmedia_converter_mgr_create(pool, NULL);
    pjmedia_event_mgr_create(pool, 0, NULL);
    pjmedia_vid_codec_mgr_create(pool, NULL);
    
    status = pjmedia_vid_dev_subsys_init(&cp.factory);
    if (status != PJ_SUCCESS)
        goto on_return;
    
    status = pjmedia_aud_subsys_init(&cp.factory);
    if (status != PJ_SUCCESS) {
        goto on_return;
    }
    
#if PJMEDIA_HAS_FFMPEG_VID_CODEC
    status = pjmedia_codec_ffmpeg_vid_init(NULL, &cp.factory);
    if (status != PJ_SUCCESS)
	goto on_return;    
#endif

    rc = aviplay(pool, argv[1]);
    
    /* 
     * File should be playing and looping now 
     */

    /* Without this sleep, Windows/DirectSound will repeteadly
     * play the last frame during destroy.
     */
    pj_thread_sleep(100);

on_return:    
#if PJMEDIA_HAS_FFMPEG_VID_CODEC
    pjmedia_codec_ffmpeg_vid_deinit();
#endif
    pjmedia_aud_subsys_shutdown();
    pjmedia_vid_dev_subsys_shutdown();
    
    pjmedia_video_format_mgr_destroy(pjmedia_video_format_mgr_instance());
    pjmedia_converter_mgr_destroy(pjmedia_converter_mgr_instance());
    pjmedia_event_mgr_destroy(pjmedia_event_mgr_instance());
    pjmedia_vid_codec_mgr_destroy(pjmedia_vid_codec_mgr_instance());    
    
    /* Release application pool */
    pj_pool_release( pool );

    /* Destroy pool factory */
    pj_caching_pool_destroy( &cp );

    /* Shutdown PJLIB */
    pj_shutdown();

    /* Done. */
    return 0;
}

int main(int argc, char *argv[])
{
    return pj_run_app(&main_func, argc, argv, 0);
}

#else

int main(int argc, char *argv[])
{
    PJ_UNUSED_ARG(argc);
    PJ_UNUSED_ARG(argv);
    puts("Error: this sample requires video capability (PJMEDIA_HAS_VIDEO == 1)");
    return -1;
}

#endif /* PJMEDIA_HAS_VIDEO */
