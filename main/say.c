/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * George Konstantoulakis <gkon@inaccessnetworks.com>
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
 * \brief Say numbers and dates (maybe words one day too)
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \note 12-16-2004 : Support for Greek added by InAccess Networks (work funded by HOL, www.hol.gr) George Konstantoulakis <gkon@inaccessnetworks.com>
 *
 * \note 2007-02-08 : Support for Georgian added by Alexander Shaduri <ashaduri@gmail.com>,
 *						Next Generation Networks (NGN).
 * \note 2007-03-20 : Support for Thai added by Dome C. <dome@tel.co.th>,
 *						IP Crossing Co., Ltd.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include <netinet/in.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

#ifdef SOLARIS
#include <iso/limits_iso.h>
#endif

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/say.h"
#include "asterisk/lock.h"
#include "asterisk/localtime.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/test.h"

/* Forward declaration */
static int wait_file(struct ast_channel *chan, const char *ints, const char *file, const char *lang);


static int say_character_str_full(struct ast_channel *chan, const char *str, const char *ints, const char *lang, enum ast_say_case_sensitivity sensitivity, int audiofd, int ctrlfd)
{
	const char *fn;
	char fnbuf[10], asciibuf[20] = "letters/ascii";
	char ltr;
	int num = 0;
	int res = 0;
	int upper = 0;
	int lower = 0;

	while (str[num] && !res) {
		fn = NULL;
		switch (str[num]) {
		case ('*'):
			fn = "digits/star";
			break;
		case ('#'):
			fn = "digits/pound";
			break;
		case ('!'):
			fn = "letters/exclaimation-point";
			break;
		case ('@'):
			fn = "letters/at";
			break;
		case ('$'):
			fn = "letters/dollar";
			break;
		case ('-'):
			fn = "letters/dash";
			break;
		case ('.'):
			fn = "letters/dot";
			break;
		case ('='):
			fn = "letters/equals";
			break;
		case ('+'):
			fn = "letters/plus";
			break;
		case ('/'):
			fn = "letters/slash";
			break;
		case (' '):
			fn = "letters/space";
			break;
		case ('0'):
		case ('1'):
		case ('2'):
		case ('3'):
		case ('4'):
		case ('5'):
		case ('6'):
		case ('7'):
		case ('8'):
		case ('9'):
			strcpy(fnbuf, "digits/X");
			fnbuf[7] = str[num];
			fn = fnbuf;
			break;
		default:
			ltr = str[num];
			if ('A' <= ltr && ltr <= 'Z') {
				ltr += 'a' - 'A';		/* file names are all lower-case */
				switch (sensitivity) {
				case AST_SAY_CASE_UPPER:
				case AST_SAY_CASE_ALL:
					upper = !upper;
				case AST_SAY_CASE_LOWER:
				case AST_SAY_CASE_NONE:
					break;
				}
			} else if ('a' <= ltr && ltr <= 'z') {
				switch (sensitivity) {
				case AST_SAY_CASE_LOWER:
				case AST_SAY_CASE_ALL:
					lower = !lower;
				case AST_SAY_CASE_UPPER:
				case AST_SAY_CASE_NONE:
					break;
				}
			}

			if (upper) {
				strcpy(fnbuf, "uppercase");
			} else if (lower) {
				strcpy(fnbuf, "lowercase");
			} else {
				strcpy(fnbuf, "letters/X");
				fnbuf[8] = ltr;
			}
			fn = fnbuf;
		}
		if ((fn && ast_fileexists(fn, NULL, lang) > 0) ||
			(snprintf(asciibuf + 13, sizeof(asciibuf) - 13, "%d", str[num]) > 0 && ast_fileexists(asciibuf, NULL, lang) > 0 && (fn = asciibuf))) {
			res = ast_streamfile(chan, fn, lang);
			if (!res) {
				if ((audiofd  > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
		if (upper || lower) {
			continue;
		}
		num++;
	}

	return res;
}

static int say_phonetic_str_full(struct ast_channel *chan, const char *str, const char *ints, const char *lang, int audiofd, int ctrlfd)
{
	const char *fn;
	char fnbuf[256];
	char ltr;
	int num = 0;
	int res = 0;

	while (str[num] && !res) {
		fn = NULL;
		switch (str[num]) {
		case ('*'):
			fn = "digits/star";
			break;
		case ('#'):
			fn = "digits/pound";
			break;
		case ('!'):
			fn = "letters/exclaimation-point";
			break;
		case ('@'):
			fn = "letters/at";
			break;
		case ('$'):
			fn = "letters/dollar";
			break;
		case ('-'):
			fn = "letters/dash";
			break;
		case ('.'):
			fn = "letters/dot";
			break;
		case ('='):
			fn = "letters/equals";
			break;
		case ('+'):
			fn = "letters/plus";
			break;
		case ('/'):
			fn = "letters/slash";
			break;
		case (' '):
			fn = "letters/space";
			break;
		case ('0'):
		case ('1'):
		case ('2'):
		case ('3'):
		case ('4'):
		case ('5'):
		case ('6'):
		case ('7'):
		case ('8'):
			strcpy(fnbuf, "digits/X");
			fnbuf[7] = str[num];
			fn = fnbuf;
			break;
		default:	/* '9' falls here... */
			ltr = str[num];
			if ('A' <= ltr && ltr <= 'Z') ltr += 'a' - 'A';		/* file names are all lower-case */
			strcpy(fnbuf, "phonetic/X_p");
			fnbuf[9] = ltr;
			fn = fnbuf;
		}
		if (fn && ast_fileexists(fn, NULL, lang) > 0) {
			res = ast_streamfile(chan, fn, lang);
			if (!res) {
				if ((audiofd  > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
		num++;
	}

	return res;
}

static int say_digit_str_full(struct ast_channel *chan, const char *str, const char *ints, const char *lang, int audiofd, int ctrlfd)
{
	const char *fn;
	char fnbuf[256];
	int num = 0;
	int res = 0;

	while (str[num] && !res) {
		fn = NULL;
		switch (str[num]) {
		case ('*'):
			fn = "digits/star";
			break;
		case ('#'):
			fn = "digits/pound";
			break;
		case ('-'):
			fn = "digits/minus";
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			strcpy(fnbuf, "digits/X");
			fnbuf[7] = str[num];
			fn = fnbuf;
			break;
		}
		if (fn && ast_fileexists(fn, NULL, lang) > 0) {
			res = ast_streamfile(chan, fn, lang);
			if (!res) {
				if ((audiofd  > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
		num++;
	}

	return res;
}

/* Forward declarations */
/*! \page Def_syntaxlang Asterisk Language Syntaxes supported
    \note Not really language codes.
	For these language codes, Asterisk will change the syntax when
	saying numbers (and in some cases dates and voicemail messages
	as well)
      \arg \b da    - Danish
      \arg \b de    - German
      \arg \b en    - English (US)
      \arg \b en_GB - English (British)
      \arg \b es    - Spanish, Mexican
      \arg \b fr    - French
      \arg \b he    - Hebrew
      \arg \b it    - Italian
      \arg \b nl    - Dutch
      \arg \b no    - Norwegian
      \arg \b pl    - Polish
      \arg \b pt    - Portuguese
      \arg \b pt_BR - Portuguese (Brazil)
      \arg \b se    - Swedish
      \arg \b zh    - Taiwanese / Chinese
      \arg \b ru    - Russian
      \arg \b ka    - Georgian
      \arg \b hu    - Hungarian

 \par Gender:
 For Some languages the numbers differ for gender and plural.
 \arg Use the option argument 'f' for female, 'm' for male and 'n' for neuter in languages like Portuguese, French, Spanish and German.
 \arg use the option argument 'c' is for commune and 'n' for neuter gender in nordic languages like Danish, Swedish and Norwegian.
 use the option argument 'p' for plural enumerations like in German

 Date/Time functions currently have less languages supported than saynumber().

 \todo Note that in future, we need to move to a model where we can differentiate further - e.g. between en_US & en_UK

 See contrib/i18n.testsuite.conf for some examples of the different syntaxes

 \par Portuguese
 Portuguese sound files needed for Time/Date functions:
 pt-ah
 pt-ao
 pt-de
 pt-e
 pt-ora
 pt-meianoite
 pt-meiodia
 pt-sss

 \par Spanish
 Spanish sound files needed for Time/Date functions:
 es-de
 es-el

 \par Italian
 Italian sound files needed for Time/Date functions:
 ore-una
 ore-mezzanotte

*/

/* Forward declarations of language specific variants of ast_say_number_full */
static int ast_say_number_full_en(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_cs(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_da(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_de(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_en_GB(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_es(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_fr(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_he(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_it(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_nl(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_no(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_pl(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_pt(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_se(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_zh(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_gr(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_ja(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_ru(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_ka(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_hu(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_th(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_ur(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_vi(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);

/* Forward declarations of language specific variants of ast_say_enumeration_full */
static int ast_say_enumeration_full_en(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_enumeration_full_da(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_enumeration_full_de(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_enumeration_full_he(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_enumeration_full_vi(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);

/* Forward declarations of ast_say_date, ast_say_datetime and ast_say_time functions */
static int ast_say_date_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_da(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_ja(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_ka(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_hu(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_th(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_he(struct ast_channel *chan, time_t t, const char *ints, const char *lang);

static int ast_say_date_with_format_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_da(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_es(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_he(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_it(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_pl(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_zh(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_ja(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_th(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);
static int ast_say_date_with_format_vi(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone);

static int ast_say_time_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_pt_BR(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_zh(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_ja(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_ka(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_hu(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_th(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_he(struct ast_channel *chan, time_t t, const char *ints, const char *lang);

static int ast_say_datetime_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_pt_BR(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_zh(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_ja(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_ka(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_hu(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_th(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_he(struct ast_channel *chan, time_t t, const char *ints, const char *lang);

static int ast_say_datetime_from_now_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_from_now_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_from_now_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_from_now_ka(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_from_now_he(struct ast_channel *chan, time_t t, const char *ints, const char *lang);

static int wait_file(struct ast_channel *chan, const char *ints, const char *file, const char *lang)
{
	int res;
	if ((res = ast_streamfile(chan, file, lang))) {
		ast_log(LOG_WARNING, "Unable to play message %s\n", file);
	}
	if (!res) {
		res = ast_waitstream(chan, ints);
	}
	return res;
}

/*! \brief  ast_say_number_full: call language-specific functions
     \note Called from AGI */
static int say_number_full(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	ast_test_suite_event_notify("SAYNUM", "Message: saying number %d\r\nNumber: %d\r\nChannel: %s", num, num, ast_channel_name(chan));
	if (!strncasecmp(language, "en_GB", 5)) {     /* British syntax */
	   return ast_say_number_full_en_GB(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "en", 2)) { /* English syntax */
	   return ast_say_number_full_en(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "cs", 2)) { /* Czech syntax */
	   return ast_say_number_full_cs(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "cz", 2)) { /* deprecated Czech syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "cz is not a standard language code.  Please switch to using cs instead.\n");
		}
		return ast_say_number_full_cs(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "da", 2)) { /* Danish syntax */
	   return ast_say_number_full_da(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "de", 2)) { /* German syntax */
	   return ast_say_number_full_de(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "es", 2)) { /* Spanish syntax */
	   return ast_say_number_full_es(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "fr", 2)) { /* French syntax */
	   return ast_say_number_full_fr(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "ge", 2)) { /* deprecated Georgian syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "ge is not a standard language code.  Please switch to using ka instead.\n");
		}
		return ast_say_number_full_ka(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "gr", 2)) { /* Greek syntax */
	   return ast_say_number_full_gr(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "ja", 2)) { /* Japanese syntax */
	   return ast_say_number_full_ja(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "he", 2)) { /* Hebrew syntax */
	   return ast_say_number_full_he(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "hu", 2)) { /* Hungarian syntax */
		return ast_say_number_full_hu(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "it", 2)) { /* Italian syntax */
	   return ast_say_number_full_it(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "ka", 2)) { /* Georgian syntax */
	   return ast_say_number_full_ka(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "mx", 2)) { /* deprecated Mexican syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "mx is not a standard language code.  Please switch to using es_MX instead.\n");
		}
		return ast_say_number_full_es(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "nl", 2)) { /* Dutch syntax */
	   return ast_say_number_full_nl(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "no", 2)) { /* Norwegian syntax */
	   return ast_say_number_full_no(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "pl", 2)) { /* Polish syntax */
	   return ast_say_number_full_pl(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "pt", 2)) { /* Portuguese syntax */
	   return ast_say_number_full_pt(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "ru", 2)) { /* Russian syntax */
	   return ast_say_number_full_ru(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "se", 2)) { /* Swedish syntax */
	   return ast_say_number_full_se(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "th", 2)) { /* Thai syntax */
		return ast_say_number_full_th(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "tw", 2)) { /* deprecated Taiwanese syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "tw is a standard language code for Twi, not Taiwanese.  Please switch to using zh_TW instead.\n");
		}
		return ast_say_number_full_zh(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "zh", 2)) { /* Taiwanese / Chinese syntax */
	   return ast_say_number_full_zh(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "ur", 2)) { /* Urdu syntax */
		return ast_say_number_full_ur(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "vi", 2)) { /* Vietnamese syntax */
		return ast_say_number_full_vi(chan, num, ints, language, audiofd, ctrlfd);
	}

	/* Default to english */
	return ast_say_number_full_en(chan, num, ints, language, audiofd, ctrlfd);
}

/*! \brief  ast_say_number_full_en: English syntax
	\note This is the default syntax, if no other syntax defined in this file is used */
static int ast_say_number_full_en(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	while (!res && (num || playh)) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playh) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			playh = 0;
		} else	if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else	if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num %= 10;
		} else {
			if (num < 1000){
				snprintf(fn, sizeof(fn), "digits/%d", (num/100));
				playh++;
				num %= 100;
			} else {
				if (num < 1000000) { /* 1,000,000 */
					res = ast_say_number_full_en(chan, num / 1000, ints, language, audiofd, ctrlfd);
					if (res)
						return res;
					num %= 1000;
					snprintf(fn, sizeof(fn), "digits/thousand");
				} else {
					if (num < 1000000000) {	/* 1,000,000,000 */
						res = ast_say_number_full_en(chan, num / 1000000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						num %= 1000000;
						ast_copy_string(fn, "digits/million", sizeof(fn));
					} else {
						ast_debug(1, "Number '%d' is too big for me\n", num);
						res = -1;
					}
				}
			}
		}
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd  > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

static int exp10_int(int power)
{
	int x, res= 1;
	for (x=0;x<power;x++)
		res *= 10;
	return res;
}

/*! \brief  ast_say_number_full_cs: Czech syntax
 *
 * files needed:
 * - 1m,2m - gender male
 * - 1w,2w - gender female
 * - 3,4,...,20
 * - 30,40,...,90
 *
 * - hundereds - 100 - sto, 200 - 2ste, 300,400 3,4sta, 500,600,...,900 5,6,...9set
 *
 * for each number 10^(3n + 3) exist 3 files represented as:
 *		1 tousand = jeden tisic = 1_E3
 *		2,3,4 tousands = dva,tri,ctyri tisice = 2-3_E3
 *		5,6,... tousands = pet,sest,... tisic = 5_E3
 *
 *		million = _E6
 *		miliard = _E9
 *		etc...
 *
 * tousand, milion are  gender male, so 1 and 2 is 1m 2m
 * miliard is gender female, so 1 and 2 is 1w 2w
 */
static int ast_say_number_full_cs(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";

	int hundered = 0;
	int left = 0;
	int length = 0;

	/* options - w = woman, m = man, n = neutral. Defaultl is woman */
	if (!options)
		options = "w";

	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	while (!res && (num || playh)) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (num < 3 ) {
			snprintf(fn, sizeof(fn), "digits/%d%c", num, options[0]);
			playh = 0;
			num = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			playh = 0;
			num = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num %= 10;
		} else if (num < 1000) {
			hundered = num / 100;
			if ( hundered == 1 ) {
				ast_copy_string(fn, "digits/1sto", sizeof(fn));
			} else if ( hundered == 2 ) {
				ast_copy_string(fn, "digits/2ste", sizeof(fn));
			} else {
				res = ast_say_number_full_cs(chan, hundered, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
				if (hundered == 3 || hundered == 4) {
					ast_copy_string(fn, "digits/sta", sizeof(fn));
				} else if ( hundered > 4 ) {
					ast_copy_string(fn, "digits/set", sizeof(fn));
				}
			}
			num -= (hundered * 100);
		} else { /* num > 1000 */
			length = (int)log10(num)+1;
			while ( (length % 3 ) != 1 ) {
				length--;
			}
			left = num / (exp10_int(length-1));
			if ( left == 2 ) {
				switch (length-1) {
					case 9: options = "w";  /* 1,000,000,000 gender female */
						break;
					default : options = "m"; /* others are male */
				}
			}
			if ( left > 1 )	{ /* we don't say "one thousand" but only thousand */
				res = ast_say_number_full_cs(chan, left, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
			}
			if ( left >= 5 ) { /* >= 5 have the same declesion */
				snprintf(fn, sizeof(fn), "digits/5_E%d", length - 1);
			} else if ( left >= 2 && left <= 4 ) {
				snprintf(fn, sizeof(fn), "digits/2-4_E%d", length - 1);
			} else { /* left == 1 */
				snprintf(fn, sizeof(fn), "digits/1_E%d", length - 1);
			}
			num -= left * (exp10_int(length-1));
		}
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1)) {
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				} else {
					res = ast_waitstream(chan, ints);
				}
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*! \brief  ast_say_number_full_da: Danish syntax
 New files:
 - In addition to English, the following sounds are required: "1N", "millions", "and" and "1-and" through "9-and"
 */
static int ast_say_number_full_da(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playa = 0;
	int cn = 1;		/* +1 = commune; -1 = neuter */
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	if (options && !strncasecmp(options, "n", 1)) cn = -1;

	while (!res && (num || playh || playa )) {
		/* The grammar for Danish numbers is the same as for English except
		* for the following:
		* - 1 exists in both commune ("en", file "1N") and neuter ("et", file "1")
		* - numbers 20 through 99 are said in reverse order, i.e. 21 is
		*   "one-and twenty" and 68 is "eight-and sixty".
		* - "million" is different in singular and plural form
		* - numbers > 1000 with zero as the third digit from last have an
		*   "and" before the last two digits, i.e. 2034 is "two thousand and
		*   four-and thirty" and 1000012 is "one million and twelve".
		*/
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playh) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			playh = 0;
		} else if (playa) {
			ast_copy_string(fn, "digits/and", sizeof(fn));
			playa = 0;
		} else if (num == 1 && cn == -1) {
			ast_copy_string(fn, "digits/1N", sizeof(fn));
			num = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {
			int ones = num % 10;
			if (ones) {
				snprintf(fn, sizeof(fn), "digits/%d-and", ones);
				num -= ones;
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			}
		} else {
			if (num < 1000) {
				int hundreds = num / 100;
				if (hundreds == 1)
					ast_copy_string(fn, "digits/1N", sizeof(fn));
				else
					snprintf(fn, sizeof(fn), "digits/%d", (num / 100));

				playh++;
				num -= 100 * hundreds;
				if (num)
					playa++;

			} else {
				if (num < 1000000) {
					res = ast_say_number_full_da(chan, num / 1000, ints, language, "n", audiofd, ctrlfd);
					if (res)
						return res;
					num = num % 1000;
					ast_copy_string(fn, "digits/thousand", sizeof(fn));
				} else {
					if (num < 1000000000) {
						int millions = num / 1000000;
						res = ast_say_number_full_da(chan, millions, ints, language, "c", audiofd, ctrlfd);
						if (res)
							return res;
						if (millions == 1)
							ast_copy_string(fn, "digits/million", sizeof(fn));
						else
							ast_copy_string(fn, "digits/millions", sizeof(fn));
						num = num % 1000000;
					} else {
						ast_debug(1, "Number '%d' is too big for me\n", num);
						res = -1;
					}
				}
				if (num && num < 100)
					playa++;
			}
		}
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*! \brief  ast_say_number_full_de: German syntax

 New files:
 In addition to English, the following sounds are required:
 - "millions"
 - "1-and" through "9-and"
 - "1F" (eine)
 - "1N" (ein)
 - NB "1" is recorded as 'eins'
 */
static int ast_say_number_full_de(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0, t = 0;
	int mf = 1;                            /* +1 = male and neuter; -1 = female */
	char fn[256] = "";
	char fna[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	if (options && (!strncasecmp(options, "f", 1)))
		mf = -1;

	while (!res && num) {
		/* The grammar for German numbers is the same as for English except
		* for the following:
		* - numbers 20 through 99 are said in reverse order, i.e. 21 is
		*   "one-and twenty" and 68 is "eight-and sixty".
		* - "one" varies according to gender
		* - 100 is 'hundert', however all other instances are 'ein hundert'
		* - 1000 is 'tausend', however all other instances are 'ein tausend'
		* - 1000000 is always 'eine million'
		* - "million" is different in singular and plural form
		* - 'and' should not go between a hundreds place value and any
		*   tens/ones place values that follows it. i.e 136 is ein hundert
		*   sechs und dreizig, not ein hundert und sechs und dreizig.
		*/
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (num == 1 && mf == -1) {
			snprintf(fn, sizeof(fn), "digits/%dF", num);
			num = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {
			int ones = num % 10;
			if (ones) {
				snprintf(fn, sizeof(fn), "digits/%d-and", ones);
				num -= ones;
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			}
		} else if (num == 100 && t == 0) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			num = 0;
		} else if (num < 1000) {
			int hundreds = num / 100;
			num = num % 100;
			if (hundreds == 1) {
				ast_copy_string(fn, "digits/1N", sizeof(fn));
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", hundreds);
			}
			ast_copy_string(fna, "digits/hundred", sizeof(fna));
		} else if (num == 1000 && t == 0) {
			ast_copy_string(fn, "digits/thousand", sizeof(fn));
			num = 0;
		} else if (num < 1000000) {
			int thousands = num / 1000;
			num = num % 1000;
			t = 1;
			if (thousands == 1) {
				ast_copy_string(fn, "digits/1N", sizeof(fn));
				ast_copy_string(fna, "digits/thousand", sizeof(fna));
			} else {
				res = ast_say_number_full_de(chan, thousands, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
				ast_copy_string(fn, "digits/thousand", sizeof(fn));
			}
		} else if (num < 1000000000) {
			int millions = num / 1000000;
			num = num % 1000000;
			t = 1;
			if (millions == 1) {
				ast_copy_string(fn, "digits/1F", sizeof(fn));
				ast_copy_string(fna, "digits/million", sizeof(fna));
			} else {
				res = ast_say_number_full_de(chan, millions, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
				ast_copy_string(fn, "digits/millions", sizeof(fn));
			}
		} else if (num <= INT_MAX) {
			int billions = num / 1000000000;
			num = num % 1000000000;
			t = 1;
			if (billions == 1) {
				ast_copy_string(fn, "digits/1F", sizeof(fn));
				ast_copy_string(fna, "digits/milliard", sizeof(fna));
			} else {
				res = ast_say_number_full_de(chan, billions, ints, language, options, audiofd, ctrlfd);
				if (res) {
					return res;
				}
				ast_copy_string(fn, "digits/milliards", sizeof(fn));
			}
		} else {
			ast_debug(1, "Number '%d' is too big for me\n", num);
			res = -1;
		}
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
			if (!res) {
				if (strlen(fna) != 0 && !ast_streamfile(chan, fna, language)) {
					if ((audiofd > -1) && (ctrlfd > -1))
						res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
					else
						res = ast_waitstream(chan, ints);
				}
				ast_stopstream(chan);
				strcpy(fna, "");
			}
		}
	}
	return res;
}

/*! \brief  ast_say_number_full_en_GB: British syntax
 New files:
  - In addition to American English, the following sounds are required:  "and"
 */
static int ast_say_number_full_en_GB(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playa = 0;
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	while (!res && (num || playh || playa )) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playh) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			playh = 0;
		} else if (playa) {
			ast_copy_string(fn, "digits/and", sizeof(fn));
			playa = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num %= 10;
		} else if (num < 1000) {
			int hundreds = num / 100;
			snprintf(fn, sizeof(fn), "digits/%d", (num / 100));

			playh++;
			num -= 100 * hundreds;
			if (num)
				playa++;
		} else if (num < 1000000) {
			res = ast_say_number_full_en_GB(chan, num / 1000, ints, language, audiofd, ctrlfd);
			if (res)
				return res;
			ast_copy_string(fn, "digits/thousand", sizeof(fn));
			num %= 1000;
			if (num && num < 100)
				playa++;
		} else if (num < 1000000000) {
				int millions = num / 1000000;
				res = ast_say_number_full_en_GB(chan, millions, ints, language, audiofd, ctrlfd);
				if (res)
					return res;
				ast_copy_string(fn, "digits/million", sizeof(fn));
				num %= 1000000;
				if (num && num < 100)
					playa++;
		} else {
				ast_debug(1, "Number '%d' is too big for me\n", num);
				res = -1;
		}

		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*! \brief  ast_say_number_full_es: Spanish syntax

 New files:
 Requires a few new audios:
   1F.gsm: feminine 'una'
   21.gsm thru 29.gsm, cien.gsm, mil.gsm, millon.gsm, millones.gsm, 100.gsm, 200.gsm, 300.gsm, 400.gsm, 500.gsm, 600.gsm, 700.gsm, 800.gsm, 900.gsm, y.gsm
 */
static int ast_say_number_full_es(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playa = 0;
	int mf = 0;                            /* +1 = male; -1 = female */
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	if (options) {
		if (!strncasecmp(options, "f", 1))
			mf = -1;
		else if (!strncasecmp(options, "m", 1))
			mf = 1;
	}

	while (!res && num) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playa) {
			ast_copy_string(fn, "digits/and", sizeof(fn));
			playa = 0;
		} else if (num == 1) {
			if (mf < 0)
				snprintf(fn, sizeof(fn), "digits/%dF", num);
			else if (mf > 0)
				snprintf(fn, sizeof(fn), "digits/%dM", num);
			else
				snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 31) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num/10)*10);
			num %= 10;
			if (num)
				playa++;
		} else if (num == 100) {
			ast_copy_string(fn, "digits/100", sizeof(fn));
			num = 0;
		} else if (num < 200) {
			ast_copy_string(fn, "digits/100-and", sizeof(fn));
			num -= 100;
		} else {
			if (num < 1000) {
				snprintf(fn, sizeof(fn), "digits/%d", (num/100)*100);
				num %= 100;
			} else if (num < 2000) {
				num %= 1000;
				ast_copy_string(fn, "digits/thousand", sizeof(fn));
			} else {
				if (num < 1000000) {
					res = ast_say_number_full_es(chan, num / 1000, ints, language, options, audiofd, ctrlfd);
					if (res)
						return res;
					num %= 1000;
					ast_copy_string(fn, "digits/thousand", sizeof(fn));
				} else {
					if (num < 2147483640) {
						if ((num/1000000) == 1) {
							res = ast_say_number_full_es(chan, num / 1000000, ints, language, "M", audiofd, ctrlfd);
							if (res)
								return res;
							ast_copy_string(fn, "digits/million", sizeof(fn));
						} else {
							res = ast_say_number_full_es(chan, num / 1000000, ints, language, options, audiofd, ctrlfd);
							if (res)
								return res;
							ast_copy_string(fn, "digits/millions", sizeof(fn));
						}
						num %= 1000000;
					} else {
						ast_debug(1, "Number '%d' is too big for me\n", num);
						res = -1;
					}
				}
			}
		}

		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);

		}

	}
	return res;
}

/*! \brief  ast_say_number_full_fr: French syntax
	Extra sounds needed:
	1F: feminin 'une'
	et: 'and' */
static int ast_say_number_full_fr(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playa = 0;
	int mf = 1;                            /* +1 = male; -1 = female */
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	if (options && !strncasecmp(options, "f", 1))
		mf = -1;

	while (!res && (num || playh || playa)) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playh) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			playh = 0;
		} else if (playa) {
			ast_copy_string(fn, "digits/et", sizeof(fn));
			playa = 0;
		} else if (num == 1) {
			if (mf < 0)
				snprintf(fn, sizeof(fn), "digits/%dF", num);
			else
				snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 21) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 70) {
			snprintf(fn, sizeof(fn), "digits/%d", (num/10)*10);
			if ((num % 10) == 1) playa++;
			num = num % 10;
		} else if (num < 80) {
			ast_copy_string(fn, "digits/60", sizeof(fn));
			if ((num % 10) == 1) playa++;
			num -= 60;
		} else if (num < 100) {
			ast_copy_string(fn, "digits/80", sizeof(fn));
			num = num - 80;
		} else if (num < 200) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			num = num - 100;
		} else if (num < 1000) {
			snprintf(fn, sizeof(fn), "digits/%d", (num/100));
			playh++;
			num = num % 100;
		} else if (num < 2000) {
			ast_copy_string(fn, "digits/thousand", sizeof(fn));
			num = num - 1000;
		} else if (num < 1000000) {
			res = ast_say_number_full_fr(chan, num / 1000, ints, language, options, audiofd, ctrlfd);
			if (res)
				return res;
			ast_copy_string(fn, "digits/thousand", sizeof(fn));
			num = num % 1000;
		} else	if (num < 1000000000) {
			res = ast_say_number_full_fr(chan, num / 1000000, ints, language, options, audiofd, ctrlfd);
			if (res)
				return res;
			ast_copy_string(fn, "digits/million", sizeof(fn));
			num = num % 1000000;
		} else {
			ast_debug(1, "Number '%d' is too big for me\n", num);
			res = -1;
		}
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}



/* Hebrew syntax
 * Check doc/lang/hebrew-digits.txt for information about the various
 * recordings required to make this translation work properly */
#define SAY_NUM_BUF_SIZE 256
static int ast_say_number_full_he(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int state = 0;				/* no need to save anything */
	int mf = -1;				/* +1 = Masculin; -1 = Feminin */
	int tmpnum = 0;

	char fn[SAY_NUM_BUF_SIZE] = "";

	ast_verb(3, "ast_say_digits_full: started. num: %d, options=\"%s\"\n", num, options);

	if (!num) {
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);
	}
	if (options && !strncasecmp(options, "m", 1)) {
		mf = 1;
	}
	ast_verb(3, "ast_say_digits_full: num: %d, state=%d, options=\"%s\", mf=%d\n", num, state, options, mf);

	/* Do we have work to do? */
	while (!res && (num || (state > 0))) {
		/* first type of work: play a second sound. In this loop
		 * we can only play one sound file at a time. Thus playing
		 * a second one requires repeating the loop just for the
		 * second file. The variable 'state' remembers where we were.
		 * state==0 is the normal mode and it means that we continue
		 * to check if the number num has yet anything left.
		 */
		ast_verb(3, "ast_say_digits_full: num: %d, state=%d, options=\"%s\", mf=%d, tmpnum=%d\n", num, state, options, mf, tmpnum);

		if (state == 1) {
			state = 0;
		} else if (state == 2) {
			if ((num >= 11) && (num < 21)) {
				if (mf < 0) {
					snprintf(fn, sizeof(fn), "digits/ve");
				} else {
					snprintf(fn, sizeof(fn), "digits/uu");
				}
			} else {
				switch (num) {
				case 1:
					snprintf(fn, sizeof(fn), "digits/ve");
					break;
				case 2:
					snprintf(fn, sizeof(fn), "digits/uu");
					break;
				case 3:
					if (mf < 0) {
						snprintf(fn, sizeof(fn), "digits/ve");
					} else {
						snprintf(fn, sizeof(fn), "digits/uu");
					}
					break;
				case 4:
					snprintf(fn, sizeof(fn), "digits/ve");
					break;
				case 5:
					snprintf(fn, sizeof(fn), "digits/ve");
					break;
				case 6:
					snprintf(fn, sizeof(fn), "digits/ve");
					break;
				case 7:
					snprintf(fn, sizeof(fn), "digits/ve");
					break;
				case 8:
					snprintf(fn, sizeof(fn), "digits/uu");
					break;
				case 9:
					snprintf(fn, sizeof(fn), "digits/ve");
					break;
				case 10:
					snprintf(fn, sizeof(fn), "digits/ve");
					break;
				}
			}
			state = 0;
		} else if (state == 3) {
			snprintf(fn, sizeof(fn), "digits/1k");
			state = 0;
		} else if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
			num = (-1) * num;
		} else if (num < 20) {
			if (mf < 0) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
			} else {
				snprintf(fn, sizeof(fn), "digits/%dm", num);
			}
			num = 0;
		} else if ((num < 100) && (num >= 20)) {
			snprintf(fn, sizeof(fn), "digits/%d", (num / 10) * 10);
			num = num % 10;
			if (num > 0) {
				state = 2;
			}
		} else if ((num >= 100) && (num < 1000)) {
			tmpnum = num / 100;
			snprintf(fn, sizeof(fn), "digits/%d00", tmpnum);
			num = num - (tmpnum * 100);
			if ((num > 0) && (num < 11)) {
				state = 2;
			}
		} else if ((num >= 1000) && (num < 10000)) {
			tmpnum = num / 1000;
			snprintf(fn, sizeof(fn), "digits/%dk", tmpnum);
			num = num - (tmpnum * 1000);
			if ((num > 0) && (num < 11)) {
				state = 2;
			}
		} else if (num < 20000) {
			snprintf(fn, sizeof(fn), "digits/%dm", (num / 1000));
			num = num % 1000;
			state = 3;
		} else if (num < 1000000) {
			res = ast_say_number_full_he(chan, num / 1000, ints, language, "m", audiofd, ctrlfd);
			if (res) {
				return res;
			}
			snprintf(fn, sizeof(fn), "digits/1k");
			num = num % 1000;
			if ((num > 0) && (num < 11)) {
				state = 2;
			}
		} else if (num < 2000000) {
			snprintf(fn, sizeof(fn), "digits/million");
			num = num % 1000000;
			if ((num > 0) && (num < 11)) {
				state = 2;
			}
		} else if (num < 3000000) {
			snprintf(fn, sizeof(fn), "digits/twomillion");
			num = num - 2000000;
			if ((num > 0) && (num < 11)) {
				state = 2;
			}
		} else if (num < 1000000000) {
			res = ast_say_number_full_he(chan, num / 1000000, ints, language, "m", audiofd, ctrlfd);
			if (res) {
				return res;
			}
			snprintf(fn, sizeof(fn), "digits/million");
			num = num % 1000000;
			if ((num > 0) && (num < 11)) {
				state = 2;
			}
		} else {
			ast_debug(1, "Number '%d' is too big for me\n", num);
			res = -1;
		}
		tmpnum = 0;
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1)) {
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				} else {
					res = ast_waitstream(chan, ints);
				}
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*! \brief  ast_say_number_full_hu: Hungarian syntax

  Extra sounds needed:
	10en: "tizen"
	20on: "huszon"
*/
static int ast_say_number_full_hu(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	/*
	Hungarian support
	like english, except numbers up to 29 are from 2 words.
	10 and first word of 1[1-9] and 20 and first word of 2[1-9] are different.
	*/

	while(!res && (num || playh)) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playh) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			playh = 0;
		} else if (num < 11 || num == 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 20) {
			ast_copy_string(fn, "digits/10en", sizeof(fn));
			num -= 10;
		} else if (num < 30) {
			ast_copy_string(fn, "digits/20on", sizeof(fn));
			num -= 20;
		} else	if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num %= 10;
		} else {
			if (num < 1000){
				snprintf(fn, sizeof(fn), "digits/%d", (num/100));
				playh++;
				num %= 100;
			} else {
				if (num < 1000000) { /* 1,000,000 */
					res = ast_say_number_full_hu(chan, num / 1000, ints, language, audiofd, ctrlfd);
					if (res)
						return res;
					num %= 1000;
					ast_copy_string(fn, "digits/thousand", sizeof(fn));
				} else {
					if (num < 1000000000) {	/* 1,000,000,000 */
						res = ast_say_number_full_hu(chan, num / 1000000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						num %= 1000000;
						ast_copy_string(fn, "digits/million", sizeof(fn));
					} else {
						ast_debug(1, "Number '%d' is too big for me\n", num);
						res = -1;
					}
				}
			}
		}
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
				if ((audiofd  > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*! \brief  ast_say_number_full_it:  Italian */
static int ast_say_number_full_it(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int tempnum = 0;
	char fn[256] = "";

	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

		/*
		Italian support

		Like english, numbers up to 20 are a single 'word', and others
		compound, but with exceptions.
		For example 21 is not twenty-one, but there is a single word in 'it'.
		Idem for 28 (ie when a the 2nd part of a compund number
		starts with a vowel)

		There are exceptions also for hundred, thousand and million.
		In english 100 = one hundred, 200 is two hundred.
		In italian 100 = cento , like to say hundred (without one),
		200 and more are like english.

		Same applies for thousand:
		1000 is one thousand in en, 2000 is two thousand.
		In it we have 1000 = mille , 2000 = 2 mila

		For million(s) we use the plural, if more than one
		Also, one million is abbreviated in it, like on-million,
		or 'un milione', not 'uno milione'.
		So the right file is provided.
		*/

	while (!res && (num || playh)) {
			if (num < 0) {
				ast_copy_string(fn, "digits/minus", sizeof(fn));
				if ( num > INT_MIN ) {
					num = -num;
				} else {
					num = 0;
				}
			} else if (playh) {
				ast_copy_string(fn, "digits/hundred", sizeof(fn));
				playh = 0;
			} else if (num < 20) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 21) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 28) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 31) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 38) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 41) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 48) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 51) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 58) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 61) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 68) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 71) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 78) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 81) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 88) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 91) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num == 98) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else if (num < 100) {
				snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
				num %= 10;
			} else {
				if (num < 1000) {
					if ((num / 100) > 1) {
						snprintf(fn, sizeof(fn), "digits/%d", (num/100));
						playh++;
					} else {
						ast_copy_string(fn, "digits/hundred", sizeof(fn));
					}
					num %= 100;
				} else {
					if (num < 1000000) { /* 1,000,000 */
						if ((num/1000) > 1)
							res = ast_say_number_full_it(chan, num / 1000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						tempnum = num;
						num %= 1000;
						if ((tempnum / 1000) < 2)
							ast_copy_string(fn, "digits/thousand", sizeof(fn));
						else /* for 1000 it says mille, for >1000 (eg 2000) says mila */
							ast_copy_string(fn, "digits/thousands", sizeof(fn));
					} else {
						if (num < 1000000000) { /* 1,000,000,000 */
							if ((num / 1000000) > 1)
								res = ast_say_number_full_it(chan, num / 1000000, ints, language, audiofd, ctrlfd);
							if (res)
								return res;
							tempnum = num;
							num %= 1000000;
							if ((tempnum / 1000000) < 2)
								ast_copy_string(fn, "digits/million", sizeof(fn));
							else
								ast_copy_string(fn, "digits/millions", sizeof(fn));
						} else {
							ast_debug(1, "Number '%d' is too big for me\n", num);
							res = -1;
						}
					}
				}
			}
			if (!res) {
				if (!ast_streamfile(chan, fn, language)) {
					if ((audiofd > -1) && (ctrlfd > -1))
						res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
					else
						res = ast_waitstream(chan, ints);
				}
				ast_stopstream(chan);
			}
		}
	return res;
}

/*! \brief  ast_say_number_full_nl: dutch syntax
 * New files: digits/nl-en
 */
static int ast_say_number_full_nl(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int units = 0;
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);
	while (!res && (num || playh )) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playh) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			playh = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {
			units = num % 10;
			if (units > 0) {
				res = ast_say_number_full_nl(chan, units, ints, language, audiofd, ctrlfd);
				if (res)
					return res;
				num = num - units;
				ast_copy_string(fn, "digits/nl-en", sizeof(fn));
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", num - units);
				num = 0;
			}
		} else if (num < 200) {
			/* hundred, not one-hundred */
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			num %= 100;
		} else if (num < 1000) {
			snprintf(fn, sizeof(fn), "digits/%d", num / 100);
			playh++;
			num %= 100;
		} else {
			if (num < 1100) {
				/* thousand, not one-thousand */
				num %= 1000;
				ast_copy_string(fn, "digits/thousand", sizeof(fn));
			} else if (num < 10000)	{ /* 1,100 to 9,9999 */
				res = ast_say_number_full_nl(chan, num / 100, ints, language, audiofd, ctrlfd);
				if (res)
					return res;
				num %= 100;
				ast_copy_string(fn, "digits/hundred", sizeof(fn));
			} else {
				if (num < 1000000) { /* 1,000,000 */
					res = ast_say_number_full_nl(chan, num / 1000, ints, language, audiofd, ctrlfd);
					if (res)
						return res;
					num %= 1000;
					ast_copy_string(fn, "digits/thousand", sizeof(fn));
				} else {
					if (num < 1000000000) { /* 1,000,000,000 */
						res = ast_say_number_full_nl(chan, num / 1000000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						num %= 1000000;
						ast_copy_string(fn, "digits/million", sizeof(fn));
					} else {
						ast_debug(1, "Number '%d' is too big for me\n", num);
						res = -1;
					}
				}
			}
		}

		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*! \brief  ast_say_number_full_no: Norwegian syntax
 * New files:
 * In addition to American English, the following sounds are required:  "and", "1N"
 *
 * The grammar for Norwegian numbers is the same as for English except
 * for the following:
 * - 1 exists in both commune ("en", file "1") and neuter ("ett", file "1N")
 *   "and" before the last two digits, i.e. 2034 is "two thousand and
 *   thirty-four" and 1000012 is "one million and twelve".
 */
static int ast_say_number_full_no(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playa = 0;
	int cn = 1;		/* +1 = commune; -1 = neuter */
	char fn[256] = "";

	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	if (options && !strncasecmp(options, "n", 1)) cn = -1;

	while (!res && (num || playh || playa )) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playh) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			playh = 0;
		} else if (playa) {
			ast_copy_string(fn, "digits/and", sizeof(fn));
			playa = 0;
		} else if (num == 1 && cn == -1) {
			ast_copy_string(fn, "digits/1N", sizeof(fn));
			num = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num %= 10;
		} else if (num < 1000) {
			int hundreds = num / 100;
			if (hundreds == 1)
				ast_copy_string(fn, "digits/1N", sizeof(fn));
			else
				snprintf(fn, sizeof(fn), "digits/%d", (num / 100));

			playh++;
			num -= 100 * hundreds;
			if (num)
				playa++;
		} else if (num < 1000000) {
			res = ast_say_number_full_no(chan, num / 1000, ints, language, "n", audiofd, ctrlfd);
			if (res)
				return res;
			ast_copy_string(fn, "digits/thousand", sizeof(fn));
			num %= 1000;
			if (num && num < 100)
				playa++;
		} else if (num < 1000000000) {
				int millions = num / 1000000;
				res = ast_say_number_full_no(chan, millions, ints, language, "c", audiofd, ctrlfd);
				if (res)
					return res;
				ast_copy_string(fn, "digits/million", sizeof(fn));
				num %= 1000000;
				if (num && num < 100)
					playa++;
		} else {
				ast_debug(1, "Number '%d' is too big for me\n", num);
				res = -1;
		}

		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

typedef struct {
	char *separator_dziesiatek;
	char *cyfry[10];
	char *cyfry2[10];
	char *setki[10];
	char *dziesiatki[10];
	char *nastki[10];
	char *rzedy[3][3];
} odmiana;

static char *pl_rzad_na_tekst(odmiana *odm, int i, int rzad)
{
	if (rzad==0)
		return "";

	if (i==1)
		return odm->rzedy[rzad - 1][0];
	if ((i > 21 || i < 11) &&  i%10 > 1 && i%10 < 5)
		return odm->rzedy[rzad - 1][1];
	else
		return odm->rzedy[rzad - 1][2];
}

static char* pl_append(char* buffer, char* str)
{
	strcpy(buffer, str);
	buffer += strlen(str);
	return buffer;
}

static void pl_odtworz_plik(struct ast_channel *chan, const char *language, int audiofd, int ctrlfd, const char *ints, char *fn)
{
	char file_name[255] = "digits/";
	strcat(file_name, fn);
	ast_debug(1, "Trying to play: %s\n", file_name);
	if (!ast_streamfile(chan, file_name, language)) {
		if ((audiofd > -1) && (ctrlfd > -1))
			ast_waitstream_full(chan, ints, audiofd, ctrlfd);
		else
			ast_waitstream(chan, ints);
	}
	ast_stopstream(chan);
}

static void powiedz(struct ast_channel *chan, const char *language, int audiofd, int ctrlfd, const char *ints, odmiana *odm, int rzad, int i)
{
	/* Initialise variables to allow compilation on Debian-stable, etc */
	int m1000E6 = 0;
	int i1000E6 = 0;
	int m1000E3 = 0;
	int i1000E3 = 0;
	int m1000 = 0;
	int i1000 = 0;
	int m100 = 0;
	int i100 = 0;

	if (i == 0 && rzad > 0) {
		return;
	}
	if (i == 0) {
		pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry[0]);
		return;
	}

	m1000E6 = i % 1000000000;
	i1000E6 = i / 1000000000;

	powiedz(chan, language, audiofd, ctrlfd, ints, odm, rzad+3, i1000E6);

	m1000E3 = m1000E6 % 1000000;
	i1000E3 = m1000E6 / 1000000;

	powiedz(chan, language, audiofd, ctrlfd, ints, odm, rzad+2, i1000E3);

	m1000 = m1000E3 % 1000;
	i1000 = m1000E3 / 1000;

	powiedz(chan, language, audiofd, ctrlfd, ints, odm, rzad+1, i1000);

	m100 = m1000 % 100;
	i100 = m1000 / 100;

        if (i100>0)
                pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->setki[i100]);

        if (m100 > 0 && m100 <= 9) {
                if (m1000 > 0)
                        pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry2[m100]);
                else
                        pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry[m100]);
        } else if (m100 % 10 == 0 && m100 != 0) {
                pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->dziesiatki[m100 / 10]);
        } else if (m100 > 10 && m100 <= 19) {
                pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->nastki[m100 % 10]);
        } else if (m100 > 20) {
                if (odm->separator_dziesiatek[0] == ' ') {
                        pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->dziesiatki[m100 / 10]);
                        pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry2[m100 % 10]);
                } else {
                        char buf[10];
                        char *b = buf;
                        b = pl_append(b, odm->dziesiatki[m100 / 10]);
                        b = pl_append(b, odm->separator_dziesiatek);
                        pl_append(b, odm->cyfry2[m100 % 10]);
                        pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, buf);
                }
        }

	if (rzad > 0) {
		pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, pl_rzad_na_tekst(odm, i, rzad));
	}
}

/* ast_say_number_full_pl: Polish syntax

Sounds needed:
0		zero
1		jeden
10		dziesiec
100		sto
1000		tysiac
1000000		milion
1000000000	miliard
1000000000.2	miliardy
1000000000.5	miliardow
1000000.2	miliony
1000000.5	milionow
1000.2		tysiace
1000.5		tysiecy
100m		stu
10m		dziesieciu
11		jedenascie
11m		jedenastu
12		dwanascie
12m		dwunastu
13		trzynascie
13m		trzynastu
14		czternascie
14m		czternastu
15		pietnascie
15m		pietnastu
16		szesnascie
16m		szesnastu
17		siedemnascie
17m		siedemnastu
18		osiemnascie
18m		osiemnastu
19		dziewietnascie
19m		dziewietnastu
1z		jedna
2		dwa
20		dwadziescia
200		dwiescie
200m		dwustu
20m		dwudziestu
2-1m		dwaj
2-2m		dwoch
2z		dwie
3		trzy
30		trzydziesci
300		trzysta
300m		trzystu
30m		trzydziestu
3-1m		trzej
3-2m		trzech
4		cztery
40		czterdziesci
400		czterysta
400m		czterystu
40m		czterdziestu
4-1m		czterej
4-2m		czterech
5		piec
50		piecdziesiat
500		piecset
500m		pieciuset
50m		piedziesieciu
5m		pieciu
6		szesc
60		szescdziesiat
600		szescset
600m		szesciuset
60m		szescdziesieciu
6m		szesciu
7		siedem
70		siedemdziesiat
700		siedemset
700m		siedmiuset
70m		siedemdziesieciu
7m		siedmiu
8		osiem
80		osiemdziesiat
800		osiemset
800m		osmiuset
80m		osiemdziesieciu
8m		osmiu
9		dziewiec
90		dziewiecdziesiat
900		dziewiecset
900m		dziewieciuset
90m		dziewiedziesieciu
9m		dziewieciu
and combinations of eg.: 20_1, 30m_3m, etc...

*/
static int ast_say_number_full_pl(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	char *zenski_cyfry[] = {"0", "1z", "2z", "3", "4", "5", "6", "7", "8", "9"};

	char *zenski_cyfry2[] = {"0", "1", "2z", "3", "4", "5", "6", "7", "8", "9"};

	char *meski_cyfry[] = {"0", "1", "2-1m", "3-1m", "4-1m", "5m",  /*"2-1mdwaj"*/ "6m", "7m", "8m", "9m"};

	char *meski_cyfry2[] = {"0", "1", "2-2m", "3-2m", "4-2m", "5m", "6m", "7m", "8m", "9m"};

	char *meski_setki[] = {"", "100m", "200m", "300m", "400m", "500m", "600m", "700m", "800m", "900m"};

	char *meski_dziesiatki[] = {"", "10m", "20m", "30m", "40m", "50m", "60m", "70m", "80m", "90m"};

	char *meski_nastki[] = {"", "11m", "12m", "13m", "14m", "15m", "16m", "17m", "18m", "19m"};

	char *nijaki_cyfry[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

	char *nijaki_cyfry2[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

	char *nijaki_setki[] = {"", "100", "200", "300", "400", "500", "600", "700", "800", "900"};

	char *nijaki_dziesiatki[] = {"", "10", "20", "30", "40", "50", "60", "70", "80", "90"};

	char *nijaki_nastki[] = {"", "11", "12", "13", "14", "15", "16", "17", "18", "19"};

	char *rzedy[][3] = { {"1000", "1000.2", "1000.5"}, {"1000000", "1000000.2", "1000000.5"}, {"1000000000", "1000000000.2", "1000000000.5"}};

	/* Initialise variables to allow compilation on Debian-stable, etc */
	odmiana *o;

	static odmiana *odmiana_nieosobowa = NULL;
	static odmiana *odmiana_meska = NULL;
	static odmiana *odmiana_zenska = NULL;

	if (odmiana_nieosobowa == NULL) {
		odmiana_nieosobowa = ast_malloc(sizeof(*odmiana_nieosobowa));

		odmiana_nieosobowa->separator_dziesiatek = " ";

		memcpy(odmiana_nieosobowa->cyfry, nijaki_cyfry, sizeof(odmiana_nieosobowa->cyfry));
		memcpy(odmiana_nieosobowa->cyfry2, nijaki_cyfry2, sizeof(odmiana_nieosobowa->cyfry));
		memcpy(odmiana_nieosobowa->setki, nijaki_setki, sizeof(odmiana_nieosobowa->setki));
		memcpy(odmiana_nieosobowa->dziesiatki, nijaki_dziesiatki, sizeof(odmiana_nieosobowa->dziesiatki));
		memcpy(odmiana_nieosobowa->nastki, nijaki_nastki, sizeof(odmiana_nieosobowa->nastki));
		memcpy(odmiana_nieosobowa->rzedy, rzedy, sizeof(odmiana_nieosobowa->rzedy));
	}

	if (odmiana_zenska == NULL) {
		odmiana_zenska = ast_malloc(sizeof(*odmiana_zenska));

		odmiana_zenska->separator_dziesiatek = " ";

		memcpy(odmiana_zenska->cyfry, zenski_cyfry, sizeof(odmiana_zenska->cyfry));
		memcpy(odmiana_zenska->cyfry2, zenski_cyfry2, sizeof(odmiana_zenska->cyfry));
		memcpy(odmiana_zenska->setki, nijaki_setki, sizeof(odmiana_zenska->setki));
		memcpy(odmiana_zenska->dziesiatki, nijaki_dziesiatki, sizeof(odmiana_zenska->dziesiatki));
		memcpy(odmiana_zenska->nastki, nijaki_nastki, sizeof(odmiana_zenska->nastki));
		memcpy(odmiana_zenska->rzedy, rzedy, sizeof(odmiana_zenska->rzedy));
	}

	if (odmiana_meska == NULL) {
		odmiana_meska = ast_malloc(sizeof(*odmiana_meska));

		odmiana_meska->separator_dziesiatek = " ";

		memcpy(odmiana_meska->cyfry, meski_cyfry, sizeof(odmiana_meska->cyfry));
		memcpy(odmiana_meska->cyfry2, meski_cyfry2, sizeof(odmiana_meska->cyfry));
		memcpy(odmiana_meska->setki, meski_setki, sizeof(odmiana_meska->setki));
		memcpy(odmiana_meska->dziesiatki, meski_dziesiatki, sizeof(odmiana_meska->dziesiatki));
		memcpy(odmiana_meska->nastki, meski_nastki, sizeof(odmiana_meska->nastki));
		memcpy(odmiana_meska->rzedy, rzedy, sizeof(odmiana_meska->rzedy));
	}

	if (options) {
		if (strncasecmp(options, "f", 1) == 0)
			o = odmiana_zenska;
		else if (strncasecmp(options, "m", 1) == 0)
			o = odmiana_meska;
		else
			o = odmiana_nieosobowa;
	} else
		o = odmiana_nieosobowa;

	powiedz(chan, language, audiofd, ctrlfd, ints, o, 0, num);
	return 0;
}

/* ast_say_number_full_pt: Portuguese syntax

 *	Extra sounds needed:
 *	For feminin all sound files ends with F
 *	100E for 100+ something
 *	1000000S for plural
 *	pt-e for 'and'
 */
static int ast_say_number_full_pt(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int mf = 1;                            /* +1 = male; -1 = female */
	char fn[256] = "";

	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	if (options && !strncasecmp(options, "f", 1))
		mf = -1;

	while (!res && num ) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (num < 20) {
			if ((num == 1 || num == 2) && (mf < 0))
				snprintf(fn, sizeof(fn), "digits/%dF", num);
			else
				snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num / 10) * 10);
			if (num % 10)
				playh = 1;
			num = num % 10;
		} else if (num < 1000) {
			if (num == 100)
				ast_copy_string(fn, "digits/100", sizeof(fn));
			else if (num < 200)
				ast_copy_string(fn, "digits/100E", sizeof(fn));
			else {
				if (mf < 0 && num > 199)
					snprintf(fn, sizeof(fn), "digits/%dF", (num / 100) * 100);
				else
					snprintf(fn, sizeof(fn), "digits/%d", (num / 100) * 100);
				if (num % 100)
					playh = 1;
			}
			num = num % 100;
		} else if (num < 1000000) {
			if (num > 1999) {
				res = ast_say_number_full_pt(chan, (num / 1000) * mf, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
			}
			ast_copy_string(fn, "digits/1000", sizeof(fn));
			if ((num % 1000) && ((num % 1000) < 100  || !(num % 100)))
				playh = 1;
			num = num % 1000;
		} else if (num < 1000000000) {
			res = ast_say_number_full_pt(chan, (num / 1000000), ints, language, options, audiofd, ctrlfd );
			if (res)
				return res;
			if (num < 2000000)
				ast_copy_string(fn, "digits/1000000", sizeof(fn));
			else
				ast_copy_string(fn, "digits/1000000S", sizeof(fn));

			if ((num % 1000000) &&
				/* no thousands */
				((!((num / 1000) % 1000) && ((num % 1000) < 100 || !(num % 100))) ||
				/* no hundreds and below */
				(!(num % 1000) && (((num / 1000) % 1000) < 100 || !((num / 1000) % 100))) ) )
				playh = 1;
			num = num % 1000000;
		} else {
			/* number is too big */
			ast_log(LOG_WARNING, "Number '%d' is too big to say.", num);
			res = -1;
		}
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
		if (!res && playh) {
			res = wait_file(chan, ints, "digits/pt-e", language);
			ast_stopstream(chan);
			playh = 0;
		}
	}
	return res;
}

/*! \brief  ast_say_number_full_se: Swedish syntax

 Sound files needed
 - 1N
*/
static int ast_say_number_full_se(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int playh = 0;
	int start = 1;
	char fn[256] = "";
	int cn = 1;		/* +1 = commune; -1 = neuter */
	int res = 0;

	if (!num) {
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);
	}
	if (options && !strncasecmp(options, "n", 1)) cn = -1;

	while (num || playh) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playh) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			playh = 0;
		} else if (start  && num < 200 && num > 99 && cn == -1) {
			/* Don't say "en hundra" just say "hundra". */
			snprintf(fn, sizeof(fn), "digits/hundred");
			num -= 100;
		} else if (num == 1 && cn == -1) {	/* En eller ett? */
			ast_copy_string(fn, "digits/1N", sizeof(fn));
			num = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {	/* Below hundreds - teens and tens */
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num %= 10;
		} else if (num < 1000) {
			/* Hundreds */
			snprintf(fn, sizeof(fn), "digits/%d", (num/100));
			playh++;
			num %= 100;
		} else if (num < 1000000) { /* 1,000,000 */
			/* Always say "ett hundra tusen", not "en hundra tusen" */
			res = ast_say_number_full_se(chan, num / 1000, ints, language, "c", audiofd, ctrlfd);
			if (res) {
				return res;
			}
			num %= 1000;
			ast_copy_string(fn, "digits/thousand", sizeof(fn));
		} else if (num < 1000000000) {	/* 1,000,000,000 */
			/* Always say "en miljon", not "ett miljon" */
			res = ast_say_number_full_se(chan, num / 1000000, ints, language, "n", audiofd, ctrlfd);
			if (res) {
				return res;
			}
			num %= 1000000;
			ast_copy_string(fn, "digits/million", sizeof(fn));
		} else {	/* Miljarder - Billions */
			ast_debug(1, "Number '%d' is too big for me\n", num);
			return -1;
		}

		if (!ast_streamfile(chan, fn, language)) {
			if ((audiofd > -1) && (ctrlfd > -1)) {
				res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
			} else {
				res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
			if (res) {
				return res;
			}
		}
		start = 0;
	}
	return 0;
}

/*! \brief  ast_say_number_full_zh: Taiwanese / Chinese syntax */
static int ast_say_number_full_zh(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playt = 0;
	int playz = 0;
	int last_length = 0;
	char buf[20] = "";
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	while (!res && (num || playh || playt || playz)) {
			if (num < 0) {
				ast_copy_string(fn, "digits/minus", sizeof(fn));
				if ( num > INT_MIN ) {
					num = -num;
				} else {
					num = 0;
				}
			} else if (playz) {
				snprintf(fn, sizeof(fn), "digits/0");
				last_length = 0;
				playz = 0;
			} else if (playh) {
				ast_copy_string(fn, "digits/hundred", sizeof(fn));
				playh = 0;
			} else if (playt) {
				snprintf(fn, sizeof(fn), "digits/thousand");
				playt = 0;
			} else	if (num < 10) {
				snprintf(buf, 10, "%d", num);
				if (last_length - strlen(buf) > 1 && last_length != 0) {
					last_length = strlen(buf);
					playz++;
					continue;
				}
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else	if (num < 100) {
				snprintf(buf, 10, "%d", num);
				if (last_length - strlen(buf) > 1 && last_length != 0) {
					last_length = strlen(buf);
					playz++;
					continue;
				}
				last_length = strlen(buf);
				snprintf(fn, sizeof(fn), "digits/%d", (num / 10) * 10);
				num %= 10;
			} else {
				if (num < 1000){
					snprintf(buf, 10, "%d", num);
					if (last_length - strlen(buf) > 1 && last_length != 0) {
						last_length = strlen(buf);
						playz++;
						continue;
					}
					snprintf(fn, sizeof(fn), "digits/%d", (num / 100));
					playh++;
					snprintf(buf, 10, "%d", num);
					ast_debug(1, "Number '%d' %d %d\n", num, (int)strlen(buf), last_length);
					last_length = strlen(buf);
					num -= ((num / 100) * 100);
				} else if (num < 10000){
					snprintf(buf, 10, "%d", num);
					snprintf(fn, sizeof(fn), "digits/%d", (num / 1000));
					playt++;
					snprintf(buf, 10, "%d", num);
					ast_debug(1, "Number '%d' %d %d\n", num, (int)strlen(buf), last_length);
					last_length = strlen(buf);
					num -= ((num / 1000) * 1000);
				} else if (num < 100000000) { /* 100,000,000 */
						res = ast_say_number_full_zh(chan, num / 10000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						snprintf(buf, 10, "%d", num);
						ast_debug(1, "Number '%d' %d %d\n", num, (int)strlen(buf), last_length);
						num -= ((num / 10000) * 10000);
						last_length = strlen(buf);
						snprintf(fn, sizeof(fn), "digits/wan");
				} else {
					if (num < 1000000000) { /* 1,000,000,000 */
						res = ast_say_number_full_zh(chan, num / 100000000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						snprintf(buf, 10, "%d", num);
						ast_debug(1, "Number '%d' %d %d\n", num, (int)strlen(buf), last_length);
						last_length = strlen(buf);
						num -= ((num / 100000000) * 100000000);
						snprintf(fn, sizeof(fn), "digits/yi");
					} else {
						ast_debug(1, "Number '%d' is too big for me\n", num);
						res = -1;
					}
				}
			}
			if (!res) {
				if (!ast_streamfile(chan, fn, language)) {
					if ((audiofd > -1) && (ctrlfd > -1))
						res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
					else
						res = ast_waitstream(chan, ints);
				}
				ast_stopstream(chan);
			}
	}
	return res;
}

/*!\internal
 * \brief Counting in Urdu, the national language of Pakistan
 * \since 1.8
 */
static int ast_say_number_full_ur(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";

	if (!num) {
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);
	}

	while (!res && (num || playh)) {
		if (playh) {
			snprintf(fn, sizeof(fn), "digits/hundred");
			playh = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 1000) {
			snprintf(fn, sizeof(fn), "digits/%d", (num / 100));
			playh++;
			num -= ((num / 100) * 100);
		} else if (num < 100000) { /* 1,00,000 */
			if ((res = ast_say_number_full_ur(chan, num / 1000, ints, language, options, audiofd, ctrlfd))) {
				return res;
			}
			num = num % 1000;
			snprintf(fn, sizeof(fn), "digits/thousand");
		} else if (num < 10000000) { /* 1,00,00,000 */
			if ((res = ast_say_number_full_ur(chan, num / 100000, ints, language, options, audiofd, ctrlfd))) {
				return res;
			}
			num = num % 100000;
			snprintf(fn, sizeof(fn), "digits/lac");
		} else if (num < 1000000000) { /* 1,00,00,00,000 */
			if ((res = ast_say_number_full_ur(chan, num / 10000000, ints, language, options, audiofd, ctrlfd))) {
				return res;
			}
			num = num % 10000000;
			snprintf(fn, sizeof(fn), "digits/crore");
		} else {
			ast_debug(1, "Number '%d' is too big for me\n", num);
			res = -1;
		}

		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1)) {
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				} else {
					res = ast_waitstream(chan, ints);
				}
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*! \brief  determine last digits for thousands/millions (ru) */
static int get_lastdigits_ru(int num) {
	if (num < 20) {
		return num;
	} else if (num < 100) {
		return get_lastdigits_ru(num % 10);
	} else if (num < 1000) {
		return get_lastdigits_ru(num % 100);
	}
	return 0;	/* number too big */
}


/*! \brief  ast_say_number_full_ru: Russian syntax

 additional files:
	n00.gsm			(one hundred, two hundred, ...)
	thousand.gsm
	million.gsm
	thousands-i.gsm		(tisyachi)
	million-a.gsm		(milliona)
	thousands.gsm
	millions.gsm
	1f.gsm			(odna)
	2f.gsm			(dve)

	where 'n' from 1 to 9
*/
static int ast_say_number_full_ru(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int lastdigits = 0;
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	while (!res && (num)) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else	if (num < 20) {
			if (options && strlen(options) == 1 && num < 3) {
			    snprintf(fn, sizeof(fn), "digits/%d%s", num, options);
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", num);
			}
			num = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", num - (num % 10));
			num %= 10;
		} else if (num < 1000){
			snprintf(fn, sizeof(fn), "digits/%d", num - (num % 100));
			num %= 100;
		} else if (num < 1000000) { /* 1,000,000 */
			lastdigits = get_lastdigits_ru(num / 1000);
			/* say thousands */
			if (lastdigits < 3) {
				res = ast_say_number_full_ru(chan, num / 1000, ints, language, "f", audiofd, ctrlfd);
			} else {
				res = ast_say_number_full_ru(chan, num / 1000, ints, language, NULL, audiofd, ctrlfd);
			}
			if (res)
				return res;
			if (lastdigits == 1) {
				ast_copy_string(fn, "digits/thousand", sizeof(fn));
			} else if (lastdigits > 1 && lastdigits < 5) {
				ast_copy_string(fn, "digits/thousands-i", sizeof(fn));
			} else {
				ast_copy_string(fn, "digits/thousands", sizeof(fn));
			}
			num %= 1000;
		} else if (num < 1000000000) {	/* 1,000,000,000 */
			lastdigits = get_lastdigits_ru(num / 1000000);
			/* say millions */
			res = ast_say_number_full_ru(chan, num / 1000000, ints, language, NULL, audiofd, ctrlfd);
			if (res)
				return res;
			if (lastdigits == 1) {
				ast_copy_string(fn, "digits/million", sizeof(fn));
			} else if (lastdigits > 1 && lastdigits < 5) {
				ast_copy_string(fn, "digits/million-a", sizeof(fn));
			} else {
				ast_copy_string(fn, "digits/millions", sizeof(fn));
			}
			num %= 1000000;
		} else {
			ast_debug(1, "Number '%d' is too big for me\n", num);
			res = -1;
		}
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd  > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*! \brief Thai syntax */
static int ast_say_number_full_th(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	while(!res && (num || playh)) {
		if (num < 0) {
			ast_copy_string(fn, "digits/lop", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playh) {
			ast_copy_string(fn, "digits/roi", sizeof(fn));
			playh = 0;
		} else if (num < 100) {
			if ((num <= 20) || ((num % 10) == 1)) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", (num / 10) * 10);
				num %= 10;
			}
		} else if (num < 1000) {
			snprintf(fn, sizeof(fn), "digits/%d", (num/100));
			playh++;
			num %= 100;
		} else if (num < 10000) { /* 10,000 */
			res = ast_say_number_full_th(chan, num / 1000, ints, language, audiofd, ctrlfd);
			if (res)
				return res;
			num %= 1000;
			ast_copy_string(fn, "digits/pan", sizeof(fn));
		} else if (num < 100000) { /* 100,000 */
			res = ast_say_number_full_th(chan, num / 10000, ints, language, audiofd, ctrlfd);
			if (res)
				return res;
			num %= 10000;
			ast_copy_string(fn, "digits/muan", sizeof(fn));
		} else if (num < 1000000) { /* 1,000,000 */
			res = ast_say_number_full_th(chan, num / 100000, ints, language, audiofd, ctrlfd);
			if (res)
				return res;
			num %= 100000;
			ast_copy_string(fn, "digits/san", sizeof(fn));
		} else {
			res = ast_say_number_full_th(chan, num / 1000000, ints, language, audiofd, ctrlfd);
			if (res)
				return res;
			num %= 1000000;
			ast_copy_string(fn, "digits/larn", sizeof(fn));
		}
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
				if ((audiofd  > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*! \brief  ast_say_number_full_vi: Vietnamese syntax */
static int ast_say_number_full_vi(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playoh = 0;
	int playohz = 0;
	int playz = 0;
	int playl = 0;
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);
	while (!res && (num || playh)) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn));
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (playl) {
			snprintf(fn, sizeof(fn), "digits/%da", num);
			playl = 0;
			num = 0;
		} else if (playh) {
			ast_copy_string(fn, "digits/hundred", sizeof(fn));
			playh = 0;
		} else if (playz) {
			ast_copy_string(fn, "digits/odd", sizeof(fn));
			playz = 0;
		} else if (playoh) {
			ast_copy_string(fn, "digits/0-hundred", sizeof(fn));
			playoh = 0;
		} else if (playohz) {
			ast_copy_string(fn, "digits/0-hundred-odd", sizeof(fn));
			playohz = 0;
		} else	if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else	if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num %= 10;
			if ((num == 5) || (num == 4) || (num == 1)) playl++;
		} else {
			if (num < 1000) {
				snprintf(fn, sizeof(fn), "digits/%d", (num/100));
				num %= 100;
				if (num && (num < 10)) {
					playz++;
					playh++;
				} else {
					playh++;
				}
			} else {
				if (num < 1000000) { /* 1,000,000 */
					res = ast_say_number_full_vi(chan, num / 1000, ints, language, audiofd, ctrlfd);
					if (res)
						return res;
					num %= 1000;
					snprintf(fn, sizeof(fn), "digits/thousand");
					if (num && (num < 10)) {
						playohz++;
					} else if (num && (num < 100)){
						playoh++;
					} else {
						playh = 0;
						playohz = 0;
						playoh = 0;
					}
				} else {
					if (num < 1000000000) {	/* 1,000,000,000 */
						res = ast_say_number_full_vi(chan, num / 1000000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						num %= 1000000;
						ast_copy_string(fn, "digits/million", sizeof(fn));
					} else {
						res = -1;
					}
				}
			}
		}
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd  > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*! \brief  ast_say_enumeration_full: call language-specific functions
 * \note Called from AGI */
static int say_enumeration_full(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	if (!strncasecmp(language, "en", 2)) {        /* English syntax */
	   return ast_say_enumeration_full_en(chan, num, ints, language, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "da", 2)) { /* Danish syntax */
	   return ast_say_enumeration_full_da(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "de", 2)) { /* German syntax */
	   return ast_say_enumeration_full_de(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "he", 2)) { /* Hebrew syntax */
		return ast_say_enumeration_full_he(chan, num, ints, language, options, audiofd, ctrlfd);
	} else if (!strncasecmp(language, "vi", 2)) { /* Vietnamese syntax */
		return ast_say_enumeration_full_vi(chan, num, ints, language, audiofd, ctrlfd);
	}

	/* Default to english */
	return ast_say_enumeration_full_en(chan, num, ints, language, audiofd, ctrlfd);
}

/*! \brief  ast_say_enumeration_full_en: English syntax
 \note This is the default syntax, if no other syntax defined in this file is used */
static int ast_say_enumeration_full_en(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0, t = 0;
	char fn[256] = "";

	while (!res && num) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn)); /* kind of senseless for enumerations, but our best effort for error checking */
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/h-%d", num);
			num = 0;
		} else if (num < 100) {
			int tens = num / 10;
			num = num % 10;
			if (num == 0) {
				snprintf(fn, sizeof(fn), "digits/h-%d", (tens * 10));
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", (tens * 10));
			}
		} else if (num < 1000) {
			int hundreds = num / 100;
			num = num % 100;
			if (hundreds > 1 || t == 1) {
				res = ast_say_number_full_en(chan, hundreds, ints, language, audiofd, ctrlfd);
			}
			if (res)
				return res;
			if (num) {
				ast_copy_string(fn, "digits/hundred", sizeof(fn));
			} else {
				ast_copy_string(fn, "digits/h-hundred", sizeof(fn));
			}
		} else if (num < 1000000) {
			int thousands = num / 1000;
			num = num % 1000;
			if (thousands > 1 || t == 1) {
				res = ast_say_number_full_en(chan, thousands, ints, language, audiofd, ctrlfd);
			}
			if (res)
				return res;
			if (num) {
				ast_copy_string(fn, "digits/thousand", sizeof(fn));
			} else {
				ast_copy_string(fn, "digits/h-thousand", sizeof(fn));
			}
			t = 1;
		} else if (num < 1000000000) {
			int millions = num / 1000000;
			num = num % 1000000;
			t = 1;
			res = ast_say_number_full_en(chan, millions, ints, language, audiofd, ctrlfd);
			if (res)
				return res;
			if (num) {
				ast_copy_string(fn, "digits/million", sizeof(fn));
			} else {
				ast_copy_string(fn, "digits/h-million", sizeof(fn));
			}
		} else if (num < INT_MAX) {
			int billions = num / 1000000000;
			num = num % 1000000000;
			t = 1;
			res = ast_say_number_full_en(chan, billions, ints, language, audiofd, ctrlfd);
			if (res)
				return res;
			if (num) {
				ast_copy_string(fn, "digits/billion", sizeof(fn));
			} else {
				ast_copy_string(fn, "digits/h-billion", sizeof(fn));
			}
		} else if (num == INT_MAX) {
			ast_copy_string(fn, "digits/h-last", sizeof(fn));
			num = 0;
		} else {
			ast_debug(1, "Number '%d' is too big for me\n", num);
			res = -1;
		}

		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1)) {
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				} else {
					res = ast_waitstream(chan, ints);
				}
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

static int ast_say_enumeration_full_vi(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	char fn[256] = "";
	ast_copy_string(fn, "digits/h", sizeof(fn));
	if (!res) {
		if (!ast_streamfile(chan, fn, language)) {
			if ((audiofd > -1) && (ctrlfd > -1)) {
				res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
			} else {
				res = ast_waitstream(chan, ints);
			}
		}
		ast_stopstream(chan);
	}

	return ast_say_number_full_vi(chan, num, ints, language, audiofd, ctrlfd);
}

/*! \brief  ast_say_enumeration_full_da: Danish syntax */
static int ast_say_enumeration_full_da(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	/* options can be: '' or 'm' male gender; 'f' female gender; 'n' neuter gender; 'p' plural */
	int res = 0, t = 0;
	char fn[256] = "", fna[256] = "";
	char *gender;

	if (options && !strncasecmp(options, "f", 1)) {
		gender = "F";
	} else if (options && !strncasecmp(options, "n", 1)) {
		gender = "N";
	} else {
		gender = "";
	}

	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	while (!res && num) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn)); /* kind of senseless for enumerations, but our best effort for error checking */
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (num < 100 && t) {
			ast_copy_string(fn, "digits/and", sizeof(fn));
			t = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/h-%d%s", num, gender);
			num = 0;
		} else if (num < 100) {
			int ones = num % 10;
			if (ones) {
				snprintf(fn, sizeof(fn), "digits/%d-and", ones);
				num -= ones;
			} else {
				snprintf(fn, sizeof(fn), "digits/h-%d%s", num, gender);
				num = 0;
			}
		} else if (num == 100 && t == 0) {
			snprintf(fn, sizeof(fn), "digits/h-hundred%s", gender);
			num = 0;
		} else if (num < 1000) {
			int hundreds = num / 100;
			num = num % 100;
			if (hundreds == 1) {
				ast_copy_string(fn, "digits/1N", sizeof(fn));
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", hundreds);
			}
			if (num) {
				ast_copy_string(fna, "digits/hundred", sizeof(fna));
			} else {
				snprintf(fna, sizeof(fna), "digits/h-hundred%s", gender);
			}
			t = 1;
		} else if (num < 1000000) {
			int thousands = num / 1000;
			num = num % 1000;
			if (thousands == 1) {
				if (num) {
					ast_copy_string(fn, "digits/1N", sizeof(fn));
					ast_copy_string(fna, "digits/thousand", sizeof(fna));
				} else {
					if (t) {
						ast_copy_string(fn, "digits/1N", sizeof(fn));
						snprintf(fna, sizeof(fna), "digits/h-thousand%s", gender);
					} else {
						snprintf(fn, sizeof(fn), "digits/h-thousand%s", gender);
					}
				}
			} else {
				res = ast_say_number_full_de(chan, thousands, ints, language, options, audiofd, ctrlfd);
				if (res) {
					return res;
				}
				if (num) {
					ast_copy_string(fn, "digits/thousand", sizeof(fn));
				} else {
					snprintf(fn, sizeof(fn), "digits/h-thousand%s", gender);
				}
			}
			t = 1;
		} else if (num < 1000000000) {
			int millions = num / 1000000;
			num = num % 1000000;
			if (millions == 1) {
				if (num) {
					ast_copy_string(fn, "digits/1F", sizeof(fn));
					ast_copy_string(fna, "digits/million", sizeof(fna));
				} else {
					ast_copy_string(fn, "digits/1N", sizeof(fn));
					snprintf(fna, sizeof(fna), "digits/h-million%s", gender);
				}
			} else {
				res = ast_say_number_full_de(chan, millions, ints, language, options, audiofd, ctrlfd);
				if (res) {
					return res;
				}
				if (num) {
					ast_copy_string(fn, "digits/millions", sizeof(fn));
				} else {
					snprintf(fn, sizeof(fn), "digits/h-million%s", gender);
				}
			}
			t = 1;
		} else if (num < INT_MAX) {
			int billions = num / 1000000000;
			num = num % 1000000000;
			if (billions == 1) {
				if (num) {
					ast_copy_string(fn, "digits/1F", sizeof(fn));
					ast_copy_string(fna, "digits/milliard", sizeof(fna));
				} else {
					ast_copy_string(fn, "digits/1N", sizeof(fn));
					snprintf(fna, sizeof(fna), "digits/h-milliard%s", gender);
				}
			} else {
				res = ast_say_number_full_de(chan, billions, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
				if (num) {
					ast_copy_string(fn, "digits/milliards", sizeof(fna));
				} else {
					snprintf(fn, sizeof(fna), "digits/h-milliard%s", gender);
				}
			}
			t = 1;
		} else if (num == INT_MAX) {
			snprintf(fn, sizeof(fn), "digits/h-last%s", gender);
			num = 0;
		} else {
			ast_debug(1, "Number '%d' is too big for me\n", num);
			res = -1;
		}

		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
			if (!res) {
				if (strlen(fna) != 0 && !ast_streamfile(chan, fna, language)) {
					if ((audiofd > -1) && (ctrlfd > -1)) {
						res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
					} else {
						res = ast_waitstream(chan, ints);
					}
				}
				ast_stopstream(chan);
				strcpy(fna, "");
			}
		}
	}
	return res;
}

/*! \brief  ast_say_enumeration_full_de: German syntax */
static int ast_say_enumeration_full_de(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	/* options can be: '' or 'm' male gender; 'f' female gender; 'n' neuter gender; 'p' plural */
	int res = 0, t = 0;
	char fn[256] = "", fna[256] = "";
	char *gender;

	if (options && !strncasecmp(options, "f", 1)) {
		gender = "F";
	} else if (options && !strncasecmp(options, "n", 1)) {
		gender = "N";
	} else {
		gender = "";
	}

	if (!num)
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

	while (!res && num) {
		if (num < 0) {
			ast_copy_string(fn, "digits/minus", sizeof(fn)); /* kind of senseless for enumerations, but our best effort for error checking */
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (num < 100 && t) {
			ast_copy_string(fn, "digits/and", sizeof(fn));
			t = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/h-%d%s", num, gender);
			num = 0;
		} else if (num < 100) {
			int ones = num % 10;
			if (ones) {
				snprintf(fn, sizeof(fn), "digits/%d-and", ones);
				num -= ones;
			} else {
				snprintf(fn, sizeof(fn), "digits/h-%d%s", num, gender);
				num = 0;
			}
		} else if (num == 100 && t == 0) {
			snprintf(fn, sizeof(fn), "digits/h-hundred%s", gender);
			num = 0;
		} else if (num < 1000) {
			int hundreds = num / 100;
			num = num % 100;
			if (hundreds == 1) {
				ast_copy_string(fn, "digits/1N", sizeof(fn));
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", hundreds);
			}
			if (num) {
				ast_copy_string(fna, "digits/hundred", sizeof(fna));
			} else {
				snprintf(fna, sizeof(fna), "digits/h-hundred%s", gender);
			}
			t = 1;
		} else if (num < 1000000) {
			int thousands = num / 1000;
			num = num % 1000;
			if (thousands == 1) {
				if (num) {
					ast_copy_string(fn, "digits/1N", sizeof(fn));
					ast_copy_string(fna, "digits/thousand", sizeof(fna));
				} else {
					if (t) {
						ast_copy_string(fn, "digits/1N", sizeof(fn));
						snprintf(fna, sizeof(fna), "digits/h-thousand%s", gender);
					} else {
						snprintf(fn, sizeof(fn), "digits/h-thousand%s", gender);
					}
				}
			} else {
				res = ast_say_number_full_de(chan, thousands, ints, language, options, audiofd, ctrlfd);
				if (res) {
					return res;
				}
				if (num) {
					ast_copy_string(fn, "digits/thousand", sizeof(fn));
				} else {
					snprintf(fn, sizeof(fn), "digits/h-thousand%s", gender);
				}
			}
			t = 1;
		} else if (num < 1000000000) {
			int millions = num / 1000000;
			num = num % 1000000;
			if (millions == 1) {
				if (num) {
					ast_copy_string(fn, "digits/1F", sizeof(fn));
					ast_copy_string(fna, "digits/million", sizeof(fna));
				} else {
					ast_copy_string(fn, "digits/1N", sizeof(fn));
					snprintf(fna, sizeof(fna), "digits/h-million%s", gender);
				}
			} else {
				res = ast_say_number_full_de(chan, millions, ints, language, options, audiofd, ctrlfd);
				if (res) {
					return res;
				}
				if (num) {
					ast_copy_string(fn, "digits/millions", sizeof(fn));
				} else {
					snprintf(fn, sizeof(fn), "digits/h-million%s", gender);
				}
			}
			t = 1;
		} else if (num < INT_MAX) {
			int billions = num / 1000000000;
			num = num % 1000000000;
			if (billions == 1) {
				if (num) {
					ast_copy_string(fn, "digits/1F", sizeof(fn));
					ast_copy_string(fna, "digits/milliard", sizeof(fna));
				} else {
					ast_copy_string(fn, "digits/1N", sizeof(fn));
					snprintf(fna, sizeof(fna), "digits/h-milliard%s", gender);
				}
			} else {
				res = ast_say_number_full_de(chan, billions, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
				if (num) {
					ast_copy_string(fn, "digits/milliards", sizeof(fna));
				} else {
					snprintf(fn, sizeof(fna), "digits/h-milliard%s", gender);
				}
			}
			t = 1;
		} else if (num == INT_MAX) {
			snprintf(fn, sizeof(fn), "digits/h-last%s", gender);
			num = 0;
		} else {
			ast_debug(1, "Number '%d' is too big for me\n", num);
			res = -1;
		}

		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
			if (!res) {
				if (strlen(fna) != 0 && !ast_streamfile(chan, fna, language)) {
					if ((audiofd > -1) && (ctrlfd > -1)) {
						res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
					} else {
						res = ast_waitstream(chan, ints);
					}
				}
				ast_stopstream(chan);
				strcpy(fna, "");
			}
		}
	}
	return res;
}

static int ast_say_enumeration_full_he(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	char fn[256] = "";
	int mf = -1;				/* +1 = Masculin; -1 = Feminin */
	ast_verb(3, "ast_say_digits_full: started. num: %d, options=\"%s\"\n", num, options);

	if (options && !strncasecmp(options, "m", 1)) {
		mf = -1;
	}

	ast_verb(3, "ast_say_digits_full: num: %d, options=\"%s\", mf=%d\n", num, options, mf);

	while (!res && num) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");	/* kind of senseless for enumerations, but our best effort for error checking */
			if (num > INT_MIN) {
				num = -num;
			} else {
				num = 0;
			}
		} else if (num < 21) {
			if (mf < 0) {
				if (num < 10) {
					snprintf(fn, sizeof(fn), "digits/f-0%d", num);
				} else {
					snprintf(fn, sizeof(fn), "digits/f-%d", num);
				}
			} else {
				if (num < 10) {
					snprintf(fn, sizeof(fn), "digits/m-0%d", num);
				} else {
					snprintf(fn, sizeof(fn), "digits/m-%d", num);
				}
			}
			num = 0;
		} else if ((num < 100) && num >= 20) {
			snprintf(fn, sizeof(fn), "digits/%d", (num / 10) * 10);
			num = num % 10;
		} else if ((num >= 100) && (num < 1000)) {
			int tmpnum = num / 100;
			snprintf(fn, sizeof(fn), "digits/%d00", tmpnum);
			num = num - (tmpnum * 100);
		} else if ((num >= 1000) && (num < 10000)) {
			int tmpnum = num / 1000;
			snprintf(fn, sizeof(fn), "digits/%dk", tmpnum);
			num = num - (tmpnum * 1000);
		} else if (num < 20000) {
			snprintf(fn, sizeof(fn), "digits/m-%d", (num / 1000));
			num = num % 1000;
		} else if (num < 1000000) {
			res = ast_say_number_full_he(chan, num / 1000, ints, language, "m", audiofd, ctrlfd);
			if (res) {
				return res;
			}
			snprintf(fn, sizeof(fn), "digits/1k");
			num = num % 1000;
		} else if (num < 2000000) {
			snprintf(fn, sizeof(fn), "digits/1m");
			num = num % 1000000;
		} else if (num < 3000000) {
			snprintf(fn, sizeof(fn), "digits/2m");
			num = num - 2000000;
		} else if (num < 1000000000) {
			res = ast_say_number_full_he(chan, num / 1000000, ints, language, "m", audiofd, ctrlfd);
			if (res) {
				return res;
			}
			snprintf(fn, sizeof(fn), "digits/1m");
			num = num % 1000000;
		} else {
			ast_debug(1, "Number '%d' is too big for me\n", num);
			res = -1;
		}
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1)) {
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				} else {
					res = ast_waitstream(chan, ints);
				}
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

static int say_date(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	if (!strncasecmp(lang, "en", 2)) {        /* English syntax */
		return ast_say_date_en(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "da", 2)) { /* Danish syntax */
		return ast_say_date_da(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "de", 2)) { /* German syntax */
		return ast_say_date_de(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "fr", 2)) { /* French syntax */
		return ast_say_date_fr(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "ge", 2)) { /* deprecated Georgian syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "ge is not a standard language code.  Please switch to using ka instead.\n");
		}
		return ast_say_date_ka(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "gr", 2)) { /* Greek syntax */
		return ast_say_date_gr(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "ja", 2)) { /* Japanese syntax */
		return ast_say_date_ja(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "he", 2)) { /* Hebrew syntax */
		return ast_say_date_he(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "hu", 2)) { /* Hungarian syntax */
		return ast_say_date_hu(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "ka", 2)) { /* Georgian syntax */
		return ast_say_date_ka(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "nl", 2)) { /* Dutch syntax */
		return ast_say_date_nl(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "pt", 2)) { /* Portuguese syntax */
		return ast_say_date_pt(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "th", 2)) { /* Thai syntax */
		return ast_say_date_th(chan, t, ints, lang);
	}

	/* Default to English */
	return ast_say_date_en(chan, t, ints, lang);
}

/*! \brief English syntax */
int ast_say_date_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct ast_tm tm;
	struct timeval when = { t, 0 };
	char fn[256];
	int res = 0;
	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char * ) NULL);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	return res;
}

/*! \brief Danish syntax */
int ast_say_date_da(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_say_enumeration(chan, tm.tm_mday, ints, lang, (char * ) NULL);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res) {
		/* Year */
		int year = tm.tm_year + 1900;
		if (year > 1999) {	/* year 2000 and later */
			res = ast_say_number(chan, year, ints, lang, (char *) NULL);
		} else {
			if (year < 1100) {
				/* I'm not going to handle 1100 and prior */
				/* We'll just be silent on the year, instead of bombing out. */
			} else {
			    /* year 1100 to 1999. will anybody need this?!? */
				snprintf(fn, sizeof(fn), "digits/%d", (year / 100));
				res = wait_file(chan, ints, fn, lang);
				if (!res) {
					res = wait_file(chan, ints, "digits/hundred", lang);
					if (!res && year % 100 != 0) {
						res = ast_say_number(chan, (year % 100), ints, lang, (char *) NULL);
					}
				}
			}
		}
	}
	return res;
}

/*! \brief German syntax */
int ast_say_date_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_say_enumeration(chan, tm.tm_mday, ints, lang, (char * ) NULL);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res) {
		/* Year */
		int year = tm.tm_year + 1900;
		if (year > 1999) {	/* year 2000 and later */
			res = ast_say_number(chan, year, ints, lang, (char *) NULL);
		} else {
			if (year < 1100) {
				/* I'm not going to handle 1100 and prior */
				/* We'll just be silent on the year, instead of bombing out. */
			} else {
			    /* year 1100 to 1999. will anybody need this?!? */
			    /* say 1967 as 'neunzehn hundert sieben und sechzig' */
				snprintf(fn, sizeof(fn), "digits/%d", (year / 100) );
				res = wait_file(chan, ints, fn, lang);
				if (!res) {
					res = wait_file(chan, ints, "digits/hundred", lang);
					if (!res && year % 100 != 0) {
						res = ast_say_number(chan, (year % 100), ints, lang, (char *) NULL);
					}
				}
			}
		}
	}
	return res;
}

/*! \brief Hungarian syntax */
int ast_say_date_hu(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&when, &tm, NULL);

	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		ast_say_number(chan, tm.tm_mday , ints, lang, (char *) NULL);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	return res;
}

/*! \brief French syntax */
int ast_say_date_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char * ) NULL);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	return res;
}

/*! \brief Dutch syntax */
int ast_say_date_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char * ) NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	return res;
}

/*! \brief Thai syntax */
int ast_say_date_th(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		ast_copy_string(fn, "digits/tee", sizeof(fn));
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char * ) NULL);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res) {
		ast_copy_string(fn, "digits/duan", sizeof(fn));
		res = ast_streamfile(chan, fn, lang);
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res){
		ast_copy_string(fn, "digits/posor", sizeof(fn));
		res = ast_streamfile(chan, fn, lang);
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	}
	return res;
}

/*! \brief Portuguese syntax */
int ast_say_date_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;

	ast_localtime(&when, &tm, NULL);
	snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
	if (!res)
		res = wait_file(chan, ints, fn, lang);
	if (!res)
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
	if (!res)
		res = wait_file(chan, ints, "digits/pt-de", lang);
	snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
	if (!res)
		res = wait_file(chan, ints, fn, lang);
	if (!res)
		res = wait_file(chan, ints, "digits/pt-de", lang);
	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);

	return res;
}

/*! \brief Hebrew syntax */
int ast_say_date_he(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res) {
			res = ast_waitstream(chan, ints);
		}
	}
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res) {
			res = ast_waitstream(chan, ints);
		}
	}
	if (!res) {
		res = ast_say_number(chan, tm.tm_mday, ints, lang, "m");
	}
	if (!res) {
		res = ast_waitstream(chan, ints);
	}
	if (!res) {
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, "m");
	}
	return res;
}

