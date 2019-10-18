/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Fairview 5 Engineering, LLC
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
 * \author \verbatim George Joseph <george.joseph@fairview5.com> \endverbatim
 *
 * \ingroup functions
 *
 * \brief PJSIP channel CLI functions
 */

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>
#include <pjsip_ua.h>

#include "asterisk/astobj2.h"
#include "asterisk/channel.h"
#include "asterisk/format.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/stasis.h"
#include "asterisk/time.h"
#include "include/chan_pjsip.h"
#include "include/cli_functions.h"


static int cli_channel_iterate(void *endpoint, ao2_callback_fn callback, void *arg)
{
	return ast_sip_for_each_channel(endpoint, callback, arg);
}

static int cli_channelstats_iterate(void *endpoint, ao2_callback_fn callback, void *arg)
{
	return ast_sip_for_each_channel(endpoint, callback, arg);
}

static int cli_channel_sort(const void *obj, const void *arg, int flags)
{
	const struct ast_channel_snapshot *left_obj = obj;
	const struct ast_channel_snapshot *right_obj = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right_obj->base->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left_obj->base->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left_obj->base->name, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp;
}

static int cli_channelstats_sort(const void *obj, const void *arg, int flags)
{
	const struct ast_channel_snapshot *left_obj = obj;
	const struct ast_channel_snapshot *right_obj = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		cmp = strcmp(left_obj->bridge->id, right_obj->bridge->id);
		if (cmp) {
			return cmp;
		}
		right_key = right_obj->base->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left_obj->base->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left_obj->base->name, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp;
}

static int cli_channel_compare(void *obj, void *arg, int flags)
{
	const struct ast_channel_snapshot *left_obj = obj;
	const struct ast_channel_snapshot *right_obj = arg;
	const char *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right_obj->base->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		if (strcmp(left_obj->base->name, right_key) == 0) {
			cmp = CMP_MATCH | CMP_STOP;
		}
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		if (strncmp(left_obj->base->name, right_key, strlen(right_key)) == 0) {
			cmp = CMP_MATCH;
		}
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp;
}

static int cli_channelstats_compare(void *obj, void *arg, int flags)
{
	const struct ast_channel_snapshot *left_obj = obj;
	const struct ast_channel_snapshot *right_obj = arg;
	const char *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		if (strcmp(left_obj->bridge->id, right_obj->bridge->id) == 0
			&& strcmp(left_obj->base->name, right_obj->base->name) == 0) {
			return CMP_MATCH | CMP_STOP;
		}
		break;
	case OBJ_SEARCH_KEY:
		if (strcmp(left_obj->base->name, right_key) == 0) {
			cmp = CMP_MATCH | CMP_STOP;
		}
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		if (strncmp(left_obj->base->name, right_key, strlen(right_key)) == 0) {
			cmp = CMP_MATCH;
		}
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp;
}

static int cli_message_to_snapshot(void *obj, void *arg, int flags)
{
	struct ast_channel_snapshot *snapshot = obj;
	struct ao2_container *snapshots = arg;

	if (!strcmp(snapshot->base->type, "PJSIP")) {
		ao2_link(snapshots, snapshot);
		return CMP_MATCH;
	}

	return 0;
}

static int cli_filter_channels(void *obj, void *arg, int flags)
{
	struct ast_channel_snapshot *channel = obj;
	regex_t *regexbuf = arg;

	if (!regexec(regexbuf, channel->base->name, 0, NULL, 0)
		|| !regexec(regexbuf, channel->dialplan->appl, 0, NULL, 0)) {
		return 0;
	}

	return CMP_MATCH;
}

static struct ao2_container *get_container(const char *regex, ao2_sort_fn sort_fn, ao2_callback_fn compare_fn)
{
	struct ao2_container *child_container;
	regex_t regexbuf;
	RAII_VAR(struct ao2_container *, parent_container, ast_channel_cache_by_name(), ao2_cleanup);

	if (!parent_container) {
		return NULL;
	}

	child_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, sort_fn, compare_fn);
	if (!child_container) {
		return NULL;
	}

	ao2_callback(parent_container, OBJ_MULTIPLE | OBJ_NODATA, cli_message_to_snapshot, child_container);

	if (!ast_strlen_zero(regex)) {
		if (regcomp(&regexbuf, regex, REG_EXTENDED | REG_NOSUB)) {
			ao2_ref(child_container, -1);
			return NULL;
		}
		ao2_callback(child_container, OBJ_UNLINK | OBJ_MULTIPLE | OBJ_NODATA, cli_filter_channels, &regexbuf);
		regfree(&regexbuf);
	}

	return child_container;
}

