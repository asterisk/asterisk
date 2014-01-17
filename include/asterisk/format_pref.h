/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Format Preference API
 */

#ifndef _AST_FORMATPREF_H_
#define _AST_FORMATPREF_H_

#include "asterisk/format.h"
#include "asterisk/format_cap.h"

#define AST_CODEC_PREF_SIZE 64
struct ast_codec_pref {
	/*! This array represents the each format in the pref list */
	struct ast_format formats[AST_CODEC_PREF_SIZE];
	/*! This array represents the format id's index in the global format list. */
	char order[AST_CODEC_PREF_SIZE];
	/*! This array represents the format's framing size if present. */
	int framing[AST_CODEC_PREF_SIZE];
};

/*! \page AudioCodecPref Audio Codec Preferences

	In order to negotiate audio codecs in the order they are configured
	in \<channel\>.conf for a device, we set up codec preference lists
	in addition to the codec capabilities setting. The capabilities
	setting is a bitmask of audio and video codecs with no internal
	order. This will reflect the offer given to the other side, where
	the prefered codecs will be added to the top of the list in the
	order indicated by the "allow" lines in the device configuration.

	Video codecs are not included in the preference lists since they
	can't be transcoded and we just have to pick whatever is supported
*/

/*!
 *\brief Initialize an audio codec preference to "no preference".
 * \arg \ref AudioCodecPref
*/
void ast_codec_pref_init(struct ast_codec_pref *pref);

/*!
 * \brief Codec located at a particular place in the preference index.
 * \param pref preference structure to get the codec out of
 * \param index to retrieve from
 * \param result ast_format structure to store the index value in
 * \return pointer to input ast_format on success, NULL on failure
*/
struct ast_format *ast_codec_pref_index(struct ast_codec_pref *pref, int index, struct ast_format *result);

/*! \brief Remove audio a codec from a preference list */
void ast_codec_pref_remove(struct ast_codec_pref *pref, struct ast_format *format);

/*! \brief Append all codecs to a preference list, without disturbing existing order */
void ast_codec_pref_append_all(struct ast_codec_pref *pref);

/*! \brief Append a audio codec to a preference list, removing it first if it was already there
*/
int ast_codec_pref_append(struct ast_codec_pref *pref, struct ast_format *format);

/*! \brief Prepend an audio codec to a preference list, removing it first if it was already there
*/
void ast_codec_pref_prepend(struct ast_codec_pref *pref, struct ast_format *format, int only_if_existing);

/*! \brief Select the best audio format according to preference list from supplied options.
 * Best audio format is returned in the result format.
 *
 * \note If "find_best" is non-zero then if nothing is found, the "Best" format of
 * the format list is selected and returned in the result structure, otherwise
 * NULL is returned
 *
 * \retval ptr to result struture.
 * \retval NULL, best codec was not found
 */
struct ast_format *ast_codec_choose(struct ast_codec_pref *pref, struct ast_format_cap *cap, int find_best, struct ast_format *result);

/*! \brief Set packet size for codec
*/
int ast_codec_pref_setsize(struct ast_codec_pref *pref, struct ast_format *format, int framems);

/*! \brief Get packet size for codec
*/
struct ast_format_list ast_codec_pref_getsize(struct ast_codec_pref *pref, struct ast_format *format);

/*! \brief Dump audio codec preference list into a string */
int ast_codec_pref_string(struct ast_codec_pref *pref, char *buf, size_t size);

/*! \brief Shift an audio codec preference list up or down 65 bytes so that it becomes an ASCII string
 * \note Due to a misunderstanding in how codec preferences are stored, this
 * list starts at 'B', not 'A'.  For backwards compatibility reasons, this
 * cannot change.
 * \param pref A codec preference list structure
 * \param buf A string denoting codec preference, appropriate for use in line transmission
 * \param size Size of \a buf
 * \param right Boolean:  if 0, convert from \a buf to \a pref; if 1, convert from \a pref to \a buf.
 */
void ast_codec_pref_convert(struct ast_codec_pref *pref, char *buf, size_t size, int right);

#endif /* _AST_FORMATPREF_H */
