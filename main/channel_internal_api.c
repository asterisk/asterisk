/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Channel Accessor API
 *
 * This file is intended to be the only file that ever accesses the
 * internals of an ast_channel. All other files should use the
 * accessor functions defined here.
 *
 * \author Terry Wilson
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <unistd.h>
#include <fcntl.h>

#include "asterisk/alertpipe.h"
#include "asterisk/paths.h"
#include "asterisk/channel.h"
#include "asterisk/channel_internal.h"
#include "asterisk/endpoints.h"
#include "asterisk/indications.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/stringfields.h"
#include "asterisk/stream.h"
#include "asterisk/test.h"
#include "asterisk/vector.h"
#include "channel_private.h"
#include "channelstorage.h"

/*! \brief The current channel storage driver */
const struct ast_channelstorage_driver *current_channel_storage_driver;
/*! \brief The current channel storage instance */
struct ast_channelstorage_instance *current_channel_storage_instance;

/*! \brief The monotonically increasing integer counter for channel uniqueids */
static int uniqueint;

/* ACCESSORS */

#define DEFINE_STRINGFIELD_SETTERS_FOR(field, assert_on_null) \
void ast_channel_##field##_set(struct ast_channel *chan, const char *value) \
{ \
	if ((assert_on_null)) ast_assert(!ast_strlen_zero(value)); \
	if (!strcmp(value, chan->field)) return; \
	ast_string_field_set(chan, field, value); \
} \
  \
void ast_channel_##field##_build_va(struct ast_channel *chan, const char *fmt, va_list ap) \
{ \
	ast_string_field_build_va(chan, field, fmt, ap); \
} \
void ast_channel_##field##_build(struct ast_channel *chan, const char *fmt, ...) \
{ \
	va_list ap; \
	va_start(ap, fmt); \
	ast_channel_##field##_build_va(chan, fmt, ap); \
	va_end(ap); \
}

#define DEFINE_STRINGFIELD_SETTERS_AND_INVALIDATE_FOR(field, publish, assert_on_null, invalidate) \
void ast_channel_##field##_set(struct ast_channel *chan, const char *value) \
{ \
	if ((assert_on_null)) ast_assert(!ast_strlen_zero(value)); \
	if (!strcmp(value, chan->field)) return; \
	ast_string_field_set(chan, field, value); \
	ast_channel_snapshot_invalidate_segment(chan, invalidate); \
	if (publish && ast_channel_internal_is_finalized(chan)) ast_channel_publish_snapshot(chan); \
} \
  \
void ast_channel_##field##_build_va(struct ast_channel *chan, const char *fmt, va_list ap) \
{ \
	ast_string_field_build_va(chan, field, fmt, ap); \
	ast_channel_snapshot_invalidate_segment(chan, invalidate); \
	if (publish && ast_channel_internal_is_finalized(chan)) ast_channel_publish_snapshot(chan); \
} \
void ast_channel_##field##_build(struct ast_channel *chan, const char *fmt, ...) \
{ \
	va_list ap; \
	va_start(ap, fmt); \
	ast_channel_##field##_build_va(chan, fmt, ap); \
	va_end(ap); \
}

DEFINE_STRINGFIELD_SETTERS_AND_INVALIDATE_FOR(language, 1, 0, AST_CHANNEL_SNAPSHOT_INVALIDATE_BASE);
DEFINE_STRINGFIELD_SETTERS_FOR(musicclass, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(latest_musicclass, 0);
DEFINE_STRINGFIELD_SETTERS_AND_INVALIDATE_FOR(accountcode, 1, 0, AST_CHANNEL_SNAPSHOT_INVALIDATE_BASE);
DEFINE_STRINGFIELD_SETTERS_AND_INVALIDATE_FOR(peeraccount, 1, 0, AST_CHANNEL_SNAPSHOT_INVALIDATE_PEER);
DEFINE_STRINGFIELD_SETTERS_AND_INVALIDATE_FOR(userfield, 0, 0, AST_CHANNEL_SNAPSHOT_INVALIDATE_BASE);
DEFINE_STRINGFIELD_SETTERS_FOR(call_forward, 0);
DEFINE_STRINGFIELD_SETTERS_FOR(parkinglot, 0);
DEFINE_STRINGFIELD_SETTERS_AND_INVALIDATE_FOR(hangupsource, 0, 0, AST_CHANNEL_SNAPSHOT_INVALIDATE_HANGUP);
DEFINE_STRINGFIELD_SETTERS_FOR(dialcontext, 0);

#define DEFINE_STRINGFIELD_GETTER_FOR(field) const char *ast_channel_##field(const struct ast_channel *chan) \
{ \
	return chan->field; \
}

DEFINE_STRINGFIELD_GETTER_FOR(name);
DEFINE_STRINGFIELD_GETTER_FOR(language);
DEFINE_STRINGFIELD_GETTER_FOR(musicclass);
DEFINE_STRINGFIELD_GETTER_FOR(latest_musicclass);
DEFINE_STRINGFIELD_GETTER_FOR(accountcode);
DEFINE_STRINGFIELD_GETTER_FOR(peeraccount);
DEFINE_STRINGFIELD_GETTER_FOR(userfield);
DEFINE_STRINGFIELD_GETTER_FOR(call_forward);
DEFINE_STRINGFIELD_GETTER_FOR(parkinglot);
DEFINE_STRINGFIELD_GETTER_FOR(hangupsource);
DEFINE_STRINGFIELD_GETTER_FOR(dialcontext);

void ast_channel_name_set(struct ast_channel *chan, const char *value)
{
	ast_assert(!ast_strlen_zero(value));
	ast_assert(!chan->linked_in_container);
	if (!strcmp(value, chan->name)) return;
	ast_string_field_set(chan, name, value);
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_BASE);
}

void ast_channel_name_build_va(struct ast_channel *chan, const char *fmt, va_list ap)
{
	ast_assert(!chan->linked_in_container);
	ast_string_field_build_va(chan, name, fmt, ap);
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_BASE); \
}

void ast_channel_name_build(struct ast_channel *chan, const char *fmt, ...)
{
	va_list ap;
	ast_assert(!chan->linked_in_container);
	va_start(ap, fmt);
	ast_channel_name_build_va(chan, fmt, ap);
	va_end(ap);
}

const char *ast_channel_uniqueid(const struct ast_channel *chan)
{
	ast_assert(chan->uniqueid.unique_id[0] != '\0');
	return chan->uniqueid.unique_id;
}

const char *ast_channel_linkedid(const struct ast_channel *chan)
{
	ast_assert(chan->linkedid.unique_id[0] != '\0');
	return chan->linkedid.unique_id;
}