static int say_date_with_format(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	if (!strncasecmp(lang, "en", 2)) {      /* English syntax */
		return ast_say_date_with_format_en(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "da", 2)) { /* Danish syntax */
		return ast_say_date_with_format_da(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "de", 2)) { /* German syntax */
		return ast_say_date_with_format_de(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "es", 2)) { /* Spanish syntax */
		return ast_say_date_with_format_es(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "he", 2)) { /* Hebrew syntax */
		return ast_say_date_with_format_he(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "fr", 2)) { /* French syntax */
		return ast_say_date_with_format_fr(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "gr", 2)) { /* Greek syntax */
		return ast_say_date_with_format_gr(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "ja", 2)) { /* Japanese syntax */
		return ast_say_date_with_format_ja(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "it", 2)) { /* Italian syntax */
		return ast_say_date_with_format_it(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "mx", 2)) { /* deprecated Mexican syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "mx is not a standard language code.  Please switch to using es_MX instead.\n");
		}
		return ast_say_date_with_format_es(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "nl", 2)) { /* Dutch syntax */
		return ast_say_date_with_format_nl(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "pl", 2)) { /* Polish syntax */
		return ast_say_date_with_format_pl(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "pt", 2)) { /* Portuguese syntax */
		return ast_say_date_with_format_pt(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "th", 2)) { /* Thai syntax */
		return ast_say_date_with_format_th(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "tw", 2)) { /* deprecated Taiwanese syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "tw is a standard language code for Twi, not Taiwanese.  Please switch to using zh_TW instead.\n");
		}
		return ast_say_date_with_format_zh(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "zh", 2)) { /* Taiwanese / Chinese syntax */
		return ast_say_date_with_format_zh(chan, t, ints, lang, format, tzone);
	} else if (!strncasecmp(lang, "vi", 2)) { /* Vietnamese syntax */
		return ast_say_date_with_format_vi(chan, t, ints, lang, format, tzone);
	}

	/* Default to English */
	return ast_say_date_with_format_en(chan, t, ints, lang, format, tzone);
}

