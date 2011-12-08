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
 * \brief Header for providers of file and format handling routines.
 * Clients of these routines should include "asterisk/file.h" instead.
 */

#ifndef _ASTERISK_MOD_FORMAT_H
#define _ASTERISK_MOD_FORMAT_H

#include "asterisk/file.h"
#include "asterisk/frame.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! \brief
 * Each supported file format is described by the following structure.
 *
 * Not all are necessary, the support routine implement default
 * values for some of them.
 * A handler typically fills a structure initializing the desired
 * fields, and then calls ast_format_def_register() with the (readonly)
 * structure as an argument.
 */
struct ast_format_def {
	char name[80];		/*!< Name of format */
	char exts[80];		/*!< Extensions (separated by | if more than one) 
	    			this format can read.  First is assumed for writing (e.g. .mp3) */
	struct ast_format format;	/*!< Format of frames it uses/provides (one only) */
	/*! 
	 * \brief Prepare an input stream for playback. 
	 * \return 0 on success, -1 on error.
	 * The FILE is already open (in s->f) so this function only needs to perform
	 * any applicable validity checks on the file. If none is required, the
	 * function can be omitted.
	 */
	int (*open)(struct ast_filestream *s);
	/*! 
	 * \brief Prepare a stream for output, and comment it appropriately if applicable.
	 * \return 0 on success, -1 on error. 
	 * Same as the open, the FILE is already open so the function just needs to 
	 * prepare any header and other fields, if any. 
	 * The function can be omitted if nothing is needed.
	 */
	int (*rewrite)(struct ast_filestream *s, const char *comment);
	/*! Write a frame to a channel */
	int (*write)(struct ast_filestream *, struct ast_frame *);
	/*! seek num samples into file, whence - like a normal seek but with offset in samples */
	int (*seek)(struct ast_filestream *, off_t, int);
	int (*trunc)(struct ast_filestream *fs);	/*!< trunc file to current position */
	off_t (*tell)(struct ast_filestream *fs);	/*!< tell current position */
	/*! Read the next frame from the filestream (if available) and report
	 * when to get next frame (in samples)
	 */
	struct ast_frame * (*read)(struct ast_filestream *, int *whennext);
	/*! Do any closing actions, if any. The descriptor and structure are closed
	 * and destroyed by the generic routines, so they must not be done here. */
	void (*close)(struct ast_filestream *);
	char * (*getcomment)(struct ast_filestream *);		/*!< Retrieve file comment */

	AST_LIST_ENTRY(ast_format_def) list;                /*!< Link */

	/*!
	 * If the handler needs a buffer (for read, typically)
	 * and/or a private descriptor, put here the
	 * required size (in bytes) and the support routine will allocate them
	 * for you, pointed by s->buf and s->private, respectively.
	 * When allocating a buffer, remember to leave AST_FRIENDLY_OFFSET
	 * spare bytes at the bginning.
	 */
	int buf_size;			/*!< size of frame buffer, if any, aligned to 8 bytes. */
	int desc_size;			/*!< size of private descriptor, if any */

	struct ast_module *module;
};

/*! \brief
 * This structure is allocated by file.c in one chunk,
 * together with buf_size and desc_size bytes of memory
 * to be used for private purposes (e.g. buffers etc.)
 */
struct ast_filestream {
	/*! Everybody reserves a block of AST_RESERVED_POINTERS pointers for us */
	struct ast_format_def *fmt;	/* need to write to the lock and usecnt */
	int flags;
	mode_t mode;
	char *open_filename;
	char *filename;
	char *realfilename;
	/*! Video file stream */
	struct ast_filestream *vfs;
	/*! Transparently translate from another format -- just once */
	struct ast_trans_pvt *trans;
	struct ast_tranlator_pvt *tr;
	struct ast_format lastwriteformat;
	int lasttimeout;
	struct ast_channel *owner;
	FILE *f;
	struct ast_frame fr;	/*!< frame produced by read, typically */
	char *buf;		/*!< buffer pointed to by ast_frame; */
	void *_private;	/*!< pointer to private buffer */
	const char *orig_chan_name;
	char *write_buffer;
};

/*! 
 * \brief Register a new file format capability.
 * Adds a format to Asterisk's format abilities.
 * \retval 0 on success
 * \retval -1 on failure
 */
int __ast_format_def_register(const struct ast_format_def *f, struct ast_module *mod);
#define ast_format_def_register(f) __ast_format_def_register(f, ast_module_info->self)

/*! 
 * \brief Unregisters a file format 
 * \param name the name of the format you wish to unregister
 * Unregisters a format based on the name of the format.
 * \retval 0 on success
 * \retval -1 on failure to unregister
 */
int ast_format_def_unregister(const char *name);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_MOD_FORMAT_H */
