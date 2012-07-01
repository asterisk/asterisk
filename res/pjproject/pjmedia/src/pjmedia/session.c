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
#include <pjmedia/session.h>
#include <pjmedia/errno.h>
#include <pj/log.h>
#include <pj/os.h> 
#include <pj/pool.h>
#include <pj/string.h>
#include <pj/assert.h>
#include <pj/ctype.h>
#include <pj/rand.h>


struct pjmedia_session
{
    pj_pool_t		   *pool;
    pjmedia_endpt	   *endpt;
    unsigned		    stream_cnt;
    pjmedia_stream_info	    stream_info[PJMEDIA_MAX_SDP_MEDIA];
    pjmedia_stream	   *stream[PJMEDIA_MAX_SDP_MEDIA];
    void		   *user_data;
};

#define THIS_FILE		"session.c"

#ifndef PJMEDIA_SESSION_SIZE
#   define PJMEDIA_SESSION_SIZE	(10*1024)
#endif

#ifndef PJMEDIA_SESSION_INC
#   define PJMEDIA_SESSION_INC	1024
#endif


static const pj_str_t ID_AUDIO = { "audio", 5};
static const pj_str_t ID_VIDEO = { "video", 5};
static const pj_str_t ID_APPLICATION = { "application", 11};
static const pj_str_t ID_IN = { "IN", 2 };
static const pj_str_t ID_IP4 = { "IP4", 3};
static const pj_str_t ID_IP6 = { "IP6", 3};
static const pj_str_t ID_RTP_AVP = { "RTP/AVP", 7 };
static const pj_str_t ID_RTP_SAVP = { "RTP/SAVP", 8 };
//static const pj_str_t ID_SDP_NAME = { "pjmedia", 7 };
static const pj_str_t ID_RTPMAP = { "rtpmap", 6 };
static const pj_str_t ID_TELEPHONE_EVENT = { "telephone-event", 15 };

static const pj_str_t STR_INACTIVE = { "inactive", 8 };
static const pj_str_t STR_SENDRECV = { "sendrecv", 8 };
static const pj_str_t STR_SENDONLY = { "sendonly", 8 };
static const pj_str_t STR_RECVONLY = { "recvonly", 8 };

/*
 * Initialize session info from SDP session descriptors.
 */
PJ_DEF(pj_status_t) pjmedia_session_info_from_sdp( pj_pool_t *pool,
			       pjmedia_endpt *endpt,
			       unsigned max_streams,
			       pjmedia_session_info *si,
			       const pjmedia_sdp_session *local,
			       const pjmedia_sdp_session *remote)
{
    unsigned i;

    PJ_ASSERT_RETURN(pool && endpt && si && local && remote, PJ_EINVAL);

    si->stream_cnt = max_streams;
    if (si->stream_cnt > local->media_count)
	si->stream_cnt = local->media_count;

    for (i=0; i<si->stream_cnt; ++i) {
	pj_status_t status;

	status = pjmedia_stream_info_from_sdp( &si->stream_info[i], pool,
					       endpt, 
					       local, remote, i);
	if (status != PJ_SUCCESS)
	    return status;
    }

    return PJ_SUCCESS;
}


/**
 * Create new session.
 */
PJ_DEF(pj_status_t) pjmedia_session_create( pjmedia_endpt *endpt, 
					    const pjmedia_session_info *si,
					    pjmedia_transport *transports[],
					    void *user_data,
					    pjmedia_session **p_session )
{
    pj_pool_t *pool;
    pjmedia_session *session;
    int i; /* Must be signed */
    pj_status_t status;

    /* Verify arguments. */
    PJ_ASSERT_RETURN(endpt && si && p_session, PJ_EINVAL);

    /* Create pool for the session. */
    pool = pjmedia_endpt_create_pool( endpt, "session", 
				      PJMEDIA_SESSION_SIZE, 
				      PJMEDIA_SESSION_INC);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    session = PJ_POOL_ZALLOC_T(pool, pjmedia_session);
    session->pool = pool;
    session->endpt = endpt;
    session->stream_cnt = si->stream_cnt;
    session->user_data = user_data;

    /* Copy stream info (this simple memcpy may break sometime) */
    pj_memcpy(session->stream_info, si->stream_info,
	      si->stream_cnt * sizeof(pjmedia_stream_info));

    /*
     * Now create and start the stream!
     */
    for (i=0; i<(int)si->stream_cnt; ++i) {

	/* Create the stream */
	status = pjmedia_stream_create(endpt, session->pool,
				       &session->stream_info[i],
				       (transports?transports[i]:NULL),
				       session,
				       &session->stream[i]);
	if (status == PJ_SUCCESS)
	    status = pjmedia_stream_start(session->stream[i]);

	if (status != PJ_SUCCESS) {

	    for ( --i; i>=0; ++i) {
		pjmedia_stream_destroy(session->stream[i]);
	    }

	    pj_pool_release(session->pool);
	    return status;
	}
    }


    /* Done. */

    *p_session = session;
    return PJ_SUCCESS;
}