/*! \brief English syntax */
int ast_say_date_with_format_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (format == NULL)
		format = "ABdY 'digits/at' IMp";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* Month enumerated */
				res = ast_say_enumeration(chan, (tm.tm_mon + 1), ints, lang, (char *) NULL);
				break;
			case 'd':
			case 'e':
				/* First - Thirtyfirst */
				res = ast_say_enumeration(chan, tm.tm_mday, ints, lang, (char *) NULL);
				break;
			case 'Y':
				/* Year */
				if (tm.tm_year > 99) {
					res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
				} else if (tm.tm_year < 1) {
					/* I'm not going to handle 1900 and prior */
					/* We'll just be silent on the year, instead of bombing out. */
				} else {
					res = wait_file(chan, ints, "digits/19", lang);
					if (!res) {
						if (tm.tm_year <= 9) {
							/* 1901 - 1909 */
							res = wait_file(chan, ints, "digits/oh", lang);
						}

						res |= ast_say_number(chan, tm.tm_year, ints, lang, (char *) NULL);
					}
				}
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/12", sizeof(nextmsg));
				else if (tm.tm_hour > 12)
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				if (format[offset] == 'H') {
					/* e.g. oh-eight */
					if (tm.tm_hour < 10) {
						res = wait_file(chan, ints, "digits/oh", lang);
					}
				} else {
					/* e.g. eight */
					if (tm.tm_hour == 0) {
						res = wait_file(chan, ints, "digits/oh", lang);
					}
				}
				if (!res) {
					if (tm.tm_hour != 0) {
						int remaining = tm.tm_hour;
						if (tm.tm_hour > 20) {
							res = wait_file(chan, ints, "digits/20", lang);
							remaining -= 20;
						}
						if (!res) {
							snprintf(nextmsg, sizeof(nextmsg), "digits/%d", remaining);
							res = wait_file(chan, ints, nextmsg, lang);
						}
					}
				}
				break;
			case 'M':
			case 'N':
				/* Minute */
				if (tm.tm_min == 0) {
					if (format[offset] == 'M') {
						res = wait_file(chan, ints, "digits/oclock", lang);
					} else {
						res = wait_file(chan, ints, "digits/hundred", lang);
					}
				} else if (tm.tm_min < 10) {
					res = wait_file(chan, ints, "digits/oh", lang);
					if (!res) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_min);
						res = wait_file(chan, ints, nextmsg, lang);
					}
				} else {
					res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
				}
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 11)
					ast_copy_string(nextmsg, "digits/p-m", sizeof(nextmsg));
				else
					ast_copy_string(nextmsg, "digits/a-m", sizeof(nextmsg));
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					gettimeofday(&now, NULL);
					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "A", tzone);
					} else if (beg_today - 2628000 < t) {
						/* Less than a month ago - "Sunday, October third" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "ABd", tzone);
					} else if (beg_today - 15768000 < t) {
						/* Less than 6 months ago - "August seventh" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "Bd", tzone);
					} else {
						/* More than 6 months ago - "April nineteenth two thousand three" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "BdY", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now;
					struct ast_tm tmnow;
					time_t beg_today;

					now = ast_tvnow();
					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "A", tzone);
					} else if (beg_today - 2628000 < t) {
						/* Less than a month ago - "Sunday, October third" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "ABd", tzone);
					} else if (beg_today - 15768000 < t) {
						/* Less than 6 months ago - "August seventh" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "Bd", tzone);
					} else {
						/* More than 6 months ago - "April nineteenth two thousand three" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "BdY", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_en(chan, t, ints, lang, "HM", tzone);
				break;
			case 'S':
				/* Seconds */
				if (tm.tm_sec == 0) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
					res = wait_file(chan, ints, nextmsg, lang);
				} else if (tm.tm_sec < 10) {
					res = wait_file(chan, ints, "digits/oh", lang);
					if (!res) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
						res = wait_file(chan, ints, nextmsg, lang);
					}
				} else {
					res = ast_say_number(chan, tm.tm_sec, ints, lang, (char *) NULL);
				}
				break;
			case 'T':
				res = ast_say_date_with_format_en(chan, t, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

static char next_item(const char *format)
{
	const char *next = ast_skip_blanks(format);
	return *next;
}

/*! \brief Danish syntax */
int ast_say_date_with_format_da(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (!format)
		format = "A dBY HMS";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* Month enumerated */
				res = ast_say_enumeration(chan, (tm.tm_mon + 1), ints, lang, "m");
				break;
			case 'd':
			case 'e':
				/* First - Thirtyfirst */
				res = ast_say_enumeration(chan, tm.tm_mday, ints, lang, "m");
				break;
			case 'Y':
				/* Year */
				{
					int year = tm.tm_year + 1900;
					if (year > 1999) {	/* year 2000 and later */
						res = ast_say_number(chan, year, ints, lang, (char *) NULL);
					} else {
						if (year < 1100) {
							/* I'm not going to handle 1100 and prior */
							/* We'll just be silent on the year, instead of bombing out. */
						} else {
						    /* year 1100 to 1999. will anybody need this?!? */
						    /* say 1967 as 'nineteen hundred seven and sixty' */
							snprintf(nextmsg, sizeof(nextmsg), "digits/%d", (year / 100) );
							res = wait_file(chan, ints, nextmsg, lang);
							if (!res) {
								res = wait_file(chan, ints, "digits/hundred", lang);
								if (!res && year % 100 != 0) {
									res = ast_say_number(chan, (year % 100), ints, lang, (char *) NULL);
								}
							}
						}
					}
				}
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				res = wait_file(chan, ints, "digits/oclock", lang);
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/12", sizeof(nextmsg));
				else if (tm.tm_hour > 12)
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
				if (!res) {
					res = wait_file(chan, ints, nextmsg, lang);
				}
				break;
			case 'H':
				/* 24-Hour, single digit hours preceded by "oh" (0) */
				if (tm.tm_hour < 10 && tm.tm_hour > 0) {
					res = wait_file(chan, ints, "digits/0", lang);
				}
				/* FALLTRHU */
			case 'k':
				/* 24-Hour */
				res = ast_say_number(chan, tm.tm_hour, ints, lang, (char *) NULL);
				break;
			case 'M':
				/* Minute */
				if (tm.tm_min > 0 || next_item(&format[offset + 1]) == 'S') { /* zero 'digits/0' only if seconds follow */
					res = ast_say_number(chan, tm.tm_min, ints, lang, "f");
				}
				if (!res && next_item(&format[offset + 1]) == 'S') { /* minutes only if seconds follow */
					if (tm.tm_min == 1) {
						res = wait_file(chan, ints, "digits/minute", lang);
					} else {
						res = wait_file(chan, ints, "digits/minutes", lang);
					}
				}
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 11)
					ast_copy_string(nextmsg, "digits/p-m", sizeof(nextmsg));
				else
					ast_copy_string(nextmsg, "digits/a-m", sizeof(nextmsg));
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or AdBY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else {
						res = ast_say_date_with_format_da(chan, t, ints, lang, "AdBY", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or AdBY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_da(chan, t, ints, lang, "A", tzone);
					} else {
						res = ast_say_date_with_format_da(chan, t, ints, lang, "AdBY", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_da(chan, t, ints, lang, "HM", tzone);
				break;
			case 'S':
				/* Seconds */
				res = wait_file(chan, ints, "digits/and", lang);
				if (!res) {
					res = ast_say_number(chan, tm.tm_sec, ints, lang, "f");
					if (!res) {
						res = wait_file(chan, ints, "digits/seconds", lang);
					}
				}
				break;
			case 'T':
				res = ast_say_date_with_format_da(chan, t, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

/*! \brief German syntax */
int ast_say_date_with_format_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (!format)
		format = "A dBY HMS";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* Month enumerated */
				res = ast_say_enumeration(chan, (tm.tm_mon + 1), ints, lang, "m");
				break;
			case 'd':
			case 'e':
				/* First - Thirtyfirst */
				res = ast_say_enumeration(chan, tm.tm_mday, ints, lang, "m");
				break;
			case 'Y':
				/* Year */
				{
					int year = tm.tm_year + 1900;
					if (year > 1999) {	/* year 2000 and later */
						res = ast_say_number(chan, year, ints, lang, (char *) NULL);
					} else {
						if (year < 1100) {
							/* I'm not going to handle 1100 and prior */
							/* We'll just be silent on the year, instead of bombing out. */
						} else {
						    /* year 1100 to 1999. will anybody need this?!? */
						    /* say 1967 as 'neunzehn hundert sieben und sechzig' */
							snprintf(nextmsg, sizeof(nextmsg), "digits/%d", (year / 100) );
							res = wait_file(chan, ints, nextmsg, lang);
							if (!res) {
								res = wait_file(chan, ints, "digits/hundred", lang);
								if (!res && year % 100 != 0) {
									res = ast_say_number(chan, (year % 100), ints, lang, (char *) NULL);
								}
							}
						}
					}
				}
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/12", sizeof(nextmsg));
				else if (tm.tm_hour > 12)
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan, ints, nextmsg, lang);
				if (!res) {
					res = wait_file(chan, ints, "digits/oclock", lang);
				}
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				res = ast_say_number(chan, tm.tm_hour, ints, lang, (char *) NULL);
				if (!res) {
					res = wait_file(chan, ints, "digits/oclock", lang);
				}
				break;
			case 'M':
				/* Minute */
				if (next_item(&format[offset + 1]) == 'S') { /* zero 'digits/0' only if seconds follow */
					res = ast_say_number(chan, tm.tm_min, ints, lang, "f"); /* female only if we say digits/minutes */
				} else if (tm.tm_min > 0) {
					res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
				}

				if (!res && next_item(&format[offset + 1]) == 'S') { /* minutes only if seconds follow */
					if (tm.tm_min == 1) {
						res = wait_file(chan, ints, "digits/minute", lang);
					} else {
						res = wait_file(chan, ints, "digits/minutes", lang);
					}
				}
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 11)
					ast_copy_string(nextmsg, "digits/p-m", sizeof(nextmsg));
				else
					ast_copy_string(nextmsg, "digits/a-m", sizeof(nextmsg));
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or AdBY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else {
						res = ast_say_date_with_format_de(chan, t, ints, lang, "AdBY", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or AdBY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_de(chan, t, ints, lang, "A", tzone);
					} else {
						res = ast_say_date_with_format_de(chan, t, ints, lang, "AdBY", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_de(chan, t, ints, lang, "HM", tzone);
				break;
			case 'S':
				/* Seconds */
				res = wait_file(chan, ints, "digits/and", lang);
				if (!res) {
					res = ast_say_number(chan, tm.tm_sec, ints, lang, "f");
					if (!res) {
						res = wait_file(chan, ints, tm.tm_sec == 1 ? "digits/second" : "digits/seconds", lang);
					}
				}
				break;
			case 'T':
				res = ast_say_date_with_format_de(chan, t, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

/*! \brief Thai syntax */
int ast_say_date_with_format_th(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (format == NULL)
		format = "a 'digits/tee' e 'digits/duan' hY  I 'digits/naliga' M 'digits/natee'";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* Month enumerated */
				res = ast_say_number(chan, (tm.tm_mon + 1), ints, lang, (char *) NULL);
				break;
			case 'd':
			case 'e':
				/* First - Thirtyfirst */
				res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
				break;
			case 'Y':
				/* Year */
				res = ast_say_number(chan, tm.tm_year + 1900 + 543, ints, lang, (char *) NULL);
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/24", sizeof(nextmsg));
				snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/24", sizeof(nextmsg));
				snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'M':
			case 'N':
				res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
				break;
			case 'P':
			case 'p':
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "A", tzone);
					} else if (beg_today - 2628000 < t) {
						/* Less than a month ago - "Sunday, October third" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "ABd", tzone);
					} else if (beg_today - 15768000 < t) {
						/* Less than 6 months ago - "August seventh" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "Bd", tzone);
					} else {
						/* More than 6 months ago - "April nineteenth two thousand three" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "BdY", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "A", tzone);
					} else if (beg_today - 2628000 < t) {
						/* Less than a month ago - "Sunday, October third" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "ABd", tzone);
					} else if (beg_today - 15768000 < t) {
						/* Less than 6 months ago - "August seventh" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "Bd", tzone);
					} else {
						/* More than 6 months ago - "April nineteenth two thousand three" */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "BdY", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_en(chan, t, ints, lang, "HM", tzone);
				break;
			case 'S':
				res = ast_say_number(chan, tm.tm_sec, ints, lang, (char *) NULL);
				break;
			case 'T':
				res = ast_say_date_with_format_en(chan, t, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

/*! \brief ast_say_date_with_format_he Say formatted date in Hebrew
 *
 * \ref ast_say_date_with_format_en for the details of the options
 *
 * Changes from the English version:
 *
 * - don't replicate in here the logic of ast_say_number_full_he
 *
 * - year is always 4-digit (because it's simpler)
 *
 * - added c, x, and X. Mainly for my tests
 *
 * - The standard "long" format used in Hebrew is AdBY, rather than ABdY
 *
 * \todo
 * - A "ha" is missing in the standard date format, before the 'd'.
 * - The numbers of 3000--19000 are not handled well
 */
int ast_say_date_with_format_he(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
#define IL_DATE_STR "AdBY"
#define IL_TIME_STR "HM"		/* NOTE: In Hebrew we do not support 12 hours, only 24. No AM or PM exists in the Hebrew language */
#define IL_DATE_STR_FULL IL_DATE_STR " 'digits/at' " IL_TIME_STR
	/* TODO: This whole function is cut&paste from
	 * ast_say_date_with_format_en . Is that considered acceptable?
	 **/
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (!format) {
		format = IL_DATE_STR_FULL;
	}

	ast_localtime(&when, &tm, tzone);

	for (offset = 0; format[offset] != '\0'; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'd':
			case 'e': /* Day of the month */
				/* I'm not sure exactly what the parameters
				* audiofd and ctrlfd to
				* ast_say_number_full_he mean, but it seems
				* safe to pass -1 there.
				*
				* At least in one of the pathes :-(
				*/
				res = ast_say_number_full_he(chan, tm.tm_mday, ints, lang, "m", -1, -1);
				break;
			case 'Y': /* Year */
				res = ast_say_number_full_he(chan, tm.tm_year + 1900, ints, lang, "f", -1, -1);
				break;
			case 'I':
			case 'l': /* 12-Hour -> we do not support 12 hour based langauges in Hebrew */
			case 'H':
			case 'k': /* 24-Hour */
				res = ast_say_number_full_he(chan, tm.tm_hour, ints, lang, "f", -1, -1);
				break;
			case 'M': /* Minute */
				if (tm.tm_min >= 0 && tm.tm_min <= 9)	/* say a leading zero if needed */
					res = ast_say_number_full_he(chan, 0, ints, lang, "f", -1, -1);
				res = ast_say_number_full_he(chan, tm.tm_min, ints, lang, "f", -1, -1);
				break;
			case 'P':
			case 'p':
				/* AM/PM - There is no AM/PM in Hebrew... */
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or "date" */
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A
				 * (weekday), or "date" */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;
					char todo = format[offset]; /* The letter to format*/

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						if (todo == 'Q') {
							res = wait_file(chan, ints, "digits/today", lang);
						}
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if ((todo != 'Q') && (beg_today - 86400 * 6 < t)) {
						/* Within the last week */
						res = ast_say_date_with_format_he(chan, t, ints, lang, "A", tzone);
					} else {
						res = ast_say_date_with_format_he(chan, t, ints, lang, IL_DATE_STR, tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_he(chan, t, ints, lang, "HM", tzone);
				break;
			case 'S': /* Seconds */
				res = ast_say_number_full_he(chan, tm.tm_sec,
					ints, lang, "f", -1, -1
				);
				break;
			case 'T':
				res = ast_say_date_with_format_he(chan, t, ints, lang, "HMS", tzone);
				break;
			/* c, x, and X seem useful for testing. Not sure
			 * if they're good for the general public */
			case 'c':
				res = ast_say_date_with_format_he(chan, t, ints, lang, IL_DATE_STR_FULL, tzone);
				break;
			case 'x':
				res = ast_say_date_with_format_he(chan, t, ints, lang, IL_DATE_STR, tzone);
				break;
			case 'X': /* Currently not locale-dependent...*/
				res = ast_say_date_with_format_he(chan, t, ints, lang, IL_TIME_STR, tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}


/*! \brief Spanish syntax */
int ast_say_date_with_format_es(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (format == NULL)
		format = "'digits/es-el' Ad 'digits/es-de' B 'digits/es-de' Y 'digits/at' IMp";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				snprintf(nextmsg, sizeof(nextmsg), "%s", sndfile);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* First - Twelfth */
				snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'd':
			case 'e':
				/* First - Thirtyfirst */
				res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
				break;
			case 'Y':
				/* Year */
				res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/12", sizeof(nextmsg));
				else if (tm.tm_hour == 1 || tm.tm_hour == 13)
					snprintf(nextmsg,sizeof(nextmsg), "digits/1F");
				else if (tm.tm_hour > 12)
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				res = ast_say_number(chan, tm.tm_hour, ints, lang, NULL);
				break;
			case 'M':
				/* Minute */
				res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 18)
					res = wait_file(chan, ints, "digits/p-m", lang);
				else if (tm.tm_hour > 12)
					res = wait_file(chan, ints, "digits/afternoon", lang);
				else if (tm.tm_hour)
					res = wait_file(chan, ints, "digits/a-m", lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else {
						res = ast_say_date_with_format_es(chan, t, ints, lang, "'digits/es-el' Ad 'digits/es-de' B 'digits/es-de' Y", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_es(chan, t, ints, lang, "A", tzone);
					} else {
						res = ast_say_date_with_format_es(chan, t, ints, lang, "'digits/es-el' Ad 'digits/es-de' B 'digits/es-de' Y", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_es(chan, t, ints, lang, "H 'digits/y' M", tzone);
				break;
			case 'S':
				/* Seconds */
				if (tm.tm_sec == 0) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
					res = wait_file(chan, ints, nextmsg, lang);
				} else if (tm.tm_sec < 10) {
					res = wait_file(chan, ints, "digits/oh", lang);
					if (!res) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
						res = wait_file(chan, ints, nextmsg, lang);
					}
				} else if ((tm.tm_sec < 21) || (tm.tm_sec % 10 == 0)) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
					res = wait_file(chan, ints, nextmsg, lang);
				} else {
					int ten, one;
					ten = (tm.tm_sec / 10) * 10;
					one = (tm.tm_sec % 10);
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", ten);
					res = wait_file(chan, ints, nextmsg, lang);
					if (!res) {
						/* Fifty, not fifty-zero */
						if (one != 0) {
							snprintf(nextmsg, sizeof(nextmsg), "digits/%d", one);
							res = wait_file(chan, ints, nextmsg, lang);
						}
					}
				}
				break;
			case 'T':
				res = ast_say_date_with_format_es(chan, t, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

/*! \brief French syntax
oclock = heure
*/
int ast_say_date_with_format_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (format == NULL)
		format = "AdBY 'digits/at' IMp";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* First - Twelfth */
				snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'd':
			case 'e':
				/* First */
				if (tm.tm_mday == 1) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mday);
					res = wait_file(chan, ints, nextmsg, lang);
				} else {
					res = ast_say_number(chan, tm.tm_mday, ints, lang, (char * ) NULL);
				}
				break;
			case 'Y':
				/* Year */
				if (tm.tm_year > 99) {
					res = wait_file(chan, ints, "digits/2", lang);
					if (!res) {
						res = wait_file(chan, ints, "digits/thousand", lang);
					}
					if (tm.tm_year > 100) {
						if (!res) {
							res = ast_say_number(chan, tm.tm_year - 100, ints, lang, (char * ) NULL);
						}
					}
				} else {
					if (tm.tm_year < 1) {
						/* I'm not going to handle 1900 and prior */
						/* We'll just be silent on the year, instead of bombing out. */
					} else {
						res = wait_file(chan, ints, "digits/thousand", lang);
						if (!res) {
							wait_file(chan, ints, "digits/9", lang);
							wait_file(chan, ints, "digits/hundred", lang);
							res = ast_say_number(chan, tm.tm_year, ints, lang, (char * ) NULL);
						}
					}
				}
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/12", sizeof(nextmsg));
				else if (tm.tm_hour > 12)
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan, ints, nextmsg, lang);
				if (!res)
					res = wait_file(chan, ints, "digits/oclock", lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				res = ast_say_number(chan, tm.tm_hour, ints, lang, (char * ) NULL);
				if (!res)
					res = wait_file(chan, ints, "digits/oclock", lang);
				break;
			case 'M':
				/* Minute */
				if (tm.tm_min == 0) {
					break;
				}
				res = ast_say_number(chan, tm.tm_min, ints, lang, (char * ) NULL);
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 11)
					ast_copy_string(nextmsg, "digits/p-m", sizeof(nextmsg));
				else
					ast_copy_string(nextmsg, "digits/a-m", sizeof(nextmsg));
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or AdBY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else {
						res = ast_say_date_with_format_fr(chan, t, ints, lang, "AdBY", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or AdBY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_fr(chan, t, ints, lang, "A", tzone);
					} else {
						res = ast_say_date_with_format_fr(chan, t, ints, lang, "AdBY", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_fr(chan, t, ints, lang, "HM", tzone);
				break;
			case 'S':
				/* Seconds */
				res = ast_say_number(chan, tm.tm_sec, ints, lang, (char * ) NULL);
				if (!res) {
					res = wait_file(chan, ints, "digits/second", lang);
				}
				break;
			case 'T':
				res = ast_say_date_with_format_fr(chan, t, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

/*! \brief Italian syntax */
int ast_say_date_with_format_it(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (format == NULL)
		format = "AdB 'digits/at' IMp";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* First - Twelfth */
				snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'd':
			case 'e':
				/* First day of the month is spelled as ordinal */
				if (tm.tm_mday == 1) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mday);
					res = wait_file(chan, ints, nextmsg, lang);
				} else {
					if (!res) {
						res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
					}
				}
				break;
			case 'Y':
				/* Year */
				if (tm.tm_year > 99) {
					res = wait_file(chan, ints, "digits/ore-2000", lang);
					if (tm.tm_year > 100) {
						if (!res) {
						/* This works until the end of 2021 */
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year - 100);
						res = wait_file(chan, ints, nextmsg, lang);
						}
					}
				} else {
					if (tm.tm_year < 1) {
						/* I'm not going to handle 1900 and prior */
						/* We'll just be silent on the year, instead of bombing out. */
					} else {
						res = wait_file(chan, ints, "digits/ore-1900", lang);
						if ((!res) && (tm.tm_year != 0)) {
							if (tm.tm_year <= 21) {
								/* 1910 - 1921 */
								snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year);
								res = wait_file(chan, ints, nextmsg, lang);
							} else {
								/* 1922 - 1999, but sounds badly in 1928, 1931, 1938, etc... */
								int ten, one;
								ten = tm.tm_year / 10;
								one = tm.tm_year % 10;
								snprintf(nextmsg, sizeof(nextmsg), "digits/%d", ten * 10);
								res = wait_file(chan, ints, nextmsg, lang);
								if (!res) {
									if (one != 0) {
										snprintf(nextmsg, sizeof(nextmsg), "digits/%d", one);
										res = wait_file(chan, ints, nextmsg, lang);
									}
								}
							}
						}
					}
				}
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/12", sizeof(nextmsg));
				else if (tm.tm_hour > 12)
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
					res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				if (tm.tm_hour == 0) {
					res = wait_file(chan, ints, "digits/ore-mezzanotte", lang);
				} else if (tm.tm_hour == 1) {
					res = wait_file(chan, ints, "digits/ore-una", lang);
				} else {
					res = ast_say_number(chan, tm.tm_hour, ints, lang, (char *) NULL);
				}
				break;
			case 'M':
				/* Minute */
				res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 11)
					ast_copy_string(nextmsg, "digits/p-m", sizeof(nextmsg));
				else
					ast_copy_string(nextmsg, "digits/a-m", sizeof(nextmsg));
					res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else {
						res = ast_say_date_with_format_it(chan, t, ints, lang, "AdB", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_it(chan, t, ints, lang, "A", tzone);
					} else {
						res = ast_say_date_with_format_it(chan, t, ints, lang, "AdB", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_it(chan, t, ints, lang, "HM", tzone);
				break;
			case 'S':
				/* Seconds */
				if (tm.tm_sec == 0) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
					res = wait_file(chan, ints, nextmsg, lang);
				} else if (tm.tm_sec < 10) {
					res = wait_file(chan, ints, "digits/oh", lang);
					if (!res) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
						res = wait_file(chan, ints, nextmsg, lang);
					}
				} else if ((tm.tm_sec < 21) || (tm.tm_sec % 10 == 0)) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
					res = wait_file(chan, ints, nextmsg, lang);
				} else {
					int ten, one;
					ten = (tm.tm_sec / 10) * 10;
					one = (tm.tm_sec % 10);
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", ten);
					res = wait_file(chan, ints, nextmsg, lang);
					if (!res) {
						/* Fifty, not fifty-zero */
						if (one != 0) {
							snprintf(nextmsg, sizeof(nextmsg), "digits/%d", one);
							res = wait_file(chan, ints, nextmsg, lang);
						}
					}
				}
				break;
			case 'T':
				res = ast_say_date_with_format_it(chan, t, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

/*! \brief Dutch syntax */
int ast_say_date_with_format_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (format == NULL)
		format = "AdBY 'digits/at' IMp";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* First - Twelfth */
				snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'd':
			case 'e':
				/* First - Thirtyfirst */
				res = ast_say_number(chan, tm.tm_mday, ints, lang, NULL);
				break;
			case 'Y':
				/* Year */
				if (tm.tm_year > 99) {
					res = wait_file(chan, ints, "digits/2", lang);
					if (!res) {
						res = wait_file(chan, ints, "digits/thousand", lang);
					}
					if (tm.tm_year > 100) {
						if (!res) {
							/* This works until the end of 2020 */
							snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year - 100);
							res = wait_file(chan, ints, nextmsg, lang);
						}
					}
				} else {
					if (tm.tm_year < 1) {
						/* I'm not going to handle 1900 and prior */
						/* We'll just be silent on the year, instead of bombing out. */
					} else {
						res = wait_file(chan, ints, "digits/19", lang);
						if (!res) {
							if (tm.tm_year <= 9) {
								/* 1901 - 1909 */
								res = wait_file(chan, ints, "digits/oh", lang);
								if (!res) {
									snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year);
									res = wait_file(chan, ints, nextmsg, lang);
								}
							} else if (tm.tm_year <= 20) {
								/* 1910 - 1920 */
								snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year);
								res = wait_file(chan, ints, nextmsg, lang);
							} else {
								/* 1921 - 1999 */
								int ten, one;
								ten = tm.tm_year / 10;
								one = tm.tm_year % 10;
								snprintf(nextmsg, sizeof(nextmsg), "digits/%d", ten * 10);
								res = wait_file(chan, ints, nextmsg, lang);
								if (!res) {
									if (one != 0) {
										snprintf(nextmsg, sizeof(nextmsg), "digits/%d", one);
										res = wait_file(chan, ints, nextmsg, lang);
									}
								}
							}
						}
					}
				}
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/12", sizeof(nextmsg));
				else if (tm.tm_hour > 12)
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				res = ast_say_number(chan, tm.tm_hour, ints, lang, (char *) NULL);
				if (!res) {
					res = wait_file(chan, ints, "digits/nl-uur", lang);
				}
				break;
			case 'M':
				/* Minute */
				res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 11)
					ast_copy_string(nextmsg, "digits/p-m", sizeof(nextmsg));
				else
					ast_copy_string(nextmsg, "digits/a-m", sizeof(nextmsg));
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or AdBY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else {
						res = ast_say_date_with_format_nl(chan, t, ints, lang, "AdBY", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or AdBY */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_nl(chan, t, ints, lang, "A", tzone);
					} else {
						res = ast_say_date_with_format_nl(chan, t, ints, lang, "AdBY", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_nl(chan, t, ints, lang, "HM", tzone);
				break;
			case 'S':
				/* Seconds */
				res = ast_say_number(chan, tm.tm_sec, ints, lang, (char *) NULL);
				break;
			case 'T':
				res = ast_say_date_with_format_nl(chan, t, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

/*! \brief Polish syntax */
int ast_say_date_with_format_pl(struct ast_channel *chan, time_t thetime, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { thetime, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	ast_localtime(&when, &tm, tzone);

	for (offset = 0 ; format[offset] != '\0' ; offset++) {
		int remaining;
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* Month enumerated */
				res = ast_say_enumeration(chan, (tm.tm_mon + 1), ints, lang, NULL);
				break;
			case 'd':
			case 'e':
				/* First - Thirtyfirst */
				remaining = tm.tm_mday;
				if (tm.tm_mday > 30) {
					res = wait_file(chan, ints, "digits/h-30", lang);
					remaining -= 30;
				}
				if (tm.tm_mday > 20 && tm.tm_mday < 30) {
					res = wait_file(chan, ints, "digits/h-20", lang);
					remaining -= 20;
				}
				if (!res) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", remaining);
					res = wait_file(chan, ints, nextmsg, lang);
				}
				break;
			case 'Y':
				/* Year */
				if (tm.tm_year > 100) {
					res = wait_file(chan, ints, "digits/2", lang);
					if (!res)
						res = wait_file(chan, ints, "digits/1000.2", lang);
					if (tm.tm_year > 100) {
						if (!res)
							res = ast_say_enumeration(chan, tm.tm_year - 100, ints, lang, NULL);
					}
				} else if (tm.tm_year == 100) {
					res = wait_file(chan, ints, "digits/h-2000", lang);
				} else {
					if (tm.tm_year < 1) {
						/* I'm not going to handle 1900 and prior */
						/* We'll just be silent on the year, instead of bombing out. */
						break;
					} else {
						res = wait_file(chan, ints, "digits/1000", lang);
						if (!res) {
							wait_file(chan, ints, "digits/900", lang);
							res = ast_say_enumeration(chan, tm.tm_year, ints, lang, NULL);
						}
					}
				}
				if (!res)
					wait_file(chan, ints, "digits/year", lang);
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/t-12", sizeof(nextmsg));
				else if (tm.tm_hour > 12)
					snprintf(nextmsg, sizeof(nextmsg), "digits/t-%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg, sizeof(nextmsg), "digits/t-%d", tm.tm_hour);

				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				if (tm.tm_hour != 0) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/t-%d", tm.tm_hour);
					res = wait_file(chan, ints, nextmsg, lang);
				} else
					res = wait_file(chan, ints, "digits/t-24", lang);
				break;
			case 'M':
			case 'N':
				/* Minute */
				if (tm.tm_min == 0) {
					if (format[offset] == 'M') {
						res = wait_file(chan, ints, "digits/oclock", lang);
					} else {
						res = wait_file(chan, ints, "digits/100", lang);
					}
				} else
					res = ast_say_number(chan, tm.tm_min, ints, lang, "f");
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 11)
					ast_copy_string(nextmsg, "digits/p-m", sizeof(nextmsg));
				else
					ast_copy_string(nextmsg, "digits/a-m", sizeof(nextmsg));
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or AdBY */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < thetime) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < thetime) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else {
						res = ast_say_date_with_format(chan, thetime, ints, lang, "AdBY", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or AdBY */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < thetime) {
						/* Today */
					} else if ((beg_today - 86400) < thetime) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < thetime) {
						/* Within the last week */
						res = ast_say_date_with_format(chan, thetime, ints, lang, "A", tzone);
					} else {
						res = ast_say_date_with_format(chan, thetime, ints, lang, "AdBY", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format(chan, thetime, ints, lang, "HM", tzone);
				break;
			case 'S':
				/* Seconds */
				res = wait_file(chan, ints, "digits/and", lang);
				if (!res) {
					if (tm.tm_sec == 1) {
						res = wait_file(chan, ints, "digits/1z", lang);
						if (!res)
							res = wait_file(chan, ints, "digits/second-a", lang);
					} else {
						res = ast_say_number(chan, tm.tm_sec, ints, lang, "f");
						if (!res) {
							int ten, one;
							ten = tm.tm_sec / 10;
							one = tm.tm_sec % 10;

							if (one > 1 && one < 5 && ten != 1)
								res = wait_file(chan, ints, "digits/seconds", lang);
							else
								res = wait_file(chan, ints, "digits/second", lang);
						}
					}
				}
				break;
			case 'T':
				res = ast_say_date_with_format(chan, thetime, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res)
			break;
	}
	return res;
}

/*! \brief Portuguese syntax */
int ast_say_date_with_format_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (format == NULL)
		format = "Ad 'digits/pt-de' B 'digits/pt-de' Y I 'digits/pt-e' Mp";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				snprintf(nextmsg, sizeof(nextmsg), "%s", sndfile);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* First - Twelfth */
				if (!strcasecmp(lang, "pt_BR")) {
					res = ast_say_number(chan, tm.tm_mon+1, ints, lang, (char *) NULL);
				} else {
					snprintf(nextmsg, sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
					res = wait_file(chan, ints, nextmsg, lang);
				}
				break;
			case 'd':
			case 'e':
				/* First - Thirtyfirst */
				res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
				break;
			case 'Y':
				/* Year */
				res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (!strcasecmp(lang, "pt_BR")) {
					if (tm.tm_hour == 0) {
						if (format[offset] == 'I')
							res = wait_file(chan, ints, "digits/pt-a", lang);
						if (!res)
							res = wait_file(chan, ints, "digits/pt-meianoite", lang);
					} else if (tm.tm_hour == 12) {
						if (format[offset] == 'I')
							res = wait_file(chan, ints, "digits/pt-ao", lang);
						if (!res)
							res = wait_file(chan, ints, "digits/pt-meiodia", lang);
						} else {
						if (format[offset] == 'I') {
							if ((tm.tm_hour % 12) != 1)
								res = wait_file(chan, ints, "digits/pt-as", lang);
							else
								res = wait_file(chan, ints, "digits/pt-a", lang);
						}
						if (!res)
							res = ast_say_number(chan, (tm.tm_hour % 12), ints, lang, "f");
					}
				} else {
					if (tm.tm_hour == 0) {
						if (format[offset] == 'I')
							res = wait_file(chan, ints, "digits/pt-ah", lang);
						if (!res)
							res = wait_file(chan, ints, "digits/pt-meianoite", lang);
						}
					else if (tm.tm_hour == 12) {
						if (format[offset] == 'I')
							res = wait_file(chan, ints, "digits/pt-ao", lang);
						if (!res)
							res = wait_file(chan, ints, "digits/pt-meiodia", lang);
					}
					else {
						if (format[offset] == 'I') {
							res = wait_file(chan, ints, "digits/pt-ah", lang);
							if ((tm.tm_hour % 12) != 1)
								if (!res)
									res = wait_file(chan, ints, "digits/pt-sss", lang);
						}
						if (!res)
							res = ast_say_number(chan, (tm.tm_hour % 12), ints, lang, "f");
					}
				}
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				if (!strcasecmp(lang, "pt_BR")) {
					res = ast_say_number(chan, tm.tm_hour, ints, lang, "f");
					if ((!res) && (format[offset] == 'H')) {
						if (tm.tm_hour > 1) {
							res = wait_file(chan, ints, "digits/hours", lang);
						} else {
							res = wait_file(chan, ints, "digits/hour", lang);
						}
					}
				} else {
					res = ast_say_number(chan, -tm.tm_hour, ints, lang, NULL);
					if (!res) {
						if (tm.tm_hour != 0) {
							int remaining = tm.tm_hour;
							if (tm.tm_hour > 20) {
								res = wait_file(chan, ints, "digits/20", lang);
								remaining -= 20;
							}
							if (!res) {
								snprintf(nextmsg, sizeof(nextmsg), "digits/%d", remaining);
								res = wait_file(chan, ints, nextmsg, lang);
							}
						}
					}
				}
				break;
			case 'M':
				/* Minute */
				if (!strcasecmp(lang, "pt_BR")) {
					res = ast_say_number(chan, tm.tm_min, ints, lang, NULL);
					if (!res) {
						if (tm.tm_min > 1) {
							res = wait_file(chan, ints, "digits/minutes", lang);
						} else {
							res = wait_file(chan, ints, "digits/minute", lang);
						}
					}
				} else {
					if (tm.tm_min == 0) {
						res = wait_file(chan, ints, "digits/pt-hora", lang);
						if (tm.tm_hour != 1)
							if (!res)
								res = wait_file(chan, ints, "digits/pt-sss", lang);
					} else {
						res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
					}
				}
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (!strcasecmp(lang, "pt_BR")) {
					if ((tm.tm_hour != 0) && (tm.tm_hour != 12)) {
						res = wait_file(chan, ints, "digits/pt-da", lang);
						if (!res) {
							if ((tm.tm_hour >= 0) && (tm.tm_hour < 12))
								res = wait_file(chan, ints, "digits/morning", lang);
							else if ((tm.tm_hour >= 12) && (tm.tm_hour < 18))
								res = wait_file(chan, ints, "digits/afternoon", lang);
							else res = wait_file(chan, ints, "digits/night", lang);
						}
					}
				} else {
					if (tm.tm_hour > 12)
						res = wait_file(chan, ints, "digits/p-m", lang);
					else if (tm.tm_hour  && tm.tm_hour < 12)
						res = wait_file(chan, ints, "digits/a-m", lang);
				}
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else {
						res = ast_say_date_with_format_pt(chan, t, ints, lang, "Ad 'digits/pt-de' B 'digits/pt-de' Y", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_pt(chan, t, ints, lang, "A", tzone);
					} else {
						res = ast_say_date_with_format_pt(chan, t, ints, lang, "Ad 'digits/pt-de' B 'digits/pt-de' Y", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_pt(chan, t, ints, lang, "H 'digits/pt-e' M", tzone);
				break;
			case 'S':
				/* Seconds */
				if (!strcasecmp(lang, "pt_BR")) {
					res = ast_say_number(chan, tm.tm_sec, ints, lang, NULL);
					if (!res) {
						if (tm.tm_sec > 1) {
							res = wait_file(chan, ints, "digits/seconds", lang);
						} else {
							res = wait_file(chan, ints, "digits/second", lang);
						}
					}
				} else {
					if (tm.tm_sec == 0) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
						res = wait_file(chan, ints, nextmsg, lang);
					} else if (tm.tm_sec < 10) {
						res = wait_file(chan, ints, "digits/oh", lang);
						if (!res) {
							snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
							res = wait_file(chan, ints, nextmsg, lang);
						}
					} else if ((tm.tm_sec < 21) || (tm.tm_sec % 10 == 0)) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
						res = wait_file(chan, ints, nextmsg, lang);
					} else {
						int ten, one;
						ten = (tm.tm_sec / 10) * 10;
						one = (tm.tm_sec % 10);
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", ten);
						res = wait_file(chan, ints, nextmsg, lang);
						if (!res) {
							/* Fifty, not fifty-zero */
							if (one != 0) {
								snprintf(nextmsg, sizeof(nextmsg), "digits/%d", one);
								res = wait_file(chan, ints, nextmsg, lang);
							}
						}
					}
				}
				break;
			case 'T':
				res = ast_say_date_with_format_pt(chan, t, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

/*! \brief Taiwanese / Chinese syntax */
int ast_say_date_with_format_zh(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (format == NULL)
		format = "YBdAkM";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
			case 'm':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'd':
			case 'e':
				/* First - Thirtyfirst */
				if (!(tm.tm_mday % 10) || (tm.tm_mday < 10)) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_mday);
					res = wait_file(chan, ints, nextmsg, lang);
				} else {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_mday - (tm.tm_mday % 10));
					res = wait_file(chan, ints, nextmsg, lang);
					if (!res) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_mday % 10);
						res = wait_file(chan, ints, nextmsg, lang);
					}
				}
				if (!res) res = wait_file(chan, ints, "digits/day", lang);
				break;
			case 'Y':
				/* Year */
				if (tm.tm_year > 99) {
					res = wait_file(chan, ints, "digits/2", lang);
					if (!res) {
						res = wait_file(chan, ints, "digits/thousand", lang);
					}
					if (tm.tm_year > 100) {
						if (!res) {
							snprintf(nextmsg, sizeof(nextmsg), "digits/%d", (tm.tm_year - 100) / 10);
							res = wait_file(chan, ints, nextmsg, lang);
							if (!res) {
								snprintf(nextmsg, sizeof(nextmsg), "digits/%d", (tm.tm_year - 100) % 10);
								res = wait_file(chan, ints, nextmsg, lang);
							}
						}
					}
					if (!res) {
						res = wait_file(chan, ints, "digits/year", lang);
					}
				} else {
					if (tm.tm_year < 1) {
						/* I'm not going to handle 1900 and prior */
						/* We'll just be silent on the year, instead of bombing out. */
					} else {
						res = wait_file(chan, ints, "digits/1", lang);
						if (!res) {
							res = wait_file(chan, ints, "digits/9", lang);
						}
						if (!res) {
							if (tm.tm_year <= 9) {
								/* 1901 - 1909 */
								res = wait_file(chan, ints, "digits/0", lang);
								if (!res) {
									snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year);
									res = wait_file(chan, ints, nextmsg, lang);
								}
							} else {
								/* 1910 - 1999 */
								snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year / 10);
								res = wait_file(chan, ints, nextmsg, lang);
								if (!res) {
									snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_year % 10);
									res = wait_file(chan, ints, nextmsg, lang);
								}
							}
						}
					}
					if (!res) {
						res = wait_file(chan, ints, "digits/year", lang);
					}
				}
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/12", sizeof(nextmsg));
				else if (tm.tm_hour > 12)
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan, ints, nextmsg, lang);
				if (!res) {
					res = wait_file(chan, ints, "digits/oclock", lang);
				}
				break;
			case 'H':
				if (tm.tm_hour < 10) {
					res = wait_file(chan, ints, "digits/0", lang);
				}
				/* XXX Static analysis warns of no break here. No idea if this is
				 * correct or not
				 */
			case 'k':
				/* 24-Hour */
				if (!(tm.tm_hour % 10) || tm.tm_hour < 10) {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
					res = wait_file(chan, ints, nextmsg, lang);
				} else {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - (tm.tm_hour % 10));
					res = wait_file(chan, ints, nextmsg, lang);
					if (!res) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour % 10);
						res = wait_file(chan, ints, nextmsg, lang);
					}
				}
				if (!res) {
					res = wait_file(chan, ints, "digits/oclock", lang);
				}
				break;
			case 'M':
				/* Minute */
				if (!(tm.tm_min % 10) || tm.tm_min < 10) {
					if (tm.tm_min < 10) {
						res = wait_file(chan, ints, "digits/0", lang);
					}
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_min);
					res = wait_file(chan, ints, nextmsg, lang);
				} else {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_min - (tm.tm_min % 10));
					res = wait_file(chan, ints, nextmsg, lang);
					if (!res) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_min % 10);
						res = wait_file(chan, ints, nextmsg, lang);
					}
				}
				if (!res) {
					res = wait_file(chan, ints, "digits/minute", lang);
				}
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 11)
					ast_copy_string(nextmsg, "digits/p-m", sizeof(nextmsg));
				else
					ast_copy_string(nextmsg, "digits/a-m", sizeof(nextmsg));
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else {
						res = ast_say_date_with_format_zh(chan, t, ints, lang, "YBdA", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_zh(chan, t, ints, lang, "A", tzone);
					} else {
						res = ast_say_date_with_format_zh(chan, t, ints, lang, "YBdA", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_zh(chan, t, ints, lang, "kM", tzone);
				break;
			case 'S':
				/* Seconds */
				if (!(tm.tm_sec % 10) || tm.tm_sec < 10) {
					if (tm.tm_sec < 10) {
						res = wait_file(chan, ints, "digits/0", lang);
					}
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec);
					res = wait_file(chan, ints, nextmsg, lang);
				} else {
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec - (tm.tm_sec % 10));
					res = wait_file(chan, ints, nextmsg, lang);
					if (!res) {
						snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_sec % 10);
						res = wait_file(chan, ints, nextmsg, lang);
					}
				}
				if (!res) {
					res = wait_file(chan, ints, "digits/second", lang);
				}
				break;
			case 'T':
				res = ast_say_date_with_format_zh(chan, t, ints, lang, "HMS", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
			break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

static int say_time(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	if (!strncasecmp(lang, "en", 2)) {	/* English syntax */
		return ast_say_time_en(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "de", 2)) { /* German syntax */
		return ast_say_time_de(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "fr", 2)) { /* French syntax */
		return ast_say_time_fr(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "ge", 2)) { /* deprecated Georgian syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "ge is not a standard language code.  Please switch to using ka instead.\n");
		}
		return ast_say_time_ka(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "gr", 2)) { /* Greek syntax */
		return ast_say_time_gr(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "ja", 2)) { /* Japanese syntax */
		return ast_say_time_ja(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "he", 2)) { /* Hebrew syntax */
		return ast_say_time_he(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "hu", 2)) { /* Hungarian syntax */
		return(ast_say_time_hu(chan, t, ints, lang));
	} else if (!strncasecmp(lang, "ka", 2)) { /* Georgian syntax */
		return ast_say_time_ka(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "nl", 2)) { /* Dutch syntax */
		return ast_say_time_nl(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "pt_BR", 5)) { /* Brazilian Portuguese syntax */
		return ast_say_time_pt_BR(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "pt", 2)) { /* Portuguese syntax */
		return ast_say_time_pt(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "th", 2)) { /* Thai syntax */
		return(ast_say_time_th(chan, t, ints, lang));
	} else if (!strncasecmp(lang, "tw", 2)) { /* deprecated Taiwanese syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "tw is a standard language code for Twi, not Taiwanese.  Please switch to using zh_TW instead.\n");
		}
		return ast_say_time_zh(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "zh", 2)) { /* Taiwanese / Chinese syntax */
		return ast_say_time_zh(chan, t, ints, lang);
	}

	/* Default to English */
	return ast_say_time_en(chan, t, ints, lang);
}

/*! \brief English syntax */
int ast_say_time_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;
	int hour, pm=0;

	ast_localtime(&when, &tm, NULL);
	hour = tm.tm_hour;
	if (!hour)
		hour = 12;
	else if (hour == 12)
		pm = 1;
	else if (hour > 12) {
		hour -= 12;
		pm = 1;
	}
	if (!res)
		res = ast_say_number(chan, hour, ints, lang, (char *) NULL);

	if (tm.tm_min > 9) {
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	} else if (tm.tm_min) {
		if (!res)
			res = ast_streamfile(chan, "digits/oh", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	} else {
		if (!res)
			res = ast_streamfile(chan, "digits/oclock", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (pm) {
		if (!res)
			res = ast_streamfile(chan, "digits/p-m", lang);
	} else {
		if (!res)
			res = ast_streamfile(chan, "digits/a-m", lang);
	}
	if (!res)
		res = ast_waitstream(chan, ints);
	return res;
}

/*! \brief German syntax */
int ast_say_time_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);
	if (!res)
		res = ast_say_number(chan, tm.tm_hour, ints, lang, "n");
	if (!res)
		res = ast_streamfile(chan, "digits/oclock", lang);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
	    if (tm.tm_min > 0)
		res = ast_say_number(chan, tm.tm_min, ints, lang, "f");
	return res;
}

/*! \brief Hungarian syntax */
int ast_say_time_hu(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);
	if (!res)
		res = ast_say_number(chan, tm.tm_hour, ints, lang, "n");
	if (!res)
		res = ast_streamfile(chan, "digits/oclock", lang);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
	    if (tm.tm_min > 0) {
			res = ast_say_number(chan, tm.tm_min, ints, lang, "f");
			if (!res)
				res = ast_streamfile(chan, "digits/minute", lang);
		}
	return res;
}