const char *ast_channel_tenantid(const struct ast_channel *chan)
{
	/* It's ok for tenantid to be empty, so no need to assert */
	return chan->linkedid.tenant_id;
}

void ast_channel_tenantid_set(struct ast_channel *chan, const char *value)
{
	if (ast_strlen_zero(value)) {
		return;
	}
	ast_copy_string(chan->linkedid.tenant_id, value, sizeof(chan->linkedid.tenant_id));
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_BASE);
}

const char *ast_channel_appl(const struct ast_channel *chan)
{
	return chan->appl;
}
void ast_channel_appl_set(struct ast_channel *chan, const char *value)
{
	chan->appl = value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_DIALPLAN);
}
const char *ast_channel_blockproc(const struct ast_channel *chan)
{
	return chan->blockproc;
}
void ast_channel_blockproc_set(struct ast_channel *chan, const char *value)
{
	chan->blockproc = value;
}
const char *ast_channel_data(const struct ast_channel *chan)
{
	return chan->data;
}
void ast_channel_data_set(struct ast_channel *chan, const char *value)
{
	chan->data = value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_DIALPLAN);
}

const char *ast_channel_context(const struct ast_channel *chan)
{
	return chan->context;
}
const char *ast_channel_lastcontext(const struct ast_channel *chan)
{
	return chan->lastcontext;
}
void ast_channel_context_set(struct ast_channel *chan, const char *value)
{
	if (!*chan->lastcontext || strcmp(value, chan->context)) {
		/* only copy to last context when it changes, unless it's empty to begin with */
		ast_copy_string(chan->lastcontext, chan->context, sizeof(chan->lastcontext));
	}
	ast_copy_string(chan->context, value, sizeof(chan->context));
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_DIALPLAN);
}
const char *ast_channel_exten(const struct ast_channel *chan)
{
	return chan->exten;
}
const char *ast_channel_lastexten(const struct ast_channel *chan)
{
	return chan->lastexten;
}
void ast_channel_exten_set(struct ast_channel *chan, const char *value)
{
	if (!*chan->lastexten || strcmp(value, chan->exten)) {
		/* only copy to last exten when it changes, unless it's empty to begin with */
		ast_copy_string(chan->lastexten, chan->exten, sizeof(chan->lastexten));
	}
	ast_copy_string(chan->exten, value, sizeof(chan->exten));
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_DIALPLAN);
}

char ast_channel_dtmf_digit_to_emulate(const struct ast_channel *chan)
{
	return chan->dtmf_digit_to_emulate;
}
void ast_channel_dtmf_digit_to_emulate_set(struct ast_channel *chan, char value)
{
	chan->dtmf_digit_to_emulate = value;
}

char ast_channel_sending_dtmf_digit(const struct ast_channel *chan)
{
	return chan->sending_dtmf_digit;
}
void ast_channel_sending_dtmf_digit_set(struct ast_channel *chan, char value)
{
	chan->sending_dtmf_digit = value;
}

struct timeval ast_channel_sending_dtmf_tv(const struct ast_channel *chan)
{
	return chan->sending_dtmf_tv;
}
void ast_channel_sending_dtmf_tv_set(struct ast_channel *chan, struct timeval value)
{
	chan->sending_dtmf_tv = value;
}

enum ama_flags ast_channel_amaflags(const struct ast_channel *chan)
{
	return chan->amaflags;
}

