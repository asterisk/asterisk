 
/****************************************************************** 
 
    iLBC Speech Coder ANSI-C Source Code 
 
    constants.h 
 
    Copyright (c) 2001, 
    Global IP Sound AB. 
    All rights reserved. 
 
******************************************************************/ 
 
#ifndef __iLBC_CONSTANTS_H 
#define __iLBC_CONSTANTS_H 
 
#include "iLBC_define.h" 
 
/* bit allocation */ 
 
extern int lsf_bitsTbl[]; 
extern int start_bitsTbl; 
extern int scale_bitsTbl; 
extern int state_bitsTbl; 
extern int cb_bitsTbl[5][CB_NSTAGES]; 
extern int search_rangeTbl[5][CB_NSTAGES]; 
extern int gain_bitsTbl[];  
 
/* ULP bit allocation */ 
 
extern int ulp_lsf_bitsTbl[6][ULP_CLASSES+2]; 
extern int ulp_start_bitsTbl[]; 
extern int ulp_startfirst_bitsTbl[]; 
extern int ulp_scale_bitsTbl[]; 
extern int ulp_state_bitsTbl[]; 
extern int ulp_extra_cb_indexTbl[CB_NSTAGES][ULP_CLASSES+2]; 
extern int ulp_extra_cb_gainTbl[CB_NSTAGES][ULP_CLASSES+2]; 
extern int ulp_cb_indexTbl[NASUB][CB_NSTAGES][ULP_CLASSES+2]; 
extern int ulp_cb_gainTbl[NASUB][CB_NSTAGES][ULP_CLASSES+2]; 
 
/* high pass filters */ 
 
extern float hpi_zero_coefsTbl[]; 
extern float hpi_pole_coefsTbl[]; 
extern float hpo_zero_coefsTbl[]; 
extern float hpo_pole_coefsTbl[];   
 
/* low pass filters */ 
extern float lpFilt_coefsTbl[]; 
 
/* LPC analysis and quantization */ 
 
extern float lpc_winTbl[]; 
extern float lpc_asymwinTbl[]; 
extern float lpc_lagwinTbl[]; 
extern float lsfCbTbl[]; 
extern float lsfmeanTbl[]; 
extern int   dim_lsfCbTbl[]; 
extern int   size_lsfCbTbl[]; 
extern float lsf_weightTbl[]; 
 
/* state quantization tables */ 
 
extern float state_sq3Tbl[]; 
extern float state_frgqTbl[]; 
 
/* gain quantization tables */ 
 
extern float gain_sq3Tbl[]; 
extern float gain_sq4Tbl[]; 
extern float gain_sq5Tbl[]; 
 
/* adaptive codebook definitions */ 
 
extern int memLfTbl[]; 
extern int stMemLTbl; 
extern float cbfiltersTbl[CB_FILTERLEN]; 
 
/* enhancer definitions */ 
 
extern float polyphaserTbl[]; 
extern float enh_plocsTbl[]; 
 
#endif 
 