/*! \brief French syntax */
int ast_say_time_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);

	res = ast_say_number(chan, tm.tm_hour, ints, lang, "f");
	if (!res)
		res = ast_streamfile(chan, "digits/oclock", lang);
	if (tm.tm_min) {
		if (!res)
		res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	}
	return res;
}

/*! \brief Dutch syntax */
int ast_say_time_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);
	if (!res)
		res = ast_say_number(chan, tm.tm_hour, ints, lang, (char *) NULL);
	if (!res)
		res = ast_streamfile(chan, "digits/nl-uur", lang);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
	    if (tm.tm_min > 0)
		res = ast_say_number(chan, tm.tm_min, ints, lang, NULL);
	return res;
}

/*! \brief Portuguese syntax */
int ast_say_time_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;
	int hour;

	ast_localtime(&when, &tm, NULL);
	hour = tm.tm_hour;
	if (!res)
		res = ast_say_number(chan, hour, ints, lang, "f");
	if (tm.tm_min) {
		if (!res)
			res = wait_file(chan, ints, "digits/pt-e", lang);
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	} else {
		if (!res)
			res = wait_file(chan, ints, "digits/pt-hora", lang);
		if (tm.tm_hour != 1)
			if (!res)
				res = wait_file(chan, ints, "digits/pt-sss", lang);
	}
	if (!res)
		res = ast_say_number(chan, hour, ints, lang, (char *) NULL);
	return res;
}

