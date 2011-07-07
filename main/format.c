/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
 * Mark Spencer <markster@digium.com>
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
 * \author Mark Spencer <markster@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/_private.h"
#include "asterisk/version.h"
#include "asterisk/format.h"
#include "asterisk/astobj2.h"
#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/config.h"

#define FORMAT_CONFIG "codecs.conf"

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

/*! \brief Format List container, This container is never directly accessed outside
 * of this file, and It only exists for building the format_list_array. */
static struct ao2_container *format_list;
/*! \brief Format List array is a read only array protected by a read write lock.
 * This array may be used outside this file with the use of reference counting to
 * guarantee safety for access by multiple threads. */
static struct ast_format_list *format_list_array;
static size_t format_list_array_len = 0;
/*! \brief Locks the format list array so a reference can be taken safely. */
static ast_rwlock_t format_list_array_lock;

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

static int has_interface(const struct ast_format *format)
{
	struct interface_ao2_wrapper *wrapper;
	struct interface_ao2_wrapper tmp_wrapper = {
		.id = format->id,
	};

	ast_rwlock_rdlock(&ilock);
	if (!(wrapper = ao2_find(interfaces, &tmp_wrapper, (OBJ_POINTER | OBJ_NOLOCK)))) {
		ast_rwlock_unlock(&ilock);
		return 0;
	}
	ast_rwlock_unlock(&ilock);
	ao2_ref(wrapper, -1);
	return 1;
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
static int format_isset_helper(const struct ast_format *format, va_list ap)
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

	/* if isset is present, use that function, else just build a new
	 * format and use the cmp function */
	if (wrapper->interface->format_attr_isset) {
		res = wrapper->interface->format_attr_isset(&format->fattr, ap);
	} else {
		wrapper->interface->format_attr_set(&tmp.fattr, ap);
		/* use our tmp structure to tell if the attributes are set or not */
		res = wrapper->interface->format_attr_cmp(&tmp.fattr, &format->fattr);
		res = (res == AST_FORMAT_CMP_NOT_EQUAL) ? -1 : 0;
	}

	ast_rwlock_unlock(&wrapper->wraplock);
	ao2_ref(wrapper, -1);

	return res;
}

int ast_format_isset(const struct ast_format *format, ... )
{
	va_list ap;
	int res;

	va_start(ap, format);
	res = format_isset_helper(format, ap);
	va_end(ap);
	return res;
}

