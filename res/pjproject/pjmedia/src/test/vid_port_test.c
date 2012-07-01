/* $Id$ */
/*
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
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
#include <pjmedia-audiodev/audiodev.h>
#include <pjmedia-codec/ffmpeg_vid_codecs.h>
#include <pjmedia/vid_codec.h>
#include <pjmedia_videodev.h>


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#define THIS_FILE 	"vid_dev_test.c"
#define LOOP_DURATION	6

static pj_bool_t is_quitting = PJ_FALSE;

static pj_status_t vid_event_cb(pjmedia_event *event,
                                void *user_data)
{
    PJ_UNUSED_ARG(user_data);

    if (event->type == PJMEDIA_EVENT_WND_CLOSED)
        is_quitting = PJ_TRUE;

    return PJ_SUCCESS;
}

static int capture_render_loopback(pj_bool_t active,
				   int cap_dev_id, int rend_dev_id,
                                   const pjmedia_format *fmt)
{
    pj_pool_t *pool;
    pjmedia_vid_port *capture=NULL, *renderer=NULL;
    pjmedia_vid_dev_info cdi, rdi;
    pjmedia_vid_port_param param;
    pjmedia_video_format_detail *vfd;
    pj_status_t status;
    int rc = 0, i;

    pool = pj_pool_create(mem, "vidportloop", 1000, 1000, NULL);

    status = pjmedia_vid_dev_get_info(cap_dev_id, &cdi);
    if (status != PJ_SUCCESS)
	goto on_return;

    status = pjmedia_vid_dev_get_info(rend_dev_id, &rdi);
    if (status != PJ_SUCCESS)
	goto on_return;

    PJ_LOG(3,(THIS_FILE,
	      "  %s (%s) ===> %s (%s)\t%s\t%dx%d\t@%d:%d fps",
	      cdi.name, cdi.driver, rdi.name, rdi.driver,
	      pjmedia_get_video_format_info(NULL, fmt->id)->name,
	      fmt->det.vid.size.w, fmt->det.vid.size.h,
	      fmt->det.vid.fps.num, fmt->det.vid.fps.denum));

    pjmedia_vid_port_param_default(&param);

    /* Create capture, set it to active (master) */
    status = pjmedia_vid_dev_default_param(pool, cap_dev_id,
					   &param.vidparam);
    if (status != PJ_SUCCESS) {
	rc = 100; goto on_return;
    }
    param.vidparam.dir = PJMEDIA_DIR_CAPTURE;
    param.vidparam.fmt = *fmt;
    param.active = (active? PJ_TRUE: PJ_FALSE);

    if (param.vidparam.fmt.detail_type != PJMEDIA_FORMAT_DETAIL_VIDEO) {
	rc = 103; goto on_return;
    }

    vfd = pjmedia_format_get_video_format_detail(&param.vidparam.fmt, PJ_TRUE);
    if (vfd == NULL) {
	rc = 105; goto on_return;
    }

    status = pjmedia_vid_port_create(pool, &param, &capture);
    if (status != PJ_SUCCESS) {
	rc = 110; goto on_return;
    }

    /* Create renderer, set it to passive (slave)  */
    status = pjmedia_vid_dev_default_param(pool, rend_dev_id,
					   &param.vidparam);
    if (status != PJ_SUCCESS) {
	rc = 120; goto on_return;
    }

    param.active = (active? PJ_FALSE: PJ_TRUE);
    param.vidparam.dir = PJMEDIA_DIR_RENDER;
    param.vidparam.rend_id = rend_dev_id;
    param.vidparam.fmt = *fmt;
    param.vidparam.disp_size = vfd->size;

    status = pjmedia_vid_port_create(pool, &param, &renderer);
    if (status != PJ_SUCCESS) {
	rc = 130; goto on_return;
    }

    /* Set event handler */
    pjmedia_event_subscribe(NULL, &vid_event_cb, NULL, renderer);

    /* Connect capture to renderer */
    status = pjmedia_vid_port_connect(
	         (active? capture: renderer),
		 pjmedia_vid_port_get_passive_port(active? renderer: capture),
		 PJ_FALSE);
    if (status != PJ_SUCCESS) {
	rc = 140; goto on_return;
    }

    /* Start streaming.. */
    status = pjmedia_vid_port_start(renderer);
    if (status != PJ_SUCCESS) {
	rc = 150; goto on_return;
    }
    status = pjmedia_vid_port_start(capture);
    if (status != PJ_SUCCESS) {
	rc = 160; goto on_return;
    }

    /* Sleep while the webcam is being displayed... */
    for (i = 0; i < LOOP_DURATION*10 && (!is_quitting); i++) {
        pj_thread_sleep(100);
    }

