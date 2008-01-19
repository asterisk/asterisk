/*
 * xpmr.h - for Xelatec Private Mobile Radio Processes
 * 
 * All Rights Reserved. Copyright (C)2007, Xelatec, LLC
 * 
 * 20070808 1235 Steven Henke, W9SH, sph@xelatec.com
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
 * \brief Private Land Mobile Radio Channel Voice and Signaling Processor
 *
 * \author Steven Henke, W9SH <sph@xelatec.com> Xelatec, LLC
 */

#ifndef XPMR_H
#define XPMR_H			1

#ifdef	CHAN_USBRADIO
#define XPMR_DEBUG0		1
#define XPMR_TRACE		0
#else
#define XPMR_DEBUG0		1
#define XPMR_TRACE		1
#endif

#if(XPMR_TRACE == 1)
#define TRACEX(a) {printf a;}
#define TRACEXL(a) {printf("%s @ %u : ",__FILE__ ,__LINE__); printf a; }
#define TRACEXT(a) { struct timeval hack; gettimeofday(&hack,NULL); printf("%ld.",hack.tv_sec%100000); printf("%i : ",(int)hack.tv_usec); printf a; }
#else						  
#define TRACEX(a)
#define TRACEXL(a)
#define TRACEXT(a)
#endif

#define i8  	int8_t
#define u8  	u_int8_t
#define i16		int16_t
#define u16 	u_int16_t
#define i32		int32_t
#define u32 	u_int32_t
#define i64 	int64_t
#define u64 	u_int64_t
			
#define M_Q24			0x01000000		//
#define M_Q23			0x00800000		//
#define M_Q22			0x00400000		//					
#define M_Q21			0x00200000		//  		
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

#define CTCSS_NUM_CODES			38
#define CTCSS_SCOUNT_MUL		100
#define CTCSS_INTEGRATE	  		3932       // 32767*.120 // 120/1000  // 0.120
#define CTCSS_INPUT_LIMIT	  	1000
#define CTCSS_DETECT_POINT	  	1989
#define CTCSS_HYSTERSIS	  	    200

#define CTCSS_TURN_OFF_TIME		160			// ms
#define CTCSS_TURN_OFF_SHIFT    240			// degrees
#define TOC_NOTONE_TIME			600			// ms

#ifndef CHAN_USBRADIO  
enum {RX_AUDIO_NONE,RX_AUDIO_SPEAKER,RX_AUDIO_FLAT};
enum {TX_AUDIO_NONE,TX_AUDIO_FLAT,TX_AUDIO_FILTERED,TX_AUDIO_PROC};
enum {CD_IGNORE,CD_XPMR_NOISE,CD_XPMR_VOX,CD_HID,CD_HID_INVERT};
enum {SD_IGNORE,SD_HID,SD_HID_INVERT,SD_XPMR};    				 // no,external,externalinvert,software
enum {RX_KEY_CARRIER,RX_KEY_CARRIER_CODE};
enum {TX_OUT_OFF,TX_OUT_VOICE,TX_OUT_LSD,TX_OUT_COMPOSITE,TX_OUT_AUX};
enum {TOC_NONE,TOC_PHASE,TOC_NOTONE};
#endif

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
	i16 z[4];	  			// maw sph today
	i16 zi;
	i16 dvu;
	i16 dvd;
	i16 zd;
	i16 setpt;
	i16 hyst;
	i16 decode;
	i16 diffpeak;
	i16 debug;				// value held from last pass
	i16 *pDebug0;			// pointer to debug output
	i16 *pDebug1;			// pointer to debug output
	i16 *pDebug2;			// pointer to debug output

} t_tdet;

typedef struct
{
	i16 enabled;						// if 0 none, 0xFFFF all tones, or single tone
	i16 *input;
	i16 clamplitude;
	i16 center;
	i16 decode;		  					// current ctcss decode index
	i32 BlankingTimer;
	u32 TurnOffTimer;
	t_tdet tdet[CTCSS_NUM_CODES];	
	i16 gain;
	i16 limit;
	i16 *pDebug0;
	i16 *pDebug1;
	i16 *pDebug2;
	i16 testIndex;
	i16 multiFreq;
	i8 relax;

} t_dec_ctcss;

typedef struct
{
	i16 enabled;						// if 0 none, 0xFFFF all tones, or single tone
	i16 clamplitude;
	i16 center;
	i16 decode;		  				// current ctcss decode value
	i32 BlankingTimer;
	u32 TurnOffTimer;
	i16 gain;
	i16 limit;
	i16 *pDebug0;
	i16 *pDebug1;
	i16 rxPolarity;
} t_dec_dcs;

