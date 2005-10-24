/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * 
 * Includes code and algorithms from the Zapata library.
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
 *
 * \brief FSK Modulator/Demodulator 
 *
 */

#include <stdio.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/fskmodem.h"

#define NBW	2
#define BWLIST	{75,800}
#define	NF	6
#define	FLIST {1400,1800,1200,2200,1300,2100}

#define STATE_SEARCH_STARTBIT	0
#define STATE_SEARCH_STARTBIT2	1
#define STATE_SEARCH_STARTBIT3	2
#define STATE_GET_BYTE			3

static inline float get_sample(short **buffer, int *len)
{
	float retval;
	retval = (float) **buffer / 256;
	(*buffer)++;
	(*len)--;
	return retval;
}

#define GET_SAMPLE get_sample(&buffer, len)

/* Coeficientes para filtros de entrada					*/
/* Tabla de coeficientes, generada a partir del programa "mkfilter"	*/
/* Formato: coef[IDX_FREC][IDX_BW][IDX_COEF]				*/
/* IDX_COEF=0	=>	1/GAIN						*/
/* IDX_COEF=1-6	=>	Coeficientes y[n]				*/

static double coef_in[NF][NBW][8]={
#include "coef_in.h"
};

/* Coeficientes para filtro de salida					*/
/* Tabla de coeficientes, generada a partir del programa "mkfilter"	*/
/* Formato: coef[IDX_BW][IDX_COEF]					*/
/* IDX_COEF=0	=>	1/GAIN						*/
/* IDX_COEF=1-6	=>	Coeficientes y[n]				*/

static double coef_out[NBW][8]={
#include "coef_out.h"
};


/* Filtro pasa-banda para frecuencia de MARCA */
static inline float filtroM(fsk_data *fskd,float in)
{
	int i,j;
	double s;
	double *pc;
	
	pc=&coef_in[fskd->f_mark_idx][fskd->bw][0];
	fskd->fmxv[(fskd->fmp+6)&7]=in*(*pc++);
	
	s=(fskd->fmxv[(fskd->fmp+6)&7] - fskd->fmxv[fskd->fmp]) + 3 * (fskd->fmxv[(fskd->fmp+2)&7] - fskd->fmxv[(fskd->fmp+4)&7]);
	for (i=0,j=fskd->fmp;i<6;i++,j++) s+=fskd->fmyv[j&7]*(*pc++);
	fskd->fmyv[j&7]=s;
	fskd->fmp++; fskd->fmp&=7;
	return s;
}

/* Filtro pasa-banda para frecuencia de ESPACIO */
static inline float filtroS(fsk_data *fskd,float in)
{
	int i,j;
	double s;
	double *pc;
	
	pc=&coef_in[fskd->f_space_idx][fskd->bw][0];
	fskd->fsxv[(fskd->fsp+6)&7]=in*(*pc++);
	
	s=(fskd->fsxv[(fskd->fsp+6)&7] - fskd->fsxv[fskd->fsp]) + 3 * (fskd->fsxv[(fskd->fsp+2)&7] - fskd->fsxv[(fskd->fsp+4)&7]);
	for (i=0,j=fskd->fsp;i<6;i++,j++) s+=fskd->fsyv[j&7]*(*pc++);
	fskd->fsyv[j&7]=s;
	fskd->fsp++; fskd->fsp&=7;
	return s;
}

/* Filtro pasa-bajos para datos demodulados */
static inline float filtroL(fsk_data *fskd,float in)
{
	int i,j;
	double s;
	double *pc;
	
	pc=&coef_out[fskd->bw][0];
	fskd->flxv[(fskd->flp + 6) & 7]=in * (*pc++); 
	
	s=     (fskd->flxv[fskd->flp]       + fskd->flxv[(fskd->flp+6)&7]) +
	  6  * (fskd->flxv[(fskd->flp+1)&7] + fskd->flxv[(fskd->flp+5)&7]) +
	  15 * (fskd->flxv[(fskd->flp+2)&7] + fskd->flxv[(fskd->flp+4)&7]) +
	  20 *  fskd->flxv[(fskd->flp+3)&7]; 
	
	for (i=0,j=fskd->flp;i<6;i++,j++) s+=fskd->flyv[j&7]*(*pc++);
	fskd->flyv[j&7]=s;
	fskd->flp++; fskd->flp&=7;
	return s;
}

static inline int demodulador(fsk_data *fskd, float *retval, float x)
{
	float xS,xM;

	fskd->cola_in[fskd->pcola]=x;
	
	xS=filtroS(fskd,x);
	xM=filtroM(fskd,x);

	fskd->cola_filtro[fskd->pcola]=xM-xS;

	x=filtroL(fskd,xM*xM - xS*xS);
	
	fskd->cola_demod[fskd->pcola++]=x;
	fskd->pcola &= (NCOLA-1);

	*retval = x;
	return(0);
}

