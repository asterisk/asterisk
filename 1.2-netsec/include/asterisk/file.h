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
 * \brief Generic File Format Support.
 */

#ifndef _ASTERISK_FILE_H
#define _ASTERISK_FILE_H

#ifndef stdin
#error You must include stdio.h before file.h!
#endif /* !stdin */

#include "asterisk/channel.h"
#include "asterisk/frame.h"
#include <fcntl.h>


#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif


/*! Convenient for waiting */
#define AST_DIGIT_ANY "0123456789#*ABCD"
#define AST_DIGIT_ANYNUM "0123456789"

#define SEEK_FORCECUR	10
	
/* Defined by individual formats.  First item MUST be a
   pointer for use by the stream manager */
struct ast_filestream;

/*! Registers a new file format */
/*! Register a new file format capability
 * Adds a format to asterisk's format abilities.  Fill in the fields, and it will work. For examples, look at some of the various format code.
 * returns 0 on success, -1 on failure
 */
int ast_format_register(const char *name, const char *exts, int format,
						struct ast_filestream * (*open)(FILE *f),
						struct ast_filestream * (*rewrite)(FILE *f, const char *comment),
						int (*write)(struct ast_filestream *, struct ast_frame *),
						int (*seek)(struct ast_filestream *, long offset, int whence),
						int (*trunc)(struct ast_filestream *),
						long (*tell)(struct ast_filestream *),
						struct ast_frame * (*read)(struct ast_filestream *, int *timetonext),
						void (*close)(struct ast_filestream *),
						char * (*getcomment)(struct ast_filestream *));
	
/*! Unregisters a file format */
/*!
 * \param name the name of the format you wish to unregister
 * Unregisters a format based on the name of the format.
 * Returns 0 on success, -1 on failure to unregister
 */
int ast_format_unregister(const char *name);

/*! Streams a file */
/*!
 * \param c channel to stream the file to
 * \param filename the name of the file you wish to stream, minus the extension
 * \param preflang the preferred language you wish to have the file streamed to you in
 * Prepares a channel for the streaming of a file.  To start the stream, afterward do a ast_waitstream() on the channel
 * Also, it will stop any existing streams on the channel.
 * Returns 0 on success, or -1 on failure.
 */
int ast_streamfile(struct ast_channel *c, const char *filename, const char *preflang);

/*! Stops a stream */
/*!
 * \param c The channel you wish to stop playback on
 * Stop playback of a stream 
 * Returns 0 regardless
 */
int ast_stopstream(struct ast_channel *c);

/*! Checks for the existence of a given file */
/*!
 * \param filename name of the file you wish to check, minus the extension
 * \param fmt the format you wish to check (the extension)
 * \param preflang (the preferred language you wisht to find the file in)
 * See if a given file exists in a given format.  If fmt is NULL,  any format is accepted.
 * Returns -1 if file does not exist, non-zero positive otherwise.
 */
int ast_fileexists(const char *filename, const char *fmt, const char *preflang);

/*! Renames a file */
/*! 
 * \param oldname the name of the file you wish to act upon (minus the extension)
 * \param newname the name you wish to rename the file to (minus the extension)
 * \param fmt the format of the file
 * Rename a given file in a given format, or if fmt is NULL, then do so for all 
 * Returns -1 on failure
 */
int ast_filerename(const char *oldname, const char *newname, const char *fmt);

/*! Deletes a file */
/*! 
 * \param filename name of the file you wish to delete (minus the extension)
 * \param fmt of the file
 * Delete a given file in a given format, or if fmt is NULL, then do so for all 
 */
int ast_filedelete(const char *filename, const char *fmt);

/*! Copies a file */
/*!
 * \param oldname name of the file you wish to copy (minus extension)
 * \param newname name you wish the file to be copied to (minus extension)
 * \param fmt the format of the file
 * Copy a given file in a given format, or if fmt is NULL, then do so for all 
 */
int ast_filecopy(const char *oldname, const char *newname, const char *fmt);

/*! Waits for a stream to stop or digit to be pressed */
/*!
 * \param c channel to waitstram on
 * \param breakon string of DTMF digits to break upon
 * Begins playback of a stream...
 * Wait for a stream to stop or for any one of a given digit to arrive,  Returns 0 
 * if the stream finishes, the character if it was interrupted, and -1 on error 
 */
int ast_waitstream(struct ast_channel *c, const char *breakon);

/*! Waits for a stream to stop or digit matching a valid one digit exten to be pressed */
/*!
 * \param c channel to waitstram on
 * \param context string of context to match digits to break upon
 * Begins playback of a stream...
 * Wait for a stream to stop or for any one of a valid extension digit to arrive,  Returns 0 
 * if the stream finishes, the character if it was interrupted, and -1 on error 
 */
int ast_waitstream_exten(struct ast_channel *c, const char *context);

/*! Same as waitstream but allows stream to be forwarded or rewound */
/*!
 * \param c channel to waitstram on
 * \param breakon string of DTMF digits to break upon
 * \param forward DTMF digit to fast forward upon
 * \param rewind DTMF digit to rewind upon
 * \param ms How many miliseconds to skip forward/back
 * Begins playback of a stream...
 * Wait for a stream to stop or for any one of a given digit to arrive,  Returns 0 
 * if the stream finishes, the character if it was interrupted, and -1 on error 
 */
