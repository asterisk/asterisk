 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    hpInput.h         
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#ifndef __iLBC_HPINPUT_H 
#define __iLBC_HPINPUT_H 
 
void hpInput(  
    float *In,  /* (i) vector to filter */ 
    int len,    /* (i) length of vector to filter */ 
    float *Out, /* (o) the resulting filtered vector */ 
    float *mem  /* (i/o) the filter state */ 
); 
 
#endif 
 
 
