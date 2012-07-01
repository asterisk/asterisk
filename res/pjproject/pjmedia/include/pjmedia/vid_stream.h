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
#ifndef __PJMEDIA_VID_STREAM_H__
#define __PJMEDIA_VID_STREAM_H__


/**
 * @file vid_stream.h
 * @brief Video Stream.
 */

#include <pjmedia/endpoint.h>
#include <pjmedia/jbuf.h>
#include <pjmedia/port.h>
#include <pjmedia/rtcp.h>
#include <pjmedia/transport.h>
#include <pjmedia/vid_codec.h>
#include <pj/sock.h>

PJ_BEGIN_DECL


/**
 * @defgroup PJMED_VID_STRM Video streams
 * @ingroup PJMEDIA_PORT
 * @brief Video communication via the network
 * @{
 *
 * A video stream is a bidirectional video communication between two
 * endpoints. It corresponds to a video media description ("m=video" line)
 * in SDP session descriptor.
 *
 * A video stream consists of two unidirectional channels:
 *  - encoding channel, which transmits unidirectional video to remote, and
 *  - decoding channel, which receives unidirectional media from remote.
 *
 * A video stream exports two media port interface (see @ref PJMEDIA_PORT),
 * one for each direction, and application normally uses this interface to
 * interconnect the stream to other PJMEDIA components, e.g: the video
 * capture port supplies frames to the encoding port and video renderer
 * consumes frames from the decoding port.
 *
 * A video stream internally manages the following objects:
 *  - an instance of video codec (see @ref PJMEDIA_VID_CODEC),
 *  - an @ref PJMED_JBUF,
 *  - two instances of RTP sessions (#pjmedia_rtp_session, one for each
 *    direction),
 *  - one instance of RTCP session (#pjmedia_rtcp_session),
 *  - and a reference to video transport to send and receive packets
 *    to/from the network (see @ref PJMEDIA_TRANSPORT).
 *
 * Video streams are created by calling #pjmedia_vid_stream_create(),
 * specifying #pjmedia_stream_info structure in the parameter. Application
 * can construct the #pjmedia_vid_stream_info structure manually, or use 
 * #pjmedia_vid_stream_info_from_sdp() function to construct the
 * #pjmedia_vid stream_info from local and remote SDP session descriptors.
 */


/**
 * Enumeration of video stream sending rate control.
 */
typedef enum pjmedia_vid_stream_rc_method
{
    /**
     * No sending rate control. All outgoing RTP packets will be transmitted
     * immediately right after encoding process is done.
     */
    PJMEDIA_VID_STREAM_RC_NONE		    = 0,

    /**
     * Simple blocking. Each outgoing RTP packet transmission may be delayed
     * to avoid peak bandwidth that is much higher than specified. The thread
     * invoking the video stream put_frame(), e.g: video capture device thread,
     * will be blocked whenever transmission delay takes place.
     */
    PJMEDIA_VID_STREAM_RC_SIMPLE_BLOCKING   = 1

} pjmedia_vid_stream_rc_method;


/**
 * Structure of configuration settings for video stream sending rate control.
 */
typedef struct pjmedia_vid_stream_rc_config
{
    /**
     * Rate control method.
     *
     * Default: PJMEDIA_VID_STREAM_RC_SIMPLE_BLOCKING.
     */
    pjmedia_vid_stream_rc_method    method;

    /**
     * Upstream/outgoing bandwidth. If this is set to zero, the video stream
     * will use codec maximum bitrate setting.
     *
     * Default: 0 (follow codec maximum bitrate).
     */
    unsigned			    bandwidth;

} pjmedia_vid_stream_rc_config;


/** 
 * This structure describes video stream information. Each video stream
 * corresponds to one "m=" line in SDP session descriptor, and it has
 * its own RTP/RTCP socket pair.
 */
