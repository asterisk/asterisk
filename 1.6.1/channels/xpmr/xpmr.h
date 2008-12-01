/*
 * xpmr.h - for Xelatec Private Mobile Radio Processes
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
 */

/*! \file
 *
 * \brief Private Land Mobile Radio Channel Voice and Signaling Processor
 *
 * \author Steven Henke, W9SH <sph@xelatec.com> Xelatec, LLC
 */

#ifndef  XPMR_H
#define  XPMR_H				1

#define  XPMR_DEV   		0 			// when running in test mode

#define  XPMR_TRACE_LEVEL	0

#ifdef	 RADIO_RTX
#define	 DTX_PROG			1			// rf transceiver module
#define  XPMR_PPTP			0 			// parallel port test probe
#else
#define	 DTX_PROG			0
#define  XPMR_PPTP			0 			
#endif

#if (DTX_PROG == 1) || 	XPMR_PPTP == 1
#include <parapindriver.h>
#endif

#ifdef	CHAN_USBRADIO
#define XPMR_DEBUG0		1
#define XPMR_TRACE		1
#define TRACEO(level,a) { if ( o->tracelevel >= level ) {printf a;} }
#else
#define XPMR_DEBUG0		1
#define XPMR_TRACE		1
#define TRACEO(level,a)
#endif


#define LSD_DFS			5
#define LSD_DFD			1

#if(XPMR_DEBUG0 == 1)
#define XPMR_DEBUG_CHANS	16
#define TSCOPE(a) {strace a;}
#else
#define XPMR_DEBUG_CHANS	0
#define TSCOPE(a)
#endif  

#define	XPMR_TRACE_AMP		8192

// TRACEM(3,TSYS_LSD,("pmr_lsdctl_exec() RX FRAME UNPROCESSED.\n"));
#if(XPMR_TRACE == 1)
#define TRACEX(a) {printf a;}
#define TRACEXL(a) {printf("%s @ %u : ",__FILE__ ,__LINE__); printf a; }
#define TRACEXT(a) {struct timeval hack; gettimeofday(&hack,NULL); printf("%ld.",hack.tv_sec%100000); printf("%i : ",(int)hack.tv_usec); printf a; }
#define TRACEXR(a) {printf a;}
#define TRACEC(level,a) {if(pChan->tracelevel>=level){printf("%08i ",pChan->frameCountRx);printf a;} }
#define TRACEF(level,a) {if(pChan->tracelevel>=level){printf a;} }
#define TRACEJ(level,a) {if(XPMR_TRACE_LEVEL>=level){printf a;} }
#define TRACES(level,a) {if(mySps->parentChan->tracelevel >= level){printf a;} }
#define TRACET(level,a) {if(pChan->tracelevel>=level){printf("%08i %02i",pChan->frameCountRx,pChan->rptnum);printf a;} }
#define TRACEXR(a) {printf a;}
#define TRACEM(level,sys,a) {if(pChan->tracelevel>=level || (pChan->tracesys[sys])){printf a;} }
#else						  
#define TRACEX(a)
#define TRACEXL(a)
#define TRACEXT(a)
#define TRACEC(level,a)
#define TRACEF(level,a)
#define TRACEJ(level,a)
#define TRACES(level,a)
#define TRACET(level,a)
#define TRACEXR(a)
#define TRACEM(level,sys,a)
#endif

#define i8  	int8_t
#define u8  	u_int8_t
#define i16		int16_t
#define u16 	u_int16_t
#define i32		int32_t
#define u32 	u_int32_t
#define i64 	int64_t
#define u64 	u_int64_t

#define M_Q31			0x80000000		//
#define M_Q30			0x40000000		//
#define M_Q29			0x20000000		//
#define M_Q28			0x10000000		//
#define M_Q27			0x08000000		//
#define M_Q26			0x04000000		//
#define M_Q25			0x02000000		//			
#define M_Q24			0x01000000		//
#define M_Q23			0x00800000		//
#define M_Q22			0x00400000		//					
#define M_Q21			0x00200000		// undsoweiter  		
#define M_Q20			0x00100000		// 1048576
#define M_Q19			0x00080000		// 524288		   
#define M_Q18			0x00040000		// 262144
#define M_Q17           0x00020000		// 131072
#define M_Q16           0x00010000		// 65536
#define M_Q15           0x00008000		// 32768
#define M_Q14           0x00004000		// 16384
#define M_Q13           0x00002000		// 8182
#define M_Q12           0x00001000		// 4096
#define M_Q11           0x00000800		// 2048
#define M_Q10           0x00000400		// 1024
#define M_Q9            0x00000200		// 512
#define M_Q8            0x00000100		// 256
#define M_Q7            0x00000080		// 128
#define M_Q6            0x00000040		// 64
#define M_Q5            0x00000020		// 32
#define M_Q4            0x00000010		// 16
#define M_Q3            0x00000008		// 16
#define M_Q2            0x00000004		// 16
#define M_Q1            0x00000002		// 16
#define M_Q0            0x00000001		// 16

