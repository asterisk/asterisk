/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Language related dialplan functions
 * 
 * Copyright (C) 2005, Digium, Inc.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static char *builtin_function_language_read(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	ast_copy_string(buf, chan->language, len);

	return buf;
}

static void builtin_function_language_write(struct ast_channel *chan, char *cmd, char *data, const char *value) 
{
	if (value)
		ast_copy_string(chan->language, value, sizeof(chan->language));
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function language_function = {
	.name = "LANGUAGE",
	.synopsis = "Gets or sets the channel's language.",
	.syntax = "LANGUAGE()",
	.desc = "Gets or sets the channel language.  This information is used for the\n"
	"syntax in generation of numbers, and to choose a natural language file\n"
	"when available.  For example, if language is set to 'fr' and the file\n"
	"'demo-congrats' is requested to be played, if the file\n"
	"'fr/demo-congrats' exists, then it will play that file, and if not\n"
	"will play the normal 'demo-congrats'.  For some language codes,\n"
	"changing the language also changes the syntax of some Asterisk\n"
	"functions, like SayNumber.\n",
	.read = builtin_function_language_read,
	.write = builtin_function_language_write,
};

/*
Local Variables:
mode: C
c-file-style: "linux"
indent-tabs-mode: nil
End:
*/
