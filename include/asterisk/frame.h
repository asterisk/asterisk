/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Asterisk internal frame definitions.
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser General Public License.  Other components of
 * Asterisk are distributed under The GNU General Public License
 * only.
 */

#ifndef _ASTERISK_FRAME_H
#define _ASTERISK_FRAME_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <asterisk/endian.h>

struct ast_codec_pref {
	char order[32];
};


/*! Data structure associated with a single frame of data */
/* A frame of data read used to communicate between 
   between channels and applications */
struct ast_frame {
	/*! Kind of frame */
	int frametype;				
	/*! Subclass, frame dependent */
	int subclass;				
	/*! Length of data */
	int datalen;				
	/*! Number of 8khz samples in this frame */
	int samples;				
	/*! Was the data malloc'd?  i.e. should we free it when we discard the frame? */
	int mallocd;				
	/*! How far into "data" the data really starts */
	int offset;				
	/*! Optional source of frame for debugging */
	const char *src;				
	/*! Pointer to actual data */
	void *data;		
	/*! Global delivery time */		
	struct timeval delivery;
	/*! Next/Prev for linking stand alone frames */
	struct ast_frame *prev;			
	/*! Next/Prev for linking stand alone frames */
	struct ast_frame *next;			
								/* Unused except if debugging is turned on, but left
								   in the struct so that it can be turned on without
								   requiring a recompile of the whole thing */
};

struct ast_frame_chain {
	/* XXX Should ast_frame chain's be just prt of frames, i.e. should they just link? XXX */
	struct ast_frame *fr;
	struct ast_frame_chain *next;
};

#define AST_FRIENDLY_OFFSET 	64		/*! It's polite for a a new frame to
						    				have this number of bytes for additional
											headers.  */
#define AST_MIN_OFFSET 		32		/*! Make sure we keep at least this much handy */

/*! Need the header be free'd? */
#define AST_MALLOCD_HDR		(1 << 0)
/*! Need the data be free'd? */
#define AST_MALLOCD_DATA	(1 << 1)
/*! Need the source be free'd? (haha!) */
#define AST_MALLOCD_SRC		(1 << 2)

/* Frame types */
/*! A DTMF digit, subclass is the digit */
#define AST_FRAME_DTMF		1
/*! Voice data, subclass is AST_FORMAT_* */
#define AST_FRAME_VOICE		2
/*! Video frame, maybe?? :) */
#define AST_FRAME_VIDEO		3
/*! A control frame, subclass is AST_CONTROL_* */
#define AST_FRAME_CONTROL	4
/*! An empty, useless frame */
#define AST_FRAME_NULL		5
/*! Inter Asterisk Exchange private frame type */
#define AST_FRAME_IAX		6
/*! Text messages */
#define AST_FRAME_TEXT		7
/*! Image Frames */
#define AST_FRAME_IMAGE		8
/*! HTML Frame */
#define AST_FRAME_HTML		9
/*! Comfort Noise frame (subclass is level of CNG in -dBov), 
    body may include zero or more 8-bit quantization coefficients */
#define AST_FRAME_CNG		10

/* HTML subclasses */
/*! Sending a URL */
#define AST_HTML_URL		1
/*! Data frame */
#define AST_HTML_DATA		2
/*! Beginning frame */
#define AST_HTML_BEGIN		4
/*! End frame */
#define AST_HTML_END		8
/*! Load is complete */
#define AST_HTML_LDCOMPLETE	16
/*! Peer is unable to support HTML */
#define AST_HTML_NOSUPPORT	17
/*! Send URL, and track */
#define AST_HTML_LINKURL	18
/*! No more HTML linkage */
#define AST_HTML_UNLINK		19
/*! Reject link request */
#define AST_HTML_LINKREJECT	20

/* Data formats for capabilities and frames alike */
/*! G.723.1 compression */
#define AST_FORMAT_G723_1	(1 << 0)
/*! GSM compression */
#define AST_FORMAT_GSM		(1 << 1)
/*! Raw mu-law data (G.711) */
#define AST_FORMAT_ULAW		(1 << 2)
/*! Raw A-law data (G.711) */
#define AST_FORMAT_ALAW		(1 << 3)
/*! ADPCM (G.726, 32kbps) */
#define AST_FORMAT_G726		(1 << 4)
/*! ADPCM (IMA) */
#define AST_FORMAT_ADPCM	(1 << 5)
/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
#define AST_FORMAT_SLINEAR	(1 << 6)
/*! LPC10, 180 samples/frame */
#define AST_FORMAT_LPC10	(1 << 7)
/*! G.729A audio */
#define AST_FORMAT_G729A	(1 << 8)
/*! SpeeX Free Compression */
#define AST_FORMAT_SPEEX	(1 << 9)
/*! iLBC Free Compression */
#define AST_FORMAT_ILBC		(1 << 10)
/*! Maximum audio format */
#define AST_FORMAT_MAX_AUDIO	(1 << 15)
/*! JPEG Images */
#define AST_FORMAT_JPEG		(1 << 16)
/*! PNG Images */
#define AST_FORMAT_PNG		(1 << 17)
/*! H.261 Video */
#define AST_FORMAT_H261		(1 << 18)
/*! H.263 Video */
#define AST_FORMAT_H263		(1 << 19)
/*! H.263+ Video */
#define AST_FORMAT_H263_PLUS	(1 << 20)
/*! Max one */
#define AST_FORMAT_MAX_VIDEO	(1 << 24)

