/* Conversion routines derived from code by guido@sienanet.it */

#define GSM_MAGIC 0xD

#ifndef GSM_H
typedef unsigned char           gsm_byte;
#endif
typedef unsigned char           wav_byte;
typedef unsigned int			uword;

#define readGSM_33(c1) { \
		gsm_byte *c = (c1); \
        LARc[0]  = (*c++ & 0xF) << 2;           /* 1 */ \
        LARc[0] |= (*c >> 6) & 0x3; \
        LARc[1]  = *c++ & 0x3F; \
        LARc[2]  = (*c >> 3) & 0x1F; \
        LARc[3]  = (*c++ & 0x7) << 2; \
        LARc[3] |= (*c >> 6) & 0x3; \
        LARc[4]  = (*c >> 2) & 0xF; \
        LARc[5]  = (*c++ & 0x3) << 2; \
        LARc[5] |= (*c >> 6) & 0x3; \
        LARc[6]  = (*c >> 3) & 0x7; \
        LARc[7]  = *c++ & 0x7; \
        Nc[0]  = (*c >> 1) & 0x7F; \
        bc[0]  = (*c++ & 0x1) << 1; \
        bc[0] |= (*c >> 7) & 0x1; \
        Mc[0]  = (*c >> 5) & 0x3; \
        xmaxc[0]  = (*c++ & 0x1F) << 1; \
        xmaxc[0] |= (*c >> 7) & 0x1; \
        xmc[0]  = (*c >> 4) & 0x7; \
        xmc[1]  = (*c >> 1) & 0x7; \
        xmc[2]  = (*c++ & 0x1) << 2; \
        xmc[2] |= (*c >> 6) & 0x3; \
        xmc[3]  = (*c >> 3) & 0x7; \
        xmc[4]  = *c++ & 0x7; \
        xmc[5]  = (*c >> 5) & 0x7; \
        xmc[6]  = (*c >> 2) & 0x7; \
        xmc[7]  = (*c++ & 0x3) << 1;            /* 10 */ \
        xmc[7] |= (*c >> 7) & 0x1; \
        xmc[8]  = (*c >> 4) & 0x7; \
        xmc[9]  = (*c >> 1) & 0x7; \
        xmc[10]  = (*c++ & 0x1) << 2; \
        xmc[10] |= (*c >> 6) & 0x3; \
        xmc[11]  = (*c >> 3) & 0x7; \
        xmc[12]  = *c++ & 0x7; \
        Nc[1]  = (*c >> 1) & 0x7F; \
        bc[1]  = (*c++ & 0x1) << 1; \
        bc[1] |= (*c >> 7) & 0x1; \
        Mc[1]  = (*c >> 5) & 0x3; \
        xmaxc[1]  = (*c++ & 0x1F) << 1; \
        xmaxc[1] |= (*c >> 7) & 0x1; \
        xmc[13]  = (*c >> 4) & 0x7; \
        xmc[14]  = (*c >> 1) & 0x7; \
        xmc[15]  = (*c++ & 0x1) << 2; \
        xmc[15] |= (*c >> 6) & 0x3; \
        xmc[16]  = (*c >> 3) & 0x7; \
        xmc[17]  = *c++ & 0x7; \
        xmc[18]  = (*c >> 5) & 0x7; \
        xmc[19]  = (*c >> 2) & 0x7; \
        xmc[20]  = (*c++ & 0x3) << 1; \
        xmc[20] |= (*c >> 7) & 0x1; \
        xmc[21]  = (*c >> 4) & 0x7; \
        xmc[22]  = (*c >> 1) & 0x7; \
        xmc[23]  = (*c++ & 0x1) << 2; \
        xmc[23] |= (*c >> 6) & 0x3; \
        xmc[24]  = (*c >> 3) & 0x7; \
        xmc[25]  = *c++ & 0x7; \
        Nc[2]  = (*c >> 1) & 0x7F; \
        bc[2]  = (*c++ & 0x1) << 1;             /* 20 */ \
        bc[2] |= (*c >> 7) & 0x1; \
        Mc[2]  = (*c >> 5) & 0x3; \
        xmaxc[2]  = (*c++ & 0x1F) << 1; \
        xmaxc[2] |= (*c >> 7) & 0x1; \
        xmc[26]  = (*c >> 4) & 0x7; \
        xmc[27]  = (*c >> 1) & 0x7; \
        xmc[28]  = (*c++ & 0x1) << 2; \
        xmc[28] |= (*c >> 6) & 0x3; \
        xmc[29]  = (*c >> 3) & 0x7; \
        xmc[30]  = *c++ & 0x7; \
        xmc[31]  = (*c >> 5) & 0x7; \
        xmc[32]  = (*c >> 2) & 0x7; \
        xmc[33]  = (*c++ & 0x3) << 1; \
        xmc[33] |= (*c >> 7) & 0x1; \
        xmc[34]  = (*c >> 4) & 0x7; \
        xmc[35]  = (*c >> 1) & 0x7; \
        xmc[36]  = (*c++ & 0x1) << 2; \
        xmc[36] |= (*c >> 6) & 0x3; \
        xmc[37]  = (*c >> 3) & 0x7; \
        xmc[38]  = *c++ & 0x7; \
        Nc[3]  = (*c >> 1) & 0x7F; \
        bc[3]  = (*c++ & 0x1) << 1; \
        bc[3] |= (*c >> 7) & 0x1; \
        Mc[3]  = (*c >> 5) & 0x3; \
        xmaxc[3]  = (*c++ & 0x1F) << 1; \
        xmaxc[3] |= (*c >> 7) & 0x1; \
        xmc[39]  = (*c >> 4) & 0x7; \
        xmc[40]  = (*c >> 1) & 0x7; \
        xmc[41]  = (*c++ & 0x1) << 2; \
        xmc[41] |= (*c >> 6) & 0x3; \
        xmc[42]  = (*c >> 3) & 0x7; \
        xmc[43]  = *c++ & 0x7;                  /* 30  */ \
        xmc[44]  = (*c >> 5) & 0x7; \
        xmc[45]  = (*c >> 2) & 0x7; \
        xmc[46]  = (*c++ & 0x3) << 1; \
        xmc[46] |= (*c >> 7) & 0x1; \
        xmc[47]  = (*c >> 4) & 0x7; \
        xmc[48]  = (*c >> 1) & 0x7; \
        xmc[49]  = (*c++ & 0x1) << 2; \
        xmc[49] |= (*c >> 6) & 0x3; \
        xmc[50]  = (*c >> 3) & 0x7; \
        xmc[51]  = *c & 0x7;                    /* 33 */ \
}

