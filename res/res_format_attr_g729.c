/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc.
 *
 * Jason Parker <jparker@sangoma.com>
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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/format.h"

/* Destroy is a required callback and must exist */
static void g729_destroy(struct ast_format *format)
{
}

/* Clone is a required callback and must exist */
static int g729_clone(const struct ast_format *src, struct ast_format *dst)
{
	return 0;
}

static void g729_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	/*
	 * According to the rfc the joint annexb format parameter should be set to 'yes'
	 * or 'no' based on the answerer (rfc7261 - 3.3). However, Asterisk being a B2BUA
	 * makes things tricky. So for now Asterisk will set annexb=no.
	 */
	ast_str_append(str, 0, "a=fmtp:%u annexb=no\r\n", payload);
}

static struct ast_format_interface g729_interface = {
	.format_destroy = g729_destroy,
	.format_clone = g729_clone,
	.format_generate_sdp_fmtp = g729_generate_sdp_fmtp,
};

static int load_module(void)
{
	if (ast_format_interface_register("g729", &g729_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "G.729 Format Attribute Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
