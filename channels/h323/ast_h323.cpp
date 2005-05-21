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
#include <arpa/inet.h>

#include <list>
#include <string>
#include <algorithm>

#include <ptlib.h>
#include <h323.h>
#include <h323pdu.h>
#include <mediafmt.h>
#include <lid.h>

#ifdef __cplusplus
extern "C" {
#endif   
#include "asterisk/logger.h"
#ifdef __cplusplus
}
#endif

#include "chan_h323.h"
#include "ast_h323.h"

/* PWlib Required Components  */
#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define BUILD_TYPE    ReleaseCode
#define BUILD_NUMBER  0

/** Counter for the number of connections */
int channelsOpen;

/* DTMF Mode */
int mode = H323_DTMF_RFC2833;

/**
 * We assume that only one endPoint should exist.
 * The application cannot run the h323_end_point_create() more than once
 * FIXME: Singleton this, for safety
 */
MyH323EndPoint *endPoint = NULL;

/** PWLib entry point */
MyProcess *localProcess = NULL;

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
	char *str = strdup(string);
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
	free(str);

	string = PString();
	char *base = string.GetPointer(2000);
	setp(base, base + string.GetSize() - 1);
	return 0;
}

#define cout (*logstream)

MyProcess::MyProcess(): PProcess("The NuFone Network's", "H.323 Channel Driver for Asterisk",
             MAJOR_VERSION, MINOR_VERSION, BUILD_TYPE, BUILD_NUMBER)
{
	/* Fix missed one in PWLib */
	PX_firstTimeStart = FALSE;
	Resume();
}

void MyProcess::Main()
{
	cout << "  == Creating H.323 Endpoint" << endl;
	endPoint = new MyH323EndPoint();
	PTrace::Initialise(0, NULL, PTrace::Timestamp | PTrace::Thread | PTrace::FileAndLine);
	PTrace::SetStream(logstream);
}

void PAssertFunc(const char *msg)
{
	ast_log(LOG_ERROR, "%s\n", msg);
	/* XXX: Probably we need to crash here */
}

H323_REGISTER_CAPABILITY(H323_G7231Capability, OPAL_G7231);
H323_REGISTER_CAPABILITY(AST_G729Capability,  OPAL_G729);
H323_REGISTER_CAPABILITY(AST_G729ACapability, OPAL_G729A);

H323_G7231Capability::H323_G7231Capability(BOOL annexA_)
  : H323AudioCapability(7, 4)
{
  	annexA = annexA_;
}

PObject::Comparison H323_G7231Capability::Compare(const PObject & obj) const
{
  	Comparison result = H323AudioCapability::Compare(obj);
  	if (result != EqualTo) {
    		return result;
	}
  	PINDEX otherAnnexA = ((const H323_G7231Capability &)obj).annexA;
  	if (annexA < otherAnnexA) {
    		return LessThan;
	}
  	if (annexA > otherAnnexA) {
    		return GreaterThan;
	}
	return EqualTo;
}

PObject * H323_G7231Capability::Clone() const
{
 	 return new H323_G7231Capability(*this);
}

PString H323_G7231Capability::GetFormatName() const
{
  	return OPAL_G7231;
}

unsigned H323_G7231Capability::GetSubType() const
{
  	return H245_AudioCapability::e_g7231;
}

BOOL H323_G7231Capability::OnSendingPDU(H245_AudioCapability & cap,
                                          unsigned packetSize) const
{
  	cap.SetTag(H245_AudioCapability::e_g7231);
  	H245_AudioCapability_g7231 & g7231 = cap;
  	g7231.m_maxAl_sduAudioFrames = packetSize;
  	g7231.m_silenceSuppression = annexA;
  	return TRUE;
}

BOOL H323_G7231Capability::OnReceivedPDU(const H245_AudioCapability & cap,
                                           unsigned & packetSize)
{
	if (cap.GetTag() != H245_AudioCapability::e_g7231) {
    		return FALSE;
	}
  	const H245_AudioCapability_g7231 & g7231 = cap;
  	packetSize = g7231.m_maxAl_sduAudioFrames;
  	annexA = g7231.m_silenceSuppression;
  	return TRUE;
}

H323Codec * H323_G7231Capability::CreateCodec(H323Codec::Direction direction) const
{
  	return NULL;
}

