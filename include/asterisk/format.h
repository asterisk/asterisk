/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief Format API
 *
 * \author David Vossel <dvossel@digium.com>
 */

#ifndef _AST_FORMAT_H_
#define _AST_FORMAT_H_

#define AST_FORMAT_ATTR_SIZE 128

#define AST_FORMAT_INC 100000

/*! This is the value that ends a var list of format attribute
 * key value pairs. */
#define AST_FORMAT_ATTR_END -1

/* \brief Format Categories*/
enum ast_format_type {
	AST_FORMAT_TYPE_AUDIO = 1 * AST_FORMAT_INC,
	AST_FORMAT_TYPE_VIDEO = 2 * AST_FORMAT_INC,
	AST_FORMAT_TYPE_IMAGE = 3 * AST_FORMAT_INC,
	AST_FORMAT_TYPE_TEXT  = 4 * AST_FORMAT_INC,
};

enum ast_format_id {
	/*! G.723.1 compression */
	AST_FORMAT_G723_1           = 1 + AST_FORMAT_TYPE_AUDIO,
	/*! GSM compression */
	AST_FORMAT_GSM              = 2 + AST_FORMAT_TYPE_AUDIO,
	/*! Raw mu-law data (G.711) */
	AST_FORMAT_ULAW             = 3 + AST_FORMAT_TYPE_AUDIO,
	/*! Raw A-law data (G.711) */
	AST_FORMAT_ALAW             = 4 + AST_FORMAT_TYPE_AUDIO,
	/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
	AST_FORMAT_G726_AAL2        = 5 + AST_FORMAT_TYPE_AUDIO,
	/*! ADPCM (IMA) */
	AST_FORMAT_ADPCM            = 6 + AST_FORMAT_TYPE_AUDIO,
	/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
	AST_FORMAT_SLINEAR          = 7 + AST_FORMAT_TYPE_AUDIO,
	/*! LPC10, 180 samples/frame */
	AST_FORMAT_LPC10            = 8 + AST_FORMAT_TYPE_AUDIO,
	/*! G.729A audio */
	AST_FORMAT_G729A            = 9 + AST_FORMAT_TYPE_AUDIO,
	/*! SpeeX Free Compression */
	AST_FORMAT_SPEEX            = 10 + AST_FORMAT_TYPE_AUDIO,
	/*! iLBC Free Compression */
	AST_FORMAT_ILBC             = 11 + AST_FORMAT_TYPE_AUDIO,
	/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
	AST_FORMAT_G726             = 12 + AST_FORMAT_TYPE_AUDIO,
	/*! G.722 */
	AST_FORMAT_G722             = 13 + AST_FORMAT_TYPE_AUDIO,
	/*! G.722.1 (also known as Siren7, 32kbps assumed) */
	AST_FORMAT_SIREN7           = 14 + AST_FORMAT_TYPE_AUDIO,
	/*! G.722.1 Annex C (also known as Siren14, 48kbps assumed) */
	AST_FORMAT_SIREN14          = 15 + AST_FORMAT_TYPE_AUDIO,
	/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
	AST_FORMAT_SLINEAR16        = 16 + AST_FORMAT_TYPE_AUDIO,
	/*! G.719 (64 kbps assumed) */
	AST_FORMAT_G719             = 17 + AST_FORMAT_TYPE_AUDIO,
	/*! SpeeX Wideband (16kHz) Free Compression */
	AST_FORMAT_SPEEX16          = 18 + AST_FORMAT_TYPE_AUDIO,
	/*! Raw mu-law data (G.711) */
	AST_FORMAT_TESTLAW          = 19 + AST_FORMAT_TYPE_AUDIO,

	/*! H.261 Video */
	AST_FORMAT_H261             = 1 + AST_FORMAT_TYPE_VIDEO,
	/*! H.263 Video */
	AST_FORMAT_H263             = 2 + AST_FORMAT_TYPE_VIDEO,
	/*! H.263+ Video */
	AST_FORMAT_H263_PLUS        = 3 + AST_FORMAT_TYPE_VIDEO,
	/*! H.264 Video */
	AST_FORMAT_H264             = 4 + AST_FORMAT_TYPE_VIDEO,
	/*! MPEG4 Video */
	AST_FORMAT_MP4_VIDEO        = 5 + AST_FORMAT_TYPE_VIDEO,

