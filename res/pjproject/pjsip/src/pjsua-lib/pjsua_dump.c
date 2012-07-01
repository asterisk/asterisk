/* $Id$ */
/* 
 * Copyright (C) 2011-2011 Teluu Inc. (http://www.teluu.com)
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
#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>

const char *good_number(char *buf, pj_int32_t val)
{
    if (val < 1000) {
	pj_ansi_sprintf(buf, "%d", val);
    } else if (val < 1000000) {
	pj_ansi_sprintf(buf, "%d.%dK",
			val / 1000,
			(val % 1000) / 100);
    } else {
	pj_ansi_sprintf(buf, "%d.%02dM",
			val / 1000000,
			(val % 1000000) / 10000);
    }

    return buf;
}

static unsigned dump_media_stat(const char *indent,
				char *buf, unsigned maxlen,
				const pjmedia_rtcp_stat *stat,
				const char *rx_info, const char *tx_info)
{
    char last_update[64];
    char packets[32], bytes[32], ipbytes[32], avg_bps[32], avg_ipbps[32];
    pj_time_val media_duration, now;
    char *p = buf, *end = buf+maxlen;
    int len;

    if (stat->rx.update_cnt == 0)
	strcpy(last_update, "never");
    else {
	pj_gettimeofday(&now);
	PJ_TIME_VAL_SUB(now, stat->rx.update);
	sprintf(last_update, "%02ldh:%02ldm:%02ld.%03lds ago",
		now.sec / 3600,
		(now.sec % 3600) / 60,
		now.sec % 60,
		now.msec);
    }

    pj_gettimeofday(&media_duration);
    PJ_TIME_VAL_SUB(media_duration, stat->start);
    if (PJ_TIME_VAL_MSEC(media_duration) == 0)
	media_duration.msec = 1;

    len = pj_ansi_snprintf(p, end-p,
	   "%s     RX %s last update:%s\n"
	   "%s        total %spkt %sB (%sB +IP hdr) @avg=%sbps/%sbps\n"
	   "%s        pkt loss=%d (%3.1f%%), discrd=%d (%3.1f%%), dup=%d (%2.1f%%), reord=%d (%3.1f%%)\n"
	   "%s              (msec)    min     avg     max     last    dev\n"
	   "%s        loss period: %7.3f %7.3f %7.3f %7.3f %7.3f\n"
	   "%s        jitter     : %7.3f %7.3f %7.3f %7.3f %7.3f\n"
#if defined(PJMEDIA_RTCP_STAT_HAS_RAW_JITTER) && PJMEDIA_RTCP_STAT_HAS_RAW_JITTER!=0
	   "%s        raw jitter : %7.3f %7.3f %7.3f %7.3f %7.3f\n"
#endif
#if defined(PJMEDIA_RTCP_STAT_HAS_IPDV) && PJMEDIA_RTCP_STAT_HAS_IPDV!=0
	   "%s        IPDV       : %7.3f %7.3f %7.3f %7.3f %7.3f\n"
#endif
	   "%s",
	   indent,
	   rx_info? rx_info : "",
	   last_update,

	   indent,
	   good_number(packets, stat->rx.pkt),
	   good_number(bytes, stat->rx.bytes),
	   good_number(ipbytes, stat->rx.bytes + stat->rx.pkt * 40),
	   good_number(avg_bps, (pj_int32_t)((pj_int64_t)stat->rx.bytes * 8 * 1000 / PJ_TIME_VAL_MSEC(media_duration))),
	   good_number(avg_ipbps, (pj_int32_t)(((pj_int64_t)stat->rx.bytes + stat->rx.pkt * 40) * 8 * 1000 / PJ_TIME_VAL_MSEC(media_duration))),
	   indent,
	   stat->rx.loss,
	   (stat->rx.loss? stat->rx.loss * 100.0 / (stat->rx.pkt + stat->rx.loss) : 0),
	   stat->rx.discard,
	   (stat->rx.discard? stat->rx.discard * 100.0 / (stat->rx.pkt + stat->rx.loss) : 0),
	   stat->rx.dup,
	   (stat->rx.dup? stat->rx.dup * 100.0 / (stat->rx.pkt + stat->rx.loss) : 0),
	   stat->rx.reorder,
	   (stat->rx.reorder? stat->rx.reorder * 100.0 / (stat->rx.pkt + stat->rx.loss) : 0),
	   indent, indent,
	   stat->rx.loss_period.min / 1000.0,
	   stat->rx.loss_period.mean / 1000.0,
	   stat->rx.loss_period.max / 1000.0,
	   stat->rx.loss_period.last / 1000.0,
	   pj_math_stat_get_stddev(&stat->rx.loss_period) / 1000.0,
	   indent,
	   stat->rx.jitter.min / 1000.0,
	   stat->rx.jitter.mean / 1000.0,
	   stat->rx.jitter.max / 1000.0,
	   stat->rx.jitter.last / 1000.0,
	   pj_math_stat_get_stddev(&stat->rx.jitter) / 1000.0,
#if defined(PJMEDIA_RTCP_STAT_HAS_RAW_JITTER) && PJMEDIA_RTCP_STAT_HAS_RAW_JITTER!=0
	   indent,
	   stat->rx_raw_jitter.min / 1000.0,
	   stat->rx_raw_jitter.mean / 1000.0,
	   stat->rx_raw_jitter.max / 1000.0,
	   stat->rx_raw_jitter.last / 1000.0,
	   pj_math_stat_get_stddev(&stat->rx_raw_jitter) / 1000.0,
#endif
#if defined(PJMEDIA_RTCP_STAT_HAS_IPDV) && PJMEDIA_RTCP_STAT_HAS_IPDV!=0
	   indent,
	   stat->rx_ipdv.min / 1000.0,
	   stat->rx_ipdv.mean / 1000.0,
	   stat->rx_ipdv.max / 1000.0,
	   stat->rx_ipdv.last / 1000.0,
	   pj_math_stat_get_stddev(&stat->rx_ipdv) / 1000.0,
#endif
	   ""
	   );

    if (len < 1 || len > end-p) {
	*p = '\0';
	return (p-buf);
    }
    p += len;

    if (stat->tx.update_cnt == 0)
	strcpy(last_update, "never");
    else {
	pj_gettimeofday(&now);
	PJ_TIME_VAL_SUB(now, stat->tx.update);
	sprintf(last_update, "%02ldh:%02ldm:%02ld.%03lds ago",
		now.sec / 3600,
		(now.sec % 3600) / 60,
		now.sec % 60,
		now.msec);
    }

    len = pj_ansi_snprintf(p, end-p,
	   "%s     TX %s last update:%s\n"
	   "%s        total %spkt %sB (%sB +IP hdr) @avg=%sbps/%sbps\n"
	   "%s        pkt loss=%d (%3.1f%%), dup=%d (%3.1f%%), reorder=%d (%3.1f%%)\n"
	   "%s              (msec)    min     avg     max     last    dev \n"
	   "%s        loss period: %7.3f %7.3f %7.3f %7.3f %7.3f\n"
	   "%s        jitter     : %7.3f %7.3f %7.3f %7.3f %7.3f\n",
	   indent,
	   tx_info,
	   last_update,

	   indent,
	   good_number(packets, stat->tx.pkt),
	   good_number(bytes, stat->tx.bytes),
	   good_number(ipbytes, stat->tx.bytes + stat->tx.pkt * 40),
	   good_number(avg_bps, (pj_int32_t)((pj_int64_t)stat->tx.bytes * 8 * 1000 / PJ_TIME_VAL_MSEC(media_duration))),
	   good_number(avg_ipbps, (pj_int32_t)(((pj_int64_t)stat->tx.bytes + stat->tx.pkt * 40) * 8 * 1000 / PJ_TIME_VAL_MSEC(media_duration))),

	   indent,
	   stat->tx.loss,
	   (stat->tx.loss? stat->tx.loss * 100.0 / (stat->tx.pkt + stat->tx.loss) : 0),
	   stat->tx.dup,
	   (stat->tx.dup? stat->tx.dup * 100.0 / (stat->tx.pkt + stat->tx.loss) : 0),
	   stat->tx.reorder,
	   (stat->tx.reorder? stat->tx.reorder * 100.0 / (stat->tx.pkt + stat->tx.loss) : 0),

	   indent, indent,
	   stat->tx.loss_period.min / 1000.0,
	   stat->tx.loss_period.mean / 1000.0,
	   stat->tx.loss_period.max / 1000.0,
	   stat->tx.loss_period.last / 1000.0,
	   pj_math_stat_get_stddev(&stat->tx.loss_period) / 1000.0,
	   indent,
	   stat->tx.jitter.min / 1000.0,
	   stat->tx.jitter.mean / 1000.0,
	   stat->tx.jitter.max / 1000.0,
	   stat->tx.jitter.last / 1000.0,
	   pj_math_stat_get_stddev(&stat->tx.jitter) / 1000.0
	   );

    if (len < 1 || len > end-p) {
	*p = '\0';
	return (p-buf);
    }
    p += len;

    len = pj_ansi_snprintf(p, end-p,
	   "%s     RTT msec      : %7.3f %7.3f %7.3f %7.3f %7.3f\n",
	   indent,
	   stat->rtt.min / 1000.0,
	   stat->rtt.mean / 1000.0,
	   stat->rtt.max / 1000.0,
	   stat->rtt.last / 1000.0,
	   pj_math_stat_get_stddev(&stat->rtt) / 1000.0
	   );
    if (len < 1 || len > end-p) {
	*p = '\0';
	return (p-buf);
    }
    p += len;

    return (p-buf);
}


/* Dump media session */
static void dump_media_session(const char *indent,
			       char *buf, unsigned maxlen,
			       pjsua_call *call)
{
    unsigned i;
    char *p = buf, *end = buf+maxlen;
    int len;

    for (i=0; i<call->med_cnt; ++i) {
	pjsua_call_media *call_med = &call->media[i];
	pjmedia_rtcp_stat stat;
	pj_bool_t has_stat;
	pjmedia_transport_info tp_info;
	char rem_addr_buf[80];
	char codec_info[32] = {'0'};
	char rx_info[80] = {'\0'};
	char tx_info[80] = {'\0'};
	const char *rem_addr;
	const char *dir_str;
	const char *media_type_str;

	switch (call_med->type) {
	case PJMEDIA_TYPE_AUDIO:
	    media_type_str = "audio";
	    break;
	case PJMEDIA_TYPE_VIDEO:
	    media_type_str = "video";
	    break;
	case PJMEDIA_TYPE_APPLICATION:
	    media_type_str = "application";
	    break;
	default:
	    media_type_str = "unknown";
	    break;
	}

	/* Check if the stream is deactivated */
	if (call_med->tp == NULL ||
	    (!call_med->strm.a.stream && !call_med->strm.v.stream))
	{
	    len = pj_ansi_snprintf(p, end-p,
		      "%s  #%d %s deactivated\n",
		      indent, i, media_type_str);
	    if (len < 1 || len > end-p) {
		*p = '\0';
		return;
	    }

	    p += len;
	    continue;
	}

	pjmedia_transport_info_init(&tp_info);
	pjmedia_transport_get_info(call_med->tp, &tp_info);

	// rem_addr will contain actual address of RTP originator, instead of
	// remote RTP address specified by stream which is fetched from the SDP.
	// Please note that we are assuming only one stream per call.
	//rem_addr = pj_sockaddr_print(&info.stream_info[i].rem_addr,
	//			     rem_addr_buf, sizeof(rem_addr_buf), 3);
	if (pj_sockaddr_has_addr(&tp_info.src_rtp_name)) {
	    rem_addr = pj_sockaddr_print(&tp_info.src_rtp_name, rem_addr_buf,
					 sizeof(rem_addr_buf), 3);
	} else {
	    pj_ansi_snprintf(rem_addr_buf, sizeof(rem_addr_buf), "-");
	    rem_addr = rem_addr_buf;
	}

	if (call_med->dir == PJMEDIA_DIR_NONE) {
	    /* To handle when the stream that is currently being paused
	     * (http://trac.pjsip.org/repos/ticket/1079)
	     */
	    dir_str = "inactive";
	} else if (call_med->dir == PJMEDIA_DIR_ENCODING)
	    dir_str = "sendonly";
	else if (call_med->dir == PJMEDIA_DIR_DECODING)
	    dir_str = "recvonly";
	else if (call_med->dir == PJMEDIA_DIR_ENCODING_DECODING)
	    dir_str = "sendrecv";
	else
	    dir_str = "inactive";

	if (call_med->type == PJMEDIA_TYPE_AUDIO) {
	    pjmedia_stream *stream = call_med->strm.a.stream;
	    pjmedia_stream_info info;

	    pjmedia_stream_get_stat(stream, &stat);
	    has_stat = PJ_TRUE;

	    pjmedia_stream_get_info(stream, &info);
	    pj_ansi_snprintf(codec_info, sizeof(codec_info), " %.*s @%dkHz",
			     (int)info.fmt.encoding_name.slen,
			     info.fmt.encoding_name.ptr,
			     info.fmt.clock_rate / 1000);
	    pj_ansi_snprintf(rx_info, sizeof(rx_info), "pt=%d,",
			     info.rx_pt);
	    pj_ansi_snprintf(tx_info, sizeof(tx_info), "pt=%d, ptime=%d,",
			     info.tx_pt,
			     info.param->setting.frm_per_pkt*
			     info.param->info.frm_ptime);

#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
	} else if (call_med->type == PJMEDIA_TYPE_VIDEO) {
	    pjmedia_vid_stream *stream = call_med->strm.v.stream;
	    pjmedia_vid_stream_info info;

	    pjmedia_vid_stream_get_stat(stream, &stat);
	    has_stat = PJ_TRUE;

	    pjmedia_vid_stream_get_info(stream, &info);
	    pj_ansi_snprintf(codec_info, sizeof(codec_info), " %.*s",
	                     (int)info.codec_info.encoding_name.slen,
			     info.codec_info.encoding_name.ptr);
	    if (call_med->dir & PJMEDIA_DIR_DECODING) {
		pjmedia_video_format_detail *vfd;
		vfd = pjmedia_format_get_video_format_detail(
					&info.codec_param->dec_fmt, PJ_TRUE);
		pj_ansi_snprintf(rx_info, sizeof(rx_info),
				 "pt=%d, size=%dx%d, fps=%.2f,",
				 info.rx_pt,
				 vfd->size.w, vfd->size.h,
				 vfd->fps.num*1.0/vfd->fps.denum);
	    }
	    if (call_med->dir & PJMEDIA_DIR_ENCODING) {
		pjmedia_video_format_detail *vfd;
		vfd = pjmedia_format_get_video_format_detail(
					&info.codec_param->enc_fmt, PJ_TRUE);
		pj_ansi_snprintf(tx_info, sizeof(tx_info),
				 "pt=%d, size=%dx%d, fps=%.2f,",
				 info.tx_pt,
				 vfd->size.w, vfd->size.h,
				 vfd->fps.num*1.0/vfd->fps.denum);
	    }
#endif /* PJMEDIA_HAS_VIDEO */

	} else {
	    has_stat = PJ_FALSE;
	}

	len = pj_ansi_snprintf(p, end-p,
		  "%s  #%d %s%s, %s, peer=%s\n",
		  indent,
		  call_med->idx,
		  media_type_str,
		  codec_info,
		  dir_str,
		  rem_addr);
	if (len < 1 || len > end-p) {
	    *p = '\0';
	    return;
	}
	p += len;

	/* Get and ICE SRTP status */
	if (call_med->tp) {
	    pjmedia_transport_info tp_info;

	    pjmedia_transport_info_init(&tp_info);
	    pjmedia_transport_get_info(call_med->tp, &tp_info);
	    if (tp_info.specific_info_cnt > 0) {
		unsigned j;
		for (j = 0; j < tp_info.specific_info_cnt; ++j) {
		    if (tp_info.spc_info[j].type == PJMEDIA_TRANSPORT_TYPE_SRTP)
		    {
			pjmedia_srtp_info *srtp_info =
				    (pjmedia_srtp_info*) tp_info.spc_info[j].buffer;

			len = pj_ansi_snprintf(p, end-p,
					       "   %s  SRTP status: %s Crypto-suite: %s",
					       indent,
					       (srtp_info->active?"Active":"Not active"),
					       srtp_info->tx_policy.name.ptr);
			if (len > 0 && len < end-p) {
			    p += len;
			    *p++ = '\n';
			    *p = '\0';
			}
		    } else if (tp_info.spc_info[j].type==PJMEDIA_TRANSPORT_TYPE_ICE) {
			const pjmedia_ice_transport_info *ii;
			unsigned jj;

			ii = (const pjmedia_ice_transport_info*)
			     tp_info.spc_info[j].buffer;

			len = pj_ansi_snprintf(p, end-p,
					       "   %s  ICE role: %s, state: %s, comp_cnt: %u",
					       indent,
					       pj_ice_sess_role_name(ii->role),
					       pj_ice_strans_state_name(ii->sess_state),
					       ii->comp_cnt);
			if (len > 0 && len < end-p) {
			    p += len;
			    *p++ = '\n';
			    *p = '\0';
			}

			for (jj=0; ii->sess_state==PJ_ICE_STRANS_STATE_RUNNING && jj<2; ++jj) {
			    const char *type1 = pj_ice_get_cand_type_name(ii->comp[jj].lcand_type);
			    const char *type2 = pj_ice_get_cand_type_name(ii->comp[jj].rcand_type);
			    char addr1[PJ_INET6_ADDRSTRLEN+10];
			    char addr2[PJ_INET6_ADDRSTRLEN+10];

			    if (pj_sockaddr_has_addr(&ii->comp[jj].lcand_addr))
				pj_sockaddr_print(&ii->comp[jj].lcand_addr, addr1, sizeof(addr1), 3);
			    else
				strcpy(addr1, "0.0.0.0:0");
			    if (pj_sockaddr_has_addr(&ii->comp[jj].rcand_addr))
				pj_sockaddr_print(&ii->comp[jj].rcand_addr, addr2, sizeof(addr2), 3);
			    else
				strcpy(addr2, "0.0.0.0:0");
			    len = pj_ansi_snprintf(p, end-p,
			                           "   %s     [%d]: L:%s (%c) --> R:%s (%c)\n",
			                           indent, jj,
			                           addr1, type1[0],
			                           addr2, type2[0]);
			    if (len > 0 && len < end-p) {
				p += len;
				*p = '\0';
			    }
			}
		    }
		}
	    }
	}


	if (has_stat) {
	    len = dump_media_stat(indent, p, end-p, &stat,
				  rx_info, tx_info);
	    p += len;
	}

#if defined(PJMEDIA_HAS_RTCP_XR) && (PJMEDIA_HAS_RTCP_XR != 0)
#   define SAMPLES_TO_USEC(usec, samples, clock_rate) \
	do { \
	    if (samples <= 4294) \
		usec = samples * 1000000 / clock_rate; \
	    else { \
		usec = samples * 1000 / clock_rate; \
		usec *= 1000; \
	    } \
	} while(0)

#   define PRINT_VOIP_MTC_VAL(s, v) \
	if (v == 127) \
	    sprintf(s, "(na)"); \
	else \
	    sprintf(s, "%d", v)

#   define VALIDATE_PRINT_BUF() \
	if (len < 1 || len > end-p) { *p = '\0'; return; } \
	p += len; *p++ = '\n'; *p = '\0'


	if (call_med->type == PJMEDIA_TYPE_AUDIO) {
	    pjmedia_stream_info info;
	    char last_update[64];
	    char loss[16], dup[16];
	    char jitter[80];
	    char toh[80];
	    char plc[16], jba[16], jbr[16];
	    char signal_lvl[16], noise_lvl[16], rerl[16];
	    char r_factor[16], ext_r_factor[16], mos_lq[16], mos_cq[16];
	    pjmedia_rtcp_xr_stat xr_stat;
	    unsigned clock_rate;
	    pj_time_val now;

	    if (pjmedia_stream_get_stat_xr(call_med->strm.a.stream,
	                                   &xr_stat) != PJ_SUCCESS)
	    {
		continue;
	    }

	    if (pjmedia_stream_get_info(call_med->strm.a.stream, &info)
		    != PJ_SUCCESS)
	    {
		continue;
	    }

	    clock_rate = info.fmt.clock_rate;
	    pj_gettimeofday(&now);

	    len = pj_ansi_snprintf(p, end-p, "\n%s  Extended reports:", indent);
	    VALIDATE_PRINT_BUF();

	    /* Statistics Summary */
	    len = pj_ansi_snprintf(p, end-p, "%s   Statistics Summary", indent);
	    VALIDATE_PRINT_BUF();

	    if (xr_stat.rx.stat_sum.l)
		sprintf(loss, "%d", xr_stat.rx.stat_sum.lost);
	    else
		sprintf(loss, "(na)");

	    if (xr_stat.rx.stat_sum.d)
		sprintf(dup, "%d", xr_stat.rx.stat_sum.dup);
	    else
		sprintf(dup, "(na)");

	    if (xr_stat.rx.stat_sum.j) {
		unsigned jmin, jmax, jmean, jdev;

		SAMPLES_TO_USEC(jmin, xr_stat.rx.stat_sum.jitter.min,
				clock_rate);
		SAMPLES_TO_USEC(jmax, xr_stat.rx.stat_sum.jitter.max,
				clock_rate);
		SAMPLES_TO_USEC(jmean, xr_stat.rx.stat_sum.jitter.mean,
				clock_rate);
		SAMPLES_TO_USEC(jdev,
			       pj_math_stat_get_stddev(&xr_stat.rx.stat_sum.jitter),
			       clock_rate);
		sprintf(jitter, "%7.3f %7.3f %7.3f %7.3f",
			jmin/1000.0, jmean/1000.0, jmax/1000.0, jdev/1000.0);
	    } else
		sprintf(jitter, "(report not available)");

	    if (xr_stat.rx.stat_sum.t) {
		sprintf(toh, "%11d %11d %11d %11d",
			xr_stat.rx.stat_sum.toh.min,
			xr_stat.rx.stat_sum.toh.mean,
			xr_stat.rx.stat_sum.toh.max,
			pj_math_stat_get_stddev(&xr_stat.rx.stat_sum.toh));
	    } else
		sprintf(toh, "(report not available)");

	    if (xr_stat.rx.stat_sum.update.sec == 0)
		strcpy(last_update, "never");
	    else {
		pj_gettimeofday(&now);
		PJ_TIME_VAL_SUB(now, xr_stat.rx.stat_sum.update);
		sprintf(last_update, "%02ldh:%02ldm:%02ld.%03lds ago",
			now.sec / 3600,
			(now.sec % 3600) / 60,
			now.sec % 60,
			now.msec);
	    }

	    len = pj_ansi_snprintf(p, end-p,
		    "%s     RX last update: %s\n"
		    "%s        begin seq=%d, end seq=%d\n"
		    "%s        pkt loss=%s, dup=%s\n"
		    "%s              (msec)    min     avg     max     dev\n"
		    "%s        jitter     : %s\n"
		    "%s        toh        : %s",
		    indent, last_update,
		    indent,
		    xr_stat.rx.stat_sum.begin_seq, xr_stat.rx.stat_sum.end_seq,
		    indent, loss, dup,
		    indent,
		    indent, jitter,
		    indent, toh
		    );
	    VALIDATE_PRINT_BUF();

	    if (xr_stat.tx.stat_sum.l)
		sprintf(loss, "%d", xr_stat.tx.stat_sum.lost);
	    else
		sprintf(loss, "(na)");

	    if (xr_stat.tx.stat_sum.d)
		sprintf(dup, "%d", xr_stat.tx.stat_sum.dup);
	    else
		sprintf(dup, "(na)");

	    if (xr_stat.tx.stat_sum.j) {
		unsigned jmin, jmax, jmean, jdev;

		SAMPLES_TO_USEC(jmin, xr_stat.tx.stat_sum.jitter.min,
				clock_rate);
		SAMPLES_TO_USEC(jmax, xr_stat.tx.stat_sum.jitter.max,
				clock_rate);
		SAMPLES_TO_USEC(jmean, xr_stat.tx.stat_sum.jitter.mean,
				clock_rate);
		SAMPLES_TO_USEC(jdev,
			       pj_math_stat_get_stddev(&xr_stat.tx.stat_sum.jitter),
			       clock_rate);
		sprintf(jitter, "%7.3f %7.3f %7.3f %7.3f",
			jmin/1000.0, jmean/1000.0, jmax/1000.0, jdev/1000.0);
	    } else
		sprintf(jitter, "(report not available)");

	    if (xr_stat.tx.stat_sum.t) {
		sprintf(toh, "%11d %11d %11d %11d",
			xr_stat.tx.stat_sum.toh.min,
			xr_stat.tx.stat_sum.toh.mean,
			xr_stat.tx.stat_sum.toh.max,
			pj_math_stat_get_stddev(&xr_stat.rx.stat_sum.toh));
	    } else
		sprintf(toh,    "(report not available)");

	    if (xr_stat.tx.stat_sum.update.sec == 0)
		strcpy(last_update, "never");
	    else {
		pj_gettimeofday(&now);
		PJ_TIME_VAL_SUB(now, xr_stat.tx.stat_sum.update);
		sprintf(last_update, "%02ldh:%02ldm:%02ld.%03lds ago",
			now.sec / 3600,
			(now.sec % 3600) / 60,
			now.sec % 60,
			now.msec);
	    }

	    len = pj_ansi_snprintf(p, end-p,
		    "%s     TX last update: %s\n"
		    "%s        begin seq=%d, end seq=%d\n"
		    "%s        pkt loss=%s, dup=%s\n"
		    "%s              (msec)    min     avg     max     dev\n"
		    "%s        jitter     : %s\n"
		    "%s        toh        : %s",
		    indent, last_update,
		    indent,
		    xr_stat.tx.stat_sum.begin_seq, xr_stat.tx.stat_sum.end_seq,
		    indent, loss, dup,
		    indent,
		    indent, jitter,
		    indent, toh
		    );
	    VALIDATE_PRINT_BUF();


	    /* VoIP Metrics */
	    len = pj_ansi_snprintf(p, end-p, "%s   VoIP Metrics", indent);
	    VALIDATE_PRINT_BUF();

	    PRINT_VOIP_MTC_VAL(signal_lvl, xr_stat.rx.voip_mtc.signal_lvl);
	    PRINT_VOIP_MTC_VAL(noise_lvl, xr_stat.rx.voip_mtc.noise_lvl);
	    PRINT_VOIP_MTC_VAL(rerl, xr_stat.rx.voip_mtc.rerl);
	    PRINT_VOIP_MTC_VAL(r_factor, xr_stat.rx.voip_mtc.r_factor);
	    PRINT_VOIP_MTC_VAL(ext_r_factor, xr_stat.rx.voip_mtc.ext_r_factor);
	    PRINT_VOIP_MTC_VAL(mos_lq, xr_stat.rx.voip_mtc.mos_lq);
	    PRINT_VOIP_MTC_VAL(mos_cq, xr_stat.rx.voip_mtc.mos_cq);

	    switch ((xr_stat.rx.voip_mtc.rx_config>>6) & 3) {
		case PJMEDIA_RTCP_XR_PLC_DIS:
		    sprintf(plc, "DISABLED");
		    break;
		case PJMEDIA_RTCP_XR_PLC_ENH:
		    sprintf(plc, "ENHANCED");
		    break;
		case PJMEDIA_RTCP_XR_PLC_STD:
		    sprintf(plc, "STANDARD");
		    break;
		case PJMEDIA_RTCP_XR_PLC_UNK:
		default:
		    sprintf(plc, "UNKNOWN");
		    break;
	    }

	    switch ((xr_stat.rx.voip_mtc.rx_config>>4) & 3) {
		case PJMEDIA_RTCP_XR_JB_FIXED:
		    sprintf(jba, "FIXED");
		    break;
		case PJMEDIA_RTCP_XR_JB_ADAPTIVE:
		    sprintf(jba, "ADAPTIVE");
		    break;
		default:
		    sprintf(jba, "UNKNOWN");
		    break;
	    }

	    sprintf(jbr, "%d", xr_stat.rx.voip_mtc.rx_config & 0x0F);

	    if (xr_stat.rx.voip_mtc.update.sec == 0)
		strcpy(last_update, "never");
	    else {
		pj_gettimeofday(&now);
		PJ_TIME_VAL_SUB(now, xr_stat.rx.voip_mtc.update);
		sprintf(last_update, "%02ldh:%02ldm:%02ld.%03lds ago",
			now.sec / 3600,
			(now.sec % 3600) / 60,
			now.sec % 60,
			now.msec);
	    }

	    len = pj_ansi_snprintf(p, end-p,
		    "%s     RX last update: %s\n"
		    "%s        packets    : loss rate=%d (%.2f%%), discard rate=%d (%.2f%%)\n"
		    "%s        burst      : density=%d (%.2f%%), duration=%d%s\n"
		    "%s        gap        : density=%d (%.2f%%), duration=%d%s\n"
		    "%s        delay      : round trip=%d%s, end system=%d%s\n"
		    "%s        level      : signal=%s%s, noise=%s%s, RERL=%s%s\n"
		    "%s        quality    : R factor=%s, ext R factor=%s\n"
		    "%s                     MOS LQ=%s, MOS CQ=%s\n"
		    "%s        config     : PLC=%s, JB=%s, JB rate=%s, Gmin=%d\n"
		    "%s        JB delay   : cur=%d%s, max=%d%s, abs max=%d%s",
		    indent,
		    last_update,
		    /* packets */
		    indent,
		    xr_stat.rx.voip_mtc.loss_rate, xr_stat.rx.voip_mtc.loss_rate*100.0/256,
		    xr_stat.rx.voip_mtc.discard_rate, xr_stat.rx.voip_mtc.discard_rate*100.0/256,
		    /* burst */
		    indent,
		    xr_stat.rx.voip_mtc.burst_den, xr_stat.rx.voip_mtc.burst_den*100.0/256,
		    xr_stat.rx.voip_mtc.burst_dur, "ms",
		    /* gap */
		    indent,
		    xr_stat.rx.voip_mtc.gap_den, xr_stat.rx.voip_mtc.gap_den*100.0/256,
		    xr_stat.rx.voip_mtc.gap_dur, "ms",
		    /* delay */
		    indent,
		    xr_stat.rx.voip_mtc.rnd_trip_delay, "ms",
		    xr_stat.rx.voip_mtc.end_sys_delay, "ms",
		    /* level */
		    indent,
		    signal_lvl, "dB",
		    noise_lvl, "dB",
		    rerl, "",
		    /* quality */
		    indent,
		    r_factor, ext_r_factor,
		    indent,
		    mos_lq, mos_cq,
		    /* config */
		    indent,
		    plc, jba, jbr, xr_stat.rx.voip_mtc.gmin,
		    /* JB delay */
		    indent,
		    xr_stat.rx.voip_mtc.jb_nom, "ms",
		    xr_stat.rx.voip_mtc.jb_max, "ms",
		    xr_stat.rx.voip_mtc.jb_abs_max, "ms"
		    );
	    VALIDATE_PRINT_BUF();

	    PRINT_VOIP_MTC_VAL(signal_lvl, xr_stat.tx.voip_mtc.signal_lvl);
	    PRINT_VOIP_MTC_VAL(noise_lvl, xr_stat.tx.voip_mtc.noise_lvl);
	    PRINT_VOIP_MTC_VAL(rerl, xr_stat.tx.voip_mtc.rerl);
	    PRINT_VOIP_MTC_VAL(r_factor, xr_stat.tx.voip_mtc.r_factor);
	    PRINT_VOIP_MTC_VAL(ext_r_factor, xr_stat.tx.voip_mtc.ext_r_factor);
	    PRINT_VOIP_MTC_VAL(mos_lq, xr_stat.tx.voip_mtc.mos_lq);
	    PRINT_VOIP_MTC_VAL(mos_cq, xr_stat.tx.voip_mtc.mos_cq);

	    switch ((xr_stat.tx.voip_mtc.rx_config>>6) & 3) {
		case PJMEDIA_RTCP_XR_PLC_DIS:
		    sprintf(plc, "DISABLED");
		    break;
		case PJMEDIA_RTCP_XR_PLC_ENH:
		    sprintf(plc, "ENHANCED");
		    break;
		case PJMEDIA_RTCP_XR_PLC_STD:
		    sprintf(plc, "STANDARD");
		    break;
		case PJMEDIA_RTCP_XR_PLC_UNK:
		default:
		    sprintf(plc, "unknown");
		    break;
	    }

	    switch ((xr_stat.tx.voip_mtc.rx_config>>4) & 3) {
		case PJMEDIA_RTCP_XR_JB_FIXED:
		    sprintf(jba, "FIXED");
		    break;
		case PJMEDIA_RTCP_XR_JB_ADAPTIVE:
		    sprintf(jba, "ADAPTIVE");
		    break;
		default:
		    sprintf(jba, "unknown");
		    break;
	    }

	    sprintf(jbr, "%d", xr_stat.tx.voip_mtc.rx_config & 0x0F);

	    if (xr_stat.tx.voip_mtc.update.sec == 0)
		strcpy(last_update, "never");
	    else {
		pj_gettimeofday(&now);
		PJ_TIME_VAL_SUB(now, xr_stat.tx.voip_mtc.update);
		sprintf(last_update, "%02ldh:%02ldm:%02ld.%03lds ago",
			now.sec / 3600,
			(now.sec % 3600) / 60,
			now.sec % 60,
			now.msec);
	    }

	    len = pj_ansi_snprintf(p, end-p,
		    "%s     TX last update: %s\n"
		    "%s        packets    : loss rate=%d (%.2f%%), discard rate=%d (%.2f%%)\n"
		    "%s        burst      : density=%d (%.2f%%), duration=%d%s\n"
		    "%s        gap        : density=%d (%.2f%%), duration=%d%s\n"
		    "%s        delay      : round trip=%d%s, end system=%d%s\n"
		    "%s        level      : signal=%s%s, noise=%s%s, RERL=%s%s\n"
		    "%s        quality    : R factor=%s, ext R factor=%s\n"
		    "%s                     MOS LQ=%s, MOS CQ=%s\n"
		    "%s        config     : PLC=%s, JB=%s, JB rate=%s, Gmin=%d\n"
		    "%s        JB delay   : cur=%d%s, max=%d%s, abs max=%d%s",
		    indent,
		    last_update,
		    /* pakcets */
		    indent,
		    xr_stat.tx.voip_mtc.loss_rate, xr_stat.tx.voip_mtc.loss_rate*100.0/256,
		    xr_stat.tx.voip_mtc.discard_rate, xr_stat.tx.voip_mtc.discard_rate*100.0/256,
		    /* burst */
		    indent,
		    xr_stat.tx.voip_mtc.burst_den, xr_stat.tx.voip_mtc.burst_den*100.0/256,
		    xr_stat.tx.voip_mtc.burst_dur, "ms",
		    /* gap */
		    indent,
		    xr_stat.tx.voip_mtc.gap_den, xr_stat.tx.voip_mtc.gap_den*100.0/256,
		    xr_stat.tx.voip_mtc.gap_dur, "ms",
		    /* delay */
		    indent,
		    xr_stat.tx.voip_mtc.rnd_trip_delay, "ms",
		    xr_stat.tx.voip_mtc.end_sys_delay, "ms",
		    /* level */
		    indent,
		    signal_lvl, "dB",
		    noise_lvl, "dB",
		    rerl, "",
		    /* quality */
		    indent,
		    r_factor, ext_r_factor,
		    indent,
		    mos_lq, mos_cq,
		    /* config */
		    indent,
		    plc, jba, jbr, xr_stat.tx.voip_mtc.gmin,
		    /* JB delay */
		    indent,
		    xr_stat.tx.voip_mtc.jb_nom, "ms",
		    xr_stat.tx.voip_mtc.jb_max, "ms",
		    xr_stat.tx.voip_mtc.jb_abs_max, "ms"
		    );
	    VALIDATE_PRINT_BUF();


	    /* RTT delay (by receiver side) */
	    len = pj_ansi_snprintf(p, end-p,
		    "%s   RTT (from recv)      min     avg     max     last    dev",
		    indent);
	    VALIDATE_PRINT_BUF();
	    len = pj_ansi_snprintf(p, end-p,
		    "%s     RTT msec      : %7.3f %7.3f %7.3f %7.3f %7.3f",
		    indent,
		    xr_stat.rtt.min / 1000.0,
		    xr_stat.rtt.mean / 1000.0,
		    xr_stat.rtt.max / 1000.0,
		    xr_stat.rtt.last / 1000.0,
		    pj_math_stat_get_stddev(&xr_stat.rtt) / 1000.0
		   );
	    VALIDATE_PRINT_BUF();
	} /* if audio */;
#endif

    }
}