/* Control frame types */
/*! Other end has hungup */
#define AST_CONTROL_HANGUP		1
/*! Local ring */
#define AST_CONTROL_RING		2
/*! Remote end is ringing */
#define AST_CONTROL_RINGING 		3
/*! Remote end has answered */
#define AST_CONTROL_ANSWER		4
/*! Remote end is busy */
#define AST_CONTROL_BUSY		5
/*! Make it go off hook */
#define AST_CONTROL_TAKEOFFHOOK		6
/*! Line is off hook */
#define AST_CONTROL_OFFHOOK		7
/*! Congestion (circuits busy) */
#define AST_CONTROL_CONGESTION		8
/*! Flash hook */
#define AST_CONTROL_FLASH		9
/*! Wink */
#define AST_CONTROL_WINK		10
/*! Set a low-level option */
#define AST_CONTROL_OPTION		11
/*! Key Radio */
#define	AST_CONTROL_RADIO_KEY		12
/*! Un-Key Radio */
#define	AST_CONTROL_RADIO_UNKEY		13
/*! Indicate PROGRESS */
#define AST_CONTROL_PROGRESS            14
/*! Indicate CALL PROCEEDING */
#define AST_CONTROL_PROCEEDING		15
/*! Indicate call is placed on hold */
#define AST_CONTROL_HOLD			16
/*! Indicate call is left from hold */
#define AST_CONTROL_UNHOLD			17

#define AST_SMOOTHER_FLAG_G729		(1 << 0)

/* Option identifiers and flags */
#define AST_OPTION_FLAG_REQUEST		0
#define AST_OPTION_FLAG_ACCEPT		1
#define AST_OPTION_FLAG_REJECT		2
#define AST_OPTION_FLAG_QUERY		4
#define AST_OPTION_FLAG_ANSWER		5
#define AST_OPTION_FLAG_WTF		6

/* Verify touchtones by muting audio transmission 
	(and reception) and verify the tone is still present */
#define AST_OPTION_TONE_VERIFY		1		

/* Put a compatible channel into TDD (TTY for the hearing-impared) mode */
#define	AST_OPTION_TDD			2

/* Relax the parameters for DTMF reception (mainly for radio use) */
#define	AST_OPTION_RELAXDTMF		3

/* Set (or clear) Audio (Not-Clear) Mode */
#define	AST_OPTION_AUDIO_MODE		4

struct ast_option_header {
	/* Always keep in network byte order */
#if __BYTE_ORDER == __BIG_ENDIAN
        u_int16_t flag:3;
        u_int16_t option:13;
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
        u_int16_t option:13;
        u_int16_t flag:3;
#else
#error Byte order not defined
#endif
#endif
		u_int8_t data[0];
};

/*  Requests a frame to be allocated */
/* 
 * \param source 
 * Request a frame be allocated.  source is an optional source of the frame, 
 * len is the requested length, or "0" if the caller will supply the buffer 
 */
#if 0 /* Unimplemented */
struct ast_frame *ast_fralloc(char *source, int len);
#endif

/*! Frees a frame */
/*! 
 * \param fr Frame to free
 * Free a frame, and the memory it used if applicable
 * no return.
 */
void ast_frfree(struct ast_frame *fr);

/*! Copies a frame */
/*! 
 * \param fr frame to act upon
 * Take a frame, and if it's not been malloc'd, make a malloc'd copy
 * and if the data hasn't been malloced then make the
 * data malloc'd.  If you need to store frames, say for queueing, then
 * you should call this function.
 * Returns a frame on success, NULL on error
 */
struct ast_frame *ast_frisolate(struct ast_frame *fr);

/*! Copies a frame */
/*! 
 * \param fr frame to copy
 * Dupliates a frame -- should only rarely be used, typically frisolate is good enough
 * Returns a frame on success, NULL on error
 */
struct ast_frame *ast_frdup(struct ast_frame *fr);

/*! Chains a frame -- unimplemented */
#if 0 /* unimplemented */
void ast_frchain(struct ast_frame_chain *fc);
#endif

/*! Reads a frame from an fd */
/*! 
 * \param fd an opened fd to read from
 * Read a frame from a stream or packet fd, as written by fd_write
 * returns a frame on success, NULL on error
 */
struct ast_frame *ast_fr_fdread(int fd);

/*! Writes a frame to an fd */
/*! 
 * \param fd Which fd to write to
 * \param frame frame to write to the fd
 * Write a frame to an fd
 * Returns 0 on success, -1 on failure
 */
