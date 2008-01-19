/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2003, 2006 Tilghman Lesher.  All rights reserved.
 * Copyright (c) 2006 Digium, Inc.
 *
 * Tilghman Lesher <app_sayunixtime__200309@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
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
 * \brief SayUnixTime application
 *
 * \author Tilghman Lesher <app_sayunixtime__200309@the-tilghman.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/say.h"
#include "asterisk/app.h"

static char *app_sayunixtime = "SayUnixTime";
static char *app_datetime = "DateTime";

static char *sayunixtime_synopsis = "Says a specified time in a custom format";

static char *sayunixtime_descrip =
"SayUnixTime([unixtime][,[timezone][,format]])\n"
"  unixtime  - time, in seconds since Jan 1, 1970.  May be negative.\n"
"              defaults to now.\n"
"  timezone  - timezone, see /usr/share/zoneinfo for a list.\n"
"              defaults to machine default.\n"
"  format    - a format the time is to be said in.  See voicemail.conf.\n"
"              defaults to \"ABdY 'digits/at' IMp\"\n";
static char *datetime_descrip =
"DateTime([unixtime][,[timezone][,format]])\n"
"  unixtime  - time, in seconds since Jan 1, 1970.  May be negative.\n"
"              defaults to now.\n"
"  timezone  - timezone, see /usr/share/zoneinfo for a list.\n"
"              defaults to machine default.\n"
"  format:   - a format the time is to be said in.  See voicemail.conf.\n"
"              defaults to \"ABdY 'digits/at' IMp\"\n";


static int sayunixtime_exec(struct ast_channel *chan, void *data)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(timeval);
		AST_APP_ARG(timezone);
		AST_APP_ARG(format);
	);
	char *parse;
	int res = 0;
	time_t unixtime;
	
	if (!data)
		return 0;

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	ast_get_time_t(args.timeval, &unixtime, time(NULL), NULL);

	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);

	if (!res)
		res = ast_say_date_with_format(chan, unixtime, AST_DIGIT_ANY,
					       chan->language, args.format, args.timezone);

	return res;
}

static int unload_module(void)
{
	int res;
	
	res = ast_unregister_application(app_sayunixtime);
	res |= ast_unregister_application(app_datetime);
	
	return res;
}

static int load_module(void)
{
	int res;
	
	res = ast_register_application(app_sayunixtime, sayunixtime_exec, sayunixtime_synopsis, sayunixtime_descrip);
	res |= ast_register_application(app_datetime, sayunixtime_exec, sayunixtime_synopsis, datetime_descrip);
	
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Say time");
