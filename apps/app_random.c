/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2003 - 2005 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <asterisk__app_random__200508@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage or distribution.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief Random application
 *
 * \author Tilghman Lesher <asterisk__app_random__200508@the-tilghman.com>
 * \ingroup applications
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"

/*! \todo The Random() app should be removed from trunk following the release of 1.4 */

static char *tdesc = "Random goto";

static char *app_random = "Random";

static char *random_synopsis = "Conditionally branches, based upon a probability";

static char *random_descrip =
"Random([probability]:[[context|]extension|]priority)\n"
"  probability := INTEGER in the range 1 to 100\n"
"DEPRECATED: Use GotoIf($[${RAND(1,100)} > <number>]?<label>)\n";

LOCAL_USER_DECL;

static int random_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;

	char *s;
	char *prob;
	int probint;
	static int deprecated = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Random requires an argument ([probability]:[[context|]extension|]priority)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);

	if (!(s = ast_strdupa(data))) {
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	prob = strsep(&s,":");
	if ((!prob) || (sscanf(prob, "%d", &probint) != 1))
		probint = 0;

	if (!deprecated) {
		deprecated = 1;
		ast_log(LOG_WARNING, "Random is deprecated in Asterisk 1.3 or later.  Replace with GotoIf($[${RAND(0,99)} + %d >= 100]?%s)\n", probint, s);
	}

	if ((ast_random() % 100) + probint > 100) {
		res = ast_parseable_goto(chan, s);
		if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "Random branches to (%s,%s,%d)\n",
				chan->context,chan->exten, chan->priority+1);
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static int unload_module(void *mod)
{
	int res;
	
	res = ast_unregister_application(app_random);
	
	STANDARD_HANGUP_LOCALUSERS;

	return res;	
}

static int load_module(void *mod)
{
	return ast_register_application(app_random, random_exec, random_synopsis, random_descrip);
}

static const char *description(void)
{
	return tdesc;
}

static const char *key(void)
{
	return ASTERISK_GPL_KEY;
}

STD_MOD1;
