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
 * \brief Asterisk internal frame definitions.
 * \arg For an explanation of frames, see \ref Def_Frame
 * \arg Frames are send of Asterisk channels, see \ref Def_Channel
 */

#ifndef _ASTERISK_FRAME_H
#define _ASTERISK_FRAME_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <sys/time.h>

#include "asterisk/format.h"
#include "asterisk/endian.h"
#include "asterisk/linkedlists.h"

/*!
 * \page Def_Frame AST Multimedia and signalling frames
 * \section Def_AstFrame What is an ast_frame ?
 * A frame of data read used to communicate between
 * between channels and applications.
 * Frames are divided into frame types and subclasses.
 *
 * \par Frame types
 * \arg \b VOICE:  Voice data, subclass is codec (AST_FORMAT_*)
 * \arg \b VIDEO:  Video data, subclass is codec (AST_FORMAT_*)
 * \arg \b DTMF:   A DTMF digit, subclass is the digit
 * \arg \b IMAGE:  Image transport, mostly used in IAX
 * \arg \b TEXT:   Text messages and character by character (real time text)
 * \arg \b TEXT_DATA:   Text messages in an ast_msg_data structure
 * \arg \b HTML:   URLs and web pages
 * \arg \b MODEM:  Modulated data encodings, such as T.38 and V.150
 * \arg \b IAX:    Private frame type for the IAX protocol
 * \arg \b CNG:    Comfort noise frames
 * \arg \b CONTROL:A control frame, subclass defined as AST_CONTROL_
 * \arg \b NULL:   Empty, useless frame
 *
 * \par Files
 * \arg frame.h    Definitions
 * \arg frame.c    Function library
 * \arg \ref Def_Channel Asterisk channels
 * \section Def_ControlFrame Control Frames
 * Control frames send signalling information between channels
 * and devices. They are prefixed with AST_CONTROL_, like AST_CONTROL_FRAME_HANGUP
 * \arg \b HANGUP          The other end has hungup
 * \arg \b RING            Local ring
 * \arg \b RINGING         The other end is ringing
 * \arg \b ANSWER          The other end has answered
 * \arg \b BUSY            Remote end is busy
 * \arg \b TAKEOFFHOOK     Make it go off hook (what's "it" ? )
 * \arg \b OFFHOOK         Line is off hook
 * \arg \b CONGESTION      Congestion (circuit is busy, not available)
 * \arg \b FLASH           Other end sends flash hook
 * \arg \b WINK            Other end sends wink
 * \arg \b OPTION          Send low-level option
 * \arg \b RADIO_KEY       Key radio (see app_rpt.c)
 * \arg \b RADIO_UNKEY     Un-key radio (see app_rpt.c)
 * \arg \b PROGRESS        Other end indicates call progress
 * \arg \b PROCEEDING      Indicates proceeding
 * \arg \b HOLD            Call is placed on hold
 * \arg \b UNHOLD          Call is back from hold
 * \arg \b VIDUPDATE       Video update requested
 * \arg \b SRCUPDATE       The source of media has changed (RTP marker bit must change)
 * \arg \b SRCCHANGE       Media source has changed (RTP marker bit and SSRC must change)
 * \arg \b CONNECTED_LINE  Connected line has changed
 * \arg \b REDIRECTING     Call redirecting information has changed.
 */

/*!
 * \brief Frame types
 *
 * \note It is important that the values of each frame type are never changed,
 *       because it will break backwards compatability with older versions.
 *       This is because these constants are transmitted directly over IAX2.
 */