void ast_channel_amaflags_set(struct ast_channel *chan, enum ama_flags value)
{
	if (chan->amaflags == value) {
		return;
	}
	chan->amaflags = value;
	ast_channel_publish_snapshot(chan);
}
int ast_channel_fdno(const struct ast_channel *chan)
{
	return chan->fdno;
}
void ast_channel_fdno_set(struct ast_channel *chan, int value)
{
	chan->fdno = value;
}
int ast_channel_hangupcause(const struct ast_channel *chan)
{
	return chan->hangupcause;
}
void ast_channel_hangupcause_set(struct ast_channel *chan, int value)
{
	chan->hangupcause = value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_HANGUP);
}
int ast_channel_tech_hangupcause(const struct ast_channel *chan)
{
	return chan->tech_hangupcause;
}
void ast_channel_tech_hangupcause_set(struct ast_channel *chan, int value)
{
	chan->tech_hangupcause = value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_HANGUP);
}
int ast_channel_priority(const struct ast_channel *chan)
{
	return chan->priority;
}
void ast_channel_priority_set(struct ast_channel *chan, int value)
{
	chan->priority = value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_DIALPLAN);
}
int ast_channel_rings(const struct ast_channel *chan)
{
	return chan->rings;
}
void ast_channel_rings_set(struct ast_channel *chan, int value)
{
	chan->rings = value;
}
int ast_channel_streamid(const struct ast_channel *chan)
{
	return chan->streamid;
}
void ast_channel_streamid_set(struct ast_channel *chan, int value)
{
	chan->streamid = value;
}
int ast_channel_timingfd(const struct ast_channel *chan)
{
	return chan->timingfd;
}
void ast_channel_timingfd_set(struct ast_channel *chan, int value)
{
	chan->timingfd = value;
}
int ast_channel_visible_indication(const struct ast_channel *chan)
{
	return chan->visible_indication;
}
void ast_channel_visible_indication_set(struct ast_channel *chan, int value)
{
	chan->visible_indication = value;
}
int ast_channel_hold_state(const struct ast_channel *chan)
{
	return chan->hold_state;
}
void ast_channel_hold_state_set(struct ast_channel *chan, int value)
{
	chan->hold_state = value;
}
int ast_channel_vstreamid(const struct ast_channel *chan)
{
	return chan->vstreamid;
}
void ast_channel_vstreamid_set(struct ast_channel *chan, int value)
{
	chan->vstreamid = value;
}
unsigned short ast_channel_transfercapability(const struct ast_channel *chan)
{
	return chan->transfercapability;
}
void ast_channel_transfercapability_set(struct ast_channel *chan, unsigned short value)
{
	chan->transfercapability = value;
}
unsigned int ast_channel_emulate_dtmf_duration(const struct ast_channel *chan)
{
	return chan->emulate_dtmf_duration;
}
void ast_channel_emulate_dtmf_duration_set(struct ast_channel *chan, unsigned int value)
{
	chan->emulate_dtmf_duration = value;
}
unsigned int ast_channel_fin(const struct ast_channel *chan)
{
	return chan->fin;
}
void ast_channel_fin_set(struct ast_channel *chan, unsigned int value)
{
	chan->fin = value;
}
unsigned int ast_channel_fout(const struct ast_channel *chan)
{
	return chan->fout;
}
void ast_channel_fout_set(struct ast_channel *chan, unsigned int value)
{
	chan->fout = value;
}
unsigned long ast_channel_insmpl(const struct ast_channel *chan)
{
	return chan->insmpl;
}
void ast_channel_insmpl_set(struct ast_channel *chan, unsigned long value)
{
	chan->insmpl = value;
}
unsigned long ast_channel_outsmpl(const struct ast_channel *chan)
{
	return chan->outsmpl;
}
void ast_channel_outsmpl_set(struct ast_channel *chan, unsigned long value)
{
	chan->outsmpl = value;
}
void *ast_channel_generatordata(const struct ast_channel *chan)
{
	return chan->generatordata;
}
void ast_channel_generatordata_set(struct ast_channel *chan, void *value)
{
	chan->generatordata = value;
}
void *ast_channel_music_state(const struct ast_channel *chan)
{
	return chan->music_state;
}
void ast_channel_music_state_set(struct ast_channel *chan, void *value)
{
	chan->music_state = value;
}
void *ast_channel_tech_pvt(const struct ast_channel *chan)
{
	return chan->tech_pvt;
}
void ast_channel_tech_pvt_set(struct ast_channel *chan, void *value)
{
	chan->tech_pvt = value;
	if (value != NULL) {
		ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_BASE);
	}
}
void *ast_channel_timingdata(const struct ast_channel *chan)
{
	return chan->timingdata;
}
void ast_channel_timingdata_set(struct ast_channel *chan, void *value)
{
	chan->timingdata = value;
}
struct ast_audiohook_list *ast_channel_audiohooks(const struct ast_channel *chan)
{
	return chan->audiohooks;
}
void ast_channel_audiohooks_set(struct ast_channel *chan, struct ast_audiohook_list *value)
{
	chan->audiohooks = value;
}
struct ast_cdr *ast_channel_cdr(const struct ast_channel *chan)
{
	return chan->cdr;
}
void ast_channel_cdr_set(struct ast_channel *chan, struct ast_cdr *value)
{
	chan->cdr = value;
}
struct ast_channel *ast_channel_masq(const struct ast_channel *chan)
{
	return chan->masq;
}
void ast_channel_masq_set(struct ast_channel *chan, struct ast_channel *value)
{
	chan->masq = value;
}
struct ast_channel *ast_channel_masqr(const struct ast_channel *chan)
{
	return chan->masqr;
}
void ast_channel_masqr_set(struct ast_channel *chan, struct ast_channel *value)
{
	chan->masqr = value;
}
struct ast_filestream *ast_channel_stream(const struct ast_channel *chan)
{
	return chan->stream;
}
void ast_channel_stream_set(struct ast_channel *chan, struct ast_filestream *value)
{
	chan->stream = value;
}
struct ast_filestream *ast_channel_vstream(const struct ast_channel *chan)
{
	return chan->vstream;
}
void ast_channel_vstream_set(struct ast_channel *chan, struct ast_filestream *value)
{
	chan->vstream = value;
}
struct ast_format_cap *ast_channel_nativeformats(const struct ast_channel *chan)
{
	return chan->nativeformats;
}

static void channel_set_default_streams(struct ast_channel *chan)
{
	enum ast_media_type type;

	ast_assert(chan != NULL);

	for (type = AST_MEDIA_TYPE_UNKNOWN; type < AST_MEDIA_TYPE_END; type++) {
		if (chan->stream_topology) {
			chan->default_streams[type] =
				ast_stream_topology_get_first_stream_by_type(chan->stream_topology, type);
		} else {
			chan->default_streams[type] = NULL;
		}
	}
}

void ast_channel_internal_set_stream_topology(struct ast_channel *chan,
	struct ast_stream_topology *topology)
{
	ast_stream_topology_free(chan->stream_topology);
	chan->stream_topology = topology;
	channel_set_default_streams(chan);
}

void ast_channel_internal_set_stream_topology_change_source(
	struct ast_channel *chan, void *change_source)
{
	chan->stream_topology_change_source = change_source;
}

void *ast_channel_get_stream_topology_change_source(struct ast_channel *chan)
{
	return chan->stream_topology_change_source;
}

void ast_channel_nativeformats_set(struct ast_channel *chan,
	struct ast_format_cap *value)
{
	SCOPE_ENTER(2, "%s: %sFormats: %s\n", S_OR(ast_channel_name(chan), "<initializing>"),
		S_COR(ast_channel_is_multistream(chan), "Multistream", ""),
		ast_str_tmp(128, ast_format_cap_get_names(value, &STR_TMP)));

	ast_assert(chan != NULL);

	ao2_replace(chan->nativeformats, value);

	/* If chan->stream_topology is NULL, the channel is being destroyed
	 * and topology is destroyed.
	 */
	if (!chan->stream_topology) {
		SCOPE_EXIT_RTN("Channel is being initialized or destroyed\n");
	}

	if (!ast_channel_is_multistream(chan) || !value) {
		struct ast_stream_topology *new_topology;

		new_topology = ast_stream_topology_create_from_format_cap(value);
		ast_channel_internal_set_stream_topology(chan, new_topology);
		SCOPE_EXIT_RTN("New %stopology set\n", value ? "" : "empty ");
	}
	SCOPE_EXIT_RTN("Set native formats but not topology\n");
}