static inline void conv66(gsm_byte * d, wav_byte * c) {
	gsm_byte frame_chain;
    unsigned int sr;
	unsigned int    LARc[8], Nc[4], Mc[4], bc[4], xmaxc[4], xmc[13*4];
	
	readGSM_33(d);
	sr = 0;
	sr = (sr >> 6) | (LARc[0] << 10);
	sr = (sr >> 6) | (LARc[1] << 10);
	*c++ = sr >> 4;
	sr = (sr >> 5) | (LARc[2] << 11);
	*c++ = sr >> 7;
	sr = (sr >> 5) | (LARc[3] << 11);
	sr = (sr >> 4) | (LARc[4] << 12);
	*c++ = sr >> 6;
	sr = (sr >> 4) | (LARc[5] << 12);
	sr = (sr >> 3) | (LARc[6] << 13);
	*c++ = sr >> 7;
	sr = (sr >> 3) | (LARc[7] << 13);
	sr = (sr >> 7) | (Nc[0] << 9);
	*c++ = sr >> 5;
	sr = (sr >> 2) | (bc[0] << 14);
	sr = (sr >> 2) | (Mc[0] << 14);
	sr = (sr >> 6) | (xmaxc[0] << 10);
	*c++ = sr >> 3;
	sr = (sr >> 3 )|( xmc[0] << 13);
	*c++ = sr >> 8;
	sr = (sr >> 3 )|( xmc[1] << 13);
	sr = (sr >> 3 )|( xmc[2] << 13);
    sr = (sr >> 3 )|( xmc[3] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[4] << 13);
    sr = (sr >> 3 )|( xmc[5] << 13);
    sr = (sr >> 3 )|( xmc[6] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[7] << 13);
    sr = (sr >> 3 )|( xmc[8] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[9] << 13);
    sr = (sr >> 3 )|( xmc[10] << 13);
    sr = (sr >> 3 )|( xmc[11] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[12] << 13);
    sr = (sr >> 7 )|( Nc[1] << 9);
    *c++ = sr >> 5;
    sr = (sr >> 2 )|( bc[1] << 14);
    sr = (sr >> 2 )|( Mc[1] << 14);
    sr = (sr >> 6 )|( xmaxc[1] << 10);
    *c++ = sr >> 3;
    sr = (sr >> 3 )|( xmc[13] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[14] << 13);
    sr = (sr >> 3 )|( xmc[15] << 13);
    sr = (sr >> 3 )|( xmc[16] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[17] << 13);
    sr = (sr >> 3 )|( xmc[18] << 13);
    sr = (sr >> 3 )|( xmc[19] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[20] << 13);
    sr = (sr >> 3 )|( xmc[21] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[22] << 13);
    sr = (sr >> 3 )|( xmc[23] << 13);
    sr = (sr >> 3 )|( xmc[24] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[25] << 13);
    sr = (sr >> 7 )|( Nc[2] << 9);
    *c++ = sr >> 5;
    sr = (sr >> 2 )|( bc[2] << 14);
    sr = (sr >> 2 )|( Mc[2] << 14);
    sr = (sr >> 6 )|( xmaxc[2] << 10);
    *c++ = sr >> 3;
    sr = (sr >> 3 )|( xmc[26] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[27] << 13);
    sr = (sr >> 3 )|( xmc[28] << 13);
    sr = (sr >> 3 )|( xmc[29] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[30] << 13);
    sr = (sr >> 3 )|( xmc[31] << 13);
    sr = (sr >> 3 )|( xmc[32] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[33] << 13);
    sr = (sr >> 3 )|( xmc[34] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[35] << 13);
    sr = (sr >> 3 )|( xmc[36] << 13);
    sr = (sr >> 3 )|( xmc[37] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[38] << 13);
    sr = (sr >> 7 )|( Nc[3] << 9);
    *c++ = sr >> 5;
    sr = (sr >> 2 )|( bc[3] << 14);
    sr = (sr >> 2 )|( Mc[3] << 14);
    sr = (sr >> 6 )|( xmaxc[3] << 10);
    *c++ = sr >> 3;
    sr = (sr >> 3 )|( xmc[39] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[40] << 13);
    sr = (sr >> 3 )|( xmc[41] << 13);
    sr = (sr >> 3 )|( xmc[42] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[43] << 13);
    sr = (sr >> 3 )|( xmc[44] << 13);
    sr = (sr >> 3 )|( xmc[45] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[46] << 13);
    sr = (sr >> 3 )|( xmc[47] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[48] << 13);
    sr = (sr >> 3 )|( xmc[49] << 13);
    sr = (sr >> 3 )|( xmc[50] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[51] << 13);
    sr = sr >> 4;
    *c = sr >> 8;
    frame_chain = *c;
    readGSM_33(d+33); /* puts all the parameters into LARc etc. */


    sr = 0;
/*                       sr = (sr >> 4 )|( s->frame_chain << 12); */
    sr = (sr >> 4 )|( frame_chain << 12);

    sr = (sr >> 6 )|( LARc[0] << 10);
    *c++ = sr >> 6;
    sr = (sr >> 6 )|( LARc[1] << 10);
    *c++ = sr >> 8;
    sr = (sr >> 5 )|( LARc[2] << 11);
    sr = (sr >> 5 )|( LARc[3] << 11);
    *c++ = sr >> 6;
    sr = (sr >> 4 )|( LARc[4] << 12);
    sr = (sr >> 4 )|( LARc[5] << 12);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( LARc[6] << 13);
    sr = (sr >> 3 )|( LARc[7] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 7 )|( Nc[0] << 9);
    sr = (sr >> 2 )|( bc[0] << 14);
    *c++ = sr >> 7;
    sr = (sr >> 2 )|( Mc[0] << 14);
    sr = (sr >> 6 )|( xmaxc[0] << 10);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[0] << 13);
    sr = (sr >> 3 )|( xmc[1] << 13);
    sr = (sr >> 3 )|( xmc[2] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[3] << 13);
    sr = (sr >> 3 )|( xmc[4] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[5] << 13);
    sr = (sr >> 3 )|( xmc[6] << 13);
    sr = (sr >> 3 )|( xmc[7] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[8] << 13);
    sr = (sr >> 3 )|( xmc[9] << 13);
    sr = (sr >> 3 )|( xmc[10] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[11] << 13);
    sr = (sr >> 3 )|( xmc[12] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 7 )|( Nc[1] << 9);
    sr = (sr >> 2 )|( bc[1] << 14);
    *c++ = sr >> 7;
    sr = (sr >> 2 )|( Mc[1] << 14);
    sr = (sr >> 6 )|( xmaxc[1] << 10);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[13] << 13);
    sr = (sr >> 3 )|( xmc[14] << 13);
    sr = (sr >> 3 )|( xmc[15] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[16] << 13);
    sr = (sr >> 3 )|( xmc[17] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[18] << 13);
    sr = (sr >> 3 )|( xmc[19] << 13);
    sr = (sr >> 3 )|( xmc[20] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[21] << 13);
    sr = (sr >> 3 )|( xmc[22] << 13);
    sr = (sr >> 3 )|( xmc[23] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[24] << 13);
    sr = (sr >> 3 )|( xmc[25] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 7 )|( Nc[2] << 9);
    sr = (sr >> 2 )|( bc[2] << 14);
    *c++ = sr >> 7;
    sr = (sr >> 2 )|( Mc[2] << 14);
    sr = (sr >> 6 )|( xmaxc[2] << 10);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[26] << 13);
    sr = (sr >> 3 )|( xmc[27] << 13);
    sr = (sr >> 3 )|( xmc[28] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[29] << 13);
    sr = (sr >> 3 )|( xmc[30] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[31] << 13);
    sr = (sr >> 3 )|( xmc[32] << 13);
    sr = (sr >> 3 )|( xmc[33] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[34] << 13);
    sr = (sr >> 3 )|( xmc[35] << 13);
    sr = (sr >> 3 )|( xmc[36] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[37] << 13);
    sr = (sr >> 3 )|( xmc[38] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 7 )|( Nc[3] << 9);
    sr = (sr >> 2 )|( bc[3] << 14);
    *c++ = sr >> 7;
    sr = (sr >> 2 )|( Mc[3] << 14);
    sr = (sr >> 6 )|( xmaxc[3] << 10);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[39] << 13);
    sr = (sr >> 3 )|( xmc[40] << 13);
    sr = (sr >> 3 )|( xmc[41] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[42] << 13);
    sr = (sr >> 3 )|( xmc[43] << 13);
    *c++ = sr >> 8;
    sr = (sr >> 3 )|( xmc[44] << 13);
    sr = (sr >> 3 )|( xmc[45] << 13);
    sr = (sr >> 3 )|( xmc[46] << 13);
    *c++ = sr >> 7;
    sr = (sr >> 3 )|( xmc[47] << 13);
    sr = (sr >> 3 )|( xmc[48] << 13);
    sr = (sr >> 3 )|( xmc[49] << 13);
    *c++ = sr >> 6;
    sr = (sr >> 3 )|( xmc[50] << 13);
    sr = (sr >> 3 )|( xmc[51] << 13);
    *c++ = sr >> 8;

}

