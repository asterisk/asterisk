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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/stringfields.h"
#include "asterisk/data.h"
#include "asterisk/indications.h"

/* AST_DATA definitions, which will probably have to be re-thought since the channel will be opaque */

#if 0	/* XXX AstData: ast_callerid no longer exists. (Equivalent code not readily apparent.) */
#define DATA_EXPORT_CALLERID(MEMBER)				\
	MEMBER(ast_callerid, cid_dnid, AST_DATA_STRING)		\
	MEMBER(ast_callerid, cid_num, AST_DATA_STRING)		\
	MEMBER(ast_callerid, cid_name, AST_DATA_STRING)		\
	MEMBER(ast_callerid, cid_ani, AST_DATA_STRING)		\
	MEMBER(ast_callerid, cid_pres, AST_DATA_INTEGER)	\
	MEMBER(ast_callerid, cid_ani2, AST_DATA_INTEGER)	\
	MEMBER(ast_callerid, cid_tag, AST_DATA_STRING)

AST_DATA_STRUCTURE(ast_callerid, DATA_EXPORT_CALLERID);
#endif

#define DATA_EXPORT_CHANNEL(MEMBER)						\
	MEMBER(ast_channel, __do_not_use_blockproc, AST_DATA_STRING)				\
	MEMBER(ast_channel, __do_not_use_appl, AST_DATA_STRING)				\
	MEMBER(ast_channel, __do_not_use_data, AST_DATA_STRING)				\
	MEMBER(ast_channel, __do_not_use_name, AST_DATA_STRING) \
	MEMBER(ast_channel, __do_not_use_language, AST_DATA_STRING)				\
	MEMBER(ast_channel, __do_not_use_musicclass, AST_DATA_STRING)			\
	MEMBER(ast_channel, __do_not_use_accountcode, AST_DATA_STRING)			\
	MEMBER(ast_channel, __do_not_use_peeraccount, AST_DATA_STRING)			\
	MEMBER(ast_channel, __do_not_use_userfield, AST_DATA_STRING)				\
	MEMBER(ast_channel, __do_not_use_call_forward, AST_DATA_STRING)			\
	MEMBER(ast_channel, __do_not_use_uniqueid, AST_DATA_STRING)				\
	MEMBER(ast_channel, __do_not_use_linkedid, AST_DATA_STRING)				\
	MEMBER(ast_channel, __do_not_use_parkinglot, AST_DATA_STRING)			\
	MEMBER(ast_channel, __do_not_use_hangupsource, AST_DATA_STRING)			\
	MEMBER(ast_channel, __do_not_use_dialcontext, AST_DATA_STRING)			\
	MEMBER(ast_channel, rings, AST_DATA_INTEGER)				\
	MEMBER(ast_channel, priority, AST_DATA_INTEGER)				\
	MEMBER(ast_channel, macropriority, AST_DATA_INTEGER)			\
	MEMBER(ast_channel, adsicpe, AST_DATA_INTEGER)				\
	MEMBER(ast_channel, fin, AST_DATA_UNSIGNED_INTEGER)			\
	MEMBER(ast_channel, fout, AST_DATA_UNSIGNED_INTEGER)			\
	MEMBER(ast_channel, emulate_dtmf_duration, AST_DATA_UNSIGNED_INTEGER)	\
	MEMBER(ast_channel, visible_indication, AST_DATA_INTEGER)		\
	MEMBER(ast_channel, __do_not_use_context, AST_DATA_STRING)				\
	MEMBER(ast_channel, __do_not_use_exten, AST_DATA_STRING)				\
	MEMBER(ast_channel, __do_not_use_macrocontext, AST_DATA_STRING)			\
	MEMBER(ast_channel, __do_not_use_macroexten, AST_DATA_STRING)

AST_DATA_STRUCTURE(ast_channel, DATA_EXPORT_CHANNEL);

