/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
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
 * \brief Channel info dialplan function
 *
 * \author Kevin P. Fleming <kpfleming@digium.com>
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/indications.h"
#include "asterisk/stringfields.h"
#define locked_copy_string(chan, dest, source, len) \
	do { \
		ast_channel_lock(chan); \
		ast_copy_string(dest, source, len); \
		ast_channel_unlock(chan); \
	} while (0)
#define locked_string_field_set(chan, field, source) \
	do { \
		ast_channel_lock(chan); \
		ast_string_field_set(chan, field, source); \
		ast_channel_unlock(chan); \
	} while (0)

static int func_channel_read(struct ast_channel *chan, char *function,
			     char *data, char *buf, size_t len)
{
	int ret = 0;

	if (!strcasecmp(data, "audionativeformat"))
		/* use the _multiple version when chan->nativeformats holds multiple formats */
		/* ast_getformatname_multiple(buf, len, chan->nativeformats & AST_FORMAT_AUDIO_MASK); */
		ast_copy_string(buf, ast_getformatname(chan->nativeformats & AST_FORMAT_AUDIO_MASK), len);
	else if (!strcasecmp(data, "videonativeformat"))
		/* use the _multiple version when chan->nativeformats holds multiple formats */
		/* ast_getformatname_multiple(buf, len, chan->nativeformats & AST_FORMAT_VIDEO_MASK); */
		ast_copy_string(buf, ast_getformatname(chan->nativeformats & AST_FORMAT_VIDEO_MASK), len);
	else if (!strcasecmp(data, "audioreadformat"))
		ast_copy_string(buf, ast_getformatname(chan->readformat), len);
	else if (!strcasecmp(data, "audiowriteformat"))
		ast_copy_string(buf, ast_getformatname(chan->writeformat), len);
	else if (!strcasecmp(data, "tonezone") && chan->zone)
		locked_copy_string(chan, buf, chan->zone->country, len);
	else if (!strcasecmp(data, "language"))
		locked_copy_string(chan, buf, chan->language, len);
	else if (!strcasecmp(data, "musicclass"))
		locked_copy_string(chan, buf, chan->musicclass, len);
	else if (!strcasecmp(data, "state"))
		locked_copy_string(chan, buf, ast_state2str(chan->_state), len);
	else if (!strcasecmp(data, "channeltype"))
		locked_copy_string(chan, buf, chan->tech->type, len);
	else if (!strcasecmp(data, "callgroup")) {
		char groupbuf[256];
		locked_copy_string(chan, buf,  ast_print_group(groupbuf, sizeof(groupbuf), chan->callgroup), len);
	} else if (!chan->tech->func_channel_read
		 || chan->tech->func_channel_read(chan, function, data, buf, len)) {
		ast_log(LOG_WARNING, "Unknown or unavailable item requested: '%s'\n", data);
		ret = -1;
	}

	return ret;
}

static int func_channel_write(struct ast_channel *chan, char *function,
			      char *data, const char *value)
{
	int ret = 0;
	signed char gainset;

	if (!strcasecmp(data, "language"))
		locked_string_field_set(chan, language, value);
	else if (!strcasecmp(data, "musicclass"))
		locked_string_field_set(chan, musicclass, value);
	else if (!strcasecmp(data, "callgroup"))
		chan->callgroup = ast_get_group(data);
	else if (!strcasecmp(data, "txgain")) {
		sscanf(value, "%hhd", &gainset);
		ast_channel_setoption(chan, AST_OPTION_TXGAIN, &gainset, sizeof(gainset), 0);
	} else if (!strcasecmp(data, "rxgain")) {
		sscanf(value, "%hhd", &gainset);
		ast_channel_setoption(chan, AST_OPTION_RXGAIN, &gainset, sizeof(gainset), 0);	
	} else if (!chan->tech->func_channel_write
		 || chan->tech->func_channel_write(chan, function, data, value)) {
		ast_log(LOG_WARNING, "Unknown or unavailable item requested: '%s'\n",
				data);
		ret = -1;
	}

	return ret;
}

static struct ast_custom_function channel_function = {
	.name = "CHANNEL",
	.synopsis = "Gets/sets various pieces of information about the channel.",
	.syntax = "CHANNEL(item)",
	.desc = "Gets/set various pieces of information about the channel.\n"
		"Standard items (provided by all channel technologies) are:\n"
		"R/O	audioreadformat		format currently being read\n"
		"R/O	audionativeformat 	format used natively for audio\n"
		"R/O	audiowriteformat 	format currently being written\n"
		"R/W	callgroup		call groups for call pickup\n"
		"R/O	channeltype		technology used for channel\n"
		"R/W	language 		language for sounds played\n"
		"R/W	musicclass 		class (from musiconhold.conf) for hold music\n"
		"R/W	rxgain			set rxgain level on channel drivers that support it\n"
		"R/O	state			state for channel\n"
		"R/O	tonezone 		zone for indications played\n"
		"R/W	txgain			set txgain level on channel drivers that support it\n"
		"R/O	videonativeformat 	format used natively for video\n"
		"\n"
		"Additional items may be available from the channel driver providing\n"
		"the channel; see its documentation for details.\n"
		"\n"
		"Any item requested that is not available on the current channel will\n"
		"return an empty string.\n",
	.read = func_channel_read,
	.write = func_channel_write,
};

static char *tdesc = "Channel information dialplan function";

static int unload_module(void *mod)
{
	return ast_custom_function_unregister(&channel_function);
}

static int load_module(void *mod)
{
	return ast_custom_function_register(&channel_function);
}

static const char *description(void)
{
	return tdesc;
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD(MOD_1 | NO_USECOUNT, NULL, NULL, NULL);
