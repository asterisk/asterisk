 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    syntFilter.h                
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#ifndef __iLBC_SYNTFILTER_H 
#define __iLBC_SYNTFILTER_H 
 
void syntFilter( 
    float *Out,     /* (i/o) Signal to be filtered */ 
    float *a,       /* (i) LP parameters */ 
    int len,    /* (i) Length of signal */ 
    float *mem      /* (i/o) Filter state */ 
); 
 
#endif 
 
 
