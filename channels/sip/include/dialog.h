/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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
 * \brief sip dialog management header file
 */

#include "sip.h"

#ifndef _SIP_DIALOG_H
#define _SIP_DIALOG_H

/*! \brief
 * when we create or delete references, make sure to use these
 * functions so we keep track of the refcounts.
 * To simplify the code, we allow a NULL to be passed to dialog_unref().
 */
#define dialog_ref(dialog, tag) ao2_t_bump(dialog, tag)
#define dialog_unref(dialog, tag) ({ ao2_t_cleanup(dialog, tag); (NULL); })

struct sip_pvt *__sip_alloc(ast_string_field callid, struct ast_sockaddr *sin,
				 int useglobal_nat, const int intended_method, struct sip_request *req, struct ast_callid *logger_callid,
				 const char *file, int line, const char *func);

#define sip_alloc(callid, addr, useglobal_nat, intended_method, req, logger_callid) \
	__sip_alloc(callid, addr, useglobal_nat, intended_method, req, logger_callid, __FILE__, __LINE__, __PRETTY_FUNCTION__)

void sip_scheddestroy_final(struct sip_pvt *p, int ms);
void sip_scheddestroy(struct sip_pvt *p, int ms);
int sip_cancel_destroy(struct sip_pvt *p);

/*! \brief Destroy SIP call structure.
 * Make it return NULL so the caller can do things like
 *	foo = sip_destroy(foo);
 * and reduce the chance of bugs due to dangling pointers.
 */
struct sip_pvt *sip_destroy(struct sip_pvt *p);

/*! \brief Destroy SIP call structure.
 * Make it return NULL so the caller can do things like
 *	foo = sip_destroy(foo);
 * and reduce the chance of bugs due to dangling pointers.
 */
void __sip_destroy(struct sip_pvt *p, int lockowner, int lockdialoglist);
/*!
 * \brief Unlink a dialog from the dialogs container, as well as any other places
 * that it may be currently stored.
 *
 * \note A reference to the dialog must be held before calling
 * this function, and this function does not release that
 * reference.
 *
 * \note The dialog must not be locked when called.
 */
void dialog_unlink_all(struct sip_pvt *dialog);

/*! \brief Acknowledges receipt of a packet and stops retransmission
 * called with p locked*/
int __sip_ack(struct sip_pvt *p, uint32_t seqno, int resp, int sipmethod);

/*! \brief Pretend to ack all packets
 * called with p locked */
void __sip_pretend_ack(struct sip_pvt *p);

/*! \brief Acks receipt of packet, keep it around (used for provisional responses) */
int __sip_semi_ack(struct sip_pvt *p, uint32_t seqno, int resp, int sipmethod);

#endif /* defined(_SIP_DIALOG_H) */
