 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    StateConstructW.h   
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#ifndef __iLBC_STATECONSTRUCTW_H 
#define __iLBC_STATECONSTRUCTW_H 
 
void StateConstructW(  
    int idxForMax,      /* (i) 6-bit index for the quantization of  
                               max amplitude */ 
    int *idxVec,    /* (i) vector of quantization indexes */ 
    float *syntDenum,   /* (i) synthesis filter denumerator */ 
    float *out,         /* (o) the decoded state vector */ 
    int len             /* (i) length of a state vector */ 
); 
 
#endif 
 
 
