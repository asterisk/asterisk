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
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
#include "asterisk/netsock.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/unaligned.h"
#include "asterisk/module.h"
#include "asterisk/rtp_engine.h"

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
	unsigned int seqno;
};

/* Forward Declarations */
static int multicast_rtp_new(struct ast_rtp_instance *instance, struct sched_context *sched, struct sockaddr_in *sin, void *data);
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
static int multicast_rtp_new(struct ast_rtp_instance *instance, struct sched_context *sched, struct sockaddr_in *sin, void *data)
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

/*! \brief Helper function which populates a control packet with useful information and sends it */
static int multicast_send_control_packet(struct ast_rtp_instance *instance, struct multicast_rtp *multicast, int command)
{
	struct multicast_control_packet control_packet = { .unique_id = htonl((u_long)time(NULL)),
							   .command = htonl(command),
	};
	struct sockaddr_in control_address, remote_address;

	ast_rtp_instance_get_local_address(instance, &control_address);
	ast_rtp_instance_get_remote_address(instance, &remote_address);

	/* Ensure the user of us have given us both the control address and destination address */
	if (!control_address.sin_addr.s_addr || !remote_address.sin_addr.s_addr) {
		return -1;
	}

	control_packet.ip = remote_address.sin_addr.s_addr;
	control_packet.port = htonl(ntohs(remote_address.sin_port));

	/* Based on a recommendation by Brian West who did the FreeSWITCH implementation we send control packets twice */
	sendto(multicast->socket, &control_packet, sizeof(control_packet), 0, (struct sockaddr *)&control_address, sizeof(control_address));
	sendto(multicast->socket, &control_packet, sizeof(control_packet), 0, (struct sockaddr *)&control_address, sizeof(control_address));

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
	struct sockaddr_in remote_address;
	int hdrlen = 12, res, codec;
	unsigned char *rtpheader;

	/* We only accept audio, nothing else */
	if (frame->frametype != AST_FRAME_VOICE) {
		return 0;
	}

	/* Grab the actual payload number for when we create the RTP packet */
	if ((codec = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(instance), 1, frame->subclass)) < 0) {
		return -1;
	}

	/* If we do not have space to construct an RTP header duplicate the frame so we get some */
	if (frame->offset < hdrlen) {
		f = ast_frdup(frame);
	}

	/* Construct an RTP header for our packet */
	rtpheader = (unsigned char *)(f->data.ptr - hdrlen);
	put_unaligned_uint32(rtpheader, htonl((2 << 30) | (codec << 16) | (multicast->seqno++) | (0 << 23)));
	put_unaligned_uint32(rtpheader + 4, htonl(f->ts * 8));
	put_unaligned_uint32(rtpheader + 8, htonl(multicast->ssrc));

	/* Finally send it out to the eager phones listening for us */
	ast_rtp_instance_get_remote_address(instance, &remote_address);
	res = sendto(multicast->socket, (void *) rtpheader, f->datalen + hdrlen, 0, (struct sockaddr *) &remote_address, sizeof(remote_address));

	if (res < 0) {
		ast_log(LOG_ERROR, "Multicast RTP Transmission error to %s:%u: %s\n",
			ast_inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port), strerror(errno));
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

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Multicast RTP Engine");
