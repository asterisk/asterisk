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
#include <pjmedia/vid_codec.h>
#include <pjmedia/errno.h>
#include <pj/array.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/string.h>


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#define THIS_FILE   "vid_codec.c"

static pjmedia_vid_codec_mgr *def_vid_codec_mgr;


/* Definition of default codecs parameters */
typedef struct pjmedia_vid_codec_default_param
{
    pj_pool_t			*pool;
    pjmedia_vid_codec_param	*param;
} pjmedia_vid_codec_default_param;


/*
 * Codec manager maintains array of these structs for each supported
 * codec.
 */
typedef struct pjmedia_vid_codec_desc
{
    pjmedia_vid_codec_info	     info;	/**< Codec info.	    */
    pjmedia_codec_id	             id;        /**< Fully qualified name   */
    pjmedia_codec_priority           prio;      /**< Priority.		    */
    pjmedia_vid_codec_factory       *factory;	/**< The factory.	    */
    pjmedia_vid_codec_default_param *def_param; /**< Default codecs 
					             parameters.	    */
} pjmedia_vid_codec_desc;


/* The declaration of video codec manager */
struct pjmedia_vid_codec_mgr
{
    /** Pool factory instance. */
    pj_pool_factory		*pf;

    /** Codec manager mutex. */
    pj_mutex_t			*mutex;

    /** List of codec factories registered to codec manager. */
    pjmedia_vid_codec_factory	 factory_list;

    /** Number of supported codecs. */
    unsigned			 codec_cnt;

    /** Array of codec descriptor. */
    pjmedia_vid_codec_desc	 codec_desc[PJMEDIA_CODEC_MGR_MAX_CODECS];

};



/* Sort codecs in codec manager based on priorities */
static void sort_codecs(pjmedia_vid_codec_mgr *mgr);


/*
 * Duplicate video codec parameter.
 */
PJ_DEF(pjmedia_vid_codec_param*) pjmedia_vid_codec_param_clone(
					pj_pool_t *pool, 
					const pjmedia_vid_codec_param *src)
{
    pjmedia_vid_codec_param *p;
    unsigned i;

    PJ_ASSERT_RETURN(pool && src, NULL);

    p = PJ_POOL_ZALLOC_T(pool, pjmedia_vid_codec_param);

    /* Update codec param */
    pj_memcpy(p, src, sizeof(pjmedia_vid_codec_param));
    for (i = 0; i < src->dec_fmtp.cnt; ++i) {
	pj_strdup(pool, &p->dec_fmtp.param[i].name, 
		  &src->dec_fmtp.param[i].name);
	pj_strdup(pool, &p->dec_fmtp.param[i].val, 
		  &src->dec_fmtp.param[i].val);
    }
    for (i = 0; i < src->enc_fmtp.cnt; ++i) {
	pj_strdup(pool, &p->enc_fmtp.param[i].name, 
		  &src->enc_fmtp.param[i].name);
	pj_strdup(pool, &p->enc_fmtp.param[i].val, 
		  &src->enc_fmtp.param[i].val);
    }

    return p;
}

/*
 * Initialize codec manager.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_create(
                                            pj_pool_t *pool,
                                            pjmedia_vid_codec_mgr **p_mgr)
{
    pjmedia_vid_codec_mgr *mgr;
    pj_status_t status;

    PJ_ASSERT_RETURN(pool, PJ_EINVAL);

    mgr = PJ_POOL_ZALLOC_T(pool, pjmedia_vid_codec_mgr);
    mgr->pf = pool->factory;
    pj_list_init (&mgr->factory_list);
    mgr->codec_cnt = 0;

    /* Create mutex */
    status = pj_mutex_create_recursive(pool, "vid-codec-mgr", &mgr->mutex);
    if (status != PJ_SUCCESS)
	return status;

    if (!def_vid_codec_mgr)
        def_vid_codec_mgr = mgr;

    if (p_mgr)
        *p_mgr = mgr;

    return PJ_SUCCESS;
}

/*
 * Initialize codec manager.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_destroy (pjmedia_vid_codec_mgr *mgr)
{
    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    /* Destroy mutex */
    if (mgr->mutex)
	pj_mutex_destroy(mgr->mutex);

    /* Just for safety, set codec manager states to zero */
    pj_bzero(mgr, sizeof(pjmedia_vid_codec_mgr));

    if (mgr == def_vid_codec_mgr)
        def_vid_codec_mgr = NULL;

    return PJ_SUCCESS;
}


