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
"ControlPlayback(filename[|skipms][|<rewindchar><ffchar><endchar>]):\n"
"  Plays  back  a  given  filename (do not put extension). Options may also\n"
"  be included following a pipe symbol.  You can use * and # to rewind and\n"
"  fast forward the playback specified. If 'endchar' is added the file will\n"
"  terminate playback when 'endchar' is pressed. Returns -1 if the channel\n"
"  was hung up, or if the file does not exist. Returns 0 otherwise.\n\n"
"  Example:  exten => 1234,1,ControlPlayback(file|4000|*#1)\n\n";


STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int is_on_phonepad(char key)
{
	return (key == 35 || key == 42 || (key >= 48 && key <= 57)) ? 1 : 0;
}

static int controlplayback_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	int skipms = 0;
	struct localuser *u;
	char tmp[256];
	char opts[3];
	char *skip = NULL, *stop = NULL;
	if (!data || ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "ControlPlayback requires an argument (filename)\n");
		return -1;
	}
	
	memset(opts,0,3);
	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	if((skip=strchr(tmp,'|'))) {
		*skip = '\0';
		*skip++;
	}

	if(skip && (stop=strchr(skip,'|'))) {
		*stop = '\0';
		*stop++;
		strncpy(opts,stop,3);
	}

	skipms = skip ? atoi(skip) : 3000;
	if(!skipms)
		skipms = 3000;

	if(opts[0] == '\0' || ! is_on_phonepad(opts[0]))
		opts[0] = '*';
	if(opts[1] == '\0' || ! is_on_phonepad(opts[1]))
		opts[1] = '#';
	if(opts[2] == '\0' || ! is_on_phonepad(opts[2]))
		opts[2] = '1';

	LOCAL_USER_ADD(u);

	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);

	ast_stopstream(chan);
	for(;;) {
		res = ast_control_streamfile(chan, tmp, &opts[1], &opts[0], skipms);
		if (res < 1)
			break;
		if(res == opts[2]) {
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
