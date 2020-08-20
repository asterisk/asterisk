/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

/*! \file
 *
 * \brief Media Stream API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/logger.h"
#include "asterisk/stream.h"
#include "asterisk/strings.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/vector.h"
#include "asterisk/config.h"
#include "asterisk/rtp_engine.h"

struct ast_stream_metadata_entry {
	size_t length;
	int value_start;
	char name_value[0];
};

const char *ast_stream_codec_negotiation_params_map[] = {
	[CODEC_NEGOTIATION_PARAM_UNSPECIFIED] = "unspecified",
	[CODEC_NEGOTIATION_PARAM_PREFER] = "prefer",
	[CODEC_NEGOTIATION_PARAM_OPERATION] = "operation",
	[CODEC_NEGOTIATION_PARAM_KEEP] = "keep",
	[CODEC_NEGOTIATION_PARAM_TRANSCODE] = "transcode",
};

const char *ast_stream_codec_negotiation_prefer_map[] = {
	[CODEC_NEGOTIATION_PREFER_UNSPECIFIED] = "unspecified",
	[CODEC_NEGOTIATION_PREFER_PENDING] = "pending",
	[CODEC_NEGOTIATION_PREFER_CONFIGURED] = "configured",
};

const char *ast_stream_codec_negotiation_operation_map[] = {
	[CODEC_NEGOTIATION_OPERATION_UNSPECIFIED] = "unspecified",
	[CODEC_NEGOTIATION_OPERATION_INTERSECT] = "intersect",
	[CODEC_NEGOTIATION_OPERATION_UNION] = "union",
	[CODEC_NEGOTIATION_OPERATION_ONLY_PREFERRED] = "only_preferred",
	[CODEC_NEGOTIATION_OPERATION_ONLY_NONPREFERRED] = "only_nonpreferred",
};

const char *ast_stream_codec_negotiation_keep_map[] = {
	[CODEC_NEGOTIATION_KEEP_UNSPECIFIED] = "unspecified",
	[CODEC_NEGOTIATION_KEEP_ALL] = "all",
	[CODEC_NEGOTIATION_KEEP_FIRST] = "first",
};

const char *ast_stream_codec_negotiation_transcode_map[] = {
	[CODEC_NEGOTIATION_TRANSCODE_UNSPECIFIED] = "unspecified",
	[CODEC_NEGOTIATION_TRANSCODE_ALLOW] = "allow",
	[CODEC_NEGOTIATION_TRANSCODE_PREVENT] = "prevent",
};

struct ast_stream {
	/*!
	 * \brief The type of media the stream is handling
	 */
	enum ast_media_type type;

	/*!
	 * \brief The position of the stream in the topology
	 */
	unsigned int position;

	/*!
	 * \brief Current formats negotiated on the stream
	 */
	struct ast_format_cap *formats;

	/*!
	 * \brief The current state of the stream
	 */
	enum ast_stream_state state;

	/*!
	 * \brief Stream metadata vector
	 */
	struct ast_variable *metadata;

	/*!
	 * \brief The group that the stream is part of
	 */
	int group;

	/*!
	 * \brief The rtp_codecs used by the stream
	 */
	struct ast_rtp_codecs *rtp_codecs;

	/*!
	 * \brief Name for the stream within the context of the channel it is on
	 */
	char name[0];
};

struct ast_stream_topology {
	/*!
	 * \brief A vector of all the streams in this topology
	 */
	AST_VECTOR(, struct ast_stream *) streams;
	/*! Indicates that this topology should not have further operations applied to it. */
	int final;
};

const char *ast_stream_codec_prefs_to_str(const struct ast_stream_codec_negotiation_prefs *prefs, struct ast_str **buf)
{
	if (!prefs || !buf || !*buf) {
		return "";
	}

	ast_str_append(buf, 0, "%s:%s, %s:%s, %s:%s, %s:%s",
		ast_stream_codec_param_to_str(CODEC_NEGOTIATION_PARAM_PREFER),
		ast_stream_codec_prefer_to_str(prefs->prefer),
		ast_stream_codec_param_to_str(CODEC_NEGOTIATION_PARAM_OPERATION),
		ast_stream_codec_operation_to_str(prefs->operation),
		ast_stream_codec_param_to_str(CODEC_NEGOTIATION_PARAM_KEEP),
		ast_stream_codec_keep_to_str(prefs->keep),
		ast_stream_codec_param_to_str(CODEC_NEGOTIATION_PARAM_TRANSCODE),
		ast_stream_codec_transcode_to_str(prefs->transcode)
		);

	return ast_str_buffer(*buf);
}

