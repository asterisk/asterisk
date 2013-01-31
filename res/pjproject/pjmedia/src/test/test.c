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

#define THIS_FILE   "test.c"

#define DO_TEST(test)	do { \
			    PJ_LOG(3, (THIS_FILE, "Running %s...", #test));  \
			    rc = test; \
			    PJ_LOG(3, (THIS_FILE,  \
				       "%s(%d)",  \
				       (rc ? "..ERROR" : "..success"), rc)); \
			    if (rc!=0) goto on_return; \
			} while (0)


pj_pool_factory *mem;


void app_perror(pj_status_t status, const char *msg)
{
    char errbuf[PJ_ERR_MSG_SIZE];
    
    pjmedia_strerror(status, errbuf, sizeof(errbuf));

    PJ_LOG(3,(THIS_FILE, "%s: %s", msg, errbuf));
}

/* Force linking PLC stuff if G.711 is disabled. See:
 *  https://trac.pjsip.org/repos/ticket/1337 
 */
#if PJMEDIA_HAS_G711_CODEC==0
void *dummy()
{
    // Dummy
    return &pjmedia_plc_save;
}
#endif

int test_main(void)
{
    int rc = 0;
    pj_caching_pool caching_pool;
    pj_pool_t *pool;

    pj_init();
    pj_caching_pool_init(&caching_pool, &pj_pool_factory_default_policy, 0);
    pool = pj_pool_create(&caching_pool.factory, "test", 1000, 512, NULL);

    pj_log_set_decor(PJ_LOG_HAS_NEWLINE);
    pj_log_set_level(3);

    mem = &caching_pool.factory;

#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
    pjmedia_video_format_mgr_create(pool, 64, 0, NULL);
    pjmedia_converter_mgr_create(pool, NULL);
    pjmedia_event_mgr_create(pool, 0, NULL);
    pjmedia_vid_codec_mgr_create(pool, NULL);
#endif

#if HAS_VID_PORT_TEST
    DO_TEST(vid_port_test());
#endif

#if HAS_VID_DEV_TEST
    DO_TEST(vid_dev_test());
#endif

#if HAS_VID_CODEC_TEST
    DO_TEST(vid_codec_test());
#endif

#if HAS_SDP_NEG_TEST
    DO_TEST(sdp_neg_test());
#endif
    //DO_TEST(sdp_test (&caching_pool.factory));
    //DO_TEST(rtp_test(&caching_pool.factory));
    //DO_TEST(session_test (&caching_pool.factory));
#if HAS_JBUF_TEST
    DO_TEST(jbuf_main());
#endif
#if HAS_MIPS_TEST
    DO_TEST(mips_test());
#endif
#if HAS_CODEC_VECTOR_TEST
    DO_TEST(codec_test_vectors());
#endif

    PJ_LOG(3,(THIS_FILE," "));

on_return:
    if (rc != 0) {
	PJ_LOG(3,(THIS_FILE,"Test completed with error(s)!"));
    } else {
	PJ_LOG(3,(THIS_FILE,"Looks like everything is okay!"));
    }

#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
    pjmedia_video_format_mgr_destroy(pjmedia_video_format_mgr_instance());
    pjmedia_converter_mgr_destroy(pjmedia_converter_mgr_instance());
    pjmedia_event_mgr_destroy(pjmedia_event_mgr_instance());
    pjmedia_vid_codec_mgr_destroy(pjmedia_vid_codec_mgr_instance());
#endif

    pj_pool_release(pool);
    pj_caching_pool_destroy(&caching_pool);

    return rc;
}
