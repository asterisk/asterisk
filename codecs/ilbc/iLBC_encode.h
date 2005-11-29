 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    iLBC_encode.h     
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#ifndef __iLBC_ILBCENCODE_H 
#define __iLBC_ILBCENCODE_H 
 
#include "iLBC_define.h" 
 
short initEncode(                   /* (o) Number of bytes encoded */ 
    iLBC_Enc_Inst_t *iLBCenc_inst   /* (i/o) Encoder instance */ 
); 
 
void iLBC_encode( 
    unsigned char *bytes,           /* (o) encoded data bits iLBC */ 
    float *block,                   /* (o) speech vector to encode */ 
    iLBC_Enc_Inst_t *iLBCenc_inst   /* (i/o) the general encoder  
                                           state */ 
); 
 
#endif 
 
 
