/*
 * Copyright (C) 2006 Voop as
 * Thorsten Lockert <tholo@voop.as>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief SNMP Agent / SubAgent support for Asterisk
 *
 * \author Thorsten Lockert <tholo@voop.as>
 */

/*!
 * \internal
 * \brief Thread running the SNMP Agent or Subagent
 * \param Not used -- required by pthread_create
 * \return A pointer with return status -- not used
 *
 * This represent the main thread of the SNMP [sub]agent, and
 * will initialize SNMP and loop, processing requests until
 * termination is requested by resetting the flag in
 * \ref res_snmp_dontStop.
 */
void	*agent_thread(void *);

/*!
 * \internal
 * Flag saying whether we run as a Subagent or full Agent
 */
extern int res_snmp_agentx_subagent;

/*!
 * \internal
 * Flag stating the agent thread should not terminate
 */
extern int res_snmp_dont_stop;
