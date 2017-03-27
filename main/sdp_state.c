/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#include "asterisk.h"
#include "asterisk/sdp_state.h"
#include "asterisk/sdp_options.h"
#include "asterisk/sdp_translator.h"
#include "asterisk/vector.h"
#include "asterisk/utils.h"
#include "asterisk/netsock2.h"
#include "asterisk/rtp_engine.h"

#include "../include/asterisk/sdp.h"
#include "asterisk/stream.h"

#include "sdp_private.h"

enum ast_sdp_state_machine {
	/*! \brief The initial state.
	 *
	 * The state machine starts here. It also goes back to this
	 * state whenever ast_sdp_state_reset() is called.
	 */
	SDP_STATE_INITIAL,
	/*! \brief We are the SDP offerer.
	 *
	 * The state machine enters this state if in the initial state
	 * and ast_sdp_state_get_local() is called. When this state is
	 * entered, a local SDP is created and then returned.
	 */
	SDP_STATE_OFFERER,
	/*! \brief We are the SDP answerer.
	 *
	 * The state machine enters this state if in the initial state
	 * and ast_sdp_state_set_remote() is called.
	 */
	SDP_STATE_ANSWERER,
	/*! \brief The SDP has been negotiated.
	 *
	 * This state can be entered from either the offerer or answerer
	 * state. When this state is entered, a joint SDP is created.
	 */
	SDP_STATE_NEGOTIATED,
	/*! \brief Not an actual state.
	 *
	 * This is just here to mark the end of the enumeration.
	 */
	SDP_STATE_END,
};

typedef int (*state_fn)(struct ast_sdp_state *state);

struct sdp_state_stream {
	union {
		/*! The underlying RTP instance */
		struct ast_rtp_instance *instance;
	};
	/*! An explicit connection address for this stream */
	struct ast_sockaddr connection_address;
	/*! Whether this stream is held or not */
	unsigned int locally_held;
};

struct sdp_state_capabilities {
	/*! Stream topology */
	struct ast_stream_topology *topology;
	/*! Additional information about the streams */
	AST_VECTOR(, struct sdp_state_stream) streams;
	/*! An explicit global connection address */
	struct ast_sockaddr connection_address;
};

struct ast_sdp_state {
	/*! Local capabilities, learned through configuration */
	struct sdp_state_capabilities local_capabilities;
	/*! Remote capabilities, learned through remote SDP */
	struct ast_stream_topology *remote_capabilities;
	/*! Joint capabilities. The combined local and remote capabilities. */
	struct sdp_state_capabilities joint_capabilities;
	/*! Local SDP. Generated via the options and local capabilities. */
	struct ast_sdp *local_sdp;
	/*! Remote SDP. Received directly from a peer. */
	struct ast_sdp *remote_sdp;
	/*! Joint SDP. The merged local and remote SDPs. */
	struct ast_sdp *joint_sdp;
	/*! SDP options. Configured options beyond media capabilities. */
	struct ast_sdp_options *options;
	/*! Translator that puts SDPs into the expected representation */
	struct ast_sdp_translator *translator;
	/*! The current state machine state that we are in */
	enum ast_sdp_state_machine state;
};

struct ast_sdp_state *ast_sdp_state_alloc(struct ast_stream_topology *streams,
	struct ast_sdp_options *options)
{
	struct ast_sdp_state *sdp_state;

	sdp_state = ast_calloc(1, sizeof(*sdp_state));
	if (!sdp_state) {
		return NULL;
	}

	sdp_state->options = options;

	sdp_state->translator = ast_sdp_translator_new(ast_sdp_options_get_impl(sdp_state->options));
	if (!sdp_state->translator) {
		ast_sdp_state_free(sdp_state);
		return NULL;
	}

	if (ast_sdp_state_update_local_topology(sdp_state, streams)) {
		ast_sdp_state_free(sdp_state);
		return NULL;
	}

	sdp_state->state = SDP_STATE_INITIAL;

	return sdp_state;
}

static void sdp_state_capabilities_free(struct sdp_state_capabilities *sdp_capabilities)
{
	int stream_index;

	for (stream_index = 0; stream_index < AST_VECTOR_SIZE(&sdp_capabilities->streams); stream_index++) {
		struct sdp_state_stream *stream_state = AST_VECTOR_GET_ADDR(&sdp_capabilities->streams, stream_index);
		enum ast_media_type type = ast_stream_get_type(ast_stream_topology_get_stream(sdp_capabilities->topology, stream_index));

		if (type == AST_MEDIA_TYPE_AUDIO || type == AST_MEDIA_TYPE_VIDEO) {
			ast_rtp_instance_destroy(stream_state->instance);
		}
	}

	ast_stream_topology_free(sdp_capabilities->topology);
	AST_VECTOR_FREE(&sdp_capabilities->streams);
}

