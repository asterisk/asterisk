/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2006, Tilghman Lesher.  All rights reserved.
 *
 * Updated by Naveen Albert <asterisk@phreaknet.org>
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
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

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
				<variable name="MORSESPACETONE">
					<para>The pitch of the spaces in (Hz), default is 0</para>
				</variable>
				<variable name="MORSETYPE">
					<para>The code type to use (AMERICAN for standard American Morse
					or INTERNATIONAL for international code.
					Default is INTERNATIONAL).</para>
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

static const char * const internationalcode[] = {
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
	".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--", /* A-M */
	"-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..", /* N-Z */
	"-.--.-", /* 91 - [ (really '(') */
	"-..-.",  /* 92 - \ (really '/') */
	"-.--.-", /* 93 - ] (really ')') */
	"",       /* 94 - ^ */
	"..--.-", /* 95 - _ */
	".----.", /* 96 - ` */
	".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--", /* a-m */
	"-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..", /* n-z */
	"-.--.-", /* 123 - { (really '(') */
	"",       /* 124 - | */
	"-.--.-", /* 125 - } (really ')') */
	"-..-.",  /* 126 - ~ (really bar) */
	". . .",  /* 127 - <del> (error) */
};

static const char * const americanmorsecode[] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", /*  0-15 */
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", /* 16-31 */
	"  ",    /* 32 - <space> */
	"---.",   /* 33 - ! */
	"..-. -.",/* 34 - " (QN)*/
	"",       /* 35 - # */
	"... .-..",/* 36 - $ (SX) */
	"",       /* 37 - % */
	". ...",  /* 38 - & (ES) */
	"..-. .-..",/* 39 - ' (QX) */
	"..... -.", /* 40 - ( (PN) */
	"..... .. ..", /* 41 - ) (PY) */
	"",       /* 42 - * */
	"",       /* 43 - + */
	".-.-",   /* 44 - , */
	".... .-..",/* 45 - (HX) */
	"..--..", /* 46 - . */
	"..- -",  /* 47 - / (UT) */
	".--.", "..-..", "...-.", "....-", "---", "......", "--..", "-....", "-..-", "0", /* 48-57 - 0-9 */
	"-.- . .",/* 58 - : (KO) */
	"... ..", /* 59 - ; */
	"",       /* 60 - < */
	"-...-",  /* 61 - = (paragraph mark) */
	"",       /* 62 - > */
	"-..-.",  /* 63 - ? */
	".--.-.", /* 64 - @ */
	".-", "-...", ".. .", "-..", ".", ".-.", "--.", "....", "..", ".-.-", "-.-", "L", "--", /* A-M */
	"-.", ". .", ".....", "..-.", ". ..", "...", "-", "..-", "...-", ".--", ".-..", ".. ..", "... .", /* N-Z */
	"..... -.", /* 91 - [ (really '(') */
	"..- -",  /* 92 - \ (really '/') */
	"..... .. ..", /* 93 - ] (really ')') */
	"",       /* 94 - ^ */
	"..--.-", /* 95 - _ */
	".----.", /* 96 - ` */
	".-", "-...", ".. .", "-..", ".", ".-.", "--.", "....", "..", ".-.-", "-.-", "L", "--", /* a-m */
	"-.", ". .", ".....", "..-.", ". ..", "...", "-", "..-", "...-", ".--", ".-..", ".. ..", "... .", /* n-z */
	"..... -.", /* 123 - { (really '(') */
	"",       /* 124 - | */
	"..... .. ..", /* 125 - } (really ')') */
	"..- -",  /* 126 - ~ (really bar) */
	". . .",  /* 127 - <del> (error) */
};

static int playtone(struct ast_channel *chan, int tone, int len)
{
	int res;
	char dtmf[20];
	snprintf(dtmf, sizeof(dtmf), "%d/%d", tone, len);
	ast_playtones_start(chan, 0, dtmf, 0);
	res = ast_safe_sleep(chan, len);
	ast_playtones_stop(chan);
	return res;
}