static void channel_data_add_flags(struct ast_data *tree,
	struct ast_channel *chan)
{
	ast_data_add_bool(tree, "DEFER_DTMF", ast_test_flag(chan, AST_FLAG_DEFER_DTMF));
	ast_data_add_bool(tree, "WRITE_INT", ast_test_flag(chan, AST_FLAG_WRITE_INT));
	ast_data_add_bool(tree, "BLOCKING", ast_test_flag(chan, AST_FLAG_BLOCKING));
	ast_data_add_bool(tree, "ZOMBIE", ast_test_flag(chan, AST_FLAG_ZOMBIE));
	ast_data_add_bool(tree, "EXCEPTION", ast_test_flag(chan, AST_FLAG_EXCEPTION));
	ast_data_add_bool(tree, "MOH", ast_test_flag(chan, AST_FLAG_MOH));
	ast_data_add_bool(tree, "SPYING", ast_test_flag(chan, AST_FLAG_SPYING));
	ast_data_add_bool(tree, "NBRIDGE", ast_test_flag(chan, AST_FLAG_NBRIDGE));
	ast_data_add_bool(tree, "IN_AUTOLOOP", ast_test_flag(chan, AST_FLAG_IN_AUTOLOOP));
	ast_data_add_bool(tree, "OUTGOING", ast_test_flag(chan, AST_FLAG_OUTGOING));
	ast_data_add_bool(tree, "IN_DTMF", ast_test_flag(chan, AST_FLAG_IN_DTMF));
	ast_data_add_bool(tree, "EMULATE_DTMF", ast_test_flag(chan, AST_FLAG_EMULATE_DTMF));
	ast_data_add_bool(tree, "END_DTMF_ONLY", ast_test_flag(chan, AST_FLAG_END_DTMF_ONLY));
	ast_data_add_bool(tree, "ANSWERED_ELSEWHERE", ast_test_flag(chan, AST_FLAG_ANSWERED_ELSEWHERE));
	ast_data_add_bool(tree, "MASQ_NOSTREAM", ast_test_flag(chan, AST_FLAG_MASQ_NOSTREAM));
	ast_data_add_bool(tree, "BRIDGE_HANGUP_RUN", ast_test_flag(chan, AST_FLAG_BRIDGE_HANGUP_RUN));
	ast_data_add_bool(tree, "BRIDGE_HANGUP_DONT", ast_test_flag(chan, AST_FLAG_BRIDGE_HANGUP_DONT));
	ast_data_add_bool(tree, "DISABLE_WORKAROUNDS", ast_test_flag(chan, AST_FLAG_DISABLE_WORKAROUNDS));
}

int ast_channel_data_add_structure(struct ast_data *tree,
	struct ast_channel *chan, int add_bridged)
{
	struct ast_channel *bc;
	struct ast_data *data_bridged;
	struct ast_data *data_cdr;
	struct ast_data *data_flags;
	struct ast_data *data_zones;
	struct ast_data *enum_node;
	struct ast_data *data_softhangup;
#if 0	/* XXX AstData: ast_callerid no longer exists. (Equivalent code not readily apparent.) */
	struct ast_data *data_callerid;
	char value_str[100];
#endif

	if (!tree) {
		return -1;
	}

	ast_data_add_structure(ast_channel, tree, chan);

	if (add_bridged) {
		bc = ast_bridged_channel(chan);
		if (bc) {
			data_bridged = ast_data_add_node(tree, "bridged");
			if (!data_bridged) {
				return -1;
			}
			ast_channel_data_add_structure(data_bridged, bc, 0);
		}
	}

	ast_data_add_codec(tree, "oldwriteformat", &chan->oldwriteformat);
	ast_data_add_codec(tree, "readformat", &chan->readformat);
	ast_data_add_codec(tree, "writeformat", &chan->writeformat);
	ast_data_add_codec(tree, "rawreadformat", &chan->rawreadformat);
	ast_data_add_codec(tree, "rawwriteformat", &chan->rawwriteformat);
	ast_data_add_codecs(tree, "nativeformats", chan->nativeformats);