AST_G729Capability::AST_G729Capability()
  : H323AudioCapability(24, 2)
{
}

PObject * AST_G729Capability::Clone() const
{
  	return new AST_G729Capability(*this);
}

unsigned AST_G729Capability::GetSubType() const
{
  	return H245_AudioCapability::e_g729;
}

PString AST_G729Capability::GetFormatName() const
{
  	return OPAL_G729;
}

H323Codec * AST_G729Capability::CreateCodec(H323Codec::Direction direction) const
{
  	return NULL;
}

AST_G729ACapability::AST_G729ACapability()
  : H323AudioCapability(24, 6)
{
}

PObject * AST_G729ACapability::Clone() const
{
  	return new AST_G729ACapability(*this);
}

unsigned AST_G729ACapability::GetSubType() const
{
  	return H245_AudioCapability::e_g729AnnexA;
}

PString AST_G729ACapability::GetFormatName() const
{
  	return OPAL_G729A;
}

H323Codec * AST_G729ACapability::CreateCodec(H323Codec::Direction direction) const
{
  	return NULL;
}

/** MyH323EndPoint 
  */
MyH323EndPoint::MyH323EndPoint()
		: H323EndPoint()
{
	// Capabilities will be negotiated on per-connection basis
	capabilities.RemoveAll();
}

/** The fullAddress parameter is used directly in the MakeCall method so
  * the General form for the fullAddress argument is :
  * [alias@][transport$]host[:port]
  * default values:	alias = the same value as host.
  *					transport = ip.
  *					port = 1720.
  */
int MyH323EndPoint::MakeCall(const PString & dest, PString & token, unsigned int *callReference, call_options_t *opts)
{
	PString fullAddress;
	MyH323Connection * connection;

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
	}
	if (!(connection = (MyH323Connection *)H323EndPoint::MakeCallLocked(fullAddress, token, opts))) {
		if (h323debug) {
			cout << "Error making call to \"" << fullAddress << '"' << endl;
		}
		return 1;
	}
	*callReference = connection->GetCallReference();	

	if (opts->cid_num) {
		connection->ast_cid_num = PString(opts->cid_num);
	}
	if (opts->cid_name) {
		connection->ast_cid_name = PString(opts->cid_name);
		connection->SetLocalPartyName(connection->ast_cid_name);
	}

	connection->dtmfCodec = (RTP_DataFrame::PayloadTypes)opts->dtmfcodec;

	if (h323debug) {
		cout << "\t-- " << GetLocalUserName() << " is calling host " << fullAddress << endl;
		cout << "\t-- Call token is " << (const char *)token << endl;
		cout << "\t-- Call reference is " << *callReference << endl;
		cout << "\t-- DTMF Payload is " << connection->dtmfCodec << endl;
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
    		H323SetAliasAddress(SupportedPrefixes[p], ((H225_VoiceCaps &)protocol).m_supportedPrefixes[p].m_prefix);
  	}
}

void MyH323EndPoint::SetGateway(void)
{
	terminalType = e_GatewayOnly;
}

BOOL MyH323EndPoint::ClearCall(const PString & token, H323Connection::CallEndReason reason)
{
	if (h323debug) {
		cout << "\t-- ClearCall: Request to clear call with token " << token << ", cause " << reason << endl;
	}
	return H323EndPoint::ClearCall(token, reason);
}

BOOL MyH323EndPoint::ClearCall(const PString & token)
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

BOOL MyH323EndPoint::OnConnectionForwarded(H323Connection & connection,
 		const PString & forwardParty,
 		const H323SignalPDU & pdu)
 {
 	if (h323debug) {
	 	cout << "\t-- Call Forwarded to " << forwardParty << endl;
 	}
	return FALSE;
 }
 
