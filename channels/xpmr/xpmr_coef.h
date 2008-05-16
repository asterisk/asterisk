/*
 * xpmr_coef.h - for Xelatec Private Mobile Radio Processes
 * 
 * All Rights Reserved. Copyright (C)2007, Xelatec, LLC
 * 
 * 20070808 1235 Steven Henke, W9SH, sph@xelatec.com
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 * 		 
 * This version may be optionally licenced under the GNU LGPL licence.
 *
 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.
 *
 * Some filter coeficients via 'WinFilter' http://www.winfilter.20m.com.
 *
 */

/*! \file
 *
 * \brief Private Land Mobile Radio Channel Voice and Signaling Processor
 *
 * \author Steven Henke, W9SH <sph@xelatec.com> Xelatec, LLC
 */

#ifndef XPMR_COEF_H
#define XMPR_COEF_H 	1

// frequencies in 0.1 Hz
static const u32 dtmf_row[] =
{
	6970,  7700,  8520,  9410
};
static const u32 dtmf_col[] =
{
	12090, 13360, 14770, 16330
};


#define CTCSS_COEF_INT		120
#define CTCSS_SAMPLE_RATE   8000
#define TDIV(x) ((CTCSS_SAMPLE_RATE*1000/x)+5)/10

#if 0
static i32 coef_ctcss[4][5]=
{
	// freq, divisor, integrator, filter
	{770,TDIV(770),CTCSS_COEF_INT,0,0},
	{1000,TDIV(1000),CTCSS_COEF_INT,0,0},
	{1035,TDIV(1035),CTCSS_COEF_INT,0,0},
	{0,0,0,0}
};
#endif

static i16 coef_ctcss_div[]=
{
2985,    // 00   067.0
2782,    // 01   071.9
2688,    // 02   074.4
2597,    // 03   077.0
2509,    // 04   079.7
2424,    // 05   082.5
2342,    // 06   085.4
2260,    // 07   088.5
2186,    // 08   091.5
2110,    // 09   094.8
2053,    // 10   097.4
2000,    // 11   100.0
1932,    // 12   103.5
1866,    // 13   107.2
1803,    // 14   110.9
1742,    // 15   114.8
1684,    // 16   118.8
1626,    // 17   123.0
1571,    // 18   127.3
1517,    // 19   131.8
1465,    // 20   136.5
1415,    // 21   141.3
1368,    // 22   146.2
1321,    // 23   151.4
1276,    // 24   156.7
1233,    // 25   162.2
1191,    // 26   167.9
1151,    // 27   173.8
1112,    // 28   179.9
1074,    // 29   186.2
1037,    // 30   192.8
983,    // 31   203.5
949,    // 32   210.7
917,    // 33   218.1
886,    // 34   225.7
856,    // 35   233.6
827,    // 36   241.8
799     // 37   250.3
};

static float freq_ctcss[]=
{
067.0,    // 00   
071.9,    // 01   
074.4,    // 02   
077.0,    // 03   
079.7,    // 04   
082.5,    // 05   
085.4,    // 06   
088.5,    // 07   
091.5,    // 08   
094.8,    // 09   
097.4,    // 10   
100.0,    // 11   
103.5,    // 12   
107.2,    // 13   
110.9,    // 14   
114.8,    // 15   
118.8,    // 16   
123.0,    // 17   
127.3,    // 18   
131.8,    // 19   
136.5,    // 20   
141.3,    // 21   
146.2,    // 22   
151.4,    // 23   
156.7,    // 24   
162.2,    // 25   
167.9,    // 26   
173.8,    // 27   
179.9,    // 28   
186.2,    // 29   
192.8,    // 30   
203.5,    // 31  
210.7 ,    // 32  
218.1 ,    // 33  
225.7 ,    // 34  
233.6 ,    // 35  
241.8 ,    // 36  
250.3      // 37  
};

