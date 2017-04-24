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
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/config.h"
#include "asterisk/codec.h"
#include "asterisk/udptl.h"

#include "asterisk/sdp.h"
#include "asterisk/stream.h"

#include "sdp_private.h"

enum ast_sdp_role {
	/*!
	 * \brief The role has not yet been determined.
	 *
	 * When the SDP state is allocated, this is the starting role.
	 * Similarly, when the SDP state is reset, the role is reverted
	 * to this.
	 */
	SDP_ROLE_NOT_SET,
	/*!
	 * \brief We are the offerer.
	 *
	 * If a local SDP is requested before a remote SDP has been set, then
	 * we assume the role of offerer. This means that we will generate an
	 * SDP from the local capabilities and configured options.
	 */
	SDP_ROLE_OFFERER,
	/*!
	 * \brief We are the answerer.
	 *
	 * If a remote SDP is set before a local SDP is requested, then we
	 * assume the role of answerer. This means that we will generate an
	 * SDP based on a merge of the remote capabilities and our local capabilities.
	 */
	SDP_ROLE_ANSWERER,
};

typedef int (*state_fn)(struct ast_sdp_state *state);

struct sdp_state_udptl {
	/*! The underlying UDPTL instance */
	struct ast_udptl *instance;
};

struct sdp_state_stream {
	/*! Type of the stream */
	enum ast_media_type type;
	union {
		/*! The underlying RTP instance */
		struct ast_rtp_instance *instance;
		/*! The underlying UDPTL instance */
		struct sdp_state_udptl *udptl;
	};
	/*! An explicit connection address for this stream */
	struct ast_sockaddr connection_address;
	/*! Whether this stream is held or not */
	unsigned int locally_held;
	/*! UDPTL session parameters */
	struct ast_control_t38_parameters t38_local_params;
};

static void sdp_state_udptl_destroy(void *obj)
{
	struct sdp_state_udptl *udptl = obj;

	if (udptl->instance) {
		ast_udptl_destroy(udptl->instance);
	}
}

static void sdp_state_stream_free(struct sdp_state_stream *state_stream)
{
	switch (state_stream->type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
		if (state_stream->instance) {
			ast_rtp_instance_destroy(state_stream->instance);
		}
		break;
	case AST_MEDIA_TYPE_IMAGE:
		ao2_cleanup(state_stream->udptl);
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_TEXT:
	case AST_MEDIA_TYPE_END:
		break;
	}
	ast_free(state_stream);
}

AST_VECTOR(sdp_state_streams, struct sdp_state_stream *);

struct sdp_state_capabilities {
	/*! Stream topology */
	struct ast_stream_topology *topology;
	/*! Additional information about the streams */
	struct sdp_state_streams streams;
	/*! An explicit global connection address */
	struct ast_sockaddr connection_address;
};

static void sdp_state_capabilities_free(struct sdp_state_capabilities *capabilities)
{
	if (!capabilities) {
		return;
	}

	ast_stream_topology_free(capabilities->topology);
	AST_VECTOR_CALLBACK_VOID(&capabilities->streams, sdp_state_stream_free);
	AST_VECTOR_FREE(&capabilities->streams);
	ast_free(capabilities);
}

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

	ast_rtp_instance_set_prop(rtp, AST_RTP_PROPERTY_RTCP, AST_RTP_INSTANCE_RTCP_STANDARD);
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

/*! \brief Internal function which creates a UDPTL instance */
static struct sdp_state_udptl *create_udptl(const struct ast_sdp_options *options)
{
	struct sdp_state_udptl *udptl;
	struct ast_sockaddr temp_media_address;
	static struct ast_sockaddr address_udptl;
	struct ast_sockaddr *media_address =  &address_udptl;

	if (options->bind_udptl_to_media_address && !ast_strlen_zero(options->media_address)) {
		ast_sockaddr_parse(&temp_media_address, options->media_address, 0);
		media_address = &temp_media_address;
	} else {
		if (ast_check_ipv6()) {
			ast_sockaddr_parse(&address_udptl, "::", 0);
		} else {
			ast_sockaddr_parse(&address_udptl, "0.0.0.0", 0);
		}
	}

	udptl = ao2_alloc_options(sizeof(*udptl), sdp_state_udptl_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!udptl) {
		return NULL;
	}

	udptl->instance = ast_udptl_new_with_bindaddr(NULL, NULL, 0, media_address);
	if (!udptl->instance) {
		ao2_ref(udptl, -1);
		return NULL;
	}

	ast_udptl_set_error_correction_scheme(udptl->instance, ast_sdp_options_get_udptl_error_correction(options));
	ast_udptl_setnat(udptl->instance, ast_sdp_options_get_udptl_symmetric(options));
	ast_udptl_set_far_max_datagram(udptl->instance, ast_sdp_options_get_udptl_far_max_datagram(options));

	return udptl;
}

static struct sdp_state_capabilities *sdp_initialize_state_capabilities(const struct ast_stream_topology *topology,
	const struct ast_sdp_options *options)
{
	struct sdp_state_capabilities *capabilities;
	int i;

	capabilities = ast_calloc(1, sizeof(*capabilities));
	if (!capabilities) {
		return NULL;
	}

	capabilities->topology = ast_stream_topology_clone(topology);
	if (!capabilities->topology) {
		sdp_state_capabilities_free(capabilities);
		return NULL;
	}

	if (AST_VECTOR_INIT(&capabilities->streams, ast_stream_topology_get_count(topology))) {
		sdp_state_capabilities_free(capabilities);
		return NULL;
	}
	ast_sockaddr_setnull(&capabilities->connection_address);

	for (i = 0; i < ast_stream_topology_get_count(topology); ++i) {
		struct sdp_state_stream *state_stream;

		state_stream = ast_calloc(1, sizeof(*state_stream));
		if (!state_stream) {
			sdp_state_capabilities_free(capabilities);
			return NULL;
		}

		state_stream->type = ast_stream_get_type(ast_stream_topology_get_stream(topology, i));
		switch (state_stream->type) {
		case AST_MEDIA_TYPE_AUDIO:
		case AST_MEDIA_TYPE_VIDEO:
			state_stream->instance = create_rtp(options, state_stream->type);
			if (!state_stream->instance) {
				sdp_state_stream_free(state_stream);
				sdp_state_capabilities_free(capabilities);
				return NULL;
			}
			break;
		case AST_MEDIA_TYPE_IMAGE:
			state_stream->udptl = create_udptl(options);
			if (!state_stream->udptl) {
				sdp_state_stream_free(state_stream);
				sdp_state_capabilities_free(capabilities);
				return NULL;
			}
			break;
		case AST_MEDIA_TYPE_UNKNOWN:
		case AST_MEDIA_TYPE_TEXT:
		case AST_MEDIA_TYPE_END:
			ast_assert(0);
			sdp_state_stream_free(state_stream);
			sdp_state_capabilities_free(capabilities);
			return NULL;
		}

		if (AST_VECTOR_APPEND(&capabilities->streams, state_stream)) {
			sdp_state_stream_free(state_stream);
			sdp_state_capabilities_free(capabilities);
			return NULL;
		}
	}

	return capabilities;
}