int ast_format_get_value(const struct ast_format *format, int key, void *value)
{
	int res = 0;
	struct interface_ao2_wrapper *wrapper;
	if (!(wrapper = find_interface(format))) {
		return -1;
	}
	ast_rwlock_rdlock(&wrapper->wraplock);
	if (!wrapper->interface ||
		!wrapper->interface->format_attr_get_val) {

		ast_rwlock_unlock(&wrapper->wraplock);
		ao2_ref(wrapper, -1);
		return -1;
	}

	res = wrapper->interface->format_attr_get_val(&format->fattr, key, value);

	ast_rwlock_unlock(&wrapper->wraplock);
	ao2_ref(wrapper, -1);

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
	default:
		return 0; /* not supported by old bitfield. */
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

int ast_format_is_slinear(const struct ast_format *format)
{
	if (format->id == AST_FORMAT_SLINEAR ||
		format->id == AST_FORMAT_SLINEAR12 ||
		format->id == AST_FORMAT_SLINEAR16 ||
		format->id == AST_FORMAT_SLINEAR24 ||
		format->id == AST_FORMAT_SLINEAR32 ||
		format->id == AST_FORMAT_SLINEAR44 ||
		format->id == AST_FORMAT_SLINEAR48 ||
		format->id == AST_FORMAT_SLINEAR96 ||
		format->id == AST_FORMAT_SLINEAR192) {
		return 1;
	}
	return 0;
}

enum ast_format_id ast_format_slin_by_rate(unsigned int rate)
{
	if (rate >= 192000) {
		return AST_FORMAT_SLINEAR192;
	} else if (rate >= 96000) {
		return AST_FORMAT_SLINEAR96;
	} else if (rate >= 48000) {
		return AST_FORMAT_SLINEAR48;
	} else if (rate >= 44100) {
		return AST_FORMAT_SLINEAR44;
	} else if (rate >= 32000) {
		return AST_FORMAT_SLINEAR32;
	} else if (rate >= 24000) {
		return AST_FORMAT_SLINEAR24;
	} else if (rate >= 16000) {
		return AST_FORMAT_SLINEAR16;
	} else if (rate >= 12000) {
		return AST_FORMAT_SLINEAR12;
	}
	return AST_FORMAT_SLINEAR;
}

const char* ast_getformatname(const struct ast_format *format)
{
	int x;
	const char *ret = "unknown";
	size_t f_len;
	const struct ast_format_list *f_list = ast_format_list_get(&f_len);
	for (x = 0; x < f_len; x++) {
		if (ast_format_cmp(&f_list[x].format, format) == AST_FORMAT_CMP_EQUAL) {
			ret = f_list[x].name;
			break;
		}
	}
	f_list = ast_format_list_destroy(f_list);
	return ret;
}


char *ast_getformatname_multiple_byid(char *buf, size_t size, enum ast_format_id id)
{
	int x;
	unsigned len;
	char *start, *end = buf;
	size_t f_len;
	const struct ast_format_list *f_list = ast_format_list_get(&f_len);

	if (!size) {
		f_list = ast_format_list_destroy(f_list);
		return buf;
	}
	snprintf(end, size, "(");
	len = strlen(end);
	end += len;
	size -= len;
	start = end;
	for (x = 0; x < f_len; x++) {
		if (f_list[x].format.id == id) {
			snprintf(end, size, "%s|", f_list[x].name);
			len = strlen(end);
			end += len;
			size -= len;
		}
	}
	if (start == end) {
		ast_copy_string(start, "nothing)", size);
	} else if (size > 1) {
		*(end - 1) = ')';
	}
	f_list = ast_format_list_destroy(f_list);
	return buf;
}

static struct ast_codec_alias_table {
	const char *alias;
	const char *realname;
} ast_codec_alias_table[] = {
	{ "slinear", "slin"},
	{ "slinear16", "slin16"},
	{ "g723.1", "g723"},
	{ "g722.1", "siren7"},
	{ "g722.1c", "siren14"},
};

static const char *ast_expand_codec_alias(const char *in)
{
	int x;

	for (x = 0; x < ARRAY_LEN(ast_codec_alias_table); x++) {
		if (!strcmp(in,ast_codec_alias_table[x].alias))
			return ast_codec_alias_table[x].realname;
	}
	return in;
}

struct ast_format *ast_getformatbyname(const char *name, struct ast_format *result)
{
	int x;
	size_t f_len;
	const struct ast_format_list *f_list = ast_format_list_get(&f_len);

	for (x = 0; x < f_len; x++) {
		if (!strcasecmp(f_list[x].name, name) ||
			 !strcasecmp(f_list[x].name, ast_expand_codec_alias(name))) {

			ast_format_copy(result, &f_list[x].format);
			f_list = ast_format_list_destroy(f_list);
			return result;
		}
	}
	f_list = ast_format_list_destroy(f_list);

	return NULL;
}

const char *ast_codec2str(struct ast_format *format)
{
	int x;
	const char *ret = "unknown";
	size_t f_len;
	const struct ast_format_list *f_list = ast_format_list_get(&f_len);

	for (x = 0; x < f_len; x++) {
		if (ast_format_cmp(&f_list[x].format, format) == AST_FORMAT_CMP_EQUAL) {
			ret = f_list[x].desc;
			break;
		}
	}
	f_list = ast_format_list_destroy(f_list);
	return ret;
}

int ast_format_rate(const struct ast_format *format)
{
	switch (format->id) {
	case AST_FORMAT_SLINEAR12:
		return 12000;
	case AST_FORMAT_SLINEAR24:
		return 24000;
	case AST_FORMAT_SLINEAR32:
		return 32000;
	case AST_FORMAT_SLINEAR44:
		return 44100;
	case AST_FORMAT_SLINEAR48:
		return 48000;
	case AST_FORMAT_SLINEAR96:
		return 96000;
	case AST_FORMAT_SLINEAR192:
		return 192000;
	case AST_FORMAT_G722:
	case AST_FORMAT_SLINEAR16:
	case AST_FORMAT_SIREN7:
	case AST_FORMAT_SPEEX16:
		return 16000;
	case AST_FORMAT_SIREN14:
	case AST_FORMAT_SPEEX32:
		return 32000;
	case AST_FORMAT_G719:
		return 48000;
	case AST_FORMAT_SILK:
		if (!(ast_format_isset(format,
			SILK_ATTR_KEY_SAMP_RATE,
			SILK_ATTR_VAL_SAMP_24KHZ,
			AST_FORMAT_ATTR_END))) {
			return 24000;
		} else if (!(ast_format_isset(format,
			SILK_ATTR_KEY_SAMP_RATE,
			SILK_ATTR_VAL_SAMP_16KHZ,
			AST_FORMAT_ATTR_END))) {
			return 16000;
		} else if (!(ast_format_isset(format,
			SILK_ATTR_KEY_SAMP_RATE,
			SILK_ATTR_VAL_SAMP_12KHZ,
			AST_FORMAT_ATTR_END))) {
			return 12000;
		} else {
			return 8000;
		}
	case AST_FORMAT_CELT:
	{
		int samplerate;
		if (!(ast_format_get_value(format,
			CELT_ATTR_KEY_SAMP_RATE,
			&samplerate))) {
			return samplerate;
		}
	}
	default:
		return 8000;
	}
}

static char *show_codecs(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int x, found=0;
	size_t f_len;
	const struct ast_format_list *f_list;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show codecs [audio|video|image|text]";
		e->usage =
			"Usage: core show codecs [audio|video|image|text]\n"
			"       Displays codec mapping\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if ((a->argc < 3) || (a->argc > 4)) {
		return CLI_SHOWUSAGE;
	}

	f_list = ast_format_list_get(&f_len);
	if (!ast_opt_dont_warn) {
		ast_cli(a->fd, "Disclaimer: this command is for informational purposes only.\n"
				"\tIt does not indicate anything about your configuration.\n");
	}

	ast_cli(a->fd, "%8s %5s %8s %s\n","ID","TYPE","NAME","DESCRIPTION");
	ast_cli(a->fd, "-----------------------------------------------------------------------------------\n");

	for (x = 0; x < f_len; x++) {
		if (a->argc == 4) {
			if (!strcasecmp(a->argv[3], "audio")) {
				if (AST_FORMAT_GET_TYPE(f_list[x].format.id) != AST_FORMAT_TYPE_AUDIO) {
					continue;
				}
			} else if (!strcasecmp(a->argv[3], "video")) {
				if (AST_FORMAT_GET_TYPE(f_list[x].format.id) != AST_FORMAT_TYPE_VIDEO) {
					continue;
				}
			} else if (!strcasecmp(a->argv[3], "image")) {
				if (AST_FORMAT_GET_TYPE(f_list[x].format.id) != AST_FORMAT_TYPE_IMAGE) {
					continue;
				}
			} else if (!strcasecmp(a->argv[3], "text")) {
				if (AST_FORMAT_GET_TYPE(f_list[x].format.id) != AST_FORMAT_TYPE_TEXT) {
					continue;
				}
			} else {
				continue;
			}
		}

		ast_cli(a->fd, "%8u %5s %8s (%s)\n",
			f_list[x].format.id,
			(AST_FORMAT_GET_TYPE(f_list[x].format.id) == AST_FORMAT_TYPE_AUDIO) ? "audio" :
			(AST_FORMAT_GET_TYPE(f_list[x].format.id)  == AST_FORMAT_TYPE_IMAGE)  ? "image" :
			(AST_FORMAT_GET_TYPE(f_list[x].format.id)  == AST_FORMAT_TYPE_VIDEO) ? "video" :
			(AST_FORMAT_GET_TYPE(f_list[x].format.id)  == AST_FORMAT_TYPE_TEXT)  ? "text"  :
			"(unk)",
			f_list[x].name,
			f_list[x].desc);
		found = 1;
	}

	f_list = ast_format_list_destroy(f_list);
	if (!found) {
		return CLI_SHOWUSAGE;
	} else {
		return CLI_SUCCESS;
	}
}

