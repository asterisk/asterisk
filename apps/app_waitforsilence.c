/*
 * Asterisk -- A telephony toolkit for Linux.
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Wait for Silence
 *   - Waits for up to 'x' milliseconds of silence, 'y' times
 *   - WaitForSilence(500,2) will wait for 1/2 second of silence, twice
 *   - WaitForSilence(1000,1) will wait for 1 second of silence, once
 *
 * WaitForSilence Application by David C. Troy <dave@popvox.com>
 * Version 1.00 2004-01-29
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/dsp.h"
#include "asterisk/module.h"
#include "asterisk/options.h"

static char *tdesc = "Wait For Silence";
static char *app = "WaitForSilence";
static char *synopsis = "Waits for a specified amount of silence";
static char *descrip = 
"  WaitForSilence(x[|y]) Wait for Silence: Waits for up to 'x' \n"
"milliseconds of silence, 'y' times or 1 if omitted\n"
"Examples:\n"
"  - WaitForSilence(500,2) will wait for 1/2 second of silence, twice\n"
"  - WaitForSilence(1000) will wait for 1 second of silence, once\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int do_waiting(struct ast_channel *chan, int maxsilence) {

	struct ast_frame *f;
	int totalsilence = 0;
	int dspsilence = 0;
	int gotsilence = 0; 
	static int silencethreshold = 64;
	int rfmt = 0;
	int res = 0;
	struct ast_dsp *sildet;	 /* silence detector dsp */

	rfmt = chan->readformat; /* Set to linear mode */
	res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
		return -1;
	}

	sildet = ast_dsp_new(); /* Create the silence detector */
	if (!sildet) {
		ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
		return -1;
	}
	ast_dsp_set_threshold(sildet, silencethreshold);

	/* Await silence... */
	f = NULL;
	for(;;) {
		res = ast_waitfor(chan, 2000);
		if (!res) {
			ast_log(LOG_WARNING, "One waitfor failed, trying another\n");
			/* Try one more time in case of masq */
			res = ast_waitfor(chan, 2000);
			if (!res) {
				ast_log(LOG_WARNING, "No audio available on %s??\n", chan->name);
				res = -1;
			}
		}

		if (res < 0) {
			f = NULL;
			break;
		}
		f = ast_read(chan);
		if (!f)
			break;
		if (f->frametype == AST_FRAME_VOICE) {
			dspsilence = 0;
			ast_dsp_silence(sildet, f, &dspsilence);
			if (dspsilence)
				totalsilence = dspsilence;
			else
				totalsilence = 0;

			if (totalsilence >= maxsilence) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Exiting with %dms silence > %dms required\n", totalsilence, maxsilence);
				/* Ended happily with silence */
				ast_frfree(f);
				gotsilence = 1;
				break;
			}
		}
		ast_frfree(f);
	}
	if (rfmt && ast_set_read_format(chan, rfmt)) {
		ast_log(LOG_WARNING, "Unable to restore format %s to channel '%s'\n", ast_getformatname(rfmt), chan->name);
	}
	ast_dsp_free(sildet);
	return gotsilence;
}

static int waitforsilence_exec(struct ast_channel *chan, void *data)
{
	int res = 1;
	struct localuser *u;
	int maxsilence = 1000;
	int iterations = 1, i;

	res = ast_answer(chan); /* Answer the channel */

	if (!data || ((sscanf(data, "%d|%d", &maxsilence, &iterations) != 2) &&
		(sscanf(data, "%d", &maxsilence) != 1))) {
		ast_log(LOG_WARNING, "Using default value of 1000ms, 1 iteration\n");
	}

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Waiting %d time(s) for %d ms silence\n", iterations, maxsilence);
	LOCAL_USER_ADD(u);
	res = 1;
	for (i=0; (i<iterations) && (res == 1); i++) {
		res = do_waiting(chan, maxsilence);
	}
	LOCAL_USER_REMOVE(u);
	if (res > 0)
		res = 0;
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, waitforsilence_exec, synopsis, descrip);
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

