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
 * \brief FSK Modem Support
 * \note Includes code and algorithms from the Zapata library.
 */

#ifndef _ASTERISK_FSKMODEM_H
#define _ASTERISK_FSKMODEM_H

#define PARITY_NONE		0
#define PARITY_EVEN		1
#define PARITY_ODD		2


#define NCOLA 0x4000

/* new filter structure */
struct filter_struct {

        int icoefs[8];
        int ip;
        int ixv[8];
        int iyv[8];
};

typedef struct {
	int nbit;	/*!< Number of Data Bits (5,7,8) */
	int parity;	/*!< Parity 0=none 1=even 2=odd */
	int instop;	/*!< Number of Stop Bits  */
	int hdlc;	/*!< Modo Packet */
	int xi0;
	int xi1;
	int xi2;

	int ispb;
	int icont;
	int bw;				/*!< Band Selector*/
	int f_mark_idx;			/*!< Mark Frequency Index (f_M-500)/5 */
	int f_space_idx;		/*!< Space Frequency Index (f_S-500)/5 */
	int state;

	int pllispb;		/*!<Pll autosense */
	int pllids;
	int pllispb2;

	struct filter_struct mark_filter;
	struct filter_struct space_filter;
	struct filter_struct demod_filter;

} fsk_data;

int fsk_serial(fsk_data *fskd, short *buffer, int *len, int *outbyte);
int fskmodem_init(fsk_data *fskd);

#endif /* _ASTERISK_FSKMODEM_H */
