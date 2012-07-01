/* $Id$ */
/*
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
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

#if defined(PJSUA_MEDIA_HAS_PJMEDIA) && PJSUA_MEDIA_HAS_PJMEDIA != 0
#  error The PJSUA_MEDIA_HAS_PJMEDIA should be declared as zero
#endif


#define THIS_FILE		"alt_pjsua_aud.c"
#define UNIMPLEMENTED(func)	PJ_LOG(2,(THIS_FILE, "*** Call to unimplemented function %s ***", #func));


/*****************************************************************************
 * Our dummy codecs. Since we won't use any PJMEDIA codecs, we need to declare
 * our own codecs and register them to PJMEDIA's codec manager. We just need
 * the info so that they can be listed in SDP. The encoding and decoding will
 * happen in your third party media stream and will not use these codecs,
 * hence the "dummy" name.
 */
static struct alt_codec
{
    pj_str_t	encoding_name;
    pj_uint8_t	payload_type;
    unsigned	clock_rate;
    unsigned	channel_cnt;
    unsigned	frm_ptime;
    unsigned	avg_bps;
    unsigned	max_bps;
} codec_list[] =
{
    /* G.729 */
    { { "G729", 4 }, 18, 8000, 1, 10, 8000, 8000 },
    /* PCMU */
    { { "PCMU", 4 }, 0, 8000, 1, 10, 64000, 64000 },
    /* Our proprietary high end low bit rate (5kbps) codec, if you wish */
    { { "FOO", 3 }, PJMEDIA_RTP_PT_START+0, 16000, 1, 20, 5000, 5000 },
};

static struct alt_codec_factory
{
    pjmedia_codec_factory	base;
} alt_codec_factory;

static pj_status_t alt_codec_test_alloc( pjmedia_codec_factory *factory,
                                         const pjmedia_codec_info *id )
{
    unsigned i;
    for (i=0; i<PJ_ARRAY_SIZE(codec_list); ++i) {
	if (pj_stricmp(&id->encoding_name, &codec_list[i].encoding_name)==0)
	    return PJ_SUCCESS;
    }
    return PJ_ENOTSUP;
}

static pj_status_t alt_codec_default_attr( pjmedia_codec_factory *factory,
                                           const pjmedia_codec_info *id,
                                           pjmedia_codec_param *attr )
{
    struct alt_codec *ac;
    unsigned i;

    PJ_UNUSED_ARG(factory);

    for (i=0; i<PJ_ARRAY_SIZE(codec_list); ++i) {
	if (pj_stricmp(&id->encoding_name, &codec_list[i].encoding_name)==0)
	    break;
    }
    if (i == PJ_ARRAY_SIZE(codec_list))
	return PJ_ENOTFOUND;

    ac = &codec_list[i];

    pj_bzero(attr, sizeof(pjmedia_codec_param));
    attr->info.clock_rate = ac->clock_rate;
    attr->info.channel_cnt = ac->channel_cnt;
    attr->info.avg_bps = ac->avg_bps;
    attr->info.max_bps = ac->max_bps;
    attr->info.pcm_bits_per_sample = 16;
    attr->info.frm_ptime = ac->frm_ptime;
    attr->info.pt = ac->payload_type;

    attr->setting.frm_per_pkt = 1;
    attr->setting.vad = 1;
    attr->setting.plc = 1;

    return PJ_SUCCESS;
}

static pj_status_t alt_codec_enum_codecs(pjmedia_codec_factory *factory,
					 unsigned *count,
					 pjmedia_codec_info codecs[])
{
    unsigned i;

    for (i=0; i<*count && i<PJ_ARRAY_SIZE(codec_list); ++i) {
	struct alt_codec *ac = &codec_list[i];
	pj_bzero(&codecs[i], sizeof(pjmedia_codec_info));
	codecs[i].encoding_name = ac->encoding_name;
	codecs[i].pt = ac->payload_type;
	codecs[i].type = PJMEDIA_TYPE_AUDIO;
	codecs[i].clock_rate = ac->clock_rate;
	codecs[i].channel_cnt = ac->channel_cnt;
    }

    *count = i;

    return PJ_SUCCESS;
}

