/*
 * DNS support
 * 
 * Written by Thorsten Lockert <tholo@trollphone.org>
 *
 * Funding provided by Troll Phone Networks AS
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_DNS_H
#define _ASTERISK_DNS_H

struct ast_channel;

extern int ast_search_dns(void *context, const char *dname, int class, int type,
			  int (*callback)(void *context, u_char *answer, int len, u_char *fullanswer));

#endif /* _ASTERISK_DNS_H */