#define writeGSM_33(c1) { \
				gsm_byte *c = (c1); \
                *c++ =   ((GSM_MAGIC & 0xF) << 4)               /* 1 */ \
                           | ((LARc[0] >> 2) & 0xF); \
                *c++ =   ((LARc[0] & 0x3) << 6) \
                           | (LARc[1] & 0x3F); \
                *c++ =   ((LARc[2] & 0x1F) << 3) \
                           | ((LARc[3] >> 2) & 0x7); \
                *c++ =   ((LARc[3] & 0x3) << 6) \
                       | ((LARc[4] & 0xF) << 2) \
                       | ((LARc[5] >> 2) & 0x3); \
                *c++ =   ((LARc[5] & 0x3) << 6) \
                       | ((LARc[6] & 0x7) << 3) \
                       | (LARc[7] & 0x7);   \
                *c++ =   ((Nc[0] & 0x7F) << 1) \
                       | ((bc[0] >> 1) & 0x1); \
                *c++ =   ((bc[0] & 0x1) << 7) \
                       | ((Mc[0] & 0x3) << 5) \
                       | ((xmaxc[0] >> 1) & 0x1F); \
                *c++ =   ((xmaxc[0] & 0x1) << 7) \
                       | ((xmc[0] & 0x7) << 4) \
                       | ((xmc[1] & 0x7) << 1) \
                           | ((xmc[2] >> 2) & 0x1); \
                *c++ =   ((xmc[2] & 0x3) << 6) \
                       | ((xmc[3] & 0x7) << 3) \
                       | (xmc[4] & 0x7); \
                *c++ =   ((xmc[5] & 0x7) << 5)                  /* 10 */ \
                       | ((xmc[6] & 0x7) << 2) \
                       | ((xmc[7] >> 1) & 0x3); \
                *c++ =   ((xmc[7] & 0x1) << 7) \
                       | ((xmc[8] & 0x7) << 4) \
                       | ((xmc[9] & 0x7) << 1) \
                       | ((xmc[10] >> 2) & 0x1); \
                *c++ =   ((xmc[10] & 0x3) << 6) \
                       | ((xmc[11] & 0x7) << 3) \
                       | (xmc[12] & 0x7); \
                *c++ =   ((Nc[1] & 0x7F) << 1) \
                       | ((bc[1] >> 1) & 0x1); \
                *c++ =   ((bc[1] & 0x1) << 7) \
                       | ((Mc[1] & 0x3) << 5) \
                       | ((xmaxc[1] >> 1) & 0x1F);  \
                *c++ =   ((xmaxc[1] & 0x1) << 7) \
                       | ((xmc[13] & 0x7) << 4) \
                           | ((xmc[14] & 0x7) << 1) \
                       | ((xmc[15] >> 2) & 0x1); \
                *c++ =   ((xmc[15] & 0x3) << 6) \
                       | ((xmc[16] & 0x7) << 3) \
                       | (xmc[17] & 0x7); \
                *c++ =   ((xmc[18] & 0x7) << 5) \
                       | ((xmc[19] & 0x7) << 2) \
                       | ((xmc[20] >> 1) & 0x3); \
                *c++ =   ((xmc[20] & 0x1) << 7) \
                       | ((xmc[21] & 0x7) << 4) \
                       | ((xmc[22] & 0x7) << 1) \
                           | ((xmc[23] >> 2) & 0x1); \
                *c++ =   ((xmc[23] & 0x3) << 6) \
                       | ((xmc[24] & 0x7) << 3) \
                           | (xmc[25] & 0x7); \
                *c++ =   ((Nc[2] & 0x7F) << 1)                  /* 20 */ \
                       | ((bc[2] >> 1) & 0x1); \
                *c++ =   ((bc[2] & 0x1) << 7) \
                       | ((Mc[2] & 0x3) << 5) \
                       | ((xmaxc[2] >> 1) & 0x1F); \
                *c++ =   ((xmaxc[2] & 0x1) << 7)   \
                       | ((xmc[26] & 0x7) << 4) \
                       | ((xmc[27] & 0x7) << 1) \
                       | ((xmc[28] >> 2) & 0x1); \
                *c++ =   ((xmc[28] & 0x3) << 6) \
                       | ((xmc[29] & 0x7) << 3) \
                       | (xmc[30] & 0x7); \
                *c++ =   ((xmc[31] & 0x7) << 5) \
                       | ((xmc[32] & 0x7) << 2) \
                       | ((xmc[33] >> 1) & 0x3); \
                *c++ =   ((xmc[33] & 0x1) << 7) \
                       | ((xmc[34] & 0x7) << 4) \
                       | ((xmc[35] & 0x7) << 1) \
                       | ((xmc[36] >> 2) & 0x1); \
                *c++ =   ((xmc[36] & 0x3) << 6) \
                           | ((xmc[37] & 0x7) << 3) \
                       | (xmc[38] & 0x7); \
                *c++ =   ((Nc[3] & 0x7F) << 1) \
                       | ((bc[3] >> 1) & 0x1); \
                *c++ =   ((bc[3] & 0x1) << 7)  \
                       | ((Mc[3] & 0x3) << 5) \
                       | ((xmaxc[3] >> 1) & 0x1F); \
                *c++ =   ((xmaxc[3] & 0x1) << 7) \
                       | ((xmc[39] & 0x7) << 4) \
                       | ((xmc[40] & 0x7) << 1) \
                       | ((xmc[41] >> 2) & 0x1); \
                *c++ =   ((xmc[41] & 0x3) << 6)                 /* 30 */ \
                       | ((xmc[42] & 0x7) << 3) \
                       | (xmc[43] & 0x7); \
                *c++ =   ((xmc[44] & 0x7) << 5) \
                       | ((xmc[45] & 0x7) << 2) \
                       | ((xmc[46] >> 1) & 0x3); \
                *c++ =   ((xmc[46] & 0x1) << 7) \
                       | ((xmc[47] & 0x7) << 4) \
                       | ((xmc[48] & 0x7) << 1) \
                       | ((xmc[49] >> 2) & 0x1); \
                *c++ =   ((xmc[49] & 0x3) << 6) \
                       | ((xmc[50] & 0x7) << 3) \
                           | (xmc[51] & 0x7); \
}

