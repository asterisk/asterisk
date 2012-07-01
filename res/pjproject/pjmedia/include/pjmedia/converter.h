/* $Id$ */
/*
 * Copyright (C) 2010-2011 Teluu Inc. (http://www.teluu.com)
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
#ifndef __PJMEDIA_CONVERTER_H__
#define __PJMEDIA_CONVERTER_H__


/**
 * @file pjmedia/converter.h Format conversion utilities
 * @brief Format conversion utilities
 */

#include <pjmedia/frame.h>
#include <pjmedia/format.h>
#include <pj/list.h>
#include <pj/pool.h>


/**
 * @defgroup PJMEDIA_CONVERTER Format converter
 * @ingroup PJMEDIA_FRAME_OP
 * @brief Audio and video converter utilities
 * @{
 */

PJ_BEGIN_DECL

/**
 * This describes conversion parameter. It specifies the source and
 * destination formats of the conversion.
 */
typedef struct pjmedia_conversion_param
{
    pjmedia_format	src;	/**< Source format.		*/
    pjmedia_format	dst;	/**< Destination format.	*/
} pjmedia_conversion_param;


/** Forward declaration of factory operation structure */
typedef struct pjmedia_converter_factory_op pjmedia_converter_factory_op;

/**
 * Converter priority guides. Converter priority determines which converter
 * instance to be used if more than one converters are able to perform the
 * requested conversion. Converter implementor can use this value to order
 * the preference based on attributes such as quality or performance. Higher
 * number indicates higher priority.
 */
typedef enum pjmedia_converter_priority_guide
{
    /** Lowest priority. */
    PJMEDIA_CONVERTER_PRIORITY_LOWEST 		= 0,

    /** Normal priority. */
    PJMEDIA_CONVERTER_PRIORITY_NORMAL 		= 15000,

    /** Highest priority. */
    PJMEDIA_CONVERTER_PRIORITY_HIGHEST 		= 32000
} pjmedia_converter_priority_guide;

/**
 * Converter factory. The converter factory registers a callback function
 * to create converters.
 */
typedef struct pjmedia_converter_factory
{
    /**
     * Standard list members.
     */
    PJ_DECL_LIST_MEMBER(struct pjmedia_converter_factory);

    /**
     * Factory name.
     */
    const char 			 *name;

    /**
     * Converter priority determines which converter instance to be used if
     * more than one converters are able to perform the requested conversion.
     * Converter implementor can use this value to order the preference based
     * on attributes such as quality or performance. Higher number indicates
     * higher priority. The pjmedia_converter_priority_guide enumeration shall
     * be used as the base value to set the priority.
     */
    int priority;

    /**
     * Pointer to factory operation.
     */
    pjmedia_converter_factory_op *op;

} pjmedia_converter_factory;

/** Forward declaration for converter operation. */
typedef struct pjmedia_converter_op pjmedia_converter_op;

/**
 * This structure describes a converter instance.
 */
typedef struct pjmedia_converter
{
    /**
     * Pointer to converter operation.
     */
    pjmedia_converter_op *op;

} pjmedia_converter;


/**
 * Converter factory operation.
 */
struct pjmedia_converter_factory_op
{
    /**
     * This function creates a converter with the specified conversion format,
     * if such format is supported.
     *
     * @param cf	The converter factory.
     * @param pool	Pool to allocate memory from.
     * @param prm	Conversion parameter.
     * @param p_cv	Pointer to hold the created converter instance.
     *
     * @return		PJ_SUCCESS if converter has been created successfully.
     */
    pj_status_t (*create_converter)(pjmedia_converter_factory *cf,
				    pj_pool_t *pool,
				    const pjmedia_conversion_param *prm,
				    pjmedia_converter **p_cv);

    /**
     * Destroy the factory.
     *
     * @param cf	The converter factory.
     */
    void (*destroy_factory)(pjmedia_converter_factory *cf);
};

/**
 * Converter operation.
 */
struct pjmedia_converter_op
{
    /**
     * Convert the buffer in the source frame and save the result in the
     * buffer of the destination frame, according to conversion format that
     * was specified when the converter was created.
     *
     * Note that application should use #pjmedia_converter_convert() instead
     * of calling this function directly.
     *
     * @param cv	The converter instance.
     * @param src_frame	The source frame.
     * @param dst_frame	The destination frame.
     *
     * @return		PJ_SUCCESS if conversion has been performed
     * 			successfully.
     */
    pj_status_t (*convert)(pjmedia_converter *cv,
			   pjmedia_frame *src_frame,
			   pjmedia_frame *dst_frame);

