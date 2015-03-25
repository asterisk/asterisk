/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief DNS NAPTR Record Support
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/dns_core.h"
#include "asterisk/dns_naptr.h"

const char *ast_dns_naptr_get_flags(const struct ast_dns_record *record)
{
	return NULL;
}

const char *ast_dns_naptr_get_service(const struct ast_dns_record *record)
{
	return NULL;
}

const char *ast_dns_naptr_get_regexp(const struct ast_dns_record *record)
{
	return NULL;
}

const char *ast_dns_naptr_get_replacement(const struct ast_dns_record *record)
{
	return NULL;
}

unsigned short ast_dns_naptr_get_order(const struct ast_dns_record *record)
{
	return 0;
}

unsigned short ast_dns_naptr_get_preference(const struct ast_dns_record *record)
{
	return 0;
}