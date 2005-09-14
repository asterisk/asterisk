/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * Digital Milliwatt Test
 * 
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"

static char *tdesc = "Digital Milliwatt (mu-law) Test Application";

static char *app = "Milliwatt";

static char *synopsis = "Generate a Constant 1000Hz tone at 0dbm (mu-law)";

static char *descrip = 
"Milliwatt(): Generate a Constant 1000Hz tone at 0dbm (mu-law)\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static char digital_milliwatt[] = {0x1e,0x0b,0x0b,0x1e,0x9e,0x8b,0x8b,0x9e} ;

static void *milliwatt_alloc(struct ast_channel *chan, void *params)
{
int	*indexp;
	indexp = malloc(sizeof(int));
	if (indexp == NULL) return(NULL);
	*indexp = 0;
	return(indexp);
}

static void milliwatt_release(struct ast_channel *chan, void *data)
{
	free(data);
	return;
}

static int milliwatt_generate(struct ast_channel *chan, void *data, int len, int samples)
{
	struct ast_frame wf;
	unsigned char waste[AST_FRIENDLY_OFFSET];
	unsigned char buf[640];
	int i,*indexp = (int *) data;

	if (len > sizeof(buf))
	{
		ast_log(LOG_WARNING,"Only doing %d bytes (%d bytes requested)\n",(int)sizeof(buf),len);
		len = sizeof(buf);
	}
	waste[0] = 0; /* make compiler happy */
	wf.frametype = AST_FRAME_VOICE;
	wf.subclass = AST_FORMAT_ULAW;
	wf.offset = AST_FRIENDLY_OFFSET;
	wf.mallocd = 0;
	wf.data = buf;
	wf.datalen = len;
	wf.samples = wf.datalen;
	wf.src = "app_milliwatt";
	wf.delivery.tv_sec = 0;
	wf.delivery.tv_usec = 0;
	/* create a buffer containing the digital milliwatt pattern */
	for(i = 0; i < len; i++)
	{
		buf[i] = digital_milliwatt[(*indexp)++];
		*indexp &= 7;
	}
	if (ast_write(chan,&wf) < 0)
	{
		ast_log(LOG_WARNING,"Failed to write frame to '%s': %s\n",chan->name,strerror(errno));
		return -1;
	}
	return 0;
}

static struct ast_generator milliwattgen = 
{
	alloc: milliwatt_alloc,
	release: milliwatt_release,
	generate: milliwatt_generate,
} ;

static int milliwatt_exec(struct ast_channel *chan, void *data)
{

	struct localuser *u;
	LOCAL_USER_ADD(u);
	ast_set_write_format(chan, AST_FORMAT_ULAW);
	ast_set_read_format(chan, AST_FORMAT_ULAW);
	if (chan->_state != AST_STATE_UP)
	{
		ast_answer(chan);
	}
	if (ast_activate_generator(chan,&milliwattgen,"milliwatt") < 0)
	{
		ast_log(LOG_WARNING,"Failed to activate generator on '%s'\n",chan->name);
		return -1;
	}
	while(!ast_safe_sleep(chan, 10000));
	ast_deactivate_generator(chan);
	LOCAL_USER_REMOVE(u);
	return -1;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, milliwatt_exec, synopsis, descrip);
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
