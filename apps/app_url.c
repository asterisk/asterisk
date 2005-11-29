/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to transmit a URL
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <string.h>
#include <stdlib.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"

static char *tdesc = "Send URL Applications";

static char *app = "SendURL";

static char *synopsis = "Send a URL";

static char *descrip = 
"  SendURL(URL[|option]): Requests client go to URL.  If the client\n"
"does not support html transport, and  there  exists  a  step  with\n"
"priority  n + 101,  then  execution  will  continue  at that step.\n"
"Otherwise, execution will continue at  the  next  priority  level.\n"
"SendURL only returns 0  if  the  URL  was  sent  correctly  or  if\n"
"the channel  does  not  support HTML transport,  and -1 otherwise.\n"
"If the option 'wait' is  specified,  execution  will  wait  for an\n"
"acknowledgement that  the  URL  has  been loaded before continuing\n"
"and will return -1 if the peer is unable to load the URL\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int sendurl_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char tmp[256];
	char *options;
	int option_wait=0;
	struct ast_frame *f;
	char *stringp=NULL;
	if (!data || !strlen((char *)data)) {
		ast_log(LOG_WARNING, "SendURL requires an argument (URL)\n");
		return -1;
	}
	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	stringp=tmp;
	strsep(&stringp, "|");
	options = strsep(&stringp, "|");
	if (options && !strcasecmp(options, "wait"))
		option_wait = 1;
	LOCAL_USER_ADD(u);
	if (!ast_channel_supports_html(chan)) {
		/* Does not support transport */
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
			chan->priority += 100;
		LOCAL_USER_REMOVE(u);
		return 0;
	}
	res = ast_channel_sendurl(chan, tmp);
	if (res > -1) {
		if (option_wait) {
			for(;;) {
				/* Wait for an event */
				res = ast_waitfor(chan, -1);
				if (res < 0) 
					break;
				f = ast_read(chan);
				if (!f) {
					res = -1;
					break;
				}
				if (f->frametype == AST_FRAME_HTML) {
					switch(f->subclass) {
					case AST_HTML_LDCOMPLETE:
						res = 0;
						ast_frfree(f);
						goto out;
						break;
					case AST_HTML_NOSUPPORT:
						/* Does not support transport */
						if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
							chan->priority += 100;
						res = 0;
						goto out;
						break;
					default:
						ast_log(LOG_WARNING, "Don't know what to do with HTML subclass %d\n", f->subclass);
					};
				}
				ast_frfree(f);
			}
		}
	}
out:	
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
	return ast_register_application(app, sendurl_exec, synopsis, descrip);
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
