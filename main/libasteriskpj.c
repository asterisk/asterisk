/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009-2012, Digium, Inc.
 * Copyright (C) 2015, Fairview 5 Engineering, LLC
 *
 * Russell Bryant <russell@digium.com>
 * George Joseph <george.joseph@fairview5.com>
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
 * \brief Loader stub for static pjproject libraries
 *
 * \author George Joseph <george.joseph@fairview5.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#ifdef HAVE_PJPROJECT
#include <pjlib.h>
#endif

#include "asterisk/options.h"
#include "asterisk/_private.h" /* ast_pj_init() */

/*!
 * \internal
 * \brief Initialize static pjproject implementation
 */
int ast_pj_init(void)
{
#ifdef HAVE_PJPROJECT_BUNDLED
	AST_PJPROJECT_INIT_LOG_LEVEL();
	pj_init();
#endif
	return 0;
}
