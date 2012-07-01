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
#include <pjmedia/vid_stream.h>
#include <pjmedia/errno.h>
#include <pjmedia/event.h>
#include <pjmedia/rtp.h>
#include <pjmedia/rtcp.h>
#include <pjmedia/jbuf.h>
#include <pjmedia/stream_common.h>
#include <pj/array.h>
#include <pj/assert.h>
#include <pj/compat/socket.h>
#include <pj/errno.h>
#include <pj/ioqueue.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/rand.h>
#include <pj/sock_select.h>
#include <pj/string.h>	    /* memcpy() */


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#define THIS_FILE			"vid_stream.c"
#define ERRLEVEL			1
#define LOGERR_(expr)			stream_perror expr
#define TRC_(expr)			PJ_LOG(5,expr)
#define SIGNATURE			PJMEDIA_SIG_PORT_VID_STREAM

#define TRACE_RC			0

/* Tracing jitter buffer operations in a stream session to a CSV file.
 * The trace will contain JB operation timestamp, frame info, RTP info, and
 * the JB state right after the operation.
 */
#define TRACE_JB			0	/* Enable/disable trace.    */
#define TRACE_JB_PATH_PREFIX		""	/* Optional path/prefix
						   for the CSV filename.    */
#if TRACE_JB
#   include <pj/file_io.h>
#   define TRACE_JB_INVALID_FD		((pj_oshandle_t)-1)
#   define TRACE_JB_OPENED(s)		(s->trace_jb_fd != TRACE_JB_INVALID_FD)
#endif

#ifndef PJMEDIA_VSTREAM_SIZE
#   define PJMEDIA_VSTREAM_SIZE	1000
#endif

#ifndef PJMEDIA_VSTREAM_INC
#   define PJMEDIA_VSTREAM_INC	1000
#endif

/* Video stream keep-alive feature is currently disabled. */
#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA != 0
#   undef PJMEDIA_STREAM_ENABLE_KA
#   define PJMEDIA_STREAM_ENABLE_KA 0
#endif


/**
 * Media channel.
 */
typedef struct pjmedia_vid_channel
{
    pjmedia_vid_stream	   *stream;	    /**< Parent stream.		    */
    pjmedia_dir		    dir;	    /**< Channel direction.	    */
    pjmedia_port	    port;	    /**< Port interface.	    */
    unsigned		    pt;		    /**< Payload type.		    */
    pj_bool_t		    paused;	    /**< Paused?.		    */
    void		   *buf;	    /**< Output buffer.		    */
    unsigned		    buf_size;	    /**< Size of output buffer.	    */
    pjmedia_rtp_session	    rtp;	    /**< RTP session.		    */
} pjmedia_vid_channel;


/**
 * This structure describes media stream.
 * A media stream is bidirectional media transmission between two endpoints.
 * It consists of two channels, i.e. encoding and decoding channels.
 * A media stream corresponds to a single "m=" line in a SDP session
 * description.
 */
struct pjmedia_vid_stream
{
    pj_pool_t		    *own_pool;      /**< Internal pool.		    */
    pjmedia_endpt	    *endpt;	    /**< Media endpoint.	    */
    pjmedia_vid_codec_mgr   *codec_mgr;	    /**< Codec manager.		    */
    pjmedia_vid_stream_info  info;	    /**< Stream info.		    */

    pjmedia_vid_channel	    *enc;	    /**< Encoding channel.	    */
    pjmedia_vid_channel	    *dec;	    /**< Decoding channel.	    */

    pjmedia_dir		     dir;	    /**< Stream direction.	    */
    void		    *user_data;	    /**< User data.		    */
    pj_str_t		     name;	    /**< Stream name		    */
    pj_str_t		     cname;	    /**< SDES CNAME		    */

    pjmedia_transport	    *transport;	    /**< Stream transport.	    */
    unsigned		     send_err_cnt;  /**< Send error count.          */

    pj_mutex_t		    *jb_mutex;
    pjmedia_jbuf	    *jb;	    /**< Jitter buffer.		    */
    char		     jb_last_frm;   /**< Last frame type from jb    */
    unsigned		     jb_last_frm_cnt;/**< Last JB frame type counter*/

    pjmedia_rtcp_session     rtcp;	    /**< RTCP for incoming RTP.	    */
    pj_uint32_t		     rtcp_last_tx;  /**< RTCP tx time in timestamp  */
    pj_uint32_t		     rtcp_interval; /**< Interval, in timestamp.    */
    pj_bool_t		     initial_rr;    /**< Initial RTCP RR sent	    */
    pj_bool_t                rtcp_sdes_bye_disabled;/**< Send RTCP SDES/BYE?*/
    void		    *out_rtcp_pkt;  /**< Outgoing RTCP packet.	    */
    unsigned		     out_rtcp_pkt_size;
					    /**< Outgoing RTCP packet size. */

    unsigned		     dec_max_size;  /**< Size of decoded/raw picture*/
    pjmedia_ratio	     dec_max_fps;   /**< Max fps of decoding dir.   */
    pjmedia_frame            dec_frame;	    /**< Current decoded frame.     */
    pjmedia_event            fmt_event;	    /**< Buffered fmt_changed event
                                                 to avoid deadlock	    */
    pjmedia_event            miss_keyframe_event; 
					    /**< Buffered missing keyframe
                                                 event for delayed republish*/

    unsigned		     frame_size;    /**< Size of encoded base frame.*/
    unsigned		     frame_ts_len;  /**< Frame length in timestamp. */

    unsigned		     rx_frame_cnt;  /**< # of array in rx_frames    */
    pjmedia_frame	    *rx_frames;	    /**< Temp. buffer for incoming
					         frame assembly.	    */

    pj_bool_t		     force_keyframe;/**< Forced to encode keyframe? */

#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA!=0
    pj_bool_t		     use_ka;	       /**< Stream keep-alive with non-
						    codec-VAD mechanism is
						    enabled?		    */
    pj_timestamp	     last_frm_ts_sent; /**< Timestamp of last sending
					            packet		    */
#endif

#if TRACE_JB
    pj_oshandle_t	     trace_jb_fd;   /**< Jitter tracing file handle.*/
    char		    *trace_jb_buf;  /**< Jitter tracing buffer.	    */
#endif

    pjmedia_vid_codec	    *codec;	    /**< Codec instance being used. */
    pj_uint32_t		     last_dec_ts;   /**< Last decoded timestamp.    */
    int			     last_dec_seq;  /**< Last decoded sequence.     */


    pj_timestamp	     ts_freq;	    /**< Timestamp frequency.	    */

#if TRACE_RC
    unsigned		     rc_total_sleep;
    unsigned		     rc_total_pkt;
    unsigned		     rc_total_img;
    pj_timestamp	     tx_start;
    pj_timestamp	     tx_end;
#endif
};

/* Prototypes */
static pj_status_t decode_frame(pjmedia_vid_stream *stream,
                                pjmedia_frame *frame);

/*
 * Print error.
 */
static void stream_perror(const char *sender, const char *title,
			  pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));
    PJ_LOG(4,(sender, "%s: %s [err:%d]", title, errmsg, status));
}


static pj_status_t send_rtcp(pjmedia_vid_stream *stream,
			     pj_bool_t with_sdes,
			     pj_bool_t with_bye);


#if TRACE_JB

PJ_INLINE(int) trace_jb_print_timestamp(char **buf, pj_ssize_t len)
{
    pj_time_val now;
    pj_parsed_time ptime;
    char *p = *buf;

    if (len < 14)
	return -1;

    pj_gettimeofday(&now);
    pj_time_decode(&now, &ptime);
    p += pj_utoa_pad(ptime.hour, p, 2, '0');
    *p++ = ':';
    p += pj_utoa_pad(ptime.min, p, 2, '0');
    *p++ = ':';
    p += pj_utoa_pad(ptime.sec, p, 2, '0');
    *p++ = '.';
    p += pj_utoa_pad(ptime.msec, p, 3, '0');
    *p++ = ',';

    *buf = p;

    return 0;
}

PJ_INLINE(int) trace_jb_print_state(pjmedia_vid_stream *stream, 
				    char **buf, pj_ssize_t len)
{
    char *p = *buf;
    char *endp = *buf + len;
    pjmedia_jb_state state;

    pjmedia_jbuf_get_state(stream->jb, &state);

    len = pj_ansi_snprintf(p, endp-p, "%d, %d, %d",
			   state.size, state.burst, state.prefetch);
    if ((len < 0) || (len >= endp-p))
	return -1;

    p += len;
    *buf = p;
    return 0;
}