enum ast_frame_type {
	/*! DTMF end event, subclass is the digit */
	AST_FRAME_DTMF_END = 1,
	/*! Voice data, subclass is AST_FORMAT_* */
	AST_FRAME_VOICE,
	/*! Video frame, maybe?? :) */
	AST_FRAME_VIDEO,
	/*! A control frame, subclass is AST_CONTROL_* */
	AST_FRAME_CONTROL,
	/*! An empty, useless frame */
	AST_FRAME_NULL,
	/*! Inter Asterisk Exchange private frame type */
	AST_FRAME_IAX,
	/*! Text messages. The character data may not be zero-terminated, so
	 * care should be taken when passing it to functions that expect a
	 * zero-terminated string. The frame's datalen member should be used
	 * as it indicates the actual number of bytes available. */
	AST_FRAME_TEXT,
	/*! Image Frames */
	AST_FRAME_IMAGE,
	/*! HTML Frame */
	AST_FRAME_HTML,
	/*! Comfort Noise frame (subclass is level of CNG in -dBov),
	    body may include zero or more 8-bit quantization coefficients */
	AST_FRAME_CNG,
	/*! Modem-over-IP data streams */
	AST_FRAME_MODEM,
	/*! DTMF begin event, subclass is the digit */
	AST_FRAME_DTMF_BEGIN,
	/*! Internal bridge module action. */
	AST_FRAME_BRIDGE_ACTION,
	/*! Internal synchronous bridge module action.
	 * Synchronous bridge actions may be queued onto bridge
	 * channels, but they absolutely must not ever be written
	 * directly into bridges.
	 */
	AST_FRAME_BRIDGE_ACTION_SYNC,
	/*! RTCP feedback (the subclass will contain the payload type) */
	AST_FRAME_RTCP,
	/*! Text message in an ast_msg_data structure */
	AST_FRAME_TEXT_DATA,
};
#define AST_FRAME_DTMF AST_FRAME_DTMF_END

enum {
	/*! This frame contains valid timing information */
	AST_FRFLAG_HAS_TIMING_INFO = (1 << 0),
	/*! This frame has been requeued */
	AST_FRFLAG_REQUEUED = (1 << 1),
	/*! This frame contains a valid sequence number */
	AST_FRFLAG_HAS_SEQUENCE_NUMBER = (1 << 2),
};

struct ast_frame_subclass {
	/*! A frame specific code */
	int integer;
	union {
		/*! The asterisk media format */
		struct ast_format *format;
		/*! The asterisk stream topology */
		struct ast_stream_topology *topology;
	};
	/*! For video formats, an indication that a frame ended */
	unsigned int frame_ending;
};

/*! \brief Data structure associated with a single frame of data
 */
struct ast_frame {
	/*! Kind of frame */
	enum ast_frame_type frametype;
	/*! Subclass, frame dependent */
	struct ast_frame_subclass subclass;
	/*! Length of data */
	int datalen;
	/*! Number of samples in this frame */
	int samples;
	/*! Was the data malloc'd?  i.e. should we free it when we discard the frame? */
	int mallocd;
	/*! The number of bytes allocated for a malloc'd frame header */
	size_t mallocd_hdr_len;
	/*! How many bytes exist _before_ "data" that can be used if needed */
	int offset;
	/*! Optional source of frame for debugging */
	const char *src;
	/*! Pointer to actual data */
	union { void *ptr; uint32_t uint32; char pad[8]; } data;
	/*! Global delivery time */
	struct timeval delivery;
	/*! For placing in a linked list */
	AST_LIST_ENTRY(ast_frame) frame_list;
	/*! Misc. frame flags */
	unsigned int flags;
	/*! Timestamp in milliseconds */
	long ts;
	/*! Length in milliseconds */
	long len;
	/*! Sequence number */
	int seqno;
	/*! Stream number the frame originated from */
	int stream_num;
};

/*!
 * Set the various field of a frame to point to a buffer.
 * Typically you set the base address of the buffer, the offset as
 * AST_FRIENDLY_OFFSET, and the datalen as the amount of bytes queued.
 * The remaining things (to be done manually) is set the number of
 * samples, which cannot be derived from the datalen unless you know
 * the number of bits per sample.
 */
#define	AST_FRAME_SET_BUFFER(fr, _base, _ofs, _datalen)	\
	{					\
	(fr)->data.ptr = (char *)_base + (_ofs);	\
	(fr)->offset = (_ofs);			\
	(fr)->datalen = (_datalen);		\
	}

/*! Queueing a null frame is fairly common, so we declare a global null frame object
    for this purpose instead of having to declare one on the stack */