typedef struct pjmedia_vid_stream_info
{
    pjmedia_type	type;	    /**< Media type (audio, video)	    */
    pjmedia_tp_proto	proto;	    /**< Transport protocol (RTP/AVP, etc.) */
    pjmedia_dir		dir;	    /**< Media direction.		    */
    pj_sockaddr		rem_addr;   /**< Remote RTP address		    */
    pj_sockaddr		rem_rtcp;   /**< Optional remote RTCP address. If
					 sin_family is zero, the RTP address
					 will be calculated from RTP.	    */
    unsigned		tx_pt;	    /**< Outgoing codec paylaod type.	    */
    unsigned		rx_pt;	    /**< Incoming codec paylaod type.	    */
    pj_uint32_t		ssrc;	    /**< RTP SSRC.			    */
    pj_uint32_t		rtp_ts;	    /**< Initial RTP timestamp.		    */
    pj_uint16_t		rtp_seq;    /**< Initial RTP sequence number.	    */
    pj_uint8_t		rtp_seq_ts_set;
				    /**< Bitmask flags if initial RTP sequence 
				         and/or timestamp for sender are set.
					 bit 0/LSB : sequence flag 
					 bit 1     : timestamp flag 	    */
    int			jb_init;    /**< Jitter buffer init delay in msec.  
					 (-1 for default).		    */
    int			jb_min_pre; /**< Jitter buffer minimum prefetch
					 delay in msec (-1 for default).    */
    int			jb_max_pre; /**< Jitter buffer maximum prefetch
					 delay in msec (-1 for default).    */
    int			jb_max;	    /**< Jitter buffer max delay in msec.   */

#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA!=0
    pj_bool_t		use_ka;	    /**< Stream keep-alive and NAT hole punch
					 (see #PJMEDIA_STREAM_ENABLE_KA)
					 is enabled?			    */
#endif

    pjmedia_vid_codec_info   codec_info;  /**< Incoming codec format info.  */
    pjmedia_vid_codec_param *codec_param; /**< Optional codec param.	    */

    pj_bool_t           rtcp_sdes_bye_disabled; 
                                    /**< Disable automatic sending of RTCP
                                         SDES and BYE.                      */

    pjmedia_vid_stream_rc_config rc_cfg;
                                    /**< Stream send rate control settings. */
} pjmedia_vid_stream_info;


/**
 * This function will initialize the video stream info based on information
 * in both SDP session descriptors for the specified stream index. 
 * The remaining information will be taken from default codec parameters. 
 * If socket info array is specified, the socket will be copied to the 
 * session info as well.
 *
 * @param si		Stream info structure to be initialized.
 * @param pool		Pool to allocate memory.
 * @param endpt		PJMEDIA endpoint instance.
 * @param local		Local SDP session descriptor.
 * @param remote	Remote SDP session descriptor.
 * @param stream_idx	Media stream index in the session descriptor.
 *
 * @return		PJ_SUCCESS if stream info is successfully initialized.
 */
PJ_DECL(pj_status_t)
pjmedia_vid_stream_info_from_sdp(pjmedia_vid_stream_info *si,
			         pj_pool_t *pool,
				 pjmedia_endpt *endpt,
				 const pjmedia_sdp_session *local,
				 const pjmedia_sdp_session *remote,
				 unsigned stream_idx);


/**
 * Initialize the video stream rate control with default settings.
 *
 * @param cfg		Video stream rate control structure to be initialized.
 */
PJ_DECL(void)
pjmedia_vid_stream_rc_config_default(pjmedia_vid_stream_rc_config *cfg);


/*
 * Opaque declaration for video stream.
 */
typedef struct pjmedia_vid_stream pjmedia_vid_stream;