static inline void conv65( wav_byte * c, gsm_byte * d){

                unsigned int sr = 0;
                unsigned int frame_chain;
				unsigned int    LARc[8], Nc[4], Mc[4], bc[4], xmaxc[4], xmc[13*4];
 
                        sr = *c++;
                        LARc[0] = sr & 0x3f;  sr >>= 6;
                        sr |= (uword)*c++ << 2;
                        LARc[1] = sr & 0x3f;  sr >>= 6;
                        sr |= (uword)*c++ << 4;
                        LARc[2] = sr & 0x1f;  sr >>= 5;
                        LARc[3] = sr & 0x1f;  sr >>= 5;
                        sr |= (uword)*c++ << 2;
                        LARc[4] = sr & 0xf;  sr >>= 4;
                        LARc[5] = sr & 0xf;  sr >>= 4;
                        sr |= (uword)*c++ << 2;                 /* 5 */
                        LARc[6] = sr & 0x7;  sr >>= 3;
                        LARc[7] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 4;
                        Nc[0] = sr & 0x7f;  sr >>= 7;
                        bc[0] = sr & 0x3;  sr >>= 2;
                        Mc[0] = sr & 0x3;  sr >>= 2;
                        sr |= (uword)*c++ << 1;
                        xmaxc[0] = sr & 0x3f;  sr >>= 6;
                        xmc[0] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        xmc[1] = sr & 0x7;  sr >>= 3;
                        xmc[2] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;
                        xmc[3] = sr & 0x7;  sr >>= 3;
                        xmc[4] = sr & 0x7;  sr >>= 3;
                        xmc[5] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;                 /* 10 */
                        xmc[6] = sr & 0x7;  sr >>= 3;
                        xmc[7] = sr & 0x7;  sr >>= 3;
                        xmc[8] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        xmc[9] = sr & 0x7;  sr >>= 3;
                        xmc[10] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;
                        xmc[11] = sr & 0x7;  sr >>= 3;
                        xmc[12] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 4;
                        Nc[1] = sr & 0x7f;  sr >>= 7;
                        bc[1] = sr & 0x3;  sr >>= 2;
                        Mc[1] = sr & 0x3;  sr >>= 2;
                        sr |= (uword)*c++ << 1;
                        xmaxc[1] = sr & 0x3f;  sr >>= 6;
                        xmc[13] = sr & 0x7;  sr >>= 3;
                        sr = *c++;                              /* 15 */
                        xmc[14] = sr & 0x7;  sr >>= 3;
                        xmc[15] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;
                        xmc[16] = sr & 0x7;  sr >>= 3;
                        xmc[17] = sr & 0x7;  sr >>= 3;
                        xmc[18] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;
                        xmc[19] = sr & 0x7;  sr >>= 3;
                        xmc[20] = sr & 0x7;  sr >>= 3;
                        xmc[21] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        xmc[22] = sr & 0x7;  sr >>= 3;
                        xmc[23] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;
                        xmc[24] = sr & 0x7;  sr >>= 3;
                        xmc[25] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 4;                 /* 20 */
                        Nc[2] = sr & 0x7f;  sr >>= 7;
                        bc[2] = sr & 0x3;  sr >>= 2;
                        Mc[2] = sr & 0x3;  sr >>= 2;
                        sr |= (uword)*c++ << 1;
                        xmaxc[2] = sr & 0x3f;  sr >>= 6;
                        xmc[26] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        xmc[27] = sr & 0x7;  sr >>= 3;
                        xmc[28] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;
                        xmc[29] = sr & 0x7;  sr >>= 3;
                        xmc[30] = sr & 0x7;  sr >>= 3;
                        xmc[31] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;
                        xmc[32] = sr & 0x7;  sr >>= 3;
                        xmc[33] = sr & 0x7;  sr >>= 3;
                        xmc[34] = sr & 0x7;  sr >>= 3;
                        sr = *c++;                              /* 25 */
                        xmc[35] = sr & 0x7;  sr >>= 3;
                        xmc[36] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;
                        xmc[37] = sr & 0x7;  sr >>= 3;
                        xmc[38] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 4;
                        Nc[3] = sr & 0x7f;  sr >>= 7;
                        bc[3] = sr & 0x3;  sr >>= 2;
                        Mc[3] = sr & 0x3;  sr >>= 2;
                        sr |= (uword)*c++ << 1;
                        xmaxc[3] = sr & 0x3f;  sr >>= 6;
                        xmc[39] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        xmc[40] = sr & 0x7;  sr >>= 3;
                        xmc[41] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;                 /* 30 */
                        xmc[42] = sr & 0x7;  sr >>= 3;
                        xmc[43] = sr & 0x7;  sr >>= 3;
                        xmc[44] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;
                        xmc[45] = sr & 0x7;  sr >>= 3;
                        xmc[46] = sr & 0x7;  sr >>= 3;
                        xmc[47] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        xmc[49] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;
                        xmc[50] = sr & 0x7;  sr >>= 3;
                        xmc[51] = sr & 0x7;  sr >>= 3;

                        frame_chain = sr & 0xf;


                        writeGSM_33(d);/* LARc etc. -> array of 33 GSM bytes */


                        sr = frame_chain;
                        sr |= (uword)*c++ << 4;                 /* 1 */
                        LARc[0] = sr & 0x3f;  sr >>= 6;
                        LARc[1] = sr & 0x3f;  sr >>= 6;
                        sr = *c++;
                        LARc[2] = sr & 0x1f;  sr >>= 5;
                        sr |= (uword)*c++ << 3;
                        LARc[3] = sr & 0x1f;  sr >>= 5;
                        LARc[4] = sr & 0xf;  sr >>= 4;
                        sr |= (uword)*c++ << 2;
                        LARc[5] = sr & 0xf;  sr >>= 4;
                        LARc[6] = sr & 0x7;  sr >>= 3;
                        LARc[7] = sr & 0x7;  sr >>= 3;
                        sr = *c++;                              /* 5 */
                        Nc[0] = sr & 0x7f;  sr >>= 7;
                        sr |= (uword)*c++ << 1;
                        bc[0] = sr & 0x3;  sr >>= 2;
                        Mc[0] = sr & 0x3;  sr >>= 2;
                        sr |= (uword)*c++ << 5;
                        xmaxc[0] = sr & 0x3f;  sr >>= 6;
                        xmc[0] = sr & 0x7;  sr >>= 3;
                        xmc[1] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;
                        xmc[2] = sr & 0x7;  sr >>= 3;
                        xmc[3] = sr & 0x7;  sr >>= 3;
                        xmc[4] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        xmc[5] = sr & 0x7;  sr >>= 3;
                        xmc[6] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;                 /* 10 */
                        xmc[7] = sr & 0x7;  sr >>= 3;
                        xmc[8] = sr & 0x7;  sr >>= 3;
                        xmc[9] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;
                        xmc[10] = sr & 0x7;  sr >>= 3;
                        xmc[11] = sr & 0x7;  sr >>= 3;
                        xmc[12] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        Nc[1] = sr & 0x7f;  sr >>= 7;
                        sr |= (uword)*c++ << 1;
                        bc[1] = sr & 0x3;  sr >>= 2;
                        Mc[1] = sr & 0x3;  sr >>= 2;
                        sr |= (uword)*c++ << 5;
                        xmaxc[1] = sr & 0x3f;  sr >>= 6;
                        xmc[13] = sr & 0x7;  sr >>= 3;
                        xmc[14] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;                 /* 15 */
                        xmc[15] = sr & 0x7;  sr >>= 3;
                        xmc[16] = sr & 0x7;  sr >>= 3;
                        xmc[17] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        xmc[18] = sr & 0x7;  sr >>= 3;
                        xmc[19] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;
                        xmc[20] = sr & 0x7;  sr >>= 3;
                        xmc[21] = sr & 0x7;  sr >>= 3;
                        xmc[22] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;
                        xmc[23] = sr & 0x7;  sr >>= 3;
                        xmc[24] = sr & 0x7;  sr >>= 3;
                        xmc[25] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        Nc[2] = sr & 0x7f;  sr >>= 7;
                        sr |= (uword)*c++ << 1;                 /* 20 */
                        bc[2] = sr & 0x3;  sr >>= 2;
                        Mc[2] = sr & 0x3;  sr >>= 2;
                        sr |= (uword)*c++ << 5;
                        xmaxc[2] = sr & 0x3f;  sr >>= 6;
                        xmc[26] = sr & 0x7;  sr >>= 3;
                        xmc[27] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;
                        xmc[28] = sr & 0x7;  sr >>= 3;
                        xmc[29] = sr & 0x7;  sr >>= 3;
                        xmc[30] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        xmc[31] = sr & 0x7;  sr >>= 3;
                        xmc[32] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;
                        xmc[33] = sr & 0x7;  sr >>= 3;
                        xmc[34] = sr & 0x7;  sr >>= 3;
                        xmc[35] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;                 /* 25 */
                        xmc[36] = sr & 0x7;  sr >>= 3;
                        xmc[37] = sr & 0x7;  sr >>= 3;
                        xmc[38] = sr & 0x7;  sr >>= 3;
                        sr = *c++;
                        Nc[3] = sr & 0x7f;  sr >>= 7;
                        sr |= (uword)*c++ << 1;
                        bc[3] = sr & 0x3;  sr >>= 2;
                        Mc[3] = sr & 0x3;  sr >>= 2;
                        sr |= (uword)*c++ << 5;
                        xmaxc[3] = sr & 0x3f;  sr >>= 6;
                        xmc[39] = sr & 0x7;  sr >>= 3;
                        xmc[40] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;
                        xmc[41] = sr & 0x7;  sr >>= 3;
                        xmc[42] = sr & 0x7;  sr >>= 3;
                        xmc[43] = sr & 0x7;  sr >>= 3;
                        sr = *c++;                              /* 30 */
                        xmc[44] = sr & 0x7;  sr >>= 3;
                        xmc[45] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 2;
                        xmc[46] = sr & 0x7;  sr >>= 3;
                        xmc[47] = sr & 0x7;  sr >>= 3;
                        xmc[48] = sr & 0x7;  sr >>= 3;
                        sr |= (uword)*c++ << 1;
                        xmc[49] = sr & 0x7;  sr >>= 3;
                        xmc[50] = sr & 0x7;  sr >>= 3;
                        xmc[51] = sr & 0x7;  sr >>= 3;
                        writeGSM_33(d+33);

}
