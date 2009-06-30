/*
 * Copyright (C) 1997-2005 by Objective Systems, Inc.
 *
 * This software is furnished under an open source license and may be 
 * used and copied only in accordance with the terms of this license. 
 * The text of the license may generally be found in the root 
 * directory of this installation in the COPYING file.  It 
 * can also be viewed online at the following URL:
 *
 *   http://www.obj-sys.com/open/license.html
 *
 * Any redistributions of this file including modified versions must 
 * maintain this copyright notice.
 *
 *****************************************************************************/

/* Run-time ctype substitution */

#include "ooasn1.h"
#include "rtctype.h"

const ASN1OCTET rtCtypeTable[256] = {
   OS_CTYPE_CTRL,                   /* 00 (NUL) */
   OS_CTYPE_CTRL,                   /* 01 (SOH) */
   OS_CTYPE_CTRL,                   /* 02 (STX) */
   OS_CTYPE_CTRL,                   /* 03 (ETX) */
   OS_CTYPE_CTRL,                   /* 04 (EOT) */
   OS_CTYPE_CTRL,                   /* 05 (ENQ) */
   OS_CTYPE_CTRL,                   /* 06 (ACK) */
   OS_CTYPE_CTRL,                   /* 07 (BEL) */
   OS_CTYPE_CTRL,                   /* 08 (BS)  */
   OS_CTYPE_CTRL|OS_CTYPE_SPACE,    /* 09 (HT)  */
   OS_CTYPE_CTRL|OS_CTYPE_SPACE,    /* 0A (LF)  */
   OS_CTYPE_CTRL|OS_CTYPE_SPACE,    /* 0B (VT)  */
   OS_CTYPE_CTRL|OS_CTYPE_SPACE,    /* 0C (FF)  */
   OS_CTYPE_CTRL|OS_CTYPE_SPACE,    /* 0D (CR)  */
   OS_CTYPE_CTRL,                   /* 0E (SI)  */
   OS_CTYPE_CTRL,                   /* 0F (SO)  */
   OS_CTYPE_CTRL,                   /* 10 (DLE) */
   OS_CTYPE_CTRL,                   /* 11 (DC1) */
   OS_CTYPE_CTRL,                   /* 12 (DC2) */
   OS_CTYPE_CTRL,                   /* 13 (DC3) */
   OS_CTYPE_CTRL,                   /* 14 (DC4) */
   OS_CTYPE_CTRL,                   /* 15 (NAK) */
   OS_CTYPE_CTRL,                   /* 16 (SYN) */
   OS_CTYPE_CTRL,                   /* 17 (ETB) */
   OS_CTYPE_CTRL,                   /* 18 (CAN) */
   OS_CTYPE_CTRL,                   /* 19 (EM)  */
   OS_CTYPE_CTRL,                   /* 1A (SUB) */
   OS_CTYPE_CTRL,                   /* 1B (ESC) */
   OS_CTYPE_CTRL,                   /* 1C (FS)  */
   OS_CTYPE_CTRL,                   /* 1D (GS)  */
   OS_CTYPE_CTRL,                   /* 1E (RS)  */
   OS_CTYPE_CTRL,                   /* 1F (US)  */
   OS_CTYPE_SPACE|OS_CTYPE_BLANK,   /* 20 SPACE */
   OS_CTYPE_PUNCT,                  /* 21 !     */
   OS_CTYPE_PUNCT,                  /* 22 "     */
   OS_CTYPE_PUNCT,                  /* 23 #     */
   OS_CTYPE_PUNCT,                  /* 24 $     */
   OS_CTYPE_PUNCT,                  /* 25 %     */
   OS_CTYPE_PUNCT,                  /* 26 &     */
   OS_CTYPE_PUNCT,                  /* 27 '     */
   OS_CTYPE_PUNCT,                  /* 28 (     */
   OS_CTYPE_PUNCT,                  /* 29 )     */
   OS_CTYPE_PUNCT,                  /* 2A *     */
   OS_CTYPE_PUNCT,                  /* 2B +     */
   OS_CTYPE_PUNCT,                  /* 2C ,     */
   OS_CTYPE_PUNCT,                  /* 2D -     */
   OS_CTYPE_PUNCT,                  /* 2E .     */
   OS_CTYPE_PUNCT,                  /* 2F /     */
   OS_CTYPE_NUMBER,                 /* 30 0     */
   OS_CTYPE_NUMBER,                 /* 31 1     */
   OS_CTYPE_NUMBER,                 /* 32 2     */
   OS_CTYPE_NUMBER,                 /* 33 3     */
   OS_CTYPE_NUMBER,                 /* 34 4     */
   OS_CTYPE_NUMBER,                 /* 35 5     */
   OS_CTYPE_NUMBER,                 /* 36 6     */
   OS_CTYPE_NUMBER,                 /* 37 7     */
   OS_CTYPE_NUMBER,                 /* 38 8     */
   OS_CTYPE_NUMBER,                 /* 39 9     */
   OS_CTYPE_PUNCT,                  /* 3A :     */
   OS_CTYPE_PUNCT,                  /* 3B ;     */
   OS_CTYPE_PUNCT,                  /* 3C <     */
   OS_CTYPE_PUNCT,                  /* 3D =     */
   OS_CTYPE_PUNCT,                  /* 3E >     */
   OS_CTYPE_PUNCT,                  /* 3F ?     */
   OS_CTYPE_PUNCT,                  /* 40 @     */
   OS_CTYPE_UPPER|OS_CTYPE_HEX,     /* 41 A     */
   OS_CTYPE_UPPER|OS_CTYPE_HEX,     /* 42 B     */
   OS_CTYPE_UPPER|OS_CTYPE_HEX,     /* 43 C     */
   OS_CTYPE_UPPER|OS_CTYPE_HEX,     /* 44 D     */
   OS_CTYPE_UPPER|OS_CTYPE_HEX,     /* 45 E     */
   OS_CTYPE_UPPER|OS_CTYPE_HEX,     /* 46 F     */
   OS_CTYPE_UPPER,                  /* 47 G     */
   OS_CTYPE_UPPER,                  /* 48 H     */
   OS_CTYPE_UPPER,                  /* 49 I     */
   OS_CTYPE_UPPER,                  /* 4A J     */
   OS_CTYPE_UPPER,                  /* 4B K     */
   OS_CTYPE_UPPER,                  /* 4C L     */
   OS_CTYPE_UPPER,                  /* 4D M     */
   OS_CTYPE_UPPER,                  /* 4E N     */
   OS_CTYPE_UPPER,                  /* 4F O     */
   OS_CTYPE_UPPER,                  /* 50 P     */
   OS_CTYPE_UPPER,                  /* 51 Q     */
   OS_CTYPE_UPPER,                  /* 52 R     */
   OS_CTYPE_UPPER,                  /* 53 S     */
   OS_CTYPE_UPPER,                  /* 54 T     */
   OS_CTYPE_UPPER,                  /* 55 U     */
   OS_CTYPE_UPPER,                  /* 56 V     */
   OS_CTYPE_UPPER,                  /* 57 W     */
   OS_CTYPE_UPPER,                  /* 58 X     */
   OS_CTYPE_UPPER,                  /* 59 Y     */
   OS_CTYPE_UPPER,                  /* 5A Z     */
   OS_CTYPE_PUNCT,                  /* 5B [     */
   OS_CTYPE_PUNCT,                  /* 5C \     */
   OS_CTYPE_PUNCT,                  /* 5D ]     */
   OS_CTYPE_PUNCT,                  /* 5E ^     */
   OS_CTYPE_PUNCT,                  /* 5F _     */
   OS_CTYPE_PUNCT,                  /* 60 `     */
   OS_CTYPE_LOWER|OS_CTYPE_HEX,     /* 61 a     */
   OS_CTYPE_LOWER|OS_CTYPE_HEX,     /* 62 b     */
   OS_CTYPE_LOWER|OS_CTYPE_HEX,     /* 63 c     */
   OS_CTYPE_LOWER|OS_CTYPE_HEX,     /* 64 d     */
   OS_CTYPE_LOWER|OS_CTYPE_HEX,     /* 65 e     */
   OS_CTYPE_LOWER|OS_CTYPE_HEX,     /* 66 f     */
   OS_CTYPE_LOWER,                  /* 67 g     */
   OS_CTYPE_LOWER,                  /* 68 h     */
   OS_CTYPE_LOWER,                  /* 69 i     */
   OS_CTYPE_LOWER,                  /* 6A j     */
   OS_CTYPE_LOWER,                  /* 6B k     */
   OS_CTYPE_LOWER,                  /* 6C l     */
   OS_CTYPE_LOWER,                  /* 6D m     */
   OS_CTYPE_LOWER,                  /* 6E n     */
   OS_CTYPE_LOWER,                  /* 6F o     */
   OS_CTYPE_LOWER,                  /* 70 p     */
   OS_CTYPE_LOWER,                  /* 71 q     */
   OS_CTYPE_LOWER,                  /* 72 r     */
   OS_CTYPE_LOWER,                  /* 73 s     */
   OS_CTYPE_LOWER,                  /* 74 t     */
   OS_CTYPE_LOWER,                  /* 75 u     */
   OS_CTYPE_LOWER,                  /* 76 v     */
   OS_CTYPE_LOWER,                  /* 77 w     */
   OS_CTYPE_LOWER,                  /* 78 x     */
   OS_CTYPE_LOWER,                  /* 79 y     */
   OS_CTYPE_LOWER,                  /* 7A z     */
   OS_CTYPE_PUNCT,                  /* 7B {     */
   OS_CTYPE_PUNCT,                  /* 7C |     */
   OS_CTYPE_PUNCT,                  /* 7D }     */
   OS_CTYPE_PUNCT,                  /* 7E ~     */
   OS_CTYPE_CTRL,                   /* 7F (DEL) */

   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0
};