static int morsecode_exec(struct ast_channel *chan, const char *data)
{
	int res = 0, ditlen, tone, toneoff, digit2;
	const char *digit;
	const char *ditlenc, *tonec, *toneb, *codetype;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: Morsecode(<string>) - no argument found\n");
		return 0;
	}

	ast_channel_lock(chan);
	/* Use variable MORESEDITLEN, if set (else 80) */
	ditlenc = pbx_builtin_getvar_helper(chan, "MORSEDITLEN");
	if (ast_strlen_zero(ditlenc) || (sscanf(ditlenc, "%30d", &ditlen) != 1)) {
		ditlen = 80;
	}

	/* Use variable MORSETONE, if set (else 800) */
	tonec = pbx_builtin_getvar_helper(chan, "MORSETONE");
	if (ast_strlen_zero(tonec) || (sscanf(tonec, "%30d", &tone) != 1)) {
		tone = 800;
	}

	/* Use variable MORSESPACETONE, if set (else 0) */
	toneb = pbx_builtin_getvar_helper(chan, "MORSESPACETONE");
	if (ast_strlen_zero(toneb) || (sscanf(toneb, "%30d", &toneoff) != 1)) {
		toneoff = 0;
	}

	/* Use variable MORSETYPE, if set (else INTERNATIONAL) */
	codetype = pbx_builtin_getvar_helper(chan, "MORSETYPE");
	if (!codetype || strcmp(codetype, "AMERICAN")) {
		codetype = "INTERNATIONAL";
	}

	ast_channel_unlock(chan);
	if (!strcmp(codetype, "AMERICAN")) {
		for (digit = data; *digit; digit++) {
			const char *dahdit;
			digit2 = *digit;
			if (digit2 < 0 || digit2 > 127) {
				continue;
			}
			for (dahdit = americanmorsecode[digit2]; *dahdit; dahdit++) {
				if (*dahdit == '-') {
					res = playtone(chan, tone, 3 * ditlen);
				} else if (*dahdit == '.') {
					res = playtone(chan, tone, 1 * ditlen);
				} else if (*dahdit == 'L' || *dahdit == 'l') {
					res = playtone(chan, tone, 6 * ditlen); /* long dash */
				} else if (*dahdit == '0') {
					res = playtone(chan, tone, 9 * ditlen); /* extra long dash */
				} else if (*dahdit == ' ') { /* space char (x20) = 6 dot lengths */
					/* Intra-char pauses, specific to American Morse */
					res = playtone(chan, toneoff, 3 * ditlen);
				} else {
					/* Account for ditlen of silence immediately following */
					res = playtone(chan, toneoff, 2 * ditlen);
				}

				/* Pause slightly between each dit and dah */
				res = playtone(chan, toneoff, 1 * ditlen);
				if (res)
					break;
			}
			/* Pause between characters */
			res = playtone(chan, toneoff, 3 * ditlen);
			if (res)
				break;
		}
	} else { /* International */
		for (digit = data; *digit; digit++) {
			const char *dahdit;
			digit2 = *digit;
			if (digit2 < 0 || digit2 > 127) {
				continue;
			}
			for (dahdit = internationalcode[digit2]; *dahdit; dahdit++) {
				if (*dahdit == '-') {
					res = playtone(chan, tone, 3 * ditlen);
				} else if (*dahdit == '.') {
					res = playtone(chan, tone, 1 * ditlen);
				} else {
					/* Account for ditlen of silence immediately following */
					res = playtone(chan, toneoff, 2 * ditlen);
				}

				/* Pause slightly between each dit and dah */
				res = playtone(chan, toneoff, 1 * ditlen);
				if (res)
					break;
			}
			/* Pause between characters */
			res = playtone(chan, toneoff, 2 * ditlen);
			if (res)
				break;
		}
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