static void trace_jb_get(pjmedia_vid_stream *stream, pjmedia_jb_frame_type ft,
			 pj_size_t fsize)
{
    char *p = stream->trace_jb_buf;
    char *endp = stream->trace_jb_buf + PJ_LOG_MAX_SIZE;
    pj_ssize_t len = 0;
    const char* ft_st;

    if (!TRACE_JB_OPENED(stream))
	return;

    /* Print timestamp. */
    if (trace_jb_print_timestamp(&p, endp-p))
	goto on_insuff_buffer;

    /* Print frame type and size */
    switch(ft) {
	case PJMEDIA_JB_MISSING_FRAME:
	    ft_st = "missing";
	    break;
	case PJMEDIA_JB_NORMAL_FRAME:
	    ft_st = "normal";
	    break;
	case PJMEDIA_JB_ZERO_PREFETCH_FRAME:
	    ft_st = "prefetch";
	    break;
	case PJMEDIA_JB_ZERO_EMPTY_FRAME:
	    ft_st = "empty";
	    break;
	default:
	    ft_st = "unknown";
	    break;
    }

    /* Print operation, size, frame count, frame type */
    len = pj_ansi_snprintf(p, endp-p, "GET,%d,1,%s,,,,", fsize, ft_st);
    if ((len < 0) || (len >= endp-p))
	goto on_insuff_buffer;
    p += len;

    /* Print JB state */
    if (trace_jb_print_state(stream, &p, endp-p))
	goto on_insuff_buffer;

    /* Print end of line */
    if (endp-p < 2)
	goto on_insuff_buffer;
    *p++ = '\n';

    /* Write and flush */
    len = p - stream->trace_jb_buf;
    pj_file_write(stream->trace_jb_fd, stream->trace_jb_buf, &len);
    pj_file_flush(stream->trace_jb_fd);
    return;

on_insuff_buffer:
    pj_assert(!"Trace buffer too small, check PJ_LOG_MAX_SIZE!");
}

static void trace_jb_put(pjmedia_vid_stream *stream,
			 const pjmedia_rtp_hdr *hdr,
			 unsigned payloadlen, unsigned frame_cnt)
{
    char *p = stream->trace_jb_buf;
    char *endp = stream->trace_jb_buf + PJ_LOG_MAX_SIZE;
    pj_ssize_t len = 0;

    if (!TRACE_JB_OPENED(stream))
	return;

    /* Print timestamp. */
    if (trace_jb_print_timestamp(&p, endp-p))
	goto on_insuff_buffer;

    /* Print operation, size, frame count, RTP info */
    len = pj_ansi_snprintf(p, endp-p,
			   "PUT,%d,%d,,%d,%d,%d,",
			   payloadlen, frame_cnt,
			   pj_ntohs(hdr->seq), pj_ntohl(hdr->ts), hdr->m);
    if ((len < 0) || (len >= endp-p))
	goto on_insuff_buffer;
    p += len;

    /* Print JB state */
    if (trace_jb_print_state(stream, &p, endp-p))
	goto on_insuff_buffer;

    /* Print end of line */
    if (endp-p < 2)
	goto on_insuff_buffer;
    *p++ = '\n';

    /* Write and flush */
    len = p - stream->trace_jb_buf;
    pj_file_write(stream->trace_jb_fd, stream->trace_jb_buf, &len);
    pj_file_flush(stream->trace_jb_fd);
    return;

on_insuff_buffer:
    pj_assert(!"Trace buffer too small, check PJ_LOG_MAX_SIZE!");
}

#endif /* TRACE_JB */

static void dump_port_info(const pjmedia_vid_channel *chan,
                           const char *event_name)
{
    const pjmedia_port_info *pi = &chan->port.info;
    char fourcc_name[5];

    PJ_LOG(5, (pi->name.ptr,
	       " %s format %s: %dx%d %s%s %d/%d(~%d)fps",
	       (chan->dir==PJMEDIA_DIR_DECODING? "Decoding":"Encoding"),
	       event_name,
	       pi->fmt.det.vid.size.w, pi->fmt.det.vid.size.h,
	       pjmedia_fourcc_name(pi->fmt.id, fourcc_name),
	       (chan->dir==PJMEDIA_DIR_ENCODING?"->":"<-"),
	       pi->fmt.det.vid.fps.num, pi->fmt.det.vid.fps.denum,
	       pi->fmt.det.vid.fps.num/pi->fmt.det.vid.fps.denum));
}

/*
 * Handle events from stream components.
 */
static pj_status_t stream_event_cb(pjmedia_event *event,
                                   void *user_data)
{
    pjmedia_vid_stream *stream = (pjmedia_vid_stream*)user_data;

    if (event->epub == stream->codec) {
	/* This is codec event */
	switch (event->type) {
	case PJMEDIA_EVENT_FMT_CHANGED:
	    /* Copy the event to avoid deadlock if we publish the event
	     * now. This happens because fmt_event may trigger restart
	     * while we're still holding the jb_mutex.
	     */
	    pj_memcpy(&stream->fmt_event, event, sizeof(*event));
	    return PJ_SUCCESS;

	case PJMEDIA_EVENT_KEYFRAME_MISSING:
	    /* Republish this event later from get_frame(). */
	    pj_memcpy(&stream->miss_keyframe_event, event, sizeof(*event));
	    return PJ_SUCCESS;

	default:
	    break;
	}
    }

    return pjmedia_event_publish(NULL, stream, event, 0);
}

#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA != 0
/*
 * Send keep-alive packet using non-codec frame.
 */
static void send_keep_alive_packet(pjmedia_vid_stream *stream)
{
#if PJMEDIA_STREAM_ENABLE_KA == PJMEDIA_STREAM_KA_EMPTY_RTP

    /* Keep-alive packet is empty RTP */
    pjmedia_vid_channel *channel = stream->enc;
    pj_status_t status;
    void *pkt;
    int pkt_len;

    TRC_((channel->port.info.name.ptr,
	  "Sending keep-alive (RTCP and empty RTP)"));

    /* Send RTP */
    status = pjmedia_rtp_encode_rtp( &stream->enc->rtp,
				     stream->enc->pt, 0,
				     1,
				     0,
				     (const void**)&pkt,
				     &pkt_len);
    pj_assert(status == PJ_SUCCESS);

    pj_memcpy(stream->enc->buf, pkt, pkt_len);
    pjmedia_transport_send_rtp(stream->transport, stream->enc->buf,
			       pkt_len);

    /* Send RTCP */
    send_rtcp(stream, PJ_TRUE, PJ_FALSE);

#elif PJMEDIA_STREAM_ENABLE_KA == PJMEDIA_STREAM_KA_USER

    /* Keep-alive packet is defined in PJMEDIA_STREAM_KA_USER_PKT */
    pjmedia_vid_channel *channel = stream->enc;
    int pkt_len;
    const pj_str_t str_ka = PJMEDIA_STREAM_KA_USER_PKT;

    TRC_((channel->port.info.name.ptr,
	  "Sending keep-alive (custom RTP/RTCP packets)"));

    /* Send to RTP port */
    pj_memcpy(stream->enc->buf, str_ka.ptr, str_ka.slen);
    pkt_len = str_ka.slen;
    pjmedia_transport_send_rtp(stream->transport, stream->enc->buf,
			       pkt_len);

    /* Send to RTCP port */
    pjmedia_transport_send_rtcp(stream->transport, stream->enc->buf,
			        pkt_len);

#else
    
    PJ_UNUSED_ARG(stream);

#endif
}
#endif	/* defined(PJMEDIA_STREAM_ENABLE_KA) */


static pj_status_t send_rtcp(pjmedia_vid_stream *stream,
			     pj_bool_t with_sdes,
			     pj_bool_t with_bye)
{
    void *sr_rr_pkt;
    pj_uint8_t *pkt;
    int len, max_len;
    pj_status_t status;

    /* Build RTCP RR/SR packet */
    pjmedia_rtcp_build_rtcp(&stream->rtcp, &sr_rr_pkt, &len);

    if (with_sdes || with_bye) {
	pkt = (pj_uint8_t*) stream->out_rtcp_pkt;
	pj_memcpy(pkt, sr_rr_pkt, len);
	max_len = stream->out_rtcp_pkt_size;
    } else {
	pkt = (pj_uint8_t*)sr_rr_pkt;
	max_len = len;
    }

    /* Build RTCP SDES packet */
    if (with_sdes) {
	pjmedia_rtcp_sdes sdes;
	pj_size_t sdes_len;

	pj_bzero(&sdes, sizeof(sdes));
	sdes.cname = stream->cname;
	sdes_len = max_len - len;
	status = pjmedia_rtcp_build_rtcp_sdes(&stream->rtcp, pkt+len,
					      &sdes_len, &sdes);
	if (status != PJ_SUCCESS) {
	    PJ_PERROR(4,(stream->name.ptr, status,
        			     "Error generating RTCP SDES"));
	} else {
	    len += sdes_len;
	}
    }

    /* Build RTCP BYE packet */
    if (with_bye) {
	pj_size_t bye_len;

	bye_len = max_len - len;
	status = pjmedia_rtcp_build_rtcp_bye(&stream->rtcp, pkt+len,
					     &bye_len, NULL);
	if (status != PJ_SUCCESS) {
	    PJ_PERROR(4,(stream->name.ptr, status,
        			     "Error generating RTCP BYE"));
	} else {
	    len += bye_len;
	}
    }

    /* Send! */
    status = pjmedia_transport_send_rtcp(stream->transport, pkt, len);

    return status;
}