void ast_sdp_state_free(struct ast_sdp_state *sdp_state)
{
	if (!sdp_state) {
		return;
	}

	sdp_state_capabilities_free(&sdp_state->local_capabilities);
	ast_stream_topology_free(sdp_state->remote_capabilities);
	sdp_state_capabilities_free(&sdp_state->joint_capabilities);
	ast_sdp_free(sdp_state->local_sdp);
	ast_sdp_free(sdp_state->remote_sdp);
	ast_sdp_free(sdp_state->joint_sdp);
	ast_sdp_options_free(sdp_state->options);
	ast_sdp_translator_free(sdp_state->translator);
}

static struct sdp_state_stream *sdp_state_get_stream(const struct ast_sdp_state *sdp_state, int stream_index)
{
	if (stream_index >= AST_VECTOR_SIZE(&sdp_state->local_capabilities.streams)) {
		return NULL;
	}

	return AST_VECTOR_GET_ADDR(&sdp_state->local_capabilities.streams, stream_index);
}

struct ast_rtp_instance *ast_sdp_state_get_rtp_instance(
	const struct ast_sdp_state *sdp_state, int stream_index)
{
	struct sdp_state_stream *stream_state;

	ast_assert(sdp_state != NULL);

	stream_state = sdp_state_get_stream(sdp_state, stream_index);
	if (!stream_state) {
		return NULL;
	}

	return stream_state->instance;
}

const struct ast_sockaddr *ast_sdp_state_get_connection_address(const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	return &sdp_state->local_capabilities.connection_address;
}

int ast_sdp_state_get_stream_connection_address(const struct ast_sdp_state *sdp_state,
	int stream_index, struct ast_sockaddr *address)
{
	struct sdp_state_stream *stream_state;
	enum ast_media_type type;

	ast_assert(sdp_state != NULL);
	ast_assert(address != NULL);

	stream_state = sdp_state_get_stream(sdp_state, stream_index);
	if (!stream_state) {
		return -1;
	}

	/* If an explicit connection address has been provided for the stream return it */
	if (!ast_sockaddr_isnull(&stream_state->connection_address)) {
		ast_sockaddr_copy(address, &stream_state->connection_address);
		return 0;
	}

	type = ast_stream_get_type(ast_stream_topology_get_stream(sdp_state->local_capabilities.topology,
		stream_index));

	if (type == AST_MEDIA_TYPE_AUDIO || type == AST_MEDIA_TYPE_VIDEO) {
		ast_rtp_instance_get_local_address(stream_state->instance, address);
	} else {
		return -1;
	}

	/* If an explicit global connection address is set use it here for the IP part */
	if (!ast_sockaddr_isnull(&sdp_state->local_capabilities.connection_address)) {
		int port = ast_sockaddr_port(address);

		ast_sockaddr_copy(address, &sdp_state->local_capabilities.connection_address);
		ast_sockaddr_set_port(address, port);
	}

	return 0;
}

const struct ast_stream_topology *ast_sdp_state_get_joint_topology(
	const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);
	if (sdp_state->state == SDP_STATE_NEGOTIATED) {
		return sdp_state->joint_capabilities.topology;
	} else {
		return sdp_state->local_capabilities.topology;
	}
}

const struct ast_stream_topology *ast_sdp_state_get_local_topology(
	const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	return sdp_state->local_capabilities.topology;
}

const struct ast_sdp_options *ast_sdp_state_get_options(
	const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	return sdp_state->options;
}

#if 0
static int merge_sdps(struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state->local_sdp != NULL);
	ast_assert(sdp_state->remote_sdp != NULL);
	/* XXX STUB */
	/* The goal of this function is to take
	 * sdp_state->local_sdp and sdp_state->remote_sdp
	 * and negotiate those into a joint SDP. This joint
	 * SDP should be stored in sdp_state->joint_sdp. After
	 * the joint SDP is created, the joint SDP should be
	 * used to create the joint topology. Finally, if necessary,
	 * the RTP session may need to be adjusted in some ways. For
	 * instance, if we previously opened three ports for three
	 * streams, but we negotiate down to two streams, then we
	 * can shut down the port for the third stream. Similarly,
	 * if we end up negotiating something like BUNDLE, then we may
	 * need to tell the RTP layer to close ports and to multiplex
	 * streams.
	 */

	return 0;
}
#endif

