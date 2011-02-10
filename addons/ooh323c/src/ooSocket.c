/*
 * Copyright (C) 1997-2005 by Objective Systems, Inc.
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
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/io.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/network.h"

#include "ooSocket.h"
#include "ootrace.h"
#if defined(_WIN32_WCE)
static int inited = 0;
#define SEND_FLAGS     0
#define SHUTDOWN_FLAGS 0
#elif defined (_WIN32)
static LPFN_SEND send;
static LPFN_SOCKET socket;
static LPFN_SETSOCKOPT setsockopt;
static LPFN_BIND bind;
static LPFN_HTONL htonl;
static LPFN_HTONS htons;
static LPFN_CONNECT connect;
static LPFN_INET_ADDR inet_addr;
static LPFN_LISTEN listen;
static LPFN_ACCEPT accept;
static LPFN_NTOHL ntohl;
static LPFN_NTOHS ntohs;
static LPFN_RECV recv;
static LPFN_SHUTDOWN shutdown;


static LPFN_IOCTLSOCKET ioctlsocket;
static LPFN_SENDTO sendto;
static LPFN_INET_NTOA inet_ntoa;
static LPFN_RECVFROM recvfrom;
static LPFN_SELECT select;
static LPFN_GETHOSTNAME gethostname;
static LPFN_GETHOSTBYNAME gethostbyname;
static LPFN_WSAGETLASTERROR  WSAGetLastError;
static LPFN_WSACLEANUP WSACleanup;
static LPFN_CLOSESOCKET closesocket;
static LPFN_GETSOCKNAME getsockname;
static HMODULE ws32 = 0;
#define SEND_FLAGS     0
#define SHUTDOWN_FLAGS SD_BOTH
#else
#define SEND_FLAGS     0
#define SHUTDOWN_FLAGS SHUT_RDWR
#define closesocket close
#endif



int ooSocketsInit ()
{
#if defined(_WIN32_WCE)
   WORD wVersionRequested;
   WSADATA wsaData;
   int err;

   if (inited) return ASN_OK; 

   wVersionRequested = MAKEWORD( 1, 1 );
    
   err = WSAStartup (wVersionRequested, &wsaData);
   if ( err != 0 ) {
      /* Tell the user that we could not find a usable */
      /* WinSock DLL.   */
      return ASN_E_NOTINIT;
   }
   inited = 1;

#elif defined (_WIN32)
   LPFN_WSASTARTUP wsaStartup = NULL;
   WSADATA wsaData;

   if (ws32 != 0) return ASN_OK;

