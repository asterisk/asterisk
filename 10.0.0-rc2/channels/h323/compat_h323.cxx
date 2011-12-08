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
#include <ptlib.h>
#include <h323.h>
#include <transports.h>

#include "ast_h323.h"
#include "compat_h323.h"

#if VERSION(OPENH323_MAJOR,OPENH323_MINOR,OPENH323_BUILD) < VERSION(1,17,3)
MyH323TransportTCP::MyH323TransportTCP(
				H323EndPoint & endpoint,
				PIPSocket::Address binding,
				PBoolean listen)
	: H323TransportTCP(endpoint, binding, listen)
{
}

PBoolean MyH323TransportTCP::Connect()
{
	if (IsListening())
		return TRUE;

	PTCPSocket * socket = new PTCPSocket(remotePort);
	Open(socket);

	channelPointerMutex.StartRead();

	socket->SetReadTimeout(10000/*endpoint.GetSignallingChannelConnectTimeout()*/);

	localPort = endpoint.GetNextTCPPort();
	WORD firstPort = localPort;
	for (;;) {
		PTRACE(4, "H323TCP\tConnecting to "
				<< remoteAddress << ':' << remotePort
				<< " (local port=" << localPort << ')');
		if (socket->Connect(localAddress, localPort, remoteAddress))
			break;

		int errnum = socket->GetErrorNumber();
		if (localPort == 0 || (errnum != EADDRINUSE && errnum != EADDRNOTAVAIL)) {
			PTRACE(1, "H323TCP\tCould not connect to "
					<< remoteAddress << ':' << remotePort
					<< " (local port=" << localPort << ") - "
					<< socket->GetErrorText() << '(' << errnum << ')');
			channelPointerMutex.EndRead();
			return SetErrorValues(socket->GetErrorCode(), errnum);
		}

		localPort = endpoint.GetNextTCPPort();
		if (localPort == firstPort) {
			PTRACE(1, "H323TCP\tCould not bind to any port in range " <<
					endpoint.GetTCPPortBase() << " to " << endpoint.GetTCPPortMax());
			channelPointerMutex.EndRead();
			return SetErrorValues(socket->GetErrorCode(), errnum);
		}
	}

	socket->SetReadTimeout(PMaxTimeInterval);

	channelPointerMutex.EndRead();

	return OnOpen();
}
#endif

PBoolean MyH323TransportUDP::DiscoverGatekeeper(H323Gatekeeper &gk, H323RasPDU &pdu, const H323TransportAddress &address)
{
	PThread *thd = PThread::Current();

	/* If we run in OpenH323's thread use it instead of creating new one */
	if (thd)
		return H323TransportUDP::DiscoverGatekeeper(gk, pdu, address);

	/* Make copy of arguments to pass them into thread */
	discoverGatekeeper = &gk;
	discoverPDU = &pdu;
	discoverAddress = &address;

	/* Assume discovery thread isn't finished */
	discoverReady = FALSE;

	/* Create discovery thread */
	thd = PThread::Create(PCREATE_NOTIFIER(DiscoverMain), 0,
							PThread::NoAutoDeleteThread,
							PThread::NormalPriority,
							"GkDiscovery:%x");

	/* Wait until discovery thread signal us its finished */ 
	for(;;) {
		discoverMutex.Wait();
		if (discoverReady)		/* Thread has been finished */
			break;
		discoverMutex.Signal();
	}
	discoverMutex.Signal();

	/* Cleanup/delete thread */
	thd->WaitForTermination();
	delete thd;

	return discoverResult;
}

void MyH323TransportUDP::DiscoverMain(PThread &thread, INT arg)
{
	PWaitAndSignal m(discoverMutex);

	discoverResult = H323TransportUDP::DiscoverGatekeeper(*discoverGatekeeper, *discoverPDU, *discoverAddress);
	discoverReady = TRUE;
}