/* TODO
 * This isn't set anywhere yet.
 */
/*! \brief Scheduler for RTCP purposes */
static struct ast_sched_context *sched;

/*! \brief Internal function which creates an RTP instance */
static struct ast_rtp_instance *create_rtp(const struct ast_sdp_options *options,
	enum ast_media_type media_type)
{
	struct ast_rtp_instance *rtp;
	struct ast_rtp_engine_ice *ice;
	struct ast_sockaddr temp_media_address;
	static struct ast_sockaddr address_rtp;
	struct ast_sockaddr *media_address =  &address_rtp;

	if (options->bind_rtp_to_media_address && !ast_strlen_zero(options->media_address)) {
		ast_sockaddr_parse(&temp_media_address, options->media_address, 0);
		media_address = &temp_media_address;
	} else {
		if (ast_check_ipv6()) {
			ast_sockaddr_parse(&address_rtp, "::", 0);
		} else {
			ast_sockaddr_parse(&address_rtp, "0.0.0.0", 0);
		}
	}

	if (!(rtp = ast_rtp_instance_new(options->rtp_engine, sched, media_address, NULL))) {
		ast_log(LOG_ERROR, "Unable to create RTP instance using RTP engine '%s'\n",
			options->rtp_engine);
		return NULL;
	}

	ast_rtp_instance_set_prop(rtp, AST_RTP_PROPERTY_RTCP, 1);
	ast_rtp_instance_set_prop(rtp, AST_RTP_PROPERTY_NAT, options->rtp_symmetric);

	if (options->ice == AST_SDP_ICE_DISABLED && (ice = ast_rtp_instance_get_ice(rtp))) {
		ice->stop(rtp);
	}

	if (options->telephone_event) {
		ast_rtp_instance_dtmf_mode_set(rtp, AST_RTP_DTMF_MODE_RFC2833);
		ast_rtp_instance_set_prop(rtp, AST_RTP_PROPERTY_DTMF, 1);
	}

	if (media_type == AST_MEDIA_TYPE_AUDIO &&
			(options->tos_audio || options->cos_audio)) {
		ast_rtp_instance_set_qos(rtp, options->tos_audio,
			options->cos_audio, "SIP RTP Audio");
	} else if (media_type == AST_MEDIA_TYPE_VIDEO &&
			(options->tos_video || options->cos_video)) {
		ast_rtp_instance_set_qos(rtp, options->tos_video,
			options->cos_video, "SIP RTP Video");
	}

	ast_rtp_instance_set_last_rx(rtp, time(NULL));

	return rtp;
}

static int sdp_state_setup_local_streams(struct ast_sdp_state *sdp_state)
{
	int stream_index;

	for (stream_index = 0; stream_index < AST_VECTOR_SIZE(&sdp_state->local_capabilities.streams); stream_index++) {
		struct sdp_state_stream *stream_state_local = AST_VECTOR_GET_ADDR(&sdp_state->local_capabilities.streams, stream_index);
		struct sdp_state_stream *stream_state_joint = NULL;
		enum ast_media_type type_local = ast_stream_get_type(ast_stream_topology_get_stream(sdp_state->local_capabilities.topology, stream_index));
		enum ast_media_type type_joint = AST_MEDIA_TYPE_UNKNOWN;

		if (stream_index < AST_VECTOR_SIZE(&sdp_state->joint_capabilities.streams)) {
			stream_state_joint = AST_VECTOR_GET_ADDR(&sdp_state->joint_capabilities.streams, stream_index);
			type_joint = ast_stream_get_type(ast_stream_topology_get_stream(sdp_state->joint_capabilities.topology, stream_index));
		}

		/* If we can reuse an existing media stream then do so */
		if (type_local == type_joint) {
			if (type_local == AST_MEDIA_TYPE_AUDIO || type_local == AST_MEDIA_TYPE_VIDEO) {
				stream_state_local->instance = ao2_bump(stream_state_joint->instance);
				continue;
			}
		}

		if (type_local == AST_MEDIA_TYPE_AUDIO || type_local == AST_MEDIA_TYPE_VIDEO) {
			/* We need to create a new RTP instance */
			stream_state_local->instance = create_rtp(sdp_state->options, type_local);
			if (!stream_state_local->instance) {
				return -1;
			}
		}
	}

	return 0;
}

