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
#include <asterisk/file.h>
#include <asterisk/channel.h>
#include <asterisk/logger.h>
#include <asterisk/say.h>
#include <asterisk/lock.h>
#include <asterisk/localtime.h>
#include "asterisk.h"
#include <stdio.h>

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
		if(strlen(fn)){ /* if length == 0, then skip this digit as it is invalid */
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

int ast_say_number_full(struct ast_channel *chan, int num, char *ints, char *language, int audiofd, int ctrlfd)
{
	int res = 0;
	int playh = 0;
	char fn[256] = "";
	if (!num) 
		return ast_say_digits_full(chan, 0,ints, language, audiofd, ctrlfd);
	if (0) {
	/* XXX Only works for english XXX */
	} else {
		/* Use english numbers if a given language is supported. */
		/* As a special case, Norwegian has the same numerical grammar
		   as English */
		if (strcasecmp(language, "no"))
			language = "en";
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
			} else {
				if (num < 1000){
					snprintf(fn, sizeof(fn), "digits/%d", (num/100));
					playh++;
					num -= ((num / 100) * 100);
				} else {
					if (num < 1000000) { /* 1,000,000 */
						res = ast_say_number_full(chan, num / 1000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						num = num % 1000;
						snprintf(fn, sizeof(fn), "digits/thousand");
					} else {
						if (num < 1000000000) {	/* 1,000,000,000 */
							res = ast_say_number_full(chan, num / 1000000, ints, language, audiofd, ctrlfd);
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
				res = ast_streamfile(chan, fn, language);
				if (!res) 
					res = ast_waitstream_full(chan, ints, audiofd, ctrlfd);
				ast_stopstream(chan);
			}
			
		}
	}
	return res;
}

int ast_say_number(struct ast_channel *chan, int num, char *ints, char *language)
{
	/* XXX Should I be merged with ast_say_number_full XXX */
	int res = 0;
	int playh = 0;
	char fn[256] = "";
	if (!num) 
		return ast_say_digits(chan, 0,ints, language);
	if (0) {
	/* XXX Only works for english XXX */
	} else {
		/* Use english numbers */
		language = "en";
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
			} else {
				if (num < 1000){
					snprintf(fn, sizeof(fn), "digits/%d", (num/100));
					playh++;
					num -= ((num / 100) * 100);
				} else {
					if (num < 1000000) {
						res = ast_say_number(chan, num / 1000, ints, language);
						if (res)
							return res;
						num = num % 1000;
						snprintf(fn, sizeof(fn), "digits/thousand");
					} else {
						if (num < 1000000000) {
							res = ast_say_number(chan, num / 1000000, ints, language);
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
				res = ast_streamfile(chan, fn, language);
				if (!res) 
					res = ast_waitstream(chan, ints);
				ast_stopstream(chan);
			}
			
		}
	}
	return res;
}
int ast_say_date(struct ast_channel *chan, time_t t, char *ints, char *lang)
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
		res = ast_say_number(chan, tm.tm_mday, ints, lang);

	if (!res)
		res = ast_waitstream(chan, ints);
	if (!res)
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang);
	return res;
}

static int wait_file(struct ast_channel *chan, char *ints, char *file, char *lang) 
{
	int res;
	if ((res = ast_streamfile(chan, file, lang)))
		ast_log(LOG_WARNING, "Unable to play message %s\n", file);
	if (!res)
		res = ast_waitstream(chan, ints);
	return res;
}

int ast_say_date_with_format(struct ast_channel *chan, time_t time, char *ints, char *lang, char *format, char *timezone)
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

int ast_say_time(struct ast_channel *chan, time_t t, char *ints, char *lang)
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
		res = ast_say_number(chan, hour, ints, lang);

	if (tm.tm_min > 9) {
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang);
	} else if (tm.tm_min) {
		if (!res)
			res = ast_streamfile(chan, "digits/oh", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang);
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

int ast_say_datetime(struct ast_channel *chan, time_t t, char *ints, char *lang)
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
		res = ast_say_number(chan, tm.tm_mday, ints, lang);

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
		res = ast_say_number(chan, hour, ints, lang);

	if (tm.tm_min > 9) {
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang);
	} else if (tm.tm_min) {
		if (!res)
			res = ast_streamfile(chan, "digits/oh", lang);
		if (!res)
			res = ast_waitstream(chan, ints);
		if (!res)
			res = ast_say_number(chan, tm.tm_min, ints, lang);
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
		res = ast_say_number(chan, tm.tm_year + 1900, ints, lang);
	return res;
}

int ast_say_datetime_from_now(struct ast_channel *chan, time_t t, char *ints, char *lang)
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
			res = ast_say_number(chan, tm.tm_mday, ints, lang);

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