	/* state */
	enum_node = ast_data_add_node(tree, "state");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_state2str(chan->_state));
	ast_data_add_int(enum_node, "value", chan->_state);

	/* hangupcause */
	enum_node = ast_data_add_node(tree, "hangupcause");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_cause2str(chan->hangupcause));
	ast_data_add_int(enum_node, "value", chan->hangupcause);

	/* amaflags */
	enum_node = ast_data_add_node(tree, "amaflags");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_cdr_flags2str(chan->amaflags));
	ast_data_add_int(enum_node, "value", chan->amaflags);

	/* transfercapability */
	enum_node = ast_data_add_node(tree, "transfercapability");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_str(enum_node, "text", ast_transfercapability2str(chan->transfercapability));
	ast_data_add_int(enum_node, "value", chan->transfercapability);

	/* _softphangup */
	data_softhangup = ast_data_add_node(tree, "softhangup");
	if (!data_softhangup) {
		return -1;
	}
	ast_data_add_bool(data_softhangup, "dev", chan->_softhangup & AST_SOFTHANGUP_DEV);
	ast_data_add_bool(data_softhangup, "asyncgoto", chan->_softhangup & AST_SOFTHANGUP_ASYNCGOTO);
	ast_data_add_bool(data_softhangup, "shutdown", chan->_softhangup & AST_SOFTHANGUP_SHUTDOWN);
	ast_data_add_bool(data_softhangup, "timeout", chan->_softhangup & AST_SOFTHANGUP_TIMEOUT);
	ast_data_add_bool(data_softhangup, "appunload", chan->_softhangup & AST_SOFTHANGUP_APPUNLOAD);
	ast_data_add_bool(data_softhangup, "explicit", chan->_softhangup & AST_SOFTHANGUP_EXPLICIT);
	ast_data_add_bool(data_softhangup, "unbridge", chan->_softhangup & AST_SOFTHANGUP_UNBRIDGE);

	/* channel flags */
	data_flags = ast_data_add_node(tree, "flags");
	if (!data_flags) {
		return -1;
	}
	channel_data_add_flags(data_flags, chan);

	ast_data_add_uint(tree, "timetohangup", chan->whentohangup.tv_sec);

#if 0	/* XXX AstData: ast_callerid no longer exists. (Equivalent code not readily apparent.) */
	/* callerid */
	data_callerid = ast_data_add_node(tree, "callerid");
	if (!data_callerid) {
		return -1;
	}
	ast_data_add_structure(ast_callerid, data_callerid, &(chan->cid));
	/* insert the callerid ton */
	enum_node = ast_data_add_node(data_callerid, "cid_ton");
	if (!enum_node) {
		return -1;
	}
	ast_data_add_int(enum_node, "value", chan->cid.cid_ton);
	snprintf(value_str, sizeof(value_str), "TON: %s/Plan: %s",
		party_number_ton2str(chan->cid.cid_ton),
		party_number_plan2str(chan->cid.cid_ton));
	ast_data_add_str(enum_node, "text", value_str);
#endif

	/* tone zone */
	if (chan->zone) {
		data_zones = ast_data_add_node(tree, "zone");
		if (!data_zones) {
			return -1;
		}
		ast_tone_zone_data_add_structure(data_zones, chan->zone);
	}

	/* insert cdr */
	data_cdr = ast_data_add_node(tree, "cdr");
	if (!data_cdr) {
		return -1;
	}

	ast_cdr_data_add_structure(data_cdr, chan->cdr, 1);

	return 0;
}

int ast_channel_data_cmp_structure(const struct ast_data_search *tree,
	struct ast_channel *chan, const char *structure_name)
{
	return ast_data_search_cmp_structure(tree, ast_channel, chan, structure_name);
}

/* ACCESSORS */

