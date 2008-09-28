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
 * This file contains the code specific to the use of the CLM 
 * (Cluster Membership) Service.
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

SaClmHandleT clm_handle;

static void clm_node_get_cb(SaInvocationT invocation, 
	const SaClmClusterNodeT *cluster_node, SaAisErrorT error);
static void clm_track_cb(const SaClmClusterNotificationBufferT *notif_buffer,
	SaUint32T num_members, SaAisErrorT error);

static const SaClmCallbacksT clm_callbacks = {
	.saClmClusterNodeGetCallback = clm_node_get_cb,
	.saClmClusterTrackCallback   = clm_track_cb,
};

static void clm_node_get_cb(SaInvocationT invocation, 
	const SaClmClusterNodeT *cluster_node, SaAisErrorT error)
{

}

static void clm_track_cb(const SaClmClusterNotificationBufferT *notif_buffer,
	SaUint32T num_members, SaAisErrorT error)
{

}

static char *ais_clm_show_members(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;
	SaClmClusterNotificationBufferT buf;
	SaClmClusterNotificationT notif[64];
	SaAisErrorT ais_res;

	switch (cmd) {
	case CLI_INIT:
		e->command = "ais show clm members";
		e->usage =
			"Usage: ais show clm members\n"
			"       List members of the cluster using the CLM (Cluster Membership) service.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	buf.notification = notif;
	buf.numberOfItems = ARRAY_LEN(notif);

	ais_res = saClmClusterTrack(clm_handle, SA_TRACK_CURRENT, &buf);
	if (ais_res != SA_AIS_OK) {
		ast_cli(a->fd, "Error retrieving current cluster members.\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "\n"
	            "=============================================================\n"
	            "=== Cluster Members =========================================\n"
	            "=============================================================\n"
	            "===\n");

	for (i = 0; i < buf.numberOfItems; i++) {
		SaClmClusterNodeT *node = &buf.notification[i].clusterNode;

		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
		               "=== Node Name: %s\n"
		               "=== ==> ID: 0x%x\n"
		               "=== ==> Address: %s\n"
		               "=== ==> Member: %s\n",
		               (char *) node->nodeName.value, (int) node->nodeId, 
		               (char *) node->nodeAddress.value,
		               node->member ? "Yes" : "No");

		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
		               "===\n");
	}

	ast_cli(a->fd, "=============================================================\n"
	               "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry ais_cli[] = {
	AST_CLI_DEFINE(ais_clm_show_members, "List current members of the cluster"),
};

int ast_ais_clm_load_module(void)
{
	SaAisErrorT ais_res;

	ais_res = saClmInitialize(&clm_handle, &clm_callbacks, &ais_version);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Could not initialize cluster membership service: %s\n",
			ais_err2str(ais_res));
		return -1;
	}

	ast_cli_register_multiple(ais_cli, ARRAY_LEN(ais_cli));

	return 0;
}

int ast_ais_clm_unload_module(void)
{
	SaAisErrorT ais_res;

	ast_cli_unregister_multiple(ais_cli, ARRAY_LEN(ais_cli));

	ais_res = saClmFinalize(clm_handle);
	if (ais_res != SA_AIS_OK) {
		ast_log(LOG_ERROR, "Problem stopping cluster membership service: %s\n", 
			ais_err2str(ais_res));
		return -1;
	}

	return 0;
}
