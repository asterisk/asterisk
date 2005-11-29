/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Channel Management
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/sched.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/file.h"
#include "asterisk/image.h"
#include "asterisk/translate.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"

static struct ast_imager *list;
AST_MUTEX_DEFINE_STATIC(listlock);

int ast_image_register(struct ast_imager *img)
{
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Registered format '%s' (%s)\n", img->name, img->desc);
	ast_mutex_lock(&listlock);
	img->next = list;
	list = img;
	ast_mutex_unlock(&listlock);
	return 0;
}

void ast_image_unregister(struct ast_imager *img)
{
	struct ast_imager *i, *prev = NULL;
	ast_mutex_lock(&listlock);
	i = list;
	while(i) {
		if (i == img) {
			if (prev) 
				prev->next = i->next;
			else
				list = i->next;
			break;
		}
		prev = i;
		i = i->next;
	}
	ast_mutex_unlock(&listlock);
	if (i && (option_verbose > 1))
		ast_verbose(VERBOSE_PREFIX_2 "Unregistered format '%s' (%s)\n", img->name, img->desc);
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

static void make_filename(char *buf, int len, char *filename, char *preflang, char *ext)
{
	if (filename[0] == '/') {
		if (preflang && strlen(preflang))
			snprintf(buf, len, "%s-%s.%s", filename, preflang, ext);
		else
			snprintf(buf, len, "%s.%s", filename, ext);
	} else {
		if (preflang && strlen(preflang))
			snprintf(buf, len, "%s/%s/%s-%s.%s", ast_config_AST_VAR_DIR, "images", filename, preflang, ext);
		else
			snprintf(buf, len, "%s/%s/%s.%s", ast_config_AST_VAR_DIR, "images", filename, ext);
	}
}

struct ast_frame *ast_read_image(char *filename, char *preflang, int format)
{
	struct ast_imager *i;
	char buf[256];
	char tmp[80];
	char *e;
	struct ast_imager *found = NULL;
	int fd;
	int len=0;
	struct ast_frame *f = NULL;
#if 0 /* We need to have some sort of read-only lock */
	ast_mutex_lock(&listlock);
#endif	
	i = list;
	while(!found && i) {
		if (i->format & format) {
			char *stringp=NULL;
			strncpy(tmp, i->exts, sizeof(tmp)-1);
			stringp=tmp;
			e = strsep(&stringp, "|");
			while(e) {
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
		i = i->next;
	}
	if (found) {
		fd = open(buf, O_RDONLY);
		if (fd > -1) {
			if (!found->identify || found->identify(fd)) {
				/* Reset file pointer */
				lseek(fd, 0, SEEK_SET);
				f = found->read_image(fd,len); 
			} else
				ast_log(LOG_WARNING, "%s does not appear to be a %s file\n", buf, i->name);
			close(fd);
		} else
			ast_log(LOG_WARNING, "Unable to open '%s': %s\n", buf, strerror(errno));
	} else
		ast_log(LOG_WARNING, "Image file '%s' not found\n", filename);
#if 0
	ast_mutex_unlock(&listlock);
#endif	
	return f;
}


int ast_send_image(struct ast_channel *chan, char *filename)
{
	struct ast_frame *f;
	int res = -1;
	if (chan->tech->send_image) {
		f = ast_read_image(filename, chan->language, -1);
		if (f) {
			res = chan->tech->send_image(chan, f);
			ast_frfree(f);
		}
	}
	return res;
}

static int show_image_formats(int fd, int argc, char *argv[])
{
#define FORMAT "%10s %10s %50s %10s\n"
#define FORMAT2 "%10s %10s %50s %10s\n"
	struct ast_imager *i;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, FORMAT, "Name", "Extensions", "Description", "Format");
	i = list;
	while(i) {
		ast_cli(fd, FORMAT2, i->name, i->exts, i->desc, ast_getformatname(i->format));
		i = i->next;
	};
	return RESULT_SUCCESS;
}

struct ast_cli_entry show_images =
{
	{ "show", "image", "formats" },
	show_image_formats,
	"Displays image formats",
"Usage: show image formats\n"
"       displays currently registered image formats (if any)\n"
};


int ast_image_init(void)
{
	ast_cli_register(&show_images);
	return 0;
}