int ast_waitstream_fr(struct ast_channel *c, const char *breakon, const char *forward, const char *rewind, int ms);

/* Same as waitstream, but with audio output to fd and monitored fd checking.  Returns
   1 if monfd is ready for reading */
int ast_waitstream_full(struct ast_channel *c, const char *breakon, int audiofd, int monfd);

/*! Starts reading from a file */
/*!
 * \param filename the name of the file to read from
 * \param type format of file you wish to read from
 * \param comment comment to go with
 * \param flags file flags
 * \param check (unimplemented, hence negligible)
 * \param mode Open mode
 * Open an incoming file stream.  flags are flags for the open() command, and 
 * if check is non-zero, then it will not read a file if there are any files that 
 * start with that name and have an extension
 * Please note, this is a blocking function.  Program execution will not return until ast_waitstream completes it's execution.
 * Returns a struct ast_filestream on success, NULL on failure
 */
struct ast_filestream *ast_readfile(const char *filename, const char *type, const char *comment, int flags, int check, mode_t mode);

/*! Starts writing a file */
/*!
 * \param filename the name of the file to write to
 * \param type format of file you wish to write out to
 * \param comment comment to go with
 * \param flags output file flags
 * \param check (unimplemented, hence negligible)
 * \param mode Open mode
 * Create an outgoing file stream.  oflags are flags for the open() command, and 
 * if check is non-zero, then it will not write a file if there are any files that 
 * start with that name and have an extension
 * Please note, this is a blocking function.  Program execution will not return until ast_waitstream completes it's execution.
 * Returns a struct ast_filestream on success, NULL on failure
 */
struct ast_filestream *ast_writefile(const char *filename, const char *type, const char *comment, int flags, int check, mode_t mode);

/*! Writes a frame to a stream */
/*! 
 * \param fs filestream to write to
 * \param f frame to write to the filestream
 * Send a frame to a filestream -- note: does NOT free the frame, call ast_frfree manually
 * Returns 0 on success, -1 on failure.
 */
int ast_writestream(struct ast_filestream *fs, struct ast_frame *f);

/*! Closes a stream */
/*!
 * \param f filestream to close
 * Close a playback or recording stream
 * Returns 0 on success, -1 on failure
 */
int ast_closestream(struct ast_filestream *f);

/*! Opens stream for use in seeking, playing */
/*!
 * \param chan channel to work with
 * \param filename to use
 * \param preflang prefered language to use
 * Returns a ast_filestream pointer if it opens the file, NULL on error
 */
struct ast_filestream *ast_openstream(struct ast_channel *chan, const char *filename, const char *preflang);

/*! Opens stream for use in seeking, playing */
/*!
 * \param chan channel to work with
 * \param filename to use
 * \param preflang prefered language to use
 * \param asis if set, don't clear generators
 * Returns a ast_filestream pointer if it opens the file, NULL on error
 */
struct ast_filestream *ast_openstream_full(struct ast_channel *chan, const char *filename, const char *preflang, int asis);
/*! Opens stream for use in seeking, playing */
/*!
 * \param chan channel to work with
 * \param filename to use
 * \param preflang prefered language to use
 * Returns a ast_filestream pointer if it opens the file, NULL on error
 */
struct ast_filestream *ast_openvstream(struct ast_channel *chan, const char *filename, const char *preflang);

/*! Applys a open stream to a channel. */
/*!
 * \param chan channel to work
 * \param s ast_filestream to apply
 * Returns 0 for success, -1 on failure
 */
int ast_applystream(struct ast_channel *chan, struct ast_filestream *s);

/*! play a open stream on a channel. */
/*!
 * \param s filestream to play
 * Returns 0 for success, -1 on failure
 */
int ast_playstream(struct ast_filestream *s);

/*! Seeks into stream */
/*!
 * \param fs ast_filestream to perform seek on
 * \param sample_offset numbers of samples to seek
 * \param whence SEEK_SET, SEEK_CUR, SEEK_END 
 * Returns 0 for success, or -1 for error
 */
int ast_seekstream(struct ast_filestream *fs, long sample_offset, int whence);

/*! Trunc stream at current location */
/*!
 * \param fs filestream to act on
 * Returns 0 for success, or -1 for error
 */
int ast_truncstream(struct ast_filestream *fs);

/*! Fast forward stream ms */
/*!
 * \param fs filestream to act on
 * \param ms milliseconds to move
 * Returns 0 for success, or -1 for error
 */
int ast_stream_fastforward(struct ast_filestream *fs, long ms);

/*! Rewind stream ms */
/*!
 * \param fs filestream to act on
 * \param ms milliseconds to move
 * Returns 0 for success, or -1 for error
 */
int ast_stream_rewind(struct ast_filestream *fs, long ms);

/*! Tell where we are in a stream */
/*!
 * \param fs fs to act on
 * Returns a long as a sample offset into stream
 */
long ast_tellstream(struct ast_filestream *fs);

/*! Read a frame from a filestream */
/*!
 * \param s ast_filestream to act on
 * Returns a frame or NULL if read failed
 */ 
struct ast_frame *ast_readframe(struct ast_filestream *s);

/*! Initialize file stuff */
/*!
 * Initializes all the various file stuff.  Basically just registers the cli stuff
 * Returns 0 all the time
 */
extern int ast_file_init(void);


#define AST_RESERVED_POINTERS 20

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_FILE_H */