/**
 * check_tx_rtcp()
 *
 * This function is can be called by either put_frame() or get_frame(),
 * to transmit periodic RTCP SR/RR report.
 */
static void check_tx_rtcp(pjmedia_vid_stream *stream, pj_uint32_t timestamp)
{
    /* Note that timestamp may represent local or remote timestamp, 
     * depending on whether this function is called from put_frame()
     * or get_frame().
     */


    if (stream->rtcp_last_tx == 0) {
	
	stream->rtcp_last_tx = timestamp;

    } else if (timestamp - stream->rtcp_last_tx >= stream->rtcp_interval) {
	pj_status_t status;
	
	status = send_rtcp(stream, !stream->rtcp_sdes_bye_disabled, PJ_FALSE);
	if (status != PJ_SUCCESS) {
	    PJ_PERROR(4,(stream->name.ptr, status,
        		 "Error sending RTCP"));
	}

	stream->rtcp_last_tx = timestamp;
    }
}


#if 0
static void dump_bin(const char *buf, unsigned len)
{
    unsigned i;

    PJ_LOG(3,(THIS_FILE, "begin dump"));
    for (i=0; i<len; ++i) {
	int j;
	char bits[9];
	unsigned val = buf[i] & 0xFF;

	bits[8] = '\0';
	for (j=0; j<8; ++j) {
	    if (val & (1 << (7-j)))
		bits[j] = '1';
	    else
		bits[j] = '0';
	}

	PJ_LOG(3,(THIS_FILE, "%2d %s [%d]", i, bits, val));
    }
    PJ_LOG(3,(THIS_FILE, "end dump"));
}
#endif


/*
 * This callback is called by stream transport on receipt of packets
 * in the RTP socket. 
 */
static void on_rx_rtp( void *data, 
		       void *pkt,
                       pj_ssize_t bytes_read)

{
    pjmedia_vid_stream *stream = (pjmedia_vid_stream*) data;
    pjmedia_vid_channel *channel = stream->dec;
    const pjmedia_rtp_hdr *hdr;
    const void *payload;
    unsigned payloadlen;
    pjmedia_rtp_status seq_st;
    pj_status_t status;
    pj_bool_t pkt_discarded = PJ_FALSE;

    /* Check for errors */
    if (bytes_read < 0) {
	LOGERR_((channel->port.info.name.ptr, "RTP recv() error", -bytes_read));
	return;
    }

    /* Ignore keep-alive packets */
    if (bytes_read < (pj_ssize_t) sizeof(pjmedia_rtp_hdr))
	return;

    /* Update RTP and RTCP session. */
    status = pjmedia_rtp_decode_rtp(&channel->rtp, pkt, bytes_read,
				    &hdr, &payload, &payloadlen);
    if (status != PJ_SUCCESS) {
	LOGERR_((channel->port.info.name.ptr, "RTP decode error", status));
	stream->rtcp.stat.rx.discard++;
	return;
    }

    /* Ignore the packet if decoder is paused */
    if (channel->paused)
	goto on_return;

    /* Update RTP session (also checks if RTP session can accept
     * the incoming packet.
     */
    pjmedia_rtp_session_update2(&channel->rtp, hdr, &seq_st, PJ_TRUE);
    if (seq_st.status.value) {
	TRC_  ((channel->port.info.name.ptr, 
		"RTP status: badpt=%d, badssrc=%d, dup=%d, "
		"outorder=%d, probation=%d, restart=%d", 
		seq_st.status.flag.badpt,
		seq_st.status.flag.badssrc,
		seq_st.status.flag.dup,
		seq_st.status.flag.outorder,
		seq_st.status.flag.probation,
		seq_st.status.flag.restart));

	if (seq_st.status.flag.badpt) {
	    PJ_LOG(4,(channel->port.info.name.ptr,
		      "Bad RTP pt %d (expecting %d)",
		      hdr->pt, channel->rtp.out_pt));
	}

	if (seq_st.status.flag.badssrc) {
	    PJ_LOG(4,(channel->port.info.name.ptr,
		      "Changed RTP peer SSRC %d (previously %d)",
		      channel->rtp.peer_ssrc, stream->rtcp.peer_ssrc));
	    stream->rtcp.peer_ssrc = channel->rtp.peer_ssrc;
	}


    }

    /* Skip bad RTP packet */
    if (seq_st.status.flag.bad) {
	pkt_discarded = PJ_TRUE;
	goto on_return;
    }

    /* Ignore if payloadlen is zero */
    if (payloadlen == 0) {
	pkt_discarded = PJ_TRUE;
	goto on_return;
    }

    pj_mutex_lock( stream->jb_mutex );

    /* Quickly see if there may be a full picture in the jitter buffer, and
     * decode them if so. More thorough check will be done in decode_frame().
     */
    if ((pj_ntohl(hdr->ts) != stream->dec_frame.timestamp.u32.lo) || hdr->m) {
	if (PJMEDIA_VID_STREAM_SKIP_PACKETS_TO_REDUCE_LATENCY) {
	    /* Always decode whenever we have picture in jb and
	     * overwrite already decoded picture if necessary
	     */
	    pj_size_t old_size = stream->dec_frame.size;

	    stream->dec_frame.size = stream->dec_max_size;
	    if (decode_frame(stream, &stream->dec_frame) != PJ_SUCCESS) {
		stream->dec_frame.size = old_size;
	    }
	} else {
	    /* Only decode if we don't already have decoded one,
	     * unless the jb is full.
	     */
	    pj_bool_t can_decode = PJ_FALSE;

	    if (pjmedia_jbuf_is_full(stream->jb)) {
		can_decode = PJ_TRUE;
	    }
	    else if (stream->dec_frame.size == 0) {
		can_decode = PJ_TRUE;
	    }

	    if (can_decode) {
		stream->dec_frame.size = stream->dec_max_size;
		if (decode_frame(stream, &stream->dec_frame) != PJ_SUCCESS) {
		    stream->dec_frame.size = 0;
		}
	    }
	}
    }

    /* Put "good" packet to jitter buffer, or reset the jitter buffer
     * when RTP session is restarted.
     */
    if (seq_st.status.flag.restart) {
	status = pjmedia_jbuf_reset(stream->jb);
	PJ_LOG(4,(channel->port.info.name.ptr, "Jitter buffer reset"));
    } else {
	/* Just put the payload into jitter buffer */
	pjmedia_jbuf_put_frame3(stream->jb, payload, payloadlen, 0, 
				pj_ntohs(hdr->seq), pj_ntohl(hdr->ts), NULL);

#if TRACE_JB
	trace_jb_put(stream, hdr, payloadlen, count);
#endif

    }
    pj_mutex_unlock( stream->jb_mutex );


    /* Check if now is the time to transmit RTCP SR/RR report.
     * We only do this when stream direction is "decoding only", 
     * because otherwise check_tx_rtcp() will be handled by put_frame()
     */
    if (stream->dir == PJMEDIA_DIR_DECODING) {
	check_tx_rtcp(stream, pj_ntohl(hdr->ts));
    }

    if (status != 0) {
	LOGERR_((channel->port.info.name.ptr, "Jitter buffer put() error", 
		status));
	pkt_discarded = PJ_TRUE;
	goto on_return;
    }

on_return:
    /* Update RTCP session */
    if (stream->rtcp.peer_ssrc == 0)
	stream->rtcp.peer_ssrc = channel->rtp.peer_ssrc;

    pjmedia_rtcp_rx_rtp2(&stream->rtcp, pj_ntohs(hdr->seq),
			 pj_ntohl(hdr->ts), payloadlen, pkt_discarded);

    /* Send RTCP RR and SDES after we receive some RTP packets */
    if (stream->rtcp.received >= 10 && !stream->initial_rr) {
	status = send_rtcp(stream, !stream->rtcp_sdes_bye_disabled,
			   PJ_FALSE);
        if (status != PJ_SUCCESS) {
            PJ_PERROR(4,(stream->name.ptr, status,
            	     "Error sending initial RTCP RR"));
	} else {
	    stream->initial_rr = PJ_TRUE;
	}
    }
}


/*
 * This callback is called by stream transport on receipt of packets
 * in the RTCP socket. 
 */
