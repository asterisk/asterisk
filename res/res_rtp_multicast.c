/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
 * Andreas 'MacBrody' Brodmann <andreas.brodmann@gmail.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 *
 * \brief Multicast RTP Engine
 *
 * \author Joshua Colp <jcolp@digium.com>
 * \author Andreas 'MacBrody' Brodmann <andreas.brodmann@gmail.com>
 *
 * \ingroup rtp_engines
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <math.h>

#include "asterisk/pbx.h"
#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/acl.h"
#include "asterisk/config.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/unaligned.h"
#include "asterisk/module.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/format_cache.h"

/*! Command value used for Linksys paging to indicate we are starting */
#define LINKSYS_MCAST_STARTCMD 6

/*! Command value used for Linksys paging to indicate we are stopping */
#define LINKSYS_MCAST_STOPCMD 7

/*! \brief Type of paging to do */
enum multicast_type {
	/*! Simple multicast enabled client/receiver paging like Snom and Barix uses */
	MULTICAST_TYPE_BASIC = 0,
	/*! More advanced Linksys type paging which requires a start and stop packet */
	MULTICAST_TYPE_LINKSYS,
};

/*! \brief Structure for a Linksys control packet */
struct multicast_control_packet {
	/*! Unique identifier for the control packet */
	uint32_t unique_id;
	/*! Actual command in the control packet */
	uint32_t command;
	/*! IP address for the RTP */
	uint32_t ip;
	/*! Port for the RTP */
	uint32_t port;
};

/*! \brief Structure for a multicast paging instance */
struct multicast_rtp {
	/*! TYpe of multicast paging this instance is doing */
	enum multicast_type type;
	/*! Socket used for sending the audio on */
	int socket;
	/*! Synchronization source value, used when creating/sending the RTP packet */
	unsigned int ssrc;
	/*! Sequence number, used when creating/sending the RTP packet */
	uint16_t seqno;
	unsigned int lastts;	
	struct timeval txcore;
};

/* Forward Declarations */
static int multicast_rtp_new(struct ast_rtp_instance *instance, struct ast_sched_context *sched, struct ast_sockaddr *addr, void *data);
static int multicast_rtp_activate(struct ast_rtp_instance *instance);
static int multicast_rtp_destroy(struct ast_rtp_instance *instance);
static int multicast_rtp_write(struct ast_rtp_instance *instance, struct ast_frame *frame);
static struct ast_frame *multicast_rtp_read(struct ast_rtp_instance *instance, int rtcp);

/* RTP Engine Declaration */
static struct ast_rtp_engine multicast_rtp_engine = {
	.name = "multicast",
	.new = multicast_rtp_new,
	.activate = multicast_rtp_activate,
	.destroy = multicast_rtp_destroy,
	.write = multicast_rtp_write,
	.read = multicast_rtp_read,
};