#define RADIANS_PER_CYCLE		(2*M_PI)

#define SAMPLE_RATE_INPUT       48000
#define SAMPLE_RATE_NETWORK     8000

#define SAMPLES_PER_BLOCK       160
#define MS_PER_FRAME            20
#define SAMPLES_PER_MS          8

#define CTCSS_NULL              -1
#define CTCSS_RXONLY            -2
#define CTCSS_NUM_CODES			38 			// 0 - 37
#define CTCSS_SCOUNT_MUL		100
#define CTCSS_INTEGRATE	  		3932       // 32767*.120 // 120/1000  // 0.120
#define CTCSS_INPUT_LIMIT	  	1000
#define CTCSS_DETECT_POINT	  	1989
#define CTCSS_HYSTERSIS	  	    200

#define CTCSS_TURN_OFF_TIME		160			// ms
#define CTCSS_TURN_OFF_SHIFT    240			// degrees
#define TOC_NOTONE_TIME			600			// ms

#define	DDB_FRAME_SIZE			160		   	// clock de-drift defaults
#define DDB_FRAMES_IN_BUFF		8
#define DDB_ERR_MODULUS 		10000

#define DCS_TURN_OFF_TIME       180

#define	NUM_TXLSD_FRAMEBUFFERS	4

#define CHAN_TXSTATE_IDLE		0
#define CHAN_TXSTATE_ACTIVE		1
#define CHAN_TXSTATE_TOC		2
#define CHAN_TXSTATE_HANGING    3
#define CHAN_TXSTATE_FINISHING  4
#define CHAN_TXSTATE_COMPLETE	5
#define CHAN_TXSTATE_USURPED	9

#define SMODE_NULL				0
#define SMODE_CARRIER			1
#define SMODE_CTCSS				2
#define SMODE_DCS       		3
#define SMODE_LSD				4
#define SMODE_MPT				5
#define SMODE_DST				6
#define SMODE_P25				7
#define SMODE_MDC				8


#define SPS_OPT_START			1
#define SPS_OPT_STOP			2
#define SPS_OPT_TURNOFF         3
#define SPS_OPT_STOPNOW			4

#define SPS_STAT_STOPPED		0
#define SPS_STAT_STARTING		1
#define SPS_STAT_RUNNING		2
#define SPS_STAT_HALTING		3

 
#define PP_BIT_TEST		6
#define PP_REG_LEN		32
#define PP_BIT_TIME		100000
					  	
#define DTX_CLK 	LP_PIN02
#define DTX_DATA 	LP_PIN03
#define DTX_ENABLE 	LP_PIN04
#define DTX_TX 	    LP_PIN05		// only used on older mods
#define DTX_TXPWR 	LP_PIN06		// not used
#define DTX_TP1 	LP_PIN07		// not used
#define DTX_TP2 	LP_PIN08		// not used

#define BIN_PROG_0 	LP_PIN06
#define BIN_PROG_1 	LP_PIN07
#define BIN_PROG_2 	LP_PIN08
#define BIN_PROG_3 	LP_PIN09 
		 
#ifndef CHAN_USBRADIO  
enum {RX_AUDIO_NONE,RX_AUDIO_SPEAKER,RX_AUDIO_FLAT};
enum {TX_AUDIO_NONE,TX_AUDIO_FLAT,TX_AUDIO_FILTERED,TX_AUDIO_PROC};
enum {CD_IGNORE,CD_XPMR_NOISE,CD_XPMR_VOX,CD_HID,CD_HID_INVERT};
enum {SD_IGNORE,SD_HID,SD_HID_INVERT,SD_XPMR};    				 // no,external,externalinvert,software
enum {RX_KEY_CARRIER,RX_KEY_CARRIER_CODE};
enum {TX_OUT_OFF,TX_OUT_VOICE,TX_OUT_LSD,TX_OUT_COMPOSITE,TX_OUT_AUX};
enum {TOC_NONE,TOC_PHASE,TOC_NOTONE};
#endif

enum dbg_pts {
 
RX_INPUT,	
RX_NOISE_AMP, 
RX_NOISE_TRIG,

RX_CTCSS_LPF,
RX_CTCSS_CENTER,
RX_CTCSS_NRZ,
RX_CTCSS_CLK,
RX_CTCSS_P0,  
RX_CTCSS_P1,
RX_CTCSS_ACCUM,
RX_CTCSS_DVDT,
RX_CTCSS_DECODE,

RX_DCS_CENTER,
RX_DCS_DEC,
RX_DCS_DIN,
RX_DCS_CLK,
RX_DCS_DAT,

RX_LSD_LPF,
RX_LSD_CLK,
RX_LSD_DAT,
RX_LSD_DEC,

RX_LSD_CENTER,
RX_LSD_SYNC,  
RX_LSD_STATE,
RX_LSD_ERR,
RX_LSD_INTE,

RX_SMODE,

TX_PTT_IN,
TX_PTT_OUT,

TX_DEDRIFT_LEAD,
TX_DEDRIFT_ERR,
TX_DEDRIFT_FACTOR,
TX_DEDRIFT_DRIFT,
TX_DEDRIFT_TWIDDLE,

TX_CTCSS_GEN,

TX_SIGGEN_0,

TX_DCS_CLK,
TX_DCS_DAT,
TX_DCS_LPF,

TX_LSD_CLK,
TX_LSD_DAT,
TX_LSD_GEN,  	
TX_LSD_LPF,

TX_NET_INT,
TX_VOX_HPF,
TX_VOX_LIM,

TX_VOX_LPF,

TX_OUT_A,
TX_OUT_B,

NUM_DEBUG_PTS  
};

typedef struct
{
	i16 mode;
	i16	point[NUM_DEBUG_PTS];
	i16 trace[16];
	i16 scale[16];
	i16 offset[16];
	i16 buffer[16 * SAMPLES_PER_BLOCK];  // allocate for rx and tx
	i16 *source[16];
} t_sdbg;

typedef struct
{
	i16 lock;
	i16 option;					// 1 = data in, 0 = data out
	i16 debug;
	i16 debugcnt;
	i32 rxframecnt;
	i32 txframecnt;

	i32 skew;

	i16 frames;
	i16 framesize;
	i16 buffersize;

	i32 timer;
	
	i32 x0,x1,y0,y1;

	i16 inputindex;
	i16 outputindex;
	i16 lead;
	i16 err;
	i16 accum;

	i16 *ptr;					// source or destination
	i16	*buff;
	
	i16 inputcnt;
	i16 initcnt;

	i32 factor;
	i32 drift;
	i32 modulus;
	i32	z1;
	struct {
		unsigned rxlock:1;
		unsigned txlock:1;
		unsigned twiddle:1;
		unsigned doitnow:1;
	}b;
}
t_dedrift;

/*
	one structure for each ctcss tone to decode 
*/
typedef struct
{
	i16 counter;			// counter to next sample
	i16 counterFactor;		// full divisor used to increment counter
	i16 binFactor;
	i16 fudgeFactor;
	i16 peak;				// peak amplitude now	maw sph now
	i16 enabled;
	i16 state;				// dead, running, error				 
	i16 zIndex;				// z bucket index
	i16 z[4];	  			 
	i16 zi;
	i16 dvu;
	i16 dvd;
	i16 zd;
	i16 setpt;
	i16 hyst;
	i16 decode;
	i16 diffpeak;
	i16 debug;				 

	#if XPMR_DEBUG0 == 1
	i16 lasttv0;
	i16 lasttv1;
	i16 lasttv2;
	i16 lasttv3;

	i16 *pDebug0;			// pointer to debug output
	i16 *pDebug1;			// pointer to debug output
	i16 *pDebug2;			// pointer to debug output
	i16 *pDebug3;			// pointer to debug output
	#endif

} t_tdet;

typedef struct
{
	i16 enabled;						// if 0 none, 0xFFFF all tones, or single tone
	i16 *input;							// source data
	i16 clamplitude;
	i16 center;
	i16 decode;		  					// current ctcss decode index
	i32 BlankingTimer;
	u32 TurnOffTimer;
	i16 gain;
	i16 limit;
	i16 debugIndex;
	i16 *pDebug0;
	i16 *pDebug1;
	i16 *pDebug2;
	i16 *pDebug3;
	i16 testIndex;
	i16 multiFreq;
	i8 relax;
	t_tdet tdet[CTCSS_NUM_CODES];

	i8 		numrxcodes;
	i16 	rxCtcssMap[CTCSS_NUM_CODES];
	char    *rxctcss[CTCSS_NUM_CODES];		// pointers to each tone in string above
	char    *txctcss[CTCSS_NUM_CODES];

	i32		txctcssdefault_index;
	float 	txctcssdefault_value;

	struct{
		unsigned valid:1;
	}b;
} t_dec_ctcss;

/*
	Low Speed Data
*/
/* 
	general purpose pmr signal processing element 
*/

struct t_pmr_chan;

typedef struct t_pmr_sps
{
	i16  index;		  	// unique to each instance

	i16  enabled;		// enabled/disabled


	struct t_pmr_chan *parentChan;
	
	i16  *source;		// source buffer
	i16  *sourceB;		// source buffer B
	i16  *sink;			// sink buffer

	i16  numChanOut;	// allows output direct to interleaved buffer
	i16  selChanOut;

	i32  ticks;			
	i32  timer;
	i32  count;

	void *buff;			// this structure's internal buffer

	i16  *debugBuff0;	// debug buffer
	i16  *debugBuff1;	// debug buffer
	i16  *debugBuff2;	// debug buffer
	i16  *debugBuff3;	// debug buffer

	i16  nSamples;		// number of samples in the buffer

	u32	 buffSize;		// buffer maximum index
	u32  buffInIndex;	// index to current input point
	u32  buffOutIndex;	// index to current output point
	u32  buffLead;		// lead of input over output through cb

	i16  decimate;		// decimation or interpolation factor (could be put in coef's)
	i16  interpolate;
	i16	 decimator;		// like the state this must be saved between calls (could be put in x's)

	u32  sampleRate;    // in Hz for elements in this structure
	u32  freq;			// in 0.1 Hz

	i16  measPeak;		// do measure Peak
	i16  amax;			// buffer amplitude maximum
	i16  amin;			// buffer amplitude minimum
	i16  apeak;			// buffer amplitude peak value (peak to peak)/2
	i16  setpt;			// amplitude set point for amplitude comparator
	i16  hyst;			// hysterysis for amplitude comparator
	i16  compOut;		// amplitude comparator output

	i32  discounteru;	// amplitude detector integrator discharge counter upper
	i32  discounterl;	// amplitude detector integrator discharge counter lower
	i32  discfactor;	// amplitude detector integrator discharge factor

	i16  err;			// error condition
	i16  option;		// option / request zero
	i16  state;         // stopped, start, stopped assumes zero'd
	
	i16  pending;

	struct {
		unsigned hit:1;
		unsigned hitlast:1;
		unsigned hita:1;
		unsigned hitb:1;
		unsigned bithit:1;
		unsigned now:1;
		unsigned next:1;
		unsigned prev:1;
		unsigned clock:1;
		unsigned hold:1;
		unsigned opt1:1;
		unsigned opt2:1;
		unsigned polarity:1;
		unsigned dotting:1;
		unsigned lastbitpending:1;
		unsigned outzero:1;
		unsigned settling:1;
		unsigned syncing:1;
	}b;

	i16  cleared;		// output buffer cleared

	i16  delay;
	i16  decode;

	i32  inputGain;	  	// apply to input data	 ? in Q7.8 format
	i32  inputGainB;	// apply to input data	 ? in Q7.8 format
	i32  outputGain;	// apply to output data  ? in Q7.8 format
	i16  mixOut;
	i16  monoOut;

	i16  filterType;	// iir, fir, 1, 2, 3, 4 ...

	i16 (*sigProc)(struct t_pmr_sps *sps);	// function to call

	i32	 calcAdjust;	// final adjustment
	i16	 nx;	 		// number of x history elements
	i16  ncoef;			// number of coefficients
	i16  size_x;		// size of each x history element
	i16  size_coef;		// size of each coefficient
	void  *x;			// history registers
	void  *x2;			// history registers, 2nd bank 
	void  *coef;		// coefficients
	void  *coef2;		// coefficients 2

	void  *nextSps;		// next Sps function

} t_pmr_sps;


struct t_dec_dcs;
struct t_lsd_control;
struct t_decLsd;;	
struct t_encLsd;

/*
	pmr channel
*/
typedef struct	t_pmr_chan
{
	i16 index;				// which one
	i16 devicenum;			// belongs to

	char *name;

	i16 enabled;			// enabled/disabled
	i16 status;				// ok, error, busy, idle, initializing
	
	i16  tracelevel;
	i16  tracetype;
	u32  tracemask;

	i16 nSamplesRx;			// max frame size
	i16 nSamplesTx;

	i32 inputSampleRate;	// in S/s  48000
	i32 baseSampleRate;		// in S/s   8000 

	i16 inputGain;
	i16 inputOffset;

	i32  ticks;				// time ticks
	u32  frameCountRx;		// number processed
	u32  frameCountTx;

	i8   txframelock;

	i32  txHangTime;
	i32  txHangTimer;
	i32  txTurnOff;
	i16  txBufferClear;

	u32 txfreq;
	u32 rxfreq;
	i8  txpower;

	i32 txsettletime;		// in samples
	i32 txsettletimer;

	i16 rxDC;			    // average DC value of input
	i16 rxSqSet;			// carrier squelch threshold
	i16 rxSqHyst;			// carrier squelch hysterysis
	i16 rxRssi;				// current Rssi level
	i16 rxQuality;			// signal quality metric
	i16 rxCarrierDetect;    // carrier detect
	i16 rxCdType;
	i16 rxSqVoxAdj;
	i16 rxExtCarrierDetect; 
	i32 inputBlanking;  	// Tx pulse eliminator

	i16 rxDemod;   		// see enum
	i16 txMod;			//

	i16 rxNoiseSquelchEnable;
	i16 rxHpfEnable;
	i16 rxDeEmpEnable;
	i16 rxCenterSlicerEnable;
	i16 rxCtcssDecodeEnable;
	i16 rxDcsDecodeEnable;
	i16 rxDelayLineEnable;

	i16 txHpfEnable;
	i16 txLimiterEnable;
	i16 txPreEmpEnable;
	i16 txLpfEnable;

	char radioDuplex;

	char    *pStr;

	// 		start channel signaling codes source
	char 	*pRxCodeSrc;					// source
	char 	*pTxCodeSrc;					// source
	char 	*pTxCodeDefault;				// source
	// 		end channel signaling codes source

	// 		start signaling code info derived from source
	i16  	numrxcodes;
	i16  	numtxcodes;
	char 	*pRxCodeStr;					// copied and cut up
	char 	**pRxCode;						// pointers to subs
	char 	*pTxCodeStr;					 
	char 	**pTxCode;

	char 	txctcssdefault[16];				// codes from higher level

	char	*rxctcssfreqs; 					// rest are derived from this 
	char    *txctcssfreqs;
	
	char    numrxctcssfreqs;
	char    numtxctcssfreqs;

	char    *rxctcss[CTCSS_NUM_CODES];		// pointers to each tone in string above
	char    *txctcss[CTCSS_NUM_CODES];

	i16 	rxCtcssMap[CTCSS_NUM_CODES];

	i8		txcodedefaultsmode;
	i16		txctcssdefault_index;
	float 	txctcssdefault_value;

	char	txctcssfreq[32];				// encode now
	char    rxctcssfreq[32];				// decode now
	// 		end most of signaling code info derived from source

	struct t_lsd_control	*pLsdCtl;

	i16  rptnum;
	i16  area;
	char *ukey;
	u32  idleinterval;
	char turnoffs;

	char pplock;

	t_dedrift	dd;

	i16 dummy;

	i32 txScramFreq;
	i32 rxScramFreq;

	i16 gainVoice;
	i16 gainSubAudible;

	i16 txMixA;				// Off, Ctcss, Voice, Composite
	i16 txMixB;				// Off, Ctcss, Voice, Composite
	
	i16 rxMuting;

	i16 rxCpuSaver;
	i16 txCpuSaver;

	i8	rxSqMode;			// 0 open, 1 carrier, 2 coded

	i8	cdMethod;

	i16	rxSquelchPoint;

	i16 rxCarrierPoint;
	i16 rxCarrierHyst;
	
	i16 txCtcssTocShift;
	i16 txCtcssTocTime;
	i8	txTocType;

	i16 smode;	  							// ctcss, dcs, lsd
	i16 smodecode;
	i16 smodewas;	  						// ctcss, dcs, lsd
	i32 smodetimer;	  						// in ms
	i32 smodetime;							// to set in ms

	t_dec_ctcss			*rxCtcss;
	struct t_dec_dcs	*decDcs;
	struct t_decLsd 	*decLsd;		 				
	struct t_encLsd		*pLsdEnc;

	i16 clamplitudeDcs;
	i16 centerDcs;
	u32 dcsBlankingTimer;
	i16 dcsDecode;							// current dcs decode value

	i16 clamplitudeLsd;
	i16 centerLsd;


	i16 txPttIn;	 		// from external request
	i16 txPttOut;			// to radio hardware
	i16 txPttHid;

	i16 bandwidth;			// wide/narrow
	i16 txCompand;			// type
	i16 rxCompand;			// 
	
	i16 txEqRight;			// muted, flat, pre-emp limited filtered
	i16 txEqLeft;

	i16 txPotRight;			// 
	i16 txPotLeft;			//

	i16 rxPotRight;			// 
	i16 rxPotLeft;			//

	i16 function;

	i16 txState;				// off,settling,on,hangtime,turnoff

	i16 spsIndex;

	t_pmr_sps *spsMeasure;		// measurement block

	t_pmr_sps *spsRx;			// 1st signal processing struct
	t_pmr_sps *spsRxLsd;
	t_pmr_sps *spsRxLsdNrz;
	t_pmr_sps *spsRxDeEmp;
	t_pmr_sps *spsRxHpf;
	t_pmr_sps *spsRxVox;
	t_pmr_sps *spsDelayLine;	// Last signal processing struct
	t_pmr_sps *spsRxOut;		// Last signal processing struct

	t_pmr_sps *spsTx;			// 1st  signal processing struct
	
	t_pmr_sps *spsTxOutA;		// Last signal processing struct
	t_pmr_sps *spsTxOutB;		// Last signal processing struct

	t_pmr_sps *spsSigGen0;		// ctcss
	t_pmr_sps *spsSigGen1;		// test and other tones
	t_pmr_sps *spsLsdGen;
	t_pmr_sps *spsTxLsdLpf;

	// tune tweaks

	i32 rxVoxTimer;				// Vox Hang Timer

	i16	*prxSquelchAdjust;

	// i16	*prxNoiseMeasure;	// for autotune
	// i32	*prxNoiseAdjust;

	i16	*prxVoiceMeasure;
	i32	*prxVoiceAdjust;	
	
	i16	*prxCtcssMeasure;
	i32	*prxCtcssAdjust;		 
	
	i16	*ptxVoiceAdjust;		// from calling application
	i32	*ptxCtcssAdjust;		// from calling application

	i32	*ptxLimiterAdjust;		// from calling application

	struct {
		unsigned pmrNoiseSquelch:1;
		unsigned rxHpf:1;
		unsigned txHpf:1;
		unsigned txLpf:1;
		unsigned rxDeEmphasis:1;
		unsigned txPreEmphasis:1;
		unsigned startSpecialTone:1;
		unsigned stopSpecialTone:1;
		unsigned doingSpecialTone:1;
		unsigned extCarrierDetect:1;
		unsigned txCapture:1;
		unsigned rxCapture:1;
		unsigned reprog:1;
		unsigned radioactive:1;
		unsigned rxplmon:1;
		unsigned remoted:1;
		unsigned loopback:1;
		unsigned rxpolarity:1;
		unsigned txpolarity:1;
		unsigned dcsrxpolarity:1;
		unsigned dcstxpolarity:1;
		unsigned lsdrxpolarity:1;
		unsigned lsdtxpolarity:1;
		unsigned txsettling:1;
		unsigned smodeturnoff:1;

		unsigned ctcssRxEnable:1;
		unsigned ctcssTxEnable:1;
		unsigned dcsRxEnable:1;
		unsigned dcsTxEnable:1;
		unsigned lmrRxEnable:1;
		unsigned lmrTxEnable:1;
		unsigned mdcRxEnable:1;
		unsigned mdcTxEnable:1;
		unsigned dstRxEnable:1;
		unsigned dstTxEnable:1;
		unsigned p25RxEnable:1;
		unsigned p25TxEnable:1;
		unsigned ax25Enable:1;

		unsigned txCtcssInhibit:1;

		unsigned rxkeyed:1;
		unsigned rxhalted:1;
		unsigned txhalted:1;
		unsigned pptp_p1:1;
		unsigned pptp_p2:1;
		unsigned tuning:1;
		unsigned pttwas:1;
	}b;

	i16 *pRxDemod;				// buffers
	i16 *pRxBase;	 			// decimated lpf input
	i16 *pRxNoise;   
	i16 *pRxLsd;				// subaudible only 
	i16 *pRxHpf;				// subaudible removed
	i16 *pRxDeEmp;        		// EIA Audio
	i16 *pRxSpeaker;        	// EIA Audio
	i16 *pRxDcTrack;			// DC Restored LSD
	i16 *pRxLsdLimit;         	// LSD Limited
	i16 *pRxCtcss;				//
	i16 *pRxSquelch;
	i16 *prxVoxMeas;				
	i16 *prxMeasure;

	i16 *pTxInput;				// input data
	i16 *pTxBase;				// input data
	i16 *pTxHpf;
	i16 *pTxPreEmp;
	i16 *pTxLimiter;
	i16 *pTxLsd;
	i16 *pTxLsdLpf;
	i16 *pTxComposite;
	i16 *pTxMod;			// upsampled, low pass filtered
	
	i16 *pTxOut;			// 

	i16	*pSigGen0;
	i16	*pSigGen1;

	i16 *pAlt0;
	i16 *pAlt1;

	i16 *pNull;

	#if XPMR_DEBUG0 == 1

	i16 *pRxLsdCen;

	i16 *pTstTxOut;

	i16 *prxDebug;			// consolidated debug buffer
	i16 *ptxDebug;			// consolidated debug buffer

	i16 *prxDebug0;
	i16 *prxDebug1;
	i16 *prxDebug2;
	i16 *prxDebug3;

	i16 *ptxDebug0;
	i16 *ptxDebug1;
	i16 *ptxDebug2;
	i16 *ptxDebug3;

	#endif

	i16 numDebugChannels;

	t_sdbg	*sdbg;

} t_pmr_chan;

/*
	function prototype declarations
*/
void 		strace(i16 point, t_sdbg *sdbg, i16 index, i16 value);
void 		strace2(t_sdbg *sdbg);

static i16	TxTestTone(t_pmr_chan *pChan, i16 function);
t_pmr_chan	*createPmrChannel(t_pmr_chan *tChan, i16 numSamples);
t_pmr_sps 	*createPmrSps(t_pmr_chan *pChan);
i16			destroyPmrChannel(t_pmr_chan *pChan);
i16			destroyPmrSps(t_pmr_sps  *pSps);
i16 		pmr_rx_frontend(t_pmr_sps *mySps);
i16 		pmr_gp_fir(t_pmr_sps *mySps);
i16 		pmr_gp_iir(t_pmr_sps *mySps);
i16 		gp_inte_00(t_pmr_sps *mySps);
i16 		gp_diff(t_pmr_sps *mySps);
i16 		CenterSlicer(t_pmr_sps *mySps);
i16 		ctcss_detect(t_pmr_chan *pmrChan);
i16 		SoftLimiter(t_pmr_sps *mySps);
i16			SigGen(t_pmr_sps *mySps);
i16 		pmrMixer(t_pmr_sps *mySps);
i16 		DelayLine(t_pmr_sps *mySps);

i16			PmrRx(t_pmr_chan *PmrChan, i16 *input, i16 *outputrx, i16 *outputtx );
i16			PmrTx(t_pmr_chan *PmrChan, i16 *input);

i16 		string_parse(char *src, char **dest, char ***ptrs);
i16 		code_string_parse(t_pmr_chan *pChan);

i16 		CtcssFreqIndex(float freq);
i16 		MeasureBlock(t_pmr_sps *mySps);

void		dedrift			(t_pmr_chan *pChan);
void 		dedrift_write	(t_pmr_chan *pChan, i16 *src);

void		ppspiout	(u32 spidata);
void		progdtx		(t_pmr_chan *pChan);
void		ppbinout	(u8 chan);

#if XPMR_PPTP == 1
void		pptp_init 		(void);
void		pptp_write		(i16 bit, i16 state);
#endif

#endif /* ! XPMR_H */

/* end of file */