static pj_status_t alt_codec_alloc_codec(pjmedia_codec_factory *factory,
					 const pjmedia_codec_info *id,
					 pjmedia_codec **p_codec)
{
    /* This will never get called since we won't be using this codec */
    UNIMPLEMENTED(alt_codec_alloc_codec)
    return PJ_ENOTSUP;
}

static pj_status_t alt_codec_dealloc_codec( pjmedia_codec_factory *factory,
                                            pjmedia_codec *codec )
{
    /* This will never get called */
    UNIMPLEMENTED(alt_codec_dealloc_codec)
    return PJ_ENOTSUP;
}

static pj_status_t alt_codec_deinit(void)
{
    pjmedia_codec_mgr *codec_mgr;
    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);
    return pjmedia_codec_mgr_unregister_factory(codec_mgr,
                                                &alt_codec_factory.base);

}

static pjmedia_codec_factory_op alt_codec_factory_op =
{
    &alt_codec_test_alloc,
    &alt_codec_default_attr,
    &alt_codec_enum_codecs,
    &alt_codec_alloc_codec,
    &alt_codec_dealloc_codec,
    &alt_codec_deinit
};


/*****************************************************************************
 * API
 */

/* Initialize third party media library. */
pj_status_t pjsua_aud_subsys_init()
{
    pjmedia_codec_mgr *codec_mgr;
    pj_status_t status;

    /* Register our "dummy" codecs */
    alt_codec_factory.base.op = &alt_codec_factory_op;
    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);
    status = pjmedia_codec_mgr_register_factory(codec_mgr,
						&alt_codec_factory.base);
    if (status != PJ_SUCCESS)
	return status;

    /* TODO: initialize your evil library here */
    return PJ_SUCCESS;
}

/* Start (audio) media library. */
pj_status_t pjsua_aud_subsys_start(void)
{
    /* TODO: */
    return PJ_SUCCESS;
}

/* Cleanup and deinitialize third party media library. */
pj_status_t pjsua_aud_subsys_destroy()
{
    /* TODO: */
    return PJ_SUCCESS;
}

/* Our callback to receive incoming RTP packets */
static void aud_rtp_cb(void *user_data, void *pkt, pj_ssize_t size)
{
    pjsua_call_media *call_med = (pjsua_call_media*) user_data;

    /* TODO: Do something with the packet */
    PJ_LOG(4,(THIS_FILE, "RX %d bytes audio RTP packet", (int)size));
}

/* Our callback to receive RTCP packets */
static void aud_rtcp_cb(void *user_data, void *pkt, pj_ssize_t size)
{
    pjsua_call_media *call_med = (pjsua_call_media*) user_data;

    /* TODO: Do something with the packet here */
    PJ_LOG(4,(THIS_FILE, "RX %d bytes audio RTCP packet", (int)size));
}

/* A demo function to send dummy "RTP" packets periodically. You would not
 * need to have this function in the real app!
 */
static void timer_to_send_aud_rtp(void *user_data)
{
    pjsua_call_media *call_med = (pjsua_call_media*) user_data;
    const char *pkt = "Not RTP packet";

    if (call_med->call->inv == NULL) {
	/* Call has been disconnected. There is race condition here as
	 * this cb may be called sometime after call has been disconnected */
	return;
    }

    pjmedia_transport_send_rtp(call_med->tp, pkt, strlen(pkt));

    pjsua_schedule_timer2(&timer_to_send_aud_rtp, call_med, 2000);
}

static void timer_to_send_aud_rtcp(void *user_data)
{
    pjsua_call_media *call_med = (pjsua_call_media*) user_data;
    const char *pkt = "Not RTCP packet";

    if (call_med->call->inv == NULL) {
	/* Call has been disconnected. There is race condition here as
	 * this cb may be called sometime after call has been disconnected */
	return;
    }

    pjmedia_transport_send_rtcp(call_med->tp, pkt, strlen(pkt));

    pjsua_schedule_timer2(&timer_to_send_aud_rtcp, call_med, 5000);
}

/* Stop the audio stream of a call. */
void pjsua_aud_stop_stream(pjsua_call_media *call_med)
{
    /* Detach our RTP/RTCP callbacks from transport */
    pjmedia_transport_detach(call_med->tp, call_med);

    /* TODO: destroy your audio stream here */
}

