/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
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

/*! \file
 *
 * \brief JPEG File format support.
 * 
 * \arg File name extension: jpeg, jpg
 * \ingroup formats
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/image.h"
#include "asterisk/endian.h"

static struct ast_frame *jpeg_read_image(int fd, int len)
{
	struct ast_frame fr;
	int res;
	char buf[65536];
	if (len > sizeof(buf) || len < 0) {
		ast_log(LOG_WARNING, "JPEG image too large to read\n");
		return NULL;
	}
	res = read(fd, buf, len);
	if (res < len) {
		ast_log(LOG_WARNING, "Only read %d of %d bytes: %s\n", res, len, strerror(errno));
	}
	memset(&fr, 0, sizeof(fr));
	fr.frametype = AST_FRAME_IMAGE;
	ast_format_set(&fr.subclass.format, AST_FORMAT_JPEG, 0);
	fr.data.ptr = buf;
	fr.src = "JPEG Read";
	fr.datalen = len;
	return ast_frisolate(&fr);
}

static int jpeg_identify(int fd)
{
	char buf[10];
	int res;
	res = read(fd, buf, sizeof(buf));
	if (res < sizeof(buf))
		return 0;
	if (memcmp(buf + 6, "JFIF", 4))
		return 0;
	return 1;
}

static int jpeg_write_image(int fd, struct ast_frame *fr)
{
	int res=0;
	if (fr->frametype != AST_FRAME_IMAGE) {
		ast_log(LOG_WARNING, "Not an image\n");
		return -1;
	}
	if (fr->subclass.format.id != AST_FORMAT_JPEG) {
		ast_log(LOG_WARNING, "Not a jpeg image\n");
		return -1;
	}
	if (fr->datalen) {
		res = write(fd, fr->data.ptr, fr->datalen);
		if (res != fr->datalen) {
			ast_log(LOG_WARNING, "Only wrote %d of %d bytes: %s\n", res, fr->datalen, strerror(errno));
			return -1;
		}
	}
	return res;
}

static struct ast_imager jpeg_format = {
	.name = "jpg",
	.desc = "JPEG (Joint Picture Experts Group)",
	.exts = "jpg|jpeg",
	.read_image = jpeg_read_image,
	.identify = jpeg_identify,
	.write_image = jpeg_write_image,
};

static int load_module(void)
{
	ast_format_set(&jpeg_format.format, AST_FORMAT_JPEG, 0);
	if (ast_image_register(&jpeg_format))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_image_unregister(&jpeg_format);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "jpeg (joint picture experts group) image format",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
