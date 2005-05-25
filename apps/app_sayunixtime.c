/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * SayUnixTime application
 * 
 * Copyright (c) 2003 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_sayunixtime__200309@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/say.h"


static char *tdesc = "Say time";

static char *app_sayunixtime = "SayUnixTime";
static char *app_datetime = "DateTime";

static char *sayunixtime_synopsis = "Says a specified time in a custom format";

static char *sayunixtime_descrip =
"SayUnixTime([unixtime][|[timezone][|format]])\n"
"  unixtime: time, in seconds since Jan 1, 1970.  May be negative.\n"
"              defaults to now.\n"
"  timezone: timezone, see /usr/share/zoneinfo for a list.\n"
"              defaults to machine default.\n"
"  format:   a format the time is to be said in.  See voicemail.conf.\n"
"              defaults to \"ABdY 'digits/at' IMp\"\n"
"  Returns 0 or -1 on hangup.\n";
static char *datetime_descrip =
"DateTime([unixtime][|[timezone][|format]])\n"
"  unixtime: time, in seconds since Jan 1, 1970.  May be negative.\n"
"              defaults to now.\n"
"  timezone: timezone, see /usr/share/zoneinfo for a list.\n"
"              defaults to machine default.\n"
"  format:   a format the time is to be said in.  See voicemail.conf.\n"
"              defaults to \"ABdY 'digits/at' IMp\"\n"
"  Returns 0 or -1 on hangup.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int sayunixtime_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *s,*zone=NULL,*timec,*format;
	time_t unixtime;
	struct timeval tv;
	
	LOCAL_USER_ADD(u);

	gettimeofday(&tv,NULL);
	unixtime = (time_t)tv.tv_sec;

	if( !strcasecmp(chan->language, "da" ) ) {
		format = "A dBY HMS";
	} else if ( !strcasecmp(chan->language, "de" ) ) {
		format = "A dBY HMS";
	} else {
		format = "ABdY 'digits/at' IMp";
	} 

	if (data) {
		s = data;
		s = ast_strdupa(s);
		if (s) {
			timec = strsep(&s,"|");
			if ((timec) && (*timec != '\0')) {
				long timein;
				if (sscanf(timec,"%ld",&timein) == 1) {
					unixtime = (time_t)timein;
				}
			}
			if (s) {
				zone = strsep(&s,"|");
				if (zone && (*zone == '\0'))
					zone = NULL;
				if (s) {
					format = s;
				}
			}
		} else {
			ast_log(LOG_ERROR, "Out of memory error\n");
		}
	}

	if (chan->_state != AST_STATE_UP) {
		res = ast_answer(chan);
	}
	if (!res)
		res = ast_say_date_with_format(chan, unixtime, AST_DIGIT_ANY, chan->language, format, zone);

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;
	res = ast_unregister_application(app_sayunixtime);
	if (! res)
		return ast_unregister_application(app_datetime);
	else
		return res;
}

int load_module(void)
{
	int res;
	res = ast_register_application(app_sayunixtime, sayunixtime_exec, sayunixtime_synopsis, sayunixtime_descrip);
	if (! res)
		return ast_register_application(app_datetime, sayunixtime_exec, sayunixtime_synopsis, datetime_descrip);
	else
		return res;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
