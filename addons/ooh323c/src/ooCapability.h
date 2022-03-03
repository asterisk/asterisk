/*
 * Copyright (C) 2004-2005 by Objective Systems, Inc.
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
/**
 * @file ooCapability.h
 * This file contains Capability management functions.
 */
#ifndef OO_CAPABILITY_H_
#define OO_CAPABILITY_H_
#include "ootypes.h"
#include "ooasn1.h"


#define OO_GSMFRAMESIZE 33 /* standard frame size for gsm is 33 bytes */

#define OORX      (1<<0)
#define OOTX      (1<<1)
#define OORXANDTX (1<<2)
#define OORXTX    (1<<3) /* For symmetric capabilities */
/* Various types of caps. Note that not all
   supported */
typedef enum OOCapabilities{
   OO_CAP_AUDIO_BASE      = 0,
   OO_G726		  = 1,
   OO_G711ALAW64K         = 2,
   OO_G711ALAW56K         = 3,
   OO_G711ULAW64K         = 4,
   OO_G711ULAW56K         = 5,
   OO_G722_64k            = 6,
   OO_G722_56k            = 7,
   OO_G722_48k            = 8,
   OO_G7231               = 9,
   OO_G728                = 10,
   OO_G729                = 11,
   OO_G729A               = 12,
#if 0
   OO_IS11172_AUDIO       = 13,
   OO_IS13818_AUDIO       = 14,
#else
   OO_AMRNB		  = 13,
   OO_G726AAL2		  = 14,
#endif
   OO_G729B               = 15,
   OO_G729AB              = 16,
   OO_G7231C              = 17,
   OO_GSMFULLRATE         = 18,
   OO_GSMHALFRATE         = 19,
   OO_GSMENHANCEDFULLRATE = 20,
   OO_GENERICAUDIO        = 21,
   OO_G729EXT             = 22,
#if 0
   OO_AUDIO_VBD           = 23,
#else
   OO_SPEEX		  = 23,
#endif
   OO_AUDIOTELEPHONYEVENT = 24,
   OO_AUDIO_TONE          = 25,
   OO_EXTELEM1            = 26,
   OO_CAP_VIDEO_BASE      = 27,
   OO_NONSTDVIDEO         = 28,
   OO_H261VIDEO           = 29,
   OO_H262VIDEO           = 30,
   OO_H263VIDEO           = 31,
   OO_IS11172VIDEO        = 32,  /* mpeg */
   OO_GENERICVIDEO        = 33,
   OO_EXTELEMVIDEO        = 34,
   OO_T38		  = 35
} OOCapabilities;


/*DTMF capabilities*/
#define OO_CAP_DTMF_RFC2833              (1<<0)
#define OO_CAP_DTMF_Q931                 (1<<1)
#define OO_CAP_DTMF_H245_alphanumeric    (1<<2)
#define OO_CAP_DTMF_H245_signal          (1<<3)
#define OO_CAP_DTMF_CISCO		 (1<<4)

/**
 * This structure defines the preference order for capabilities.
 *
 */
typedef struct OOCapPrefs {
  int order[20];
  int index;
}OOCapPrefs;

typedef struct OOCapParams {
   int txframes;  /*!< Number of frames per packet for transmission */
   int rxframes;  /*!< Number of frames per packet for reception */
   OOBOOL silenceSuppression;
} OOCapParams;

typedef struct OOGSMCapParams {
   unsigned txframes;
   unsigned rxframes;
   OOBOOL scrambled;
   OOBOOL comfortNoise;
} OOGSMCapParams;

typedef enum OOPictureFormat{
   OO_PICFORMAT_SQCIF,
   OO_PICFORMAT_QCIF,
   OO_PICFORMAT_CIF,
   OO_PICFORMAT_CIF4,
   OO_PICFORMAT_CIF16
}OOPictureFormat;

typedef struct OOH263CapParams {
   enum OOPictureFormat picFormat; /* !< One of sqcif, qcif, cif, cif4, cif16*/
   unsigned MPI; /* !< Minimum Picture Interval */
  unsigned maxBitRate; /* !< Maximum bit rate for transmission/reception in units of 100 bits/sec */
} OOH263CapParams;