static int get_bit_raw(fsk_data *fskd, short *buffer, int *len)
{
	/* Esta funcion implementa un DPLL para sincronizarse con los bits */
	float x,spb,spb2,ds;
	int f;

	spb=fskd->spb; 
	if (fskd->spb == 7) spb = 8000.0 / 1200.0;
	ds=spb/32.;
	spb2=spb/2.;

	for (f=0;;){
		if (demodulador(fskd,&x, GET_SAMPLE)) return(-1);
		if ((x*fskd->x0)<0) {	/* Transicion */
			if (!f) {
				if (fskd->cont<(spb2)) fskd->cont+=ds; else fskd->cont-=ds;
				f=1;
			}
		}
		fskd->x0=x;
		fskd->cont+=1.;
		if (fskd->cont>spb) {
			fskd->cont-=spb;
			break;
		}
	}
	f=(x>0)?0x80:0;
	return(f);
}

int fsk_serie(fsk_data *fskd, short *buffer, int *len, int *outbyte)
{
	int a;
	int i,j,n1,r;
	int samples=0;
	int olen;
	switch(fskd->state) {
		/* Pick up where we left off */
	case STATE_SEARCH_STARTBIT2:
		goto search_startbit2;
	case STATE_SEARCH_STARTBIT3:
		goto search_startbit3;
	case STATE_GET_BYTE:
		goto getbyte;
	}
	/* Esperamos bit de start	*/
	do {
/* this was jesus's nice, reasonable, working (at least with RTTY) code
to look for the beginning of the start bit. Unfortunately, since TTY/TDD's
just start sending a start bit with nothing preceding it at the beginning
of a transmission (what a LOSING design), we cant do it this elegantly */
/*
		if (demodulador(zap,&x1)) return(-1);
		for(;;) {
			if (demodulador(zap,&x2)) return(-1);
			if (x1>0 && x2<0) break;
			x1=x2;
		}
*/
/* this is now the imprecise, losing, but functional code to detect the
beginning of a start bit in the TDD sceanario. It just looks for sufficient
level to maybe, perhaps, guess, maybe that its maybe the beginning of
a start bit, perhaps. This whole thing stinks! */
		if (demodulador(fskd,&fskd->x1,GET_SAMPLE)) return(-1);
		samples++;
		for(;;)
		   {
search_startbit2:		   
			if (!*len) {
				fskd->state = STATE_SEARCH_STARTBIT2;
				return 0;
			}
			samples++;
			if (demodulador(fskd,&fskd->x2,GET_SAMPLE)) return(-1);
#if 0
			printf("x2 = %5.5f ", fskd->x2);
#endif			
			if (fskd->x2 < -0.5) break; 
		   }
search_startbit3:		   
		/* Esperamos 0.5 bits antes de usar DPLL */
		i=fskd->spb/2;
		if (*len < i) {
			fskd->state = STATE_SEARCH_STARTBIT3;
			return 0;
		}
		for(;i;i--) { if (demodulador(fskd,&fskd->x1,GET_SAMPLE)) return(-1); 
#if 0
			printf("x1 = %5.5f ", fskd->x1);
#endif			
	samples++; }

		/* x1 debe ser negativo (confirmación del bit de start) */

	} while (fskd->x1>0);
	fskd->state = STATE_GET_BYTE;

getbyte:

	/* Need at least 80 samples (for 1200) or
		1320 (for 45.5) to be sure we'll have a byte */
	if (fskd->nbit < 8) {
		if (*len < 1320)
			return 0;
	} else {
		if (*len < 80)
			return 0;
	}
	/* Leemos ahora los bits de datos */
	j=fskd->nbit;
	for (a=n1=0;j;j--) {
		olen = *len;
		i=get_bit_raw(fskd, buffer, len);
		buffer += (olen - *len);
		if (i == -1) return(-1);
		if (i) n1++;
		a>>=1; a|=i;
	}
	j=8-fskd->nbit;
	a>>=j;

	/* Leemos bit de paridad (si existe) y la comprobamos */
	if (fskd->paridad) {
		olen = *len;
		i=get_bit_raw(fskd, buffer, len); 
		buffer += (olen - *len);
		if (i == -1) return(-1);
		if (i) n1++;
		if (fskd->paridad==1) {	/* paridad=1 (par) */
			if (n1&1) a|=0x100;		/* error */
		} else {			/* paridad=2 (impar) */
			if (!(n1&1)) a|=0x100;	/* error */
		}
	}
	
	/* Leemos bits de STOP. Todos deben ser 1 */
	
	for (j=fskd->nstop;j;j--) {
		r = get_bit_raw(fskd, buffer, len);
		if (r == -1) return(-1);
		if (!r) a|=0x200;
	}

	/* Por fin retornamos  */
	/* Bit 8 : Error de paridad */
	/* Bit 9 : Error de Framming */

	*outbyte = a;
	fskd->state = STATE_SEARCH_STARTBIT;
	return 1;
}
