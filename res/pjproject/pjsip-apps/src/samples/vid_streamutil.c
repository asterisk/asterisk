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


/**
 * \page page_pjmedia_samples_vid_streamutil_c Samples: Video Streaming
 *
 * This example mainly demonstrates how to stream video to remote
 * peer using RTP.
 *
 * This file is pjsip-apps/src/samples/vid_streamutil.c
 *
 * \includelineno vid_streamutil.c
 */

#include <pjlib.h>
#include <pjlib-util.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>
#include <pjmedia/transport_srtp.h>


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#include <stdlib.h>	/* atoi() */
#include <stdio.h>

#include "util.h"


static const char *desc = 
 " vid_streamutil                                                       \n"
 "\n"
 " PURPOSE:                                                             \n"
 "  Demonstrate how to use pjmedia video stream component to		\n"
 "  transmit/receive RTP packets to/from video device/file.		\n"
 "\n"
 "\n"
 " USAGE:                                                               \n"
 "  vid_streamutil [options]                                            \n"
 "\n"
 "\n"
 " Options:                                                             \n"
 "  --codec=CODEC         Set the codec name.                           \n"
 "  --local-port=PORT     Set local RTP port (default=4000)             \n"
 "  --remote=IP:PORT      Set the remote peer. If this option is set,   \n"
 "                        the program will transmit RTP audio to the    \n"
 "                        specified address. (default: recv only)       \n"
 "  --play-file=AVI       Send video from the AVI file instead of from  \n"
 "                        the video device.                             \n"
 "  --send-recv           Set stream direction to bidirectional.        \n"
 "  --send-only           Set stream direction to send only             \n"
 "  --recv-only           Set stream direction to recv only (default)   \n"
 
 "  --send-width          Video width to be sent                        \n"
 "  --send-height         Video height to be sent                       \n"
 "                        --send-width and --send-height not applicable \n"
 "                        for file streaming (see --play-file)          \n"

 "  --send-pt             Payload type for sending                      \n"
 "  --recv-pt             Payload type for receiving                    \n"

#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
 "  --use-srtp[=NAME]     Enable SRTP with crypto suite NAME            \n"
 "                        e.g: AES_CM_128_HMAC_SHA1_80 (default),       \n"
 "                             AES_CM_128_HMAC_SHA1_32                  \n"
 "                        Use this option along with the TX & RX keys,  \n"
 "                        formated of 60 hex digits (e.g: E148DA..)     \n"
 "  --srtp-tx-key         SRTP key for transmiting                      \n"
 "  --srtp-rx-key         SRTP key for receiving                        \n"
#endif

 "\n"
;

#define THIS_FILE	"vid_streamutil.c"


/* If set, local renderer will be created to play original file */
#define HAS_LOCAL_RENDERER_FOR_PLAY_FILE    1


/* Default width and height for the renderer, better be set to maximum
 * acceptable size.
 */
#define DEF_RENDERER_WIDTH		    640
#define DEF_RENDERER_HEIGHT		    480


/* Prototype */
static void print_stream_stat(pjmedia_vid_stream *stream,
			      const pjmedia_vid_codec_param *codec_param);

/* Prototype for LIBSRTP utility in file datatypes.c */
int hex_string_to_octet_string(char *raw, char *hex, int len);

/* 
 * Register all codecs. 
 */
static pj_status_t init_codecs(pj_pool_factory *pf)
{
    pj_status_t status;

    /* To suppress warning about unused var when all codecs are disabled */
    PJ_UNUSED_ARG(status);

#if defined(PJMEDIA_HAS_FFMPEG_VID_CODEC) && PJMEDIA_HAS_FFMPEG_VID_CODEC != 0
    status = pjmedia_codec_ffmpeg_vid_init(NULL, pf);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);
#endif

    return PJ_SUCCESS;
}

/* 
 * Register all codecs. 
 */
static void deinit_codecs()
{
#if defined(PJMEDIA_HAS_FFMPEG_VID_CODEC) && PJMEDIA_HAS_FFMPEG_VID_CODEC != 0
    pjmedia_codec_ffmpeg_vid_deinit();
#endif
}

