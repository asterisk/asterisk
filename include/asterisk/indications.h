/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2002, Pauline Middelink
 * Copyright (C) 2009, Digium, Inc.
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

/*!
 * \file
 * \brief Tone Indication Support
 *
 * \author Pauline Middelink <middelink@polyware.nl>
 * \author Russell Bryant <russell@digium.com>
 */

#ifndef _ASTERISK_INDICATIONS_H
#define _ASTERISK_INDICATIONS_H

#include "asterisk/astobj2.h"
#include "asterisk/utils.h"
#include "asterisk/data.h"

/*!
 * \brief Description of a tone
 */
struct ast_tone_zone_sound {
	/*! \brief Name of the tone.  For example, "busy". */
	const char *name;
	/*!
	 * \brief Description of a tone
	 *
	 * The format is a comma separated list of tone parts in the following format:
	 *
	 * Format: [!][M]freq[<+|*>freq2][/duration]
	 *  - '!' - means that the element is NOT repeated
	 *  - 'M' - interpret the frequencies as midi notes instead of frequencies
	 *  - freq - The first frequency
	 *  - freq2 - The second frequency (optional)
	 *  - '*' - modulate freq by freq2 at a fixed depth of 90%
	 *  - '+' - combine the frequencies
	 *  - duration - the length of the tone part (optional, forever if not specified)
	 */
	const char *data;
	/*! \brief Linked list fields for including in the list on an ast_tone_zone */
	AST_LIST_ENTRY(ast_tone_zone_sound) entry;
	/*! \brief Flags only used internally */
	union {
		uint32_t __padding;
		struct {
			unsigned int killme:1;
		};
	};
};

#define MAX_TONEZONE_COUNTRY 16

/*!
 * \brief A set of tones for a given locale
 *
 * \note If a reference to this tone zone is held, then the country
 *       is guaranteed not to change.  It is safe to read it without
 *       locking the tone zone.  This is not the case for any other
 *       field.
 */
struct ast_tone_zone {
	/*! \brief Country code that this set of tones is for */
	char country[MAX_TONEZONE_COUNTRY];
	/*! 
	 * \brief Text description of the given country.
	 *
	 * This is for nothing more than friendly display to a human.
	 */
	char description[40];
	/*! \brief Number of ring cadence elements in the ringcadence array */
	unsigned int  nrringcadence;
	/*! 
	 * \brief Array of ring cadence parts
	 *
	 * Each element is an amount of time in milliseconds.  The first element
	 * is for time on, and from there it alternates between on and off.
	 */
	int *ringcadence;
	/*! \brief A list of tones for this locale */
	AST_LIST_HEAD_NOLOCK(, ast_tone_zone_sound) tones;
	/*! \brief Flags only used internally */
	union {
		uint32_t __padding;
		struct {
			unsigned int killme:1;
		};
	};
};

/*!
 * \brief A description of a part of a tone
 *
 * The elements in this structure map to the format described for the data
 * part of the ast_tone_zone_sound struct.
 */
struct ast_tone_zone_part {
	unsigned int freq1;
	unsigned int freq2;
	unsigned int time;
	unsigned int modulate:1;
	unsigned int midinote:1;
};

/*!
 * \brief Parse a tone part
 *
 * \param s The part of a tone to parse.  This should be in the form described for
 *        the data part of ast_tone_zone_sound.  '!' should be removed if present.
 * \param tone_data An output parameter that contains the result of the parsing.
 *
 * \retval 0 success
 * \retval -1 failure, and the contents of tone_data are undefined
 */
int ast_tone_zone_part_parse(const char *s, struct ast_tone_zone_part *tone_data);

/*!
 * \brief locate ast_tone_zone
 *
 * \param country country to find.  If NULL is provided, get the default.
 *
 * \return a reference to the specified country if found or NULL if not found
 */
struct ast_tone_zone *ast_get_indication_zone(const char *country);

/*!
 * \brief Locate a tone zone sound
 *
 * \param zone Zone to look in for a sound, if NULL, the default will be used
 * \param indication Sound to look for, such as "busy"
 *
 * \return a reference to the specified sound if it exists, NULL if not
 */
struct ast_tone_zone_sound *ast_get_indication_tone(const struct ast_tone_zone *zone, const char *indication);

/*!
 * \brief Start playing a list of tones on a channel
 *
 * \param chan the channel to play tones on
 * \param vol volume
 * \param tonelist the list of tones to play, comma separated
 * \param interruptible whether or not this tone can be interrupted
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_playtones_start(struct ast_channel *chan, int vol, const char *tonelist, int interruptible);

/*!
 * \brief Stop playing tones on a channel
 *
 * \param chan the channel to stop tones on
 */
void ast_playtones_stop(struct ast_channel *chan);

/*!
 * \brief Get the number of registered tone zones
 *
 * \return the total number of registered tone zones
 */
int ast_tone_zone_count(void);

/*!
 * \brief Get an iterator for the available tone zones
 *
 * Use ao2_iterator_next() to iterate the tone zones.
 *
 * \return an initialized iterator
 */
struct ao2_iterator ast_tone_zone_iterator_init(void);

/*!
 * \brief Lock an ast_tone_zone
 */
#define ast_tone_zone_lock(tz) ao2_lock(tz)

/*!
 * \brief Unlock an ast_tone_zone
 */
#define ast_tone_zone_unlock(tz) ao2_unlock(tz)

/*!
 * \brief Trylock an ast_tone_zone
 */
#define ast_tone_zone_trylock(tz) ao2_trylock(tz)

/*!
 * \brief Release a reference to an ast_tone_zone
 *
 * \return NULL
 */
static inline struct ast_tone_zone *ast_tone_zone_unref(struct ast_tone_zone *tz)
{
	ao2_ref(tz, -1);
	return NULL;
}

/*!
 * \brief Increase the reference count on an ast_tone_zone
 *
 * \return The tone zone provided as an argument
 */
static inline struct ast_tone_zone *ast_tone_zone_ref(struct ast_tone_zone *tz)
{
	ao2_ref(tz, +1);
	return tz;
}

/*!
 * \brief Release a reference to an ast_tone_zone_sound
 *
 * \return NULL
 */
static inline struct ast_tone_zone_sound *ast_tone_zone_sound_unref(struct ast_tone_zone_sound *ts)
{
	ao2_ref(ts, -1);
	return NULL;
}

/*!
 * \brief Increase the reference count on an ast_tone_zone_sound
 *
 * \return The tone zone sound provided as an argument
 */
static inline struct ast_tone_zone_sound *ast_tone_zone_sound_ref(struct ast_tone_zone_sound *ts)
{
	ao2_ref(ts, +1);
	return ts;
}

/*!
 * \brief Add a tone_zone structure to the data tree specified.
 *
 * \retval <0 on error.
 * \retval 0 on success.
 */
int ast_tone_zone_data_add_structure(struct ast_data *tree, struct ast_tone_zone *zone);

#endif /* _ASTERISK_INDICATIONS_H */
