/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc.
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

/*!
 * \file
 * \brief Siren7 format attribute interface
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <ctype.h>                      /* for tolower */

#include "asterisk/module.h"
#include "asterisk/format.h"
#include "asterisk/astobj2.h"           /* for ao2_bump */
#include "asterisk/logger.h"            /* for ast_log, LOG_WARNING */
#include "asterisk/strings.h"           /* for ast_str_append */

/* Destroy is a required callback and must exist */
static void siren7_destroy(struct ast_format *format)
{
}

/* Clone is a required callback and must exist */
static int siren7_clone(const struct ast_format *src, struct ast_format *dst)
{
	return 0;
}

static struct ast_format *siren7_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	char *attribs = ast_strdupa(attributes), *attrib;
	unsigned int val;

	/* lower-case everything, so we are case-insensitive */
	for (attrib = attribs; *attrib; ++attrib) {
		*attrib = tolower(*attrib);
	} /* based on channels/chan_sip.c:process_a_sdp_image() */

	if (sscanf(attribs, "bitrate=%30u", &val) == 1) {
		if (val != 32000) {
			ast_log(LOG_WARNING, "Got Siren7 offer at %u bps, but only 32000 bps supported; ignoring.\n", val);
			return NULL;
		}
	}

	/* We aren't modifying the format and once passed back it won't be touched, so use what we were given */
	return ao2_bump((struct ast_format *)format);
}

static void siren7_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	ast_str_append(str, 0, "a=fmtp:%u bitrate=32000\r\n", payload);
}

static struct ast_format_interface siren7_interface = {
	.format_destroy = siren7_destroy,
	.format_clone = siren7_clone,
	.format_parse_sdp_fmtp = siren7_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = siren7_generate_sdp_fmtp,
};

static int load_module(void)
{
	if (ast_format_interface_register("siren7", &siren7_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Siren7 Format Attribute Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