/*!
 * \brief SDP state, the main structure used to keep track of SDP negotiation
 * and settings.
 *
 * Most fields are pretty self-explanatory, but negotiated_capabilities and
 * proposed_capabilities could use some further explanation. When an SDP
 * state is allocated, a stream topology is provided that dictates the
 * types of streams to offer in the resultant SDP. At the time the SDP
 * is allocated, this topology is used to create the proposed_capabilities.
 *
 * If we are the SDP offerer, then the proposed_capabilities are what are used
 * to generate the SDP offer. When the SDP answer arrives, the proposed capabilities
 * are merged with the SDP answer to create the negotiated capabilities.
 *
 * If we are the SDP answerer, then the incoming SDP offer is merged with our
 * proposed capabilities to to create the negotiated capabilities. These negotiated
 * capabilities are what we send in our SDP answer.
 *
 * Any changes that a user of the API performs will occur on the proposed capabilities.
 * The negotiated capabilities are only altered based on actual SDP negotiation. This is
 * done so that the negotiated capabilities can be fallen back on if the proposed
 * capabilities run into some sort of issue.
 */
struct ast_sdp_state {
	/*! Current capabilities */
	struct sdp_state_capabilities *negotiated_capabilities;
	/*! Proposed capabilities */
	struct sdp_state_capabilities *proposed_capabilities;
	/*! Remote capabilities, learned through remote SDP */
	struct ast_stream_topology *remote_capabilities;
	/*! Local SDP. Generated via the options and local capabilities. */
	struct ast_sdp *local_sdp;
	/*! SDP options. Configured options beyond media capabilities. */
	struct ast_sdp_options *options;
	/*! Translator that puts SDPs into the expected representation */
	struct ast_sdp_translator *translator;
	/*! The role that we occupy in SDP negotiation */
	enum ast_sdp_role role;
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

	sdp_state->proposed_capabilities = sdp_initialize_state_capabilities(streams, options);
	if (!sdp_state->proposed_capabilities) {
		ast_sdp_state_free(sdp_state);
		return NULL;
	}

	sdp_state->role = SDP_ROLE_NOT_SET;

	return sdp_state;
}

void ast_sdp_state_free(struct ast_sdp_state *sdp_state)
{
	if (!sdp_state) {
		return;
	}

	sdp_state_capabilities_free(sdp_state->negotiated_capabilities);
	sdp_state_capabilities_free(sdp_state->proposed_capabilities);
	ast_stream_topology_free(sdp_state->remote_capabilities);
	ast_sdp_free(sdp_state->local_sdp);
	ast_sdp_options_free(sdp_state->options);
	ast_sdp_translator_free(sdp_state->translator);
	ast_free(sdp_state);
}

static struct sdp_state_stream *sdp_state_get_stream(const struct ast_sdp_state *sdp_state, int stream_index)
{
	if (stream_index >= AST_VECTOR_SIZE(&sdp_state->proposed_capabilities->streams)) {
		return NULL;
	}

	return AST_VECTOR_GET(&sdp_state->proposed_capabilities->streams, stream_index);
}

struct ast_rtp_instance *ast_sdp_state_get_rtp_instance(
	const struct ast_sdp_state *sdp_state, int stream_index)
{
	struct sdp_state_stream *stream_state;

	ast_assert(sdp_state != NULL);
	ast_assert(ast_stream_get_type(ast_stream_topology_get_stream(sdp_state->proposed_capabilities->topology,
		stream_index)) == AST_MEDIA_TYPE_AUDIO || ast_stream_get_type(ast_stream_topology_get_stream(
			sdp_state->proposed_capabilities->topology, stream_index)) == AST_MEDIA_TYPE_VIDEO);

	stream_state = sdp_state_get_stream(sdp_state, stream_index);
	if (!stream_state) {
		return NULL;
	}

	return stream_state->instance;
}

struct ast_udptl *ast_sdp_state_get_udptl_instance(
	const struct ast_sdp_state *sdp_state, int stream_index)
{
	struct sdp_state_stream *stream_state;

	ast_assert(sdp_state != NULL);
	ast_assert(ast_stream_get_type(ast_stream_topology_get_stream(sdp_state->proposed_capabilities->topology,
		stream_index)) == AST_MEDIA_TYPE_IMAGE);

	stream_state = sdp_state_get_stream(sdp_state, stream_index);
	if (!stream_state || !stream_state->udptl) {
		return NULL;
	}

	return stream_state->udptl->instance;
}

const struct ast_sockaddr *ast_sdp_state_get_connection_address(const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	return &sdp_state->proposed_capabilities->connection_address;
}

int ast_sdp_state_get_stream_connection_address(const struct ast_sdp_state *sdp_state,
	int stream_index, struct ast_sockaddr *address)
{
	struct sdp_state_stream *stream_state;

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

	switch (ast_stream_get_type(ast_stream_topology_get_stream(sdp_state->proposed_capabilities->topology,
		stream_index))) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
		ast_rtp_instance_get_local_address(stream_state->instance, address);
		break;
	case AST_MEDIA_TYPE_IMAGE:
		ast_udptl_get_us(stream_state->udptl->instance, address);
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_TEXT:
	case AST_MEDIA_TYPE_END:
		return -1;
	}

	/* If an explicit global connection address is set use it here for the IP part */
	if (!ast_sockaddr_isnull(&sdp_state->proposed_capabilities->connection_address)) {
		int port = ast_sockaddr_port(address);

		ast_sockaddr_copy(address, &sdp_state->proposed_capabilities->connection_address);
		ast_sockaddr_set_port(address, port);
	}

	return 0;
}

