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
 * \brief Media Format API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#include "asterisk/codec.h"

#ifndef _AST_FORMAT_H_
#define _AST_FORMAT_H_

struct ast_format;

/*! \brief Format comparison results */
enum ast_format_cmp_res {
	/*! Both formats are equivalent to each other */
	AST_FORMAT_CMP_EQUAL = 0,
	/*! Both formats are completely different and not the same in any way */
	AST_FORMAT_CMP_NOT_EQUAL,
	/*! Both formats are similar but not equivalent */
	AST_FORMAT_CMP_SUBSET,
};

/*! \brief Optional format interface to extend format operations */
struct ast_format_interface {
	/*!
	 * \brief Callback for when the format is destroyed, used to release attribute resources
	 *
	 * \param format The format structure to destroy
	 */
	void (*const format_destroy)(struct ast_format *format);

	/*!
	 * \brief Callback for when the format is cloned, used to clone attributes
	 *
	 * \param src Source format of attributes
	 * \param dst Destination format for attributes
	 *
	 * \retval 0 success
	 * \retval -1 failure
	 */
	int (*const format_clone)(const struct ast_format *src, struct ast_format *dst);

	/*!
	 * \brief Determine if format 1 is a subset of format 2.
	 *
	 * \param format1 First format to compare
	 * \param format2 Second format which the first is compared against
	 *
	 * \retval ast_format_cmp_res representing the result of comparing format1 and format2.
	 */
	enum ast_format_cmp_res (* const format_cmp)(const struct ast_format *format1,
		const struct ast_format *format2);

	/*!
	 * \brief Get a format with the joint compatible attributes of both provided formats.
	 *
	 * \param format1 The first format
	 * \param format2 The second format
	 *
	 * \retval non-NULL if joint format
	 * \retval NULL if no joint format
	 *
	 * \note The returned format has its reference count incremented and must be released using
	 * ao2_ref or ao2_cleanup.
	 */
	struct ast_format *(* const format_get_joint)(const struct ast_format *format1,
		const struct ast_format *format2);

	/*!
	 * \brief Set an attribute on a format
	 *
	 * \param name The name of the attribute
	 * \param value The value of the attribute
	 *
	 * \retval non-NULL success
	 * \retval NULL failure
	 */
	struct ast_format *(* const format_attribute_set)(const struct ast_format *format,
		const char *name, const char *value);

	/*!
	 * \brief Parse SDP attribute information, interpret it, and store it in the format structure.
	 *
	 * \param format Format to set attributes on
	 * \param attributes A string containing only the attributes from the fmtp line
	 *
	 * \retval non-NULL Success, values were valid
	 * \retval NULL Failure, some values were not acceptable
	 */
	struct ast_format *(* const format_parse_sdp_fmtp)(const struct ast_format *format, const char *attributes);

	/*!
	 * \brief Generate SDP attribute information from an ast_format structure.
	 *
	 * \param format The format containing attributes
	 * \param payload The payload number to place into the fmtp line
	 * \param str The generated fmtp line
	 *
	 * \note This callback should generate a full fmtp line using the provided payload number.
	 */
	void (* const format_generate_sdp_fmtp)(const struct ast_format *format, unsigned int payload,
		struct ast_str **str);

	/*!
	 * \since 13.6.0
	 * \brief Retrieve a particular format attribute setting
	 *
	 * \param format The format containing attributes
	 * \param name The name of the attribute to retrieve
	 *
	 * \retval NULL if the parameter is not set on the format
	 * \retval non-NULL the format attribute value
	 */
	const void *(* const format_attribute_get)(const struct ast_format *format, const char *name);
};

/*!
 * \brief Initialize media format support
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_format_init(void);

/*!
 * \brief Create a new media format
 *
 * \param codec The codec to use
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note The format is returned with reference count incremented. It must be released using
 * ao2_ref or ao2_cleanup.
 */
struct ast_format *ast_format_create(struct ast_codec *codec);

/*!
 * \brief Create a new media format with a specific name
 *
 * \param format_name The name to use for the format
 * \param codec The codec to use
 *
 * \note This creation function should be used when the name of the \c codec
 * cannot be explicitly used for the name of the format. This is the case for
 * codecs with multiple sample rates
 *
 * \note The format is returned with reference count incremented. It must be released using
 * ao2_ref or ao2_cleanup.
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
struct ast_format *ast_format_create_named(const char *format_name, struct ast_codec *codec);

/*!
 * \brief Clone an existing media format so it can be modified
 *
 * \param format The existing media format
 *
 * \note The returned format is a new ao2 object. It must be released using ao2_cleanup.
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
struct ast_format *ast_format_clone(const struct ast_format *format);

/*!
 * \brief Compare two formats
 *
 * \retval ast_format_cmp_res representing the result of comparing format1 and format2.
 */
enum ast_format_cmp_res ast_format_cmp(const struct ast_format *format1, const struct ast_format *format2);

/*!
 * \brief Get a common joint capability between two formats
 *
 * \retval non-NULL if joint capability exists
 * \retval NULL if no joint capability exists
 *
 * \note The returned format must be treated as immutable.
 */
struct ast_format *ast_format_joint(const struct ast_format *format1, const struct ast_format *format2);

/*!
 * \brief Set an attribute on a format to a specific value
 *
 * \param format The format to set the attribute on
 * \param name Attribute name
 * \param value Attribute value
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
struct ast_format *ast_format_attribute_set(const struct ast_format *format, const char *name,
	const char *value);

/*!
 * \since 13.6.0
 *
 * \param format The format to retrieve the attribute from
 * \param name Attribute name
 *
 * \retval non-NULL the attribute value
 * \retval NULL the attribute does not exist or is unset
 */