on_return:
    if (status != PJ_SUCCESS)
	PJ_PERROR(3, (THIS_FILE, status, "   error"));

    if (capture)
        pjmedia_vid_port_stop(capture);
    if (renderer)
        pjmedia_vid_port_stop(renderer);
    if (capture)
	pjmedia_vid_port_destroy(capture);
    if (renderer) {
        pjmedia_event_unsubscribe(NULL, &vid_event_cb, NULL, renderer);
	pjmedia_vid_port_destroy(renderer);
    }

    pj_pool_release(pool);
    return rc;
}

static int find_device(pjmedia_dir dir,
		       pj_bool_t has_callback)
{
    unsigned i, count = pjmedia_vid_dev_count();
 
    for (i = 0; i < count; ++i) {
	pjmedia_vid_dev_info cdi;

	if (pjmedia_vid_dev_get_info(i, &cdi) != PJ_SUCCESS)
	    continue;
	if ((cdi.dir & dir) != 0 && cdi.has_callback == has_callback)
	    return i;
    }
    
    return -999;
}

static int vidport_test(void)
{
    int i, j, k, l;
    int cap_id, rend_id;
    pjmedia_format_id test_fmts[] = {
        PJMEDIA_FORMAT_RGBA,
        PJMEDIA_FORMAT_I420
    };

    PJ_LOG(3, (THIS_FILE, " Video port tests:"));

    /* Capturer's role: active/passive. */
    for (i = 1; i >= 0; i--) {
	/* Capturer's device has_callback: TRUE/FALSE. */
	for (j = 1; j >= 0; j--) {
	    cap_id = find_device(PJMEDIA_DIR_CAPTURE, j);
	    if (cap_id < 0)
		continue;

	    /* Renderer's device has callback: TRUE/FALSE. */
	    for (k = 1; k >= 0; k--) {
		rend_id = find_device(PJMEDIA_DIR_RENDER, k);
		if (rend_id < 0)
		    continue;

		/* Check various formats to test format conversion. */
		for (l = 0; l < PJ_ARRAY_SIZE(test_fmts); ++l) {
		    pjmedia_format fmt;

		    PJ_LOG(3,(THIS_FILE,
			      "capturer %s (stream: %s) ===> "
			      "renderer %s (stream: %s)",
			      (i? "active": "passive"),
			      (j? "active": "passive"),
			      (i? "passive": "active"),
			      (k? "active": "passive")));

		    pjmedia_format_init_video(&fmt, test_fmts[l],
					      640, 480, 25, 1);
		    capture_render_loopback(i, cap_id, rend_id, &fmt);
		}
	    }
	}
    }

    return 0;
}

int vid_port_test(void)
{
    int rc = 0;
    pj_status_t status;

    status = pjmedia_vid_dev_subsys_init(mem);
    if (status != PJ_SUCCESS)
        return -10;

    rc = vidport_test();
    if (rc != 0)
	goto on_return;

on_return:
    pjmedia_vid_dev_subsys_shutdown();

    return rc;
}


#endif /* PJMEDIA_HAS_VIDEO */
