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
 * \brief Generic File Format Support.
 * Should be included by clients of the file handling routines.
 * File service providers should instead include mod_format.h
 */

#ifndef _ASTERISK_FILE_H
#define _ASTERISK_FILE_H

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_filestream;
struct ast_format;

/*! The maximum number of formats we expect to see in a format string */
#define AST_MAX_FORMATS 10

/*! Convenient for waiting */
#define AST_DIGIT_NONE ""
#define AST_DIGIT_ANY "0123456789#*ABCD"
#define AST_DIGIT_ANYNUM "0123456789"

#define SEEK_FORCECUR	10

/*! The type of event associated with a ast_waitstream_fr_cb invocation */
enum ast_waitstream_fr_cb_values {
	AST_WAITSTREAM_CB_REWIND = 1,
	AST_WAITSTREAM_CB_FASTFORWARD,
	AST_WAITSTREAM_CB_START
};

/*!
 * \brief callback used during dtmf controlled file playback to indicate
 * location of playback in a file after rewinding or fastforwarding
 * a file.
 */
typedef void (ast_waitstream_fr_cb)(struct ast_channel *chan, long ms, enum ast_waitstream_fr_cb_values val);

/*!
 * \brief Streams a file
 * \param c channel to stream the file to
 * \param filename the name of the file you wish to stream, minus the extension
 * \param preflang the preferred language you wish to have the file streamed to you in
 * Prepares a channel for the streaming of a file.  To start the stream, afterward do a ast_waitstream() on the channel
 * Also, it will stop any existing streams on the channel.
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_streamfile(struct ast_channel *c, const char *filename, const char *preflang);

/*!
 * \brief stream file until digit
 * If the file name is non-empty, try to play it.
 * \note If digits == "" then we can simply check for non-zero.
 * \return 0 if success.
 * \retval -1 if error.
 * \retval digit if interrupted by a digit.
 */
int ast_stream_and_wait(struct ast_channel *chan, const char *file, const char *digits);

/*!
 * \brief Stops a stream
 *
 * \param c The channel you wish to stop playback on
 *
 * Stop playback of a stream
 *
 * \retval 0 always
 *
 * \note The channel does not need to be locked before calling this function.
 */
int ast_stopstream(struct ast_channel *c);

/*!
 * \brief Checks for the existence of a given file
 * \param filename name of the file you wish to check, minus the extension
 * \param fmt the format you wish to check (the extension)
 * \param preflang (the preferred language you wisht to find the file in)
 * See if a given file exists in a given format.  If fmt is NULL,  any format is accepted.
 * \retval 0, false. The file does not exist
 * \retval 1, true. The file does exist.
 */
int ast_fileexists(const char *filename, const char *fmt, const char *preflang);

/*!
 * \brief Renames a file
 * \param oldname the name of the file you wish to act upon (minus the extension)
 * \param newname the name you wish to rename the file to (minus the extension)
 * \param fmt the format of the file
 * Rename a given file in a given format, or if fmt is NULL, then do so for all
 * \return -1 on failure
 */
int ast_filerename(const char *oldname, const char *newname, const char *fmt);

/*!
 * \brief  Deletes a file
 * \param filename name of the file you wish to delete (minus the extension)
 * \param fmt of the file
 * Delete a given file in a given format, or if fmt is NULL, then do so for all
 */
int ast_filedelete(const char *filename, const char *fmt);

/*!
 * \brief Copies a file
 * \param oldname name of the file you wish to copy (minus extension)
 * \param newname name you wish the file to be copied to (minus extension)
 * \param fmt the format of the file
 * Copy a given file in a given format, or if fmt is NULL, then do so for all
 */
int ast_filecopy(const char *oldname, const char *newname, const char *fmt);

/*!
 * \brief Callback called for each file found when reading directories
 * \param dir_name the name of the directory
 * \param filename the name of the file
 * \param obj user data object
 * \return non-zero to stop reading, otherwise zero to continue
 *
 * \note dir_name is not processed by realpath or other functions,
 *       symbolic links are not resolved.  This ensures dir_name
 *       always starts with the exact string originally passed to
 *       \ref ast_file_read_dir or \ref ast_file_read_dirs.
 */
typedef int (*ast_file_on_file)(const char *dir_name, const char *filename, void *obj);