static char *show_codec_n(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	enum ast_format_id format_id;
	int x, found = 0;
	int type_punned_codec;
	size_t f_len;
	const struct ast_format_list *f_list;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show codec";
		e->usage =
			"Usage: core show codec <number>\n"
			"       Displays codec mapping\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (sscanf(a->argv[3], "%30d", &type_punned_codec) != 1) {
		return CLI_SHOWUSAGE;
	}
	format_id = type_punned_codec;

	f_list = ast_format_list_get(&f_len);
	for (x = 0; x < f_len; x++) {
		if (f_list[x].format.id == format_id) {
			found = 1;
			ast_cli(a->fd, "%11u %s\n", (unsigned int) format_id, f_list[x].desc);
			break;
		}
	}

	if (!found) {
		ast_cli(a->fd, "Codec %d not found\n", format_id);
	}

	f_list = ast_format_list_destroy(f_list);
	return CLI_SUCCESS;
}

/* Builtin Asterisk CLI-commands for debugging */
static struct ast_cli_entry my_clis[] = {
	AST_CLI_DEFINE(show_codecs, "Displays a list of codecs"),
	AST_CLI_DEFINE(show_codec_n, "Shows a specific codec"),
};
int init_framer(void)
{
	ast_cli_register_multiple(my_clis, ARRAY_LEN(my_clis));
	return 0;
}

