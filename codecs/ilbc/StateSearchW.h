 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    StateSearchW.h      
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#ifndef __iLBC_STATESEARCHW_H 
#define __iLBC_STATESEARCHW_H 
 
void AbsQuantW( 
    float *in,          /* (i) vector to encode */ 
    float *syntDenum,   /* (i) denominator of synthesis filter */ 
    float *weightDenum, /* (i) denominator of weighting filter */ 
    int *out,           /* (o) vector of quantizer indexes */ 
    int len,        /* (i) length of vector to encode and  
                               vector of quantizer indexes */ 
    int state_first     /* (i) position of start state in the  
                               80 vec */ 
); 
 
void StateSearchW(  
    float *residual,/* (i) target residual vector */ 
    float *syntDenum,   /* (i) lpc synthesis filter */ 
    float *weightDenum, /* (i) weighting filter denuminator */ 
    int *idxForMax,     /* (o) quantizer index for maximum  
                               amplitude */ 
    int *idxVec,    /* (o) vector of quantization indexes */ 
    int len,        /* (i) length of all vectors */ 
    int state_first     /* (i) position of start state in the  
                               80 vec */ 
); 
 
 
#endif 
 
 