/*
 * This function is called whenever SDP negotiation has completed
 * successfully. Here you'd want to start your audio stream
 * based on the info in the SDPs.
 */
pj_status_t pjsua_aud_channel_update(pjsua_call_media *call_med,
                                     pj_pool_t *tmp_pool,
                                     pjmedia_stream_info *si,
				     const pjmedia_sdp_session *local_sdp,
				     const pjmedia_sdp_session *remote_sdp)
{
    pj_status_t status = PJ_SUCCESS;

    PJ_LOG(4,(THIS_FILE,"Alt audio channel update.."));
    pj_log_push_indent();

    /* Check if no media is active */
    if (si->dir != PJMEDIA_DIR_NONE) {
	/* Attach our RTP and RTCP callbacks to the media transport */
	status = pjmedia_transport_attach(call_med->tp, call_med,
	                                  &si->rem_addr, &si->rem_rtcp,
	                                  pj_sockaddr_get_len(&si->rem_addr),
	                                  &aud_rtp_cb, &aud_rtcp_cb);

	/* For a demonstration, let's use a timer to send "RTP" packet
	 * periodically.
	 */
	pjsua_schedule_timer2(&timer_to_send_aud_rtp, call_med, 0);
	pjsua_schedule_timer2(&timer_to_send_aud_rtcp, call_med, 2500);

	/* TODO:
	 *   - Create and start your media stream based on the parameters
	 *     in si
	 */
    }

on_return:
    pj_log_pop_indent();
    return status;
}


/*****************************************************************************
 *
 * Call API which MAY need to be re-implemented if different backend is used.
 */

/* Check if call has an active media session. */
PJ_DEF(pj_bool_t) pjsua_call_has_media(pjsua_call_id call_id)
{
    UNIMPLEMENTED(pjsua_call_has_media)
    return PJ_TRUE;
}


/* Get the conference port identification associated with the call. */
PJ_DEF(pjsua_conf_port_id) pjsua_call_get_conf_port(pjsua_call_id call_id)
{
    UNIMPLEMENTED(pjsua_call_get_conf_port)
    return PJSUA_INVALID_ID;
}

/* Get media stream info for the specified media index. */
PJ_DEF(pj_status_t) pjsua_call_get_stream_info( pjsua_call_id call_id,
                                                unsigned med_idx,
                                                pjsua_stream_info *psi)
{
    pj_bzero(psi, sizeof(*psi));
    UNIMPLEMENTED(pjsua_call_get_stream_info)
    return PJ_ENOTSUP;
}

/* Get media stream statistic for the specified media index.  */
PJ_DEF(pj_status_t) pjsua_call_get_stream_stat( pjsua_call_id call_id,
                                                unsigned med_idx,
                                                pjsua_stream_stat *stat)
{
    pj_bzero(stat, sizeof(*stat));
    UNIMPLEMENTED(pjsua_call_get_stream_stat)
    return PJ_ENOTSUP;
}

/*
 * Send DTMF digits to remote using RFC 2833 payload formats.
 */
PJ_DEF(pj_status_t) pjsua_call_dial_dtmf( pjsua_call_id call_id,
					  const pj_str_t *digits)
{
    UNIMPLEMENTED(pjsua_call_dial_dtmf)
    return PJ_ENOTSUP;
}

/*****************************************************************************
 * Below are auxiliary API that we don't support (feel free to implement them
 * with the other media stack)
 */

/* Get maximum number of conference ports. */
PJ_DEF(unsigned) pjsua_conf_get_max_ports(void)
{
    UNIMPLEMENTED(pjsua_conf_get_max_ports)
    return 0xFF;
}

/* Get current number of active ports in the bridge. */
PJ_DEF(unsigned) pjsua_conf_get_active_ports(void)
{
    UNIMPLEMENTED(pjsua_conf_get_active_ports)
    return 0;
}

/* Enumerate all conference ports. */
PJ_DEF(pj_status_t) pjsua_enum_conf_ports(pjsua_conf_port_id id[],
					  unsigned *count)
{
    *count = 0;
    UNIMPLEMENTED(pjsua_enum_conf_ports)
    return PJ_ENOTSUP;
}

/* Get information about the specified conference port */
PJ_DEF(pj_status_t) pjsua_conf_get_port_info( pjsua_conf_port_id id,
					      pjsua_conf_port_info *info)
{
    UNIMPLEMENTED(pjsua_conf_get_port_info)
    return PJ_ENOTSUP;
}