extern struct ast_frame ast_null_frame;

/*! \brief Offset into a frame's data buffer.
 *
 * By providing some "empty" space prior to the actual data of an ast_frame,
 * this gives any consumer of the frame ample space to prepend other necessary
 * information without having to create a new buffer.
 *
 * As an example, RTP can use the data from an ast_frame and simply prepend the
 * RTP header information into the space provided by AST_FRIENDLY_OFFSET instead
 * of having to create a new buffer with the necessary space allocated.
 */
#define AST_FRIENDLY_OFFSET 	64
#define AST_MIN_OFFSET 		32	/*! Make sure we keep at least this much handy */

/*! Need the header be free'd? */
#define AST_MALLOCD_HDR		(1 << 0)
/*! Need the data be free'd? */
#define AST_MALLOCD_DATA	(1 << 1)
/*! Need the source be free'd? (haha!) */
#define AST_MALLOCD_SRC		(1 << 2)

/* MODEM subclasses */
/*! T.38 Fax-over-IP */
#define AST_MODEM_T38		1
/*! V.150 Modem-over-IP */
#define AST_MODEM_V150		2

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

/*!
 * \brief Internal control frame subtype field values.
 *
 * \warning
 * IAX2 sends these values out over the wire.  To prevent future
 * incompatibilities, pick the next value in the enum from whatever
 * is on the current trunk.  If you lose the merge race you need to
 * fix the previous branches to match what is on trunk.  In addition
 * you need to change chan_iax2 to explicitly allow the control
 * frame over the wire if it makes sense for the frame to be passed
 * to another Asterisk instance.
 */
enum ast_control_frame_type {
	AST_CONTROL_HANGUP = 1,			/*!< Other end has hungup */
	AST_CONTROL_RING = 2,			/*!< Local ring */
	AST_CONTROL_RINGING = 3,		/*!< Remote end is ringing */
	AST_CONTROL_ANSWER = 4,			/*!< Remote end has answered */
	AST_CONTROL_BUSY = 5,			/*!< Remote end is busy */
	AST_CONTROL_TAKEOFFHOOK = 6,	/*!< Make it go off hook */
	AST_CONTROL_OFFHOOK = 7,		/*!< Line is off hook */
	AST_CONTROL_CONGESTION = 8,		/*!< Congestion (circuits busy) */
	AST_CONTROL_FLASH = 9,			/*!< Flash hook */
	AST_CONTROL_WINK = 10,			/*!< Wink */
	AST_CONTROL_OPTION = 11,		/*!< Set a low-level option */
	AST_CONTROL_RADIO_KEY = 12,		/*!< Key Radio */
	AST_CONTROL_RADIO_UNKEY = 13,	/*!< Un-Key Radio */
	AST_CONTROL_PROGRESS = 14,		/*!< Indicate PROGRESS */
	AST_CONTROL_PROCEEDING = 15,	/*!< Indicate CALL PROCEEDING */
	AST_CONTROL_HOLD = 16,			/*!< Indicate call is placed on hold */
	AST_CONTROL_UNHOLD = 17,		/*!< Indicate call is left from hold */
	AST_CONTROL_VIDUPDATE = 18,		/*!< Indicate video frame update */
	_XXX_AST_CONTROL_T38 = 19,		/*!< T38 state change request/notification \deprecated This is no longer supported. Use AST_CONTROL_T38_PARAMETERS instead. */
	AST_CONTROL_SRCUPDATE = 20,		/*!< Indicate source of media has changed */
	AST_CONTROL_TRANSFER = 21,		/*!< Indicate status of a transfer request */
	AST_CONTROL_CONNECTED_LINE = 22,/*!< Indicate connected line has changed */
	AST_CONTROL_REDIRECTING = 23,	/*!< Indicate redirecting id has changed */
	AST_CONTROL_T38_PARAMETERS = 24,/*!< T38 state change request/notification with parameters */
	AST_CONTROL_CC = 25,			/*!< Indication that Call completion service is possible */
	AST_CONTROL_SRCCHANGE = 26,		/*!< Media source has changed and requires a new RTP SSRC */
	AST_CONTROL_READ_ACTION = 27,	/*!< Tell ast_read to take a specific action */
	AST_CONTROL_AOC = 28,			/*!< Advice of Charge with encoded generic AOC payload */
	AST_CONTROL_END_OF_Q = 29,		/*!< Indicate that this position was the end of the channel queue for a softhangup. */
	AST_CONTROL_INCOMPLETE = 30,	/*!< Indication that the extension dialed is incomplete */
	AST_CONTROL_MCID = 31,			/*!< Indicate that the caller is being malicious. */
	AST_CONTROL_UPDATE_RTP_PEER = 32, /*!< Interrupt the bridge and have it update the peer */
	AST_CONTROL_PVT_CAUSE_CODE = 33, /*!< Contains an update to the protocol-specific cause-code stored for branching dials */
	AST_CONTROL_MASQUERADE_NOTIFY = 34,	/*!< A masquerade is about to begin/end. (Never sent as a frame but directly with ast_indicate_data().) */
	AST_CONTROL_STREAM_TOPOLOGY_REQUEST_CHANGE = 35,    /*!< Channel indication that a stream topology change has been requested */
	AST_CONTROL_STREAM_TOPOLOGY_CHANGED = 36,           /*!< Channel indication that a stream topology change has occurred */
	AST_CONTROL_STREAM_TOPOLOGY_SOURCE_CHANGED = 37,    /*!< Channel indication that one of the source streams has changed its source */

