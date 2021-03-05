/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Lorenzo Miniero <lorenzo@meetecho.com>
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
 * \brief Opus format attribute interface
 *
 * \author Lorenzo Miniero <lorenzo@meetecho.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <ctype.h>

#include "asterisk/module.h"
#include "asterisk/format.h"
#include "asterisk/astobj2.h"
#include "asterisk/logger.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"
#include "asterisk/opus.h"

/*!
 * \brief Opus attribute structure.
 *
 * \note http://tools.ietf.org/html/rfc7587#section-6
 */
struct opus_attr {
	int maxbitrate;
	int maxplayrate;
	int ptime;
	int stereo;
	int cbr;
	int fec;
	int dtx;
	int spropmaxcapturerate;
	int spropstereo;
	int maxptime;
	/* Note data is expected to be an ao2_object type */
	void *data;
};

static struct opus_attr default_opus_attr = {
	.maxbitrate = CODEC_OPUS_DEFAULT_BITRATE,
	.maxplayrate = CODEC_OPUS_DEFAULT_SAMPLE_RATE,
	.ptime = CODEC_OPUS_DEFAULT_PTIME,
	.stereo = CODEC_OPUS_DEFAULT_STEREO,
	.cbr = CODEC_OPUS_DEFAULT_CBR,
	.fec = CODEC_OPUS_DEFAULT_FEC,
	.dtx = CODEC_OPUS_DEFAULT_DTX,
	.spropmaxcapturerate = CODEC_OPUS_DEFAULT_SAMPLE_RATE,
	.spropstereo = CODEC_OPUS_DEFAULT_STEREO,
	.maxptime = CODEC_OPUS_DEFAULT_MAX_PTIME
};

static void opus_destroy(struct ast_format *format)
{
	struct opus_attr *attr = ast_format_get_attribute_data(format);

	if (!attr) {
		return;
	}

	ao2_cleanup(attr->data);
	ast_free(attr);
}

static int opus_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct opus_attr *original = ast_format_get_attribute_data(src);
	struct opus_attr *attr = ast_malloc(sizeof(*attr));

	if (!attr) {
		return -1;
	}

	*attr = original ? *original : default_opus_attr;
	ao2_bump(attr->data);

	ast_format_set_attribute_data(dst, attr);
	ast_format_set_channel_count(dst, ast_format_get_channel_count(src));

	return 0;
}

static void sdp_fmtp_get(const char *attributes, const char *name, int *attr)
{
	const char *kvp = attributes;
	int val;

	if (ast_strlen_zero(attributes)) {
		return;
	}

	/* This logic goes through each attribute in the fmtp line looking for the
	 * requested named attribute.
	 */
	while (*kvp) {
		/* Skip any preceeding blanks as some implementations separate attributes using spaces too */
		kvp = ast_skip_blanks(kvp);

		/* If we are at the requested attribute get its value and return */
		if (!strncmp(kvp, name, strlen(name)) && kvp[strlen(name)] == '=') {
			if (sscanf(kvp, "%*[^=]=%30d", &val) == 1) {
				*attr = val;
				break;
			}
		}

		/* Move on to the next attribute if possible */
		kvp = strchr(kvp, ';');
		if (!kvp) {
			break;
		}

		kvp++;
	}
}

static struct ast_format *opus_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	char *attribs = ast_strdupa(attributes), *attrib;
	struct ast_format *cloned;
	struct opus_attr *attr;

	cloned = ast_format_clone(format);
	if (!cloned) {
		return NULL;
	}

	attr = ast_format_get_attribute_data(cloned);

	/* lower-case everything, so we are case-insensitive */
	for (attrib = attribs; *attrib; ++attrib) {
		*attrib = tolower(*attrib);
	} /* based on channels/chan_sip.c:process_a_sdp_image() */

	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_MAX_PLAYBACK_RATE, &attr->maxplayrate);
	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_MAX_CODED_AUDIO_BANDWIDTH,
		&attr->maxplayrate);
	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_SPROP_MAX_CAPTURE_RATE,
		&attr->spropmaxcapturerate);
	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_MAX_PTIME, &attr->maxptime);
	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_PTIME, &attr->ptime);
	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_MAX_AVERAGE_BITRATE, &attr->maxbitrate);
	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_STEREO, &attr->stereo);
	if (attr->stereo) {
		ast_format_set_channel_count(cloned, 2);
	}
	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_SPROP_STEREO, &attr->spropstereo);
	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_CBR, &attr->cbr);
	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_FEC, &attr->fec);
	sdp_fmtp_get(attribs, CODEC_OPUS_ATTR_DTX, &attr->dtx);

	return cloned;
}