struct OOH323CallData;
struct OOLogicalChannel;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This callback is used for starting media receive channel. This callback
 * function is triggered when receive media channel has to be started.
 * @param call     Call for which receive media channel has to be started.
 * @param pChannel Channel details. This structure has important information
 *                 such as rtp ip:port and capability describing media type
 *                 to be received.
 * @return         OO_OK, on success. OO_FAILED, on failure
 */
typedef int (*cb_StartReceiveChannel)
     (struct OOH323CallData *call, struct OOLogicalChannel *pChannel);


/**
 * This callback is used for starting media transmit channel. This callback
 * function is triggered when transmit media channel has to be started.
 * @param call     Call for which transmit media channel has to be started.
 * @param pChannel Channel details. This structure has important information
 *                 such as rtp ip:port and capability describing media type
 *                 to be transmitted.
 * @return         OO_OK, on success. OO_FAILED, on failure
 */
typedef int (*cb_StartTransmitChannel)
     (struct OOH323CallData *call, struct OOLogicalChannel *pChannel);

/**
 * This callback is used for stopping media receive channel. This callback
 * function is triggered when receive media channel has to be stopped.
 * @param call     Call for which receive media channel has to be stopped.
 * @param pChannel Channel details. This structure has important information
 *                 such as rtp ip:port and capability describing media type
 *                 being received.
 * @return         OO_OK, on success. OO_FAILED, on failure
 */
typedef int (*cb_StopReceiveChannel)
     (struct OOH323CallData *call, struct OOLogicalChannel *pChannel);

/**
 * This callback is used for stopping media transmit channel. This callback
 * function is triggered when transmit media channel has to be stopped.
 * @param call     Call for which transmit media channel has to be stopped.
 * @param pChannel Channel details. This structure has important information
 *                 such as rtp ip:port and capability describing media type
 *                 being transmitted.
 * @return         OO_OK, on success. OO_FAILED, on failure
 */
typedef int (*cb_StopTransmitChannel)
     (struct OOH323CallData *call, struct OOLogicalChannel *pChannel);

typedef enum OOCapType {
   OO_CAP_TYPE_AUDIO,
   OO_CAP_TYPE_VIDEO,
   OO_CAP_TYPE_DATA
} OOCapType;

/**
 * Structure to store information related to end point
 * capability
 */
typedef struct ooH323EpCapability {
   int dir;
   int cap;
   OOCapType capType;
   void *params;
   cb_StartReceiveChannel startReceiveChannel;
   cb_StartTransmitChannel startTransmitChannel;
   cb_StopReceiveChannel stopReceiveChannel;
   cb_StopTransmitChannel stopTransmitChannel;
   struct ooH323EpCapability *next;
} ooH323EpCapability;




#ifndef EXTERN
#if defined (MAKE_DLL)
#define EXTERN __declspec(dllexport)
#else
#define EXTERN
#endif /* MAKE_DLL */
#endif /* EXTERN */

/**
 * @defgroup capmgmt  Capability Management
 * @{
 */