struct ast_framehook_list *ast_channel_framehooks(const struct ast_channel *chan)
{
	return chan->framehooks;
}
void ast_channel_framehooks_set(struct ast_channel *chan, struct ast_framehook_list *value)
{
	chan->framehooks = value;
}
struct ast_generator *ast_channel_generator(const struct ast_channel *chan)
{
	return chan->generator;
}
void ast_channel_generator_set(struct ast_channel *chan, struct ast_generator *value)
{
	chan->generator = value;
}
struct ast_pbx *ast_channel_pbx(const struct ast_channel *chan)
{
	return chan->pbx;
}
void ast_channel_pbx_set(struct ast_channel *chan, struct ast_pbx *value)
{
	chan->pbx = value;
}
struct ast_sched_context *ast_channel_sched(const struct ast_channel *chan)
{
	return chan->sched;
}
void ast_channel_sched_set(struct ast_channel *chan, struct ast_sched_context *value)
{
	chan->sched = value;
}
struct ast_timer *ast_channel_timer(const struct ast_channel *chan)
{
	return chan->timer;
}
void ast_channel_timer_set(struct ast_channel *chan, struct ast_timer *value)
{
	chan->timer = value;
}
struct ast_tone_zone *ast_channel_zone(const struct ast_channel *chan)
{
	return chan->zone;
}
void ast_channel_zone_set(struct ast_channel *chan, struct ast_tone_zone *value)
{
	chan->zone = value;
}
struct ast_trans_pvt *ast_channel_readtrans(const struct ast_channel *chan)
{
	return chan->readtrans;
}
void ast_channel_readtrans_set(struct ast_channel *chan, struct ast_trans_pvt *value)
{
	chan->readtrans = value;
}
struct ast_trans_pvt *ast_channel_writetrans(const struct ast_channel *chan)
{
	return chan->writetrans;
}
void ast_channel_writetrans_set(struct ast_channel *chan, struct ast_trans_pvt *value)
{
	chan->writetrans = value;
}
const struct ast_channel_tech *ast_channel_tech(const struct ast_channel *chan)
{
	return chan->tech;
}
void ast_channel_tech_set(struct ast_channel *chan, const struct ast_channel_tech *value)
{
	if (value->read_stream || value->write_stream) {
		ast_assert(value->read_stream && value->write_stream);
	}

	chan->tech = value;
}
enum ast_channel_adsicpe ast_channel_adsicpe(const struct ast_channel *chan)
{
	return chan->adsicpe;
}
void ast_channel_adsicpe_set(struct ast_channel *chan, enum ast_channel_adsicpe value)
{
	chan->adsicpe = value;
}
enum ast_channel_state ast_channel_state(const struct ast_channel *chan)
{
	return chan->state;
}
ast_callid ast_channel_callid(const struct ast_channel *chan)
{
	return chan->callid;
}
void ast_channel_callid_set(struct ast_channel *chan, ast_callid callid)
{
	char call_identifier_from[AST_CALLID_BUFFER_LENGTH];
	char call_identifier_to[AST_CALLID_BUFFER_LENGTH];
	call_identifier_from[0] = '\0';
	ast_callid_strnprint(call_identifier_to, sizeof(call_identifier_to), callid);
	if (chan->callid) {
		ast_callid_strnprint(call_identifier_from, sizeof(call_identifier_from), chan->callid);
		ast_debug(3, "Channel Call ID changing from %s to %s\n", call_identifier_from, call_identifier_to);
	}

	chan->callid = callid;

	ast_test_suite_event_notify("CallIDChange",
		"State: CallIDChange\r\n"
		"Channel: %s\r\n"
		"CallID: %s\r\n"
		"PriorCallID: %s",
		ast_channel_name(chan),
		call_identifier_to,
		call_identifier_from);
}

void ast_channel_state_set(struct ast_channel *chan, enum ast_channel_state value)
{
	chan->state = value;
}
void ast_channel_set_oldwriteformat(struct ast_channel *chan, struct ast_format *format)
{
	ao2_replace(chan->oldwriteformat, format);
}
void ast_channel_set_rawreadformat(struct ast_channel *chan, struct ast_format *format)
{
	ao2_replace(chan->rawreadformat, format);
}
void ast_channel_set_rawwriteformat(struct ast_channel *chan, struct ast_format *format)
{
	ao2_replace(chan->rawwriteformat, format);
}
void ast_channel_set_readformat(struct ast_channel *chan, struct ast_format *format)
{
	ao2_replace(chan->readformat, format);
}
void ast_channel_set_writeformat(struct ast_channel *chan, struct ast_format *format)
{
	ao2_replace(chan->writeformat, format);
}
struct ast_format *ast_channel_oldwriteformat(struct ast_channel *chan)
{
	return chan->oldwriteformat;
}
struct ast_format *ast_channel_rawreadformat(struct ast_channel *chan)
{
	return chan->rawreadformat;
}
struct ast_format *ast_channel_rawwriteformat(struct ast_channel *chan)
{
	return chan->rawwriteformat;
}
struct ast_format *ast_channel_readformat(struct ast_channel *chan)
{
	return chan->readformat;
}
struct ast_format *ast_channel_writeformat(struct ast_channel *chan)
{
	return chan->writeformat;
}
struct ast_hangup_handler_list *ast_channel_hangup_handlers(struct ast_channel *chan)
{
	return &chan->hangup_handlers;
}
struct ast_datastore_list *ast_channel_datastores(struct ast_channel *chan)
{
	return &chan->datastores;
}
struct ast_autochan_list *ast_channel_autochans(struct ast_channel *chan)
{
	return &chan->autochans;
}
struct ast_readq_list *ast_channel_readq(struct ast_channel *chan)
{
	return &chan->readq;
}
struct ast_frame *ast_channel_dtmff(struct ast_channel *chan)
{
	return &chan->dtmff;
}
struct ast_jb *ast_channel_jb(struct ast_channel *chan)
{
	return &chan->jb;
}
struct ast_party_caller *ast_channel_caller(struct ast_channel *chan)
{
	return &chan->caller;
}
struct ast_party_connected_line *ast_channel_connected(struct ast_channel *chan)
{
	return &chan->connected;
}
struct ast_party_connected_line *ast_channel_connected_indicated(struct ast_channel *chan)
{
	return &chan->connected_indicated;
}
struct ast_party_id ast_channel_connected_effective_id(struct ast_channel *chan)
{
	return ast_party_id_merge(&chan->connected.id, &chan->connected.priv);
}
struct ast_party_dialed *ast_channel_dialed(struct ast_channel *chan)
{
	return &chan->dialed;
}
struct ast_party_redirecting *ast_channel_redirecting(struct ast_channel *chan)
{
	return &chan->redirecting;
}
struct ast_party_id ast_channel_redirecting_effective_orig(struct ast_channel *chan)
{
	return ast_party_id_merge(&chan->redirecting.orig, &chan->redirecting.priv_orig);
}
struct ast_party_id ast_channel_redirecting_effective_from(struct ast_channel *chan)
{
	return ast_party_id_merge(&chan->redirecting.from, &chan->redirecting.priv_from);
}
struct ast_party_id ast_channel_redirecting_effective_to(struct ast_channel *chan)
{
	return ast_party_id_merge(&chan->redirecting.to, &chan->redirecting.priv_to);
}
struct timeval *ast_channel_dtmf_tv(struct ast_channel *chan)
{
	return &chan->dtmf_tv;
}
struct timeval *ast_channel_whentohangup(struct ast_channel *chan)
{
	return &chan->whentohangup;
}
struct varshead *ast_channel_varshead(struct ast_channel *chan)
{
	return &chan->varshead;
}
void ast_channel_dtmff_set(struct ast_channel *chan, struct ast_frame *value)
{
	chan->dtmff = *value;
}
void ast_channel_jb_set(struct ast_channel *chan, struct ast_jb *value)
{
	chan->jb = *value;
}
void ast_channel_caller_set(struct ast_channel *chan, struct ast_party_caller *value)
{
	chan->caller = *value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_CALLER);
}
void ast_channel_connected_set(struct ast_channel *chan, struct ast_party_connected_line *value)
{
	chan->connected = *value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_CONNECTED);
}
void ast_channel_dialed_set(struct ast_channel *chan, struct ast_party_dialed *value)
{
	chan->dialed = *value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_CALLER);
}
void ast_channel_redirecting_set(struct ast_channel *chan, struct ast_party_redirecting *value)
{
	chan->redirecting = *value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_CALLER);
}
void ast_channel_dtmf_tv_set(struct ast_channel *chan, struct timeval *value)
{
	chan->dtmf_tv = *value;
}
void ast_channel_whentohangup_set(struct ast_channel *chan, struct timeval *value)
{
	chan->whentohangup = *value;
}
void ast_channel_varshead_set(struct ast_channel *chan, struct varshead *value)
{
	chan->varshead = *value;
}
struct timeval ast_channel_creationtime(struct ast_channel *chan)
{
	return chan->creationtime;
}
void ast_channel_creationtime_set(struct ast_channel *chan, struct timeval *value)
{
	chan->creationtime = *value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_BASE);
}