    /**
     * Destroy the converter instance.
     *
     * Note that application should use #pjmedia_converter_destroy() instead
     * of calling this function directly.
     *
     * @param cv	The converter.
     */
    void (*destroy)(pjmedia_converter *cv);

};


/**
 * Opaque data type for conversion manager. Typically, the conversion manager
 * is a singleton instance, although application may instantiate more than one
 * instances of this if required.
 */
typedef struct pjmedia_converter_mgr pjmedia_converter_mgr;


/**
 * Create a new conversion manager instance. This will also set the pointer
 * to the singleton instance if the value is still NULL.
 *
 * @param pool		Pool to allocate memory from.
 * @param mgr		Pointer to hold the created instance of the
 * 			conversion manager.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_converter_mgr_create(pj_pool_t *pool,
						  pjmedia_converter_mgr **mgr);

/**
 * Get the singleton instance of the conversion manager.
 *
 * @return		The instance.
 */
PJ_DECL(pjmedia_converter_mgr*) pjmedia_converter_mgr_instance(void);

/**
 * Manually assign a specific video manager instance as the singleton
 * instance. Normally this is not needed if only one instance is ever
 * going to be created, as the library automatically assign the singleton
 * instance.
 *
 * @param mgr		The instance to be used as the singleton instance.
 * 			Application may specify NULL to clear the singleton
 * 			singleton instance.
 */
PJ_DECL(void) pjmedia_converter_mgr_set_instance(pjmedia_converter_mgr *mgr);

/**
 * Destroy a converter manager. If the manager happens to be the singleton
 * instance, the singleton instance will be set to NULL.
 *
 * @param mgr		The converter manager. Specify NULL to use
 * 			the singleton instance.
 */
PJ_DECL(void) pjmedia_converter_mgr_destroy(pjmedia_converter_mgr *mgr);

/**
 * Register a converter factory to the converter manager.
 *
 * @param mgr		The converter manager. Specify NULL to use
 * 			the singleton instance.
 * @param f		The converter factory to be registered.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t)
pjmedia_converter_mgr_register_factory(pjmedia_converter_mgr *mgr,
				       pjmedia_converter_factory *f);

/**
 * Unregister a previously registered converter factory from the converter
 * manager.
 *
 * @param mgr		The converter manager. Specify NULL to use
 * 			the singleton instance.
 * @param f		The converter factory to be unregistered.
 * @param call_destroy	If this is set to non-zero, the \a destroy_factory()
 * 			callback of the factory will be called while
 * 			unregistering the factory from the manager.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t)
pjmedia_converter_mgr_unregister_factory(pjmedia_converter_mgr *mgr,
				         pjmedia_converter_factory *f,
				         pj_bool_t call_destroy);

/**
 * Create a converter instance to perform the specified format conversion
 * as specified in \a param.
 *
 * @param mgr		The converter manager. Specify NULL to use
 * 			the singleton instance.
 * @param pool		Pool to allocate the memory from.
 * @param param		Conversion parameter.
 * @param p_cv		Pointer to hold the created converter.
 *
 * @return		PJ_SUCCESS if a converter has been created successfully
 * 			or the appropriate error code.
 */
PJ_DECL(pj_status_t) pjmedia_converter_create(pjmedia_converter_mgr *mgr,
					      pj_pool_t *pool,
					      pjmedia_conversion_param *param,
					      pjmedia_converter **p_cv);

/**
 * Convert the buffer in the source frame and save the result in the
 * buffer of the destination frame, according to conversion format that
 * was specified when the converter was created.
 *
 * @param cv		The converter instance.
 * @param src_frame	The source frame.
 * @param dst_frame	The destination frame.
 *
 * @return		PJ_SUCCESS if conversion has been performed
 * 			successfully.
 */
PJ_DECL(pj_status_t) pjmedia_converter_convert(pjmedia_converter *cv,
					       pjmedia_frame *src_frame,
					       pjmedia_frame *dst_frame);

/**
 * Destroy the converter.
 *
 * @param cv		The converter instance.
 */
PJ_DECL(void) pjmedia_converter_destroy(pjmedia_converter *cv);


PJ_END_DECL

/**
 * @}
 */


#endif /* __PJMEDIA_CONVERTER_H__ */