/**
 * This function is used to add rfc2833 based dtmf detection capability
 * @param call                   Call if enabling for call, else null for
 *                               endpoint.
 * @param dynamicRTPPayloadType  dynamicRTPPayloadType to be used.
 * @return                       OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCapabilityEnableDTMFRFC2833
   (struct OOH323CallData *call, int dynamicRTPPayloadType);

/**
 * This function is used to remove rfc2833 dtmf detection capability.
 * @param call             Handle to call, if disabling for the call, else NULL
 *                         for end-point.
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCapabilityDisableDTMFRFC2833(struct OOH323CallData *call);


/**
 * This function is used to enable support for H.245 based alphanumeric dtmf
 * capability.
 * @param call             Handle to call, if enabling for the call, else NULL
 *                         for end-point.
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCapabilityEnableDTMFH245Alphanumeric(struct OOH323CallData *call);

/**
 * This function is used to disable support for H.245 based alphanumeric dtmf
 * capability.
 * @param call             Handle to call, if disabling for the call, else NULL
 *                         for end-point.
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCapabilityDisableDTMFH245Alphanumeric
                                             (struct OOH323CallData *call);

/**
 * This function is used to enable support for H.245 based signal dtmf
 * capability.
 * @param call             Handle to call, if enabling for the call, else NULL
 *                         for end-point.
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCapabilityEnableDTMFH245Signal(struct OOH323CallData *call);

/**
 * This function is used to disable support for H.245 based signal dtmf
 * capability.
 * @param call             Handle to call, if disabling for the call, else NULL
 *                         for end-point.
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCapabilityDisableDTMFH245Signal(struct OOH323CallData *call);

/**
 * This function is used to enable support for dtmf using Q.931 Keypad IE.
 * @param call             Handle to call, if enabling for the call, else NULL
 *                         for end-point.
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCapabilityEnableDTMFQ931Keypad(struct OOH323CallData *call);

/**
 * This function is used to disable support for dtmf using Q.931 Keypad IE.
 * @param call             Handle to call, if disabling for the call, else NULL
 *                         for end-point.
 * @return                 OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCapabilityDisableDTMFQ931Keypad(struct OOH323CallData *call);

/**
 * This function is used to add simple capabilities which have only rxframes
 * and txframes parameters to the endpoint or call.(ex. G711, G728, G723.1,
 * G729)
 * @param call                 Handle to a call. If this is not Null, then
 *                             capability is added to call's remote endpoint
 *                             capability list, else it is added to local H323
 *                             endpoint list.
 * @param cap                  Type of G711 capability to be added.
 * @param txframes             Number of frames per packet for transmission.
 * @param rxframes             Number of frames per packet for reception.
 * @param silenceSuppression   Indicates support for silence suppression.
 *                             Used only in case of g7231, otherwise ignored.
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 * @param remote               TRUE, if adding call's remote capability.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCapabilityAddSimpleCapability
   (struct OOH323CallData *call, int cap, int txframes, int rxframes,
    OOBOOL silenceSuppression, int dir,
    cb_StartReceiveChannel startReceiveChannel,
    cb_StartTransmitChannel startTransmitChannel,
    cb_StopReceiveChannel stopReceiveChannel,
    cb_StopTransmitChannel stopTransmitChannel,
    OOBOOL remote);


/**
 * This is an internal helper function which is used to add a GSM capability
 * to local endpoints capability list or to remote endpoints capability list or
 * to a call's capability list.
 * @param call                 Handle to a call. If this is not Null, then
 *                             capability is added to call's remote endpoint
 *                             capability list, else it is added to local H323
 *                             endpoint list.
 * @param cap                  Type of GSM capability to be added.
 * @param framesPerPkt         Number of GSM frames per packet.
 * @param comfortNoise         Comfort noise spec for the capability.
 * @param scrambled            Scrambled enabled/disabled for the capability.
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 * @param remote               TRUE, if adding call's remote capabilities.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure.
 */
int ooCapabilityAddGSMCapability(struct OOH323CallData *call, int cap,
                                unsigned framesPerPkt, OOBOOL comfortNoise,
                                OOBOOL scrambled, int dir,
                                cb_StartReceiveChannel startReceiveChannel,
                                cb_StartTransmitChannel startTransmitChannel,
                                cb_StopReceiveChannel stopReceiveChannel,
                                cb_StopTransmitChannel stopTransmitChannel,
                                OOBOOL remote);


/**
 * This function is used to add H263 video capability to local endpoints
 * capability list or to remote endpoints capability list or to a call's
 * capability list.
 * @param call                 Handle to a call. If this is not Null, then
 *                             capability is added to call's remote endpoint
 *                             capability list, else it is added to local H323
 *                             endpoint list.
 * @param sqcifMPI             Minimum picture interval for encoding/decoding
 *                             of SQCIF pictures.
 * @param qcifMPI              Minimum picture interval for encoding/decoding
 *                             of QCIF pictures.
 * @param cifMPI               Minimum picture interval for encoding/decoding
 *                             of CIF pictures.
 * @param cif4MPI              Minimum picture interval for encoding/decoding
 *                             of CIF4 pictures.
 * @param cif16MPI             Minimum picture interval for encoding/decoding
 *                             of CIF16 pictures.
 * @param maxBitRate           Maximum bit rate in units of 100 bits/s at
 *                             which a transmitter can transmit video or a
 *                             receiver can receive video.
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 * @param remote               TRUE, if adding call's remote capabilities.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure.
 */
