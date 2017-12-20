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

struct sdp_state_rtp {
	/*! The underlying RTP instance */
	struct ast_rtp_instance *instance;
};

struct sdp_state_udptl {
	/*! The underlying UDPTL instance */
	struct ast_udptl *instance;
};

struct sdp_state_stream {
	/*! Type of the stream */
	enum ast_media_type type;
	union {
		/*! The underlying RTP instance */
		struct sdp_state_rtp *rtp;
		/*! The underlying UDPTL instance */
		struct sdp_state_udptl *udptl;
	};
	/*! An explicit connection address for this stream */
	struct ast_sockaddr connection_address;
	/*!
	 * \brief Stream is on hold by remote side
	 *
	 * \note This flag is never set on the
	 * sdp_state->proposed_capabilities->streams states.  This is useful
	 * when the remote sends us a reINVITE with a deferred SDP to place
	 * us on and off of hold.
	 */
	unsigned int remotely_held:1;
	/*! Stream is on hold by local side */
	unsigned int locally_held:1;
	/*! UDPTL session parameters */
	struct ast_control_t38_parameters t38_local_params;
};

static int sdp_is_stream_type_supported(enum ast_media_type type)
{
	int is_supported = 0;

	switch (type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
	case AST_MEDIA_TYPE_IMAGE:
		is_supported = 1;
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_TEXT:
	case AST_MEDIA_TYPE_END:
		break;
	}
	return is_supported;
}

static void sdp_state_rtp_destroy(void *obj)
{
	struct sdp_state_rtp *rtp = obj;

	if (rtp->instance) {
		ast_rtp_instance_stop(rtp->instance);
		ast_rtp_instance_destroy(rtp->instance);
	}
}

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
		ao2_cleanup(state_stream->rtp);
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

/*! \brief Internal function which creates an RTP instance */
static struct sdp_state_rtp *create_rtp(const struct ast_sdp_options *options,
	enum ast_media_type media_type)
{
	struct sdp_state_rtp *rtp;
	struct ast_rtp_engine_ice *ice;
	static struct ast_sockaddr address_rtp;
	struct ast_sockaddr *media_address = &address_rtp;

	if (!ast_strlen_zero(options->interface_address)) {
		if (!ast_sockaddr_parse(&address_rtp, options->interface_address, 0)) {
			ast_log(LOG_ERROR, "Attempted to bind RTP to invalid media address: %s\n",
				options->interface_address);
			return NULL;
		}
	} else {
		if (ast_check_ipv6()) {
			ast_sockaddr_parse(&address_rtp, "::", 0);
		} else {
			ast_sockaddr_parse(&address_rtp, "0.0.0.0", 0);
		}
	}

	rtp = ao2_alloc_options(sizeof(*rtp), sdp_state_rtp_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!rtp) {
		return NULL;
	}

	rtp->instance = ast_rtp_instance_new(options->rtp_engine,
		ast_sdp_options_get_sched_type(options, media_type), media_address, NULL);
	if (!rtp->instance) {
		ast_log(LOG_ERROR, "Unable to create RTP instance using RTP engine '%s'\n",
			options->rtp_engine);
		ao2_ref(rtp, -1);
		return NULL;
	}

	ast_rtp_instance_set_prop(rtp->instance, AST_RTP_PROPERTY_RTCP,
		AST_RTP_INSTANCE_RTCP_STANDARD);
	ast_rtp_instance_set_prop(rtp->instance, AST_RTP_PROPERTY_NAT,
		options->rtp_symmetric);

	if (options->ice == AST_SDP_ICE_DISABLED
		&& (ice = ast_rtp_instance_get_ice(rtp->instance))) {
		ice->stop(rtp->instance);
	}

	if (options->dtmf == AST_SDP_DTMF_RFC_4733 || options->dtmf == AST_SDP_DTMF_AUTO) {
		ast_rtp_instance_dtmf_mode_set(rtp->instance, AST_RTP_DTMF_MODE_RFC2833);
		ast_rtp_instance_set_prop(rtp->instance, AST_RTP_PROPERTY_DTMF, 1);
	} else if (options->dtmf == AST_SDP_DTMF_INBAND) {
		ast_rtp_instance_dtmf_mode_set(rtp->instance, AST_RTP_DTMF_MODE_INBAND);
	}

	switch (media_type) {
	case AST_MEDIA_TYPE_AUDIO:
		if (options->tos_audio || options->cos_audio) {
			ast_rtp_instance_set_qos(rtp->instance, options->tos_audio,
				options->cos_audio, "SIP RTP Audio");
		}
		break;
	case AST_MEDIA_TYPE_VIDEO:
		if (options->tos_video || options->cos_video) {
			ast_rtp_instance_set_qos(rtp->instance, options->tos_video,
				options->cos_video, "SIP RTP Video");
		}
		break;
	case AST_MEDIA_TYPE_IMAGE:
	case AST_MEDIA_TYPE_TEXT:
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_END:
		break;
	}

	ast_rtp_instance_set_last_rx(rtp->instance, time(NULL));

	return rtp;
}

/*! \brief Internal function which creates a UDPTL instance */
static struct sdp_state_udptl *create_udptl(const struct ast_sdp_options *options)
{
	struct sdp_state_udptl *udptl;
	static struct ast_sockaddr address_udptl;
	struct ast_sockaddr *media_address = &address_udptl;