/* Add arbitrary media port to PJSUA's conference bridge. */
PJ_DEF(pj_status_t) pjsua_conf_add_port( pj_pool_t *pool,
					 pjmedia_port *port,
					 pjsua_conf_port_id *p_id)
{
    *p_id = PJSUA_INVALID_ID;
    UNIMPLEMENTED(pjsua_conf_add_port)
    /* We should return PJ_ENOTSUP here, but this API is needed by pjsua
     * application or otherwise it will refuse to start.
     */
    return PJ_SUCCESS;
}

/* Remove arbitrary slot from the conference bridge. */
PJ_DEF(pj_status_t) pjsua_conf_remove_port(pjsua_conf_port_id id)
{
    UNIMPLEMENTED(pjsua_conf_remove_port)
    return PJ_ENOTSUP;
}

/* Establish unidirectional media flow from souce to sink. */
PJ_DEF(pj_status_t) pjsua_conf_connect( pjsua_conf_port_id source,
					pjsua_conf_port_id sink)
{
    UNIMPLEMENTED(pjsua_conf_connect)
    return PJ_ENOTSUP;
}

/* Disconnect media flow from the source to destination port. */
PJ_DEF(pj_status_t) pjsua_conf_disconnect( pjsua_conf_port_id source,
					   pjsua_conf_port_id sink)
{
    UNIMPLEMENTED(pjsua_conf_disconnect)
    return PJ_ENOTSUP;
}

/* Adjust the signal level to be transmitted from the bridge to the
 * specified port by making it louder or quieter.
 */
PJ_DEF(pj_status_t) pjsua_conf_adjust_tx_level(pjsua_conf_port_id slot,
					       float level)
{
    UNIMPLEMENTED(pjsua_conf_adjust_tx_level)
    return PJ_ENOTSUP;
}

/* Adjust the signal level to be received from the specified port (to
 * the bridge) by making it louder or quieter.
 */
PJ_DEF(pj_status_t) pjsua_conf_adjust_rx_level(pjsua_conf_port_id slot,
					       float level)
{
    UNIMPLEMENTED(pjsua_conf_adjust_rx_level)
    return PJ_ENOTSUP;
}


/* Get last signal level transmitted to or received from the specified port. */
PJ_DEF(pj_status_t) pjsua_conf_get_signal_level(pjsua_conf_port_id slot,
						unsigned *tx_level,
						unsigned *rx_level)
{
    UNIMPLEMENTED(pjsua_conf_get_signal_level)
    return PJ_ENOTSUP;
}

/* Create a file player, and automatically connect this player to
 * the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_player_create( const pj_str_t *filename,
					 unsigned options,
					 pjsua_player_id *p_id)
{
    UNIMPLEMENTED(pjsua_player_create)
    return PJ_ENOTSUP;
}

/* Create a file playlist media port, and automatically add the port
 * to the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_playlist_create( const pj_str_t file_names[],
					   unsigned file_count,
					   const pj_str_t *label,
					   unsigned options,
					   pjsua_player_id *p_id)
{
    UNIMPLEMENTED(pjsua_playlist_create)
    return PJ_ENOTSUP;
}

/* Get conference port ID associated with player. */
PJ_DEF(pjsua_conf_port_id) pjsua_player_get_conf_port(pjsua_player_id id)
{
    UNIMPLEMENTED(pjsua_player_get_conf_port)
    return -1;
}

/* Get the media port for the player. */
PJ_DEF(pj_status_t) pjsua_player_get_port( pjsua_player_id id,
					   pjmedia_port **p_port)
{
    UNIMPLEMENTED(pjsua_player_get_port)
    return PJ_ENOTSUP;
}

/* Set playback position. */
PJ_DEF(pj_status_t) pjsua_player_set_pos( pjsua_player_id id,
					  pj_uint32_t samples)
{
    UNIMPLEMENTED(pjsua_player_set_pos)
    return PJ_ENOTSUP;
}

/* Close the file, remove the player from the bridge, and free
 * resources associated with the file player.
 */
