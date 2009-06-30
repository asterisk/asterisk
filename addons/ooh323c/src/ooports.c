/*
 * Copyright (C) 2004-2005 by Objective Systems, Inc.
 *
 * This software is furnished under an open source license and may be 
 * used and copied only in accordance with the terms of this license. 
 * The text of the license may generally be found in the root 
 * directory of this installation in the COPYING file.  It 
 * can also be viewed online at the following URL:
 *
 *   http://www.obj-sys.com/open/license.html
 *
 * Any redistributions of this file including modified versions must 
 * maintain this copyright notice.
 *
 *****************************************************************************/


#include "ooports.h"
#include "ooh323ep.h"
#include "ootrace.h"

/** Global endpoint structure */
extern OOH323EndPoint gH323ep;

/* Get the next port of type TCP/UDP/RTP */
int ooGetNextPort (OOH323PortType type)
{
   if(type==OOTCP)
   {
      if(gH323ep.tcpPorts.current <= gH323ep.tcpPorts.max)
         return gH323ep.tcpPorts.current++;
      else
      {
         gH323ep.tcpPorts.current = gH323ep.tcpPorts.start;
         return gH323ep.tcpPorts.current++;
      }
   }
   if(type==OOUDP)
   {
      if(gH323ep.udpPorts.current <= gH323ep.udpPorts.max)
         return gH323ep.udpPorts.current++;
      else
      {
         gH323ep.udpPorts.current = gH323ep.udpPorts.start;
         return gH323ep.udpPorts.current++;
      }
   }
   if(type==OORTP)
   {
      if(gH323ep.rtpPorts.current <= gH323ep.rtpPorts.max)
         return gH323ep.rtpPorts.current++;
      else
      {
         gH323ep.rtpPorts.current = gH323ep.rtpPorts.start;
         return gH323ep.rtpPorts.current++;
      }
   }
   return OO_FAILED;
}

int ooBindPort (OOH323PortType type, OOSOCKET socket, char *ip)
{
   int initialPort, bindPort, ret;
   OOIPADDR ipAddrs;

   initialPort = ooGetNextPort (type);
   bindPort = initialPort;

   ret= ooSocketStrToAddr (ip, &ipAddrs);

   while(1)
   {
      if((ret=ooSocketBind(socket, ipAddrs, bindPort))==0)
      {
         return bindPort;
      }
      else
      {
         bindPort = ooGetNextPort (type);
         if (bindPort == initialPort) return OO_FAILED;
      }
   }
}

#ifdef _WIN32        
int ooBindOSAllocatedPort(OOSOCKET socket, char *ip)
{
   OOIPADDR ipAddrs;
   int size, ret;
   struct sockaddr_in name;
   size = sizeof(struct sockaddr_in);
   ret= ooSocketStrToAddr (ip, &ipAddrs);
   if((ret=ooSocketBind(socket, ipAddrs, 
                     0))==ASN_OK)
   {
      ret = ooSocketGetSockName(socket, &name, &size);
      if(ret == ASN_OK)
      {
         return name.sin_port;
         
      }
   }

   return OO_FAILED;
}
#endif
