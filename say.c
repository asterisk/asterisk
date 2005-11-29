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

#include <asterisk/file.h>
#include <asterisk/channel.h>
#include <asterisk/logger.h>
#include <asterisk/say.h>
#include <stdio.h>

int ast_say_digit_str(struct ast_channel *chan, char *fn2, char *ints, char *lang)
{
	char fn[256] = "";
	int num = 0;
	int res = 0;
	while(fn2[num] && !res) {
		snprintf(fn, sizeof(fn), "digits/%c", fn2[num]);
		res = ast_streamfile(chan, fn, lang);
		if (!res) 
			res = ast_waitstream(chan, ints);
		ast_stopstream(chan);
		num++;
	}
	return res;
}

int ast_say_digits(struct ast_channel *chan, int num, char *ints, char *lang)
{
	char fn2[256];
	snprintf(fn2, sizeof(fn2), "%d", num);
	return ast_say_digit_str(chan, fn2, ints, lang);
}
int ast_say_number(struct ast_channel *chan, int num, char *ints, char *language)
{
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
		while(num && !res) {
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
