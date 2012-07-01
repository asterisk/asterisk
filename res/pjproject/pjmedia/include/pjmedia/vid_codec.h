/* $Id$ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
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
#ifndef __PJMEDIA_VID_CODEC_H__
#define __PJMEDIA_VID_CODEC_H__


/**
 * @file vid_codec.h
 * @brief Video codec framework.
 */

#include <pjmedia/codec.h>
#include <pjmedia/event.h>
#include <pjmedia/format.h>
#include <pjmedia/types.h>
#include <pj/list.h>
#include <pj/pool.h>

PJ_BEGIN_DECL

/**
 * @defgroup PJMEDIA_VID_CODEC Video Codecs
 * @ingroup PJMEDIA_CODEC
 * @{
 */

#define PJMEDIA_VID_CODEC_MAX_DEC_FMT_CNT    8
#define PJMEDIA_VID_CODEC_MAX_FPS_CNT        16

/**
 * This enumeration specifies the packetization property of video encoding
 * process. The value is bitmask, and smaller value will have higher priority
 * to be used.
 */
typedef enum pjmedia_vid_packing
{
    /**
     * This specifies that the packetization is unknown, or if nothing
     * is supported.
     */
    PJMEDIA_VID_PACKING_UNKNOWN,

    /**
     * This specifies that the result of video encoding process will be
     * segmented into packets, which is suitable for RTP transmission.
     * The maximum size of the packets is set in \a enc_mtu field of
     * pjmedia_vid_codec_param.
     */
    PJMEDIA_VID_PACKING_PACKETS = 1,

    /**
     * This specifies that video encoding function will produce a whole
     * or full frame from the source frame. This is normally used for
     * encoding video for offline storage such as to an AVI file. The
     * maximum size of the packets is set in \a enc_mtu field of
     * pjmedia_vid_codec_param.
     */
    PJMEDIA_VID_PACKING_WHOLE = 2

} pjmedia_vid_packing;


/**
 * Enumeration of video frame info flag for the bit_info field in the
 * pjmedia_frame.
 */
typedef enum pjmedia_vid_frm_bit_info
{
    /**
     * The video frame is keyframe.
     */
    PJMEDIA_VID_FRM_KEYFRAME	= 1

} pjmedia_vid_frm_bit_info;


/**
 * Encoding option.
 */
typedef struct pjmedia_vid_encode_opt
{
    /**
     * Flag to force the encoder to generate keyframe for the specified input
     * frame. When this flag is set, application can verify the result by
     * examining PJMEDIA_VID_FRM_KEYFRAME flag in the bit_info field of the
     * output frame.
     */
    pj_bool_t force_keyframe;

} pjmedia_vid_encode_opt;


/** 
 * Identification used to search for codec factory that supports specific 
 * codec specification. 
 */
typedef struct pjmedia_vid_codec_info
{
    pjmedia_format_id   fmt_id;         /**< Encoded format ID              */
    unsigned            pt;             /**< Payload type		    */
    pj_str_t	        encoding_name;  /**< Encoding name                  */
    pj_str_t	        encoding_desc;	/**< Encoding desc		    */
    unsigned            clock_rate;     /**< Clock rate			    */
    pjmedia_dir         dir;            /**< Direction                      */
    unsigned            dec_fmt_id_cnt; /**< # of supported encoding source 
                                             format IDs                     */
    pjmedia_format_id   dec_fmt_id[PJMEDIA_VID_CODEC_MAX_DEC_FMT_CNT];
                                        /**< Supported encoding source 
                                             format IDs                     */
    unsigned		packings;	/**< Supported or requested packings,
					     strategies, bitmask from
					     pjmedia_vid_packing	    */
    unsigned            fps_cnt;        /**< # of supported frame-rates, can be
					     zero (support any frame-rate)  */
    pjmedia_ratio       fps[PJMEDIA_VID_CODEC_MAX_FPS_CNT];
                                        /**< Supported frame-rates	    */

} pjmedia_vid_codec_info;


/** 
 * Detailed codec attributes used in configuring a codec and in querying
 * the capability of codec factories. Default attributes of any codecs could
 * be queried using #pjmedia_vid_codec_mgr_get_default_param() and modified
 * using #pjmedia_vid_codec_mgr_set_default_param().
 *
 * Please note that codec parameter also contains SDP specific setting, 
 * #dec_fmtp and #enc_fmtp, which may need to be set appropriately based on
 * the effective setting. See each codec documentation for more detail.
 */
