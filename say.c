/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Say numbers and dates (maybe words one day too)
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <time.h>
#include <ctype.h>
#include <asterisk/file.h>
#include <asterisk/channel.h>
#include <asterisk/logger.h>
#include <asterisk/say.h>
#include <asterisk/lock.h>
#include <asterisk/localtime.h>
#include <asterisk/utils.h>
#include "asterisk.h"
#include <stdio.h>


/* Forward declaration */
static int wait_file(struct ast_channel *chan, char *ints, char *file, char *lang);

int ast_say_digit_str(struct ast_channel *chan, char *fn2, char *ints, char *lang)
{
	/* XXX Merge with full version? XXX */
	char fn[256] = "";
	int num = 0;
	int res = 0;
	while(fn2[num] && !res) {
		fn[0] = '\0';
		switch (fn2[num]) {
			case ('*'):
				snprintf(fn, sizeof(fn), "digits/star");
				break;
			case ('#'):
				snprintf(fn, sizeof(fn), "digits/pound");
				break;
			default:
				if((fn2[num] >= '0') && (fn2[num] <= '9')){ /* Must be in {0-9} */
					snprintf(fn, sizeof(fn), "digits/%c", fn2[num]);
				}
		}
		if(!ast_strlen_zero(fn)){ /* if length == 0, then skip this digit as it is invalid */
			res = ast_streamfile(chan, fn, lang);
			if (!res)
				res = ast_waitstream(chan, ints);
			ast_stopstream(chan);
		}
		num++;
	}
	return res;
}

int ast_say_character_str(struct ast_channel *chan, char *fn2, char *ints, char *lang)
{
	/* XXX Merge with full version? XXX */
	char fn[256] = "";
	char ltr;
	int num = 0;
	int res = 0;
	while(fn2[num] && !res) {
		fn[0] = '\0';
		switch (fn2[num]) {
			case ('*'):
				snprintf(fn, sizeof(fn), "digits/star");
				break;
			case ('#'):
				snprintf(fn, sizeof(fn), "digits/pound");
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
  				snprintf(fn, sizeof(fn), "digits/%c", fn2[num]);
 				break;
			case ('!'):
				strncpy(fn, "letters/exclaimation-point", sizeof(fn));
				break;    	
 			case ('@'):
 				strncpy(fn, "letters/at", sizeof(fn));
 				break;
 			case ('$'):
 				strncpy(fn, "letters/dollar", sizeof(fn));
 				break;
 			case ('-'):
 				strncpy(fn, "letters/dash", sizeof(fn));
 				break;
 			case ('.'):
 				strncpy(fn, "letters/dot", sizeof(fn));
 				break;
 			case ('='):
 				strncpy(fn, "letters/equals", sizeof(fn));
 				break;
 			case ('+'):
 				strncpy(fn, "letters/plus", sizeof(fn));
 				break;
 			case ('/'):
 				strncpy(fn, "letters/slash", sizeof(fn));
 				break;
 			case (' '):
 				strncpy(fn, "letters/space", sizeof(fn));
 				break;
 			default:
 				ltr = fn2[num];
 				if ('A' <= ltr && ltr <= 'Z') ltr += 'a' - 'A';		/* file names are all lower-case */
 				snprintf(fn, sizeof(fn), "letters/%c", ltr);
  		}
		if(!ast_strlen_zero(fn)) { /* if length == 0, then skip this digit as it is invalid */
			res = ast_streamfile(chan, fn, lang);
			if (!res) 
				res = ast_waitstream(chan, ints);
		}	ast_stopstream(chan);
		num++;
	}
	return res;
}

int ast_say_phonetic_str(struct ast_channel *chan, char *fn2, char *ints, char *lang)
{
	/* XXX Merge with full version? XXX */
	char fn[256] = "";
	char ltr;
	int num = 0;
	int res = 0;
	int temp;
	int play;
	char hex[3];
/*	while(fn2[num] && !res) { */
	while(fn2[num]) {
		play=1;
		switch (fn2[num]) {
			case ('*'):
				snprintf(fn, sizeof(fn), "digits/star");
				break;
			case ('#'):
				snprintf(fn, sizeof(fn), "digits/pound");
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
				snprintf(fn, sizeof(fn), "digits/%c", fn2[num]);
				break;
			case ('!'):
				strncpy(fn, "exclaimation-point", sizeof(fn));
				break;    	
			case ('@'):
				strncpy(fn, "at", sizeof(fn));
				break;
			case ('$'):
				strncpy(fn, "dollar", sizeof(fn));
				break;	
			case ('-'):
				strncpy(fn, "dash", sizeof(fn));
				break;
			case ('.'):
				strncpy(fn, "dot", sizeof(fn));
				break;
			case ('='):
				strncpy(fn, "equals", sizeof(fn));
				break;
			case ('+'):
				strncpy(fn, "plus", sizeof(fn));
				break;
			case ('/'):
				strncpy(fn, "slash", sizeof(fn));
				break;
			case (' '):
				strncpy(fn, "space", sizeof(fn));
				break;
			case ('%'):
				play=0;
				/* check if we have 2 chars after the % */
				if (strlen(fn2) > num+2)
				{
				    hex[0]=fn2[num+1];
				    hex[1]=fn2[num+2];
				    hex[2]='\0';
				    if (sscanf(hex,"%x", &temp))
				    { /* Hex to char convertion successfull */
				        fn2[num+2]=temp;
				        num++;
				        if (temp==37)
				        { /* If it is a percent, play it now */
				    	    strncpy(fn, "percent", sizeof(fn));
					    	num++;
					    	play=1;
						}
						/* check for invalid characters */
						if ((temp<32) || (temp>126))
						{
						    num++;
						}
				    }
				}
				else
				    num++;
				break;
			default:	/* '9' falls through to here, too */
				ltr = tolower(fn2[num]);
				snprintf(fn, sizeof(fn), "phonetic/%c_p", ltr);
		}
		if (play)
		{
		    res = ast_streamfile(chan, fn, lang);
		    if (!res) 
			res = ast_waitstream(chan, ints);
		    ast_stopstream(chan);
		}
		num++;
	}
	return res;
}

int ast_say_digit_str_full(struct ast_channel *chan, char *fn2, char *ints, char *lang, int audiofd, int ctrlfd)
{
	char fn[256] = "";
	int num = 0;
	int res = 0;
	while(fn2[num] && !res) {
		snprintf(fn, sizeof(fn), "digits/%c", fn2[num]);
		res = ast_streamfile(chan, fn, lang);
		if (!res) 
			res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
		ast_stopstream(chan);
		num++;
	}
	return res;
}

