/*
 * ast_h323.cpp
 *
 * OpenH323 Channel Driver for ASTERISK PBX.
 *			By  Jeremy McNamara
 *			For The NuFone Network
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
#include "ast_h323.h"
#include "h323t38.h"


/* PWlib Required Components  */
#define MAJOR_VERSION 0
#define MINOR_VERSION 1
#define BUILD_TYPE    ReleaseCode
#define BUILD_NUMBER  0

/** Counter for the number of connections */
int channelsOpen;

/* DTMF Mode */
int mode = H323_DTMF_RFC2833;

/* Make those variables accessible from chan_h323.c */
extern "C" {
/** Options for connections creation */
call_options_t global_options;
}

/**
 * We assume that only one endPoint should exist.
 * The application cannot run the h323_end_point_create() more than once
 * FIXME: Singleton this, for safety
 */
MyH323EndPoint *endPoint = NULL;

/** PWLib entry point */
MyProcess *localProcess = NULL;

/** H.323 listener */
H323ListenerTCP *tcpListener;

/* Provide common methods to split out non-user parts of OpenH323 aliases */
static void FormatAliases(PString & aliases)
{
	/* Convert complex strings */
	//  FIXME: deal more than one source alias 
	char *s;
	const char *p = (const char *)aliases;
	if ((s = strchr(p, '(')) != NULL)
		*s = '\0';
	else if ((s = strchr(p, ',')) != NULL)
		*s = '\0';
	else if ((s = strchr(p, '[')) != NULL)
		*s = '\0';
	else if ((s = strchr(p, ' ')) != NULL)
		*s = '\0';
	else if ((s = strchr(p, '\t')) != NULL)
		*s = '\0';
	/* Strip trailing spaces */
	for(s = (char *)(p + strlen(p)); (s > p) && (*--s == ' '); *s = '\0');
}

MyProcess::MyProcess(): PProcess("The NuFone Network's", "H.323 Channel Driver for Asterisk",
             MAJOR_VERSION, MINOR_VERSION, BUILD_TYPE, BUILD_NUMBER)
{
	Resume();
}

MyProcess::~MyProcess()
{
	cout << " == PWLib proces going down." << endl;
	delete endPoint;
	endPoint = NULL;
}

void MyProcess::Main()
{
	cout << "  == Creating H.323 Endpoint" << endl;
	endPoint = new MyH323EndPoint();
	endPoint->DisableDetectInBandDTMF(TRUE);
	PTrace::Initialise(0, NULL, PTrace::Timestamp | PTrace::Thread | PTrace::FileAndLine);
}

ClearCallThread::ClearCallThread(const char *tc) : PThread(10000, PThread::NoAutoDeleteThread)
{ 
	token = tc;
	Resume(); 
}

ClearCallThread::~ClearCallThread()
{
	if (h323debug)
		cout <<  " == ClearCall thread going down." << endl;
	return;
}
    
void ClearCallThread::Main()
{
	endPoint->ClearCall(token);
	return;
}


#define H323_NAME OPAL_G7231_6k3"{sw}"
#define H323_G729  OPAL_G729 "{sw}"
#define H323_G729A OPAL_G729A"{sw}"

H323_REGISTER_CAPABILITY(H323_G7231Capability, H323_NAME);
H323_REGISTER_CAPABILITY(AST_G729Capability,  H323_G729);
H323_REGISTER_CAPABILITY(AST_G729ACapability, H323_G729A);

H323_G7231Capability::H323_G7231Capability(BOOL annexA_)
  : H323AudioCapability(7, 4)
{
  annexA = annexA_;
  rtpPayloadType = RTP_DataFrame::G7231;
}

PObject::Comparison H323_G7231Capability::Compare(const PObject & obj) const
{
  Comparison result = H323AudioCapability::Compare(obj);
  if (result != EqualTo)
    return result;

  PINDEX otherAnnexA = ((const H323_G7231Capability &)obj).annexA;
  if (annexA < otherAnnexA)
    return LessThan;
  if (annexA > otherAnnexA)
    return GreaterThan;
  return EqualTo;
}

PObject * H323_G7231Capability::Clone() const
{
  return new H323_G7231Capability(*this);
}


PString H323_G7231Capability::GetFormatName() const
{
  return H323_NAME;
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
  if (cap.GetTag() != H245_AudioCapability::e_g7231)
    return FALSE;

  const H245_AudioCapability_g7231 & g7231 = cap;
  packetSize = g7231.m_maxAl_sduAudioFrames;
  annexA = g7231.m_silenceSuppression;

  return TRUE;
}


H323Codec * H323_G7231Capability::CreateCodec(H323Codec::Direction direction) const
{
  return NULL;
}


/////////////////////////////////////////////////////////////////////////////

AST_G729Capability::AST_G729Capability()
  : H323AudioCapability(24, 6)
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
  return H323_G729;
}


H323Codec * AST_G729Capability::CreateCodec(H323Codec::Direction direction) const
{
  return NULL;
}
/////////////////////////////////////////////////////////////////////////////

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
  return H323_G729A;
}


H323Codec * AST_G729ACapability::CreateCodec(H323Codec::Direction direction) const
{
  return NULL;
}

