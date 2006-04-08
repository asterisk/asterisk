/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2006, Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_morsecode__v001@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief Morsecode application
 *
 * \author Tilghman Lesher <app_morsecode__v001@the-tilghman.com>
 *
 * \ingroup applications
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/indications.h"

static char *tdesc = "Morse code";

static char *app_morsecode = "Morsecode";

static char *morsecode_synopsis = "Plays morse code";

static char *morsecode_descrip =
"Usage: Morsecode(<string>)\n"
"Plays the Morse code equivalent of the passed string.  If the variable\n"
"MORSEDITLEN is set, it will use that value for the length (in ms) of the dit\n"
"(defaults to 80).  Additionally, if MORSETONE is set, it will use that tone\n"
"(in Hz).  The tone default is 800.\n";

LOCAL_USER_DECL;

static char *morsecode[] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", /*  0-15 */
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", /* 16-31 */
	" ",      /* 32 - <space> */
	".-.-.-", /* 33 - ! */
	".-..-.", /* 34 - " */
	"",       /* 35 - # */
	"",       /* 36 - $ */
	"",       /* 37 - % */
	"",       /* 38 - & */
	".----.", /* 39 - ' */
	"-.--.-", /* 40 - ( */
	"-.--.-", /* 41 - ) */
	"",       /* 42 - * */
	"",       /* 43 - + */
	"--..--", /* 44 - , */
	"-....-", /* 45 - - */
	".-.-.-", /* 46 - . */
	"-··-·",  /* 47 - / */
	"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.", /* 48-57 - 0-9 */
	"---...", /* 58 - : */
	"-·-·-·", /* 59 - ; */
	"",       /* 60 - < */
	"-...-",  /* 61 - = */
	"",       /* 62 - > */
	"..--..", /* 63 - ? */
	".--.-.", /* 64 - @ */
	".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
	"-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
	"-.--.-", /* 91 - [ (really '(') */
	"-··-·",  /* 92 - \ (really '/') */
	"-.--.-", /* 93 - ] (really ')') */
	"",       /* 94 - ^ */
	"··--·-", /* 95 - _ */
	".----.", /* 96 - ` */
	".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
	"-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
	"-.--.-", /* 123 - { (really '(') */
	"",       /* 124 - | */
	"-.--.-", /* 125 - } (really ')') */
	"-··-·",  /* 126 - ~ (really bar) */
	"· · ·",  /* 127 - <del> (error) */
};

static void playtone(struct ast_channel *chan, int tone, int len)
{
	char dtmf[20];
	snprintf(dtmf, sizeof(dtmf), "%d/%d", tone, len);
	ast_playtones_start(chan, 0, dtmf, 0);
	ast_safe_sleep(chan, len);
	ast_playtones_stop(chan);
}

static int morsecode_exec(struct ast_channel *chan, void *data)
{
	int res=0, ditlen, tone;
	char *digit;
	const char *ditlenc, *tonec;
	struct localuser *u;

	LOCAL_USER_ADD(u);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: Morsecode(<string>) - no argument found\n");
		LOCAL_USER_REMOVE(u);
		return 0;
	}

	/* Use variable MORESEDITLEN, if set (else 80) */
	ditlenc = pbx_builtin_getvar_helper(chan, "MORSEDITLEN");
	if (ast_strlen_zero(ditlenc) || (sscanf(ditlenc, "%d", &ditlen) != 1)) {
		ditlen = 80;
	}

	/* Use variable MORSETONE, if set (else 800) */
	tonec = pbx_builtin_getvar_helper(chan, "MORSETONE");
	if (ast_strlen_zero(tonec) || (sscanf(tonec, "%d", &tone) != 1)) {
		tone = 800;
	}

	for (digit = data; *digit; digit++) {
		char *dahdit;
		if (*digit < 0) {
			continue;
		}
		for (dahdit = morsecode[(int)*digit]; *dahdit; dahdit++) {
			if (*dahdit == '-') {
				playtone(chan, tone, 3 * ditlen);
			} else if (*dahdit == '.') {
				playtone(chan, tone, 1 * ditlen);
			} else {
				/* Account for ditlen of silence immediately following */
				playtone(chan, 0, 2 * ditlen);
			}

			/* Pause slightly between each dit and dah */
			playtone(chan, 0, 1 * ditlen);
		}
		/* Pause between characters */
		playtone(chan, 0, 2 * ditlen);
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app_morsecode);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	return ast_register_application(app_morsecode, morsecode_exec, morsecode_synopsis, morsecode_descrip);
}

const char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

const char *key()
{
	return ASTERISK_GPL_KEY;
}