struct timeval ast_channel_answertime(struct ast_channel *chan)
{
	return chan->answertime;
}

void ast_channel_answertime_set(struct ast_channel *chan, struct timeval *value)
{
	chan->answertime = *value;
}

/* Evil softhangup accessors */
int ast_channel_softhangup_internal_flag(struct ast_channel *chan)
{
	return chan->softhangup;
}
void ast_channel_softhangup_internal_flag_set(struct ast_channel *chan, int value)
{
	chan->softhangup = value;
}
void ast_channel_softhangup_internal_flag_add(struct ast_channel *chan, int value)
{
	chan->softhangup |= value;
}
void ast_channel_softhangup_internal_flag_clear(struct ast_channel *chan, int value)
{
	chan ->softhangup &= ~value;
}

int ast_channel_unbridged_nolock(struct ast_channel *chan)
{
	return chan->unbridged;
}

int ast_channel_unbridged(struct ast_channel *chan)
{
	int res;
	ast_channel_lock(chan);
	res = ast_channel_unbridged_nolock(chan);
	ast_channel_unlock(chan);
	return res;
}

void ast_channel_set_unbridged_nolock(struct ast_channel *chan, int value)
{
	chan->unbridged = !!value;
	ast_queue_frame(chan, &ast_null_frame);
}

void ast_channel_set_unbridged(struct ast_channel *chan, int value)
{
	ast_channel_lock(chan);
	ast_channel_set_unbridged_nolock(chan, value);
	ast_channel_unlock(chan);
}

int ast_channel_is_t38_active_nolock(struct ast_channel *chan)
{
	return chan->is_t38_active;
}

int ast_channel_is_t38_active(struct ast_channel *chan)
{
	int res;

	ast_channel_lock(chan);
	res = ast_channel_is_t38_active_nolock(chan);
	ast_channel_unlock(chan);
	return res;
}

void ast_channel_set_is_t38_active_nolock(struct ast_channel *chan, int is_t38_active)
{
	chan->is_t38_active = !!is_t38_active;
}

void ast_channel_set_is_t38_active(struct ast_channel *chan, int is_t38_active)
{
	ast_channel_lock(chan);
	ast_channel_set_is_t38_active_nolock(chan, is_t38_active);
	ast_channel_unlock(chan);
}

void ast_channel_callid_cleanup(struct ast_channel *chan)
{
	chan->callid = 0;
}

/* Typedef accessors */
ast_group_t ast_channel_callgroup(const struct ast_channel *chan)
{
	return chan->callgroup;
}
void ast_channel_callgroup_set(struct ast_channel *chan, ast_group_t value)
{
	chan->callgroup = value;
}
ast_group_t ast_channel_pickupgroup(const struct ast_channel *chan)
{
	return chan->pickupgroup;
}
void ast_channel_pickupgroup_set(struct ast_channel *chan, ast_group_t value)
{
	chan->pickupgroup = value;
}
struct ast_namedgroups *ast_channel_named_callgroups(const struct ast_channel *chan)
{
	return chan->named_callgroups;
}
void ast_channel_named_callgroups_set(struct ast_channel *chan, struct ast_namedgroups *value)
{
	ast_unref_namedgroups(chan->named_callgroups);
	chan->named_callgroups = ast_ref_namedgroups(value);
}
struct ast_namedgroups *ast_channel_named_pickupgroups(const struct ast_channel *chan)
{
	return chan->named_pickupgroups;
}
void ast_channel_named_pickupgroups_set(struct ast_channel *chan, struct ast_namedgroups *value)
{
	ast_unref_namedgroups(chan->named_pickupgroups);
	chan->named_pickupgroups = ast_ref_namedgroups(value);
}

/* Alertpipe functions */
int ast_channel_alert_write(struct ast_channel *chan)
{
	return ast_alertpipe_write(chan->alertpipe);
}

ast_alert_status_t ast_channel_internal_alert_flush(struct ast_channel *chan)
{
	return ast_alertpipe_flush(chan->alertpipe);
}

ast_alert_status_t ast_channel_internal_alert_read(struct ast_channel *chan)
{
	return ast_alertpipe_read(chan->alertpipe);
}

int ast_channel_alert_writable(struct ast_channel *chan)
{
	return ast_alertpipe_writable(chan->alertpipe);
}

int ast_channel_internal_alert_readable(struct ast_channel *chan)
{
	return ast_alertpipe_readable(chan->alertpipe);
}

void ast_channel_internal_alertpipe_clear(struct ast_channel *chan)
{
	ast_alertpipe_clear(chan->alertpipe);
}

void ast_channel_internal_alertpipe_close(struct ast_channel *chan)
{
	ast_alertpipe_close(chan->alertpipe);
}

int ast_channel_internal_alertpipe_init(struct ast_channel *chan)
{
	return ast_alertpipe_init(chan->alertpipe);
}

int ast_channel_internal_alert_readfd(struct ast_channel *chan)
{
	return ast_alertpipe_readfd(chan->alertpipe);
}

void ast_channel_internal_alertpipe_swap(struct ast_channel *chan1, struct ast_channel *chan2)
{
	ast_alertpipe_swap(chan1->alertpipe, chan2->alertpipe);
}

