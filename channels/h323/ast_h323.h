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

/**  These need to be redefined here because the C++
	 side of this driver is blind to the asterisk headers */	
/*! G.723.1 compression */
#define AST_FORMAT_G723_1	(1 << 0)
/*! GSM compression */
#define AST_FORMAT_GSM		(1 << 1)
/*! Raw mu-law data (G.711) */
#define AST_FORMAT_ULAW		(1 << 2)
/*! Raw A-law data (G.711) */
#define AST_FORMAT_ALAW		(1 << 3)
/*! MPEG-2 layer 3 */
#define AST_FORMAT_MP3		(1 << 4)
/*! ADPCM (whose?) */
#define AST_FORMAT_ADPCM	(1 << 5)
/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
#define AST_FORMAT_SLINEAR	(1 << 6)
/*! LPC10, 180 samples/frame */
#define AST_FORMAT_LPC10	(1 << 7)
/*! G.729A audio */
#define AST_FORMAT_G729A	(1 << 8)
/*! SpeeX Free Compression */
#define AST_FORMAT_SPEEX	(1 << 9)
/*! ILBC Free Codec */
#define AST_FORMAT_ILBC         (1 << 10)

/**This class describes the G.723.1 codec capability.
 */
class H323_G7231Capability : public H323AudioCapability
{
    PCLASSINFO(H323_G7231Capability, H323AudioCapability);
  public:
    	H323_G7231Capability(BOOL annexA = TRUE);
    	Comparison Compare(const PObject & obj) const;
   	PObject * Clone() const;
  	virtual H323Codec * CreateCodec(H323Codec::Direction direction) const;
	unsigned GetSubType() const;
	PString GetFormatName() const;
    	BOOL OnSendingPDU(H245_AudioCapability & pdu, unsigned packetSize) const;
	BOOL OnReceivedPDU(const H245_AudioCapability & pdu, unsigned & packetSize);
  protected:
    	BOOL annexA;
};

/**This class describes the (fake) G729 codec capability.
 */
class AST_G729Capability : public H323AudioCapability
{
  PCLASSINFO(AST_G729Capability, H323AudioCapability);

  public:
    AST_G729Capability();
    /* Create a copy of the object. */
    virtual PObject * Clone() const;

    /* Create the codec instance, allocating resources as required. */
    virtual H323Codec * CreateCodec(H323Codec::Direction direction) const;

    /* Get the sub-type of the capability. This is a code dependent on the
       main type of the capability.

       This returns one of the four possible combinations of mode and speed
       using the enum values of the protocol ASN H245_AudioCapability class. */
    virtual unsigned GetSubType() const;

    /* Get the name of the media data format this class represents. */
    virtual PString GetFormatName() const;

};

/* This class describes the VoiceAge G729A codec capability. */
class AST_G729ACapability : public H323AudioCapability
{
  PCLASSINFO(AST_G729ACapability, H323AudioCapability);

  public:
    /* Create a new G.729A capability. */
    AST_G729ACapability();

    /* Create a copy of the object. */
    virtual PObject * Clone() const;
    /* Create the codec instance, allocating resources as required. */
    virtual H323Codec * CreateCodec(H323Codec::Direction direction) const;

    /* Get the sub-type of the capability. This is a code dependent on the
       main type of the capability.

       This returns one of the four possible combinations of mode and speed
       using the enum values of the protocol ASN H245_AudioCapability class. */
    virtual unsigned GetSubType() const;

    /* Get the name of the media data format this class represents. */
    virtual PString GetFormatName() const;
};

class MyH323EndPoint : public H323EndPoint {

	PCLASSINFO(MyH323EndPoint, H323EndPoint);

	public:
	int MakeCall(const PString &, PString &, unsigned int *, call_options_t *opts);
	BOOL ClearCall(const PString &, H323Connection::CallEndReason reason);
	BOOL ClearCall(const PString &);

	void OnClosedLogicalChannel(H323Connection &, const H323Channel &);
	void OnConnectionEstablished(H323Connection &, const PString &);
	void OnConnectionCleared(H323Connection &, const PString &);
	H323Connection * CreateConnection(unsigned, void *);
	void SendUserTone(const PString &, char);
	H323Capabilities GetCapabilities(void);
	BOOL OnConnectionForwarded(H323Connection &, const PString &, const H323SignalPDU &);
	BOOL ForwardConnection(H323Connection &, const PString &, const H323SignalPDU &);
    	void SetEndpointTypeInfo( H225_EndpointType & info ) const;
    	void SetGateway(void);
	PStringArray SupportedPrefixes;	
};

class MyH323Connection : public H323Connection {

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
	BOOL OnAlerting(const H323SignalPDU &, const PString &);
	BOOL OnSendReleaseComplete(H323SignalPDU &);
	BOOL OnReceivedSignalSetup(const H323SignalPDU &);
	BOOL OnReceivedFacility(const H323SignalPDU &);
	BOOL OnSendSignalSetup(H323SignalPDU &);
	BOOL OnStartLogicalChannel(H323Channel &);
	BOOL OnClosingLogicalChannel(H323Channel &);	
	void SendUserInputTone(char, unsigned);
	void OnUserInputTone(char, unsigned, unsigned, unsigned);
	void OnUserInputString(const PString &value);
	BOOL OnReceivedProgress(const H323SignalPDU &);
	void OnSendCapabilitySet(H245_TerminalCapabilitySet &);
	BOOL OnReceivedCapabilitySet(const H323Capabilities &, const H245_MultiplexCapability *,
				     H245_TerminalCapabilitySetReject &);
	void SetCause(int _cause) { cause = _cause; };

	PString sourceAliases;
	PString destAliases;
	PString sourceE164;
	PString destE164;

	WORD sessionId;
	BOOL bridging;			

	unsigned progressSetup;
	unsigned progressAlert;
	int cause;

	RTP_DataFrame::PayloadTypes dtmfCodec;

	PString ast_cid_num;
	PString ast_cid_name;
};

class MyH323_ExternalRTPChannel : public H323_ExternalRTPChannel {

        PCLASSINFO(MyH323_ExternalRTPChannel, H323_ExternalRTPChannel);

        public:
	MyH323_ExternalRTPChannel(
		MyH323Connection & connection,
      		const H323Capability & capability, 
      		Directions direction,              
      		unsigned sessionID);

	~MyH323_ExternalRTPChannel();

	/* Overrides */
	BOOL Start(void);
	BOOL OnReceivedAckPDU(const H245_H2250LogicalChannelAckParameters & param);

	protected:
	BYTE payloadCode;

	PIPSocket::Address localIpAddr;
	PIPSocket::Address remoteIpAddr;
        WORD localPort;
        WORD remotePort;
}; 

/**
 * The MyProcess is a necessary descendant PProcess class so that the H323EndPoint 
 * objected to be created from within that class. (Solves the who owns main() problem). 
 */
class MyProcess : public PProcess {

	PCLASSINFO(MyProcess, PProcess);
    
	public:
	MyProcess();
	void Main(); 
};

#endif /* !defined AST_H323_H */
