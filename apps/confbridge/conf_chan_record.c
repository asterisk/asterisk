/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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
 * \brief ConfBridge recorder channel driver
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */


#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/format_cache.h"
#include "include/confbridge.h"

/* ------------------------------------------------------------------- */

static unsigned int name_sequence = 0;

static int rec_call(struct ast_channel *chan, const char *addr, int timeout)
{
	/* Make sure anyone calling ast_call() for this channel driver is going to fail. */
	return -1;
}

static struct ast_frame *rec_read(struct ast_channel *ast)
{
	return &ast_null_frame;
}

static int rec_write(struct ast_channel *ast, struct ast_frame *f)
{
	return 0;
}

static struct ast_channel *rec_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct ast_channel *chan;
	const char *conf_name = data;
	RAII_VAR(struct ast_format_cap *, capabilities, NULL, ao2_cleanup);
	int generated_seqno = ast_atomic_fetchadd_int((int *) &name_sequence, +1);

	capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!capabilities) {
		return NULL;
	}
	ast_format_cap_append_by_type(capabilities, AST_MEDIA_TYPE_UNKNOWN);

	chan = ast_channel_alloc(1, AST_STATE_UP, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0,
		"CBRec/%s-%08x",
		conf_name, (unsigned) generated_seqno);
	if (!chan) {
		return NULL;
	}
	if (ast_channel_add_bridge_role(chan, "recorder")) {
		ast_channel_unlock(chan);
		ast_channel_release(chan);
		return NULL;
	}

	ast_channel_tech_set(chan, conf_record_get_tech());
	ast_channel_nativeformats_set(chan, capabilities);
	ast_channel_set_writeformat(chan, ast_format_slin);
	ast_channel_set_rawwriteformat(chan, ast_format_slin);
	ast_channel_set_readformat(chan, ast_format_slin);
	ast_channel_set_rawreadformat(chan, ast_format_slin);
	ast_channel_unlock(chan);
	return chan;
}

static struct ast_channel_tech record_tech = {
	.type = "CBRec",
	.description = "Conference Bridge Recording Channel",
	.requester = rec_request,
	.call = rec_call,
	.read = rec_read,
	.write = rec_write,
	.properties = AST_CHAN_TP_INTERNAL,
};

struct ast_channel_tech *conf_record_get_tech(void)
{
	return &record_tech;
}