/*!
 * \internal
 * \brief Sets a codec prefs member based on a the preference name and string value
 *
 * \warning This macro will cause the calling function to return if a preference name
 * is matched but the value isn't valid for this preference.
 */
#define set_pref_value(_name, _value, _prefs, _UC, _lc, _error_message) \
({ \
	int _res = 0; \
	if (strcmp(_name, ast_stream_codec_negotiation_params_map[CODEC_NEGOTIATION_PARAM_ ## _UC]) == 0) { \
		int i; \
		for (i = CODEC_NEGOTIATION_ ## _UC ## _UNSPECIFIED + 1; i < CODEC_NEGOTIATION_ ## _UC ## _END; i++) { \
			if (strcmp(value, ast_stream_codec_negotiation_ ## _lc ## _map[i]) == 0) { \
				prefs->_lc = i; \
			} \
		} \
		if ( prefs->_lc == CODEC_NEGOTIATION_ ## _UC ## _UNSPECIFIED) { \
			_res = -1; \
			if (_error_message) { \
				ast_str_append(_error_message, 0, "Codec preference '%s' has invalid value '%s'", name, value); \
			} \
		} \
	} \
	if (_res < 0) { \
		return _res; \
	} \
})

int ast_stream_codec_prefs_parse(const char *pref_string, struct ast_stream_codec_negotiation_prefs *prefs,
	struct ast_str **error_message)
{
	char *initial_value = ast_strdupa(pref_string);
	char *current_value;
	char *pref;
	char *saveptr1;
	char *saveptr2;
	char *name;
	char *value;

	if (!prefs) {
		return -1;
	}

	prefs->prefer = CODEC_NEGOTIATION_PREFER_UNSPECIFIED;
	prefs->operation = CODEC_NEGOTIATION_OPERATION_UNSPECIFIED;
	prefs->keep = CODEC_NEGOTIATION_KEEP_UNSPECIFIED;
	prefs->transcode = CODEC_NEGOTIATION_TRANSCODE_UNSPECIFIED;

	for (current_value = initial_value; (pref = strtok_r(current_value, ",", &saveptr1)) != NULL; ) {
		name = strtok_r(pref, ": ", &saveptr2);
		value = strtok_r(NULL, ": ", &saveptr2);

		if (!name || !value) {
			if (error_message) {
				ast_str_append(error_message, 0, "Codec preference '%s' is invalid", pref);
			}
			return -1;
		}

		set_pref_value(name, value, prefs, OPERATION, operation, error_message);
		set_pref_value(name, value, prefs, PREFER, prefer, error_message);
		set_pref_value(name, value, prefs, KEEP, keep, error_message);
		set_pref_value(name, value, prefs, TRANSCODE, transcode, error_message);

		current_value = NULL;
	}

	return 0;
}

const char *ast_stream_state_map[] = {
	[AST_STREAM_STATE_REMOVED] = "removed",
	[AST_STREAM_STATE_SENDRECV] = "sendrecv",
	[AST_STREAM_STATE_SENDONLY] = "sendonly",
	[AST_STREAM_STATE_RECVONLY] = "recvonly",
	[AST_STREAM_STATE_INACTIVE] = "inactive",
};

#define MIN_STREAM_NAME_LEN 16

struct ast_stream *ast_stream_alloc(const char *name, enum ast_media_type type)
{
	struct ast_stream *stream;
	size_t name_len = MAX(strlen(S_OR(name, "")), MIN_STREAM_NAME_LEN); /* Ensure there is enough room for 'removed' or a type-position */

	stream = ast_calloc(1, sizeof(*stream) + name_len + 1);
	if (!stream) {
		return NULL;
	}

	stream->type = type;
	stream->state = AST_STREAM_STATE_INACTIVE;
	stream->group = -1;
	strcpy(stream->name, S_OR(name, "")); /* Safe */

	stream->formats = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!stream->formats) {
		ast_free(stream);
		return NULL;
	}

	return stream;
}

struct ast_stream *ast_stream_clone(const struct ast_stream *stream, const char *name)
{
	struct ast_stream *new_stream;
	const char *stream_name;
	size_t name_len;

	if (!stream) {
		return NULL;
	}

	stream_name = name ?: stream->name;
	name_len = MAX(strlen(S_OR(stream_name, "")), MIN_STREAM_NAME_LEN); /* Ensure there is enough room for 'removed' or a type-position */
	new_stream = ast_calloc(1, sizeof(*stream) + name_len + 1);
	if (!new_stream) {
		return NULL;
	}

	memcpy(new_stream, stream, sizeof(*new_stream));
	strcpy(new_stream->name, stream_name); /* Safe */
	new_stream->group = -1;

	new_stream->formats = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!new_stream->formats) {
		ast_free(new_stream);
		return NULL;
	}
	ast_format_cap_append_from_cap(new_stream->formats, stream->formats, AST_MEDIA_TYPE_UNKNOWN);

	new_stream->metadata = ast_stream_get_metadata_list(stream);

	/* rtp_codecs aren't cloned */

	return new_stream;
}

void ast_stream_free(struct ast_stream *stream)
{
	if (!stream) {
		return;
	}

	ast_variables_destroy(stream->metadata);

	if (stream->rtp_codecs) {
		ast_rtp_codecs_payloads_destroy(stream->rtp_codecs);
	}

	ao2_cleanup(stream->formats);

	ast_free(stream);
}

const char *ast_stream_get_name(const struct ast_stream *stream)
{
	ast_assert(stream != NULL);

	return stream->name;
}

enum ast_media_type ast_stream_get_type(const struct ast_stream *stream)
{
	ast_assert(stream != NULL);

	return stream->type;
}

void ast_stream_set_type(struct ast_stream *stream, enum ast_media_type type)
{
	ast_assert(stream != NULL);

	stream->type = type;
}

const struct ast_format_cap *ast_stream_get_formats(const struct ast_stream *stream)
{
	ast_assert(stream != NULL);

	return stream->formats;
}

const char *ast_stream_to_str(const struct ast_stream *stream, struct ast_str **buf)
{
	if (!buf || !*buf) {
		return "";
	}

	if (!stream) {
		ast_str_append(buf, 0, "(null stream)");
		return ast_str_buffer(*buf);
	}

	ast_str_append(buf, 0, "%d:%s:%s:%s ",
		stream->position,
		S_OR(stream->name, "noname"),
		ast_codec_media_type2str(stream->type),
		ast_stream_state_map[stream->state]);
	ast_format_cap_append_names(stream->formats, buf);

	return ast_str_buffer(*buf);
}

int ast_stream_get_format_count(const struct ast_stream *stream)
{
	ast_assert(stream != NULL);

	return stream->formats ? ast_format_cap_count(stream->formats) : 0;
}

void ast_stream_set_formats(struct ast_stream *stream, struct ast_format_cap *caps)
{
	ast_assert(stream != NULL);

	ao2_cleanup(stream->formats);
	stream->formats = ao2_bump(caps);
}

enum ast_stream_state ast_stream_get_state(const struct ast_stream *stream)
{
	ast_assert(stream != NULL);

	return stream->state;
}

void ast_stream_set_state(struct ast_stream *stream, enum ast_stream_state state)
{
	ast_assert(stream != NULL);

	stream->state = state;

}

const char *ast_stream_state2str(enum ast_stream_state state)
{
	switch (state) {
	case AST_STREAM_STATE_REMOVED:
		return "removed";
	case AST_STREAM_STATE_SENDRECV:
		return "sendrecv";
	case AST_STREAM_STATE_SENDONLY:
		return "sendonly";
	case AST_STREAM_STATE_RECVONLY:
		return "recvonly";
	case AST_STREAM_STATE_INACTIVE:
		return "inactive";
	default:
		return "<unknown>";
	}
}

enum ast_stream_state ast_stream_str2state(const char *str)
{
	if (!strcmp("sendrecv", str)) {
		return AST_STREAM_STATE_SENDRECV;
	}
	if (!strcmp("sendonly", str)) {
		return AST_STREAM_STATE_SENDONLY;
	}
	if (!strcmp("recvonly", str)) {
		return AST_STREAM_STATE_RECVONLY;
	}
	if (!strcmp("inactive", str)) {
		return AST_STREAM_STATE_INACTIVE;
	}
	return AST_STREAM_STATE_REMOVED;
}

const char *ast_stream_get_metadata(const struct ast_stream *stream, const char *m_key)
{
	struct ast_variable *v;

	ast_assert_return(stream != NULL, NULL);
	ast_assert_return(m_key != NULL, NULL);

	for (v = stream->metadata; v; v = v->next) {
		if (strcmp(v->name, m_key) == 0) {
			return v->value;
		}
	}

	return NULL;
}

struct ast_variable *ast_stream_get_metadata_list(const struct ast_stream *stream)
{
	struct ast_variable *v;
	struct ast_variable *vout = NULL;

	ast_assert_return(stream != NULL, NULL);

	for (v = stream->metadata; v; v = v->next) {
		struct ast_variable *vt = ast_variable_new(v->name, v->value, "");

		if (!vt) {
			ast_variables_destroy(vout);
			return NULL;
		}

		ast_variable_list_append(&vout, vt);
	}

	return vout;
}

int ast_stream_set_metadata(struct ast_stream *stream, const char *m_key, const char *value)
{
	struct ast_variable *v;
	struct ast_variable *prev;

	ast_assert_return(stream != NULL, -1);
	ast_assert_return(m_key != NULL, -1);

	prev = NULL;
	v = stream->metadata;
	while(v) {
		struct ast_variable *next = v->next;
		if (strcmp(v->name, m_key) == 0) {
			if (prev) {
				prev->next = next;
			} else {
				stream->metadata = next;
			}
			ast_free(v);
			break;
		} else {
			prev = v;
		}
		v = next;
	}

	if (!value) {
		return 0;
	}

	v = ast_variable_new(m_key, value, "");
	if (!v) {
		return -1;
	}

	ast_variable_list_append(&stream->metadata, v);

	return 0;
}

int ast_stream_get_position(const struct ast_stream *stream)
{
	ast_assert(stream != NULL);

	return stream->position;
}

struct ast_rtp_codecs *ast_stream_get_rtp_codecs(const struct ast_stream *stream)
{
	ast_assert(stream != NULL);

	return stream->rtp_codecs;
}

void ast_stream_set_rtp_codecs(struct ast_stream *stream, struct ast_rtp_codecs *rtp_codecs)
{
	ast_assert(stream != NULL);

	if (stream->rtp_codecs) {
		ast_rtp_codecs_payloads_destroy(rtp_codecs);
	}

	stream->rtp_codecs = rtp_codecs;
}

struct ast_stream *ast_stream_create_resolved(struct ast_stream *pending_stream,
	struct ast_stream *validation_stream, struct ast_stream_codec_negotiation_prefs *prefs,
	struct ast_str **error_message)
{
	struct ast_format_cap *preferred_caps = NULL;
	struct ast_format_cap *nonpreferred_caps = NULL;
	struct ast_format_cap *joint_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	struct ast_stream *joint_stream;
	enum ast_media_type media_type = pending_stream ? pending_stream->type : AST_MEDIA_TYPE_UNKNOWN;
	int res = 0;
	SCOPE_ENTER(4, "Pending: %s  Validation: %s  Prefs: %s\n",
		ast_str_tmp(128, ast_stream_to_str(pending_stream, &STR_TMP)),
		ast_str_tmp(128, ast_stream_to_str(validation_stream, &STR_TMP)),
		ast_str_tmp(128, ast_stream_codec_prefs_to_str(prefs, &STR_TMP)));

	if (!pending_stream || !validation_stream || !prefs || !joint_caps
		|| media_type == AST_MEDIA_TYPE_UNKNOWN) {
		if (error_message) {
			ast_str_append(error_message, 0, "Invalid arguments");
		}
		ao2_cleanup(joint_caps);
		SCOPE_EXIT_RTN_VALUE(NULL, "Invalid arguments\n");
	}

	if (prefs->prefer == CODEC_NEGOTIATION_PREFER_PENDING) {
		preferred_caps = pending_stream->formats;
		nonpreferred_caps = validation_stream->formats;
	} else {
		preferred_caps = validation_stream->formats;
		nonpreferred_caps = pending_stream->formats;
	}
	ast_format_cap_set_framing(joint_caps, ast_format_cap_get_framing(pending_stream->formats));

	switch(prefs->operation) {
	case CODEC_NEGOTIATION_OPERATION_ONLY_PREFERRED:
		res = ast_format_cap_append_from_cap(joint_caps, preferred_caps, media_type);
		break;
	case CODEC_NEGOTIATION_OPERATION_ONLY_NONPREFERRED:
		res = ast_format_cap_append_from_cap(joint_caps, nonpreferred_caps, media_type);
		break;
	case CODEC_NEGOTIATION_OPERATION_INTERSECT:
		res = ast_format_cap_get_compatible(preferred_caps, nonpreferred_caps, joint_caps);
		break;
	case CODEC_NEGOTIATION_OPERATION_UNION:
		res = ast_format_cap_append_from_cap(joint_caps, preferred_caps, media_type);
		if (res == 0) {
			res = ast_format_cap_append_from_cap(joint_caps, nonpreferred_caps, media_type);
		}
		break;
	default:
		break;
	}

	if (res) {
		if (error_message) {
			ast_str_append(error_message, 0, "No common formats available for media type '%s' ",
				ast_codec_media_type2str(pending_stream->type));
			ast_format_cap_append_names(preferred_caps, error_message);
			ast_str_append(error_message, 0, "<>");
			ast_format_cap_append_names(nonpreferred_caps, error_message);
			ast_str_append(error_message, 0, " with prefs: ");
			ast_stream_codec_prefs_to_str(prefs, error_message);
		}

		ao2_cleanup(joint_caps);
		SCOPE_EXIT_RTN_VALUE(NULL, "No common formats available\n");
	}

	if (!ast_format_cap_empty(joint_caps)) {
		if (prefs->keep == CODEC_NEGOTIATION_KEEP_FIRST) {
			struct ast_format *single = ast_format_cap_get_format(joint_caps, 0);
			ast_format_cap_remove_by_type(joint_caps, AST_MEDIA_TYPE_UNKNOWN);
			ast_format_cap_append(joint_caps, single, 0);
			ao2_ref(single, -1);
		}
	} else {
		if (error_message) {
			ast_str_append(error_message, 0, "No common formats available for media type '%s' ",
				ast_codec_media_type2str(pending_stream->type));
			ast_format_cap_append_names(preferred_caps, error_message);
			ast_str_append(error_message, 0, "<>");
			ast_format_cap_append_names(nonpreferred_caps, error_message);
			ast_str_append(error_message, 0, " with prefs: ");
			ast_stream_codec_prefs_to_str(prefs, error_message);
		}
	}

	joint_stream = ast_stream_clone(pending_stream, NULL);
	if (!joint_stream) {
		ao2_cleanup(joint_caps);
		return NULL;
	}

	/* ref to joint_caps will be transferred to the stream */
	ast_stream_set_formats(joint_stream, joint_caps);

	if (TRACE_ATLEAST(3)) {
		struct ast_str *buf = ast_str_create((AST_FORMAT_CAP_NAMES_LEN * 3) + AST_STREAM_MAX_CODEC_PREFS_LENGTH);
		if (buf) {
			ast_str_set(&buf, 0, "Resolved '%s' stream ", ast_codec_media_type2str(pending_stream->type));
			ast_format_cap_append_names(preferred_caps, &buf);
			ast_str_append(&buf, 0, "<>");
			ast_format_cap_append_names(nonpreferred_caps, &buf);
			ast_str_append(&buf, 0, " to ");
			ast_format_cap_append_names(joint_caps, &buf);
			ast_str_append(&buf, 0, " with prefs: ");
			ast_stream_codec_prefs_to_str(prefs, &buf);
			ast_trace(1, "%s\n", ast_str_buffer(buf));
			ast_free(buf);
		}
	}

	ao2_cleanup(joint_caps);
	SCOPE_EXIT_RTN_VALUE(joint_stream, "Joint stream: %s\n", ast_str_tmp(128, ast_stream_to_str(joint_stream, &STR_TMP)));
}

static void stream_topology_destroy(void *data)
{
	struct ast_stream_topology *topology = data;

	AST_VECTOR_CALLBACK_VOID(&topology->streams, ast_stream_free);
	AST_VECTOR_FREE(&topology->streams);
}

#define TOPOLOGY_INITIAL_STREAM_COUNT 2
struct ast_stream_topology *ast_stream_topology_alloc(void)
{
	struct ast_stream_topology *topology;

	topology = ao2_alloc_options(sizeof(*topology), stream_topology_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!topology) {
		return NULL;
	}

	if (AST_VECTOR_INIT(&topology->streams, TOPOLOGY_INITIAL_STREAM_COUNT)) {
		ao2_ref(topology, -1);
		topology = NULL;
	}

	return topology;
}

struct ast_stream_topology *ast_stream_topology_clone(
	const struct ast_stream_topology *topology)
{
	struct ast_stream_topology *new_topology;
	int i;

	ast_assert(topology != NULL);

	new_topology = ast_stream_topology_alloc();
	if (!new_topology) {
		return NULL;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&topology->streams); i++) {
		struct ast_stream *existing = AST_VECTOR_GET(&topology->streams, i);
		struct ast_stream *stream = ast_stream_clone(existing, NULL);

		if (!stream || AST_VECTOR_APPEND(&new_topology->streams, stream)) {
			ast_stream_free(stream);
			ast_stream_topology_free(new_topology);
			return NULL;
		}

		ast_stream_set_group(stream, ast_stream_get_group(existing));
	}

	return new_topology;
}