const struct ast_stream_topology *ast_sdp_state_get_joint_topology(
	const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	if (sdp_state->negotiated_capabilities) {
		return sdp_state->negotiated_capabilities->topology;
	}

	return sdp_state->proposed_capabilities->topology;
}

const struct ast_stream_topology *ast_sdp_state_get_local_topology(
	const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	return sdp_state->proposed_capabilities->topology;
}

const struct ast_sdp_options *ast_sdp_state_get_options(
	const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	return sdp_state->options;
}

/*!
 * \brief Merge two streams into a joint stream.
 *
 * \param local Our local stream
 * \param remote A remote stream
 * \retval NULL An error occurred
 * \retval non-NULL The joint stream created
 */
static struct ast_stream *merge_streams(const struct ast_stream *local,
	const struct ast_stream *remote)
{
	struct ast_stream *joint_stream;
	struct ast_format_cap *joint_cap;
	struct ast_format_cap *local_cap;
	struct ast_format_cap *remote_cap;
	struct ast_str *local_buf = ast_str_alloca(128);
	struct ast_str *remote_buf = ast_str_alloca(128);
	struct ast_str *joint_buf = ast_str_alloca(128);

	joint_stream = ast_stream_alloc(ast_codec_media_type2str(ast_stream_get_type(remote)),
		ast_stream_get_type(remote));
	if (!joint_stream) {
		return NULL;
	}

	if (!local) {
		return joint_stream;
	}

	joint_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!joint_cap) {
		ast_stream_free(joint_stream);
		return NULL;
	}

	local_cap = ast_stream_get_formats(local);
	remote_cap = ast_stream_get_formats(remote);

	ast_format_cap_get_compatible(local_cap, remote_cap, joint_cap);

	ast_debug(3, "Combined local '%s' with remote '%s' to get joint '%s'. Joint has %zu formats\n",
		ast_format_cap_get_names(local_cap, &local_buf),
		ast_format_cap_get_names(remote_cap, &remote_buf),
		ast_format_cap_get_names(joint_cap, &joint_buf),
		ast_format_cap_count(joint_cap));

	ast_stream_set_formats(joint_stream, joint_cap);

	ao2_ref(joint_cap, -1);

	return joint_stream;
}

/*!
 * \brief Get a local stream that corresponds with a remote stream.
 *
 * \param local The local topology
 * \param media_type The type of stream we are looking for
 * \param[in,out] media_indices Keeps track of where to start searching in the topology
 * \retval NULL No corresponding stream found
 * \retval non-NULL The corresponding stream
 */
static int get_corresponding_index(const struct ast_stream_topology *local,
	enum ast_media_type media_type, int *media_indices)
{
	int i;
	int winner = -1;

	for (i = media_indices[media_type]; i < ast_stream_topology_get_count(local); ++i) {
		struct ast_stream *candidate;

		candidate = ast_stream_topology_get_stream(local, i);
		if (ast_stream_get_type(candidate) == media_type) {
			winner = i;
			break;
		}
	}

	media_indices[media_type] = i + 1;
	return winner;
}

/*!
 * \brief Merge existing stream capabilities and a new topology into joint capabilities.
 *
 * This is a bit complicated. The idea is that we already have some capabilities set, and
 * we've now been confronted with a new stream topology. We want to take what's been
 * presented to us and merge those new capabilities with our own.
 *
 * For each of the new streams, we try to find a corresponding stream in our current
 * capabilities. If we find one, then we get the compatible formats of the two streams
 * and create a new stream with those formats set. We then will re-use the underlying
 * media instance (such as an RTP instance) on this merged stream.
 *
 * The create_new parameter determines whether we should attempt to create new media
 * instances.
 * If we do not find a corresponding stream, then we create a new one. If the
 * create_new parameter is true, this created stream is made a clone of the new stream,
 * and a media instance is created. If the create_new parameter is not true, then the
 * created stream has no formats set and no media instance is created for it.
 *
 * \param current Current capabilities of the SDP state (may be NULL)
 * \param new_topology The new topology to base merged capabilities on
 * \param options The options set on the SDP state
 * \retval NULL An error occurred
 * \retval non-NULL The merged capabilities
 */
