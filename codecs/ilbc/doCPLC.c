 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    doCPLC.c  
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#include <math.h> 
#include <string.h> 
 
#include "iLBC_define.h" 
#include "doCPLC.h"
 
/*----------------------------------------------------------------* 
 *  Compute cross correlation and pitch gain for pitch prediction 
 *  of last subframe at given lag. 
 *---------------------------------------------------------------*/ 
 
static void compCorr( 
    float *cc,      /* (o) cross correlation coefficient */ 
    float *gc,      /* (o) gain */ 
    float *buffer,  /* (i) signal buffer */ 
    int lag,    /* (i) pitch lag */ 
    int bLen,       /* (i) length of buffer */ 
    int sRange      /* (i) correlation search length */ 
){ 
    int i; 
    float ftmp1, ftmp2; 
     
    ftmp1 = 0.0; 
    ftmp2 = 0.0; 
    for (i=0; i<sRange; i++) { 
        ftmp1 += buffer[bLen-sRange+i] * 
            buffer[bLen-sRange+i-lag]; 
        ftmp2 += buffer[bLen-sRange+i-lag] *  
                buffer[bLen-sRange+i-lag]; 
    } 
 
    if (ftmp2 > 0.0) { 
        *cc = ftmp1*ftmp1/ftmp2; 
        *gc = (float)fabs(ftmp1/ftmp2); 
    } 
    else { 
        *cc = 0.0; 
        *gc = 0.0; 
    } 
} 
 
/*----------------------------------------------------------------* 
 *  Packet loss concealment routine. Conceals a residual signal 
 *  and LP parameters. If no packet loss, update state. 
 *---------------------------------------------------------------*/ 
 
