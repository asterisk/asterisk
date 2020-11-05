#include "asterisk.h"

/* version 4.0, compatiblity overview as of October 2015 */
/* based on res/res_format_attr_silk.c */

#include <ctype.h>                      /* for tolower */
#include <math.h>                       /* for log10, floor */

#include "asterisk/module.h"
#include "asterisk/format.h"
#include "asterisk/astobj2.h"           /* for ao2_bump */
#include "asterisk/format_cache.h"      /* for ast_format_amr(wb) */
#include "asterisk/logger.h"            /* for ast_debug, ast_log, etc */
#include "asterisk/strings.h"           /* for ast_str_append */
#include "asterisk/utils.h"             /* for MAX, ast_calloc, ast_free, etc */

#include "asterisk/amr.h"

/* Asterisk internal defaults; can differ from RFC defaults */ 
static struct amr_attr default_amr_attr = {
	.octet_align            =  0, /* bandwidth efficient */
	.mode_set               =  0, /* all modes           */
	.mode_change_period     =  0, /* not specified       */
	.mode_change_capability =  0, /* not supported       */
	.mode_change_neighbor   =  0, /* change to any       */
	.crc                    =  0, /* off                 */
	.robust_sorting         =  0, /* off                 */
	.interleaving           =  0, /* off                 */
	.max_red                = -1, /* no redundancy limit */
};

static void amr_destroy(struct ast_format *format)
{
	struct amr_attr *attr = ast_format_get_attribute_data(format);

	ast_free(attr);
}

static int amr_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct amr_attr *original = ast_format_get_attribute_data(src);
	struct amr_attr *attr = ast_malloc(sizeof(*attr));

	if (!attr) {
		return -1;
	}

	if (original) {
		*attr = *original;
	} else {
		*attr = default_amr_attr;
		/* internal variables for transcoding module */
		if (16000 == ast_format_get_sample_rate(src)) {
			attr->mode_current = 8;
			attr->vad = 0;
		} else {
			attr->mode_current = 7;
			attr->vad = 1;
		}
	}

	ast_format_set_attribute_data(dst, attr);

	return 0;
}

static struct ast_format *amr_parse_sdp_fmtp(const struct ast_format *format, const char *attrib)
{
	struct ast_format *cloned;
	struct amr_attr *attr;
	unsigned int val;
	char *attributes;
	char *tmp;
	const unsigned int size = 9; /* same as bit-field definition of mode_set */
	int v[size];
	/* init each slot as 'not specified' */
	for (val = 0; val < size; val = val + 1) {
		v[val] = -1;
	}

	cloned = ast_format_clone(format);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	/* lower-case everything, so we are case-insensitive */
	/* no implementation is known which is affected by this */
	attributes = ast_strdupa(attrib);
	for (tmp = attributes; *tmp; ++tmp) {
		*tmp = tolower(*tmp);
	} /* based on channels/chan_sip.c:process_a_sdp_image() */

	attr->octet_align = 0;
	tmp = strstr(attributes, "octet-align=");
	if (tmp) {
		if (sscanf(tmp, "octet-align=%30u", &val) == 1) {
			attr->octet_align = val;
		}
	}

	attr->mode_set = 0;
	tmp = strstr(attributes, "mode-set=");
	if (tmp) {
		if (sscanf(tmp, "mode-set=%30u,%30u,%30u,%30u,%30u,%30u,%30u,%30u,%30u",
				   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) > 0) {
			for (val = 0; val < size; val = val + 1) {
				if (0 <= v[val] && v[val] < size) {
					attr->mode_set = (attr->mode_set | (1 << v[val]));
					attr->mode_current = v[val];
				}
			}
		}
	}

	attr->mode_change_capability = 0;
	tmp = strstr(attributes, "mode-change-capability=");
	if (tmp) {
		if (sscanf(tmp, "mode-change-capability=%30u", &val) == 1) {
			attr->mode_change_capability = val;
		}
	}

	attr->mode_change_period = 0;
	tmp = strstr(attributes, "mode-change-period=");
	if (tmp) {
		if (sscanf(tmp, "mode-change-period=%30u", &val) == 1) {
			attr->mode_change_period = val;
		}
	}