const struct ast_sdp *ast_sdp_state_get_local_sdp(struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	if (!sdp_state->local_sdp) {
		if (sdp_state_setup_local_streams(sdp_state)) {
			return NULL;
		}
		sdp_state->local_sdp = ast_sdp_create_from_state(sdp_state);
	}

	return sdp_state->local_sdp;
}

const void *ast_sdp_state_get_local_sdp_impl(struct ast_sdp_state *sdp_state)
{
	const struct ast_sdp *sdp = ast_sdp_state_get_local_sdp(sdp_state);

	if (!sdp) {
		return NULL;
	}

	return ast_sdp_translator_from_sdp(sdp_state->translator, sdp);
}

void ast_sdp_state_set_remote_sdp(struct ast_sdp_state *sdp_state, struct ast_sdp *sdp)
{
	ast_assert(sdp_state != NULL);

	sdp_state->remote_sdp = sdp;
}

int ast_sdp_state_set_remote_sdp_from_impl(struct ast_sdp_state *sdp_state, void *remote)
{
	struct ast_sdp *sdp;

	ast_assert(sdp_state != NULL);

	sdp = ast_sdp_translator_to_sdp(sdp_state->translator, remote);
	if (!sdp) {
		return -1;
	}

	sdp_state->remote_sdp = sdp;

	return 0;
}

int ast_sdp_state_reset(struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	ast_sdp_free(sdp_state->local_sdp);
	sdp_state->local_sdp = NULL;

	ast_sdp_free(sdp_state->remote_sdp);
	sdp_state->remote_sdp = NULL;

	ast_sdp_free(sdp_state->joint_sdp);
	sdp_state->joint_sdp = NULL;

	ast_stream_topology_free(sdp_state->remote_capabilities);
	sdp_state->remote_capabilities = NULL;

	ast_stream_topology_free(sdp_state->joint_capabilities.topology);
	sdp_state->joint_capabilities.topology = NULL;

	sdp_state->state = SDP_STATE_INITIAL;

	return 0;
}

int ast_sdp_state_update_local_topology(struct ast_sdp_state *sdp_state, struct ast_stream_topology *streams)
{
	ast_assert(sdp_state != NULL);
	ast_assert(streams != NULL);

	sdp_state_capabilities_free(&sdp_state->local_capabilities);
	sdp_state->local_capabilities.topology = ast_stream_topology_clone(streams);
	if (!sdp_state->local_capabilities.topology) {
		return -1;
	}

	if (AST_VECTOR_INIT(&sdp_state->local_capabilities.streams, ast_stream_topology_get_count(streams))) {
		return -1;
	}

	return 0;
}

void ast_sdp_state_set_local_address(struct ast_sdp_state *sdp_state, struct ast_sockaddr *address)
{
	ast_assert(sdp_state != NULL);

	if (!address) {
		ast_sockaddr_setnull(&sdp_state->local_capabilities.connection_address);
	} else {
		ast_sockaddr_copy(&sdp_state->local_capabilities.connection_address, address);
	}
}

int ast_sdp_state_set_connection_address(struct ast_sdp_state *sdp_state, int stream_index,
	struct ast_sockaddr *address)
{
	struct sdp_state_stream *stream_state;
	ast_assert(sdp_state != NULL);

	stream_state = sdp_state_get_stream(sdp_state, stream_index);
	if (!stream_state) {
		return -1;
	}

	if (!address) {
		ast_sockaddr_setnull(&stream_state->connection_address);
	} else {
		ast_sockaddr_copy(&stream_state->connection_address, address);
	}

	return 0;
}

void ast_sdp_state_set_locally_held(struct ast_sdp_state *sdp_state,
	int stream_index, unsigned int locally_held)
{
	struct sdp_state_stream *stream_state;
	ast_assert(sdp_state != NULL);

	stream_state = sdp_state_get_stream(sdp_state, stream_index);
	if (!stream_state) {
		return;
	}

	stream_state->locally_held = locally_held;
}

unsigned int ast_sdp_state_get_locally_held(const struct ast_sdp_state *sdp_state,
	int stream_index)
{
	struct sdp_state_stream *stream_state;
	ast_assert(sdp_state != NULL);

	stream_state = sdp_state_get_stream(sdp_state, stream_index);
	if (!stream_state) {
		return 0;
	}

	return stream_state->locally_held;
}