/* file descriptor array accessors */
void ast_channel_internal_fd_set(struct ast_channel *chan, int which, int value)
{
	int pos;

	/* This ensures that if the vector has to grow with unused positions they will be
	 * initialized to -1.
	 */
	for (pos = AST_VECTOR_SIZE(&chan->fds); pos < which; pos++) {
		AST_VECTOR_REPLACE(&chan->fds, pos, -1);
	}

	AST_VECTOR_REPLACE(&chan->fds, which, value);
}
void ast_channel_internal_fd_clear(struct ast_channel *chan, int which)
{
	if (which >= AST_VECTOR_SIZE(&chan->fds)) {
		return;
	}

	AST_VECTOR_REPLACE(&chan->fds, which, -1);
}
void ast_channel_internal_fd_clear_all(struct ast_channel *chan)
{
	AST_VECTOR_RESET(&chan->fds, AST_VECTOR_ELEM_CLEANUP_NOOP);
}
int ast_channel_fd(const struct ast_channel *chan, int which)
{
	return (which >= AST_VECTOR_SIZE(&chan->fds)) ? -1 : AST_VECTOR_GET(&chan->fds, which);
}
int ast_channel_fd_isset(const struct ast_channel *chan, int which)
{
	return ast_channel_fd(chan, which) > -1;
}

int ast_channel_fd_count(const struct ast_channel *chan)
{
	return AST_VECTOR_SIZE(&chan->fds);
}

int ast_channel_fd_add(struct ast_channel *chan, int value)
{
	int pos = AST_EXTENDED_FDS;

	while (ast_channel_fd_isset(chan, pos)) {
		pos += 1;
	}

	AST_VECTOR_REPLACE(&chan->fds, pos, value);

	return pos;
}

pthread_t ast_channel_blocker(const struct ast_channel *chan)
{
	return chan->blocker;
}
void ast_channel_blocker_set(struct ast_channel *chan, pthread_t value)
{
	chan->blocker = value;
}

int ast_channel_blocker_tid(const struct ast_channel *chan)
{
	return chan->blocker_tid;
}
void ast_channel_blocker_tid_set(struct ast_channel *chan, int value)
{
	chan->blocker_tid = value;
}

ast_timing_func_t ast_channel_timingfunc(const struct ast_channel *chan)
{
	return chan->timingfunc;
}
void ast_channel_timingfunc_set(struct ast_channel *chan, ast_timing_func_t value)
{
	chan->timingfunc = value;
}

struct ast_bridge *ast_channel_internal_bridge(const struct ast_channel *chan)
{
	return chan->bridge;
}
void ast_channel_internal_bridge_set(struct ast_channel *chan, struct ast_bridge *value)
{
	chan->bridge = value;
	ast_channel_snapshot_invalidate_segment(chan, AST_CHANNEL_SNAPSHOT_INVALIDATE_BRIDGE);
	ast_channel_publish_snapshot(chan);
}

struct ast_bridge_channel *ast_channel_internal_bridge_channel(const struct ast_channel *chan)
{
	return chan->bridge_channel;
}
void ast_channel_internal_bridge_channel_set(struct ast_channel *chan, struct ast_bridge_channel *value)
{
	chan->bridge_channel = value;
}

struct ast_flags *ast_channel_flags(struct ast_channel *chan)
{
	return &chan->flags;
}

static int collect_names_cb(void *obj, void *arg, int flags)
{
	struct ast_control_pvt_cause_code *cause_code = obj;
	struct ast_str **str = arg;

	ast_str_append(str, 0, "%s%s", (ast_str_strlen(*str) ? "," : ""), cause_code->chan_name);

	return 0;
}

struct ast_str *ast_channel_dialed_causes_channels(const struct ast_channel *chan)
{
	struct ast_str *chanlist = ast_str_create(128);

	if (!chanlist) {
		return NULL;
	}

	ao2_callback(chan->dialed_causes, 0, collect_names_cb, &chanlist);

	return chanlist;
}

struct ast_control_pvt_cause_code *ast_channel_dialed_causes_find(const struct ast_channel *chan, const char *chan_name)
{
	struct ao2_iterator causes;
	struct ast_control_pvt_cause_code *cause_code;

	causes = ao2_iterator_init(chan->dialed_causes, 0);
	while ((cause_code = ao2_iterator_next(&causes))) {
		if (strcmp(cause_code->chan_name, chan_name)) {
			ao2_ref(cause_code, -1);
			continue;
		}
		if (!cause_code->cause_extended) {
			ao2_iterator_destroy(&causes);
			return cause_code;
		}
		ao2_ref(cause_code, -1);
	}
	ao2_iterator_destroy(&causes);

	return NULL;
}

struct ao2_iterator *ast_channel_dialed_causes_find_multiple(const struct ast_channel *chan, const char *chan_name)
{
	struct ao2_iterator *causes;
	struct ast_control_pvt_cause_code *cause_code;

	causes = ao2_find(chan->dialed_causes, chan_name, OBJ_SEARCH_KEY | OBJ_MULTIPLE);
	while ((cause_code = ao2_iterator_next(causes))) {
		ao2_ref(cause_code, -1);
	}
	ao2_iterator_destroy(causes);

	return ao2_find(chan->dialed_causes, chan_name, OBJ_SEARCH_KEY | OBJ_MULTIPLE);
}

static int remove_dialstatus_cb(void *obj, void *arg, int flags)
{
	struct ast_control_pvt_cause_code *cause_code = obj;
	char *str = ast_tech_to_upper(ast_strdupa(arg));
	char *pc_str = ast_tech_to_upper(ast_strdupa(cause_code->chan_name));

	if (cause_code->cause_extended) {
		return 0;
	}
	return !strcmp(pc_str, str) ? CMP_MATCH | CMP_STOP : 0;
}

int ast_channel_dialed_causes_add(const struct ast_channel *chan, const struct ast_control_pvt_cause_code *cause_code, int datalen)
{
	struct ast_control_pvt_cause_code *ao2_cause_code;
	char *arg = ast_strdupa(cause_code->chan_name);

	ao2_callback(chan->dialed_causes, OBJ_MULTIPLE | OBJ_NODATA | OBJ_UNLINK, remove_dialstatus_cb, arg);

	ao2_cause_code = ao2_alloc(datalen, NULL);
	if (ao2_cause_code) {
		memcpy(ao2_cause_code, cause_code, datalen);
		ao2_link(chan->dialed_causes, ao2_cause_code);
		ao2_ref(ao2_cause_code, -1);
		return 0;
	}

	return -1;
}