/*! \brief Brazilian Portuguese syntax */
int ast_say_time_pt_BR(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);

	res = ast_say_number(chan, tm.tm_hour, ints, lang, "f");
	if (!res) {
		if (tm.tm_hour > 1)
			res = wait_file(chan, ints, "digits/hours", lang);
		else
			res = wait_file(chan, ints, "digits/hour", lang);
	}
	if ((!res) && (tm.tm_min)) {
		res = wait_file(chan, ints, "digits/pt-e", lang);
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
		if (!res) {
			if (tm.tm_min > 1)
				res = wait_file(chan, ints, "digits/minutes", lang);
			else
				res = wait_file(chan, ints, "digits/minute", lang);
		}
	}
	return res;
}

/*! \brief Thai  syntax */
int ast_say_time_th(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;
	int hour;
	ast_localtime(&when, &tm, NULL);
	hour = tm.tm_hour;
	if (!hour)
		hour = 24;
	if (!res)
		res = ast_say_number(chan, hour, ints, lang, (char *) NULL);
	if (!res)
		res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	return res;
}

/*! \brief Taiwanese / Chinese  syntax */
int ast_say_time_zh(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;
	int hour, pm=0;

	ast_localtime(&when, &tm, NULL);
	hour = tm.tm_hour;
	if (!hour)
		hour = 12;
	else if (hour == 12)
		pm = 1;
	else if (hour > 12) {
		hour -= 12;
		pm = 1;
	}
	if (pm) {
		if (!res)
			res = ast_streamfile(chan, "digits/p-m", lang);
	} else {
		if (!res)
			res = ast_streamfile(chan, "digits/a-m", lang);
	}
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
		res = ast_say_number(chan, hour, ints, lang, (char *) NULL);
	if (!res)
		res = ast_streamfile(chan, "digits/oclock", lang);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
		res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	if (!res)
		res = ast_streamfile(chan, "digits/minute", lang);
	if (!res)
		res = ast_waitstream(chan, ints);
	return res;
}