void doThePLC( 
    float *PLCresidual, /* (o) concealed residual */  
    float *PLClpc,      /* (o) concealed LP parameters */   
    int PLI,        /* (i) packet loss indicator  
                               0 - no PL, 1 = PL */  
    float *decresidual, /* (i) decoded residual */ 
    float *lpc,         /* (i) decoded LPC (only used for no PL) */ 
    int inlag,          /* (i) pitch lag */ 
    iLBC_Dec_Inst_t *iLBCdec_inst  
                        /* (i/o) decoder instance */ 
){ 
    int lag=20, randlag; 
    float gain, maxcc; 
    float gain_comp, maxcc_comp; 
    int i, pick, offset; 
    float ftmp, ftmp1, randvec[BLOCKL], pitchfact; 
             
    /* Packet Loss */ 
 
    if (PLI == 1) { 
         
        (*iLBCdec_inst).consPLICount += 1; 
         
        /* if previous frame not lost,  
           determine pitch pred. gain */ 
         
        if ((*iLBCdec_inst).prevPLI != 1) { 
 
            /* Search around the previous lag to find the  
               best pitch period */ 
             
            lag=inlag-3; 
            compCorr(&maxcc, &gain, (*iLBCdec_inst).prevResidual, 
                    lag, BLOCKL, 60); 
            for (i=inlag-2;i<=inlag+3;i++) { 
                compCorr(&maxcc_comp, &gain_comp,  
                    (*iLBCdec_inst).prevResidual, 
                    i, BLOCKL, 60); 
                 
                if (maxcc_comp>maxcc) { 
                    maxcc=maxcc_comp; 
                    gain=gain_comp; 
                    lag=i; 
                } 
            } 
             
            if (gain > 1.0) { 
                gain = 1.0; 
            } 
        } 
 
        /* previous frame lost, use recorded lag and gain */ 
 
        else { 
            lag=(*iLBCdec_inst).prevLag; 
            gain=(*iLBCdec_inst).prevGain; 
        } 
         
        /* Attenuate signal and scale down pitch pred gain if  
           several frames lost consecutively */ 
 
         
        if ((*iLBCdec_inst).consPLICount > 1) { 
            gain *= (float)0.9; 
        } 
         
        /* Compute mixing factor of picth repeatition and noise */ 
 
         
        if (gain > PLC_XT_MIX) { 
            pitchfact = PLC_YT_MIX; 
        } else if (gain < PLC_XB_MIX) { 
            pitchfact = PLC_YB_MIX; 
        } else { 
            pitchfact = PLC_YB_MIX + (gain - PLC_XB_MIX) *  
                (PLC_YT_MIX-PLC_YB_MIX)/(PLC_XT_MIX-PLC_XB_MIX); 
        } 
         
        /* compute concealed residual */ 
 
        (*iLBCdec_inst).energy = 0.0; 
        for (i=0; i<BLOCKL; i++) { 
 
            /* noise component */ 
 
            (*iLBCdec_inst).seed=((*iLBCdec_inst).seed*69069L+1) &  
                (0x80000000L-1); 
            randlag = 50 + ((signed long) (*iLBCdec_inst).seed)%70; 
            pick = i - randlag; 
             
            if (pick < 0) { 
                randvec[i] = gain *  
                    (*iLBCdec_inst).prevResidual[BLOCKL+pick]; 
            } else { 
                randvec[i] = gain * randvec[pick]; 
            } 
 
            /* pitch repeatition component */ 
 
            pick = i - lag; 
             
            if (pick < 0) { 
                PLCresidual[i] = gain *  
                    (*iLBCdec_inst).prevResidual[BLOCKL+pick]; 
            } else { 
                PLCresidual[i] = gain * PLCresidual[pick]; 
            } 
 
            /* mix noise and pitch repeatition */ 
 
            PLCresidual[i] = (pitchfact * PLCresidual[i] + 
                ((float)1.0 - pitchfact) * randvec[i]); 
             
            (*iLBCdec_inst).energy += PLCresidual[i] *  
                PLCresidual[i]; 
        } 
         
        /* less than 30 dB, use only noise */ 
         
        if (sqrt((*iLBCdec_inst).energy/(float)BLOCKL) < 30.0) {  
            (*iLBCdec_inst).energy = 0.0; 
            gain=0.0; 
            for (i=0; i<BLOCKL; i++) { 
                PLCresidual[i] = randvec[i]; 
                (*iLBCdec_inst).energy += PLCresidual[i] *  
                    PLCresidual[i]; 
            } 
        } 
         
        /* conceal LPC by bandwidth expansion of old LPC */ 
 
        ftmp=PLC_BWEXPAND; 
        PLClpc[0]=(float)1.0; 
        for (i=1; i<LPC_FILTERORDER+1; i++) { 
            PLClpc[i] = ftmp * (*iLBCdec_inst).prevLpc[i]; 
            ftmp *= PLC_BWEXPAND; 
        } 
         
    } 
 
    /* previous frame lost and this frame OK, mixing in  
    with new frame */ 
 
    else if ((*iLBCdec_inst).prevPLI == 1) { 
         
        lag = (*iLBCdec_inst).prevLag; 
        gain = (*iLBCdec_inst).prevGain; 
         
        /* if pitch pred gain high, do overlap-add */ 
         
        if (gain >= PLC_GAINTHRESHOLD) {         
         
            /* Compute mixing factor of pitch repeatition  
            and noise */ 
             
            if (gain > PLC_XT_MIX) { 
                pitchfact = PLC_YT_MIX; 
            } else if (gain < PLC_XB_MIX) { 
                pitchfact = PLC_YB_MIX; 
            } else { 
                pitchfact = PLC_YB_MIX + (gain - PLC_XB_MIX) *  
                    (PLC_YT_MIX-PLC_YB_MIX)/(PLC_XT_MIX-PLC_XB_MIX); 
            } 
 
            /* compute concealed residual for 3 subframes */ 
 
            for (i=0; i<3*SUBL; i++) { 
                 
                (*iLBCdec_inst).seed=((*iLBCdec_inst).seed* 
                    69069L+1) & (0x80000000L-1); 
                randlag = 50 + ((signed long)  
                    (*iLBCdec_inst).seed)%70; 
                 
                /* noise component */ 
 
                pick = i - randlag; 
                 
                if (pick < 0) { 
                    randvec[i] = gain *  
                        (*iLBCdec_inst).prevResidual[BLOCKL+pick]; 
                } else { 
                    randvec[i] = gain * randvec[pick]; 
                } 
                 
                /* pitch repeatition component */ 
 
                pick = i - lag; 
                 
                if (pick < 0) { 
                    PLCresidual[i] = gain *  
                        (*iLBCdec_inst).prevResidual[BLOCKL+pick]; 
                } else { 
                    PLCresidual[i] = gain * PLCresidual[pick]; 
                } 
 
                /* mix noise and pitch repeatition */ 
 
                PLCresidual[i] = (pitchfact * PLCresidual[i] +  
                    ((float)1.0 - pitchfact) * randvec[i]); 
            } 
             
            /* interpolate concealed residual with actual  
               residual */ 
 
            offset = 3*SUBL; 
            for (i=0; i<offset; i++) { 
                ftmp1 = (float) (i+1) / (float) (offset+1); 
                ftmp = (float)1.0 - ftmp1; 
                PLCresidual[i]=PLCresidual[i]*ftmp+ 
                    decresidual[i]*ftmp1; 
            } 
             
            memcpy(PLCresidual+offset, decresidual+offset,  
                (BLOCKL-offset)*sizeof(float)); 
 
        } else { 
            memcpy(PLCresidual, decresidual, BLOCKL*sizeof(float)); 
        } 
 
        /* copy LPC */ 
 
        memcpy(PLClpc, lpc, (LPC_FILTERORDER+1)*sizeof(float)); 
                         
        (*iLBCdec_inst).consPLICount = 0; 
    } 
 
    /* no packet loss, copy input */ 
 
    else { 
        memcpy(PLCresidual, decresidual, BLOCKL*sizeof(float)); 
        memcpy(PLClpc, lpc, (LPC_FILTERORDER+1)*sizeof(float)); 
    } 
     
    /* update state */ 
 
    (*iLBCdec_inst).prevLag = lag; 
    (*iLBCdec_inst).prevGain = gain; 
    (*iLBCdec_inst).prevPLI = PLI; 
    memcpy((*iLBCdec_inst).prevLpc, PLClpc,  
        (LPC_FILTERORDER+1)*sizeof(float)); 
    memcpy((*iLBCdec_inst).prevResidual, PLCresidual, 
        BLOCKL*sizeof(float)); 
} 
 
 