	if (!ast_strlen_zero(options->interface_address)) {
		if (!ast_sockaddr_parse(&address_udptl, options->interface_address, 0)) {
			ast_log(LOG_ERROR, "Attempted to bind UDPTL to invalid media address: %s\n",
				options->interface_address);
			return NULL;
		}
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

static struct ast_stream *merge_local_stream(const struct ast_sdp_options *options,
	const struct ast_stream *update);

static struct sdp_state_capabilities *sdp_initialize_state_capabilities(const struct ast_stream_topology *topology,
	const struct ast_sdp_options *options)
{
	struct sdp_state_capabilities *capabilities;
	struct ast_stream *stream;
	unsigned int topology_count;
	unsigned int max_streams;
	unsigned int idx;

	capabilities = ast_calloc(1, sizeof(*capabilities));
	if (!capabilities) {
		return NULL;
	}

	capabilities->topology = ast_stream_topology_alloc();
	if (!capabilities->topology) {
		sdp_state_capabilities_free(capabilities);
		return NULL;
	}

	max_streams = ast_sdp_options_get_max_streams(options);
	if (topology) {
		topology_count = ast_stream_topology_get_count(topology);
	} else {
		topology_count = 0;
	}

	/* Gather acceptable streams from the initial topology */
	for (idx = 0; idx < topology_count; ++idx) {
		stream = ast_stream_topology_get_stream(topology, idx);
		if (!sdp_is_stream_type_supported(ast_stream_get_type(stream))) {
			/* Delete the unsupported stream from the initial topology */
			continue;
		}
		if (max_streams <= ast_stream_topology_get_count(capabilities->topology)) {
			/* Cannot support any more streams */
			break;
		}

		stream = merge_local_stream(options, stream);
		if (!stream) {
			sdp_state_capabilities_free(capabilities);
			return NULL;
		}

		if (ast_stream_topology_append_stream(capabilities->topology, stream) < 0) {
			ast_stream_free(stream);
			sdp_state_capabilities_free(capabilities);
			return NULL;
		}
	}

	/*
	 * Remove trailing declined streams from the initial built topology.
	 * No need to waste space in the SDP with these unused slots.
	 */
	for (idx = ast_stream_topology_get_count(capabilities->topology); idx--;) {
		stream = ast_stream_topology_get_stream(capabilities->topology, idx);
		if (ast_stream_get_state(stream) != AST_STREAM_STATE_REMOVED) {
			break;
		}
		ast_stream_topology_del_stream(capabilities->topology, idx);
	}

	topology_count = ast_stream_topology_get_count(capabilities->topology);
	if (AST_VECTOR_INIT(&capabilities->streams, topology_count)) {
		sdp_state_capabilities_free(capabilities);
		return NULL;
	}

	for (idx = 0; idx < topology_count; ++idx) {
		struct sdp_state_stream *state_stream;

		state_stream = ast_calloc(1, sizeof(*state_stream));
		if (!state_stream) {
			sdp_state_capabilities_free(capabilities);
			return NULL;
		}

		stream = ast_stream_topology_get_stream(capabilities->topology, idx);
		state_stream->type = ast_stream_get_type(stream);
		if (ast_stream_get_state(stream) != AST_STREAM_STATE_REMOVED) {
			switch (state_stream->type) {
			case AST_MEDIA_TYPE_AUDIO:
			case AST_MEDIA_TYPE_VIDEO:
				state_stream->rtp = create_rtp(options, state_stream->type);
				if (!state_stream->rtp) {
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
				/* Unsupported stream type already handled earlier */
				ast_assert(0);
				break;
			}
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
 * to generate the offer SDP. When the answer SDP arrives, the proposed capabilities
 * are merged with the answer SDP to create the negotiated capabilities.
 *
 * If we are the SDP answerer, then the incoming offer SDP is merged with our
 * proposed capabilities to create the negotiated capabilities. These negotiated
 * capabilities are what we send in our answer SDP.
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
	/*!
	 * \brief New topology waiting to be merged.
	 *
	 * \details
	 * Repeated topology updates are merged into each other here until
	 * negotiations are restarted and we create an offer.
	 */
	struct ast_stream_topology *pending_topology_update;
	/*! Local SDP. Generated via the options and negotiated/proposed capabilities. */
	struct ast_sdp *local_sdp;
	/*! Saved remote SDP */
	struct ast_sdp *remote_sdp;
	/*! SDP options. Configured options beyond media capabilities. */
	struct ast_sdp_options *options;
	/*! Translator that puts SDPs into the expected representation */
	struct ast_sdp_translator *translator;
	/*! An explicit global connection address */
	struct ast_sockaddr connection_address;
	/*! The role that we occupy in SDP negotiation */
	enum ast_sdp_role role;
	/*! TRUE if all streams on hold by local side */
	unsigned int locally_held:1;
	/*! TRUE if the remote offer resulted in all streams being declined. */
	unsigned int remote_offer_rejected:1;
};

struct ast_sdp_state *ast_sdp_state_alloc(struct ast_stream_topology *topology,
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

	sdp_state->proposed_capabilities = sdp_initialize_state_capabilities(topology, options);
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
	ao2_cleanup(sdp_state->local_sdp);
	ao2_cleanup(sdp_state->remote_sdp);
	ast_sdp_options_free(sdp_state->options);
	ast_sdp_translator_free(sdp_state->translator);
	ast_free(sdp_state);
}

/*!
 * \internal
 * \brief Allow a configured callback to alter the new negotiated joint topology.
 * \since 15.0.0
 *
 * \details
 * The callback can alter topology stream names, formats, or decline streams.
 *
 * \param sdp_state
 * \param topology Joint topology that we intend to generate the answer SDP.
 *
 * \return Nothing
 */
static void sdp_state_cb_answerer_modify_topology(const struct ast_sdp_state *sdp_state,
	struct ast_stream_topology *topology)
{
	ast_sdp_answerer_modify_cb cb;

	cb = ast_sdp_options_get_answerer_modify_cb(sdp_state->options);
	if (cb) {
		void *context;
		const struct ast_stream_topology *neg_topology;/*!< Last negotiated topology */
#ifdef AST_DEVMODE
		struct ast_stream *stream;
		int idx;
		enum ast_media_type type[ast_stream_topology_get_count(topology)];
		enum ast_stream_state state[ast_stream_topology_get_count(topology)];

		/*
		 * Save stream types and states to validate that they don't
		 * get changed unexpectedly.
		 */
		for (idx = 0; idx < ast_stream_topology_get_count(topology); ++idx) {
			stream = ast_stream_topology_get_stream(topology, idx);
			type[idx] = ast_stream_get_type(stream);
			state[idx] = ast_stream_get_state(stream);
		}
#endif

		context = ast_sdp_options_get_state_context(sdp_state->options);
		neg_topology = sdp_state->negotiated_capabilities
			? sdp_state->negotiated_capabilities->topology : NULL;
		cb(context, neg_topology, topology);

#ifdef AST_DEVMODE
		for (idx = 0; idx < ast_stream_topology_get_count(topology); ++idx) {
			stream = ast_stream_topology_get_stream(topology, idx);

			/* Check that active streams have at least one format */
			ast_assert(ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED
				|| (ast_stream_get_formats(stream)
					&& ast_format_cap_count(ast_stream_get_formats(stream))));

			/* Check that stream types didn't change. */
			ast_assert(type[idx] == ast_stream_get_type(stream));

			/* Check that streams didn't get resurected. */
			ast_assert(state[idx] != AST_STREAM_STATE_REMOVED
				|| ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED);
		}
#endif
	}
}

/*!
 * \internal
 * \brief Allow a configured callback to alter the merged local topology.
 * \since 15.0.0
 *
 * \details
 * The callback can modify streams in the merged topology.  The
 * callback can decline, add/remove/update formats, or rename
 * streams.  Changing anything else on the streams is likely to not
 * end well.
 *
 * \param sdp_state
 * \param topology Merged topology that we intend to generate the offer SDP.
 *
 * \return Nothing
 */
static void sdp_state_cb_offerer_modify_topology(const struct ast_sdp_state *sdp_state,
	struct ast_stream_topology *topology)
{
	ast_sdp_offerer_modify_cb cb;

	cb = ast_sdp_options_get_offerer_modify_cb(sdp_state->options);
	if (cb) {
		void *context;
		const struct ast_stream_topology *neg_topology;/*!< Last negotiated topology */

		context = ast_sdp_options_get_state_context(sdp_state->options);
		neg_topology = sdp_state->negotiated_capabilities
			? sdp_state->negotiated_capabilities->topology : NULL;
		cb(context, neg_topology, topology);

#ifdef AST_DEVMODE
		{
			struct ast_stream *stream;
			int idx;

			/* Check that active streams have at least one format */
			for (idx = 0; idx < ast_stream_topology_get_count(topology); ++idx) {
				stream = ast_stream_topology_get_stream(topology, idx);
				ast_assert(ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED
					|| (ast_stream_get_formats(stream)
						&& ast_format_cap_count(ast_stream_get_formats(stream))));
			}
		}
#endif
	}
}

/*!
 * \internal
 * \brief Allow a configured callback to configure the merged local topology.
 * \since 15.0.0
 *
 * \details
 * The callback can configure other parameters associated with each
 * active stream on the topology.  The callback can call several SDP
 * API calls to configure the proposed capabilities of the streams
 * before we create the offer SDP.  For example, the callback could
 * configure a stream specific connection address, T.38 parameters,
 * RTP instance, or UDPTL instance parameters.
 *
 * \param sdp_state
 * \param topology Merged topology that we intend to generate the offer SDP.
 *
 * \return Nothing
 */
static void sdp_state_cb_offerer_config_topology(const struct ast_sdp_state *sdp_state,
	const struct ast_stream_topology *topology)
{
	ast_sdp_offerer_config_cb cb;

	cb = ast_sdp_options_get_offerer_config_cb(sdp_state->options);
	if (cb) {
		void *context;

		context = ast_sdp_options_get_state_context(sdp_state->options);
		cb(context, topology);
	}
}

/*!
 * \internal
 * \brief Call any registered pre-apply topology callback.
 * \since 15.0.0
 *
 * \param sdp_state
 * \param topology
 *
 * \return Nothing
 */
static void sdp_state_cb_preapply_topology(const struct ast_sdp_state *sdp_state,
	const struct ast_stream_topology *topology)
{
	ast_sdp_preapply_cb cb;

	cb = ast_sdp_options_get_preapply_cb(sdp_state->options);
	if (cb) {
		void *context;

		context = ast_sdp_options_get_state_context(sdp_state->options);
		cb(context, topology);
	}
}

/*!
 * \internal
 * \brief Call any registered post-apply topology callback.
 * \since 15.0.0
 *
 * \param sdp_state
 * \param topology
 *
 * \return Nothing
 */
static void sdp_state_cb_postapply_topology(const struct ast_sdp_state *sdp_state,
	const struct ast_stream_topology *topology)
{
	ast_sdp_postapply_cb cb;

	cb = ast_sdp_options_get_postapply_cb(sdp_state->options);
	if (cb) {
		void *context;

		context = ast_sdp_options_get_state_context(sdp_state->options);
		cb(context, topology);
	}
}

static const struct sdp_state_capabilities *sdp_state_get_joint_capabilities(
	const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	if (sdp_state->negotiated_capabilities) {
		return sdp_state->negotiated_capabilities;
	}

	return sdp_state->proposed_capabilities;
}

static struct sdp_state_stream *sdp_state_get_stream(const struct ast_sdp_state *sdp_state, int stream_index)
{
	if (stream_index >= AST_VECTOR_SIZE(&sdp_state->proposed_capabilities->streams)) {
		return NULL;
	}

	return AST_VECTOR_GET(&sdp_state->proposed_capabilities->streams, stream_index);
}

static struct sdp_state_stream *sdp_state_get_joint_stream(const struct ast_sdp_state *sdp_state, int stream_index)
{
	const struct sdp_state_capabilities *capabilities;

	capabilities = sdp_state_get_joint_capabilities(sdp_state);
	if (AST_VECTOR_SIZE(&capabilities->streams) <= stream_index) {
		return NULL;
	}

	return AST_VECTOR_GET(&capabilities->streams, stream_index);
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
	if (!stream_state || !stream_state->rtp) {
		return NULL;
	}

	return stream_state->rtp->instance;
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

	return &sdp_state->connection_address;
}

static int sdp_state_stream_get_connection_address(const struct ast_sdp_state *sdp_state,
	struct sdp_state_stream *stream_state, struct ast_sockaddr *address)
{
	ast_assert(sdp_state != NULL);
	ast_assert(stream_state != NULL);
	ast_assert(address != NULL);

	/* If an explicit connection address has been provided for the stream return it */
	if (!ast_sockaddr_isnull(&stream_state->connection_address)) {
		ast_sockaddr_copy(address, &stream_state->connection_address);
		return 0;
	}

	switch (stream_state->type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
		if (!stream_state->rtp->instance) {
			return -1;
		}
		ast_rtp_instance_get_local_address(stream_state->rtp->instance, address);
		break;
	case AST_MEDIA_TYPE_IMAGE:
		if (!stream_state->udptl->instance) {
			return -1;
		}
		ast_udptl_get_us(stream_state->udptl->instance, address);
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_TEXT:
	case AST_MEDIA_TYPE_END:
		return -1;
	}

	if (ast_sockaddr_isnull(address)) {
		/* No address is set on the stream state. */
		return -1;
	}

	/* If an explicit global connection address is set use it here for the IP part */
	if (!ast_sockaddr_isnull(&sdp_state->connection_address)) {
		int port = ast_sockaddr_port(address);

		ast_sockaddr_copy(address, &sdp_state->connection_address);
		ast_sockaddr_set_port(address, port);
	}

	return 0;
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

	return sdp_state_stream_get_connection_address(sdp_state, stream_state, address);
}

const struct ast_stream_topology *ast_sdp_state_get_joint_topology(
	const struct ast_sdp_state *sdp_state)
{
	const struct sdp_state_capabilities *capabilities;

	capabilities = sdp_state_get_joint_capabilities(sdp_state);
	return capabilities->topology;
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

static struct ast_stream *decline_stream(enum ast_media_type type, const char *name)
{
	struct ast_stream *stream;

	if (!name) {
		name = ast_codec_media_type2str(type);
	}
	stream = ast_stream_alloc(name, type);
	if (!stream) {
		return NULL;
	}
	ast_stream_set_state(stream, AST_STREAM_STATE_REMOVED);
	return stream;
}

/*!
 * \brief Merge an update stream into a local stream.
 *
 * \param options SDP Options
 * \param update An updated stream
 *
 * \retval NULL An error occurred
 * \retval non-NULL The joint stream created
 */
static struct ast_stream *merge_local_stream(const struct ast_sdp_options *options,
	const struct ast_stream *update)
{
	struct ast_stream *joint_stream;
	struct ast_format_cap *joint_cap;
	struct ast_format_cap *allowed_cap;
	struct ast_format_cap *update_cap;
	enum ast_stream_state joint_state;

	joint_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!joint_cap) {
		return NULL;
	}

	update_cap = ast_stream_get_formats(update);
	allowed_cap = ast_sdp_options_get_format_cap_type(options,
		ast_stream_get_type(update));
	if (allowed_cap && update_cap) {
		struct ast_str *allowed_buf = ast_str_alloca(128);
		struct ast_str *update_buf = ast_str_alloca(128);
		struct ast_str *joint_buf = ast_str_alloca(128);

		ast_format_cap_get_compatible(allowed_cap, update_cap, joint_cap);
		ast_debug(3,
			"Filtered update '%s' with allowed '%s' to get joint '%s'. Joint has %zu formats\n",
			ast_format_cap_get_names(update_cap, &update_buf),
			ast_format_cap_get_names(allowed_cap, &allowed_buf),
			ast_format_cap_get_names(joint_cap, &joint_buf),
			ast_format_cap_count(joint_cap));
	}

	/* Determine the joint stream state */
	joint_state = AST_STREAM_STATE_REMOVED;
	if (ast_stream_get_state(update) != AST_STREAM_STATE_REMOVED
		&& ast_format_cap_count(joint_cap)) {
		joint_state = AST_STREAM_STATE_SENDRECV;
	}

	joint_stream = ast_stream_alloc(ast_stream_get_name(update),
		ast_stream_get_type(update));
	if (joint_stream) {
		ast_stream_set_state(joint_stream, joint_state);
		if (joint_state != AST_STREAM_STATE_REMOVED) {
			ast_stream_set_formats(joint_stream, joint_cap);
		}
	}

	ao2_ref(joint_cap, -1);

	return joint_stream;
}

/*!
 * \brief Merge a remote stream into a local stream.
 *
 * \param sdp_state
 * \param local Our local stream (NULL if creating new stream)
 * \param locally_held Nonzero if the local stream is held
 * \param remote A remote stream
 *
 * \retval NULL An error occurred
 * \retval non-NULL The joint stream created
 */
static struct ast_stream *merge_remote_stream(const struct ast_sdp_state *sdp_state,
	const struct ast_stream *local, unsigned int locally_held,
	const struct ast_stream *remote)
{
	struct ast_stream *joint_stream;
	struct ast_format_cap *joint_cap;
	struct ast_format_cap *local_cap;
	struct ast_format_cap *remote_cap;
	const char *joint_name;
	enum ast_stream_state joint_state;
	enum ast_stream_state remote_state;

	joint_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!joint_cap) {
		return NULL;
	}

	remote_cap = ast_stream_get_formats(remote);
	if (local) {
		local_cap = ast_stream_get_formats(local);
	} else {
		local_cap = ast_sdp_options_get_format_cap_type(sdp_state->options,
			ast_stream_get_type(remote));
	}
	if (local_cap && remote_cap) {
		struct ast_str *local_buf = ast_str_alloca(128);
		struct ast_str *remote_buf = ast_str_alloca(128);
		struct ast_str *joint_buf = ast_str_alloca(128);

		ast_format_cap_get_compatible(local_cap, remote_cap, joint_cap);
		ast_debug(3,
			"Combined local '%s' with remote '%s' to get joint '%s'. Joint has %zu formats\n",
			ast_format_cap_get_names(local_cap, &local_buf),
			ast_format_cap_get_names(remote_cap, &remote_buf),
			ast_format_cap_get_names(joint_cap, &joint_buf),
			ast_format_cap_count(joint_cap));
	}

	/* Determine the joint stream state */
	remote_state = ast_stream_get_state(remote);
	joint_state = AST_STREAM_STATE_REMOVED;
	if ((!local || ast_stream_get_state(local) != AST_STREAM_STATE_REMOVED)
		&& ast_format_cap_count(joint_cap)) {
		if (sdp_state->locally_held || locally_held) {
			switch (remote_state) {
			case AST_STREAM_STATE_REMOVED:
				break;
			case AST_STREAM_STATE_INACTIVE:
				joint_state = AST_STREAM_STATE_INACTIVE;
				break;
			case AST_STREAM_STATE_SENDRECV:
				joint_state = AST_STREAM_STATE_SENDONLY;
				break;
			case AST_STREAM_STATE_SENDONLY:
				joint_state = AST_STREAM_STATE_INACTIVE;
				break;
			case AST_STREAM_STATE_RECVONLY:
				joint_state = AST_STREAM_STATE_SENDONLY;
				break;
			}
		} else {
			switch (remote_state) {
			case AST_STREAM_STATE_REMOVED:
				break;
			case AST_STREAM_STATE_INACTIVE:
				joint_state = AST_STREAM_STATE_RECVONLY;
				break;
			case AST_STREAM_STATE_SENDRECV:
				joint_state = AST_STREAM_STATE_SENDRECV;
				break;
			case AST_STREAM_STATE_SENDONLY:
				joint_state = AST_STREAM_STATE_RECVONLY;
				break;
			case AST_STREAM_STATE_RECVONLY:
				joint_state = AST_STREAM_STATE_SENDRECV;
				break;
			}
		}
	}

	if (local) {
		joint_name = ast_stream_get_name(local);
	} else {
		joint_name = ast_codec_media_type2str(ast_stream_get_type(remote));
	}
	joint_stream = ast_stream_alloc(joint_name, ast_stream_get_type(remote));
	if (joint_stream) {
		ast_stream_set_state(joint_stream, joint_state);
		if (joint_state != AST_STREAM_STATE_REMOVED) {
			ast_stream_set_formats(joint_stream, joint_cap);
		}
	}

	ao2_ref(joint_cap, -1);

	return joint_stream;
}

/*!
 * \internal
 * \brief Determine if a merged topology should be rejected.
 * \since 15.0.0
 *
 * \param topology What topology to determine if we reject
 *
 * \retval 0 if not rejected.
 * \retval non-zero if rejected.
 */
static int sdp_topology_is_rejected(struct ast_stream_topology *topology)
{
	int idx;
	struct ast_stream *stream;

	for (idx = ast_stream_topology_get_count(topology); idx--;) {
		stream = ast_stream_topology_get_stream(topology, idx);
		if (ast_stream_get_state(stream) != AST_STREAM_STATE_REMOVED) {
			/* At least one stream is not declined */
			return 0;
		}
	}

	/* All streams are declined */
	return 1;
}

static void sdp_state_stream_copy_common(struct sdp_state_stream *dst, const struct sdp_state_stream *src)
{
	ast_sockaddr_copy(&dst->connection_address,
		&src->connection_address);
	/* Explicitly does not copy the local or remote hold states. */
	dst->t38_local_params = src->t38_local_params;
}

static void sdp_state_stream_copy(struct sdp_state_stream *dst, const struct sdp_state_stream *src)
{
	*dst = *src;

	switch (dst->type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
		ao2_bump(dst->rtp);
		break;
	case AST_MEDIA_TYPE_IMAGE:
		ao2_bump(dst->udptl);
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_TEXT:
	case AST_MEDIA_TYPE_END:
		break;
	}
}

/*!
 * \internal
 * \brief Initialize an int vector and default the contents to the member index.
 * \since 15.0.0
 *
 * \param vect Vetctor to initialize and set to default values.
 * \param size Size of the vector to setup.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int sdp_vect_idx_init(struct ast_vector_int *vect, size_t size)
{
	int idx;

	if (AST_VECTOR_INIT(vect, size)) {
		return -1;
	}
	for (idx = 0; idx < size; ++idx) {
		AST_VECTOR_APPEND(vect, idx);
	}
	return 0;
}

/*!
 * \internal
 * \brief Compare stream types for sort order.
 * \since 15.0.0
 *
 * \param left Stream parameter on left
 * \param right Stream parameter on right
 *
 * \retval <0 left stream sorts first.
 * \retval =0 streams match.
 * \retval >0 right stream sorts first.
 */
static int sdp_stream_cmp_by_type(const struct ast_stream *left, const struct ast_stream *right)
{
	enum ast_media_type left_type = ast_stream_get_type(left);
	enum ast_media_type right_type = ast_stream_get_type(right);

	/* Treat audio and image as the same for T.38 support */
	if (left_type == AST_MEDIA_TYPE_IMAGE) {
		left_type = AST_MEDIA_TYPE_AUDIO;
	}
	if (right_type == AST_MEDIA_TYPE_IMAGE) {
		right_type = AST_MEDIA_TYPE_AUDIO;
	}

	return left_type - right_type;
}

/*!
 * \internal
 * \brief Compare stream names and types for sort order.
 * \since 15.0.0
 *
 * \param left Stream parameter on left
 * \param right Stream parameter on right
 *
 * \retval <0 left stream sorts first.
 * \retval =0 streams match.
 * \retval >0 right stream sorts first.
 */
static int sdp_stream_cmp_by_name(const struct ast_stream *left, const struct ast_stream *right)
{
	int cmp;
	const char *left_name;

	left_name = ast_stream_get_name(left);
	cmp = strcmp(left_name, ast_stream_get_name(right));
	if (!cmp) {
		cmp = sdp_stream_cmp_by_type(left, right);
		if (!cmp) {
			/* Are the stream names real or type names which aren't matchable? */
			if (ast_strlen_zero(left_name)
				|| !strcmp(left_name, ast_codec_media_type2str(ast_stream_get_type(left)))
				|| !strcmp(left_name, ast_codec_media_type2str(ast_stream_get_type(right)))) {
				/* The streams don't actually have real names */
				cmp = -1;
			}
		}
	}
	return cmp;
}

/*!
 * \internal
 * \brief Merge topology streams by the match function.
 * \since 15.0.0
 *
 * \param sdp_state
 * \param current_topology Topology to update with state.
 * \param update_topology Topology to merge into the current topology.
 * \param current_vect Stream index vector of remaining current_topology streams.
 * \param update_vect Stream index vector of remaining update_topology streams.
 * \param backfill_candidate Array of flags marking current_topology streams
 *            that can be reused for a different stream.
 * \param match Stream comparison function to identify corresponding streams
 *            between the current_topology and update_topology.
 * \param merged_topology Output topology of merged streams.
 * \param compact_streams TRUE if backfill and limit number of streams.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int sdp_merge_streams_match(
	const struct ast_sdp_state *sdp_state,
	const struct ast_stream_topology *current_topology,
	const struct ast_stream_topology *update_topology,
	struct ast_vector_int *current_vect,
	struct ast_vector_int *update_vect,
	char backfill_candidate[],
	int (*match)(const struct ast_stream *left, const struct ast_stream *right),
	struct ast_stream_topology *merged_topology,
	int compact_streams)
{
	struct ast_stream *current_stream;
	struct ast_stream *update_stream;
	int current_idx;
	int update_idx;
	int idx;

	for (current_idx = 0; current_idx < AST_VECTOR_SIZE(current_vect);) {
		idx = AST_VECTOR_GET(current_vect, current_idx);
		current_stream = ast_stream_topology_get_stream(current_topology, idx);

		for (update_idx = 0; update_idx < AST_VECTOR_SIZE(update_vect); ++update_idx) {
			idx = AST_VECTOR_GET(update_vect, update_idx);
			update_stream = ast_stream_topology_get_stream(update_topology, idx);

			if (match(current_stream, update_stream)) {
				continue;
			}

			if (!compact_streams
				|| ast_stream_get_state(current_stream) != AST_STREAM_STATE_REMOVED
				|| ast_stream_get_state(update_stream) != AST_STREAM_STATE_REMOVED) {
				struct ast_stream *merged_stream;

				merged_stream = merge_local_stream(sdp_state->options, update_stream);
				if (!merged_stream) {
					return -1;
				}
				idx = AST_VECTOR_GET(current_vect, current_idx);
				if (ast_stream_topology_set_stream(merged_topology, idx, merged_stream)) {
					ast_stream_free(merged_stream);
					return -1;
				}

				/*
				 * The current_stream cannot be considered a backfill_candidate
				 * anymore since it got updated.
				 *
				 * XXX It could be argued that if the declined status didn't
				 * change because the merged_stream became declined then we
				 * shouldn't remove the stream slot as a backfill_candidate
				 * and we shouldn't update the merged_topology stream.  If we
				 * then backfilled the stream we would likely mess up the core
				 * if it is matching streams by type since the core attempted
				 * to update the stream with an incompatible stream.  Any
				 * backfilled streams could cause a stream type ordering
				 * problem.  However, we do need to reclaim declined stream
				 * slots sometime.
				 */
				backfill_candidate[idx] = 0;
			}

			AST_VECTOR_REMOVE_ORDERED(current_vect, current_idx);
			AST_VECTOR_REMOVE_ORDERED(update_vect, update_idx);
			goto matched_next;
		}

		++current_idx;
matched_next:;
	}
	return 0;
}

