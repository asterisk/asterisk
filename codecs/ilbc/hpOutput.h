 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    hpOutput.h        
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#ifndef __iLBC_HPOUTPUT_H 
#define __iLBC_HPOUTPUT_H 
 
void hpOutput( 
    float *In,  /* (i) vector to filter */ 
    int len,/* (i) length of vector to filter */ 
    float *Out, /* (o) the resulting filtered vector */ 
    float *mem  /* (i/o) the filter state */ 
); 
 
#endif 
 
 
