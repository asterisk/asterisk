/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Media Format Bitfield Compatibility API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _IAX2_CODEC_PREF_H_
#define _IAX2_CODEC_PREF_H_

struct ast_format;
struct ast_codec;
struct ast_format_cap;

#define IAX2_CODEC_PREF_SIZE 64
struct iax2_codec_pref {
	/*! Array is ordered by preference.  Contains the iax2_supported_formats[] index + 1. */
	char order[IAX2_CODEC_PREF_SIZE];
	/*! Framing size of the codec */
	unsigned int framing[IAX2_CODEC_PREF_SIZE];
};

/*!
 * \brief Convert an iax2_codec_pref order value into a format bitfield
 *
 * \param order_value value being converted
 *
 * \return the bitfield value of the order_value format
 */
uint64_t iax2_codec_pref_order_value_to_format_bitfield(int order_value);

/*!
 * \brief Convert a format bitfield into an iax2_codec_pref order value
 *
 * \param bitfield value being converted
 *
 * \return the iax2_codec_pref order value of the most significant format
 *  in the bitfield.
 *
 * \note This is really meant to be used on single format bitfields.
 *  It will work with multiformat bitfields, but it can only return the
 *  index of the most significant one if that is the case.
 */
int iax2_codec_pref_format_bitfield_to_order_value(uint64_t bitfield);

/*!
 * \brief Codec located at a particular place in the preference index.
 * \param pref preference structure to get the codec out of
 * \param index to retrieve from
 * \param result ast_format structure to store the index value in
 * \return pointer to input ast_format on success, NULL on failure
*/
struct ast_format *iax2_codec_pref_index(struct iax2_codec_pref *pref, int index, struct ast_format **result);

/*!
 * \brief Convert a preference structure to a capabilities structure.
 *
 * \param pref Formats in preference order to build the capabilities.
 * \param cap Capabilities structure to place formats into
 *
 * \retval 0 on success.
 * \retval -1 on error.
 *
 * \note If failure occurs the capabilities structure may contain a partial set of formats
 */
int iax2_codec_pref_to_cap(struct iax2_codec_pref *pref, struct ast_format_cap *cap);

/*!
 * \brief Convert a bitfield to a format capabilities structure in the "best" order.
 *
 * \param bitfield The bitfield for the media formats
 * \param prefs Format preference order to use as a guide. (May be NULL)
 * \param cap Capabilities structure to place formats into
 *
 * \retval 0 on success.
 * \retval -1 on error.
 *
 * \note If failure occurs the capabilities structure may contain a partial set of formats
 */
int iax2_codec_pref_best_bitfield2cap(uint64_t bitfield, struct iax2_codec_pref *prefs, struct ast_format_cap *cap);

/*! \brief Removes format from the pref list that aren't in the bitfield */
void iax2_codec_pref_remove_missing(struct iax2_codec_pref *pref, uint64_t bitfield);

/*!
 * \brief Dump audio codec preference list into a string
 *
 * \param pref preference structure to dump string representation of order for
 * \param buf character buffer to put string into
 * \param size size of the character buffer
 *
 * \return -1 on error. Otherwise returns the remaining spaaaaaace in the buffer.
 *
 * \note Format is (codec1|codec2|codec3|...) -- if the list is too long for the
 *  size of the buffer, codecs will be written until they exceed the length
 *  remaining in which case the list will be closed with '...)' after the last
 *  writable codec.
 */
int iax2_codec_pref_string(struct iax2_codec_pref *pref, char *buf, size_t size);

/*! \brief Append a audio codec to a preference list, removing it first if it was already there
*/
void iax2_codec_pref_append(struct iax2_codec_pref *pref, struct ast_format *format, unsigned int framing);

/*! \brief Prepend an audio codec to a preference list, removing it first if it was already there
*/
void iax2_codec_pref_prepend(struct iax2_codec_pref *pref, struct ast_format *format, unsigned int framing,
	int only_if_existing);

/*! \brief Shift an audio codec preference list up or down 65 bytes so that it becomes an ASCII string
 * \note Due to a misunderstanding in how codec preferences are stored, this
 * list starts at 'B', not 'A'.  For backwards compatibility reasons, this
 * cannot change.
 * \param pref A codec preference list structure
 * \param buf A string denoting codec preference, appropriate for use in line transmission
 * \param size Size of \a buf
 * \param right Boolean:  if 0, convert from \a buf to \a pref; if 1, convert from \a pref to \a buf.
 */
void iax2_codec_pref_convert(struct iax2_codec_pref *pref, char *buf, size_t size, int right);

/*!
 * \brief Create codec preference list from the given bitfield formats.
 * \since 13.0.0
 *
 * \param pref Codec preference list to setup from the given bitfield.
 * \param bitfield Format bitfield to guide preference list creation.
 *
 * \return Updated bitfield with any bits not mapped to a format cleared.
 */
uint64_t iax2_codec_pref_from_bitfield(struct iax2_codec_pref *pref, uint64_t bitfield);

#endif /* _IAX2_CODEC_PREF_H_ */
