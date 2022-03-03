/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Martin Pycko <martinp@digium.com>
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
 * \brief Internal Asterisk hangup causes
 */

#ifndef _ASTERISK_CAUSES_H
#define _ASTERISK_CAUSES_H

/*! \page AstCauses Hangup Causes for Asterisk

The Asterisk hangup causes are delivered to the dialplan in the
${HANGUPCAUSE} channel variable after a call (after execution
of "dial").

In SIP, we have a conversion table to convert between SIP
return codes and Q.931 both ways. This is to improve SIP/ISDN
compatibility.

These are the current codes, based on the Q.850/Q.931
specification:

	- AST_CAUSE_UNALLOCATED                      1
	- AST_CAUSE_NO_ROUTE_TRANSIT_NET             2
	- AST_CAUSE_NO_ROUTE_DESTINATION             3
	- AST_CAUSE_MISDIALLED_TRUNK_PREFIX          5
	- AST_CAUSE_CHANNEL_UNACCEPTABLE             6
	- AST_CAUSE_CALL_AWARDED_DELIVERED           7
	- AST_CAUSE_PRE_EMPTED                       8
	- AST_CAUSE_NUMBER_PORTED_NOT_HERE          14
	- AST_CAUSE_NORMAL_CLEARING                 16
	- AST_CAUSE_USER_BUSY                       17
	- AST_CAUSE_NO_USER_RESPONSE                18
	- AST_CAUSE_NO_ANSWER                       19
	- AST_CAUSE_CALL_REJECTED                   21
	- AST_CAUSE_NUMBER_CHANGED                  22
	- AST_CAUSE_REDIRECTED_TO_NEW_DESTINATION   23
	- AST_CAUSE_ANSWERED_ELSEWHERE              26
	- AST_CAUSE_DESTINATION_OUT_OF_ORDER        27
	- AST_CAUSE_INVALID_NUMBER_FORMAT           28
	- AST_CAUSE_FACILITY_REJECTED               29
	- AST_CAUSE_RESPONSE_TO_STATUS_ENQUIRY      30
	- AST_CAUSE_NORMAL_UNSPECIFIED              31
	- AST_CAUSE_NORMAL_CIRCUIT_CONGESTION       34
	- AST_CAUSE_NETWORK_OUT_OF_ORDER            38
	- AST_CAUSE_NORMAL_TEMPORARY_FAILURE        41
	- AST_CAUSE_SWITCH_CONGESTION               42
	- AST_CAUSE_ACCESS_INFO_DISCARDED           43
	- AST_CAUSE_REQUESTED_CHAN_UNAVAIL          44
	- AST_CAUSE_FACILITY_NOT_SUBSCRIBED         50
	- AST_CAUSE_OUTGOING_CALL_BARRED            52
	- AST_CAUSE_INCOMING_CALL_BARRED            54
	- AST_CAUSE_BEARERCAPABILITY_NOTAUTH        57
	- AST_CAUSE_BEARERCAPABILITY_NOTAVAIL       58
	- AST_CAUSE_BEARERCAPABILITY_NOTIMPL        65
	- AST_CAUSE_CHAN_NOT_IMPLEMENTED            66
	- AST_CAUSE_FACILITY_NOT_IMPLEMENTED        69
	- AST_CAUSE_INVALID_CALL_REFERENCE          81
	- AST_CAUSE_INCOMPATIBLE_DESTINATION        88
	- AST_CAUSE_INVALID_MSG_UNSPECIFIED         95
	- AST_CAUSE_MANDATORY_IE_MISSING            96
	- AST_CAUSE_MESSAGE_TYPE_NONEXIST           97
	- AST_CAUSE_WRONG_MESSAGE                   98
	- AST_CAUSE_IE_NONEXIST                     99
	- AST_CAUSE_INVALID_IE_CONTENTS            100
	- AST_CAUSE_WRONG_CALL_STATE               101
	- AST_CAUSE_RECOVERY_ON_TIMER_EXPIRE       102
	- AST_CAUSE_MANDATORY_IE_LENGTH_ERROR      103
	- AST_CAUSE_PROTOCOL_ERROR                 111
	- AST_CAUSE_INTERWORKING                   127

For more information:
- \ref app_dial.c
*/

/*! \name Causes for disconnection (from Q.850/Q.931)
 *  These are the internal cause codes used in Asterisk.
 *  \ref AstCauses
 *
 * @{
 */