	attr->mode_change_neighbor = 0;
	tmp = strstr(attributes, "mode-change-neighbor=");
	if (tmp) {
		if (sscanf(tmp, "mode-change-neighbor=%30u", &val) == 1) {
			attr->mode_change_neighbor = val;
		}
	}

	attr->crc = 0;
	tmp = strstr(attributes, "crc=");
	if (tmp) {
		if (sscanf(tmp, "crc=%30u", &val) == 1) {
			attr->crc = val;
			if (attr->crc) {
				attr->octet_align = 1;
			}
		}
	}

	attr->robust_sorting = 0;
	tmp = strstr(attributes, "robust-sorting=");
	if (tmp) {
		if (sscanf(tmp, "robust-sorting=%30u", &val) == 1) {
			attr->robust_sorting = val;
			if (attr->robust_sorting) {
				attr->octet_align = 1;
			}
		}
	}

	attr->interleaving = 0;
	tmp = strstr(attributes, "interleaving=");
	if (tmp) {
		if (sscanf(tmp, "interleaving=%30u", &val) == 1) {
			attr->interleaving = val;
			if (attr->interleaving) {
				attr->octet_align = 1;
			}
		}
	}

	attr->max_red = -1;
	tmp = strstr(attributes, "max-red=");
	if (tmp) {
		if (sscanf(tmp, "max-red=%30u", &val) == 1) {
			attr->max_red = val;
		}
	}

	return cloned;
}