static pj_status_t create_file_player( pj_pool_t *pool,
				       const char *file_name,
				       pjmedia_port **p_play_port)
{
    pjmedia_avi_streams *avi_streams;
    pjmedia_avi_stream *vid_stream;
    pjmedia_port *play_port;
    pj_status_t status;

    status = pjmedia_avi_player_create_streams(pool, file_name, 0, &avi_streams);
    if (status != PJ_SUCCESS)
	return status;

    vid_stream = pjmedia_avi_streams_get_stream_by_media(avi_streams,
                                                         0,
                                                         PJMEDIA_TYPE_VIDEO);
    if (!vid_stream)
	return PJ_ENOTFOUND;

    play_port = pjmedia_avi_stream_get_port(vid_stream);
    pj_assert(play_port);

    *p_play_port = play_port;

    return PJ_SUCCESS;
}

/* 
 * Create stream based on the codec, dir, remote address, etc. 
 */
static pj_status_t create_stream( pj_pool_t *pool,
				  pjmedia_endpt *med_endpt,
				  const pjmedia_vid_codec_info *codec_info,
                                  pjmedia_vid_codec_param *codec_param,
				  pjmedia_dir dir,
				  pj_int8_t rx_pt,
				  pj_int8_t tx_pt,
				  pj_uint16_t local_port,
				  const pj_sockaddr_in *rem_addr,
#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
				  pj_bool_t use_srtp,
				  const pj_str_t *crypto_suite,
				  const pj_str_t *srtp_tx_key,
				  const pj_str_t *srtp_rx_key,
#endif
				  pjmedia_vid_stream **p_stream )
{
    pjmedia_vid_stream_info info;
    pjmedia_transport *transport = NULL;
    pj_status_t status;
#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    pjmedia_transport *srtp_tp = NULL;
#endif

    /* Reset stream info. */
    pj_bzero(&info, sizeof(info));

    /* Initialize stream info formats */
    info.type = PJMEDIA_TYPE_VIDEO;
    info.dir = dir;
    info.codec_info = *codec_info;
    info.tx_pt = (tx_pt == -1)? codec_info->pt : tx_pt;
    info.rx_pt = (rx_pt == -1)? codec_info->pt : rx_pt;
    info.ssrc = pj_rand();
    if (codec_param)
        info.codec_param = codec_param;
    
    /* Copy remote address */
    pj_memcpy(&info.rem_addr, rem_addr, sizeof(pj_sockaddr_in));

    /* If remote address is not set, set to an arbitrary address
     * (otherwise stream will assert).
     */
    if (info.rem_addr.addr.sa_family == 0) {
	const pj_str_t addr = pj_str("127.0.0.1");
	pj_sockaddr_in_init(&info.rem_addr.ipv4, &addr, 0);
    }

    /* Create media transport */
    status = pjmedia_transport_udp_create(med_endpt, NULL, local_port,
					  0, &transport);
    if (status != PJ_SUCCESS)
	return status;

#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    /* Check if SRTP enabled */
    if (use_srtp) {
	pjmedia_srtp_crypto tx_plc, rx_plc;

	status = pjmedia_transport_srtp_create(med_endpt, transport, 
					       NULL, &srtp_tp);
	if (status != PJ_SUCCESS)
	    return status;

	pj_bzero(&tx_plc, sizeof(pjmedia_srtp_crypto));
	pj_bzero(&rx_plc, sizeof(pjmedia_srtp_crypto));

	tx_plc.key = *srtp_tx_key;
	tx_plc.name = *crypto_suite;
	rx_plc.key = *srtp_rx_key;
	rx_plc.name = *crypto_suite;
	
	status = pjmedia_transport_srtp_start(srtp_tp, &tx_plc, &rx_plc);
	if (status != PJ_SUCCESS)
	    return status;

	transport = srtp_tp;
    }
#endif

    /* Now that the stream info is initialized, we can create the 
     * stream.
     */

    status = pjmedia_vid_stream_create( med_endpt, pool, &info, 
					transport, 
					NULL, p_stream);

    if (status != PJ_SUCCESS) {
	app_perror(THIS_FILE, "Error creating stream", status);
	pjmedia_transport_close(transport);
	return status;
    }


    return PJ_SUCCESS;
}


