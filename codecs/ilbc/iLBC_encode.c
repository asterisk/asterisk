 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    iLBC_encode.c  
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#include <math.h> 
#include <string.h> 
 
#include "iLBC_define.h" 
#include "LPCencode.h" 
#include "FrameClassify.h" 
#include "StateSearchW.h" 
#include "StateConstructW.h" 
#include "helpfun.h" 
#include "constants.h" 
#include "packing.h" 
#include "iLBC_encode.h"
#include "iCBSearch.h" 
#include "iCBConstruct.h" 
#include "hpInput.h" 
#include "anaFilter.h" 
#include "syntFilter.h" 
 
/*----------------------------------------------------------------* 
 *  Initiation of encoder instance. 
 *---------------------------------------------------------------*/ 
 
short initEncode(                   /* (o) Number of bytes encoded */ 
    iLBC_Enc_Inst_t *iLBCenc_inst   /* (i/o) Encoder instance */ 
){ 
    memset((*iLBCenc_inst).anaMem, 0,  
        LPC_FILTERORDER*sizeof(float)); 
    memcpy((*iLBCenc_inst).lsfold, lsfmeanTbl, 
        LPC_FILTERORDER*sizeof(float)); 
    memcpy((*iLBCenc_inst).lsfdeqold, lsfmeanTbl, 
        LPC_FILTERORDER*sizeof(float)); 
    memset((*iLBCenc_inst).lpc_buffer, 0,  
        LPC_LOOKBACK*sizeof(float)); 
    memset((*iLBCenc_inst).hpimem, 0, 4*sizeof(float)); 
 
    return (NO_OF_BYTES); 
} 
 
/*----------------------------------------------------------------* 
 *  main encoder function  
 *---------------------------------------------------------------*/ 
 