EXTERN int ooCapabilityAddH263VideoCapability(struct OOH323CallData *call,
                               unsigned sqcifMPI, unsigned qcifMPI,
                               unsigned cifMPI, unsigned cif4MPI,
                               unsigned cif16MPI, unsigned maxBitRate, int dir,
                               cb_StartReceiveChannel startReceiveChannel,
                               cb_StartTransmitChannel startTransmitChannel,
                               cb_StopReceiveChannel stopReceiveChannel,
                               cb_StopTransmitChannel stopTransmitChannel,
                               OOBOOL remote);


/**
 * This function is an helper function to ooCapabilityAddH263VideoCapability.
 * @param call                 Handle to a call. If this is not Null, then
 *                             capability is added to call's remote endpoint
 *                             capability list, else it is added to local H323
 *                             endpoint list.
 * @param sqcifMPI             Minimum picture interval for encoding/decoding
 *                             of SQCIF pictures.
 * @param qcifMPI              Minimum picture interval for encoding/decoding
 *                             of QCIF pictures.
 * @param cifMPI               Minimum picture interval for encoding/decoding
 *                             of CIF pictures.
 * @param cif4MPI              Minimum picture interval for encoding/decoding
 *                             of CIF4 pictures.
 * @param cif16MPI             Minimum picture interval for encoding/decoding
 *                             of CIF16 pictures.
 * @param maxBitRate           Maximum bit rate in units of 100 bits/s at
 *                             which a transmitter can transmit video or a
 *                             receiver can receive video.
 * @param dir                  Direction of capability.OORX, OOTX, OORXANDTX
 * @param startReceiveChannel  Callback function to start receive channel.
 * @param startTransmitChannel Callback function to start transmit channel.
 * @param stopReceiveChannel   Callback function to stop receive channel.
 * @param stopTransmitChannel  Callback function to stop transmit channel.
 * @param remote               TRUE, if adding call's remote capabilities.
 *
 * @return                     OO_OK, on success. OO_FAILED, on failure.
 */
int ooCapabilityAddH263VideoCapability_helper(struct OOH323CallData *call,
                              unsigned sqcifMPI, unsigned qcifMPI,
                              unsigned cifMPI, unsigned cif4MPI,
                              unsigned cif16MPI, unsigned maxBitRate, int dir,
                              cb_StartReceiveChannel startReceiveChannel,
                              cb_StartTransmitChannel startTransmitChannel,
                              cb_StopReceiveChannel stopReceiveChannel,
                              cb_StopTransmitChannel stopTransmitChannel,
                              OOBOOL remote);

/**
 * This function is used to add a audio capability to calls remote
 * capability list.
 * @param call                Handle to the call.
 * @param audioCap            Handle to the remote endpoint's audio capability.
 * @param dir                 Direction in which capability is supported by
 *                            remote endpoint.
 *
 * @return                    OO_OK, on success. OO_FAILED, otherwise.
 */
int ooAddRemoteAudioCapability(struct OOH323CallData *call,
                               H245AudioCapability *audioCap, int dir);


/**
 * This function is used to add a capability to call's remote  capability list.
 * The capabilities to be added are extracted from received TCS message.
 * @param call           Handle to the call.
 * @param cap            Handle to the remote endpoint's H245 capability.
 *
 * @return               OO_OK, on success. OO_FAILED, otherwise.
 */
int ooAddRemoteCapability(struct OOH323CallData *call, H245Capability *cap);

/**
 * This function is used to update joint capabilities for call. It checks
 * whether remote capability can be supported by local capabilities for the
 * call and if supported makes entry into the joint capability list for the
 * call.
 * @param call           Handle to the call
 * @param cap            Remote cap which will be tested for compatibility.
 *
 * @return               returns OO_OK, if updated else OO_FAILED;
 */