static int format_list_add_custom(struct ast_format_list *new)
{
	struct ast_format_list *entry;
	if (!(entry = ao2_alloc(sizeof(*entry), NULL))) {
		return -1;
	}
	memcpy(entry, new, sizeof(struct ast_format_list));
	entry->custom_entry = 1;
	ao2_link(format_list, entry);
	return 0;
}
static int format_list_add_static(
	const struct ast_format *format,
	const char *name,
	int samplespersecond,
	const char *description,
	int fr_len,
	int min_ms,
	int max_ms,
	int inc_ms,
	int def_ms,
	unsigned int flags,
	int cur_ms)
{
	struct ast_format_list *entry;
	if (!(entry = ao2_alloc(sizeof(*entry), NULL))) {
		return -1;
	}
	ast_format_copy(&entry->format, format);
	ast_copy_string(entry->name, name, sizeof(entry->name));
	ast_copy_string(entry->desc, description, sizeof(entry->desc));
	entry->samplespersecond = samplespersecond;
	entry->fr_len = fr_len;
	entry->min_ms = min_ms;
	entry->max_ms = max_ms;
	entry->inc_ms = inc_ms;
	entry->def_ms = def_ms;
	entry->flags = flags;
	entry->cur_ms = cur_ms;
	entry->custom_entry = 0;

	ao2_link(format_list, entry);
	return 0;
}

static int list_all_custom(void *obj, void *arg, int flag)
{
	struct ast_format_list *entry = obj;
	return entry->custom_entry ? CMP_MATCH : 0;
}

static int list_cmp_cb(void *obj, void *arg, int flags)
{
	struct ast_format_list *entry1 = obj;
	struct ast_format_list *entry2 = arg;

	return (ast_format_cmp(&entry1->format, &entry2->format) == AST_FORMAT_CMP_EQUAL) ? CMP_MATCH | CMP_STOP : 0;
}
static int list_hash_cb(const void *obj, const int flags)
{
	return ao2_container_count(format_list);
}

const struct ast_format_list *ast_format_list_get(size_t *size)
{
	struct ast_format_list *list;
	ast_rwlock_rdlock(&format_list_array_lock);
	ao2_ref(format_list_array, 1);
	list = format_list_array;
	*size = format_list_array_len;
	ast_rwlock_unlock(&format_list_array_lock);
	return list;
}
const struct ast_format_list *ast_format_list_destroy(const struct ast_format_list *list)
{
	ao2_ref((void *) list, -1);
	return NULL;
}

