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
 * \brief Codec API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _AST_CODEC_H_
#define _AST_CODEC_H_

/*! \brief Types of media */
enum ast_media_type {
	AST_MEDIA_TYPE_UNKNOWN = 0,
	AST_MEDIA_TYPE_AUDIO,
	AST_MEDIA_TYPE_VIDEO,
	AST_MEDIA_TYPE_IMAGE,
	AST_MEDIA_TYPE_TEXT,
	AST_MEDIA_TYPE_END,
};

struct ast_module;

/*! \brief Represents a media codec within Asterisk. */
struct ast_codec {
	/*! \brief Internal unique identifier for this codec, set at registration time (starts at 1) */
	unsigned int id;
	/*! \brief Name for this codec */
	const char *name;
	/*! \brief Brief description */
	const char *description;
	/*! \brief Type of media this codec contains */
	enum ast_media_type type;
	/*! \brief Sample rate (number of samples carried in a second) */
	unsigned int sample_rate;
	/*! \brief Minimum length of media that can be carried (in milliseconds) in a frame */
	unsigned int minimum_ms;
	/*! \brief Maximum length of media that can be carried (in milliseconds) in a frame */
	unsigned int maximum_ms;
	/*! \brief Default length of media carried (in milliseconds) in a frame */
	unsigned int default_ms;
	/*! \brief Length in bytes of the data payload of a minimum_ms frame */
	unsigned int minimum_bytes;
	/*!
	 * \brief Retrieve the number of samples in a frame
	 *
	 * \param frame The frame to examine
	 *
	 * \return the number of samples
	 */
	int (*samples_count)(struct ast_frame *frame);
	/*!
	 * \brief Retrieve the length of media from number of samples
	 *
	 * \param samples The number of samples
	 *
	 * \return The length of media in milliseconds
	 */
	int (*get_length)(unsigned int samples);
	/*! \brief Whether the media can be smoothed or not */
	unsigned int smooth;
	/*! \brief Flags to be passed to the smoother */
	unsigned int smoother_flags;
	/*! \brief The module that registered this codec */
	struct ast_module *mod;
};

/*!
 * \brief Initialize codec support within the core.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_codec_init(void);

/*!
 * \brief Initialize built-in codecs within the core.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_codec_builtin_init(void);

/*!
 * \brief This function is used to register a codec with the Asterisk core. Registering
 * allows it to be passed through in frames and configured in channel drivers.
 *
 * \param codec to register
 * \param mod the module this codec is provided by
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __ast_codec_register(struct ast_codec *codec, struct ast_module *mod);

/*!
 * \brief This function is used to register a codec with the Asterisk core. Registering
 * allows it to be passed through in frames and configured in channel drivers.
 *
 * \param codec to register
 *
 * \retval 0 success
 * \retval -1 failure
 */
#define ast_codec_register(codec) __ast_codec_register(codec, AST_MODULE_SELF)

/*!
 * \brief Retrieve a codec given a name, type, and sample rate
 *
 * \param name The name of the codec
 * \param type The type of the codec
 * \param sample_rate Optional sample rate, may not be applicable for some types
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note The returned codec is reference counted and ao2_ref or ao2_cleanup
 * must be used to release the reference.
 */
struct ast_codec *ast_codec_get(const char *name, enum ast_media_type type, unsigned int sample_rate);

/*!
 * \brief Retrieve a codec given the unique identifier
 *
 * \param id The unique identifier
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note Identifiers start at 1 so if iterating don't start at 0.
 *
 * \note The returned codec is reference counted and ao2_ref or ao2_cleanup
 * must be used to release the reference.
 */
struct ast_codec *ast_codec_get_by_id(int id);

/*!
 * \brief Retrieve the current maximum identifier for codec iteration
 *
 * \return Maximum codec identifier
 */
int ast_codec_get_max(void);

/*!
 * \brief Conversion function to take a media type and turn it into a string
 *
 * \param type The media type
 *
 * \retval string representation of the media type
 */
const char *ast_codec_media_type2str(enum ast_media_type type);

/*!
 * \brief Conversion function to take a media string and convert it to a media type
 *
 * \param media_type_str The media type string
 *
 * \retval The ast_media_type that corresponds to the string
 *
 * \since 15.0.0
 */
enum ast_media_type ast_media_type_from_str(const char *media_type_str);

/*!
 * \brief Get the number of samples contained within a frame
 *
 * \param frame The frame itself
 *
 * \retval number of samples in the frame
 */
unsigned int ast_codec_samples_count(struct ast_frame *frame);

/*!
 * \brief Get the length of media (in milliseconds) given a number of samples
 *
 * \param codec The codec itself
 * \param samples The number of samples
 *
 * \retval length of media (in milliseconds)
 */
unsigned int ast_codec_determine_length(const struct ast_codec *codec, unsigned int samples);

#endif /* _AST_CODEC_H */