static void on_rx_rtcp( void *data,
                        void *pkt, 
                        pj_ssize_t bytes_read)
{
    pjmedia_vid_stream *stream = (pjmedia_vid_stream*) data;

    /* Check for errors */
    if (bytes_read < 0) {
	LOGERR_((stream->cname.ptr, "RTCP recv() error", 
		 -bytes_read));
	return;
    }

    pjmedia_rtcp_rx_rtcp(&stream->rtcp, pkt, bytes_read);
}

static pj_status_t put_frame(pjmedia_port *port,
                             pjmedia_frame *frame)
{
    pjmedia_vid_stream *stream = (pjmedia_vid_stream*) port->port_data.pdata;
    pjmedia_vid_channel *channel = stream->enc;
    pj_status_t status = 0;
    pjmedia_frame frame_out;
    unsigned rtp_ts_len;
    void *rtphdr;
    int rtphdrlen;
    pj_bool_t has_more_data = PJ_FALSE;
    pj_size_t total_sent = 0;
    pjmedia_vid_encode_opt enc_opt;
    unsigned pkt_cnt = 0;
    pj_timestamp initial_time;

#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA != 0
    /* If the interval since last sending packet is greater than
     * PJMEDIA_STREAM_KA_INTERVAL, send keep-alive packet.
     */
    if (stream->use_ka)
    {
	pj_uint32_t dtx_duration;

	dtx_duration = pj_timestamp_diff32(&stream->last_frm_ts_sent, 
					   &frame->timestamp);
        /* Video stream keep-alive feature is currently disabled. */
        /*
        if (dtx_duration >
	    PJMEDIA_STREAM_KA_INTERVAL *
            PJMEDIA_PIA_SRATE(&channel->port.info))
	{
	    send_keep_alive_packet(stream);
	    stream->last_frm_ts_sent = frame->timestamp;
	}
        */
    }
#endif

    /* Don't do anything if stream is paused */
    if (channel->paused) {
	return PJ_SUCCESS;
    }

    /* Get frame length in timestamp unit */
    rtp_ts_len = stream->frame_ts_len;

    /* Init frame_out buffer. */
    frame_out.buf = ((char*)channel->buf) + sizeof(pjmedia_rtp_hdr);
    frame_out.size = 0;

    /* Init encoding option */
    pj_bzero(&enc_opt, sizeof(enc_opt));
    if (stream->force_keyframe) {
	/* Force encoder to generate keyframe */
	enc_opt.force_keyframe = PJ_TRUE;
	stream->force_keyframe = PJ_FALSE;
	TRC_((channel->port.info.name.ptr,
	      "Forcing encoder to generate keyframe"));
    }

    /* Encode! */
    status = pjmedia_vid_codec_encode_begin(stream->codec, &enc_opt, frame,
                                            channel->buf_size -
                                               sizeof(pjmedia_rtp_hdr),
                                            &frame_out,
                                            &has_more_data);
    if (status != PJ_SUCCESS) {
	LOGERR_((channel->port.info.name.ptr,
		"Codec encode_begin() error", status));

	/* Update RTP timestamp */
	pjmedia_rtp_encode_rtp(&channel->rtp, channel->pt, 1, 0,
			       rtp_ts_len,  (const void**)&rtphdr,
			       &rtphdrlen);
	return status;
    }
    
    pj_get_timestamp(&initial_time);

    /* Loop while we have frame to send */
    for (;;) {
	status = pjmedia_rtp_encode_rtp(&channel->rtp,
	                                channel->pt,
	                                (has_more_data == PJ_FALSE ? 1 : 0),
	                                frame_out.size,
	                                rtp_ts_len,
	                                (const void**)&rtphdr,
	                                &rtphdrlen);
	if (status != PJ_SUCCESS) {
	    LOGERR_((channel->port.info.name.ptr,
		    "RTP encode_rtp() error", status));
	    return status;
	}

	// Copy RTP header to the beginning of packet
	pj_memcpy(channel->buf, rtphdr, sizeof(pjmedia_rtp_hdr));

	// Send the RTP packet to the transport.
	status = pjmedia_transport_send_rtp(stream->transport,
	                                    (char*)channel->buf,
	                                    frame_out.size +
						sizeof(pjmedia_rtp_hdr));
	if (status != PJ_SUCCESS) {
	    enum { COUNT_TO_REPORT = 20 };
	    if (stream->send_err_cnt++ == 0) {
		LOGERR_((channel->port.info.name.ptr,
			 "Transport send_rtp() error",
			 status));
	    }
	    if (stream->send_err_cnt > COUNT_TO_REPORT)
		stream->send_err_cnt = 0;
	    /* Ignore this error */
	}

	pjmedia_rtcp_tx_rtp(&stream->rtcp, frame_out.size);
	total_sent += frame_out.size;
	pkt_cnt++;

	if (!has_more_data)
	    break;

	/* Next packets use same timestamp */
	rtp_ts_len = 0;

	frame_out.size = 0;

	/* Encode more! */
	status = pjmedia_vid_codec_encode_more(stream->codec,
	                                       channel->buf_size -
						   sizeof(pjmedia_rtp_hdr),
				               &frame_out,
					       &has_more_data);
	if (status != PJ_SUCCESS) {
	    LOGERR_((channel->port.info.name.ptr,
		     "Codec encode_more() error", status));
	    /* Ignore this error (?) */
	    break;
	}

	/* Send rate control */
	if (stream->info.rc_cfg.method==PJMEDIA_VID_STREAM_RC_SIMPLE_BLOCKING)
	{
	    pj_timestamp now, next_send_ts, total_send_ts;

	    total_send_ts.u64 = total_sent * stream->ts_freq.u64 * 8 /
				stream->info.rc_cfg.bandwidth;
	    next_send_ts = initial_time;
	    pj_add_timestamp(&next_send_ts, &total_send_ts);

	    pj_get_timestamp(&now);
	    if (pj_cmp_timestamp(&now, &next_send_ts) < 0) {
		unsigned ms_sleep;
		ms_sleep = pj_elapsed_msec(&now, &next_send_ts);

		if (ms_sleep > 10)
		    ms_sleep = 10;

		pj_thread_sleep(ms_sleep);
	    }
	}
    }

#if TRACE_RC
    /* Trace log for rate control */
    {
	pj_timestamp end_time;
	unsigned total_sleep;

	pj_get_timestamp(&end_time);
	total_sleep = pj_elapsed_msec(&initial_time, &end_time);
	PJ_LOG(5, (stream->name.ptr, "total pkt=%d size=%d sleep=%d",
		   pkt_cnt, total_sent, total_sleep));

	if (stream->tx_start.u64 == 0)
	    stream->tx_start = initial_time;
	stream->tx_end = end_time;
	stream->rc_total_pkt += pkt_cnt;
	stream->rc_total_sleep += total_sleep;
	stream->rc_total_img++;
    }
#endif

    /* Check if now is the time to transmit RTCP SR/RR report. 
     * We only do this when stream direction is not "decoding only", because
     * when it is, check_tx_rtcp() will be handled by get_frame().
     */
    if (stream->dir != PJMEDIA_DIR_DECODING) {
	check_tx_rtcp(stream, pj_ntohl(channel->rtp.out_hdr.ts));
    }

    /* Do nothing if we have nothing to transmit */
    if (total_sent == 0) {
	return PJ_SUCCESS;
    }

    /* Update stat */
    stream->rtcp.stat.rtp_tx_last_ts = pj_ntohl(stream->enc->rtp.out_hdr.ts);
    stream->rtcp.stat.rtp_tx_last_seq = pj_ntohs(stream->enc->rtp.out_hdr.seq);

#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA!=0
    /* Update timestamp of last sending packet. */
    stream->last_frm_ts_sent = frame->timestamp;
#endif

    return PJ_SUCCESS;
}