int ast_stream_topology_equal(const struct ast_stream_topology *left,
	const struct ast_stream_topology *right)
{
	int index;

	ast_assert(left != NULL);
	ast_assert(right != NULL);

	if (ast_stream_topology_get_count(left) != ast_stream_topology_get_count(right)) {
		return 0;
	}

	for (index = 0; index < ast_stream_topology_get_count(left); ++index) {
		const struct ast_stream *left_stream = ast_stream_topology_get_stream(left, index);
		const struct ast_stream *right_stream = ast_stream_topology_get_stream(right, index);

		if (ast_stream_get_type(left_stream) != ast_stream_get_type(right_stream)) {
			return 0;
		}

		if (ast_stream_get_state(left_stream) != ast_stream_get_state(right_stream)) {
			return 0;
		}

		if (!ast_stream_get_formats(left_stream) && ast_stream_get_formats(right_stream) &&
			ast_format_cap_count(ast_stream_get_formats(right_stream))) {
			/* A NULL format capabilities and an empty format capabilities are the same, as they have
			 * no formats inside. If one does though... they are not equal.
			 */
			return 0;
		} else if (!ast_stream_get_formats(right_stream) && ast_stream_get_formats(left_stream) &&
			ast_format_cap_count(ast_stream_get_formats(left_stream))) {
			return 0;
		} else if (ast_stream_get_formats(left_stream) && ast_stream_get_formats(right_stream) &&
			!ast_format_cap_identical(ast_stream_get_formats(left_stream), ast_stream_get_formats(right_stream))) {
			/* But if both are actually present we need to do an actual identical check. */
			return 0;
		}

		if (strcmp(ast_stream_get_name(left_stream), ast_stream_get_name(right_stream))) {
			return 0;
		}
	}

	return 1;
}

