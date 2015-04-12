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

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/indications.h"

/*** DOCUMENTATION
	<application name="Morsecode" language="en_US">
		<synopsis>
			Plays morse code.
		</synopsis>
		<syntax>
			<parameter name="string" required="true">
				<para>String to playback as morse code to channel</para>
			</parameter>
		</syntax>
		<description>
			<para>Plays the Morse code equivalent of the passed string.</para>
			<para>This application does not automatically answer and should be preceeded by
			an application such as Answer() or Progress().</para>
			<para>This application uses the following variables:</para>
			<variablelist>
				<variable name="MORSEDITLEN">
					<para>Use this value in (ms) for length of dit</para>
				</variable>
				<variable name="MORSETONE">
					<para>The pitch of the tone in (Hz), default is 800</para>
				</variable>
			</variablelist>
		</description>
		<see-also>
			<ref type="application">SayAlpha</ref>
			<ref type="application">SayPhonetic</ref>
		</see-also>
	</application>
 ***/	
static const char app_morsecode[] = "Morsecode";

static const char * const morsecode[] = {
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
	"-..-.",  /* 47 - / */
	"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.", /* 48-57 - 0-9 */
	"---...", /* 58 - : */
	"-.-.-.", /* 59 - ; */
	"",       /* 60 - < */
	"-...-",  /* 61 - = */
	"",       /* 62 - > */
	"..--..", /* 63 - ? */
	".--.-.", /* 64 - @ */
	".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
	"-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
	"-.--.-", /* 91 - [ (really '(') */
	"-..-.",  /* 92 - \ (really '/') */
	"-.--.-", /* 93 - ] (really ')') */
	"",       /* 94 - ^ */
	"..--.-", /* 95 - _ */
	".----.", /* 96 - ` */
	".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
	"-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
	"-.--.-", /* 123 - { (really '(') */
	"",       /* 124 - | */
	"-.--.-", /* 125 - } (really ')') */
	"-..-.",  /* 126 - ~ (really bar) */
	". . .",  /* 127 - <del> (error) */
};

static void playtone(struct ast_channel *chan, int tone, int len)
{
	char dtmf[20];
	snprintf(dtmf, sizeof(dtmf), "%d/%d", tone, len);
	ast_playtones_start(chan, 0, dtmf, 0);
	ast_safe_sleep(chan, len);
	ast_playtones_stop(chan);
}

static int morsecode_exec(struct ast_channel *chan, const char *data)
{
	int res=0, ditlen, tone;
	const char *digit;
	const char *ditlenc, *tonec;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: Morsecode(<string>) - no argument found\n");
		return 0;
	}

	/* Use variable MORESEDITLEN, if set (else 80) */
	ast_channel_lock(chan);
	ditlenc = pbx_builtin_getvar_helper(chan, "MORSEDITLEN");
	if (ast_strlen_zero(ditlenc) || (sscanf(ditlenc, "%30d", &ditlen) != 1)) {
		ditlen = 80;
	}
	ast_channel_unlock(chan);

	/* Use variable MORSETONE, if set (else 800) */
	ast_channel_lock(chan);
	tonec = pbx_builtin_getvar_helper(chan, "MORSETONE");
	if (ast_strlen_zero(tonec) || (sscanf(tonec, "%30d", &tone) != 1)) {
		tone = 800;
	}
	ast_channel_unlock(chan);

	for (digit = data; *digit; digit++) {
		int digit2 = *digit;
		const char *dahdit;
		if (digit2 < 0) {
			continue;
		}
		for (dahdit = morsecode[digit2]; *dahdit; dahdit++) {
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

	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app_morsecode);
}

static int load_module(void)
{
	return ast_register_application_xml(app_morsecode, morsecode_exec);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Morse code");