static void amr_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	int appended = 0;
	int listed = 0;
	struct amr_attr *attr = ast_format_get_attribute_data(format);

	if (!attr) {
		attr = &default_amr_attr;
	}

	if (0 != attr->octet_align) {
		if (0 == appended) {
			ast_str_append(str, 0, "a=fmtp:%d ", payload);
		} else {
			ast_str_append(str, 0, ";");
		}
		ast_str_append(str, 0, "octet-align=%d", attr->octet_align);
		appended = appended + 1;
	}
	if (0 != attr->mode_set)
	{
		if (0 == appended) {
			ast_str_append(str, 0, "a=fmtp:%d ", payload);
		} else {
			ast_str_append(str, 0, ";");
		}
		ast_str_append(str, 0, "mode-set=");
		if (attr->mode_set & 0x01) {
			if (0 == listed) {
				ast_str_append(str, 0, "0");
			} else {
				ast_str_append(str, 0, ",0");
			}
			listed = listed + 1;
		}
		if (attr->mode_set & 0x02) {
			if (0 == listed) {
				ast_str_append(str, 0, "1");
			} else {
				ast_str_append(str, 0, ",1");
			}
			listed = listed + 1;
		}
		if (attr->mode_set & 0x04) {
			if (0 == listed) {
				ast_str_append(str, 0, "2");
			} else {
				ast_str_append(str, 0, ",2");
			}
			listed = listed + 1;
		}
		if (attr->mode_set & 0x08) {
			if (0 == listed) {
				ast_str_append(str, 0, "3");
			} else {
				ast_str_append(str, 0, ",3");
			}
			listed = listed + 1;
		}
		if (attr->mode_set & 0x10) {
			if (0 == listed) {
				ast_str_append(str, 0, "4");
			} else {
				ast_str_append(str, 0, ",4");
			}
			listed = listed + 1;
		}
		if (attr->mode_set & 0x20) {
			if (0 == listed) {
				ast_str_append(str, 0, "5");
			} else {
				ast_str_append(str, 0, ",5");
			}
			listed = listed + 1;
		}
		if (attr->mode_set & 0x40) {
			if (0 == listed) {
				ast_str_append(str, 0, "6");
			} else {
				ast_str_append(str, 0, ",6");
			}
			listed = listed + 1;
		}
		if (attr->mode_set & 0x80) {
			if (0 == listed) {
				ast_str_append(str, 0, "7");
			} else {
				ast_str_append(str, 0, ",7");
			}
			listed = listed + 1;
		}
		if (attr->mode_set & 0x100) {
			if (0 == listed) {
				ast_str_append(str, 0, "8");
			} else {
				ast_str_append(str, 0, ",8");
			}
			listed = listed + 1;
		}
		appended = appended + 1;
	}
	if (0 != attr->mode_change_capability) {
		if (0 == appended) {
			ast_str_append(str, 0, "a=fmtp:%d ", payload);
		} else {
			ast_str_append(str, 0, ";");
		}
		ast_str_append(str, 0, "mode-change-capability=%d", attr->mode_change_capability);
		appended = appended + 1;
	}
	if (0 != attr->mode_change_period) {
		if (0 == appended) {
			ast_str_append(str, 0, "a=fmtp:%d ", payload);
		} else {
			ast_str_append(str, 0, ";");
		}
		ast_str_append(str, 0, "mode-change-period=%d", attr->mode_change_period);
		appended = appended + 1;
	}
	if (0 != attr->mode_change_neighbor) {
		if (0 == appended) {
			ast_str_append(str, 0, "a=fmtp:%d ", payload);
		} else {
			ast_str_append(str, 0, ";");
		}
		ast_str_append(str, 0, "mode-change-neighbor=%d", attr->mode_change_neighbor);
		appended = appended + 1;
	}
	if (0 != attr->crc) {
		if (0 == appended) {
			ast_str_append(str, 0, "a=fmtp:%d ", payload);
		} else {
			ast_str_append(str, 0, ";");
		}
		ast_str_append(str, 0, "crc=%d", attr->crc);
		appended = appended + 1;
	}
	if (0 != attr->robust_sorting) {
		if (0 == appended) {
			ast_str_append(str, 0, "a=fmtp:%d ", payload);
		} else {
			ast_str_append(str, 0, ";");
		}
		ast_str_append(str, 0, "robust-sorting=%d", attr->robust_sorting);
		appended = appended + 1;
	}
	if (0 != attr->interleaving) {
		if (0 == appended) {
			ast_str_append(str, 0, "a=fmtp:%d ", payload);
		} else {
			ast_str_append(str, 0, ";");
		}
		ast_str_append(str, 0, "interleaving=%d", attr->interleaving);
		appended = appended + 1;
	}
	if (0 <= attr->max_red) {
		if (0 == appended) {
			ast_str_append(str, 0, "a=fmtp:%d ", payload);
		} else {
			ast_str_append(str, 0, ";");
		}
		ast_str_append(str, 0, "max-red=%d", attr->max_red);
		appended = appended + 1;
	}
	if (0 != appended) {
		ast_str_append(str, 0, "\r\n");
	}
	/* 
	 * AMR-WB, GSM gateway compatible setting:
	 * ast_str_append(str, 0, "a=fmtp:%d mode-set=0,1,2; mode-change-period=2; mode-change-neighbor=1\r\n", payload);
	 * less than 25kb/s in SIP RTP which equals iLBC:30 in data traffic, and AMR-WB is a wide-band codec!
	 */
}

/*
 * Octet Alignment
 *
 * AMR and AMR-WB support 'octet-align=1' which does not stuff the
 * header bits over octet borders and which is readable in
 * Wireshark. This parameter is negotiated via SDP. Belledonne and
 * CounterPath do not offer AMR, only AMR-WB. Android offers only AMR.
 * 
 * Nokia Symbian/S60: configurable via user-interface; default: off
 * Nokia Series 40: configurable via OMA Client Provisioning (OMA CP); default: off
 * Nokia Asha Software Platform: configurable via OMA CP; default: on
 * CounterPath Bria (iOS): no setting known; off
 * CounterPath Bria (Android): see iOS
 * CounterPath Bria (BlackBerry 10): AMR/AMR-WB not available, as of version 1.3.1
 *    Bug: since version 3.4, fmtp is missing = negotiating octet-aligned mode fails
 * BeeHD (iOS): no setting known; on
 *    Bug: Nokia Symbian/S60 calls BeeHD, no audio in BeeHD; AMR-WB works
 * Belledonne Linphone 1.0.2 (Windows Phone 8): no setting known; on
 *    Bug: ignores octet-align=0 in fmtp = distorted audio
 * CSipSimple 1.02r2330 (Android): no setting known; off
 *    Bug: CSipSimple calls a Nokia Symbian/S60, no audio in Nokia; AMR works
 *    Bug: AMR-WB ignores octet-align=1 in fmtp = distorted audio; AMR works
 *         fixed upstream with <https://trac.pjsip.org/repos/changeset/5122>
 * Google Android 5: no setting known; off
 * Join (iOS): no setting known; on
 *    Bug: AMR-WB ignores octet-align=0 in fmtp = distorted audio; AMR works
 * PortGo (iOS): no setting known; off (is able to change alignment through negotiation)
 */