typedef struct play_file_data
{
    const char *file_name;
    pjmedia_port *play_port;
    pjmedia_port *stream_port;
    pjmedia_vid_codec *decoder;
    pjmedia_port *renderer;
    void *read_buf;
    pj_size_t read_buf_size;
    void *dec_buf;
    pj_size_t dec_buf_size;
} play_file_data;


static void clock_cb(const pj_timestamp *ts, void *user_data)
{
    play_file_data *play_file = (play_file_data*)user_data;
    pjmedia_frame read_frame, write_frame;
    pj_status_t status;

    PJ_UNUSED_ARG(ts);

    /* Read frame from file */
    read_frame.buf = play_file->read_buf;
    read_frame.size = play_file->read_buf_size;
    pjmedia_port_get_frame(play_file->play_port, &read_frame);

    /* Decode frame, if needed */
    if (play_file->decoder) {
	pjmedia_vid_codec *decoder = play_file->decoder;

	write_frame.buf = play_file->dec_buf;
	write_frame.size = play_file->dec_buf_size;
	status = pjmedia_vid_codec_decode(decoder, 1, &read_frame,
	                                  write_frame.size, &write_frame);
	if (status != PJ_SUCCESS)
	    return;
    } else {
	write_frame = read_frame;
    }

    /* Display frame locally */
    if (play_file->renderer)
	pjmedia_port_put_frame(play_file->renderer, &write_frame);

    /* Send frame */
    pjmedia_port_put_frame(play_file->stream_port, &write_frame);
}


/*
 * usage()
 */
static void usage()
{
    puts(desc);
}

/*
 * main()
 */