#define DEFINE_STRINGFIELD_SETTERS_FOR(field) \
void ast_channel_##field##_set(struct ast_channel *chan, const char *value) \
{ \
	ast_string_field_set(chan, __do_not_use_##field, value); \
} \
  \
void ast_channel_##field##_build_va(struct ast_channel *chan, const char *fmt, va_list ap) \
{ \
	ast_string_field_build_va(chan, __do_not_use_##field, fmt, ap); \
} \
void ast_channel_##field##_build(struct ast_channel *chan, const char *fmt, ...) \
{ \
	va_list ap; \
	va_start(ap, fmt); \
	ast_channel_##field##_build_va(chan, fmt, ap); \
	va_end(ap); \
}

DEFINE_STRINGFIELD_SETTERS_FOR(name)
DEFINE_STRINGFIELD_SETTERS_FOR(language)
DEFINE_STRINGFIELD_SETTERS_FOR(musicclass)
DEFINE_STRINGFIELD_SETTERS_FOR(accountcode)
DEFINE_STRINGFIELD_SETTERS_FOR(peeraccount)
DEFINE_STRINGFIELD_SETTERS_FOR(userfield)
DEFINE_STRINGFIELD_SETTERS_FOR(call_forward)
DEFINE_STRINGFIELD_SETTERS_FOR(uniqueid)
DEFINE_STRINGFIELD_SETTERS_FOR(linkedid)
DEFINE_STRINGFIELD_SETTERS_FOR(parkinglot)
DEFINE_STRINGFIELD_SETTERS_FOR(hangupsource)
DEFINE_STRINGFIELD_SETTERS_FOR(dialcontext)

#define DEFINE_STRINGFIELD_GETTER_FOR(field) const char *ast_channel_##field(const struct ast_channel *chan) \
{ \
	return chan->__do_not_use_##field; \
}

DEFINE_STRINGFIELD_GETTER_FOR(name)
DEFINE_STRINGFIELD_GETTER_FOR(language)
DEFINE_STRINGFIELD_GETTER_FOR(musicclass)
DEFINE_STRINGFIELD_GETTER_FOR(accountcode)
DEFINE_STRINGFIELD_GETTER_FOR(peeraccount)
DEFINE_STRINGFIELD_GETTER_FOR(userfield)
DEFINE_STRINGFIELD_GETTER_FOR(call_forward)
DEFINE_STRINGFIELD_GETTER_FOR(uniqueid)
DEFINE_STRINGFIELD_GETTER_FOR(linkedid)
DEFINE_STRINGFIELD_GETTER_FOR(parkinglot)
DEFINE_STRINGFIELD_GETTER_FOR(hangupsource)
DEFINE_STRINGFIELD_GETTER_FOR(dialcontext)

const char *ast_channel_appl(const struct ast_channel *chan)
{
	return chan->__do_not_use_appl;
}
void ast_channel_appl_set(struct ast_channel *chan, const char *value)
{
	chan->__do_not_use_appl = value;
}
const char *ast_channel_blockproc(const struct ast_channel *chan)
{
	return chan->__do_not_use_blockproc;
}
void ast_channel_blockproc_set(struct ast_channel *chan, const char *value)
{
	chan->__do_not_use_blockproc = value;
}
const char *ast_channel_data(const struct ast_channel *chan)
{
	return chan->__do_not_use_data;
}
void ast_channel_data_set(struct ast_channel *chan, const char *value)
{
	chan->__do_not_use_data = value;
}


const char *ast_channel_context(const struct ast_channel *chan)
{
	return chan->__do_not_use_context;
}
void ast_channel_context_set(struct ast_channel *chan, const char *value)
{
	ast_copy_string(chan->__do_not_use_context, value, sizeof(chan->__do_not_use_context));
}
const char *ast_channel_exten(const struct ast_channel *chan)
{
	return chan->__do_not_use_exten;
}
void ast_channel_exten_set(struct ast_channel *chan, const char *value)
{
	ast_copy_string(chan->__do_not_use_exten, value, sizeof(chan->__do_not_use_exten));
}
const char *ast_channel_macrocontext(const struct ast_channel *chan)
{
	return chan->__do_not_use_macrocontext;
}
void ast_channel_macrocontext_set(struct ast_channel *chan, const char *value)
{
	ast_copy_string(chan->__do_not_use_macrocontext, value, sizeof(chan->__do_not_use_macrocontext));
}
const char *ast_channel_macroexten(const struct ast_channel *chan)
{
	return chan->__do_not_use_macroexten;
}
void ast_channel_macroexten_set(struct ast_channel *chan, const char *value)
{
	ast_copy_string(chan->__do_not_use_macroexten, value, sizeof(chan->__do_not_use_macroexten));
}