/*!
 * \brief Recursively iterate through files and directories up to max_depth
 * \param dir_name the name of the directory to search
 * \param on_file callback called on each file
 * \param obj user data object
 * \param max_depth re-curse into sub-directories up to a given maximum (-1 = infinite)
 * \return -1 or errno on failure, otherwise 0
 */
int ast_file_read_dirs(const char *dir_name, ast_file_on_file on_file, void *obj, int max_depth);

/*!
 * \brief Iterate over each file in a given directory
 * \param dir_name the name of the directory to search
 * \param on_file callback called on each file
 * \param obj user data object
 * \return -1 or errno on failure, otherwise 0
 */
#define ast_file_read_dir(dir_name, on_file, obj) ast_file_read_dirs(dir_name, on_file, obj, 1)

/*!
 * \brief Waits for a stream to stop or digit to be pressed
 * \param c channel to waitstream on
 * \param breakon string of DTMF digits to break upon
 * Begins playback of a stream...
 * Wait for a stream to stop or for any one of a given digit to arrive,
 * \retval 0 if the stream finishes
 * \retval the character if it was interrupted by the channel.
 * \retval -1 on error
 */
int ast_waitstream(struct ast_channel *c, const char *breakon);

/*!
 * \brief Waits for a stream to stop or digit matching a valid one digit exten to be pressed
 * \param c channel to waitstream on
 * \param context string of context to match digits to break upon
 * Begins playback of a stream...
 * Wait for a stream to stop or for any one of a valid extension digit to arrive,
 * \retval 0 if the stream finishes.
 * \retval the character if it was interrupted.
 * \retval -1 on error.
 */
int ast_waitstream_exten(struct ast_channel *c, const char *context);

/*!
 * \brief Same as waitstream but allows stream to be forwarded or rewound
 * \param c channel to waitstream on
 * \param breakon string of DTMF digits to break upon
 * \param forward DTMF digit to fast forward upon
 * \param rewind DTMF digit to rewind upon
 * \param ms How many miliseconds to skip forward/back
 * Begins playback of a stream...
 * Wait for a stream to stop or for any one of a given digit to arrive,
 * \retval 0 if the stream finishes.
 * \retval the character if it was interrupted,
 * \retval the value of the control frame if it was interrupted by some other party,
 * \retval -1 on error.
 */
int ast_waitstream_fr(struct ast_channel *c, const char *breakon, const char *forward, const char *rewind, int ms);

/*!
 * \brief Same as waitstream_fr but allows a callback to be alerted when a user
 * fastforwards or rewinds the file.
 * \param c channel to waitstream on
 * \param breakon string of DTMF digits to break upon
 * \param forward DTMF digit to fast forward upon
 * \param rewind DTMF digit to rewind upon
 * \param ms How many milliseconds to skip forward/back
 * \param cb to call when rewind or fastforward occurs.
 * Begins playback of a stream...
 * Wait for a stream to stop or for any one of a given digit to arrive,
 * \retval 0 if the stream finishes.
 * \retval the character if it was interrupted,
 * \retval the value of the control frame if it was interrupted by some other party,
 * \retval -1 on error.
 */
int ast_waitstream_fr_w_cb(struct ast_channel *c,
	const char *breakon,
	const char *forward,
	const char *rewind,
	int ms,
	ast_waitstream_fr_cb cb);

/*!
 * Same as waitstream, but with audio output to fd and monitored fd checking.
 *
 * \return 1 if monfd is ready for reading
 */
int ast_waitstream_full(struct ast_channel *c, const char *breakon, int audiofd, int monfd);

/*!
 * \brief Starts reading from a file
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
 * \retval a struct ast_filestream on success.
 * \retval NULL on failure.
 */
struct ast_filestream *ast_readfile(const char *filename, const char *type, const char *comment, int flags, int check, mode_t mode);

/*!
 * \brief Starts writing a file
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
 * \retval a struct ast_filestream on success.
 * \retval NULL on failure.
 */
struct ast_filestream *ast_writefile(const char *filename, const char *type, const char *comment, int flags, int check, mode_t mode);

