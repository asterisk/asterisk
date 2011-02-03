/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 * \brief Format API
 *
 * \author David Vossel <dvossel@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/_private.h"
#include "asterisk/version.h"
#include "asterisk/format.h"
#include "asterisk/astobj2.h"
#include "asterisk/lock.h"

/*! This is the container for all the format attribute interfaces.
 *  An ao2 container was chosen for fast lookup. */
static struct ao2_container *interfaces;

/*! This is the lock used to protect the interfaces container.  Yes, ao2_containers
 * do have their own locking, but we need the capability of performing read/write
 * locks on this specific container. */
static ast_rwlock_t ilock;

/*! a wrapper is used put interfaces into the ao2 container. */
struct interface_ao2_wrapper {
	enum ast_format_id id;
	const struct ast_format_attr_interface *interface;
	/*! a read write lock must be used to protect the wrapper instead
	 * of the ao2 lock. */
	ast_rwlock_t wraplock;
};

static int interface_cmp_cb(void *obj, void *arg, int flags)
{
	struct interface_ao2_wrapper *wrapper1 = obj;
	struct interface_ao2_wrapper *wrapper2 = arg;

	return (wrapper2->id == wrapper1->id) ? CMP_MATCH | CMP_STOP : 0;
}

static int interface_hash_cb(const void *obj, const int flags)
{
	const struct interface_ao2_wrapper *wrapper = obj;
	return wrapper->id;
}

static void interface_destroy_cb(void *obj)
{
	struct interface_ao2_wrapper *wrapper = obj;
	ast_rwlock_destroy(&wrapper->wraplock);
}

void ast_format_copy(struct ast_format *dst, const struct ast_format *src)
{
	memcpy(dst, src, sizeof(struct ast_format));
}

void ast_format_set_video_mark(struct ast_format *format)
{
	format->fattr.rtp_marker_bit = 1;
}

int ast_format_get_video_mark(const struct ast_format *format)
{
	return format->fattr.rtp_marker_bit;
}

static struct interface_ao2_wrapper *find_interface(const struct ast_format *format)
{
	struct interface_ao2_wrapper *wrapper;
	struct interface_ao2_wrapper tmp_wrapper = {
		.id = format->id,
	};

	ast_rwlock_rdlock(&ilock);
	if (!(wrapper = ao2_find(interfaces, &tmp_wrapper, (OBJ_POINTER | OBJ_NOLOCK)))) {
		ast_rwlock_unlock(&ilock);
		return NULL;
	}
	ast_rwlock_unlock(&ilock);

	return wrapper;
}

/*! \internal
 * \brief set format attributes using an interface
 */
static int format_set_helper(struct ast_format *format, va_list ap)
{
	struct interface_ao2_wrapper *wrapper;

	if (!(wrapper = find_interface(format))) {
		ast_log(LOG_WARNING, "Could not find format interface to set.\n");
		return -1;
	}

	ast_rwlock_rdlock(&wrapper->wraplock);
	if (!wrapper->interface || !wrapper->interface->format_attr_set) {
		ast_rwlock_unlock(&wrapper->wraplock);
		ao2_ref(wrapper, -1);
		return -1;
	}

	wrapper->interface->format_attr_set(&format->fattr, ap);

	ast_rwlock_unlock(&wrapper->wraplock);
	ao2_ref(wrapper, -1);

	return 0;
}

struct ast_format *ast_format_append(struct ast_format *format, ... )
{
	va_list ap;
	va_start(ap, format);
	format_set_helper(format, ap);
	va_end(ap);

	return format;
}

struct ast_format *ast_format_set(struct ast_format *format, enum ast_format_id id, int set_attributes, ... )
{
	/* initialize the structure before setting it. */
	ast_format_clear(format);

	format->id = id;

	if (set_attributes) {
		va_list ap;
		va_start(ap, set_attributes);
		format_set_helper(format, ap);
		va_end(ap);
	}

	return format;
}

void ast_format_clear(struct ast_format *format)
{
	format->id = 0;
	memset(&format->fattr, 0, sizeof(format->fattr));
}

/*! \internal
 * \brief determine if a list of attribute key value pairs are set on a format
 */