PJ_DEF(pjmedia_vid_codec_mgr*) pjmedia_vid_codec_mgr_instance(void)
{
    //pj_assert(def_vid_codec_mgr);
    return def_vid_codec_mgr;
}

PJ_DEF(void) pjmedia_vid_codec_mgr_set_instance(pjmedia_vid_codec_mgr* mgr)
{
    def_vid_codec_mgr = mgr;
}


/*
 * Register a codec factory.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_register_factory(
                                    pjmedia_vid_codec_mgr *mgr,
				    pjmedia_vid_codec_factory *factory)
{
    pjmedia_vid_codec_info info[PJMEDIA_CODEC_MGR_MAX_CODECS];
    unsigned i, count;
    pj_status_t status;

    PJ_ASSERT_RETURN(factory, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    /* Enum codecs */
    count = PJ_ARRAY_SIZE(info);
    status = factory->op->enum_info(factory, &count, info);
    if (status != PJ_SUCCESS)
	return status;

    pj_mutex_lock(mgr->mutex);

    /* Check codec count */
    if (count + mgr->codec_cnt > PJ_ARRAY_SIZE(mgr->codec_desc)) {
	pj_mutex_unlock(mgr->mutex);
	return PJ_ETOOMANY;
    }


    /* Save the codecs */
    for (i=0; i<count; ++i) {
	pj_memcpy( &mgr->codec_desc[mgr->codec_cnt+i],
		   &info[i], sizeof(pjmedia_vid_codec_info));
	mgr->codec_desc[mgr->codec_cnt+i].prio = PJMEDIA_CODEC_PRIO_NORMAL;
	mgr->codec_desc[mgr->codec_cnt+i].factory = factory;
	pjmedia_vid_codec_info_to_id( &info[i],
				  mgr->codec_desc[mgr->codec_cnt+i].id,
				  sizeof(pjmedia_codec_id));
    }

    /* Update count */
    mgr->codec_cnt += count;

    /* Re-sort codec based on priorities */
    sort_codecs(mgr);

    /* Add factory to the list */
    pj_list_push_back(&mgr->factory_list, factory);

    pj_mutex_unlock(mgr->mutex);

    return PJ_SUCCESS;
}


/*
 * Unregister a codec factory.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_unregister_factory(
				pjmedia_vid_codec_mgr *mgr, 
				pjmedia_vid_codec_factory *factory)
{
    unsigned i;
    PJ_ASSERT_RETURN(factory, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    pj_mutex_lock(mgr->mutex);

    /* Factory must be registered. */
    if (pj_list_find_node(&mgr->factory_list, factory) != factory) {
	pj_mutex_unlock(mgr->mutex);
	return PJ_ENOTFOUND;
    }

    /* Erase factory from the factory list */
    pj_list_erase(factory);


    /* Remove all supported codecs from the codec manager that were created 
     * by the specified factory.
     */
    for (i=0; i<mgr->codec_cnt; ) {

	if (mgr->codec_desc[i].factory == factory) {
	    /* Remove the codec from array of codec descriptions */
	    pj_array_erase(mgr->codec_desc, sizeof(mgr->codec_desc[0]), 
			   mgr->codec_cnt, i);
	    --mgr->codec_cnt;

	} else {
	    ++i;
	}
    }

    pj_mutex_unlock(mgr->mutex);

    return PJ_SUCCESS;
}


