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

#ifndef _ASTERISK_SAY_H
#define _ASTERISK_SAY_H

#include <asterisk/channel.h>
#include <asterisk/file.h>

#include <time.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

//! says a number
/*! 
 * \param chan channel to say them number on
 * \param num number to say on the channel
 * \param ints which dtmf to interrupt on
 * \param lang language to speak the number
 * \param options set to 'f' for female, 'm' for masculine (used in portuguese)
 * Vocally says a number on a given channel
 * Returns 0 on success, DTMF digit on interrupt, -1 on failure
 */
int ast_say_number(struct ast_channel *chan, int num, char *ints, char *lang, char *options);

/* Same as above with audiofd for received audio and returns 1 on ctrlfd being readable */
int ast_say_number_full(struct ast_channel *chan, int num, char *ints, char *lang, char *options, int audiofd, int ctrlfd);

//! says digits
/*!
 * \param chan channel to act upon
 * \param num number to speak
 * \param ints which dtmf to interrupt on
 * \param lang language to speak
 * Vocally says digits of a given number
 * Returns 0 on success, dtmf if interrupted, -1 on failure
 */
int ast_say_digits(struct ast_channel *chan, int num, char *ints, char *lang);
int ast_say_digits_full(struct ast_channel *chan, int num, char *ints, char *lang, int audiofd, int ctrlfd);

//! says digits of a string
/*! 
 * \param chan channel to act upon
 * \param num string to speak
 * \param ints which dtmf to interrupt on
 * \param lang language to speak in
 * Vocally says the digits of a given string
 * Returns 0 on success, dtmf if interrupted, -1 on failure
 */
int ast_say_digit_str(struct ast_channel *chan, char *num, char *ints, char *lang);
int ast_say_digit_str_full(struct ast_channel *chan, char *num, char *ints, char *lang, int audiofd, int ctrlfd);
int ast_say_character_str(struct ast_channel *chan, char *num, char *ints, char *lang);
int ast_say_character_str_full(struct ast_channel *chan, char *num, char *ints, char *lang, int audiofd, int ctrlfd);
int ast_say_phonetic_str(struct ast_channel *chan, char *num, char *ints, char *lang);
int ast_say_phonetic_str_full(struct ast_channel *chan, char *num, char *ints, char *lang, int audiofd, int ctrlfd);

int ast_say_datetime(struct ast_channel *chan, time_t t, char *ints, char *lang);

int ast_say_time(struct ast_channel *chan, time_t t, char *ints, char *lang);

int ast_say_date(struct ast_channel *chan, time_t t, char *ints, char *lang);

int ast_say_datetime_from_now(struct ast_channel *chan, time_t t, char *ints, char *lang);

int ast_say_date_with_format(struct ast_channel *chan, time_t t, char *ints, char *lang, char *format, char *timezone);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
