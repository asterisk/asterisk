/*
 * ast_h323.h
 *
 * OpenH323 Channel Driver for ASTERISK PBX.
 *			By Jeremy McNamara
 *                      For The NuFone Network 
 * 
 * This code has been derived from code created by
 *              Michael Manousos and Mark Spencer
 *
 * This file is part of the chan_h323 driver for Asterisk
 *
 * chan_h323 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * chan_h323 is distributed WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Version Info: $Id$
 */

#ifndef AST_H323_H
#define AST_H323_H

#include "ast_ptlib.h"

#define VERSION(a,b,c) ((a)*10000+(b)*100+(c))

class MyH323EndPoint : public H323EndPoint
{
	PCLASSINFO(MyH323EndPoint, H323EndPoint);

public:
	MyH323EndPoint();
	int MyMakeCall(const PString &, PString &, void *_callReference, void *_opts);
	PBoolean ClearCall(const PString &, H323Connection::CallEndReason reason);
	PBoolean ClearCall(const PString &);

	void OnClosedLogicalChannel(H323Connection &, const H323Channel &);
	void OnConnectionEstablished(H323Connection &, const PString &);
	void OnConnectionCleared(H323Connection &, const PString &);
	virtual H323Connection * CreateConnection(unsigned, void *, H323Transport *, H323SignalPDU *);
	void SendUserTone(const PString &, char);
	PBoolean OnConnectionForwarded(H323Connection &, const PString &, const H323SignalPDU &);
	PBoolean ForwardConnection(H323Connection &, const PString &, const H323SignalPDU &);
	void SetEndpointTypeInfo( H225_EndpointType & info ) const;
	void SetGateway(void);
	PStringArray SupportedPrefixes;
};

class MyH323Connection : public H323Connection
{
	PCLASSINFO(MyH323Connection, H323Connection);

public:
	MyH323Connection(MyH323EndPoint &, unsigned, unsigned);
	~MyH323Connection();
	H323Channel * CreateRealTimeLogicalChannel(const H323Capability &,
			H323Channel::Directions,
			unsigned,
			const H245_H2250LogicalChannelParameters *,
			RTP_QOS *);
	H323Connection::AnswerCallResponse OnAnswerCall(const PString &,
			const H323SignalPDU &,
			H323SignalPDU &);
	void OnReceivedReleaseComplete(const H323SignalPDU &);
	PBoolean OnAlerting(const H323SignalPDU &, const PString &);
	PBoolean OnSendReleaseComplete(H323SignalPDU &);
	PBoolean OnReceivedSignalSetup(const H323SignalPDU &);
	PBoolean OnReceivedFacility(const H323SignalPDU &);
	PBoolean OnSendSignalSetup(H323SignalPDU &);
	PBoolean OnStartLogicalChannel(H323Channel &);
	PBoolean OnClosingLogicalChannel(H323Channel &);
	virtual void SendUserInputTone(char tone, unsigned duration = 0, unsigned logicalChannel = 0, unsigned rtpTimestamp = 0);
	virtual void OnUserInputTone(char, unsigned, unsigned, unsigned);
	virtual void OnUserInputString(const PString &value);
	PBoolean OnReceivedProgress(const H323SignalPDU &);
	PBoolean MySendProgress();
	void OnSendCapabilitySet(H245_TerminalCapabilitySet &);
	void OnSetLocalCapabilities();
	void SetCapabilities(int, int, void *, int);
	PBoolean OnReceivedCapabilitySet(const H323Capabilities &, const H245_MultiplexCapability *,
			H245_TerminalCapabilitySetReject &);
	void SetCause(int _cause) { cause = _cause; };
	virtual PBoolean StartControlChannel(const H225_TransportAddress & h245Address);
	void SetCallOptions(void *opts, PBoolean isIncoming);
	void SetCallDetails(void *callDetails, const H323SignalPDU &setupPDU, PBoolean isIncoming);
	virtual H323Connection::CallEndReason SendSignalSetup(const PString&, const H323TransportAddress&);
#ifdef TUNNELLING
	virtual PBoolean HandleSignalPDU(H323SignalPDU &pdu);
	PBoolean EmbedTunneledInfo(H323SignalPDU &pdu);
#endif
#ifdef H323_H450
	virtual void OnReceivedLocalCallHold(int linkedId);
	virtual void OnReceivedLocalCallRetrieve(int linkedId);
#endif
	void MyHoldCall(BOOL localHold);

	PString sourceAliases;
	PString destAliases;
	PString sourceE164;
	PString destE164;
	int cid_presentation;
	int cid_ton;
	PString rdnis;
	int redirect_reason;
	int transfer_capability;

	WORD sessionId;
	PBoolean bridging;
#ifdef TUNNELLING
	int remoteTunnelOptions;
	int tunnelOptions;
#endif

	unsigned holdHandling;
	unsigned progressSetup;
	unsigned progressAlert;
	int cause;

	RTP_DataFrame::PayloadTypes dtmfCodec[2];
	int dtmfMode;
};

class MyH323_ExternalRTPChannel : public H323_ExternalRTPChannel
{
	PCLASSINFO(MyH323_ExternalRTPChannel, H323_ExternalRTPChannel);

public:
	MyH323_ExternalRTPChannel(
			MyH323Connection & connection,
			const H323Capability & capability,
			Directions direction,
			unsigned sessionID);

	~MyH323_ExternalRTPChannel();

	/* Overrides */
	PBoolean Start(void);
	PBoolean OnReceivedAckPDU(const H245_H2250LogicalChannelAckParameters & param);

protected:
	BYTE payloadCode;

	PIPSocket::Address localIpAddr;
	PIPSocket::Address remoteIpAddr;
	/* Additional functions in order to have chan_h323 compile with H323Plus */
#if VERSION(OPENH323_MAJOR, OPENH323_MINOR, OPENH323_BUILD) > VERSION(1,19,4)
	BOOL OnReceivedAltPDU(const H245_ArrayOf_GenericInformation & alternate );
	BOOL OnSendingAltPDU(H245_ArrayOf_GenericInformation & alternate) const;
	void OnSendOpenAckAlt(H245_ArrayOf_GenericInformation & alternate) const;
	BOOL OnReceivedAckAltPDU(const H245_ArrayOf_GenericInformation & alternate);
#endif
	WORD localPort;
	WORD remotePort;
};

#ifdef H323_H450

#if VERSION(OPENH323_MAJOR, OPENH323_MINOR, OPENH323_BUILD) > VERSION(1,19,4)
#include <h450/h450pdu.h>
#else
#include <h450pdu.h>
#endif

class MyH4504Handler : public H4504Handler
{
	PCLASSINFO(MyH4504Handler, H4504Handler);

public:
	MyH4504Handler(MyH323Connection &_conn, H450xDispatcher &_disp);
	virtual void OnReceivedLocalCallHold(int linkedId);
	virtual void OnReceivedLocalCallRetrieve(int linkedId);

private:
	MyH323Connection *conn;
};
#endif

#endif /* !defined AST_H323_H */