static int build_format_list_array(void)
{
	struct ast_format_list *tmp;
	size_t arraysize = sizeof(struct ast_format_list) * ao2_container_count(format_list);
	int i = 0;
	struct ao2_iterator it;

	ast_rwlock_wrlock(&format_list_array_lock);
	tmp = format_list_array;
	if (!(format_list_array = ao2_alloc(arraysize, NULL))) {
		format_list_array = tmp;
		ast_rwlock_unlock(&format_list_array_lock);
		return -1;
	}
	format_list_array_len = ao2_container_count(format_list);
	if (tmp) {
		ao2_ref(tmp, -1);
	}

	/* walk through the container adding elements to the static array */
	it = ao2_iterator_init(format_list, 0);
	while ((tmp = ao2_iterator_next(&it)) && (i < format_list_array_len)) {
		memcpy(&format_list_array[i], tmp, sizeof(struct ast_format_list));
		ao2_ref(tmp, -1);
		i++;
	}
	ao2_iterator_destroy(&it);

	ast_rwlock_unlock(&format_list_array_lock);
	return 0;
}
static int format_list_init(void)
{
	struct ast_format tmpfmt;
	if (!(format_list = ao2_container_alloc(283, list_hash_cb, list_cmp_cb))) {
		return -1;
	}
	/* initiate static entries XXX DO NOT CHANGE THIS ORDER! */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_G723_1, 0), "g723", 8000, "G.723.1", 20, 30, 300, 30, 30, 0, 0);       /*!< G723.1 */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_GSM, 0), "gsm",  8000, "GSM", 33, 20, 300, 20, 20, 0, 0);              /*!< codec_gsm.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0), "ulaw", 8000, "G.711 u-law", 80, 10, 150, 10, 20, 0, 0);     /*!< codec_ulaw.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0), "alaw", 8000, "G.711 A-law", 80, 10, 150, 10, 20, 0, 0);     /*!< codec_alaw.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_G726, 0), "g726", 8000, "G.726 RFC3551", 40, 10, 300, 10, 20, 0, 0);   /*!< codec_g726.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_ADPCM, 0), "adpcm" , 8000, "ADPCM", 40, 10, 300, 10, 20, 0, 0);        /*!< codec_adpcm.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0), "slin", 8000, "16 bit Signed Linear PCM", 160, 10, 70, 10, 20, AST_SMOOTHER_FLAG_BE, 0); /*!< Signed linear */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_LPC10, 0), "lpc10", 8000, "LPC10", 7, 20, 20, 20, 20, 0, 0);           /*!< codec_lpc10.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0), "g729", 8000, "G.729A", 10, 10, 230, 10, 20, AST_SMOOTHER_FLAG_G729, 0);   /*!< Binary commercial distribution */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SPEEX, 0), "speex", 8000, "SpeeX", 10, 10, 60, 10, 20, 0, 0);          /*!< codec_speex.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SPEEX16, 0), "speex16", 16000, "SpeeX 16khz", 10, 10, 60, 10, 20, 0, 0);   /*!< codec_speex.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_ILBC, 0), "ilbc", 8000, "iLBC", 50, 30, 30, 30, 30, 0, 0);                 /*!< codec_ilbc.c */ /* inc=30ms - workaround */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_G726_AAL2, 0), "g726aal2", 8000, "G.726 AAL2", 40, 10, 300, 10, 20, 0, 0); /*!< codec_g726.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_G722, 0), "g722", 16000, "G722", 80, 10, 150, 10, 20, 0, 0);               /*!< codec_g722.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR16, 0), "slin16", 16000, "16 bit Signed Linear PCM (16kHz)", 320, 10, 70, 10, 20, AST_SMOOTHER_FLAG_BE, 0);/*!< Signed linear (16kHz) */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_JPEG, 0), "jpeg", 0, "JPEG image", 0, 0, 0, 0 ,0 ,0 ,0);          /*!< See format_jpeg.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_PNG, 0), "png", 0, "PNG image", 0, 0, 0, 0 ,0 ,0 ,0);             /*!< PNG Image format */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_H261, 0), "h261", 0, "H.261 Video", 0, 0, 0, 0 ,0 ,0 ,0);         /*!< H.261 Video Passthrough */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_H263, 0), "h263", 0, "H.263 Video", 0, 0, 0, 0 ,0 ,0 ,0);         /*!< H.263 Passthrough support, see format_h263.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_H263_PLUS, 0), "h263p", 0, "H.263+ Video", 0, 0, 0,0 ,0 ,0, 0);  /*!< H.263plus passthrough support See format_h263.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_H264, 0), "h264", 0, "H.264 Video", 0, 0, 0, 0 ,0 ,0, 0);         /*!< Passthrough support, see format_h263.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_MP4_VIDEO, 0), "mpeg4", 0, "MPEG4 Video", 0, 0, 0, 0, 0 ,0, 0);   /*!< Passthrough support for MPEG4 */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_T140RED, 0), "red", 1, "T.140 Realtime Text with redundancy", 0, 0, 0,0 ,0 ,0, 0);     /*!< Redundant T.140 Realtime Text */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_T140, 0), "t140", 0, "Passthrough T.140 Realtime Text", 0, 0, 0, 0 ,0 ,0, 0);      /*!< Passthrough support for T.140 Realtime Text */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SIREN7, 0), "siren7", 16000, "ITU G.722.1 (Siren7, licensed from Polycom)", 80, 20, 80, 20, 20, 0, 0); /*!< Binary commercial distribution */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SIREN14, 0), "siren14", 32000, "ITU G.722.1 Annex C, (Siren14, licensed from Polycom)", 120, 20, 80, 20, 20, 0, 0);	/*!< Binary commercial distribution */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_TESTLAW, 0), "testlaw", 8000, "G.711 test-law", 80, 10, 150, 10, 20, 0, 0);    /*!< codec_ulaw.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_G719, 0), "g719", 48000, "ITU G.719", 160, 20, 80, 20, 20, 0, 0);

	/* ORDER MAY CHANGE AFTER THIS POINT IN THE LIST */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SPEEX32, 0), "speex32", 32000, "SpeeX 32khz", 10, 10, 60, 10, 20, 0, 0);   /*!< codec_speex.c */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR12, 0), "slin12", 12000, "16 bit Signed Linear PCM (12kHz)", 240, 10, 70, 10, 20, AST_SMOOTHER_FLAG_BE, 0);/*!< Signed linear (12kHz) */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR24, 0), "slin24", 24000, "16 bit Signed Linear PCM (24kHz)", 480, 10, 70, 10, 20, AST_SMOOTHER_FLAG_BE, 0);/*!< Signed linear (24kHz) */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR32, 0), "slin32", 32000, "16 bit Signed Linear PCM (32kHz)", 640, 10, 70, 10, 20, AST_SMOOTHER_FLAG_BE, 0);/*!< Signed linear (32kHz) */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR44, 0), "slin44", 44100, "16 bit Signed Linear PCM (44kHz)", 882, 10, 70, 10, 20, AST_SMOOTHER_FLAG_BE, 0);/*!< Signed linear (44.1kHz) */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR48, 0), "slin48", 48000, "16 bit Signed Linear PCM (48kHz)", 960, 10, 70, 10, 20, AST_SMOOTHER_FLAG_BE, 0);/*!< Signed linear (48kHz) */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR96, 0), "slin96", 96000, "16 bit Signed Linear PCM (96kHz)", 1920, 10, 70, 10, 20, AST_SMOOTHER_FLAG_BE, 0);/*!< Signed linear (96kHz) */
	format_list_add_static(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR192, 0), "slin192", 192000, "16 bit Signed Linear PCM (192kHz)", 3840, 10, 70, 10, 20, AST_SMOOTHER_FLAG_BE, 0);/*!< Signed linear (192kHz) */

	return 0;
}