/*
	Low Speed Data decoding both polarities
*/
typedef struct
{
	i16 counter;			// counter to next sample
	i16 synced;
	u32 syncCorr[2];
	u32 data[2];
	i16 state;				// disabled, enabled,
	i16 decode;
	i16 debug;

	i16 polarity;
	u32 frameNum;

	u16 area;
	u16 chan;
	u16 home;
	u16 id;
	u16 free;

	u16 crc;
	i16 rssi;

} t_decLsd;


/* general purpose pmr signal processing element */

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

	u32  ticks;

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

/*
	pmr channel
*/
typedef struct	t_pmr_chan
{
	i16 index;				// which one
	i16 enabled;			// enabled/disabled
	i16 status;				// ok, error, busy, idle, initializing

	i16 nSamplesRx;			// max frame size
	i16 nSamplesTx;

	i32 inputSampleRate;	// in S/s  48000
	i32 baseSampleRate;		// in S/s   8000 

	i16 inputGain;
	i16 inputOffset;

	u32  frameCountRx;		// number processed
	u32  frameCountTx;

	i32  txHangTime;
	i32  txTurnOff;

	i16 rxDC;			    // average DC value of input
	i16 rxSqSet;			// carrier squelch threshold
	i16 rxSqHyst;			// carrier squelch hysterysis
	i16 rxRssi;				// current Rssi level
	i16 rxQuality;			// signal quality metric
	i16 rxCarrierDetect;    // carrier detect
	i16 rxCdType;
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
	}b;

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

	i16 rxCtcssMap[CTCSS_NUM_CODES];
	
	i16 txCtcssTocShift;
	i16 txCtcssTocTime;
	i8	txTocType;

	float txCtcssFreq;
	float rxCtcssFreq;
	float rxInputGain;
	
	i16 rxCtcssIndex;

	i16 txPttIn;	 		// from external request
	i16 txPttOut;			// to radio hardware

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

	i16 txState;			// off,settling,on,hangtime,turnoff

	t_pmr_sps *spsMeasure;	// measurement block

	t_pmr_sps *spsRx;			// 1st signal processing struct
	t_pmr_sps *spsRxLsd;
	t_pmr_sps *spsRxDeEmp;
	t_pmr_sps *spsRxHpf;
	t_pmr_sps *spsRxVox;
	t_pmr_sps *spsDelayLine;	// Last signal processing struct
	t_pmr_sps *spsRxOut;		// Last signal processing struct

	t_pmr_sps *spsTx;			// 1st  signal processing struct

	t_pmr_sps *spsTxLsdLpf;
	t_pmr_sps *spsTxOutA;		// Last signal processing struct

	t_pmr_sps *spsTxOutB;		// Last signal processing struct

	t_pmr_sps *spsSigGen0;		// ctcss
	t_pmr_sps *spsSigGen1;		// test and other tones

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

	i16 *pTxBase;				// input data
	i16 *pTxHpf;
	i16 *pTxPreEmp;
	i16 *pTxLimiter;
	i16 *pTxLsd;
	i16 *pTxLsdLpf;
	i16 *pTxComposite;
	i16 *pTxMod;			// upsampled, low pass filtered
	
	i16 *pTxOut;			// 
	
	i16 *pTxPttIn;
	i16 *pTxPttOut;
	i16 *pTxHang;
	i16 *pTxCode;

	i16	*pSigGen0;
	i16	*pSigGen1;

	i16 *pAlt0;
	i16 *pAlt1;

	i16 *pNull;

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

	t_dec_ctcss	*rxCtcss;
			  
	i16 clamplitudeDcs;
	i16 centerDcs;
	u32 dcsBlankingTimer;
	i16 dcsDecode;							// current dcs decode value

	i16 clamplitudeLsd;
	i16 centerLsd;
	t_decLsd decLsd[2];		 				// for both polarities

} t_pmr_chan;

static i16			TxTestTone(t_pmr_chan *pChan, i16 function);

t_pmr_chan	*createPmrChannel(t_pmr_chan *tChan, i16 numSamples);
t_pmr_sps 	*createPmrSps(void);
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
i16			PmrRx(t_pmr_chan *PmrChan, i16 *input, i16 *output);
i16			PmrTx(t_pmr_chan *PmrChan, i16 *input, i16 *output);
i16 		CtcssFreqIndex(float freq);
i16 		MeasureBlock(t_pmr_sps *mySps);
#endif /* ! XPMR_H */

/* end of file */