/*! \brief Hebrew syntax */
int ast_say_time_he(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;
	int hour;

	ast_localtime(&when, &tm, NULL);
	hour = tm.tm_hour;
	if (!hour)
		hour = 12;

	if (!res)
		res = ast_say_number_full_he(chan, hour, ints, lang, "f", -1, -1);

	if (tm.tm_min > 9) {
		if (!res)
			res = ast_say_number_full_he(chan, tm.tm_min, ints, lang, "f", -1, -1);
	} else if (tm.tm_min) {
		if (!res) {				/* say a leading zero if needed */
			res = ast_say_number_full_he(chan, 0, ints, lang, "f", -1, -1);
		}
		if (!res)
			res = ast_waitstream(chan, ints);
		if (!res)
			res = ast_say_number_full_he(chan, tm.tm_min, ints, lang, "f", -1, -1);
	} else {
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_waitstream(chan, ints);
	return res;
}


static int say_datetime(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	if (!strncasecmp(lang, "en", 2)) {        /* English syntax */
		return ast_say_datetime_en(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "de", 2)) { /* German syntax */
		return ast_say_datetime_de(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "fr", 2)) { /* French syntax */
		return ast_say_datetime_fr(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "ge", 2)) { /* deprecated Georgian syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "ge is not a standard language code.  Please switch to using ka instead.\n");
		}
		return ast_say_datetime_ka(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "gr", 2)) { /* Greek syntax */
		return ast_say_datetime_gr(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "ja", 2)) { /* Japanese syntax */
		return ast_say_datetime_ja(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "he", 2)) { /* Hebrew syntax */
		return ast_say_datetime_he(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "hu", 2)) { /* Hungarian syntax */
		return ast_say_datetime_hu(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "ka", 2)) { /* Georgian syntax */
		return ast_say_datetime_ka(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "nl", 2)) { /* Dutch syntax */
		return ast_say_datetime_nl(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "pt_BR", 5)) { /* Brazilian Portuguese syntax */
		return ast_say_datetime_pt_BR(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "pt", 2)) { /* Portuguese syntax */
		return ast_say_datetime_pt(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "th", 2)) { /* Thai syntax */
		return ast_say_datetime_th(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "tw", 2)) { /* deprecated Taiwanese syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "tw is a standard language code for Twi, not Taiwanese.  Please switch to using zh_TW instead.\n");
		}
		return ast_say_datetime_zh(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "zh", 2)) { /* Taiwanese / Chinese syntax */
		return ast_say_datetime_zh(chan, t, ints, lang);
	}

	/* Default to English */
	return ast_say_datetime_en(chan, t, ints, lang);
}

/*! \brief English syntax */
int ast_say_datetime_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	int hour, pm=0;

	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);

	hour = tm.tm_hour;
	if (!hour)
		hour = 12;
	else if (hour == 12)
		pm = 1;
	else if (hour > 12) {
		hour -= 12;
		pm = 1;
	}
	if (!res)
		res = ast_say_number(chan, hour, ints, lang, (char *) NULL);

	if (tm.tm_min > 9) {
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	} else if (tm.tm_min) {
		if (!res)
			res = ast_streamfile(chan, "digits/oh", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	} else {
		if (!res)
			res = ast_streamfile(chan, "digits/oclock", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (pm) {
		if (!res)
			res = ast_streamfile(chan, "digits/p-m", lang);
	} else {
		if (!res)
			res = ast_streamfile(chan, "digits/a-m", lang);
	}
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	return res;
}

/*! \brief German syntax */
int ast_say_datetime_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);
	res = ast_say_date(chan, t, ints, lang);
	if (!res)
		ast_say_time(chan, t, ints, lang);
	return res;

}

/*! \brief Hungarian syntax */
int ast_say_datetime_hu(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);
	res = ast_say_date(chan, t, ints, lang);
	if (!res)
		ast_say_time(chan, t, ints, lang);
	return res;
}

/*! \brief French syntax */
int ast_say_datetime_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;

	ast_localtime(&when, &tm, NULL);

	if (!res)
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);

	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}

	if (!res)
		res = ast_say_number(chan, tm.tm_hour, ints, lang, "f");
	if (!res)
			res = ast_streamfile(chan, "digits/oclock", lang);
	if (tm.tm_min > 0) {
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	}
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	return res;
}

/*! \brief Dutch syntax */
int ast_say_datetime_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);
	res = ast_say_date(chan, t, ints, lang);
	if (!res) {
		res = ast_streamfile(chan, "digits/nl-om", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		ast_say_time(chan, t, ints, lang);
	return res;
}

/*! \brief Portuguese syntax */
int ast_say_datetime_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	int hour, pm=0;

	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);

	hour = tm.tm_hour;
	if (!hour)
		hour = 12;
	else if (hour == 12)
		pm = 1;
	else if (hour > 12) {
		hour -= 12;
		pm = 1;
	}
	if (!res)
		res = ast_say_number(chan, hour, ints, lang, (char *) NULL);

	if (tm.tm_min > 9) {
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	} else if (tm.tm_min) {
		if (!res)
			res = ast_streamfile(chan, "digits/oh", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	} else {
		if (!res)
			res = ast_streamfile(chan, "digits/oclock", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (pm) {
		if (!res)
			res = ast_streamfile(chan, "digits/p-m", lang);
	} else {
		if (!res)
			res = ast_streamfile(chan, "digits/a-m", lang);
	}
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	return res;
}

/*! \brief Brazilian Portuguese syntax */
int ast_say_datetime_pt_BR(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);
	res = ast_say_date(chan, t, ints, lang);
	if (!res)
		res = ast_say_time(chan, t, ints, lang);
	return res;
}

/*! \brief Thai syntax */
int ast_say_datetime_th(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	int hour;
	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res){
		ast_copy_string(fn, "digits/posor", sizeof(fn));
		res = ast_streamfile(chan, fn, lang);
		res = ast_say_number(chan, tm.tm_year + 1900 + 543, ints, lang, (char *) NULL);
	}
	if (!res)
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);

	hour = tm.tm_hour;
	if (!hour)
		hour = 24;
	if (!res){
		ast_copy_string(fn, "digits/wela", sizeof(fn));
		res = ast_streamfile(chan, fn, lang);
	}
	if (!res)
		res = ast_say_number(chan, hour, ints, lang, (char *) NULL);
	if (!res)
		res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	return res;
}

/*! \brief Taiwanese / Chinese syntax */
int ast_say_datetime_zh(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	int hour, pm=0;

	ast_localtime(&when, &tm, NULL);
	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (!res)
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}

	hour = tm.tm_hour;
	if (!hour)
		hour = 12;
	else if (hour == 12)
		pm = 1;
	else if (hour > 12) {
		hour -= 12;
		pm = 1;
	}
	if (pm) {
		if (!res)
			res = ast_streamfile(chan, "digits/p-m", lang);
	} else {
		if (!res)
			res = ast_streamfile(chan, "digits/a-m", lang);
	}
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
		res = ast_say_number(chan, hour, ints, lang, (char *) NULL);
	if (!res)
		res = ast_streamfile(chan, "digits/oclock", lang);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
		res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	if (!res)
		res = ast_streamfile(chan, "digits/minute", lang);
	if (!res)
		res = ast_waitstream(chan, ints);
	return res;
}

/*! \brief Hebrew syntax */
int ast_say_datetime_he(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	int hour;

	ast_localtime(&when, &tm, NULL);
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res) {
			res = ast_waitstream(chan, ints);
		}
	}
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res) {
			res = ast_waitstream(chan, ints);
		}
	}
	if (!res) {
		res = ast_say_number(chan, tm.tm_mday, ints, lang, "f");
	}

	hour = tm.tm_hour;
	if (!hour) {
		hour = 12;
	}

	if (!res) {
		res = ast_say_number(chan, hour, ints, lang, "f");
	}

	if (tm.tm_min > 9) {
		if (!res) {
			res = ast_say_number(chan, tm.tm_min, ints, lang, "f");
		}
	} else if (tm.tm_min) {
		if (!res) {
			/* say a leading zero if needed */
			res = ast_say_number(chan, 0, ints, lang, "f");
		}
		if (!res) {
			res = ast_waitstream(chan, ints);
		}
		if (!res) {
			res = ast_say_number(chan, tm.tm_min, ints, lang, "f");
		}
	} else {
		if (!res) {
			res = ast_waitstream(chan, ints);
		}
	}
	if (!res) {
		res = ast_waitstream(chan, ints);
	}
	if (!res) {
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, "f");
	}
	return res;
}