/*!
 * \brief Writes a frame to a stream
 * \param fs filestream to write to
 * \param f frame to write to the filestream
 * Send a frame to a filestream -- note: does NOT free the frame, call ast_frfree manually
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_writestream(struct ast_filestream *fs, struct ast_frame *f);

/*!
 * \brief Closes a stream
 * \param f filestream to close
 * Close a playback or recording stream
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_closestream(struct ast_filestream *f);

/*!
 * \brief Opens stream for use in seeking, playing
 * \param chan channel to work with
 * \param filename to use
 * \param preflang prefered language to use
 * \retval a ast_filestream pointer if it opens the file.
 * \retval NULL on error.
 */
struct ast_filestream *ast_openstream(struct ast_channel *chan, const char *filename, const char *preflang);

/*!
 * \brief Opens stream for use in seeking, playing
 * \param chan channel to work with
 * \param filename to use
 * \param preflang prefered language to use
 * \param asis if set, don't clear generators
 * \retval a ast_filestream pointer if it opens the file.
 * \retval NULL on error.
 */
struct ast_filestream *ast_openstream_full(struct ast_channel *chan, const char *filename, const char *preflang, int asis);
/*!
 * \brief Opens stream for use in seeking, playing
 * \param chan channel to work with
 * \param filename to use
 * \param preflang prefered language to use
 * \retval a ast_filestream pointer if it opens the file.
 * \retval NULL on error.
 */
struct ast_filestream *ast_openvstream(struct ast_channel *chan, const char *filename, const char *preflang);

/*!
 * \brief Applies a open stream to a channel.
 * \param chan channel to work
 * \param s ast_filestream to apply
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_applystream(struct ast_channel *chan, struct ast_filestream *s);

/*!
 * \brief Play a open stream on a channel.
 * \param s filestream to play
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_playstream(struct ast_filestream *s);

/*!
 * \brief Seeks into stream
 * \param fs ast_filestream to perform seek on
 * \param sample_offset numbers of samples to seek
 * \param whence SEEK_SET, SEEK_CUR, SEEK_END
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_seekstream(struct ast_filestream *fs, off_t sample_offset, int whence);

/*!
 * \brief Trunc stream at current location
 * \param fs filestream to act on
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_truncstream(struct ast_filestream *fs);

/*!
 * \brief Fast forward stream ms
 * \param fs filestream to act on
 * \param ms milliseconds to move
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_stream_fastforward(struct ast_filestream *fs, off_t ms);

/*!
 * \brief Rewind stream ms
 * \param fs filestream to act on
 * \param ms milliseconds to move
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_stream_rewind(struct ast_filestream *fs, off_t ms);

/*!
 * \brief Tell where we are in a stream
 * \param fs fs to act on
 * \return a long as a sample offset into stream
 */
off_t ast_tellstream(struct ast_filestream *fs);

/*!
 * \brief Return the sample rate of the stream's format
 * \param fs fs to act on
 * \return sample rate in Hz
 */
int ast_ratestream(struct ast_filestream *fs);

/*!
 * \brief Read a frame from a filestream
 * \param s ast_filestream to act on
 * \return a frame.
 * \retval NULL if read failed.
 */
struct ast_frame *ast_readframe(struct ast_filestream *s);

/*! Initialize file stuff */
/*!
 * Initializes all the various file stuff.  Basically just registers the cli stuff
 * Returns 0 all the time
 */
int ast_file_init(void);


#define AST_RESERVED_POINTERS 20

/*! Remove duplicate formats from a format string. */
/*!
 * \param fmts a format string, this string will be modified
 * \retval NULL error
 * \return a pointer to the reduced format string, this is a pointer to fmts
 */
char *ast_format_str_reduce(char *fmts);

/*!
 * \brief Get the ast_format associated with the given file extension
 * \since 12
 *
 * \param file_ext The file extension for which to find the format
 *
 * \retval NULL if not found
 * \retval A pointer to the ast_format associated with this file extension
 */
struct ast_format *ast_get_format_for_file_ext(const char *file_ext);

/*!
 * \brief Get a suitable filename extension for the given MIME type
 *
 * \param mime_type The MIME type for which to find extensions
 * \param buffer A pointer to a buffer to receive the extension
 * \param capacity The size of 'buffer' in bytes
 *
 * \retval 1 if an extension was found for the provided MIME type
 * \retval 0 if the MIME type was not found
 */
int ast_get_extension_for_mime_type(const char *mime_type, char *buffer, size_t capacity);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_FILE_H */