/*
 * Get session info.
 */
PJ_DEF(pj_status_t) pjmedia_session_get_info( pjmedia_session *session,
					      pjmedia_session_info *info )
{
    PJ_ASSERT_RETURN(session && info, PJ_EINVAL);

    info->stream_cnt = session->stream_cnt;
    pj_memcpy(info->stream_info, session->stream_info,
	      session->stream_cnt * sizeof(pjmedia_stream_info));

    return PJ_SUCCESS;
}

/*
 * Get user data.
 */
PJ_DEF(void*) pjmedia_session_get_user_data( pjmedia_session *session)
{
    return (session? session->user_data : NULL);
}

/**
 * Destroy media session.
 */
PJ_DEF(pj_status_t) pjmedia_session_destroy (pjmedia_session *session)
{
    unsigned i;

    PJ_ASSERT_RETURN(session, PJ_EINVAL);

    for (i=0; i<session->stream_cnt; ++i) {
	
	pjmedia_stream_destroy(session->stream[i]);

    }

    pj_pool_release (session->pool);

    return PJ_SUCCESS;
}


/**
 * Activate all stream in media session.
 *
 */
PJ_DEF(pj_status_t) pjmedia_session_resume(pjmedia_session *session,
					   pjmedia_dir dir)
{
    unsigned i;

    PJ_ASSERT_RETURN(session, PJ_EINVAL);

    for (i=0; i<session->stream_cnt; ++i) {
	pjmedia_session_resume_stream(session, i, dir);
    }

    return PJ_SUCCESS;
}


/**
 * Suspend receipt and transmission of all stream in media session.
 *
 */
PJ_DEF(pj_status_t) pjmedia_session_pause(pjmedia_session *session,
					  pjmedia_dir dir)
{
    unsigned i;

    PJ_ASSERT_RETURN(session, PJ_EINVAL);

    for (i=0; i<session->stream_cnt; ++i) {
	pjmedia_session_pause_stream(session, i, dir);
    }

    return PJ_SUCCESS;
}


/**
 * Suspend receipt and transmission of individual stream in media session.
 */
PJ_DEF(pj_status_t) pjmedia_session_pause_stream( pjmedia_session *session,
						  unsigned index,
						  pjmedia_dir dir)
{
    PJ_ASSERT_RETURN(session && index < session->stream_cnt, PJ_EINVAL);

    return pjmedia_stream_pause(session->stream[index], dir);
}


/**
 * Activate individual stream in media session.
 *
 */
PJ_DEF(pj_status_t) pjmedia_session_resume_stream( pjmedia_session *session,
						   unsigned index,
						   pjmedia_dir dir)
{
    PJ_ASSERT_RETURN(session && index < session->stream_cnt, PJ_EINVAL);

    return pjmedia_stream_resume(session->stream[index], dir);
}

/**
 * Send RTCP SDES for the session.
 */
PJ_DEF(pj_status_t) 
pjmedia_session_send_rtcp_sdes( const pjmedia_session *session )
{
    unsigned i;

    PJ_ASSERT_RETURN(session, PJ_EINVAL);

    for (i=0; i<session->stream_cnt; ++i) {
	pjmedia_stream_send_rtcp_sdes(session->stream[i]);
    }

    return PJ_SUCCESS;
}

/**
 * Send RTCP BYE for the session.
 */
PJ_DEF(pj_status_t) 
pjmedia_session_send_rtcp_bye( const pjmedia_session *session )
{
    unsigned i;

    PJ_ASSERT_RETURN(session, PJ_EINVAL);

    for (i=0; i<session->stream_cnt; ++i) {
	pjmedia_stream_send_rtcp_bye(session->stream[i]);
    }

    return PJ_SUCCESS;
}

