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
 
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/utils.h"
#include <string.h>
#include <stdlib.h>

static char *tdesc = "Control Playback Application";

static char *app = "ControlPlayback";

static char *synopsis = "Play a file with fast forward and rewind";

static char *descrip = 
"ControlPlayback(filename[|skipms[|ffchar[|rewchar[|stopchar[|pausechr]]]]]):\n"
"  Plays  back  a  given  filename (do not put extension). Options may also\n"
"  be included following a pipe symbol.  You can use * and # to rewind and\n"
"  fast forward the playback specified. If 'stopchar' is added the file will\n"
"  terminate playback when 'stopchar' is pressed. Returns -1 if the channel\n"
"  was hung up. if the file does not exist jumps to n+101 if it present.\n\n"
"  Example:  exten => 1234,1,ControlPlayback(file|4000|*|#|1|0)\n\n";

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
	char *skip = NULL, *fwd = NULL, *rev = NULL, *stop = NULL, *pause = NULL, *file = NULL;
	

	if (!data || ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "ControlPlayback requires an argument (filename)\n");
		return -1;
	}

	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	file = tmp;

	if ((skip=strchr(tmp,'|'))) {
		*skip = '\0';
		*skip++;
		fwd=strchr(skip,'|');
		if (fwd) {
			*fwd = '\0';
			*fwd++;
			rev = strchr(fwd,'|');
			if (rev) {
				*rev = '\0';
				*rev++;
				stop = strchr(rev,'|');
				if (stop) {
					*stop = '\0';
					*stop++;
					pause = strchr(stop,'|');
					if (pause) {
						*pause = '\0';
						*pause++;
					}
				}
			}
		}
	}

	skipms = skip ? atoi(skip) : 3000;
	if (!skipms)
		skipms = 3000;

	if (!fwd || !is_on_phonepad(*fwd))
		fwd = "#";
	if (!rev || !is_on_phonepad(*rev))
		rev = "*";
	if (stop && !is_on_phonepad(*stop))
		stop = NULL;
	if (pause && !is_on_phonepad(*pause))
		pause = NULL;

	LOCAL_USER_ADD(u);

	res = ast_control_streamfile(chan, file, fwd, rev, stop, pause, skipms);

	LOCAL_USER_REMOVE(u);
	
	/* If we stopped on one of our stop keys, return 0  */
	if(stop && strchr(stop, res)) 
		res = 0;

	if(res < 0) {
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
            chan->priority+=100;
		res = 0;
	}
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