int ast_fr_fdwrite(int fd, struct ast_frame *frame);

/*! Sends a hangup to an fd */
/*! 
 * \param fd fd to write to
 * Send a hangup (NULL equivalent) on an fd
 * Returns 0 on success, -1 on failure
 */
int ast_fr_fdhangup(int fd);

void ast_swapcopy_samples(void *dst, const void *src, int samples);

/* Helpers for byteswapping native samples to/from 
   little-endian and big-endian. */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ast_frame_byteswap_le(fr) do { ; } while(0)
#define ast_frame_byteswap_be(fr) do { struct ast_frame *__f = (fr); ast_swapcopy_samples(__f->data, __f->data, __f->samples); } while(0)
#else
#define ast_frame_byteswap_le(fr) do { struct ast_frame *__f = (fr); ast_swapcopy_samples(__f->data, __f->data, __f->samples); } while(0)
#define ast_frame_byteswap_be(fr) do { ; } while(0)
#endif


/*! Get the name of a format */
/*!
 * \param format id of format
 * \return A static string containing the name of the format or "UNKN" if unknown.
 */
extern char* ast_getformatname(int format);

/*! Get the names of a set of formats */
/*!
 * \param buf a buffer for the output string
 * \param n size of buf (bytes)
 * \param format the format (combined IDs of codecs)
 * Prints a list of readable codec names corresponding to "format".
 * ex: for format=AST_FORMAT_GSM|AST_FORMAT_SPEEX|AST_FORMAT_ILBC it will return "0x602 (GSM|SPEEX|ILBC)"
 * \return The return value is buf.
 */
extern char* ast_getformatname_multiple(char *buf, size_t size, int format);

/*!
 * \param name string of format
 * Gets a format from a name.
 * This returns the form of the format in binary on success, 0 on error.
 */
extern int ast_getformatbyname(char *name);

/*! Get a name from a format */
/*!
 * \param codec codec number (1,2,4,8,16,etc.)
 * Gets a name from a format
 * This returns a static string identifying the format on success, 0 on error.
 */
extern char *ast_codec2str(int codec);

/*! Pick the best codec  */
/* Choose the best codec...  Uhhh...   Yah. */
extern int ast_best_codec(int fmts);

struct ast_smoother;

extern struct ast_format_list *ast_get_format_list_index(int index);
extern struct ast_format_list *ast_get_format_list(size_t *size);
extern struct ast_smoother *ast_smoother_new(int bytes);
extern void ast_smoother_set_flags(struct ast_smoother *smoother, int flags);
extern int ast_smoother_get_flags(struct ast_smoother *smoother);
extern void ast_smoother_free(struct ast_smoother *s);
extern void ast_smoother_reset(struct ast_smoother *s, int bytes);
extern int __ast_smoother_feed(struct ast_smoother *s, struct ast_frame *f, int swap);
extern struct ast_frame *ast_smoother_read(struct ast_smoother *s);
#define ast_smoother_feed(s,f) do { __ast_smoother_feed(s, f, 0); } while(0)
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ast_smoother_feed_be(s,f) do { __ast_smoother_feed(s, f, 1); } while(0)
#define ast_smoother_feed_le(s,f) do { __ast_smoother_feed(s, f, 0); } while(0)
#else
#define ast_smoother_feed_be(s,f) do { __ast_smoother_feed(s, f, 0); } while(0)
#define ast_smoother_feed_le(s,f) do { __ast_smoother_feed(s, f, 1); } while(0)
#endif

extern void ast_frame_dump(char *name, struct ast_frame *f, char *prefix);

/* Initialize a codec preference to "no preference" */
extern void ast_codec_pref_init(struct ast_codec_pref *pref);

/* Codec located at  a particular place in the preference index */
extern int ast_codec_pref_index(struct ast_codec_pref *pref, int index);

/* Remove a codec from a preference list */
extern void ast_codec_pref_remove(struct ast_codec_pref *pref, int format);

/* Append a codec to a preference list, removing it first if it was already there */
extern int ast_codec_pref_append(struct ast_codec_pref *pref, int format);

/* Select the best format according to preference list from supplied options. 
   If "find_best" is non-zero then if nothing is found, the "Best" format of 
   the format list is selected, otherwise 0 is returned. */
extern int ast_codec_choose(struct ast_codec_pref *pref, int formats, int find_best);

/* Parse an "allow" or "deny" line and update the mask and pref if provided */
extern void ast_parse_allow_disallow(struct ast_codec_pref *pref, int *mask, char *list, int allowing);

/* Dump codec preference list into a string */
extern int ast_codec_pref_string(struct ast_codec_pref *pref, char *buf, size_t size);

/* Shift a codec preference list up or down 65 bytes so that it becomes an ASCII string */
extern void ast_codec_pref_convert(struct ast_codec_pref *pref, char *buf, size_t size, int right);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif


#endif
