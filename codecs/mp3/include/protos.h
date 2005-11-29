/*====================================================================*/
int hybrid(MPEG *m, void *xin, void *xprev, float *y,
	   int btype, int nlong, int ntot, int nprev);
int hybrid_sum(MPEG *m, void *xin, void *xin_left, float *y,
	       int btype, int nlong, int ntot);
void sum_f_bands(void *a, void *b, int n);
void FreqInvert(float *y, int n);
void antialias(MPEG *m, void *x, int n);
void ms_process(void *x, int n);	/* sum-difference stereo */
void is_process_MPEG1(MPEG *m, void *x,	/* intensity stereo */
		      SCALEFACT * sf,
		      CB_INFO cb_info[2],	/* [ch] */
		      int nsamp, int ms_mode);
void is_process_MPEG2(MPEG *m, void *x,	/* intensity stereo */
		      SCALEFACT * sf,
		      CB_INFO cb_info[2],	/* [ch] */
		      IS_SF_INFO * is_sf_info,
		      int nsamp, int ms_mode);

void unpack_huff(void *xy, int n, int ntable);
int unpack_huff_quad(void *vwxy, int n, int nbits, int ntable);
void dequant(MPEG *m, SAMPLE sample[], int *nsamp,
	     SCALEFACT * sf,
	     GR * gr,
	     CB_INFO * cb_info, int ncbl_mixed);
void unpack_sf_sub_MPEG1(SCALEFACT * scalefac, GR * gr,
			 int scfsi,	/* bit flag */
			 int igr);
void unpack_sf_sub_MPEG2(SCALEFACT sf[],	/* return intensity scale */
			 GR * grdat,
			 int is_and_ch, IS_SF_INFO * is_sf_info);


/*---------- quant ---------------------------------*/
/* 8 bit lookup x = pow(2.0, 0.25*(global_gain-210)) */
float *quant_init_global_addr(MPEG *m);


/* x = pow(2.0, -0.5*(1+scalefact_scale)*scalefac + preemp) */
typedef float LS[4][32];
LS *quant_init_scale_addr(MPEG *m);


float *quant_init_pow_addr(MPEG *m);
float *quant_init_subblock_addr(MPEG *m);

typedef int iARRAY22[22];
iARRAY22 *quant_init_band_addr(MPEG *m);

/*---------- antialias ---------------------------------*/
typedef float PAIR[2];
PAIR *alias_init_addr(MPEG *m);
