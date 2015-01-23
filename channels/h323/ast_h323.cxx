#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/*
 * ast_h323.cpp
 *
 * OpenH323 Channel Driver for ASTERISK PBX.
 *			By  Jeremy McNamara
 *			For The NuFone Network
 *
 * chan_h323 has been derived from code created by
 *               Michael Manousos and Mark Spencer
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

#define VERSION(a,b,c) ((a)*10000+(b)*100+(c))

#include <arpa/inet.h>

#include <list>
#include <string>
#include <algorithm>

#include <ptlib.h>
#include <h323.h>
#include <h323pdu.h>
#include <h323neg.h>
#include <mediafmt.h>

/* H323 Plus */
#if VERSION(OPENH323_MAJOR, OPENH323_MINOR, OPENH323_BUILD) > VERSION(1,19,4)

#ifdef H323_H450
#include "h450/h4501.h"
#include "h450/h4504.h"
#include "h450/h45011.h"
#include "h450/h450pdu.h"
#endif

#ifdef H323_H460
#include <h460/h4601.h>
#endif

#else /* !H323 Plus */

#include <lid.h>
#ifdef H323_H450
#include "h4501.h"
#include "h4504.h"
#include "h45011.h"
#include "h450pdu.h"
#endif

#endif /* H323 Plus */

#include "compat_h323.h"

#include "asterisk.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "asterisk/compat.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/astobj.h"
#ifdef __cplusplus
}
#endif

#undef open
#undef close

#include "chan_h323.h"
#include "ast_h323.h"
#include "cisco-h225.h"
#include "caps_h323.h"

/* PWLIB_MAJOR renamed to PTLIB_MAJOR in 2.x.x */
#if (defined(PTLIB_MAJOR) || VERSION(PWLIB_MAJOR, PWLIB_MINOR, PWLIB_BUILD) >= VERSION(1,12,0))
#define SKIP_PWLIB_PIPE_BUG_WORKAROUND 1
#endif

///////////////////////////////////////////////
/* We have to have a PProcess running for the life of the instance to give
 * h323plus a static instance of PProcess to get system information.
 * This class is defined with PDECLARE_PROCESS().  See pprocess.h from pwlib.
 */

/* PWlib Required Components  */
#if VERSION(OPENH323_MAJOR, OPENH323_MINOR, OPENH323_BUILD) > VERSION(1,19,4)
#define MAJOR_VERSION 1
#define MINOR_VERSION 19
#define BUILD_TYPE    ReleaseCode
#define BUILD_NUMBER  6
#else
#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define BUILD_TYPE    ReleaseCode
#define BUILD_NUMBER  0
#endif
 
const char *h323manufact = "The NuFone Networks";
const char *h323product  = "H.323 Channel Driver for Asterisk";
 
PDECLARE_PROCESS(MyProcess,PProcess,h323manufact,h323product,MAJOR_VERSION,MINOR_VERSION,BUILD_TYPE,BUILD_NUMBER)
static MyProcess localProcess;  // active for the life of the DLL
/* void MyProcess::Main()
{
}
*/
////////////////////////////////////////////////


/** Counter for the number of connections */
static int channelsOpen;

/**
 * We assume that only one endPoint should exist.
 * The application cannot run the h323_end_point_create() more than once
 * FIXME: Singleton this, for safety
 */
static MyH323EndPoint *endPoint = NULL;

#ifndef SKIP_PWLIB_PIPE_BUG_WORKAROUND
static int _timerChangePipe[2];
#endif

static unsigned traceOptions = PTrace::Timestamp | PTrace::Thread | PTrace::FileAndLine;

class PAsteriskLog : public PObject, public iostream {
	PCLASSINFO(PAsteriskLog, PObject);

	public:
	PAsteriskLog() : iostream(cout.rdbuf()) { init(&buffer); }
	~PAsteriskLog() { flush(); }

	private:
	PAsteriskLog(const PAsteriskLog &) : iostream(cout.rdbuf()) { }
	PAsteriskLog & operator=(const PAsteriskLog &) { return *this; }

	class Buffer : public streambuf {
		public:
		virtual int overflow(int=EOF);
		virtual int underflow();
		virtual int sync();
		PString string;
	} buffer;
	friend class Buffer;
};

static PAsteriskLog *logstream = NULL;

int PAsteriskLog::Buffer::overflow(int c)
{
	if (pptr() >= epptr()) {
		int ppos = pptr() - pbase();
		char *newptr = string.GetPointer(string.GetSize() + 2000);
		setp(newptr, newptr + string.GetSize() - 1);
		pbump(ppos);
	}
	if (c != EOF) {
		*pptr() = (char)c;
		pbump(1);
	}
	return 0;
}

int PAsteriskLog::Buffer::underflow()
{
	return EOF;
}

int PAsteriskLog::Buffer::sync()
{
	char *str = ast_strdup(string);
	char *s, *s1;
	char c;

	/* Pass each line with different ast_verbose() call */
	for (s = str; s && *s; s = s1) {
		s1 = strchr(s, '\n');
		if (!s1)
			s1 = s + strlen(s);
		else
			s1++;
		c = *s1;
		*s1 = '\0';
		ast_verbose("%s", s);
		*s1 = c;
	}
	ast_free(str);

	string = PString();
	char *base = string.GetPointer(2000);
	setp(base, base + string.GetSize() - 1);
	return 0;
}

static ostream &my_endl(ostream &os)
{
	if (logstream) {
		PTrace::SetOptions(traceOptions);
		return PTrace::End(os);
	}
	return endl(os);
}

#define cout \
	(logstream ? (PTrace::ClearOptions((unsigned)-1), PTrace::Begin(0, __FILE__, __LINE__)) : std::cout)
#define endl my_endl

void MyProcess::Main()
{
	PTrace::Initialise(PTrace::GetLevel(), NULL, traceOptions);
	PTrace::SetStream(logstream);

	cout << "  == Creating H.323 Endpoint" << endl;
	if (endPoint) {
		cout << "  == ENDPOINT ALREADY CREATED" << endl;
		return;
	}
	endPoint = new MyH323EndPoint();
	/* Due to a bug in the H.323 recomendation/stack we should request a sane
	   amount of bandwidth from the GK - this function is ignored if not using a GK
	   We are requesting 128 (64k in each direction), which is the worst case codec. */
	endPoint->SetInitialBandwidth(1280);
}

void PAssertFunc(const char *msg)
{
	ast_log(LOG_ERROR, "%s\n", msg);
	/* XXX: Probably we need to crash here */
}


/** MyH323EndPoint
  */
MyH323EndPoint::MyH323EndPoint()
		: H323EndPoint()
{
	/* Capabilities will be negotiated on per-connection basis */
	capabilities.RemoveAll();

	/* Reset call setup timeout to some more reasonable value than 1 minute */
	signallingChannelCallTimeout = PTimeInterval(0, 0, 10);	/* 10 minutes */
}

/** The fullAddress parameter is used directly in the MakeCall method so
  * the General form for the fullAddress argument is :
  * [alias@][transport$]host[:port]
  * default values:	alias = the same value as host.
  *					transport = ip.
  *					port = 1720.
  */
int MyH323EndPoint::MyMakeCall(const PString & dest, PString & token, void *_callReference, void *_opts)
{
	PString fullAddress;
	MyH323Connection * connection;
	H323Transport *transport = NULL;
	unsigned int *callReference = (unsigned int *)_callReference;
	call_options_t *opts = (call_options_t *)_opts;

	/* Determine whether we are using a gatekeeper or not. */
	if (GetGatekeeper()) {
		fullAddress = dest;
		if (h323debug) {
			cout << " -- Making call to " << fullAddress << " using gatekeeper." << endl;
		}
	} else {
		fullAddress = dest;
		if (h323debug) {
			cout << " -- Making call to " << fullAddress << " without gatekeeper." << endl;
		}
		/* Use bindaddr for outgoing calls too if we don't use gatekeeper */
		if (listeners.GetSize() > 0) {
			H323TransportAddress taddr = listeners[0].GetTransportAddress();
			PIPSocket::Address addr;
			WORD port;
			if (taddr.GetIpAndPort(addr, port)) {
				/* Create own transport for specific addresses only */
				if (addr) {
					if (h323debug)
						cout << "Using " << addr << " for outbound call" << endl;
					transport = new MyH323TransportTCP(*this, addr);
					if (!transport)
						cout << "Unable to create transport for outgoing call" << endl;
				}
			} else
				cout << "Unable to get address and port" << endl;
		}
	}
	if (!(connection = (MyH323Connection *)H323EndPoint::MakeCallLocked(fullAddress, token, opts, transport))) {
		if (h323debug) {
			cout << "Error making call to \"" << fullAddress << '"' << endl;
		}
		return 1;
	}
	*callReference = connection->GetCallReference();

	if (h323debug) {
		cout << "\t-- " << GetLocalUserName() << " is calling host " << fullAddress << endl;
		cout << "\t-- Call token is " << (const char *)token << endl;
		cout << "\t-- Call reference is " << *callReference << endl;
#ifdef PTRACING
		cout << "\t-- DTMF Payload is " << connection->dtmfCodec << endl;
#endif
	}
	connection->Unlock();
	return 0;
}

void MyH323EndPoint::SetEndpointTypeInfo( H225_EndpointType & info ) const
{
	H323EndPoint::SetEndpointTypeInfo(info);

	if (terminalType == e_GatewayOnly){
		info.RemoveOptionalField(H225_EndpointType::e_terminal);
		info.IncludeOptionalField(H225_EndpointType::e_gateway);
	}

	info.m_gateway.IncludeOptionalField(H225_GatewayInfo::e_protocol);
	info.m_gateway.m_protocol.SetSize(1);
	H225_SupportedProtocols &protocol=info.m_gateway.m_protocol[0];
	protocol.SetTag(H225_SupportedProtocols::e_voice);
	PINDEX as=SupportedPrefixes.GetSize();
	((H225_VoiceCaps &)protocol).m_supportedPrefixes.SetSize(as);
	for (PINDEX p=0; p<as; p++) {
		H323SetAliasAddress(SupportedPrefixes[p], ((H225_VoiceCaps &)protocol).m_supportedPrefixes[p].m_prefix, H225_AliasAddress::e_dialedDigits);
	}
}

void MyH323EndPoint::SetGateway(void)
{
	terminalType = e_GatewayOnly;
}

PBoolean MyH323EndPoint::ClearCall(const PString & token, H323Connection::CallEndReason reason)
{
	if (h323debug) {
#ifdef PTRACING
		cout << "\t-- ClearCall: Request to clear call with token " << token << ", cause " << reason << endl;
#else
		cout << "\t-- ClearCall: Request to clear call with token " << token << ", cause [" << (int)reason << "]" << endl;
#endif
	}
	return H323EndPoint::ClearCall(token, reason);
}

PBoolean MyH323EndPoint::ClearCall(const PString & token)
{
	if (h323debug) {
		cout << "\t-- ClearCall: Request to clear call with token " << token << endl;
	}
	return H323EndPoint::ClearCall(token, H323Connection::EndedByLocalUser);
}

void MyH323EndPoint::SendUserTone(const PString &token, char tone)
{
	H323Connection *connection = NULL;

	connection = FindConnectionWithLock(token);
	if (connection != NULL) {
		connection->SendUserInputTone(tone, 500);
		connection->Unlock();
	}
}

void MyH323EndPoint::OnClosedLogicalChannel(H323Connection & connection, const H323Channel & channel)
{
	channelsOpen--;
	if (h323debug) {
		cout << "\t\tchannelsOpen = " << channelsOpen << endl;
	}
	H323EndPoint::OnClosedLogicalChannel(connection, channel);
}

PBoolean MyH323EndPoint::OnConnectionForwarded(H323Connection & connection,
		const PString & forwardParty,
		const H323SignalPDU & pdu)
{
	if (h323debug) {
		cout << "\t-- Call Forwarded to " << forwardParty << endl;
	}
	return FALSE;
}

PBoolean MyH323EndPoint::ForwardConnection(H323Connection & connection,
		const PString & forwardParty,
		const H323SignalPDU & pdu)
{
	if (h323debug) {
		cout << "\t-- Forwarding call to " << forwardParty << endl;
	}
	return H323EndPoint::ForwardConnection(connection, forwardParty, pdu);
}

