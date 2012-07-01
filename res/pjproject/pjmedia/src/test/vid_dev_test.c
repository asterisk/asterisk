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
#define LOOP_DURATION	10

static pj_bool_t is_quitting = PJ_FALSE;

static const char *vid_dir_name(pjmedia_dir dir)
{
    switch (dir) {
    case PJMEDIA_DIR_CAPTURE:
	return "capture";
    case PJMEDIA_DIR_RENDER:
	return "render";
    case PJMEDIA_DIR_CAPTURE_RENDER:
	return "capture & render";
    default:
	return "unknown";
    }
}

static int enum_devs(void)
{
    unsigned i, dev_cnt;
    pj_status_t status;

    PJ_LOG(3, (THIS_FILE, " Enum video devices:"));
    dev_cnt = pjmedia_vid_dev_count();
    for (i = 0; i < dev_cnt; ++i) {
        pjmedia_vid_dev_info di;
        status = pjmedia_vid_dev_get_info(i, &di);
        if (status == PJ_SUCCESS) {
            unsigned j;

            PJ_LOG(3, (THIS_FILE, " %3d: %s (%s) - %s", i, di.name, di.driver,
        	       vid_dir_name(di.dir)));

            PJ_LOG(3,(THIS_FILE, "      Supported formats:"));
            for (j=0; j<di.fmt_cnt; ++j) {
        	const pjmedia_video_format_info *vfi;

        	vfi = pjmedia_get_video_format_info(NULL, di.fmt[j].id);
        	PJ_LOG(3,(THIS_FILE, "       %s",
        		  (vfi ? vfi->name : "unknown")));
            }
        }
    }

    return PJ_SUCCESS;
}

static pj_status_t vid_event_cb(pjmedia_event *event,
                                void *user_data)
{
    PJ_UNUSED_ARG(user_data);

    if (event->type == PJMEDIA_EVENT_WND_CLOSED)
        is_quitting = PJ_TRUE;

    return PJ_SUCCESS;
}

static int capture_render_loopback(int cap_dev_id, int rend_dev_id,
                                   const pjmedia_format *fmt)
{
    pj_pool_t *pool;
    pjmedia_vid_port *capture=NULL, *renderer=NULL;
    pjmedia_vid_dev_info cdi, rdi;
    pjmedia_vid_port_param param;
    pjmedia_video_format_detail *vfd;
    pj_status_t status;
    int rc = 0, i;

    pool = pj_pool_create(mem, "vidloop", 1000, 1000, NULL);

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
    param.active = PJ_TRUE;

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

    param.active = PJ_FALSE;
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
                 capture,
		 pjmedia_vid_port_get_passive_port(renderer),
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

static int loopback_test(void)
{
    unsigned count, i;
    pjmedia_format_id test_fmts[] = {
        PJMEDIA_FORMAT_YUY2
    };
    pjmedia_rect_size test_sizes[] = {
	{176,144},	/* QCIF */
	{352,288},	/* CIF */
	{704,576}	/* 4CIF */
    };
    pjmedia_ratio test_fpses[] = {
	{25, 1},
	{30, 1},
    };
    pj_status_t status;

    PJ_LOG(3, (THIS_FILE, " Loopback tests (prepare you webcams):"));

    count = pjmedia_vid_dev_count();
    for (i=0; i<count; ++i) {
	pjmedia_vid_dev_info cdi;
	unsigned j;

	status = pjmedia_vid_dev_get_info(i, &cdi);
	if (status != PJ_SUCCESS)
	    return -300;

	/* Only interested with capture device */
	if ((cdi.dir & PJMEDIA_DIR_CAPTURE) == 0)
	    continue;

	for (j=i+1; j<count; ++j) {
	    pjmedia_vid_dev_info rdi;
	    unsigned k;

	    status = pjmedia_vid_dev_get_info(j, &rdi);
	    if (status != PJ_SUCCESS)
		return -310;

	    /* Only interested with render device */
	    if ((rdi.dir & PJMEDIA_DIR_RENDER) == 0)
		continue;

	    /* Test with the format, size, and fps combinations */
	    for (k=0; k<PJ_ARRAY_SIZE(test_fmts); ++k) {
		unsigned l;

		for (l=0; l<PJ_ARRAY_SIZE(test_sizes); ++l) {
		    unsigned m;

		    for (m=0; m<PJ_ARRAY_SIZE(test_fpses); ++m) {
			pjmedia_format fmt;

			pjmedia_format_init_video(&fmt, test_fmts[k],
			                          test_sizes[l].w,
			                          test_sizes[l].h,
			                          test_fpses[m].num,
			                          test_fpses[m].denum);

			capture_render_loopback(i, j, &fmt);
		    }
		}
	    } /* k */

	}
    }

    return 0;
}

int vid_dev_test(void)
{
    int rc = 0;
    pj_status_t status;
    
    status = pjmedia_vid_dev_subsys_init(mem);
    if (status != PJ_SUCCESS)
        return -10;

    rc = enum_devs();
    if (rc != 0)
	goto on_return;

    rc = loopback_test();
    if (rc != 0)
	goto on_return;

on_return:
    pjmedia_vid_dev_subsys_shutdown();
    
    return rc;
}


#endif /* PJMEDIA_HAS_VIDEO */