//   ws32 = LoadLibrary ("WSOCK32.DLL");
  ws32 = LoadLibrary ("WS2_32.DLL");
   if (ws32 == NULL) return ASN_E_NOTINIT;
   
   wsaStartup = (LPFN_WSASTARTUP) GetProcAddress (ws32, "WSAStartup");
   if (wsaStartup == NULL) return ASN_E_NOTINIT;
   
   send = (LPFN_SEND) GetProcAddress (ws32, "send");
   if (send == NULL) return ASN_E_NOTINIT;
   
   socket = (LPFN_SOCKET) GetProcAddress (ws32, "socket");
   if (socket == NULL) return ASN_E_NOTINIT;
   
   setsockopt = (LPFN_SETSOCKOPT) GetProcAddress (ws32, "setsockopt");
   if (setsockopt == NULL) return ASN_E_NOTINIT;
   
   bind = (LPFN_BIND) GetProcAddress (ws32, "bind");
   if (bind == NULL) return ASN_E_NOTINIT;
   
   htonl = (LPFN_HTONL) GetProcAddress (ws32, "htonl");
   if (htonl == NULL) return ASN_E_NOTINIT;
   
   htons = (LPFN_HTONS) GetProcAddress (ws32, "htons");
   if (htons == NULL) return ASN_E_NOTINIT;
   
   connect = (LPFN_CONNECT) GetProcAddress (ws32, "connect");
   if (connect == NULL) return ASN_E_NOTINIT;
   
   listen = (LPFN_LISTEN) GetProcAddress (ws32, "listen");
   if (listen == NULL) return ASN_E_NOTINIT;
   
   accept = (LPFN_ACCEPT) GetProcAddress (ws32, "accept");
   if (accept == NULL) return ASN_E_NOTINIT;
   
   inet_addr = (LPFN_INET_ADDR) GetProcAddress (ws32, "inet_addr");
   if (inet_addr == NULL) return ASN_E_NOTINIT;
   
   ntohl = (LPFN_NTOHL) GetProcAddress (ws32, "ntohl");
   if (ntohl == NULL) return ASN_E_NOTINIT;
   
   ntohs = (LPFN_NTOHS) GetProcAddress (ws32, "ntohs");
   if (ntohs == NULL) return ASN_E_NOTINIT;
   
   recv = (LPFN_RECV) GetProcAddress (ws32, "recv");
   if (recv == NULL) return ASN_E_NOTINIT;
   
   shutdown = (LPFN_SHUTDOWN) GetProcAddress (ws32, "shutdown");
   if (shutdown == NULL) return ASN_E_NOTINIT;
   
   closesocket = (LPFN_CLOSESOCKET) GetProcAddress (ws32, "closesocket");
   if (closesocket == NULL) return ASN_E_NOTINIT;

   getsockname = (LPFN_GETSOCKNAME) GetProcAddress (ws32, "getsockname");
   if (getsockname == NULL) return ASN_E_NOTINIT;
   
   ioctlsocket = (LPFN_IOCTLSOCKET) GetProcAddress(ws32, "ioctlsocket");
   if(ioctlsocket == NULL) return ASN_E_NOTINIT;

   sendto = (LPFN_SENDTO) GetProcAddress (ws32, "sendto");
   if (sendto == NULL) return ASN_E_NOTINIT;

   inet_ntoa = (LPFN_INET_NTOA) GetProcAddress (ws32, "inet_ntoa");
   if (inet_ntoa == NULL) return ASN_E_NOTINIT;

   recvfrom = (LPFN_RECVFROM) GetProcAddress (ws32, "recvfrom");
   if (recvfrom == NULL) return ASN_E_NOTINIT;

   select = (LPFN_SELECT) GetProcAddress (ws32, "select");
   if (select == NULL) return ASN_E_NOTINIT;

   gethostname = (LPFN_GETHOSTNAME) GetProcAddress (ws32, "gethostname");
   if (gethostname == NULL) return ASN_E_NOTINIT;

   gethostbyname = (LPFN_GETHOSTBYNAME) GetProcAddress (ws32, "gethostbyname");
   if (gethostbyname == NULL) return ASN_E_NOTINIT;
   
   WSAGetLastError = (LPFN_WSAGETLASTERROR) GetProcAddress (ws32, 
                                                           "WSAGetLastError");
   if (WSAGetLastError == NULL) return ASN_E_NOTINIT;

   WSACleanup = (LPFN_WSACLEANUP) GetProcAddress (ws32, "WSACleanup");
   if (WSACleanup == NULL) return ASN_E_NOTINIT;
   
      
   if (wsaStartup (MAKEWORD(1, 1), &wsaData) == -1) return ASN_E_NOTINIT;
#endif
   return ASN_OK;
}

#if defined (_WIN32) || \
defined(_HP_UX) || defined(__hpux) || defined(_HPUX_SOURCE)
typedef int OOSOCKLEN;
#else
typedef socklen_t OOSOCKLEN;
#endif