static struct ast_format *amr_getjoint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct amr_attr *attr1 = ast_format_get_attribute_data(format1);
	struct amr_attr *attr2 = ast_format_get_attribute_data(format2);
	struct amr_attr *attr_res;
	struct ast_format *jointformat = NULL;

	if (!attr1) {
		attr1 = &default_amr_attr;
	}

	if (!attr2) {
		attr2 = &default_amr_attr;
	}

	if (format1 == ast_format_amrwb || format1 == ast_format_amr) {
		jointformat = (struct ast_format *) format2;
	}
	if (format2 == ast_format_amrwb || format2 == ast_format_amr) {
		jointformat = (struct ast_format *) format1;
	}
	if (format1 == format2) {
		if (!jointformat) {
			ast_debug(3, "Both formats were not cached but the same.\n");
			jointformat = (struct ast_format *) format1;
		} else {
			ast_debug(3, "Both formats were cached.\n");
			jointformat = NULL;
		}
	}
	if (!jointformat) {
		ast_debug(3, "Which pointer shall be returned? Let us create a new one!\n");
		jointformat = ast_format_clone(format1);
	} else {
		ao2_bump(jointformat);
	}
	if (!jointformat) {
		return NULL;
	}
	attr_res = ast_format_get_attribute_data(jointformat);

	if (0 == attr1->mode_set && 0 == attr2->mode_set) {
		attr_res->mode_set = 0; /* both allowed all = 0 */
	} else if (0 != attr1->mode_set && 0 == attr2->mode_set) {
		attr_res->mode_set = attr1->mode_set; /* attr2 allowed all */
	} else if (0 == attr1->mode_set && 0 != attr2->mode_set) {
		attr_res->mode_set = attr2->mode_set; /* attr1 allowed all */
	} else { /* both parties restrict, let us check if they match */
		attr_res->mode_set = (attr1->mode_set & attr2->mode_set);
		if (0 == attr_res->mode_set) {
			/* not expected because everyone supports 0,1,2 */
			ast_log(LOG_WARNING, "mode-set did not match\n");
			return NULL;
		}
	}
	attr_res->mode_change_period = MAX(attr1->mode_change_period, attr2->mode_change_period);
	attr_res->mode_change_capability = MAX(attr1->mode_change_capability, attr2->mode_change_capability);
	attr_res->mode_change_neighbor = MAX(attr1->mode_change_neighbor, attr2->mode_change_neighbor);
	attr_res->crc = MAX(attr1->crc, attr2->crc);
	attr_res->robust_sorting = MAX(attr1->robust_sorting, attr2->robust_sorting);
	attr_res->interleaving = MAX(attr1->interleaving, attr2->interleaving);
	attr_res->max_red = MAX(attr1->max_red, attr2->max_red);

	/* internal variables for transcoding module */
	/* starting point; later, changes with a change-mode request (CMR) */
	if (0 < attr_res->mode_set) {
		attr_res->mode_current = floor(log10(attr_res->mode_set) / log10(2));
	}
	attr_res->vad = MAX(attr1->vad, attr2->vad);

	return jointformat;
}

static struct ast_format_interface amr_interface = {
	.format_destroy = amr_destroy,
	.format_clone = amr_clone,
	.format_cmp = NULL,
	.format_get_joint = amr_getjoint,
	.format_attribute_set = NULL,
	.format_parse_sdp_fmtp = amr_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = amr_generate_sdp_fmtp,
};

static int load_module(void)
{
	if (ast_format_interface_register("amr", &amr_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_format_interface_register("amrwb", &amr_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER,
	"AMR Format Attribute Module",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
