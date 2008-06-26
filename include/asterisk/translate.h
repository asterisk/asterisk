/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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

/*! \file
 * \brief Support for translation of data formats.
 * \ref translate.c
 */

#ifndef _ASTERISK_TRANSLATE_H
#define _ASTERISK_TRANSLATE_H

#define MAX_AUDIO_FORMAT 15 /* Do not include video here */
#define MAX_FORMAT 32	/* Do include video here */

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#if 1	/* need lots of stuff... */
#include "asterisk/frame.h"
#include "asterisk/plc.h"
#include "asterisk/linkedlists.h"
// XXX #include "asterisk/module.h"
#endif

struct ast_trans_pvt;	/* declared below */

/*! \brief
 * Descriptor of a translator. 
 *
 * Name, callbacks, and various options
 * related to run-time operation (size of buffers, auxiliary
 * descriptors, etc).
 *
 * A codec registers itself by filling the relevant fields
 * of a structure and passing it as an argument to
 * ast_register_translator(). The structure should not be
 * modified after a successful registration, and its address
 * must be used as an argument to ast_unregister_translator().
 *
 * As a minimum, a translator should supply name, srcfmt and dstfmt,
 * the required buf_size (in bytes) and buffer_samples (in samples),
 * and a few callbacks (framein, frameout, sample).
 * The outbuf is automatically prepended by AST_FRIENDLY_OFFSET
 * spare bytes so generic routines can place data in there.
 *
 * Note, the translator is not supposed to do any memory allocation
 * or deallocation, nor any locking, because all of this is done in
 * the generic code.
 *
 * Translators using generic plc (packet loss concealment) should
 * supply a non-zero plc_samples indicating the size (in samples)
 * of artificially generated frames and incoming data.
 * Generic plc is only available for dstfmt = SLINEAR
 */
struct ast_translator {
	const char name[80];		/*!< Name of translator */
	int srcfmt;			/*!< Source format (note: bit position,
					  converted to index during registration) */
	int dstfmt;			/*!< Destination format (note: bit position,
					  converted to index during registration) */

	int (*newpvt)(struct ast_trans_pvt *); /*!< initialize private data 
					associated with the translator */

	int (*framein)(struct ast_trans_pvt *pvt, struct ast_frame *in);
					/*!< Input frame callback. Store 
					     (and possibly convert) input frame. */

	struct ast_frame * (*frameout)(struct ast_trans_pvt *pvt);
					/*!< Output frame callback. Generate a frame 
					    with outbuf content. */

	void (*destroy)(struct ast_trans_pvt *pvt);
					/*!< cleanup private data, if needed 
						(often unnecessary). */

	struct ast_frame * (*sample)(void);	/*!< Generate an example frame */

	/*! \brief size of outbuf, in samples. Leave it 0 if you want the framein
	 * callback deal with the frame. Set it appropriately if you
	 * want the code to checks if the incoming frame fits the
	 * outbuf (this is e.g. required for plc).
	 */
	int buffer_samples;	/*< size of outbuf, in samples */

	/*! \brief size of outbuf, in bytes. Mandatory. The wrapper code will also
	 * allocate an AST_FRIENDLY_OFFSET space before.
	 */
	int buf_size;

	int desc_size;			/*!< size of private descriptor in pvt->pvt, if any */
	int plc_samples;		/*!< set to the plc block size if used, 0 otherwise */
	int useplc;			/*!< current status of plc, changed at runtime */
	int native_plc;			/*!< true if the translator can do native plc */

	struct ast_module *module;	/* opaque reference to the parent module */

	int cost;			/*!< Cost in milliseconds for encoding/decoding 1 second of sound */
	int active;			/*!< Whether this translator should be used or not */
	AST_LIST_ENTRY(ast_translator) list;	/*!< link field */
};

/*! \brief
 * Default structure for translators, with the basic fields and buffers,
 * all allocated as part of the same chunk of memory. The buffer is
 * preceded by \ref AST_FRIENDLY_OFFSET bytes in front of the user portion.
 * 'buf' points right after this space.
 *
 * *_framein() routines operate in two ways:
 * 1. some convert on the fly and place the data directly in outbuf;
 *    in this case 'samples' and 'datalen' contain the number of samples
 *    and number of bytes available in the buffer.
 *    In this case we can use a generic *_frameout() routine that simply
 *    takes whatever is there and places it into the output frame.
 * 2. others simply store the (unconverted) samples into a working
 *    buffer, and leave the conversion task to *_frameout().
 *    In this case, the intermediate buffer must be in the private
 *    descriptor, 'datalen' is left to 0, while 'samples' is still
 *    updated with the number of samples received.
 */