/* Print call info */
void print_call(const char *title,
	        int call_id,
	        char *buf, pj_size_t size)
{
    int len;
    pjsip_inv_session *inv = pjsua_var.calls[call_id].inv;
    pjsip_dialog *dlg = inv->dlg;
    char userinfo[128];

    /* Dump invite sesion info. */

    len = pjsip_hdr_print_on(dlg->remote.info, userinfo, sizeof(userinfo));
    if (len < 0)
	pj_ansi_strcpy(userinfo, "<--uri too long-->");
    else
	userinfo[len] = '\0';

    len = pj_ansi_snprintf(buf, size, "%s[%s] %s",
			   title,
			   pjsip_inv_state_name(inv->state),
			   userinfo);
    if (len < 1 || len >= (int)size) {
	pj_ansi_strcpy(buf, "<--uri too long-->");
	len = 18;
    } else
	buf[len] = '\0';
}


/*
 * Dump call and media statistics to string.
 */
PJ_DEF(pj_status_t) pjsua_call_dump( pjsua_call_id call_id,
				     pj_bool_t with_media,
				     char *buffer,
				     unsigned maxlen,
				     const char *indent)
{
    pjsua_call *call;
    pjsip_dialog *dlg;
    pj_time_val duration, res_delay, con_delay;
    char tmp[128];
    char *p, *end;
    pj_status_t status;
    int len;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
		     PJ_EINVAL);

    status = acquire_call("pjsua_call_dump()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
	return status;

    *buffer = '\0';
    p = buffer;
    end = buffer + maxlen;
    len = 0;

    print_call(indent, call_id, tmp, sizeof(tmp));

    len = pj_ansi_strlen(tmp);
    pj_ansi_strcpy(buffer, tmp);

    p += len;
    *p++ = '\r';
    *p++ = '\n';

    /* Calculate call duration */
    if (call->conn_time.sec != 0) {
	pj_gettimeofday(&duration);
	PJ_TIME_VAL_SUB(duration, call->conn_time);
	con_delay = call->conn_time;
	PJ_TIME_VAL_SUB(con_delay, call->start_time);
    } else {
	duration.sec = duration.msec = 0;
	con_delay.sec = con_delay.msec = 0;
    }

    /* Calculate first response delay */
    if (call->res_time.sec != 0) {
	res_delay = call->res_time;
	PJ_TIME_VAL_SUB(res_delay, call->start_time);
    } else {
	res_delay.sec = res_delay.msec = 0;
    }

    /* Print duration */
    len = pj_ansi_snprintf(p, end-p,
		           "%s  Call time: %02dh:%02dm:%02ds, "
		           "1st res in %d ms, conn in %dms",
			   indent,
		           (int)(duration.sec / 3600),
		           (int)((duration.sec % 3600)/60),
		           (int)(duration.sec % 60),
		           (int)PJ_TIME_VAL_MSEC(res_delay),
		           (int)PJ_TIME_VAL_MSEC(con_delay));

    if (len > 0 && len < end-p) {
	p += len;
	*p++ = '\n';
	*p = '\0';
    }

    /* Dump session statistics */
    if (with_media)
	dump_media_session(indent, p, end-p, call);

    pjsip_dlg_dec_lock(dlg);

    return PJ_SUCCESS;
}