static struct sdp_state_capabilities *merge_capabilities(const struct sdp_state_capabilities *current,
	const struct ast_stream_topology *new_topology, const struct ast_sdp_options *options, int create_missing)
{
	struct sdp_state_capabilities *joint_capabilities;
	struct ast_stream_topology *topology;
	int media_indices[AST_MEDIA_TYPE_END] = {0};
	int i;

	ast_assert(current != NULL);

	joint_capabilities = ast_calloc(1, sizeof(*joint_capabilities));
	if (!joint_capabilities) {
		return NULL;
	}

	joint_capabilities->topology = ast_stream_topology_alloc();
	if (!joint_capabilities->topology) {
		goto fail;
	}

	if (AST_VECTOR_INIT(&joint_capabilities->streams, AST_VECTOR_SIZE(&current->streams))) {
		goto fail;
	}
	ast_sockaddr_copy(&joint_capabilities->connection_address, &current->connection_address);
	topology = current->topology;

	for (i = 0; i < ast_stream_topology_get_count(new_topology); ++i) {
		enum ast_media_type new_stream_type;
		struct ast_stream *new_stream;
		struct ast_stream *current_stream;
		struct ast_stream *joint_stream;
		struct sdp_state_stream *current_state_stream;
		struct sdp_state_stream *joint_state_stream;
		int current_index;

		joint_state_stream = ast_calloc(1, sizeof(*joint_state_stream));
		if (!joint_state_stream) {
			goto fail;
		}

		new_stream = ast_stream_topology_get_stream(new_topology, i);
		new_stream_type = ast_stream_get_type(new_stream);

		current_index = get_corresponding_index(topology, new_stream_type, media_indices);
		if (current_index >= 0) {
			current_stream = ast_stream_topology_get_stream(topology, current_index);
			joint_stream = merge_streams(current_stream, new_stream);
			if (!joint_stream) {
				sdp_state_stream_free(joint_state_stream);
				goto fail;
			}

			current_state_stream = AST_VECTOR_GET(&current->streams, current_index);
			joint_state_stream->type = current_state_stream->type;

			switch (joint_state_stream->type) {
			case AST_MEDIA_TYPE_AUDIO:
			case AST_MEDIA_TYPE_VIDEO:
				joint_state_stream->instance = ao2_bump(current_state_stream->instance);
				break;
			case AST_MEDIA_TYPE_IMAGE:
				joint_state_stream->udptl = ao2_bump(current_state_stream->udptl);
				joint_state_stream->t38_local_params = current_state_stream->t38_local_params;
				break;
			case AST_MEDIA_TYPE_UNKNOWN:
			case AST_MEDIA_TYPE_TEXT:
			case AST_MEDIA_TYPE_END:
				break;
			}

			if (!ast_sockaddr_isnull(&current_state_stream->connection_address)) {
				ast_sockaddr_copy(&joint_state_stream->connection_address, &current_state_stream->connection_address);
			} else {
				ast_sockaddr_setnull(&joint_state_stream->connection_address);
			}
			joint_state_stream->locally_held = current_state_stream->locally_held;
		} else if (create_missing) {
			/* We don't have a stream state that corresponds to the stream in the new topology, so
			 * create a stream state as appropriate.
			 */
			joint_stream = ast_stream_clone(new_stream);
			if (!joint_stream) {
				sdp_state_stream_free(joint_state_stream);
				goto fail;
			}

			switch (new_stream_type) {
			case AST_MEDIA_TYPE_AUDIO:
			case AST_MEDIA_TYPE_VIDEO:
				joint_state_stream->instance = create_rtp(options, new_stream_type);
				if (!joint_state_stream->instance) {
					ast_stream_free(joint_stream);
					sdp_state_stream_free(joint_state_stream);
					goto fail;
				}
				break;
			case AST_MEDIA_TYPE_IMAGE:
				joint_state_stream->udptl = create_udptl(options);
				if (!joint_state_stream->udptl) {
					ast_stream_free(joint_stream);
					sdp_state_stream_free(joint_state_stream);
					goto fail;
				}
				break;
			case AST_MEDIA_TYPE_UNKNOWN:
			case AST_MEDIA_TYPE_TEXT:
			case AST_MEDIA_TYPE_END:
				break;
			}
			ast_sockaddr_setnull(&joint_state_stream->connection_address);
			joint_state_stream->locally_held = 0;
		} else {
			/* We don't have a stream that corresponds to the stream in the new topology. Create a
			 * dummy stream to go in its place so that the resulting SDP created will contain
			 * the stream but will have no port or codecs set
			 */
			joint_stream = ast_stream_alloc("dummy", new_stream_type);
			if (!joint_stream) {
				sdp_state_stream_free(joint_state_stream);
				goto fail;
			}
		}

		if (ast_stream_topology_append_stream(joint_capabilities->topology, joint_stream) < 0) {
			ast_stream_free(joint_stream);
			sdp_state_stream_free(joint_state_stream);
			goto fail;
		}
		if (AST_VECTOR_APPEND(&joint_capabilities->streams, joint_state_stream)) {
			sdp_state_stream_free(joint_state_stream);
			goto fail;
		}
	}

	return joint_capabilities;

fail:
	sdp_state_capabilities_free(joint_capabilities);
	return NULL;
}

/*!
 * \brief Apply remote SDP's ICE information to our RTP session
 *
 * \param state The SDP state on which negotiation has taken place
 * \param options The SDP options we support
 * \param remote_sdp The SDP we most recently received
 * \param remote_m_line The stream on which we are examining ICE candidates
 */
static void update_ice(const struct ast_sdp_state *state, struct ast_rtp_instance *rtp, const struct ast_sdp_options *options,
	const struct ast_sdp *remote_sdp, const struct ast_sdp_m_line *remote_m_line)
{
	struct ast_rtp_engine_ice *ice;
	const struct ast_sdp_a_line *attr;
	unsigned int attr_i;

	/* If ICE support is not enabled or available exit early */
	if (ast_sdp_options_get_ice(options) != AST_SDP_ICE_ENABLED_STANDARD || !(ice = ast_rtp_instance_get_ice(rtp))) {
		return;
	}

	attr = ast_sdp_m_find_attribute(remote_m_line, "ice-ufrag", -1);
	if (!attr) {
		attr = ast_sdp_find_attribute(remote_sdp, "ice-ufrag", -1);
	}
	if (attr) {
		ice->set_authentication(rtp, attr->value, NULL);
	} else {
		return;
	}

	attr = ast_sdp_m_find_attribute(remote_m_line, "ice-pwd", -1);
	if (!attr) {
		attr = ast_sdp_find_attribute(remote_sdp, "ice-pwd", -1);
	}
	if (attr) {
		ice->set_authentication(rtp, NULL, attr->value);
	} else {
		return;
	}

	if (ast_sdp_m_find_attribute(remote_m_line, "ice-lite", -1)) {
		ice->ice_lite(rtp);
	}

	/* Find all of the candidates */
	for (attr_i = 0; attr_i < ast_sdp_m_get_a_count(remote_m_line); ++attr_i) {
		char foundation[32];
		char transport[32];
		char address[INET6_ADDRSTRLEN + 1];
		char cand_type[6];
		char relay_address[INET6_ADDRSTRLEN + 1] = "";
		unsigned int port;
		unsigned int relay_port = 0;
		struct ast_rtp_engine_ice_candidate candidate = { 0, };

		attr = ast_sdp_m_get_a(remote_m_line, attr_i);

		/* If this is not a candidate line skip it */
		if (strcmp(attr->name, "candidate")) {
			continue;
		}

		if (sscanf(attr->value, "%31s %30u %31s %30u %46s %30u typ %5s %*s %23s %*s %30u",
			foundation, &candidate.id, transport, (unsigned *)&candidate.priority, address,
			&port, cand_type, relay_address, &relay_port) < 7) {
			/* Candidate did not parse properly */
			continue;
		}

		if (ast_sdp_options_get_rtcp_mux(options)
			&& ast_sdp_m_find_attribute(remote_m_line, "rtcp-mux", -1)
			&& candidate.id > 1) {
			/* Remote side may have offered RTP and RTCP candidates. However, if we're using RTCP MUX,
			 * then we should ignore RTCP candidates.
			 */
			continue;
		}

		candidate.foundation = foundation;
		candidate.transport = transport;

		ast_sockaddr_parse(&candidate.address, address, PARSE_PORT_FORBID);
		ast_sockaddr_set_port(&candidate.address, port);

		if (!strcasecmp(cand_type, "host")) {
			candidate.type = AST_RTP_ICE_CANDIDATE_TYPE_HOST;
		} else if (!strcasecmp(cand_type, "srflx")) {
			candidate.type = AST_RTP_ICE_CANDIDATE_TYPE_SRFLX;
		} else if (!strcasecmp(cand_type, "relay")) {
			candidate.type = AST_RTP_ICE_CANDIDATE_TYPE_RELAYED;
		} else {
			continue;
		}

		if (!ast_strlen_zero(relay_address)) {
			ast_sockaddr_parse(&candidate.relay_address, relay_address, PARSE_PORT_FORBID);
		}

		if (relay_port) {
			ast_sockaddr_set_port(&candidate.relay_address, relay_port);
		}

		ice->add_remote_candidate(rtp, &candidate);
	}

	if (state->role == SDP_ROLE_OFFERER) {
		ice->set_role(rtp, AST_RTP_ICE_ROLE_CONTROLLING);
	} else {
		ice->set_role(rtp, AST_RTP_ICE_ROLE_CONTROLLED);
	}

	ice->start(rtp);
}