static void opus_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct opus_attr *attr = ast_format_get_attribute_data(format);
	int base_fmtp_size;
	int original_size;

	if (!attr) {
		/*
		 * (Only) cached formats do not have attribute data assigned because
		 * they were created before this attribute module was registered.
		 * Therefore, we assume the default attribute values here.
		 */
		attr = &default_opus_attr;
	}

	original_size = ast_str_strlen(*str);
	base_fmtp_size = ast_str_append(str, 0, "a=fmtp:%u ", payload);

	if (CODEC_OPUS_DEFAULT_SAMPLE_RATE != attr->maxplayrate) {
		ast_str_append(str, 0, "%s=%d;",
			CODEC_OPUS_ATTR_MAX_PLAYBACK_RATE, attr->maxplayrate);
	}

	if (CODEC_OPUS_DEFAULT_SAMPLE_RATE != attr->spropmaxcapturerate) {
		ast_str_append(str, 0, "%s=%d;",
			CODEC_OPUS_ATTR_SPROP_MAX_CAPTURE_RATE, attr->spropmaxcapturerate);
	}

	if (CODEC_OPUS_DEFAULT_BITRATE != attr->maxbitrate || attr->maxbitrate > 0) {
		ast_str_append(str, 0, "%s=%d;",
			CODEC_OPUS_ATTR_MAX_AVERAGE_BITRATE, attr->maxbitrate);
	}

	if (CODEC_OPUS_DEFAULT_STEREO != attr->stereo) {
		ast_str_append(str, 0, "%s=%d;",
			CODEC_OPUS_ATTR_STEREO, attr->stereo);
	}

	if (CODEC_OPUS_DEFAULT_STEREO != attr->spropstereo) {
		ast_str_append(str, 0, "%s=%d;",
			CODEC_OPUS_ATTR_SPROP_STEREO, attr->spropstereo);
	}

	if (CODEC_OPUS_DEFAULT_CBR != attr->cbr) {
		ast_str_append(str, 0, "%s=%d;",
			CODEC_OPUS_ATTR_CBR, attr->cbr);
	}

	if (CODEC_OPUS_DEFAULT_FEC!= attr->fec) {
		ast_str_append(str, 0, "%s=%d;",
		       CODEC_OPUS_ATTR_FEC, attr->fec);
	}

	if (CODEC_OPUS_DEFAULT_DTX != attr->dtx) {
		ast_str_append(str, 0, "%s=%d;",
			CODEC_OPUS_ATTR_DTX, attr->dtx);
	}

	if (base_fmtp_size == ast_str_strlen(*str) - original_size) {
		ast_str_truncate(*str, original_size);
	} else {
		ast_str_truncate(*str, -1);
		ast_str_append(str, 0, "\r\n");
	}
}

static struct ast_format *opus_getjoint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct opus_attr *attr1 = ast_format_get_attribute_data(format1);
	struct opus_attr *attr2 = ast_format_get_attribute_data(format2);
	struct ast_format *jointformat;
	struct opus_attr *attr_res;

	if (!attr1) {
		attr1 = &default_opus_attr;
	}

	if (!attr2) {
		attr2 = &default_opus_attr;
	}

	jointformat = ast_format_clone(format1);
	if (!jointformat) {
		return NULL;
	}

	if (ast_format_get_channel_count(format1) == 2 || ast_format_get_channel_count(format2) == 2) {
		ast_format_set_channel_count(jointformat, 2);
	}
	attr_res = ast_format_get_attribute_data(jointformat);

	attr_res->dtx = attr1->dtx || attr2->dtx ? 1 : 0;

	/* Only do FEC if both sides want it.  If a peer specifically requests not
	 * to receive with FEC, it may be a waste of bandwidth. */
	attr_res->fec = attr1->fec && attr2->fec ? 1 : 0;

	attr_res->cbr = attr1->cbr || attr2->cbr ? 1 : 0;
	attr_res->spropstereo = attr1->spropstereo || attr2->spropstereo ? 1 : 0;

	/* Only do stereo if both sides want it.  If a peer specifically requests not
	 * to receive stereo signals, it may be a waste of bandwidth. */
	attr_res->stereo = attr1->stereo && attr2->stereo ? 1 : 0;

	if (attr1->maxbitrate < 0) {
		attr_res->maxbitrate = attr2->maxbitrate;
	} else if (attr2->maxbitrate < 0) {
		attr_res->maxbitrate = attr1->maxbitrate;
	} else {
		attr_res->maxbitrate = MIN(attr1->maxbitrate, attr2->maxbitrate);
	}

	attr_res->spropmaxcapturerate = MIN(attr1->spropmaxcapturerate, attr2->spropmaxcapturerate);
	attr_res->maxplayrate = MIN(attr1->maxplayrate, attr2->maxplayrate);

	return jointformat;
}

