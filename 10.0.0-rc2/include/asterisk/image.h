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
 * \brief General Asterisk channel definitions for image handling
 */

#ifndef _ASTERISK_IMAGE_H
#define _ASTERISK_IMAGE_H

/*! \brief structure associated with registering an image format */
struct ast_imager {
	char *name;			/*!< Name */
	char *desc;			/*!< Description */
	char *exts;			/*!< Extension(s) (separated by '|' ) */
	struct ast_format format; /*!< Image format */
	struct ast_frame *(*read_image)(int fd, int len);	/*!< Read an image from a file descriptor */
	int (*identify)(int fd);				/*!< Identify if this is that type of file */
	int (*write_image)(int fd, struct ast_frame *frame);	/*!< Returns length written */
	AST_LIST_ENTRY(ast_imager) list;			/*!< For linked list */
};

/*! 
 * \brief Check for image support on a channel 
 * \param chan channel to check
 * Checks the channel to see if it supports the transmission of images
 * \return non-zero if image transmission is supported
 */
int ast_supports_images(struct ast_channel *chan);

/*! 
 * \brief Sends an image 
 * \param chan channel to send image on
 * \param filename filename of image to send (minus extension)
 * Sends an image on the given channel.
 * \retval 0 on success
 * \retval -1 on error
 */
int ast_send_image(struct ast_channel *chan, const char *filename);

/*! 
 * \brief Make an image 
 * \param filename filename of image to prepare
 * \param preflang preferred language to get the image...?
 * \param format the format of the file, NULL for any image format
 * Make an image from a filename ??? No estoy positivo
 * \retval an ast_frame on success
 * \retval NULL on failure
 */
struct ast_frame *ast_read_image(const char *filename, const char *preflang, struct ast_format *format);

/*! 
 * \brief Register image format
 * \param imgdrv Populated ast_imager structure with info to register
 * Registers an image format
 * \return 0 regardless
 */
int ast_image_register(struct ast_imager *imgdrv);

/*! 
 * \brief Unregister an image format 
 * \param imgdrv pointer to the ast_imager structure you wish to unregister
 * Unregisters the image format passed in.
 * Returns nothing
 */
void ast_image_unregister(struct ast_imager *imgdrv);

/*! 
 * \brief Initialize image stuff
 * Initializes all the various image stuff.  Basically just registers the cli stuff
 * \return 0 all the time
 */
int ast_image_init(void);

#endif /* _ASTERISK_IMAGE_H */