static int format_isset_helper(struct ast_format *format, va_list ap)
{
	int res;
	struct interface_ao2_wrapper *wrapper;
	struct ast_format tmp = {
		.id = format->id,
		.fattr = { { 0, }, },
	};

	if (!(wrapper = find_interface(format))) {
		return -1;
	}

	ast_rwlock_rdlock(&wrapper->wraplock);
	if (!wrapper->interface ||
		!wrapper->interface->format_attr_set ||
		!wrapper->interface->format_attr_cmp) {

		ast_rwlock_unlock(&wrapper->wraplock);
		ao2_ref(wrapper, -1);
		return -1;
	}

	wrapper->interface->format_attr_set(&tmp.fattr, ap);

	/* use our tmp structure to tell if the attributes are set or not */
	res = wrapper->interface->format_attr_cmp(&tmp.fattr, &format->fattr);

	ast_rwlock_unlock(&wrapper->wraplock);
	ao2_ref(wrapper, -1);

	return (res == AST_FORMAT_CMP_NOT_EQUAL) ? -1 : 0;
}

int ast_format_isset(struct ast_format *format, ... )
{
	va_list ap;
	int res;

	va_start(ap, format);
	res = format_isset_helper(format, ap);
	va_end(ap);
	return res;
}


/*! \internal
 * \brief cmp format attributes using an interface
 */
static enum ast_format_cmp_res format_cmp_helper(const struct ast_format *format1, const struct ast_format *format2)
{
	enum ast_format_cmp_res res = AST_FORMAT_CMP_EQUAL;
	struct interface_ao2_wrapper *wrapper;

	if (!(wrapper = find_interface(format1))) {
		return res;
	}

	ast_rwlock_rdlock(&wrapper->wraplock);
	if (!wrapper->interface || !wrapper->interface->format_attr_cmp) {
		ast_rwlock_unlock(&wrapper->wraplock);
		ao2_ref(wrapper, -1);
		return res;
	}

	res = wrapper->interface->format_attr_cmp(&format1->fattr, &format2->fattr);

	ast_rwlock_unlock(&wrapper->wraplock);
	ao2_ref(wrapper, -1);

	return res;
}

enum ast_format_cmp_res ast_format_cmp(const struct ast_format *format1, const struct ast_format *format2)
{
	if (format1->id != format2->id) {
		return AST_FORMAT_CMP_NOT_EQUAL;
	}

	return format_cmp_helper(format1, format2);
}

/*! \internal
 * \brief get joint format attributes using an interface
 */
static int format_joint_helper(const struct ast_format *format1, const struct ast_format *format2, struct ast_format *result)
{
	int res = 0;
	struct interface_ao2_wrapper *wrapper;

	if (!(wrapper = find_interface(format1))) {
		/* if no interface is present, we assume formats are joint by id alone */
		return res;
	}

	ast_rwlock_rdlock(&wrapper->wraplock);
	if (wrapper->interface && wrapper->interface->format_attr_get_joint) {
		res = wrapper->interface->format_attr_get_joint(&format1->fattr, &format2->fattr, &result->fattr);
	}
	ast_rwlock_unlock(&wrapper->wraplock);

	ao2_ref(wrapper, -1);

	return res;
}

int ast_format_joint(const struct ast_format *format1, const struct ast_format *format2, struct ast_format *result)
{
	if (format1->id != format2->id) {
		return -1;
	}
	result->id = format1->id;
	return format_joint_helper(format1, format2, result);
}


