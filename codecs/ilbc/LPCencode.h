 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    LPCencode.h 
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#ifndef __iLBC_LPCENCOD_H 
#define __iLBC_LPCENCOD_H 
 
void LPCencode(  
    float *syntdenum,   /* (i/o) synthesis filter coefficients  
                               before/after encoding */ 
    float *weightdenum, /* (i/o) weighting denumerator coefficients 
                               before/after encoding */ 
    int *lsf_index,     /* (o) lsf quantization index */ 
    float *data,    /* (i) lsf coefficients to quantize */ 
    iLBC_Enc_Inst_t *iLBCenc_inst  
                        /* (i/o) the encoder state structure */ 
); 
 
#endif 
 
 