#define AST_CAUSE_UNALLOCATED                    1
#define AST_CAUSE_NO_ROUTE_TRANSIT_NET           2
#define AST_CAUSE_NO_ROUTE_DESTINATION           3
#define AST_CAUSE_MISDIALLED_TRUNK_PREFIX        5
#define AST_CAUSE_CHANNEL_UNACCEPTABLE           6
#define AST_CAUSE_CALL_AWARDED_DELIVERED         7
#define AST_CAUSE_PRE_EMPTED                     8
#define AST_CAUSE_NUMBER_PORTED_NOT_HERE        14
#define AST_CAUSE_NORMAL_CLEARING               16
#define AST_CAUSE_USER_BUSY                     17
#define AST_CAUSE_NO_USER_RESPONSE              18
#define AST_CAUSE_NO_ANSWER                     19
#define AST_CAUSE_SUBSCRIBER_ABSENT             20
#define AST_CAUSE_CALL_REJECTED                 21
#define AST_CAUSE_NUMBER_CHANGED                22
#define AST_CAUSE_REDIRECTED_TO_NEW_DESTINATION 23
#define AST_CAUSE_ANSWERED_ELSEWHERE            26
#define AST_CAUSE_DESTINATION_OUT_OF_ORDER      27
#define AST_CAUSE_INVALID_NUMBER_FORMAT         28
#define AST_CAUSE_FACILITY_REJECTED             29
#define AST_CAUSE_RESPONSE_TO_STATUS_ENQUIRY    30
#define AST_CAUSE_NORMAL_UNSPECIFIED            31
#define AST_CAUSE_NORMAL_CIRCUIT_CONGESTION     34
#define AST_CAUSE_NETWORK_OUT_OF_ORDER          38
#define AST_CAUSE_NORMAL_TEMPORARY_FAILURE      41
#define AST_CAUSE_SWITCH_CONGESTION             42
#define AST_CAUSE_ACCESS_INFO_DISCARDED         43
#define AST_CAUSE_REQUESTED_CHAN_UNAVAIL        44
#define AST_CAUSE_FACILITY_NOT_SUBSCRIBED       50
#define AST_CAUSE_OUTGOING_CALL_BARRED          52
#define AST_CAUSE_INCOMING_CALL_BARRED          54
#define AST_CAUSE_BEARERCAPABILITY_NOTAUTH      57
#define AST_CAUSE_BEARERCAPABILITY_NOTAVAIL     58
#define AST_CAUSE_BEARERCAPABILITY_NOTIMPL      65
#define AST_CAUSE_CHAN_NOT_IMPLEMENTED          66
#define AST_CAUSE_FACILITY_NOT_IMPLEMENTED      69
#define AST_CAUSE_INVALID_CALL_REFERENCE        81
#define AST_CAUSE_INCOMPATIBLE_DESTINATION      88
#define AST_CAUSE_INVALID_MSG_UNSPECIFIED       95
#define AST_CAUSE_MANDATORY_IE_MISSING          96
#define AST_CAUSE_MESSAGE_TYPE_NONEXIST         97
#define AST_CAUSE_WRONG_MESSAGE                 98
#define AST_CAUSE_IE_NONEXIST                   99
#define AST_CAUSE_INVALID_IE_CONTENTS          100
#define AST_CAUSE_WRONG_CALL_STATE             101
#define AST_CAUSE_RECOVERY_ON_TIMER_EXPIRE     102
#define AST_CAUSE_MANDATORY_IE_LENGTH_ERROR    103
#define AST_CAUSE_PROTOCOL_ERROR               111
#define AST_CAUSE_INTERWORKING                 127

/* Special Asterisk aliases */
#define AST_CAUSE_BUSY          AST_CAUSE_USER_BUSY
#define AST_CAUSE_FAILURE       AST_CAUSE_NETWORK_OUT_OF_ORDER
#define AST_CAUSE_NORMAL        AST_CAUSE_NORMAL_CLEARING
#define AST_CAUSE_NOANSWER      AST_CAUSE_NO_ANSWER
#define AST_CAUSE_CONGESTION    AST_CAUSE_NORMAL_CIRCUIT_CONGESTION
#define AST_CAUSE_UNREGISTERED  AST_CAUSE_SUBSCRIBER_ABSENT
#define AST_CAUSE_NOTDEFINED    0
#define AST_CAUSE_NOSUCHDRIVER  AST_CAUSE_CHAN_NOT_IMPLEMENTED

/*! @} */

#endif /* _ASTERISK_CAUSES_H */