void MyH323EndPoint::OnConnectionEstablished(H323Connection & connection, const PString & estCallToken)
{
	if (h323debug) {
		cout << "\t=-= In OnConnectionEstablished for call " << connection.GetCallReference() << endl;
		cout << "\t\t-- Connection Established with \"" << connection.GetRemotePartyName() << "\"" << endl;
	}
	on_connection_established(connection.GetCallReference(), (const char *)connection.GetCallToken());
}

/** OnConnectionCleared callback function is called upon the dropping of an established
  * H323 connection.
  */
void MyH323EndPoint::OnConnectionCleared(H323Connection & connection, const PString & clearedCallToken)
{
	PString remoteName = connection.GetRemotePartyName();

	switch (connection.GetCallEndReason()) {
		case H323Connection::EndedByCallForwarded:
			if (h323debug) {
				cout << "-- " << remoteName << " has forwarded the call" << endl;
			}
			break;
		case H323Connection::EndedByRemoteUser:
			if (h323debug) {
				cout << "-- " << remoteName << " has cleared the call" << endl;
			}
			break;
		case H323Connection::EndedByCallerAbort:
			if (h323debug) {
				cout << "-- " << remoteName << " has stopped calling" << endl;
			}
			break;
		case H323Connection::EndedByRefusal:
			if (h323debug) {
				cout << "-- " << remoteName << " did not accept your call" << endl;
			}
			break;
		case H323Connection::EndedByRemoteBusy:
			if (h323debug) {
				cout << "-- " << remoteName << " was busy" << endl;
			}
			break;
		case H323Connection::EndedByRemoteCongestion:
			if (h323debug) {
				cout << "-- Congested link to " << remoteName << endl;
			}
			break;
		case H323Connection::EndedByNoAnswer:
			if (h323debug) {
				cout << "-- " << remoteName << " did not answer your call" << endl;
			}
			break;
		case H323Connection::EndedByTransportFail:
			if (h323debug) {
				cout << "-- Call with " << remoteName << " ended abnormally" << endl;
			}
			break;
		case H323Connection::EndedByCapabilityExchange:
			if (h323debug) {
				cout << "-- Could not find common codec with " << remoteName << endl;
			}
			break;
		case H323Connection::EndedByNoAccept:
			if (h323debug) {
				cout << "-- Did not accept incoming call from " << remoteName << endl;
			}
			break;
		case H323Connection::EndedByAnswerDenied:
			if (h323debug) {
				cout << "-- Refused incoming call from " << remoteName << endl;
			}
			break;
		case H323Connection::EndedByNoUser:
			if (h323debug) {
				cout << "-- Remote endpoint could not find user: " << remoteName << endl;
			}
			break;
		case H323Connection::EndedByNoBandwidth:
			if (h323debug) {
				cout << "-- Call to " << remoteName << " aborted, insufficient bandwidth." << endl;
			}
			break;
		case H323Connection::EndedByUnreachable:
			if (h323debug) {
				cout << "-- " << remoteName << " could not be reached." << endl;
			}
			break;
		case H323Connection::EndedByHostOffline:
			if (h323debug) {
				cout << "-- " << remoteName << " is not online." << endl;
			}
			break;
		case H323Connection::EndedByNoEndPoint:
			if (h323debug) {
				cout << "-- No phone running for " << remoteName << endl;
			}
			break;
		case H323Connection::EndedByConnectFail:
			if (h323debug) {
				cout << "-- Transport error calling " << remoteName << endl;
			}
			break;
		default:
			if (h323debug) {
#ifdef PTRACING
				cout << " -- Call with " << remoteName << " completed (" << connection.GetCallEndReason() << ")" << endl;
#else
				cout << " -- Call with " << remoteName << " completed ([" << (int)connection.GetCallEndReason() << "])" << endl;
#endif
			}
	}

	if (connection.IsEstablished()) {
		if (h323debug) {
			cout << "\t-- Call duration " << setprecision(0) << setw(5) << (PTime() - connection.GetConnectionStartTime()) << endl;
		}
	}
	/* Invoke the PBX application registered callback */
	on_connection_cleared(connection.GetCallReference(), clearedCallToken);
	return;
}

H323Connection * MyH323EndPoint::CreateConnection(unsigned callReference, void *userData, H323Transport *transport, H323SignalPDU *setupPDU)
{
	unsigned options = 0;
	call_options_t *opts = (call_options_t *)userData;
	MyH323Connection *conn;

	if (opts && opts->fastStart) {
		options |= H323Connection::FastStartOptionEnable;
	} else {
		options |= H323Connection::FastStartOptionDisable;
	}
	if (opts && opts->h245Tunneling) {
		options |= H323Connection::H245TunnelingOptionEnable;
	} else {
		options |= H323Connection::H245TunnelingOptionDisable;
	}
/* Disable until I can figure out the proper way to deal with this */
#if 0
	if (opts->silenceSuppression) {
		options |= H323Connection::SilenceSuppresionOptionEnable;
	} else {
		options |= H323Connection::SilenceSUppressionOptionDisable;
	}
#endif
	conn = new MyH323Connection(*this, callReference, options);
	if (conn) {
		if (opts)
			conn->SetCallOptions(opts, (setupPDU ? TRUE : FALSE));
	}
	return conn;
}

/* MyH323Connection Implementation */
MyH323Connection::MyH323Connection(MyH323EndPoint & ep, unsigned callReference,
							unsigned options)
	: H323Connection(ep, callReference, options)
{
#ifdef H323_H450
	/* Dispatcher will free out all registered handlers */
	delete h450dispatcher;
	h450dispatcher = new H450xDispatcher(*this);
	h4502handler = new H4502Handler(*this, *h450dispatcher);
	h4504handler = new MyH4504Handler(*this, *h450dispatcher);
	h4506handler = new H4506Handler(*this, *h450dispatcher);
	h45011handler = new H45011Handler(*this, *h450dispatcher);
#endif
	cause = -1;
	sessionId = 0;
	bridging = FALSE;
	holdHandling = progressSetup = progressAlert = 0;
	dtmfMode = 0;
	dtmfCodec[0] = dtmfCodec[1] = (RTP_DataFrame::PayloadTypes)0;
	redirect_reason = -1;
	transfer_capability = -1;
#ifdef TUNNELLING
	tunnelOptions = remoteTunnelOptions = 0;
#endif
	if (h323debug) {
		cout << "	== New H.323 Connection created." << endl;
	}
	return;
}

MyH323Connection::~MyH323Connection()
{
	if (h323debug) {
		cout << "	== H.323 Connection deleted." << endl;
	}
	return;
}

PBoolean MyH323Connection::OnReceivedProgress(const H323SignalPDU &pdu)
{
	PBoolean isInband;
	unsigned pi;

	if (!H323Connection::OnReceivedProgress(pdu)) {
		return FALSE;
	}

	if (!pdu.GetQ931().GetProgressIndicator(pi))
		pi = 0;
	if (h323debug) {
		cout << "\t- Progress Indicator: " << pi << endl;
	}

	switch(pi) {
	case Q931::ProgressNotEndToEndISDN:
	case Q931::ProgressInbandInformationAvailable:
		isInband = TRUE;
		break;
	default:
		isInband = FALSE;
	}
	on_progress(GetCallReference(), (const char *)GetCallToken(), isInband);

	return connectionState != ShuttingDownConnection;
}

PBoolean MyH323Connection::MySendProgress()
{
	/* The code taken from H323Connection::AnsweringCall() but ALWAYS send
	   PROGRESS message, including slow start operations */
	H323SignalPDU progressPDU;
	H225_Progress_UUIE &prog = progressPDU.BuildProgress(*this);

	if (!mediaWaitForConnect) {
		if (SendFastStartAcknowledge(prog.m_fastStart))
			prog.IncludeOptionalField(H225_Progress_UUIE::e_fastStart);
		else {
			if (connectionState == ShuttingDownConnection)
				return FALSE;

			/* Do early H.245 start */
			earlyStart = TRUE;
			if (!h245Tunneling) {
				if (!H323Connection::StartControlChannel())
					return FALSE;
				prog.IncludeOptionalField(H225_Progress_UUIE::e_h245Address);
				controlChannel->SetUpTransportPDU(prog.m_h245Address, TRUE);
			}
		}
	}
	progressPDU.GetQ931().SetProgressIndicator(Q931::ProgressInbandInformationAvailable);

#ifdef TUNNELLING
	EmbedTunneledInfo(progressPDU);
#endif
	HandleTunnelPDU(&progressPDU);
	WriteSignalPDU(progressPDU);

	return TRUE;
}

H323Connection::AnswerCallResponse MyH323Connection::OnAnswerCall(const PString & caller,
								const H323SignalPDU & setupPDU,
								H323SignalPDU & connectPDU)
{
	unsigned pi;

	if (h323debug) {
		cout << "\t=-= In OnAnswerCall for call " << GetCallReference() << endl;
	}

	if (connectionState == ShuttingDownConnection)
		return H323Connection::AnswerCallDenied;

	if (!setupPDU.GetQ931().GetProgressIndicator(pi)) {
		pi = 0;
	}
	if (h323debug) {
		cout << "\t\t- Progress Indicator: " << pi << endl;
	}
	if (progressAlert) {
		pi = progressAlert;
	} else if (pi == Q931::ProgressOriginNotISDN) {
		pi = Q931::ProgressInbandInformationAvailable;
	}
	if (pi && alertingPDU) {
		alertingPDU->GetQ931().SetProgressIndicator(pi);
	}
	if (h323debug) {
		cout << "\t\t- Inserting PI of " << pi << " into ALERTING message" << endl;
	}

#ifdef TUNNELLING
	if (alertingPDU)
		EmbedTunneledInfo(*alertingPDU);
	EmbedTunneledInfo(connectPDU);
#endif

	if (!on_answer_call(GetCallReference(), (const char *)GetCallToken())) {
		return H323Connection::AnswerCallDenied;
	}
	/* The call will be answered later with "AnsweringCall()" function.
	 */
	return ((pi || (fastStartState != FastStartDisabled)) ? AnswerCallDeferredWithMedia : AnswerCallDeferred);
}

PBoolean MyH323Connection::OnAlerting(const H323SignalPDU & alertingPDU, const PString & username)
{
	if (h323debug) {
		cout << "\t=-= In OnAlerting for call " << GetCallReference()
			<< ": sessionId=" << sessionId << endl;
		cout << "\t-- Ringing phone for \"" << username << "\"" << endl;
	}

	if (on_progress) {
		PBoolean isInband;
		unsigned alertingPI;

		if (!alertingPDU.GetQ931().GetProgressIndicator(alertingPI)) {
			alertingPI = 0;
		}
		if (h323debug) {
			cout << "\t\t- Progress Indicator: " << alertingPI << endl;
		}

		switch(alertingPI) {
		case Q931::ProgressNotEndToEndISDN:
		case Q931::ProgressInbandInformationAvailable:
			isInband = TRUE;
			break;
		default:
			isInband = FALSE;
		}
		on_progress(GetCallReference(), (const char *)GetCallToken(), isInband);
	}
	on_chan_ringing(GetCallReference(), (const char *)GetCallToken() );
	return connectionState != ShuttingDownConnection;
}

void MyH323Connection::SetCallOptions(void *o, PBoolean isIncoming)
{
	call_options_t *opts = (call_options_t *)o;

	progressSetup = opts->progress_setup;
	progressAlert = opts->progress_alert;
	holdHandling = opts->holdHandling;
	dtmfCodec[0] = (RTP_DataFrame::PayloadTypes)opts->dtmfcodec[0];
	dtmfCodec[1] = (RTP_DataFrame::PayloadTypes)opts->dtmfcodec[1];
	dtmfMode = opts->dtmfmode;

	if (isIncoming) {
		fastStartState = (opts->fastStart ? FastStartInitiate : FastStartDisabled);
		h245Tunneling = (opts->h245Tunneling ? TRUE : FALSE);
	} else {
		sourceE164 = PString(opts->cid_num);
		SetLocalPartyName(PString(opts->cid_name));
		SetDisplayName(PString(opts->cid_name));
		if (opts->redirect_reason >= 0) {
			rdnis = PString(opts->cid_rdnis);
			redirect_reason = opts->redirect_reason;
		}
		cid_presentation = opts->presentation;
		cid_ton = opts->type_of_number;
		if (opts->transfer_capability >= 0) {
			transfer_capability = opts->transfer_capability;
		}
	}
	tunnelOptions = opts->tunnelOptions;
}

