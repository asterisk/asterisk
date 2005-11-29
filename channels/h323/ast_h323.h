/*
 * h323wrap.h
 *
 * OpenH323 Channel Driver for ASTERISK PBX.
 *			By Jeremy McNamara
 *                      For The NuFone Network 
 * 
 * This code has been derived from code created by 
 *		Michael Manousos and Mark Spencer
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
 */


#include <ptlib.h>
#include <h323.h>
#include <h323pdu.h>
#include <mediafmt.h>
#include <lid.h>

#include <list>
#include <string>
#include <algorithm>

#include "chan_h323.h"

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


/**This class describes the G.723.1 codec capability.
 */
class H323_G7231Capability : public H323AudioCapability
{
    PCLASSINFO(H323_G7231Capability, H323AudioCapability);
  public:
    H323_G7231Capability(
      BOOL annexA = TRUE  /// Enable Annex A silence insertion descriptors
    );
    Comparison Compare(const PObject & obj) const;

    PObject * Clone() const;
  
	virtual H323Codec * CreateCodec(
      H323Codec::Direction direction  /// Direction in which this instance runs
    ) const;

	unsigned GetSubType() const;
    PString GetFormatName() const;
    BOOL OnSendingPDU(
      H245_AudioCapability & pdu,  /// PDU to set information on
      unsigned packetSize          /// Packet size to use in capability
    ) const;

    BOOL OnReceivedPDU(
      const H245_AudioCapability & pdu,  /// PDU to get information from
      unsigned & packetSize              /// Packet size to use in capability
    );
  
  protected:
    BOOL annexA;
};

class MyH323EndPoint : public H323EndPoint {

	PCLASSINFO(MyH323EndPoint, H323EndPoint);

	public:

	int MakeCall(const PString &, PString &, unsigned int *, unsigned int);
	BOOL ClearCall(const PString &);
//	BOOL OnIncomingCall( H323Connection & connection, const H323SignalPDU &, H323SignalPDU &);

	void OnClosedLogicalChannel(H323Connection &, const H323Channel &);
	void OnConnectionEstablished(H323Connection &, const PString &);
	void OnConnectionCleared(H323Connection &, const PString &);
	H323Connection * CreateConnection(unsigned, void *);
	void SendUserTone(const PString &, char);
	H323Capabilities GetCapabilities(void);

	PStringArray SupportedPrefixes;	
	
    void SetEndpointTypeInfo( H225_EndpointType & info ) const;
    void SetGateway(void);
};

  
class MyH323Connection : public H323Connection {

	PCLASSINFO(MyH323Connection, H323Connection);

	public:
	MyH323Connection(MyH323EndPoint &, unsigned, unsigned, WORD);
	~MyH323Connection();

	H323Channel * CreateRealTimeLogicalChannel(const H323Capability &, H323Channel::Directions, unsigned, 
											   const H245_H2250LogicalChannelParameters *);
	H323Connection::AnswerCallResponse OnAnswerCall(const PString &, const H323SignalPDU &, H323SignalPDU &);
	BOOL OnAlerting(const H323SignalPDU &, const PString &);
	BOOL OnReceivedSignalSetup(const H323SignalPDU &);
	void OnReceivedReleaseComplete(const H323SignalPDU &);
	BOOL OnSendSignalSetup(H323SignalPDU &);
	BOOL OnStartLogicalChannel(H323Channel &);
	BOOL OnClosingLogicalChannel(H323Channel &);
	void SendUserInputTone(char, unsigned);
	void OnUserInputTone(char, unsigned, unsigned, unsigned);
	void OnUserInputString(const PString &value);

	PString sourceAliases;
	PString destAliases;
	PString sourceE164;
	PString destE164;

	PIPSocket::Address externalIpAddress;	// IP address of media server
    PIPSocket::Address remoteIpAddress;		// IP Address of remote endpoint
	WORD			   externalPort;		// local media server Data port (control is dataPort+1)
	WORD			   remotePort;			// remote endpoint Data port (control is dataPort+1)
};


#if 0
class MyGatekeeperServer : public H323GatekeeperServer
{
    PCLASSINFO(MyGatekeeperServer, H323GatekeeperServer);
  public:
    MyGatekeeperServer(MyH323EndPoint & ep);

    // Overrides
    virtual H323GatekeeperCall * CreateCall(
      const OpalGloballyUniqueID & callIdentifier,
      H323GatekeeperCall::Direction direction
    );
    virtual BOOL TranslateAliasAddressToSignalAddress(
      const H225_AliasAddress & alias,
      H323TransportAddress & address
    );

    // new functions
    BOOL Initialise();

  private:
    class RouteMap : public PObject {
        PCLASSINFO(RouteMap, PObject);
      public:
        RouteMap(
          const PString & alias,
          const PString & host
        );
        RouteMap(
          const RouteMap & map
        ) : alias(map.alias), regex(map.alias), host(map.host) { }

        void PrintOn(
          ostream & strm
        ) const;

        BOOL IsValid() const;

        BOOL IsMatch(
          const PString & alias
        ) const;

        const H323TransportAddress & GetHost() const { return host; }

      private:
        PString              alias;
        PRegularExpression   regex;
        H323TransportAddress host;
    };
    PList<RouteMap> routes;

    PMutex reconfigurationMutex;
};

#endif

/**
 * The MyProcess is a necessary descendant PProcess class so that the H323EndPoint 
 * objected to be created from within that class. (Who owns main() problem). 
 */
class MyProcess : public PProcess {

	PCLASSINFO(MyProcess, PProcess);
    
	public:
	MyProcess();
	~MyProcess();

	void Main(); 
	
	
};


/** 
 * This class handles the termination of a call.
 * Note that OpenH323 Library requires that the termination
 * of a call should be done inside a separate thread of execution.
 */
class ClearCallThread : public PThread {

	PCLASSINFO(ClearCallThread, PThread);

	public:
	ClearCallThread(const char *tc);
	~ClearCallThread();    
	
	void Main();
	
	protected:
	PString	token;
};