/*!
 * \brief Update RTP instances based on merged SDPs
 *
 * RTP instances, when first allocated, cannot make assumptions about what the other
 * side supports and thus has to go with some default behaviors. This function gets
 * called after we know both what we support and what the remote endpoint supports.
 * This way, we can update the RTP instance to reflect what is supported by both
 * sides.
 *
 * \param state The SDP state in which SDPs have been negotiated
 * \param rtp The RTP instance that is being updated
 * \param options Our locally-supported SDP options
 * \param remote_sdp The SDP we most recently received
 * \param remote_m_line The remote SDP stream that corresponds to the RTP instance we are modifying
 */
static void update_rtp_after_merge(const struct ast_sdp_state *state, struct ast_rtp_instance *rtp,
    const struct ast_sdp_options *options,
	const struct ast_sdp *remote_sdp,
	const struct ast_sdp_m_line *remote_m_line)
{
	if (ast_sdp_options_get_rtcp_mux(options) && ast_sdp_m_find_attribute(remote_m_line, "rtcp-mux", -1)) {
		ast_rtp_instance_set_prop(rtp, AST_RTP_PROPERTY_RTCP, AST_RTP_INSTANCE_RTCP_MUX);
	} else {
		ast_rtp_instance_set_prop(rtp, AST_RTP_PROPERTY_RTCP, AST_RTP_INSTANCE_RTCP_STANDARD);
	}

	if (ast_sdp_options_get_ice(options) == AST_SDP_ICE_ENABLED_STANDARD) {
		update_ice(state, rtp, options, remote_sdp, remote_m_line);
	}
}

/*!
 * \brief Update UDPTL instances based on merged SDPs
 *
 * UDPTL instances, when first allocated, cannot make assumptions about what the other
 * side supports and thus has to go with some default behaviors. This function gets
 * called after we know both what we support and what the remote endpoint supports.
 * This way, we can update the UDPTL instance to reflect what is supported by both
 * sides.
 *
 * \param state The SDP state in which SDPs have been negotiated
 * \param udptl The UDPTL instance that is being updated
 * \param options Our locally-supported SDP options
 * \param remote_sdp The SDP we most recently received
 * \param remote_m_line The remote SDP stream that corresponds to the RTP instance we are modifying
 */
static void update_udptl_after_merge(const struct ast_sdp_state *state, struct sdp_state_udptl *udptl,
    const struct ast_sdp_options *options,
	const struct ast_sdp *remote_sdp,
	const struct ast_sdp_m_line *remote_m_line)
{
	struct ast_sdp_a_line *a_line;
	struct ast_sdp_c_line *c_line;
	unsigned int fax_max_datagram;
	struct ast_sockaddr *addrs;

	a_line = ast_sdp_m_find_attribute(remote_m_line, "t38faxmaxdatagram", -1);
	if (!a_line) {
		a_line = ast_sdp_m_find_attribute(remote_m_line, "t38maxdatagram", -1);
	}
	if (a_line && !ast_sdp_options_get_udptl_far_max_datagram(options) &&
		(sscanf(a_line->value, "%30u", &fax_max_datagram) == 1)) {
		ast_udptl_set_far_max_datagram(udptl->instance, fax_max_datagram);
	}

	a_line = ast_sdp_m_find_attribute(remote_m_line, "t38faxudpec", -1);
	if (a_line) {
		if (!strcasecmp(a_line->value, "t38UDPRedundancy")) {
			ast_udptl_set_error_correction_scheme(udptl->instance, UDPTL_ERROR_CORRECTION_REDUNDANCY);
		} else if (!strcasecmp(a_line->value, "t38UDPFEC")) {
			ast_udptl_set_error_correction_scheme(udptl->instance, UDPTL_ERROR_CORRECTION_FEC);
		} else {
			ast_udptl_set_error_correction_scheme(udptl->instance, UDPTL_ERROR_CORRECTION_NONE);
		}
	}

	c_line = remote_sdp->c_line;
	if (remote_m_line->c_line) {
		c_line = remote_m_line->c_line;
	}

	if (ast_sockaddr_resolve(&addrs, c_line->address, PARSE_PORT_FORBID, AST_AF_UNSPEC) > 0) {
		ast_sockaddr_set_port(addrs, remote_m_line->port);
		ast_udptl_set_peer(udptl->instance, addrs);
		ast_free(addrs);
	}
}