void ast_stream_topology_free(struct ast_stream_topology *topology)
{
	ao2_cleanup(topology);
}

int ast_stream_topology_append_stream(struct ast_stream_topology *topology, struct ast_stream *stream)
{
	ast_assert(topology && stream);

	if (AST_VECTOR_APPEND(&topology->streams, stream)) {
		return -1;
	}

	stream->position = AST_VECTOR_SIZE(&topology->streams) - 1;

	if (ast_strlen_zero(stream->name)) {
		snprintf(stream->name, MIN_STREAM_NAME_LEN, "%s-%d", ast_codec_media_type2str(stream->type), stream->position);
	}

	return AST_VECTOR_SIZE(&topology->streams) - 1;
}

int ast_stream_topology_get_count(const struct ast_stream_topology *topology)
{
	ast_assert(topology != NULL);

	return AST_VECTOR_SIZE(&topology->streams);
}

int ast_stream_topology_get_active_count(const struct ast_stream_topology *topology)
{
	int i;
	int count = 0;
	ast_assert(topology != NULL);

	for (i = 0; i < AST_VECTOR_SIZE(&topology->streams); i++) {
		struct ast_stream *stream = AST_VECTOR_GET(&topology->streams, i);
		if (stream->state != AST_STREAM_STATE_REMOVED) {
			count++;
		}
	}

	return count;
}