/**
 * Create a video stream based on the specified parameter. After the video
 * stream has been created, application normally would want to get the media
 * port interface of the stream, by calling pjmedia_vid_stream_get_port().
 * The media port interface exports put_frame() and get_frame() function,
 * used to transmit and receive media frames from the stream.
 *
 * Without application calling put_frame() and get_frame(), there will be 
 * no media frames transmitted or received by the stream.
 *
 * @param endpt		Media endpoint.
 * @param pool		Optional pool to allocate memory for the stream. If
 * 			this is not specified, one will be created internally.
 * 			A large number of memory may be needed because jitter
 *			buffer needs to preallocate some storage.
 * @param info		Stream information to create the stream. Upon return,
 * 			this info will be updated with the information from
 * 			the instantiated codec. Note that if the "pool"
 * 			argument is NULL, some fields in this "info" parameter
 * 			will be allocated from the internal pool of the
 * 			stream, which means that they will only remain valid
 * 			as long as the stream is not destroyed.
 * @param tp		Media transport instance used to transmit and receive
 * 			RTP/RTCP packets to/from the underlying network.
 * @param user_data	Arbitrary user data (for future callback feature).
 * @param p_stream	Pointer to receive the video stream.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_create(
					pjmedia_endpt *endpt,
					pj_pool_t *pool,
					pjmedia_vid_stream_info *info,
					pjmedia_transport *tp,
					void *user_data,
					pjmedia_vid_stream **p_stream);

/**
 * Destroy the video stream.
 *
 * @param stream	The video stream.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_destroy(pjmedia_vid_stream *stream);


/**
 * Get the media port interface of the stream. The media port interface
 * declares put_frame() and get_frame() function, which is the only 
 * way for application to transmit and receive media frames from the
 * stream. As bidirectional video streaming may have different video
 * formats in the encoding and decoding direction, there are two media
 * ports exported by the video stream, one for each direction.
 *
 * @param stream	The video stream.
 * @param dir		The video direction.
 * @param p_port	Pointer to receive the port interface.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_get_port(
					    pjmedia_vid_stream *stream,
					    pjmedia_dir dir,
					    pjmedia_port **p_port);


/**
 * Get the media transport object associated with this stream.
 *
 * @param st		The video stream.
 *
 * @return		The transport object being used by the stream.
 */
PJ_DECL(pjmedia_transport*) pjmedia_vid_stream_get_transport(
					    pjmedia_vid_stream *st);


/**
 * Get the stream statistics. See also #pjmedia_stream_get_stat_jbuf()
 *
 * @param stream	The video stream.
 * @param stat		Media stream statistics.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_get_stat(
					    const pjmedia_vid_stream *stream,
					    pjmedia_rtcp_stat *stat);

/**
 * Reset the video stream statistics.
 *
 * @param stream	The video stream.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_reset_stat(pjmedia_vid_stream *stream);


/**
 * Get current jitter buffer state. See also #pjmedia_stream_get_stat()
 *
 * @param stream	The video stream.
 * @param state		Jitter buffer state.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_get_stat_jbuf(
					    const pjmedia_vid_stream *stream,
					    pjmedia_jb_state *state);


/**
 * Get the stream info.
 *
 * @param stream	The video stream.
 * @param info		Video stream info.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_get_info(
					    const pjmedia_vid_stream *stream,
					    pjmedia_vid_stream_info *info);


/**
 * Start the video stream. This will start the appropriate channels
 * in the video stream, depending on the video direction that was set
 * when the stream was created.
 *
 * @param stream	The video stream.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_start(pjmedia_vid_stream *stream);


/**
 * Query if the stream is started on the specified direction.
 *
 * @param stream	The video stream.
 * @param dir 		The direction to be checked.
 *
 * @return		PJ_TRUE if stream is started.
 */
PJ_DECL(pj_bool_t) pjmedia_vid_stream_is_running(pjmedia_vid_stream *stream,
                                                 pjmedia_dir dir);

/**
 * Pause stream channels.
 *
 * @param stream	The video stream.
 * @param dir		Which channel direction to pause.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_pause(pjmedia_vid_stream *stream,
					      pjmedia_dir dir);

/**
 * Resume stream channels.
 *
 * @param stream	The video stream.
 * @param dir		Which channel direction to resume.
 *
 * @return		PJ_SUCCESS on success;
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_resume(pjmedia_vid_stream *stream,
					       pjmedia_dir dir);


/**
 * Force stream to send video keyframe on the next transmission.
 *
 * @param stream	The video stream.
 *
 * @return		PJ_SUCCESS on success;
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_send_keyframe(
						pjmedia_vid_stream *stream);


/**
 * Send RTCP SDES for the media stream.
 *
 * @param stream	The media stream.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_send_rtcp_sdes(
						pjmedia_vid_stream *stream);


/**
 * Send RTCP BYE for the media stream.
 *
 * @param stream	The media stream.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_stream_send_rtcp_bye(
						pjmedia_vid_stream *stream);


/**
 * @}
 */

PJ_END_DECL


#endif	/* __PJMEDIA_VID_STREAM_H__ */