/*
	noise squelch carrier detect filter
*/
static const int16_t taps_fir_bpf_noise_1 = 66;
static const int32_t gain_fir_bpf_noise_1 = 65536;
static const int16_t coef_fir_bpf_noise_1[] = { 
      139,
     -182,
     -269,
      -66,
       56,
       59,
      250,
      395,
      -80,
     -775,
     -557,
      437,
      779,
      210,
      -17,
      123,
     -692,
    -1664,
     -256,
     2495,
     2237,
    -1018,
    -2133,
     -478,
    -1134,
    -2711,
     2642,
    10453,
     4010,
    -14385,
    -16488,
     6954,
    23030,
     6954,
    -16488,
    -14385,
     4010,
    10453,
     2642,
    -2711,
    -1134,
     -478,
    -2133,
    -1018,
     2237,
     2495,
     -256,
    -1664,
     -692,
      123,
      -17,
      210,
      779,
      437,
     -557,
     -775,
      -80,
      395,
      250,
       59,
       56,
      -66,
     -269,
     -182,
      139,
      257
};
/*
	tbd
*/
static const int16_t taps_fir_lpf_3K_1 = 66;
static const int32_t gain_fir_lpf_3K_1 = 131072;
static const int16_t coef_fir_lpf_3K_1[] = { 
      259,
       58,
     -185,
     -437,
     -654,
     -793,
     -815,
     -696,
     -434,
      -48,
      414,
      886,
     1284,
     1523,
     1529,
     1254,
      691,
     -117,
    -1078,
    -2049,
    -2854,
    -3303,
    -3220,
    -2472,
     -995,
     1187,
     3952,
     7086,
    10300,
    13270,
    15672,
    17236,
    17778,
    17236,
    15672,
    13270,
    10300,
     7086,
     3952,
     1187,
     -995,
    -2472,
    -3220,
    -3303,
    -2854,
    -2049,
    -1078,
     -117,
      691,
     1254,
     1529,
     1523,
     1284,
      886,
      414,
      -48,
     -434,
     -696,
     -815,
     -793,
     -654,
     -437,
     -185,
       58,
      259,
      393
};

/**************************************************************
Filter type: Low Pass
Filter model: Butterworth
Filter order: 9
Sampling Frequency: 8 KHz
Cut Frequency: 0.250000 KHz
Coefficents Quantization: 16-bit
***************************************************************/
static const int16_t taps_fir_lpf_250_11_64 = 64;
static const int32_t gain_fir_lpf_250_11_64 = 262144;
static const int16_t coef_fir_lpf_250_11_64[] = 
{
      366,
       -3,
     -418,
     -865,
    -1328,
    -1788,
    -2223,
    -2609,
    -2922,
    -3138,
    -3232,
    -3181,
    -2967,
    -2573,
    -1988,
    -1206,
     -228,
      937,
     2277,
     3767,
     5379,
     7077,
     8821,
    10564,
    12259,
    13855,
    15305,
    16563,
    17588,
    18346,
    18812,
    18968,
    18812,
    18346,
    17588,
    16563,
    15305,
    13855,
    12259,
    10564,
     8821,
     7077,
     5379,
     3767,
     2277,
      937,
     -228,
    -1206,
    -1988,
    -2573,
    -2967,
    -3181,
    -3232,
    -3138,
    -2922,
    -2609,
    -2223,
    -1788,
    -1328,
     -865,
     -418,
       -3,
      366,
      680
};

// de-emphasis integrator 300 Hz with 8KS/s
// a0, b1
static const int16_t taps_int_lpf_300_1_2 = 2;
static const int32_t gain_int_lpf_300_1_2 = 8182;
static const int16_t coef_int_lpf_300_1_2[]={
6878,
25889
};

// pre-emphasis differentiator 4000 Hz with 8KS/s
// a0,a1,b0,
static const int16_t taps_int_hpf_4000_1_2 = 2;
//static const int32_t gain_int_hpf_4000_1_2 = 16384;  // per calculations
static const int32_t gain_int_hpf_4000_1_2 = 13404; // hand tweaked for unity gain at 1KHz
static const int16_t coef_int_hpf_4000_1_2[]={
17610,
-17610,
2454
};