struct ast_stream *ast_stream_topology_get_stream(
	const struct ast_stream_topology *topology, unsigned int stream_num)
{
	ast_assert(topology != NULL);

	return AST_VECTOR_GET(&topology->streams, stream_num);
}

int ast_stream_topology_set_stream(struct ast_stream_topology *topology,
	unsigned int position, struct ast_stream *stream)
{
	struct ast_stream *existing_stream;

	ast_assert(topology && stream);

	if (position > AST_VECTOR_SIZE(&topology->streams)) {
		return -1;
	}

	if (position < AST_VECTOR_SIZE(&topology->streams)) {
		existing_stream = AST_VECTOR_GET(&topology->streams, position);
		ast_stream_free(existing_stream);
	}

	stream->position = position;

	if (position == AST_VECTOR_SIZE(&topology->streams)) {
		return AST_VECTOR_APPEND(&topology->streams, stream);
	}

	if (ast_strlen_zero(stream->name)) {
		snprintf(stream->name, MIN_STREAM_NAME_LEN, "%s-%d", ast_codec_media_type2str(stream->type), stream->position);
	}

	return AST_VECTOR_REPLACE(&topology->streams, position, stream);
}

int ast_stream_topology_del_stream(struct ast_stream_topology *topology,
	unsigned int position)
{
	struct ast_stream *stream;

	ast_assert(topology != NULL);

	if (AST_VECTOR_SIZE(&topology->streams) <= position) {
		return -1;
	}

	stream = AST_VECTOR_REMOVE_ORDERED(&topology->streams, position);
	ast_stream_free(stream);

	/* Fix up higher stream position indices */
	for (; position < AST_VECTOR_SIZE(&topology->streams); ++position) {
		stream = AST_VECTOR_GET(&topology->streams, position);
		stream->position = position;
	}

	return 0;
}

