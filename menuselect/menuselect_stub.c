/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 - 2006, Russell Bryant
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

/*
 * \file
 *
 * \author Russell Bryant <russell@digium.com>
 * 
 * \brief Menu stub
 */

#include <stdlib.h>
#include <stdio.h>

#include "menuselect.h"

int run_menu(void)
{
	fprintf(stderr, "**************************************************\n");
	fprintf(stderr, "*** Install ncurses to use the menu interface! ***\n");
	fprintf(stderr, "**************************************************\n");

	return -1;
}
