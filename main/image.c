/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Image Management
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/time.h>
#include <sys/stat.h>
#include <signal.h>

#include "asterisk/paths.h"	/* use ast_config_AST_DATA_DIR */
#include "asterisk/sched.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/image.h"
#include "asterisk/translate.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"

/* XXX Why don't we just use the formats struct for this? */
static AST_RWLIST_HEAD_STATIC(imagers, ast_imager);

int ast_image_register(struct ast_imager *img)
{
	AST_RWLIST_WRLOCK(&imagers);
	AST_RWLIST_INSERT_HEAD(&imagers, img, list);
	AST_RWLIST_UNLOCK(&imagers);
	ast_verb(2, "Registered format '%s' (%s)\n", img->name, img->desc);
	return 0;
}

void ast_image_unregister(struct ast_imager *img)
{
	AST_RWLIST_WRLOCK(&imagers);
	img = AST_RWLIST_REMOVE(&imagers, img, list);
	AST_RWLIST_UNLOCK(&imagers);

	if (img)
		ast_verb(2, "Unregistered format '%s' (%s)\n", img->name, img->desc);
}

int ast_supports_images(struct ast_channel *chan)
{
	if (!chan || !chan->tech)
		return 0;
	if (!chan->tech->send_image)
		return 0;
	return 1;
}

static int file_exists(char *filename)
{
	int res;
	struct stat st;
	res = stat(filename, &st);
	if (!res)
		return st.st_size;
	return 0;
}

static void make_filename(char *buf, int len, const char *filename, const char *preflang, char *ext)
{
	if (filename[0] == '/') {
		if (!ast_strlen_zero(preflang))
			snprintf(buf, len, "%s-%s.%s", filename, preflang, ext);
		else
			snprintf(buf, len, "%s.%s", filename, ext);
	} else {
		if (!ast_strlen_zero(preflang))
			snprintf(buf, len, "%s/%s/%s-%s.%s", ast_config_AST_DATA_DIR, "images", filename, preflang, ext);
		else
			snprintf(buf, len, "%s/%s/%s.%s", ast_config_AST_DATA_DIR, "images", filename, ext);
	}
}

struct ast_frame *ast_read_image(const char *filename, const char *preflang, struct ast_format *format)
{
	struct ast_imager *i;
	char buf[256];
	char tmp[80];
	char *e;
	struct ast_imager *found = NULL;
	int fd;
	int len=0;
	struct ast_frame *f = NULL;
	
	AST_RWLIST_RDLOCK(&imagers);
	AST_RWLIST_TRAVERSE(&imagers, i, list) {
		/* if NULL image format, just pick the first one, otherwise match it. */
		if (!format || (ast_format_cmp(&i->format, format) == AST_FORMAT_CMP_EQUAL)) {
			char *stringp=NULL;
			ast_copy_string(tmp, i->exts, sizeof(tmp));
			stringp = tmp;
			e = strsep(&stringp, "|");
			while (e) {
				make_filename(buf, sizeof(buf), filename, preflang, e);
				if ((len = file_exists(buf))) {
					found = i;
					break;
				}
				make_filename(buf, sizeof(buf), filename, NULL, e);
				if ((len = file_exists(buf))) {
					found = i;
					break;
				}
				e = strsep(&stringp, "|");
			}
		}
		if (found)
			break;	
	}

	if (found) {
		fd = open(buf, O_RDONLY);
		if (fd > -1) {
			if (!found->identify || found->identify(fd)) {
				/* Reset file pointer */
				lseek(fd, 0, SEEK_SET);
				f = found->read_image(fd, len); 
			} else
				ast_log(LOG_WARNING, "%s does not appear to be a %s file\n", buf, found->name);
			close(fd);
		} else
			ast_log(LOG_WARNING, "Unable to open '%s': %s\n", buf, strerror(errno));
	} else
		ast_log(LOG_WARNING, "Image file '%s' not found\n", filename);
	
	AST_RWLIST_UNLOCK(&imagers);
	
	return f;
}

int ast_send_image(struct ast_channel *chan, const char *filename)
{
	struct ast_frame *f;
	int res = -1;
	if (chan->tech->send_image) {
		f = ast_read_image(filename, chan->language, NULL);
		if (f) {
			res = chan->tech->send_image(chan, f);
			ast_frfree(f);
		}
	}
	return res;
}

static char *handle_core_show_image_formats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%10s %10s %50s %10s\n"
#define FORMAT2 "%10s %10s %50s %10s\n"
	struct ast_imager *i;
	int count_fmt = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show image formats";
		e->usage =
			"Usage: core show image formats\n"
			"       Displays currently registered image formats (if any).\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 4)
		return CLI_SHOWUSAGE;
	ast_cli(a->fd, FORMAT, "Name", "Extensions", "Description", "Format");
	ast_cli(a->fd, FORMAT, "----", "----------", "-----------", "------");
	AST_RWLIST_RDLOCK(&imagers);
	AST_RWLIST_TRAVERSE(&imagers, i, list) {
		ast_cli(a->fd, FORMAT2, i->name, i->exts, i->desc, ast_getformatname(&i->format));
		count_fmt++;
	}
	AST_RWLIST_UNLOCK(&imagers);
	ast_cli(a->fd, "\n%d image format%s registered.\n", count_fmt, count_fmt == 1 ? "" : "s");
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_image[] = {
	AST_CLI_DEFINE(handle_core_show_image_formats, "Displays image formats")
};

int ast_image_init(void)
{
	ast_cli_register_multiple(cli_image, ARRAY_LEN(cli_image));
	return 0;
}