int ast_say_character_str_full(struct ast_channel *chan, char *fn2, char *ints, char *lang, int audiofd, int ctrlfd)
{
	char fn[256] = "";
	char ltr;
	int num = 0;
	int res = 0;
	while(fn2[num] && !res) {
		switch (fn2[num]) {
			case ('*'):
				snprintf(fn, sizeof(fn), "digits/star");
				break;
			case ('#'):
				snprintf(fn, sizeof(fn), "digits/pound");
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
				snprintf(fn, sizeof(fn), "digits/%c", fn2[num]);
				break;
			case ('!'):
				strncpy(fn, "exclaimation-point", sizeof(fn));
				break;    	
			case ('@'):
				strncpy(fn, "at", sizeof(fn));
				break;
			case ('$'):
				strncpy(fn, "dollar", sizeof(fn));
				break;
			case ('-'):
				strncpy(fn, "dash", sizeof(fn));
				break;
			case ('.'):
				strncpy(fn, "dot", sizeof(fn));
				break;
			case ('='):
				strncpy(fn, "equals", sizeof(fn));
				break;
			case ('+'):
				strncpy(fn, "plus", sizeof(fn));
				break;
			case ('/'):
				strncpy(fn, "slash", sizeof(fn));
				break;
			case (' '):
				strncpy(fn, "space", sizeof(fn));
				break;
			default:
				ltr = fn2[num];
				if ('A' <= ltr && ltr <= 'Z') ltr += 'a' - 'A';		/* file names are all lower-case */
				snprintf(fn, sizeof(fn), "letters/%c", ltr);
		}
		/* snprintf(fn, sizeof(fn), "digits/%c", fn2[num]); */
		res = ast_streamfile(chan, fn, lang);
		if (!res) 
			res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
		ast_stopstream(chan);
		num++;
	}
	return res;
}

int ast_say_phonetic_str_full(struct ast_channel *chan, char *fn2, char *ints, char *lang, int audiofd, int ctrlfd)
{
	char fn[256] = "";
	char ltr;
	int num = 0;
	int res = 0;
	while(fn2[num] && !res) {
		switch (fn2[num]) {
			case ('*'):
				snprintf(fn, sizeof(fn), "digits/star");
				break;
			case ('#'):
				snprintf(fn, sizeof(fn), "digits/pound");
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
				snprintf(fn, sizeof(fn), "digits/%c", fn2[num]);
				break;
			case ('!'):
				strncpy(fn, "exclaimation-point", sizeof(fn));
				break;    	
			case ('@'):
				strncpy(fn, "at", sizeof(fn));
				break;
			case ('$'):
				strncpy(fn, "dollar", sizeof(fn));
				break;
			case ('-'):
				strncpy(fn, "dash", sizeof(fn));
				break;
			case ('.'):
				strncpy(fn, "dot", sizeof(fn));
				break;
			case ('='):
				strncpy(fn, "equals", sizeof(fn));
				break;
			case ('+'):
				strncpy(fn, "plus", sizeof(fn));
				break;
			case ('/'):
				strncpy(fn, "slash", sizeof(fn));
				break;
			case (' '):
				strncpy(fn, "space", sizeof(fn));
				break;
			default:	/* '9' falls here... */
				ltr = fn2[num];
				if ('A' <= ltr && ltr <= 'Z') ltr += 'a' - 'A';		/* file names are all lower-case */
				snprintf(fn, sizeof(fn), "phonetic/%c", ltr);
			}
		/* snprintf(fn, sizeof(fn), "digits/%c", fn2[num]); */
		res = ast_streamfile(chan, fn, lang);
		if (!res) 
			res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
		ast_stopstream(chan);
		num++;
	}
	return res;
}

int ast_say_digits(struct ast_channel *chan, int num, char *ints, char *lang)
{
	/* XXX Should I be merged with say_digits_full XXX */
	char fn2[256];
	snprintf(fn2, sizeof(fn2), "%d", num);
	return ast_say_digit_str(chan, fn2, ints, lang);
}

int ast_say_digits_full(struct ast_channel *chan, int num, char *ints, char *lang, int audiofd, int ctrlfd)
{
	char fn2[256];
	snprintf(fn2, sizeof(fn2), "%d", num);
	return ast_say_digit_str_full(chan, fn2, ints, lang, audiofd, ctrlfd);
}

/* Forward declarations */
/* Syntaxes supported, not really language codes.
      da - Danish
      de - German
      en - English
      es - Spanish, Mexican
      fr - French
      it - Italian
      nl - Dutch
      pt - Portuguese
      se - Swedish

 Gender:
 For Portuguese, French & Spanish, we're using m & f options to saynumber() to indicate if the gender is masculine or feminine.
 For Danish, we're using c & n options to saynumber() to indicate if the gender is commune or neutrum.
 This still needs to be implemented for German (although the option is passed to the function, it currently does nothing with it).
 
 Date/Time functions currently have less languages supported than saynumber().

 Note that in future, we need to move to a model where we can differentiate further - e.g. between en_US & en_UK

 See contrib/i18n.testsuite.conf for some examples of the different syntaxes

 Portuguese sound files needed for Time/Date functions:
 pt-ah
 pt-ao
 pt-de
 pt-e
 pt-ora
 pt-meianoite
 pt-meiodia
 pt-sss

 Spanish sound files needed for Time/Date functions:
 es-de
 es-el

*/

/* Forward declarations of language specific variants of ast_say_number_full */
static int ast_say_number_full_en(struct ast_channel *chan, int num, char *ints, char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_da(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_de(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_es(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_fr(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_it(struct ast_channel *chan, int num, char *ints, char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_nl(struct ast_channel *chan, int num, char *ints, char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_pt(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_se(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd);

/* Forward declarations of ast_say_date, ast_say_datetime and ast_say_time functions */
static int ast_say_date_en(struct ast_channel *chan, time_t t, char *ints, char *lang);
static int ast_say_date_nl(struct ast_channel *chan, time_t t, char *ints, char *lang);
static int ast_say_date_pt(struct ast_channel *chan, time_t t, char *ints, char *lang);

static int ast_say_date_with_format_en(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone);
static int ast_say_date_with_format_de(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone);
static int ast_say_date_with_format_es(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone);
static int ast_say_date_with_format_nl(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone);
static int ast_say_date_with_format_pt(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone);
static int ast_say_time_en(struct ast_channel *chan, time_t t, char *ints, char *lang);
static int ast_say_time_nl(struct ast_channel *chan, time_t t, char *ints, char *lang);
static int ast_say_time_pt(struct ast_channel *chan, time_t t, char *ints, char *lang);
static int ast_say_datetime_en(struct ast_channel *chan, time_t t, char *ints, char *lang);
static int ast_say_datetime_nl(struct ast_channel *chan, time_t t, char *ints, char *lang);
static int ast_say_datetime_pt(struct ast_channel *chan, time_t t, char *ints, char *lang);
static int ast_say_datetime_from_now_en(struct ast_channel *chan, time_t t, char *ints, char *lang);
static int ast_say_datetime_from_now_pt(struct ast_channel *chan, time_t t, char *ints, char *lang);

static int wait_file(struct ast_channel *chan, char *ints, char *file, char *lang) 
{
	int res;
	if ((res = ast_streamfile(chan, file, lang)))
		ast_log(LOG_WARNING, "Unable to play message %s\n", file);
	if (!res)
		res = ast_waitstream(chan, ints);
	return res;
}

/*--- ast_say_number_full: call language-specific functions */
/* Called from AGI */
int ast_say_number_full(struct ast_channel *chan, int num, char *ints, char *language, int audiofd, int ctrlfd)
{
	char *options=(char *) NULL; 	/* While waiting for a general hack for agi */

	if (!strcasecmp(language,"en") ) {	/* English syntax */
	   return(ast_say_number_full_en(chan, num, ints, language, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "da") ) {	/* Danish syntax */
	   return(ast_say_number_full_da(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "de") ) {	/* German syntax */
	   return(ast_say_number_full_de(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "es") || !strcasecmp(language, "mx")) {	/* Spanish syntax */
	   return(ast_say_number_full_es(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "fr") ) {	/* French syntax */
	   return(ast_say_number_full_fr(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "it") ) {	/* Italian syntax */
	   return(ast_say_number_full_it(chan, num, ints, language, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "nl") ) {	/* Dutch syntax */
	   return(ast_say_number_full_nl(chan, num, ints, language, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "pt") ) {	/* Portuguese syntax */
	   return(ast_say_number_full_pt(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "se") ) {	/* Swedish syntax */
	   return(ast_say_number_full_se(chan, num, ints, language, options, audiofd, ctrlfd));
	}

	/* Default to english */
	return(ast_say_number_full_en(chan, num, ints, language, audiofd, ctrlfd));
}

/*--- ast_say_number: call language-specific functions without file descriptors */
int ast_say_number(struct ast_channel *chan, int num, char *ints, char *language, char *options)
{
	if (!strcasecmp(language,"en") ) {	/* English syntax */
	   return(ast_say_number_full_en(chan, num, ints, language, -1, -1));
	}else if (!strcasecmp(language, "da")) {	/* Danish syntax */
	   return(ast_say_number_full_da(chan, num, ints, language, options, -1, -1));
	} else if (!strcasecmp(language, "de")) {	/* German syntax */
	   return(ast_say_number_full_de(chan, num, ints, language, options, -1, -1));
	} else if (!strcasecmp(language, "es") || !strcasecmp(language, "mx")) {	/* Spanish syntax */
	   return(ast_say_number_full_es(chan, num, ints, language, options, -1, -1));
	} else if (!strcasecmp(language, "fr")) {	/* French syntax */
	   return(ast_say_number_full_fr(chan, num, ints, language, options, -1, -1));
	} else if (!strcasecmp(language, "it")) {	/* Italian syntax */
	   return(ast_say_number_full_it(chan, num, ints, language, -1, -1));
	} else if (!strcasecmp(language, "nl")) {	/* Dutch syntax */
	   return(ast_say_number_full_nl(chan, num, ints, language, -1, -1));
	} else if (!strcasecmp(language, "pt")) {	/* Portuguese syntax */
	   return(ast_say_number_full_pt(chan, num, ints, language, options, -1, -1));
	} else if (!strcasecmp(language, "se")) {	/* Swedish syntax */
	   return(ast_say_number_full_se(chan, num, ints, language, options, -1, -1));
	}

	/* Default to english */
        return(ast_say_number_full_en(chan, num, ints, language, -1, -1));
}

/*--- ast_say_number_full_en: English syntax */
/* This is the default syntax, if no other syntax defined in this file is used */
static int ast_say_number_full_en(struct ast_channel *chan, int num, char *ints, char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	while(!res && (num || playh)) {
			if (playh) {
				snprintf(fn, sizeof(fn), "digits/hundred");
				playh = 0;
			} else	if (num < 20) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else	if (num < 100) {
				snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
				num -= ((num / 10) * 10);
			} else {
				if (num < 1000){
					snprintf(fn, sizeof(fn), "digits/%d", (num/100));
					playh++;
					num -= ((num / 100) * 100);
				} else {
					if (num < 1000000) { /* 1,000,000 */
						res = ast_say_number_full_en(chan, num / 1000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						num = num % 1000;
						snprintf(fn, sizeof(fn), "digits/thousand");
					} else {
						if (num < 1000000000) {	/* 1,000,000,000 */
							res = ast_say_number_full_en(chan, num / 1000000, ints, language, audiofd, ctrlfd);
							if (res)
								return res;
							num = num % 1000000;
							snprintf(fn, sizeof(fn), "digits/million");
						} else {
							ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
							res = -1;
						}
					}
				}
			}
			if (!res) {
				if(!ast_streamfile(chan, fn, language)) {
					if (audiofd && ctrlfd)
						res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
					else
						res = ast_waitstream(chan, ints);
				}
				ast_stopstream(chan);

                        }
			
	}
	return res;
}

/*--- ast_say_number_full_da: Danish syntax */
/* New files:
 In addition to English, the following sounds are required: "1N", "millions", "and" and "1-and" through "9-and" 
 */
static int ast_say_number_full_da(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playa = 0;
	int cn = 1;		/* +1 = Commune; -1 = Neutrum */
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	if (options && !strncasecmp(options, "n",1)) cn = -1;

	while(!res && (num || playh || playa )) {
		/* The grammar for Danish numbers is the same as for English except
		* for the following:
		* - 1 exists in both commune ("en", file "1N") and neutrum ("et", file "1")
		* - numbers 20 through 99 are said in reverse order, i.e. 21 is
		*   "one-and twenty" and 68 is "eight-and sixty".
		* - "million" is different in singular and plural form
		* - numbers > 1000 with zero as the third digit from last have an
		*   "and" before the last two digits, i.e. 2034 is "two thousand and
		*   four-and thirty" and 1000012 is "one million and twelve".
		*/
		if (playh) {
			snprintf(fn, sizeof(fn), "digits/hundred");
			playh = 0;
		} else if (playa) {
			snprintf(fn, sizeof(fn), "digits/and");
			playa = 0;
		} else if (num == 1 && cn == -1) {
			snprintf(fn, sizeof(fn), "digits/1N");
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
					snprintf(fn, sizeof(fn), "digits/1N");
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
					snprintf(fn, sizeof(fn), "digits/thousand");
				} else {
					if (num < 1000000000) {
						int millions = num / 1000000;
						res = ast_say_number_full_da(chan, millions, ints, language, "c", audiofd, ctrlfd);
						if (res)
							return res;
						if (millions == 1)
							snprintf(fn, sizeof(fn), "digits/million");
						else
							snprintf(fn, sizeof(fn), "digits/millions");
						num = num % 1000000;
					} else {
						ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
						res = -1;
					}
				}
				if (num && num < 100)
					playa++;
			}
		}
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
				if (audiofd && ctrlfd) 
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else  
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*--- ast_say_number_full_de: German syntax */
/* New files:
 In addition to English, the following sounds are required:
 "millions"
 "1-and" through "9-and" 
 "1F" (eine)
 "1N" (ein)
 NB "1" is recorded as 'eins'
 */
static int ast_say_number_full_de(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int t = 0;
	int mf = 1;                            /* +1 = Male, Neutrum; -1 = Female */
	char fn[256] = "";
	char fna[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	if (options && (!strncasecmp(options, "f",1)))
		mf = -1;

	while(!res && (num || playh)) {
		/* The grammar for German numbers is the same as for English except
		* for the following:
		* - numbers 20 through 99 are said in reverse order, i.e. 21 is
		*   "one-and twenty" and 68 is "eight-and sixty".
		* - "one" varies according to gender
		* - 100 is 'hundert', however all other instances are 'ein hundert'
		* - 1000 is 'tausend', however all other instances are 'ein tausend'
		* - 1000000 is always 'ein million'
		* - "million" is different in singular and plural form
		*/
		if (playh) {
			snprintf(fn, sizeof(fn), "digits/hundred");
			playh = 0;
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
		} else if (num == 100) {
			snprintf(fn, sizeof(fn), "digits/hundred");
			num = num - 100;
		} else if (num < 1000) {
			int hundreds = num / 100;
			if (hundreds == 1)
				snprintf(fn, sizeof(fn), "digits/1N");
			else
				snprintf(fn, sizeof(fn), "digits/%d", (num / 100));
			playh++;
			num -= 100 * hundreds;
		} else if (num == 1000 && t == 0) {
			snprintf(fn, sizeof(fn), "digits/thousand");
			num = 0;
		} else 	if (num < 1000000) {
			t = 1;
			int thousands = num / 1000;
			if (thousands == 1) {
				snprintf(fn, sizeof(fn), "digits/1N");
				snprintf(fna, sizeof(fna), "digits/thousand");
			} else {
				res = ast_say_number_full_de(chan, thousands, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
				snprintf(fn, sizeof(fn), "digits/thousand");
			}
			num = num % 1000;
		} else if (num < 1000000000) {
			t = 1;
			int millions = num / 1000000;
			if (millions == 1) {
				snprintf(fn, sizeof(fn), "digits/1N");
				snprintf(fna, sizeof(fna), "digits/million");
			} else {
				res = ast_say_number_full_de(chan, millions, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
				snprintf(fn, sizeof(fn), "digits/millions");
			}
			num = num % 1000000;
		} else {
			ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
			res = -1;
		}
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
				if (audiofd && ctrlfd) 
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else  
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
			if(!ast_streamfile(chan, fna, language)) {
				if (audiofd && ctrlfd) 
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else  
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
			strcpy(fna, "");
		}
	}
	return res;
}

/*--- ast_say_number_full_es: Spanish syntax */
/* New files:
 Requires a few new audios:
   1F.gsm: feminine 'una'
   21.gsm thru 29.gsm, cien.gsm, mil.gsm, millon.gsm, millones.gsm, 100.gsm, 200.gsm, 300.gsm, 400.gsm, 500.gsm, 600.gsm, 700.gsm, 800.gsm, 900.gsm, y.gsm 
 */
static int ast_say_number_full_es(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playa = 0;
	int mf = 1;                            /* +1 = Masculin; -1 = Feminin */
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	if (options && !strncasecmp(options, "f",1))
		mf = -1;

	while (!res && num) {
		if (playa) {
			snprintf(fn, sizeof(fn), "digits/y");
			playa = 0;
		} else if (num == 1) {
			if (mf < 0)
				snprintf(fn, sizeof(fn), "digits/%dF", num);
			else
				snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 31) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num/10)*10);
			num -= ((num/10)*10);
			if (num)
				playa++;
		} else if (num == 100) {
			snprintf(fn, sizeof(fn), "digits/cien");
			num = 0;
		} else {
			if (num < 1000) {
				snprintf(fn, sizeof(fn), "digits/%d", (num/100)*100);
				num -= ((num/100)*100);
			} else {
				if (num < 1000000) {
					res = ast_say_number_full_es(chan, num / 1000, ints, language, options, audiofd, ctrlfd);
					if (res)
						return res;
					num = num % 1000;
					snprintf(fn, sizeof(fn), "digits/mil");
				} else {
					if (num < 2147483640) {
						res = ast_say_number_full_es(chan, num / 1000000, ints, language, options, audiofd, ctrlfd);
						if (res)
							return res;
						if ((num/1000000) == 1) {
							snprintf(fn, sizeof(fn), "digits/millon");
						} else {
							snprintf(fn, sizeof(fn), "digits/millones");
						}
						num = num % 1000000;
					} else {
						ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
						res = -1;
					}
				}
			}
		}

		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
				if (audiofd && ctrlfd)
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);

		}
			
	}
	return res;
}


/*--- ast_say_number_full_fr: French syntax */
/* 	Extra sounds needed:
 	1F: feminin 'une'
 	et: 'and' */
static int ast_say_number_full_fr(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playa = 0;
	int mf = 1;                            /* +1 = Masculin; -1 = Feminin */
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);
	
	if (options && !strncasecmp(options, "f",1))
		mf = -1;

	while(!res && (num || playh || playa)) {
		if (playh) {
			snprintf(fn, sizeof(fn), "digits/hundred");
			playh = 0;
		} else if (playa) {
			snprintf(fn, sizeof(fn), "digits/et");
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
			snprintf(fn, sizeof(fn), "digits/60");
			if ((num % 10) == 1) playa++;
			num = num - 60;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/80");
			num = num - 80;
		} else if (num < 200) {
			snprintf(fn, sizeof(fn), "digits/hundred");
			num = num - 100;
		} else if (num < 1000) {
			snprintf(fn, sizeof(fn), "digits/%d", (num/100));
			playh++;
			num = num % 100;
		} else if (num < 2000) {
			snprintf(fn, sizeof(fn), "digits/thousand");
			num = num - 1000;
		} else if (num < 1000000) {
			res = ast_say_number_full_fr(chan, num / 1000, ints, language, options, audiofd, ctrlfd);
			if (res)
				return res;
			snprintf(fn, sizeof(fn), "digits/thousand");
			num = num % 1000;
		} else	if (num < 1000000000) {
			res = ast_say_number_full_fr(chan, num / 1000000, ints, language, options, audiofd, ctrlfd);
			if (res)
				return res;
			snprintf(fn, sizeof(fn), "digits/million");
			num = num % 1000000;
		} else {
			ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
			res = -1;
		}
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
				if (audiofd && ctrlfd)
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*--- ast_say_number_full_it:  Italian */
static int ast_say_number_full_it(struct ast_channel *chan, int num, char *ints, char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int tempnum = 0;
	char fn[256] = "";

	if (!num)
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

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

		while(!res && (num || playh)) {
			if (playh) {
				snprintf(fn, sizeof(fn), "digits/hundred");
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
				num -= ((num / 10) * 10);
			} else {
				if (num < 1000) {
					if ((num / 100) > 1) {
						snprintf(fn, sizeof(fn), "digits/%d", (num/100));
						playh++;
					} else {
						snprintf(fn, sizeof(fn), "digits/hundred");
					}
					num -= ((num / 100) * 100);
				} else {
					if (num < 1000000) { /* 1,000,000 */
						if ((num/1000) > 1)
							res = ast_say_number_full(chan, num / 1000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						tempnum = num;
						num = num % 1000;
						if ((tempnum / 1000) < 2)
							snprintf(fn, sizeof(fn), "digits/thousand");
						else /* for 1000 it says mille, for >1000 (eg 2000) says mila */
							snprintf(fn, sizeof(fn), "digits/thousands");
					} else {
						if (num < 1000000000) { /* 1,000,000,000 */
							if ((num / 1000000) > 1)
								res = ast_say_number_full(chan, num / 1000000, ints, language, audiofd, ctrlfd);
							if (res)
								return res;
							tempnum = num;
							num = num % 1000000;
							if ((tempnum / 1000000) < 2)
								snprintf(fn, sizeof(fn), "digits/million");
							else
								snprintf(fn, sizeof(fn), "digits/millions");
						} else {
							ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
							res = -1;
						}
					}
				}
			}
			if (!res) {
				if(!ast_streamfile(chan, fn, language)) {
					if (audiofd && ctrlfd)
						res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
					else
						res = ast_waitstream(chan, ints);
				}
				ast_stopstream(chan);
			}
		}
	return res;
}

/*--- ast_say_number_full_nl: dutch syntax */
/* New files: digits/nl-en
 */
static int ast_say_number_full_nl(struct ast_channel *chan, int num, char *ints, char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int units = 0;
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);
	while (!res && (num || playh )) {
		if (playh) {
			snprintf(fn, sizeof(fn), "digits/hundred");
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
				snprintf(fn, sizeof(fn), "digits/nl-en");
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", num - units);
				num = 0;
			}
		} else {
			if (num < 1000) {
				snprintf(fn, sizeof(fn), "digits/%d", (num/100));
				playh++;
				num -= ((num / 100) * 100);
			} else {
				if (num < 1000000) { /* 1,000,000 */
					res = ast_say_number_full_en(chan, num / 1000, ints, language, audiofd, ctrlfd);
					if (res)
						return res;
					num = num % 1000;
					snprintf(fn, sizeof(fn), "digits/thousand");
				} else {
					if (num < 1000000000) { /* 1,000,000,000 */
						res = ast_say_number_full_en(chan, num / 1000000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						num = num % 1000000;
						snprintf(fn, sizeof(fn), "digits/million");
					} else {
						ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
						res = -1;
					}
				}
			}
		}

		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
				if (audiofd && ctrlfd)
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/* ast_say_number_full_pt: Portuguese syntax */
/* 	Extra sounds needed: */
/* 	For feminin all sound files end with F */
/*	100E for 100+ something */
/*	1000000S for plural */
/*	pt-e for 'and' */
static int ast_say_number_full_pt(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int mf = 1;                            /* +1 = Masculin; -1 = Feminin */
	char fn[256] = "";

	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	if (options && !strncasecmp(options, "f",1))
		mf = -1;

	while(!res && num ) {
		if (num < 20) {
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
				snprintf(fn, sizeof(fn), "digits/100");
			else if (num < 200)
				snprintf(fn, sizeof(fn), "digits/100E");
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
			snprintf(fn, sizeof(fn), "digits/1000");
			if ((num % 1000) && ((num % 1000) < 100  || !(num % 100)))
				playh = 1;
			num = num % 1000;
		} else if (num < 1000000000) {
			res = ast_say_number_full_pt(chan, (num / 1000000), ints, language, options, audiofd, ctrlfd );
			if (res)
				return res;
			if (num < 2000000)
				snprintf(fn, sizeof(fn), "digits/1000000");
			else
				snprintf(fn, sizeof(fn), "digits/1000000S");
 
			if ((num % 1000000) &&
				// no thousands
				((!((num / 1000) % 1000) && ((num % 1000) < 100 || !(num % 100))) ||
				// no hundreds and below
				(!(num % 1000) && (((num / 1000) % 1000) < 100 || !((num / 1000) % 100))) ) )
				playh = 1;
			num = num % 1000000;
		}
		if (!res && playh) {
			res = wait_file(chan, ints, "digits/pt-e", language);
			ast_stopstream(chan);
			playh = 0;
		}
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
				if (audiofd && ctrlfd)
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);			else
					res = ast_waitstream(chan, ints);
			}
			ast_stopstream(chan);
		}
	}
	return res;
}

/*--- ast_say_number_full_se: Swedish/Norwegian syntax */
static int ast_say_number_full_se(struct ast_channel *chan, int num, char *ints, char *language, char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";
	int cn = 1;		/* +1 = Commune; -1 = Neutrum */
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);
	if (options && !strncasecmp(options, "n",1)) cn = -1;

	while(!res && (num || playh)) {
			if (playh) {
				snprintf(fn, sizeof(fn), "digits/hundred");
				playh = 0;
			} else
			if (num < 20) {
				snprintf(fn, sizeof(fn), "digits/%d", num);
				num = 0;
			} else
			if (num < 100) {
				snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
				num -= ((num / 10) * 10);
		        } else 
			if (num == 1 && cn == -1) {	/* En eller ett? */
			 	snprintf(fn, sizeof(fn), "digits/1N");
				num = 0;
			} else {
				if (num < 1000){
					snprintf(fn, sizeof(fn), "digits/%d", (num/100));
					playh++;
					num -= ((num / 100) * 100);
				} else {
					if (num < 1000000) { /* 1,000,000 */
						res = ast_say_number_full_se(chan, num / 1000, ints, language, options, audiofd, ctrlfd);
						if (res)
							return res;
						num = num % 1000;
						snprintf(fn, sizeof(fn), "digits/thousand");
					} else {
						if (num < 1000000000) {	/* 1,000,000,000 */
							res = ast_say_number_full_se(chan, num / 1000000, ints, language, options, audiofd, ctrlfd);
							if (res)
								return res;
							num = num % 1000000;
							snprintf(fn, sizeof(fn), "digits/million");
						} else {
							ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
							res = -1;
						}
					}
				}
			}
			 if (!res) {
                                if(!ast_streamfile(chan, fn, language)) {
                                    if (audiofd && ctrlfd)
                                        res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
                                    else
                                         res = ast_waitstream(chan, ints);
                                }
                                ast_stopstream(chan);

                        }
			
	}
	return res;
}

int ast_say_date(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	if (!strcasecmp(lang,"en") ) {	/* English syntax */
		return(ast_say_date_en(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "nl") ) {	/* Dutch syntax */
		return(ast_say_date_nl(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "pt") ) {	/* Portuguese syntax */
		return(ast_say_date_pt(chan, t, ints, lang));
	}

	/* Default to English */
	return(ast_say_date_en(chan, t, ints, lang));
}


/* English syntax */
int ast_say_date_en(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	struct tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&t,&tm,NULL);
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

/* Dutch syntax */
int ast_say_date_nl(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	struct tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&t,&tm,NULL);
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

/* Portuguese syntax */
int ast_say_date_pt(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	struct tm tm;
	char fn[256];
	int res = 0;
	ast_localtime(&t,&tm,NULL);
	localtime_r(&t,&tm);
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

int ast_say_date_with_format(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone)
{
	if (!strcasecmp(lang, "en") ) {	/* English syntax */
		return(ast_say_date_with_format_en(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "de") ) {	/* German syntax */
		return(ast_say_date_with_format_de(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "es") || !strcasecmp(lang, "mx")) {	/* Spanish syntax */
		return(ast_say_date_with_format_es(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "nl") ) {	/* Dutch syntax */
		return(ast_say_date_with_format_nl(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "pt") ) {	/* Portuguese syntax */
		return(ast_say_date_with_format_pt(chan, time, ints, lang, format, timezone));
	}

	/* Default to English */
	return(ast_say_date_with_format_en(chan, time, ints, lang, format, timezone));
}

/* English syntax */
int ast_say_date_with_format_en(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone)
{
	struct tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	ast_localtime(&time,&tm,timezone);

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
				snprintf(nextmsg,sizeof(nextmsg), AST_SOUNDS "/%s", sndfile);
				res = wait_file(chan,ints,nextmsg,lang);
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
				if ((tm.tm_mday < 21) || (tm.tm_mday == 30)) {
					snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mday);
					res = wait_file(chan,ints,nextmsg,lang);
				} else if (tm.tm_mday == 31) {
					/* "Thirty" and "first" */
					res = wait_file(chan,ints, "digits/30",lang);
					if (!res) {
						res = wait_file(chan,ints, "digits/h-1",lang);
					}
				} else {
					/* Between 21 and 29 - two sounds */
					res = wait_file(chan,ints, "digits/20",lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_mday - 20);
						res = wait_file(chan,ints,nextmsg,lang);
					}
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
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					snprintf(nextmsg,sizeof(nextmsg), "digits/12");
				else if (tm.tm_hour > 12)
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan,ints,nextmsg,lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				if (format[offset] == 'H') {
					/* e.g. oh-eight */
					if (tm.tm_hour < 10) {
						res = wait_file(chan,ints, "digits/oh",lang);
					}
				} else {
					/* e.g. eight */
					if (tm.tm_hour == 0) {
						res = wait_file(chan,ints, "digits/oh",lang);
					}
				}
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
				break;
			case 'M':
				/* Minute */
				if (tm.tm_min == 0) {
					res = wait_file(chan,ints, "digits/oclock",lang);
				} else if (tm.tm_min < 10) {
					res = wait_file(chan,ints, "digits/oh",lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_min);
						res = wait_file(chan,ints,nextmsg,lang);
					}
				} else if ((tm.tm_min < 21) || (tm.tm_min % 10 == 0)) {
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
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				{
					struct timeval now;
					struct tm tmnow;
					time_t beg_today;

					gettimeofday(&now,NULL);
					ast_localtime(&now.tv_sec,&tmnow,timezone);
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
					struct tm tmnow;
					time_t beg_today;

					gettimeofday(&now,NULL);
					ast_localtime(&now.tv_sec,&tmnow,timezone);
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
				} else if (tm.tm_sec < 10) {
					res = wait_file(chan,ints, "digits/oh",lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
						res = wait_file(chan,ints,nextmsg,lang);
					}
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

/* German syntax */
/* NB This currently is a 100% clone of the English syntax, just getting ready to make changes... */
int ast_say_date_with_format_de(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone)
{
	struct tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	ast_localtime(&time,&tm,timezone);

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
				snprintf(nextmsg,sizeof(nextmsg), AST_SOUNDS "/%s", sndfile);
				res = wait_file(chan,ints,nextmsg,lang);
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
				if ((tm.tm_mday < 21) || (tm.tm_mday == 30)) {
					snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mday);
					res = wait_file(chan,ints,nextmsg,lang);
				} else if (tm.tm_mday == 31) {
					/* "Thirty" and "first" */
					res = wait_file(chan,ints, "digits/30",lang);
					if (!res) {
						res = wait_file(chan,ints, "digits/h-1",lang);
					}
				} else {
					/* Between 21 and 29 - two sounds */
					res = wait_file(chan,ints, "digits/20",lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_mday - 20);
						res = wait_file(chan,ints,nextmsg,lang);
					}
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
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					snprintf(nextmsg,sizeof(nextmsg), "digits/12");
				else if (tm.tm_hour > 12)
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan,ints,nextmsg,lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				if (format[offset] == 'H') {
					/* e.g. oh-eight */
					if (tm.tm_hour < 10) {
						res = wait_file(chan,ints, "digits/oh",lang);
					}
				} else {
					/* e.g. eight */
					if (tm.tm_hour == 0) {
						res = wait_file(chan,ints, "digits/oh",lang);
					}
				}
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
				break;
			case 'M':
				/* Minute */
				if (tm.tm_min == 0) {
					res = wait_file(chan,ints, "digits/oclock",lang);
				} else if (tm.tm_min < 10) {
					res = wait_file(chan,ints, "digits/oh",lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_min);
						res = wait_file(chan,ints,nextmsg,lang);
					}
				} else if ((tm.tm_min < 21) || (tm.tm_min % 10 == 0)) {
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
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				{
					struct timeval now;
					struct tm tmnow;
					time_t beg_today;

					gettimeofday(&now,NULL);
					ast_localtime(&now.tv_sec,&tmnow,timezone);
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
					struct tm tmnow;
					time_t beg_today;

					gettimeofday(&now,NULL);
					ast_localtime(&now.tv_sec,&tmnow,timezone);
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
				} else if (tm.tm_sec < 10) {
					res = wait_file(chan,ints, "digits/oh",lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
						res = wait_file(chan,ints,nextmsg,lang);
					}
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

/* Spanish syntax */
int ast_say_date_with_format_es(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone)
{
	struct tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	ast_localtime(&time,&tm,timezone);

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
				snprintf(nextmsg,sizeof(nextmsg), "%s", sndfile);
				res = wait_file(chan,ints,nextmsg,lang);
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
					snprintf(nextmsg,sizeof(nextmsg), "digits/12");
				else if (tm.tm_hour > 12)
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan,ints,nextmsg,lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				res = ast_say_number(chan, -tm.tm_hour, ints, lang, NULL);
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
				break;
			case 'M':
				/* Minute */
				res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);	
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 12)
					res = wait_file(chan, ints, "digits/p-m", lang);
				else if (tm.tm_hour  && tm.tm_hour < 12)
					res = wait_file(chan, ints, "digits/a-m", lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				{
					struct timeval now;
					struct tm tmnow;
					time_t beg_today;

					gettimeofday(&now,NULL);
					ast_localtime(&now.tv_sec,&tmnow,timezone);
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
						res = ast_say_date_with_format(chan, time, ints, lang, "'digits/es-el' Ad 'digits/es-de' B 'digits/es-de' Y", timezone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
				{
					struct timeval now;
					struct tm tmnow;
					time_t beg_today;

					gettimeofday(&now,NULL);
					ast_localtime(&now.tv_sec,&tmnow,timezone);
					/* This might be slightly off, if we transcend a leap second, but never more off than 1 second */
					/* In any case, it saves not having to do ast_mktime() */
					beg_today = now.tv_sec - (tmnow.tm_hour * 3600) - (tmnow.tm_min * 60) - (tmnow.tm_sec);
					if (beg_today < time) {
						/* Today */
						res = wait_file(chan,ints, "digits/today",lang);
					} else if ((beg_today - 86400) < time) {
						/* Yesterday */
						res = wait_file(chan,ints, "digits/yesterday",lang);
					} else if (beg_today - 86400 * 6 < time) {
						/* Within the last week */
						res = ast_say_date_with_format(chan, time, ints, lang, "A", timezone);
					} else {
						res = ast_say_date_with_format(chan, time, ints, lang, "'digits/es-el' Ad 'digits/es-de' B 'digits/es-de' Y", timezone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format(chan, time, ints, lang, "H 'digits/y' M", timezone);
				break;
			case 'S':
				/* Seconds */
				if (tm.tm_sec == 0) {
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
					res = wait_file(chan,ints,nextmsg,lang);
				} else if (tm.tm_sec < 10) {
					res = wait_file(chan,ints, "digits/oh",lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
						res = wait_file(chan,ints,nextmsg,lang);
					}
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

/* Dutch syntax */
int ast_say_date_with_format_nl(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone)
{
	struct tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	ast_localtime(&time,&tm,timezone);

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
				snprintf(nextmsg,sizeof(nextmsg), AST_SOUNDS "/%s", sndfile);
				res = wait_file(chan,ints,nextmsg,lang);
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
				res = ast_say_number(chan, tm.tm_mday, ints, lang, NULL);
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
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
				if (tm.tm_hour == 0)
					snprintf(nextmsg,sizeof(nextmsg), "digits/12");
				else if (tm.tm_hour > 12)
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan,ints,nextmsg,lang);
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				res = ast_say_number(chan, tm.tm_hour, ints, lang, (char *) NULL);
				if (!res) {
					res = wait_file(chan,ints, "digits/nl-uur",lang);
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
					snprintf(nextmsg,sizeof(nextmsg), "digits/p-m");
				else
					snprintf(nextmsg,sizeof(nextmsg), "digits/a-m");
				res = wait_file(chan,ints,nextmsg,lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				{
					struct timeval now;
					struct tm tmnow;
					time_t beg_today;

					gettimeofday(&now,NULL);
					ast_localtime(&now.tv_sec,&tmnow,timezone);
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
					struct tm tmnow;
					time_t beg_today;

					gettimeofday(&now,NULL);
					ast_localtime(&now.tv_sec,&tmnow,timezone);
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
				} else if (tm.tm_sec < 10) {
					res = wait_file(chan,ints, "digits/oh",lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
						res = wait_file(chan,ints,nextmsg,lang);
					}
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

/* Portuguese syntax */
int ast_say_date_with_format_pt(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone)
{
	struct tm tm;
	int res=0, offset, sndoffset;
	char sndfile[256], nextmsg[256];

	ast_localtime(&time,&tm,timezone);

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
				snprintf(nextmsg,sizeof(nextmsg), "%s", sndfile);
				res = wait_file(chan,ints,nextmsg,lang);
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
				res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
				break;
			case 'Y':
				/* Year */
				res = ast_say_number(chan, tm.tm_year + 1900, ints, lang, (char *) NULL);
				break;
			case 'I':
			case 'l':
				/* 12-Hour */
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
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				res = ast_say_number(chan, -tm.tm_hour, ints, lang, NULL);
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
				break;
			case 'M':
				/* Minute */
				if (tm.tm_min == 0) {
					res = wait_file(chan, ints, "digits/pt-hora", lang);
					if (tm.tm_hour != 1)
						if (!res)
							res = wait_file(chan, ints, "digits/pt-sss", lang);			} else {
					res = wait_file(chan,ints,"digits/pt-e",lang);
					if (!res)
						res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);	
				}
				break;
			case 'P':
			case 'p':
				/* AM/PM */
				if (tm.tm_hour > 12)
					res = wait_file(chan, ints, "digits/p-m", lang);
				else if (tm.tm_hour  && tm.tm_hour < 12)
					res = wait_file(chan, ints, "digits/a-m", lang);
				break;
			case 'Q':
				/* Shorthand for "Today", "Yesterday", or ABdY */
				{
					struct timeval now;
					struct tm tmnow;
					time_t beg_today;

					gettimeofday(&now,NULL);
					ast_localtime(&now.tv_sec,&tmnow,timezone);
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
						res = ast_say_date_with_format(chan, time, ints, lang, "Ad 'digits/pt-de' B 'digits/pt-de' Y", timezone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or ABdY */
				{
					struct timeval now;
					struct tm tmnow;
					time_t beg_today;

					gettimeofday(&now,NULL);
					ast_localtime(&now.tv_sec,&tmnow,timezone);
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
						res = ast_say_date_with_format(chan, time, ints, lang, "Ad 'digits/pt-de' B 'digits/pt-de' Y", timezone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format(chan, time, ints, lang, "H 'digits/pt-e' M", timezone);
				break;
			case 'S':
				/* Seconds */
				if (tm.tm_sec == 0) {
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
					res = wait_file(chan,ints,nextmsg,lang);
				} else if (tm.tm_sec < 10) {
					res = wait_file(chan,ints, "digits/oh",lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
						res = wait_file(chan,ints,nextmsg,lang);
					}
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

int ast_say_time(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	if (!strcasecmp(lang, "en") ) {	/* English syntax */
		return(ast_say_time_en(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "nl") ) {	/* Dutch syntax */
		return(ast_say_time_nl(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "pt") ) {	/* Portuguese syntax */
		return(ast_say_time_pt(chan, t, ints, lang));
	}

	/* Default to English */
	return(ast_say_time_en(chan, t, ints, lang));
}

/* English syntax */
int ast_say_time_en(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	struct tm tm;
	int res = 0;
	int hour, pm=0;
	localtime_r(&t,&tm);
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

/* Dutch syntax */
int ast_say_time_nl(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	struct tm tm;
	int res = 0;
	int hour;
	localtime_r(&t,&tm);
	hour = tm.tm_hour;
	if (!res)
		res = ast_say_number(chan, hour, ints, lang, (char *) NULL);

	if (!res)
		res = ast_streamfile(chan, "digits/nl-uur", lang);
	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
	    if (tm.tm_min > 0) 
		res = ast_say_number(chan, tm.tm_min, ints, lang, NULL);
	return res;
}

/* Portuguese syntax */
int ast_say_time_pt(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	struct tm tm;
	int res = 0;
	int hour;
	localtime_r(&t,&tm);
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

int ast_say_datetime(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	if (!strcasecmp(lang, "en") ) {	/* English syntax */
		return(ast_say_datetime_en(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "nl") ) {	/* Dutch syntax */
		return(ast_say_datetime_nl(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "pt") ) {	/* Portuguese syntax */
		return(ast_say_datetime_pt(chan, t, ints, lang));
	}

	/* Default to English */
	return(ast_say_datetime_en(chan, t, ints, lang));
}

/* English syntax */
int ast_say_datetime_en(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	struct tm tm;
	char fn[256];
	int res = 0;
	int hour, pm=0;
	localtime_r(&t,&tm);
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

/* Dutch syntax */
int ast_say_datetime_nl(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	struct tm tm;
	int res = 0;
	localtime_r(&t,&tm);
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

/* Portuguese syntax */
int ast_say_datetime_pt(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	struct tm tm;
	char fn[256];
	int res = 0;
	int hour, pm=0;
	localtime_r(&t,&tm);
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

int ast_say_datetime_from_now(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	if (!strcasecmp(lang, "en") ) {	/* English syntax */
		return(ast_say_datetime_from_now_en(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "pt") ) {	/* Portuguese syntax */
		return(ast_say_datetime_from_now_pt(chan, t, ints, lang));
	}

	/* Default to English */
	return(ast_say_datetime_from_now_en(chan, t, ints, lang));
}

/* English syntax */
int ast_say_datetime_from_now_en(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	int res=0;
	time_t nowt;
	int daydiff;
	struct tm tm;
	struct tm now;
	char fn[256];

	time(&nowt);

	localtime_r(&t,&tm);
	localtime_r(&nowt,&now);
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

/* Portuguese syntax */
int ast_say_datetime_from_now_pt(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	int res=0;
	time_t nowt;
	int daydiff;
	struct tm tm;
	struct tm now;
	char fn[256];

	time(&nowt);

	localtime_r(&t,&tm);
	localtime_r(&nowt,&now);
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
	snprintf(fn, sizeof(fn), "digits/pt-ah");
	if (!res)
		res = wait_file(chan, ints, fn, lang);
	if (tm.tm_hour != 1)
	if (!res)
		res = wait_file(chan, ints, "digits/pt-sss", lang);
	if (!res)
		res = ast_say_time(chan, t, ints, lang);
	return res;
}