BOOL MyH323EndPoint::ForwardConnection(H323Connection & connection,
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
			if (h323debug)
				cout << " -- Call with " << remoteName << " completed (" << connection.GetCallEndReason() << ")" << endl;

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

H323Connection * MyH323EndPoint::CreateConnection(unsigned callReference, void *o)
{
	unsigned options = 0;
	call_options_t *opts = (call_options_t *)o;

	if (opts && opts->noFastStart) {
		options |= H323Connection::FastStartOptionDisable;
	} else {
		options |= H323Connection::FastStartOptionEnable;
	}
	if (opts && opts->noH245Tunneling) {
		options |= H323Connection::H245TunnelingOptionDisable;
	} else {
		options |= H323Connection::H245TunnelingOptionEnable;
	}
/* Disable until I can figure out the proper way to deal with this */
#if 0
	if (opts->noSilenceSuppression) {
		options |= H323Connection::SilenceSuppresionOptionDisable;
	} else {
		options |= H323Connection::SilenceSUppressionOptionEnable;
	}
#endif
	return new MyH323Connection(*this, callReference, options);
}

/* MyH323Connection Implementation */    
MyH323Connection::MyH323Connection(MyH323EndPoint & ep, unsigned callReference,
							unsigned options)
	: H323Connection(ep, callReference, options)
{
	cause = -1;
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

BOOL MyH323Connection::OnReceivedProgress(const H323SignalPDU &pdu)
{
	BOOL isInband;
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

H323Connection::AnswerCallResponse MyH323Connection::OnAnswerCall(const PString & caller,
								  const H323SignalPDU & setupPDU,
								  H323SignalPDU & /*connectPDU*/)
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
	if (pi) {
		alertingPDU->GetQ931().SetProgressIndicator(pi);
	}
	if (h323debug) {
		cout << "\t\t- Inserting PI of " << pi << " into ALERTING message" << endl;
	}

	if (!on_answer_call(GetCallReference(), (const char *)GetCallToken())) {
		return H323Connection::AnswerCallDenied;
	}
	/* The call will be answered later with "AnsweringCall()" function.
	 */ 
	return H323Connection::AnswerCallDeferredWithMedia;
}

BOOL MyH323Connection::OnAlerting(const H323SignalPDU & alertingPDU, const PString & username)
{
	if (h323debug) {
	        cout << "\t=-= In OnAlerting for call " << GetCallReference()
	              << ": sessionId=" << sessionId << endl;
                 cout << "\t-- Ringing phone for \"" << username << "\"" << endl;
	}

	if (on_progress) {
		BOOL isInband;
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

BOOL MyH323Connection::OnReceivedSignalSetup(const H323SignalPDU & setupPDU)
{
	call_details_t cd;
	PString sourceE164;
	PString destE164;
	PString sourceName;
	PString sourceAliases;	
	PString destAliases;
	PIPSocket::Address Ip;
	WORD sourcePort;
	char *s, *s1; 

	if (h323debug) {
		cout << ("\t--Received SETUP message\n");
	}

	if (connectionState == ShuttingDownConnection)
		return FALSE;

	sourceAliases = setupPDU.GetSourceAliases();
	destAliases = setupPDU.GetDestinationAlias();
			
	sourceE164 = "";
	setupPDU.GetSourceE164(sourceE164);
	sourceName = "";
	sourceName=setupPDU.GetQ931().GetDisplayName();
	destE164 = "";
	setupPDU.GetDestinationE164(destE164);

	/* Convert complex strings */
	//  FIXME: deal more than one source alias 
    	if ((s = strchr(sourceAliases, ' ')) != NULL) {
                *s = '\0';
	}
    	if ((s = strchr(sourceAliases, '\t')) != NULL) {
                *s = '\0';
	}
 	if ((s1 = strchr(destAliases, ' ')) != NULL) {
         	*s1 = '\0';
	}
	if ((s1 = strchr(destAliases, '\t')) != NULL) {
         	*s1 = '\0';
	}

	memset(&cd, 0, sizeof(cd));
	cd.call_reference = GetCallReference();
	cd.call_token = strdup((const char *)GetCallToken());
	cd.call_source_aliases = strdup((const char *)sourceAliases);
	cd.call_dest_alias = strdup((const char *)destAliases);
	cd.call_source_e164 = strdup((const char *)sourceE164);
	cd.call_dest_e164 = strdup((const char *)destE164);
	cd.call_source_name = strdup((const char *)sourceName);

	GetSignallingChannel()->GetRemoteAddress().GetIpAndPort(Ip, sourcePort);
 	cd.sourceIp = strdup((const char *)Ip.AsString());

	/* Notify Asterisk of the request */
	call_options_t *res = on_incoming_call(&cd);

	if (!res) {
		if (h323debug) {
			cout << "	-- Call Failed" << endl;
		}
		return FALSE;
	}

	progressSetup = res->progress_setup;
	progressAlert = res->progress_alert;
	dtmfCodec = (RTP_DataFrame::PayloadTypes)res->dtmfcodec;


	return H323Connection::OnReceivedSignalSetup(setupPDU);
}

BOOL MyH323Connection::OnSendSignalSetup(H323SignalPDU & setupPDU)
{
	call_details_t cd;
	char *s, *s1;

	if (h323debug) { 
		cout << "	-- Sending SETUP message" << endl;
	}

	if (connectionState == ShuttingDownConnection)
		return FALSE;

	if (!ast_cid_num.IsEmpty()) {
		setupPDU.GetQ931().SetCallingPartyNumber(ast_cid_num);
	}

	if (!ast_cid_name.IsEmpty()) {
		setupPDU.GetQ931().SetDisplayName(ast_cid_name);
	}

	sourceAliases = setupPDU.GetSourceAliases();
	destAliases = setupPDU.GetDestinationAlias();

	sourceE164 = "";
	setupPDU.GetSourceE164(sourceE164);
	destE164 = "";
	setupPDU.GetDestinationE164(destE164);

	/* Convert complex strings */
	//  FIXME: deal more than one source alias 
	
    	if ((s = strchr(sourceAliases, ' ')) != NULL) {
                *s = '\0';
	}
    	if ((s = strchr(sourceAliases, '\t')) != NULL) {
                *s = '\0';
	}
    	if ((s1 = strchr(destAliases, ' ')) != NULL) {
        	 *s1 = '\0';
	}
	if ((s1 = strchr(destAliases, '\t')) != NULL) {
         	*s1 = '\0';
	}

	memset(&cd, 0, sizeof(cd));
	cd.call_reference = GetCallReference();
	cd.call_token = strdup((const char *)GetCallToken());
	cd.call_source_aliases = strdup((const char *)sourceAliases);
	cd.call_dest_alias = strdup((const char *)destAliases);
	cd.call_source_e164 = strdup((const char *)sourceE164);
	cd.call_dest_e164 = strdup((const char *)destE164);

	int res = on_outgoing_call(&cd);
	if (!res) {
		if (h323debug) {
			cout << "\t-- Call Failed" << endl;
		}
		return FALSE;
	}

	if (progressSetup) {
		setupPDU.GetQ931().SetProgressIndicator(progressSetup);
	}
	return H323Connection::OnSendSignalSetup(setupPDU);
}

BOOL MyH323Connection::OnSendReleaseComplete(H323SignalPDU & releaseCompletePDU)
{
	if (h323debug) {
		cout << "\t-- Sending RELEASE COMPLETE" << endl;
	}
	if (cause > 0)
		releaseCompletePDU.GetQ931().SetCause((Q931::CauseValues)cause);
	return H323Connection::OnSendReleaseComplete(releaseCompletePDU);
}

BOOL MyH323Connection::OnReceivedFacility(const H323SignalPDU & pdu)
{
	if (h323debug) {
		cout << "\t-- Received Facility message... " << endl;
	}	
	return H323Connection::OnReceivedFacility(pdu);
}

void MyH323Connection::OnReceivedReleaseComplete(const H323SignalPDU & pdu)
{
	if (h323debug) {
		cout <<  "\t-- Received RELEASE COMPLETE message..." << endl;
	}
	if (on_hangup)
		on_hangup(GetCallReference(), (const char *)GetCallToken(), pdu.GetQ931().GetCause());
	return H323Connection::OnReceivedReleaseComplete(pdu);
}

BOOL MyH323Connection::OnClosingLogicalChannel(H323Channel & channel)
{
	if (h323debug) {
		cout << "\t-- Closing logical channel..." << endl;
	}
	return H323Connection::OnClosingLogicalChannel(channel);
}

void MyH323Connection::SendUserInputTone(char tone, unsigned duration)
{
	if (h323debug) {
		cout << "\t-- Sending user input tone (" << tone << ") to remote" << endl;
	}
	on_send_digit(GetCallReference(), tone, (const char *)GetCallToken());	
	H323Connection::SendUserInputTone(tone, duration);
}

void MyH323Connection::OnUserInputTone(char tone, unsigned duration, unsigned logicalChannel, unsigned rtpTimestamp)
{
	if (mode == H323_DTMF_INBAND) {
		if (h323debug) {
			cout << "\t-- Received user input tone (" << tone << ") from remote" << endl;
		}
		on_send_digit(GetCallReference(), tone, (const char *)GetCallToken());
	}
	H323Connection::OnUserInputTone(tone, duration, logicalChannel, rtpTimestamp);
}

void MyH323Connection::OnUserInputString(const PString &value)
{
	if (mode == H323_DTMF_RFC2833) {
		if (h323debug) {
			cout <<  "\t-- Received user input string (" << value << ") from remote." << endl;
		}
		on_send_digit(GetCallReference(), value[0], (const char *)GetCallToken());
	}	
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
				atec.m_dynamicRTPPayloadType = dtmfCodec;
				on_set_rfc2833_payload(GetCallReference(), (const char *)GetCallToken(), (int)dtmfCodec);
				if (h323debug) {
					cout << "\t-- Transmitting RFC2833 on payload " <<
						atec.m_dynamicRTPPayloadType << endl;
				}
			}
		}
	}
}

void MyH323Connection::OnSetLocalCapabilities()
{
	if (on_setcapabilities)
		on_setcapabilities(GetCallReference(), (const char *)callToken);
}

BOOL MyH323Connection::OnReceivedCapabilitySet(const H323Capabilities & remoteCaps,
					       const H245_MultiplexCapability * muxCap,
					       H245_TerminalCapabilitySetReject & reject)
{
	if (!H323Connection::OnReceivedCapabilitySet(remoteCaps, muxCap, reject)) {
		return FALSE;
	}

	const H323Capability * cap = remoteCaps.FindCapability(H323_UserInputCapability::SubTypeNames[H323_UserInputCapability::SignalToneRFC2833]);
	if (cap != NULL) {
		RTP_DataFrame::PayloadTypes pt = ((H323_UserInputCapability*)cap)->GetPayloadType();
		on_set_rfc2833_payload(GetCallReference(), (const char *)GetCallToken(), (int)pt);
		if (h323debug) {
			cout << "\t-- Inbound RFC2833 on payload " << pt << endl;
		}
	}
	return TRUE;
}

H323Channel * MyH323Connection::CreateRealTimeLogicalChannel(const H323Capability & capability,	
								   H323Channel::Directions dir,
								   unsigned sessionID,
		 					           const H245_H2250LogicalChannelParameters * /*param*/,
								   RTP_QOS * /*param*/ )
{
	return new MyH323_ExternalRTPChannel(*this, capability, dir, sessionID);
}

/** This callback function is invoked once upon creation of each
  * channel for an H323 session 
  */
BOOL MyH323Connection::OnStartLogicalChannel(H323Channel & channel)
{    
	/* Increase the count of channels we have open */
	channelsOpen++;

	if (h323debug) {
		cout << "\t-- Started logical channel: ";
		cout << ((channel.GetDirection()==H323Channel::IsTransmitter)?"sending ":((channel.GetDirection()==H323Channel::IsReceiver)?"receiving ":" "));
		cout << (const char *)(channel.GetCapability()).GetFormatName() << endl;
		cout <<  "\t\t-- channelsOpen = " << channelsOpen << endl;
	}
	return connectionState != ShuttingDownConnection;
}

void MyH323Connection::SetCapabilities(int cap, int dtmfMode)
{
	int g711Frames = 20;
//	int gsmFrames  = 4;
	PINDEX lastcap = -1; /* last common capability index */

#if 0
	if (cap & AST_FORMAT_SPEEX) {
		/* Not real sure if Asterisk acutally supports all
		   of the various different bit rates so add them 
		   all and figure it out later*/

		localCapabilities.SetCapability(0, 0, new SpeexNarrow2AudioCapability());
		localCapabilities.SetCapability(0, 0, new SpeexNarrow3AudioCapability());
		localCapabilities.SetCapability(0, 0, new SpeexNarrow4AudioCapability());
		localCapabilities.SetCapability(0, 0, new SpeexNarrow5AudioCapability());
		localCapabilities.SetCapability(0, 0, new SpeexNarrow6AudioCapability());
	}
#endif 
	if (cap & AST_FORMAT_G729A) {
		AST_G729ACapability *g729aCap;
		AST_G729Capability *g729Cap;
		lastcap = localCapabilities.SetCapability(0, 0, g729aCap = new AST_G729ACapability);
		lastcap = localCapabilities.SetCapability(0, 0, g729Cap = new AST_G729Capability);
	}
	
	if (cap & AST_FORMAT_G723_1) {
		H323_G7231Capability *g7231Cap;
		lastcap = localCapabilities.SetCapability(0, 0, g7231Cap = new H323_G7231Capability);
	} 
#if 0
	if (cap & AST_FORMAT_GSM) {
		H323_GSM0610Capability *gsmCap;
	    	lastcap = localCapabilities.SetCapability(0, 0, gsmCap = new H323_GSM0610Capability);
	    	gsmCap->SetTxFramesInPacket(gsmFrames);
	} 
#endif
	if (cap & AST_FORMAT_ULAW) {
		H323_G711Capability *g711uCap;
	    	lastcap = localCapabilities.SetCapability(0, 0, g711uCap = new H323_G711Capability(H323_G711Capability::muLaw));
		g711uCap->SetTxFramesInPacket(g711Frames);
	} 

	if (cap & AST_FORMAT_ALAW) {
		H323_G711Capability *g711aCap;
		lastcap = localCapabilities.SetCapability(0, 0, g711aCap = new H323_G711Capability(H323_G711Capability::ALaw));
		g711aCap->SetTxFramesInPacket(g711Frames);
	}

	lastcap++;
	lastcap = localCapabilities.SetCapability(0, lastcap, new H323_UserInputCapability(H323_UserInputCapability::HookFlashH245));

	lastcap++;
	mode = dtmfMode;
	if (dtmfMode == H323_DTMF_INBAND) {
		localCapabilities.SetCapability(0, lastcap, new H323_UserInputCapability(H323_UserInputCapability::SignalToneH245));
		sendUserInputMode = SendUserInputAsTone;
	} else {
		localCapabilities.SetCapability(0, lastcap, new H323_UserInputCapability(H323_UserInputCapability::SignalToneRFC2833));
		sendUserInputMode = SendUserInputAsInlineRFC2833;
	}

	if (h323debug) {
		cout <<  "Allowed Codecs:\n\t" << setprecision(2) << localCapabilities << endl;
	}
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
		/* tell the H.323 stack  */ 
		SetExternalAddress(H323TransportAddress(localIpAddr, localPort), H323TransportAddress(localIpAddr, localPort + 1));
		/* clean up allocated memory */
		free(info);
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

BOOL MyH323_ExternalRTPChannel::Start(void)
{
	/* Call ancestor first */
	if (!H323_ExternalRTPChannel::Start()) {
		return FALSE;
	}

	if (h323debug) {
		cout << "\t\tExternal RTP Session Starting" << endl;
		cout << "\t\tRTP channel id " << sessionID << " parameters:" << endl;
	}

	/* Update RTP parameters by outgoing voice path only */
	if (GetDirection() == IsTransmitter) {
		/* Collect the remote information */
		H323_ExternalRTPChannel::GetRemoteAddress(remoteIpAddr, remotePort);

		if (h323debug) {
			cout << "\t\t-- remoteIpAddress: " << remoteIpAddr << endl;
			cout << "\t\t-- remotePort: " << remotePort << endl;
			cout << "\t\t-- ExternalIpAddress: " <<  localIpAddr << endl;
			cout << "\t\t-- ExternalPort: " << localPort << endl;
		}

		/* Notify Asterisk of remote RTP information */
		on_start_rtp_channel(connection.GetCallReference(), (const char *)remoteIpAddr.AsString(), remotePort, 
			(const char *)connection.GetCallToken(), (int)payloadCode);
	}
	return TRUE;
}

BOOL MyH323_ExternalRTPChannel::OnReceivedAckPDU(const H245_H2250LogicalChannelAckParameters & param)
{
	PIPSocket::Address remoteIpAddress;
	WORD remotePort;

	if (h323debug) {
		cout << "	MyH323_ExternalRTPChannel::OnReceivedAckPDU" << endl;
	}

	if (H323_ExternalRTPChannel::OnReceivedAckPDU(param)) {
		GetRemoteAddress(remoteIpAddress, remotePort);
		if (h323debug) {
			cout << "		-- remoteIpAddress: " << remoteIpAddress << endl;
			cout << "		-- remotePort: " << remotePort << endl;
		}
		on_start_rtp_channel(connection.GetCallReference(), (const char *)remoteIpAddress.AsString(),
				remotePort, (const char *)connection.GetCallToken(), (int)payloadCode);
		return TRUE;
	}
	return FALSE;
}

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
	localProcess = new MyProcess();	
	localProcess->Main();
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
	
/** Installs the callback functions on behalf of the PBX application  */
void h323_callback_register(setup_incoming_cb  	ifunc,
			    setup_outbound_cb  	sfunc,
 			    on_rtp_cb   	rtpfunc,
			    start_rtp_cb   	lfunc,
 			    clear_con_cb	clfunc,
 			    chan_ringing_cb     rfunc,
			    con_established_cb 	efunc,
 			    send_digit_cb	dfunc,
 			    answer_call_cb	acfunc,
			    progress_cb		pgfunc,
			    rfc2833_cb		dtmffunc,
			    hangup_cb		hangupfunc,
			    setcapabilities_cb	capabilityfunc)
{
	on_incoming_call = ifunc;
	on_outgoing_call = sfunc;
	on_external_rtp_create = rtpfunc;
	on_start_rtp_channel = lfunc;
	on_connection_cleared = clfunc;
	on_chan_ringing = rfunc;
	on_connection_established = efunc;
	on_send_digit = dfunc;
	on_answer_call = acfunc;
	on_progress = pgfunc;
	on_set_rfc2833_payload = dtmffunc;
	on_hangup = hangupfunc;
	on_setcapabilities = capabilityfunc;
}

/**
 * Add capability to the capability table of the end point. 
 */
int h323_set_capabilities(const char *token, int cap, int dtmfMode)
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
	conn->SetCapabilities(cap, dtmfMode);
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
	endPoint->RemoveAliasName(localProcess->GetUserName());

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
			free(prefix);
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
		if (endPoint->DiscoverGatekeeper(new H323TransportUDP(*endPoint))) {
			cout << "== Using " << (endPoint->GetGatekeeper())->GetName() << " as our Gatekeeper." << endl;
		} else {
			cout << "Warning: Could not find a gatekeeper." << endl;
			return 1;
		}	
	} else {
		rasChannel = new H323TransportUDP(*endPoint);

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

	res = endPoint->MakeCall(host, token, &cd->call_reference, call_options);
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
        	cout << "\tSending alerting\n" << endl;
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
        connection->AnsweringCall(H323Connection::AnswerCallDeferredWithMedia);
        connection->Unlock();
        return 0;  
}

/** This function tells the h.323 stack to either 
    answer or deny an incoming call  */
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

int h323_show_codec(int fd, int argc, char *argv[])
{
	cout <<  "Allowed Codecs:\n\t" << setprecision(2) << endPoint->GetCapabilities() << endl;
	return 0;
}

int h323_soft_hangup(const char *data)
{
	PString token(data);
	BOOL result;
	cout << "Soft hangup" << endl;	
	result = endPoint->ClearCall(token);	
	return result;
}

/* alas, this doesn't work :(   */
void h323_native_bridge(const char *token, const char *them, char *capability)
{
	H323Channel *channel;
	MyH323Connection *connection = (MyH323Connection *)endPoint->FindConnectionWithLock(token);
	
	if (!connection) {
		cout << "ERROR: No connection found, this is bad\n";
		return;
	}

	cout << "Native Bridge:  them [" << them << "]" << endl; 

	channel = connection->FindChannel(connection->sessionId, TRUE);
	connection->bridging = TRUE;
	connection->CloseLogicalChannelNumber(channel->GetNumber());
	
	connection->Unlock();
	return;

}

#undef cout
void h323_end_process(void)
{
	if (endPoint) {
		endPoint->ClearAllCalls();
		endPoint->RemoveListener(NULL);
		delete endPoint;
		endPoint = NULL;
	}
	if (localProcess) {
		delete localProcess;
		localProcess = NULL;
	}
	PTrace::SetLevel(0);
	PTrace::SetStream(&cout);
	if (logstream) {
		delete logstream;
		logstream = NULL;
	}
}

} /* extern "C" */