static struct ao2_container *cli_channel_get_container(const char *regex)
{
	return get_container(regex, cli_channel_sort, cli_channel_compare);
}

static struct ao2_container *cli_channelstats_get_container(const char *regex)
{
	return get_container(regex, cli_channelstats_sort, cli_channelstats_compare);
}

static const char *cli_channel_get_id(const void *obj)
{
	const struct ast_channel_snapshot *snapshot = obj;

	return snapshot->base->name;
}

static void *cli_channel_retrieve_by_id(const char *id)
{
	return ast_channel_snapshot_get_latest_by_name(id);
}

static int cli_channel_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_LAST_TABSTOP - indent - 13;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <ChannelId%*.*s>  <State.....>  <Time.....>\n",
		indent, "Channel", filler, filler, CLI_HEADER_FILLER);
	if (context->recurse) {
		context->indent_level++;
		indent = CLI_INDENT_TO_SPACES(context->indent_level);
		filler = CLI_LAST_TABSTOP - indent - 38;
		ast_str_append(&context->output_buffer, 0,
			"%*s: <DialedExten%*.*s>  CLCID: <ConnectedLineCID.......>\n",
			indent, "Exten", filler, filler, CLI_HEADER_FILLER);
		context->indent_level--;
	}

	return 0;
}

static int cli_channel_print_body(void *obj, void *arg, int flags)
{
	const struct ast_channel_snapshot *snapshot = obj;
	struct ast_sip_cli_context *context = arg;
	char *print_name = NULL;
	int print_name_len;
	int indent;
	int flexwidth;
	char *print_time = alloca(32);

	ast_assert(context->output_buffer != NULL);

	print_name_len = strlen(snapshot->base->name) + strlen(snapshot->dialplan->appl) + 2;
	print_name = alloca(print_name_len);

	/* Append the application */
	snprintf(print_name, print_name_len, "%s/%s", snapshot->base->name, snapshot->dialplan->appl);

	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	flexwidth = CLI_LAST_TABSTOP - indent;

	ast_format_duration_hh_mm_ss(ast_tvnow().tv_sec - snapshot->base->creationtime.tv_sec, print_time, 32);

	ast_str_append(&context->output_buffer, 0, "%*s: %-*.*s %-12.12s  %-11.11s\n",
		CLI_INDENT_TO_SPACES(context->indent_level), "Channel",
		flexwidth, flexwidth,
		print_name,
		ast_state2str(snapshot->state),
		print_time);

	if (context->recurse) {
		context->indent_level++;
		indent = CLI_INDENT_TO_SPACES(context->indent_level);
		flexwidth = CLI_LAST_TABSTOP - indent - 25;

		ast_str_append(&context->output_buffer, 0,
			"%*s: %-*.*s  CLCID: \"%s\" <%s>\n",
			indent, "Exten",
			flexwidth, flexwidth,
			snapshot->dialplan->exten,
			snapshot->connected->name,
			snapshot->connected->number
			);
		context->indent_level--;
		if (context->indent_level == 0) {
			ast_str_append(&context->output_buffer, 0, "\n");
		}
	}

	return 0;
}

static int cli_channelstats_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"                                             ...........Receive......... .........Transmit..........\n"
		" BridgeId ChannelId ........ UpTime.. Codec.   Count    Lost Pct  Jitter   Count    Lost Pct  Jitter RTT....\n"
		" =================");

	return 0;
}