/* Decode one image from jitter buffer */
static pj_status_t decode_frame(pjmedia_vid_stream *stream,
                                pjmedia_frame *frame)
{
    pjmedia_vid_channel *channel = stream->dec;
    pj_uint32_t last_ts = 0;
    int frm_first_seq = 0, frm_last_seq = 0;
    pj_bool_t got_frame = PJ_FALSE;
    unsigned cnt;
    pj_status_t status;

    /* Repeat get payload from the jitter buffer until all payloads with same
     * timestamp are collected.
     */

    /* Check if we got a decodable frame */
    for (cnt=0; ; ++cnt) {
	char ptype;
	pj_uint32_t ts;
	int seq;

	/* Peek frame from jitter buffer. */
	pjmedia_jbuf_peek_frame(stream->jb, cnt, NULL, NULL,
				&ptype, NULL, &ts, &seq);
	if (ptype == PJMEDIA_JB_NORMAL_FRAME) {
	    if (last_ts == 0) {
		last_ts = ts;
		frm_first_seq = seq;
	    }
	    if (ts != last_ts) {
		got_frame = PJ_TRUE;
		break;
	    }
	    frm_last_seq = seq;
	} else if (ptype == PJMEDIA_JB_ZERO_EMPTY_FRAME) {
	    /* No more packet in the jitter buffer */
	    break;
	}
    }

    if (got_frame) {
	unsigned i;

	/* Generate frame bitstream from the payload */
	if (cnt > stream->rx_frame_cnt) {
	    PJ_LOG(1,(channel->port.info.name.ptr,
		      "Discarding %u frames because array is full!",
		      cnt - stream->rx_frame_cnt));
	    pjmedia_jbuf_remove_frame(stream->jb, cnt - stream->rx_frame_cnt);
	    cnt = stream->rx_frame_cnt;
	}

	for (i = 0; i < cnt; ++i) {
	    char ptype;

	    stream->rx_frames[i].type = PJMEDIA_FRAME_TYPE_VIDEO;
	    stream->rx_frames[i].timestamp.u64 = last_ts;
	    stream->rx_frames[i].bit_info = 0;

	    /* We use jbuf_peek_frame() as it will returns the pointer of
	     * the payload (no buffer and memcpy needed), just as we need.
	     */
	    pjmedia_jbuf_peek_frame(stream->jb, i,
				    (const void**)&stream->rx_frames[i].buf,
				    &stream->rx_frames[i].size, &ptype,
				    NULL, NULL, NULL);

	    if (ptype != PJMEDIA_JB_NORMAL_FRAME) {
		/* Packet lost, must set payload to NULL and keep going */
		stream->rx_frames[i].buf = NULL;
		stream->rx_frames[i].size = 0;
		stream->rx_frames[i].type = PJMEDIA_FRAME_TYPE_NONE;
		continue;
	    }
	}

	/* Decode */
	status = pjmedia_vid_codec_decode(stream->codec, cnt,
	                                  stream->rx_frames,
	                                  frame->size, frame);
	if (status != PJ_SUCCESS) {
	    LOGERR_((channel->port.info.name.ptr, "codec decode() error",
		     status));
	    frame->type = PJMEDIA_FRAME_TYPE_NONE;
	    frame->size = 0;
	}

	pjmedia_jbuf_remove_frame(stream->jb, cnt);
    }

    /* Learn remote frame rate after successful decoding */
    if (frame->type == PJMEDIA_FRAME_TYPE_VIDEO && frame->size)
    {
	/* Only check remote frame rate when timestamp is not wrapping and
	 * sequence is increased by 1.
	 */
	if (last_ts > stream->last_dec_ts &&
	    frm_first_seq - stream->last_dec_seq == 1)
	{
	    pj_uint32_t ts_diff;
	    pjmedia_video_format_detail *vfd;

	    ts_diff = last_ts - stream->last_dec_ts;
	    vfd = pjmedia_format_get_video_format_detail(
				    &channel->port.info.fmt, PJ_TRUE);
	    if (stream->info.codec_info.clock_rate * vfd->fps.denum !=
		vfd->fps.num * ts_diff)
	    {
		/* Frame rate changed, update decoding port info */
		if (stream->info.codec_info.clock_rate % ts_diff == 0) {
		    vfd->fps.num = stream->info.codec_info.clock_rate/ts_diff;
		    vfd->fps.denum = 1;
		} else {
		    vfd->fps.num = stream->info.codec_info.clock_rate;
		    vfd->fps.denum = ts_diff;
		}

		/* Update stream info */
		stream->info.codec_param->dec_fmt.det.vid.fps = vfd->fps;

		/* Publish PJMEDIA_EVENT_FMT_CHANGED event if frame rate
		 * increased and not exceeding 100fps.
		 */
		if (vfd->fps.num/vfd->fps.denum <= 100 &&
		    vfd->fps.num * stream->dec_max_fps.denum >
		    stream->dec_max_fps.num * vfd->fps.denum)
		{
		    pjmedia_event *event = &stream->fmt_event;

		    /* Update max fps of decoding dir */
		    stream->dec_max_fps = vfd->fps;

		    /* Use the buffered format changed event:
		     * - just update the framerate if there is pending event,
		     * - otherwise, init the whole event.
		     */
		    if (stream->fmt_event.type != PJMEDIA_EVENT_NONE) {
			event->data.fmt_changed.new_fmt.det.vid.fps = vfd->fps;
		    } else {
			pjmedia_event_init(event, PJMEDIA_EVENT_FMT_CHANGED,
					   &frame->timestamp, stream);
			event->data.fmt_changed.dir = PJMEDIA_DIR_DECODING;
			pj_memcpy(&event->data.fmt_changed.new_fmt,
				  &stream->info.codec_param->dec_fmt,
				  sizeof(pjmedia_format));
		    }
		}
	    }
	}

	/* Update last frame seq and timestamp */
	stream->last_dec_seq = frm_last_seq;
	stream->last_dec_ts = last_ts;
    }

    return got_frame ? PJ_SUCCESS : PJ_ENOTFOUND;
}


static pj_status_t get_frame(pjmedia_port *port,
                             pjmedia_frame *frame)
{
    pjmedia_vid_stream *stream = (pjmedia_vid_stream*) port->port_data.pdata;
    pjmedia_vid_channel *channel = stream->dec;

    /* Return no frame is channel is paused */
    if (channel->paused) {
	frame->type = PJMEDIA_FRAME_TYPE_NONE;
	frame->size = 0;
	return PJ_SUCCESS;
    }

    /* Report pending events. Do not publish the event while holding the
     * jb_mutex as that would lead to deadlock. It should be safe to
     * operate on fmt_event without the mutex because format change normally
     * would only occur once during the start of the media.
     */
    if (stream->fmt_event.type != PJMEDIA_EVENT_NONE) {
	pjmedia_event_fmt_changed_data *fmt_chg_data;

	fmt_chg_data = &stream->fmt_event.data.fmt_changed;

	/* Update stream info and decoding channel port info */
	if (fmt_chg_data->dir == PJMEDIA_DIR_DECODING) {
	    pjmedia_format_copy(&stream->info.codec_param->dec_fmt,
				&fmt_chg_data->new_fmt);
	    pjmedia_format_copy(&stream->dec->port.info.fmt,
				&fmt_chg_data->new_fmt);

	    /* Override the framerate to be 1.5x higher in the event
	     * for the renderer.
	     */
	    fmt_chg_data->new_fmt.det.vid.fps.num *= 3;
	    fmt_chg_data->new_fmt.det.vid.fps.num /= 2;
	} else {
	    pjmedia_format_copy(&stream->info.codec_param->enc_fmt,
				&fmt_chg_data->new_fmt);
	    pjmedia_format_copy(&stream->enc->port.info.fmt,
				&fmt_chg_data->new_fmt);
	}

	dump_port_info(fmt_chg_data->dir==PJMEDIA_DIR_DECODING ?
			stream->dec : stream->enc,
		       "changed");

	pjmedia_event_publish(NULL, port, &stream->fmt_event, 0);

	stream->fmt_event.type = PJMEDIA_EVENT_NONE;
    }

    if (stream->miss_keyframe_event.type != PJMEDIA_EVENT_NONE) {
	pjmedia_event_publish(NULL, port, &stream->miss_keyframe_event,
			      PJMEDIA_EVENT_PUBLISH_POST_EVENT);
	stream->miss_keyframe_event.type = PJMEDIA_EVENT_NONE;
    }

    pj_mutex_lock( stream->jb_mutex );

    if (stream->dec_frame.size == 0) {
	/* Don't have frame in buffer, try to decode one */
	if (decode_frame(stream, frame) != PJ_SUCCESS) {
	    frame->type = PJMEDIA_FRAME_TYPE_NONE;
	    frame->size = 0;
	}
    } else {
	if (frame->size < stream->dec_frame.size) {
	    PJ_LOG(4,(stream->dec->port.info.name.ptr,
		      "Error: not enough buffer for decoded frame "
		      "(supplied=%d, required=%d)",
		      (int)frame->size, (int)stream->dec_frame.size));
	    frame->type = PJMEDIA_FRAME_TYPE_NONE;
	    frame->size = 0;
	} else {
	    frame->type = stream->dec_frame.type;
	    frame->timestamp = stream->dec_frame.timestamp;
	    frame->size = stream->dec_frame.size;
	    pj_memcpy(frame->buf, stream->dec_frame.buf, frame->size);
	}

	stream->dec_frame.size = 0;
    }

    pj_mutex_unlock( stream->jb_mutex );

    return PJ_SUCCESS;
}

/*
 * Create media channel.
 */
