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
#include <pjmedia-videodev/errno.h>
#include <pj/string.h>
#include <pj/unicode.h>

/* PJMEDIA-videodev's own error codes/messages
 * MUST KEEP THIS ARRAY SORTED!!
 * Message must be limited to 64 chars!
 */


#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)


#if defined(PJ_HAS_ERROR_STRING) && (PJ_HAS_ERROR_STRING != 0)

static const struct
{
    int code;
    const char *msg;
} err_str[] =
{
    PJ_BUILD_ERR( PJMEDIA_EVID_ERR,	    "Unspecified video device error" ),
    PJ_BUILD_ERR( PJMEDIA_EVID_SYSERR,	    "Unknown error from video driver" ),
    PJ_BUILD_ERR( PJMEDIA_EVID_INIT,	    "video subsystem not initialized" ),
    PJ_BUILD_ERR( PJMEDIA_EVID_INVDEV,	    "Invalid video device" ),
    PJ_BUILD_ERR( PJMEDIA_EVID_NODEV,	    "Found no video devices" ),
    PJ_BUILD_ERR( PJMEDIA_EVID_NODEFDEV,    "Unable to find default video device" ),
    PJ_BUILD_ERR( PJMEDIA_EVID_NOTREADY,    "video device not ready" ),
    PJ_BUILD_ERR( PJMEDIA_EVID_INVCAP,	    "Invalid or unsupported video capability" ),
    PJ_BUILD_ERR( PJMEDIA_EVID_INVOP,	    "Invalid or unsupported video device operation" ),
    PJ_BUILD_ERR( PJMEDIA_EVID_BADFORMAT,   "Bad or invalid video device format" ),
    PJ_BUILD_ERR( PJMEDIA_EVID_SAMPFORMAT,  "Invalid video device sample format"),
    PJ_BUILD_ERR( PJMEDIA_EVID_BADLATENCY,  "Bad video latency setting")

};

#endif	/* PJ_HAS_ERROR_STRING */



/*
 * pjmedia_videodev_strerror()
 */
PJ_DEF(pj_str_t) pjmedia_videodev_strerror(pj_status_t statcode,
					   char *buf, pj_size_t bufsize )
{
    pj_str_t errstr;

#if defined(PJ_HAS_ERROR_STRING) && (PJ_HAS_ERROR_STRING != 0)

    /* videodev error */
    if (statcode >= PJMEDIA_VIDEODEV_ERRNO_START &&
	statcode < PJMEDIA_VIDEODEV_ERRNO_END)
    {
	/* Find the error in the table.
	 * Use binary search!
	 */
	int first = 0;
	int n = PJ_ARRAY_SIZE(err_str);

	while (n > 0) {
	    int half = n/2;
	    int mid = first + half;

	    if (err_str[mid].code < statcode) {
		first = mid+1;
		n -= (half+1);
	    } else if (err_str[mid].code > statcode) {
		n = half;
	    } else {
		first = mid;
		break;
	    }
	}


	if (PJ_ARRAY_SIZE(err_str) && err_str[first].code == statcode) {
	    pj_str_t msg;

	    msg.ptr = (char*)err_str[first].msg;
	    msg.slen = pj_ansi_strlen(err_str[first].msg);

	    errstr.ptr = buf;
	    pj_strncpy_with_null(&errstr, &msg, bufsize);
	    return errstr;

	}
    }
#endif	/* PJ_HAS_ERROR_STRING */

    /* Error not found. */
    errstr.ptr = buf;
    errstr.slen = pj_ansi_snprintf(buf, bufsize,
				   "Unknown pjmedia-videodev error %d",
				   statcode);

    return errstr;
}


#endif /* PJMEDIA_HAS_VIDEO */
