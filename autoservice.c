/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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

/*! \file
 *
 * \brief Automatic channel service routines
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>			/* For PI */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/frame.h"
#include "asterisk/sched.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/file.h"
#include "asterisk/translate.h"
#include "asterisk/manager.h"
#include "asterisk/chanvars.h"
#include "asterisk/linkedlists.h"
#include "asterisk/indications.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"

#define MAX_AUTOMONS 256

struct asent {
	struct ast_channel *chan;
	AST_LIST_ENTRY(asent) list;
};

static AST_LIST_HEAD_STATIC(aslist, asent);

static pthread_t asthread = AST_PTHREADT_NULL;

static void *autoservice_run(void *ign)
{
	struct ast_channel *mons[MAX_AUTOMONS];
	int x;
	int ms;
	struct ast_channel *chan;
	struct asent *as;
	struct ast_frame *f;

	for(;;) {
		x = 0;
		AST_LIST_LOCK(&aslist);
		AST_LIST_TRAVERSE(&aslist, as, list) {
			if (!as->chan->_softhangup) {
				if (x < MAX_AUTOMONS)
					mons[x++] = as->chan;
				else
					ast_log(LOG_WARNING, "Exceeded maximum number of automatic monitoring events.  Fix autoservice.c\n");
			}
		}
		AST_LIST_UNLOCK(&aslist);

		ms = 500;
		chan = ast_waitfor_n(mons, x, &ms);
		if (chan) {
			/* Read and ignore anything that occurs */
			f = ast_read(chan);
			if (f)
				ast_frfree(f);
		}
	}
	asthread = AST_PTHREADT_NULL;
	return NULL;
}

int ast_autoservice_start(struct ast_channel *chan)
{
	int res = -1;
	struct asent *as;
	int needstart;
	AST_LIST_LOCK(&aslist);

	/* Check if autoservice thread is executing */
	needstart = (asthread == AST_PTHREADT_NULL) ? 1 : 0 ;

	/* Check if the channel already has autoservice */
	AST_LIST_TRAVERSE(&aslist, as, list) {
		if (as->chan == chan)
			break;
	}

	/* If not, start autoservice on channel */
	if (!as) {
		as = calloc(1, sizeof(struct asent));
		if (as) {
			as->chan = chan;
			AST_LIST_INSERT_HEAD(&aslist, as, list);
			res = 0;
			if (needstart) {
				if (ast_pthread_create(&asthread, NULL, autoservice_run, NULL)) {
					ast_log(LOG_WARNING, "Unable to create autoservice thread :(\n");
					/* There will only be a single member in the list at this point,
					   the one we just added. */
					AST_LIST_REMOVE(&aslist, as, list);
					free(as);
					res = -1;
				} else
					pthread_kill(asthread, SIGURG);
			}
		}
	}
	AST_LIST_UNLOCK(&aslist);
	return res;
}

int ast_autoservice_stop(struct ast_channel *chan)
{
	int res = -1;
	struct asent *as;

	AST_LIST_LOCK(&aslist);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&aslist, as, list) {	
		if (as->chan == chan) {
			AST_LIST_REMOVE_CURRENT(&aslist, list);
			free(as);
			if (!chan->_softhangup)
				res = 0;
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END

	if (asthread != AST_PTHREADT_NULL) 
		pthread_kill(asthread, SIGURG);
	AST_LIST_UNLOCK(&aslist);

	/* Wait for it to un-block */
	while(ast_test_flag(chan, AST_FLAG_BLOCKING))
		usleep(1000);
	return res;
}
