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
#include <pjmedia/converter.h>
#include <pj/assert.h>
#include <pj/errno.h>

#define THIS_FILE	"converter.c"

struct pjmedia_converter_mgr
{
    pjmedia_converter_factory  factory_list;
};

static pjmedia_converter_mgr *converter_manager_instance;

#if PJMEDIA_HAS_LIBSWSCALE && PJMEDIA_HAS_LIBAVUTIL
PJ_DECL(pj_status_t)
pjmedia_libswscale_converter_init(pjmedia_converter_mgr *mgr);
#endif


PJ_DEF(pj_status_t) pjmedia_converter_mgr_create(pj_pool_t *pool,
					         pjmedia_converter_mgr **p_mgr)
{
    pjmedia_converter_mgr *mgr;
    pj_status_t status = PJ_SUCCESS;

    mgr = PJ_POOL_ALLOC_T(pool, pjmedia_converter_mgr);
    pj_list_init(&mgr->factory_list);

    if (!converter_manager_instance)
	converter_manager_instance = mgr;

#if PJMEDIA_HAS_LIBSWSCALE && PJMEDIA_HAS_LIBAVUTIL
    status = pjmedia_libswscale_converter_init(mgr);
    if (status != PJ_SUCCESS) {
	PJ_PERROR(4,(THIS_FILE, status,
		     "Error initializing libswscale converter"));
    }
#endif

    if (p_mgr)
	*p_mgr = mgr;

    return PJ_SUCCESS;
}

PJ_DEF(pjmedia_converter_mgr*) pjmedia_converter_mgr_instance(void)
{
    pj_assert(converter_manager_instance != NULL);
    return converter_manager_instance;
}

PJ_DEF(void) pjmedia_converter_mgr_set_instance(pjmedia_converter_mgr *mgr)
{
    converter_manager_instance = mgr;
}

PJ_DEF(void) pjmedia_converter_mgr_destroy(pjmedia_converter_mgr *mgr)
{
    pjmedia_converter_factory *f;

    if (!mgr) mgr = pjmedia_converter_mgr_instance();

    PJ_ASSERT_ON_FAIL(mgr != NULL, return);

    f = mgr->factory_list.next;
    while (f != &mgr->factory_list) {
	pjmedia_converter_factory *next = f->next;
	pj_list_erase(f);
	(*f->op->destroy_factory)(f);
	f = next;
    }

    if (converter_manager_instance == mgr)
	converter_manager_instance = NULL;
}

PJ_DEF(pj_status_t)
pjmedia_converter_mgr_register_factory(pjmedia_converter_mgr *mgr,
				       pjmedia_converter_factory *factory)
{
    pjmedia_converter_factory *pf;

    if (!mgr) mgr = pjmedia_converter_mgr_instance();

    PJ_ASSERT_RETURN(mgr != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(!pj_list_find_node(&mgr->factory_list, factory),
		     PJ_EEXISTS);

    pf = mgr->factory_list.next;
    while (pf != &mgr->factory_list) {
	if (pf->priority > factory->priority)
	    break;
	pf = pf->next;
    }
    pj_list_insert_before(pf, factory);
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t)
pjmedia_converter_mgr_unregister_factory(pjmedia_converter_mgr *mgr,
				         pjmedia_converter_factory *f,
				         pj_bool_t destroy)
{
    if (!mgr) mgr = pjmedia_converter_mgr_instance();

    PJ_ASSERT_RETURN(mgr != NULL, PJ_EINVAL);

    PJ_ASSERT_RETURN(pj_list_find_node(&mgr->factory_list, f), PJ_ENOTFOUND);
    pj_list_erase(f);
    if (destroy)
	(*f->op->destroy_factory)(f);
    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_converter_create(pjmedia_converter_mgr *mgr,
					      pj_pool_t *pool,
					      pjmedia_conversion_param *param,
					      pjmedia_converter **p_cv)
{
    pjmedia_converter_factory *f;
    pjmedia_converter *cv = NULL;
    pj_status_t status = PJ_ENOTFOUND;

    if (!mgr) mgr = pjmedia_converter_mgr_instance();

    PJ_ASSERT_RETURN(mgr != NULL, PJ_EINVAL);

    *p_cv = NULL;

    f = mgr->factory_list.next;
    while (f != &mgr->factory_list) {
	status = (*f->op->create_converter)(f, pool, param, &cv);
	if (status == PJ_SUCCESS)
	    break;
	f = f->next;
    }

    if (status != PJ_SUCCESS)
	return status;

    *p_cv = cv;

    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_converter_convert(pjmedia_converter *cv,
					       pjmedia_frame *src_frame,
					       pjmedia_frame *dst_frame)
{
    return (*cv->op->convert)(cv, src_frame, dst_frame);
}

PJ_DEF(void) pjmedia_converter_destroy(pjmedia_converter *cv)
{
    (*cv->op->destroy)(cv);
}


