/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Generic File Format Support.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_FILE_H
#define _ASTERISK_FILE_H

#include <asterisk/channel.h>
#include <asterisk/frame.h>
#include <fcntl.h>


#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif


/* Convenient for waiting */
#define AST_DIGIT_ANY "0123456789#*"

/* Defined by individual formats.  First item MUST be a
   pointer for use by the stream manager */
struct ast_filestream;

/* Register a new file format capability */
int ast_format_register(char *name, char *exts, int format,
						struct ast_filestream * (*open)(int fd),
						struct ast_filestream * (*rewrite)(int fd, char *comment),
						int (*apply)(struct ast_channel *, struct ast_filestream *),
						int (*write)(struct ast_filestream *, struct ast_frame *),
						struct ast_frame * (*read)(struct ast_filestream *),
						void (*close)(struct ast_filestream *),
						char * (*getcomment)(struct ast_filestream *));
	
int ast_format_unregister(char *name);

/* Start streaming a file, in the preferred language if possible */
int ast_streamfile(struct ast_channel *c, char *filename, char *preflang);

/* Stop playback of a stream */
int ast_stopstream(struct ast_channel *c);

/* See if a given file exists in a given format.  If fmt is NULL,  any format is accepted.*/
/* Returns -1 if file does not exist */
int ast_fileexists(char *filename, char *fmt, char *preflang);

/* Rename a given file in a given format, or if fmt is NULL, then do so for all */
int ast_filerename(char *oldname, char *newname, char *fmt);

/* Delete a given file in a given format, or if fmt is NULL, then do so for all */
int ast_filedelete(char *filename, char *fmt);

/* Copy a given file in a given format, or if fmt is NULL, then do so for all */
int ast_filecopy(char *oldname, char *newname, char *fmt);

/* Wait for a stream to stop or for any one of a given digit to arrive,  Returns
   0 if the stream finishes, the character if it was interrupted, and -1 on error */
char ast_waitstream(struct ast_channel *c, char *breakon);

/* Create an outgoing file stream.  oflags are flags for the open() command, and
   if check is non-zero, then it will not write a file if there are any files that
   start with that name and have an extension */
struct ast_filestream *ast_writefile(char *filename, char *type, char *comment, int oflags, int check, mode_t mode);

/* Send a frame to a filestream -- note: does NOT free the frame, call ast_frfree manually */
int ast_writestream(struct ast_filestream *fs, struct ast_frame *f);

/* Close a playback or recording stream */
int ast_closestream(struct ast_filestream *f);

#define AST_RESERVED_POINTERS 4

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif



#endif