typedef struct pjmedia_vid_codec_param
{
    pjmedia_dir         dir;            /**< Direction                      */
    pjmedia_vid_packing packing; 	/**< Packetization strategy.	    */

    pjmedia_format      enc_fmt;        /**< Encoded format	            */
    pjmedia_codec_fmtp  enc_fmtp;       /**< Encoder fmtp params	    */
    unsigned            enc_mtu;        /**< MTU or max payload size setting*/

    pjmedia_format      dec_fmt;        /**< Decoded format	            */
    pjmedia_codec_fmtp  dec_fmtp;       /**< Decoder fmtp params	    */

    pj_bool_t		ignore_fmtp;	/**< Ignore fmtp params. If set to
					     PJ_TRUE, the codec will apply
					     format settings specified in
					     enc_fmt and dec_fmt only.	    */

} pjmedia_vid_codec_param;


/**
 * Duplicate video codec parameter.
 *
 * @param pool	    The pool.
 * @param src	    The video codec parameter to be duplicated.
 *
 * @return	    Duplicated codec parameter.
 */
PJ_DECL(pjmedia_vid_codec_param*) pjmedia_vid_codec_param_clone(
					pj_pool_t *pool, 
					const pjmedia_vid_codec_param *src);

/**
 * Forward declaration for video codec.
 */
typedef struct pjmedia_vid_codec pjmedia_vid_codec;


/**
 * This structure describes codec operations. Each codec MUST implement
 * all of these functions.
 */
typedef struct pjmedia_vid_codec_op
{
    /** 
     * See #pjmedia_vid_codec_init().
     */
    pj_status_t	(*init)(pjmedia_vid_codec *codec, 
			pj_pool_t *pool );

    /** 
     * See #pjmedia_vid_codec_open().
     */
    pj_status_t	(*open)(pjmedia_vid_codec *codec, 
			pjmedia_vid_codec_param *param );

    /** 
     * See #pjmedia_vid_codec_close().
     */
    pj_status_t (*close)(pjmedia_vid_codec *codec);

    /** 
     * See #pjmedia_vid_codec_modify().
     */
    pj_status_t	(*modify)(pjmedia_vid_codec *codec,
			  const pjmedia_vid_codec_param *param);

    /** 
     * See #pjmedia_vid_codec_get_param().
     */
    pj_status_t	(*get_param)(pjmedia_vid_codec *codec,
			     pjmedia_vid_codec_param *param);

    /**
     * See #pjmedia_vid_codec_encode_begin().
     */
    pj_status_t (*encode_begin)(pjmedia_vid_codec *codec,
				const pjmedia_vid_encode_opt *opt,
				const pjmedia_frame *input,
				unsigned out_size,
				pjmedia_frame *output,
				pj_bool_t *has_more);

    /**
     * See #pjmedia_vid_codec_encode_more()
     */
    pj_status_t (*encode_more)(pjmedia_vid_codec *codec,
			       unsigned out_size,
			       pjmedia_frame *output,
			       pj_bool_t *has_more);


    /*
     * See #pjmedia_vid_codec_decode().
     */
    pj_status_t (*decode)(pjmedia_vid_codec *codec,
			  pj_size_t count,
			  pjmedia_frame packets[],
			  unsigned out_size,
			  pjmedia_frame *output);

    /**
     * See #pjmedia_vid_codec_recover()
     */
    pj_status_t (*recover)(pjmedia_vid_codec *codec,
			   unsigned out_size,
			   pjmedia_frame *output);

} pjmedia_vid_codec_op;



/*
 * Forward declaration for pjmedia_vid_codec_factory.
 */
typedef struct pjmedia_vid_codec_factory pjmedia_vid_codec_factory;


/**
 * This structure describes a video codec instance. Codec implementers
 * should use #pjmedia_vid_codec_init() to initialize this structure with
 * default values.
 */
struct pjmedia_vid_codec
{
    /** Entries to put this codec instance in codec factory's list. */
    PJ_DECL_LIST_MEMBER(struct pjmedia_vid_codec);

    /** Codec's private data. */
    void			*codec_data;

    /** Codec factory where this codec was allocated. */
    pjmedia_vid_codec_factory   *factory;