static pj_status_t create_channel( pj_pool_t *pool,
				   pjmedia_vid_stream *stream,
				   pjmedia_dir dir,
				   unsigned pt,
				   const pjmedia_vid_stream_info *info,
				   pjmedia_vid_channel **p_channel)
{
    enum { M = 32 };
    pjmedia_vid_channel *channel;
    pj_status_t status;
    unsigned min_out_pkt_size;
    pj_str_t name;
    const char *type_name;
    pjmedia_format *fmt;
    char fourcc_name[5];
    pjmedia_port_info *pi;
    
    pj_assert(info->type == PJMEDIA_TYPE_VIDEO);
    pj_assert(dir == PJMEDIA_DIR_DECODING || dir == PJMEDIA_DIR_ENCODING);

    /* Allocate memory for channel descriptor */
    channel = PJ_POOL_ZALLOC_T(pool, pjmedia_vid_channel);
    PJ_ASSERT_RETURN(channel != NULL, PJ_ENOMEM);

    /* Init vars */
    if (dir==PJMEDIA_DIR_DECODING) {
	type_name = "vstdec";
	fmt = &info->codec_param->dec_fmt;
    } else {
	type_name = "vstenc";
	fmt = &info->codec_param->enc_fmt;
    }
    name.ptr = (char*) pj_pool_alloc(pool, M);
    name.slen = pj_ansi_snprintf(name.ptr, M, "%s%p", type_name, stream);
    pi = &channel->port.info;

    /* Init channel info. */
    channel->stream = stream;
    channel->dir = dir;
    channel->paused = 1;
    channel->pt = pt;
    
    /* Allocate buffer for outgoing packet. */
    if (dir == PJMEDIA_DIR_ENCODING) {
	channel->buf_size = sizeof(pjmedia_rtp_hdr) + stream->frame_size;

	/* It should big enough to hold (minimally) RTCP SR with an SDES. */
	min_out_pkt_size =  sizeof(pjmedia_rtcp_sr_pkt) +
			    sizeof(pjmedia_rtcp_common) +
			    (4 + stream->cname.slen) +
			    32;

	if (channel->buf_size < min_out_pkt_size)
	    channel->buf_size = min_out_pkt_size;

	channel->buf = pj_pool_alloc(pool, channel->buf_size);
	PJ_ASSERT_RETURN(channel->buf != NULL, PJ_ENOMEM);
    }

    /* Create RTP and RTCP sessions: */
    if (info->rtp_seq_ts_set == 0) {
	status = pjmedia_rtp_session_init(&channel->rtp, pt, info->ssrc);
    } else {
	pjmedia_rtp_session_setting settings;

	settings.flags = (pj_uint8_t)((info->rtp_seq_ts_set << 2) | 3);
	settings.default_pt = pt;
	settings.sender_ssrc = info->ssrc;
	settings.seq = info->rtp_seq;
	settings.ts = info->rtp_ts;
	status = pjmedia_rtp_session_init2(&channel->rtp, settings);
    }
    if (status != PJ_SUCCESS)
	return status;

    /* Init port. */
    pjmedia_port_info_init2(pi, &name, SIGNATURE, dir, fmt);
    if (dir == PJMEDIA_DIR_DECODING) {
	channel->port.get_frame = &get_frame;
    } else {
	pi->fmt.id = info->codec_param->dec_fmt.id;
	channel->port.put_frame = &put_frame;
    }

    /* Init port. */
    channel->port.port_data.pdata = stream;

    PJ_LOG(5, (name.ptr,
	       "%s channel created %dx%d %s%s%.*s %d/%d(~%d)fps",
	       (dir==PJMEDIA_DIR_ENCODING?"Encoding":"Decoding"),
	       pi->fmt.det.vid.size.w, pi->fmt.det.vid.size.h,
	       pjmedia_fourcc_name(pi->fmt.id, fourcc_name),
	       (dir==PJMEDIA_DIR_ENCODING?"->":"<-"),
	       info->codec_info.encoding_name.slen,
	       info->codec_info.encoding_name.ptr,
	       pi->fmt.det.vid.fps.num, pi->fmt.det.vid.fps.denum,
	       pi->fmt.det.vid.fps.num/pi->fmt.det.vid.fps.denum));

    /* Done. */
    *p_channel = channel;
    return PJ_SUCCESS;
}


/*
 * Create stream.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_create(
					pjmedia_endpt *endpt,
					pj_pool_t *pool,
					pjmedia_vid_stream_info *info,
					pjmedia_transport *tp,
					void *user_data,
					pjmedia_vid_stream **p_stream)
{
    enum { M = 32 };
    pj_pool_t *own_pool = NULL;
    pjmedia_vid_stream *stream;
    unsigned jb_init, jb_max, jb_min_pre, jb_max_pre;
    int frm_ptime, chunks_per_frm;
    pjmedia_video_format_detail *vfd_enc, *vfd_dec;
    char *p;
    unsigned dec_mtu;
    pj_status_t status;

    if (!pool) {
	own_pool = pjmedia_endpt_create_pool( endpt, "vstrm%p",
	                                      PJMEDIA_VSTREAM_SIZE,
	                                      PJMEDIA_VSTREAM_INC);
	PJ_ASSERT_RETURN(own_pool != NULL, PJ_ENOMEM);
	pool = own_pool;
    }

    /* Allocate stream */
    stream = PJ_POOL_ZALLOC_T(pool, pjmedia_vid_stream);
    PJ_ASSERT_RETURN(stream != NULL, PJ_ENOMEM);
    stream->own_pool = own_pool;

    /* Get codec manager */
    stream->codec_mgr = pjmedia_vid_codec_mgr_instance();
    PJ_ASSERT_RETURN(stream->codec_mgr, PJMEDIA_CODEC_EFAILED);

    /* Init stream/port name */
    stream->name.ptr = (char*) pj_pool_alloc(pool, M);
    stream->name.slen = pj_ansi_snprintf(stream->name.ptr, M, 
					 "vstrm%p", stream);

    /* Create and initialize codec: */
    status = pjmedia_vid_codec_mgr_alloc_codec(stream->codec_mgr, 
					       &info->codec_info,
					       &stream->codec);
    if (status != PJ_SUCCESS)
	return status;

    /* Get codec param: */
    if (!info->codec_param) {
	pjmedia_vid_codec_param def_param;

	status = pjmedia_vid_codec_mgr_get_default_param(stream->codec_mgr, 
						         &info->codec_info,
						         &def_param);
	if (status != PJ_SUCCESS)
	    return status;

	info->codec_param = pjmedia_vid_codec_param_clone(pool, &def_param);
	pj_assert(info->codec_param);
    }

    /* Init codec param and adjust MTU */
    info->codec_param->dir = info->dir;
    info->codec_param->enc_mtu -= (sizeof(pjmedia_rtp_hdr) +
				   PJMEDIA_STREAM_RESV_PAYLOAD_LEN);
    if (info->codec_param->enc_mtu > PJMEDIA_MAX_MTU)
	info->codec_param->enc_mtu = PJMEDIA_MAX_MTU;

    /* MTU estimation for decoding direction */
    dec_mtu = PJMEDIA_MAX_MTU;

    vfd_enc = pjmedia_format_get_video_format_detail(
					&info->codec_param->enc_fmt, PJ_TRUE);
    vfd_dec = pjmedia_format_get_video_format_detail(
					&info->codec_param->dec_fmt, PJ_TRUE);

    /* Init stream: */
    stream->endpt = endpt;
    stream->dir = info->dir;
    stream->user_data = user_data;
    stream->rtcp_interval = (PJMEDIA_RTCP_INTERVAL-500 + (pj_rand()%1000)) *
			    info->codec_info.clock_rate / 1000;
    stream->rtcp_sdes_bye_disabled = info->rtcp_sdes_bye_disabled;

    stream->jb_last_frm = PJMEDIA_JB_NORMAL_FRAME;

#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA!=0
    stream->use_ka = info->use_ka;
#endif

    /* Build random RTCP CNAME. CNAME has user@host format */
    stream->cname.ptr = p = (char*) pj_pool_alloc(pool, 20);
    pj_create_random_string(p, 5);
    p += 5;
    *p++ = '@'; *p++ = 'p'; *p++ = 'j';
    pj_create_random_string(p, 6);
    p += 6;
    *p++ = '.'; *p++ = 'o'; *p++ = 'r'; *p++ = 'g';
    stream->cname.slen = p - stream->cname.ptr;


    /* Create mutex to protect jitter buffer: */

    status = pj_mutex_create_simple(pool, NULL, &stream->jb_mutex);
    if (status != PJ_SUCCESS)
	return status;

    /* Init and open the codec. */
    status = pjmedia_vid_codec_init(stream->codec, pool);
    if (status != PJ_SUCCESS)
	return status;
    status = pjmedia_vid_codec_open(stream->codec, info->codec_param);
    if (status != PJ_SUCCESS)
	return status;

    /* Subscribe to codec events */
    pjmedia_event_subscribe(NULL, &stream_event_cb, stream,
                            stream->codec);

    /* Estimate the maximum frame size */
    stream->frame_size = vfd_enc->size.w * vfd_enc->size.h * 4;