PJ_DEF(pj_status_t) pjsua_player_destroy(pjsua_player_id id)
{
    UNIMPLEMENTED(pjsua_player_destroy)
    return PJ_ENOTSUP;
}

/* Create a file recorder, and automatically connect this recorder to
 * the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_recorder_create( const pj_str_t *filename,
					   unsigned enc_type,
					   void *enc_param,
					   pj_ssize_t max_size,
					   unsigned options,
					   pjsua_recorder_id *p_id)
{
    UNIMPLEMENTED(pjsua_recorder_create)
    return PJ_ENOTSUP;
}


/* Get conference port associated with recorder. */
PJ_DEF(pjsua_conf_port_id) pjsua_recorder_get_conf_port(pjsua_recorder_id id)
{
    UNIMPLEMENTED(pjsua_recorder_get_conf_port)
    return -1;
}

/* Get the media port for the recorder. */
PJ_DEF(pj_status_t) pjsua_recorder_get_port( pjsua_recorder_id id,
					     pjmedia_port **p_port)
{
    UNIMPLEMENTED(pjsua_recorder_get_port)
    return PJ_ENOTSUP;
}

/* Destroy recorder (this will complete recording). */
PJ_DEF(pj_status_t) pjsua_recorder_destroy(pjsua_recorder_id id)
{
    UNIMPLEMENTED(pjsua_recorder_destroy)
    return PJ_ENOTSUP;
}

/* Enum sound devices. */
PJ_DEF(pj_status_t) pjsua_enum_aud_devs( pjmedia_aud_dev_info info[],
					 unsigned *count)
{
    UNIMPLEMENTED(pjsua_enum_aud_devs)
    return PJ_ENOTSUP;
}

PJ_DEF(pj_status_t) pjsua_enum_snd_devs( pjmedia_snd_dev_info info[],
					 unsigned *count)
{
    UNIMPLEMENTED(pjsua_enum_snd_devs)
    return PJ_ENOTSUP;
}

/* Select or change sound device. */
PJ_DEF(pj_status_t) pjsua_set_snd_dev( int capture_dev, int playback_dev)
{
    UNIMPLEMENTED(pjsua_set_snd_dev)
    return PJ_SUCCESS;
}

/* Get currently active sound devices. */
PJ_DEF(pj_status_t) pjsua_get_snd_dev(int *capture_dev, int *playback_dev)
{
    *capture_dev = *playback_dev = PJSUA_INVALID_ID;
    UNIMPLEMENTED(pjsua_get_snd_dev)
    return PJ_ENOTSUP;
}

/* Use null sound device. */
PJ_DEF(pj_status_t) pjsua_set_null_snd_dev(void)
{
    UNIMPLEMENTED(pjsua_set_null_snd_dev)
    return PJ_ENOTSUP;
}

/* Use no device! */
PJ_DEF(pjmedia_port*) pjsua_set_no_snd_dev(void)
{
    UNIMPLEMENTED(pjsua_set_no_snd_dev)
    return NULL;
}

/* Configure the AEC settings of the sound port. */
PJ_DEF(pj_status_t) pjsua_set_ec(unsigned tail_ms, unsigned options)
{
    UNIMPLEMENTED(pjsua_set_ec)
    return PJ_ENOTSUP;
}

/* Get current AEC tail length. */
PJ_DEF(pj_status_t) pjsua_get_ec_tail(unsigned *p_tail_ms)
{
    UNIMPLEMENTED(pjsua_get_ec_tail)
    return PJ_ENOTSUP;
}

/* Check whether the sound device is currently active. */
PJ_DEF(pj_bool_t) pjsua_snd_is_active(void)
{
    UNIMPLEMENTED(pjsua_snd_is_active)
    return PJ_FALSE;
}

/* Configure sound device setting to the sound device being used. */
PJ_DEF(pj_status_t) pjsua_snd_set_setting( pjmedia_aud_dev_cap cap,
					   const void *pval, pj_bool_t keep)
{
    UNIMPLEMENTED(pjsua_snd_set_setting)
    return PJ_ENOTSUP;
}

/* Retrieve a sound device setting. */
PJ_DEF(pj_status_t) pjsua_snd_get_setting(pjmedia_aud_dev_cap cap, void *pval)
{
    UNIMPLEMENTED(pjsua_snd_get_setting)
    return PJ_ENOTSUP;
}