struct ast_trans_pvt {
	struct ast_translator *t;
	struct ast_frame f;         /*!< used in frameout */
	int samples;                /*!< samples available in outbuf */
	/*! \brief actual space used in outbuf */
	int datalen;
	void *pvt;                  /*!< more private data, if any */
	union {
		char *c;                /*!< the useful portion of the buffer */
		unsigned char *uc;      /*!< the useful portion of the buffer */
		int16_t *i16;
		uint8_t *ui8;
	} outbuf; 
	plc_state_t *plc;           /*!< optional plc pointer */
	struct ast_trans_pvt *next; /*!< next in translator chain */
	struct timeval nextin;
	struct timeval nextout;
	unsigned int destroy:1;
};

/*! \brief generic frameout function */
struct ast_frame *ast_trans_frameout(struct ast_trans_pvt *pvt,
        int datalen, int samples);

struct ast_trans_pvt;

/*!
 * \brief Register a translator
 * This registers a codec translator with asterisk
 * \param t populated ast_translator structure
 * \param module handle to the module that owns this translator
 * \return 0 on success, -1 on failure
 */
int __ast_register_translator(struct ast_translator *t, struct ast_module *module);

/*! \brief See \ref __ast_register_translator() */
#define ast_register_translator(t) __ast_register_translator(t, ast_module_info->self)

/*!
 * \brief Unregister a translator
 * Unregisters the given tranlator
 * \param t translator to unregister
 * \return 0 on success, -1 on failure
 */
int ast_unregister_translator(struct ast_translator *t);

/*!
 * \brief Activate a previously deactivated translator
 * \param t translator to activate
 * \return nothing
 *
 * Enables the specified translator for use.
 */
void ast_translator_activate(struct ast_translator *t);

/*!
 * \brief Deactivate a translator
 * \param t translator to deactivate
 * \return nothing
 *
 * Disables the specified translator from being used.
 */
void ast_translator_deactivate(struct ast_translator *t);

/*!
 * \brief Chooses the best translation path
 *
 * Given a list of sources, and a designed destination format, which should
 * I choose? 
 * \return Returns 0 on success, -1 if no path could be found.  
 * \note Modifies dests and srcs in place 
 */
int ast_translator_best_choice(int *dsts, int *srcs);

/*! 
 * \brief Builds a translator path
 * Build a path (possibly NULL) from source to dest 
 * \param dest destination format
 * \param source source format
 * \return ast_trans_pvt on success, NULL on failure
 * */
struct ast_trans_pvt *ast_translator_build_path(int dest, int source);

/*!
 * \brief Frees a translator path
 * Frees the given translator path structure
 * \param tr translator path to get rid of
 */
void ast_translator_free_path(struct ast_trans_pvt *tr);

/*!
 * \brief translates one or more frames
 * Apply an input frame into the translator and receive zero or one output frames.  Consume
 * determines whether the original frame should be freed
 * \param tr translator structure to use for translation
 * \param f frame to translate
 * \param consume Whether or not to free the original frame
 * \return an ast_frame of the new translation format on success, NULL on failure
 */
struct ast_frame *ast_translate(struct ast_trans_pvt *tr, struct ast_frame *f, int consume);

/*!
 * \brief Returns the number of steps required to convert from 'src' to 'dest'.
 * \param dest destination format
 * \param src source format
 * \return the number of translation steps required, or -1 if no path is available
 */
unsigned int ast_translate_path_steps(unsigned int dest, unsigned int src);

/*!
 * \brief Mask off unavailable formats from a format bitmask
 * \param dest possible destination formats
 * \param src source formats
 * \return the destination formats that are available in the source or translatable
 *
 * The result will include all formats from 'dest' that are either present
 * in 'src' or translatable from a format present in 'src'.
 *
 * \note Only a single audio format and a single video format can be
 * present in 'src', or the function will produce unexpected results.
 */
unsigned int ast_translate_available_formats(unsigned int dest, unsigned int src);

/*!
 * \brief Hint that a frame from a translator has been freed
 *
 * This is sort of a hack.  This function gets called when ast_frame_free() gets
 * called on a frame that has the AST_FRFLAG_FROM_TRANSLATOR flag set.  This is
 * because it is possible for a translation path to be destroyed while a frame
 * from a translator is still in use.  Specifically, this happens if a masquerade
 * happens after a call to ast_read() but before the frame is done being processed, 
 * since the frame processing is generally done without the channel lock held.
 *
 * \return nothing
 */
void ast_translate_frame_freed(struct ast_frame *fr);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_TRANSLATE_H */