int ast_format_list_init()
{
	if (ast_rwlock_init(&format_list_array_lock)) {
		return -1;
	}
	if (format_list_init()) {
		goto init_list_cleanup;
	}
	if (build_format_list_array()) {
		goto init_list_cleanup;
	}

	return 0;
init_list_cleanup:

	ast_rwlock_destroy(&format_list_array_lock);
	ao2_ref(format_list, -1);
	if (format_list_array) {
		ao2_ref(format_list_array, -1);
	}
	return -1;
}

int ast_format_attr_init()
{
	ast_cli_register_multiple(my_clis, ARRAY_LEN(my_clis));
	if (ast_rwlock_init(&ilock)) {
		return -1;
	}

	if (!(interfaces = ao2_container_alloc(283, interface_hash_cb, interface_cmp_cb))) {
		ast_rwlock_destroy(&ilock);
		goto init_cleanup;
	}
	return 0;

init_cleanup:
	ast_rwlock_destroy(&ilock);
	if (interfaces) {
		ao2_ref(interfaces, -1);
	}
	return -1;
}

static int custom_celt_format(struct ast_format_list *entry, unsigned int maxbitrate, unsigned int framesize)
{
	if (!entry->samplespersecond) {
		ast_log(LOG_WARNING, "Custom CELT format definition '%s' requires sample rate to be defined.\n", entry->name);
	}
	ast_format_set(&entry->format, AST_FORMAT_CELT, 0);
	if (!has_interface(&entry->format)) {
		return -1;
	}

	snprintf(entry->desc, sizeof(entry->desc), "CELT Custom Format %dkhz", entry->samplespersecond/1000);

	ast_format_append(&entry->format,
		CELT_ATTR_KEY_SAMP_RATE, entry->samplespersecond,
		CELT_ATTR_KEY_MAX_BITRATE, maxbitrate,
		CELT_ATTR_KEY_FRAME_SIZE, framesize,
		AST_FORMAT_ATTR_END);

	entry->fr_len = 80;
	entry->min_ms = 20;
	entry->max_ms = 20;
	entry->inc_ms = 20;
	entry->def_ms = 20;
	return 0;
}

