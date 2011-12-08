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
 */

#ifndef RES_AIS_AIS_H
#define RES_AIS_AIS_H

#include <saAis.h>
#include <saClm.h>
#include <saEvt.h>

extern SaVersionT ais_version;

extern SaClmHandleT  clm_handle;
extern SaEvtHandleT  evt_handle;

int ast_ais_clm_load_module(void);
int ast_ais_clm_unload_module(void);

int ast_ais_evt_load_module(void);
int ast_ais_evt_unload_module(void);

const char *ais_err2str(SaAisErrorT error);

#endif /* RES_AIS_AIS_H */