#if 0
    stream->frame_size = vfd_enc->max_bps/8 * vfd_enc->fps.denum /
			 vfd_enc->fps.num;
    
    /* As the maximum frame_size is not represented directly by maximum bps
     * (which includes intra and predicted frames), let's increase the
     * frame size value for safety.
     */
    stream->frame_size <<= 4;
#endif

    /* Validate the frame size */
    if (stream->frame_size == 0 || 
	stream->frame_size > PJMEDIA_MAX_VIDEO_ENC_FRAME_SIZE)
    {
	stream->frame_size = PJMEDIA_MAX_VIDEO_ENC_FRAME_SIZE;
    }

    /* Get frame length in timestamp unit */
    stream->frame_ts_len = info->codec_info.clock_rate *
                           vfd_enc->fps.denum / vfd_enc->fps.num;

    /* Initialize send rate states */
    pj_get_timestamp_freq(&stream->ts_freq);
    if (info->rc_cfg.bandwidth == 0)
	info->rc_cfg.bandwidth = vfd_enc->max_bps;

    /* For simple blocking, need to have bandwidth large enough, otherwise
     * we can slow down the transmission too much
     */
    if (info->rc_cfg.method==PJMEDIA_VID_STREAM_RC_SIMPLE_BLOCKING &&
	info->rc_cfg.bandwidth < vfd_enc->avg_bps * 3)
    {
	info->rc_cfg.bandwidth = vfd_enc->avg_bps * 3;
    }

    /* Override the initial framerate in the decoding direction. This initial
     * value will be used by the renderer to configure its clock, and setting
     * it to a bit higher value can avoid the possibility of high latency
     * caused by clock drift (remote encoder clock runs slightly faster than
     * local renderer clock) or video setup lag. Note that the actual framerate
     * will be continuously calculated based on the incoming RTP timestamps.
     */
    vfd_dec->fps.num = vfd_dec->fps.num * 3 / 2;
    stream->dec_max_fps = vfd_dec->fps;

    /* Create decoder channel */
    status = create_channel( pool, stream, PJMEDIA_DIR_DECODING, 
			     info->rx_pt, info, &stream->dec);
    if (status != PJ_SUCCESS)
	return status;

    /* Create encoder channel */
    status = create_channel( pool, stream, PJMEDIA_DIR_ENCODING, 
			     info->tx_pt, info, &stream->enc);
    if (status != PJ_SUCCESS)
	return status;

    /* Create temporary buffer for immediate decoding */
    stream->dec_max_size = vfd_dec->size.w * vfd_dec->size.h * 4;
    stream->dec_frame.buf = pj_pool_alloc(pool, stream->dec_max_size);

    /* Init jitter buffer parameters: */
    frm_ptime	    = 1000 * vfd_enc->fps.denum / vfd_enc->fps.num;
    chunks_per_frm  = stream->frame_size / dec_mtu;
    if (chunks_per_frm == 0) chunks_per_frm = 1;

    /* JB max count, default 500ms */
    if (info->jb_max >= frm_ptime)
	jb_max	    = info->jb_max * chunks_per_frm / frm_ptime;
    else
	jb_max	    = 500 * chunks_per_frm / frm_ptime;

    /* JB min prefetch, default 1 frame */
    if (info->jb_min_pre >= frm_ptime)
	jb_min_pre  = info->jb_min_pre * chunks_per_frm / frm_ptime;
    else
	jb_min_pre  = 1;

    /* JB max prefetch, default 4/5 JB max count */
    if (info->jb_max_pre >= frm_ptime)
	jb_max_pre  = info->jb_max_pre * chunks_per_frm / frm_ptime;
    else
	jb_max_pre  = jb_max * 4 / 5;

    /* JB init prefetch, default 0 */
    if (info->jb_init >= frm_ptime)
	jb_init  = info->jb_init * chunks_per_frm / frm_ptime;
    else
	jb_init  = 0;

    /* Allocate array for temporary storage for assembly of incoming
     * frames. Add more just in case.
     */
    stream->rx_frame_cnt = chunks_per_frm * 2;
    stream->rx_frames = pj_pool_calloc(pool, stream->rx_frame_cnt,
                                       sizeof(stream->rx_frames[0]));

    /* Create jitter buffer */
    status = pjmedia_jbuf_create(pool, &stream->dec->port.info.name,
                                 dec_mtu + PJMEDIA_STREAM_RESV_PAYLOAD_LEN,
				 1000 * vfd_enc->fps.denum / vfd_enc->fps.num,
				 jb_max, &stream->jb);
    if (status != PJ_SUCCESS)
	return status;


    /* Set up jitter buffer */
    pjmedia_jbuf_set_adaptive(stream->jb, jb_init, jb_min_pre, jb_max_pre);
    pjmedia_jbuf_set_discard(stream->jb, PJMEDIA_JB_DISCARD_NONE);

    /* Init RTCP session: */
    {
	pjmedia_rtcp_session_setting rtcp_setting;

	pjmedia_rtcp_session_setting_default(&rtcp_setting);
	rtcp_setting.name = stream->name.ptr;
	rtcp_setting.ssrc = info->ssrc;
	rtcp_setting.rtp_ts_base = pj_ntohl(stream->enc->rtp.out_hdr.ts);
	rtcp_setting.clock_rate = info->codec_info.clock_rate;
	rtcp_setting.samples_per_frame = 1;

	pjmedia_rtcp_init2(&stream->rtcp, &rtcp_setting);
    }

    /* Allocate outgoing RTCP buffer, should be enough to hold SR/RR, SDES,
     * BYE, and XR.
     */
    stream->out_rtcp_pkt_size =  sizeof(pjmedia_rtcp_sr_pkt) +
				 sizeof(pjmedia_rtcp_common) +
				 (4 + stream->cname.slen) +
				 32;
    if (stream->out_rtcp_pkt_size > PJMEDIA_MAX_MTU)
	stream->out_rtcp_pkt_size = PJMEDIA_MAX_MTU;

    stream->out_rtcp_pkt = pj_pool_alloc(pool, stream->out_rtcp_pkt_size);

    /* Only attach transport when stream is ready. */
    status = pjmedia_transport_attach(tp, stream, &info->rem_addr, 
				      &info->rem_rtcp, 
				      pj_sockaddr_get_len(&info->rem_addr), 
                                      &on_rx_rtp, &on_rx_rtcp);
    if (status != PJ_SUCCESS)
	return status;

    stream->transport = tp;

    /* Send RTCP SDES */
    if (!stream->rtcp_sdes_bye_disabled) {
        pjmedia_vid_stream_send_rtcp_sdes(stream);
    }

#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA!=0
    /* NAT hole punching by sending KA packet via RTP transport. */
    if (stream->use_ka)
	send_keep_alive_packet(stream);
#endif

#if TRACE_JB
    {
	char trace_name[PJ_MAXPATH];
	pj_ssize_t len;

	pj_ansi_snprintf(trace_name, sizeof(trace_name), 
			 TRACE_JB_PATH_PREFIX "%s.csv",
			 channel->port.info.name.ptr);
	status = pj_file_open(pool, trace_name, PJ_O_RDWR,
			      &stream->trace_jb_fd);
	if (status != PJ_SUCCESS) {
	    stream->trace_jb_fd = TRACE_JB_INVALID_FD;
	    PJ_LOG(3,(THIS_FILE, "Failed creating RTP trace file '%s'", 
		      trace_name));
	} else {
	    stream->trace_jb_buf = (char*)pj_pool_alloc(pool, PJ_LOG_MAX_SIZE);

	    /* Print column header */
	    len = pj_ansi_snprintf(stream->trace_jb_buf, PJ_LOG_MAX_SIZE,
				   "Time, Operation, Size, Frame Count, "
				   "Frame type, RTP Seq, RTP TS, RTP M, "
				   "JB size, JB burst level, JB prefetch\n");
	    pj_file_write(stream->trace_jb_fd, stream->trace_jb_buf, &len);
	    pj_file_flush(stream->trace_jb_fd);
	}
    }
#endif

    /* Save the stream info */
    pj_memcpy(&stream->info, info, sizeof(*info));
    stream->info.codec_param = pjmedia_vid_codec_param_clone(
						pool, info->codec_param);

    /* Success! */
    *p_stream = stream;

    PJ_LOG(5,(THIS_FILE, "Video stream %s created", stream->name.ptr));

    return PJ_SUCCESS;
}