struct ast_stream_topology *ast_stream_topology_create_from_format_cap(
	struct ast_format_cap *cap)
{
	struct ast_stream_topology *topology;
	enum ast_media_type type;

	topology = ast_stream_topology_alloc();
	if (!topology || !cap || !ast_format_cap_count(cap)) {
		return topology;
	}

	for (type = AST_MEDIA_TYPE_UNKNOWN + 1; type < AST_MEDIA_TYPE_END; type++) {
		struct ast_format_cap *new_cap;
		struct ast_stream *stream;

		if (!ast_format_cap_has_type(cap, type)) {
			continue;
		}

		new_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!new_cap) {
			ast_stream_topology_free(topology);
			return NULL;
		}

		ast_format_cap_set_framing(new_cap, ast_format_cap_get_framing(cap));
		if (ast_format_cap_append_from_cap(new_cap, cap, type)) {
			ao2_cleanup(new_cap);
			ast_stream_topology_free(topology);
			return NULL;
		}

		stream = ast_stream_alloc(NULL, type);
		if (!stream) {
			ao2_cleanup(new_cap);
			ast_stream_topology_free(topology);
			return NULL;
		}

		ast_stream_set_formats(stream, new_cap);
		ao2_ref(new_cap, -1);
		stream->state = AST_STREAM_STATE_SENDRECV;
		if (ast_stream_topology_append_stream(topology, stream) == -1) {
			ast_stream_free(stream);
			ast_stream_topology_free(topology);
			return NULL;
		}