void ast_channel_dialed_causes_clear(const struct ast_channel *chan)
{
	ao2_callback(chan->dialed_causes, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
}

/*! \brief Hash function for pvt cause code frames */
static int pvt_cause_hash_fn(const void *vpc, const int flags)
{
	const struct ast_control_pvt_cause_code *pc = vpc;
	return ast_str_hash(ast_tech_to_upper(ast_strdupa(pc->chan_name)));
}

/*! \brief Comparison function for pvt cause code frames */
static int pvt_cause_cmp_fn(void *obj, void *vstr, int flags)
{
	struct ast_control_pvt_cause_code *pc = obj;
	char *str = ast_tech_to_upper(ast_strdupa(vstr));
	char *pc_str = ast_tech_to_upper(ast_strdupa(pc->chan_name));
	return !strcmp(pc_str, str) ? CMP_MATCH | CMP_STOP : 0;
}

#define DIALED_CAUSES_BUCKETS 37

struct ast_channel *__ast_channel_internal_alloc_with_initializers(void (*destructor)(void *obj), const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, const struct ast_channel_initializers *initializers, const char *file, int line, const char *function)
{
	struct ast_channel *tmp;

	tmp = __ao2_alloc(sizeof(*tmp), destructor,
		AO2_ALLOC_OPT_LOCK_MUTEX, "", file, line, function);

	if (!tmp) {
		return NULL;
	}

	if ((ast_string_field_init(tmp, 128))) {
		return ast_channel_unref(tmp);
	}

	tmp->dialed_causes = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		DIALED_CAUSES_BUCKETS, pvt_cause_hash_fn, NULL, pvt_cause_cmp_fn);
	if (!tmp->dialed_causes) {
		return ast_channel_unref(tmp);
	}

	/* Check initializers validity here for early abort. Unfortunately, we can't do much here because
	 * tenant ID is part of linked ID, which would overwrite it further down. */
	if (initializers) {
		if (initializers->version == 0) {
			ast_log(LOG_ERROR, "Channel initializers must have a non-zero version.\n");
			return ast_channel_unref(tmp);
		} else if (initializers->version != AST_CHANNEL_INITIALIZERS_VERSION) {
			ast_log(LOG_ERROR, "ABI mismatch for ast_channel_initializers. "
				"Please ensure all modules were compiled for "
				"this version of Asterisk.\n");
			return ast_channel_unref(tmp);
		}
	}

	/* set the creation time in the uniqueid */
	tmp->uniqueid.creation_time = time(NULL);
	tmp->uniqueid.creation_unique = ast_atomic_fetchadd_int(&uniqueint, 1);

	/* use provided id or default to historical {system-}time.# format */
	if (assignedids && !ast_strlen_zero(assignedids->uniqueid)) {
		ast_copy_string(tmp->uniqueid.unique_id, assignedids->uniqueid, sizeof(tmp->uniqueid.unique_id));
	} else if (ast_strlen_zero(ast_config_AST_SYSTEM_NAME)) {
		snprintf(tmp->uniqueid.unique_id, sizeof(tmp->uniqueid.unique_id), "%li.%d",
			(long)(tmp->uniqueid.creation_time),
			tmp->uniqueid.creation_unique);
	} else {
		snprintf(tmp->uniqueid.unique_id, sizeof(tmp->uniqueid.unique_id), "%s-%li.%d",
			ast_config_AST_SYSTEM_NAME,
			(long)(tmp->uniqueid.creation_time),
			tmp->uniqueid.creation_unique);
	}

	/* copy linked id from parent channel if known */
	if (requestor) {
		tmp->linkedid = requestor->linkedid;
	} else {
		tmp->linkedid = tmp->uniqueid;
	}

	/* Things like tenant ID need to be set here, otherwise they would be overwritten by
	 * things like inheriting linked ID above. */
	if (initializers) {
		ast_copy_string(tmp->linkedid.tenant_id, initializers->tenantid, sizeof(tmp->linkedid.tenant_id));
	}

	AST_VECTOR_INIT(&tmp->fds, AST_MAX_FDS);

	/* Force all channel snapshot segments to be created on first use, so we don't have to check if
	 * an old snapshot exists.
	 */
	ast_set_flag(&tmp->snapshot_segment_flags, AST_FLAGS_ALL);

	return tmp;
}

struct ast_channel *__ast_channel_internal_alloc(void (*destructor)(void *obj), const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, const char *file, int line, const char *function)
{
	return __ast_channel_internal_alloc_with_initializers(destructor, assignedids, requestor, NULL, file, line, function);
}

struct ast_channel *ast_channel_internal_oldest_linkedid(struct ast_channel *a, struct ast_channel *b)
{
	ast_assert(a->linkedid.creation_time != 0);
	ast_assert(b->linkedid.creation_time != 0);

	if (a->linkedid.creation_time < b->linkedid.creation_time) {
		return a;
	}
	if (b->linkedid.creation_time < a->linkedid.creation_time) {
		return b;
	}
	if (a->linkedid.creation_unique < b->linkedid.creation_unique) {
		return a;
	}
	return b;
}

void ast_channel_internal_copy_linkedid(struct ast_channel *dest, struct ast_channel *source)
{
	if (dest->linkedid.creation_time == source->linkedid.creation_time
		&& dest->linkedid.creation_unique == source->linkedid.creation_unique
		&& !strcmp(dest->linkedid.unique_id, source->linkedid.unique_id)) {
		return;
	}
	dest->linkedid = source->linkedid;
	ast_channel_snapshot_invalidate_segment(dest, AST_CHANNEL_SNAPSHOT_INVALIDATE_PEER);
	ast_channel_publish_snapshot(dest);
}

void ast_channel_internal_swap_uniqueid_and_linkedid(struct ast_channel *a, struct ast_channel *b)
{
	struct ast_channel_id temp;

	/* This operation is used as part of masquerading and so does not invalidate the peer
	 * segment. This is due to the masquerade process invalidating all segments.
	 */

	/*
	 * Since unique ids can be a key in the channel storage backend,
	 * ensure that neither channel is linked in or the keys will be
	 * invalid.
	 */
	ast_assert(!a->linked_in_container && !b->linked_in_container);

	temp = a->uniqueid;
	a->uniqueid = b->uniqueid;
	b->uniqueid = temp;

	temp = a->linkedid;
	a->linkedid = b->linkedid;
	b->linkedid = temp;
}

void ast_channel_internal_swap_topics(struct ast_channel *a, struct ast_channel *b)
{
	struct stasis_topic *topic;
	struct stasis_forward *forward;

	topic = a->topic;
	a->topic = b->topic;
	b->topic = topic;

	forward = a->channel_forward;
	a->channel_forward = b->channel_forward;
	b->channel_forward = forward;
}