/*! \brief Function called to create a new multicast instance */
static int multicast_rtp_new(struct ast_rtp_instance *instance, struct ast_sched_context *sched, struct ast_sockaddr *addr, void *data)
{
	struct multicast_rtp *multicast;
	const char *type = data;

	if (!(multicast = ast_calloc(1, sizeof(*multicast)))) {
		return -1;
	}

	if (!strcasecmp(type, "basic")) {
		multicast->type = MULTICAST_TYPE_BASIC;
	} else if (!strcasecmp(type, "linksys")) {
		multicast->type = MULTICAST_TYPE_LINKSYS;
	} else {
		ast_free(multicast);
		return -1;
	}

	if ((multicast->socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		ast_free(multicast);
		return -1;
	}

	multicast->ssrc = ast_random();

	ast_rtp_instance_set_data(instance, multicast);

	return 0;
}

static int rtp_get_rate(struct ast_format *format)
{
	return ast_format_cmp(format, ast_format_g722) == AST_FORMAT_CMP_EQUAL ?
		8000 : ast_format_get_sample_rate(format);
}

static unsigned int calc_txstamp(struct multicast_rtp *rtp, struct timeval *delivery)
{
        struct timeval t;
        long ms;

        if (ast_tvzero(rtp->txcore)) {
                rtp->txcore = ast_tvnow();
                rtp->txcore.tv_usec -= rtp->txcore.tv_usec % 20000;
        }

        t = (delivery && !ast_tvzero(*delivery)) ? *delivery : ast_tvnow();
        if ((ms = ast_tvdiff_ms(t, rtp->txcore)) < 0) {
                ms = 0;
        }
        rtp->txcore = t;

        return (unsigned int) ms;
}

/*! \brief Helper function which populates a control packet with useful information and sends it */
static int multicast_send_control_packet(struct ast_rtp_instance *instance, struct multicast_rtp *multicast, int command)
{
	struct multicast_control_packet control_packet = { .unique_id = htonl((u_long)time(NULL)),
							   .command = htonl(command),
	};
	struct ast_sockaddr control_address, remote_address;

	ast_rtp_instance_get_local_address(instance, &control_address);
	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* Ensure the user of us have given us both the control address and destination address */
	if (ast_sockaddr_isnull(&control_address) ||
	    ast_sockaddr_isnull(&remote_address)) {
		return -1;
	}

	/* The protocol only supports IPv4. */
	if (ast_sockaddr_is_ipv6(&remote_address)) {
		ast_log(LOG_WARNING, "Cannot send control packet for IPv6 "
			"remote address.\n");
		return -1;
	}

	control_packet.ip = htonl(ast_sockaddr_ipv4(&remote_address));
	control_packet.port = htonl(ast_sockaddr_port(&remote_address));

	/* Based on a recommendation by Brian West who did the FreeSWITCH implementation we send control packets twice */
	ast_sendto(multicast->socket, &control_packet, sizeof(control_packet), 0, &control_address);
	ast_sendto(multicast->socket, &control_packet, sizeof(control_packet), 0, &control_address);

	return 0;
}

/*! \brief Function called to indicate that audio is now going to flow */
static int multicast_rtp_activate(struct ast_rtp_instance *instance)
{
	struct multicast_rtp *multicast = ast_rtp_instance_get_data(instance);

	if (multicast->type != MULTICAST_TYPE_LINKSYS) {
		return 0;
	}

	return multicast_send_control_packet(instance, multicast, LINKSYS_MCAST_STARTCMD);
}

/*! \brief Function called to destroy a multicast instance */
static int multicast_rtp_destroy(struct ast_rtp_instance *instance)
{
	struct multicast_rtp *multicast = ast_rtp_instance_get_data(instance);

	if (multicast->type == MULTICAST_TYPE_LINKSYS) {
		multicast_send_control_packet(instance, multicast, LINKSYS_MCAST_STOPCMD);
	}

	close(multicast->socket);

	ast_free(multicast);

	return 0;
}

/*! \brief Function called to broadcast some audio on a multicast instance */
static int multicast_rtp_write(struct ast_rtp_instance *instance, struct ast_frame *frame)
{
	struct multicast_rtp *multicast = ast_rtp_instance_get_data(instance);
	struct ast_frame *f = frame;
	struct ast_sockaddr remote_address;
	int hdrlen = 12, res = 0, codec;
	unsigned char *rtpheader;
	unsigned int ms = calc_txstamp(multicast, &frame->delivery);
	int rate = rtp_get_rate(frame->subclass.format) / 1000;

	/* We only accept audio, nothing else */
	if (frame->frametype != AST_FRAME_VOICE) {
		return 0;
	}

	/* Grab the actual payload number for when we create the RTP packet */
	if ((codec = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(instance), 1, frame->subclass.format, 0)) < 0) {
		return -1;
	}

	/* If we do not have space to construct an RTP header duplicate the frame so we get some */
	if (frame->offset < hdrlen) {
		f = ast_frdup(frame);
	}
	
	/* Calucate last TS */
	multicast->lastts = multicast->lastts + ms * rate;
	
	/* Construct an RTP header for our packet */
	rtpheader = (unsigned char *)(f->data.ptr - hdrlen);
	put_unaligned_uint32(rtpheader, htonl((2 << 30) | (codec << 16) | (multicast->seqno)));
	
	if (ast_test_flag(f, AST_FRFLAG_HAS_TIMING_INFO)) {
		put_unaligned_uint32(rtpheader + 4, htonl(f->ts * 8));
	} else {
		put_unaligned_uint32(rtpheader + 4, htonl(multicast->lastts));
	}

	put_unaligned_uint32(rtpheader + 8, htonl(multicast->ssrc));

	/* Increment sequence number and wrap to 0 if it overflows 16 bits. */
	multicast->seqno = 0xFFFF & (multicast->seqno + 1);

	/* Finally send it out to the eager phones listening for us */
	ast_rtp_instance_get_remote_address(instance, &remote_address);

	if (ast_sendto(multicast->socket, (void *) rtpheader, f->datalen + hdrlen, 0, &remote_address) < 0) {
		ast_log(LOG_ERROR, "Multicast RTP Transmission error to %s: %s\n",
			ast_sockaddr_stringify(&remote_address),
			strerror(errno));
		res = -1;
	}

	/* If we were forced to duplicate the frame free the new one */
	if (frame != f) {
		ast_frfree(f);
	}

	return res;
}

/*! \brief Function called to read from a multicast instance */
static struct ast_frame *multicast_rtp_read(struct ast_rtp_instance *instance, int rtcp)
{
	return &ast_null_frame;
}

static int load_module(void)
{
	if (ast_rtp_engine_register(&multicast_rtp_engine)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_rtp_engine_unregister(&multicast_rtp_engine);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Multicast RTP Engine",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
