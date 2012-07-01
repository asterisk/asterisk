/* $Id$ */
/*
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <pjmedia-videodev/config.h>


#if defined(PJMEDIA_VIDEO_DEV_HAS_DSHOW) && PJMEDIA_VIDEO_DEV_HAS_DSHOW != 0


#include <assert.h>
#include <streams.h>

typedef void (*input_callback)(void *user_data, IMediaSample *pMediaSample);

const GUID CLSID_NullRenderer = {0xF9168C5E, 0xCEB2, 0x4FAA, {0xB6, 0xBF,
                                 0x32, 0x9B, 0xF3, 0x9F, 0xA1, 0xE4}};

const GUID CLSID_SourceFilter = {0xF9168C5E, 0xCEB2, 0x4FAA, {0xB6, 0xBF,
                                 0x32, 0x9B, 0xF3, 0x9F, 0xA1, 0xE5}};

class NullRenderer: public CBaseRenderer
{
public:
    NullRenderer(HRESULT *pHr);
    virtual ~NullRenderer();

    virtual HRESULT CheckMediaType(const CMediaType *pmt);
    virtual HRESULT DoRenderSample(IMediaSample *pMediaSample);

    input_callback  input_cb;
    void           *user_data;
};

class OutputPin: public CBaseOutputPin
{
public:
    OutputPin(CBaseFilter *pFilter, CCritSec *pLock, HRESULT *pHr);
    ~OutputPin();

    HRESULT Push(void *buf, long size);

    virtual HRESULT CheckMediaType(const CMediaType *pmt);
    virtual HRESULT DecideBufferSize(IMemAllocator *pAlloc, 
                                     ALLOCATOR_PROPERTIES *ppropInputRequest);

    CMediaType mediaType;
    long bufSize;
};

class SourceFilter: public CBaseFilter
{
public:
    SourceFilter();
    ~SourceFilter();

    int GetPinCount();
    CBasePin* GetPin(int n);

protected:
    CCritSec lock;
    OutputPin* outPin;
};

OutputPin::OutputPin(CBaseFilter *pFilter, CCritSec *pLock, HRESULT *pHr):
    CBaseOutputPin("OutputPin", pFilter, pLock, pHr, L"OutputPin")
{
}

OutputPin::~OutputPin()
{
}

HRESULT OutputPin::CheckMediaType(const CMediaType *pmt)
{
    return S_OK;
}

HRESULT OutputPin::DecideBufferSize(IMemAllocator *pAlloc, 
                                    ALLOCATOR_PROPERTIES *ppropInputRequest)
{
    ALLOCATOR_PROPERTIES properties;

    ppropInputRequest->cbBuffer = bufSize;
    ppropInputRequest->cBuffers = 1;

    /* First set the buffer descriptions we're interested in */
    pAlloc->SetProperties(ppropInputRequest, &properties);

    return S_OK;
}

HRESULT OutputPin::Push(void *buf, long size)
{
    HRESULT hr;
    IMediaSample *pSample;
    VIDEOINFOHEADER *vi;
    AM_MEDIA_TYPE *pmt;
    BYTE *dst_buf;

    /**
     * Hold the critical section here as the pin might get disconnected
     * during the Deliver() method call.
     */
    m_pLock->Lock();

    hr = GetDeliveryBuffer(&pSample, NULL, NULL, 0);
    if (FAILED(hr))
        goto on_error;

    pSample->GetMediaType(&pmt);
    if (pmt) {
        mediaType.Set(*pmt);
        bufSize = pmt->lSampleSize;
    }

    pSample->GetPointer(&dst_buf);
    vi = (VIDEOINFOHEADER *)mediaType.pbFormat;
    if (vi->rcSource.right == vi->bmiHeader.biWidth) {
        assert(pSample->GetSize() >= size);
        memcpy(dst_buf, buf, size);
    } else {
        unsigned i, bpp;
        unsigned dststride, srcstride;
        BYTE *src_buf = (BYTE *)buf;

        bpp = size / abs(vi->bmiHeader.biHeight) / vi->rcSource.right;
        dststride = vi->bmiHeader.biWidth * bpp;
        srcstride = vi->rcSource.right * bpp;
        for (i = abs(vi->bmiHeader.biHeight); i > 0; i--) {
            memcpy(dst_buf, src_buf, srcstride);
            dst_buf += dststride;
            src_buf += srcstride;
        }
    }
    pSample->SetActualDataLength(size);

    hr = Deliver(pSample);

    pSample->Release();

on_error:
    m_pLock->Unlock();
    return hr;
}

SourceFilter::SourceFilter(): CBaseFilter("SourceFilter", NULL, &lock, 
                                          CLSID_SourceFilter)
{
    HRESULT hr;
    outPin = new OutputPin(this, &lock, &hr);
}

SourceFilter::~SourceFilter()
{
}

int SourceFilter::GetPinCount()
{
    return 1;
}

CBasePin* SourceFilter::GetPin(int n)
{
    return outPin;
}

NullRenderer::NullRenderer(HRESULT *pHr): CBaseRenderer(CLSID_NullRenderer,
                                                        "NullRenderer",
                                                        NULL, pHr)
{
    input_cb = NULL;
}

NullRenderer::~NullRenderer()
{
}

HRESULT NullRenderer::CheckMediaType(const CMediaType *pmt)
{
    return S_OK;
}

HRESULT NullRenderer::DoRenderSample(IMediaSample *pMediaSample)
{
    if (input_cb)
        input_cb(user_data, pMediaSample);

    return S_OK;
}

extern "C" IBaseFilter* NullRenderer_Create(input_callback input_cb,
                                             void *user_data)
{
    HRESULT hr;
    NullRenderer *renderer = new NullRenderer(&hr);
    renderer->AddRef();
    renderer->input_cb = input_cb;
    renderer->user_data = user_data;

    return (CBaseFilter *)renderer;
}

extern "C" IBaseFilter* SourceFilter_Create(SourceFilter **pSrc)
{
    SourceFilter *src = new SourceFilter();
    src->AddRef();
    *pSrc = src;

    return (CBaseFilter *)src;
}

extern "C" HRESULT SourceFilter_Deliver(SourceFilter *src,
                                        void *buf, long size)
{
    return ((OutputPin *)src->GetPin(0))->Push(buf, size);
}

extern "C" void SourceFilter_SetMediaType(SourceFilter *src,
                                          AM_MEDIA_TYPE *pmt)
{
    ((OutputPin *)src->GetPin(0))->mediaType.Set(*pmt);
    ((OutputPin *)src->GetPin(0))->bufSize = pmt->lSampleSize;
}


#endif	/* PJMEDIA_VIDEO_DEV_HAS_DSHOW */
