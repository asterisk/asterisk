/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief Usage of the SAForum AIS (Application Interface Specification)
 *
 * \arg http://www.openais.org/
 *
 * This file contains the code specific to the use of the CKPT (Checkpoint)
 * service.
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "ais.h"

#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/logger.h"

SaCkptHandleT ckpt_handle;

static const SaCkptCallbacksT ckpt_callbacks;

int ast_ais_ckpt_load_module(void)
{
	SaAisErrorT ais_res;

	ais_res = saCkptInitialize(&ckpt_handle, &ckpt_callbacks, &ais_version);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Could not initialize CKPT service: %s\n",
			ais_err2str(ais_res));
		return -1;
	}

	return 0;
}

int ast_ais_ckpt_unload_module(void)
{
	SaAisErrorT ais_res;

	ais_res = saCkptFinalize(amf_handle);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Problem stopping CKPT service: %s\n", 
			ais_err2str(ais_res));
		return -1;
	}

	return 0;
}
