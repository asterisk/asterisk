/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * ADSI Support (built upon Caller*ID) 
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 * Includes code and algorithms from the Zapata library.
 *
 */

#ifndef _ADSI_H
#define _ADSI_H
#include <asterisk/callerid.h>

/* ADSI Message types */
#define ADSI_MSG_DISPLAY	132
#define ADSI_MSG_DOWNLOAD	133

/* ADSI Parameters (display) */
#define ADSI_LOAD_SOFTKEY	128
#define ADSI_INIT_SOFTKEY_LINE	129
#define ADSI_LOAD_VIRTUAL_DISP	130
#define ADSI_LINE_CONTROL	131
#define ADSI_INFORMATION	132
#define ADSI_DISC_SESSION	133
#define ADSI_SWITCH_TO_DATA	134
#define ADSI_SWITCH_TO_VOICE	135
#define ADSI_CLEAR_SOFTKEY	136
#define ADSI_INPUT_CONTROL	137
#define ADSI_INPUT_FORMAT	138
#define ADSI_SWITCH_TO_PERIPH	139
#define ADSI_MOVE_DATA		140
#define ADSI_LOAD_DEFAULT	141
#define ADSI_CONNECT_SESSION	142
#define ADSI_CLEAR_TYPE_AHEAD	143
#define ADSI_DISPLAY_CALL_BUF	144
#define ADSI_CLEAR_CALL_BUF	145
#define ADSI_SWITCH_TO_ALT	146
#define ADSI_SWITCH_TO_GRAPHICS	147
#define ADSI_CLEAR_SCREEN	148
#define ADSI_QUERY_CONFIG	149
#define ADSI_QUERY_CPEID	150
#define ADSI_SWITCH_TO_APP	151

/* Feature download messages */
#define ADSI_LOAD_SOFTKEY_TABLE	128	/* Conveniently identical to the soft version */
#define ADSI_LOAD_PREDEF_DISP	129	/* Load predefined display */
#define ADSI_LOAD_SCRIPT	130
#define ADSI_DOWNLOAD_CONNECT	131
#define ADSI_DOWNLOAD_DISC	132

/* Special return string codes */
#define ADSI_ENCODED_DTMF	0x80	/* Transmit following chars with encoded dtmf */
#define ADSI_ON_HOOK		0x81	/* Open switch-hook */
#define ADSI_OFF_HOOK		0x82	/* Close switch-hook */
#define ADSI_FLASH		0x83	/* Flash switch-hook */
#define ADSI_DIAL_TONE_DETECT	0x84	/* Wait for dialtone */
#define ADSI_LINE_NUMBER	0x85	/* Send current line number using DTMF/encoded DTMF */
#define ADSI_BLANK		0x86	/* Blank (does nothing) */
#define ADSI_SEND_CHARS		0x87	/* Send collected digits/characters */
#define ADSI_CLEAR_CHARS	0x88	/* Clear characters/digits collected */
#define ADSI_BACKSPACE		0x89	/* Erase last collected digit */
#define ADSI_TAB_COLUMN		0x8A	/* Display specified display column of current line */
#define ADSI_GOTO_LINE		0x8B	/* Go to given page and line number */
#define ADSI_GOTO_LINE_REL	0x8C	/* Go to given line (relative to current) */
#define ADSI_PAGE_UP		0x8D	/* Go up one page */
#define ADSI_PAGE_DOWN		0x8E	/* Go down one page */
#define ADSI_EXTENDED_DTMF	0x8F	/* Send DTMF tones for 250ms instead of 60 ms */
#define ADSI_DELAY		0x90	/* Delay for given # (times 10) of ms */
#define ADSI_DIAL_PULSE_ONE	0x91	/* Send a dial pulse "1" */
#define ADSI_SWITCH_TO_DATA2	0x92	/* Switch CPE to data mode */
#define ADSI_SWITCH_TO_VOICE2	0x93	/* Switch CPE to voice mode */
#define ADSI_DISP_CALL_BUF	0x94	/* Display specified call buffer */
#define ADSI_CLEAR_CALL_B	0x95	/* Clear specified call buffer */

