 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    LPC_decode.c  
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#include <math.h> 
#include <string.h> 
 
#include "helpfun.h" 
#include "lsf.h" 
#include "iLBC_define.h" 
#include "constants.h" 
#include "LPCdecode.h"
 
/*----------------------------------------------------------------* 
 *  interpolation of lsf coefficients for the decoder                            
 *---------------------------------------------------------------*/ 
 
void LSFinterpolate2a_dec(  
    float *a,           /* (o) lpc coefficients for a sub frame */ 
    float *lsf1,    /* (i) first lsf coefficient vector */ 
    float *lsf2,    /* (i) second lsf coefficient vector */ 
    float coef,         /* (i) interpolation weight */ 
    int length          /* (i) length of lsf vectors */ 
){ 
    float  lsftmp[LPC_FILTERORDER]; 
     
    interpolate(lsftmp, lsf1, lsf2, coef, length); 
    lsf2a(a, lsftmp); 
} 
 
/*----------------------------------------------------------------* 
 *  obtain dequantized lsf coefficients from quantization index  
 *---------------------------------------------------------------*/ 
 
void SimplelsfDEQ( 
    float *lsfdeq,      /* (o) dequantized lsf coefficients */ 
    int *index          /* (i) quantization index */ 
){   
    int    i,j, pos, cb_pos; 
 
    /* decode first LSF */ 
     
    pos = 0; 
    cb_pos = 0; 
    for (i = 0; i < LSF_NSPLIT; i++) { 
        for (j = 0; j < dim_lsfCbTbl[i]; j++) { 
            lsfdeq[pos + j] = lsfCbTbl[cb_pos +  
                (long)(index[i])*dim_lsfCbTbl[i] + j]; 
        }        
        pos += dim_lsfCbTbl[i]; 
        cb_pos += size_lsfCbTbl[i]*dim_lsfCbTbl[i]; 
    } 
 
    /* decode last LSF */ 
     
    pos = 0; 
    cb_pos = 0; 
    for (i = 0; i < LSF_NSPLIT; i++) { 
        for (j = 0; j < dim_lsfCbTbl[i]; j++) { 
            lsfdeq[LPC_FILTERORDER + pos + j] = lsfCbTbl[cb_pos +  
                (long)(index[LSF_NSPLIT + i])*dim_lsfCbTbl[i] + j]; 
        }        
        pos += dim_lsfCbTbl[i]; 
        cb_pos += size_lsfCbTbl[i]*dim_lsfCbTbl[i]; 
    } 
} 
 
/*----------------------------------------------------------------* 
 *  obtain synthesis and weighting filters form lsf coefficients  
 *---------------------------------------------------------------*/ 
 
void DecoderInterpolateLSF(  
    float *syntdenum,   /* (o) synthesis filter coefficients */ 
    float *weightdenum, /* (o) weighting denumerator  
                               coefficients */ 
    float *lsfdeq,      /* (i) dequantized lsf coefficients */ 
    int length,         /* (i) length of lsf coefficient vector */ 
    iLBC_Dec_Inst_t *iLBCdec_inst  
                        /* (i) the decoder state structure */ 
){ 
    int    i, pos, lp_length; 
    float  lp[LPC_FILTERORDER + 1], *lsfdeq2; 
         
    lsfdeq2 = lsfdeq + length; 
    lp_length = length + 1; 
     
    /* subframe 1: Interpolation between old and first */ 
 
    LSFinterpolate2a_dec(lp, (*iLBCdec_inst).lsfdeqold, lsfdeq,  
        lsf_weightTbl[0], length); 
    memcpy(syntdenum,lp,lp_length*sizeof(float)); 
    bwexpand(weightdenum, lp, LPC_CHIRP_WEIGHTDENUM, lp_length); 
 
    /* subframes 2 to 6: interpolation between first and last  
    LSF */ 
     
    pos = lp_length; 
    for (i = 1; i < 6; i++) { 
        LSFinterpolate2a_dec(lp, lsfdeq, lsfdeq2, lsf_weightTbl[i],  
            length); 
        memcpy(syntdenum + pos,lp,lp_length*sizeof(float)); 
        bwexpand(weightdenum + pos, lp,  
            LPC_CHIRP_WEIGHTDENUM, lp_length); 
        pos += lp_length; 
    } 
     
    /* update memory */ 
 
    memcpy((*iLBCdec_inst).lsfdeqold, lsfdeq2, length*sizeof(float)); 
 
} 
 
 