/** MyH323EndPoint 
  * The fullAddress parameter is used directly in the MakeCall method so
  * the General form for the fullAddress argument is :
  * [alias@][transport$]host[:port]
  * default values:	alias = the same value as host.
  *					transport = ip.
  *					port = 1720.
  */
int MyH323EndPoint::MakeCall(const PString & dest, PString & token, 
				unsigned int *callReference,
				call_options_t *call_options)
{
	PString fullAddress;
	MyH323Connection * connection;

	/* Determine whether we are using a gatekeeper or not. */
	if (GetGatekeeper() != NULL) {
		fullAddress = dest;
		if (h323debug)
			cout << " -- Making call to " << fullAddress << " using gatekeeper." << endl;
	} else {
			fullAddress = dest; /* host */
			if (h323debug)
				cout << " -- Making call to " << fullAddress << "." << endl;
	}

	if (!(connection = (MyH323Connection *)H323EndPoint::MakeCallLocked(fullAddress, token, call_options))) {
		if (h323debug)
			cout << "Error making call to \"" << fullAddress << '"' << endl;
		return 1;
	}
	
	*callReference = connection->GetCallReference();
	
	if (call_options->callerid)
		connection->SetCID(call_options->callerid);	// Use our local function to setup H.323 caller ID correctly

	connection->Unlock(); 	

	if (h323debug) {
		cout << "	-- " << GetLocalUserName() << " is calling host " << fullAddress << endl;
		cout << "	-- " << "Call token is " << (const char *)token << endl;
		cout << "	-- Call reference is " << *callReference << endl;
	}
	return 0;
}