/*!
 * \internal
 * \brief Merge the current local topology with an updated topology.
 * \since 15.0.0
 *
 * \param sdp_state
 * \param current_topology Topology to update with state.
 * \param update_topology Topology to merge into the current topology.
 * \param compact_streams TRUE if backfill and limit number of streams.
 *
 * \retval merged topology on success.
 * \retval NULL on failure.
 */
static struct ast_stream_topology *merge_local_topologies(
	const struct ast_sdp_state *sdp_state,
	const struct ast_stream_topology *current_topology,
	const struct ast_stream_topology *update_topology,
	int compact_streams)
{
	struct ast_stream_topology *merged_topology;
	struct ast_stream *current_stream;
	struct ast_stream *update_stream;
	struct ast_stream *merged_stream;
	struct ast_vector_int current_vect;
	struct ast_vector_int update_vect;
	int current_idx = ast_stream_topology_get_count(current_topology);
	int update_idx;
	int idx;
	char backfill_candidate[current_idx];

	memset(backfill_candidate, 0, current_idx);

	if (compact_streams) {
		/* Limit matching consideration to the maximum allowed live streams. */
		idx = ast_sdp_options_get_max_streams(sdp_state->options);
		if (idx < current_idx) {
			current_idx = idx;
		}
	}
	if (sdp_vect_idx_init(&current_vect, current_idx)) {
		return NULL;
	}

	if (sdp_vect_idx_init(&update_vect, ast_stream_topology_get_count(update_topology))) {
		AST_VECTOR_FREE(&current_vect);
		return NULL;
	}

	merged_topology = ast_stream_topology_clone(current_topology);
	if (!merged_topology) {
		goto fail;
	}

	/*
	 * Remove any unsupported current streams from match consideration
	 * and mark potential backfill candidates.
	 */
	for (current_idx = AST_VECTOR_SIZE(&current_vect); current_idx--;) {
		idx = AST_VECTOR_GET(&current_vect, current_idx);
		current_stream = ast_stream_topology_get_stream(current_topology, idx);
		if (ast_stream_get_state(current_stream) == AST_STREAM_STATE_REMOVED
			&& compact_streams) {
			/* The declined stream is a potential backfill candidate */
			backfill_candidate[idx] = 1;
		}
		if (sdp_is_stream_type_supported(ast_stream_get_type(current_stream))) {
			continue;
		}
		/* Unsupported current streams should always be declined */
		ast_assert(ast_stream_get_state(current_stream) == AST_STREAM_STATE_REMOVED);

		AST_VECTOR_REMOVE_ORDERED(&current_vect, current_idx);
	}

	/* Remove any unsupported update streams from match consideration. */
	for (update_idx = AST_VECTOR_SIZE(&update_vect); update_idx--;) {
		idx = AST_VECTOR_GET(&update_vect, update_idx);
		update_stream = ast_stream_topology_get_stream(update_topology, idx);
		if (sdp_is_stream_type_supported(ast_stream_get_type(update_stream))) {
			continue;
		}

		AST_VECTOR_REMOVE_ORDERED(&update_vect, update_idx);
	}

	/* Match by stream name and type */
	if (sdp_merge_streams_match(sdp_state, current_topology, update_topology,
		&current_vect, &update_vect, backfill_candidate, sdp_stream_cmp_by_name,
		merged_topology, compact_streams)) {
		goto fail;
	}

	/* Match by stream type */
	if (sdp_merge_streams_match(sdp_state, current_topology, update_topology,
		&current_vect, &update_vect, backfill_candidate, sdp_stream_cmp_by_type,
		merged_topology, compact_streams)) {
		goto fail;
	}

	/* Decline unmatched current stream slots */
	for (current_idx = AST_VECTOR_SIZE(&current_vect); current_idx--;) {
		idx = AST_VECTOR_GET(&current_vect, current_idx);
		current_stream = ast_stream_topology_get_stream(current_topology, idx);

		if (ast_stream_get_state(current_stream) == AST_STREAM_STATE_REMOVED) {
			/* Stream is already declined. */
			continue;
		}

		merged_stream = decline_stream(ast_stream_get_type(current_stream),
			ast_stream_get_name(current_stream));
		if (!merged_stream) {
			goto fail;
		}
		if (ast_stream_topology_set_stream(merged_topology, idx, merged_stream)) {
			ast_stream_free(merged_stream);
			goto fail;
		}
	}

	/* Backfill new update stream slots into pre-existing declined current stream slots */
	while (AST_VECTOR_SIZE(&update_vect)) {
		idx = ast_stream_topology_get_count(current_topology);
		for (current_idx = 0; current_idx < idx; ++current_idx) {
			if (backfill_candidate[current_idx]) {
				break;
			}
		}
		if (idx <= current_idx) {
			/* No more backfill candidates remain. */
			break;
		}
		/* There should only be backfill stream slots when we are compact_streams */
		ast_assert(compact_streams);

		idx = AST_VECTOR_GET(&update_vect, 0);
		update_stream = ast_stream_topology_get_stream(update_topology, idx);
		AST_VECTOR_REMOVE_ORDERED(&update_vect, 0);

		if (ast_stream_get_state(update_stream) == AST_STREAM_STATE_REMOVED) {
			/* New stream is already declined so don't bother adding it. */
			continue;
		}

		merged_stream = merge_local_stream(sdp_state->options, update_stream);
		if (!merged_stream) {
			goto fail;
		}
		if (ast_stream_get_state(merged_stream) == AST_STREAM_STATE_REMOVED) {
			/* New stream not compatible so don't bother adding it. */
			ast_stream_free(merged_stream);
			continue;
		}

		/* Add the new stream into the backfill stream slot. */
		if (ast_stream_topology_set_stream(merged_topology, current_idx, merged_stream)) {
			ast_stream_free(merged_stream);
			goto fail;
		}
		backfill_candidate[current_idx] = 0;
	}

	/* Append any remaining new update stream slots that can fit. */
	while (AST_VECTOR_SIZE(&update_vect)
		&& (!compact_streams
			|| ast_stream_topology_get_count(merged_topology)
				< ast_sdp_options_get_max_streams(sdp_state->options))) {
		idx = AST_VECTOR_GET(&update_vect, 0);
		update_stream = ast_stream_topology_get_stream(update_topology, idx);
		AST_VECTOR_REMOVE_ORDERED(&update_vect, 0);

		if (ast_stream_get_state(update_stream) == AST_STREAM_STATE_REMOVED) {
			/* New stream is already declined so don't bother adding it. */
			continue;
		}

		merged_stream = merge_local_stream(sdp_state->options, update_stream);
		if (!merged_stream) {
			goto fail;
		}
		if (ast_stream_get_state(merged_stream) == AST_STREAM_STATE_REMOVED) {
			/* New stream not compatible so don't bother adding it. */
			ast_stream_free(merged_stream);
			continue;
		}

		/* Append the new update stream. */
		if (ast_stream_topology_append_stream(merged_topology, merged_stream) < 0) {
			ast_stream_free(merged_stream);
			goto fail;
		}
	}

	AST_VECTOR_FREE(&current_vect);
	AST_VECTOR_FREE(&update_vect);
	return merged_topology;

fail:
	ast_stream_topology_free(merged_topology);
	AST_VECTOR_FREE(&current_vect);
	AST_VECTOR_FREE(&update_vect);
	return NULL;
}