    /** Operations to codec. */
    pjmedia_vid_codec_op	*op;
};



/**
 * This structure describes operations that must be supported by codec 
 * factories.
 */
typedef struct pjmedia_vid_codec_factory_op
{
    /** 
     * Check whether the factory can create codec with the specified 
     * codec info.
     *
     * @param factory	The codec factory.
     * @param info	The codec info.
     *
     * @return		PJ_SUCCESS if this factory is able to create an
     *			instance of codec with the specified info.
     */
    pj_status_t	(*test_alloc)(pjmedia_vid_codec_factory *factory, 
			      const pjmedia_vid_codec_info *info );

    /** 
     * Create default attributes for the specified codec ID. This function
     * can be called by application to get the capability of the codec.
     *
     * @param factory	The codec factory.
     * @param info	The codec info.
     * @param attr	The attribute to be initialized.
     *
     * @return		PJ_SUCCESS if success.
     */
    pj_status_t (*default_attr)(pjmedia_vid_codec_factory *factory, 
    				const pjmedia_vid_codec_info *info,
    				pjmedia_vid_codec_param *attr );

    /** 
     * Enumerate supported codecs that can be created using this factory.
     * 
     *  @param factory	The codec factory.
     *  @param count	On input, specifies the number of elements in
     *			the array. On output, the value will be set to
     *			the number of elements that have been initialized
     *			by this function.
     *  @param info	The codec info array, which contents will be 
     *			initialized upon return.
     *
     *  @return		PJ_SUCCESS on success.
     */
    pj_status_t (*enum_info)(pjmedia_vid_codec_factory *factory, 
			     unsigned *count, 
			     pjmedia_vid_codec_info codecs[]);

    /** 
     * Create one instance of the codec with the specified codec info.
     *
     * @param factory	The codec factory.
     * @param info	The codec info.
     * @param p_codec	Pointer to receive the codec instance.
     *
     * @return		PJ_SUCCESS on success.
     */
    pj_status_t (*alloc_codec)(pjmedia_vid_codec_factory *factory, 
			       const pjmedia_vid_codec_info *info,
			       pjmedia_vid_codec **p_codec);

    /** 
     * This function is called by codec manager to return a particular 
     * instance of codec back to the codec factory.
     *
     * @param factory	The codec factory.
     * @param codec	The codec instance to be returned.
     *
     * @return		PJ_SUCCESS on success.
     */
    pj_status_t (*dealloc_codec)(pjmedia_vid_codec_factory *factory, 
				 pjmedia_vid_codec *codec );

} pjmedia_vid_codec_factory_op;



/**
 * Codec factory describes a module that is able to create codec with specific
 * capabilities. These capabilities can be queried by codec manager to create
 * instances of codec.
 */
struct pjmedia_vid_codec_factory
{
    /** Entries to put this structure in the codec manager list. */
    PJ_DECL_LIST_MEMBER(struct pjmedia_vid_codec_factory);

    /** The factory's private data. */
    void		     *factory_data;

    /** Operations to the factory. */
    pjmedia_vid_codec_factory_op *op;

};


/**
 * Opaque declaration for codec manager.
 */
typedef struct pjmedia_vid_codec_mgr pjmedia_vid_codec_mgr;

/**
 * Declare maximum codecs
 */
#define PJMEDIA_VID_CODEC_MGR_MAX_CODECS	    32


/**
 * Initialize codec manager. If there is no the default video codec manager,
 * this function will automatically set the default video codec manager to
 * the new codec manager instance. Normally this function is called by pjmedia
 * endpoint's initialization code.
 *
 * @param pool	    The pool instance.
 * @param mgr	    The pointer to the new codec manager instance.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_codec_mgr_create(pj_pool_t *pool,
                                                  pjmedia_vid_codec_mgr **mgr);


/**
 * Destroy codec manager. Normally this function is called by pjmedia
 * endpoint's deinitialization code.
 *
 * @param mgr	    Codec manager instance.  If NULL, it is the default codec
 *		    manager instance will be destroyed.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_codec_mgr_destroy(pjmedia_vid_codec_mgr *mgr);


/**
 * Get the default codec manager instance.
 *
 * @return	    The default codec manager instance or NULL if none.
 */
PJ_DECL(pjmedia_vid_codec_mgr*) pjmedia_vid_codec_mgr_instance(void);


