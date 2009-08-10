/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * WaitForSilence Application by David C. Troy <dave@popvox.com>
 * Version 1.11 2006-06-29
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
 * \brief Wait for Silence
 *   - Waits for up to 'x' milliseconds of silence, 'y' times \n
 *   - WaitForSilence(500,2) will wait for 1/2 second of silence, twice \n
 *   - WaitForSilence(1000,1) will wait for 1 second of silence, once \n
 *   - WaitForSilence(300,3,10) will wait for 300ms of silence, 3 times, and return after 10sec \n
 *
 * \author David C. Troy <dave@popvox.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/dsp.h"
#include "asterisk/module.h"
#include "asterisk/options.h"

static char *app = "WaitForSilence";
static char *synopsis = "Waits for a specified amount of silence";
static char *descrip =
"  WaitForSilence(silencerequired[|iterations][|timeout]) \n"
"Wait for Silence: Waits for up to 'silencerequired' \n"
"milliseconds of silence, 'iterations' times or once if omitted.\n"
"An optional timeout specified the number of seconds to return\n"
"after, even if we do not receive the specified amount of silence.\n"
"Use 'timeout' with caution, as it may defeat the purpose of this\n"
"application, which is to wait indefinitely until silence is detected\n"
"on the line.  This is particularly useful for reverse-911-type\n"
"call broadcast applications where you need to wait for an answering\n"
"machine to complete its spiel before playing a message.\n"
"The timeout parameter is specified only to avoid an infinite loop in\n"
"cases where silence is never achieved.  Typically you will want to\n"
"include two or more calls to WaitForSilence when dealing with an answering\n"
"machine; first waiting for the spiel to finish, then waiting for the beep, etc.\n\n"
  "Examples:\n"
"  - WaitForSilence(500|2) will wait for 1/2 second of silence, twice\n"
"  - WaitForSilence(1000) will wait for 1 second of silence, once\n"
"  - WaitForSilence(300|3|10) will wait for 300ms silence, 3 times,\n"
"     and returns after 10 sec, even if silence is not detected\n\n"
"Sets the channel variable WAITSTATUS with to one of these values:\n"
"SILENCE - if exited with silence detected\n"
"TIMEOUT - if exited without silence detected after timeout\n";

static int do_waiting(struct ast_channel *chan, int silencereqd, time_t waitstart, int timeout) {
	struct ast_frame *f;
	int dspsilence = 0;
	static int silencethreshold = 128;
	int rfmt = 0;
	int res = 0;
	struct ast_dsp *sildet;	 /* silence detector dsp */
 	time_t now;

	rfmt = chan->readformat; /* Set to linear mode */
	res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set channel to linear mode, giving up\n");
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
		/* Start with no silence received */
		dspsilence = 0;

		res = ast_waitfor(chan, silencereqd);

		/* Must have gotten a hangup; let's exit */
		if (res < 0) {
			f = NULL;
			break;
		}
		
		/* We waited and got no frame; sounds like digital silence or a muted digital channel */
		if (res == 0) {
			dspsilence = silencereqd;
		} else {
			/* Looks like we did get a frame, so let's check it out */
			f = ast_read(chan);
			if (!f)
				break;
			if (f && f->frametype == AST_FRAME_VOICE) {
				ast_dsp_silence(sildet, f, &dspsilence);
			}
			if (f) {
				ast_frfree(f);
			}
		}

		if (option_verbose > 6)
			ast_verbose(VERBOSE_PREFIX_3 "Got %dms silence< %dms required\n", dspsilence, silencereqd);

		if (dspsilence >= silencereqd) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Exiting with %dms silence >= %dms required\n", dspsilence, silencereqd);
			/* Ended happily with silence */
			res = 1;
			pbx_builtin_setvar_helper(chan, "WAITSTATUS", "SILENCE");
			ast_log(LOG_DEBUG, "WAITSTATUS was set to SILENCE\n");
			break;
		}

		if ( timeout && (difftime(time(&now),waitstart) >= timeout) ) {
			pbx_builtin_setvar_helper(chan, "WAITSTATUS", "TIMEOUT");
			ast_log(LOG_DEBUG, "WAITSTATUS was set to TIMEOUT\n");
			res = 0;
			break;
		}
	}


	if (rfmt && ast_set_read_format(chan, rfmt)) {
		ast_log(LOG_WARNING, "Unable to restore format %s to channel '%s'\n", ast_getformatname(rfmt), chan->name);
	}
	ast_dsp_free(sildet);
	return res;
}

static int waitforsilence_exec(struct ast_channel *chan, void *data)
{
	int res = 1;
	int silencereqd = 1000;
	int timeout = 0;
	int iterations = 1, i;
	time_t waitstart;

	res = ast_answer(chan); /* Answer the channel */

	if (!data || ( (sscanf(data, "%30d|%30d|%30d", &silencereqd, &iterations, &timeout) != 3) &&
		(sscanf(data, "%30d|%30d", &silencereqd, &iterations) != 2) &&
		(sscanf(data, "%30d", &silencereqd) != 1) ) ) {
		ast_log(LOG_WARNING, "Using default value of 1000ms, 1 iteration, no timeout\n");
	}

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Waiting %d time(s) for %d ms silence with %d timeout\n", iterations, silencereqd, timeout);

	time(&waitstart);
	res = 1;
	for (i=0; (i<iterations) && (res == 1); i++) {
		res = do_waiting(chan, silencereqd, waitstart, timeout);
	}
	if (res > 0)
		res = 0;
	return res;
}


static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	
	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	return ast_register_application(app, waitforsilence_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Wait For Silence");