void ast_channel_internal_swap_endpoint_forward(struct ast_channel *a, struct ast_channel *b)
{
	struct stasis_forward *temp;

	temp = a->endpoint_forward;
	a->endpoint_forward = b->endpoint_forward;
	b->endpoint_forward = temp;
}

void ast_channel_internal_swap_snapshots(struct ast_channel *a, struct ast_channel *b)
{
	struct ast_channel_snapshot *snapshot;

	snapshot = a->snapshot;
	a->snapshot = b->snapshot;
	b->snapshot = snapshot;
}

void ast_channel_internal_set_fake_ids(struct ast_channel *chan, const char *uniqueid, const char *linkedid)
{
	ast_copy_string(chan->uniqueid.unique_id, uniqueid, sizeof(chan->uniqueid.unique_id));
	ast_copy_string(chan->linkedid.unique_id, linkedid, sizeof(chan->linkedid.unique_id));
}

void ast_channel_internal_cleanup(struct ast_channel *chan)
{
	if (chan->dialed_causes) {
		ao2_t_ref(chan->dialed_causes, -1,
			"done with dialed causes since the channel is going away");
		chan->dialed_causes = NULL;
	}

	ast_string_field_free_memory(chan);

	chan->channel_forward = stasis_forward_cancel(chan->channel_forward);
	chan->endpoint_forward = stasis_forward_cancel(chan->endpoint_forward);

	ao2_cleanup(chan->topic);
	chan->topic = NULL;

	ast_channel_internal_set_stream_topology(chan, NULL);

	AST_VECTOR_FREE(&chan->fds);
}

void ast_channel_internal_finalize(struct ast_channel *chan)
{
	chan->finalized = 1;
}

int ast_channel_internal_is_finalized(struct ast_channel *chan)
{
	return chan->finalized;
}

struct stasis_topic *ast_channel_topic(struct ast_channel *chan)
{
	if (!chan) {
		return ast_channel_topic_all();
	}

	return chan->topic;
}

int ast_channel_forward_endpoint(struct ast_channel *chan,
	struct ast_endpoint *endpoint)
{
	ast_assert(chan != NULL);
	ast_assert(endpoint != NULL);

	chan->endpoint_forward =
		stasis_forward_all(ast_channel_topic(chan),
			ast_endpoint_topic(endpoint));
	if (!chan->endpoint_forward) {
		return -1;
	}

	return 0;
}

int ast_channel_internal_setup_topics(struct ast_channel *chan)
{
	char *topic_name;
	int ret;
	ast_assert(chan->topic == NULL);

	if (ast_strlen_zero(chan->uniqueid.unique_id)) {
		static int dummy_id;
		ret = ast_asprintf(&topic_name, "channel:dummy-%d", ast_atomic_fetchadd_int(&dummy_id, +1));
	} else {
		ret = ast_asprintf(&topic_name, "channel:%s", chan->uniqueid.unique_id);
	}

	if (ret < 0) {
		return -1;
	}

	chan->topic = stasis_topic_create(topic_name);
	ast_free(topic_name);
	if (!chan->topic) {
		return -1;
	}

	chan->channel_forward = stasis_forward_all(ast_channel_topic(chan),
		ast_channel_topic_all());
	if (!chan->channel_forward) {
		ao2_ref(chan->topic, -1);
		chan->topic = NULL;
		return -1;
	}

	return 0;
}

AST_THREADSTORAGE(channel_errno);

void ast_channel_internal_errno_set(enum ast_channel_error error)
{
	enum ast_channel_error *error_code = ast_threadstorage_get(&channel_errno, sizeof(*error_code));
	if (!error_code) {
		return;
	}

	*error_code = error;
}

enum ast_channel_error ast_channel_internal_errno(void)
{
	enum ast_channel_error *error_code = ast_threadstorage_get(&channel_errno, sizeof(*error_code));
	if (!error_code) {
		return AST_CHANNEL_ERROR_UNKNOWN;
	}

	return *error_code;
}

struct ast_stream_topology *ast_channel_get_stream_topology(
	const struct ast_channel *chan)
{
	ast_assert(chan != NULL);

	return chan->stream_topology;
}

struct ast_stream_topology *ast_channel_set_stream_topology(struct ast_channel *chan,
	struct ast_stream_topology *topology)
{
	struct ast_stream_topology *new_topology;
	SCOPE_ENTER(1, "%s: %s\n", ast_channel_name(chan),
		ast_str_tmp(256, ast_stream_topology_to_str(topology, &STR_TMP)));

	ast_assert(chan != NULL);

	/* A non-MULTISTREAM channel can't manipulate topology directly */
	ast_assert(ast_channel_is_multistream(chan));

	/* Unless the channel is being destroyed, we always want a topology on
	 * it even if its empty.
	 */
	if (!topology) {
		new_topology = ast_stream_topology_alloc();
	} else {
		new_topology = topology;
	}

	if (new_topology) {
		ast_channel_internal_set_stream_topology(chan, new_topology);
	}

	SCOPE_EXIT_RTN_VALUE(new_topology, "Used %s topology\n", topology ? "provided" : "empty");
}

struct ast_stream *ast_channel_get_default_stream(struct ast_channel *chan,
	enum ast_media_type type)
{
	ast_assert(chan != NULL);
	ast_assert(type < AST_MEDIA_TYPE_END);

	return chan->default_streams[type];
}

void ast_channel_internal_swap_stream_topology(struct ast_channel *chan1,
	struct ast_channel *chan2)
{
	struct ast_stream_topology *tmp_topology;

	ast_assert(chan1 != NULL && chan2 != NULL);

	tmp_topology = chan1->stream_topology;
	chan1->stream_topology = chan2->stream_topology;
	chan2->stream_topology = tmp_topology;

	channel_set_default_streams(chan1);
	channel_set_default_streams(chan2);
}

int ast_channel_is_multistream(struct ast_channel *chan)
{
	return (chan && chan->tech && chan->tech->read_stream && chan->tech->write_stream);
}

struct ast_channel_snapshot *ast_channel_snapshot(const struct ast_channel *chan)
{
	return chan->snapshot;
}

void ast_channel_snapshot_set(struct ast_channel *chan, struct ast_channel_snapshot *snapshot)
{
	ao2_cleanup(chan->snapshot);
	chan->snapshot = ao2_bump(snapshot);
}

struct ast_flags *ast_channel_snapshot_segment_flags(struct ast_channel *chan)
{
	return &chan->snapshot_segment_flags;
}