const void *ast_format_attribute_get(const struct ast_format *format, const char *name);

/*!
 * \brief This function is used to have a media format aware module parse and interpret
 * SDP attribute information. Once interpreted this information is stored on the format
 * itself using Asterisk format attributes.
 *
 * \param format to set
 * \param attributes string containing the fmtp line from the SDP
 *
 * \retval non-NULL success, attribute values were valid
 * \retval NULL failure, values were not acceptable
 */
struct ast_format *ast_format_parse_sdp_fmtp(const struct ast_format *format, const char *attributes);

/*!
 * \brief This function is used to produce an fmtp SDP line for an Asterisk format. The
 * attributes present on the Asterisk format are translated into the SDP equivalent.
 *
 * \param format to generate an fmtp line for
 * \param payload numerical payload for the fmtp line
 * \param str structure that the fmtp line will be appended to
 */
void ast_format_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str);

/*!
 * \brief Register a format interface for use with the provided codec
 *
 * \param codec The name of codec the interface is applicable to
 * \param interface A pointer to the interface implementation
 * \param mod The module this format interface is provided by
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __ast_format_interface_register(const char *codec, const struct ast_format_interface *interface, struct ast_module *mod);

/*!
 * \brief Register a format interface for use with the provided codec
 *
 * \param codec The name of codec the interface is applicable to
 * \param interface A pointer to the interface implementation
 *
 * \retval 0 success
 * \retval -1 failure
 */
#define ast_format_interface_register(codec, interface) __ast_format_interface_register(codec, interface, AST_MODULE_SELF)

/*!
 * \brief Get the attribute data on a format
 *
 * \param format The media format
 *
 * \return Currently set attribute data
 */
void *ast_format_get_attribute_data(const struct ast_format *format);

/*!
 * \brief Set the attribute data on a format
 *
 * \param format The media format
 * \param attribute_data The attribute data
 */
void ast_format_set_attribute_data(struct ast_format *format, void *attribute_data);

/*!
 * \brief Get the name associated with a format
 *
 * \param format The media format
 *
 * \return The name of the format
 */
const char *ast_format_get_name(const struct ast_format *format);

/*!
 * \brief Get the channel count on a format
 *
 * \param The media format
 *
 * \return Currently set channel count
 */
unsigned int ast_format_get_channel_count(const struct ast_format *format);

/*!
 * \brief Set the channel count on a format
 *
 * \param format The media format
 * \param channel_count The number of audio channels used
 *
 */
void ast_format_set_channel_count(struct ast_format *format, unsigned int channel_count);

/*!
 * \brief Get the codec associated with a format
 *
 * \param format The media format
 *
 * \return The codec
 *
 * \note The reference count of the returned codec is increased by 1 and must be decremented
 */
struct ast_codec *ast_format_get_codec(const struct ast_format *format);

/*!
 * \brief Get the codec identifier associated with a format
 *
 * \param format The media format
 *
 * \return codec identifier
 */
unsigned int ast_format_get_codec_id(const struct ast_format *format);

/*!
 * \brief Get the codec name associated with a format
 *
 * \param format The media format
 *
 * \return The codec name
 */
const char *ast_format_get_codec_name(const struct ast_format *format);

/*!
 * \brief Get whether or not the format can be smoothed
 *
 * \param format The media format
 *
 * \retval 0 the format cannot be smoothed
 * \retval 1 the format can be smoothed
 */
int ast_format_can_be_smoothed(const struct ast_format *format);

/*!
 * \since 13.17.0
 *
 * \brief Get smoother flags for this format
 *
 * \param format The media format
 *
 * \return smoother flags for the provided format
 */
int ast_format_get_smoother_flags(const struct ast_format *format);

/*!
 * \brief Get the media type of a format
 *
 * \param format The media format
 *
 * \return the media type
 */
enum ast_media_type ast_format_get_type(const struct ast_format *format);

/*!
 * \brief Get the default framing size (in milliseconds) for a format
 *
 * \param format The media format
 *
 * \return default framing size in milliseconds
 */
unsigned int ast_format_get_default_ms(const struct ast_format *format);

/*!
 * \brief Get the minimum amount of media carried in this format
 *
 * \param format The media format
 *
 * \return minimum framing size in milliseconds
 */
unsigned int ast_format_get_minimum_ms(const struct ast_format *format);

/*!
 * \brief Get the maximum amount of media carried in this format
 *
 * \param format The media format
 *
 * \return maximum framing size in milliseconds
 */
unsigned int ast_format_get_maximum_ms(const struct ast_format *format);

/*!
 * \brief Get the minimum number of bytes expected in a frame for this format
 *
 * \param format The media format
 *
 * \return minimum expected bytes in a frame for this format
 */
unsigned int ast_format_get_minimum_bytes(const struct ast_format *format);

/*!
 * \brief Get the sample rate of a media format
 *
 * \param format The media format
 *
 * \return sample rate
 */
unsigned int ast_format_get_sample_rate(const struct ast_format *format);

/*!
 * \brief Get the length (in milliseconds) for the format with a given number of samples
 *
 * \param format The media format
 * \param samples The number of samples
 *
 * \return length of media (in milliseconds)
 */
unsigned int ast_format_determine_length(const struct ast_format *format, unsigned int samples);

/*!
 * \since 12
 * \brief Get the message type used for signaling a format registration
 *
 * \retval Stasis message type for format registration
 * \retval NULL on error
 */
struct stasis_message_type *ast_format_register_type(void);

/*!
 * \since 12
 * \brief Get the message type used for signaling a format unregistration
 *
 * \retval Stasis message type for format unregistration
 * \retval NULL on error
 */
struct stasis_message_type *ast_format_unregister_type(void);

#endif /* _AST_FORMAT_H */