static int custom_silk_format(struct ast_format_list *entry, unsigned int maxbitrate, int usedtx, int usefec, int packetloss_percentage)
{
	if (!entry->samplespersecond) {
		ast_log(LOG_WARNING, "Custom SILK format definition '%s' requires sample rate to be defined.\n", entry->name);
	}
	ast_format_set(&entry->format, AST_FORMAT_SILK, 0);

	if (!has_interface(&entry->format)) {
		return -1;
	}

	switch (entry->samplespersecond) {
	case 8000:
		ast_copy_string(entry->desc, "SILK Custom Format 8khz", sizeof(entry->desc));
		ast_format_append(&entry->format,
			SILK_ATTR_KEY_SAMP_RATE, SILK_ATTR_VAL_SAMP_8KHZ,
			AST_FORMAT_ATTR_END);
		break;
	case 12000:
		ast_copy_string(entry->desc, "SILK Custom Format 12khz", sizeof(entry->desc));
		ast_format_append(&entry->format,
			SILK_ATTR_KEY_SAMP_RATE, SILK_ATTR_VAL_SAMP_12KHZ,
			AST_FORMAT_ATTR_END);
		break;
	case 16000:
		ast_copy_string(entry->desc, "SILK Custom Format 16khz", sizeof(entry->desc));
		ast_format_append(&entry->format,
			SILK_ATTR_KEY_SAMP_RATE, SILK_ATTR_VAL_SAMP_16KHZ,
			AST_FORMAT_ATTR_END);
		break;
	case 24000:
		ast_copy_string(entry->desc, "SILK Custom Format 24khz", sizeof(entry->desc));
		ast_format_append(&entry->format,
			SILK_ATTR_KEY_SAMP_RATE, SILK_ATTR_VAL_SAMP_24KHZ,
			AST_FORMAT_ATTR_END);
		break;
	default:
		ast_log(LOG_WARNING, "Custom SILK format definition '%s' can not support sample rate %d\n", entry->name, entry->samplespersecond);
		return -1;
	}
	ast_format_append(&entry->format,
			SILK_ATTR_KEY_MAX_BITRATE, maxbitrate,
			SILK_ATTR_KEY_DTX, usedtx ? 1 : 0,
			SILK_ATTR_KEY_FEC, usefec ? 1 : 0,
			SILK_ATTR_KEY_PACKETLOSS_PERCENTAGE, packetloss_percentage,
			AST_FORMAT_ATTR_END);

	entry->fr_len = 80;
	entry->min_ms = 20;
	entry->max_ms = 20;
	entry->inc_ms = 20;
	entry->def_ms = 20;
	return 0;
}

static int conf_process_format_name(const char *name, enum ast_format_id *id)
{
	if (!strcasecmp(name, "silk")) {
		*id = AST_FORMAT_SILK;
	} else if (!strcasecmp(name, "celt")) {
		*id = AST_FORMAT_CELT;
	} else {
		*id = 0;
		return -1;
	}
	return 0;
}

