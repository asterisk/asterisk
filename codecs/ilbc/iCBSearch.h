 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    iCBSearch.h         
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#ifndef __iLBC_ICBSEARCH_H 
#define __iLBC_ICBSEARCH_H 
 
void iCBSearch( 
    int *index,         /* (o) Codebook indices */ 
    int *gain_index,/* (o) Gain quantization indices */ 
    float *intarget,/* (i) Target vector for encoding */     
    float *mem,         /* (i) Buffer for codebook construction */ 
    int lMem,           /* (i) Length of buffer */ 
    int lTarget,    /* (i) Length of vector */ 
    int nStages,    /* (i) Number of codebook stages */ 
    float *weightDenum, /* (i) weighting filter coefficients */ 
    float *weightState, /* (i) weighting filter state */ 
    int block           /* (i) the subblock number */ 
); 
 
#endif 
 
 