uint64_t ast_format_id_to_old_bitfield(enum ast_format_id id)
{
	switch (id) {
	/*! G.723.1 compression */
	case AST_FORMAT_G723_1:
		return (1ULL << 0);
	/*! GSM compression */
	case AST_FORMAT_GSM:
		return (1ULL << 1);
	/*! Raw mu-law data (G.711) */
	case AST_FORMAT_ULAW:
		return (1ULL << 2);
	/*! Raw A-law data (G.711) */
	case AST_FORMAT_ALAW:
		return (1ULL << 3);
	/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
	case AST_FORMAT_G726_AAL2:
		return (1ULL << 4);
	/*! ADPCM (IMA) */
	case AST_FORMAT_ADPCM:
		return (1ULL << 5);
	/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
	case AST_FORMAT_SLINEAR:
		return (1ULL << 6);
	/*! LPC10, 180 samples/frame */
	case AST_FORMAT_LPC10:
		return (1ULL << 7);
	/*! G.729A audio */
	case AST_FORMAT_G729A:
		return (1ULL << 8);
	/*! SpeeX Free Compression */
	case AST_FORMAT_SPEEX:
		return (1ULL << 9);
	/*! iLBC Free Compression */
	case AST_FORMAT_ILBC:
		return (1ULL << 10);
	/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
	case AST_FORMAT_G726:
		return (1ULL << 11);
	/*! G.722 */
	case AST_FORMAT_G722:
		return (1ULL << 12);
	/*! G.722.1 (also known as Siren7, 32kbps assumed) */
	case AST_FORMAT_SIREN7:
		return (1ULL << 13);
	/*! G.722.1 Annex C (also known as Siren14, 48kbps assumed) */
	case AST_FORMAT_SIREN14:
		return (1ULL << 14);
	/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
	case AST_FORMAT_SLINEAR16:
		return (1ULL << 15);
	/*! G.719 (64 kbps assumed) */
	case AST_FORMAT_G719:
		return (1ULL << 32);
	/*! SpeeX Wideband (16kHz) Free Compression */
	case AST_FORMAT_SPEEX16:
		return (1ULL << 33);
	/*! Raw mu-law data (G.711) */
	case AST_FORMAT_TESTLAW:
		return (1ULL << 47);

	/*! H.261 Video */
	case AST_FORMAT_H261:
		return (1ULL << 18);
	/*! H.263 Video */
	case AST_FORMAT_H263:
		return (1ULL << 19);
	/*! H.263+ Video */
	case AST_FORMAT_H263_PLUS:
		return (1ULL << 20);
	/*! H.264 Video */
	case AST_FORMAT_H264:
		return (1ULL << 21);
	/*! MPEG4 Video */
	case AST_FORMAT_MP4_VIDEO:
		return (1ULL << 22);

	/*! JPEG Images */
	case AST_FORMAT_JPEG:
		return (1ULL << 16);
	/*! PNG Images */
	case AST_FORMAT_PNG:
		return (1ULL << 17);

	/*! T.140 RED Text format RFC 4103 */
	case AST_FORMAT_T140RED:
		return (1ULL << 26);
	/*! T.140 Text format - ITU T.140, RFC 4103 */
	case AST_FORMAT_T140:
		return (1ULL << 27);
	}

	return 0;

}
uint64_t ast_format_to_old_bitfield(const struct ast_format *format)
{
	return ast_format_id_to_old_bitfield(format->id);
}

struct ast_format *ast_format_from_old_bitfield(struct ast_format *dst, uint64_t src)
{
	switch (src) {
	/*! G.723.1 compression */
	case (1ULL << 0):
		return ast_format_set(dst, AST_FORMAT_G723_1, 0);
	/*! GSM compression */
	case (1ULL << 1):
		return ast_format_set(dst, AST_FORMAT_GSM, 0);
	/*! Raw mu-law data (G.711) */
	case (1ULL << 2):
		return ast_format_set(dst, AST_FORMAT_ULAW, 0);
	/*! Raw A-law data (G.711) */
	case (1ULL << 3):
		return ast_format_set(dst, AST_FORMAT_ALAW, 0);
	/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
	case (1ULL << 4):
		return ast_format_set(dst, AST_FORMAT_G726_AAL2, 0);
	/*! ADPCM (IMA) */
	case (1ULL << 5):
		return ast_format_set(dst, AST_FORMAT_ADPCM, 0);
	/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
	case (1ULL << 6):
		return ast_format_set(dst, AST_FORMAT_SLINEAR, 0);
	/*! LPC10, 180 samples/frame */
	case (1ULL << 7):
		return ast_format_set(dst, AST_FORMAT_LPC10, 0);
	/*! G.729A audio */
	case (1ULL << 8):
		return ast_format_set(dst, AST_FORMAT_G729A, 0);
	/*! SpeeX Free Compression */
	case (1ULL << 9):
		return ast_format_set(dst, AST_FORMAT_SPEEX, 0);
	/*! iLBC Free Compression */
	case (1ULL << 10):
		return ast_format_set(dst, AST_FORMAT_ILBC, 0);
	/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
	case (1ULL << 11):
		return ast_format_set(dst, AST_FORMAT_G726, 0);
	/*! G.722 */
	case (1ULL << 12):
		return ast_format_set(dst, AST_FORMAT_G722, 0);
	/*! G.722.1 (also known as Siren7, 32kbps assumed) */
	case (1ULL << 13):
		return ast_format_set(dst, AST_FORMAT_SIREN7, 0);
	/*! G.722.1 Annex C (also known as Siren14, 48kbps assumed) */
	case (1ULL << 14):
		return ast_format_set(dst, AST_FORMAT_SIREN14, 0);
	/*! Raw 16-bit Signed Linear (16000 Hz) PCM */
	case (1ULL << 15):
		return ast_format_set(dst, AST_FORMAT_SLINEAR16, 0);
	/*! G.719 (64 kbps assumed) */
	case (1ULL << 32):
		return ast_format_set(dst, AST_FORMAT_G719, 0);
	/*! SpeeX Wideband (16kHz) Free Compression */
	case (1ULL << 33):
		return ast_format_set(dst, AST_FORMAT_SPEEX16, 0);
	/*! Raw mu-law data (G.711) */
	case (1ULL << 47):
		return ast_format_set(dst, AST_FORMAT_TESTLAW, 0);

	/*! H.261 Video */
	case (1ULL << 18):
		return ast_format_set(dst, AST_FORMAT_H261, 0);
	/*! H.263 Video */
	case (1ULL << 19):
		return ast_format_set(dst, AST_FORMAT_H263, 0);
	/*! H.263+ Video */
	case (1ULL << 20):
		return ast_format_set(dst, AST_FORMAT_H263_PLUS, 0);
	/*! H.264 Video */
	case (1ULL << 21):
		return ast_format_set(dst, AST_FORMAT_H264, 0);
	/*! MPEG4 Video */
	case (1ULL << 22):
		return ast_format_set(dst, AST_FORMAT_MP4_VIDEO, 0);

	/*! JPEG Images */
	case (1ULL << 16):
		return ast_format_set(dst, AST_FORMAT_JPEG, 0);
	/*! PNG Images */
	case (1ULL << 17):
		return ast_format_set(dst, AST_FORMAT_PNG, 0);

	/*! T.140 RED Text format RFC 4103 */
	case (1ULL << 26):
		return ast_format_set(dst, AST_FORMAT_T140RED, 0);
	/*! T.140 Text format - ITU T.140, RFC 4103 */
	case (1ULL << 27):
		return ast_format_set(dst, AST_FORMAT_T140, 0);
	}
	ast_format_clear(dst);
	return NULL;
}