static int conf_process_sample_rate(const char *rate, unsigned int *result)
{
	if (!strcasecmp(rate, "8000")) {
		*result = 8000;
	} else if (!strcasecmp(rate, "12000")) {
		*result = 12000;
	} else if (!strcasecmp(rate, "16000")) {
		*result = 16000;
	} else if (!strcasecmp(rate, "24000")) {
		*result = 24000;
	} else if (!strcasecmp(rate, "32000")) {
		*result = 32000;
	} else if (!strcasecmp(rate, "44100")) {
		*result = 44100;
	} else if (!strcasecmp(rate, "48000")) {
		*result = 48000;
	} else if (!strcasecmp(rate, "96000")) {
		*result = 96000;
	} else if (!strcasecmp(rate, "192000")) {
		*result = 192000;
	} else {
		*result = 0;
		return -1;
	}

	return 0;
}
static int load_format_config(void)
{
	struct ast_flags config_flags = { 0, };
	struct ast_config *cfg = ast_config_load(FORMAT_CONFIG, config_flags);
	struct ast_format_list entry;
	struct ast_variable *var;
	char *cat = NULL;
	int add_it = 0;

	struct {
		enum ast_format_id id;
		unsigned int maxbitrate;
		unsigned int framesize;
		unsigned int packetloss_percentage;
		int usefec;
		int usedtx;
	} settings;

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		return 0;
	}

	/* remove all custom formats from the AO2 Container. Note, this has no affect on the
	 * global format list until the list is rebuild.  That is why this is okay to do while
	 * reloading the config. */
	ao2_callback(format_list, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, list_all_custom, NULL);

	while ((cat = ast_category_browse(cfg, cat))) {
		memset(&entry, 0, sizeof(entry));
		memset(&settings, 0, sizeof(settings));
		add_it = 0;

		if (!(ast_variable_retrieve(cfg, cat, "type"))) {
			continue;
		}
		ast_copy_string(entry.name, cat, sizeof(entry.name));
		var = ast_variable_browse(cfg, cat);
		for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
			if (!strcasecmp(var->name, "type") && conf_process_format_name(var->value, &settings.id)) {
				ast_log(LOG_WARNING, "Can not make custom format type for '%s' at line %d of %s\n",
					var->value, var->lineno, FORMAT_CONFIG);
				continue;
			} else if (!strcasecmp(var->name, "samprate") && conf_process_sample_rate(var->value, &entry.samplespersecond)) {
				ast_log(LOG_WARNING, "Sample rate '%s' at line %d of %s is not supported.\n",
						var->value, var->lineno, FORMAT_CONFIG);
			} else if (!strcasecmp(var->name, "maxbitrate")) {
				if (sscanf(var->value, "%30u", &settings.maxbitrate) != 1) {
					ast_log(LOG_WARNING, "maxbitrate '%s' at line %d of %s is not supported.\n",
						var->value, var->lineno, FORMAT_CONFIG);
				}
			} else if (!strcasecmp(var->name, "framesize")) {
				if (sscanf(var->value, "%30u", &settings.framesize) != 1) {
					ast_log(LOG_WARNING, "framesize '%s' at line %d of %s is not supported.\n",
						var->value, var->lineno, FORMAT_CONFIG);
				}
			} else if (!strcasecmp(var->name, "dtx")) {
				settings.usedtx = ast_true(var->value) ? 1 : 0;
			} else if (!strcasecmp(var->name, "fec")) {
				settings.usefec = ast_true(var->value) ? 1 : 0;
			} else if (!strcasecmp(var->name, "packetloss_percentage")) {
				if ((sscanf(var->value, "%30u", &settings.packetloss_percentage) != 1) || (settings.packetloss_percentage > 100)) {
					ast_log(LOG_WARNING, "packetloss_percentage '%s' at line %d of %s is not supported.\n",
						var->value, var->lineno, FORMAT_CONFIG);
				}
			}
		}

		switch (settings.id) {
		case AST_FORMAT_SILK:
			if (!(custom_silk_format(&entry, settings.maxbitrate, settings.usedtx, settings.usefec, settings.packetloss_percentage))) {
				add_it = 1;
			}
			break;
		case AST_FORMAT_CELT:
			if (!(custom_celt_format(&entry, settings.maxbitrate, settings.framesize))) {
				add_it = 1;
			}
			break;
		default:
			ast_log(LOG_WARNING, "Can not create custom format %s\n", entry.name);
		}

		if (add_it) {
			format_list_add_custom(&entry);
		}
	}
	ast_config_destroy(cfg);
	build_format_list_array();
	return 0;
}

int ast_format_attr_reg_interface(const struct ast_format_attr_interface *interface)
{
	int x;
	size_t f_len;
	const struct ast_format_list *f_list;
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

	/* This will find all custom formats in codecs.conf for this new registered interface */
	load_format_config();

	/* update the RTP engine to all custom formats created for this interface */
	f_list = ast_format_list_get(&f_len);
	for (x = 0; x < f_len; x++) {
		if (f_list[x].format.id == tmp_wrapper.id) {
			ast_rtp_engine_load_format(&f_list[x].format);
		}
	}

	return 0;
}

int ast_format_attr_unreg_interface(const struct ast_format_attr_interface *interface)
{
	int x;
	size_t f_len;
	const struct ast_format_list *f_list;
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

	/* update the RTP engine to remove all custom formats created for this interface */
	f_list = ast_format_list_get(&f_len);
	for (x = 0; x < f_len; x++) {
		if (f_list[x].format.id == tmp_wrapper.id) {
			ast_rtp_engine_unload_format(&f_list[x].format);
		}
	}

	/* This will remove all custom formats previously created for this interface */
	load_format_config();

	return 0;
}