static void set_negotiated_capabilities(struct ast_sdp_state *sdp_state,
	struct sdp_state_capabilities *new_capabilities)
{
	struct sdp_state_capabilities *old_capabilities = sdp_state->negotiated_capabilities;

	sdp_state->negotiated_capabilities = new_capabilities;
	sdp_state_capabilities_free(old_capabilities);
}

static void set_proposed_capabilities(struct ast_sdp_state *sdp_state,
	struct sdp_state_capabilities *new_capabilities)
{
	struct sdp_state_capabilities *old_capabilities = sdp_state->proposed_capabilities;

	sdp_state->proposed_capabilities = new_capabilities;
	sdp_state_capabilities_free(old_capabilities);
}

static struct ast_sdp *sdp_create_from_state(const struct ast_sdp_state *sdp_state,
	const struct sdp_state_capabilities *capabilities);

/*!
 * \brief Merge SDPs into a joint SDP.
 *
 * This function is used to take a remote SDP and merge it with our local
 * capabilities to produce a new local SDP. After creating the new local SDP,
 * it then iterates through media instances and updates them as necessary. For
 * instance, if a specific RTP feature is supported by both us and the far end,
 * then we can ensure that the feature is enabled.
 *
 * \param sdp_state The current SDP state
 * \retval -1 Failure
 * \retval 0 Success
 */
static int merge_sdps(struct ast_sdp_state *sdp_state, const struct ast_sdp *remote_sdp)
{
	struct sdp_state_capabilities *joint_capabilities;
	int i;

	sdp_state->remote_capabilities = ast_get_topology_from_sdp(remote_sdp);
	if (!sdp_state->remote_capabilities) {
		return -1;
	}

	joint_capabilities = merge_capabilities(sdp_state->proposed_capabilities,
		sdp_state->remote_capabilities, sdp_state->options, 0);
	if (!joint_capabilities) {
		return -1;
	}
	set_negotiated_capabilities(sdp_state, joint_capabilities);

	if (sdp_state->local_sdp) {
		ast_sdp_free(sdp_state->local_sdp);
		sdp_state->local_sdp = NULL;
	}

	sdp_state->local_sdp = sdp_create_from_state(sdp_state, joint_capabilities);
	if (!sdp_state->local_sdp) {
		return -1;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&joint_capabilities->streams); ++i) {
		struct sdp_state_stream *state_stream;

		state_stream = AST_VECTOR_GET(&joint_capabilities->streams, i);

		switch (ast_stream_get_type(ast_stream_topology_get_stream(joint_capabilities->topology, i))) {
		case AST_MEDIA_TYPE_AUDIO:
		case AST_MEDIA_TYPE_VIDEO:
			update_rtp_after_merge(sdp_state, state_stream->instance, sdp_state->options,
				remote_sdp, ast_sdp_get_m(remote_sdp, i));
			break;
		case AST_MEDIA_TYPE_IMAGE:
			update_udptl_after_merge(sdp_state, state_stream->udptl, sdp_state->options,
				remote_sdp, ast_sdp_get_m(remote_sdp, i));
			break;
		case AST_MEDIA_TYPE_UNKNOWN:
		case AST_MEDIA_TYPE_TEXT:
		case AST_MEDIA_TYPE_END:
			break;
		}
	}

	return 0;
}

