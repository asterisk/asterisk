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
#include <pjmedia/types.h>
#include <pj/assert.h>

/**
 * Utility function to return the string name for a pjmedia_type.
 *
 * @param t		The media type.
 *
 * @return		String.
 */
PJ_DEF(const char*) pjmedia_type_name(pjmedia_type t)
{
    const char *type_names[] = {
	"none",
	"audio",
	"video",
	"application",
	"unknown"
    };

    pj_assert(t < PJ_ARRAY_SIZE(type_names));
    pj_assert(PJMEDIA_TYPE_UNKNOWN == 4);

    if (t < PJ_ARRAY_SIZE(type_names))
	return type_names[t];
    else
	return "??";
}