EXTERN int ooCapabilityUpdateJointCapabilities
   (struct OOH323CallData* call, H245Capability *cap);


/**
 * This function is used to update joint video capabilities for call. It checks
 * whether remote capability can be supported by local capabilities for the
 * call and if supported makes entry into the joint capability list for the
 * call.
 * @param call           Handle to the call
 * @param videoCap       Remote video capability which will be tested for
 *                       compatibility.
 * @param dir            Direction of the capability
 *
 * @return               returns OO_OK, if updated else OO_FAILED;
 */
EXTERN int ooCapabilityUpdateJointCapabilitiesVideo
   (struct OOH323CallData *call, H245VideoCapability *videoCap, int dir);


/**
 * This function is used to update joint video H263 capabilities for call. It
 * checks whether remote capability can be supported by local capabilities for
 * the call and if supported makes entry into the joint capability list for the
 * call.
 * @param call           Handle to the call
 * @param pH263Cap       Remote H263 video capability which will be tested for
 *                       compatibility.
 * @param dir            Direction of the H263 capability
 *
 * @return               returns OO_OK, if updated else OO_FAILED;
 */
EXTERN int ooCapabilityUpdateJointCapabilitiesVideoH263
   (struct OOH323CallData *call, H245H263VideoCapability *pH263Cap, int dir);


/**
 * This function is used to test whether the endpoint capability in the
 * specified direction can be supported by the audio capability.
 * @param call               Handle to the call.
 * @param epCap              Endpoint capability.
 * @param dataType           Data type with which compatibility has to
 *                           be tested.
 * @param dir                Direction indicating whether endpoint capability
 *                           will be used for transmission or reception.
 *
 * @return                   TRUE, if compatible. FALSE, otherwise.
 */

ASN1BOOL ooCapabilityCheckCompatibility(struct OOH323CallData *call,
                                        ooH323EpCapability *epCap,
                                        H245DataType *dataType, int dir);


/**
 * This function is used to create a audio capability structure using the
 * capability type.
 * @param epCap       Capability.
 * @param pctxt       Handle to OOCTXT which will be used to allocate memory
 *                    for new audio capability.
 * @param dir         Direction in which the newly created capability will be
 *                    used.
 *
 * @return            Newly created audio capability on success, NULL on
 *                    failure.
 */
struct H245AudioCapability* ooCapabilityCreateAudioCapability
(ooH323EpCapability* epCap, OOCTXT *pctxt, int dir);

/**
 * This function is used to create a video capability structure using the
 * capability type.
 * @param epCap       Capability.
 * @param pctxt       Handle to OOCTXT which will be used to allocate memory
 *                    for new video capability.
 * @param dir         Direction in which the newly created capability will be
 *                    used.
 *
 * @return            Newly created video capability on success, NULL on
 *                    failure.
 */
struct H245VideoCapability* ooCapabilityCreateVideoCapability
   (ooH323EpCapability *epCap, OOCTXT *pctxt, int dir);


/**
 * This function is used to create a dtmf capability which can be added to
 * a TCS message.
 * @param cap         Type of dtmf capability to be created.
 * @param pctxt       Pointer to OOCTXT structure to be used for memory
 *                    allocation.
 *
 * @return            Pointer to the created DTMF capability, NULL in case of
 *                    failure.
 */
void * ooCapabilityCreateDTMFCapability(int cap, int dtmfcodec, OOCTXT *pctxt);


/**
 * This function is used to create a GSM Full Rate capability structure.
 * @param epCap       Handle to the endpoint capability.
 * @param pctxt       Handle to OOCTXT which will be used to allocate memory
 *                    for new audio capability.
 * @param dir         Direction for the newly created capability.
 *
 * @return            Newly created audio capability on success, NULL on
 *                    failure.
 */
struct H245AudioCapability* ooCapabilityCreateGSMFullRateCapability
   (ooH323EpCapability *epCap, OOCTXT* pctxt, int dir);