int ooSocketCreate (OOSOCKET* psocket) 
{
   int on;
   int keepalive = 1, keepcnt = 24, keepidle = 120, keepintvl = 30;
   struct linger linger;
   OOSOCKET sock = socket (AF_INET,
                             SOCK_STREAM,
                             0);
  
   if (sock == OOSOCKET_INVALID){
      OOTRACEERR1("Error:Failed to create TCP socket\n");
      return ASN_E_INVSOCKET;
   }

   on = 1;
   if (setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, 
                   (const char* ) &on, sizeof (on)) == -1)
   {
      OOTRACEERR1("Error:Failed to set socket option SO_REUSEADDR\n");
      return ASN_E_INVSOCKET;
   }
   linger.l_onoff = 1;
   linger.l_linger = 0;
   if (setsockopt (sock, SOL_SOCKET, SO_LINGER, 
                   (const char* ) &linger, sizeof (linger)) == -1)
   {
      OOTRACEERR1("Error:Failed to set socket option linger\n");
      return ASN_E_INVSOCKET;
   }
   setsockopt (sock, SOL_SOCKET, SO_KEEPALIVE, (const char *)&keepalive,
			sizeof(keepalive));
   setsockopt (sock, SOL_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
   setsockopt (sock, SOL_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
   setsockopt (sock, SOL_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
   *psocket = sock;
   return ASN_OK;
}

int ooSocketCreateUDP (OOSOCKET* psocket) 
{
   int on;
   struct linger linger;

   OOSOCKET sock = socket (AF_INET,
                             SOCK_DGRAM,
                             0);

   if (sock == OOSOCKET_INVALID){
      OOTRACEERR1("Error:Failed to create UDP socket\n");
      return ASN_E_INVSOCKET;
   }

   on = 1;
   if (setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, 
                   (const char* ) &on, sizeof (on)) == -1)
   {
      OOTRACEERR1("Error:Failed to set socket option SO_REUSEADDR\n");
      return ASN_E_INVSOCKET;
   }
   linger.l_onoff = 1;
   linger.l_linger = 0;
   /*if (setsockopt (sock, SOL_SOCKET, SO_LINGER, 
                 (const char* ) &linger, sizeof (linger)) == -1)
      return ASN_E_INVSOCKET;
   */
   *psocket = sock;
   return ASN_OK;
}

int ooSocketClose (OOSOCKET socket)
{
   shutdown (socket, SHUTDOWN_FLAGS);
   if (closesocket (socket) == -1)
      return ASN_E_INVSOCKET;
   return ASN_OK;
}

int ooSocketBind (OOSOCKET socket, OOIPADDR addr, int port) 
{
   struct sockaddr_in m_addr;

   if (socket == OOSOCKET_INVALID)
   { 
      OOTRACEERR1("Error:Invalid socket passed to bind\n");
      return ASN_E_INVSOCKET;
   }

   memset (&m_addr, 0, sizeof (m_addr));
   m_addr.sin_family = AF_INET;
   m_addr.sin_addr.s_addr = (addr == 0) ? INADDR_ANY : htonl (addr);
   m_addr.sin_port = htons ((unsigned short)port);

   if (bind (socket, (struct sockaddr *) (void*) &m_addr,
                     sizeof (m_addr)) == -1)
   {
      if (errno != EADDRINUSE) {
      	perror ("bind");
      	OOTRACEERR2("Error:Bind failed, error: %d\n", errno);
      }
      return ASN_E_INVSOCKET;
   }

   return ASN_OK;
}


int ooSocketGetSockName(OOSOCKET socket, struct sockaddr_in *name, socklen_t *size)
{
   int ret;
   ret = getsockname(socket, (struct sockaddr*)name, size);
   if(ret == 0)
      return ASN_OK;
   else{
      OOTRACEERR1("Error:ooSocketGetSockName - getsockname\n");
      return ASN_E_INVSOCKET;
   }
}

int ooSocketGetIpAndPort(OOSOCKET socket, char *ip, int len, int *port)
{
   int ret=ASN_OK;
   socklen_t size;
   struct sockaddr_in addr;
   const char *host=NULL;

   size = sizeof(addr);

   ret = ooSocketGetSockName(socket, &addr, &size);
   if(ret != 0)
      return ASN_E_INVSOCKET;

   host = ast_inet_ntoa(addr.sin_addr);

   if(host && strlen(host) < (unsigned)len)
      strcpy(ip, host);   
   else{
     OOTRACEERR1("Error:Insufficient buffer for ip address - "
                 "ooSocketGetIpAndPort\n");
      return -1;
   }
   
   *port = addr.sin_port;

   return ASN_OK;
}

int ooSocketListen (OOSOCKET socket, int maxConnection) 
{
   if (socket == OOSOCKET_INVALID) return ASN_E_INVSOCKET;

   if (listen (socket, maxConnection) == -1)
      return ASN_E_INVSOCKET;

   return ASN_OK;
}

int ooSocketAccept (OOSOCKET socket, OOSOCKET *pNewSocket, 
                    OOIPADDR* destAddr, int* destPort) 
{
   struct sockaddr_in m_addr;
   OOSOCKLEN addr_length = sizeof (m_addr);

   if (socket == OOSOCKET_INVALID) return ASN_E_INVSOCKET;
   if (pNewSocket == 0) return ASN_E_INVPARAM;

   *pNewSocket = accept (socket, (struct sockaddr *) (void*) &m_addr, 
                         &addr_length);
   if (*pNewSocket <= 0) return ASN_E_INVSOCKET;

   if (destAddr != 0) 
      *destAddr = ntohl (m_addr.sin_addr.s_addr);
   if (destPort != 0)
      *destPort = ntohs (m_addr.sin_port);

   return ASN_OK;
}

int ooSocketConnect (OOSOCKET socket, const char* host, int port) 
{
   struct sockaddr_in m_addr;

   if (socket == OOSOCKET_INVALID)
   { 
      return ASN_E_INVSOCKET;
   }
   
   memset (&m_addr, 0, sizeof (m_addr));

   m_addr.sin_family = AF_INET;
   m_addr.sin_port = htons ((unsigned short)port);
   m_addr.sin_addr.s_addr = inet_addr (host);

   if (connect (socket, (struct sockaddr *) (void*) &m_addr, 
                sizeof (struct sockaddr_in)) == -1)
   {
      return ASN_E_INVSOCKET;
   }
   return ASN_OK;
}
/*
// **Need to add check whether complete data was sent by checking the return
// **value of send and if complete data is not sent then add mechanism to 
// **send remaining bytes. This will make ooSocketSend call atomic.
*/
int ooSocketSend (OOSOCKET socket, const ASN1OCTET* pdata, ASN1UINT size)
{
   if (socket == OOSOCKET_INVALID) return ASN_E_INVSOCKET;
   
   if (send (socket, (const char*) pdata, size, SEND_FLAGS) == -1)
      return ASN_E_INVSOCKET;
   return ASN_OK;
}

int ooSocketSendTo(OOSOCKET socket, const ASN1OCTET* pdata, ASN1UINT size,
                     const char* host, int port)
{
   struct sockaddr_in m_addr;
   if (socket == OOSOCKET_INVALID) return ASN_E_INVSOCKET;
   
   memset (&m_addr, 0, sizeof (m_addr));

   m_addr.sin_family = AF_INET;
   m_addr.sin_port = htons ((unsigned short)port);
   m_addr.sin_addr.s_addr = inet_addr (host);
   if (sendto (socket, (const char*) pdata, size, SEND_FLAGS, 
                                    (const struct sockaddr*)&m_addr, 
                                    sizeof(m_addr)) == -1)
      return ASN_E_INVSOCKET;
   return ASN_OK;
}

int ooSocketRecvPeek(OOSOCKET socket, ASN1OCTET* pbuf, ASN1UINT bufsize)
{
   int len;
   int flags = MSG_PEEK;

   if (socket == OOSOCKET_INVALID) return ASN_E_INVSOCKET;

   if ((len = recv (socket, (char*) pbuf, bufsize, flags)) == -1)
      return ASN_E_INVSOCKET;
   return len;
}

int ooSocketRecv (OOSOCKET socket, ASN1OCTET* pbuf, ASN1UINT bufsize)
{
   int len;
   if (socket == OOSOCKET_INVALID) return ASN_E_INVSOCKET;

   if ((len = recv (socket, (char*) pbuf, bufsize, 0)) == -1)
      return ASN_E_INVSOCKET;
   return len;
}

int ooSocketRecvFrom (OOSOCKET socket, ASN1OCTET* pbuf, ASN1UINT bufsize,
                      char* remotehost, ASN1UINT hostBufLen, int * remoteport)
{
   struct sockaddr_in m_addr;
   int len, addrlen;
   const char * host=NULL;
   if (socket == OOSOCKET_INVALID) return ASN_E_INVSOCKET;
   addrlen = sizeof(m_addr);

   memset (&m_addr, 0, sizeof (m_addr));
      
   if ((len = recvfrom (socket, (char*) pbuf, bufsize, 0, 
                        (struct sockaddr*)&m_addr, (socklen_t *) &addrlen)) == -1)
      return ASN_E_INVSOCKET;

   if(remoteport)
      *remoteport = ntohs(m_addr.sin_port);
   if(remotehost)
   {
      host = ast_inet_ntoa(m_addr.sin_addr);
      if(strlen(host) < (hostBufLen-1))
         strcpy(remotehost, host);
      else
         return -1;
   }
   return len;
}

int ooSocketSelect(int nfds, fd_set *readfds, fd_set *writefds, 
                     fd_set *exceptfds, struct timeval * timeout)
{
   int ret;   
#if defined (_WIN32)
  ret = select(nfds, readfds, writefds, exceptfds, 
             (const struct timeval *) timeout);
#else
   ret = select(nfds, readfds, writefds, exceptfds, timeout);
#endif
   return ret;
}

int ooSocketPoll(struct pollfd *pfds, int nfds, int timeout)
{
 return ast_poll(pfds, nfds, timeout);
}

int ooPDRead(struct pollfd *pfds, int nfds, int fd)
{
 int i;
 for (i=0;i<nfds;i++) 
  if (pfds[i].fd == fd && (pfds[i].revents & POLLIN))
   return 1;
 return 0;
}

int ooPDWrite(struct pollfd *pfds, int nfds, int fd)
{
 int i;
 for (i=0;i<nfds;i++)
  if (pfds[i].fd == fd && (pfds[i].revents & POLLOUT))
   return 1;
 return 0;
}

int ooGetLocalIPAddress(char * pIPAddrs)
{
   int ret;
   struct hostent *hp;
   struct ast_hostent phost;
   char hostname[100];

   if(pIPAddrs == NULL)
      return -1; /* Need to find suitable return value */
   ret = gethostname(hostname, 100);
   if(ret == 0)
   {
      if (!(hp = ast_gethostbyname(hostname, &phost))) {
	  		struct in_addr i;
			memcpy(&i, hp->h_addr, sizeof(i));
			  strcpy(pIPAddrs, (ast_inet_ntoa(i) == NULL) ? "127.0.0.1" : ast_inet_ntoa(i));
      } else {
         return -1;
      }
   }
   else{
      return -1;
   }
   return ASN_OK;
}

int ooSocketStrToAddr (const char* pIPAddrStr, OOIPADDR* pIPAddr) 
{
   int b1, b2, b3, b4;
   int rv = sscanf (pIPAddrStr, "%d.%d.%d.%d", &b1, &b2, &b3, &b4);
   if (rv != 4 ||
      (b1 < 0 || b1 > 256) || (b2 < 0 || b2 > 256) ||
      (b3 < 0 || b3 > 256) || (b4 < 0 || b4 > 256))
      return ASN_E_INVPARAM;
   *pIPAddr = ((b1 & 0xFF) << 24) | ((b2 & 0xFF) << 16) | 
              ((b3 & 0xFF) << 8) | (b4 & 0xFF);
   return ASN_OK;
}

int ooSocketConvertIpToNwAddr(char *inetIp, unsigned char *netIp)
{

   struct sockaddr_in sin = {0};
#ifdef _WIN32
   sin.sin_addr.s_addr = inet_addr(inetIp);
   if(sin.sin_addr.s_addr == INADDR_NONE)
   {
      OOTRACEERR1("Error:Failed to convert address\n");
      return -1;
   }
#else
   if(!inet_aton(inetIp, &sin.sin_addr))
   {
      OOTRACEERR1("Error:Failed to convert address\n");
      return -1;
   }
  
#endif
   
   memcpy(netIp, (char*)&sin.sin_addr.s_addr, sizeof(unsigned long));
   return ASN_OK;
}

int ooSocketAddrToStr (OOIPADDR ipAddr, char* pbuf, int bufsize)
{
   char buf1[5], buf2[5], buf3[5], buf4[5];
   int cnt = 0;

   if (bufsize < 8) 
      return ASN_E_BUFOVFLW;

   cnt += sprintf (buf1, "%lu", (ipAddr >> 24) & 0xFF);
   cnt += sprintf (buf2, "%lu", (ipAddr >> 16) & 0xFF);
   cnt += sprintf (buf3, "%lu", (ipAddr >> 8) & 0xFF);
   cnt += sprintf (buf4, "%lu", ipAddr & 0xFF);
   if (bufsize < cnt + 4)
      return ASN_E_BUFOVFLW;
   sprintf (pbuf, "%s.%s.%s.%s", buf1, buf2, buf3, buf4);
   return ASN_OK;
}

int ooSocketsCleanup (void)
{
#ifdef _WIN32
   int ret = WSACleanup();
   if(ret == 0)
      return ASN_OK;
   else
      return ret;
#endif
   return ASN_OK;
}

long ooSocketHTONL(long val)
{
   return htonl(val);
}

short ooSocketHTONS(short val)
{
   return htons(val);
}

#ifndef _WIN32
int ooSocketGetInterfaceList(OOCTXT *pctxt, OOInterface **ifList)
{
   OOSOCKET sock;
   struct ifconf ifc;
   int ifNum;
   OOInterface *pIf=NULL;
   struct sockaddr_in sin;

   OOTRACEDBGA1("Retrieving local interfaces\n");
   if(ooSocketCreateUDP(&sock)!= ASN_OK)
   {
      OOTRACEERR1("Error:Failed to create udp socket - "
                  "ooSocketGetInterfaceList\n");   
      return -1;
   }
#ifdef SIOCGIFNUM
   if(ioctl(sock, SIOCGIFNUM, &ifNum) >= 0)
   {
      OOTRACEERR1("Error: ioctl for ifNum failed\n");
      return -1;
   }
#else
   ifNum = 50;
#endif
 
   ifc.ifc_len = ifNum * sizeof(struct ifreq);
   ifc.ifc_req = (struct ifreq *)memAlloc(pctxt, ifNum *sizeof(struct ifreq));
   if(!ifc.ifc_req)
   {
      OOTRACEERR1("Error:Memory - ooSocketGetInterfaceList - ifc.ifc_req\n");
      return -1;
   }

   if (ioctl(sock, SIOCGIFCONF, &ifc) >= 0) {
      void * ifEndList = (char *)ifc.ifc_req + ifc.ifc_len;
      struct ifreq *ifName;
      struct ifreq ifReq;
      int flags;
      for (ifName = ifc.ifc_req; (void*)ifName < ifEndList; ifName++) {
         char *pName=NULL;
         char addr[50];
#ifdef ifr_netmask
	char mask[50];
#endif
         
         pIf = (struct OOInterface*)memAlloc(pctxt, sizeof(struct OOInterface));
         pName = (char*)memAlloc(pctxt, strlen(ifName->ifr_name)+1);
         if(!pIf)
         {
            OOTRACEERR1("Error:Memory - ooSocketGetInterfaceList - "
                        "pIf/pName\n");
            return -1;
         }
         OOTRACEDBGA2("\tInterface name: %s\n", ifName->ifr_name);
         
         
         strcpy(ifReq.ifr_name, ifName->ifr_name);
         strcpy(pName, ifName->ifr_name);
         pIf->name = pName;

         /* Check whether the interface is up*/
         if (ioctl(sock, SIOCGIFFLAGS, &ifReq) < 0) {
            OOTRACEERR2("Error:Unable to determine status of interface %s\n", 
                        pName);
            memFreePtr(pctxt, pIf->name);
            memFreePtr(pctxt, pIf);
            continue;
         }
         flags = ifReq.ifr_flags;
         if (!(flags & IFF_UP)) {
            OOTRACEWARN2("Warn:Interface %s is not up\n", pName);
            memFreePtr(pctxt, pIf->name);
            memFreePtr(pctxt, pIf);
            continue;
         }

         /* Retrieve interface address */
         if (ioctl(sock, SIOCGIFADDR, &ifReq) < 0) 
         {
            OOTRACEWARN2("Warn:Unable to determine address of interface %s\n", 
                          pName);
            memFreePtr(pctxt, pIf->name);
            memFreePtr(pctxt, pIf);
            continue;
         }
	 memcpy(&sin, &ifReq.ifr_addr, sizeof(struct sockaddr_in));
	 strcpy(addr, ast_inet_ntoa(sin.sin_addr));
         OOTRACEDBGA2("\tIP address is %s\n", addr);
         pIf->addr = (char*)memAlloc(pctxt, strlen(addr)+1);
         if(!pIf->addr)
         {
            OOTRACEERR1("Error:Memory - ooSocketGetInterfaceList - "
                        "pIf->addr\n");
            memFreePtr(pctxt, pIf->name);
            memFreePtr(pctxt, pIf);
            return -1;
         }
         strcpy(pIf->addr, addr);
         
#ifdef ifr_netmask
         if (ioctl(sock, SIOCGIFNETMASK, &ifReq) < 0) 
         {
            OOTRACEWARN2("Warn:Unable to determine mask for interface %s\n", 
                          pName);
            memFreePtr(pctxt, pIf->name);
            memFreePtr(pctxt, pIf->addr);
            memFreePtr(pctxt, pIf);
            continue;
         }
	 memcpy(&sin, &ifReq.ifr_netmask, sizeof(struct sockaddr_in));
	 strcpy(mask, ast_inet_ntoa(sin.sin_addr));
         OOTRACEDBGA2("\tMask is %s\n", mask);
         pIf->mask = (char*)memAlloc(pctxt, strlen(mask)+1);
         if(!pIf->mask)
         {
            OOTRACEERR1("Error:Memory - ooSocketGetInterfaceList - "
                        "pIf->mask\n");
            memFreePtr(pctxt, pIf->name);
            memFreePtr(pctxt, pIf->addr);
            memFreePtr(pctxt, pIf);
            return -1;
         }
         strcpy(pIf->mask, mask);
#endif
         pIf->next = NULL;

         /* Add to the list */
         if(!*ifList)
         {
            *ifList = pIf;
            pIf = NULL;
         }
         else{
            pIf->next = *ifList;
            *ifList = pIf;
            pIf=NULL;
         }
/*
#if defined(OO_FREEBSD) || defined(OO_OPENBSD) || defined(OO_NETBSD) || defined(OO_MACOSX) || defined(OO_VXWORKS) || defined(OO_RTEMS) || defined(OO_QNX)
#ifndef _SIZEOF_ADDR_IFREQ
#define _SIZEOF_ADDR_IFREQ(ifr) \
        ((ifr).ifr_addr.sa_len > sizeof(struct sockaddr) ? \
         (sizeof(struct ifreq) - sizeof(struct sockaddr) + \
          (ifr).ifr_addr.sa_len) : sizeof(struct ifreq))
#endif
      ifName = (struct ifreq *)((char *)ifName + _SIZEOF_ADDR_IFREQ(*ifName));
#else
      ifName++;
*/
      }

   }  
   return ASN_OK;
}
#endif
