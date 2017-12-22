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
#include "asterisk/multicast_rtp.h"
#include "asterisk/app.h"
#include "asterisk/smoother.h"

/*! Command value used for Linksys paging to indicate we are starting */
#define LINKSYS_MCAST_STARTCMD 6

/*! Command value used for Linksys paging to indicate we are stopping */
#define LINKSYS_MCAST_STOPCMD 7

/*! \brief Type of paging to do */
enum multicast_type {
	/*! Type has not been set yet */
	MULTICAST_TYPE_UNSPECIFIED = 0,
	/*! Simple multicast enabled client/receiver paging like Snom and Barix uses */
	MULTICAST_TYPE_BASIC,
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
	struct ast_smoother *smoother;
};

#define MAX_TIMESTAMP_SKEW 640

enum {
	OPT_CODEC = (1 << 0),
	OPT_LOOP =  (1 << 1),
	OPT_TTL =   (1 << 2),
	OPT_IF =    (1 << 3),
};

enum {
	OPT_ARG_CODEC = 0,
	OPT_ARG_LOOP,
	OPT_ARG_TTL,
	OPT_ARG_IF,
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(multicast_rtp_options, BEGIN_OPTIONS
	/*! Set the codec to be used for multicast RTP */
	AST_APP_OPTION_ARG('c', OPT_CODEC, OPT_ARG_CODEC),
	/*! Set whether multicast RTP is looped back to the sender */
	AST_APP_OPTION_ARG('l', OPT_LOOP, OPT_ARG_LOOP),
	/*! Set the hop count for multicast RTP */
	AST_APP_OPTION_ARG('t', OPT_TTL, OPT_ARG_TTL),
	/*! Set the interface from which multicast RTP is sent */
	AST_APP_OPTION_ARG('i', OPT_IF, OPT_ARG_IF),
END_OPTIONS );

struct ast_multicast_rtp_options {
	char *type;
	char *options;
	struct ast_format *fmt;
	struct ast_flags opts;
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	/*! The type and options are stored in this buffer */
	char buf[0];
};

struct ast_multicast_rtp_options *ast_multicast_rtp_create_options(const char *type,
	const char *options)
{
	struct ast_multicast_rtp_options *mcast_options;
	char *pos;

	mcast_options = ast_calloc(1, sizeof(*mcast_options)
			+ strlen(type)
			+ strlen(S_OR(options, "")) + 2);
	if (!mcast_options) {
		return NULL;
	}

	pos = mcast_options->buf;

	/* Safe */
	strcpy(pos, type);
	mcast_options->type = pos;
	pos += strlen(type) + 1;

	if (!ast_strlen_zero(options)) {
		strcpy(pos, options); /* Safe */
	}
	mcast_options->options = pos;

	if (ast_app_parse_options(multicast_rtp_options, &mcast_options->opts,
		mcast_options->opt_args, mcast_options->options)) {
		ast_log(LOG_WARNING, "Error parsing multicast RTP options\n");
		ast_multicast_rtp_free_options(mcast_options);
		return NULL;
	}

	return mcast_options;
}

void ast_multicast_rtp_free_options(struct ast_multicast_rtp_options *mcast_options)
{
	ast_free(mcast_options);
}

struct ast_format *ast_multicast_rtp_options_get_format(struct ast_multicast_rtp_options *mcast_options)
{
	if (ast_test_flag(&mcast_options->opts, OPT_CODEC)
		&& !ast_strlen_zero(mcast_options->opt_args[OPT_ARG_CODEC])) {
		return ast_format_cache_get(mcast_options->opt_args[OPT_ARG_CODEC]);
	}