static int say_datetime_from_now(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	if (!strncasecmp(lang, "en", 2)) {        /* English syntax */
		return ast_say_datetime_from_now_en(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "fr", 2)) { /* French syntax */
		return ast_say_datetime_from_now_fr(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "ge", 2)) { /* deprecated Georgian syntax */
		static int deprecation_warning = 0;
		if (deprecation_warning++ % 10 == 0) {
			ast_log(LOG_WARNING, "ge is not a standard language code.  Please switch to using ka instead.\n");
		}
		return ast_say_datetime_from_now_ka(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "he", 2)) { /* Hebrew syntax */
		return ast_say_datetime_from_now_he(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "ka", 2)) { /* Georgian syntax */
		return ast_say_datetime_from_now_ka(chan, t, ints, lang);
	} else if (!strncasecmp(lang, "pt", 2)) { /* Portuguese syntax */
		return ast_say_datetime_from_now_pt(chan, t, ints, lang);
	}

	/* Default to English */
	return ast_say_datetime_from_now_en(chan, t, ints, lang);
}

/*! \brief English syntax */
int ast_say_datetime_from_now_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	int res=0;
	struct timeval nowtv = ast_tvnow(), when = { t, 0 };
	int daydiff;
	struct ast_tm tm;
	struct ast_tm now;
	char fn[256];

	ast_localtime(&when, &tm, NULL);
	ast_localtime(&nowtv, &now, NULL);
	daydiff = now.tm_yday - tm.tm_yday;
	if ((daydiff < 0) || (daydiff > 6)) {
		/* Day of month and month */
		if (!res) {
			snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
			res = ast_streamfile(chan, fn, lang);
			if (!res)
				res = ast_waitstream(chan, ints);
		}
		if (!res)
			res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);

	} else if (daydiff) {
		/* Just what day of the week */
		if (!res) {
			snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
			res = ast_streamfile(chan, fn, lang);
			if (!res)
				res = ast_waitstream(chan, ints);
		}
	} /* Otherwise, it was today */
	if (!res)
		res = ast_say_time(chan, t, ints, lang);
	return res;
}

/*! \brief French syntax */
int ast_say_datetime_from_now_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	int res=0;
	struct timeval nowtv = ast_tvnow(), when = { t, 0 };
	int daydiff;
	struct ast_tm tm;
	struct ast_tm now;
	char fn[256];

	ast_localtime(&when, &tm, NULL);
	ast_localtime(&nowtv, &now, NULL);
	daydiff = now.tm_yday - tm.tm_yday;
	if ((daydiff < 0) || (daydiff > 6)) {
		/* Day of month and month */
		if (!res) {
			snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
			res = ast_streamfile(chan, fn, lang);
			if (!res)
				res = ast_waitstream(chan, ints);
		}
		if (!res)
			res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);

	} else if (daydiff) {
		/* Just what day of the week */
		if (!res) {
			snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
			res = ast_streamfile(chan, fn, lang);
			if (!res)
				res = ast_waitstream(chan, ints);
		}
	} /* Otherwise, it was today */
	if (!res)
		res = ast_say_time(chan, t, ints, lang);
	return res;
}

/*! \brief Portuguese syntax */
int ast_say_datetime_from_now_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	int res=0;
	int daydiff;
	struct ast_tm tm;
	struct ast_tm now;
	struct timeval nowtv = ast_tvnow(), when = { t, 0 };
	char fn[256];

	ast_localtime(&when, &tm, NULL);
	ast_localtime(&nowtv, &now, NULL);
	daydiff = now.tm_yday - tm.tm_yday;
	if ((daydiff < 0) || (daydiff > 6)) {
		/* Day of month and month */
		if (!res)
			res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
		if (!res)
			res = wait_file(chan, ints, "digits/pt-de", lang);
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		if (!res)
			res = wait_file(chan, ints, fn, lang);

	} else if (daydiff) {
		/* Just what day of the week */
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		if (!res)
			res = wait_file(chan, ints, fn, lang);
	}	/* Otherwise, it was today */
	if (!strcasecmp(lang, "pt_BR")) {
		if (tm.tm_hour > 1) {
			ast_copy_string(fn, "digits/pt-as", sizeof(fn));
		} else {
			ast_copy_string(fn, "digits/pt-a", sizeof(fn));
		}
		if (!res)
			res = wait_file(chan, ints, fn, lang);
	} else {
		ast_copy_string(fn, "digits/pt-ah", sizeof(fn));
		if (!res)
			res = wait_file(chan, ints, fn, lang);
		if (tm.tm_hour != 1)
		if (!res)
			res = wait_file(chan, ints, "digits/pt-sss", lang);
		if (!res)
			res = ast_say_time(chan, t, ints, lang);
	}
	return res;
}

/*! \brief Hebrew syntax */
int ast_say_datetime_from_now_he(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	int res = 0;
	struct timeval nowt = ast_tvnow(), when = { t, 0 };
	int daydiff;
	struct ast_tm tm;
	struct ast_tm now;
	char fn[256];

	ast_localtime(&when, &tm, NULL);
	ast_localtime(&nowt, &now, NULL);
	daydiff = now.tm_yday - tm.tm_yday;
	if ((daydiff < 0) || (daydiff > 6)) {
		/* Day of month and month */
		if (!res) {
			snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
			res = ast_streamfile(chan, fn, lang);
			if (!res)
				res = ast_waitstream(chan, ints);
		}
		if (!res) {
			res = ast_say_number(chan, tm.tm_mday, ints, lang, "f");
		}
	} else if (daydiff) {
		/* Just what day of the week */
		if (!res) {
			snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
			res = ast_streamfile(chan, fn, lang);
			if (!res) {
				res = ast_waitstream(chan, ints);
			}
		}
	}							/* Otherwise, it was today */
	if (!res) {
		res = ast_say_time(chan, t, ints, lang);
	}
	return res;
}



/*! \brief Greek
 * digits/female-[1..4] : "Mia, dyo , treis, tessereis"
 */
static int gr_say_number_female(int num, struct ast_channel *chan, const char *ints, const char *lang){
	int tmp;
	int left;
	int res;
	char fn[256] = "";

	/* ast_debug(1, "\n\n Saying number female %s %d \n\n", lang, num); */
	if (num < 5) {
		snprintf(fn, sizeof(fn), "digits/female-%d", num);
		res = wait_file(chan, ints, fn, lang);
	} else if (num < 13) {
		res = ast_say_number(chan, num, ints, lang, (char *) NULL);
	} else if (num <100 ) {
		tmp = (num/10) * 10;
		left = num - tmp;
		snprintf(fn, sizeof(fn), "digits/%d", tmp);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
		if (left)
			gr_say_number_female(left, chan, ints, lang);

	} else {
		return -1;
	}
	return res;
}



/*! \brief Greek support
 *	A list of the files that you need to create
 ->	digits/xilia = "xilia"
 ->	digits/myrio = "ekatomyrio"
 ->	digits/thousands = "xiliades"
 ->	digits/millions = "ektatomyria"
 ->	digits/[1..12]   :: A pronunciation of th digits form 1 to 12 e.g. "tria"
 ->	digits/[10..100]  :: A pronunciation of the tens from 10 to 90
				 e.g. 80 = "ogdonta"
				 Here we must note that we use digits/tens/100 to utter "ekato"
				 and digits/hundred-100 to utter "ekaton"
 ->	digits/hundred-[100...1000] :: A pronunciation of  hundreds from 100 to 1000 e.g 400 =
				 "terakosia". Here again we use hundreds/1000 for "xilia"
				 and digits/thousnds for "xiliades"
*/
static int ast_say_number_full_gr(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	char fn[256] = "";
	int i=0;


	if (!num) {
		ast_copy_string(fn, "digits/0", sizeof(fn));
		res = ast_streamfile(chan, fn, ast_channel_language(chan));
		if (!res)
			return  ast_waitstream(chan, ints);
	}

	while (!res && num ) {
		i++;
		if (num < 13) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num <= 100) {
			/* 13 < num <= 100  */
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num %= 10;
		} else if (num < 200) {
			/* 100 < num < 200 */
			snprintf(fn, sizeof(fn), "digits/hundred-100");
			num %= 100;
		} else if (num < 1000) {
			/* 200 < num < 1000 */
			snprintf(fn, sizeof(fn), "digits/hundred-%d", (num/100)*100);
			num %= 100;
		} else if (num < 2000){
			snprintf(fn, sizeof(fn), "digits/xilia");
			num %= 1000;
		} else {
			/* num >  1000 */
			if (num < 1000000) {
				res = ast_say_number_full_gr(chan, (num / 1000), ints, ast_channel_language(chan), audiofd, ctrlfd);
				if (res)
					return res;
				num %= 1000;
				snprintf(fn, sizeof(fn), "digits/thousands");
			}  else {
				if (num < 1000000000) { /* 1,000,000,000 */
					res = ast_say_number_full_gr(chan, (num / 1000000), ints, ast_channel_language(chan), audiofd, ctrlfd);
					if (res)
						return res;
					num %= 1000000;
					snprintf(fn, sizeof(fn), "digits/millions");
				} else {
					ast_debug(1, "Number '%d' is too big for me\n", num);
					res = -1;
				}
			}
		}
		if (!res) {
			if (!ast_streamfile(chan, fn, language)) {
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/* Japanese syntax */
static int ast_say_number_full_ja(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
     int res = 0;
     int playh = 0;
     char fn[256] = "";
     if (!num)
             return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);

     while (!res && (num || playh)) {
             if (num < 0) {
                     ast_copy_string(fn, "digits/minus", sizeof(fn));
                     if ( num > INT_MIN ) {
                             num = -num;
                     } else {
                             num = 0;
                     }
             } else if (playh) {
                     ast_copy_string(fn, "digits/hundred", sizeof(fn));
                     playh = 0;
             } else  if (num < 20) {
                     snprintf(fn, sizeof(fn), "digits/%d", num);
                     num = 0;
             } else  if (num < 100) {
                     snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
                     num %= 10;
             } else {
                     if (num < 1000){
                             snprintf(fn, sizeof(fn), "digits/%d", (num/100));
                             playh++;
                             num %= 100;
                     } else {
                             if (num < 1000000) { /* 1,000,000 */
                                     res = ast_say_number_full_en(chan, num / 1000, ints, language, audiofd, ctrlfd);
                                     if (res)
                                             return res;
                                     num %= 1000;
                                     snprintf(fn, sizeof(fn), "digits/thousand");
                             } else {
                                     if (num < 1000000000) { /* 1,000,000,000 */
                                             res = ast_say_number_full_en(chan, num / 1000000, ints, language, audiofd, ctrlfd);
                                             if (res)
                                                     return res;
                                             num %= 1000000;
                                             ast_copy_string(fn, "digits/million", sizeof(fn));
                                     } else {
                                             ast_debug(1, "Number '%d' is too big for me\n", num);
                                             res = -1;
                                     }
                             }
                     }
             }
             if (!res) {
                     if (!ast_streamfile(chan, fn, language)) {
                             if ((audiofd  > -1) && (ctrlfd > -1))
                                     res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
                             else
                                     res = ast_waitstream(chan, ints);
                     }
                     ast_stopstream(chan);
             }
     }
     return res;
}


/*! \brief Greek support
 *
 * The format is  weekday - day - month -year
 *
 * A list of the files that you need to create
 * digits/day-[1..7]  : "Deytera .. Paraskeyh"
 * digits/months/1..12 : "Ianouariou .. Dekembriou"
	Attention the months are in "gekinh klhsh"
 */
static int ast_say_date_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct ast_tm tm;
	struct timeval when = { t, 0 };

	char fn[256];
	int res = 0;


	ast_localtime(&when, &tm, NULL);
	/* W E E K - D A Y */
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	/* D A Y */
	if (!res) {
		gr_say_number_female(tm.tm_mday, chan, ints, lang);
	}
	/* M O N T H */
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	/* Y E A R */
	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	return res;
}


/* Japanese syntax */
int ast_say_date_ja(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
      struct ast_tm tm;
      struct timeval tv = { t, 0 };
      char fn[256];
      int res = 0;
      ast_localtime(&tv, &tm, NULL);
      if (!res)
              res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
      if (!res)
              res = ast_waitstream(chan, ints);
      if (!res)
              res = ast_streamfile(chan, "digits/nen", lang);
      if (!res) {
              snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
              res = ast_streamfile(chan, fn, lang);
              if (!res)
                      res = ast_waitstream(chan, ints);
      }
      if (!res)
              res = ast_say_number(chan, tm.tm_mday, ints, lang, (char * ) NULL);
      if (!res)
              res = ast_streamfile(chan, "digits/nichi", lang);
      if (!res) {
              snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
              res = ast_streamfile(chan, fn, lang);
              if (!res)
                      res = ast_waitstream(chan, ints);
      }
      return res;
}


/*! \brief Greek support
 *
 * A list of the files that you need to create
 * - digits/female/1..4 : "Mia, dyo , treis, tesseris "
 * - digits/kai : "KAI"
 * - didgits : "h wra"
 * - digits/p-m : "meta meshmbrias"
 * - digits/a-m : "pro meshmbrias"
 */
static int ast_say_time_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{

	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;
	int hour, pm=0;

	ast_localtime(&when, &tm, NULL);
	hour = tm.tm_hour;

	if (!hour)
		hour = 12;
	else if (hour == 12)
		pm = 1;
	else if (hour > 12) {
		hour -= 12;
		pm = 1;
	}

	res = gr_say_number_female(hour, chan, ints, lang);
	if (tm.tm_min) {
		if (!res)
			res = ast_streamfile(chan, "digits/kai", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	} else {
		if (!res)
			res = ast_streamfile(chan, "digits/hwra", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	if (pm) {
		if (!res)
			res = ast_streamfile(chan, "digits/p-m", lang);
	} else {
		if (!res)
			res = ast_streamfile(chan, "digits/a-m", lang);
	}
	if (!res)
		res = ast_waitstream(chan, ints);
	return res;
}


/* Japanese */
static int ast_say_time_ja(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{

      struct timeval tv = { t, 0 };
      struct ast_tm tm;
      int res = 0;
      int hour, pm=0;

      ast_localtime(&tv, &tm, NULL);
      hour = tm.tm_hour;

      if (!hour)
              hour = 12;
      else if (hour == 12)
              pm = 1;
      else if (hour > 12) {
              hour -= 12;
              pm = 1;
      }

      if (pm) {
              if (!res)
                      res = ast_streamfile(chan, "digits/p-m", lang);
      } else {
              if (!res)
                      res = ast_streamfile(chan, "digits/a-m", lang);
      }
      if (hour == 9 || hour == 21) {
              if (!res)
                      res = ast_streamfile(chan, "digits/9_2", lang);
      } else {
              if (!res)
                      res = ast_say_number(chan, hour, ints, lang, (char *) NULL);
      }
      if (!res)
              res = ast_streamfile(chan, "digits/ji", lang);
      if (!res)
              res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
      if (!res)
              res = ast_streamfile(chan, "digits/fun", lang);
      if (!res)
              res = ast_waitstream(chan, ints);
      return res;
}


/*! \brief Greek support
 */
static int ast_say_datetime_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;

	ast_localtime(&when, &tm, NULL);

	/* W E E K - D A Y */
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}
	/* D A Y */
	if (!res) {
		gr_say_number_female(tm.tm_mday, chan, ints, lang);
	}
	/* M O N T H */
	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res)
			res = ast_waitstream(chan, ints);
	}

	res = ast_say_time_gr(chan, t, ints, lang);
	return res;
}

/* Japanese syntax */
int ast_say_datetime_ja(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
      struct timeval tv = { t, 0 };
      struct ast_tm tm;
      char fn[256];
      int res = 0;
      int hour, pm=0;

      ast_localtime(&tv, &tm, NULL);

      if (!res)
              res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
      if (!res)
              res = ast_streamfile(chan, "digits/nen", lang);
      if (!res) {
              snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
              res = ast_streamfile(chan, fn, lang);
              if (!res)
                      res = ast_waitstream(chan, ints);
      }
      if (!res)
              res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
      if (!res)
              res = ast_streamfile(chan, "digits/nichi", lang);
      if (!res) {
              snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
              res = ast_streamfile(chan, fn, lang);
      if (!res)
              res = ast_waitstream(chan, ints);
      }

      hour = tm.tm_hour;
      if (!hour)
              hour = 12;
      else if (hour == 12)
              pm = 1;
      else if (hour > 12) {
              hour -= 12;
              pm = 1;
      }
      if (pm) {
              if (!res)
                      res = ast_streamfile(chan, "digits/p-m", lang);
      } else {
              if (!res)
                      res = ast_streamfile(chan, "digits/a-m", lang);
      }
      if (hour == 9 || hour == 21) {
              if (!res)
                      res = ast_streamfile(chan, "digits/9_2", lang);
      } else {
              if (!res)
                      res = ast_say_number(chan, hour, ints, lang, (char *) NULL);
      }
      if (!res)
              res = ast_streamfile(chan, "digits/ji", lang);
      if (!res)
              res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
      if (!res)
              res = ast_streamfile(chan, "digits/fun", lang);
      if (!res)
              res = ast_waitstream(chan, ints);
      return res;
}

/*! \brief Greek support
 */
static int ast_say_date_with_format_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (!format)
		format = "AdBY 'digits/at' IMp";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
		case '\'':
			/* Literal name of a sound file */
			for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
				sndfile[sndoffset] = format[offset];
			}
			sndfile[sndoffset] = '\0';
			res = wait_file(chan, ints, sndfile, lang);
			break;
		case 'A':
		case 'a':
			/* Sunday - Saturday */
			snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
			res = wait_file(chan, ints, nextmsg, lang);
			break;
		case 'B':
		case 'b':
		case 'h':
			/* January - December */
			snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
			res = wait_file(chan, ints, nextmsg, lang);
			break;
		case 'd':
		case 'e':
			/* first - thirtyfirst */
			gr_say_number_female(tm.tm_mday, chan, ints, lang);
			break;
		case 'Y':
			/* Year */

			ast_say_number_full_gr(chan, 1900+tm.tm_year, ints, ast_channel_language(chan), -1, -1);
			break;
		case 'I':
		case 'l':
			/* 12-Hour */
			if (tm.tm_hour == 0)
				gr_say_number_female(12, chan, ints, lang);
			else if (tm.tm_hour > 12)
				gr_say_number_female(tm.tm_hour - 12, chan, ints, lang);
			else
				gr_say_number_female(tm.tm_hour, chan, ints, lang);
			break;
		case 'H':
		case 'k':
			/* 24-Hour */
			gr_say_number_female(tm.tm_hour, chan, ints, lang);
			break;
		case 'M':
			/* Minute */
			if (tm.tm_min) {
				if (!res)
					res = ast_streamfile(chan, "digits/kai", lang);
				if (!res)
					res = ast_waitstream(chan, ints);
				if (!res)
					res = ast_say_number_full_gr(chan, tm.tm_min, ints, lang, -1, -1);
			} else {
				if (!res)
					res = ast_streamfile(chan, "digits/oclock", lang);
				if (!res)
					res = ast_waitstream(chan, ints);
			}
			break;
		case 'P':
		case 'p':
			/* AM/PM */
			if (tm.tm_hour > 11)
				ast_copy_string(nextmsg, "digits/p-m", sizeof(nextmsg));
			else
				ast_copy_string(nextmsg, "digits/a-m", sizeof(nextmsg));
			res = wait_file(chan, ints, nextmsg, lang);
			break;
		case 'Q':
			/* Shorthand for "Today", "Yesterday", or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
			{
				struct timeval now = ast_tvnow();
				struct ast_tm tmnow;
				time_t beg_today;

				ast_localtime(&now, &tmnow, tzone);
				/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
				/* In any case, it saves not having to do ast_mktime() */
				beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
				if (beg_today < t) {
					/* Today */
					res = wait_file(chan, ints, "digits/today", lang);
				} else if (beg_today - 86400 < t) {
					/* Yesterday */
					res = wait_file(chan, ints, "digits/yesterday", lang);
				} else {
					res = ast_say_date_with_format_gr(chan, t, ints, lang, "AdBY", tzone);
				}
			}
			break;
		case 'q':
			/* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
			{
				struct timeval now = ast_tvnow();
				struct ast_tm tmnow;
				time_t beg_today;

				ast_localtime(&now, &tmnow, tzone);
				/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
				/* In any case, it saves not having to do ast_mktime() */
				beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
				if (beg_today < t) {
					/* Today */
				} else if ((beg_today - 86400) < t) {
					/* Yesterday */
					res = wait_file(chan, ints, "digits/yesterday", lang);
				} else if (beg_today - 86400 * 6 < t) {
					/* Within the last week */
					res = ast_say_date_with_format_gr(chan, t, ints, lang, "A", tzone);
				} else {
					res = ast_say_date_with_format_gr(chan, t, ints, lang, "AdBY", tzone);
				}
			}
			break;
		case 'R':
			res = ast_say_date_with_format_gr(chan, t, ints, lang, "HM", tzone);
			break;
		case 'S':
			/* Seconds */
			ast_copy_string(nextmsg, "digits/kai", sizeof(nextmsg));
			res = wait_file(chan, ints, nextmsg, lang);
			if (!res)
				res = ast_say_number_full_gr(chan, tm.tm_sec, ints, lang, -1, -1);
			if (!res)
				ast_copy_string(nextmsg, "digits/seconds", sizeof(nextmsg));
			res = wait_file(chan, ints, nextmsg, lang);
			break;
		case 'T':
			res = ast_say_date_with_format_gr(chan, t, ints, lang, "HMS", tzone);
			break;
		case ' ':
		case '	':
			/* Just ignore spaces and tabs */
			break;
		default:
			/* Unknown character */
			ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

