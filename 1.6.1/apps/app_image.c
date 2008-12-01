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
 * \brief App to transmit an image
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/image.h"

static char *app = "SendImage";

static char *synopsis = "Send an image file";

static char *descrip = 
"  SendImage(filename): Sends an image on a channel.\n"
"Result of transmission will be stored in SENDIMAGESTATUS\n"
"channel variable:\n"
"    SUCCESS      Transmission succeeded\n"
"    FAILURE      Transmission failed\n"
"    UNSUPPORTED  Image transmission not supported by channel\n";


static int sendimage_exec(struct ast_channel *chan, void *data)
{

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SendImage requires an argument (filename)\n");
		return -1;
	}

	if (!ast_supports_images(chan)) {
		/* Does not support transport */
		pbx_builtin_setvar_helper(chan, "SENDIMAGESTATUS", "UNSUPPORTED");
		return 0;
	}

	if (!ast_send_image(chan, data)) {
		pbx_builtin_setvar_helper(chan, "SENDIMAGESTATUS", "SUCCESS");
	} else {
		pbx_builtin_setvar_helper(chan, "SENDIMAGESTATUS", "FAILURE");
	}
	
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application(app, sendimage_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Image Transmission Application");