#ifdef __ADSI_CPE
/* These messages are reserved for the ADSI CPE only */
#define ADSI_DISPLAY_CONTROL	0x98	/* Store predefined display identified next / Display status display page */
#define ADSI_DISPLAY_SOFT_KEYS	0x99	/* Display the script soft keys identified next */
#define ADSI_CHANGE_STATE	0x9A	/* Change state of service script */
#define ADSI_START_CLEAR_TIMER	0x9B	/* Start / Clear timer */
#define ADSI_SET_SCRIPT_FLAG	0x9C	/* Set / clear a script flag */
#define ADSI_JUMP_TO_SUBSCRIPT	0x9D	/* Jump to specified subscript */
#define ADSI_EVENT_22_TRIGGER	0x9E	/* Trigger an occurance of event 22 */
#define ADSI_EVENT_23_TRIGGER	0x9f	/* Trigger an occurance of event 23 */
#define ADSI_EXIT		0xA0	/* Exit the service script interpreter */
#endif

/* Display pages */
#define ADSI_INFO_PAGE	0x0
#define ADSI_COMM_PAGE	0x1

#define ADSI_KEY_APPS	16	/* 16 to 33 reserved for applications */

/* Justification */
#define ADSI_JUST_LEFT	0x2
#define ADSI_JUST_RIGHT 0x1
#define ADSI_JUST_CENT  0x0	/* Center */
#define ADSI_JUST_IND	0x3	/* Indent */

#define ADSI_KEY_SKT	0x80	/* Load from SKT */
#define ADSI_KEY_HILITE	0x40	/* Highlight key */

#define ADSI_DIR_FROM_LEFT (0)
#define ADSI_DIR_FROM_RIGHT (1)

//! Perform Asterisk ADSI initialization (for channel drivers that want
//   to support ADSI when the handset is first lifted) */
/*! 
 * \param chan Channel to initialize for ADSI (if supported)
 * 
 * Returns 0 on success (or adsi unavailable) and -1 on hangup
 * 
 */
extern int adsi_channel_init(struct ast_channel *chan);

extern int adsi_begin_download(struct ast_channel *chan, char *service, char *fdn, char *sec, int version);

extern int adsi_end_download(struct ast_channel *chan);

//! Restore ADSI initialization (for applications that play with ADSI
//  and want to restore it to normal.  If you touch "INFO" then you
//  have to use the adsi_channel_init again instead.
/*!
 * \param chan Channel to restore
 * 
 * Returns 0 on success (or adsi unavailable) and -1 on hangup
 *
 */
extern int adsi_channel_restore(struct ast_channel *chan);

//! Display some stuff on the screen
/*!
 * \param chan Channel to display on
 * \param lines NULL-terminated list of things to print (no more than 4 recommended)
 * \param align list of alignments to use (ADSI_JUST_LEFT, ADSI_JUST_RIGHT, ADSI_JUST_CEN, etc..)
 * \param voice whether to jump into voice mode when finished
 *
 * Return 0 on success (or adsi unavailable) and -1 on hangup
 *
 */
extern int adsi_print(struct ast_channel *chan, char **lines, int *align, int voice);

//! Check if scripts for a given app are already loaded.  Version may be -1
//  if any version is okay, or 0-255 for a specific version.
/*!
 * \param chan Channel to test for loaded app
 * \param app Four character app name (must be unique to your application)
 * \param ver optional version number
 * \param data Non-zero if you want to be put in data mode
 *
 * Returns 0 if scripts is not loaded or not an ADSI CPE.  Returns -1
 * on hangup.  Returns 1 if script already loaded.
 */
extern int adsi_load_session(struct ast_channel *chan, unsigned char *app, int ver, int data);
extern int adsi_unload_session(struct ast_channel *chan);

/* ADSI Layer 2 transmission functions */
extern int adsi_transmit_messages(struct ast_channel *chan, unsigned char **msg, int *msglen, int *msgtype);
extern int adsi_transmit_message(struct ast_channel *chan, unsigned char *msg, int msglen, int msgtype);

//! Read some encoded DTMF data.  
/*!
 * Returns number of bytes received
 */
extern int adsi_read_encoded_dtmf(struct ast_channel *chan, unsigned char *buf, int maxlen);

/* ADSI Layer 3 creation functions */

//! Connects an ADSI Display Session */
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 * \param fdn Optional 4 byte Feature Download Number (for loading soft keys)
 * \param ver Optional version number (0-255, or -1 to omit)
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */

extern int adsi_connect_session(unsigned char *buf, unsigned char *fdn, int ver);

//! Build Query CPE ID of equipment */
/*!
 *  Returns number of bytes added to message
 */
extern int adsi_query_cpeid(unsigned char *buf);
extern int adsi_query_cpeinfo(unsigned char *buf);