int main(int argc, char *argv[])
{
    pj_caching_pool cp;
    pjmedia_endpt *med_endpt;
    pj_pool_t *pool;
    pjmedia_vid_stream *stream = NULL;
    pjmedia_port *enc_port, *dec_port;
    pj_status_t status; 

    pjmedia_vid_port *capture=NULL, *renderer=NULL;
    pjmedia_vid_port_param vpp;

#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    /* SRTP variables */
    pj_bool_t use_srtp = PJ_FALSE;
    char tmp_tx_key[64];
    char tmp_rx_key[64];
    pj_str_t  srtp_tx_key = {NULL, 0};
    pj_str_t  srtp_rx_key = {NULL, 0};
    pj_str_t  srtp_crypto_suite = {NULL, 0};
    int	tmp_key_len;
#endif

    /* Default values */
    const pjmedia_vid_codec_info *codec_info;
    pjmedia_vid_codec_param codec_param;
    pjmedia_dir dir = PJMEDIA_DIR_DECODING;
    pj_sockaddr_in remote_addr;
    pj_uint16_t local_port = 4000;
    char *codec_id = NULL;
    pjmedia_rect_size tx_size = {0};
    pj_int8_t rx_pt = -1, tx_pt = -1;

    play_file_data play_file = { NULL };
    pjmedia_port *play_port = NULL;
    pjmedia_vid_codec *play_decoder = NULL;
    pjmedia_clock *play_clock = NULL;

    enum {
	OPT_CODEC	= 'c',
	OPT_LOCAL_PORT	= 'p',
	OPT_REMOTE	= 'r',
	OPT_PLAY_FILE	= 'f',
	OPT_SEND_RECV	= 'b',
	OPT_SEND_ONLY	= 's',
	OPT_RECV_ONLY	= 'i',
	OPT_SEND_WIDTH	= 'W',
	OPT_SEND_HEIGHT	= 'H',
	OPT_RECV_PT	= 't',
	OPT_SEND_PT	= 'T',
#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
	OPT_USE_SRTP	= 'S',
#endif
	OPT_SRTP_TX_KEY	= 'x',
	OPT_SRTP_RX_KEY	= 'y',
	OPT_HELP	= 'h',
    };

    struct pj_getopt_option long_options[] = {
	{ "codec",	    1, 0, OPT_CODEC },
	{ "local-port",	    1, 0, OPT_LOCAL_PORT },
	{ "remote",	    1, 0, OPT_REMOTE },
	{ "play-file",	    1, 0, OPT_PLAY_FILE },
	{ "send-recv",      0, 0, OPT_SEND_RECV },
	{ "send-only",      0, 0, OPT_SEND_ONLY },
	{ "recv-only",      0, 0, OPT_RECV_ONLY },
	{ "send-width",     1, 0, OPT_SEND_WIDTH },
	{ "send-height",    1, 0, OPT_SEND_HEIGHT },
	{ "recv-pt",        1, 0, OPT_RECV_PT },
	{ "send-pt",        1, 0, OPT_SEND_PT },
#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
	{ "use-srtp",	    2, 0, OPT_USE_SRTP },
	{ "srtp-tx-key",    1, 0, OPT_SRTP_TX_KEY },
	{ "srtp-rx-key",    1, 0, OPT_SRTP_RX_KEY },
#endif
	{ "help",	    0, 0, OPT_HELP },
	{ NULL, 0, 0, 0 },
    };

    int c;
    int option_index;


    pj_bzero(&remote_addr, sizeof(remote_addr));


    /* init PJLIB : */
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);


    /* Parse arguments */
    pj_optind = 0;
    while((c=pj_getopt_long(argc,argv, "h", long_options, &option_index))!=-1)
    {
	switch (c) {
	case OPT_CODEC:
	    codec_id = pj_optarg;
	    break;

	case OPT_LOCAL_PORT:
	    local_port = (pj_uint16_t) atoi(pj_optarg);
	    if (local_port < 1) {
		printf("Error: invalid local port %s\n", pj_optarg);
		return 1;
	    }
	    break;

	case OPT_REMOTE:
	    {
		pj_str_t ip = pj_str(strtok(pj_optarg, ":"));
		pj_uint16_t port = (pj_uint16_t) atoi(strtok(NULL, ":"));

		status = pj_sockaddr_in_init(&remote_addr, &ip, port);
		if (status != PJ_SUCCESS) {
		    app_perror(THIS_FILE, "Invalid remote address", status);
		    return 1;
		}
	    }
	    break;

	case OPT_PLAY_FILE:
	    play_file.file_name = pj_optarg;
	    break;

	case OPT_SEND_RECV:
	    dir = PJMEDIA_DIR_ENCODING_DECODING;
	    break;

	case OPT_SEND_ONLY:
	    dir = PJMEDIA_DIR_ENCODING;
	    break;

	case OPT_RECV_ONLY:
	    dir = PJMEDIA_DIR_DECODING;
	    break;

	case OPT_SEND_WIDTH:
	    tx_size.w = (unsigned)atoi(pj_optarg);
	    break;

	case OPT_SEND_HEIGHT:
	    tx_size.h = (unsigned)atoi(pj_optarg);
	    break;

	case OPT_RECV_PT:
	    rx_pt = (pj_int8_t)atoi(pj_optarg);
	    break;

	case OPT_SEND_PT:
	    tx_pt = (pj_int8_t)atoi(pj_optarg);
	    break;

#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
	case OPT_USE_SRTP:
	    use_srtp = PJ_TRUE;
	    if (pj_optarg) {
		pj_strset(&srtp_crypto_suite, pj_optarg, strlen(pj_optarg));
	    } else {
		srtp_crypto_suite = pj_str("AES_CM_128_HMAC_SHA1_80");
	    }
	    break;

	case OPT_SRTP_TX_KEY:
	    tmp_key_len = hex_string_to_octet_string(tmp_tx_key, pj_optarg, 
						     strlen(pj_optarg));
	    pj_strset(&srtp_tx_key, tmp_tx_key, tmp_key_len/2);
	    break;

	case OPT_SRTP_RX_KEY:
	    tmp_key_len = hex_string_to_octet_string(tmp_rx_key, pj_optarg,
						     strlen(pj_optarg));
	    pj_strset(&srtp_rx_key, tmp_rx_key, tmp_key_len/2);
	    break;
#endif

	case OPT_HELP:
	    usage();
	    return 1;

	default:
	    printf("Invalid options %s\n", argv[pj_optind]);
	    return 1;
	}

    }


    /* Verify arguments. */
    if (dir & PJMEDIA_DIR_ENCODING) {
	if (remote_addr.sin_addr.s_addr == 0) {
	    printf("Error: remote address must be set\n");
	    return 1;
	}
    }

    if (play_file.file_name != NULL && dir != PJMEDIA_DIR_ENCODING) {
	printf("Direction is set to --send-only because of --play-file\n");
	dir = PJMEDIA_DIR_ENCODING;
    }

