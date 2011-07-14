/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
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
 * \brief Simple two channel bridging module
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \ingroup bridges
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridging.h"
#include "asterisk/bridging_technology.h"
#include "asterisk/frame.h"

static int simple_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_channel *c0 = AST_LIST_FIRST(&bridge->channels)->chan, *c1 = AST_LIST_LAST(&bridge->channels)->chan;

	/* If this is the first channel we can't make it compatible... unless we make it compatible with itself O.o */
	if (AST_LIST_FIRST(&bridge->channels) == AST_LIST_LAST(&bridge->channels)) {
		return 0;
	}

	/* See if we need to make these compatible */
	if ((ast_format_cmp(&c0->writeformat, &c1->readformat) == AST_FORMAT_CMP_EQUAL) &&
		(ast_format_cmp(&c0->readformat, &c1->writeformat) == AST_FORMAT_CMP_EQUAL) &&
		(ast_format_cap_identical(c0->nativeformats, c1->nativeformats))) {
		return 0;
	}

	/* BOOM! We do. */
	return ast_channel_make_compatible(c0, c1);
}

static enum ast_bridge_write_result simple_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_channel *other = NULL;

	/* If this is the only channel in this bridge then immediately exit */
	if (AST_LIST_FIRST(&bridge->channels) == AST_LIST_LAST(&bridge->channels)) {
		return AST_BRIDGE_WRITE_FAILED;
	}

	/* Find the channel we actually want to write to */
	if (!(other = (AST_LIST_FIRST(&bridge->channels) == bridge_channel ? AST_LIST_LAST(&bridge->channels) : AST_LIST_FIRST(&bridge->channels)))) {
		return AST_BRIDGE_WRITE_FAILED;
	}

	/* Write the frame out if they are in the waiting state... don't worry about freeing it, the bridging core will take care of it */
	if (other->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
		ast_write(other->chan, frame);
	}

	return AST_BRIDGE_WRITE_SUCCESS;
}

static struct ast_bridge_technology simple_bridge = {
	.name = "simple_bridge",
	.capabilities = AST_BRIDGE_CAPABILITY_1TO1MIX | AST_BRIDGE_CAPABILITY_THREAD,
	.preference = AST_BRIDGE_PREFERENCE_MEDIUM,
	.join = simple_bridge_join,
	.write = simple_bridge_write,
};

static int unload_module(void)
{
	ast_format_cap_destroy(simple_bridge.format_capabilities);
	return ast_bridge_technology_unregister(&simple_bridge);
}

static int load_module(void)
{
	if (!(simple_bridge.format_capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_add_all_by_type(simple_bridge.format_capabilities, AST_FORMAT_TYPE_AUDIO);
	ast_format_cap_add_all_by_type(simple_bridge.format_capabilities, AST_FORMAT_TYPE_VIDEO);
	ast_format_cap_add_all_by_type(simple_bridge.format_capabilities, AST_FORMAT_TYPE_TEXT);

	return ast_bridge_technology_register(&simple_bridge);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Simple two channel bridging module");