/*
 * Enum all codecs.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_enum_codecs(
                                pjmedia_vid_codec_mgr *mgr, 
			        unsigned *count, 
			        pjmedia_vid_codec_info codecs[],
			        unsigned *prio)
{
    unsigned i;

    PJ_ASSERT_RETURN(count && codecs, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    pj_mutex_lock(mgr->mutex);

    if (*count > mgr->codec_cnt)
	*count = mgr->codec_cnt;
    
    for (i=0; i<*count; ++i) {
	pj_memcpy(&codecs[i], 
		  &mgr->codec_desc[i].info, 
		  sizeof(pjmedia_vid_codec_info));
    }

    if (prio) {
	for (i=0; i < *count; ++i)
	    prio[i] = mgr->codec_desc[i].prio;
    }

    pj_mutex_unlock(mgr->mutex);

    return PJ_SUCCESS;
}


/*
 * Get codec info for the specified payload type.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_get_codec_info(
                                    pjmedia_vid_codec_mgr *mgr,
				    unsigned pt,
				    const pjmedia_vid_codec_info **p_info)
{
    unsigned i;

    PJ_ASSERT_RETURN(p_info, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    pj_mutex_lock(mgr->mutex);

    for (i=0; i<mgr->codec_cnt; ++i) {
	if (mgr->codec_desc[i].info.pt == pt) {
	    *p_info = &mgr->codec_desc[i].info;

	    pj_mutex_unlock(mgr->mutex);
	    return PJ_SUCCESS;
	}
    }

    pj_mutex_unlock(mgr->mutex);

    return PJMEDIA_CODEC_EUNSUP;
}


PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_get_codec_info2(
				    pjmedia_vid_codec_mgr *mgr,
				    pjmedia_format_id fmt_id,
				    const pjmedia_vid_codec_info **p_info)
{
    unsigned i;

    PJ_ASSERT_RETURN(p_info, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    pj_mutex_lock(mgr->mutex);

    for (i=0; i<mgr->codec_cnt; ++i) {
	if (mgr->codec_desc[i].info.fmt_id == fmt_id) {
	    *p_info = &mgr->codec_desc[i].info;

	    pj_mutex_unlock(mgr->mutex);
	    return PJ_SUCCESS;
	}
    }

    pj_mutex_unlock(mgr->mutex);

    return PJMEDIA_CODEC_EUNSUP;
}


/*
 * Convert codec info struct into a unique codec identifier.
 * A codec identifier looks something like "H263/34".
 */
PJ_DEF(char*) pjmedia_vid_codec_info_to_id(
                                        const pjmedia_vid_codec_info *info,
				        char *id, unsigned max_len )
{
    int len;

    PJ_ASSERT_RETURN(info && id && max_len, NULL);

    len = pj_ansi_snprintf(id, max_len, "%.*s/%u",
			   (int)info->encoding_name.slen,
			   info->encoding_name.ptr,
			   info->pt);

    if (len < 1 || len >= (int)max_len) {
	id[0] = '\0';
	return NULL;
    }

    return id;
}


/*
 * Find codecs by the unique codec identifier. This function will find
 * all codecs that match the codec identifier prefix. For example, if
 * "L16" is specified, then it will find "L16/8000/1", "L16/16000/1",
 * and so on, up to the maximum count specified in the argument.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_find_codecs_by_id(
                                     pjmedia_vid_codec_mgr *mgr,
				     const pj_str_t *codec_id,
				     unsigned *count,
				     const pjmedia_vid_codec_info *p_info[],
				     unsigned prio[])
{
    unsigned i, found = 0;

    PJ_ASSERT_RETURN(codec_id && count && *count, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    pj_mutex_lock(mgr->mutex);

    for (i=0; i<mgr->codec_cnt; ++i) {

	if (codec_id->slen == 0 ||
	    pj_strnicmp2(codec_id, mgr->codec_desc[i].id, 
			 codec_id->slen) == 0) 
	{

	    if (p_info)
		p_info[found] = &mgr->codec_desc[i].info;
	    if (prio)
		prio[found] = mgr->codec_desc[i].prio;

	    ++found;

	    if (found >= *count)
		break;
	}

    }

    pj_mutex_unlock(mgr->mutex);

    *count = found;

    return found ? PJ_SUCCESS : PJ_ENOTFOUND;
}


/* Swap two codecs positions in codec manager */
static void swap_codec(pjmedia_vid_codec_mgr *mgr, unsigned i, unsigned j)
{
    pjmedia_vid_codec_desc tmp;

    pj_memcpy(&tmp, &mgr->codec_desc[i], sizeof(pjmedia_vid_codec_desc));

    pj_memcpy(&mgr->codec_desc[i], &mgr->codec_desc[j], 
	       sizeof(pjmedia_vid_codec_desc));

    pj_memcpy(&mgr->codec_desc[j], &tmp, sizeof(pjmedia_vid_codec_desc));
}