/*
	ctcss decode filter
*/
/**************************************************************
Filter type: Low Pass
Filter model: Butterworth
Filter order: 9
Sampling Frequency: 8 KHz
Cut Frequency: 0.250000 KHz
Coefficents Quantization: 16-bit
***************************************************************/
static const int16_t taps_fir_lpf_250_9_66 = 66;
static const int32_t gain_fir_lpf_250_9_66 = 262144;
static const int16_t coef_fir_lpf_250_9_66[] = 
{ 
  676,
  364,
   -3,
 -415,
 -860,
-1320,
-1777,
-2209,
-2593,
-2904,
-3119,
-3212,
-3162,
-2949,
-2557,
-1975,
-1198,
 -226,
  932,
 2263,
 3744,
 5346,
 7034,
 8767,
10499,
12184,
13770,
15211,
16462,
17480,
18234,
18696,
18852,
18696,
18234,
17480,
16462,
15211,
13770,
12184,
10499,
 8767,
 7034,
 5346,
 3744,
 2263,
  932,
 -226,
-1198,
-1975,
-2557,
-2949,
-3162,
-3212,
-3119,
-2904,
-2593,
-2209,
-1777,
-1320,
 -860,
 -415,
   -3,
  364,
  676,
  927
};
/* *************************************************************
Filter type: Low Pass
Filter model: Butterworth
Filter order: 9
Sampling Frequency: 8 KHz
Cut Frequency: 0.215 KHz
Coefficents Quantization: 16-bit
***************************************************************/
static const int16_t taps_fir_lpf_215_9_88 = 88;
static const int32_t gain_fir_lpf_215_9_88 = 524288;
static const int16_t coef_fir_lpf_215_9_88[] = {
 2038,
 2049,
 1991,
 1859,
 1650,
 1363,
  999,
  562,
   58,
 -502,
-1106,
-1739,
-2382,
-3014,
-3612,
-4153,
-4610,
-4959,
-5172,
-5226,
-5098,
-4769,
-4222,
-3444,
-2430,
-1176,
  310,
 2021,
 3937,
 6035,
 8284,
10648,
13086,
15550,
17993,
20363,
22608,
24677,
26522,
28099,
29369,
30299,
30867,
31058,
30867,
30299,
29369,
28099,
26522,
24677,
22608,
20363,
17993,
15550,
13086,
10648,
 8284,
 6035,
 3937,
 2021,
  310,
-1176,
-2430,
-3444,
-4222,
-4769,
-5098,
-5226,
-5172,
-4959,
-4610,
-4153,
-3612,
-3014,
-2382,
-1739,
-1106,
 -502,
   58,
  562,
  999,
 1363,
 1650,
 1859,
 1991,
 2049,
 2038,
 1966
};
// end coef fir_lpf_215_9_88
//
/**************************************************************
Filter type: High Pass
Filter model: Butterworth
Filter order: 9
Sampling Frequency: 8 KHz
Cut Frequency: 0.300000 KHz
Coefficents Quantization: 16-bit
***************************************************************/
static const int16_t taps_fir_hpf_300_9_66 = 66;
static const int32_t gain_fir_hpf_300_9_66 = 32768;
static const int16_t coef_fir_hpf_300_9_66[] = 
{ 
 -141,
 -114,
  -77,
  -30,
   23,
   83,
  147,
  210,
  271,
  324,
  367,
  396,
  407,
  396,
  362,
  302,
  216,
  102,
  -36,
 -199,
 -383,
 -585,
 -798,
-1017,
-1237,
-1452,
-1653,
-1836,
-1995,
-2124,
-2219,
-2278,
30463,
-2278,
-2219,
-2124,
-1995,
-1836,
-1653,
-1452,
-1237,
-1017,
 -798,
 -585,
 -383,
 -199,
  -36,
  102,
  216,
  302,
  362,
  396,
  407,
  396,
  367,
  324,
  271,
  210,
  147,
   83,
   23,
  -30,
  -77,
 -114,
 -141,
 -158
    };
#endif /* !XPMR_COEF_H */
/* end of file */




