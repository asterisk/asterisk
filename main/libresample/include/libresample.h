/**********************************************************************

  resample.h

  Real-time library interface by Dominic Mazzoni

  Based on resample-1.7:
    http://www-ccrma.stanford.edu/~jos/resample/

  License: LGPL - see the file LICENSE.txt for more information

**********************************************************************/

/*!
 * \file
 * \brief libresample API
 * \author Dominic Mazzoni
 */

#ifndef LIBRESAMPLE_INCLUDED
#define LIBRESAMPLE_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

/*!
 * \brief Create a resampler
 *
 * \param highQuality Set this argument to non-zero to enable higher quality
 *      resampling.  
 * \param minFactor This is the minimum resampling factor that will be used for
 *      this resampler.  The resampling factor is calculated in the following
 *      way: ( from sample rate / to sample rate ).
 * \param maxFactor This is the maximum resampling factor that will be used for
 *      this resampler.
 *
 * Use this function to create a new resampler that will maintain state
 * information about the stream of audio being resampled.
 *
 * \return A handle to a new resampler
 */
void *resample_open(int      highQuality,
                    double   minFactor,
                    double   maxFactor);

/*!
 * \brief Duplicate a resampler
 *
 * \param handle the resampler to duplicate
 *
 * \return A new handle to a resampler, initialized with the same parameters
 * used to create the original resampler.
 */
void *resample_dup(const void *handle);

/*!
 * \brief Get filter width for resampler
 * 
 * \param handle the resampler
 *
 * \return the filter width.
 */
int resample_get_filter_width(const void *handle);

/*!
 * \brief Resample a chunk of audio
 *
 * \param handle the resampler
 * \param factor the resampling factor.  This factor should be calculated as
 *      ( from sample rate / to sample rate ).  So, for converting from 8 kHz
 *      to 16 kHz, this value would be 2.0.
 * \param inBuffer the input buffer for audio to resample.
 * \param inBufferLen the number of samples in the input buffer
 * \param lastFlag Set this argument to non-zero if the data in the input buffer
 *      is known to be the end of a stream.  This would be used if you're
 *      resampling a file, for example.
 * \param inBufferUsed This is an output parameter that indicates how many
 *      samples were consumed from the input buffer.  Generally, this function
 *      is called in a loop until you know that the entire input buffer has
 *      been consumed, as it may take multiple calls to complete.
 * \param outBuffer This is the output buffer.  This function will write the
 *      resampled audio into this buffer.
 * \param outBufferLen This parameter specifies how many samples there is room
 *      for in the output buffer.
 *
 * This is the main function used for resampling audio.  It should be called
 * in a loop until all of the data from the input buffer is consumed, or the
 * output buffer has been filled.
 *
 * \return the number of samples written to the output buffer.  If the return
 *         value is equal to the value provided in the outBufferLen parameter,
 *         then the output buffer has been filled.
 */
int resample_process(void   *handle,
                     double  factor,
                     float  *inBuffer,
                     int     inBufferLen,
                     int     lastFlag,
                     int    *inBufferUsed,
                     float  *outBuffer,
                     int     outBufferLen);

/*!
 * \brief Close a resampler
 *
 * \param handle the resampler to close
 * 
 * Use this function to release a handle to a resampler that was created using
 * either resample_open() or resample_dup().
 *
 * \return nothing.
 */
void resample_close(void *handle);

#ifdef __cplusplus
}		/* extern "C" */
#endif	/* __cplusplus */

#endif /* LIBRESAMPLE_INCLUDED */