/* Sort codecs in codec manager based on priorities */
static void sort_codecs(pjmedia_vid_codec_mgr *mgr)
{
    unsigned i;

   /* Re-sort */
    for (i=0; i<mgr->codec_cnt; ++i) {
	unsigned j, max;

	for (max=i, j=i+1; j<mgr->codec_cnt; ++j) {
	    if (mgr->codec_desc[j].prio > mgr->codec_desc[max].prio)
		max = j;
	}

	if (max != i)
	    swap_codec(mgr, i, max);
    }

    /* Change PJMEDIA_CODEC_PRIO_HIGHEST codecs to NEXT_HIGHER */
    for (i=0; i<mgr->codec_cnt; ++i) {
	if (mgr->codec_desc[i].prio == PJMEDIA_CODEC_PRIO_HIGHEST)
	    mgr->codec_desc[i].prio = PJMEDIA_CODEC_PRIO_NEXT_HIGHER;
	else
	    break;
    }
}


/**
 * Set codec priority. The codec priority determines the order of
 * the codec in the SDP created by the endpoint. If more than one codecs
 * are found with the same codec_id prefix, then the function sets the
 * priorities of all those codecs.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_set_codec_priority(
				pjmedia_vid_codec_mgr *mgr, 
				const pj_str_t *codec_id,
				pj_uint8_t prio)
{
    unsigned i, found = 0;

    PJ_ASSERT_RETURN(codec_id, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    pj_mutex_lock(mgr->mutex);

    /* Update the priorities of affected codecs */
    for (i=0; i<mgr->codec_cnt; ++i) 
    {
	if (codec_id->slen == 0 ||
	    pj_strnicmp2(codec_id, mgr->codec_desc[i].id, 
			 codec_id->slen) == 0) 
	{
	    mgr->codec_desc[i].prio = (pjmedia_codec_priority) prio;
	    ++found;
	}
    }

    if (!found) {
	pj_mutex_unlock(mgr->mutex);
	return PJ_ENOTFOUND;
    }

    /* Re-sort codecs */
    sort_codecs(mgr);

    pj_mutex_unlock(mgr->mutex);

    return PJ_SUCCESS;
}


/*
 * Allocate one codec.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_alloc_codec(
                                        pjmedia_vid_codec_mgr *mgr, 
					const pjmedia_vid_codec_info *info,
					pjmedia_vid_codec **p_codec)
{
    pjmedia_vid_codec_factory *factory;
    pj_status_t status;

    PJ_ASSERT_RETURN(info && p_codec, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    *p_codec = NULL;

    pj_mutex_lock(mgr->mutex);

    factory = mgr->factory_list.next;
    while (factory != &mgr->factory_list) {

	if ( (*factory->op->test_alloc)(factory, info) == PJ_SUCCESS ) {

	    status = (*factory->op->alloc_codec)(factory, info, p_codec);
	    if (status == PJ_SUCCESS) {
		pj_mutex_unlock(mgr->mutex);
		return PJ_SUCCESS;
	    }

	}

	factory = factory->next;
    }

    pj_mutex_unlock(mgr->mutex);

    return PJMEDIA_CODEC_EUNSUP;
}


/*
 * Get default codec parameter.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_get_default_param(
                                        pjmedia_vid_codec_mgr *mgr,
					const pjmedia_vid_codec_info *info,
					pjmedia_vid_codec_param *param )
{
    pjmedia_vid_codec_factory *factory;
    pj_status_t status;
    pjmedia_codec_id codec_id;
    pjmedia_vid_codec_desc *codec_desc = NULL;
    unsigned i;

    PJ_ASSERT_RETURN(info && param, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    if (!pjmedia_vid_codec_info_to_id(info, (char*)&codec_id, 
                                      sizeof(codec_id)))
	return PJ_EINVAL;

    pj_mutex_lock(mgr->mutex);

    /* First, lookup default param in codec desc */
    for (i=0; i < mgr->codec_cnt; ++i) {
	if (pj_ansi_stricmp(codec_id, mgr->codec_desc[i].id) == 0) {
	    codec_desc = &mgr->codec_desc[i];
	    break;
	}
    }

    /* If we found the codec and its default param is set, return it */
    if (codec_desc && codec_desc->def_param) {
	pj_memcpy(param, codec_desc->def_param->param, 
		  sizeof(pjmedia_vid_codec_param));

	pj_mutex_unlock(mgr->mutex);
	return PJ_SUCCESS;
    }

    /* Otherwise query the default param from codec factory */
    factory = mgr->factory_list.next;
    while (factory != &mgr->factory_list) {

	if ( (*factory->op->test_alloc)(factory, info) == PJ_SUCCESS ) {

	    status = (*factory->op->default_attr)(factory, info, param);
	    if (status == PJ_SUCCESS) {
		/* Check for invalid max_bps. */
		//if (param->info.max_bps < param->info.avg_bps)
		//    param->info.max_bps = param->info.avg_bps;

		pj_mutex_unlock(mgr->mutex);
		return PJ_SUCCESS;
	    }

	}

	factory = factory->next;
    }

    pj_mutex_unlock(mgr->mutex);


    return PJMEDIA_CODEC_EUNSUP;
}