	return NULL;
}

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

static int set_type(struct multicast_rtp *multicast, const char *type)
{
	if (!strcasecmp(type, "basic")) {
		multicast->type = MULTICAST_TYPE_BASIC;
	} else if (!strcasecmp(type, "linksys")) {
		multicast->type = MULTICAST_TYPE_LINKSYS;
	} else {
		ast_log(LOG_WARNING, "Unrecognized multicast type '%s' specified.\n", type);
		return -1;
	}

	return 0;
}

static void set_ttl(int sock, const char *ttl_str)
{
	int ttl;

	if (ast_strlen_zero(ttl_str)) {
		return;
	}

	ast_debug(3, "Setting multicast TTL to %s\n", ttl_str);

	if (sscanf(ttl_str, "%30d", &ttl) < 1) {
		ast_log(LOG_WARNING, "Invalid multicast ttl option '%s'\n", ttl_str);
		return;
	}

	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
		ast_log(LOG_WARNING, "Could not set multicast ttl to '%s': %s\n",
			ttl_str, strerror(errno));
	}
}

static void set_loop(int sock, const char *loop_str)
{
	unsigned char loop;

	if (ast_strlen_zero(loop_str)) {
		return;
	}

	ast_debug(3, "Setting multicast loop to %s\n", loop_str);

	if (sscanf(loop_str, "%30hhu", &loop) < 1) {
		ast_log(LOG_WARNING, "Invalid multicast loop option '%s'\n", loop_str);
		return;
	}

	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
		ast_log(LOG_WARNING, "Could not set multicast loop to '%s': %s\n",
			loop_str, strerror(errno));
	}
}

static void set_if(int sock, const char *if_str)
{
	struct in_addr iface;

	if (ast_strlen_zero(if_str)) {
		return;
	}

	ast_debug(3, "Setting multicast if to %s\n", if_str);

	if (!inet_aton(if_str, &iface)) {
		ast_log(LOG_WARNING, "Cannot parse if option '%s'\n", if_str);
	}

	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface)) < 0) {
		ast_log(LOG_WARNING, "Could not set multicast if to '%s': %s\n",
			if_str, strerror(errno));
	}
}