/*!
 * \internal
 * \brief Remove declined streams appended beyond orig_topology.
 * \since 15.0.0
 *
 * \param sdp_state
 * \param orig_topology Negotiated or initial topology.
 * \param new_topology New proposed topology.
 *
 * \return Nothing
 */
static void remove_appended_declined_streams(const struct ast_sdp_state *sdp_state,
	const struct ast_stream_topology *orig_topology,
	struct ast_stream_topology *new_topology)
{
	struct ast_stream *stream;
	int orig_count;
	int idx;

	orig_count = ast_stream_topology_get_count(orig_topology);
	for (idx = ast_stream_topology_get_count(new_topology); orig_count < idx;) {
		--idx;
		stream = ast_stream_topology_get_stream(new_topology, idx);
		if (ast_stream_get_state(stream) != AST_STREAM_STATE_REMOVED) {
			continue;
		}
		ast_stream_topology_del_stream(new_topology, idx);
	}
}

/*!
 * \internal
 * \brief Setup a new state stream from a possibly existing state stream.
 * \since 15.0.0
 *
 * \param sdp_state
 * \param new_state_stream What state stream to setup
 * \param old_state_stream Source of previous state stream information.
 *            May be NULL.
 * \param new_type Type of the new state stream.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int setup_new_stream_capabilities(
	const struct ast_sdp_state *sdp_state,
	struct sdp_state_stream *new_state_stream,
	struct sdp_state_stream *old_state_stream,
	enum ast_media_type new_type)
{
	if (old_state_stream) {
		/*
		 * Copy everything potentially useful for a new stream state type
		 * from the old stream of a possible different type.
		 */
		sdp_state_stream_copy_common(new_state_stream, old_state_stream);
		/* We also need to preserve the locally_held state for the new stream. */
		new_state_stream->locally_held = old_state_stream->locally_held;
	}
	new_state_stream->type = new_type;

	switch (new_type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
		new_state_stream->rtp = create_rtp(sdp_state->options, new_type);
		if (!new_state_stream->rtp) {
			return -1;
		}
		break;
	case AST_MEDIA_TYPE_IMAGE:
		new_state_stream->udptl = create_udptl(sdp_state->options);
		if (!new_state_stream->udptl) {
			return -1;
		}
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_TEXT:
	case AST_MEDIA_TYPE_END:
		break;
	}
	return 0;
}

/*!
 * \brief Merge existing stream capabilities and a new topology.
 *
 * \param sdp_state The state needing capabilities merged
 * \param new_topology The topology to merge with our proposed capabilities
 *
 * \details
 *
 * This is a bit complicated. The idea is that we already have some
 * capabilities set, and we've now been confronted with a new stream
 * topology from the system.  We want to take what we had before and
 * merge them with the new topology from the system.
 *
 * According to the RFC, stream slots can change their types only if
 * they are carrying the same logical information or an offer is
 * reusing a declined slot or new stream slots are added to the end
 * of the list.  Switching a stream from audio to T.38 makes sense
 * because the stream slot is carrying the same information just in a
 * different format.
 *
 * We can setup new streams offered by the system up to our
 * configured maximum stream slots.  New stream slots requested over
 * the maximum are discarded.
 *
 * \retval NULL An error occurred
 * \retval non-NULL The merged capabilities
 */
static struct sdp_state_capabilities *merge_local_capabilities(
	const struct ast_sdp_state *sdp_state,
	const struct ast_stream_topology *new_topology)
{
	const struct sdp_state_capabilities *current = sdp_state->proposed_capabilities;
	struct sdp_state_capabilities *merged_capabilities;
	int idx;

	ast_assert(current != NULL);

	merged_capabilities = ast_calloc(1, sizeof(*merged_capabilities));
	if (!merged_capabilities) {
		return NULL;
	}

	merged_capabilities->topology = merge_local_topologies(sdp_state, current->topology,
		new_topology, 1);
	if (!merged_capabilities->topology) {
		goto fail;
	}
	sdp_state_cb_offerer_modify_topology(sdp_state, merged_capabilities->topology);
	remove_appended_declined_streams(sdp_state, current->topology,
		merged_capabilities->topology);

	if (AST_VECTOR_INIT(&merged_capabilities->streams,
		ast_stream_topology_get_count(merged_capabilities->topology))) {
		goto fail;
	}

	for (idx = 0; idx < ast_stream_topology_get_count(merged_capabilities->topology); ++idx) {
		struct sdp_state_stream *merged_state_stream;
		struct sdp_state_stream *current_state_stream;
		struct ast_stream *merged_stream;
		struct ast_stream *current_stream;
		enum ast_media_type merged_stream_type;
		enum ast_media_type current_stream_type;

		merged_state_stream = ast_calloc(1, sizeof(*merged_state_stream));
		if (!merged_state_stream) {
			goto fail;
		}

		merged_stream = ast_stream_topology_get_stream(merged_capabilities->topology, idx);
		merged_stream_type = ast_stream_get_type(merged_stream);

		if (idx < ast_stream_topology_get_count(current->topology)) {
			current_state_stream = AST_VECTOR_GET(&current->streams, idx);
			current_stream = ast_stream_topology_get_stream(current->topology, idx);
			current_stream_type = ast_stream_get_type(current_stream);
		} else {
			/* The merged topology is adding a stream */
			current_state_stream = NULL;
			current_stream = NULL;
			current_stream_type = AST_MEDIA_TYPE_UNKNOWN;
		}

		if (ast_stream_get_state(merged_stream) == AST_STREAM_STATE_REMOVED) {
			if (current_state_stream) {
				/* Copy everything potentially useful to a declined stream state. */
				sdp_state_stream_copy_common(merged_state_stream, current_state_stream);
			}
			merged_state_stream->type = merged_stream_type;
		} else if (!current_stream
			|| ast_stream_get_state(current_stream) == AST_STREAM_STATE_REMOVED) {
			/* This is a new stream */
			if (setup_new_stream_capabilities(sdp_state, merged_state_stream,
				current_state_stream, merged_stream_type)) {
				sdp_state_stream_free(merged_state_stream);
				goto fail;
			}
		} else if (merged_stream_type == current_stream_type) {
			/* Stream type is not changing. */
			sdp_state_stream_copy(merged_state_stream, current_state_stream);
		} else {
			/*
			 * Stream type is changing.  Need to replace the stream.
			 *
			 * Unsupported streams should already be handled earlier because
			 * they are always declined.
			 */
			ast_assert(sdp_is_stream_type_supported(merged_stream_type));

			/*
			 * XXX We might need to keep the old RTP instance if the new
			 * stream type is also RTP.  We would just be changing between
			 * audio and video in that case.  However we will create a new
			 * RTP instance anyway since its purpose has to be changing.
			 * Any RTP packets in flight from the old stream type might
			 * cause mischief.
			 */
			if (setup_new_stream_capabilities(sdp_state, merged_state_stream,
				current_state_stream, merged_stream_type)) {
				sdp_state_stream_free(merged_state_stream);
				goto fail;
			}
		}

		if (AST_VECTOR_APPEND(&merged_capabilities->streams, merged_state_stream)) {
			sdp_state_stream_free(merged_state_stream);
			goto fail;
		}
	}

	return merged_capabilities;

fail:
	sdp_state_capabilities_free(merged_capabilities);
	return NULL;
}

static void merge_remote_stream_capabilities(
	const struct ast_sdp_state *sdp_state,
	struct sdp_state_stream *joint_state_stream,
	struct sdp_state_stream *local_state_stream,
	struct ast_stream *remote_stream)
{
	struct ast_rtp_codecs *codecs;

	*joint_state_stream = *local_state_stream;
	/*
	 * Need to explicitly set the type to the remote because we could
	 * be changing the type between audio and video.
	 */
	joint_state_stream->type = ast_stream_get_type(remote_stream);

	switch (joint_state_stream->type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
		ao2_bump(joint_state_stream->rtp);
		codecs = ast_stream_get_data(remote_stream, AST_STREAM_DATA_RTP_CODECS);
		ast_assert(codecs != NULL);
		if (sdp_state->role == SDP_ROLE_ANSWERER) {
			/*
			 * Setup rx payload type mapping to prefer the mapping
			 * from the peer that the RFC says we SHOULD use.
			 */
			ast_rtp_codecs_payloads_xover(codecs, codecs, NULL);
		}
		ast_rtp_codecs_payloads_copy(codecs,
			ast_rtp_instance_get_codecs(joint_state_stream->rtp->instance),
			joint_state_stream->rtp->instance);
		break;
	case AST_MEDIA_TYPE_IMAGE:
		joint_state_stream->udptl = ao2_bump(joint_state_stream->udptl);
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_TEXT:
	case AST_MEDIA_TYPE_END:
		break;
	}
}

static int create_remote_stream_capabilities(
	const struct ast_sdp_state *sdp_state,
	struct sdp_state_stream *joint_state_stream,
	struct sdp_state_stream *local_state_stream,
	struct ast_stream *remote_stream)
{
	struct ast_rtp_codecs *codecs;

	/* We can only create streams if we are the answerer */
	ast_assert(sdp_state->role == SDP_ROLE_ANSWERER);

	if (local_state_stream) {
		/*
		 * Copy everything potentially useful for a new stream state type
		 * from the old stream of a possible different type.
		 */
		sdp_state_stream_copy_common(joint_state_stream, local_state_stream);
		/* We also need to preserve the locally_held state for the new stream. */
		joint_state_stream->locally_held = local_state_stream->locally_held;
	}
	joint_state_stream->type = ast_stream_get_type(remote_stream);

	switch (joint_state_stream->type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
		joint_state_stream->rtp = create_rtp(sdp_state->options, joint_state_stream->type);
		if (!joint_state_stream->rtp) {
			return -1;
		}

		/*
		 * Setup rx payload type mapping to prefer the mapping
		 * from the peer that the RFC says we SHOULD use.
		 */
		codecs = ast_stream_get_data(remote_stream, AST_STREAM_DATA_RTP_CODECS);
		ast_assert(codecs != NULL);
		ast_rtp_codecs_payloads_xover(codecs, codecs, NULL);
		ast_rtp_codecs_payloads_copy(codecs,
			ast_rtp_instance_get_codecs(joint_state_stream->rtp->instance),
			joint_state_stream->rtp->instance);
		break;
	case AST_MEDIA_TYPE_IMAGE:
		joint_state_stream->udptl = create_udptl(sdp_state->options);
		if (!joint_state_stream->udptl) {
			return -1;
		}
		break;
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_TEXT:
	case AST_MEDIA_TYPE_END:
		break;
	}
	return 0;
}

/*!
 * \internal
 * \brief Create a joint topology from the remote topology.
 * \since 15.0.0
 *
 * \param sdp_state The state needing capabilities merged.
 * \param local Capabilities to merge the remote topology into.
 * \param remote_topology The topology to merge with our local capabilities.
 *
 * \retval joint topology on success.
 * \retval NULL on failure.
 */
static struct ast_stream_topology *merge_remote_topology(
	const struct ast_sdp_state *sdp_state,
	const struct sdp_state_capabilities *local,
	const struct ast_stream_topology *remote_topology)
{
	struct ast_stream_topology *joint_topology;
	int idx;

	joint_topology = ast_stream_topology_alloc();
	if (!joint_topology) {
		return NULL;
	}

	for (idx = 0; idx < ast_stream_topology_get_count(remote_topology); ++idx) {
		enum ast_media_type local_stream_type;
		enum ast_media_type remote_stream_type;
		struct ast_stream *remote_stream;
		struct ast_stream *local_stream;
		struct ast_stream *joint_stream;
		struct sdp_state_stream *local_state_stream;

		remote_stream = ast_stream_topology_get_stream(remote_topology, idx);
		remote_stream_type = ast_stream_get_type(remote_stream);

		if (idx < ast_stream_topology_get_count(local->topology)) {
			local_state_stream = AST_VECTOR_GET(&local->streams, idx);
			local_stream = ast_stream_topology_get_stream(local->topology, idx);
			local_stream_type = ast_stream_get_type(local_stream);
		} else {
			/* The remote is adding a stream slot */
			local_state_stream = NULL;
			local_stream = NULL;
			local_stream_type = AST_MEDIA_TYPE_UNKNOWN;

			if (sdp_state->role != SDP_ROLE_ANSWERER) {
				/* Remote cannot add a new stream slot in an answer SDP */
				ast_debug(1,
					"Bad.  Ignoring new %s stream slot remote answer SDP trying to add.\n",
					ast_codec_media_type2str(remote_stream_type));
				continue;
			}
		}

		if (local_stream
			&& ast_stream_get_state(local_stream) != AST_STREAM_STATE_REMOVED) {
			if (remote_stream_type == local_stream_type) {
				/* Stream type is not changing. */
				joint_stream = merge_remote_stream(sdp_state, local_stream,
					local_state_stream->locally_held, remote_stream);
			} else if (sdp_state->role == SDP_ROLE_ANSWERER) {
				/* Stream type is changing. */
				joint_stream = merge_remote_stream(sdp_state, NULL,
					local_state_stream->locally_held, remote_stream);
			} else {
				/*
				 * Remote cannot change the stream type we offered.
				 * Mark as declined.
				 */
				ast_debug(1,
					"Bad.  Remote answer SDP trying to change the stream type from %s to %s.\n",
					ast_codec_media_type2str(local_stream_type),
					ast_codec_media_type2str(remote_stream_type));
				joint_stream = decline_stream(local_stream_type,
					ast_stream_get_name(local_stream));
			}
		} else {
			/* Local stream is either dead/declined or nonexistent. */
			if (sdp_state->role == SDP_ROLE_ANSWERER) {
				if (sdp_is_stream_type_supported(remote_stream_type)
					&& ast_stream_get_state(remote_stream) != AST_STREAM_STATE_REMOVED
					&& idx < ast_sdp_options_get_max_streams(sdp_state->options)) {
					/* Try to create the new stream */
					joint_stream = merge_remote_stream(sdp_state, NULL,
						local_state_stream ? local_state_stream->locally_held : 0,
						remote_stream);
				} else {
					const char *stream_name;

					/* Decline the remote stream. */
					if (local_stream
						&& local_stream_type == remote_stream_type) {
						/* Preserve the previous stream name */
						stream_name = ast_stream_get_name(local_stream);
					} else {
						stream_name = NULL;
					}
					joint_stream = decline_stream(remote_stream_type, stream_name);
				}
			} else {
				/* Decline the stream. */
				if (DEBUG_ATLEAST(1)
					&& ast_stream_get_state(remote_stream) != AST_STREAM_STATE_REMOVED) {
					/*
					 * Remote cannot request a new stream in place of a declined
					 * stream in an answer SDP.
					 */
					ast_log(LOG_DEBUG,
						"Bad.  Remote answer SDP trying to use a declined stream slot for %s.\n",
						ast_codec_media_type2str(remote_stream_type));
				}
				joint_stream = decline_stream(local_stream_type,
					ast_stream_get_name(local_stream));
			}
		}

		if (!joint_stream) {
			goto fail;
		}
		if (ast_stream_topology_append_stream(joint_topology, joint_stream) < 0) {
			ast_stream_free(joint_stream);
			goto fail;
		}
	}

	return joint_topology;

fail:
	ast_stream_topology_free(joint_topology);
	return NULL;
}

/*!
 * \brief Merge our stream capabilities and a remote topology into joint capabilities.
 *
 * \param sdp_state The state needing capabilities merged
 * \param remote_topology The topology to merge with our proposed capabilities
 *
 * \details
 * This is a bit complicated. The idea is that we already have some
 * capabilities set, and we've now been confronted with a stream
 * topology from the remote end.  We want to take what's been
 * presented to us and merge those new capabilities with our own.
 *
 * According to the RFC, stream slots can change their types only if
 * they are carrying the same logical information or an offer is
 * reusing a declined slot or new stream slots are added to the end
 * of the list.  Switching a stream from audio to T.38 makes sense
 * because the stream slot is carrying the same information just in a
 * different format.
 *
 * When we are the answerer we can setup new streams offered by the
 * remote up to our configured maximum stream slots.  New stream
 * slots offered over the maximum are unconditionally declined.
 *
 * \retval NULL An error occurred
 * \retval non-NULL The merged capabilities
 */
static struct sdp_state_capabilities *merge_remote_capabilities(
	const struct ast_sdp_state *sdp_state,
	const struct ast_stream_topology *remote_topology)
{
	const struct sdp_state_capabilities *local = sdp_state->proposed_capabilities;
	struct sdp_state_capabilities *joint_capabilities;
	int idx;

	ast_assert(local != NULL);

	joint_capabilities = ast_calloc(1, sizeof(*joint_capabilities));
	if (!joint_capabilities) {
		return NULL;
	}

	joint_capabilities->topology = merge_remote_topology(sdp_state, local, remote_topology);
	if (!joint_capabilities->topology) {
		goto fail;
	}

	if (sdp_state->role == SDP_ROLE_ANSWERER) {
		sdp_state_cb_answerer_modify_topology(sdp_state, joint_capabilities->topology);
	}
	idx = ast_stream_topology_get_count(joint_capabilities->topology);
	if (AST_VECTOR_INIT(&joint_capabilities->streams, idx)) {
		goto fail;
	}

	for (idx = 0; idx < ast_stream_topology_get_count(remote_topology); ++idx) {
		enum ast_media_type local_stream_type;
		enum ast_media_type remote_stream_type;
		struct ast_stream *remote_stream;
		struct ast_stream *local_stream;
		struct ast_stream *joint_stream;
		struct sdp_state_stream *local_state_stream;
		struct sdp_state_stream *joint_state_stream;

		joint_state_stream = ast_calloc(1, sizeof(*joint_state_stream));
		if (!joint_state_stream) {
			goto fail;
		}

		remote_stream = ast_stream_topology_get_stream(remote_topology, idx);
		remote_stream_type = ast_stream_get_type(remote_stream);

		if (idx < ast_stream_topology_get_count(local->topology)) {
			local_state_stream = AST_VECTOR_GET(&local->streams, idx);
			local_stream = ast_stream_topology_get_stream(local->topology, idx);
			local_stream_type = ast_stream_get_type(local_stream);
		} else {
			/* The remote is adding a stream slot */
			local_state_stream = NULL;
			local_stream = NULL;
			local_stream_type = AST_MEDIA_TYPE_UNKNOWN;

			if (sdp_state->role != SDP_ROLE_ANSWERER) {
				/* Remote cannot add a new stream slot in an answer SDP */
				sdp_state_stream_free(joint_state_stream);
				break;
			}
		}

		joint_stream = ast_stream_topology_get_stream(joint_capabilities->topology,
			idx);

		if (local_stream
			&& ast_stream_get_state(local_stream) != AST_STREAM_STATE_REMOVED) {
			if (ast_stream_get_state(joint_stream) == AST_STREAM_STATE_REMOVED) {
				/* Copy everything potentially useful to a declined stream state. */
				sdp_state_stream_copy_common(joint_state_stream, local_state_stream);

				joint_state_stream->type = ast_stream_get_type(joint_stream);
			} else if (remote_stream_type == local_stream_type) {
				/* Stream type is not changing. */
				merge_remote_stream_capabilities(sdp_state, joint_state_stream,
					local_state_stream, remote_stream);
				ast_assert(joint_state_stream->type == ast_stream_get_type(joint_stream));
			} else {
				/*
				 * Stream type is changing.  Need to replace the stream.
				 *
				 * XXX We might need to keep the old RTP instance if the new
				 * stream type is also RTP.  We would just be changing between
				 * audio and video in that case.  However we will create a new
				 * RTP instance anyway since its purpose has to be changing.
				 * Any RTP packets in flight from the old stream type might
				 * cause mischief.
				 */
				if (create_remote_stream_capabilities(sdp_state, joint_state_stream,
					local_state_stream, remote_stream)) {
					sdp_state_stream_free(joint_state_stream);
					goto fail;
				}
				ast_assert(joint_state_stream->type == ast_stream_get_type(joint_stream));
			}
		} else {
			/* Local stream is either dead/declined or nonexistent. */
			if (sdp_state->role == SDP_ROLE_ANSWERER) {
				if (ast_stream_get_state(joint_stream) == AST_STREAM_STATE_REMOVED) {
					if (local_state_stream) {
						/* Copy everything potentially useful to a declined stream state. */
						sdp_state_stream_copy_common(joint_state_stream, local_state_stream);
					}
					joint_state_stream->type = ast_stream_get_type(joint_stream);
				} else {
					/* Try to create the new stream */
					if (create_remote_stream_capabilities(sdp_state, joint_state_stream,
						local_state_stream, remote_stream)) {
						sdp_state_stream_free(joint_state_stream);
						goto fail;
					}
					ast_assert(joint_state_stream->type == ast_stream_get_type(joint_stream));
				}
			} else {
				/* Decline the stream. */
				ast_assert(ast_stream_get_state(joint_stream) == AST_STREAM_STATE_REMOVED);
				if (local_state_stream) {
					/* Copy everything potentially useful to a declined stream state. */
					sdp_state_stream_copy_common(joint_state_stream, local_state_stream);
				}
				joint_state_stream->type = ast_stream_get_type(joint_stream);
			}
		}

		/* Determine if the remote placed the stream on hold. */
		joint_state_stream->remotely_held = 0;
		if (ast_stream_get_state(joint_stream) != AST_STREAM_STATE_REMOVED) {
			enum ast_stream_state remote_state;

			remote_state = ast_stream_get_state(remote_stream);
			switch (remote_state) {
			case AST_STREAM_STATE_INACTIVE:
			case AST_STREAM_STATE_SENDONLY:
				joint_state_stream->remotely_held = 1;
				break;
			default:
				break;
			}
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
	const struct ast_sdp_a_line *attr_rtcp_mux;
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

	if (ast_sdp_find_attribute(remote_sdp, "ice-lite", -1)) {
		ice->ice_lite(rtp);
	}

	attr_rtcp_mux = ast_sdp_m_find_attribute(remote_m_line, "rtcp-mux", -1);

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

		if (candidate.id > 1
			&& attr_rtcp_mux
			&& ast_sdp_options_get_rtcp_mux(options)) {
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
 * \param rtp The RTP wrapper that is being updated
 * \param options Our locally-supported SDP options
 * \param remote_sdp The SDP we most recently received
 * \param remote_m_line The remote SDP stream that corresponds to the RTP instance we are modifying
 */
static void update_rtp_after_merge(const struct ast_sdp_state *state,
	struct sdp_state_rtp *rtp,
    const struct ast_sdp_options *options,
	const struct ast_sdp *remote_sdp,
	const struct ast_sdp_m_line *remote_m_line)
{
	struct ast_sdp_c_line *c_line;
	struct ast_sockaddr *addrs;

	c_line = remote_m_line->c_line;
	if (!c_line) {
		c_line = remote_sdp->c_line;
	}
	/*
	 * There must be a c= line somewhere but that would be an error by
	 * the far end that should have been caught by a validation check
	 * before we processed the SDP.
	 */
	ast_assert(c_line != NULL);

	if (ast_sockaddr_resolve(&addrs, c_line->address, PARSE_PORT_FORBID, AST_AF_UNSPEC) > 0) {
		/* Apply connection information to the RTP instance */
		ast_sockaddr_set_port(addrs, remote_m_line->port);
		ast_rtp_instance_set_remote_address(rtp->instance, addrs);
		ast_free(addrs);
	}

	if (ast_sdp_options_get_rtcp_mux(options)
		&& ast_sdp_m_find_attribute(remote_m_line, "rtcp-mux", -1)) {
		ast_rtp_instance_set_prop(rtp->instance, AST_RTP_PROPERTY_RTCP,
			AST_RTP_INSTANCE_RTCP_MUX);
	} else {
		ast_rtp_instance_set_prop(rtp->instance, AST_RTP_PROPERTY_RTCP,
			AST_RTP_INSTANCE_RTCP_STANDARD);
	}

	update_ice(state, rtp->instance, options, remote_sdp, remote_m_line);
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

	c_line = remote_m_line->c_line;
	if (!c_line) {
		c_line = remote_sdp->c_line;
	}
	/*
	 * There must be a c= line somewhere but that would be an error by
	 * the far end that should have been caught by a validation check
	 * before we processed the SDP.
	 */
	ast_assert(c_line != NULL);

	if (ast_sockaddr_resolve(&addrs, c_line->address, PARSE_PORT_FORBID, AST_AF_UNSPEC) > 0) {
		/* Apply connection information to the UDPTL instance */
		ast_sockaddr_set_port(addrs, remote_m_line->port);
		ast_udptl_set_peer(udptl->instance, addrs);
		ast_free(addrs);
	}
}

static void sdp_apply_negotiated_state(struct ast_sdp_state *sdp_state)
{
	struct sdp_state_capabilities *capabilities = sdp_state->negotiated_capabilities;
	int idx;

	if (!capabilities) {
		/* Nothing to apply */
		return;
	}

	sdp_state_cb_preapply_topology(sdp_state, capabilities->topology);
	for (idx = 0; idx < AST_VECTOR_SIZE(&capabilities->streams); ++idx) {
		struct sdp_state_stream *state_stream;
		struct ast_stream *stream;

		stream = ast_stream_topology_get_stream(capabilities->topology, idx);
		if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
			/* Stream is declined */
			continue;
		}

		state_stream = AST_VECTOR_GET(&capabilities->streams, idx);
		switch (ast_stream_get_type(stream)) {
		case AST_MEDIA_TYPE_AUDIO:
		case AST_MEDIA_TYPE_VIDEO:
			update_rtp_after_merge(sdp_state, state_stream->rtp, sdp_state->options,
				sdp_state->remote_sdp, ast_sdp_get_m(sdp_state->remote_sdp, idx));
			break;
		case AST_MEDIA_TYPE_IMAGE:
			update_udptl_after_merge(sdp_state, state_stream->udptl, sdp_state->options,
				sdp_state->remote_sdp, ast_sdp_get_m(sdp_state->remote_sdp, idx));
			break;
		case AST_MEDIA_TYPE_UNKNOWN:
		case AST_MEDIA_TYPE_TEXT:
		case AST_MEDIA_TYPE_END:
			/* All unsupported streams are declined */
			ast_assert(0);
			break;
		}
	}
	sdp_state_cb_postapply_topology(sdp_state, capabilities->topology);
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

/*!
 * \internal
 * \brief Copy the new capabilities into the proposed capabilities.
 * \since 15.0.0
 *
 * \param sdp_state The current SDP state
 * \param new_capabilities Capabilities to copy
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int update_proposed_capabilities(struct ast_sdp_state *sdp_state,
	struct sdp_state_capabilities *new_capabilities)
{
	struct sdp_state_capabilities *proposed_capabilities;
	int idx;

	proposed_capabilities = ast_calloc(1, sizeof(*proposed_capabilities));
	if (!proposed_capabilities) {
		return -1;
	}

	proposed_capabilities->topology = ast_stream_topology_clone(new_capabilities->topology);
	if (!proposed_capabilities->topology) {
		goto fail;
	}

	if (AST_VECTOR_INIT(&proposed_capabilities->streams,
		AST_VECTOR_SIZE(&new_capabilities->streams))) {
		goto fail;
	}

	for (idx = 0; idx < AST_VECTOR_SIZE(&new_capabilities->streams); ++idx) {
		struct sdp_state_stream *proposed_state_stream;
		struct sdp_state_stream *new_state_stream;

		proposed_state_stream = ast_calloc(1, sizeof(*proposed_state_stream));
		if (!proposed_state_stream) {
			goto fail;
		}

		new_state_stream = AST_VECTOR_GET(&new_capabilities->streams, idx);
		*proposed_state_stream = *new_state_stream;

		switch (proposed_state_stream->type) {
		case AST_MEDIA_TYPE_AUDIO:
		case AST_MEDIA_TYPE_VIDEO:
			ao2_bump(proposed_state_stream->rtp);
			break;
		case AST_MEDIA_TYPE_IMAGE:
			ao2_bump(proposed_state_stream->udptl);
			break;
		case AST_MEDIA_TYPE_UNKNOWN:
		case AST_MEDIA_TYPE_TEXT:
		case AST_MEDIA_TYPE_END:
			break;
		}

		/* This is explicitly never set on the proposed capabilities struct */
		proposed_state_stream->remotely_held = 0;

		if (AST_VECTOR_APPEND(&proposed_capabilities->streams, proposed_state_stream)) {
			sdp_state_stream_free(proposed_state_stream);
			goto fail;
		}
	}

	set_proposed_capabilities(sdp_state, proposed_capabilities);
	return 0;

fail:
	sdp_state_capabilities_free(proposed_capabilities);
	return -1;
}

static struct ast_sdp *sdp_create_from_state(const struct ast_sdp_state *sdp_state,
	const struct sdp_state_capabilities *capabilities);

/*!
 * \brief Merge SDPs into a joint SDP.
 *
 * This function is used to take a remote SDP and merge it with our local
 * capabilities to produce a new local SDP.  After creating the new local SDP,
 * it then iterates through media instances and updates them as necessary.  For
 * instance, if a specific RTP feature is supported by both us and the far end,
 * then we can ensure that the feature is enabled.
 *
 * \param sdp_state The current SDP state
 *
 * \retval 0 Success
 * \retval -1 Failure
 *         Use ast_sdp_state_is_offer_rejected() to see if the offer SDP was rejected.
 */
static int merge_sdps(struct ast_sdp_state *sdp_state, const struct ast_sdp *remote_sdp)
{
	struct sdp_state_capabilities *joint_capabilities;
	struct ast_stream_topology *remote_capabilities;

	remote_capabilities = ast_get_topology_from_sdp(remote_sdp,
		ast_sdp_options_get_g726_non_standard(sdp_state->options));
	if (!remote_capabilities) {
		return -1;
	}

	joint_capabilities = merge_remote_capabilities(sdp_state, remote_capabilities);
	ast_stream_topology_free(remote_capabilities);
	if (!joint_capabilities) {
		return -1;
	}
	if (sdp_state->role == SDP_ROLE_ANSWERER) {
		sdp_state->remote_offer_rejected =
			sdp_topology_is_rejected(joint_capabilities->topology) ? 1 : 0;
		if (sdp_state->remote_offer_rejected) {
			sdp_state_capabilities_free(joint_capabilities);
			return -1;
		}
	}
	set_negotiated_capabilities(sdp_state, joint_capabilities);

	ao2_cleanup(sdp_state->remote_sdp);
	sdp_state->remote_sdp = ao2_bump((struct ast_sdp *) remote_sdp);

	sdp_apply_negotiated_state(sdp_state);

	return 0;
}

const struct ast_sdp *ast_sdp_state_get_local_sdp(struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	switch (sdp_state->role) {
	case SDP_ROLE_NOT_SET:
		ast_assert(sdp_state->local_sdp == NULL);
		sdp_state->role = SDP_ROLE_OFFERER;

		if (sdp_state->pending_topology_update) {
			struct sdp_state_capabilities *capabilities;

			/* We have a topology update to perform before generating the offer */
			capabilities = merge_local_capabilities(sdp_state,
				sdp_state->pending_topology_update);
			if (!capabilities) {
				break;
			}
			ast_stream_topology_free(sdp_state->pending_topology_update);
			sdp_state->pending_topology_update = NULL;
			set_proposed_capabilities(sdp_state, capabilities);
		}

		/*
		 * Allow the system to configure the topology streams
		 * before we create the offer SDP.
		 */
		sdp_state_cb_offerer_config_topology(sdp_state,
			sdp_state->proposed_capabilities->topology);

		sdp_state->local_sdp = sdp_create_from_state(sdp_state, sdp_state->proposed_capabilities);
		break;
	case SDP_ROLE_OFFERER:
		break;
	case SDP_ROLE_ANSWERER:
		if (!sdp_state->local_sdp
			&& sdp_state->negotiated_capabilities
			&& !sdp_state->remote_offer_rejected) {
			sdp_state->local_sdp = sdp_create_from_state(sdp_state, sdp_state->negotiated_capabilities);
		}
		break;
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

int ast_sdp_state_set_remote_sdp_from_impl(struct ast_sdp_state *sdp_state, const void *remote)
{
	struct ast_sdp *sdp;
	int ret;

	ast_assert(sdp_state != NULL);

	sdp = ast_sdp_translator_to_sdp(sdp_state->translator, remote);
	if (!sdp) {
		return -1;
	}
	ret = ast_sdp_state_set_remote_sdp(sdp_state, sdp);
	ao2_ref(sdp, -1);
	return ret;
}

int ast_sdp_state_is_offer_rejected(struct ast_sdp_state *sdp_state)
{
	return sdp_state->remote_offer_rejected;
}

int ast_sdp_state_is_offerer(struct ast_sdp_state *sdp_state)
{
	return sdp_state->role == SDP_ROLE_OFFERER;
}

int ast_sdp_state_is_answerer(struct ast_sdp_state *sdp_state)
{
	return sdp_state->role == SDP_ROLE_ANSWERER;
}

int ast_sdp_state_restart_negotiations(struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	ao2_cleanup(sdp_state->local_sdp);
	sdp_state->local_sdp = NULL;

	sdp_state->role = SDP_ROLE_NOT_SET;
	sdp_state->remote_offer_rejected = 0;

	if (sdp_state->negotiated_capabilities) {
		update_proposed_capabilities(sdp_state, sdp_state->negotiated_capabilities);
	}

	return 0;
}

int ast_sdp_state_update_local_topology(struct ast_sdp_state *sdp_state, struct ast_stream_topology *topology)
{
	struct ast_stream_topology *merged_topology;

	ast_assert(sdp_state != NULL);
	ast_assert(topology != NULL);

	if (sdp_state->pending_topology_update) {
		merged_topology = merge_local_topologies(sdp_state,
			sdp_state->pending_topology_update, topology, 0);
		if (!merged_topology) {
			return -1;
		}
		ast_stream_topology_free(sdp_state->pending_topology_update);
		sdp_state->pending_topology_update = merged_topology;
	} else {
		sdp_state->pending_topology_update = ast_stream_topology_clone(topology);
		if (!sdp_state->pending_topology_update) {
			return -1;
		}
	}

	return 0;
}

void ast_sdp_state_set_local_address(struct ast_sdp_state *sdp_state, struct ast_sockaddr *address)
{
	ast_assert(sdp_state != NULL);

	if (!address) {
		ast_sockaddr_setnull(&sdp_state->connection_address);
	} else {
		ast_sockaddr_copy(&sdp_state->connection_address, address);
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

void ast_sdp_state_set_global_locally_held(struct ast_sdp_state *sdp_state, unsigned int locally_held)
{
	ast_assert(sdp_state != NULL);

	sdp_state->locally_held = locally_held ? 1 : 0;
}

unsigned int ast_sdp_state_get_global_locally_held(const struct ast_sdp_state *sdp_state)
{
	ast_assert(sdp_state != NULL);

	return sdp_state->locally_held;
}

void ast_sdp_state_set_locally_held(struct ast_sdp_state *sdp_state,
	int stream_index, unsigned int locally_held)
{
	struct sdp_state_stream *stream_state;
	ast_assert(sdp_state != NULL);

	locally_held = locally_held ? 1 : 0;

	stream_state = sdp_state_get_joint_stream(sdp_state, stream_index);
	if (stream_state) {
		stream_state->locally_held = locally_held;
	}

	stream_state = sdp_state_get_stream(sdp_state, stream_index);
	if (stream_state) {
		stream_state->locally_held = locally_held;
	}
}

unsigned int ast_sdp_state_get_locally_held(const struct ast_sdp_state *sdp_state,
	int stream_index)
{
	struct sdp_state_stream *stream_state;
	ast_assert(sdp_state != NULL);

	stream_state = sdp_state_get_joint_stream(sdp_state, stream_index);
	if (!stream_state) {
		return 0;
	}

	return stream_state->locally_held;
}

unsigned int ast_sdp_state_get_remotely_held(const struct ast_sdp_state *sdp_state,
	int stream_index)
{
	struct sdp_state_stream *stream_state;

	ast_assert(sdp_state != NULL);

	stream_state = sdp_state_get_joint_stream(sdp_state, stream_index);
	if (!stream_state) {
		return 0;
	}

	return stream_state->remotely_held;
}

void ast_sdp_state_set_t38_parameters(struct ast_sdp_state *sdp_state,
	int stream_index, struct ast_control_t38_parameters *params)
{
	struct sdp_state_stream *stream_state;
	ast_assert(sdp_state != NULL && params != NULL);

	stream_state = sdp_state_get_stream(sdp_state, stream_index);
	if (stream_state) {
		stream_state->t38_local_params = *params;
	}
}

/*!
 * \brief Add SSRC-level attributes if appropriate.
 *
 * This function does nothing if the SDP options indicate not to add SSRC-level attributes.
 *
 * Currently, the only attribute added is cname, which is retrieved from the RTP instance.
 *
 * \param m_line The m_line on which to add the SSRC attributes
 * \param options Options that indicate what, if any, SSRC attributes to add
 * \param rtp RTP instance from which we get SSRC-level information
 */
static void add_ssrc_attributes(struct ast_sdp_m_line *m_line, const struct ast_sdp_options *options,
	struct ast_rtp_instance *rtp)
{
	struct ast_sdp_a_line *a_line;
	char attr_buffer[128];

	if (!ast_sdp_options_get_ssrc(options)) {
		return;
	}

	snprintf(attr_buffer, sizeof(attr_buffer), "%u cname:%s", ast_rtp_instance_get_ssrc(rtp),
		ast_rtp_instance_get_cname(rtp));

	a_line = ast_sdp_a_alloc("ssrc", attr_buffer);
	if (!a_line) {
		return;
	}
	ast_sdp_m_add_a(m_line, a_line);
}

/*!
 * \internal
 * \brief Create a declined m-line from a remote requested stream.
 * \since 15.0.0
 *
 * \details
 * Using the last received remote SDP create a declined stream
 * m-line for the requested stream.  The stream may be unsupported.
 *
 * \param sdp Our SDP under construction to append the declined stream.
 * \param sdp_state
 * \param stream_index Which remote SDP stream we are declining.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int sdp_add_m_from_declined_remote_stream(struct ast_sdp *sdp,
	const struct ast_sdp_state *sdp_state, int stream_index)
{
	const struct ast_sdp_m_line *m_line_remote;
	struct ast_sdp_m_line *m_line;
	int idx;

	ast_assert(sdp && sdp_state && sdp_state->remote_sdp);
	ast_assert(stream_index < ast_sdp_get_m_count(sdp_state->remote_sdp));

	/*
	 * The only way we can generate a declined unsupported stream
	 * m-line is if the remote offered it to us.
	 */
	m_line_remote = ast_sdp_get_m(sdp_state->remote_sdp, stream_index);

	/* Copy remote SDP stream m-line except for port number. */
	m_line = ast_sdp_m_alloc(m_line_remote->type, 0, m_line_remote->port_count,
		m_line_remote->proto, NULL);
	if (!m_line) {
		return -1;
	}

	/* Copy any m-line payload strings from the remote SDP */
	for (idx = 0; idx < ast_sdp_m_get_payload_count(m_line_remote); ++idx) {
		const struct ast_sdp_payload *payload_remote;
		struct ast_sdp_payload *payload;

		payload_remote = ast_sdp_m_get_payload(m_line_remote, idx);
		payload = ast_sdp_payload_alloc(payload_remote->fmt);
		if (!payload) {
			ast_sdp_m_free(m_line);
			return -1;
		}
		if (ast_sdp_m_add_payload(m_line, payload)) {
			ast_sdp_payload_free(payload);
			ast_sdp_m_free(m_line);
			return -1;
		}
	}

	if (ast_sdp_add_m(sdp, m_line)) {
		ast_sdp_m_free(m_line);
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Create a declined m-line for our SDP stream.
 * \since 15.0.0
 *
 * \param sdp Our SDP under construction to append the declined stream.
 * \param sdp_state
 * \param type Stream type we are declining.
 * \param stream_index Which remote SDP stream we are declining.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int sdp_add_m_from_declined_stream(struct ast_sdp *sdp,
	const struct ast_sdp_state *sdp_state, enum ast_media_type type, int stream_index)
{
	struct ast_sdp_m_line *m_line;
	const char *proto;
	const char *fmt;
	struct ast_sdp_payload *payload;

	if (sdp_state->role == SDP_ROLE_ANSWERER) {
		/* We are declining the remote stream or it is still declined. */
		return sdp_add_m_from_declined_remote_stream(sdp, sdp_state, stream_index);
	}

	/* Send declined remote stream in our offer if the type matches. */
	if (sdp_state->remote_sdp
		&& stream_index < ast_sdp_get_m_count(sdp_state->remote_sdp)) {
		if (!sdp_is_stream_type_supported(type)
			|| !strcasecmp(ast_sdp_get_m(sdp_state->remote_sdp, stream_index)->type,
				ast_codec_media_type2str(type))) {
			/* Stream is still declined */
			return sdp_add_m_from_declined_remote_stream(sdp, sdp_state, stream_index);
		}
	}

	/* Build a new declined stream in our offer. */
	switch (type) {
	case AST_MEDIA_TYPE_AUDIO:
	case AST_MEDIA_TYPE_VIDEO:
		proto = "RTP/AVP";
		break;
	case AST_MEDIA_TYPE_IMAGE:
		proto = "udptl";
		break;
	default:
		/* Stream type not supported */
		ast_assert(0);
		return -1;
	}
	m_line = ast_sdp_m_alloc(ast_codec_media_type2str(type), 0, 1, proto, NULL);
	if (!m_line) {
		return -1;
	}

	/* Add a dummy static payload type */
	switch (type) {
	case AST_MEDIA_TYPE_AUDIO:
		fmt = "0"; /* ulaw */
		break;
	case AST_MEDIA_TYPE_VIDEO:
		fmt = "31"; /* H.261 */
		break;
	case AST_MEDIA_TYPE_IMAGE:
		fmt = "t38"; /* T.38 */
		break;
	default:
		/* Stream type not supported */
		ast_assert(0);
		ast_sdp_m_free(m_line);
		return -1;
	}
	payload = ast_sdp_payload_alloc(fmt);
	if (!payload || ast_sdp_m_add_payload(m_line, payload)) {
		ast_sdp_payload_free(payload);
		ast_sdp_m_free(m_line);
		return -1;
	}

	if (ast_sdp_add_m(sdp, m_line)) {
		ast_sdp_m_free(m_line);
		return -1;
	}

	return 0;
}

static int sdp_add_m_from_rtp_stream(struct ast_sdp *sdp, const struct ast_sdp_state *sdp_state,
	const struct sdp_state_capabilities *capabilities, int stream_index)
{
	struct ast_stream *stream;
	struct ast_sdp_m_line *m_line;
	struct ast_format_cap *caps;
	int i;
	int rtp_code;
	int rtp_port;
	int min_packet_size = 0;
	int max_packet_size = 0;
	enum ast_media_type media_type;
	char tmp[64];
	struct sdp_state_stream *stream_state;
	struct ast_rtp_instance *rtp;
	struct ast_sdp_a_line *a_line;
	const struct ast_sdp_options *options;
	const char *direction;

	stream = ast_stream_topology_get_stream(capabilities->topology, stream_index);

	ast_assert(sdp && sdp_state && stream);

	options = sdp_state->options;
	caps = ast_stream_get_formats(stream);

	stream_state = AST_VECTOR_GET(&capabilities->streams, stream_index);
	if (stream_state->rtp && caps && ast_format_cap_count(caps)
		&& AST_STREAM_STATE_REMOVED != ast_stream_get_state(stream)) {
		rtp = stream_state->rtp->instance;
	} else {
		/* This is a disabled stream */
		rtp = NULL;
	}

	if (rtp) {
		struct ast_sockaddr address_rtp;

		if (sdp_state_stream_get_connection_address(sdp_state, stream_state, &address_rtp)) {
			return -1;
		}
		rtp_port = ast_sockaddr_port(&address_rtp);
	} else {
		rtp_port = 0;
	}

	media_type = ast_stream_get_type(stream);
	if (!rtp_port) {
		/* Declined/disabled stream */
		return sdp_add_m_from_declined_stream(sdp, sdp_state, media_type, stream_index);
	}

	/* Stream is not declined/disabled */
	m_line = ast_sdp_m_alloc(ast_codec_media_type2str(media_type), rtp_port, 1,
		options->encryption != AST_SDP_ENCRYPTION_DISABLED ? "RTP/SAVP" : "RTP/AVP",
		NULL);
	if (!m_line) {
		return -1;
	}

	for (i = 0; i < ast_format_cap_count(caps); i++) {
		struct ast_format *format = ast_format_cap_get_format(caps, i);

		rtp_code = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(rtp), 1,
			format, 0);
		if (rtp_code == -1) {
			ast_log(LOG_WARNING,"Unable to get rtp codec payload code for %s\n",
				ast_format_get_name(format));
			ao2_ref(format, -1);
			continue;
		}

		if (ast_sdp_m_add_format(m_line, options, rtp_code, 1, format, 0)) {
			ast_sdp_m_free(m_line);
			ao2_ref(format, -1);
			return -1;
		}

		if (ast_format_get_maximum_ms(format)
			&& ((ast_format_get_maximum_ms(format) < max_packet_size)
				|| !max_packet_size)) {
			max_packet_size = ast_format_get_maximum_ms(format);
		}

		ao2_ref(format, -1);
	}

	if (media_type != AST_MEDIA_TYPE_VIDEO
		&& (options->dtmf == AST_SDP_DTMF_RFC_4733 || options->dtmf == AST_SDP_DTMF_AUTO)) {
		i = AST_RTP_DTMF;
		rtp_code = ast_rtp_codecs_payload_code(
			ast_rtp_instance_get_codecs(rtp), 0, NULL, i);
		if (-1 < rtp_code) {
			if (ast_sdp_m_add_format(m_line, options, rtp_code, 0, NULL, i)) {
				ast_sdp_m_free(m_line);
				return -1;
			}

			snprintf(tmp, sizeof(tmp), "%d 0-16", rtp_code);
			a_line = ast_sdp_a_alloc("fmtp", tmp);
			if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
				ast_sdp_a_free(a_line);
				ast_sdp_m_free(m_line);
				return -1;
			}
		}
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

	if (sdp_state->locally_held || stream_state->locally_held) {
		if (stream_state->remotely_held) {
			direction = "inactive";
		} else {
			direction = "sendonly";
		}
	} else {
		if (stream_state->remotely_held) {
			direction = "recvonly";
		} else {
			/* Default is "sendrecv" */
			direction = NULL;
		}
	}
	if (direction) {
		a_line = ast_sdp_a_alloc(direction, "");
		if (!a_line || ast_sdp_m_add_a(m_line, a_line)) {
			ast_sdp_a_free(a_line);
			ast_sdp_m_free(m_line);
			return -1;
		}
	}

	add_ssrc_attributes(m_line, options, rtp);

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
	const struct sdp_state_capabilities *capabilities, int stream_index)
{
	struct ast_stream *stream;
	struct ast_sdp_m_line *m_line;
	struct ast_sdp_payload *payload;
	enum ast_media_type media_type;
	char tmp[64];
	struct sdp_state_udptl *udptl;
	struct ast_sdp_a_line *a_line;
	struct sdp_state_stream *stream_state;
	int udptl_port;

	stream = ast_stream_topology_get_stream(capabilities->topology, stream_index);

	ast_assert(sdp && sdp_state && stream);

	stream_state = AST_VECTOR_GET(&capabilities->streams, stream_index);
	if (stream_state->udptl
		&& AST_STREAM_STATE_REMOVED != ast_stream_get_state(stream)) {
		udptl = stream_state->udptl;
	} else {
		/* This is a disabled stream */
		udptl = NULL;
	}

	if (udptl) {
		struct ast_sockaddr address_udptl;

		if (sdp_state_stream_get_connection_address(sdp_state, stream_state, &address_udptl)) {
			return -1;
		}
		udptl_port = ast_sockaddr_port(&address_udptl);
	} else {
		udptl_port = 0;
	}

	media_type = ast_stream_get_type(stream);
	if (!udptl_port) {
		/* Declined/disabled stream */
		return sdp_add_m_from_declined_stream(sdp, sdp_state, media_type, stream_index);
	}

	/* Stream is not declined/disabled */
	m_line = ast_sdp_m_alloc(ast_codec_media_type2str(media_type), udptl_port, 1,
		"udptl", NULL);
	if (!m_line) {
		return -1;
	}

	payload = ast_sdp_payload_alloc("t38");
	if (!payload || ast_sdp_m_add_payload(m_line, payload)) {
		ast_sdp_payload_free(payload);
		ast_sdp_m_free(m_line);
		return -1;
	}

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

	options = sdp_state->options;
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
	t_line = ast_sdp_t_alloc(0, 0);
	if (!t_line) {
		goto error;
	}

	sdp = ast_sdp_alloc(o_line, c_line, s_line, t_line);
	if (!sdp) {
		goto error;
	}

	stream_count = ast_stream_topology_get_count(topology);
	for (stream_num = 0; stream_num < stream_count; stream_num++) {
		switch (ast_stream_get_type(ast_stream_topology_get_stream(topology, stream_num))) {
		case AST_MEDIA_TYPE_AUDIO:
		case AST_MEDIA_TYPE_VIDEO:
			if (sdp_add_m_from_rtp_stream(sdp, sdp_state, capabilities, stream_num)) {
				goto error;
			}
			break;
		case AST_MEDIA_TYPE_IMAGE:
			if (sdp_add_m_from_udptl_stream(sdp, sdp_state, capabilities, stream_num)) {
				goto error;
			}
			break;
		case AST_MEDIA_TYPE_UNKNOWN:
		case AST_MEDIA_TYPE_TEXT:
		case AST_MEDIA_TYPE_END:
			/* Decline any of these streams from the remote. */
			if (sdp_add_m_from_declined_remote_stream(sdp, sdp_state, stream_num)) {
				goto error;
			}
			break;
		}
	}

	return sdp;

error:
	if (sdp) {
		ao2_ref(sdp, -1);
	} else {
		ast_sdp_t_free(t_line);
		ast_sdp_s_free(s_line);
		ast_sdp_c_free(c_line);
		ast_sdp_o_free(o_line);
	}

	return NULL;
}