	/*
	 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
	 *
	 * IAX2 sends these values out over the wire.  To prevent future
	 * incompatibilities, pick the next value in the enum from whatever
	 * is on the current trunk.  If you lose the merge race you need to
	 * fix the previous branches to match what is on trunk.  In addition
	 * you need to change chan_iax2 to explicitly allow the control
	 * frame over the wire if it makes sense for the frame to be passed
	 * to another Asterisk instance.
	 *
	 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
	 */

	/* Control frames used to manipulate a stream on a channel. The values for these
	 * must be greater than the allowed value for a 8-bit char, so that they avoid
	 * conflicts with DTMF values. */
	AST_CONTROL_STREAM_STOP = 1000,		/*!< Indicate to a channel in playback to stop the stream */
	AST_CONTROL_STREAM_SUSPEND = 1001,	/*!< Indicate to a channel in playback to suspend the stream */
	AST_CONTROL_STREAM_RESTART = 1002,	/*!< Indicate to a channel in playback to restart the stream */
	AST_CONTROL_STREAM_REVERSE = 1003,	/*!< Indicate to a channel in playback to rewind */
	AST_CONTROL_STREAM_FORWARD = 1004,	/*!< Indicate to a channel in playback to fast forward */
	/* Control frames to manipulate recording on a channel. */
	AST_CONTROL_RECORD_CANCEL = 1100,	/*!< Indicated to a channel in record to stop recording and discard the file */
	AST_CONTROL_RECORD_STOP = 1101,	/*!< Indicated to a channel in record to stop recording */
	AST_CONTROL_RECORD_SUSPEND = 1102,	/*!< Indicated to a channel in record to suspend/unsuspend recording */
	AST_CONTROL_RECORD_MUTE = 1103,	/*!< Indicated to a channel in record to mute/unmute (i.e. write silence) recording */
};

/*!
 * \brief Actions to indicate to, and be handled on channel read
 *
 * The subtype to specify for an AST_CONTROL_READ_ACTION frame. These
 * frames are then to be enacted on within a channel's read thread.
 */
enum ast_frame_read_action {
	AST_FRAME_READ_ACTION_CONNECTED_LINE_MACRO,
	AST_FRAME_READ_ACTION_SEND_TEXT,
	AST_FRAME_READ_ACTION_SEND_TEXT_DATA,
};

struct ast_control_read_action_payload {
	/* An indicator to ast_read of what action to
	 * take with the frame;
	 */
	enum ast_frame_read_action action;
	/* The size of the frame's payload
	 */
	size_t payload_size;
	/* A payload for the frame.
	 */
	unsigned char payload[0];
};