	/*! JPEG Images */
	AST_FORMAT_JPEG             = 1 + AST_FORMAT_TYPE_IMAGE,
	/*! PNG Images */
	AST_FORMAT_PNG              = 2 + AST_FORMAT_TYPE_IMAGE,

	/*! T.140 RED Text format RFC 4103 */
	AST_FORMAT_T140RED          = 1 + AST_FORMAT_TYPE_TEXT,
	/*! T.140 Text format - ITU T.140, RFC 4103 */
	AST_FORMAT_T140             = 2 + AST_FORMAT_TYPE_TEXT,
};

/*! Determine what type of media a ast_format_id is. */
#define AST_FORMAT_GET_TYPE(id) (((int) (id / AST_FORMAT_INC)) * AST_FORMAT_INC)

/*! \brief This structure contains the buffer used for format attributes */
struct ast_format_attr {
	/*! The buffer formats can use to represent attributes */
	uint8_t format_attr[AST_FORMAT_ATTR_SIZE];
	/*! If a format's payload needs to pass through that a new marker is required
	 * for RTP, this variable will be set. */
	uint8_t rtp_marker_bit;
};

/*! \brief Represents a media format within Asterisk. */
struct ast_format {
	/*! The unique id representing this format from all the other formats. */
	enum ast_format_id id;
	/*!  Attribute structure used to associate attributes with a format. */
	struct ast_format_attr fattr;
};

enum ast_format_cmp_res {
	/*! structure 1 is identical to structure 2. */
	AST_FORMAT_CMP_EQUAL = 0,
	/*! structure 1 contains elements not in structure 2. */
	AST_FORMAT_CMP_NOT_EQUAL,
	/*! structure 1 is a proper subset of the elements in structure 2.*/
	AST_FORMAT_CMP_SUBSET,
};

/*! \brief A format must register an attribute interface if it requires the use of the format attributes void pointer */
struct ast_format_attr_interface {
	/*! format type */
	enum ast_format_id id;

	/*! \brief Determine if format_attr 1 is a subset of format_attr 2.
	 *
	 * \retval ast_format_cmp_res representing the result of comparing fattr1 and fattr2.
	 */
	enum ast_format_cmp_res (* const format_attr_cmp)(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2);

	/*! \brief Get joint attributes of same format type if they exist.
	 *
	 * \retval 0 if joint attributes exist
	 * \retval -1 if no joint attributes are present
	 */
	int (* const format_attr_get_joint)(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2, struct ast_format_attr *result);

	/*! \brief Set format capabilities from a list of key value pairs ending with AST_FORMAT_ATTR_END.
	 * \note This function does not need to call va_end of the va_list. */
	void (* const format_attr_set)(struct ast_format_attr *format_attr, va_list ap);
};

/*!
 * \brief This function is used to set an ast_format object to represent a media format
 * with optional format attributes represented by format specific key value pairs.
 *
 * \param format to set
 * \param id, format id to set on format
 * \param set_attributes, are there attributes to set on this format. 0 == false, 1 == True.
 * \param var list of attribute key value pairs, must end with AST_FORMAT_ATTR_END;
 *
 * \details Example usage.
 * ast_format_set(format, AST_FORMAT_ULAW, 0); // no capability attributes are needed for ULAW
 *
 * ast_format_set(format, AST_FORMAT_SILK, 1, // SILK has capability attributes.
 *	  AST_FORMAT_SILK_ATTR_RATE, 24000,
 *	  AST_FORMAT_SILK_ATTR_RATE, 16000,
 *	  AST_FORMAT_SILK_ATTR_RATE, 12000,
 *	  AST_FORMAT_SILK_ATTR_RATE, 8000,
 *	  AST_FORMAT_ATTR_END);
 *
 * \note This function will initialize the ast_format structure.
 *
 * \return Pointer to ast_format object, same pointer that is passed in
 * by the first argument.
 */