const struct ast_sdp *ast_sdp_state_get_local_sdp(struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	if (sdp_state->role == SDP_ROLE_NOT_SET) {
		ast_assert(sdp_state->local_sdp == NULL);
		sdp_state->role = SDP_ROLE_OFFERER;
		sdp_state->local_sdp = sdp_create_from_state(sdp_state, sdp_state->proposed_capabilities);
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

int ast_sdp_state_set_remote_sdp(struct ast_sdp_state *sdp_state, const struct ast_sdp *sdp)
{
	ast_assert(sdp_state != NULL);

	if (sdp_state->role == SDP_ROLE_NOT_SET) {
		sdp_state->role = SDP_ROLE_ANSWERER;
	}

	return merge_sdps(sdp_state, sdp);
}

int ast_sdp_state_set_remote_sdp_from_impl(struct ast_sdp_state *sdp_state, void *remote)
{
	struct ast_sdp *sdp;
	int ret;

	ast_assert(sdp_state != NULL);

	sdp = ast_sdp_translator_to_sdp(sdp_state->translator, remote);
	if (!sdp) {
		return -1;
	}
	ret = ast_sdp_state_set_remote_sdp(sdp_state, sdp);
	ast_sdp_free(sdp);
	return ret;
}

int ast_sdp_state_reset(struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	ast_sdp_free(sdp_state->local_sdp);
	sdp_state->local_sdp = NULL;

	ast_stream_topology_free(sdp_state->remote_capabilities);
	sdp_state->remote_capabilities = NULL;

	set_proposed_capabilities(sdp_state, NULL);

	sdp_state->role = SDP_ROLE_NOT_SET;

	return 0;
}

int ast_sdp_state_update_local_topology(struct ast_sdp_state *sdp_state, struct ast_stream_topology *streams)
{
	struct sdp_state_capabilities *capabilities;
	ast_assert(sdp_state != NULL);
	ast_assert(streams != NULL);

	capabilities = merge_capabilities(sdp_state->proposed_capabilities, streams, sdp_state->options, 1);
	if (!capabilities) {
		return -1;
	}
	set_proposed_capabilities(sdp_state, capabilities);

	return 0;
}

void ast_sdp_state_set_local_address(struct ast_sdp_state *sdp_state, struct ast_sockaddr *address)
{
	ast_assert(sdp_state != NULL);

	if (!address) {
		ast_sockaddr_setnull(&sdp_state->proposed_capabilities->connection_address);
	} else {
		ast_sockaddr_copy(&sdp_state->proposed_capabilities->connection_address, address);
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

void ast_sdp_state_set_t38_parameters(struct ast_sdp_state *sdp_state,
	int stream_index, struct ast_control_t38_parameters *params)
{
	struct sdp_state_stream *stream_state;
	ast_assert(sdp_state != NULL && params != NULL);

	stream_state = sdp_state_get_stream(sdp_state, stream_index);
	if (!stream_state) {
		return;
	}

	stream_state->t38_local_params = *params;
}

static int sdp_add_m_from_rtp_stream(struct ast_sdp *sdp, const struct ast_sdp_state *sdp_state,
	const struct ast_sdp_options *options, const struct sdp_state_capabilities *capabilities, int stream_index)
{
	struct ast_stream *stream;
	struct ast_sdp_m_line *m_line;
	struct ast_format_cap *caps;
	int i;
	int rtp_code;
	int min_packet_size = 0;
	int max_packet_size = 0;
	enum ast_media_type media_type;
	char tmp[64];
	struct ast_sockaddr address_rtp;
	struct ast_rtp_instance *rtp;
	struct ast_sdp_a_line *a_line;

	stream = ast_stream_topology_get_stream(capabilities->topology, stream_index);
	rtp = AST_VECTOR_GET(&capabilities->streams, stream_index)->instance;

	ast_assert(sdp && options && stream);

	media_type = ast_stream_get_type(stream);
	if (rtp) {
		if (ast_sdp_state_get_stream_connection_address(sdp_state, 0, &address_rtp)) {
			return -1;
		}
	} else {
		ast_sockaddr_setnull(&address_rtp);
	}

	m_line = ast_sdp_m_alloc(
		ast_codec_media_type2str(ast_stream_get_type(stream)),
		ast_sockaddr_port(&address_rtp), 1,
		options->encryption != AST_SDP_ENCRYPTION_DISABLED ? "RTP/SAVP" : "RTP/AVP",
		NULL);
	if (!m_line) {
		return -1;
	}

	caps = ast_stream_get_formats(stream);

	for (i = 0; i < ast_format_cap_count(caps); i++) {
		struct ast_format *format = ast_format_cap_get_format(caps, i);

		if ((rtp_code = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(rtp), 1, format, 0)) == -1) {
			ast_log(LOG_WARNING,"Unable to get rtp codec payload code for %s\n", ast_format_get_name(format));
			ao2_ref(format, -1);
			continue;
		}

		if (ast_sdp_m_add_format(m_line, options, rtp_code, 1, format, 0)) {
			ast_sdp_m_free(m_line);
			ao2_ref(format, -1);
			return -1;
		}

		if (ast_format_get_maximum_ms(format) &&
			((ast_format_get_maximum_ms(format) < max_packet_size) || !max_packet_size)) {
			max_packet_size = ast_format_get_maximum_ms(format);
		}

		ao2_ref(format, -1);
	}

	if (rtp && media_type != AST_MEDIA_TYPE_VIDEO) {
		for (i = 1LL; i <= AST_RTP_MAX; i <<= 1) {
			if (!(options->telephone_event & i)) {
				continue;
			}

			rtp_code = ast_rtp_codecs_payload_code(
				ast_rtp_instance_get_codecs(rtp), 0, NULL, i);
			if (rtp_code == -1) {
				continue;
			}

			if (ast_sdp_m_add_format(m_line, options, rtp_code, 0, NULL, i)) {
				ast_sdp_m_free(m_line);
				return -1;
			}

			if (i == AST_RTP_DTMF) {
				snprintf(tmp, sizeof(tmp), "%d 0-16", rtp_code);
				a_line = ast_sdp_a_alloc("fmtp", tmp);
				if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
					ast_sdp_a_free(a_line);
					ast_sdp_m_free(m_line);
					return -1;
				}
			}
		}
	}

	if (ast_sdp_m_get_a_count(m_line) == 0) {
		ast_sdp_m_free(m_line);
		return 0;
	}

	/* If ptime is set add it as an attribute */
	min_packet_size = ast_rtp_codecs_get_framing(ast_rtp_instance_get_codecs(rtp));
	if (!min_packet_size) {
		min_packet_size = ast_format_cap_get_framing(caps);
	}
	if (min_packet_size) {
		snprintf(tmp, sizeof(tmp), "%d", min_packet_size);

		a_line = ast_sdp_a_alloc("ptime", tmp);
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
	}

	if (max_packet_size) {
		snprintf(tmp, sizeof(tmp), "%d", max_packet_size);
		a_line = ast_sdp_a_alloc("maxptime", tmp);
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
	}

	a_line = ast_sdp_a_alloc(ast_sdp_state_get_locally_held(sdp_state, stream_index) ? "sendonly" : "sendrecv", "");
	if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
		ast_sdp_a_free(a_line);
		ast_sdp_m_free(m_line);
		return -1;
	}

	if (ast_sdp_add_m(sdp, m_line)) {
		ast_sdp_m_free(m_line);
		return -1;
	}

	return 0;
}

/*! \brief Get Max T.38 Transmission rate from T38 capabilities */
static unsigned int t38_get_rate(enum ast_control_t38_rate rate)
{
	switch (rate) {
	case AST_T38_RATE_2400:
		return 2400;
	case AST_T38_RATE_4800:
		return 4800;
	case AST_T38_RATE_7200:
		return 7200;
	case AST_T38_RATE_9600:
		return 9600;
	case AST_T38_RATE_12000:
		return 12000;
	case AST_T38_RATE_14400:
		return 14400;
	default:
		return 0;
	}
}

static int sdp_add_m_from_udptl_stream(struct ast_sdp *sdp, const struct ast_sdp_state *sdp_state,
	const struct ast_sdp_options *options, const struct sdp_state_capabilities *capabilities, int stream_index)
{
	struct ast_stream *stream;
	struct ast_sdp_m_line *m_line;
	struct ast_sdp_payload *payload;
	char tmp[64];
	struct ast_sockaddr address_udptl;
	struct sdp_state_udptl *udptl;
	struct ast_sdp_a_line *a_line;
	struct sdp_state_stream *stream_state;

	stream = ast_stream_topology_get_stream(capabilities->topology, stream_index);
	udptl = AST_VECTOR_GET(&capabilities->streams, stream_index)->udptl;

	ast_assert(sdp && options && stream);

	if (udptl) {
		if (ast_sdp_state_get_stream_connection_address(sdp_state, 0, &address_udptl)) {
			return -1;
		}
	} else {
		ast_sockaddr_setnull(&address_udptl);
	}

	m_line = ast_sdp_m_alloc(
		ast_codec_media_type2str(ast_stream_get_type(stream)),
		ast_sockaddr_port(&address_udptl), 1, "udptl", NULL);
	if (!m_line) {
		return -1;
	}

	payload = ast_sdp_payload_alloc("t38");
	if (!payload || ast_sdp_m_add_payload(m_line, payload)) {
		ast_sdp_payload_free(payload);
		ast_sdp_m_free(m_line);
		return -1;
	}

	stream_state = sdp_state_get_stream(sdp_state, stream_index);

	snprintf(tmp, sizeof(tmp), "%u", stream_state->t38_local_params.version);
	a_line = ast_sdp_a_alloc("T38FaxVersion", tmp);
	if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
		ast_sdp_a_free(a_line);
		ast_sdp_m_free(m_line);
		return -1;
	}

	snprintf(tmp, sizeof(tmp), "%u", t38_get_rate(stream_state->t38_local_params.rate));
	a_line = ast_sdp_a_alloc("T38FaxMaxBitRate", tmp);
	if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
		ast_sdp_a_free(a_line);
		ast_sdp_m_free(m_line);
		return -1;
	}

	if (stream_state->t38_local_params.fill_bit_removal) {
		a_line = ast_sdp_a_alloc("T38FaxFillBitRemoval", "");
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
	}

	if (stream_state->t38_local_params.transcoding_mmr) {
		a_line = ast_sdp_a_alloc("T38FaxTranscodingMMR", "");
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
	}

	if (stream_state->t38_local_params.transcoding_jbig) {
		a_line = ast_sdp_a_alloc("T38FaxTranscodingJBIG", "");
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
	}

	switch (stream_state->t38_local_params.rate_management) {
	case AST_T38_RATE_MANAGEMENT_TRANSFERRED_TCF:
		a_line = ast_sdp_a_alloc("T38FaxRateManagement", "transferredTCF");
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
		break;
	case AST_T38_RATE_MANAGEMENT_LOCAL_TCF:
		a_line = ast_sdp_a_alloc("T38FaxRateManagement", "localTCF");
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
		break;
	}

	snprintf(tmp, sizeof(tmp), "%u", ast_udptl_get_local_max_datagram(udptl->instance));
	a_line = ast_sdp_a_alloc("T38FaxMaxDatagram", tmp);
	if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
		ast_sdp_a_free(a_line);
		ast_sdp_m_free(m_line);
		return -1;
	}

	switch (ast_udptl_get_error_correction_scheme(udptl->instance)) {
	case UDPTL_ERROR_CORRECTION_NONE:
		break;
	case UDPTL_ERROR_CORRECTION_FEC:
		a_line = ast_sdp_a_alloc("T38FaxUdpEC", "t38UDPFEC");
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
		break;
	case UDPTL_ERROR_CORRECTION_REDUNDANCY:
		a_line = ast_sdp_a_alloc("T38FaxUdpEC", "t38UDPRedundancy");
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
		break;
	}

	if (ast_sdp_add_m(sdp, m_line)) {
		ast_sdp_m_free(m_line);
		return -1;
	}

	return 0;
}

