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
#include <asterisk/file.h>
#include <asterisk/channel.h>
#include <asterisk/logger.h>
#include <asterisk/say.h>
#include <stdio.h>

int ast_say_digit_str(struct ast_channel *chan, char *fn2, char *ints, char *lang)
{
	/* XXX Merge with full version? XXX */
	char fn[256] = "";
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
			default:
				snprintf(fn, sizeof(fn), "digits/%c", fn2[num]);
			}
		res = ast_streamfile(chan, fn, lang);
		if (!res) 
			res = ast_waitstream(chan, ints);
		ast_stopstream(chan);
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
						res = ast_say_number_full(chan, num / 1000, ints, language, audiofd, ctrlfd);
						if (res)
							return res;
						num = num % 1000;
						snprintf(fn, sizeof(fn), "digits/thousand");
					} else {
						if (num < 1000000000) {
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
	localtime_r(&t,&tm);
	if (!&tm) {
		ast_log(LOG_WARNING, "Unable to derive local time\n");
		return -1;
	}
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

int ast_say_time(struct ast_channel *chan, time_t t, char *ints, char *lang)
{
	struct tm tm;
	int res = 0;
	int hour, pm=0;
	localtime_r(&t,&tm);
	if (!&tm) {
		ast_log(LOG_WARNING, "Unable to derive local time\n");
		return -1;
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
	if (!&tm) {
		ast_log(LOG_WARNING, "Unable to derive local time\n");
		return -1;
	}
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
	if (!&tm) {
		ast_log(LOG_WARNING, "Unable to derive local time\n");
		return -1;
	}
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

