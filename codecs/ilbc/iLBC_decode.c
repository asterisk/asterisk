 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    iLBC_decode.c  
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#include <math.h> 
#include <stdlib.h> 
 
#include "iLBC_define.h" 
#include "StateConstructW.h" 
#include "LPCdecode.h" 
#include "iCBConstruct.h" 
#include "doCPLC.h" 
#include "helpfun.h" 
#include "constants.h" 
#include "packing.h" 
#include "iLBC_decode.h"
#include "string.h" 
#include "enhancer.h" 
#include "hpOutput.h" 
#include "syntFilter.h" 
 
/*----------------------------------------------------------------* 
 *  Initiation of decoder instance. 
 *---------------------------------------------------------------*/ 
 
short initDecode(                   /* (o) Number of decoded  
                                           samples */ 
    iLBC_Dec_Inst_t *iLBCdec_inst,  /* (i/o) Decoder instance */ 
    int use_enhancer                /* (i) 1 to use enhancer 
                                           0 to run without  
                                             enhancer */ 
){ 
    int i; 

    memset((*iLBCdec_inst).syntMem, 0,
        LPC_FILTERORDER*sizeof(float)); 
    memcpy((*iLBCdec_inst).lsfdeqold, lsfmeanTbl,  
        LPC_FILTERORDER*sizeof(float)); 
 
    memset((*iLBCdec_inst).old_syntdenum, 0,  
        ((LPC_FILTERORDER + 1)*NSUB)*sizeof(float)); 
    for (i=0; i<NSUB; i++) 
        (*iLBCdec_inst).old_syntdenum[i*(LPC_FILTERORDER+1)]=1.0; 
 
    (*iLBCdec_inst).last_lag = 20; 
 
    (*iLBCdec_inst).prevLag = 120; 
    (*iLBCdec_inst).prevGain = 0.0; 
    (*iLBCdec_inst).consPLICount = 0; 
    (*iLBCdec_inst).prevPLI = 0; 
    (*iLBCdec_inst).prevLpc[0] = 1.0; 
    memset((*iLBCdec_inst).prevLpc+1,0, 
        LPC_FILTERORDER*sizeof(float)); 
    memset((*iLBCdec_inst).prevResidual, 0, BLOCKL*sizeof(float)); 
    (*iLBCdec_inst).seed=777; 
 
    memset((*iLBCdec_inst).hpomem, 0, 4*sizeof(float)); 
 
    (*iLBCdec_inst).use_enhancer = use_enhancer; 
    memset((*iLBCdec_inst).enh_buf, 0, ENH_BUFL*sizeof(float)); 
    for (i=0;i<ENH_NBLOCKS_TOT;i++)  
        (*iLBCdec_inst).enh_period[i]=(float)40.0; 
 
    iLBCdec_inst->prev_enh_pl = 0;

    return (BLOCKL); 
} 
 
/*----------------------------------------------------------------* 
 *  frame residual decoder function (subrutine to iLBC_decode)  
 *---------------------------------------------------------------*/ 
 