void MyH323Connection::SetCallDetails(void *callDetails, const H323SignalPDU &setupPDU, PBoolean isIncoming)
{
	PString sourceE164;
	PString destE164;
	PString sourceAliases;
	PString destAliases;
	char *s, *s1;
	call_details_t *cd = (call_details_t *)callDetails;

	memset(cd, 0, sizeof(*cd));
	cd->call_reference = GetCallReference();
	cd->call_token = strdup((const char *)GetCallToken());

	sourceE164 = "";
	setupPDU.GetSourceE164(sourceE164);
	cd->call_source_e164 = strdup((const char *)sourceE164);

	destE164 = "";
	setupPDU.GetDestinationE164(destE164);
	cd->call_dest_e164 = strdup((const char *)destE164);

	/* XXX Is it possible to have this information for outgoing calls too? XXX */
	if (isIncoming) {
		PString sourceName;
		PIPSocket::Address Ip;
		WORD sourcePort;
		PString redirect_number;
		unsigned redirect_reason;
		unsigned plan, type, screening, presentation;
		Q931::InformationTransferCapability capability;
		unsigned transferRate, codingStandard, userInfoLayer1;

		/* Fetch presentation and type information about calling party's number */
		if (setupPDU.GetQ931().GetCallingPartyNumber(sourceName, &plan, &type, &presentation, &screening, 0, 0)) {
			/* Construct fields back */
			cd->type_of_number = (type << 4) | plan;
			cd->presentation = (presentation << 5) | screening;
		} else if (cd->call_source_e164[0]) {
			cd->type_of_number = 0;		/* UNKNOWN */
			cd->presentation = 0x03;	/* ALLOWED NETWORK NUMBER - Default */
			if (setupPDU.GetQ931().HasIE(Q931::UserUserIE)) {
				const H225_Setup_UUIE &setup_uuie = setupPDU.m_h323_uu_pdu.m_h323_message_body;
				if (setup_uuie.HasOptionalField(H225_Setup_UUIE::e_presentationIndicator))
					cd->presentation = (cd->presentation & 0x9f) | (((unsigned int)setup_uuie.m_presentationIndicator.GetTag()) << 5);
				if (setup_uuie.HasOptionalField(H225_Setup_UUIE::e_screeningIndicator))
					cd->presentation = (cd->presentation & 0xe0) | (((unsigned int)setup_uuie.m_screeningIndicator.GetValue()) & 0x1f);
			}
		} else {
			cd->type_of_number = 0;		/* UNKNOWN */
			cd->presentation = 0x43;	/* NUMBER NOT AVAILABLE */
		}

		sourceName = setupPDU.GetQ931().GetDisplayName();
		cd->call_source_name = strdup((const char *)sourceName);

		GetSignallingChannel()->GetRemoteAddress().GetIpAndPort(Ip, sourcePort);
		cd->sourceIp = strdup((const char *)Ip.AsString());

		if (setupPDU.GetQ931().GetRedirectingNumber(redirect_number, NULL, NULL, NULL, NULL, &redirect_reason, 0, 0, 0)) {
			cd->redirect_number = strdup((const char *)redirect_number);
			cd->redirect_reason = redirect_reason;
		}
		else
			cd->redirect_reason = -1;

		/* Fetch Q.931's transfer capability */
		if (((Q931 &)setupPDU.GetQ931()).GetBearerCapabilities(capability, transferRate, &codingStandard, &userInfoLayer1))
			cd->transfer_capability = ((unsigned int)capability & 0x1f) | (codingStandard << 5);
		else
			cd->transfer_capability = 0x00;	/* ITU coding of Speech */

		/* Don't show local username as called party name */
		SetDisplayName(cd->call_dest_e164);
	}

	/* Convert complex strings */
	//  FIXME: deal more than one source alias
	sourceAliases = setupPDU.GetSourceAliases();
	s1 = strdup((const char *)sourceAliases);
	if ((s = strchr(s1, ' ')) != NULL)
		*s = '\0';
	if ((s = strchr(s1, '\t')) != NULL)
		*s = '\0';
	cd->call_source_aliases = s1;

	destAliases = setupPDU.GetDestinationAlias();
	s1 = strdup((const char *)destAliases);
	if ((s = strchr(s1, ' ')) != NULL)
		*s = '\0';
	if ((s = strchr(s1, '\t')) != NULL)
		*s = '\0';
	cd->call_dest_alias = s1;
}

#ifdef TUNNELLING
static PBoolean FetchInformationElements(Q931 &q931, const PBYTEArray &data)
{
	PINDEX offset = 0;

	while (offset < data.GetSize()) {
		// Get field discriminator
		int discriminator = data[offset++];

#if 0
		/* Do not overwrite existing IEs */
		if (q931.HasIE((Q931::InformationElementCodes)discriminator)) {
			if ((discriminatir & 0x80) == 0)
				offset += data[offset++];
			if (offset > data.GetSize())
				return FALSE;
			continue;
		}
#endif

		PBYTEArray * item = new PBYTEArray;

		// For discriminator with high bit set there is no data
		if ((discriminator & 0x80) == 0) {
			int len = data[offset++];

#if 0		// That is not H.225 but regular Q.931 (ISDN) IEs
			if (discriminator == UserUserIE) {
				// Special case of User-user field. See 7.2.2.31/H.225.0v4.
				len <<= 8;
				len |= data[offset++];

				// we also have a protocol discriminator, which we ignore
				offset++;

				// before decrementing the length, make sure it is not zero
				if (len == 0)
					return FALSE;

				// adjust for protocol discriminator
				len--;
			}
#endif

			if (offset + len > data.GetSize()) {
				delete item;
				return FALSE;
			}

			memcpy(item->GetPointer(len), (const BYTE *)data+offset, len);
			offset += len;
		}

		q931.SetIE((Q931::InformationElementCodes)discriminator, *item);
		delete item;
	}
	return TRUE;
}

static PBoolean FetchCiscoTunneledInfo(Q931 &q931, const H323SignalPDU &pdu)
{
	PBoolean res = FALSE;
	const H225_H323_UU_PDU &uuPDU = pdu.m_h323_uu_pdu;

	if(uuPDU.HasOptionalField(H225_H323_UU_PDU::e_nonStandardControl)) {
		for(int i = 0; i < uuPDU.m_nonStandardControl.GetSize(); ++i) {
			const H225_NonStandardParameter &np = uuPDU.m_nonStandardControl[i];
			const H225_NonStandardIdentifier &id = np.m_nonStandardIdentifier;
			if (id.GetTag() == H225_NonStandardIdentifier::e_h221NonStandard) {
				const H225_H221NonStandard &ni = id;
				/* Check for Cisco */
				if ((ni.m_t35CountryCode == 181) && (ni.m_t35Extension == 0) && (ni.m_manufacturerCode == 18)) {
					const PBYTEArray &data = np.m_data;
					if (h323debug)
						cout << setprecision(0) << "Received non-standard Cisco extension data " << np.m_data << endl;
					CISCO_H225_H323_UU_NonStdInfo c;
					PPER_Stream strm(data);
					if (c.Decode(strm)) {
						PBoolean haveIEs = FALSE;
						if (h323debug)
							cout << setprecision(0) << "H323_UU_NonStdInfo = " << c << endl;
						if (c.HasOptionalField(CISCO_H225_H323_UU_NonStdInfo::e_protoParam)) {
							FetchInformationElements(q931, c.m_protoParam.m_qsigNonStdInfo.m_rawMesg);
							haveIEs = TRUE;
						}
						if (c.HasOptionalField(CISCO_H225_H323_UU_NonStdInfo::e_commonParam)) {
							FetchInformationElements(q931, c.m_commonParam.m_redirectIEinfo.m_redirectIE);
							haveIEs = TRUE;
						}
						if (haveIEs && h323debug)
							cout << setprecision(0) << "Information elements collected:" << q931 << endl;
						res = TRUE;
					} else {
						cout << "ERROR while decoding non-standard Cisco extension" << endl;
						return FALSE;
					}
				}
			}
		}
	}
	return res;
}

static PBoolean EmbedCiscoTunneledInfo(H323SignalPDU &pdu)
{
	static const struct {
		Q931::InformationElementCodes ie;
		PBoolean dontDelete;
	} codes[] = {
		{ Q931::RedirectingNumberIE, },
		{ Q931::FacilityIE, },
//		{ Q931::CallingPartyNumberIE, TRUE },
	};

	PBoolean res = FALSE;
	PBoolean notRedirOnly = FALSE;
	Q931 tmpQ931;
	Q931 &q931 = pdu.GetQ931();

	for(unsigned i = 0; i < ARRAY_LEN(codes); ++i) {
		if (q931.HasIE(codes[i].ie)) {
			tmpQ931.SetIE(codes[i].ie, q931.GetIE(codes[i].ie));
			if (!codes[i].dontDelete)
				q931.RemoveIE(codes[i].ie);
			if (codes[i].ie != Q931::RedirectingNumberIE)
				notRedirOnly = TRUE;
			res = TRUE;
		}
	}
	/* Have something to embed */
	if (res) {
		PBYTEArray msg;
		if (!tmpQ931.Encode(msg))
			return FALSE;
		PBYTEArray ies(msg.GetPointer() + 5, msg.GetSize() - 5);

		H225_H323_UU_PDU &uuPDU = pdu.m_h323_uu_pdu;
		if(!uuPDU.HasOptionalField(H225_H323_UU_PDU::e_nonStandardControl)) {
			uuPDU.IncludeOptionalField(H225_H323_UU_PDU::e_nonStandardControl);
			uuPDU.m_nonStandardControl.SetSize(0);
		}
		H225_NonStandardParameter *np = new H225_NonStandardParameter;
		uuPDU.m_nonStandardControl.Append(np);
		H225_NonStandardIdentifier &nsi = (*np).m_nonStandardIdentifier;
		nsi.SetTag(H225_NonStandardIdentifier::e_h221NonStandard);
		H225_H221NonStandard &ns = nsi;
		ns.m_t35CountryCode = 181;
		ns.m_t35Extension = 0;
		ns.m_manufacturerCode = 18;

		CISCO_H225_H323_UU_NonStdInfo c;
		c.IncludeOptionalField(CISCO_H225_H323_UU_NonStdInfo::e_version);
		c.m_version = 0;

		if (notRedirOnly) {
			c.IncludeOptionalField(CISCO_H225_H323_UU_NonStdInfo::e_protoParam);
			CISCO_H225_QsigNonStdInfo &qsigInfo = c.m_protoParam.m_qsigNonStdInfo;
			qsigInfo.m_iei = ies[0];
			qsigInfo.m_rawMesg = ies;
		} else {
			c.IncludeOptionalField(CISCO_H225_H323_UU_NonStdInfo::e_commonParam);
			c.m_commonParam.m_redirectIEinfo.m_redirectIE = ies;
		}
		PPER_Stream stream;
		c.Encode(stream);
		stream.CompleteEncoding();
		(*np).m_data = stream;
	}
	return res;
}

static const char OID_QSIG[] = "1.3.12.9";

static PBoolean FetchQSIGTunneledInfo(Q931 &q931, const H323SignalPDU &pdu)
{
	PBoolean res = FALSE;
	const H225_H323_UU_PDU &uuPDU = pdu.m_h323_uu_pdu;
	if (uuPDU.HasOptionalField(H225_H323_UU_PDU::e_tunnelledSignallingMessage)) {
		const H225_H323_UU_PDU_tunnelledSignallingMessage &sig = uuPDU.m_tunnelledSignallingMessage;
		const H225_TunnelledProtocol_id &proto = sig.m_tunnelledProtocolID.m_id;
		if ((proto.GetTag() == H225_TunnelledProtocol_id::e_tunnelledProtocolObjectID) &&
				(((const PASN_ObjectId &)proto).AsString() == OID_QSIG)) {
			const H225_ArrayOf_PASN_OctetString &sigs = sig.m_messageContent;
			for(int i = 0; i < sigs.GetSize(); ++i) {
				const PASN_OctetString &msg = sigs[i];
				if (h323debug)
					cout << setprecision(0) << "Q.931 message data is " << msg << endl;
				if(!q931.Decode((const PBYTEArray &)msg)) {
					cout << "Error while decoding Q.931 message" << endl;
					return FALSE;
				}
				res = TRUE;
				if (h323debug)
					cout << setprecision(0) << "Received QSIG message " << q931 << endl;
			}
		}
	}
	return res;
}

