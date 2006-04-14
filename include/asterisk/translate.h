/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 */

#ifndef _ASTERISK_TRANSLATE_H
#define _ASTERISK_TRANSLATE_H

#define MAX_FORMAT 32

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

/*!
 * Descriptor of a translator. Name, callbacks, and various options
 * related to run-time operation (size of buffers, auxiliary
 * descriptors, etc).
 * A coded registers itself by filling the relevant fields
 * of a structure and passing it as an argument to
 * ast_register_translator(). The structure should not be
 * modified after a successful register(), and its address
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
	const char name[80];		/*! Name of translator */
	int srcfmt;			/*! Source format (note: bit position) */
	int dstfmt;			/*! Destination format (note: bit position) */

	/*! initialize private data associated with the translator */
	void *(*newpvt)(struct ast_trans_pvt *);

	/*! Input frame callback. Store (and possibly convert) input frame. */
	int (*framein)(struct ast_trans_pvt *pvt, struct ast_frame *in);

	/*! Output frame callback. Generate a frame with outbuf content. */
	struct ast_frame * (*frameout)(struct ast_trans_pvt *pvt);

	/*! cleanup private data, if needed (often unnecessary). */
	void (*destroy)(struct ast_trans_pvt *pvt);

	struct ast_frame * (*sample)(void);	/*! Generate an example frame */

	/*! size of outbuf, in samples. Leave it 0 if you want the framein
	 * callback deal with the frame. Set it appropriately if you
	 * want the code to checks if the incoming frame fits the
	 * outbuf (this is e.g. required for plc).
	 */
	int buffer_samples;	/* size of outbuf, in samples */

	/*! size of outbuf, in bytes. Mandatory. The wrapper code will also
	 * allocate an AST_FRIENDLY_OFFSET space before.
	 */
	int buf_size;

	int desc_size;		/*! size of private descriptor in pvt->pvt, if any */
	int plc_samples;	/* set to the plc block size if used, 0 otherwise */
	int useplc;		/* current status of plc, changed at runtime */

	void *module;		/* opaque reference to the parent module */

	int cost;		/*! Cost in milliseconds for encoding/decoding 1 second of sound */
	AST_LIST_ENTRY(ast_translator) list;	/*! link field */
};

/*
 * Default structure for translators, with the basic fields and buffers,
 * all allocated as part of the same chunk of memory. The buffer is
 * preceded by AST_FRIENDLY_OFFSET bytes in front of the user portion.
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
	struct ast_frame f;	/* used in frameout */
	int samples;		/* samples available in outbuf */
	int datalen;		/* actual space used in outbuf */
	void *pvt;		/* more private data, if any */
	char *outbuf;		/* the useful portion of the buffer */
	plc_state_t *plc;	/* optional plc pointer */
	struct ast_trans_pvt *next;	/* next in translator chain */
	struct timeval nextin;
	struct timeval nextout;
};

/* generic frameout function */
struct ast_frame *ast_trans_frameout(struct ast_trans_pvt *pvt,
        int datalen, int samples);

struct ast_trans_pvt;

/*!
 * \brief Register a translator
 * \param t populated ast_translator structure
 * This registers a codec translator with asterisk
 * Returns 0 on success, -1 on failure
 */
int ast_register_translator(struct ast_translator *t, void *module);

/*!
 * \brief Unregister a translator
 * \param t translator to unregister
 * Unregisters the given tranlator
 * Returns 0 on success, -1 on failure
 */
int ast_unregister_translator(struct ast_translator *t);

/*!
 * \brief Chooses the best translation path
 *
 * Given a list of sources, and a designed destination format, which should
 * I choose? Returns 0 on success, -1 if no path could be found.  Modifies
 * dests and srcs in place 
 */
int ast_translator_best_choice(int *dsts, int *srcs);

/*! 
 * \brief Builds a translator path
 * \param dest destination format
 * \param source source format
 * Build a path (possibly NULL) from source to dest 
 * Returns ast_trans_pvt on success, NULL on failure
 * */
struct ast_trans_pvt *ast_translator_build_path(int dest, int source);

/*!
 * \brief Frees a translator path
 * \param tr translator path to get rid of
 * Frees the given translator path structure
 */
void ast_translator_free_path(struct ast_trans_pvt *tr);

/*!
 * \brief translates one or more frames
 * \param tr translator structure to use for translation
 * \param f frame to translate
 * \param consume Whether or not to free the original frame
 * Apply an input frame into the translator and receive zero or one output frames.  Consume
 * determines whether the original frame should be freed
 * Returns an ast_frame of the new translation format on success, NULL on failure
 */
struct ast_frame *ast_translate(struct ast_trans_pvt *tr, struct ast_frame *f, int consume);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_TRANSLATE_H */
