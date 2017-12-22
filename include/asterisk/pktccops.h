/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Attila Domjan
 *
 * Attila Domjan <attila.domjan.hu@gmail.com>
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
 * \brief PacketCable COPS
 *
 * \author Attila Domjan <attila.domjan.hu@gmail.com>
 */

#ifndef _ASTERISK_PKTCCOPS_H
#define _ASTERISK_PKTCCOPS_H

#include "asterisk/optional_api.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

enum {
	GATE_SET,
	GATE_INFO,
	GATE_SET_HAVE_GATEID,
	GATE_DEL
};

enum {
	GATE_ALLOC_FAILED,
	GATE_ALLOC_PROGRESS,
	GATE_ALLOCATED,
	GATE_CLOSED,
	GATE_CLOSED_ERR,
	GATE_OPEN,
	GATE_DELETED,
	GATE_TIMEOUT
};

struct cops_gate {
	AST_LIST_ENTRY(cops_gate) list;
	uint32_t gateid;
	uint16_t trid;
	time_t in_transaction;
	uint32_t mta;
	int state;
	time_t allocated;
	time_t checked;
	time_t deltimer;
	struct cops_cmts *cmts;
	int (* got_dq_gi) (struct cops_gate *gate);
	int (* gate_remove) (struct cops_gate *gate);
	int (* gate_open) (struct cops_gate *gate);
	void *tech_pvt;
};


AST_OPTIONAL_API(struct cops_gate *, ast_pktccops_gate_alloc,
	(int cmd,  struct cops_gate *gate, uint32_t mta, uint32_t actcount,
	 float bitrate, uint32_t psize, uint32_t ssip, uint16_t ssport,
	 int (* const got_dq_gi) (struct cops_gate *gate),
	 int (* const gate_remove) (struct cops_gate *gate)),
	{ return NULL; });

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_PKTCCOPS_H */