/**
 * Enumerate media stream in the session.
 */
PJ_DEF(pj_status_t) pjmedia_session_enum_streams(const pjmedia_session *session,
						 unsigned *count, 
						 pjmedia_stream_info info[])
{
    unsigned i;

    PJ_ASSERT_RETURN(session && count && *count && info, PJ_EINVAL);

    if (*count > session->stream_cnt)
	*count = session->stream_cnt;

    for (i=0; i<*count; ++i) {
	pj_memcpy(&info[i], &session->stream_info[i], 
                  sizeof(pjmedia_stream_info));
    }

    return PJ_SUCCESS;
}


/*
 * Get the port interface.
 */
PJ_DEF(pj_status_t) pjmedia_session_get_port(  pjmedia_session *session,
					       unsigned index,
					       pjmedia_port **p_port)
{
    return pjmedia_stream_get_port( session->stream[index], p_port);
}

/*
 * Get statistics
 */
PJ_DEF(pj_status_t) pjmedia_session_get_stream_stat( pjmedia_session *session,
						     unsigned index,
						     pjmedia_rtcp_stat *stat)
{
    PJ_ASSERT_RETURN(session && stat && index < session->stream_cnt, 
		     PJ_EINVAL);

    return pjmedia_stream_get_stat(session->stream[index], stat);
}


/**
 * Reset session statistics.
 */
PJ_DEF(pj_status_t) pjmedia_session_reset_stream_stat( pjmedia_session *session,
						       unsigned index)
{
    PJ_ASSERT_RETURN(session && index < session->stream_cnt, PJ_EINVAL);

    return pjmedia_stream_reset_stat(session->stream[index]);
}


#if defined(PJMEDIA_HAS_RTCP_XR) && (PJMEDIA_HAS_RTCP_XR != 0)
/*
 * Get extended statistics
 */
PJ_DEF(pj_status_t) pjmedia_session_get_stream_stat_xr(
					     pjmedia_session *session,
					     unsigned index,
					     pjmedia_rtcp_xr_stat *stat_xr)
{
    PJ_ASSERT_RETURN(session && stat_xr && index < session->stream_cnt, 
		     PJ_EINVAL);

    return pjmedia_stream_get_stat_xr(session->stream[index], stat_xr);
}
#endif

PJ_DEF(pj_status_t) pjmedia_session_get_stream_stat_jbuf(
					    pjmedia_session *session,
					    unsigned index,
					    pjmedia_jb_state *state)
{
    PJ_ASSERT_RETURN(session && state && index < session->stream_cnt, 
		     PJ_EINVAL);

    return pjmedia_stream_get_stat_jbuf(session->stream[index], state);
}

/*
 * Dial DTMF digit to the stream, using RFC 2833 mechanism.
 */
PJ_DEF(pj_status_t) pjmedia_session_dial_dtmf( pjmedia_session *session,
					       unsigned index,
					       const pj_str_t *ascii_digits )
{
    PJ_ASSERT_RETURN(session && ascii_digits, PJ_EINVAL);
    return pjmedia_stream_dial_dtmf(session->stream[index], ascii_digits);
}

/*
 * Check if the specified stream has received DTMF digits.
 */
PJ_DEF(pj_status_t) pjmedia_session_check_dtmf( pjmedia_session *session,
					        unsigned index )
{
    PJ_ASSERT_RETURN(session, PJ_EINVAL);
    return pjmedia_stream_check_dtmf(session->stream[index]);
}


/*
 * Retrieve DTMF digits from the specified stream.
 */
PJ_DEF(pj_status_t) pjmedia_session_get_dtmf( pjmedia_session *session,
					      unsigned index,
					      char *ascii_digits,
					      unsigned *size )
{
    PJ_ASSERT_RETURN(session && ascii_digits && size, PJ_EINVAL);
    return pjmedia_stream_get_dtmf(session->stream[index], ascii_digits,
				   size);
}

/*
 * Install DTMF callback.
 */
PJ_DEF(pj_status_t) pjmedia_session_set_dtmf_callback(pjmedia_session *session,
				  unsigned index,
				  void (*cb)(pjmedia_stream*, 
				 	     void *user_data, 
					     int digit), 
				  void *user_data)
{
    PJ_ASSERT_RETURN(session && index < session->stream_cnt, PJ_EINVAL);
    return pjmedia_stream_set_dtmf_callback(session->stream[index], cb,
					    user_data);
}