#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    /* SRTP validation */
    if (use_srtp) {
	if (!srtp_tx_key.slen || !srtp_rx_key.slen)
	{
	    printf("Error: Key for each SRTP stream direction must be set\n");
	    return 1;
	}
    }
#endif

    /* Must create a pool factory before we can allocate any memory. */
    pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

    /* 
     * Initialize media endpoint.
     * This will implicitly initialize PJMEDIA too.
     */
    status = pjmedia_endpt_create(&cp.factory, NULL, 1, &med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    /* Create memory pool for application purpose */
    pool = pj_pool_create( &cp.factory,	    /* pool factory	    */
			   "app",	    /* pool name.	    */
			   4000,	    /* init size	    */
			   4000,	    /* increment size	    */
			   NULL		    /* callback on error    */
			   );

    /* Init video format manager */
    pjmedia_video_format_mgr_create(pool, 64, 0, NULL);

    /* Init video converter manager */
    pjmedia_converter_mgr_create(pool, NULL);

    /* Init event manager */
    pjmedia_event_mgr_create(pool, 0, NULL);

    /* Init video codec manager */
    pjmedia_vid_codec_mgr_create(pool, NULL);

    /* Init video subsystem */
    pjmedia_vid_dev_subsys_init(&cp.factory);

    /* Register all supported codecs */
    status = init_codecs(&cp.factory);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);


    /* Find which codec to use. */
    if (codec_id) {
	unsigned count = 1;
	pj_str_t str_codec_id = pj_str(codec_id);

        status = pjmedia_vid_codec_mgr_find_codecs_by_id(NULL,
						         &str_codec_id, &count,
						         &codec_info, NULL);
	if (status != PJ_SUCCESS) {
	    printf("Error: unable to find codec %s\n", codec_id);
	    return 1;
	}
    } else {
        static pjmedia_vid_codec_info info[1];
        unsigned count = PJ_ARRAY_SIZE(info);

	/* Default to first codec */
	pjmedia_vid_codec_mgr_enum_codecs(NULL, &count, info, NULL);
        codec_info = &info[0];
    }

    /* Get codec default param for info */
    status = pjmedia_vid_codec_mgr_get_default_param(NULL, codec_info, 
				                     &codec_param);
    pj_assert(status == PJ_SUCCESS);
    
    /* Set outgoing video size */
    if (tx_size.w && tx_size.h)
        codec_param.enc_fmt.det.vid.size = tx_size;

#if DEF_RENDERER_WIDTH && DEF_RENDERER_HEIGHT
    /* Set incoming video size */
    if (DEF_RENDERER_WIDTH > codec_param.dec_fmt.det.vid.size.w)
	codec_param.dec_fmt.det.vid.size.w = DEF_RENDERER_WIDTH;
    if (DEF_RENDERER_HEIGHT > codec_param.dec_fmt.det.vid.size.h)
	codec_param.dec_fmt.det.vid.size.h = DEF_RENDERER_HEIGHT;