/*
 * Set default codec parameter.
 */
PJ_DEF(pj_status_t) pjmedia_vid_codec_mgr_set_default_param( 
					    pjmedia_vid_codec_mgr *mgr,
					    const pjmedia_vid_codec_info *info,
					    const pjmedia_vid_codec_param *param )
{
    unsigned i;
    pjmedia_codec_id codec_id;
    pjmedia_vid_codec_desc *codec_desc = NULL;
    pj_pool_t *pool, *old_pool = NULL;
    pjmedia_vid_codec_default_param *p;

    PJ_ASSERT_RETURN(info, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    if (!pjmedia_vid_codec_info_to_id(info, (char*)&codec_id, sizeof(codec_id)))
	return PJ_EINVAL;

    pj_mutex_lock(mgr->mutex);

    /* Lookup codec desc */
    for (i=0; i < mgr->codec_cnt; ++i) {
	if (pj_ansi_stricmp(codec_id, mgr->codec_desc[i].id) == 0) {
	    codec_desc = &mgr->codec_desc[i];
	    break;
	}
    }

    /* Codec not found */
    if (!codec_desc) {
	pj_mutex_unlock(mgr->mutex);
	return PJMEDIA_CODEC_EUNSUP;
    }

    /* If codec param is previously set */
    if (codec_desc->def_param) {
	pj_assert(codec_desc->def_param->pool);
        old_pool = codec_desc->def_param->pool;
	codec_desc->def_param = NULL;
    }

    /* When param is set to NULL, i.e: setting default codec param to library
     * default setting, just return PJ_SUCCESS.
     */
    if (NULL == param) {
	pj_mutex_unlock(mgr->mutex);
        if (old_pool)
	    pj_pool_release(old_pool);
	return PJ_SUCCESS;
    }

    /* Create new default codec param instance */
    pool = pj_pool_create(mgr->pf, (char*)codec_id, 256, 256, NULL);
    codec_desc->def_param = PJ_POOL_ZALLOC_T(pool,
					     pjmedia_vid_codec_default_param);
    p = codec_desc->def_param;
    p->pool = pool;

    /* Update codec default param */
    p->param = pjmedia_vid_codec_param_clone(pool, param);
    if (!p->param)
	return PJ_EINVAL;

    codec_desc->def_param = p;

    pj_mutex_unlock(mgr->mutex);

    /* Release old pool at the very end, as application tends to apply changes
     * to the existing/old codec param fetched using
     * pjmedia_vid_codec_mgr_get_default_param() which doesn't do deep clone.
     */
    if (old_pool)
	pj_pool_release(old_pool);

    return PJ_SUCCESS;
}


/*
 * Dealloc codec.
 */
PJ_DEF(pj_status_t)
pjmedia_vid_codec_mgr_dealloc_codec(pjmedia_vid_codec_mgr *mgr,
				    pjmedia_vid_codec *codec)
{
    PJ_ASSERT_RETURN(codec, PJ_EINVAL);

    if (!mgr) mgr = def_vid_codec_mgr;
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    return (*codec->factory->op->dealloc_codec)(codec->factory, codec);
}


#endif /* PJMEDIA_HAS_VIDEO */