/**
 * Set the default codec manager instance.
 *
 * @param mgr	    The codec manager instance.
 */
PJ_DECL(void) pjmedia_vid_codec_mgr_set_instance(pjmedia_vid_codec_mgr* mgr);


/** 
 * Register codec factory to codec manager. This will also register
 * all supported codecs in the factory to the codec manager.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param factory   The codec factory to be registered.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_vid_codec_mgr_register_factory( pjmedia_vid_codec_mgr *mgr,
				        pjmedia_vid_codec_factory *factory);

/**
 * Unregister codec factory from the codec manager. This will also
 * remove all the codecs registered by the codec factory from the
 * codec manager's list of supported codecs.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param factory   The codec factory to be unregistered.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_vid_codec_mgr_unregister_factory( pjmedia_vid_codec_mgr *mgr, 
				          pjmedia_vid_codec_factory *factory);

/**
 * Enumerate all supported codecs that have been registered to the
 * codec manager by codec factories.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param count	    On input, specifies the number of elements in
 *		    the array. On output, the value will be set to
 *		    the number of elements that have been initialized
 *		    by this function.
 * @param info	    The codec info array, which contents will be 
 *		    initialized upon return.
 * @param prio	    Optional pointer to receive array of codec priorities.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t)
pjmedia_vid_codec_mgr_enum_codecs(pjmedia_vid_codec_mgr *mgr,
				  unsigned *count,
				  pjmedia_vid_codec_info info[],
				  unsigned *prio);


/**
 * Get codec info for the specified payload type. The payload type must be
 * static or locally defined in #pjmedia_video_pt.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param pt	    The payload type/number.
 * @param info	    Pointer to receive codec info.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_vid_codec_mgr_get_codec_info( pjmedia_vid_codec_mgr *mgr,
				      unsigned pt,
				      const pjmedia_vid_codec_info **info);


/**
 * Get codec info for the specified format ID.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param fmt_id    Format ID. See #pjmedia_format_id
 * @param info	    Pointer to receive codec info.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_vid_codec_mgr_get_codec_info2(pjmedia_vid_codec_mgr *mgr,
				      pjmedia_format_id fmt_id,
				      const pjmedia_vid_codec_info **info);


/**
 * Convert codec info struct into a unique codec identifier.
 * A codec identifier looks something like "H263/90000".
 *
 * @param info	    The codec info
 * @param id	    Buffer to put the codec info string.
 * @param max_len   The length of the buffer.
 *
 * @return	    The null terminated codec info string, or NULL if
 *		    the buffer is not long enough.
 */
PJ_DECL(char*) pjmedia_vid_codec_info_to_id(const pjmedia_vid_codec_info *info,
                                            char *id, unsigned max_len );


/**
 * Find codecs by the unique codec identifier. This function will find
 * all codecs that match the codec identifier prefix. For example, if
 * "H26" is specified, then it will find "H263/90000", "H264/90000",
 * and so on, up to the maximum count specified in the argument.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param codec_id  The full codec ID or codec ID prefix. If an empty
 *		    string is given, it will match all codecs.
 * @param count	    Maximum number of codecs to find. On return, it
 *		    contains the actual number of codecs found.
 * @param p_info    Array of pointer to codec info to be filled. This
 *		    argument may be NULL, which in this case, only
 *		    codec count will be returned.
 * @param prio	    Optional array of codec priorities.
 *
 * @return	    PJ_SUCCESS if at least one codec info is found.
 */
PJ_DECL(pj_status_t) 
pjmedia_vid_codec_mgr_find_codecs_by_id(pjmedia_vid_codec_mgr *mgr,
					const pj_str_t *codec_id,
					unsigned *count,
					const pjmedia_vid_codec_info *p_info[],
					unsigned prio[]);


/**
 * Set codec priority. The codec priority determines the order of
 * the codec in the SDP created by the endpoint. If more than one codecs
 * are found with the same codec_id prefix, then the function sets the
 * priorities of all those codecs.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param codec_id  The full codec ID or codec ID prefix. If an empty
 *		    string is given, it will match all codecs.
 * @param prio	    Priority to be set. The priority can have any value
 *		    between 1 to 255. When the priority is set to zero,
 *		    the codec will be disabled.
 *
 * @return	    PJ_SUCCESS if at least one codec info is found.
 */