static void Decode( 
    float *decresidual,     /* (o) decoded residual frame */ 
    int start,              /* (i) location of start state */ 
    int idxForMax,          /* (i) codebook index for the maximum  
                                   value */ 
    int *idxVec,        /* (i) codebook indexes for the samples  
                                   in the start state */ 
    float *syntdenum,       /* (i) the decoded synthesis filter  
                                   coefficients */ 
    int *cb_index,          /* (i) the indexes for the adaptive  
                                   codebook */ 
    int *gain_index,    /* (i) the indexes for the corresponding  
                                   gains */ 
    int *extra_cb_index,/* (i) the indexes for the adaptive  
                                   codebook part of start state */ 
    int *extra_gain_index,  /* (i) the indexes for the corresponding 
                                   gains */ 
    int state_first         /* (i) 1 if non adaptive part of start  
                                   state comes first 0 if that part  
                                   comes last */ 
){ 
    float reverseDecresidual[BLOCKL], mem[CB_MEML]; 
    int k, meml_gotten, Nfor, Nback, i; 
    int diff, start_pos; 
    int subcount, subframe; 
 
    diff = STATE_LEN - STATE_SHORT_LEN; 
     
    if (state_first == 1) { 
        start_pos = (start-1)*SUBL; 
    } else { 
        start_pos = (start-1)*SUBL + diff; 
    } 
 
    /* decode scalar part of start state */ 
 
    StateConstructW(idxForMax, idxVec,  
        &syntdenum[(start-1)*(LPC_FILTERORDER+1)],  
        &decresidual[start_pos], STATE_SHORT_LEN); 
 
     
    if (state_first) { /* put adaptive part in the end */ 
                 
        /* setup memory */ 
 
        memset(mem, 0, (CB_MEML-STATE_SHORT_LEN)*sizeof(float)); 
        memcpy(mem+CB_MEML-STATE_SHORT_LEN, decresidual+start_pos, 
            STATE_SHORT_LEN*sizeof(float)); 
         
        /* construct decoded vector */ 
 
        iCBConstruct(&decresidual[start_pos+STATE_SHORT_LEN], 
            extra_cb_index, extra_gain_index, mem+CB_MEML-stMemLTbl, 
            stMemLTbl, diff, CB_NSTAGES); 
     
    }  
    else {/* put adaptive part in the beginning */ 
         
        /* create reversed vectors for prediction */ 
 
        for(k=0; k<diff; k++ ){ 
            reverseDecresidual[k] =  
                decresidual[(start+1)*SUBL -1-(k+STATE_SHORT_LEN)]; 
        } 
         
        /* setup memory */ 
 
        meml_gotten = STATE_SHORT_LEN; 
        for( k=0; k<meml_gotten; k++){  
            mem[CB_MEML-1-k] = decresidual[start_pos + k]; 
        }  
        memset(mem, 0, (CB_MEML-k)*sizeof(float)); 
         
        /* construct decoded vector */ 
 
        iCBConstruct(reverseDecresidual, extra_cb_index,  
            extra_gain_index, mem+CB_MEML-stMemLTbl, stMemLTbl, 
            diff, CB_NSTAGES); 
         
        /* get decoded residual from reversed vector */ 
 
        for( k=0; k<diff; k++ ){ 
            decresidual[start_pos-1-k] = reverseDecresidual[k]; 
        } 
    } 
 
    /* counter for predicted subframes */ 
 
    subcount=0; 
 
    /* forward prediction of subframes */ 
 
    Nfor = NSUB-start-1; 
     
    if( Nfor > 0 ){ 
         
        /* setup memory */ 
 
        memset(mem, 0, (CB_MEML-STATE_LEN)*sizeof(float)); 
        memcpy(mem+CB_MEML-STATE_LEN, decresidual+(start-1)*SUBL, 
            STATE_LEN*sizeof(float)); 
 
        /* loop over subframes to encode */ 
 
        for (subframe=0; subframe<Nfor; subframe++) { 
             
            /* construct decoded vector */ 
 
            iCBConstruct(&decresidual[(start+1+subframe)*SUBL],  
                cb_index+subcount*CB_NSTAGES,  
                gain_index+subcount*CB_NSTAGES,  
                mem+CB_MEML-memLfTbl[subcount],  
                memLfTbl[subcount], SUBL, CB_NSTAGES); 
 
            /* update memory */ 
 
            memcpy(mem, mem+SUBL, (CB_MEML-SUBL)*sizeof(float)); 
            memcpy(mem+CB_MEML-SUBL,  
                &decresidual[(start+1+subframe)*SUBL], 
                SUBL*sizeof(float)); 
 
            subcount++; 
 
        } 
 
    } 
     
    /* backward prediction of subframes */ 
 
    Nback = start-1; 
 
    if( Nback > 0 ){ 
 
        /* setup memory */ 
 
        meml_gotten = SUBL*(NSUB+1-start); 
 
         
        if( meml_gotten > CB_MEML ) {  
            meml_gotten=CB_MEML; 
        } 
        for( k=0; k<meml_gotten; k++) {  
            mem[CB_MEML-1-k] = decresidual[(start-1)*SUBL + k]; 
        } 
        memset(mem, 0, (CB_MEML-k)*sizeof(float)); 
 
        /* loop over subframes to decode */ 
 
        for (subframe=0; subframe<Nback; subframe++) { 
             
            /* construct decoded vector */ 
 
            iCBConstruct(&reverseDecresidual[subframe*SUBL],  
                cb_index+subcount*CB_NSTAGES,  
                gain_index+subcount*CB_NSTAGES,  
                mem+CB_MEML-memLfTbl[subcount], memLfTbl[subcount],  
                SUBL, CB_NSTAGES); 
 
            /* update memory */ 
 
            memcpy(mem, mem+SUBL, (CB_MEML-SUBL)*sizeof(float)); 
            memcpy(mem+CB_MEML-SUBL,  
                &reverseDecresidual[subframe*SUBL], 
                SUBL*sizeof(float)); 
 
            subcount++; 
        } 
 
        /* get decoded residual from reversed vector */ 
 
        for (i = 0; i < SUBL*Nback; i++) 
            decresidual[SUBL*Nback - i - 1] =  
            reverseDecresidual[i]; 
    } 
} 
 