/* Japanese syntax */
int ast_say_date_with_format_ja(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
{
     struct timeval tv = { time, 0 };
     struct ast_tm tm;
     int res=0, offset, sndoffset;
     char sndfile[256], nextmsg[256];

     if (!format)
           format = "YbdAPIMS";

     ast_localtime(&tv, &tm, timezone);

     for (offset=0 ; format[offset] != '\0' ; offset++) {
             ast_log(LOG_DEBUG, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
             switch (format[offset]) {
                     /* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
                     case '\'':
                             /* Literal name of a sound file */
                             sndoffset=0;
                             for (sndoffset=0 ; (format[++offset] != '\'') && (sndoffset < 256) ; sndoffset++)
                                     sndfile[sndoffset] = format[offset];
                             sndfile[sndoffset] = '\0';
                             res = wait_file(chan,ints,sndfile,lang);
                             break;
                     case 'A':
                     case 'a':
                             /* Sunday - Saturday */
                             snprintf(nextmsg,sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
                             res = wait_file(chan,ints,nextmsg,lang);
                             break;
                     case 'B':
                     case 'b':
                     case 'h':
                             /* January - December */
                             snprintf(nextmsg,sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
                             res = wait_file(chan,ints,nextmsg,lang);
                             break;
                     case 'd':
                     case 'e':
                             /* First - Thirtyfirst */
                             if (tm.tm_mday < 21) {
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d_2", tm.tm_mday);
                                     res = wait_file(chan,ints,nextmsg,lang);
                             } else if (tm.tm_mday < 30) {
                                     /* Between 21 and 29 - two sounds */
                                     res = wait_file(chan,ints, "digits/20",lang);
                                     if (!res) {
                                             snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_mday - 20);
                                             res = wait_file(chan,ints,nextmsg,lang);
                                     }
                                     res = wait_file(chan,ints, "digits/nichi",lang);
                             } else if (tm.tm_mday == 30) {
                                     /* 30 */
                                     res = wait_file(chan,ints, "digits/h-30_2",lang);
                             } else {
                                     /* 31 */
                                     res = wait_file(chan,ints, "digits/30",lang);
                                     res = wait_file(chan,ints, "digits/1",lang);
                                     res = wait_file(chan,ints, "digits/nichi",lang);
                             }
                             break;
                     case 'Y':
                             /* Year */
                             if (tm.tm_year > 99) {
                                     res = wait_file(chan,ints, "digits/2",lang);
                                     if (!res) {
                                             res = wait_file(chan,ints, "digits/thousand",lang);
                                     }
                                     if (tm.tm_year > 100) {
                                             if (!res) {
                                                     /* This works until the end of 2020 */
                                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year - 100);
                                                     res = wait_file(chan,ints,nextmsg,lang);
                                             }
                                     }
                             } else {
                                     if (tm.tm_year < 1) {
                                             /* I'm not going to handle 1900 and prior */
                                             /* We'll just be silent on the year, instead of bombing out. */
	                                       } else {
							 res = wait_file(chan,ints, "digits/19",lang);
                                             if (!res) {
                                                     if (tm.tm_year <= 9) {
                                                             /* 1901 - 1909 */
                                                             res = wait_file(chan,ints, "digits/oh",lang);
                                                             if (!res) {
                                                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year);
                                                                     res = wait_file(chan,ints,nextmsg,lang);
                                                             }
                                                     } else if (tm.tm_year <= 20) {
                                                             /* 1910 - 1920 */
                                                             snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year);
                                                             res = wait_file(chan,ints,nextmsg,lang);
                                                     } else {
                                                             /* 1921 - 1999 */
                                                             int ten, one;
                                                             ten = tm.tm_year / 10;
                                                             one = tm.tm_year % 10;
                                                             snprintf(nextmsg,sizeof(nextmsg), "digits/%d", ten * 10);
                                                             res = wait_file(chan,ints,nextmsg,lang);
                                                             if (!res) {
                                                                     if (one != 0) {
                                                                             snprintf(nextmsg,sizeof(nextmsg), "digits/%d", one);
                                                                             res = wait_file(chan,ints,nextmsg,lang);
                                                                     }
                                                             }
                                                     }
                                             }
                                     }
                             }
                             res = wait_file(chan,ints, "digits/nen",lang);
                             break;
                     case 'P':
                     case 'p':
                             /* AM/PM */
                             if (tm.tm_hour > 11)
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/p-m");
                             else
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/a-m");
                             res = wait_file(chan,ints,nextmsg,lang);
                             break;
                     case 'I':
                     case 'l':
                             /* 12-Hour */
                             if (tm.tm_hour == 0)
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/12");
                             else if (tm.tm_hour == 9 || tm.tm_hour == 21)
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/9_2");
                             else if (tm.tm_hour > 12)
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
                             else
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour);
                             res = wait_file(chan,ints,nextmsg,lang);
                             if(!res) res = wait_file(chan,ints, "digits/ji",lang);
                             break;
                     case 'H':
                     case 'k':
                             if (!res) {
                                     if (tm.tm_hour != 0) {
                                             int remainder = tm.tm_hour;
                                             if (tm.tm_hour > 20) {
                                                     res = wait_file(chan,ints, "digits/20",lang);
                                                     remainder -= 20;
                                             }
                                             if (!res) {
                                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", remainder);
                                                     res = wait_file(chan,ints,nextmsg,lang);
                                             }
                                     }
                             }
                             res = wait_file(chan,ints, "digits/ji",lang);
                             break;
                     case 'M':
                             /* Minute */
                             if ((tm.tm_min < 21) || (tm.tm_min % 10 == 0)) {
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_min);
                                     res = wait_file(chan,ints,nextmsg,lang);
                             } else {
                                     int ten, one;
                                     ten = (tm.tm_min / 10) * 10;
                                     one = (tm.tm_min % 10);
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", ten);
                                     res = wait_file(chan,ints,nextmsg,lang);
                                     if (!res) {
                                             /* Fifty, not fifty-zero */
                                             if (one != 0) {
                                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", one);
                                                     res = wait_file(chan,ints,nextmsg,lang);
                                             }
                                     }
                             }
                             res = wait_file(chan,ints, "digits/fun",lang);
                             break;
                     case 'Q':
                             /* Shorthand for "Today", "Yesterday", or ABdY */
                             {
                                     struct timeval now;
                                     struct ast_tm tmnow;
                                     time_t beg_today;

                                     gettimeofday(&now,NULL);
                                     ast_localtime(&now,&tmnow,timezone);
                                     /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
                                     /* In any case, it saves not having to do ast_mktime() */
                                     beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
                                     if (beg_today < time) {
                                             /* Today */
                                             res = wait_file(chan,ints, "digits/today",lang);
                                     } else if (beg_today - 86400 < time) {
                                             /* Yesterday */
                                              res = wait_file(chan,ints, "digits/yesterday",lang);
                                     } else {
                                              res = ast_say_date_with_format(chan, time, ints, lang, "ABdY", timezone);
                                     }
                             }
                             break;
                     case 'q':
                             /* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
                             {
                                     struct timeval now;
                                     struct ast_tm tmnow;
                                     time_t beg_today;

                                     gettimeofday(&now,NULL);
                                     ast_localtime(&now,&tmnow,timezone);
                                     /* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
                                     /* In any case, it saves not having to do ast_mktime() */
                                     beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
                                     if (beg_today < time) {
                                             /* Today */
                                     } else if ((beg_today - 86400) < time) {
                                             /* Yesterday */
                                             res = wait_file(chan,ints, "digits/yesterday",lang);
                                     } else if (beg_today - 86400 * 6 < time) {
                                             /* Within the last week */
                                             res = ast_say_date_with_format(chan, time, ints, lang, "A", timezone);
                                     } else {
                                             res = ast_say_date_with_format(chan, time, ints, lang, "ABdY", timezone);
                                     }
                             }
                             break;
                     case 'R':
                             res = ast_say_date_with_format(chan, time, ints, lang, "HM", timezone);
                             break;
                     case 'S':
                             /* Seconds */
                             if (tm.tm_sec == 0) {
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
                                     res = wait_file(chan,ints,nextmsg,lang);
                             } else if ((tm.tm_sec < 21) || (tm.tm_sec % 10 == 0)) {
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
                                     res = wait_file(chan,ints,nextmsg,lang);
                             } else {
                                     int ten, one;
                                     ten = (tm.tm_sec / 10) * 10;
                                     one = (tm.tm_sec % 10);
                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", ten);
                                     res = wait_file(chan,ints,nextmsg,lang);
                                     if (!res) {
                                             /* Fifty, not fifty-zero */
                                             if (one != 0) {
                                                     snprintf(nextmsg,sizeof(nextmsg), "digits/%d", one);
                                                     res = wait_file(chan,ints,nextmsg,lang);
                                             }
                                     }
                             }
                             res = wait_file(chan,ints, "digits/byou",lang);
                             break;
                     case 'T':
                             res = ast_say_date_with_format(chan, time, ints, lang, "HMS", timezone);
                             break;
                     case ' ':
                     case '	':
                             /* Just ignore spaces and tabs */
                             break;
                     default:
                             /* Unknown character */
                             ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
             }
             /* Jump out on DTMF */
             if (res) {
                     break;
             }
     }
     return res;
}

/*! \brief Vietnamese syntax */
int ast_say_date_with_format_vi(struct ast_channel *chan, time_t t, const char *ints, const char *lang, const char *format, const char *tzone)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	if (format == NULL)
		format = "A 'digits/day' eB 'digits/year' Y 'digits/at' k 'hours' M 'minutes' p";

	ast_localtime(&when, &tm, tzone);

	for (offset=0 ; format[offset] != '\0' ; offset++) {
		ast_debug(1, "Parsing %c (offset %d) in %s\n", format[offset], offset, format);
		switch (format[offset]) {
			/* NOTE:  if you add more options here, please try to be consistent with strftime(3) */
			case '\'':
				/* Literal name of a sound file */
				for (sndoffset = 0; !strchr("\'\0", format[++offset]) && (sndoffset < sizeof(sndfile) - 1) ; sndoffset++) {
					sndfile[sndoffset] = format[offset];
				}
				sndfile[sndoffset] = '\0';
				res = wait_file(chan, ints, sndfile, lang);
				break;
			case 'A':
			case 'a':
				/* Sunday - Saturday */
				snprintf(nextmsg, sizeof(nextmsg), "digits/day-%d", tm.tm_wday);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'B':
			case 'b':
			case 'h':
				/* January - December */
				snprintf(nextmsg, sizeof(nextmsg), "digits/mon-%d", tm.tm_mon);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'm':
				/* Month enumerated */
				res = ast_say_enumeration(chan, (tm.tm_mon + 1), ints, lang, (char *) NULL);
				break;
			case 'd':
			case 'e':
				/* 1 - 31 */
				res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
				break;
			case 'Y':
				/* Year */
				if (tm.tm_year > 99) {
					res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
				} else if (tm.tm_year < 1) {
					/* I'm not going to handle 1900 and prior */
					/* We'll just be silent on the year, instead of bombing out. */
				} else {
					res = wait_file(chan, ints, "digits/19", lang);
					if (!res) {
						if (tm.tm_year <= 9) {
							/* 1901 - 1909 */
							res = wait_file(chan, ints, "digits/odd", lang);
						}

						res |= ast_say_number(chan, tm.tm_year, ints, lang, (char *) NULL);
					}
				}
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					ast_copy_string(nextmsg, "digits/12", sizeof(nextmsg));
				else if (tm.tm_hour > 12)
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg, sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				if (format[offset] == 'H') {
					/* e.g. oh-eight */
					if (tm.tm_hour < 10) {
						res = wait_file(chan, ints, "digits/0", lang);
					}
				} else {
					/* e.g. eight */
					if (tm.tm_hour == 0) {
						res = wait_file(chan, ints, "digits/0", lang);
					}
				}
				if (!res) {
					if (tm.tm_hour != 0) {
						int remaining = tm.tm_hour;
						if (tm.tm_hour > 20) {
							res = wait_file(chan, ints, "digits/20", lang);
							remaining -= 20;
						}
						if (!res) {
							snprintf(nextmsg, sizeof(nextmsg), "digits/%d", remaining);
							res = wait_file(chan, ints, nextmsg, lang);
						}
					}
				}
				break;
			case 'M':
			case 'N':
				/* Minute */
				res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 11)
					ast_copy_string(nextmsg, "digits/p-m", sizeof(nextmsg));
				else
					ast_copy_string(nextmsg, "digits/a-m", sizeof(nextmsg));
				res = wait_file(chan, ints, nextmsg, lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now = ast_tvnow();
					struct ast_tm tmnow;
					time_t beg_today;

					gettimeofday(&now, NULL);
					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
						res = wait_file(chan, ints, "digits/today", lang);
					} else if (beg_today - 86400 < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_vi(chan, t, ints, lang, "A", tzone);
					} else if (beg_today - 2628000 < t) {
						/* Less than a month ago - "Chu nhat ngay 13 thang 2" */
						res = ast_say_date_with_format_vi(chan, t, ints, lang, "A 'digits/day' dB", tzone);
					} else if (beg_today - 15768000 < t) {
						/* Less than 6 months ago - "August seventh" */
						res = ast_say_date_with_format_vi(chan, t, ints, lang, "'digits/day' dB", tzone);
					} else {
						/* More than 6 months ago - "April nineteenth two thousand three" */
						res = ast_say_date_with_format_vi(chan, t, ints, lang, "'digits/day' dB 'digits/year' Y", tzone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
				/* XXX As emphasized elsewhere, this should the native way in your
				 * language to say the date, with changes in what you say, depending
				 * upon how recent the date is. XXX */
				{
					struct timeval now;
					struct ast_tm tmnow;
					time_t beg_today;

					now = ast_tvnow();
					ast_localtime(&now, &tmnow, tzone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < t) {
						/* Today */
					} else if ((beg_today - 86400) < t) {
						/* Yesterday */
						res = wait_file(chan, ints, "digits/yesterday", lang);
					} else if (beg_today - 86400 * 6 < t) {
						/* Within the last week */
						res = ast_say_date_with_format_en(chan, t, ints, lang, "A", tzone);
					} else if (beg_today - 2628000 < t) {
						/* Less than a month ago - "Chu nhat ngay 13 thang 2" */
						res = ast_say_date_with_format_vi(chan, t, ints, lang, "A 'digits/day' dB", tzone);
					} else if (beg_today - 15768000 < t) {
						/* Less than 6 months ago - "August seventh" */
						res = ast_say_date_with_format_vi(chan, t, ints, lang, "'digits/day' dB", tzone);
					} else {
						/* More than 6 months ago - "April nineteenth two thousand three" */
						res = ast_say_date_with_format_vi(chan, t, ints, lang, "'digits/day' dB 'digits/year' Y", tzone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format_vi(chan, t, ints, lang, "HM", tzone);
				break;
			case 'S':
				/* Seconds */
				res = ast_say_number(chan, tm.tm_sec, ints, lang, (char *) NULL);
				break;
			case 'T':
				res = ast_say_date_with_format_vi(chan, t, ints, lang, "H 'hours' M 'minutes' S 'seconds'", tzone);
				break;
			case ' ':
			case '	':
				/* Just ignore spaces and tabs */
				break;
			default:
				/* Unknown character */
				ast_log(LOG_WARNING, "Unknown character in datetime format %s: %c at pos %d\n", format, format[offset], offset);
		}
		/* Jump out on DTMF */
		if (res) {
			break;
		}
	}
	return res;
}

/*! \brief Georgian support

	Convert a number into a semi-localized string. Only for Georgian.
	res must be of at least 256 bytes, preallocated.
	The output corresponds to Georgian spoken numbers, so
	it may be either converted to real words by applying a direct conversion
	table, or played just by substituting the entities with played files.

	Output may consist of the following tokens (separated by spaces):
	0, minus.
	1-9, 1_-9_. (erti, ori, sami, otxi, ... . erti, or, sam, otx, ...).
	10-19.
	20, 40, 60, 80, 20_, 40_, 60_, 80_. (oci, ormoci, ..., ocda, ormocda, ...).
	100, 100_, 200, 200_, ..., 900, 900_. (asi, as, orasi, oras, ...).
	1000, 1000_. (atasi, atas).
	1000000, 1000000_. (milioni, milion).
	1000000000, 1000000000_. (miliardi, miliard).

	To be able to play the sounds, each of the above tokens needs
	a corresponding sound file. (e.g. 200_.gsm).
*/
static char* ast_translate_number_ka(int num, char* res, int res_len)
{
	char buf[256];
	int digit = 0;
	int remaining = 0;


	if (num < 0) {
		strncat(res, "minus ", res_len - strlen(res) - 1);
		if ( num > INT_MIN ) {
			num = -num;
		} else {
			num = 0;
		}
	}


	/* directly read the numbers */
	if (num <= 20 || num == 40 || num == 60 || num == 80 || num == 100) {
		snprintf(buf, sizeof(buf), "%d", num);
		strncat(res, buf, res_len - strlen(res) - 1);
		return res;
	}


	if (num < 40) {  /* ocda... */
		strncat(res, "20_ ", res_len - strlen(res) - 1);
		return ast_translate_number_ka(num - 20, res, res_len);
	}

	if (num < 60) {  /* ormocda... */
		strncat(res, "40_ ", res_len - strlen(res) - 1);
		return ast_translate_number_ka(num - 40, res, res_len);
	}

	if (num < 80) {  /* samocda... */
		strncat(res, "60_ ", res_len - strlen(res) - 1);
		return ast_translate_number_ka(num - 60, res, res_len);
	}

	if (num < 100) {  /* otxmocda... */
		strncat(res, "80_ ", res_len - strlen(res) - 1);
		return ast_translate_number_ka(num - 80, res, res_len);
	}


	if (num < 1000) {  /*  as, oras, samas, ..., cxraas. asi, orasi, ..., cxraasi. */
		remaining = num % 100;
		digit = (num - remaining) / 100;

		if (remaining == 0) {
			snprintf(buf, sizeof(buf), "%d", num);
			strncat(res, buf, res_len - strlen(res) - 1);
			return res;
		} else {
			snprintf(buf, sizeof(buf), "%d_ ", digit*100);
			strncat(res, buf, res_len - strlen(res) - 1);
			return ast_translate_number_ka(remaining, res, res_len);
		}
	}


	if (num == 1000) {
		strncat(res, "1000", res_len - strlen(res) - 1);
		return res;
	}


	if (num < 1000000) {
		remaining = num % 1000;
		digit = (num - remaining) / 1000;

		if (remaining == 0) {
			ast_translate_number_ka(digit, res, res_len);
			strncat(res, " 1000", res_len - strlen(res) - 1);
			return res;
		}

		if (digit == 1) {
			strncat(res, "1000_ ", res_len - strlen(res) - 1);
			return ast_translate_number_ka(remaining, res, res_len);
		}

		ast_translate_number_ka(digit, res, res_len);
		strncat(res, " 1000_ ", res_len - strlen(res) - 1);
		return ast_translate_number_ka(remaining, res, res_len);
	}


	if (num == 1000000) {
		strncat(res, "1 1000000", res_len - strlen(res) - 1);
		return res;
	}


	if (num < 1000000000) {
		remaining = num % 1000000;
		digit = (num - remaining) / 1000000;

		if (remaining == 0) {
			ast_translate_number_ka(digit, res, res_len);
			strncat(res, " 1000000", res_len - strlen(res) - 1);
			return res;
		}

		ast_translate_number_ka(digit, res, res_len);
		strncat(res, " 1000000_ ", res_len - strlen(res) - 1);
		return ast_translate_number_ka(remaining, res, res_len);
	}


	if (num == 1000000000) {
		strncat(res, "1 1000000000", res_len - strlen(res) - 1);
		return res;
	}


	if (num > 1000000000) {
		remaining = num % 1000000000;
		digit = (num - remaining) / 1000000000;

		if (remaining == 0) {
			ast_translate_number_ka(digit, res, res_len);
			strncat(res, " 1000000000", res_len - strlen(res) - 1);
			return res;
		}

		ast_translate_number_ka(digit, res, res_len);
		strncat(res, " 1000000000_ ", res_len - strlen(res) - 1);
		return ast_translate_number_ka(remaining, res, res_len);
	}

	return res;

}



/*! \brief  ast_say_number_full_ka: Georgian syntax */
static int ast_say_number_full_ka(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	char fn[512] = "";
	char* s = 0;
	const char* remaining = fn;

	if (!num) {
		return ast_say_digits_full(chan, 0, ints, language, audiofd, ctrlfd);
	}


	ast_translate_number_ka(num, fn, 512);



	while (res == 0 && (s = strstr(remaining, " "))) {
		size_t len = s - remaining;
		char* new_string = ast_malloc(len + 1 + strlen("digits/"));

		sprintf(new_string, "digits/");
		strncat(new_string, remaining, len);  /* we can't sprintf() it, it's not null-terminated. */
/*		new_string[len + strlen("digits/")] = '\0'; */

		if (!ast_streamfile(chan, new_string, language)) {
			if ((audiofd  > -1) && (ctrlfd > -1)) {
				res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
			} else {
				res = ast_waitstream(chan, ints);
			}
		}
		ast_stopstream(chan);

		ast_free(new_string);

		remaining = s + 1;  /* position just after the found space char. */
		while (*remaining == ' ') {  /* skip multiple spaces */
			remaining++;
		}
	}


	/* the last chunk. */
	if (res == 0 && *remaining) {

		char* new_string = ast_malloc(strlen(remaining) + 1 + strlen("digits/"));
		sprintf(new_string, "digits/%s", remaining);

		if (!ast_streamfile(chan, new_string, language)) {
			if ((audiofd  > -1) && (ctrlfd > -1)) {
				res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
			} else {
				res = ast_waitstream(chan, ints);
			}
		}
		ast_stopstream(chan);

		ast_free(new_string);

	}


	return res;

}



/*! \brief Georgian syntax. e.g. "oriatas xuti tslis 5 noemberi".

Georgian support for date/time requires the following files (*.gsm):

 - mon-1, mon-2, ... (ianvari, tebervali, ...)
 - day-1, day-2, ... (orshabati, samshabati, ...)
 - saati_da
 - tsuti
 - tslis
*/
static int ast_say_date_ka(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&when, &tm, NULL);

	if (!res) {
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
	}

	if (!res) {
		snprintf(fn, sizeof(fn), "digits/tslis %d", tm.tm_wday);
		res = ast_streamfile(chan, fn, lang);
		if (!res) {
			res = ast_waitstream(chan, ints);
		}
	}

	if (!res) {
		res = ast_say_number(chan, tm.tm_mday, ints, lang, (char * ) NULL);
/*		if (!res)
			res = ast_waitstream(chan, ints);
*/
	}

	if (!res) {
		snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
		res = ast_streamfile(chan, fn, lang);
		if (!res) {
			res = ast_waitstream(chan, ints);
		}
	}
	return res;

}





/*! \brief Georgian syntax. e.g. "otxi saati da eqvsi tsuti" */
static int ast_say_time_ka(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);

	res = ast_say_number(chan, tm.tm_hour, ints, lang, (char*)NULL);
	if (!res) {
		res = ast_streamfile(chan, "digits/saati_da", lang);
		if (!res) {
			res = ast_waitstream(chan, ints);
		}
	}

	if (tm.tm_min) {
		if (!res) {
			res = ast_say_number(chan, tm.tm_min, ints, lang, (char*)NULL);

			if (!res) {
				res = ast_streamfile(chan, "digits/tsuti", lang);
				if (!res) {
					res = ast_waitstream(chan, ints);
				}
			}
		}
	}
	return res;
}



/*! \brief Georgian syntax. Say date, then say time. */
static int ast_say_datetime_ka(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct timeval when = { t, 0 };
	struct ast_tm tm;
	int res = 0;

	ast_localtime(&when, &tm, NULL);
	res = ast_say_date(chan, t, ints, lang);
	if (!res) {
		ast_say_time(chan, t, ints, lang);
	}
	return res;

}




/*! \brief Georgian syntax */
static int ast_say_datetime_from_now_ka(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	int res=0;
	int daydiff;
	struct ast_tm tm;
	struct ast_tm now;
	struct timeval when = { t, 0 }, nowt = ast_tvnow();
	char fn[256];

	ast_localtime(&when, &tm, NULL);
	ast_localtime(&nowt, &now, NULL);
	daydiff = now.tm_yday - tm.tm_yday;
	if ((daydiff < 0) || (daydiff > 6)) {
		/* Day of month and month */
		if (!res) {
			res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
		}
		if (!res) {
			snprintf(fn, sizeof(fn), "digits/mon-%d", tm.tm_mon);
			res = ast_streamfile(chan, fn, lang);
			if (!res) {
				res = ast_waitstream(chan, ints);
			}
		}

	} else if (daydiff) {
		/* Just what day of the week */
		if (!res) {
			snprintf(fn, sizeof(fn), "digits/day-%d", tm.tm_wday);
			res = ast_streamfile(chan, fn, lang);
			if (!res) {
				res = ast_waitstream(chan, ints);
			}
		}
	} /* Otherwise, it was today */
	if (!res) {
		res = ast_say_time(chan, t, ints, lang);
	}

	return res;
}

/*! \brief
 * In English, we use the plural for everything but one. For example:
 *  - 1 degree
 *  - 2 degrees
 *  - 5 degrees
 * The filename for the plural form is generated by appending "s". Note that
 * purpose is to generate a unique filename, not to implement irregular
 * declensions. Thus:
 *  - 1 man
 *  - 2 mans (the "mans" soundfile will of course say "men")
 */
static const char *counted_noun_ending_en(int num)
{
	if (num == 1 || num == -1) {
		return "";
	} else {
		return "s";
	}
}

/*! \brief
 * Counting of objects in slavic languages such as Russian and Ukrainian the
 * rules are more complicated. There are two plural forms used in counting.
 * They are the genative singular which we represent with the suffix "x1" and
 * the genative plural which we represent with the suffix "x2". The base names
 * of the soundfiles remain in English. For example:
 *  - 1 degree (soudfile says "gradus")
 *  - 2 degreex1 (soundfile says "gradusa")
 *  - 5 degreex2 (soundfile says "gradusov")
 */
static const char *counted_noun_ending_slavic(int num)
{
	if (num < 0) {
	    num *= -1;
	}
	num %= 100;			/* never pay attention to more than two digits */
	if (num >= 20) {		/* for numbers 20 and above, pay attention to only last digit */
	    num %= 10;
	}
	if (num == 1) {			/* singular */
	    return "";
	}
	if (num > 0 && num < 5) {	/* 2--4 get genative singular */
	    return "x1";
	} else {			/* 5--19 get genative plural */
	    return "x2";
	}
}

int ast_say_counted_noun(struct ast_channel *chan, int num, const char noun[])
{
	char *temp;
	int temp_len;
	const char *ending;
	if (!strncasecmp(ast_channel_language(chan), "ru", 2)) {        /* Russian */
		ending = counted_noun_ending_slavic(num);
	} else if (!strncasecmp(ast_channel_language(chan), "ua", 2)) { /* Ukrainian */
		ending = counted_noun_ending_slavic(num);
	} else if (!strncasecmp(ast_channel_language(chan), "pl", 2)) { /* Polish */
		ending = counted_noun_ending_slavic(num);
	} else {                                            /* English and default */
		ending = counted_noun_ending_en(num);
	}
	temp = ast_alloca((temp_len = (strlen(noun) + strlen(ending) + 1)));
	snprintf(temp, temp_len, "%s%s", noun, ending);
	return ast_play_and_wait(chan, temp);
}

/*! \brief
 * In slavic languages such as Russian and Ukrainian the rules for declining
 * adjectives are simpler than those for nouns.  When counting we use only
 * the singular (to which we give no suffix) and the genative plural (which
 * we represent by adding an "x").  Oh, an in the singular gender matters
 * so we append the supplied gender suffix ("m", "f", "n").
 */
static const char *counted_adjective_ending_ru(int num, const char gender[])
{
	if (num < 0) {
	    num *= -1;
	}
	num %= 100;		/* never pay attention to more than two digits */
	if (num >= 20) {	/* at 20 and beyond only the last digit matters */
	    num %= 10;
	}
	if (num == 1) {
	    return gender ? gender : "";
	} else {		/* all other numbers get the genative plural */
	    return "x";
	}
}

int ast_say_counted_adjective(struct ast_channel *chan, int num, const char adjective[], const char gender[])
{
	char *temp;
	int temp_len;
	const char *ending;
	if (!strncasecmp(ast_channel_language(chan), "ru", 2)) {           /* Russian */
		ending = counted_adjective_ending_ru(num, gender);
	} else if (!strncasecmp(ast_channel_language(chan), "ua", 2)) {    /* Ukrainian */
		ending = counted_adjective_ending_ru(num, gender);
	} else if (!strncasecmp(ast_channel_language(chan), "pl", 2)) {    /* Polish */
		ending = counted_adjective_ending_ru(num, gender);
	} else {                                               /* English and default */
		ending = "";
	}
	temp = ast_alloca((temp_len = (strlen(adjective) + strlen(ending) + 1)));
	snprintf(temp, temp_len, "%s%s", adjective, ending);
	return ast_play_and_wait(chan, temp);
}



/*! \brief
 * remap the 'say' functions to use those in this file
 */
static void __attribute__((constructor)) __say_init(void)
{
	ast_say_number_full = say_number_full;
	ast_say_enumeration_full = say_enumeration_full;
	ast_say_digit_str_full = say_digit_str_full;
	ast_say_character_str_full = say_character_str_full;
	ast_say_phonetic_str_full = say_phonetic_str_full;
	ast_say_datetime = say_datetime;
	ast_say_time = say_time;
	ast_say_date = say_date;
	ast_say_datetime_from_now = say_datetime_from_now;
	ast_say_date_with_format = say_date_with_format;
}