/*!
 * \brief Create an SDP based on current SDP state
 *
 * \param sdp_state The current SDP state
 * \retval NULL Failed to create SDP
 * \retval non-NULL Newly-created SDP
 */
static struct ast_sdp *sdp_create_from_state(const struct ast_sdp_state *sdp_state,
	const struct sdp_state_capabilities *capabilities)
{
	struct ast_sdp *sdp = NULL;
	struct ast_stream_topology *topology;
	const struct ast_sdp_options *options;
	int stream_num;
	struct ast_sdp_o_line *o_line = NULL;
	struct ast_sdp_c_line *c_line = NULL;
	struct ast_sdp_s_line *s_line = NULL;
	struct ast_sdp_t_line *t_line = NULL;
	char *address_type;
	struct timeval tv = ast_tvnow();
	uint32_t t;
	int stream_count;

	options = ast_sdp_state_get_options(sdp_state);
	topology = capabilities->topology;

	t = tv.tv_sec + 2208988800UL;
	address_type = (strchr(options->media_address, ':') ? "IP6" : "IP4");

	o_line = ast_sdp_o_alloc(options->sdpowner, t, t, address_type, options->media_address);
	if (!o_line) {
		goto error;
	}
	c_line = ast_sdp_c_alloc(address_type, options->media_address);
	if (!c_line) {
		goto error;
	}

	s_line = ast_sdp_s_alloc(options->sdpsession);
	if (!s_line) {
		goto error;
	}

	sdp = ast_sdp_alloc(o_line, c_line, s_line, NULL);
	if (!sdp) {
		goto error;
	}

	stream_count = ast_stream_topology_get_count(topology);

	for (stream_num = 0; stream_num < stream_count; stream_num++) {
		switch (ast_stream_get_type(ast_stream_topology_get_stream(topology, stream_num))) {
		case AST_MEDIA_TYPE_AUDIO:
		case AST_MEDIA_TYPE_VIDEO:
			if (sdp_add_m_from_rtp_stream(sdp, sdp_state, options, capabilities, stream_num)) {
				goto error;
			}
			break;
		case AST_MEDIA_TYPE_IMAGE:
			if (sdp_add_m_from_udptl_stream(sdp, sdp_state, options, capabilities, stream_num)) {
				goto error;
			}
			break;
		case AST_MEDIA_TYPE_UNKNOWN:
		case AST_MEDIA_TYPE_TEXT:
		case AST_MEDIA_TYPE_END:
			break;
		}
	}

	return sdp;

error:
	if (sdp) {
		ast_sdp_free(sdp);
	} else {
		ast_sdp_t_free(t_line);
		ast_sdp_s_free(s_line);
		ast_sdp_c_free(c_line);
		ast_sdp_o_free(o_line);
	}

	return NULL;
}