/*----------------------------------------------------------------* 
 *  main decoder function  
 *---------------------------------------------------------------*/ 
 
void iLBC_decode(  
    float *decblock,            /* (o) decoded signal block */ 
    unsigned char *bytes,           /* (i) encoded signal bits */ 
    iLBC_Dec_Inst_t *iLBCdec_inst,  /* (i/o) the decoder state  
                                             structure */ 
    int mode                    /* (i) 0: bad packet, PLC,  
                                           1: normal */ 
){ 
    float data[BLOCKL]; 
    float lsfdeq[LPC_FILTERORDER*LPC_N]; 
    float PLCresidual[BLOCKL], PLClpc[LPC_FILTERORDER + 1]; 
    float zeros[BLOCKL], one[LPC_FILTERORDER + 1]; 
    int k, i, start, idxForMax, pos, lastpart, ulp; 
    int lag, ilag; 
    float cc, maxcc; 
    int idxVec[STATE_LEN]; 
    int check; 
    int gain_index[NASUB*CB_NSTAGES], extra_gain_index[CB_NSTAGES]; 
    int cb_index[CB_NSTAGES*NASUB], extra_cb_index[CB_NSTAGES]; 
    int lsf_i[LSF_NSPLIT*LPC_N]; 
    int state_first; 
    unsigned char *pbytes; 
    float weightdenum[(LPC_FILTERORDER + 1)*NSUB]; 
    int order_plus_one; 
    float syntdenum[NSUB*(LPC_FILTERORDER+1)];  
    float decresidual[BLOCKL]; 
     
    if (mode>0) { /* the data are good */ 
 
        /* decode data */ 
         
        pbytes=bytes; 
        pos=0; 
 
        /* Set everything to zero before decoding */ 
 
        for (k=0;k<6;k++) { 
            lsf_i[k]=0; 
        } 
        start=0; 
        state_first=0; 
        idxForMax=0; 
        for (k=0; k<STATE_SHORT_LEN; k++) { 
            idxVec[k]=0; 
        } 
        for (k=0;k<CB_NSTAGES;k++) { 
            extra_cb_index[k]=0; 
        } 
        for (k=0;k<CB_NSTAGES;k++) { 
            extra_gain_index[k]=0; 
        } 
        for (i=0; i<NASUB; i++) { 
            for (k=0; k<CB_NSTAGES; k++) { 
                cb_index[i*CB_NSTAGES+k]=0; 
            } 
        } 
        for (i=0; i<NASUB; i++) { 
            for (k=0; k<CB_NSTAGES; k++) { 
                gain_index[i*CB_NSTAGES+k]=0; 
            } 
        } 
 
        /* loop over ULP classes */ 
 
        for (ulp=0; ulp<3; ulp++) { 
         
            /* LSF */ 
            for (k=0;k<6;k++) { 
                unpack( &pbytes, &lastpart,  
                    ulp_lsf_bitsTbl[k][ulp], &pos); 
                packcombine(&lsf_i[k], lastpart, 
                    ulp_lsf_bitsTbl[k][ulp]); 
            } 
 
            /* Start block info */ 
 
            unpack( &pbytes, &lastpart,  
                ulp_start_bitsTbl[ulp], &pos); 
            packcombine(&start, lastpart,  
                ulp_start_bitsTbl[ulp]); 
 
            unpack( &pbytes, &lastpart,  
                ulp_startfirst_bitsTbl[ulp], &pos); 
            packcombine(&state_first, lastpart, 
                ulp_startfirst_bitsTbl[ulp]); 
 
            unpack( &pbytes, &lastpart,  
                ulp_scale_bitsTbl[ulp], &pos); 
            packcombine(&idxForMax, lastpart,  
                ulp_scale_bitsTbl[ulp]); 
 
            for (k=0; k<STATE_SHORT_LEN; k++) { 
                unpack( &pbytes, &lastpart,  
                    ulp_state_bitsTbl[ulp], &pos); 
                packcombine(idxVec+k, lastpart, 
                    ulp_state_bitsTbl[ulp]); 
            } 
 
            /* 22 sample block */ 
 
            for (k=0;k<CB_NSTAGES;k++) { 
                unpack( &pbytes, &lastpart,  
                    ulp_extra_cb_indexTbl[k][ulp], &pos); 
                packcombine(extra_cb_index+k, lastpart,  
                    ulp_extra_cb_indexTbl[k][ulp]); 
            } 
            for (k=0;k<CB_NSTAGES;k++) { 
                unpack( &pbytes, &lastpart,  
                    ulp_extra_cb_gainTbl[k][ulp], &pos); 
                packcombine(extra_gain_index+k, lastpart, 
                    ulp_extra_cb_gainTbl[k][ulp]); 
            } 
                 
            /* The four 40 sample sub blocks */ 
 
            for (i=0; i<NASUB; i++) { 
                for (k=0; k<CB_NSTAGES; k++) { 
                    unpack( &pbytes, &lastpart,  
                        ulp_cb_indexTbl[i][k][ulp], &pos); 
                    packcombine(cb_index+i*CB_NSTAGES+k, lastpart,  
                        ulp_cb_indexTbl[i][k][ulp]); 
                } 
            } 
             
            for (i=0; i<NASUB; i++) { 
                for (k=0; k<CB_NSTAGES; k++) { 
                    unpack( &pbytes, &lastpart,  
                        ulp_cb_gainTbl[i][k][ulp], &pos); 
                    packcombine(gain_index+i*CB_NSTAGES+k, lastpart,  
                        ulp_cb_gainTbl[i][k][ulp]); 
                } 
            } 
        } 
 
        /* Check for bit errors */ 
        if( (start<1) || (start>5) ) 
            mode = 0; 
 
        if (mode==1) { /* No bit errors was detected,  
                          continue decoding */ 
                 
            /* adjust index */ 
            index_conv_dec(cb_index); 
 
            /* decode the lsf */ 
 
            SimplelsfDEQ(lsfdeq, lsf_i); 
            check=LSF_check(lsfdeq, LPC_FILTERORDER, LPC_N); 
            DecoderInterpolateLSF(syntdenum, weightdenum,  
                lsfdeq, LPC_FILTERORDER, iLBCdec_inst); 
         
            Decode(decresidual, start, idxForMax, idxVec,  
                syntdenum, cb_index, gain_index,  
                extra_cb_index, extra_gain_index,  
                state_first); 
 
            /* preparing the plc for a future loss! */ 
 
            doThePLC(PLCresidual, PLClpc, 0, decresidual,  
                syntdenum + (LPC_FILTERORDER + 1)*(NSUB - 1), 
                (*iLBCdec_inst).last_lag, iLBCdec_inst); 
 
         
            memcpy(decresidual, PLCresidual, BLOCKL*sizeof(float)); 
        } 
         
    } 
     
    if (mode == 0) { 
        /* the data is bad (either a PLC call 
         * was made or a bit error was detected) 
         */ 
         
        /* packet loss conceal */ 
 
        memset(zeros, 0, BLOCKL*sizeof(float)); 
         
        one[0] = 1; 
        memset(one+1, 0, LPC_FILTERORDER*sizeof(float)); 
         
        start=0; 
         
        doThePLC(PLCresidual, PLClpc, 1, zeros, one, 
            (*iLBCdec_inst).last_lag, iLBCdec_inst); 
        memcpy(decresidual, PLCresidual, BLOCKL*sizeof(float)); 
         
        order_plus_one = LPC_FILTERORDER + 1; 
        for (i = 0; i < NSUB; i++) { 
            memcpy(syntdenum+(i*order_plus_one), PLClpc,  
                order_plus_one*sizeof(float)); 
        } 
    } 
 
    if ((*iLBCdec_inst).use_enhancer == 1) { 
 
        /* post filtering */ 
         
        (*iLBCdec_inst).last_lag =  
            enhancerInterface(data, decresidual, iLBCdec_inst); 
 
        /* synthesis filtering */ 
         
        for (i=0; i < 2; i++) { 
            syntFilter(data + i*SUBL,  
                (*iLBCdec_inst).old_syntdenum +  
                (i+4)*(LPC_FILTERORDER+1), SUBL,  
                (*iLBCdec_inst).syntMem); 
        } 
        for (i=2; i < NSUB; i++) { 
            syntFilter(data + i*SUBL,  
                syntdenum + (i-2)*(LPC_FILTERORDER+1), SUBL,  
                (*iLBCdec_inst).syntMem); 
        } 
 
    } else { 
 
        /* Find last lag */ 
        lag = 20; 
        maxcc = xCorrCoef(&decresidual[BLOCKL-ENH_BLOCKL],  
            &decresidual[BLOCKL-ENH_BLOCKL-lag], ENH_BLOCKL); 
         
        for (ilag=21; ilag<120; ilag++) { 
            cc = xCorrCoef(&decresidual[BLOCKL-ENH_BLOCKL],  
                &decresidual[BLOCKL-ENH_BLOCKL-ilag], ENH_BLOCKL); 
         
            if (cc > maxcc) { 
                maxcc = cc; 
                lag = ilag; 
            } 
        } 
        (*iLBCdec_inst).last_lag = lag; 
 
        /* copy data and run synthesis filter */ 
 
        memcpy(data, decresidual, BLOCKL*sizeof(float)); 
        for (i=0; i < NSUB; i++) { 
            syntFilter(data + i*SUBL,  
                syntdenum + i*(LPC_FILTERORDER+1), SUBL,  
                (*iLBCdec_inst).syntMem); 
        } 
    } 
 
    /* high pass filtering on output if desired, otherwise  
       copy to out */ 
 
    /*hpOutput(data, BLOCKL, decblock, (*iLBCdec_inst).hpomem);*/ 
    memcpy(decblock,data,BLOCKL*sizeof(float)); 
 
    memcpy((*iLBCdec_inst).old_syntdenum, syntdenum,  
        NSUB*(LPC_FILTERORDER+1)*sizeof(float)); 
 
    iLBCdec_inst->prev_enh_pl=0; 
 
    if (mode==0) { /* PLC was used */ 
        iLBCdec_inst->prev_enh_pl=1; 
    } 
} 
 
 