void MyH323EndPoint::SetEndpointTypeInfo( H225_EndpointType & info ) const
{
//  cout << " **** Terminal type: " << terminalType << endl;
  H323EndPoint::SetEndpointTypeInfo(info);
//  cout << " **** INFO: " << info << endl;
  /* Because H323EndPoint::SetEndpointTypeInfo() don't set correctly
     endpoint type, force manual setting it */
  if(terminalType == e_GatewayOnly)
  {
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

H323Capabilities MyH323EndPoint::GetCapabilities(void)
{
	return capabilities;
}

BOOL MyH323EndPoint::ClearCall(const PString & token)
{
	if (h323debug)
		cout << "	-- ClearCall: Request to clear call with token " << token << endl;
	return H323EndPoint::ClearCall(token);
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
	if (h323debug)
		cout << "		channelsOpen = " << channelsOpen << endl;
	H323EndPoint::OnClosedLogicalChannel(connection, channel);
}

BOOL MyH323EndPoint::OnConnectionForwarded(H323Connection & connection,
	const PString & forwardParty,
	const H323SignalPDU & pdu)
{
 	if (h323debug)
 	cout << "       -- Call Forwarded to " << forwardParty << endl;
 	return FALSE;
}

BOOL MyH323EndPoint::ForwardConnection(H323Connection & connection,
		const PString & forwardParty,
		const H323SignalPDU & pdu)
{
 	if (h323debug)
 		cout << "       -- Forwarding call to " << forwardParty << endl;
 	return H323EndPoint::ForwardConnection(connection, forwardParty, pdu);
}

void MyH323EndPoint::OnConnectionEstablished(H323Connection & connection, const PString & estCallToken)
{
	if (h323debug)
		cout << "	-- Connection Established with \"" << connection.GetRemotePartyName() << "\"" << endl;
	on_connection_established(connection.GetCallReference());
}

/** OnConnectionCleared callback function is called upon the dropping of an established
  * H323 connection. 
  */
void MyH323EndPoint::OnConnectionCleared(H323Connection & connection, const PString & clearedCallToken)
{
	PString remoteName = connection.GetRemotePartyName();
	
	call_details_t cd;

	/* Use common alias formatting routine */
	FormatAliases(remoteName);

	cd.call_reference = connection.GetCallReference();
	cd.call_token = (const char *)connection.GetCallToken();
	cd.call_source_aliases = (const char *)remoteName;

	/* Invoke the PBX application registered callback */
	on_connection_cleared(cd);

	switch (connection.GetCallEndReason()) {
		case H323Connection::EndedByCallForwarded :
			if (h323debug)
				cout << " -- " << remoteName << " has forwarded the call" << endl;
			break;
		case H323Connection::EndedByRemoteUser :
			if (h323debug)
				cout << " -- " << remoteName << " has cleared the call" << endl;
			break;
		case H323Connection::EndedByCallerAbort :
			if (h323debug)
				cout << " -- " << remoteName << " has stopped calling" << endl;
			break;
		case H323Connection::EndedByRefusal :
			if (h323debug)
				cout << " -- " << remoteName << " did not accept your call" << endl;
			break;
		case H323Connection::EndedByRemoteBusy :
			if (h323debug)
			cout << " -- " << remoteName << " was busy" << endl;
			break;
		case H323Connection::EndedByRemoteCongestion :
			if (h323debug)
				cout << " -- Congested link to " << remoteName << endl;
			break;
		case H323Connection::EndedByNoAnswer :
			if (h323debug)
				cout << " -- " << remoteName << " did not answer your call" << endl;
			break;
		case H323Connection::EndedByTransportFail :
			if (h323debug)
				cout << " -- Call with " << remoteName << " ended abnormally" << endl;
			break;
		case H323Connection::EndedByCapabilityExchange :
			if (h323debug)
				cout << " -- Could not find common codec with " << remoteName << endl;
			break;
		case H323Connection::EndedByNoAccept :
			if (h323debug)
				cout << " -- Did not accept incoming call from " << remoteName << endl;
			break;
		case H323Connection::EndedByAnswerDenied :
			if (h323debug)
				cout << " -- Refused incoming call from " << remoteName << endl;
			break;
		case H323Connection::EndedByNoUser :
			if (h323debug)
				cout << " -- Remote endpoint could not find user: " << remoteName << endl;
			break;
		case H323Connection::EndedByNoBandwidth :
			if (h323debug)
				cout << " -- Call to " << remoteName << " aborted, insufficient bandwidth." << endl;
			break;
		case H323Connection::EndedByUnreachable :
			if (h323debug)
				cout << " -- " << remoteName << " could not be reached." << endl;
			break;
		case H323Connection::EndedByHostOffline :
			if (h323debug)
				cout << " -- " << remoteName << " is not online." << endl;
			break;
		case H323Connection::EndedByNoEndPoint :
			if (h323debug)
				cout << " -- No phone running for " << remoteName << endl;
			break;
		case H323Connection::EndedByConnectFail :
			if (h323debug)
				cout << " -- Transport error calling " << remoteName << endl;
			break;
		default :
			if (h323debug)
				cout << " -- Call with " << remoteName << " completed (" << connection.GetCallEndReason() << ")" << endl;
	}
	if(connection.IsEstablished()) 
		if (h323debug)
			cout << "	 -- Call duration " << setprecision(0) << setw(5) << (PTime() - connection.GetConnectionStartTime()) << endl;
}


H323Connection * MyH323EndPoint::CreateConnection(unsigned callReference, void *outbound)
{
	unsigned options = 0;
	call_options_t *call_options = (call_options_t *)outbound;

	if (!call_options)
		call_options = &global_options;

	if (call_options->noFastStart)
		options |= H323Connection::FastStartOptionDisable;

	if (call_options->noH245Tunnelling)
		options |= H323Connection::H245TunnelingOptionDisable;

	/* Set silence detection mode - won't work for Asterisk's RTP but can be used in the negotiation process */
	SetSilenceDetectionMode(call_options->noSilenceSuppression ? H323AudioCodec::NoSilenceDetection : H323AudioCodec::AdaptiveSilenceDetection);

	return new MyH323Connection(*this, callReference, options, call_options);
}

/* MyH323_ExternalRTPChannel */
MyH323_ExternalRTPChannel::MyH323_ExternalRTPChannel(
      H323Connection & connection,        /// Connection to endpoint for channel
      const H323Capability & capability,  /// Capability channel is using
      Directions direction,               /// Direction of channel
      unsigned sessionID                  /// Session ID for channel
    ): H323_ExternalRTPChannel(connection,capability,direction,sessionID)
{
}

MyH323_ExternalRTPChannel::MyH323_ExternalRTPChannel(
      H323Connection & connection,        /// Connection to endpoint for channel
      const H323Capability & capability,  /// Capability channel is using
      Directions direction,               /// Direction of channel
      unsigned sessionID,                 /// Session ID for channel
      const H323TransportAddress & data,  /// Data address
      const H323TransportAddress & control/// Control address
    ): H323_ExternalRTPChannel(connection, capability, direction, sessionID, data, control)
{
}

MyH323_ExternalRTPChannel::MyH323_ExternalRTPChannel(
      H323Connection & connection,        /// Connection to endpoint for channel
      const H323Capability & capability,  /// Capability channel is using
      Directions direction,               /// Direction of channel
      unsigned sessionID,                 /// Session ID for channel
      const PIPSocket::Address & ip,      /// IP address of media server
      WORD dataPort                       /// Data port (control is dataPort+1)
    ): H323_ExternalRTPChannel(connection, capability, direction, sessionID, ip, dataPort)
{
}

BOOL MyH323_ExternalRTPChannel::Start()
{
	BOOL res;
	PIPSocket::Address	remoteIpAddress;		// IP Address of remote endpoint
	WORD			remotePort;			// remote endpoint Data port (control is dataPort+1)
	PIPSocket::Address	externalIpAddress;	// IP address of media server
	WORD			externalPort;		// local media server Data port (control is dataPort+1)

	res = H323_ExternalRTPChannel::Start();
	if (!res)
		return res;

	if (h323debug) {
		/* Show H.323 channel number to make debugging more comfortable */
		cout << "	 -- Started RTP media for channel " << GetNumber() << ": ";	
		cout << ((GetDirection()==H323Channel::IsTransmitter)?"sending ":((GetDirection()==H323Channel::IsReceiver)?"receiving ":" ")); 
		cout << (const char *)(GetCapability()).GetFormatName() << endl;
	}
	
	if(!GetRemoteAddress(remoteIpAddress, remotePort) && h323debug)
		cout << "		** Unable to get remote IP address" << endl;
	externalMediaAddress.GetIpAndPort(externalIpAddress, externalPort);

	if (h323debug) {
		cout << "		-- remoteIpAddress: " << remoteIpAddress << endl;
		cout << "		-- remotePort: " << remotePort << endl;
		cout << "		-- ExternalIpAddress: " << externalIpAddress << endl;
		cout << "		-- ExternalPort: " << externalPort << endl;
	}

	const OpalMediaFormat & mediaFormat = codec->GetMediaFormat();
	if (rtpPayloadType == RTP_DataFrame::IllegalPayloadType) {
		rtpPayloadType = capability->GetPayloadType();
		if (rtpPayloadType == RTP_DataFrame::IllegalPayloadType)
			rtpPayloadType = mediaFormat.GetPayloadType();
	}

	/* Deduce direction of starting channel */
	int direction;
	if (GetDirection()==H323Channel::IsTransmitter)
		direction = 1;
	else if (GetDirection()==H323Channel::IsReceiver)
		direction = 0;
	else
		direction = -1;

	/* Notify Asterisk of remote RTP information */
	/* direction and payload arguments needs to
	 * correctly setup RTP transport
	 */
	on_start_logical_channel(connection.GetCallReference(), (const char *)remoteIpAddress.AsString(), remotePort, direction, (int)rtpPayloadType);

	return TRUE;	
}

/* MyH323Connection */    
MyH323Connection::MyH323Connection(MyH323EndPoint & ep,
					unsigned callReference,
					unsigned options)
					: H323Connection(ep, 
						callReference, 
						options)
{
	remoteIpAddress	= 0; 	// IP Address of remote endpoint
	remotePort	= 0;	// remote endpoint Data port (control is dataPort+1)

	progressSetup	= global_options.progress_setup;
	progressAlert	= global_options.progress_alert;

	if (h323debug)
		cout << "	== New H.323 Connection created." << endl;
	return;
}

/* MyH323Connection */    
MyH323Connection::MyH323Connection(MyH323EndPoint & ep,
					unsigned callReference,
					unsigned options,
					call_options_t *call_options)
					: H323Connection(ep, 
						callReference, 
						options)
{
	remoteIpAddress	= 0; 	// IP Address of remote endpoint
	remotePort	= 0;	// remote endpoint Data port (control is dataPort+1)

	if (!call_options)
		call_options = &global_options;

	progressSetup	= call_options->progress_setup;
	progressAlert	= call_options->progress_alert;

	if (h323debug)
		cout << "	== New H.323 Connection created." << endl;
	return;
}

MyH323Connection::~MyH323Connection()
{
	if (h323debug)
		cout << "	== H.323 Connection deleted." << endl;
	return;
}

/* Declare reference to standard Asterisk's callerid parser */
extern "C" {
	void ast_callerid_parse(const char *, char **, char **);
}

/*
 * Setup H.323 caller ID to allow OpenH323 to set up Q.931's
 * IE:DisplayName and IE:DisplayNumber fields correctly
 */
void MyH323Connection::SetCID(const char *callerid)
{
	char *name;
	char *num;

	ast_callerid_parse(callerid, &name, &num);

	if (h323debug)
		cout << "name=" << name << ", num=" << num << endl;

	if ((name && *name) || (num && *num))
	{
		localAliasNames.RemoveAll();
		if(name && *name) {
			SetLocalPartyName(PString(name));
//			localAliasNames.AppendString(name);
		}
		if(num && *num)
			localAliasNames.AppendString(PString(num));
	}
}

BOOL MyH323Connection::OnReceivedProgress(const H323SignalPDU & pdu)
{
	BOOL res;

	res = H323Connection::OnReceivedProgress(pdu);

	if(res && on_progress) {
		BOOL inband;
		unsigned ProgressPI;

		if(!pdu.GetQ931().GetProgressIndicator(ProgressPI))
			ProgressPI = 0;
		if(h323debug)
			cout << "Progress Indicator is " << ProgressPI << endl;

		/* XXX Is this correct? XXX */
		switch(ProgressPI) {
		case Q931::ProgressNotEndToEndISDN:
		case Q931::ProgressInbandInformationAvailable:
			inband = TRUE;
			break;
		default:
			inband = FALSE;
		}
		on_progress(GetCallReference(), inband);
	}

	return res;
}

H323Connection::AnswerCallResponse	MyH323Connection::OnAnswerCall(const PString & caller,
																   const H323SignalPDU & setupPDU,
																   H323SignalPDU & /*connectPDU*/)
{
	unsigned	ProgressInd;

	/* The call will be answered later with "AnsweringCall()" function.
	 */ 
	if(!setupPDU.GetQ931().GetProgressIndicator(ProgressInd))
		ProgressInd = 0;
	if(h323debug)
		cout << "PI in SETUP was " << ProgressInd << endl;

	/* Progress indicator must be always set to 8 if Setup Have Progress indicator equal to 3 */
	if(progressAlert)
		ProgressInd = progressAlert;
	else if(ProgressInd == Q931::ProgressOriginNotISDN)
		ProgressInd = Q931::ProgressInbandInformationAvailable;
	if(ProgressInd)
		alertingPDU->GetQ931().SetProgressIndicator(ProgressInd);
	if(h323debug)
		cout << "Adding PI=" << ProgressInd << " to ALERT message" << endl;

	return H323Connection::AnswerCallAlertWithMedia;
}

BOOL  MyH323Connection::OnAlerting(const H323SignalPDU & alertingPDU, const PString & username)
{
	if (h323debug)
		cout << "	-- Ringing phone for \"" << username << "\"" << endl;

	if (on_progress) {
		BOOL inband;
		unsigned alertingPI;

		if(!alertingPDU.GetQ931().GetProgressIndicator(alertingPI))
			alertingPI = 0;
		if(h323debug)
			cout << "Progress Indicator is " << alertingPI << endl;

		/* XXX Is this correct? XXX */
		switch(alertingPI) {
		case Q931::ProgressNotEndToEndISDN:
		case Q931::ProgressInbandInformationAvailable:
			inband = TRUE;
			break;
		default:
			inband = FALSE;
		}
		on_progress(GetCallReference(), inband);
	}
	return TRUE;
}

BOOL MyH323Connection::OnReceivedSignalSetup(const H323SignalPDU & setupPDU)
{
	
	if (h323debug)
		cout << "	-- Received SETUP message..." << endl;

	call_details_t cd;
	
	PString sourceE164;
	PString destE164;
	PString redirE164;
	PString sourceAliases;	
	PString destAliases;
	PString sourceIp;
		
	PIPSocket::Address Ip;
	WORD			   sourcePort;

	sourceAliases = setupPDU.GetSourceAliases();
	destAliases = setupPDU.GetDestinationAlias();
			
	sourceE164 = "";
	setupPDU.GetSourceE164(sourceE164);
	destE164 = "";
	setupPDU.GetDestinationE164(destE164);
	if(!setupPDU.GetQ931().GetRedirectingNumber(redirE164))
		redirE164 = "";

	/* Use common alias formatting routine */
	FormatAliases(sourceAliases);
	FormatAliases(destAliases);

	GetSignallingChannel()->GetRemoteAddress().GetIpAndPort(Ip, sourcePort);

	sourceIp = Ip.AsString();

	cd.call_reference		= GetCallReference();
	cd.call_token			= (const char *)GetCallToken();
	cd.call_source_aliases		= (const char *)sourceAliases;
	cd.call_dest_alias		= (const char *)destAliases;
	cd.call_source_e164		= (const char *)sourceE164;
	cd.call_dest_e164		= (const char *)destE164;
	cd.call_redir_e164		= (const char *)redirE164;
	cd.sourceIp			= (const char *)sourceIp;
	
	/* Notify Asterisk of the request */
	call_options_t *res = on_incoming_call(cd); 

	if (!res) {
		if (h323debug)
			cout << "	-- Call Failed" << endl;
		return FALSE;
	}

	progressSetup = res->progress_setup;
	progressAlert = res->progress_alert;
	
	return H323Connection::OnReceivedSignalSetup(setupPDU);
}

BOOL MyH323Connection::OnSendSignalSetup(H323SignalPDU & setupPDU)
{
	call_details_t cd;
	
	if (h323debug) 
		cout << "	-- Sending SETUP message" << endl;
	
	sourceAliases = setupPDU.GetSourceAliases();
	destAliases = setupPDU.GetDestinationAlias();

	sourceE164 = "";
	setupPDU.GetSourceE164(sourceE164);
	destE164 = "";
	setupPDU.GetDestinationE164(destE164);

	/* Use common alias formatting routine */
	FormatAliases(sourceAliases);
	FormatAliases(destAliases);

	cd.call_reference		= GetCallReference();
	cd.call_token			= (const char *)GetCallToken();
	cd.call_source_aliases		= (const char *)sourceAliases;
	cd.call_dest_alias		= (const char *)destAliases;
	cd.call_source_e164		= (const char *)sourceE164;
	cd.call_dest_e164		= (const char *)destE164;

	int res = on_outgoing_call(cd);	
		
	if (!res) {
		if (h323debug)
			cout << "	-- Call Failed" << endl;
		return FALSE;
	}

	if(progressSetup)
		setupPDU.GetQ931().SetProgressIndicator(progressSetup);
//	setupPDU.GetQ931().SetProgressIndicator(Q931::ProgressInbandInformationAvailable);
//	setupPDU.GetQ931().SetProgressIndicator(Q931::ProgressOriginNotISDN);
	return H323Connection::OnSendSignalSetup(setupPDU);
}

BOOL MyH323Connection::OnSendReleaseComplete(H323SignalPDU & releaseCompletePDU)
{
	if (h323debug)
		cout << "	-- Sending RELEASE COMPLETE" << endl;
	return H323Connection::OnSendReleaseComplete(releaseCompletePDU);
}

BOOL MyH323Connection::OnReceivedFacility(const H323SignalPDU & pdu)
{
	if (h323debug)
		cout << "	-- Received Facility message... " << endl;
	return H323Connection::OnReceivedFacility(pdu);
}

void MyH323Connection::OnReceivedReleaseComplete(const H323SignalPDU & pdu)
{
	if (h323debug)
		cout <<  "	-- Received RELEASE COMPLETE message..." << endl;
	return H323Connection::OnReceivedReleaseComplete(pdu);

}

BOOL MyH323Connection::OnClosingLogicalChannel(H323Channel & channel)
{
	if (h323debug)
		cout << "	-- Closing logical channel..." << endl;
	return H323Connection::OnClosingLogicalChannel(channel);
}


void MyH323Connection::SendUserInputTone(char tone, unsigned duration)
{
	if (h323debug)
		cout << "	-- Sending user input tone (" << tone << ") to remote" << endl;

	on_send_digit(GetCallReference(), tone);
		
	H323Connection::SendUserInputTone(tone, duration);
}

void MyH323Connection::OnUserInputTone(char tone, unsigned duration, unsigned logicalChannel, unsigned rtpTimestamp)
{
	if (mode == H323_DTMF_INBAND) {
		if (h323debug)
			cout << "	-- Received user input tone (" << tone << ") from remote" << endl;
		on_send_digit(GetCallReference(), tone);
	}
	H323Connection::OnUserInputTone(tone, duration, logicalChannel, rtpTimestamp);
}

void MyH323Connection::OnUserInputString(const PString &value)
{
	if (mode == H323_DTMF_RFC2833) {
		if (h323debug)
			cout <<  "	-- Received user input string (" << value << ") from remote." << endl;
		on_send_digit(GetCallReference(), value[0]);
	}	
}

H323Channel * MyH323Connection::CreateRealTimeLogicalChannel(const H323Capability & capability,
														     H323Channel::Directions dir,
														     unsigned sessionID,
														     const H245_H2250LogicalChannelParameters * /*param*/)
{
	struct rtp_info *info;
	WORD port;

	/* Determine the Local (A side) IP Address and port */
	info = on_create_connection(GetCallReference()); 
	
//	if (bridging) {
//		externalIpAddress = PIPSocket::Address(info->addr);
//	} else {
        GetControlChannel().GetLocalAddress().GetIpAndPort(externalIpAddress, port);
//	}

//	externalIpAddress = PIPSocket::Address("192.168.1.50");

	externalPort = info->port;
	
	if (h323debug) {
		cout << "	=*= In CreateRealTimeLogicalChannel for call " << GetCallReference() << endl;
		cout << "		-- externalIpAddress: " << externalIpAddress << endl;
		cout << "		-- externalPort: " << externalPort << endl;
		cout << "		-- SessionID: " << sessionID << endl;
		cout << "		-- Direction: " << dir << endl;
	}
	return new MyH323_ExternalRTPChannel(*this, capability, dir, sessionID, externalIpAddress, externalPort);
}

/** This callback function is invoked once upon creation of each
  * channel for an H323 session 
  */
BOOL MyH323Connection::OnStartLogicalChannel(H323Channel & channel)
{    
	if (h323debug) {
		/* Show H.323 channel number to make debugging more comfortable */
		cout << "	 -- Started logical channel " << channel.GetNumber() << ": ";	
		cout << ((channel.GetDirection()==H323Channel::IsTransmitter)?"sending ":((channel.GetDirection()==H323Channel::IsReceiver)?"receiving ":" ")); 
		cout << (const char *)(channel.GetCapability()).GetFormatName() << endl;
	}
	// adjust the count of channels we have open
	channelsOpen++;
	if (h323debug)
		cout <<  "		-- channelsOpen = " << channelsOpen << endl;
	
#if 0
	H323_ExternalRTPChannel & external = (H323_ExternalRTPChannel &)channel;
	if(!external.GetRemoteAddress(remoteIpAddress, remotePort) && h323debug)
		cout << "		** Unable to get remote IP address" << endl;

	if (h323debug) {
		cout << "		-- remoteIpAddress: " << remoteIpAddress << endl;
		cout << "		-- remotePort: " << remotePort << endl;
		cout << "		-- ExternalIpAddress: " << externalIpAddress << endl;
		cout << "		-- ExternalPort: " << externalPort << endl;
	}

	/* Try to determine negotiated RTP payload format to configure
	 * RTP stack more quickly (not to wait at least one packet with
	 * filled RTP payload
	 */
	RTP_DataFrame::PayloadTypes payloadType = channel.GetCapability().GetPayloadType();
	cout << " *** channel's payload is " << payloadType << endl;
	if (payloadType == RTP_DataFrame::IllegalPayloadType) {
		payloadType = channel.GetCodec()->GetMediaFormat().GetPayloadType();
		cout << " *** channel's codec payload is " << payloadType << endl;
	}
	if ((payloadType == RTP_DataFrame::IllegalPayloadType) || (payloadType >= RTP_DataFrame::DynamicBase)) {
		OpalMediaFormat mediaFormat = channel.GetCodec()->GetMediaFormat();
//		if (mediaFormat.GetPayloadType() < RTP_DataFrame::DynamicBase)
		{
			payloadType = mediaFormat.GetPayloadType();
			cout << " *** channel's Opal media payload is " << payloadType << endl;
		}
	}
//	if ((payloadType == RTP_DataFrame::IllegalPayloadType)) {
//		OpalMediaFormat OMF((const char *)(channel.GetCapability()).GetFormatName(), 1);
//		if (OMF.IsValid())
//		{
//			payloadType = OMF.GetPayloadType();
//			cout << " *** channel's OMF payload is " << payloadType << endl;
//		}
//	}

	/* Deduce direction of starting channel */
	int direction;
	if (channel.GetDirection()==H323Channel::IsTransmitter)
		direction = 1;
	else if (channel.GetDirection()==H323Channel::IsReceiver)
		direction = 0;
	else
		direction = -1;

	/* Notify Asterisk of remote RTP information */
	/* direction and payload arguments needs to
	 * correctly setup RTP transport
	 */
	on_start_logical_channel(GetCallReference(), (const char *)remoteIpAddress.AsString(), remotePort, direction, (int)payloadType);
#endif

	return TRUE;	
}

#if 0
MyGatekeeperServer::MyGatekeeperServer(MyH323EndPoint & ep)
  : H323GatekeeperServer(ep),
    endpoint(ep)
{
}


BOOL MyGatekeeperServer::Initialise()
{
  PINDEX i;

  PWaitAndSignal mutex(reconfigurationMutex);
  
  SetGatekeeperIdentifier("TESTIES");

    // Interfaces to listen on
  H323TransportAddressArray interfaces;
  interfaces.Append(new H323TransportAddress(0.0.0.0);
  AddListeners(interfaces);

  // lots more to come
  
  return TRUE;

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

void h323_end_process(void)
{	
	delete localProcess;
}

void h323_debug(int flag, unsigned level)
{
	if (flag) 
		PTrace:: SetLevel(level); 
	else 
		PTrace:: SetLevel(0); 
}
	
/** Installs the callback functions on behalf of the PBX application  */
void h323_callback_register(setup_incoming_cb  ifunc,
							setup_outbound_cb  sfunc,
							on_connection_cb   confunc,
							start_logchan_cb   lfunc,
 							clear_con_cb	   clfunc,
 							con_established_cb efunc,
 							send_digit_cb	   dfunc,
 							progress_cb        pgfunc)
{
	on_incoming_call = ifunc;
	on_outgoing_call = sfunc;
	on_create_connection = confunc;
	on_start_logical_channel = lfunc;
	on_connection_cleared = clfunc;
	on_connection_established = efunc;
	on_send_digit = dfunc;
	on_progress = pgfunc;
}

/**
 * Add capability to the capability table of the end point. 
 */
int h323_set_capability(int cap, int dtmfMode)
{
	int g711Frames = 30;
	int gsmFrames  = 4;
	PINDEX last_cap = -1;	/* last common capability block index */

	
	if (!h323_end_point_exist()) {
		cout << " ERROR: [h323_set_capablity] No Endpoint, this is bad" << endl;
		return 1;
	}

	/* User input mode moved to the end of procedure */

	/* Hardcode this for now (Someone tell me if T.38 works now 
	   or provide me with some debug so we can make this work */

//	last_cap = endPoint->SetCapability(0, 0, new H323_T38Capability(H323_T38Capability::e_UDP));
	
	if (cap & AST_FORMAT_SPEEX) {
		/* Not real sure if Asterisk acutally supports all
		   of the various different bit rates so add them 
		   all and figure it out later*/

		last_cap = endPoint->SetCapability(0, 0, new SpeexNarrow2AudioCapability());
		last_cap = endPoint->SetCapability(0, 0, new SpeexNarrow3AudioCapability());
		last_cap = endPoint->SetCapability(0, 0, new SpeexNarrow4AudioCapability());
		last_cap = endPoint->SetCapability(0, 0, new SpeexNarrow5AudioCapability());
		last_cap = endPoint->SetCapability(0, 0, new SpeexNarrow6AudioCapability());
	}

	if (cap & AST_FORMAT_G729A) {
		AST_G729ACapability *g729aCap;
		AST_G729Capability *g729Cap;
		last_cap = endPoint->SetCapability(0, 0, g729aCap = new AST_G729ACapability);
		last_cap = endPoint->SetCapability(0, 0, g729Cap = new AST_G729Capability);
	}
	
	if (cap & AST_FORMAT_G723_1) {
		H323_G7231Capability *g7231Cap, *g7231Cap1;
		last_cap = endPoint->SetCapability(0, 0, g7231Cap = new H323_G7231Capability);
		last_cap = endPoint->SetCapability(0, 0, g7231Cap1 = new H323_G7231Capability(FALSE));
	} 

	if (cap & AST_FORMAT_GSM) {
		H323_GSM0610Capability *gsmCap;
		last_cap = endPoint->SetCapability(0, 0, gsmCap = new H323_GSM0610Capability);
		gsmCap->SetTxFramesInPacket(gsmFrames);
	} 

	if (cap & AST_FORMAT_ULAW) {
		H323_G711Capability *g711uCap;
		last_cap = endPoint->SetCapability(0, 0, g711uCap = new H323_G711Capability(H323_G711Capability::muLaw));
		g711uCap->SetTxFramesInPacket(g711Frames);
	} 

	if (cap & AST_FORMAT_ALAW) {
		H323_G711Capability *g711aCap;
		last_cap = endPoint->SetCapability(0, 0, g711aCap = new H323_G711Capability(H323_G711Capability::ALaw));
		g711aCap->SetTxFramesInPacket(g711Frames);
	} 

	/* Add HookFlash capability - not used yet now */
	last_cap++;
	last_cap = endPoint->SetCapability(0, last_cap, new H323_UserInputCapability(H323_UserInputCapability::HookFlashH245));

	/* Add correct UserInputMode capability
	 * This allows remote party to send UserIput
	 * correctly embedded into protocol
	 */
	last_cap++;
	mode = dtmfMode;
	if (dtmfMode == H323_DTMF_INBAND) {
		endPoint->SetCapability(0, last_cap, new H323_UserInputCapability(H323_UserInputCapability::SignalToneH245));
		endPoint->SetSendUserInputMode(H323Connection::SendUserInputAsTone);
	} else {
		endPoint->SetCapability(0, last_cap, new H323_UserInputCapability(H323_UserInputCapability::SignalToneRFC2833));
		endPoint->SetSendUserInputMode(H323Connection::SendUserInputAsInlineRFC2833);
	}

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

	if (!listenPort)
		listenPort = 1720;
	
	tcpListener = new H323ListenerTCP(*endPoint, interfaceAddress, (WORD)listenPort);

	if (!endPoint->StartListener(tcpListener)) {
		cout << "ERROR: Could not open H.323 listener port on " << ((H323ListenerTCP *) tcpListener)->GetListenerPort() << endl;
		delete tcpListener;
		return 1;
		
	}
		
//	cout << "  == H.323 listener started on " << ((H323ListenerTCP *) tcpListener)->GetTransportAddress() << endl;
	cout << "  == H.323 listener started" << endl;

	return 0;
};
 
   
int h323_set_alias(struct oh323_alias *alias)
{
	char *p;
	char *num;
	PString h323id(alias->name);
	PString e164(alias->e164);
	
	if (!h323_end_point_exist()) {
		cout << "ERROR: [h323_set_alias] No Endpoint, this is bad!" << endl;
		return 1;
	}

	cout << "  == Adding alias \"" << h323id << "\" to endpoint" << endl;
	endPoint->AddAliasName(h323id);	

	endPoint->RemoveAliasName(localProcess->GetUserName());

	if (!e164.IsEmpty()) {
		cout << "  == Adding E.164 \"" << e164 << "\" to endpoint" << endl;
		endPoint->AddAliasName(e164);
	}
	if (strlen(alias->prefix)) {
		p = alias->prefix;
		num = strsep(&p, ",");
		while(num) {
	        cout << "  == Adding Prefix \"" << num << "\" to endpoint" << endl;
			endPoint->SupportedPrefixes += PString(num);
			endPoint->SetGateway();
	        num = strsep(&p, ",");		
		}
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

/** Establish Gatekeeper communiations, if so configured, 
  *	register aliases for the H.323 endpoint to respond to.
  */
int h323_set_gk(int gatekeeper_discover, char *gatekeeper, char *secret)
{
	PString gkName = PString(gatekeeper);
	PString pass   = PString(secret);

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
			cout << "  == Using " << (endPoint->GetGatekeeper())->GetName() << " as our Gatekeeper." << endl;
		} else {
			cout << "  *** Could not find a gatekeeper." << endl;
			return 1;
		}	
	} else {
		/* Gatekeeper operations */
		H323TransportUDP *rasChannel = new H323TransportUDP(*endPoint);
	
		if (!rasChannel) {
			cout << "	*** No RAS Channel, this is bad" << endl;
			return 1;
		}
		if (endPoint->SetGatekeeper(gkName, rasChannel)) {
			cout << "  == Using " << (endPoint->GetGatekeeper())->GetName() << " as our Gatekeeper." << endl;
		} else {
			cout << "  *** Error registering with gatekeeper \"" << gkName << "\". " << endl;
			
			/* XXX Maybe we should fire a new thread to attempt to re-register later and not kill asterisk here? */

		//	delete rasChannel;
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
int h323_make_call(char *host, call_details_t *cd, call_options_t *call_options)
{
	int res;
	PString	token;

	if (!h323_end_point_exist()) {
		return 1;
	}
	
	PString dest(host);

	res = endPoint->MakeCall(dest, token, &cd->call_reference, call_options);
	memcpy((char *)(cd->call_token), (const unsigned char *)token, token.GetLength());
	
	return res;
};

int h323_clear_call(const char *call_token)
{
	if (!h323_end_point_exist()) {
		return 1;
	}

	ClearCallThread	*clearCallThread = new ClearCallThread(call_token);
	clearCallThread->WaitForTermination();
	
	return 0;
};

/** This function tells the h.323 stack to either 
    answer or deny an incoming call  */
int h323_answering_call(const char *token, int busy) 
{
	const PString currentToken(token);

	H323Connection * connection;
	
	connection = endPoint->FindConnectionWithLock(currentToken);
	
	if (connection == NULL) {
		cout << "No connection found for " << token << endl;
		return -1;
	}

	if (!busy){
		connection->AnsweringCall(H323Connection::AnswerCallNow);
		connection->Unlock();

	} else {
		connection->AnsweringCall(H323Connection::AnswerCallDenied);
		connection->Unlock();
	};

	return 0;
}


int h323_show_codec(int fd, int argc, char *argv[])
{
	cout <<  "Allowed Codecs:\n\t" << setprecision(2) << endPoint->GetCapabilities() << endl;

	return 0;
}


/* alas, this doesn't work :(   */
void h323_native_bridge(const char *token, char *them, char *capability)
{
	H323Channel *channel;
	MyH323Connection *connection = (MyH323Connection *)endPoint->FindConnectionWithLock(token);
	
	if (!connection){
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

} /* extern "C" */