/**
 * This function is used to create a simple(g711, g728, g723.1, g729) audio
 * capability structure.
 *
 * @param epCap       Handle to the endpoint capability
 * @param pctxt       Handle to OOCTXT which will be used to allocate memory
 *                    for new audio capability.
 * @param dir         Direction in which the newly created capability will be
 *                    used.
 *
 * @return            Newly created audio capability on success, NULL on
 *                    failure.
 */
struct H245AudioCapability* ooCapabilityCreateSimpleCapability
   (ooH323EpCapability *epCap, OOCTXT* pctxt, int dir);
struct H245AudioCapability* ooCapabilityCreateNonStandardCapability
   (ooH323EpCapability *epCap, OOCTXT* pctxt, int dir);


/**
 * This function is used to create a H263 video capability
 * structure.
 * @param epCap       Handle to the endpoint capability
 * @param pctxt       Handle to OOCTXT which will be used to allocate memory
 *                    for new video capability.
 * @param dir         Direction in which the newly created capability will be
 *                    used.
 *
 * @return            Newly created video capability on success, NULL on
 *                    failure.
 */
struct H245VideoCapability* ooCapabilityCreateH263VideoCapability
(ooH323EpCapability *epCap, OOCTXT* pctxt, int dir);


/**
 * This function is used to determine whether a particular capability
 * can be supported by the endpoint.
 * @param call       Handle to the call.
 * @param audioCap   Handle to the audio capability.
 * @param dir        Direction in which support is desired.
 *
 * @return          Handle to the copyof capability which supports audioCap,
 *                  Null if none found
 */
ooH323EpCapability* ooIsAudioDataTypeSupported
(struct OOH323CallData *call, H245AudioCapability* audioCap, int dir);

/**
 * This function is used to determine whether a particular video capability
 * can be supported by the endpoint.
 * @param call       Handle to the call.
 * @param pVideoCap  Handle to the  video capability.
 * @param dir        Direction in which support is desired.
 *
 * @return          Handle to the copy of capability which supports video
 *                  capability, Null if none found
 */
ooH323EpCapability* ooIsVideoDataTypeSupported
   (struct OOH323CallData *call, H245VideoCapability* pVideoCap, int dir);

/**
 * This function is used to determine whether a particular H263 capability
 * can be supported by the endpoint.
 * @param call       Handle to the call.
 * @param pH263Cap   Handle to the H263 video capability.
 * @param dir        Direction in which support is desired.
 * @param picFormat  Picture type(cif, qcif etc.)
 *
 * @return          Handle to the copy of capability which supports H263
 *                  capability, Null if none found
 */
ooH323EpCapability* ooIsVideoDataTypeH263Supported
   (struct OOH323CallData *call, H245H263VideoCapability* pH263Cap, int dir,
    OOPictureFormat picFormat);

/**
 * This function is used to determine whether a particular capability type
 * can be supported by the endpoint.
 * @param call       Handle to the call.
 * @param data       Handle to the capability type.
 * @param dir        Direction in which support is desired.
 *
 * @return          Handle to the copy of capability which supports specified
 *                  capability type, Null if none found
 */
ooH323EpCapability* ooIsDataTypeSupported
(struct OOH323CallData *call, H245DataType *data, int dir);

/* fill t.38 application data */
H245DataMode_application* ooCreateT38ApplicationData
                                (OOCTXT* pctxt, H245DataMode_application *app);

H245DataApplicationCapability* ooCapabilityCreateT38Capability
   (ooH323EpCapability *epCap, OOCTXT* pctxt, int dir);


/**
 * This function is used to clear the capability preference order.
 * @param call      Handle to call, if capability preference order for call
 *                  has to be cleared, NULL for endpoint.
 *
 * @return          OO_OK, on success. OO_FAILED, on failure
 */
EXTERN  int ooResetCapPrefs(struct OOH323CallData *call);

