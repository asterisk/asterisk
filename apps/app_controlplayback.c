/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Trivial application to control playback a sound file
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/app.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/utils.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

static char *tdesc = "Control Playback Application";

static char *app = "ControlPlayback";

static char *synopsis = "Play a file with fast forward and rewind";

static char *descrip = 
"  ControlPlayback(filename[|skipms][|endplay]):  Plays  back  a  given  filename\n"
"(do not put extension). Options may also be included following a pipe symbol.\n"
"you can use * and # to rewind and fast forward the playback specified. If 'endplay' is.\n"
"added the file will terminate playback when 1 is pressed. Returns -1 if the channel\n"
"was hung up, or if the file does not exist. Returns 0 otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int controlplayback_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	int skipms = 0;
	struct localuser *u;
	char tmp[256];
	char *skip, *endplay;
	int option_endplay = 0;
	if (!data || ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "ControlPlayback requires an argument (filename)\n");
		return -1;
	}
	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	if((skip=strchr(tmp,'|'))) {
		*skip = '\0';
		*skip++;
		if((endplay=strchr(skip,'|'))) {
			*endplay = '\0';
			*endplay++;
			if(!strcmp(endplay,"endplay")) {
				option_endplay = 1;
			}
		}
	}
	if (atoi(skip) > 0) {
		skipms =  atoi(skip);
	} else {
		skipms = 3000;
	}
		
	LOCAL_USER_ADD(u);

	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);

	ast_stopstream(chan);
	for(;;) {
		res = ast_control_streamfile(chan, tmp, "#", "*", skipms);
		if (res < 1)
			break;
		if(option_endplay && res == 49) {
			res = 0;
			break;
		}
	}
	ast_stopstream(chan);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, controlplayback_exec, synopsis, descrip);
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
