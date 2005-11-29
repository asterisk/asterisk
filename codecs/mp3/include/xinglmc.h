/*____________________________________________________________________________
   
   FreeAmp - The Free MP3 Player
   Portions Copyright (C) 1998-1999 EMusic.com

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
   
   $Id$

____________________________________________________________________________*/

#ifndef INCLUDED_XINGLMC_H_
#define INCLUDED_XINGLMC_H_

/* system headers */
#include <stdlib.h>
#include <time.h>

/* project headers */
#include "config.h"

#include "pmi.h"
#include "pmo.h"
#include "mutex.h"
#include "event.h"
#include "lmc.h"
#include "thread.h"
#include "mutex.h"
#include "queue.h"
#include "semaphore.h"

extern    "C"
{
#include "mhead.h"
#include "port.h"
}

#define BS_BUFBYTES 60000U
#define PCM_BUFBYTES 60000U

typedef struct
{
   int       (*decode_init) (MPEG_HEAD * h, int framebytes_arg,
              int reduction_code, int transform_code,
              int convert_code, int freq_limit);
   void      (*decode_info) (DEC_INFO * info);
             IN_OUT(*decode) (unsigned char *bs, short *pcm);
}
AUDIO;

#define FRAMES_FLAG     0x0001
#define BYTES_FLAG      0x0002
#define TOC_FLAG        0x0004
#define VBR_SCALE_FLAG  0x0008

#define FRAMES_AND_BYTES (FRAMES_FLAG | BYTES_FLAG)

// structure to receive extracted header
// toc may be NULL
typedef struct 
{
    int h_id;       // from MPEG header, 0=MPEG2, 1=MPEG1
    int samprate;   // determined from MPEG header
    int flags;      // from Xing header data
    int frames;     // total bit stream frames from Xing header data
    int bytes;      // total bit stream bytes from Xing header data
    int vbr_scale;  // encoded vbr scale from Xing header data
    unsigned char *toc;  // pointer to unsigned char toc_buffer[100]
                         // may be NULL if toc not desired
}   XHEADDATA;

enum
{
   lmcError_MinimumError = 1000,
   lmcError_DecodeFailed,
   lmcError_AudioDecodeInitFailed,
   lmcError_DecoderThreadFailed,
   lmcError_PMIError,
   lmcError_PMOError,
   lmcError_MaximumError
};

class     XingLMC:public LogicalMediaConverter
{

   public:
            XingLMC(FAContext *context);
   virtual ~XingLMC();

   virtual uint32 CalculateSongLength(const char *url);

   virtual Error ChangePosition(int32 position);

   virtual Error CanDecode();
   virtual void  Clear();
   virtual Error ExtractMediaInfo();

   virtual void  SetPMI(PhysicalMediaInput *pmi) { m_pPmi = pmi; };
   virtual void  SetPMO(PhysicalMediaOutput *pmo) { m_pPmo = pmo; };
   virtual Error Prepare(PullBuffer *pInputBuffer, PullBuffer *&pOutBuffer);
   virtual Error InitDecoder();

   virtual Error SetEQData(float *);
   virtual Error SetEQData(bool);

   virtual vector<char *> *GetExtensions(void);
   
 private:

   static void          DecodeWorkerThreadFunc(void *);
   void                 DecodeWork();
   Error                BeginRead(void *&pBuffer, unsigned int iBytesNeeded,
                                  bool bBufferUp = true);
   Error                BlockingBeginRead(void *&pBuffer, 
                                          unsigned int iBytesNeeded);
   Error                EndRead(size_t iBytesUsed);
   Error                AdvanceBufferToNextFrame();
   Error                GetHeadInfo();
   Error                GetBitstreamStats(float &fTotalSeconds, float &fMsPerFrame,
                                          int &iTotalFrames, int &iSampleRate, 
                                          int &iLayer);

   int                  GetXingHeader(XHEADDATA *X,  unsigned char *buf);
   int                  SeekPoint(unsigned char TOC[100], int file_bytes, float percent);
   int                  ExtractI4(unsigned char *buf);

   PhysicalMediaInput  *m_pPmi;
   PhysicalMediaOutput *m_pPmo;

   int                  m_iMaxWriteSize;
   int32                m_frameBytes, m_iBufferUpInterval, m_iBufferSize;
   size_t               m_lFileSize;
   MPEG_HEAD            m_sMpegHead;
   int32                m_iBitRate;
   bool                 m_bBufferingUp;
   Thread              *m_decoderThread;

   int32                m_frameCounter;
   time_t               m_iBufferUpdate;
   char                *m_szUrl;
   const char          *m_szError;
   AUDIO                m_audioMethods; 
   XHEADDATA           *m_pXingHeader;
   
   // These vars are used for a nasty hack.
   FILE                *m_fpFile;
   char                *m_pLocalReadBuffer;
};

#endif /* _XINGLMC_H */