struct ast_format *ast_format_set(struct ast_format *format, enum ast_format_id id, int set_attributes, ... );

/*!
 * \brief After ast_format_set has been used on a function, this function can be used to
 * set additional format attributes to the structure.
 *
 * \param format to set
 * \param var list of attribute key value pairs, must end with AST_FORMAT_ATTR_END;
 *
 * \details Example usage.
 * ast_format_set(format, AST_FORMAT_SILK, 0);
 * ast_format_append(format, // SILK has capability attributes.
 *	  AST_FORMAT_SILK_ATTR_RATE, 24000,
 *	  AST_FORMAT_SILK_ATTR_RATE, 16000,
 *	  AST_FORMAT_SILK_ATTR_RATE, 12000,
 *	  AST_FORMAT_SILK_ATTR_RATE, 8000,
 *	  AST_FORMAT_ATTR_END);
 *
 * \return Pointer to ast_format object, same pointer that is passed in
 * by the first argument.
 */
struct ast_format *ast_format_append(struct ast_format *format, ... );

/*!
 * \brief Clears the format stucture.
 */
void ast_format_clear(struct ast_format *format);

/*!
 * \brief This function is used to set an ast_format object to represent a media format
 * with optional capability attributes represented by format specific key value pairs.
 *
 * \details Example usage. Is this SILK format capable of 8khz
 * is_8khz = ast_format_isset(format, AST_FORMAT_SILK_CAP_RATE, 8000);
 *
 * \return 0, The format key value pairs are within the capabilities defined in this structure.
 * \return -1, The format key value pairs are _NOT_ within the capabilities of this structure.
 */
int ast_format_isset(struct ast_format *format, ... );

/*!
 * \brief Compare ast_formats structures
 *
 * \retval ast_format_cmp_res representing the result of comparing format1 and format2.
 */
enum ast_format_cmp_res ast_format_cmp(const struct ast_format *format1, const struct ast_format *format2);

/*!
 * \brief Find joint format attributes of two ast_format
 * structures containing the same uid and return the intersection in the
 * result structure.
 *
 * retval 0, joint attribute capabilities exist.
 * retval -1, no joint attribute capabilities exist.
 */
int ast_format_joint(const struct ast_format *format1, const struct ast_format *format2, struct ast_format *result);

/*!
 * \brief copy format src into format dst.
 */
void ast_format_copy(struct ast_format *dst, const struct ast_format *src);

/*!
 * \brief Set the rtp mark value on the format to indicate to the interface
 * writing this format's payload that a new RTP marker is necessary.
 */
void ast_format_set_video_mark(struct ast_format *format);

/*!
 * \brief Determine of the marker bit is set or not on this format.
 *
 * \retval 1, true
 * \retval 0, false
 */
int ast_format_get_video_mark(const struct ast_format *format);

/*!
 * \brief ast_format to old bitfield format represenatation
 *
 * \note This is only to be used for IAX2 compatibility 
 *
 * \retval iax2 representation of ast_format
 * \retval 0, if no representation existis for iax2
 */
uint64_t ast_format_to_old_bitfield(const struct ast_format *format);

/*!
 * \brief ast_format_id to old bitfield format represenatation
 *
 */
uint64_t ast_format_id_to_old_bitfield(enum ast_format_id id);

/*!
 * \brief convert old bitfield format to ast_format represenatation
 * \note This is only to be used for IAX2 compatibility 
 *
 * \retval on success, pointer to the dst format in the input parameters
 * \retval on failure, NULL
 */
struct ast_format *ast_format_from_old_bitfield(struct ast_format *dst, uint64_t src);

/*!
 * \brief convert old bitfield format to ast_format_id value
 */
enum ast_format_id ast_format_id_from_old_bitfield(uint64_t src);

/*!
 * \brief register ast_format_attr_interface with core.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_format_attr_reg_interface(const struct ast_format_attr_interface *interface);

/*!
 * \brief unregister format_attr interface with core.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_format_attr_unreg_interface(const struct ast_format_attr_interface *interface);

/*!
 * \brief Init the ast_format attribute interface register container.
 */
int ast_format_attr_init(void);

#endif /* _AST_FORMAT_H */
