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
 * FSK Modem Support 
 * Includes code and algorithms from the Zapata library.
 */

#ifndef _ASTERISK_FSKMODEM_H
#define _ASTERISK_FSKMODEM_H

#define PARITY_NONE		0
#define PARITY_EVEN		1
#define PARITY_ODD		2


#define NCOLA 0x4000

typedef struct {
	float spb;	/* Samples / Bit */
	int nbit;	/* Number of Data Bits (5,7,8) */
	float nstop;	/* Number of Stop Bits 1,1.5,2  */
	int paridad;	/* Parity 0=none 1=even 2=odd */
	int hdlc;	/* Modo Packet */
	float x0;
	float x1;
	float x2;
	float cont;
	int bw;		/* Ancho de Banda */
	double fmxv[8],fmyv[8];	/* filter stuff for M filter */
	int	fmp;		/* pointer for M filter */
	double fsxv[8],fsyv[8];	/* filter stuff for S filter */
	int	fsp;		/* pointer for S filter */
	double flxv[8],flyv[8];	/* filter stuff for L filter */
	int	flp;		/* pointer for L filter */
	int f_mark_idx;	/* Indice de frecuencia de marca (f_M-500)/5 */
	int f_space_idx;/* Indice de frecuencia de espacio (f_S-500)/5 */
	int state;
	int pcola;	/* Puntero de las colas de datos */
	float cola_in[NCOLA];		/* Cola de muestras de entrada */
	float cola_filtro[NCOLA];	/* Cola de muestras tras filtros */
	float cola_demod[NCOLA];	/* Cola de muestras demoduladas */
} fsk_data;

/* Retrieve a serial byte into outbyte.  Buffer is a pointer into a series of 
   shorts and len records the number of bytes in the buffer.  len will be 
   overwritten with the number of bytes left that were not consumed, and the
   return value is as follows:
     0: Still looking for something...  
	 1: An output byte was received and stored in outbyte
	 -1: An error occured in the transmission
   He must be called with at least 80 bytes of buffer. */
extern int fsk_serie(fsk_data *fskd, short *buffer, int *len, int *outbyte);

#endif /* _ASTERISK_FSKMODEM_H */