void iLBC_encode( 
    unsigned char *bytes,           /* (o) encoded data bits iLBC */ 
    float *block,                   /* (o) speech vector to encode */ 
    iLBC_Enc_Inst_t *iLBCenc_inst   /* (i/o) the general encoder  
                                           state */ 
){ 
     
    float data[BLOCKL]; 
    float residual[BLOCKL], reverseResidual[BLOCKL]; 
 
    int start, idxForMax, idxVec[STATE_LEN]; 
    float reverseDecresidual[BLOCKL], mem[CB_MEML]; 
    int n, k, meml_gotten, Nfor, Nback, i, pos; 
    int gain_index[CB_NSTAGES*NASUB], extra_gain_index[CB_NSTAGES]; 
    int cb_index[CB_NSTAGES*NASUB],extra_cb_index[CB_NSTAGES]; 
    int lsf_i[LSF_NSPLIT*LPC_N]; 
    unsigned char *pbytes; 
    int diff, start_pos, state_first; 
    float en1, en2; 
    int index, ulp, firstpart; 
    int subcount, subframe; 
    float weightState[LPC_FILTERORDER]; 
    float syntdenum[NSUB*(LPC_FILTERORDER+1)];  
    float weightdenum[NSUB*(LPC_FILTERORDER+1)];  
    float decresidual[BLOCKL]; 
 
    /* high pass filtering of input signal if such is not done  
           prior to calling this function */ 
 
    /*hpInput(block, BLOCKL, data, (*iLBCenc_inst).hpimem);*/ 
 
    /* otherwise simply copy */ 
 
    memcpy(data,block,BLOCKL*sizeof(float)); 
         
    /* LPC of hp filtered input data */ 
 
    LPCencode(syntdenum, weightdenum, lsf_i, data, 
        iLBCenc_inst); 
 
    /* inverse filter to get residual */ 
 
    for (n=0; n<NSUB; n++ ) { 
        anaFilter(&data[n*SUBL], &syntdenum[n*(LPC_FILTERORDER+1)],  
            SUBL, &residual[n*SUBL], (*iLBCenc_inst).anaMem); 
    } 
 
    /* find state location */ 
 
    start = FrameClassify(residual); 
     
    /* check if state should be in first or last part of the  
    two subframes */ 
 
    diff = STATE_LEN - STATE_SHORT_LEN; 
    en1 = 0; 
    index = (start-1)*SUBL; 
    for (i = 0; i < STATE_SHORT_LEN; i++) { 
        en1 += residual[index+i]*residual[index+i]; 
    } 
    en2 = 0; 
    index = (start-1)*SUBL+diff; 
    for (i = 0; i < STATE_SHORT_LEN; i++) { 
        en2 += residual[index+i]*residual[index+i]; 
    } 
     
     
    if (en1 > en2) { 
        state_first = 1; 
        start_pos = (start-1)*SUBL; 
    } else { 
        state_first = 0; 
        start_pos = (start-1)*SUBL + diff; 
    } 
 
    /* scalar quantization of state */ 
 
    StateSearchW(&residual[start_pos],  
        &syntdenum[(start-1)*(LPC_FILTERORDER+1)],  
        &weightdenum[(start-1)*(LPC_FILTERORDER+1)], &idxForMax,  
        idxVec, STATE_SHORT_LEN, state_first); 
 
    StateConstructW(idxForMax, idxVec,  
        &syntdenum[(start-1)*(LPC_FILTERORDER+1)],  
        &decresidual[start_pos], STATE_SHORT_LEN); 
 
    /* predictive quantization in state */ 
     
    if (state_first) { /* put adaptive part in the end */ 
         
        /* setup memory */ 
 
        memset(mem, 0, (CB_MEML-STATE_SHORT_LEN)*sizeof(float)); 
        memcpy(mem+CB_MEML-STATE_SHORT_LEN, decresidual+start_pos,  
                       STATE_SHORT_LEN*sizeof(float)); 
        memset(weightState, 0, LPC_FILTERORDER*sizeof(float)); 
 
        /* encode subframes */ 
 
        iCBSearch(extra_cb_index, extra_gain_index,  
            &residual[start_pos+STATE_SHORT_LEN],  
            mem+CB_MEML-stMemLTbl, 
            stMemLTbl, diff, CB_NSTAGES,  
            &weightdenum[start*(LPC_FILTERORDER+1)], weightState, 0); 
 
        /* construct decoded vector */ 
 
        iCBConstruct(&decresidual[start_pos+STATE_SHORT_LEN], 
            extra_cb_index, extra_gain_index, mem+CB_MEML-stMemLTbl,  
            stMemLTbl, diff, CB_NSTAGES); 
     
    }  
    else { /* put adaptive part in the beginning */ 
         
        /* create reversed vectors for prediction */ 
 
        for(k=0; k<diff; k++ ){ 
            reverseResidual[k] = residual[(start+1)*SUBL -1 
                -(k+STATE_SHORT_LEN)]; 
        } 
         
        /* setup memory */ 
 
        meml_gotten = STATE_SHORT_LEN; 
        for( k=0; k<meml_gotten; k++){  
            mem[CB_MEML-1-k] = decresidual[start_pos + k]; 
        }  
        memset(mem, 0, (CB_MEML-k)*sizeof(float)); 
        memset(weightState, 0, LPC_FILTERORDER*sizeof(float)); 
         
        /* encode subframes */ 
 
        iCBSearch(extra_cb_index, extra_gain_index,  
            reverseResidual, mem+CB_MEML-stMemLTbl, stMemLTbl, diff,  
            CB_NSTAGES, &weightdenum[(start-1)*(LPC_FILTERORDER+1)],  
            weightState, 0); 
 
        /* construct decoded vector */ 
 
        iCBConstruct(reverseDecresidual, extra_cb_index,  
            extra_gain_index, mem+CB_MEML-stMemLTbl, stMemLTbl, diff,  
            CB_NSTAGES); 
         
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
        memset(weightState, 0, LPC_FILTERORDER*sizeof(float)); 
 
        /* loop over subframes to encode */ 
 
        for (subframe=0; subframe<Nfor; subframe++) { 
 
            /* encode subframe */ 
 
            iCBSearch(cb_index+subcount*CB_NSTAGES,  
                gain_index+subcount*CB_NSTAGES,  
                &residual[(start+1+subframe)*SUBL],  
                mem+CB_MEML-memLfTbl[subcount], memLfTbl[subcount],  
                SUBL, CB_NSTAGES,  
                &weightdenum[(start+1+subframe)*(LPC_FILTERORDER+1)], 
                weightState, subcount+1); 
 
            /* construct decoded vector */ 
 
            iCBConstruct(&decresidual[(start+1+subframe)*SUBL],  
                cb_index+subcount*CB_NSTAGES,  
                gain_index+subcount*CB_NSTAGES,  
                mem+CB_MEML-memLfTbl[subcount], memLfTbl[subcount],  
                SUBL, CB_NSTAGES); 
 
            /* update memory */ 
 
            memcpy(mem, mem+SUBL, (CB_MEML-SUBL)*sizeof(float)); 
            memcpy(mem+CB_MEML-SUBL,  
                &decresidual[(start+1+subframe)*SUBL],  
                SUBL*sizeof(float)); 
            memset(weightState, 0, LPC_FILTERORDER*sizeof(float)); 
 
            subcount++; 
        } 
    } 
     
 
    /* backward prediction of subframes */ 
 
    Nback = start-1; 
 
     
    if( Nback > 0 ){ 
                
        /* create reverse order vectors */ 
 
        for( n=0; n<Nback; n++ ){ 
            for( k=0; k<SUBL; k++ ){ 
                reverseResidual[n*SUBL+k] =  
                    residual[(start-1)*SUBL-1-n*SUBL-k]; 
                reverseDecresidual[n*SUBL+k] =  
                    decresidual[(start-1)*SUBL-1-n*SUBL-k]; 
            } 
        } 
 
        /* setup memory */ 
 
        meml_gotten = SUBL*(NSUB+1-start); 
 
         
        if( meml_gotten > CB_MEML ) {  
            meml_gotten=CB_MEML; 
        } 
        for( k=0; k<meml_gotten; k++) {  
            mem[CB_MEML-1-k] = decresidual[(start-1)*SUBL + k]; 
        }  
        memset(mem, 0, (CB_MEML-k)*sizeof(float)); 
        memset(weightState, 0, LPC_FILTERORDER*sizeof(float)); 
 
        /* loop over subframes to encode */ 
 
        for (subframe=0; subframe<Nback; subframe++) { 
             
            /* encode subframe */ 
 
            iCBSearch(cb_index+subcount*CB_NSTAGES,  
                gain_index+subcount*CB_NSTAGES,  
                &reverseResidual[subframe*SUBL],  
                mem+CB_MEML-memLfTbl[subcount], memLfTbl[subcount],  
                SUBL, CB_NSTAGES,  
                &weightdenum[(start-2-subframe)*(LPC_FILTERORDER+1)],  
                weightState, subcount+1); 
 
            /* construct decoded vector */ 
 
            iCBConstruct(&reverseDecresidual[subframe*SUBL],  
                cb_index+subcount*CB_NSTAGES,  
                gain_index+subcount*CB_NSTAGES,  
                mem+CB_MEML-memLfTbl[subcount],  
                memLfTbl[subcount], SUBL, CB_NSTAGES); 
 
            /* update memory */ 
 
            memcpy(mem, mem+SUBL, (CB_MEML-SUBL)*sizeof(float)); 
            memcpy(mem+CB_MEML-SUBL,  
                &reverseDecresidual[subframe*SUBL], 
                SUBL*sizeof(float)); 
            memset(weightState, 0, LPC_FILTERORDER*sizeof(float)); 
 
            subcount++; 
 
        } 
 
        /* get decoded residual from reversed vector */ 
 
        for (i = 0; i < SUBL*Nback; i++) { 
            decresidual[SUBL*Nback - i - 1] =  
                reverseDecresidual[i]; 
        } 
    } 
    /* end encoding part */ 
 
    /* adjust index */ 
    index_conv_enc(cb_index); 
 
    /* pack bytes */ 
 
    pbytes=bytes; 
    pos=0; 
 
    /* loop over the 3 ULP classes */ 
 
    for (ulp=0; ulp<3; ulp++) { 
     
        /* LSF */ 
        for (k=0;k<6;k++) { 
            packsplit(&lsf_i[k], &firstpart, &lsf_i[k],  
                ulp_lsf_bitsTbl[k][ulp],  
                ulp_lsf_bitsTbl[k][ulp]+ 
                ulp_lsf_bitsTbl[k][ulp+1]+ 
                ulp_lsf_bitsTbl[k][ulp+2]); 
            dopack( &pbytes, firstpart,  
                ulp_lsf_bitsTbl[k][ulp], &pos); 
        } 
 
        /* Start block info */ 
 
        packsplit(&start, &firstpart, &start,  
            ulp_start_bitsTbl[ulp],  
            ulp_start_bitsTbl[ulp]+ 
            ulp_start_bitsTbl[ulp+1]+ 
            ulp_start_bitsTbl[ulp+2]); 
        dopack( &pbytes, firstpart,  
            ulp_start_bitsTbl[ulp], &pos); 
 
        packsplit(&state_first, &firstpart, &state_first,  
            ulp_startfirst_bitsTbl[ulp],  
            ulp_startfirst_bitsTbl[ulp]+ 
            ulp_startfirst_bitsTbl[ulp+1]+ 
            ulp_startfirst_bitsTbl[ulp+2]); 
        dopack( &pbytes, firstpart,  
            ulp_startfirst_bitsTbl[ulp], &pos); 
 
        packsplit(&idxForMax, &firstpart, &idxForMax,  
            ulp_scale_bitsTbl[ulp], ulp_scale_bitsTbl[ulp]+ 
            ulp_scale_bitsTbl[ulp+1]+ulp_scale_bitsTbl[ulp+2]); 
        dopack( &pbytes, firstpart,  
            ulp_scale_bitsTbl[ulp], &pos); 
 
        for (k=0; k<STATE_SHORT_LEN; k++) { 
            packsplit(idxVec+k, &firstpart, idxVec+k,  
                ulp_state_bitsTbl[ulp],  
                ulp_state_bitsTbl[ulp]+ 
                ulp_state_bitsTbl[ulp+1]+ 
                ulp_state_bitsTbl[ulp+2]); 
            dopack( &pbytes, firstpart,  
                ulp_state_bitsTbl[ulp], &pos); 
        } 
 
        /* 22 sample block */ 
 
        for (k=0;k<CB_NSTAGES;k++) { 
            packsplit(extra_cb_index+k, &firstpart,  
                extra_cb_index+k,  
                ulp_extra_cb_indexTbl[k][ulp],  
                ulp_extra_cb_indexTbl[k][ulp]+ 
                ulp_extra_cb_indexTbl[k][ulp+1]+ 
                ulp_extra_cb_indexTbl[k][ulp+2]); 
            dopack( &pbytes, firstpart,  
                ulp_extra_cb_indexTbl[k][ulp], &pos); 
        } 
        for (k=0;k<CB_NSTAGES;k++) { 
            packsplit(extra_gain_index+k, &firstpart,  
                extra_gain_index+k,  
                ulp_extra_cb_gainTbl[k][ulp],  
                ulp_extra_cb_gainTbl[k][ulp]+ 
                ulp_extra_cb_gainTbl[k][ulp+1]+ 
                ulp_extra_cb_gainTbl[k][ulp+2]); 
            dopack( &pbytes, firstpart,  
                ulp_extra_cb_gainTbl[k][ulp], &pos); 
        } 
             
        /* The four 40 sample sub blocks */ 
 
        for (i=0; i<NASUB; i++) { 
            for (k=0; k<CB_NSTAGES; k++) { 
                packsplit(cb_index+i*CB_NSTAGES+k, &firstpart,  
                    cb_index+i*CB_NSTAGES+k,  
                    ulp_cb_indexTbl[i][k][ulp],  
                    ulp_cb_indexTbl[i][k][ulp]+ 
                    ulp_cb_indexTbl[i][k][ulp+1]+ 
                    ulp_cb_indexTbl[i][k][ulp+2]); 
                dopack( &pbytes, firstpart,  
                    ulp_cb_indexTbl[i][k][ulp], &pos); 
            } 
        } 
         
        for (i=0; i<NASUB; i++) { 
            for (k=0; k<CB_NSTAGES; k++) { 
                packsplit(gain_index+i*CB_NSTAGES+k, &firstpart,  
                    gain_index+i*CB_NSTAGES+k,  
                    ulp_cb_gainTbl[i][k][ulp],  
                    ulp_cb_gainTbl[i][k][ulp]+ 
                    ulp_cb_gainTbl[i][k][ulp+1]+ 
                    ulp_cb_gainTbl[i][k][ulp+2]); 
                dopack( &pbytes, firstpart,  
                    ulp_cb_gainTbl[i][k][ulp], &pos); 
            } 
        } 
    } 
 
    /* set the last unused bit to zero */ 
    dopack( &pbytes, 0, 1, &pos); 
} 
 
 