static struct ast_format *opus_set(const struct ast_format *format,
	const char *name, const char *value)
{
	struct ast_format *cloned;
	struct opus_attr *attr;
	int val;

	if (!(cloned = ast_format_clone(format))) {
		return NULL;
	}

	attr = ast_format_get_attribute_data(cloned);

	if (!strcmp(name, CODEC_OPUS_ATTR_DATA)) {
		ao2_cleanup(attr->data);
		attr->data = ao2_bump((void*)value);
		return cloned;
	}

	if (sscanf(value, "%30d", &val) != 1) {
		ast_log(LOG_WARNING, "Unknown value '%s' for attribute type '%s'\n",
			value, name);
		ao2_ref(cloned, -1);
		return NULL;
	}

	if (!strcasecmp(name, CODEC_OPUS_ATTR_MAX_PLAYBACK_RATE)) {
		attr->maxplayrate = val;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_MAX_CODED_AUDIO_BANDWIDTH)) {
		attr->maxplayrate = val;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_SPROP_MAX_CAPTURE_RATE)) {
		attr->spropmaxcapturerate = val;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_MAX_PTIME)) {
		attr->maxptime = val;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_PTIME)) {
		attr->ptime = val;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_MAX_AVERAGE_BITRATE)) {
		attr->maxbitrate = val;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_STEREO)) {
		attr->stereo = val;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_SPROP_STEREO)) {
		attr->spropstereo = val;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_CBR)) {
		attr->cbr = val;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_FEC)) {
		attr->fec = val;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_DTX)) {
		attr->dtx = val;
	} else {
		ast_log(LOG_WARNING, "unknown attribute type %s\n", name);
	}

	return cloned;
}

static const void *opus_get(const struct ast_format *format, const char *name)
{
	struct opus_attr *attr = ast_format_get_attribute_data(format);
	int *val = NULL;

	if (!attr) {
		return NULL;
	}

	if (!strcasecmp(name, CODEC_OPUS_ATTR_DATA)) {
		return ao2_bump(attr->data);
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_MAX_PLAYBACK_RATE)) {
		val = &attr->maxplayrate;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_SPROP_MAX_CAPTURE_RATE)) {
		val = &attr->spropmaxcapturerate;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_MAX_PTIME)) {
		val = &attr->maxptime;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_PTIME)) {
		val = &attr->ptime;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_MAX_AVERAGE_BITRATE)) {
		val = &attr->maxbitrate;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_STEREO)) {
		val = &attr->stereo;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_SPROP_STEREO)) {
		val = &attr->spropstereo;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_CBR)) {
		val = &attr->cbr;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_FEC)) {
		val = &attr->fec;
	} else if (!strcasecmp(name, CODEC_OPUS_ATTR_DTX)) {
		val = &attr->dtx;
	} else {
		ast_log(LOG_WARNING, "unknown attribute type %s\n", name);
	}

	return val;
}

static struct ast_format_interface opus_interface = {
	.format_destroy = opus_destroy,
	.format_clone = opus_clone,
	.format_get_joint = opus_getjoint,
	.format_attribute_set = opus_set,
	.format_parse_sdp_fmtp = opus_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = opus_generate_sdp_fmtp,
	.format_attribute_get = opus_get
};

static int load_module(void)
{
	if (__ast_format_interface_register("opus", &opus_interface, ast_module_info->self)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Opus Format Attribute Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DRIVER /* Needs to load before codec_opus */
);
