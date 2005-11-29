/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * General Asterisk channel definitions.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_IMAGE_H
#define _ASTERISK_IMAGE_H

struct ast_imager {
    char *name;					/* Name */
	char *desc;					/* Description */
	char *exts;					/* Extension(s) (separated by '|' ) */
	int format;					/* Image format */
	struct ast_frame *(*read_image)(int fd, int len);	/* Read an image from a file descriptor */
	int (*identify)(int fd);		/* Identify if this is that type of file */
	int (*write_image)(int fd, struct ast_frame *frame); /* Returns length written */
	struct ast_imager *next;
};

/* Returns non-zero if image transmission is supported */
extern int ast_supports_images(struct ast_channel *chan);

/* Sends an image */
extern int ast_send_image(struct ast_channel *chan, char *filename);

/* Make an image from a filename */
extern struct ast_frame *ast_read_image(char *filename, char *preflang, int format);

/* Register an image format */
extern int ast_image_register(struct ast_imager *imgdrv);

extern void ast_image_unregister(struct ast_imager *imgdrv);

/* Initialize image stuff */
extern int ast_image_init(void);

#endif