		snprintf(stream->name, MIN_STREAM_NAME_LEN, "%s-%d", ast_codec_media_type2str(stream->type), stream->position);
	}

	return topology;
}

struct ast_format_cap *ast_stream_topology_get_formats_by_type(
    struct ast_stream_topology *topology, enum ast_media_type type)
{
	struct ast_format_cap *caps;
	int i;

	ast_assert(topology != NULL);

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		return NULL;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&topology->streams); i++) {
		struct ast_stream *stream;

		stream = AST_VECTOR_GET(&topology->streams, i);
		if (!stream->formats || stream->state == AST_STREAM_STATE_REMOVED) {
			continue;
		}
		if (type == AST_MEDIA_TYPE_UNKNOWN || type == stream->type) {
			ast_format_cap_append_from_cap(caps, stream->formats, AST_MEDIA_TYPE_UNKNOWN);
		}
	}

	return caps;
}

struct ast_format_cap *ast_stream_topology_get_formats(
    struct ast_stream_topology *topology)
{
	return ast_stream_topology_get_formats_by_type(topology, AST_MEDIA_TYPE_UNKNOWN);
}

const char *ast_stream_topology_to_str(const struct ast_stream_topology *topology,
	struct ast_str **buf)
{
	int i;

	if (!buf ||!*buf) {
		return "";
	}

	if (!topology) {
		ast_str_append(buf, 0, "(null topology)");
		return ast_str_buffer(*buf);
	}

	ast_str_append(buf, 0, "%s", S_COR(topology->final, "final", ""));