PJ_DECL(pj_status_t)
pjmedia_vid_codec_mgr_set_codec_priority(pjmedia_vid_codec_mgr *mgr, 
					 const pj_str_t *codec_id,
					 pj_uint8_t prio);


/**
 * Get default codec param for the specified codec info.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param info	    The codec info, which default parameter's is being
 *		    queried.
 * @param param	    On return, will be filled with the default codec
 *		    parameter.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_vid_codec_mgr_get_default_param(pjmedia_vid_codec_mgr *mgr,
					const pjmedia_vid_codec_info *info,
					pjmedia_vid_codec_param *param);


/**
 * Set default codec param for the specified codec info.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param pool	    The pool instance.
 * @param info	    The codec info, which default parameter's is being
 *		    updated.
 * @param param	    The new default codec parameter. Set to NULL to reset
 *		    codec parameter to library default settings.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_vid_codec_mgr_set_default_param(pjmedia_vid_codec_mgr *mgr,
				        const pjmedia_vid_codec_info *info,
				        const pjmedia_vid_codec_param *param);


/**
 * Request the codec manager to allocate one instance of codec with the
 * specified codec info. The codec will enumerate all codec factories
 * until it finds factory that is able to create the specified codec.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param info	    The information about the codec to be created.
 * @param p_codec   Pointer to receive the codec instance.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_vid_codec_mgr_alloc_codec( pjmedia_vid_codec_mgr *mgr, 
			           const pjmedia_vid_codec_info *info,
			           pjmedia_vid_codec **p_codec);

/**
 * Deallocate the specified codec instance. The codec manager will return
 * the instance of the codec back to its factory.
 *
 * @param mgr	    The codec manager instance. If NULL, the default codec
 *		    manager instance will be used.
 * @param codec	    The codec instance.
 *
 * @return	    PJ_SUCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_vid_codec_mgr_dealloc_codec(
                                                pjmedia_vid_codec_mgr *mgr, 
						pjmedia_vid_codec *codec);



/** 
 * Initialize codec using the specified attribute.
 *
 * @param codec	    The codec instance.
 * @param pool	    Pool to use when the codec needs to allocate
 *		    some memory.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_INLINE(pj_status_t) pjmedia_vid_codec_init( pjmedia_vid_codec *codec, 
					       pj_pool_t *pool )
{
    return (*codec->op->init)(codec, pool);
}


/** 
 * Open the codec and initialize with the specified parameter.
 * Upon successful initialization, the codec may modify the parameter
 * and fills in the unspecified values (such as size or frame rate of
 * the encoder format, as it may need to be negotiated with remote
 * preferences via SDP fmtp).
 *
 * @param codec	    The codec instance.
 * @param param	    Codec initialization parameter.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_INLINE(pj_status_t) pjmedia_vid_codec_open(pjmedia_vid_codec *codec,
                                              pjmedia_vid_codec_param *param)
{
    return (*codec->op->open)(codec, param);
}


/** 
 * Close and shutdown codec, releasing all resources allocated by
 * this codec, if any.
 *
 * @param codec	    The codec instance.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_INLINE(pj_status_t) pjmedia_vid_codec_close( pjmedia_vid_codec *codec )
{
    return (*codec->op->close)(codec);
}


/** 
 * Modify the codec parameter after the codec is open. 
 * Note that not all codec parameters can be modified during run-time. 
 * When the parameter cannot be changed, this function will return 
 * non-PJ_SUCCESS, and the original parameters will not be changed.
 *
 * @param codec	The codec instance.
 * @param param	The new codec parameter.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_INLINE(pj_status_t)
pjmedia_vid_codec_modify(pjmedia_vid_codec *codec,
                         const pjmedia_vid_codec_param *param)
{
    return (*codec->op->modify)(codec, param);
}


/** 
 * Get the codec parameter after the codec is opened. 
 *
 * @param codec	The codec instance.
 * @param param	The codec parameter.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_INLINE(pj_status_t)
pjmedia_vid_codec_get_param(pjmedia_vid_codec *codec,
			    pjmedia_vid_codec_param *param)
{
    return (*codec->op->get_param)(codec, param);
}

/** 
 * Encode the specified input frame. The input MUST contain only one picture
 * with the appropriate format as specified when opening the codec. Depending
 * on the packing or packetization set in the \a packing param, the process
 * may produce multiple encoded packets or payloads to represent the picture.
 * This is true for example for PJMEDIA_VID_PACKING_PACKETS packing. In this
 * case, the \a has_more field will be set to PJ_TRUE, and application should
 * call pjmedia_vid_codec_encode_more() to get the remaining results from the
 * codec.
 *
 * @param codec		The codec instance.
 * @param opt		Optional encoding options.
 * @param input		The input frame.
 * @param out_size	The length of buffer in the output frame. This
 * 			should be at least the same as the configured
 * 			encoding MTU of the codec.
 * @param output	The output frame.
 * @param has_more	PJ_TRUE if more payloads are available; application
 * 			should then call pjmedia_vid_codec_encode_more()
 * 			to retrieve the remaining results.
 *
 * @return		PJ_SUCCESS on success;
 */