#endif

    if (play_file.file_name) {
	pjmedia_video_format_detail *file_vfd;
        pjmedia_clock_param clock_param;
        char fmt_name[5];

	/* Create file player */
	status = create_file_player(pool, play_file.file_name, &play_port);
	if (status != PJ_SUCCESS)
	    goto on_exit;

	/* Collect format info */
	file_vfd = pjmedia_format_get_video_format_detail(&play_port->info.fmt,
							  PJ_TRUE);
	PJ_LOG(2, (THIS_FILE, "Reading video stream %dx%d %s @%.2ffps",
		   file_vfd->size.w, file_vfd->size.h,
		   pjmedia_fourcc_name(play_port->info.fmt.id, fmt_name),
		   (1.0*file_vfd->fps.num/file_vfd->fps.denum)));

	/* Allocate file read buffer */
	play_file.read_buf_size = PJMEDIA_MAX_VIDEO_ENC_FRAME_SIZE;
	play_file.read_buf = pj_pool_zalloc(pool, play_file.read_buf_size);

	/* Create decoder, if the file and the stream uses different codec */
	if (codec_info->fmt_id != (pjmedia_format_id)play_port->info.fmt.id) {
	    const pjmedia_video_format_info *dec_vfi;
	    pjmedia_video_apply_fmt_param dec_vafp = {0};
	    const pjmedia_vid_codec_info *codec_info2;
	    pjmedia_vid_codec_param codec_param2;

	    /* Find decoder */
	    status = pjmedia_vid_codec_mgr_get_codec_info2(NULL,
							   play_port->info.fmt.id,
							   &codec_info2);
	    if (status != PJ_SUCCESS)
		goto on_exit;

	    /* Init decoder */
	    status = pjmedia_vid_codec_mgr_alloc_codec(NULL, codec_info2,
						       &play_decoder);
	    if (status != PJ_SUCCESS)
		goto on_exit;

	    status = play_decoder->op->init(play_decoder, pool);
	    if (status != PJ_SUCCESS)
		goto on_exit;

	    /* Open decoder */
	    status = pjmedia_vid_codec_mgr_get_default_param(NULL, codec_info2,
							     &codec_param2);
	    if (status != PJ_SUCCESS)
		goto on_exit;

	    codec_param2.dir = PJMEDIA_DIR_DECODING;
	    status = play_decoder->op->open(play_decoder, &codec_param2);
	    if (status != PJ_SUCCESS)
		goto on_exit;

	    /* Get decoder format info and apply param */
	    dec_vfi = pjmedia_get_video_format_info(NULL,
						    codec_info2->dec_fmt_id[0]);
	    if (!dec_vfi || !dec_vfi->apply_fmt) {
		status = PJ_ENOTSUP;
		goto on_exit;
	    }
	    dec_vafp.size = file_vfd->size;
	    (*dec_vfi->apply_fmt)(dec_vfi, &dec_vafp);

	    /* Allocate buffer to receive decoder output */
	    play_file.dec_buf_size = dec_vafp.framebytes;
	    play_file.dec_buf = pj_pool_zalloc(pool, play_file.dec_buf_size);
	}

	/* Create player clock */
        clock_param.usec_interval = PJMEDIA_PTIME(&file_vfd->fps);
        clock_param.clock_rate = codec_info->clock_rate;
	status = pjmedia_clock_create2(pool, &clock_param,
				       PJMEDIA_CLOCK_NO_HIGHEST_PRIO,
				       &clock_cb, &play_file, &play_clock);
	if (status != PJ_SUCCESS)
	    goto on_exit;

	/* Override stream codec param for encoding direction */
	codec_param.enc_fmt.det.vid.size = file_vfd->size;
	codec_param.enc_fmt.det.vid.fps  = file_vfd->fps;

    } else {
        pjmedia_vid_port_param_default(&vpp);

        /* Set as active for all video devices */
        vpp.active = PJ_TRUE;

	/* Create video device port. */
        if (dir & PJMEDIA_DIR_ENCODING) {
            /* Create capture */
            status = pjmedia_vid_dev_default_param(
					pool,
					PJMEDIA_VID_DEFAULT_CAPTURE_DEV,
					&vpp.vidparam);
            if (status != PJ_SUCCESS)
	        goto on_exit;

            pjmedia_format_copy(&vpp.vidparam.fmt, &codec_param.enc_fmt);
	    vpp.vidparam.fmt.id = codec_param.dec_fmt.id;
            vpp.vidparam.dir = PJMEDIA_DIR_CAPTURE;
            
            status = pjmedia_vid_port_create(pool, &vpp, &capture);
            if (status != PJ_SUCCESS)
	        goto on_exit;
        }
	
        if (dir & PJMEDIA_DIR_DECODING) {
            /* Create renderer */
            status = pjmedia_vid_dev_default_param(
					pool,
					PJMEDIA_VID_DEFAULT_RENDER_DEV,
					&vpp.vidparam);
            if (status != PJ_SUCCESS)
	        goto on_exit;

            pjmedia_format_copy(&vpp.vidparam.fmt, &codec_param.dec_fmt);
            vpp.vidparam.dir = PJMEDIA_DIR_RENDER;
            vpp.vidparam.disp_size = vpp.vidparam.fmt.det.vid.size;
	    vpp.vidparam.flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS;
	    vpp.vidparam.window_flags = PJMEDIA_VID_DEV_WND_BORDER |
					PJMEDIA_VID_DEV_WND_RESIZABLE;

            status = pjmedia_vid_port_create(pool, &vpp, &renderer);
            if (status != PJ_SUCCESS)
	        goto on_exit;
        }
    }

    /* Set to ignore fmtp */
    codec_param.ignore_fmtp = PJ_TRUE;

    /* Create stream based on program arguments */
    status = create_stream(pool, med_endpt, codec_info, &codec_param,
                           dir, rx_pt, tx_pt, local_port, &remote_addr, 
#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
			   use_srtp, &srtp_crypto_suite, 
			   &srtp_tx_key, &srtp_rx_key,
#endif
			   &stream);
    if (status != PJ_SUCCESS)
	goto on_exit;

    /* Get the port interface of the stream */
    status = pjmedia_vid_stream_get_port(stream, PJMEDIA_DIR_ENCODING,
				         &enc_port);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    status = pjmedia_vid_stream_get_port(stream, PJMEDIA_DIR_DECODING,
				         &dec_port);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    /* Start streaming */
    status = pjmedia_vid_stream_start(stream);
    if (status != PJ_SUCCESS)
        goto on_exit;

    /* Start renderer */
    if (renderer) {
        status = pjmedia_vid_port_connect(renderer, dec_port, PJ_FALSE);
        if (status != PJ_SUCCESS)
	    goto on_exit;
        status = pjmedia_vid_port_start(renderer);
        if (status != PJ_SUCCESS)
            goto on_exit;
    }

    /* Start capture */
    if (capture) {
        status = pjmedia_vid_port_connect(capture, enc_port, PJ_FALSE);
        if (status != PJ_SUCCESS)
	    goto on_exit;
        status = pjmedia_vid_port_start(capture);
        if (status != PJ_SUCCESS)
            goto on_exit;
    }

    /* Start playing file */
    if (play_file.file_name) {

#if HAS_LOCAL_RENDERER_FOR_PLAY_FILE
        /* Create local renderer */
        pjmedia_vid_port_param_default(&vpp);
        vpp.active = PJ_FALSE;
        status = pjmedia_vid_dev_default_param(
				pool,
				PJMEDIA_VID_DEFAULT_RENDER_DEV,
				&vpp.vidparam);
        if (status != PJ_SUCCESS)
	    goto on_exit;

        vpp.vidparam.dir = PJMEDIA_DIR_RENDER;
        pjmedia_format_copy(&vpp.vidparam.fmt, &codec_param.dec_fmt);
	vpp.vidparam.fmt.det.vid.size = play_port->info.fmt.det.vid.size;
	vpp.vidparam.fmt.det.vid.fps = play_port->info.fmt.det.vid.fps;
        vpp.vidparam.disp_size = vpp.vidparam.fmt.det.vid.size;
	vpp.vidparam.flags |= PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW_FLAGS;
	vpp.vidparam.window_flags = PJMEDIA_VID_DEV_WND_BORDER |
				    PJMEDIA_VID_DEV_WND_RESIZABLE;

	status = pjmedia_vid_port_create(pool, &vpp, &renderer);
        if (status != PJ_SUCCESS)
	    goto on_exit;
        status = pjmedia_vid_port_start(renderer);
        if (status != PJ_SUCCESS)
            goto on_exit;
#endif

	/* Init play file data */
	play_file.play_port = play_port;
	play_file.stream_port = enc_port;
	play_file.decoder = play_decoder;
	if (renderer) {
	    play_file.renderer = pjmedia_vid_port_get_passive_port(renderer);
	}

	status = pjmedia_clock_start(play_clock);
	if (status != PJ_SUCCESS)
	    goto on_exit;
    }

    /* Done */

    if (dir == PJMEDIA_DIR_DECODING)
	printf("Stream is active, dir is recv-only, local port is %d\n",
	       local_port);
    else if (dir == PJMEDIA_DIR_ENCODING)
	printf("Stream is active, dir is send-only, sending to %s:%d\n",
	       pj_inet_ntoa(remote_addr.sin_addr),
	       pj_ntohs(remote_addr.sin_port));
    else
	printf("Stream is active, send/recv, local port is %d, "
	       "sending to %s:%d\n",
	       local_port,
	       pj_inet_ntoa(remote_addr.sin_addr),
	       pj_ntohs(remote_addr.sin_port));

    if (dir & PJMEDIA_DIR_ENCODING)
	PJ_LOG(2, (THIS_FILE, "Sending %dx%d %.*s @%.2ffps",
		   codec_param.enc_fmt.det.vid.size.w,
		   codec_param.enc_fmt.det.vid.size.h,
		   codec_info->encoding_name.slen,
		   codec_info->encoding_name.ptr,
		   (1.0*codec_param.enc_fmt.det.vid.fps.num/
		    codec_param.enc_fmt.det.vid.fps.denum)));

    for (;;) {
	char tmp[10];

	puts("");
	puts("Commands:");
	puts("  q     Quit");
	puts("");

	printf("Command: "); fflush(stdout);

	if (fgets(tmp, sizeof(tmp), stdin) == NULL) {
	    puts("EOF while reading stdin, will quit now..");
	    break;
	}

	if (tmp[0] == 'q')
	    break;

    }



    /* Start deinitialization: */
on_exit:

    /* Stop video devices */
    if (capture)
        pjmedia_vid_port_stop(capture);
    if (renderer)
        pjmedia_vid_port_stop(renderer);

    /* Stop and destroy file clock */
    if (play_clock) {
	pjmedia_clock_stop(play_clock);
	pjmedia_clock_destroy(play_clock);
    }

    /* Destroy file reader/player */
    if (play_port)
	pjmedia_port_destroy(play_port);

    /* Destroy file decoder */
    if (play_decoder) {
	play_decoder->op->close(play_decoder);
	pjmedia_vid_codec_mgr_dealloc_codec(NULL, play_decoder);
    }

    /* Destroy video devices */
    if (capture)
	pjmedia_vid_port_destroy(capture);
    if (renderer)
	pjmedia_vid_port_destroy(renderer);

    /* Destroy stream */
    if (stream) {
	pjmedia_transport *tp;

	tp = pjmedia_vid_stream_get_transport(stream);
	pjmedia_vid_stream_destroy(stream);
	
	pjmedia_transport_close(tp);
    }

    /* Deinit codecs */
    deinit_codecs();

    /* Shutdown video subsystem */
    pjmedia_vid_dev_subsys_shutdown();

    /* Destroy event manager */
    pjmedia_event_mgr_destroy(NULL);

    /* Release application pool */
    pj_pool_release( pool );

    /* Destroy media endpoint. */
    pjmedia_endpt_destroy( med_endpt );

    /* Destroy pool factory */
    pj_caching_pool_destroy( &cp );

    /* Shutdown PJLIB */
    pj_shutdown();

    return (status == PJ_SUCCESS) ? 0 : 1;
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