//! Get CPE ID from an attached ADSI compatible CPE.
/*!
 * Returns 1 on success, storing 4 bytes of CPE ID at buf
 * or -1 on hangup, or 0 if there was no hangup but it failed to find the
 * device ID.  Returns to voice mode if "voice" is non-zero.
 */
extern int adsi_get_cpeid(struct ast_channel *chan, unsigned char *cpeid, int voice);

extern int adsi_get_cpeinfo(struct ast_channel *chan, int *width, int *height, int *buttons, int voice);

//! Begin an ADSI script download */
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 * \param service, a 1-18 byte name of the feature
 * \param fdn 4 byte Feature Download Number (for loading soft keys)
 * \param sec 4 byte vendor security code
 * \param ver version number (0-255, or -1 to omit)
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */

extern int adsi_download_connect(unsigned char *buf, unsigned char *service, unsigned char *fdn, unsigned char *sec, int ver);

//! Disconnects a running session
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */
extern int adsi_disconnect_session(unsigned char *buf);

//! Disconnects (and hopefully saves) a downloaded script
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */
extern int adsi_download_disconnect(unsigned char *buf);

//! Puts CPE in data mode...
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */
extern int adsi_data_mode(unsigned char *buf);
extern int adsi_clear_soft_keys(unsigned char *buf);
extern int adsi_clear_screen(unsigned char *buf);

//! Puts CPE in voice mode...
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 * \param when (a time in seconds) to make the switch
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */
extern int adsi_voice_mode(unsigned char *buf, int when);

//! Returns non-zero if Channel does or might support ADSI
/*!
 * \param chan Channel to check
 *
 */
extern int adsi_available(struct ast_channel *chan);

//! Loads a line of info into the display */
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 * \param page Page to load (ADSI_COMM_PAGE or ADSI_INFO_PAGE)
 * \param line Line number to load (1-4 for Comm page, 1-33 for info page)
 * \param just Line justification (ADSI_JUST_LEFT, ADSI_JUST_RIGHT, ADSI_JUST_CENT, ADSI_JUST_IND)
 * \param wrap Wrap (1 = yes, 0 = no)
 * \param col1 Text to place in first column
 * \param col2 Text to place in second column
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */

extern int adsi_display(unsigned char *buf, int page, int line, int just, int wrap, unsigned char *col1, unsigned char *col2);

//! Sets the current line and page */
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 * \param page Which page (ADSI_COMM_PAGE or ADSI_INFO_PAGE)
 * \param line Line number (1-33 for info page, 1-4 for comm page)
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */

extern int adsi_set_line(unsigned char *buf, int page, int line);

//! Creates "load soft key" parameters
/*! 
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 * \param key Key code from 2 to 33, for which key we are loading
 * \param llabel Long label for key (1-18 bytes)
 * \param slabel Short label for key (1-7 bytes)
 * \param ret Optional return sequence (NULL for none)
 * \param data whether to put CPE in data mode before sending digits
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */
extern int adsi_load_soft_key(unsigned char *buf, int key, unsigned char *llabel, unsigned char *slabel, unsigned char *ret, int data);

//! Set which soft keys should be displayed
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 * \param keys Array of 8 unsigned chars with the key numbers, may be OR'd with ADSI_KEY_HILITE
 *             But remember, the last two keys aren't real keys, they're for scrolling
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */
extern int adsi_set_keys(unsigned char *buf, unsigned char *keys);

//! Set input information
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 * \param page Which page to input on (ADSI_COMM_PAGE or ADSI_INFO_PAGE)
 * \param line Line number to input on
 * \param display Set to zero to obscure input, or 1 to leave visible
 * \param format Format number to use (0-7)
 * \param just Justification (left, right center, indent)
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */
extern int adsi_input_control(unsigned char *buf, int page, int line, int display, int format, int just);

//! Set input format
/*!
 * \param buf Character buffer to create parameter in (must have at least 256 free)
 * \param num Which format we are setting
 * \param dir Which direction (ADSI_DIR_FROM_LEFT or ADSI_DIR_FROM_RIGHT)
 * \param wrap Set to 1 to permit line wrap, or 0 if not
 * \param format1 Format for column 1
 * \param format2 Format for column 2
 *
 * Returns number of bytes added to buffer or -1 on error.
 *
 */
extern int adsi_input_format(unsigned char *buf, int num, int dir, int wrap, unsigned char *format1, unsigned char *format2);
#endif