PJ_INLINE(pj_status_t)
pjmedia_vid_codec_encode_begin( pjmedia_vid_codec *codec,
				const pjmedia_vid_encode_opt *opt,
				const pjmedia_frame *input,
				unsigned out_size,
				pjmedia_frame *output,
				pj_bool_t *has_more)
{
    return (*codec->op->encode_begin)(codec, opt, input, out_size, output,
				      has_more);
}

/**
 * Retrieve more encoded packets/payloads from the codec. Application
 * should call this function repeatedly until \a has_more flag is set
 * to PJ_FALSE.
 *
 * @param codec		The codec instance.
 * @param out_size	The length of buffer in the output frame. This
 * 			should be at least the same as as the configured
 * 			encoding MTU of the codec.
 * @param output	The output frame.
 * @param has_more	PJ_TRUE if more payloads are available, which in
 * 			this case application should call \a encode_more()
 * 			to retrieve them.
 *
 * @return		PJ_SUCCESS on success;
 */
PJ_INLINE(pj_status_t)
pjmedia_vid_codec_encode_more( pjmedia_vid_codec *codec,
			       unsigned out_size,
			       pjmedia_frame *output,
			       pj_bool_t *has_more)
{
    return (*codec->op->encode_more)(codec, out_size, output, has_more);
}

/** 
 * Decode the input packets into one picture. If the packing is set to
 * PJMEDIA_VID_PACKING_PACKETS when opening the codec, the codec is set
 * to decode multiple encoded packets into one picture. These encoded
 * packets are typically retrieved from the jitter buffer. If the packing
 * is set to PJMEDIA_VID_PACKING_WHOLE, then this decode function can only
 * accept one frame as the input.
 *
 * Note that the decoded picture format may different to the configured
 * setting (i.e. the format specified in the #pjmedia_vid_codec_param when
 * opening the codec), in this case the PJMEDIA_EVENT_FMT_CHANGED event will
 * be emitted by the codec to notify the event. The codec parameter will
 * also be updated, and application can query the format by using
 * pjmedia_vid_codec_get_param().
 *
 * @param codec		The codec instance.
 * @param pkt_count	Number of packets in the input.
 * @param packets	Array of input packets, each containing an encoded
 * 			frame.
 * @param out_size	The length of buffer in the output frame.
 * @param output	The output frame.
 *
 * @return		PJ_SUCCESS on success;
 */
PJ_INLINE(pj_status_t) pjmedia_vid_codec_decode(pjmedia_vid_codec *codec,
						pj_size_t pkt_count,
						pjmedia_frame packets[],
						unsigned out_size,
						pjmedia_frame *output)
{
    return (*codec->op->decode)(codec, pkt_count, packets, out_size, output);
}

/**
 * Recover a missing frame.
 *
 * @param codec		The codec instance.
 * @param out_size	The length of buffer in the output frame.
 * @param output	The output frame where generated signal
 *			will be placed.
 *
 * @return		PJ_SUCCESS on success;
 */
PJ_INLINE(pj_status_t) pjmedia_vid_codec_recover(pjmedia_vid_codec *codec,
                                                 unsigned out_size,
                                                 pjmedia_frame *output)
{
    if (codec->op && codec->op->recover)
	return (*codec->op->recover)(codec, out_size, output);
    else
	return PJ_ENOTSUP;
}


/**
 * @}
 */

/**
 * @defgroup PJMEDIA_CODEC_VID_CODECS Supported video codecs
 * @ingroup PJMEDIA_VID_CODEC
 */




PJ_END_DECL


#endif	/* __PJMEDIA_VID_CODEC_H__ */