static int cli_channelstats_print_body(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	const struct ast_channel_snapshot *snapshot = obj;
	struct ast_channel *channel = ast_channel_get_by_name(snapshot->base->name);
	struct ast_sip_channel_pvt *cpvt = NULL;
	struct ast_sip_session *session;
	struct ast_sip_session_media *media;
	struct ast_rtp_instance_stats stats;
	char *print_name = NULL;
	char *print_time = alloca(32);
	char codec_in_use[7];
	int stats_res = -1;

	ast_assert(context->output_buffer != NULL);

	if (!channel) {
		ast_str_append(&context->output_buffer, 0, " %s not valid\n", snapshot->base->name);
		return 0;
	}

	ast_channel_lock(channel);

	cpvt = ast_channel_tech_pvt(channel);
	session = cpvt ? cpvt->session : NULL;
	if (!session) {
		ast_str_append(&context->output_buffer, 0, " %s not valid\n", snapshot->base->name);
		ast_channel_unlock(channel);
		ao2_cleanup(channel);
		return 0;
	}

	media = session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO];
	if (!media || !media->rtp) {
		ast_str_append(&context->output_buffer, 0, " %s not valid\n", snapshot->base->name);
		ast_channel_unlock(channel);
		ao2_cleanup(channel);
		return 0;
	}

	codec_in_use[0] = '\0';

	if (ast_channel_rawreadformat(channel)) {
		ast_copy_string(codec_in_use, ast_format_get_name(ast_channel_rawreadformat(channel)), sizeof(codec_in_use));
	}

	stats_res = ast_rtp_instance_get_stats(media->rtp, &stats, AST_RTP_INSTANCE_STAT_ALL);
	ast_channel_unlock(channel);

	print_name = ast_strdupa(snapshot->base->name);
	/* Skip the PJSIP/.  We know what channel type it is and we need the space. */
	print_name += 6;

	ast_format_duration_hh_mm_ss(ast_tvnow().tv_sec - snapshot->base->creationtime.tv_sec, print_time, 32);

	if (stats_res == -1) {
		ast_str_append(&context->output_buffer, 0, "%s direct media\n", snapshot->base->name);
	} else {
		ast_str_append(&context->output_buffer, 0,
			" %8.8s %-18.18s %-8.8s %-6.6s %6u%s %6u%s %3u %7.3f %6u%s %6u%s %3u %7.3f %7.3f\n",
			snapshot->bridge->id,
			print_name,
			print_time,
			codec_in_use,
			stats.rxcount > 100000 ? stats.rxcount / 1000 : stats.rxcount,
			stats.rxcount > 100000 ? "K": " ",
			stats.rxploss > 100000 ? stats.rxploss / 1000 : stats.rxploss,
			stats.rxploss > 100000 ? "K": " ",
			stats.rxcount ? (stats.rxploss * 100) / stats.rxcount : 0,
			MIN(stats.rxjitter, 999.999),
			stats.txcount > 100000 ? stats.txcount / 1000 : stats.txcount,
			stats.txcount > 100000 ? "K": " ",
			stats.txploss > 100000 ? stats.txploss / 1000 : stats.txploss,
			stats.txploss > 100000 ? "K": " ",
			stats.txcount ? (stats.txploss * 100) / stats.txcount : 0,
			MIN(stats.txjitter, 999.999),
			MIN(stats.normdevrtt, 999.999)
		);
	}

	ao2_cleanup(channel);

	return 0;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Channels",
		.command = "pjsip list channels",
		.usage = "Usage: pjsip list channels [ like <pattern> ]\n"
				"       List the active PJSIP channels\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Channels",
		.command = "pjsip show channels",
		.usage = "Usage: pjsip show channels [ like <pattern> ]\n"
				"       List(detailed) the active PJSIP channels\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Channel",
		.command = "pjsip show channel",
		.usage = "Usage: pjsip show channel\n"
				 "       List(detailed) the active PJSIP channel\n"),

	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Channel Stats",
		.command = "pjsip show channelstats",
		.usage = "Usage: pjsip show channelstats [ like <pattern> ]\n"
				"       List(detailed) the active PJSIP channel stats\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
};

struct ast_sip_cli_formatter_entry *channelstats_formatter;
struct ast_sip_cli_formatter_entry *channel_formatter;

int pjsip_channel_cli_register(void)
{
	channel_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!channel_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for channel_formatter\n");
		return -1;
	}
	channel_formatter->name = "channel";
	channel_formatter->print_header = cli_channel_print_header;
	channel_formatter->print_body = cli_channel_print_body;
	channel_formatter->get_container = cli_channel_get_container;
	channel_formatter->iterate = cli_channel_iterate;
	channel_formatter->retrieve_by_id = cli_channel_retrieve_by_id;
	channel_formatter->get_id = cli_channel_get_id;

	channelstats_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!channelstats_formatter) {
		ao2_ref(channel_formatter, -1);
		ast_log(LOG_ERROR, "Unable to allocate memory for channelstats_formatter\n");
		return -1;
	}
	channelstats_formatter->name = "channelstat";
	channelstats_formatter->print_header = cli_channelstats_print_header;
	channelstats_formatter->print_body = cli_channelstats_print_body;
	channelstats_formatter->get_container = cli_channelstats_get_container;
	channelstats_formatter->iterate = cli_channelstats_iterate;
	channelstats_formatter->retrieve_by_id = cli_channel_retrieve_by_id;
	channelstats_formatter->get_id = cli_channel_get_id;

	ast_sip_register_cli_formatter(channel_formatter);
	ast_sip_register_cli_formatter(channelstats_formatter);
	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	return 0;
}

void pjsip_channel_cli_unregister(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sip_unregister_cli_formatter(channel_formatter);
	ast_sip_unregister_cli_formatter(channelstats_formatter);
}
