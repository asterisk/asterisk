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

/*! structure associated with registering an image format */
struct ast_imager {
	/*! Name */
	char *name;						
	/*! Description */
	char *desc;						
	/*! Extension(s) (separated by '|' ) */
	char *exts;						
	/*! Image format */
	int format;						
	/*! Read an image from a file descriptor */
	struct ast_frame *(*read_image)(int fd, int len);	
	/*! Identify if this is that type of file */
	int (*identify)(int fd);				
	/*! Returns length written */
	int (*write_image)(int fd, struct ast_frame *frame); 	
	/*! For linked list */
	struct ast_imager *next;
};

/*! Check for image support on a channel */
/*! 
 * \param chan channel to check
 * Checks the channel to see if it supports the transmission of images
 * Returns non-zero if image transmission is supported
 */
extern int ast_supports_images(struct ast_channel *chan);

/*! Sends an image */
/*!
 * \param chan channel to send image on
 * \param filename filename of image to send (minus extension)
 * Sends an image on the given channel.
 * Returns 0 on success, -1 on error
 */
extern int ast_send_image(struct ast_channel *chan, char *filename);

/*! Make an image */
/*! 
 * \param filename filename of image to prepare
 * \param preflang preferred language to get the image...?
 * \param format the format of the file
 * Make an image from a filename ??? No estoy positivo
 * Returns an ast_frame on success, NULL on failure
 */
extern struct ast_frame *ast_read_image(char *filename, char *preflang, int format);

/*! Register image format */
/*! 
 * \param imgdrv Populated ast_imager structure with info to register
 * Registers an image format
 * Returns 0 regardless
 */
extern int ast_image_register(struct ast_imager *imgdrv);

/*! Unregister an image format */
/*!
 * \param imgdrv pointer to the ast_imager structure you wish to unregister
 * Unregisters the image format passed in
 * Returns nothing
 */
extern void ast_image_unregister(struct ast_imager *imgdrv);

/*! Initialize image stuff */
/*!
 * Initializes all the various image stuff.  Basically just registers the cli stuff
 * Returns 0 all the time
 */
extern int ast_image_init(void);

#endif