/*
 * Destroy stream.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_destroy( pjmedia_vid_stream *stream )
{
    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);

#if TRACE_RC
    {
	unsigned total_time;

	total_time = pj_elapsed_msec(&stream->tx_start, &stream->tx_end);
	PJ_LOG(5, (stream->name.ptr, 
		   "RC stat: pkt_cnt=%.2f/image, sleep=%.2fms/s, fps=%.2f",
		   stream->rc_total_pkt*1.0/stream->rc_total_img,
		   stream->rc_total_sleep*1000.0/total_time,
		   stream->rc_total_img*1000.0/total_time));
    }
#endif

    /* Send RTCP BYE (also SDES) */
    if (!stream->rtcp_sdes_bye_disabled) {
	send_rtcp(stream, PJ_TRUE, PJ_TRUE);
    }

    /* Detach from transport 
     * MUST NOT hold stream mutex while detaching from transport, as
     * it may cause deadlock. See ticket #460 for the details.
     */
    if (stream->transport) {
	pjmedia_transport_detach(stream->transport, stream);
	stream->transport = NULL;
    }

    /* This function may be called when stream is partly initialized. */
    if (stream->jb_mutex)
	pj_mutex_lock(stream->jb_mutex);


    /* Free codec. */
    if (stream->codec) {
        pjmedia_event_unsubscribe(NULL, &stream_event_cb, stream,
                                  stream->codec);
	pjmedia_vid_codec_close(stream->codec);
	pjmedia_vid_codec_mgr_dealloc_codec(stream->codec_mgr, stream->codec);
	stream->codec = NULL;
    }

    /* Free mutex */
    
    if (stream->jb_mutex) {
	pj_mutex_destroy(stream->jb_mutex);
	stream->jb_mutex = NULL;
    }

    /* Destroy jitter buffer */
    if (stream->jb) {
	pjmedia_jbuf_destroy(stream->jb);
	stream->jb = NULL;
    }

#if TRACE_JB
    if (TRACE_JB_OPENED(stream)) {
	pj_file_close(stream->trace_jb_fd);
	stream->trace_jb_fd = TRACE_JB_INVALID_FD;
    }
#endif

    if (stream->own_pool) {
	pj_pool_t *pool = stream->own_pool;
	stream->own_pool = NULL;
	pj_pool_release(pool);
    }

    return PJ_SUCCESS;
}


/*
 * Get the port interface.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_get_port(pjmedia_vid_stream *stream,
						pjmedia_dir dir,
						pjmedia_port **p_port )
{
    PJ_ASSERT_RETURN(dir==PJMEDIA_DIR_ENCODING || dir==PJMEDIA_DIR_DECODING,
		     PJ_EINVAL);

    if (dir == PJMEDIA_DIR_ENCODING)
	*p_port = &stream->enc->port;
    else
	*p_port = &stream->dec->port;

    return PJ_SUCCESS;
}


/*
 * Get the transport object
 */
PJ_DEF(pjmedia_transport*) pjmedia_vid_stream_get_transport(
						    pjmedia_vid_stream *st)
{
    return st->transport;
}


/*
 * Get stream statistics.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_get_stat(
					    const pjmedia_vid_stream *stream,
					    pjmedia_rtcp_stat *stat)
{
    PJ_ASSERT_RETURN(stream && stat, PJ_EINVAL);

    pj_memcpy(stat, &stream->rtcp.stat, sizeof(pjmedia_rtcp_stat));
    return PJ_SUCCESS;
}


/*
 * Reset the stream statistics in the middle of a stream session.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_reset_stat(pjmedia_vid_stream *stream)
{
    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    pjmedia_rtcp_init_stat(&stream->rtcp.stat);

    return PJ_SUCCESS;
}


/*
 * Get jitter buffer state.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_get_stat_jbuf(
					    const pjmedia_vid_stream *stream,
					    pjmedia_jb_state *state)
{
    PJ_ASSERT_RETURN(stream && state, PJ_EINVAL);
    return pjmedia_jbuf_get_state(stream->jb, state);
}


/*
 * Get the stream info.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_get_info(
					    const pjmedia_vid_stream *stream,
					    pjmedia_vid_stream_info *info)
{
    PJ_ASSERT_RETURN(stream && info, PJ_EINVAL);
    pj_memcpy(info, &stream->info, sizeof(*info));
    return PJ_SUCCESS;
}


/*
 * Start stream.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_start(pjmedia_vid_stream *stream)
{

    PJ_ASSERT_RETURN(stream && stream->enc && stream->dec, PJ_EINVALIDOP);

    if (stream->enc && (stream->dir & PJMEDIA_DIR_ENCODING)) {
	stream->enc->paused = 0;
	//pjmedia_snd_stream_start(stream->enc->snd_stream);
	PJ_LOG(4,(stream->enc->port.info.name.ptr, "Encoder stream started"));
    } else {
	PJ_LOG(4,(stream->enc->port.info.name.ptr, "Encoder stream paused"));
    }

    if (stream->dec && (stream->dir & PJMEDIA_DIR_DECODING)) {
	stream->dec->paused = 0;
	//pjmedia_snd_stream_start(stream->dec->snd_stream);
	PJ_LOG(4,(stream->dec->port.info.name.ptr, "Decoder stream started"));
    } else {
	PJ_LOG(4,(stream->dec->port.info.name.ptr, "Decoder stream paused"));
    }

    return PJ_SUCCESS;
}


/*
 * Check status.
 */
PJ_DEF(pj_bool_t) pjmedia_vid_stream_is_running(pjmedia_vid_stream *stream,
                                                pjmedia_dir dir)
{
    pj_bool_t is_running = PJ_TRUE;

    PJ_ASSERT_RETURN(stream, PJ_FALSE);

    if (dir & PJMEDIA_DIR_ENCODING) {
	is_running &= (stream->enc && !stream->enc->paused);
    }

    if (dir & PJMEDIA_DIR_DECODING) {
	is_running &= (stream->dec && !stream->dec->paused);
    }

    return is_running;
}

/*
 * Pause stream.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_pause(pjmedia_vid_stream *stream,
					     pjmedia_dir dir)
{
    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    if ((dir & PJMEDIA_DIR_ENCODING) && stream->enc) {
	stream->enc->paused = 1;
	PJ_LOG(4,(stream->enc->port.info.name.ptr, "Encoder stream paused"));
    }

    if ((dir & PJMEDIA_DIR_DECODING) && stream->dec) {
	stream->dec->paused = 1;

	/* Also reset jitter buffer */
	pj_mutex_lock( stream->jb_mutex );
	pjmedia_jbuf_reset(stream->jb);
	pj_mutex_unlock( stream->jb_mutex );

	PJ_LOG(4,(stream->dec->port.info.name.ptr, "Decoder stream paused"));
    }

    return PJ_SUCCESS;
}


/*
 * Resume stream
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_resume(pjmedia_vid_stream *stream,
					      pjmedia_dir dir)
{
    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    if ((dir & PJMEDIA_DIR_ENCODING) && stream->enc) {
	stream->enc->paused = 0;
	PJ_LOG(4,(stream->enc->port.info.name.ptr, "Encoder stream resumed"));
    }

    if ((dir & PJMEDIA_DIR_DECODING) && stream->dec) {
	stream->dec->paused = 0;
	PJ_LOG(4,(stream->dec->port.info.name.ptr, "Decoder stream resumed"));
    }

    return PJ_SUCCESS;
}


/*
 * Force stream to send video keyframe.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_send_keyframe(
						pjmedia_vid_stream *stream)
{
    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    if (!pjmedia_vid_stream_is_running(stream, PJMEDIA_DIR_ENCODING))
	return PJ_EINVALIDOP;

    stream->force_keyframe = PJ_TRUE;

    return PJ_SUCCESS;
}


/*
 * Send RTCP SDES.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_send_rtcp_sdes(
						pjmedia_vid_stream *stream)
{
    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    return send_rtcp(stream, PJ_TRUE, PJ_FALSE);
}


/*
 * Send RTCP BYE.
 */
PJ_DEF(pj_status_t) pjmedia_vid_stream_send_rtcp_bye(
						pjmedia_vid_stream *stream)
{
    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    if (stream->enc && stream->transport) {
	return send_rtcp(stream, PJ_TRUE, PJ_TRUE);
    }

    return PJ_SUCCESS;
}


/*
 * Initialize the video stream rate control with default settings.
 */
PJ_DEF(void)
pjmedia_vid_stream_rc_config_default(pjmedia_vid_stream_rc_config *cfg)
{
    pj_bzero(cfg, sizeof(*cfg));
    cfg->method = PJMEDIA_VID_STREAM_RC_SIMPLE_BLOCKING;
}


#endif /* PJMEDIA_HAS_VIDEO */