/*! \brief Function called to create a new multicast instance */
static int multicast_rtp_new(struct ast_rtp_instance *instance, struct ast_sched_context *sched, struct ast_sockaddr *addr, void *data)
{
	struct multicast_rtp *multicast;
	struct ast_multicast_rtp_options *mcast_options = data;

	if (!(multicast = ast_calloc(1, sizeof(*multicast)))) {
		return -1;
	}

	if (set_type(multicast, mcast_options->type)) {
		ast_free(multicast);
		return -1;
	}

	if ((multicast->socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		ast_free(multicast);
		return -1;
	}

	if (ast_test_flag(&mcast_options->opts, OPT_LOOP)) {
		set_loop(multicast->socket, mcast_options->opt_args[OPT_ARG_LOOP]);
	}

	if (ast_test_flag(&mcast_options->opts, OPT_TTL)) {
		set_ttl(multicast->socket, mcast_options->opt_args[OPT_ARG_TTL]);
	}

	if (ast_test_flag(&mcast_options->opts, OPT_IF)) {
		set_if(multicast->socket, mcast_options->opt_args[OPT_ARG_IF]);
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

	if (multicast->smoother) {
		ast_smoother_free(multicast->smoother);
	}

	close(multicast->socket);

	ast_free(multicast);

	return 0;
}

static int rtp_raw_write(struct ast_rtp_instance *instance, struct ast_frame *frame, int codec)
{
	struct multicast_rtp *multicast = ast_rtp_instance_get_data(instance);
	unsigned int ms = calc_txstamp(multicast, &frame->delivery);
	unsigned char *rtpheader;
	struct ast_sockaddr remote_address = { {0,} };
	int rate = rtp_get_rate(frame->subclass.format) / 1000;
	int hdrlen = 12, mark = 0;

	if (ast_format_cmp(frame->subclass.format, ast_format_g722) == AST_FORMAT_CMP_EQUAL) {
		frame->samples /= 2;
	}

	if (ast_test_flag(frame, AST_FRFLAG_HAS_TIMING_INFO)) {
		multicast->lastts = frame->ts * rate;
	} else {
		/* Try to predict what our timestamp should be */
		int pred = multicast->lastts + frame->samples;

		/* Calculate last TS */
		multicast->lastts = multicast->lastts + ms * rate;
		if (ast_tvzero(frame->delivery)) {
			int delta = abs((int) multicast->lastts - pred);
			if (delta < MAX_TIMESTAMP_SKEW) {
				multicast->lastts = pred;
			} else {
				ast_debug(3, "Difference is %d, ms is %u\n", delta, ms);
				mark = 1;
			}
		}
	}

	/* Construct an RTP header for our packet */
	rtpheader = (unsigned char *)(frame->data.ptr - hdrlen);

	put_unaligned_uint32(rtpheader, htonl((2 << 30) | (codec << 16) | (multicast->seqno) | (mark << 23)));
	put_unaligned_uint32(rtpheader + 4, htonl(multicast->lastts));
	put_unaligned_uint32(rtpheader + 8, htonl(multicast->ssrc));

	/* Increment sequence number and wrap to 0 if it overflows 16 bits. */
	multicast->seqno = 0xFFFF & (multicast->seqno + 1);

	/* Finally send it out to the eager phones listening for us */
	ast_rtp_instance_get_remote_address(instance, &remote_address);

	if (ast_sendto(multicast->socket, (void *) rtpheader, frame->datalen + hdrlen, 0, &remote_address) < 0) {
		ast_log(LOG_ERROR, "Multicast RTP Transmission error to %s: %s\n",
			ast_sockaddr_stringify(&remote_address),
			strerror(errno));
		return -1;
	}

	return 0;
}

/*! \brief Function called to broadcast some audio on a multicast instance */
static int multicast_rtp_write(struct ast_rtp_instance *instance, struct ast_frame *frame)
{
	struct multicast_rtp *multicast = ast_rtp_instance_get_data(instance);
	struct ast_format *format;
	struct ast_frame *f;
	int codec;

	/* We only accept audio, nothing else */
	if (frame->frametype != AST_FRAME_VOICE) {
		return 0;
	}

	/* Grab the actual payload number for when we create the RTP packet */
	codec = ast_rtp_codecs_payload_code_tx(ast_rtp_instance_get_codecs(instance),
		1, frame->subclass.format, 0);
	if (codec < 0) {
		return -1;
	}

	format = frame->subclass.format;
	if (!multicast->smoother && ast_format_can_be_smoothed(format)) {
		unsigned int smoother_flags = ast_format_get_smoother_flags(format);
		unsigned int framing_ms = ast_rtp_codecs_get_framing(ast_rtp_instance_get_codecs(instance));

		if (!framing_ms && (smoother_flags & AST_SMOOTHER_FLAG_FORCED)) {
			framing_ms = ast_format_get_default_ms(format);
		}

		if (framing_ms) {
			multicast->smoother = ast_smoother_new((framing_ms * ast_format_get_minimum_bytes(format)) / ast_format_get_minimum_ms(format));
			if (!multicast->smoother) {
				ast_log(LOG_WARNING, "Unable to create smoother: format %s ms: %u len %u\n",
						ast_format_get_name(format), framing_ms, ast_format_get_minimum_bytes(format));
				return -1;
			}
			ast_smoother_set_flags(multicast->smoother, smoother_flags);
		}
	}

	if (multicast->smoother) {
		if (ast_smoother_test_flag(multicast->smoother, AST_SMOOTHER_FLAG_BE)) {
			ast_smoother_feed_be(multicast->smoother, frame);
		} else {
			ast_smoother_feed(multicast->smoother, frame);
		}

		while ((f = ast_smoother_read(multicast->smoother)) && f->data.ptr) {
			rtp_raw_write(instance, f, codec);
		}
	} else {
		int hdrlen = 12;

		/* If we do not have space to construct an RTP header duplicate the frame so we get some */
		if (frame->offset < hdrlen) {
			f = ast_frdup(frame);
		} else {
			f = frame;
		}

		if (f->data.ptr) {
			rtp_raw_write(instance, f, codec);
		}

		if (f != frame) {
			ast_frfree(f);
		}
	}

	return 0;
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Multicast RTP Engine",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