static H225_EndpointType *GetEndpointType(H323SignalPDU &pdu)
{
	if (!pdu.GetQ931().HasIE(Q931::UserUserIE))
		return NULL;

	H225_H323_UU_PDU_h323_message_body &body = pdu.m_h323_uu_pdu.m_h323_message_body;
	switch (body.GetTag()) {
	case H225_H323_UU_PDU_h323_message_body::e_setup:
		return &((H225_Setup_UUIE &)body).m_sourceInfo;
	case H225_H323_UU_PDU_h323_message_body::e_callProceeding:
		return &((H225_CallProceeding_UUIE &)body).m_destinationInfo;
	case H225_H323_UU_PDU_h323_message_body::e_connect:
		return &((H225_Connect_UUIE &)body).m_destinationInfo;
	case H225_H323_UU_PDU_h323_message_body::e_alerting:
		return &((H225_Alerting_UUIE &)body).m_destinationInfo;
	case H225_H323_UU_PDU_h323_message_body::e_facility:
		return &((H225_Facility_UUIE &)body).m_destinationInfo;
	case H225_H323_UU_PDU_h323_message_body::e_progress:
		return &((H225_Progress_UUIE &)body).m_destinationInfo;
	}
	return NULL;
}

static PBoolean QSIGTunnelRequested(H323SignalPDU &pdu)
{
	H225_EndpointType *epType = GetEndpointType(pdu);
	if (epType) {
		if (!(*epType).HasOptionalField(H225_EndpointType::e_supportedTunnelledProtocols)) {
			return FALSE;
		}
		H225_ArrayOf_TunnelledProtocol &protos = (*epType).m_supportedTunnelledProtocols;
		for (int i = 0; i < protos.GetSize(); ++i)
		{
			if ((protos[i].GetTag() == H225_TunnelledProtocol_id::e_tunnelledProtocolObjectID) &&
					(((const PASN_ObjectId &)protos[i]).AsString() == OID_QSIG)) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

static PBoolean EmbedQSIGTunneledInfo(H323SignalPDU &pdu)
{
	static const Q931::InformationElementCodes codes[] =
	{ Q931::RedirectingNumberIE, Q931::FacilityIE };

	Q931 &q931 = pdu.GetQ931();
	PBYTEArray message;

	q931.Encode(message);

	/* Remove non-standard IEs */
	for(unsigned i = 0; i < ARRAY_LEN(codes); ++i) {
		if (q931.HasIE(codes[i])) {
			q931.RemoveIE(codes[i]);
		}
	}

	H225_H323_UU_PDU &uuPDU = pdu.m_h323_uu_pdu;
	H225_EndpointType *epType = GetEndpointType(pdu);
	if (epType) {
		if (!(*epType).HasOptionalField(H225_EndpointType::e_supportedTunnelledProtocols)) {
			(*epType).IncludeOptionalField(H225_EndpointType::e_supportedTunnelledProtocols);
			(*epType).m_supportedTunnelledProtocols.SetSize(0);
		}
		H225_ArrayOf_TunnelledProtocol &protos = (*epType).m_supportedTunnelledProtocols;
		PBoolean addQSIG = TRUE;
		for (int i = 0; i < protos.GetSize(); ++i)
		{
			if ((protos[i].GetTag() == H225_TunnelledProtocol_id::e_tunnelledProtocolObjectID) &&
					(((PASN_ObjectId &)protos[i]).AsString() == OID_QSIG)) {
				addQSIG = FALSE;
				break;
			}
		}
		if (addQSIG) {
			H225_TunnelledProtocol *proto = new H225_TunnelledProtocol;
			(*proto).m_id.SetTag(H225_TunnelledProtocol_id::e_tunnelledProtocolObjectID);
			(PASN_ObjectId &)(proto->m_id) = OID_QSIG;
			protos.Append(proto);
		}
	}
	if (!uuPDU.HasOptionalField(H225_H323_UU_PDU::e_tunnelledSignallingMessage))
		uuPDU.IncludeOptionalField(H225_H323_UU_PDU::e_tunnelledSignallingMessage);
	H225_H323_UU_PDU_tunnelledSignallingMessage &sig = uuPDU.m_tunnelledSignallingMessage;
	H225_TunnelledProtocol_id &proto = sig.m_tunnelledProtocolID.m_id;
	if ((proto.GetTag() != H225_TunnelledProtocol_id::e_tunnelledProtocolObjectID) ||
			(((const PASN_ObjectId &)proto).AsString() != OID_QSIG)) {
		proto.SetTag(H225_TunnelledProtocol_id::e_tunnelledProtocolObjectID);
		(PASN_ObjectId &)proto = OID_QSIG;
		sig.m_messageContent.SetSize(0);
	}
	PASN_OctetString *msg = new PASN_OctetString;
	sig.m_messageContent.Append(msg);
	*msg = message;
	return TRUE;
}

PBoolean MyH323Connection::EmbedTunneledInfo(H323SignalPDU &pdu)
{
	if ((tunnelOptions & H323_TUNNEL_QSIG) || (remoteTunnelOptions & H323_TUNNEL_QSIG))
		EmbedQSIGTunneledInfo(pdu);
	if ((tunnelOptions & H323_TUNNEL_CISCO) || (remoteTunnelOptions & H323_TUNNEL_CISCO))
		EmbedCiscoTunneledInfo(pdu);

	return TRUE;
}

/* Handle tunneled messages */
PBoolean MyH323Connection::HandleSignalPDU(H323SignalPDU &pdu)
{
	if (pdu.GetQ931().HasIE(Q931::UserUserIE)) {
		Q931 tunneledInfo;
		const Q931 *q931Info;

		q931Info = NULL;
		if (FetchCiscoTunneledInfo(tunneledInfo, pdu)) {
			q931Info = &tunneledInfo;
			remoteTunnelOptions |= H323_TUNNEL_CISCO;
		}
		if (FetchQSIGTunneledInfo(tunneledInfo, pdu)) {
			q931Info = &tunneledInfo;
			remoteTunnelOptions |= H323_TUNNEL_QSIG;
		}
		if (!(remoteTunnelOptions & H323_TUNNEL_QSIG) && QSIGTunnelRequested(pdu)) {
			remoteTunnelOptions |= H323_TUNNEL_QSIG;
		}
		if (q931Info) {
			if (q931Info->HasIE(Q931::RedirectingNumberIE)) {
				pdu.GetQ931().SetIE(Q931::RedirectingNumberIE, q931Info->GetIE(Q931::RedirectingNumberIE));
				if (h323debug) {
					PString number;
					unsigned reason;
					if(q931Info->GetRedirectingNumber(number, NULL, NULL, NULL, NULL, &reason, 0, 0, 0))
						cout << "Got redirection from " << number << ", reason " << reason << endl;
				}
			}
		}
	}

	return H323Connection::HandleSignalPDU(pdu);
}
#endif

PBoolean MyH323Connection::OnReceivedSignalSetup(const H323SignalPDU & setupPDU)
{
	call_details_t cd;

	if (h323debug) {
		cout << "\t--Received SETUP message" << endl;
	}

	if (connectionState == ShuttingDownConnection)
		return FALSE;

	SetCallDetails(&cd, setupPDU, TRUE);

	/* Notify Asterisk of the request */
	call_options_t *res = on_incoming_call(&cd);

	if (!res) {
		if (h323debug) {
			cout << "\t-- Call Failed" << endl;
		}
		return FALSE;
	}

	SetCallOptions(res, TRUE);

	/* Disable fastStart if requested by remote side */
	if (h245Tunneling && !setupPDU.m_h323_uu_pdu.m_h245Tunneling) {
		masterSlaveDeterminationProcedure->Stop();
		capabilityExchangeProcedure->Stop();
		PTRACE(3, "H225\tFast Start DISABLED!");
		h245Tunneling = FALSE;
	}

	return H323Connection::OnReceivedSignalSetup(setupPDU);
}

PBoolean MyH323Connection::OnSendSignalSetup(H323SignalPDU & setupPDU)
{
	call_details_t cd;

	if (h323debug) {
		cout << "\t-- Sending SETUP message" << endl;
	}

	if (connectionState == ShuttingDownConnection)
		return FALSE;

	if (progressSetup)
		setupPDU.GetQ931().SetProgressIndicator(progressSetup);

	if (redirect_reason >= 0) {
		setupPDU.GetQ931().SetRedirectingNumber(rdnis, 0, 0, 0, 0, redirect_reason);
		/* OpenH323 incorrectly fills number IE when redirecting reason is specified - fix it */
		PBYTEArray IE(setupPDU.GetQ931().GetIE(Q931::RedirectingNumberIE));
		IE[0] = IE[0] & 0x7f;
		IE[1] = IE[1] & 0x7f;
		setupPDU.GetQ931().SetIE(Q931::RedirectingNumberIE, IE);
	}

	if (transfer_capability)
		setupPDU.GetQ931().SetBearerCapabilities((Q931::InformationTransferCapability)(transfer_capability & 0x1f), 1, ((transfer_capability >> 5) & 3));

	SetCallDetails(&cd, setupPDU, FALSE);

	int res = on_outgoing_call(&cd);
	if (!res) {
		if (h323debug) {
			cout << "\t-- Call Failed" << endl;
		}
		return FALSE;
	}

	/* OpenH323 will build calling party information with default
	   type and presentation information, so build it to be recorded
	   by embedding routines */
	setupPDU.GetQ931().SetCallingPartyNumber(sourceE164, (cid_ton >> 4) & 0x07,
			cid_ton & 0x0f, (cid_presentation >> 5) & 0x03, cid_presentation & 0x1f);
	setupPDU.GetQ931().SetDisplayName(GetDisplayName());

#ifdef TUNNELLING
	EmbedTunneledInfo(setupPDU);
#endif

	return H323Connection::OnSendSignalSetup(setupPDU);
}

static PBoolean BuildFastStartList(const H323Channel & channel,
		H225_ArrayOf_PASN_OctetString & array,
		H323Channel::Directions reverseDirection)
{
	H245_OpenLogicalChannel open;
	const H323Capability & capability = channel.GetCapability();

	if (channel.GetDirection() != reverseDirection) {
		if (!capability.OnSendingPDU(open.m_forwardLogicalChannelParameters.m_dataType))
			return FALSE;
	}
	else {
		if (!capability.OnSendingPDU(open.m_reverseLogicalChannelParameters.m_dataType))
			return FALSE;

		open.m_forwardLogicalChannelParameters.m_multiplexParameters.SetTag(
				H245_OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters::e_none);
		open.m_forwardLogicalChannelParameters.m_dataType.SetTag(H245_DataType::e_nullData);
		open.IncludeOptionalField(H245_OpenLogicalChannel::e_reverseLogicalChannelParameters);
	}

	if (!channel.OnSendingPDU(open))
		return FALSE;

	PTRACE(4, "H225\tBuild fastStart:\n	" << setprecision(2) << open);
	PINDEX last = array.GetSize();
	array.SetSize(last+1);
	array[last].EncodeSubType(open);

	PTRACE(3, "H225\tBuilt fastStart for " << capability);
	return TRUE;
}

H323Connection::CallEndReason MyH323Connection::SendSignalSetup(const PString & alias,
		const H323TransportAddress & address)
{
	// Start the call, first state is asking gatekeeper
	connectionState = AwaitingGatekeeperAdmission;

	// Indicate the direction of call.
	if (alias.IsEmpty())
		remotePartyName = remotePartyAddress = address;
	else {
		remotePartyName = alias;
		remotePartyAddress = alias + '@' + address;
	}

	// Start building the setup PDU to get various ID's
	H323SignalPDU setupPDU;
	H225_Setup_UUIE & setup = setupPDU.BuildSetup(*this, address);

#ifdef H323_H450
	h450dispatcher->AttachToSetup(setupPDU);
#endif

	// Save the identifiers generated by BuildSetup
	setupPDU.GetQ931().GetCalledPartyNumber(remotePartyNumber);

	H323TransportAddress gatekeeperRoute = address;

	// Check for gatekeeper and do admission check if have one
	H323Gatekeeper * gatekeeper = endpoint.GetGatekeeper();
	H225_ArrayOf_AliasAddress newAliasAddresses;
	if (gatekeeper != NULL) {
		H323Gatekeeper::AdmissionResponse response;
		response.transportAddress = &gatekeeperRoute;
		response.aliasAddresses = &newAliasAddresses;
		if (!gkAccessTokenOID)
			response.accessTokenData = &gkAccessTokenData;
		while (!gatekeeper->AdmissionRequest(*this, response, alias.IsEmpty())) {
			PTRACE(1, "H225\tGatekeeper refused admission: "
					<< (response.rejectReason == UINT_MAX
					? PString("Transport error")
					: H225_AdmissionRejectReason(response.rejectReason).GetTagName()));
#ifdef H323_H450
			h4502handler->onReceivedAdmissionReject(H4501_GeneralErrorList::e_notAvailable);
#endif

			switch (response.rejectReason) {
			case H225_AdmissionRejectReason::e_calledPartyNotRegistered:
				return EndedByNoUser;
			case H225_AdmissionRejectReason::e_requestDenied:
				return EndedByNoBandwidth;
			case H225_AdmissionRejectReason::e_invalidPermission:
			case H225_AdmissionRejectReason::e_securityDenial:
				return EndedBySecurityDenial;
			case H225_AdmissionRejectReason::e_resourceUnavailable:
				return EndedByRemoteBusy;
			case H225_AdmissionRejectReason::e_incompleteAddress:
				if (OnInsufficientDigits())
					break;
				// Then default case
			default:
				return EndedByGatekeeper;
			}

			PString lastRemotePartyName = remotePartyName;
			while (lastRemotePartyName == remotePartyName) {
				Unlock(); // Release the mutex as can deadlock trying to clear call during connect.
				digitsWaitFlag.Wait();
				if (!Lock()) // Lock while checking for shutting down.
					return EndedByCallerAbort;
			}
		}
		mustSendDRQ = TRUE;
		if (response.gatekeeperRouted) {
			setup.IncludeOptionalField(H225_Setup_UUIE::e_endpointIdentifier);
			setup.m_endpointIdentifier = gatekeeper->GetEndpointIdentifier();
			gatekeeperRouted = TRUE;
		}
	}

#ifdef H323_TRANSNEXUS_OSP
	// check for OSP server (if not using GK)
	if (gatekeeper == NULL) {
		OpalOSP::Provider * ospProvider = endpoint.GetOSPProvider();
		if (ospProvider != NULL) {
			OpalOSP::Transaction * transaction = new OpalOSP::Transaction();
			if (transaction->Open(*ospProvider) != 0) {
				PTRACE(1, "H225\tCannot create OSP transaction");
				return EndedByOSPRefusal;
			}

			OpalOSP::Transaction::DestinationInfo destInfo;
			if (!AuthoriseOSPTransaction(*transaction, destInfo)) {
				delete transaction;
				return EndedByOSPRefusal;
			}

			// save the transaction for use by the call
			ospTransaction = transaction;

			// retrieve the call information
			gatekeeperRoute = destInfo.destinationAddress;
			newAliasAddresses.Append(new H225_AliasAddress(destInfo.calledNumber));

			// insert the token
			setup.IncludeOptionalField(H225_Setup_UUIE::e_tokens);
			destInfo.InsertToken(setup.m_tokens);
		}
	}
#endif

	// Update the field e_destinationAddress in the SETUP PDU to reflect the new 
	// alias received in the ACF (m_destinationInfo).
	if (newAliasAddresses.GetSize() > 0) {
		setup.IncludeOptionalField(H225_Setup_UUIE::e_destinationAddress);
		setup.m_destinationAddress = newAliasAddresses;

		// Update the Q.931 Information Element (if is an E.164 address)
		PString e164 = H323GetAliasAddressE164(newAliasAddresses);
		if (!e164)
			remotePartyNumber = e164;
	}

	if (addAccessTokenToSetup && !gkAccessTokenOID && !gkAccessTokenData.IsEmpty()) {
		PString oid1, oid2;
		PINDEX comma = gkAccessTokenOID.Find(',');
		if (comma == P_MAX_INDEX)
			oid1 = oid2 = gkAccessTokenOID;
		else {
			oid1 = gkAccessTokenOID.Left(comma);
			oid2 = gkAccessTokenOID.Mid(comma+1);
		}
		setup.IncludeOptionalField(H225_Setup_UUIE::e_tokens);
		PINDEX last = setup.m_tokens.GetSize();
		setup.m_tokens.SetSize(last+1);
		setup.m_tokens[last].m_tokenOID = oid1;
		setup.m_tokens[last].IncludeOptionalField(H235_ClearToken::e_nonStandard);
		setup.m_tokens[last].m_nonStandard.m_nonStandardIdentifier = oid2;
		setup.m_tokens[last].m_nonStandard.m_data = gkAccessTokenData;
	}

	if (!signallingChannel->SetRemoteAddress(gatekeeperRoute)) {
		PTRACE(1, "H225\tInvalid "
					 << (gatekeeperRoute != address ? "gatekeeper" : "user")
					 << " supplied address: \"" << gatekeeperRoute << '"');
		connectionState = AwaitingTransportConnect;
		return EndedByConnectFail;
	}

	// Do the transport connect
	connectionState = AwaitingTransportConnect;

	// Release the mutex as can deadlock trying to clear call during connect.
	Unlock();

	signallingChannel->SetWriteTimeout(100);

	PBoolean connectFailed = !signallingChannel->Connect();

	// Lock while checking for shutting down.
	if (!Lock())
		return EndedByCallerAbort;

	// See if transport connect failed, abort if so.
	if (connectFailed) {
		connectionState = NoConnectionActive;
		switch (signallingChannel->GetErrorNumber()) {
			case ENETUNREACH :
				return EndedByUnreachable;
			case ECONNREFUSED :
				return EndedByNoEndPoint;
			case ETIMEDOUT :
				return EndedByHostOffline;
		}
		return EndedByConnectFail;
	}

	PTRACE(3, "H225\tSending Setup PDU");
	connectionState = AwaitingSignalConnect;

	// Put in all the signalling addresses for link
	setup.IncludeOptionalField(H225_Setup_UUIE::e_sourceCallSignalAddress);
	signallingChannel->SetUpTransportPDU(setup.m_sourceCallSignalAddress, TRUE);
	if (!setup.HasOptionalField(H225_Setup_UUIE::e_destCallSignalAddress)) {
		setup.IncludeOptionalField(H225_Setup_UUIE::e_destCallSignalAddress);
		signallingChannel->SetUpTransportPDU(setup.m_destCallSignalAddress, FALSE);
	}

	// If a standard call do Fast Start (if required)
	if (setup.m_conferenceGoal.GetTag() == H225_Setup_UUIE_conferenceGoal::e_create) {

		// Get the local capabilities before fast start is handled
		OnSetLocalCapabilities();

		// Ask the application what channels to open
		PTRACE(3, "H225\tCheck for Fast start by local endpoint");
		fastStartChannels.RemoveAll();
		OnSelectLogicalChannels();

		// If application called OpenLogicalChannel, put in the fastStart field
		if (!fastStartChannels.IsEmpty()) {
			PTRACE(3, "H225\tFast start begun by local endpoint");
			for (PINDEX i = 0; i < fastStartChannels.GetSize(); i++)
				BuildFastStartList(fastStartChannels[i], setup.m_fastStart, H323Channel::IsReceiver);
			if (setup.m_fastStart.GetSize() > 0)
				setup.IncludeOptionalField(H225_Setup_UUIE::e_fastStart);
		}

		// Search the capability set and see if we have video capability
		for (PINDEX i = 0; i < localCapabilities.GetSize(); i++) {
			switch (localCapabilities[i].GetMainType()) {
			case H323Capability::e_Audio:
			case H323Capability::e_UserInput:
				break;

			default:	// Is video or other data (eg T.120)
				setupPDU.GetQ931().SetBearerCapabilities(Q931::TransferUnrestrictedDigital, 6);
				i = localCapabilities.GetSize(); // Break out of the for loop
				break;
			}
		}
	}

	if (!OnSendSignalSetup(setupPDU))
		return EndedByNoAccept;

	// Do this again (was done when PDU was constructed) in case
	// OnSendSignalSetup() changed something.
//	setupPDU.SetQ931Fields(*this, TRUE);
	setupPDU.GetQ931().GetCalledPartyNumber(remotePartyNumber);

	fastStartState = FastStartDisabled;
	PBoolean set_lastPDUWasH245inSETUP = FALSE;

	if (h245Tunneling && doH245inSETUP) {
		h245TunnelTxPDU = &setupPDU;

		// Try and start the master/slave and capability exchange through the tunnel
		// Note: this used to be disallowed but is now allowed as of H323v4
		PBoolean ok = StartControlNegotiations();

		h245TunnelTxPDU = NULL;

		if (!ok)
			return EndedByTransportFail;

		if (setup.m_fastStart.GetSize() > 0) {
			// Now if fast start as well need to put this in setup specific field
			// and not the generic H.245 tunneling field
			setup.IncludeOptionalField(H225_Setup_UUIE::e_parallelH245Control);
			setup.m_parallelH245Control = setupPDU.m_h323_uu_pdu.m_h245Control;
			setupPDU.m_h323_uu_pdu.RemoveOptionalField(H225_H323_UU_PDU::e_h245Control);
			set_lastPDUWasH245inSETUP = TRUE;
		}
	}

	// Send the initial PDU
	setupTime = PTime();
	if (!WriteSignalPDU(setupPDU))
		return EndedByTransportFail;

	// WriteSignalPDU always resets lastPDUWasH245inSETUP.
	// So set it here if required
	if (set_lastPDUWasH245inSETUP)
		lastPDUWasH245inSETUP = TRUE;

	// Set timeout for remote party to answer the call
	signallingChannel->SetReadTimeout(endpoint.GetSignallingChannelCallTimeout());

	return NumCallEndReasons;
}


PBoolean MyH323Connection::OnSendReleaseComplete(H323SignalPDU & releaseCompletePDU)
{
	if (h323debug) {
		cout << "\t-- Sending RELEASE COMPLETE" << endl;
	}
	if (cause > 0)
		releaseCompletePDU.GetQ931().SetCause((Q931::CauseValues)cause);

#ifdef TUNNELLING
	EmbedTunneledInfo(releaseCompletePDU);
#endif

	return H323Connection::OnSendReleaseComplete(releaseCompletePDU);
}

PBoolean MyH323Connection::OnReceivedFacility(const H323SignalPDU & pdu)
{
	if (h323debug) {
		cout << "\t-- Received Facility message... " << endl;
	}
	return H323Connection::OnReceivedFacility(pdu);
}

void MyH323Connection::OnReceivedReleaseComplete(const H323SignalPDU & pdu)
{
	if (h323debug) {
		cout << "\t-- Received RELEASE COMPLETE message..." << endl;
	}
	if (on_hangup)
		on_hangup(GetCallReference(), (const char *)GetCallToken(), pdu.GetQ931().GetCause());
	return H323Connection::OnReceivedReleaseComplete(pdu);
}

PBoolean MyH323Connection::OnClosingLogicalChannel(H323Channel & channel)
{
	if (h323debug) {
		cout << "\t-- Closing logical channel..." << endl;
	}
	return H323Connection::OnClosingLogicalChannel(channel);
}

void MyH323Connection::SendUserInputTone(char tone, unsigned duration, unsigned logicalChannel, unsigned rtpTimestamp)
{
	SendUserInputModes mode = GetRealSendUserInputMode();
//	That is recursive call... Why?
//	on_receive_digit(GetCallReference(), tone, (const char *)GetCallToken());
	if ((tone != ' ') || (mode == SendUserInputAsTone) || (mode == SendUserInputAsInlineRFC2833)) {
		if (h323debug) {
			cout << "\t-- Sending user input tone (" << tone << ") to remote" << endl;
		}
		H323Connection::SendUserInputTone(tone, duration);
	}
}

void MyH323Connection::OnUserInputTone(char tone, unsigned duration, unsigned logicalChannel, unsigned rtpTimestamp)
{
	/* Why we should check this? */
	if ((dtmfMode & (H323_DTMF_CISCO | H323_DTMF_RFC2833 | H323_DTMF_SIGNAL)) != 0) {
		if (h323debug) {
			cout << "\t-- Received user input tone (" << tone << ") from remote" << endl;
		}
		on_receive_digit(GetCallReference(), tone, (const char *)GetCallToken(), duration);
	}
}

void MyH323Connection::OnUserInputString(const PString &value)
{
	if (h323debug) {
		cout << "\t-- Received user input string (" << value << ") from remote." << endl;
	}
	on_receive_digit(GetCallReference(), value[0], (const char *)GetCallToken(), 0);
}

void MyH323Connection::OnSendCapabilitySet(H245_TerminalCapabilitySet & pdu)
{
	PINDEX i;

	H323Connection::OnSendCapabilitySet(pdu);

	H245_ArrayOf_CapabilityTableEntry & tables = pdu.m_capabilityTable;
	for(i = 0; i < tables.GetSize(); i++)
	{
		H245_CapabilityTableEntry & entry = tables[i];
		if (entry.HasOptionalField(H245_CapabilityTableEntry::e_capability)) {
			H245_Capability & cap = entry.m_capability;
			if (cap.GetTag() == H245_Capability::e_receiveRTPAudioTelephonyEventCapability) {
				H245_AudioTelephonyEventCapability & atec = cap;
				atec.m_dynamicRTPPayloadType = dtmfCodec[0];
//				on_set_rfc2833_payload(GetCallReference(), (const char *)GetCallToken(), (int)dtmfCodec[0]);
#ifdef PTRACING
				if (h323debug) {
					cout << "\t-- Receiving RFC2833 on payload " <<
						atec.m_dynamicRTPPayloadType << endl;
				}
#endif
			}
		}
	}
}

void MyH323Connection::OnSetLocalCapabilities()
{
	if (on_setcapabilities)
		on_setcapabilities(GetCallReference(), (const char *)callToken);
}

PBoolean MyH323Connection::OnReceivedCapabilitySet(const H323Capabilities & remoteCaps,
							const H245_MultiplexCapability * muxCap,
							H245_TerminalCapabilitySetReject & reject)
{
	struct __codec__ {
		unsigned int asterisk_codec;
		unsigned int h245_cap;
		const char *oid;
		const char *formatName;
	};
	static const struct __codec__ codecs[] = {
		{ AST_FORMAT_G723_1, H245_AudioCapability::e_g7231 },
		{ AST_FORMAT_GSM, H245_AudioCapability::e_gsmFullRate },
		{ AST_FORMAT_ULAW, H245_AudioCapability::e_g711Ulaw64k },
		{ AST_FORMAT_ALAW, H245_AudioCapability::e_g711Alaw64k },
		{ AST_FORMAT_G729A, H245_AudioCapability::e_g729AnnexA },
		{ AST_FORMAT_G729A, H245_AudioCapability::e_g729 },
		{ AST_FORMAT_G726_AAL2, H245_AudioCapability::e_nonStandard, NULL, CISCO_G726r32 },
#ifdef AST_FORMAT_MODEM
		{ AST_FORMAT_MODEM, H245_DataApplicationCapability_application::e_t38fax },
#endif
		{ 0 }
	};

#if 0
	static const struct __codec__ vcodecs[] = {
#ifdef HAVE_H261
		{ AST_FORMAT_H261, H245_VideoCapability::e_h261VideoCapability },
#endif
#ifdef HAVE_H263
		{ AST_FORMAT_H263, H245_VideoCapability::e_h263VideoCapability },
#endif
#ifdef HAVE_H264
		{ AST_FORMAT_H264, H245_VideoCapability::e_genericVideoCapability, "0.0.8.241.0.0.1" },
#endif
		{ 0 }
	};
#endif
	struct ast_codec_pref prefs;
	RTP_DataFrame::PayloadTypes pt;

	if (!H323Connection::OnReceivedCapabilitySet(remoteCaps, muxCap, reject)) {
		return FALSE;
	}

	memset(&prefs, 0, sizeof(prefs));
	int peer_capabilities = 0;
	for (int i = 0; i < remoteCapabilities.GetSize(); ++i) {
		unsigned int subType = remoteCapabilities[i].GetSubType();
		if (h323debug) {
			cout << "Peer capability is " << remoteCapabilities[i] << endl;
		}
		switch(remoteCapabilities[i].GetMainType()) {
		case H323Capability::e_Audio:
			for (int x = 0; codecs[x].asterisk_codec > 0; ++x) {
				if ((subType == codecs[x].h245_cap) && (!codecs[x].formatName || (!strcmp(codecs[x].formatName, (const char *)remoteCapabilities[i].GetFormatName())))) {
					int ast_codec = codecs[x].asterisk_codec;
					int ms = 0;
					struct ast_format tmpfmt;
					if (!(peer_capabilities & ast_format_id_to_old_bitfield((enum ast_format_id) ast_codec))) {
						struct ast_format_list format;
						ast_codec_pref_append(&prefs, ast_format_set(&tmpfmt, (enum ast_format_id) ast_codec, 0));
						format = ast_codec_pref_getsize(&prefs, &tmpfmt);
						if ((ast_codec == AST_FORMAT_ALAW) || (ast_codec == AST_FORMAT_ULAW)) {
							ms = remoteCapabilities[i].GetTxFramesInPacket();
						} else
							ms = remoteCapabilities[i].GetTxFramesInPacket() * format.inc_ms;
						ast_codec_pref_setsize(&prefs, &tmpfmt, ms);
					}
					if (h323debug) {
						cout << "Found peer capability " << remoteCapabilities[i] << ", Asterisk code is " << ast_codec << ", frame size (in ms) is " << ms << endl;
					}
					peer_capabilities |= ast_format_id_to_old_bitfield((enum ast_format_id) ast_codec);
				}
			}
			break;
		case H323Capability::e_Data:
			if (!strcmp((const char *)remoteCapabilities[i].GetFormatName(), CISCO_DTMF_RELAY)) {
				pt = remoteCapabilities[i].GetPayloadType();
				if ((dtmfMode & H323_DTMF_CISCO) != 0) {
					on_set_rfc2833_payload(GetCallReference(), (const char *)GetCallToken(), (int)pt, 1);
//					if (sendUserInputMode == SendUserInputAsTone)
//						sendUserInputMode = SendUserInputAsInlineRFC2833;
				}
#ifdef PTRACING
				if (h323debug) {
					cout << "\t-- Outbound Cisco RTP DTMF on payload " << pt << endl;
				}
#endif
			}
			break;
		case H323Capability::e_UserInput:
			if (!strcmp((const char *)remoteCapabilities[i].GetFormatName(), H323_UserInputCapability::SubTypeNames[H323_UserInputCapability::SignalToneRFC2833])) {
				pt = remoteCapabilities[i].GetPayloadType();
				if ((dtmfMode & H323_DTMF_RFC2833) != 0) {
					on_set_rfc2833_payload(GetCallReference(), (const char *)GetCallToken(), (int)pt, 0);
//					if (sendUserInputMode == SendUserInputAsTone)
//						sendUserInputMode = SendUserInputAsInlineRFC2833;
				}
#ifdef PTRACING
				if (h323debug) {
					cout << "\t-- Outbound RFC2833 on payload " << pt << endl;
				}
#endif
			}
			break;
#if 0
		case H323Capability::e_Video:
			for (int x = 0; vcodecs[x].asterisk_codec > 0; ++x) {
				if (subType == vcodecs[x].h245_cap) {
					H245_CapabilityIdentifier *cap = NULL;
					H245_GenericCapability y;
					if (vcodecs[x].oid) {
						cap = new H245_CapabilityIdentifier(H245_CapabilityIdentifier::e_standard);
						PASN_ObjectId &object_id = *cap;
						object_id = vcodecs[x].oid;
						y.m_capabilityIdentifier = *cap;
					}
					if ((subType != H245_VideoCapability::e_genericVideoCapability) ||
							(vcodecs[x].oid && ((const H323GenericVideoCapability &)remoteCapabilities[i]).IsGenericMatch((const H245_GenericCapability)y))) {
						if (h323debug) {
							cout << "Found peer video capability " << remoteCapabilities[i] << ", Asterisk code is " << vcodecs[x].asterisk_codec << endl;
						}
						peer_capabilities |= vcodecs[x].asterisk_codec;
					}
					if (cap)
						delete(cap);
				}
			}
			break;
#endif
		default:
			break;
		}
	}

#if 0
	redir_capabilities &= peer_capabilities;
#endif
	if (on_setpeercapabilities)
		on_setpeercapabilities(GetCallReference(), (const char *)callToken, peer_capabilities, &prefs);

	return TRUE;
}

H323Channel * MyH323Connection::CreateRealTimeLogicalChannel(const H323Capability & capability,
									H323Channel::Directions dir,
									unsigned sessionID,
									const H245_H2250LogicalChannelParameters * /*param*/,
									RTP_QOS * /*param*/ )
{
	/* Do not open tx channel when transmitter has been paused by empty TCS */
	if ((dir == H323Channel::IsTransmitter) && transmitterSidePaused)
		return NULL;

	return new MyH323_ExternalRTPChannel(*this, capability, dir, sessionID);
}

/** This callback function is invoked once upon creation of each
  * channel for an H323 session
  */
PBoolean MyH323Connection::OnStartLogicalChannel(H323Channel & channel)
{
	/* Increase the count of channels we have open */
	channelsOpen++;

	if (h323debug) {
		cout << "\t-- Started logical channel: "
				<< ((channel.GetDirection() == H323Channel::IsTransmitter) ? "sending " : ((channel.GetDirection() == H323Channel::IsReceiver) ? "receiving " : " "))
				<< (const char *)(channel.GetCapability()).GetFormatName() << endl;
		cout << "\t\t-- channelsOpen = " << channelsOpen << endl;
	}
	return connectionState != ShuttingDownConnection;
}

void MyH323Connection::SetCapabilities(int caps, int dtmf_mode, void *_prefs, int pref_codec)
{
	PINDEX lastcap = -1; /* last common capability index */
	int alreadysent = 0;
	int codec;
	int x, y;
	struct ast_codec_pref *prefs = (struct ast_codec_pref *)_prefs;
	struct ast_format_list format;
	int frames_per_packet;
	struct ast_format tmpfmt;
	H323Capability *cap;

	localCapabilities.RemoveAll();

	/* Add audio codecs in preference order first, then
	   audio codecs without preference as allowed by mask */
	for (y = 0, x = -1; x < 32 + 32; ++x) {
		ast_format_clear(&tmpfmt);
		if (x < 0)
			codec = pref_codec;
		else if (y || (!(ast_codec_pref_index(prefs, x, &tmpfmt)))) {
			if (!y)
				y = 1;
			else
				y <<= 1;
			codec = y;
		}
		if (tmpfmt.id) {
			codec = ast_format_to_old_bitfield(&tmpfmt);
		}
		if (!(caps & codec) || (alreadysent & codec) || (AST_FORMAT_GET_TYPE(ast_format_id_from_old_bitfield(codec)) != AST_FORMAT_TYPE_AUDIO))
			continue;
		alreadysent |= codec;
		/* format.cur_ms will be set to default if packetization is not explicitly set */
		format = ast_codec_pref_getsize(prefs, ast_format_from_old_bitfield(&tmpfmt, codec));
		frames_per_packet = (format.inc_ms ? format.cur_ms / format.inc_ms : format.cur_ms);
		switch(ast_format_id_from_old_bitfield(codec)) {
#if 0
		case AST_FORMAT_SPEEX:
			/* Not real sure if Asterisk acutally supports all
			   of the various different bit rates so add them
			   all and figure it out later*/

			lastcap = localCapabilities.SetCapability(0, 0, new SpeexNarrow2AudioCapability());
			lastcap = localCapabilities.SetCapability(0, 0, new SpeexNarrow3AudioCapability());
			lastcap = localCapabilities.SetCapability(0, 0, new SpeexNarrow4AudioCapability());
			lastcap = localCapabilities.SetCapability(0, 0, new SpeexNarrow5AudioCapability());
			lastcap = localCapabilities.SetCapability(0, 0, new SpeexNarrow6AudioCapability());
			break;
#endif
		case AST_FORMAT_G729A:
			AST_G729ACapability *g729aCap;
			AST_G729Capability *g729Cap;
			lastcap = localCapabilities.SetCapability(0, 0, g729aCap = new AST_G729ACapability(frames_per_packet));
			lastcap = localCapabilities.SetCapability(0, 0, g729Cap = new AST_G729Capability(frames_per_packet));
			g729aCap->SetTxFramesInPacket(format.cur_ms);
			g729Cap->SetTxFramesInPacket(format.cur_ms);
			break;
		case AST_FORMAT_G723_1:
			AST_G7231Capability *g7231Cap;
			lastcap = localCapabilities.SetCapability(0, 0, g7231Cap = new AST_G7231Capability(frames_per_packet, TRUE));
			g7231Cap->SetTxFramesInPacket(format.cur_ms);
			lastcap = localCapabilities.SetCapability(0, 0, g7231Cap = new AST_G7231Capability(frames_per_packet, FALSE));
			g7231Cap->SetTxFramesInPacket(format.cur_ms);
			break;
		case AST_FORMAT_GSM:
			AST_GSM0610Capability *gsmCap;
			lastcap = localCapabilities.SetCapability(0, 0, gsmCap = new AST_GSM0610Capability(frames_per_packet));
			gsmCap->SetTxFramesInPacket(format.cur_ms);
			break;
		case AST_FORMAT_ULAW:
			AST_G711Capability *g711uCap;
			lastcap = localCapabilities.SetCapability(0, 0, g711uCap = new AST_G711Capability(format.cur_ms, H323_G711Capability::muLaw));
			g711uCap->SetTxFramesInPacket(format.cur_ms);
			break;
		case AST_FORMAT_ALAW:
			AST_G711Capability *g711aCap;
			lastcap = localCapabilities.SetCapability(0, 0, g711aCap = new AST_G711Capability(format.cur_ms, H323_G711Capability::ALaw));
			g711aCap->SetTxFramesInPacket(format.cur_ms);
			break;
		case AST_FORMAT_G726_AAL2:
			AST_CiscoG726Capability *g726Cap;
			lastcap = localCapabilities.SetCapability(0, 0, g726Cap = new AST_CiscoG726Capability(frames_per_packet));
			g726Cap->SetTxFramesInPacket(format.cur_ms);
			break;
		default:
			alreadysent &= ~codec;
			break;
		}
	}

	cap = new H323_UserInputCapability(H323_UserInputCapability::HookFlashH245);
	if (cap && cap->IsUsable(*this)) {
		lastcap++;
		lastcap = localCapabilities.SetCapability(0, lastcap, cap);
	} else {
		delete cap;				/* Capability is not usable */
	}

	dtmfMode = dtmf_mode;
	if (h323debug) {
		cout << "DTMF mode is " << (int)dtmfMode << endl;
	}
	if (dtmfMode) {
		lastcap++;
		if (dtmfMode == H323_DTMF_INBAND) {
			cap = new H323_UserInputCapability(H323_UserInputCapability::BasicString);
			if (cap && cap->IsUsable(*this)) {
				lastcap = localCapabilities.SetCapability(0, lastcap, cap);
			} else {
				delete cap;		/* Capability is not usable */
			}	
			sendUserInputMode = SendUserInputAsString;
		} else {
			if ((dtmfMode & H323_DTMF_RFC2833) != 0) {
				cap = new H323_UserInputCapability(H323_UserInputCapability::SignalToneRFC2833);
				if (cap && cap->IsUsable(*this))
					lastcap = localCapabilities.SetCapability(0, lastcap, cap);
				else {
					dtmfMode |= H323_DTMF_SIGNAL;
					delete cap;	/* Capability is not usable */
				}
			}
			if ((dtmfMode & H323_DTMF_CISCO) != 0) {
				/* Try Cisco's RTP DTMF relay too, but prefer RFC2833 or h245-signal */
				cap = new AST_CiscoDtmfCapability();
				if (cap && cap->IsUsable(*this)) {
					lastcap = localCapabilities.SetCapability(0, lastcap, cap);
					/* We cannot send Cisco RTP DTMFs, use h245-signal instead */
					dtmfMode |= H323_DTMF_SIGNAL;
				} else {
					dtmfMode |= H323_DTMF_SIGNAL;
					delete cap;	/* Capability is not usable */
				}
			}
			if ((dtmfMode & H323_DTMF_SIGNAL) != 0) {
				/* Cisco usually sends DTMF correctly only through h245-alphanumeric or h245-signal */
				cap = new H323_UserInputCapability(H323_UserInputCapability::SignalToneH245);
				if (cap && cap->IsUsable(*this))
					lastcap = localCapabilities.SetCapability(0, lastcap, cap);
				else
					delete cap;	/* Capability is not usable */
			}
			sendUserInputMode = SendUserInputAsTone;	/* RFC2833 transmission handled at Asterisk level */
		}
	}

	if (h323debug) {
		cout << "Allowed Codecs for " << GetCallToken() << " (" << GetSignallingChannel()->GetLocalAddress() << "):\n\t" << setprecision(2) << localCapabilities << endl;
	}
}

PBoolean MyH323Connection::StartControlChannel(const H225_TransportAddress & h245Address)
{
	// Check that it is an IP address, all we support at the moment
	if (h245Address.GetTag() != H225_TransportAddress::e_ipAddress
#if P_HAS_IPV6
		&& h245Address.GetTag() != H225_TransportAddress::e_ip6Address
#endif
	) {
		PTRACE(1, "H225\tConnect of H245 failed: Unsupported transport");
		return FALSE;
	}

	// Already have the H245 channel up.
	if (controlChannel != NULL)
		return TRUE;

	PIPSocket::Address addr;
	WORD port;
	GetSignallingChannel()->GetLocalAddress().GetIpAndPort(addr, port);
	if (addr) {
		if (h323debug)
			cout << "Using " << addr << " for outbound H.245 transport" << endl;
		controlChannel = new MyH323TransportTCP(endpoint, addr);
	} else
		controlChannel = new MyH323TransportTCP(endpoint);
	if (!controlChannel->SetRemoteAddress(h245Address)) {
		PTRACE(1, "H225\tCould not extract H245 address");
		delete controlChannel;
		controlChannel = NULL;
		return FALSE;
	}
	if (!controlChannel->Connect()) {
		PTRACE(1, "H225\tConnect of H245 failed: " << controlChannel->GetErrorText());
		delete controlChannel;
		controlChannel = NULL;
		return FALSE;
	}

	controlChannel->StartControlChannel(*this);
	return TRUE;
}

#ifdef H323_H450
void MyH323Connection::OnReceivedLocalCallHold(int linkedId)
{
	if (on_hold)
		on_hold(GetCallReference(), (const char *)GetCallToken(), 1);
}

void MyH323Connection::OnReceivedLocalCallRetrieve(int linkedId)
{
	if (on_hold)
		on_hold(GetCallReference(), (const char *)GetCallToken(), 0);
}
#endif

void MyH323Connection::MyHoldCall(PBoolean isHold)
{
	if (((holdHandling & H323_HOLD_NOTIFY) != 0) || ((holdHandling & H323_HOLD_Q931ONLY) != 0)) {
		PBYTEArray x ((const BYTE *)(isHold ? "\xF9" : "\xFA"), 1);
		H323SignalPDU signal;
		signal.BuildNotify(*this);
		signal.GetQ931().SetIE((Q931::InformationElementCodes)39 /* Q931::NotifyIE */, x);
		if (h323debug)
			cout << "Sending " << (isHold ? "HOLD" : "RETRIEVE") << " notification: " << signal << endl;
		if ((holdHandling & H323_HOLD_Q931ONLY) != 0) {
			PBYTEArray rawData;
			signal.GetQ931().RemoveIE(Q931::UserUserIE);
			signal.GetQ931().Encode(rawData);
			signallingChannel->WritePDU(rawData);
		} else
			WriteSignalPDU(signal);
	}
#ifdef H323_H450
	if ((holdHandling & H323_HOLD_H450) != 0) {
		if (isHold)
			h4504handler->HoldCall(TRUE);
		else if (IsLocalHold())
			h4504handler->RetrieveCall();
	}
#endif
}


/* MyH323_ExternalRTPChannel */
MyH323_ExternalRTPChannel::MyH323_ExternalRTPChannel(MyH323Connection & connection,
							const H323Capability & capability,
							Directions direction,
							unsigned id)
	: H323_ExternalRTPChannel::H323_ExternalRTPChannel(connection, capability, direction, id)
{
	struct rtp_info *info;

	/* Determine the Local (A side) IP Address and port */
	info = on_external_rtp_create(connection.GetCallReference(), (const char *)connection.GetCallToken());
	if (!info) {
		cout << "\tERROR: on_external_rtp_create failure" << endl;
		return;
	} else {
		localIpAddr = info->addr;
		localPort = info->port;
		/* tell the H.323 stack */
		SetExternalAddress(H323TransportAddress(localIpAddr, localPort), H323TransportAddress(localIpAddr, localPort + 1));
		/* clean up allocated memory */
		ast_free(info);
	}

	/* Get the payload code	*/
	OpalMediaFormat format(capability.GetFormatName(), FALSE);
	payloadCode = format.GetPayloadType();
}

MyH323_ExternalRTPChannel::~MyH323_ExternalRTPChannel()
{
	if (h323debug) {
		cout << "\tExternalRTPChannel Destroyed" << endl;
	}
}

PBoolean MyH323_ExternalRTPChannel::Start(void)
{
	/* Call ancestor first */
	if (!H323_ExternalRTPChannel::Start()) {
		return FALSE;
	}

	if (h323debug) {
		cout << "\t\tExternal RTP Session Starting" << endl;
		cout << "\t\tRTP channel id " << sessionID << " parameters:" << endl;
	}

	/* Collect the remote information */
	H323_ExternalRTPChannel::GetRemoteAddress(remoteIpAddr, remotePort);

	if (h323debug) {
		cout << "\t\t-- remoteIpAddress: " << remoteIpAddr << endl;
		cout << "\t\t-- remotePort: " << remotePort << endl;
		cout << "\t\t-- ExternalIpAddress: " << localIpAddr << endl;
		cout << "\t\t-- ExternalPort: " << localPort << endl;
	}
	/* Notify Asterisk of remote RTP information */
	on_start_rtp_channel(connection.GetCallReference(), (const char *)remoteIpAddr.AsString(), remotePort,
		(const char *)connection.GetCallToken(), (int)payloadCode);
	return TRUE;
}

PBoolean MyH323_ExternalRTPChannel::OnReceivedAckPDU(const H245_H2250LogicalChannelAckParameters & param)
{
	if (h323debug) {
		cout << "	MyH323_ExternalRTPChannel::OnReceivedAckPDU" << endl;
	}

	if (H323_ExternalRTPChannel::OnReceivedAckPDU(param)) {
		GetRemoteAddress(remoteIpAddr, remotePort);
		if (h323debug) {
			cout << "		-- remoteIpAddress: " << remoteIpAddr << endl;
			cout << "		-- remotePort: " << remotePort << endl;
		}
		on_start_rtp_channel(connection.GetCallReference(), (const char *)remoteIpAddr.AsString(),
				remotePort, (const char *)connection.GetCallToken(), (int)payloadCode);
		return TRUE;
	}
	return FALSE;
}

#ifdef H323_H450
MyH4504Handler::MyH4504Handler(MyH323Connection &_conn, H450xDispatcher &_disp)
	:H4504Handler(_conn, _disp)
{
	conn = &_conn;
}

void MyH4504Handler::OnReceivedLocalCallHold(int linkedId)
{
	if (conn) {
		conn->Lock();
		conn->OnReceivedLocalCallHold(linkedId);
		conn->Unlock();
	}
}

void MyH4504Handler::OnReceivedLocalCallRetrieve(int linkedId)
{
	if (conn) {
		conn->Lock();
		conn->OnReceivedLocalCallRetrieve(linkedId);
		conn->Unlock();
	}
}
#endif


/** IMPLEMENTATION OF C FUNCTIONS */

/**
 * The extern "C" directive takes care for
 * the ANSI-C representation of linkable symbols
 */

extern "C" {

int h323_end_point_exist(void)
{
	if (!endPoint) {
		return 0;
	}
	return 1;
}

void h323_end_point_create(void)
{
	channelsOpen = 0;
	logstream = new PAsteriskLog();
	PTrace::SetStream(logstream); 
	endPoint = new MyH323EndPoint();
}

void h323_gk_urq(void)
{
	if (!h323_end_point_exist()) {
		cout << " ERROR: [h323_gk_urq] No Endpoint, this is bad" << endl;
		return;
	}
	endPoint->RemoveGatekeeper();
}

void h323_debug(int flag, unsigned level)
{
	if (flag) {
		PTrace:: SetLevel(level);
	} else {
		PTrace:: SetLevel(0);
	}
}

/** Installs the callback functions on behalf of the PBX application */
void h323_callback_register(setup_incoming_cb		ifunc,
							setup_outbound_cb		sfunc,
							on_rtp_cb				rtpfunc,
							start_rtp_cb			lfunc,
							clear_con_cb			clfunc,
							chan_ringing_cb			rfunc,
							con_established_cb		efunc,
							receive_digit_cb		dfunc,
							answer_call_cb			acfunc,
							progress_cb				pgfunc,
							rfc2833_cb				dtmffunc,
							hangup_cb				hangupfunc,
							setcapabilities_cb		capabilityfunc,
							setpeercapabilities_cb	peercapabilityfunc,
							onhold_cb				holdfunc)
{
	on_incoming_call = ifunc;
	on_outgoing_call = sfunc;
	on_external_rtp_create = rtpfunc;
	on_start_rtp_channel = lfunc;
	on_connection_cleared = clfunc;
	on_chan_ringing = rfunc;
	on_connection_established = efunc;
	on_receive_digit = dfunc;
	on_answer_call = acfunc;
	on_progress = pgfunc;
	on_set_rfc2833_payload = dtmffunc;
	on_hangup = hangupfunc;
	on_setcapabilities = capabilityfunc;
	on_setpeercapabilities = peercapabilityfunc;
	on_hold = holdfunc;
}

/**
 * Add capability to the capability table of the end point.
 */
int h323_set_capabilities(const char *token, int cap, int dtmf_mode, struct ast_codec_pref *prefs, int pref_codec)
{
	MyH323Connection *conn;

	if (!h323_end_point_exist()) {
		cout << " ERROR: [h323_set_capablities] No Endpoint, this is bad" << endl;
		return 1;
	}
	if (!token || !*token) {
		cout << " ERROR: [h323_set_capabilities] Invalid call token specified." << endl;
		return 1;
	}

	PString myToken(token);
	conn = (MyH323Connection *)endPoint->FindConnectionWithLock(myToken);
	if (!conn) {
		cout << " ERROR: [h323_set_capabilities] Unable to find connection " << token << endl;
		return 1;
	}
	conn->SetCapabilities((/*conn->bridging ? conn->redir_capabilities :*/ cap), dtmf_mode, prefs, pref_codec);
	conn->Unlock();

	return 0;
}

/** Start the H.323 listener */
int h323_start_listener(int listenPort, struct sockaddr_in bindaddr)
{

	if (!h323_end_point_exist()) {
		cout << "ERROR: [h323_start_listener] No Endpoint, this is bad!" << endl;
		return 1;
	}

	PIPSocket::Address interfaceAddress(bindaddr.sin_addr);
	if (!listenPort) {
		listenPort = 1720;
	}
	/** H.323 listener */
	H323ListenerTCP *tcpListener;
	tcpListener = new H323ListenerTCP(*endPoint, interfaceAddress, (WORD)listenPort);
	if (!endPoint->StartListener(tcpListener)) {
		cout << "ERROR: Could not open H.323 listener port on " << ((H323ListenerTCP *) tcpListener)->GetListenerPort() << endl;
		delete tcpListener;
		return 1;
	}
	cout << "  == H.323 listener started" << endl;
	return 0;
};

/* Addition of functions just to make the channel driver compile with H323Plus */
#if VERSION(OPENH323_MAJOR, OPENH323_MINOR, OPENH323_BUILD) > VERSION(1,19,4)
/* Alternate RTP port information for Same NAT */
BOOL MyH323_ExternalRTPChannel::OnReceivedAltPDU(const H245_ArrayOf_GenericInformation & alternate )
{
	return TRUE;
}

/* Alternate RTP port information for Same NAT */
BOOL MyH323_ExternalRTPChannel::OnSendingAltPDU(H245_ArrayOf_GenericInformation & alternate) const
{
	return TRUE;
}

/* Alternate RTP port information for Same NAT */
void MyH323_ExternalRTPChannel::OnSendOpenAckAlt(H245_ArrayOf_GenericInformation & alternate) const
{
}

/* Alternate RTP port information for Same NAT */
BOOL MyH323_ExternalRTPChannel::OnReceivedAckAltPDU(const H245_ArrayOf_GenericInformation & alternate)
{
	return TRUE;
}
#endif


int h323_set_alias(struct oh323_alias *alias)
{
	char *p;
	char *num;
	PString h323id(alias->name);
	PString e164(alias->e164);
	char *prefix;

	if (!h323_end_point_exist()) {
		cout << "ERROR: [h323_set_alias] No Endpoint, this is bad!" << endl;
		return 1;
	}

	cout << "== Adding alias \"" << h323id << "\" to endpoint" << endl;
	endPoint->AddAliasName(h323id);
	endPoint->RemoveAliasName(PProcess::Current().GetName());

	if (!e164.IsEmpty()) {
		cout << "== Adding E.164 \"" << e164 << "\" to endpoint" << endl;
		endPoint->AddAliasName(e164);
	}
	if (strlen(alias->prefix)) {
		p = prefix = strdup(alias->prefix);
		while((num = strsep(&p, ",")) != (char *)NULL) {
			cout << "== Adding Prefix \"" << num << "\" to endpoint" << endl;
			endPoint->SupportedPrefixes += PString(num);
			endPoint->SetGateway();
		}
		if (prefix)
			ast_free(prefix);
	}
	return 0;
}

void h323_set_id(char *id)
{
	PString h323id(id);

	if (h323debug) {
		cout << "  == Using '" << h323id << "' as our H.323ID for this call" << endl;
	}
	/* EVIL HACK */
	endPoint->SetLocalUserName(h323id);
}

void h323_show_tokens(void)
{
	cout << "Current call tokens: " << setprecision(2) << endPoint->GetAllConnections() << endl;
}

void h323_show_version(void)
{
    cout << "H.323 version: " << OPENH323_MAJOR << "." << OPENH323_MINOR << "." << OPENH323_BUILD << endl;
}

/** Establish Gatekeeper communiations, if so configured,
  *	register aliases for the H.323 endpoint to respond to.
  */
int h323_set_gk(int gatekeeper_discover, char *gatekeeper, char *secret)
{
	PString gkName = PString(gatekeeper);
	PString pass = PString(secret);
	H323TransportUDP *rasChannel;

	if (!h323_end_point_exist()) {
		cout << "ERROR: [h323_set_gk] No Endpoint, this is bad!" << endl;
		return 1;
	}

	if (!gatekeeper) {
		cout << "Error: Gatekeeper cannot be NULL" << endl;
		return 1;
	}
	if (strlen(secret)) {
		endPoint->SetGatekeeperPassword(pass);
	}
	if (gatekeeper_discover) {
		/* discover the gk using multicast */
		if (endPoint->DiscoverGatekeeper(new MyH323TransportUDP(*endPoint))) {
			cout << "== Using " << (endPoint->GetGatekeeper())->GetName() << " as our Gatekeeper." << endl;
		} else {
			cout << "Warning: Could not find a gatekeeper." << endl;
			return 1;
		}
	} else {
		rasChannel = new MyH323TransportUDP(*endPoint);

		if (!rasChannel) {
			cout << "Error: No RAS Channel, this is bad" << endl;
			return 1;
		}
		if (endPoint->SetGatekeeper(gkName, rasChannel)) {
			cout << "== Using " << (endPoint->GetGatekeeper())->GetName() << " as our Gatekeeper." << endl;
		} else {
			cout << "Error registering with gatekeeper \"" << gkName << "\". " << endl;
			/* XXX Maybe we should fire a new thread to attempt to re-register later and not kill asterisk here? */
			return 1;
		}
	}
	return 0;
}

/** Send a DTMF tone over the H323Connection with the
  * specified token.
  */
void h323_send_tone(const char *call_token, char tone)
{
	if (!h323_end_point_exist()) {
		cout << "ERROR: [h323_send_tone] No Endpoint, this is bad!" << endl;
		return;
	}
	PString token = PString(call_token);
	endPoint->SendUserTone(token, tone);
}

/** Make a call to the remote endpoint.
  */
int h323_make_call(char *dest, call_details_t *cd, call_options_t *call_options)
{
	int res;
	PString	token;
	PString	host(dest);

	if (!h323_end_point_exist()) {
		return 1;
	}

	res = endPoint->MyMakeCall(host, token, &cd->call_reference, call_options);
	memcpy((char *)(cd->call_token), (const unsigned char *)token, token.GetLength());
	return res;
};

int h323_clear_call(const char *call_token, int cause)
{
	H225_ReleaseCompleteReason dummy;
	H323Connection::CallEndReason r = H323Connection::EndedByLocalUser;
	MyH323Connection *connection;
	const PString currentToken(call_token);

	if (!h323_end_point_exist()) {
		return 1;
	}

	if (cause) {
		r = H323TranslateToCallEndReason((Q931::CauseValues)(cause), dummy);
	}

	connection = (MyH323Connection *)endPoint->FindConnectionWithLock(currentToken);
	if (connection) {
		connection->SetCause(cause);
		connection->SetCallEndReason(r);
		connection->Unlock();
	}
	endPoint->ClearCall(currentToken, r);
	return 0;
};

/* Send Alerting PDU to H.323 caller */
int h323_send_alerting(const char *token)
{
	const PString currentToken(token);
	H323Connection * connection;

	if (h323debug) {
		cout << "\tSending alerting" << endl;
	}
	connection = endPoint->FindConnectionWithLock(currentToken);
	if (!connection) {
		cout << "No connection found for " << token << endl;
		return -1;
	}
	connection->AnsweringCall(H323Connection::AnswerCallPending);
	connection->Unlock();
	return 0;
}

/* Send Progress PDU to H.323 caller */
int h323_send_progress(const char *token)
{
	const PString currentToken(token);
	H323Connection * connection;

	connection = endPoint->FindConnectionWithLock(currentToken);
	if (!connection) {
		cout << "No connection found for " << token << endl;
		return -1;
	}
#if 1
	((MyH323Connection *)connection)->MySendProgress();
#else
	connection->AnsweringCall(H323Connection::AnswerCallDeferredWithMedia);
#endif
	connection->Unlock();
	return 0;
}

/** This function tells the h.323 stack to either
    answer or deny an incoming call */
int h323_answering_call(const char *token, int busy)
{
	const PString currentToken(token);
	H323Connection * connection;

	connection = endPoint->FindConnectionWithLock(currentToken);

	if (!connection) {
		cout << "No connection found for " << token << endl;
		return -1;
	}
	if (!busy) {
		if (h323debug) {
			cout << "\tAnswering call " << token << endl;
		}
		connection->AnsweringCall(H323Connection::AnswerCallNow);
	} else {
		if (h323debug) {
			cout << "\tdenying call " << token << endl;
		}
		connection->AnsweringCall(H323Connection::AnswerCallDenied);
	}
	connection->Unlock();
	return 0;
}

int h323_soft_hangup(const char *data)
{
	PString token(data);
	PBoolean result;
	cout << "Soft hangup" << endl;
	result = endPoint->ClearCall(token);
	return result;
}

/* alas, this doesn't work :( */
void h323_native_bridge(const char *token, const char *them, char *capability)
{
	H323Channel *channel;
	MyH323Connection *connection = (MyH323Connection *)endPoint->FindConnectionWithLock(token);

	if (!connection) {
		cout << "ERROR: No connection found, this is bad" << endl;
		return;
	}

	cout << "Native Bridge:  them [" << them << "]" << endl;

	channel = connection->FindChannel(connection->sessionId, TRUE);
	connection->bridging = TRUE;
	connection->CloseLogicalChannelNumber(channel->GetNumber());

	connection->Unlock();
	return;

}

int h323_hold_call(const char *token, int is_hold)
{
	MyH323Connection *conn = (MyH323Connection *)endPoint->FindConnectionWithLock(token);
	if (!conn) {
		cout << "ERROR: No connection found, this is bad" << endl;
		return -1;
	}
	conn->MyHoldCall((PBoolean)is_hold);
	conn->Unlock();
	return 0;
}

#undef cout
#undef endl
void h323_end_process(void)
{
	if (endPoint) {
		delete endPoint;
		endPoint = NULL;
	}
#ifndef SKIP_PWLIB_PIPE_BUG_WORKAROUND
	close(_timerChangePipe[0]);
	close(_timerChangePipe[1]);
#endif
	if (logstream) {
		PTrace::SetStream(NULL);
		delete logstream;
		logstream = NULL;
	}
}

} /* extern "C" */

