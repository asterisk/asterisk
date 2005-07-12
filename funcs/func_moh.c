/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Functions for reading or setting the MusicOnHold class
 * 
 * Copyright (C) 2005, Digium, Inc.
 *
 * Russell Bryant <russelb@clemson.edu> 
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdlib.h>

#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"

static char *function_moh_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	ast_copy_string(buf, chan->musicclass, len);

	return buf;
}

static void function_moh_write(struct ast_channel *chan, char *cmd, char *data, const char *value) 
{
	ast_copy_string(chan->musicclass, value, MAX_MUSICCLASS);
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function moh_function = {
	.name = "MUSICCLASS",
	.synopsis = "Read or Set the MusicOnHold class",
	.syntax = "MUSICCLASS()",
	.desc = "This function will read or set the music on hold class for a channel.\n",
	.read = function_moh_read,
	.write = function_moh_write,
};