	for (i = 0; i < AST_VECTOR_SIZE(&topology->streams); i++) {
		struct ast_stream *stream;

		stream = AST_VECTOR_GET(&topology->streams, i);
		ast_str_append(buf, 0, " <");
		ast_stream_to_str(stream, buf);
		ast_str_append(buf, 0, ">");
	}

	return ast_str_buffer(*buf);
}

struct ast_stream *ast_stream_topology_get_first_stream_by_type(
	const struct ast_stream_topology *topology,
	enum ast_media_type type)
{
	int i;

	ast_assert(topology != NULL);

	for (i = 0; i < AST_VECTOR_SIZE(&topology->streams); i++) {
		struct ast_stream *stream;

		stream = AST_VECTOR_GET(&topology->streams, i);
		if (stream->type == type
			&& stream->state != AST_STREAM_STATE_REMOVED) {
			return stream;
		}
	}

	return NULL;
}

void ast_stream_topology_map(const struct ast_stream_topology *topology,
	struct ast_vector_int *types, struct ast_vector_int *v0, struct ast_vector_int *v1)
{
	int i;
	int nths[AST_MEDIA_TYPE_END] = {0};
	int size = ast_stream_topology_get_count(topology);

	/*
	 * Clear out any old mappings and initialize the new ones
	 */
	AST_VECTOR_FREE(v0);
	AST_VECTOR_FREE(v1);

	/*
	 * Both vectors are sized to the topology. The media types vector is always
	 * guaranteed to be the size of the given topology or greater.
	 */
	AST_VECTOR_INIT(v0, size);
	AST_VECTOR_INIT(v1, size);

	for (i = 0; i < size; ++i) {
		struct ast_stream *stream = ast_stream_topology_get_stream(topology, i);
		enum ast_media_type type = ast_stream_get_type(stream);
		int index = AST_VECTOR_GET_INDEX_NTH(types, ++nths[type],
			type, AST_VECTOR_ELEM_DEFAULT_CMP);

		if (index == -1) {
			/*
			 * If a given type is not found for an index level then update the
			 * media types vector with that type. This keeps the media types
			 * vector always at the max topology size.
			 */
			AST_VECTOR_APPEND(types, type);
			index = AST_VECTOR_SIZE(types) - 1;
		}

		/*
		 * The mapping is reflexive in the sense that if it maps in one direction
		 * then the reverse direction maps back to the other's index.
		 */
		AST_VECTOR_REPLACE(v0, i, index);
		AST_VECTOR_REPLACE(v1, index, i);
	}
}

struct ast_stream_topology *ast_stream_topology_create_resolved(
	struct ast_stream_topology *pending_topology, struct ast_stream_topology *configured_topology,
	struct ast_stream_codec_negotiation_prefs *prefs, struct ast_str**error_message)
{
	struct ast_stream_topology *joint_topology = ast_stream_topology_alloc();
	int res = 0;
	int i;

	if (!pending_topology || !configured_topology || !joint_topology) {
		ao2_cleanup(joint_topology);
		return NULL;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&pending_topology->streams); i++) {
		struct ast_stream *pending_stream = AST_VECTOR_GET(&pending_topology->streams, i);
		struct ast_stream *configured_stream =
			ast_stream_topology_get_first_stream_by_type(configured_topology, pending_stream->type);
		struct ast_stream *joint_stream;

		if (!configured_stream) {
			joint_stream = ast_stream_clone(pending_stream, NULL);
			if (!joint_stream) {
				ao2_cleanup(joint_topology);
				return NULL;
			}
			ast_stream_set_state(joint_stream, AST_STREAM_STATE_REMOVED);
		} else {
			joint_stream = ast_stream_create_resolved(pending_stream, configured_stream, prefs, error_message);
			if (!joint_stream) {
				ao2_cleanup(joint_topology);
				return NULL;
			} else if (ast_stream_get_format_count(joint_stream) == 0) {
				ast_stream_set_state(joint_stream, AST_STREAM_STATE_REMOVED);
			}
		}

		res = ast_stream_topology_append_stream(joint_topology, joint_stream);
		if (res < 0) {
			ast_stream_free(joint_stream);
			ao2_cleanup(joint_topology);
			return NULL;
		}
	}

	return joint_topology;
}

int ast_stream_get_group(const struct ast_stream *stream)
{
	ast_assert(stream != NULL);

	return stream->group;
}

void ast_stream_set_group(struct ast_stream *stream, int group)
{
	ast_assert(stream != NULL);

	stream->group = group;
}
