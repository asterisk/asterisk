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

/*
 * TTY/TDD Generation support 
 * Includes code and algorithms from the Zapata library.
 */

#ifndef _ASTERISK_TDD_H
#define _ASTERISK_TDD_H

#define	TDD_BYTES_PER_CHAR	2700

struct tdd_state;
typedef struct tdd_state TDDSTATE;

/*! CallerID Initialization */
/*!
 * Initializes the TDD system.  Mostly stuff for inverse FFT
 */
extern void tdd_init(void);

/*! Generates a CallerID FSK stream in ulaw format suitable for transmission. */
/*!
 * \param buf Buffer to use. This needs to be large enough to accomodate all the generated samples.
 * \param string This is the string to send.
 * This function creates a stream of TDD data in ulaw format. It returns the size
 * (in bytes) of the data (if it returns a size of 0, there is probably an error)
*/
extern int tdd_generate(struct tdd_state *tdd, unsigned char *buf, const char *string);

/*! Create a TDD state machine */
/*!
 * This function returns a malloc'd instance of the tdd_state data structure.
 * Returns a pointer to a malloc'd tdd_state structure, or NULL on error.
 */
extern struct tdd_state *tdd_new(void);

/*! Read samples into the state machine, and return character (if any). */
/*!
 * \param tdd Which state machine to act upon
 * \param buffer containing your samples
 * \param samples number of samples contained within the buffer.
 *
 * Send received audio to the TDD demodulator.
 * Returns -1 on error, 0 for "needs more samples", 
 * and > 0 (the character) if reception of a character is complete.
 */
extern int tdd_feed(struct tdd_state *tdd, unsigned char *ubuf, int samples);

/*! Free a TDD state machine */
/*!
 * \param tdd This is the tdd_state state machine to free
 * This function frees tdd_state tdd.
 */
extern void tdd_free(struct tdd_state *tdd);

/*! Generate Echo Canceller diable tone (2100HZ) */
/*!
 * \param outbuf This is the buffer to receive the tone data
 * \param len This is the length (in samples) of the tone data to generate
 * Returns 0 if no error, and -1 if error.
 */
extern int ast_tdd_gen_ecdisa(unsigned char *outbuf, int len);

#endif /* _ASTERISK_TDD_H */