enum ast_format_id ast_format_id_from_old_bitfield(uint64_t src)
{
	struct ast_format dst;
	if (ast_format_from_old_bitfield(&dst, src)) {
		return dst.id;
	}
	return 0;
}

int ast_format_attr_init()
{
	if (ast_rwlock_init(&ilock)) {
		return -1;
	}
	if (!(interfaces = ao2_container_alloc(283, interface_hash_cb, interface_cmp_cb))) {
		ast_rwlock_destroy(&ilock);
		return -1;
	}
	return 0;
}

int ast_format_attr_reg_interface(const struct ast_format_attr_interface *interface)
{
	struct interface_ao2_wrapper *wrapper;
	struct interface_ao2_wrapper tmp_wrapper = {
		.id = interface->id,
	};

	/* check for duplicates first*/
	ast_rwlock_wrlock(&ilock);
	if ((wrapper = ao2_find(interfaces, &tmp_wrapper, (OBJ_POINTER | OBJ_NOLOCK)))) {
		ast_rwlock_unlock(&ilock);
		ast_log(LOG_WARNING, "Can not register attribute interface for format id %d, interface already exists.\n", interface->id);
		ao2_ref(wrapper, -1);
		return -1;
	}
	ast_rwlock_unlock(&ilock);

	if (!(wrapper = ao2_alloc(sizeof(*wrapper), interface_destroy_cb))) {
		return -1;
	}

	wrapper->interface = interface;
	wrapper->id = interface->id;
	ast_rwlock_init(&wrapper->wraplock);

	/* use the write lock whenever the interface container is modified */
	ast_rwlock_wrlock(&ilock);
	ao2_link(interfaces, wrapper);
	ast_rwlock_unlock(&ilock);

	ao2_ref(wrapper, -1);

	return 0;
}

int ast_format_attr_unreg_interface(const struct ast_format_attr_interface *interface)
{
	struct interface_ao2_wrapper *wrapper;
	struct interface_ao2_wrapper tmp_wrapper = {
		.id = interface->id,
	};

	/* use the write lock whenever the interface container is modified */
	ast_rwlock_wrlock(&ilock);
	if (!(wrapper = ao2_find(interfaces, &tmp_wrapper, (OBJ_POINTER | OBJ_UNLINK | OBJ_NOLOCK)))) {
		ast_rwlock_unlock(&ilock);
		return -1;
	}
	ast_rwlock_unlock(&ilock);

	ast_rwlock_wrlock(&wrapper->wraplock);
	wrapper->interface = NULL;
	ast_rwlock_unlock(&wrapper->wraplock);

	ao2_ref(wrapper, -1);

	return 0;
}