enum ast_control_t38 {
	AST_T38_REQUEST_NEGOTIATE = 1,	/*!< Request T38 on a channel (voice to fax) */
	AST_T38_REQUEST_TERMINATE,	/*!< Terminate T38 on a channel (fax to voice) */
	AST_T38_NEGOTIATED,		/*!< T38 negotiated (fax mode) */
	AST_T38_TERMINATED,		/*!< T38 terminated (back to voice) */
	AST_T38_REFUSED,		/*!< T38 refused for some reason (usually rejected by remote end) */
	AST_T38_REQUEST_PARMS,		/*!< request far end T.38 parameters for a channel in 'negotiating' state */
};

enum ast_control_t38_rate {
	AST_T38_RATE_2400 = 1,
	AST_T38_RATE_4800,
	AST_T38_RATE_7200,
	AST_T38_RATE_9600,
	AST_T38_RATE_12000,
	/* Set to 0 so it's taken as default when unspecified.
	 * See ITU-T T.38 Implementors' Guide (11 May 2012),
	 * Table H.2: if the T38MaxBitRate attribute is omitted
	 * it should use a default of 14400. */
	AST_T38_RATE_14400 = 0,
};

enum ast_control_t38_rate_management {
	AST_T38_RATE_MANAGEMENT_TRANSFERRED_TCF = 0,
	AST_T38_RATE_MANAGEMENT_LOCAL_TCF,
};

struct ast_control_t38_parameters {
	enum ast_control_t38 request_response;			/*!< Request or response of the T38 control frame */
	unsigned int version;					/*!< Supported T.38 version */
	unsigned int max_ifp; 					/*!< Maximum IFP size supported */
	enum ast_control_t38_rate rate;				/*!< Maximum fax rate supported */
	enum ast_control_t38_rate_management rate_management;	/*!< Rate management setting */
	unsigned int fill_bit_removal:1;			/*!< Set if fill bit removal can be used */
	unsigned int transcoding_mmr:1;				/*!< Set if MMR transcoding can be used */
	unsigned int transcoding_jbig:1;			/*!< Set if JBIG transcoding can be used */
};

enum ast_control_transfer {
	AST_TRANSFER_SUCCESS = 0, /*!< Transfer request on the channel worked */
	AST_TRANSFER_FAILED,      /*!< Transfer request on the channel failed */
};

struct ast_control_pvt_cause_code {
	char chan_name[AST_CHANNEL_NAME];	/*!< Name of the channel that originated the cause information */
	unsigned int emulate_sip_cause:1;	/*!< Indicates whether this should be used to emulate SIP_CAUSE support */
	int ast_cause;				/*!< Asterisk cause code associated with this message */
	char code[1];				/*!< Tech-specific cause code information, beginning with the name of the tech */
};

/* Option identifiers and flags */
#define AST_OPTION_FLAG_REQUEST		0
#define AST_OPTION_FLAG_ACCEPT		1
#define AST_OPTION_FLAG_REJECT		2
#define AST_OPTION_FLAG_QUERY		4
#define AST_OPTION_FLAG_ANSWER		5
#define AST_OPTION_FLAG_WTF		6

/*! Verify touchtones by muting audio transmission
 * (and reception) and verify the tone is still present
 * Option data is a single signed char value 0 or 1 */
#define AST_OPTION_TONE_VERIFY		1

/*! Put a compatible channel into TDD (TTY for the hearing-impaired) mode
 * Option data is a single signed char value 0 or 1 */
#define	AST_OPTION_TDD			2

/*! Relax the parameters for DTMF reception (mainly for radio use)
 * Option data is a single signed char value 0 or 1 */
#define	AST_OPTION_RELAXDTMF		3

/*! Set (or clear) Audio (Not-Clear) Mode
 * Option data is a single signed char value 0 or 1 */
#define	AST_OPTION_AUDIO_MODE		4

/*! Set channel transmit gain
 * Option data is a single signed char representing number of decibels (dB)
 * to set gain to (on top of any gain specified in channel driver) */
#define AST_OPTION_TXGAIN		5

/*! Set channel receive gain
 * Option data is a single signed char representing number of decibels (dB)
 * to set gain to (on top of any gain specified in channel driver) */
#define AST_OPTION_RXGAIN		6

/* set channel into "Operator Services" mode
 * Option data is a struct oprmode
 *
 * \note This option should never be sent over the network */
#define	AST_OPTION_OPRMODE		7

/*! Explicitly enable or disable echo cancelation for the given channel
 * Option data is a single signed char value 0 or 1
 *
 * \note This option appears to be unused in the code. It is handled, but never
 * set or queried. */
#define	AST_OPTION_ECHOCAN		8

/*! \brief Handle channel write data
 * If a channel needs to process the data from a func_channel write operation
 * after func_channel_write executes, it can define the setoption callback
 * and process this option. A pointer to an ast_chan_write_info_t will be passed.
 *
 * \note This option should never be passed over the network. */
#define AST_OPTION_CHANNEL_WRITE 9

/* !
 * Read-only. Allows query current status of T38 on the channel.
 * data: ast_t38state
 */
#define AST_OPTION_T38_STATE		10

/*! Request that the channel driver deliver frames in a specific format
 * Option data is a format_t */
#define AST_OPTION_FORMAT_READ          11

/*! Request that the channel driver be prepared to accept frames in a specific format
 * Option data is a format_t */
#define AST_OPTION_FORMAT_WRITE         12

/*! Request that the channel driver make two channels of the same tech type compatible if possible
 * Option data is an ast_channel
 *
 * \note This option should never be passed over the network */
#define AST_OPTION_MAKE_COMPATIBLE      13

/*! Get or set the digit detection state of the channel
 * Option data is a single signed char value 0 or 1 */
#define AST_OPTION_DIGIT_DETECT		14

/*! Get or set the fax tone detection state of the channel
 * Option data is a single signed char value 0 or 1 */
#define AST_OPTION_FAX_DETECT		15

/*! Get the device name from the channel (Read only)
 * Option data is a character buffer of suitable length */
#define AST_OPTION_DEVICE_NAME		16

/*! Get the CC agent type from the channel (Read only)
 * Option data is a character buffer of suitable length */
#define AST_OPTION_CC_AGENT_TYPE    17

/*! Get or set the security options on a channel
 * Option data is an integer value of 0 or 1 */
#define AST_OPTION_SECURE_SIGNALING        18
#define AST_OPTION_SECURE_MEDIA            19

struct oprmode {
	struct ast_channel *peer;
	int mode;
} ;

struct ast_option_header {
	/* Always keep in network byte order */
#if __BYTE_ORDER == __BIG_ENDIAN
	uint16_t flag:3;
	uint16_t option:13;
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
	uint16_t option:13;
	uint16_t flag:3;
#else
#error Byte order not defined
#endif
#endif
		uint8_t data[0];
};

#if 0 /* Unimplemented */
/*! \brief  Requests a frame to be allocated
 *
 * \param source, len
 * Request a frame be allocated.  source is an optional source of the frame,
 * len is the requested length, or "0" if the caller will supply the buffer
 */
struct ast_frame *ast_fralloc(char *source, int len);
#endif

/*!
 * \brief Frees a frame or list of frames
 *
 * \param frame Frame to free, or head of list to free
 * \param cache Whether to consider this frame for frame caching
 */
void ast_frame_free(struct ast_frame *frame, int cache);

#define ast_frfree(fr) ast_frame_free(fr, 1)

/*!
 * \brief NULL-safe wrapper for \ref ast_frfree, good for \ref RAII_VAR.
 * \param frame Frame to free, or head of list to free.
 */
void ast_frame_dtor(struct ast_frame *frame);

/*! \brief Makes a frame independent of any static storage
 * \param fr frame to act upon
 * Take a frame, and if it's not been malloc'd, make a malloc'd copy
 * and if the data hasn't been malloced then make the
 * data malloc'd.  If you need to store frames, say for queueing, then
 * you should call this function.
 * \return Returns a frame on success, NULL on error
 * \note This function may modify the frame passed to it, so you must
 * not assume the frame will be intact after the isolated frame has
 * been produced. In other words, calling this function on a frame
 * should be the last operation you do with that frame before freeing
 * it (or exiting the block, if the frame is on the stack.)
 */
#define ast_frisolate(fr) __ast_frisolate(fr, __FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_frame *__ast_frisolate(struct ast_frame *fr, const char *file, int line, const char *func);

/*! \brief Copies a frame
 * \param fr frame to copy
 * Duplicates a frame -- should only rarely be used, typically frisolate is good enough
 * \return Returns a frame on success, NULL on error
 */
#define ast_frdup(fr) __ast_frdup(fr, __FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_frame *__ast_frdup(const struct ast_frame *fr, const char *file, int line, const char *func);

void ast_swapcopy_samples(void *dst, const void *src, int samples);

/* Helpers for byteswapping native samples to/from
   little-endian and big-endian. */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ast_frame_byteswap_le(fr) do { ; } while(0)
#define ast_frame_byteswap_be(fr) do { struct ast_frame *__f = (fr); ast_swapcopy_samples(__f->data.ptr, __f->data.ptr, __f->samples); } while(0)
#else
#define ast_frame_byteswap_le(fr) do { struct ast_frame *__f = (fr); ast_swapcopy_samples(__f->data.ptr, __f->data.ptr, __f->samples); } while(0)
#define ast_frame_byteswap_be(fr) do { ; } while(0)
#endif

void ast_frame_dump(const char *name, struct ast_frame *f, char *prefix);

/*! \brief Appends a frame to the end of a list of frames, truncating the maximum length of the list */
struct ast_frame *ast_frame_enqueue(struct ast_frame *head, struct ast_frame *f, int maxlen, int dupe);

/*!
  \brief Adjusts the volume of the audio samples contained in a frame.
  \param f The frame containing the samples (must be AST_FRAME_VOICE and AST_FORMAT_SLINEAR)
  \param adjustment The number of dB to adjust up or down.
  \return 0 for success, non-zero for an error
 */
int ast_frame_adjust_volume(struct ast_frame *f, int adjustment);

/*!
  \brief Adjusts the volume of the audio samples contained in a frame.
  \param f The frame containing the samples (must be AST_FRAME_VOICE and AST_FORMAT_SLINEAR)
  \param adjustment The number of dB to adjust up or down.
  \return 0 for success, non-zero for an error
 */
int ast_frame_adjust_volume_float(struct ast_frame *f, float adjustment);

/*!
  \brief Sums two frames of audio samples.
  \param f1 The first frame (which will contain the result)
  \param f2 The second frame
  \return 0 for success, non-zero for an error

  The frames must be AST_FRAME_VOICE and must contain AST_FORMAT_SLINEAR samples,
  and must contain the same number of samples.
 */
int ast_frame_slinear_sum(struct ast_frame *f1, struct ast_frame *f2);

/*!
 * \brief Clear all audio samples from an ast_frame. The frame must be AST_FRAME_VOICE and AST_FORMAT_SLINEAR
 */
int ast_frame_clear(struct ast_frame *frame);

/*!
 * \brief Copy the discription of a frame's subclass into the provided string
 *
 * \param f The frame to get the information from
 * \param subclass Buffer to fill with subclass information
 * \param slen Length of subclass buffer
 * \param moreinfo Buffer to fill with additional information
 * \param mlen Length of moreinfo buffer
 * \return Pointer to subclass
 * \since 11
 */
char *ast_frame_subclass2str(struct ast_frame *f, char *subclass, size_t slen, char *moreinfo, size_t mlen);

/*!
 * \brief Copy the discription of a frame type into the provided string
 *
 * \param frame_type The frame type to be described
 * \param ftype Buffer to fill with frame type description
 * \param len Length of subclass buffer
 * \return Pointer to ftype
 * \since 11
 */
char *ast_frame_type2str(enum ast_frame_type frame_type, char *ftype, size_t len);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_FRAME_H */
