/* $Id$ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pjmedia.h>
#include <pjlib-util.h>
#include <pjlib.h>
#include <stdio.h>
#include <stdlib.h>

#define app_perror(a,b,c) printf("%s: %s (%d)", a, b, c)


/* For logging purpose. */
#define THIS_FILE   "cmp_wav.c"
#define BYTES_PER_FRAME	    512

static const char *desc = 
" FILE		    						    \n"
"		    						    \n"
"  cmp_wav.c	    						    \n"
"		    						    \n"
" PURPOSE	    						    \n"
"		    						    \n"
"  Compare two WAV files.					    \n"
"		    						    \n"
" USAGE		    						    \n"
"		    						    \n"
"  cmp_wav ORIGINAL_WAV DEGRADED_WAV [TIME] [DETAIL]		    \n"
"		    						    \n"
"  ORIGINAL_WAV	    The original WAV file as reference.		    \n"
"  DEGRADED_WAV	    The degraded WAV file.			    \n"
"  TIME	            Compare only some part of the files		    \n"
"                   (in ms, since the beginning).		    \n"
"                   Specify 0 (default) to compare the whole time.  \n"
"  DETAIL           Show detail result, 1 or 0 (default=0, means no)\n"
"		    						    \n"
"  Both files must have same clock rate and must contain	    \n"
"  uncompressed (i.e. 16bit) PCM.				    \n";


/* Sum of multiplication of corresponding samples in buf1 & buf2 */
double sum_mult_sig(pj_int16_t *buf1, pj_int16_t *buf2, unsigned nsamples)
{
    double mag = 0;

    while (nsamples--)
	mag += (double)*buf1++ * (double)*buf2++;

    return mag;
}


/*
 * main()
 */
int main(int argc, char *argv[])
{
    pj_caching_pool cp;
    pjmedia_endpt *med_endpt;
    pj_pool_t *pool;
    pjmedia_port *file_ori_port;
    pjmedia_port *file_deg_port;
    pj_status_t status;
    unsigned first_nsamples = 0;
    unsigned samples_compared = 0;

    char buf1[BYTES_PER_FRAME];
    char buf2[BYTES_PER_FRAME];

    double ref_mag = 0;
    double deg_mag = 0;
    double mix_mag = 0;

    int detail = 0;
    int res_deg, res_mix, res_overall;

    if (argc < 3) {
    	puts("Error: original & degraded filename required");
	puts(desc);
	return 1;
    }

    /* Set log level. */
    pj_log_set_level(3);

    /* Must init PJLIB first: */
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    /* Must create a pool factory before we can allocate any memory. */
    pj_caching_pool_init(&cp, &pj_pool_factory_default_policy, 0);

    /* 
     * Initialize media endpoint.
     * This will implicitly initialize PJMEDIA too.
     */
    status = pjmedia_endpt_create(&cp.factory, NULL, 1, &med_endpt);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    /* Create memory pool for our file player */
    pool = pj_pool_create( &cp.factory,	    /* pool factory	    */
			   "wav",	    /* pool name.	    */
			   4000,	    /* init size	    */
			   4000,	    /* increment size	    */
			   NULL		    /* callback on error    */
			   );

    /* Create file media port from the original WAV file */
    status = pjmedia_wav_player_port_create(  pool,	/* memory pool	    */
					      argv[1],	/* file to play	    */
					      40,	/* ptime.	    */
					      PJMEDIA_FILE_NO_LOOP,	/* flags	    */
					      0,	/* default buffer   */
					      &file_ori_port/* returned port    */
					      );
    if (status != PJ_SUCCESS) {
	app_perror(THIS_FILE, "Unable to use WAV file", status);
	return 1;
    }

    /* Create file media port from the degraded WAV file */
    status = pjmedia_wav_player_port_create(  pool,	/* memory pool	    */
					      argv[2],	/* file to play	    */
					      40,	/* ptime.	    */
					      PJMEDIA_FILE_NO_LOOP,	/* flags	    */
					      0,	/* default buffer   */
					      &file_deg_port/* returned port    */
					      );
    if (status != PJ_SUCCESS) {
	app_perror(THIS_FILE, "Unable to use WAV file", status);
	return 1;
    }

    if (file_ori_port->info.clock_rate != file_deg_port->info.clock_rate) {
	app_perror(THIS_FILE, "Clock rates must be same.", PJ_EINVAL);
	return 1;
    }

    if (argc > 3)
	first_nsamples = atoi(argv[3]) * file_ori_port->info.clock_rate / 1000;

    if (argc > 4)
	detail = atoi(argv[4]);

    while (1) {
	pjmedia_frame f1, f2;

	f1.buf = buf1;
	f1.size = BYTES_PER_FRAME;
	f2.buf = buf2;
	f2.size = BYTES_PER_FRAME;

	status = pjmedia_port_get_frame(file_ori_port, &f1);
	if (status == PJ_EEOF) {
	    break;
	} else if (status != PJ_SUCCESS) {
	    app_perror(THIS_FILE, "Error occured while reading file", status);
	    break;
	}
	status = pjmedia_port_get_frame(file_deg_port, &f2);
	if (status == PJ_EEOF) {
	    break;
	} else if (status != PJ_SUCCESS) {
	    app_perror(THIS_FILE, "Error occured while reading file", status);
	    break;
	}

	/* Calculate magnitudes */
	ref_mag += sum_mult_sig(f1.buf, f1.buf, BYTES_PER_FRAME >> 1);
	deg_mag += sum_mult_sig(f2.buf, f2.buf, BYTES_PER_FRAME >> 1);
	mix_mag += sum_mult_sig(f1.buf, f2.buf, BYTES_PER_FRAME >> 1);

	samples_compared += BYTES_PER_FRAME >> 1;
	if (first_nsamples && samples_compared >= first_nsamples)
	    break;
    }

    /* Degraded magnitude compared to reference magnitude 
     */
    res_deg = (int) (deg_mag / ref_mag * 100.0);
    if (res_deg < 0)
	res_deg = -1;
    else if (res_deg >= 81)
	res_deg = 9;
    else
	res_deg = pj_isqrt(res_deg);

    /* Mixed magnitude (don't know what this is actually :D) compared to 
     * reference magnitude 
     */
    res_mix = (int) (mix_mag / ref_mag * 100.0);
    if (res_mix < 0)
	res_mix = -1;
    else if (res_mix >= 81)
	res_mix = 9;
    else
	res_mix = pj_isqrt(res_mix);

    /* Overall score.
     * If mixed score is -1, then overall score should be -1 as well.
     * Apply no weighting (1:1) for now.
     */
    if (res_mix == -1)
	res_overall = -1;
    else
	res_overall = (res_mix*1 + res_deg*1) / 2;

    if (detail) {
	printf("Reference = %.0f\n", ref_mag);
	printf("Degraded  = %.0f\n", deg_mag);
	printf("Mixed     = %.0f\n", mix_mag);

	printf("\n");

	printf("Score 1   = %d\n", res_deg);
	printf("Score 2   = %d\n", res_mix);

	printf("\n");
    }

    printf("Overall   = %d\n", res_overall);

    /* Destroy file port */
    status = pjmedia_port_destroy( file_ori_port );
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    status = pjmedia_port_destroy( file_deg_port );
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

    /* Release application pool */
    pj_pool_release( pool );

    /* Destroy media endpoint. */
    pjmedia_endpt_destroy( med_endpt );

    /* Destroy pool factory */
    pj_caching_pool_destroy( &cp );

    /* Shutdown PJLIB */
    pj_shutdown();


    /* Done. */
    return 0;
}

