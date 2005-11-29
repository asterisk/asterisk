 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    iLBC_define.h     
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
#include <string.h> 
 
#ifndef __iLBC_ILBCDEFINE_H 
#define __iLBC_ILBCDEFINE_H 
 
/* general codec settings */ 
 
#define FS (float)8000.0 
#define BLOCKL 240 
#define NSUB 6  
#define NASUB 4  
#define SUBL 40 
#define STATE_LEN 80 
#define STATE_SHORT_LEN 58 
 
/* LPC settings */ 
 
#define LPC_FILTERORDER 10 
#define LPC_CHIRP_SYNTDENUM (float)0.9025 
#define LPC_CHIRP_WEIGHTDENUM (float)0.4222 
#define LPC_LOOKBACK 60 
#define LPC_N 2 
#define LPC_ASYMDIFF 20 
#define LPC_BW (float)60.0 
#define LPC_WN (float)1.0001 
#define LSF_NSPLIT 3 
#define LSF_NUMBER_OF_STEPS 4 
#define LPC_HALFORDER LPC_FILTERORDER/2 
 
/* cb settings */ 
 
#define CB_NSTAGES 3 
#define CB_EXPAND 2 
#define CB_MEML 147 
#define CB_FILTERLEN 2*4 
#define CB_HALFFILTERLEN 4 
#define CB_RESRANGE 34 
#define CB_MAXGAIN (float) 1.3  
 
/* enhancer */ 
 
#define ENH_BLOCKL 80   /* block length */ 
#define ENH_BLOCKL_HALF (ENH_BLOCKL/2) 
#define ENH_HL 3    /* 2*ENH_HL+1 is number blocks 
                           in said second sequence */ 
#define ENH_SLOP 2      /* max difference estimated and 
                           correct pitch period */ 
#define ENH_PLOCSL 20   /* pitch-estimates and 
                           pitch-locations buffer length */ 
#define ENH_OVERHANG 2 
#define ENH_UPS0 4      /* upsampling rate */ 
#define ENH_FL0 3       /* 2*FLO+1 is the length of each filter */ 
#define ENH_VECTL (ENH_BLOCKL+2*ENH_FL0) 
#define ENH_CORRDIM (2*ENH_SLOP+1) 
#define ENH_NBLOCKS (BLOCKL/ENH_BLOCKL) 
#define ENH_NBLOCKS_EXTRA 5 
#define ENH_NBLOCKS_TOT 8 /* ENH_NBLOCKS+ENH_NBLOCKS_EXTRA */ 
#define ENH_BUFL (ENH_NBLOCKS_TOT)*ENH_BLOCKL 
#define ENH_ALPHA0 (float)0.05 
 
/* PLC */ 
 
#define PLC_BFIATTENUATE (float)0.9 
#define PLC_GAINTHRESHOLD (float)0.5 
#define PLC_BWEXPAND (float)0.99 
#define PLC_XT_MIX (float)1.0 
#define PLC_XB_MIX (float)0.0 
#define PLC_YT_MIX (float)0.95 
#define PLC_YB_MIX (float)0.0 
 
/* Down sampling */ 
 
#define FILTERORDER_DS 7 
#define DELAY_DS 3 
#define FACTOR_DS 2 
 
/* bit stream defs */ 
 
#define NO_OF_BYTES 50 
#define STATE_BITS 3 
#define BYTE_LEN 8 
#define ULP_CLASSES 3 
 
/* help parameters */ 
 
#define FLOAT_MAX (float)1.0e37 
#define EPS (float)2.220446049250313e-016  
#define PI (float)3.14159265358979323846 
#define MIN_SAMPLE -32768 
#define MAX_SAMPLE 32767 
#define TWO_PI (float)6.283185307 
#define PI2 (float)0.159154943 
 
/* type definition encoder instance */ 
typedef struct iLBC_Enc_Inst_t_ { 
 
    /* analysis filter state */ 
    float anaMem[LPC_FILTERORDER]; 
 
    /* old lsf parameters for interpolation */ 
    float lsfold[LPC_FILTERORDER]; 
    float lsfdeqold[LPC_FILTERORDER]; 
 
    /* signal buffer for LP analysis */ 
    float lpc_buffer[LPC_LOOKBACK + BLOCKL]; 
 
    /* state of input HP filter */ 
    float hpimem[4]; 
 
} iLBC_Enc_Inst_t; 
 
/* type definition decoder instance */ 
typedef struct iLBC_Dec_Inst_t_ { 
    /* synthesis filter state */ 
    float syntMem[LPC_FILTERORDER]; 
 
    /* old LSF for interpolation */ 
    float lsfdeqold[LPC_FILTERORDER]; 
 
    /* pitch lag estimated in enhancer and used in PLC */ 
    int last_lag; 
 
    /* PLC state information */ 
    int prevLag, consPLICount, prevPLI, prev_enh_pl; 
    float prevGain, prevLpc[LPC_FILTERORDER+1]; 
    float prevResidual[NSUB*SUBL]; 
    float energy; 
    unsigned long seed; 
 
    /* previous synthesis filter parameters */ 
    float old_syntdenum[(LPC_FILTERORDER + 1)*NSUB]; 
 
    /* state of output HP filter */ 
    float hpomem[4]; 
 
    /* enhancer state information */ 
    int use_enhancer; 
    float enh_buf[ENH_BUFL]; 
    float enh_period[ENH_NBLOCKS_TOT]; 
 
} iLBC_Dec_Inst_t; 
 
#endif 
 
 