/**
 * This function is used to remove a particular capability from preference
 * list.
 * @param call     Handle to call, if call's preference list has to be modified
 *                 else NULL, to modify endpoint's preference list.
 * @param cap      Capability to be removed
 *
 * @return         OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN  int ooRemoveCapFromCapPrefs(struct OOH323CallData *call, int cap);

/**
 * This function is used to append a particular capability to preference
 * list.
 * @param call     Handle to call, if call's preference list has to be modified
 *                 else NULL, to modify endpoint's preference list.
 * @param cap      Capability to be appended.
 *
 * @return         OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooAppendCapToCapPrefs(struct OOH323CallData *call, int cap);

/**
 * This function is used to change preference order of a particular capability
 * in the preference list.
 * @param call     Handle to call, if call's preference list has to be modified
 *                 else NULL, to modify endpoint's preference list.
 * @param cap      Capability concerned
 * @param pos      New position in the preference order
 *
 * @return         OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooChangeCapPrefOrder(struct OOH323CallData *call, int cap, int pos);

/**
 * This function is used to prepend a particular capability to preference
 * list.
 * @param call     Handle to call, if call's preference list has to be modified
 *                 else NULL, to modify endpoint's preference list.
 * @param cap      Capability to be prepended.
 *
 * @return         OO_OK, on success. OO_FAILED, otherwise.
 */
EXTERN int ooPreppendCapToCapPrefs(struct OOH323CallData *call, int cap);

/**
 * This function is used to retrieve the text description for a capability
 * type.
 * @param cap     Capability
 * @return        The text description string.
 */
EXTERN const char* ooGetCapTypeText (OOCapabilities cap);


EXTERN int epCapIsPreferred(struct OOH323CallData *call, ooH323EpCapability *epCap);

/**/
ASN1BOOL ooCapabilityCheckCompatibility_Simple
   (struct OOH323CallData *call, ooH323EpCapability* epCap,
    H245AudioCapability* audioCap, int dir);
ASN1BOOL ooCapabilityCheckCompatibility_NonStandard
   (struct OOH323CallData *call, ooH323EpCapability* epCap,
    H245AudioCapability* audioCap, int dir);
OOBOOL ooCapabilityCheckCompatibility_GSM
   (struct OOH323CallData *call, ooH323EpCapability* epCap,
    H245AudioCapability* audioCap, int dir);
OOBOOL ooCapabilityCheckCompatibility_T38
   (struct OOH323CallData *call, ooH323EpCapability* epCap,
    H245DataApplicationCapability* t38Cap, int dir);
OOBOOL ooCapabilityCheckCompatibility_H263Video
   (struct OOH323CallData *call, ooH323EpCapability *epCap,
    H245VideoCapability *pVideoCap, int dir);
OOBOOL ooCapabilityCheckCompatibility_Audio
   (struct OOH323CallData *call, ooH323EpCapability* epCap,
    H245AudioCapability* audioCap, int dir);
OOBOOL ooCapabilityCheckCompatibility_Video
   (struct OOH323CallData *call, ooH323EpCapability* epCap,
    H245VideoCapability* videoCap, int dir);
ooH323EpCapability* ooIsAudioDataTypeGSMSupported
   (struct OOH323CallData *call, H245AudioCapability* audioCap, int dir);
ooH323EpCapability* ooIsAudioDataTypeSimpleSupported
   (struct OOH323CallData *call, H245AudioCapability* audioCap, int dir);
ooH323EpCapability* ooIsT38Supported
   (struct OOH323CallData *call, H245DataApplicationCapability* t38Cap, int dir);
ooH323EpCapability* ooIsAudioDataTypeNonStandardSupported
   (struct OOH323CallData *call, H245AudioCapability* audioCap, int dir);
int ooAddRemoteDataApplicationCapability(struct OOH323CallData *call,
                               H245DataApplicationCapability *dataCap,
                               int dir);
int ooCapabilityEnableDTMFCISCO
   (struct OOH323CallData *call, int dynamicRTPPayloadType);
int ooCapabilityDisableDTMFCISCO(struct OOH323CallData *call);
int ooCapabilityAddT38Capability
   (struct OOH323CallData *call, int cap, int dir,
    cb_StartReceiveChannel startReceiveChannel,
    cb_StartTransmitChannel startTransmitChannel,
    cb_StopReceiveChannel stopReceiveChannel,
    cb_StopTransmitChannel stopTransmitChannel,
    OOBOOL remote);


/**/


/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif
