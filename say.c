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
 *
 * 12-16-2004 : Support for Greek added by InAccess Networks (work funded by HOL, www.hol.gr)
 *  						George Konstantoulakis <gkon@inaccessnetworks.com>
 */

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <asterisk/file.h>
#include <asterisk/channel.h>
#include <asterisk/logger.h>
#include <asterisk/say.h>
#include <asterisk/lock.h>
#include <asterisk/localtime.h>
#include <asterisk/utils.h>
#include "asterisk.h"
#include <stdio.h>

#ifdef SOLARIS
#include <iso/limits_iso.h>
#endif


/* Forward declaration */
static int wait_file(struct ast_channel *chan, const char *ints, const char *file, const char *lang);

int ast_say_digit_str(struct ast_channel *chan, const char *fn2, const char *ints, const char *lang)
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

int ast_say_character_str(struct ast_channel *chan, const char *fn2, const char *ints, const char *lang)
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

int ast_say_phonetic_str(struct ast_channel *chan, const char *fn2, const char *ints, const char *lang)
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

int ast_say_digit_str_full(struct ast_channel *chan, const char *fn2, const char *ints, const char *lang, int audiofd, int ctrlfd)
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

int ast_say_character_str_full(struct ast_channel *chan, const char *fn2, const char *ints, const char *lang, int audiofd, int ctrlfd)
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

int ast_say_phonetic_str_full(struct ast_channel *chan, const char *fn2, const char *ints, const char *lang, int audiofd, int ctrlfd)
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

int ast_say_digits(struct ast_channel *chan, int num, const char *ints, const char *lang)
{
	/* XXX Should I be merged with say_digits_full XXX */
	char fn2[256];
	snprintf(fn2, sizeof(fn2), "%d", num);
	return ast_say_digit_str(chan, fn2, ints, lang);
}

int ast_say_digits_full(struct ast_channel *chan, int num, const char *ints, const char *lang, int audiofd, int ctrlfd)
{
	char fn2[256];
	snprintf(fn2, sizeof(fn2), "%d", num);
	return ast_say_digit_str_full(chan, fn2, ints, lang, audiofd, ctrlfd);
}

/* Forward declarations */
/* Syntaxes supported, not really language codes.
      da    - Danish
      de    - German
      en    - English (US)
      en_GB - English (British)
      es    - Spanish, Mexican
      fr    - French
      it    - Italian
      nl    - Dutch
      no    - Norwegian
      pl    - Polish       
      pt    - Portuguese
      se    - Swedish
      tw    - Taiwanese

 Gender:
 For Some languages the numbers differ for gender and plural
 Use the option argument 'f' for female, 'm' for male and 'n' for neuter in languages like Portuguese, French, Spanish and German.
 use the option argument 'c' is for commune and 'n' for neuter gender in nordic languages like Danish, Swedish and Norwegian.
 use the option argument 'p' for plural enumerations like in German
 
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

 Italian sound files needed for Time/Date functions:
 ore-una
 ore-mezzanotte

*/

/* Forward declarations of language specific variants of ast_say_number_full */
static int ast_say_number_full_en(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_cz(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_da(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_de(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_en_GB(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_es(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_fr(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_it(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_nl(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_no(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_pl(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_pt(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_se(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);
static int ast_say_number_full_tw(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_number_full_gr(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);

/* Forward declarations of language specific variants of ast_say_enumeration_full */
static int ast_say_enumeration_full_en(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd);
static int ast_say_enumeration_full_de(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd);

/* Forward declarations of ast_say_date, ast_say_datetime and ast_say_time functions */
static int ast_say_date_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_date_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);

static int ast_say_date_with_format_en(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone);
static int ast_say_date_with_format_de(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone);
static int ast_say_date_with_format_es(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone);
static int ast_say_date_with_format_fr(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone);
static int ast_say_date_with_format_it(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone);
static int ast_say_date_with_format_nl(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone);
static int ast_say_date_with_format_pt(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone);
static int ast_say_date_with_format_tw(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone);
static int ast_say_date_with_format_gr(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone);

static int ast_say_time_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_tw(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_time_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);

static int ast_say_datetime_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_tw(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);

static int ast_say_datetime_from_now_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_from_now_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang);
static int ast_say_datetime_from_now_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang);

static int wait_file(struct ast_channel *chan, const char *ints, const char *file, const char *lang) 
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
int ast_say_number_full(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	if (!strcasecmp(language,"en") ) {	/* English syntax */
	   return(ast_say_number_full_en(chan, num, ints, language, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "cz") ) {	/* Czech syntax */
	   return(ast_say_number_full_cz(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "da") ) {	/* Danish syntax */
	   return(ast_say_number_full_da(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "de") ) {	/* German syntax */
	   return(ast_say_number_full_de(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "en_GB") ) {	/* British syntax */
	   return(ast_say_number_full_en_GB(chan, num, ints, language, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "no") ) {	/* Norwegian syntax */
	   return(ast_say_number_full_no(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "es") || !strcasecmp(language, "mx")) {	/* Spanish syntax */
	   return(ast_say_number_full_es(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "fr") ) {	/* French syntax */
	   return(ast_say_number_full_fr(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "it") ) {	/* Italian syntax */
	   return(ast_say_number_full_it(chan, num, ints, language, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "nl") ) {	/* Dutch syntax */
	   return(ast_say_number_full_nl(chan, num, ints, language, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "pl") ) {	/* Polish syntax */
	   return(ast_say_number_full_pl(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "pt") ) {	/* Portuguese syntax */
	   return(ast_say_number_full_pt(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "se") ) {	/* Swedish syntax */
	   return(ast_say_number_full_se(chan, num, ints, language, options, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "tw")) {	/* Taiwanese syntax */
	   return(ast_say_number_full_tw(chan, num, ints, language, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "gr") ) {	/* Greek syntax */
		return(ast_say_number_full_gr(chan, num, ints, language, audiofd, ctrlfd));
	}

	/* Default to english */
	return(ast_say_number_full_en(chan, num, ints, language, audiofd, ctrlfd));
}

/*--- ast_say_number: call language-specific functions without file descriptors */
int ast_say_number(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options)
{
	return(ast_say_number_full(chan, num, ints, language, options, -1, -1));
}

/*--- ast_say_number_full_en: English syntax */
/* This is the default syntax, if no other syntax defined in this file is used */
static int ast_say_number_full_en(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	while(!res && (num || playh)) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (playh) {
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

/*--- ast_say_number_full_cz: Czech syntax */
/* files needed:
 * 1m,2m - gender male
 * 1w,2w - gender female
 * 3,4,...,20
 * 30,40,...,90
 * 
 * hundereds - 100 - sto, 200 - 2ste, 300,400 3,4sta, 500,600,...,900 5,6,...9set 
 * 
 * for each number 10^(3n + 3) exist 3 files represented as:
 * 		1 tousand = jeden tisic = 1_E3
 * 		2,3,4 tousands = dva,tri,ctyri tisice = 2-3_E3
 * 		5,6,... tousands = pet,sest,... tisic = 5_E3
 *
 * 		million = _E6
 * 		miliard = _E9
 * 		etc...
 *
 * tousand, milion are  gender male, so 1 and 2 is 1m 2m
 * miliard is gender female, so 1 and 2 is 1w 2w
 */
static int ast_say_number_full_cz(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
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
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);
	
	while(!res && (num || playh)) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (num < 3 ) {
			snprintf(fn, sizeof(fn), "digits/%d%c",num,options[0]);
			playh = 0;
			num = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d",num);
			playh = 0;
			num = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num -= ((num / 10) * 10);
		} else if (num < 1000) {
			hundered = num / 100;
			if ( hundered == 1 ) {
				snprintf(fn, sizeof(fn), "digits/1sto");
			} else if ( hundered == 2 ) {
				snprintf(fn, sizeof(fn), "digits/2ste");
			} else {
					res = ast_say_number_full_cz(chan,hundered,ints,language,options,audiofd,ctrlfd);
				if (res)
					return res;
				if (hundered == 3 || hundered == 4) {	
					snprintf(fn, sizeof(fn), "digits/sta");
				} else if ( hundered > 4 ) {
					snprintf(fn, sizeof(fn), "digits/set");
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
			if ( left > 1 )	{ /* we dont say "one thousand" but only thousand */
				res = ast_say_number_full_cz(chan,left,ints,language,options,audiofd,ctrlfd);
				if (res) 
					return res;
			}
			if ( left >= 5 ) { /* >= 5 have the same declesion */
				snprintf(fn, sizeof(fn), "digits/5_E%d",length-1);	
			} else if ( left >= 2 && left <= 4 ) {
				snprintf(fn, sizeof(fn), "digits/2-4_E%d",length-1);
			} else { /* left == 1 */
				snprintf(fn, sizeof(fn), "digits/1_E%d",length-1);
			}
			num -= left * (exp10_int(length-1));
		}
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
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

/*--- ast_say_number_full_da: Danish syntax */
/* New files:
 In addition to English, the following sounds are required: "1N", "millions", "and" and "1-and" through "9-and" 
 */
static int ast_say_number_full_da(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playa = 0;
	int cn = 1;		/* +1 = commune; -1 = neuter */
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	if (options && !strncasecmp(options, "n",1)) cn = -1;

	while(!res && (num || playh || playa )) {
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
			snprintf(fn, sizeof(fn), "digits/minus");
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (playh) {
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

/*--- ast_say_number_full_de: German syntax */
/* New files:
 In addition to English, the following sounds are required:
 "millions"
 "1-and" through "9-and" 
 "1F" (eine)
 "1N" (ein)
 NB "1" is recorded as 'eins'
 */
static int ast_say_number_full_de(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0, t = 0;
	int mf = 1;                            /* +1 = male and neuter; -1 = female */
	char fn[256] = "";
	char fna[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	if (options && (!strncasecmp(options, "f",1)))
		mf = -1;

	while(!res && num) {
		/* The grammar for German numbers is the same as for English except
		* for the following:
		* - numbers 20 through 99 are said in reverse order, i.e. 21 is
		*   "one-and twenty" and 68 is "eight-and sixty".
		* - "one" varies according to gender
		* - 100 is 'hundert', however all other instances are 'ein hundert'
		* - 1000 is 'tausend', however all other instances are 'ein tausend'
		* - 1000000 is always 'eine million'
		* - "million" is different in singular and plural form
		*/
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (num < 100 && t) {
			snprintf(fn, sizeof(fn), "digits/and");
			t = 0;
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
			snprintf(fn, sizeof(fn), "digits/hundred");
			num = 0;
		} else if (num < 1000) {
			int hundreds = num / 100;
			num = num % 100;
			if (hundreds == 1) {
				snprintf(fn, sizeof(fn), "digits/1N");
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", hundreds);
			}
			snprintf(fna, sizeof(fna), "digits/hundred");
			t = 1;
		} else if (num == 1000 && t == 0) {
			snprintf(fn, sizeof(fn), "digits/thousand");
			num = 0;
		} else 	if (num < 1000000) {
			int thousands = num / 1000;
			num = num % 1000;
			t = 1;
			if (thousands == 1) {
				snprintf(fn, sizeof(fn), "digits/1N");
				snprintf(fna, sizeof(fna), "digits/thousand");
			} else {
				res = ast_say_number_full_de(chan, thousands, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
				snprintf(fn, sizeof(fn), "digits/thousand");
			}
		} else if (num < 1000000000) {
			int millions = num / 1000000;
			num = num % 1000000;
			t = 1;
			if (millions == 1) {
				snprintf(fn, sizeof(fn), "digits/1F");
				snprintf(fna, sizeof(fna), "digits/million");
			} else {
				res = ast_say_number_full_de(chan, millions, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
				snprintf(fn, sizeof(fn), "digits/millions");
			}
		} else if (num <= INT_MAX) {
			int billions = num / 1000000000;
			num = num % 1000000000;
			t = 1;
			if (billions == 1) {
				snprintf(fn, sizeof(fn), "digits/1F");
				snprintf(fna, sizeof(fna), "digits/milliard");
			} else {
				res = ast_say_number_full_de(chan, billions, ints, language, options, audiofd, ctrlfd);
				if (res) {
					return res;
				}
				snprintf(fn, sizeof(fn), "digits/milliards");
			}
		} else {
			ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
			res = -1;
		}
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
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

/*--- ast_say_number_full_en_GB: British and Norwegian syntax */
/* New files:
 In addition to American English, the following sounds are required:  "and"
 */
static int ast_say_number_full_en_GB(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playa = 0;
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	while(!res && (num || playh || playa )) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (playh) {
			snprintf(fn, sizeof(fn), "digits/hundred");
			playh = 0;
		} else if (playa) {
			snprintf(fn, sizeof(fn), "digits/and");
			playa = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num -= ((num / 10) * 10);
		} else if (num < 1000) {
			int hundreds = num / 100;
			snprintf(fn, sizeof(fn), "digits/%d", (num / 100));

			playh++;
			num -= 100 * hundreds;
			if (num)
				playa++;
		} else 	if (num < 1000000) {
			res = ast_say_number_full_en_GB(chan, num / 1000, ints, language, audiofd, ctrlfd);
			if (res)
				return res;
			snprintf(fn, sizeof(fn), "digits/thousand");
			num = num % 1000;
			if (num && num < 100)
				playa++;
		} else 	if (num < 1000000000) {
				int millions = num / 1000000;
				res = ast_say_number_full_en_GB(chan, millions, ints, language, audiofd, ctrlfd);
				if (res)
					return res;
				snprintf(fn, sizeof(fn), "digits/million");
				num = num % 1000000;
				if (num && num < 100)
					playa++;
		} else {
				ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
				res = -1;
		}
		
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
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

/*--- ast_say_number_full_es: Spanish syntax */
/* New files:
 Requires a few new audios:
   1F.gsm: feminine 'una'
   21.gsm thru 29.gsm, cien.gsm, mil.gsm, millon.gsm, millones.gsm, 100.gsm, 200.gsm, 300.gsm, 400.gsm, 500.gsm, 600.gsm, 700.gsm, 800.gsm, 900.gsm, y.gsm 
 */
static int ast_say_number_full_es(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playa = 0;
	int mf = 1;                            /* +1 = male; -1 = female */
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	if (options && !strncasecmp(options, "f",1))
		mf = -1;

	while (!res && num) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (playa) {
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

/*--- ast_say_number_full_fr: French syntax */
/* 	Extra sounds needed:
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
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);
	
	if (options && !strncasecmp(options, "f",1))
		mf = -1;

	while(!res && (num || playh || playa)) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (playh) {
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

/*--- ast_say_number_full_it:  Italian */
static int ast_say_number_full_it(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
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
			if (num < 0) {
				snprintf(fn, sizeof(fn), "digits/minus");
				if ( num > INT_MIN ) {
					num = -num;
				} else {
					num = 0;
				}	
			} else if (playh) {
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
							res = ast_say_number_full_it(chan, num / 1000, ints, language, audiofd, ctrlfd);
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
								res = ast_say_number_full_it(chan, num / 1000000, ints, language, audiofd, ctrlfd);
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

/*--- ast_say_number_full_nl: dutch syntax */
/* New files: digits/nl-en
 */
static int ast_say_number_full_nl(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int units = 0;
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);
	while (!res && (num || playh )) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (playh) {
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

/*--- ast_say_number_full_no: Norwegian syntax */
/* New files:
 In addition to American English, the following sounds are required:  "and", "1N"
 */
static int ast_say_number_full_no(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int playa = 0;
	int cn = 1;		/* +1 = commune; -1 = neuter */
	char fn[256] = "";
	
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);
	
	if (options && !strncasecmp(options, "n",1)) cn = -1;

	while(!res && (num || playh || playa )) {
		/* The grammar for Norwegian numbers is the same as for English except
		* for the following:
		* - 1 exists in both commune ("en", file "1") and neuter ("ett", file "1N")
		*   "and" before the last two digits, i.e. 2034 is "two thousand and
		*   thirty-four" and 1000012 is "one million and twelve".
		*/
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (playh) {
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
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num -= ((num / 10) * 10);
		} else if (num < 1000) {
			int hundreds = num / 100;
			if (hundreds == 1)
				snprintf(fn, sizeof(fn), "digits/1N");
			else
				snprintf(fn, sizeof(fn), "digits/%d", (num / 100));

			playh++;
			num -= 100 * hundreds;
			if (num)
				playa++;
		} else 	if (num < 1000000) {
			res = ast_say_number_full_no(chan, num / 1000, ints, language, "n", audiofd, ctrlfd);
			if (res)
				return res;
			snprintf(fn, sizeof(fn), "digits/thousand");
			num = num % 1000;
			if (num && num < 100)
				playa++;
		} else 	if (num < 1000000000) {
				int millions = num / 1000000;
				res = ast_say_number_full_no(chan, millions, ints, language, "c", audiofd, ctrlfd);
				if (res)
					return res;
				snprintf(fn, sizeof(fn), "digits/million");
				num = num % 1000000;
				if (num && num < 100)
					playa++;
		} else {
				ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
				res = -1;
		}
		
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
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
	ast_log(LOG_DEBUG, "Trying to play: %s\n", file_name);
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

	if ( m100 > 0 && m100 <=9 ) {
		if (m1000>0)
			pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry2[m100]);
		else
			pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry[m100]);
	} else if (m100 % 10 == 0) {
		pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->dziesiatki[m100 / 10]);
	} else if (m100 <= 19 ) {
		pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->nastki[m100 % 10]);
	} else if (m100 != 0) {
		if (odm->separator_dziesiatek[0]==' ') {
			pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->dziesiatki[m100 / 10]);
			pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, odm->cyfry2[m100 % 10]);
		} else {
			char buf[10];
			char *b = buf;
			b = pl_append(b, odm->dziesiatki[m100 / 10]);  
			b = pl_append(b, odm->separator_dziesiatek);  
			b = pl_append(b, odm->cyfry2[m100 % 10]); 
			pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, buf);
		}
	} 

	if (rzad > 0) {
		pl_odtworz_plik(chan, language, audiofd, ctrlfd, ints, pl_rzad_na_tekst(odm, i, rzad));
	}
}

/* ast_say_number_full_pl: Polish syntax */
static int ast_say_number_full_pl(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
/*
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
2		dwie
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
{
	char *zenski_cyfry[] = {"0","1z", "2z", "3", "4", "5", "6", "7", "8", "9"};

	char *zenski_cyfry2[] = {"0","1", "2z", "3", "4", "5", "6", "7", "8", "9"};

	char *meski_cyfry[] = {"0","1", "2-1m", "3-1m", "4-1m", "5m",  /*"2-1mdwaj"*/ "6m", "7m", "8m", "9m"};

	char *meski_cyfry2[] = {"0","1", "2-2m", "3-2m", "4-2m", "5m", "6m", "7m", "8m", "9m"};

	char *meski_setki[] = {"", "100m", "200m", "300m", "400m", "500m", "600m", "700m", "800m", "900m"};

	char *meski_dziesiatki[] = {"", "10m", "20m", "30m", "40m", "50m", "60m", "70m", "80m", "90m"};

	char *meski_nastki[] = {"", "11m", "12m", "13m", "14m", "15m", "16m", "17m", "18m", "19m"};

	char *nijaki_cyfry[] = {"0","1", "2", "3", "4", "5", "6", "7", "8", "9"};

	char *nijaki_cyfry2[] = {"0","1", "2", "3", "4", "5", "6", "7", "8", "9"};

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
		odmiana_nieosobowa = (odmiana *) malloc(sizeof(odmiana));

		odmiana_nieosobowa->separator_dziesiatek = "_";

		memcpy(odmiana_nieosobowa->cyfry, nijaki_cyfry, sizeof(odmiana_nieosobowa->cyfry));
		memcpy(odmiana_nieosobowa->cyfry2, nijaki_cyfry2, sizeof(odmiana_nieosobowa->cyfry));
		memcpy(odmiana_nieosobowa->setki, nijaki_setki, sizeof(odmiana_nieosobowa->setki));
		memcpy(odmiana_nieosobowa->dziesiatki, nijaki_dziesiatki, sizeof(odmiana_nieosobowa->dziesiatki));
		memcpy(odmiana_nieosobowa->nastki, nijaki_nastki, sizeof(odmiana_nieosobowa->nastki));
		memcpy(odmiana_nieosobowa->rzedy, rzedy, sizeof(odmiana_nieosobowa->rzedy));
	}

	if (odmiana_zenska == NULL) {
		odmiana_zenska = (odmiana *) malloc(sizeof(odmiana));

		odmiana_zenska->separator_dziesiatek = " ";

		memcpy(odmiana_zenska->cyfry, zenski_cyfry, sizeof(odmiana_zenska->cyfry));
		memcpy(odmiana_zenska->cyfry2, zenski_cyfry2, sizeof(odmiana_zenska->cyfry));
		memcpy(odmiana_zenska->setki, nijaki_setki, sizeof(odmiana_zenska->setki));
		memcpy(odmiana_zenska->dziesiatki, nijaki_dziesiatki, sizeof(odmiana_zenska->dziesiatki));
		memcpy(odmiana_zenska->nastki, nijaki_nastki, sizeof(odmiana_zenska->nastki));
		memcpy(odmiana_zenska->rzedy, rzedy, sizeof(odmiana_zenska->rzedy));
	}

	if (odmiana_meska == NULL) {
		odmiana_meska = (odmiana *) malloc(sizeof(odmiana));

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

/* ast_say_number_full_pt: Portuguese syntax */
/* 	Extra sounds needed: */
/* 	For feminin all sound files end with F */
/*	100E for 100+ something */
/*	1000000S for plural */
/*	pt-e for 'and' */
static int ast_say_number_full_pt(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	int mf = 1;                            /* +1 = male; -1 = female */
	char fn[256] = "";

	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	if (options && !strncasecmp(options, "f",1))
		mf = -1;

	while(!res && num ) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
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
				/* no thousands */
				((!((num / 1000) % 1000) && ((num % 1000) < 100 || !(num % 100))) ||
				/* no hundreds and below */
				(!(num % 1000) && (((num / 1000) % 1000) < 100 || !((num / 1000) % 100))) ) )
				playh = 1;
			num = num % 1000000;
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

/*--- ast_say_number_full_se: Swedish syntax */
static int ast_say_number_full_se(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";
	int cn = 1;		/* +1 = commune; -1 = neuter */
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);
	if (options && !strncasecmp(options, "n",1)) cn = -1;

	while(!res && (num || playh)) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus");
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (playh) {
			snprintf(fn, sizeof(fn), "digits/hundred");
			playh = 0;
		} else if (num < 20) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num < 100) {
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num -= ((num / 10) * 10);
		} else if (num == 1 && cn == -1) {	/* En eller ett? */
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
					if (res) {
						return res;
					}
					num = num % 1000;
					snprintf(fn, sizeof(fn), "digits/thousand");
				} else {
					if (num < 1000000000) {	/* 1,000,000,000 */
						res = ast_say_number_full_se(chan, num / 1000000, ints, language, options, audiofd, ctrlfd);
						if (res) {
							return res;
						}
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
				if ((audiofd > -1) && (ctrlfd > -1))
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				else
					res = ast_waitstream(chan, ints);
				ast_stopstream(chan);
			}
		}
	}
	return res;
}

/*--- ast_say_number_full_tw: Taiwanese syntax */
static int ast_say_number_full_tw(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";
	if (!num)
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	while(!res && (num || playh)) {
			if (num < 0) {
				snprintf(fn, sizeof(fn), "digits/minus");
				if ( num > INT_MIN ) {
					num = -num;
				} else {
					num = 0;
				}	
			} else if (playh) {
				snprintf(fn, sizeof(fn), "digits/hundred");
				playh = 0;
			} else	if (num < 10) {
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
						res = ast_say_number_full_tw(chan, num / 1000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						num = num % 1000;
						snprintf(fn, sizeof(fn), "digits/thousand");
					} else {
						if (num < 1000000000) {	/* 1,000,000,000 */
							res = ast_say_number_full_tw(chan, num / 1000000, ints, language, audiofd, ctrlfd);
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

/*--- ast_say_enumeration_full: call language-specific functions */
/* Called from AGI */
int ast_say_enumeration_full(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	if (!strcasecmp(language,"en") ) {	/* English syntax */
	   return(ast_say_enumeration_full_en(chan, num, ints, language, audiofd, ctrlfd));
	} else if (!strcasecmp(language, "de") ) {	/* German syntax */
	   return(ast_say_enumeration_full_de(chan, num, ints, language, options, audiofd, ctrlfd));
	} 
	
	/* Default to english */
	return(ast_say_enumeration_full_en(chan, num, ints, language, audiofd, ctrlfd));
}

/*--- ast_say_enumeration: call language-specific functions without file descriptors */
int ast_say_enumeration(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options)
{
	return(ast_say_enumeration_full(chan, num, ints, language, options, -1, -1));
}

/*--- ast_say_enumeration_full_en: English syntax */
/* This is the default syntax, if no other syntax defined in this file is used */
static int ast_say_enumeration_full_en(struct ast_channel *chan, int num, const char *ints, const char *language, int audiofd, int ctrlfd)
{
	int res = 0, t = 0;
	char fn[256] = "";
	
	while(!res && num) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus"); /* kind of senseless for enumerations, but our best effort for error checking */
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
				snprintf(fn, sizeof(fn), "digits/hundred");
			} else {
				snprintf(fn, sizeof(fn), "digits/h-hundred");
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
				snprintf(fn, sizeof(fn), "digits/thousand");
			} else {
				snprintf(fn, sizeof(fn), "digits/h-thousand");
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
				snprintf(fn, sizeof(fn), "digits/million");
			} else {
				snprintf(fn, sizeof(fn), "digits/h-million");
			}
		} else if (num < INT_MAX) {
			int billions = num / 1000000000;
			num = num % 1000000000;
			t = 1;
			res = ast_say_number_full_en(chan, billions, ints, language, audiofd, ctrlfd);
			if (res)
				return res;
			if (num) {					
				snprintf(fn, sizeof(fn), "digits/billion");
			} else {
				snprintf(fn, sizeof(fn), "digits/h-billion");
			}
		} else if (num == INT_MAX) {
			snprintf(fn, sizeof(fn), "digits/h-last");
			num = 0;
		} else {
			ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
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

/*--- ast_say_enumeration_full_de: German syntax */
static int ast_say_enumeration_full_de(struct ast_channel *chan, int num, const char *ints, const char *language, const char *options, int audiofd, int ctrlfd)
{
	/* options can be: '' or 'm' male gender; 'f' female gender; 'n' neuter gender; 'p' plural */
	int res = 0, t = 0;
	char fn[256] = "", fna[256] = "";
	char *gender;

	if (options && !strncasecmp(options, "f",1)) {
		gender = "F";
	} else if (options && !strncasecmp(options, "n",1)) {
		gender = "N";
	} else {
		gender = "";
	}

	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);

	while(!res && num) {
		if (num < 0) {
			snprintf(fn, sizeof(fn), "digits/minus"); /* kind of senseless for enumerations, but our best effort for error checking */
			if ( num > INT_MIN ) {
				num = -num;
			} else {
				num = 0;
			}	
		} else if (num < 100 && t) {
			snprintf(fn, sizeof(fn), "digits/and");
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
				snprintf(fn, sizeof(fn), "digits/1N");
			} else {
				snprintf(fn, sizeof(fn), "digits/%d", hundreds);
			}
			if (num) {					
				snprintf(fna, sizeof(fna), "digits/hundred");
			} else {
				snprintf(fna, sizeof(fna), "digits/h-hundred%s", gender);
			}
			t = 1;
		} else 	if (num < 1000000) {
			int thousands = num / 1000;
			num = num % 1000;
			if (thousands == 1) {
				if (num) {					
					snprintf(fn, sizeof(fn), "digits/1N");
					snprintf(fna, sizeof(fna), "digits/thousand");
				} else {
					if (t) {
						snprintf(fn, sizeof(fn), "digits/1N");
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
					snprintf(fn, sizeof(fn), "digits/thousand");
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
					snprintf(fn, sizeof(fn), "digits/1F");
					snprintf(fna, sizeof(fna), "digits/million");
				} else {
					snprintf(fn, sizeof(fn), "digits/1N");
					snprintf(fna, sizeof(fna), "digits/h-million%s", gender);
				}
			} else {
				res = ast_say_number_full_de(chan, millions, ints, language, options, audiofd, ctrlfd);
				if (res) {
					return res;
				}
				if (num) {					
					snprintf(fn, sizeof(fn), "digits/millions");
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
					snprintf(fn, sizeof(fn), "digits/1F");
					snprintf(fna, sizeof(fna), "digits/milliard");
				} else {
					snprintf(fn, sizeof(fn), "digits/1N");
					snprintf(fna, sizeof(fna), "digits/h-milliard%s", gender);
				}
			} else {
				res = ast_say_number_full_de(chan, billions, ints, language, options, audiofd, ctrlfd);
				if (res)
					return res;
				if (num) {					
					snprintf(fn, sizeof(fna), "digits/milliards");
				} else {
					snprintf(fn, sizeof(fna), "digits/h-milliard%s", gender);
				}
			}
			t = 1;
		} else if (num == INT_MAX) {
			snprintf(fn, sizeof(fn), "digits/h-last%s", gender);
			num = 0;
		} else {
			ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
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

int ast_say_date(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	if (!strcasecmp(lang, "en") ) {	/* English syntax */
		return(ast_say_date_en(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "de") ) {	/* German syntax */
		return(ast_say_date_de(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "fr") ) {	/* French syntax */
		return(ast_say_date_fr(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "nl") ) {	/* Dutch syntax */
		return(ast_say_date_nl(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "pt") ) {	/* Portuguese syntax */
		return(ast_say_date_pt(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "gr") ) {  			/* Greek syntax */
		return(ast_say_date_gr(chan, t, ints, lang));
	}

	/* Default to English */
	return(ast_say_date_en(chan, t, ints, lang));
}

/* English syntax */
int ast_say_date_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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

/* German syntax */
int ast_say_date_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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
			    /* say 1967 as 'neunzen hundert sieben und sechzig' */
				snprintf(fn,sizeof(fn), "digits/%d", (year / 100) );
				res = wait_file(chan, ints, fn, lang);
				if (!res) {
					res = wait_file(chan,ints, "digits/hundred", lang);
					if (!res && year % 100 != 0) {
						res = ast_say_number(chan, (year % 100), ints, lang, (char *) NULL);	
					}
				}
			}
		}
	}
	return res;
}

/* French syntax */
int ast_say_date_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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

/* Dutch syntax */
int ast_say_date_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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
int ast_say_date_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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

int ast_say_date_with_format(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
{
	if (!strcasecmp(lang, "en") ) {	/* English syntax */
		return(ast_say_date_with_format_en(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "de") ) {	/* German syntax */
		return(ast_say_date_with_format_de(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "es") || !strcasecmp(lang, "mx")) {	/* Spanish syntax */
		return(ast_say_date_with_format_es(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "fr") ) {	/* French syntax */
		return(ast_say_date_with_format_fr(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "it") ) {  /* Italian syntax */
		return(ast_say_date_with_format_it(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "nl") ) {	/* Dutch syntax */
		return(ast_say_date_with_format_nl(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "pt") ) {	/* Portuguese syntax */
		return(ast_say_date_with_format_pt(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "tw") ) {	/* Taiwanese syntax */
		return(ast_say_date_with_format_tw(chan, time, ints, lang, format, timezone));
	} else if (!strcasecmp(lang, "gr") ) {  			/* Greek syntax */
		return(ast_say_date_with_format_gr(chan, time, ints, lang, format, timezone));
	}

	/* Default to English */
	return(ast_say_date_with_format_en(chan, time, ints, lang, format, timezone));
}

/* English syntax */
int ast_say_date_with_format_en(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
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
int ast_say_date_with_format_de(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
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
						    /* say 1967 as 'neunzen hundert sieben und sechzig' */
							snprintf(nextmsg,sizeof(nextmsg), "digits/%d", (year / 100) );
							res = wait_file(chan,ints,nextmsg,lang);
							if (!res) {
								res = wait_file(chan,ints, "digits/hundred",lang);
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
					snprintf(nextmsg,sizeof(nextmsg), "digits/12");
				else if (tm.tm_hour > 12)
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour - 12);
				else
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour);
				res = wait_file(chan,ints,nextmsg,lang);
				if (!res) {
					res = wait_file(chan,ints,"digits/oclock",lang);
				}
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				res = ast_say_number(chan, tm.tm_hour, ints, lang, (char *) NULL);	
				if (!res) {
					res = wait_file(chan,ints,"digits/oclock",lang);
				}
				break;
			case 'M':
				/* Minute */
				if (tm.tm_min > 0 || format[offset+ 1 ] == 'S' ) { /* zero 'digits/0' only if seconds follow (kind of a hack) */
					res = ast_say_number(chan, tm.tm_min, ints, lang, "f");	
				}
				if ( !res && format[offset + 1] == 'S' ) { /* minutes only if seconds follow (kind of a hack) */
					if (tm.tm_min == 1) {
						res = wait_file(chan,ints,"digits/minute",lang);
					} else {
						res = wait_file(chan,ints,"digits/minutes",lang);
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
				/* Shorthand for "Today", "Yesterday", or AdBY */
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
						res = ast_say_date_with_format(chan, time, ints, lang, "AdBY", timezone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or AdBY */
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
						res = ast_say_date_with_format(chan, time, ints, lang, "AdBY", timezone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format(chan, time, ints, lang, "HM", timezone);
				break;
			case 'S':
				/* Seconds */
				res = wait_file(chan,ints, "digits/and",lang);
				if (!res) {
					res = ast_say_number(chan, tm.tm_sec, ints, lang, "f");	
					if (!res) {
						res = wait_file(chan,ints, "digits/seconds",lang);
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
int ast_say_date_with_format_es(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
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
			case 'm':
				/* First - Twelfth */
				snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
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

/* French syntax 
oclock = heure
*/
int ast_say_date_with_format_fr(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
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
			case 'm':
				/* First - Twelfth */
				snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
				res = wait_file(chan,ints,nextmsg,lang);
				break;
			case 'd':
			case 'e':
				/* First */
				if (tm.tm_mday == 1) {
					snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mday);
					res = wait_file(chan,ints,nextmsg,lang);
				} else {
					res = ast_say_number(chan, tm.tm_mday, ints, lang, (char * ) NULL);
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
							res = ast_say_number(chan, tm.tm_year - 100, ints, lang, (char * ) NULL);
						}
					}
				} else {
					if (tm.tm_year < 1) {
						/* I'm not going to handle 1900 and prior */
						/* We'll just be silent on the year, instead of bombing out. */
					} else {
						res = wait_file(chan,ints, "digits/19",lang);
						if (!res) {
							res = ast_say_number(chan, tm.tm_year, ints, lang, (char * ) NULL);
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
				res = ast_say_number(chan, tm.tm_hour, ints, lang, (char * ) NULL);
				if (!res) {
					if (format[offset] == 'H') {
						res = wait_file(chan,ints, "digits/oclock",lang);
					}
				}
				break;
			case 'M':
				/* Minute */
				res = wait_file(chan,ints, "digits/oclock",lang);
				if (res) break;
				if (tm.tm_min == 0) break;
				res = ast_say_number(chan, tm.tm_hour, ints, lang, (char * ) NULL);
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
				/* Shorthand for "Today", "Yesterday", or AdBY */
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
						res = ast_say_date_with_format(chan, time, ints, lang, "AdBY", timezone);
					}
				}
				break;
			case 'q':
				/* Shorthand for "" (today), "Yesterday", A (weekday), or AdBY */
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
						res = ast_say_date_with_format(chan, time, ints, lang, "AdBY", timezone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format(chan, time, ints, lang, "HM", timezone);
				break;
			case 'S':
				/* Seconds */
				res = ast_say_number(chan, tm.tm_hour, ints, lang, (char * ) NULL);
				if (!res) {
					res = wait_file(chan,ints, "digits/second",lang);
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

int ast_say_date_with_format_it(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
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
			case 'm':
				/* First - Twelfth */
				snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
				res = wait_file(chan,ints,nextmsg,lang);
				break;
			case 'd':
			case 'e':
				/* First day of the month is spelled as ordinal */
				if (tm.tm_mday == 1) {
					snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mday);
					res = wait_file(chan,ints,nextmsg,lang);
				} else {
					if (!res) {
						res = ast_say_number(chan, tm.tm_mday, ints, lang, (char *) NULL);
					}
				}
				break;
			case 'Y':
				/* Year */
				if (tm.tm_year > 99) {
					res = wait_file(chan,ints, "digits/ore-2000",lang);
					if (tm.tm_year > 100) {
						if (!res) {
						/* This works until the end of 2021 */
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year - 100);
						res = wait_file(chan,ints,nextmsg,lang);
						}
					}
				} else {
					if (tm.tm_year < 1) {
						/* I'm not going to handle 1900 and prior */
						/* We'll just be silent on the year, instead of bombing out. */
					} else {
						res = wait_file(chan,ints, "digits/ore-1900",lang);
						if ((!res) && (tm.tm_year != 0)) {
							if (tm.tm_year <= 21) {
								/* 1910 - 1921 */
								snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year);
								res = wait_file(chan,ints,nextmsg,lang);
							} else {
								/* 1922 - 1999, but sounds badly in 1928, 1931, 1938, etc... */
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
				if (tm.tm_hour == 0) {
					res = wait_file(chan,ints, "digits/ore-mezzanotte",lang);
				} else if (tm.tm_hour == 1) {
					res = wait_file(chan,ints, "digits/ore-una",lang);
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
						res = ast_say_date_with_format(chan, time, ints, lang, "AdB", timezone);
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
						res = ast_say_date_with_format(chan, time, ints, lang, "AdB", timezone);
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

/* Dutch syntax */
int ast_say_date_with_format_nl(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
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
			case 'm':
				/* First - Twelfth */
				snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
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
int ast_say_date_with_format_pt(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
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
			case 'm':
				/* First - Twelfth */
				snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
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

/* Taiwanese syntax */
int ast_say_date_with_format_tw(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
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
			case 'm':
				/* First - Twelfth */
				snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mon +1);
				res = wait_file(chan,ints,nextmsg,lang);
				break;
			case 'd':
			case 'e':
				/* First - Thirtyfirst */
				if (!(tm.tm_mday % 10) || (tm.tm_mday < 10)) {
					snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mday);
					res = wait_file(chan,ints,nextmsg,lang);
				} else {
					snprintf(nextmsg,sizeof(nextmsg), "digits/h-%dh", tm.tm_mday - (tm.tm_mday % 10));
					res = wait_file(chan,ints,nextmsg,lang);
					if(!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/h-%d", tm.tm_mday % 10);
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
							snprintf(nextmsg,sizeof(nextmsg), "digits/%d", (tm.tm_year - 100) / 10);
							res = wait_file(chan,ints,nextmsg,lang);
							if (!res) {
								snprintf(nextmsg,sizeof(nextmsg), "digits/%d", (tm.tm_year - 100) % 10);
								res = wait_file(chan,ints,nextmsg,lang);
							}
						}
					}
					if (!res) {
						res = wait_file(chan,ints, "digits/year",lang);
					}
				} else {
					if (tm.tm_year < 1) {
						/* I'm not going to handle 1900 and prior */
						/* We'll just be silent on the year, instead of bombing out. */
					} else {
						res = wait_file(chan,ints, "digits/1",lang);
						if (!res) {
							res = wait_file(chan,ints, "digits/9",lang);
						}
						if (!res) {
							if (tm.tm_year <= 9) {
								/* 1901 - 1909 */
								res = wait_file(chan,ints, "digits/0",lang);
								if (!res) {
									snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year);
									res = wait_file(chan,ints,nextmsg,lang);
								}
							} else {
								/* 1910 - 1999 */
								snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year / 10);
								res = wait_file(chan,ints,nextmsg,lang);
								if (!res) {
									snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_year % 10);
									res = wait_file(chan,ints,nextmsg,lang);
								}
							}
						}
					}
					if (!res) {
						res = wait_file(chan,ints, "digits/year",lang);
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
				if (!res) {
					res = wait_file(chan,ints, "digits/oclock",lang);
				}
				break;
			case 'H':
			case 'k':
				/* 24-Hour */
				if (!(tm.tm_hour % 10) || tm.tm_hour < 10) {
					if (tm.tm_hour < 10) {
						res = wait_file(chan, ints, "digits/0", lang);
					}
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour);
					res = wait_file(chan,ints,nextmsg,lang);
				} else {
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour - (tm.tm_hour % 10));
					res = wait_file(chan,ints,nextmsg,lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_hour % 10);
						res = wait_file(chan,ints,nextmsg,lang);
					}
				}
				if (!res) {
					res = wait_file(chan,ints, "digits/oclock",lang);
				}
				break;
			case 'M':
				/* Minute */
				if (!(tm.tm_min % 10) || tm.tm_min < 10) {
					if (tm.tm_min < 10) {
						res = wait_file(chan, ints, "digits/0", lang);
					}
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_min);
					res = wait_file(chan,ints,nextmsg,lang);
				} else {
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_min - (tm.tm_min % 10));
					res = wait_file(chan,ints,nextmsg,lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_min % 10);
						res = wait_file(chan,ints,nextmsg,lang);
					}
				}
				if (!res) {
					res = wait_file(chan,ints, "digits/minute",lang);
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
						res = ast_say_date_with_format(chan, time, ints, lang, "YBdA", timezone);
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
						res = ast_say_date_with_format(chan, time, ints, lang, "YBdA", timezone);
					}
				}
				break;
			case 'R':
				res = ast_say_date_with_format(chan, time, ints, lang, "HM", timezone);
				break;
			case 'S':
				/* Seconds */
				if (!(tm.tm_sec % 10) || tm.tm_sec < 10) {
					if (tm.tm_sec < 10) {
						res = wait_file(chan, ints, "digits/0", lang);
					}
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec);
					res = wait_file(chan,ints,nextmsg,lang);
				} else {
					snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec - (tm.tm_sec % 10));
					res = wait_file(chan,ints,nextmsg,lang);
					if (!res) {
						snprintf(nextmsg,sizeof(nextmsg), "digits/%d", tm.tm_sec % 10);
						res = wait_file(chan,ints,nextmsg,lang);
					}
				}
				if (!res) {
					res = wait_file(chan,ints, "digits/second",lang);
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

int ast_say_time(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	if (!strcasecmp(lang, "en") ) {	/* English syntax */
		return(ast_say_time_en(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "de") ) {	/* German syntax */
		return(ast_say_time_de(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "fr") ) {	/* French syntax */
		return(ast_say_time_fr(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "nl") ) {	/* Dutch syntax */
		return(ast_say_time_nl(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "pt") ) {	/* Portuguese syntax */
		return(ast_say_time_pt(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "tw") ) {	/* Taiwanese syntax */
		return(ast_say_time_tw(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "gr") ) {  			/* Greek syntax */
		return(ast_say_time_gr(chan, t, ints, lang));
	}

	/* Default to English */
	return(ast_say_time_en(chan, t, ints, lang));
}

/* English syntax */
int ast_say_time_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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

/* German syntax */
int ast_say_time_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct tm tm;
	int res = 0;
	localtime_r(&t,&tm);
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

/* French syntax */
int ast_say_time_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct tm tm;
	int res = 0;
	localtime_r(&t,&tm);

	res = ast_say_number(chan, tm.tm_hour, ints, lang, "f");
	if (!res)
		res = ast_streamfile(chan, "digits/oclock", lang);
	if (tm.tm_min) {
		if (!res)
		res = ast_say_number(chan, tm.tm_min, ints, lang, (char *) NULL);
	}
	return res;
}

/* Dutch syntax */
int ast_say_time_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct tm tm;
	int res = 0;
	localtime_r(&t,&tm);
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

/* Portuguese syntax */
int ast_say_time_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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

/* Taiwanese syntax */
int ast_say_time_tw(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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

int ast_say_datetime(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	if (!strcasecmp(lang, "en") ) {	/* English syntax */
		return(ast_say_datetime_en(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "de") ) {	/* German syntax */
		return(ast_say_datetime_de(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "fr") ) {	/* French syntax */
		return(ast_say_datetime_fr(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "nl") ) {	/* Dutch syntax */
		return(ast_say_datetime_nl(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "pt") ) {	/* Portuguese syntax */
		return(ast_say_datetime_pt(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "tw") ) {	/* Taiwanese syntax */
		return(ast_say_datetime_tw(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "gr") ) {  			/* Greek syntax */
		return(ast_say_datetime_gr(chan, t, ints, lang));
	}

	/* Default to English */
	return(ast_say_datetime_en(chan, t, ints, lang));
}

/* English syntax */
int ast_say_datetime_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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

/* German syntax */
int ast_say_datetime_de(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct tm tm;
	int res = 0;
	localtime_r(&t,&tm);
	res = ast_say_date(chan, t, ints, lang);
	if (!res) 
		ast_say_time(chan, t, ints, lang);
	return res;

}

/* French syntax */
int ast_say_datetime_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct tm tm;
	char fn[256];
	int res = 0;
	localtime_r(&t,&tm);

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

/* Dutch syntax */
int ast_say_datetime_nl(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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
int ast_say_datetime_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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

/* Taiwanese syntax */
int ast_say_datetime_tw(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct tm tm;
	char fn[256];
	int res = 0;
	int hour, pm=0;
	localtime_r(&t,&tm);
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

int ast_say_datetime_from_now(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	if (!strcasecmp(lang, "en") ) {	/* English syntax */
		return(ast_say_datetime_from_now_en(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "fr") ) {	/* French syntax */
		return(ast_say_datetime_from_now_fr(chan, t, ints, lang));
	} else if (!strcasecmp(lang, "pt") ) {	/* Portuguese syntax */
		return(ast_say_datetime_from_now_pt(chan, t, ints, lang));
	}

	/* Default to English */
	return(ast_say_datetime_from_now_en(chan, t, ints, lang));
}

/* English syntax */
int ast_say_datetime_from_now_en(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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

/* French syntax */
int ast_say_datetime_from_now_fr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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
int ast_say_datetime_from_now_pt(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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


/*********************************** GREEK SUPPORT ***************************************/


/*
 * digits/female-[1..4] : "Mia, dyo , treis, tessereis"
 */
static int gr_say_number_female(int num, struct ast_channel *chan, const char *ints, const char *lang){
	int tmp;
	int left;
	int res;
	char fn[256] = "";

	/* ast_log(LOG_DEBUG, "\n\n Saying number female %s %d \n\n",lang, num); */
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



/*
 *  	A list of the files that you need to create
 ->  	digits/xilia = "xilia"
 ->  	digits/myrio = "ekatomyrio"
 ->  	digits/thousands = "xiliades"
 ->  	digits/millions = "ektatomyria"
 ->  	digits/[1..12]   :: A pronunciation of th digits form 1 to 12 e.g. "tria"
 ->  	digits/[10..100]  :: A pronunciation of the tens from 10 to 90 
															 e,g 80 = "ogdonta" 
						 Here we must note that we use digits/tens/100 to utter "ekato"
						 and digits/hundred-100 to utter "ekaton"
 ->  	digits/hundred-[100...1000] :: A pronunciation of  hundreds from 100 to 1000 e.g 400 = 
																		 "terakosia". Here again we use hundreds/1000 for "xilia" 
						 and digits/thousnds for "xiliades"
*/

static int ast_say_number_full_gr(struct ast_channel *chan, int num, const char *ints, const char *language,int audiofd, int ctrlfd)
{
	int res = 0;
	char fn[256] = "";
	int i=0;

 
	if (!num) {
		snprintf(fn, sizeof(fn), "digits/0");
		res = ast_streamfile(chan, fn, chan->language);
		if (!res)
			return  ast_waitstream(chan, ints);
	}

	while(!res && num ) {
		i++;
		if (num < 13) {
			snprintf(fn, sizeof(fn), "digits/%d", num);
			num = 0;
		} else if (num <= 100) {
			/* 13 < num <= 100  */
			snprintf(fn, sizeof(fn), "digits/%d", (num /10) * 10);
			num -= ((num / 10) * 10); 
		} else if (num < 200) {
			/* 100 < num < 200 */
			snprintf(fn, sizeof(fn), "digits/hundred-100");
			num -= ((num / 100) * 100);
		}else if (num < 1000) {
			/* 200 < num < 1000 */
			snprintf(fn, sizeof(fn), "digits/hundred-%d", (num/100)*100);
			num -= ((num / 100) * 100);
		}else if (num < 2000){
			snprintf(fn, sizeof(fn), "digits/xilia");
			num -= ((num / 1000) * 1000);
		}
		else {
			/* num >  1000 */ 
			if (num < 1000000) {
				res = ast_say_number_full_gr(chan, (num / 1000), ints, chan->language, audiofd, ctrlfd);
				if (res)
					return res;
				num = num % 1000;
				snprintf(fn, sizeof(fn), "digits/thousands");
			}  else {
				if (num < 1000000000) { /* 1,000,000,000 */
					res = ast_say_number_full_gr(chan, (num / 1000000), ints, chan->language ,audiofd, ctrlfd);
					if (res)
						return res;
					num = num % 1000000;
					snprintf(fn, sizeof(fn), "digits/millions");
				} else {
					ast_log(LOG_DEBUG, "Number '%d' is too big for me\n", num);
					res = -1;
				}
			}
		} 
		if (!res) {
			if(!ast_streamfile(chan, fn, language)) {
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


/*
 * The format is  weekday - day - month -year
 * 
 * A list of the files that you need to create
 * digits/day-[1..7]  : "Deytera .. Paraskeyh"
 * digits/months/1..12 : "Ianouariou .. Dekembriou"  
													Attention the months are in 
				"gekinh klhsh"
 */


static int ast_say_date_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct tm tm;
	
	char fn[256];
	int res = 0;
	

	ast_localtime(&t,&tm,NULL);
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


 
/* A list of the files that you need to create
 * digits/female/1..4 : "Mia, dyo , treis, tesseris "
 * digits/kai : "KAI"
 * didgits : "h wra"
 * digits/p-m : "meta meshmbrias" 
 * digits/a-m : "pro meshmbrias"
 */

static int ast_say_time_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
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



static int ast_say_datetime_gr(struct ast_channel *chan, time_t t, const char *ints, const char *lang)
{
	struct tm tm;
	char fn[256];
	int res = 0;
	localtime_r(&t,&tm);

	
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

static int ast_say_date_with_format_gr(struct ast_channel *chan, time_t time, const char *ints, const char *lang, const char *format, const char *timezone)
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
			/* first - thirtyfirst */
			gr_say_number_female(tm.tm_mday, chan, ints, lang);
			break;
		case 'Y':
			/* Year */
			
			ast_say_number_full_gr(chan, 1900+tm.tm_year, ints, chan->language, -1, -1);
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
					res = ast_say_date_with_format(chan, time, ints, lang, "AdBY", timezone);
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
					res = ast_say_date_with_format(chan, time, ints, lang, "AdBY", timezone);
				}
			}
			break;
		case 'R':
			res = ast_say_date_with_format(chan, time, ints, lang, "HM", timezone);
			break;
		case 'S':
			/* Seconds */
			snprintf(nextmsg,sizeof(nextmsg), "digits/kai");
			res = wait_file(chan,ints,nextmsg,lang);
			if (!res)
				res = ast_say_number_full_gr(chan, tm.tm_sec, ints, lang, -1, -1);
			if (!res)
				snprintf(nextmsg,sizeof(nextmsg), "digits/seconds");
			res = wait_file(chan,ints,nextmsg,lang);
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
