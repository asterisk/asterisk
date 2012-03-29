/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2008, Digium, Inc.
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
 * This file contains the common code between the uses of the different AIS
 * services.
 *
 * \note This module is still considered experimental, as it exposes the
 * internal binary format of events between Asterisk servers over a network.
 * However, this format is still subject to change between 1.6.X releases.
 */

/*** MODULEINFO
	<depend>ais</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#include "ais/ais.h"

#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"

static struct {
	pthread_t id;
	unsigned int stop:1;
} dispatch_thread = {
	.id = AST_PTHREADT_NULL,
};

SaVersionT ais_version = { 'B', 1, 1 };

static const struct ais_error {
	SaAisErrorT error;
	const char *desc;
} ais_errors[] = {
        { SA_AIS_OK, "OK" },
        { SA_AIS_ERR_LIBRARY, "Library Error" },
        { SA_AIS_ERR_VERSION, "Version Not Compatible" },
        { SA_AIS_ERR_INIT, "Callback Not Registered" },
        { SA_AIS_ERR_TIMEOUT, "Timeout" },
        { SA_AIS_ERR_TRY_AGAIN , "Try Again" },
        { SA_AIS_ERR_INVALID_PARAM, "Invalid Parameter" },
        { SA_AIS_ERR_NO_MEMORY, "No Memory" },
        { SA_AIS_ERR_BAD_HANDLE, "Invalid Handle" },
        { SA_AIS_ERR_BUSY, "Resource Already In Use" },
        { SA_AIS_ERR_ACCESS, "Access Denied" },
        { SA_AIS_ERR_NOT_EXIST, "Does Not Exist" },
        { SA_AIS_ERR_NAME_TOO_LONG, "Name Too Long" },
        { SA_AIS_ERR_EXIST, "Already Exists" },
        { SA_AIS_ERR_NO_SPACE, "Buffer Too Small" },
        { SA_AIS_ERR_INTERRUPT, "Request Interrupted" },
        { SA_AIS_ERR_NAME_NOT_FOUND, "Name Not Found" },
        { SA_AIS_ERR_NO_RESOURCES, "Not Enough Resources" },
        { SA_AIS_ERR_NOT_SUPPORTED, "Requested Function Not Supported" },
        { SA_AIS_ERR_BAD_OPERATION, "Operation Not Allowed" },
        { SA_AIS_ERR_FAILED_OPERATION, "Operation Failed" },
        { SA_AIS_ERR_MESSAGE_ERROR, "Communication Error" },
        { SA_AIS_ERR_QUEUE_FULL, "Destination Queue Full" },
        { SA_AIS_ERR_QUEUE_NOT_AVAILABLE, "Destination Queue Not Available" },
        { SA_AIS_ERR_BAD_FLAGS, "Invalid Flags" },
        { SA_AIS_ERR_TOO_BIG, "Value Too Large" },
        { SA_AIS_ERR_NO_SECTIONS, "No More Sections to Initialize" },
};

const char *ais_err2str(SaAisErrorT error)
{
	int x;

	for (x = 0; x < ARRAY_LEN(ais_errors); x++) {
		if (ais_errors[x].error == error)
			return ais_errors[x].desc;
	}

	return "Unknown";
}

static void *dispatch_thread_handler(void *data)
{
	SaSelectionObjectT clm_fd, evt_fd;
	int res;
	struct pollfd pfd[2] = { { .events = POLLIN, }, { .events = POLLIN, } };
	SaAisErrorT ais_res;

	ais_res = saClmSelectionObjectGet(clm_handle, &clm_fd);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Failed to retrieve select fd for CLM service.  "
			"This module will not operate.\n");
		return NULL;
	}

	ais_res = saEvtSelectionObjectGet(evt_handle, &evt_fd);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Failed to retrieve select fd for EVT service.  "
			"This module will not operate.\n");
		return NULL;
	}

	pfd[0].fd = clm_fd;
	pfd[1].fd = evt_fd;

	while (!dispatch_thread.stop) {
		pfd[0].revents = 0;
		pfd[1].revents = 0;

		res = ast_poll(pfd, 2, -1);
		if (res == -1 && errno != EINTR && errno != EAGAIN) {
			ast_log(LOG_ERROR, "Select error (%s) dispatch thread going away now, "
				"and the module will no longer operate.\n", strerror(errno));
			break;
		}

		if (pfd[0].revents & POLLIN) {
			saClmDispatch(clm_handle,   SA_DISPATCH_ALL);
		}
		if (pfd[1].revents & POLLIN) {
			saEvtDispatch(evt_handle,   SA_DISPATCH_ALL);
		}
	}

	return NULL;
}

static int load_module(void)
{
	if (ast_ais_clm_load_module())
		goto return_error;

	if (ast_ais_evt_load_module())
		goto evt_failed;

	if (ast_pthread_create_background(&dispatch_thread.id, NULL, 
		dispatch_thread_handler, NULL)) {
		ast_log(LOG_ERROR, "Error starting AIS dispatch thread.\n");
		goto dispatch_failed;
	}

	return AST_MODULE_LOAD_SUCCESS;

dispatch_failed:
	ast_ais_evt_unload_module();
evt_failed:
	ast_ais_clm_unload_module();
return_error:
	return AST_MODULE_LOAD_DECLINE;
}

static int unload_module(void)
{
	ast_ais_clm_unload_module();
	ast_ais_evt_unload_module();

	if (dispatch_thread.id != AST_PTHREADT_NULL) {
		dispatch_thread.stop = 1;
		pthread_kill(dispatch_thread.id, SIGURG); /* poke! */
		pthread_join(dispatch_thread.id, NULL);
	}

	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SAForum AIS");
