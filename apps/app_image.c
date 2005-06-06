/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * App to transmit an image
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

static char *tdesc = "Image Transmission Application";

static char *app = "SendImage";

static char *synopsis = "Send an image file";

static char *descrip = 
"  SendImage(filename): Sends an image on a channel. If the channel\n"
"does not support  image transport, and there exists  a  step  with\n"
"priority n + 101, then  execution  will  continue  at  that  step.\n"
"Otherwise,  execution  will continue at  the  next priority level.\n"
"SendImage only  returns  0 if  the  image was sent correctly or if\n"
"the channel does not support image transport, and -1 otherwise.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int sendimage_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	if (!data || !strlen((char *)data)) {
		ast_log(LOG_WARNING, "SendImage requires an argument (filename)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	if (!ast_supports_images(chan)) {
		/* Does not support transport */
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
			chan->priority += 100;
		return 0;
	}
	res = ast_send_image(chan, data);
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
	return ast_register_application(app, sendimage_exec, synopsis, descrip);
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
