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


//! Convenient for waiting
#define AST_DIGIT_ANY "0123456789#*"

/* Defined by individual formats.  First item MUST be a
   pointer for use by the stream manager */
struct ast_filestream;

//! Registers a new file format
/*! Register a new file format capability
 * Adds a format to asterisk's format abilities.  Fill in the fields, and it will work. For examples, look at some of the various format code.
 * returns 0 on success, -1 on failure
 */
int ast_format_register(char *name, char *exts, int format,
						struct ast_filestream * (*open)(int fd),
						struct ast_filestream * (*rewrite)(int fd, char *comment),
						int (*apply)(struct ast_channel *, struct ast_filestream *),
						int (*write)(struct ast_filestream *, struct ast_frame *),
						struct ast_frame * (*read)(struct ast_filestream *),
						void (*close)(struct ast_filestream *),
						char * (*getcomment)(struct ast_filestream *));
	
//! Unregisters a file format
/*!
 * \param name the name of the format you wish to unregister
 * Unregisters a format based on the name of the format.
 * Returns 0 on success, -1 on failure to unregister
 */
int ast_format_unregister(char *name);

//! Streams a file
/*!
 * \param c channel to stream the file to
 * \param filename the name of the file you wish to stream, minus the extension
 * \param preflang the preferred language you wish to have the file streamed to you in
 * Prepares a channel for the streaming of a file.  To start the stream, afterward do a ast_waitstream() on the channel
 * Also, it will stop any existing streams on the channel.
 * Returns 0 on success, or -1 on failure.
 */
int ast_streamfile(struct ast_channel *c, char *filename, char *preflang);

//! Stops a stream
/*!
 * \param c The channel you wish to stop playback on
 * Stop playback of a stream 
 * Returns 0 regardless
 */
int ast_stopstream(struct ast_channel *c);

//! Checks for the existence of a given file
/*!
 * \param filename name of the file you wish to check, minus the extension
 * \param fmt the format you wish to check (the extension)
 * \param preflang (the preferred language you wisht to find the file in)
 * See if a given file exists in a given format.  If fmt is NULL,  any format is accepted.
 * Returns -1 if file does not exist, non-zero positive otherwise.
 */
int ast_fileexists(char *filename, char *fmt, char *preflang);

//! Renames a file
/*! 
 * \param oldname the name of the file you wish to act upon (minus the extension)
 * \param newname the name you wish to rename the file to (minus the extension)
 * \param fmt the format of the file
 * Rename a given file in a given format, or if fmt is NULL, then do so for all 
 * Returns -1 on failure
 */
int ast_filerename(char *oldname, char *newname, char *fmt);

//! Deletes a file
/*! 
 * \param filename name of the file you wish to delete (minus the extension)
 * \param format of the file
 * Delete a given file in a given format, or if fmt is NULL, then do so for all 
 */
int ast_filedelete(char *filename, char *fmt);

//! Copies a file
/*!
 * \param oldname name of the file you wish to copy (minus extension)
 * \param newname name you wish the file to be copied to (minus extension)
 * \param fmt the format of the file
 * Copy a given file in a given format, or if fmt is NULL, then do so for all 
 */
int ast_filecopy(char *oldname, char *newname, char *fmt);

//! Waits for a stream to stop or digit to be pressed
/*!
 * \param c channel to waitstram on
 * \param breakon string of DTMF digits to break upon
 * Begins playback of a stream...
 * Wait for a stream to stop or for any one of a given digit to arrive,  Returns 0 
 * if the stream finishes, the character if it was interrupted, and -1 on error 
 */
char ast_waitstream(struct ast_channel *c, char *breakon);

//! Starts writing a file
/*!
 * \param filename the name of the file to write to
 * \param type format of file you wish to write out to
 * \param comment comment to go with
 * \param oflags output file flags
 * \param check (unimplemented, hence negligible)
 * \param mode Open mode
 * Create an outgoing file stream.  oflags are flags for the open() command, and 
 * if check is non-zero, then it will not write a file if there are any files that 
 * start with that name and have an extension
 * Please note, this is a blocking function.  Program execution will not return until ast_waitstream completes it's execution.
 * Returns a struct ast_filestream on success, NULL on failure
 */
struct ast_filestream *ast_writefile(char *filename, char *type, char *comment, int oflags, int check, mode_t mode);

//! Writes a frame to a stream
/*! 
 * \param fs filestream to write to
 * \param f frame to write to the filestream
 * Send a frame to a filestream -- note: does NOT free the frame, call ast_frfree manually
 * Returns 0 on success, -1 on failure.
 */
int ast_writestream(struct ast_filestream *fs, struct ast_frame *f);

//! Closes a stream
/*!
 * \param f filestream to close
 * Close a playback or recording stream
 * Returns 0 on success, -1 on failure
 */
int ast_closestream(struct ast_filestream *f);

#define AST_RESERVED_POINTERS 4

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif



#endif
