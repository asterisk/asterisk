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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/bridging.h"
#include "include/confbridge.h"

/* ------------------------------------------------------------------- */

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

static struct ast_channel *rec_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct ast_channel *chan;
	struct ast_format format;
	const char *conf_name = data;

	chan = ast_channel_alloc(1, AST_STATE_UP, NULL, NULL, NULL, NULL, NULL, NULL, 0,
		"CBRec/conf-%s-uid-%d",
		conf_name, (int) ast_random());
	if (!chan) {
		return NULL;
	}
	if (ast_channel_add_bridge_role(chan, "recorder")) {
		ast_channel_release(chan);
		return NULL;
	}
	ast_format_set(&format, AST_FORMAT_SLINEAR, 0);
	ast_channel_tech_set(chan, conf_record_get_tech());
	ast_format_cap_add_all(ast_channel_nativeformats(chan));
	ast_format_copy(ast_channel_writeformat(chan), &format);
	ast_format_copy(ast_channel_rawwriteformat(chan), &format);
	ast_format_copy(ast_channel_readformat(chan), &format);
	ast_format_copy(ast_channel_rawreadformat(chan), &format);
	return chan;
}

static struct ast_channel_tech record_tech = {
	.type = "CBRec",
	.description = "Conference Bridge Recording Channel",
	.requester = rec_request,
	.call = rec_call,
	.read = rec_read,
	.write = rec_write,
};

struct ast_channel_tech *conf_record_get_tech(void)
{
	return &record_tech;
}
