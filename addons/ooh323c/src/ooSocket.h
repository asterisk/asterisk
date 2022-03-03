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

/**
 * @file ooSocket.h
 * Common runtime constants, data structure definitions, and run-time functions
 * to support the sockets' operations.
 */
#ifndef _OOSOCKET_H_
#define _OOSOCKET_H_

#include "asterisk/poll-compat.h"
#include "asterisk/compiler.h"

#ifdef _WIN32_WCE
#include <winsock.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <sys/types.h>
#define INCL_WINSOCK_API_TYPEDEFS   1
#define INCL_WINSOCK_API_PROTOTYPES 0
#include <winsock2.h>
#else
#include <sys/types.h>
#include "sys/time.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#endif

#include "ooasn1.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#ifdef MAKE_DLL
#define EXTERN __declspec(dllexport)
#elif defined (USEASN1DLL)
#define EXTERN __declspec(dllimport)
#else
#define EXTERN
#endif /* MAKE_DLL */
#endif /* EXTERN */

/**
 * @defgroup sockets Socket Layer
 * @{
 */
#if defined (_WIN64)
typedef unsigned __int64 OOSOCKET; /**< Socket's handle */
#elif defined (_WIN32)
typedef unsigned int OOSOCKET; /**< Socket's handle */
#else
typedef int OOSOCKET;          /**< Socket's handle */
#endif

#define OOSOCKET_INVALID ((OOSOCKET)-1)


/**
 * The IP address represented as unsigned long value. The most significant 8
 * bits in this unsigned long value represent the first number of the IP
 * address. The least significant 8 bits represent the last number of the IP
 * address.
 */
/* typedef unsigned long OOIPADDR; */
typedef struct ast_sockaddr OOIPADDR;

#define OOIPADDR_ANY   ((OOIPADDR)0)
#define OOIPADDR_LOCAL ((OOIPADDR)0x7f000001UL) /* 127.0.0.1 */

typedef struct OOInterface{
   char *name;
   char *addr;
   char *mask;
   struct OOInterface *next;
}OOInterface;



/**
 * This function permits an incoming connection attempt on a socket. It
 * extracts the first connection on the queue of pending connections on socket.
 * It then creates a new socket and returns a handle to the new socket. The
 * newly created socket is the socket that will handle the actual connection
 * and has the same properties as original socket. See description of 'accept'
 * socket function for further details.
 *
 * @param socket       The socket's handle created by call to ::rtSocketCreate
 *                     function.
 * @param pNewSocket   The pointer to variable to receive the new socket's
 *                     handle.
 * @param destAddr     Optional pointer to a buffer that receives the IP
 *                     address of the connecting entity. It may be NULL.
 * @param destPort     Optional pointer to a buffer that receives the port of
 *                     the connecting entity. It may be NULL.
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketAccept (OOSOCKET socket, OOSOCKET *pNewSocket,
                             char* destAddr, int* destPort);

/**
 * This function converts an IP address to its string representation.
 *
 * @param ipAddr       The IP address to be converted.
 * @param pbuf         Pointer to the buffer to receive a string with the IP
 *                     address.
 * @param bufsize      Size of the buffer.
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketAddrToStr (OOIPADDR ipAddr, char* pbuf, int bufsize);

/**
 * This function associates a local address with a socket. It is used on an
 * unconnected socket before subsequent calls to the ::rtSocketConnect or
 * ::rtSocketListen functions. See description of 'bind' socket function for
 * further details.
 *
 * @param socket       The socket's handle created by call to ::rtSocketCreate
 *                     function.
 * @param addr         The local IP address to assign to the socket.
 * @param port         The local port number to assign to the socket.
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketBind (OOSOCKET socket, OOIPADDR addr, int port);

/**
 * This function closes an existing socket.
 *
 * @param socket       The socket's handle created by call to ::rtSocketCreate
 *                     or ::rtSocketAccept function.
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketClose (OOSOCKET socket);

/**
 * This function establishes a connection to a specified socket. It is used to
 * create a connection to the specified destination. When the socket call
 * completes successfully, the socket is ready to send and receive data. See
 * description of 'connect' socket function for further details.
 *
 * @param socket       The socket's handle created by call to ::rtSocketCreate
 *                     function.
 * @param host         The null-terminated string with the IP address in the
 *                     following format: "NNN.NNN.NNN.NNN", where NNN is a
 *                     number in the range (0..255).
 * @param port         The destination port to connect.
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketConnect (OOSOCKET socket, const char* host, int port);

/**
 * This function creates a socket. The only streaming TCP/IP sockets are
 * supported at the moment.
 *
 * @param psocket      The pointer to the socket's handle variable to receive
 *                     the handle of new socket.
 * @param family       Which family socket will created
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketCreate (OOSOCKET* psocket, int family);

/**
 * This function creates a UDP datagram socket.
 *
 * @param psocket      The pointer to the socket's handle variable to receive
 *                     the handle of new socket.
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketCreateUDP (OOSOCKET* psocket, int family);

/**
 * This function initiates use of sockets by an application. This function must
 * be called first before use sockets.
 *
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketsInit (void);

/**
 * This function terminates use of sockets by an application. This function
 * must be called after done with sockets.
 *
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketsCleanup (void);

/**
 * This function places a socket a state where it is listening for an incoming
 * connection. To accept connections, a socket is first created with the
 * ::rtSocketCreate function and bound to a local address with the
 * ::rtSocketBind function, a maxConnection for incoming connections is
 * specified with ::rtSocketListen, and then the connections are accepted with
 * the ::rtSocketAccept function. See description of 'listen' socket function
 * for further details.
 *
 * @param socket        The socket's handle created by call to
 *                      ::rtSocketCreate function.
 * @param maxConnection Maximum length of the queue of pending connections.
 * @return              Completion status of operation: 0 (ASN_OK) =
 *                      success, negative return value is error.
 */
EXTERN int ooSocketListen (OOSOCKET socket, int maxConnection);


/**
 * This function is used to peek at the received data without actually removing
 * it from the receive socket buffer. A receive call after this will get the
 * same data from the socket.
 * @param socket       The socket's handle created by call to ::rtSocketCreate
 *                     or ::rtSocketAccept function.
 * @param pbuf         Pointer to the buffer for the incoming data.
 * @param bufsize      Length of the buffer.
 * @return             If no error occurs, returns the number of bytes
 *                     received. Otherwise, the negative value is error code.
 */
EXTERN int ooSocketRecvPeek
   (OOSOCKET socket, ASN1OCTET* pbuf, ASN1UINT bufsize);

/**
 * This function receives data from a connected socket. It is used to read
 * incoming data on sockets. The socket must be connected before calling this
 * function. See description of 'recv' socket function for further details.
 *
 * @param socket       The socket's handle created by call to ::rtSocketCreate
 *                     or ::rtSocketAccept function.
 * @param pbuf         Pointer to the buffer for the incoming data.
 * @param bufsize      Length of the buffer.
 * @return             If no error occurs, returns the number of bytes
 *                     received. Otherwise, the negative value is error code.
 */
EXTERN int ooSocketRecv (OOSOCKET socket, ASN1OCTET* pbuf,
                           ASN1UINT bufsize);

/**
 * This function receives data from a connected/unconnected socket. It is used
 * to read incoming data on sockets. It populates the remotehost and
 * remoteport parameters with information of remote host. See description of
 * 'recvfrom' socket function for further details.
 *
 * @param socket       The socket's handle created by call to ooSocketCreate
 *
 * @param pbuf         Pointer to the buffer for the incoming data.
 * @param bufsize      Length of the buffer.
 * @param remotehost   Pointer to a buffer in which remote ip address
 *                     will be returned.
 * @param hostBufLen   Length of the buffer passed for remote ip address.
 * @param remoteport   Pointer to an int in which remote port number
 *                     will be returned.
 *
 * @return             If no error occurs, returns the number of bytes
 *                     received. Otherwise, negative value.
 */
EXTERN int ooSocketRecvFrom (OOSOCKET socket, ASN1OCTET* pbuf,
                             ASN1UINT bufsize, char * remotehost,
                             ASN1UINT hostBufLen, int * remoteport);
/**
 * This function sends data on a connected socket. It is used to write outgoing
 * data on a connected socket. See description of 'send' socket function for
 * further details.
 *
 * @param socket       The socket's handle created by call to ::rtSocketCreate
 *                     or ::rtSocketAccept function.
 * @param pdata        Buffer containing the data to be transmitted.
 * @param size         Length of the data in pdata.
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketSend (OOSOCKET socket, const ASN1OCTET* pdata,
                           ASN1UINT size);

/**
 * This function sends data on a connected or unconnected socket. See
 * description of 'sendto' socket function for further details.
 *
 * @param socket       The socket's handle created by call to ::rtSocketCreate
 *                       or ::rtSocketAccept function.
 * @param pdata        Buffer containing the data to be transmitted.
 * @param size         Length of the data in pdata.
 * @param remotehost   Remote host ip address to which data has to
 *                     be sent.
 * @param remoteport   Remote port ip address to which data has to
 *                     be sent.
 *
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketSendTo(OOSOCKET socket, const ASN1OCTET* pdata,
                            ASN1UINT size, const char* remotehost,
                            int remoteport);

/**
 * This function is used for synchronous monitoring of multiple sockets.
 * For more information refer to documentation of "select" system call.
 *
 * @param nfds         The highest numbered descriptor to be monitored
 *                     plus one.
 * @param readfds      The descriptors listed in readfds will be watched for
 *                     whether read would block on them.
 * @param writefds     The descriptors listed in writefds will be watched for
 *                     whether write would block on them.
 * @param exceptfds    The descriptors listed in exceptfds will be watched for
 *                     exceptions.
 * @param timeout      Upper bound on amout of time elapsed before select
 *                     returns.
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
EXTERN int ooSocketSelect(int nfds, fd_set *readfds, fd_set *writefds,
                            fd_set *exceptfds, struct timeval * timeout) attribute_deprecated;

EXTERN int ooSocketPoll(struct pollfd *pfds, int nfds, int timeout);

EXTERN int ooPDRead(struct pollfd *pfds, int nfds, int fd);
EXTERN int ooPDWrite(struct pollfd *pfds, int nfds, int fd);

/**
 * This function converts the string with IP address to a double word
 * representation. The converted address may be used with the ::rtSocketBind
 * function.
 *
 * @param pIPAddrStr   The null-terminated string with the IP address in the
 *                     following format: "NNN.NNN.NNN.NNN", where NNN is a
 *                     number in the range (0..255).
 * @param pIPAddr      Pointer to the converted IP address.
 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
/* EXTERN int ooSocketStrToAddr (const char* pIPAddrStr, OOIPADDR* pIPAddr); */

/**
 * This function converts an internet dotted ip address to network address
 *
 * @param inetIp       The null-terminated string with the IP address in the
 *                     following format: "NNN.NNN.NNN.NNN", where NNN is a
 *                     number in the range (0..255).
 * @param netIp        Buffer in which the converted address will be returned.

 * @return             Completion status of operation: 0 (ASN_OK) = success,
 *                     negative return value is error.
 */
/* EXTERN int ooSocketConvertIpToNwAddr(char *inetIp, unsigned char *netIp); */

/**
 * This function retrives the IP address of the local host.
 *
 * @param pIPAddrs   Pointer to a char buffer in which local IP address will be
 *                   returned.
 * @return           Completion status of operation: 0 (ASN_OK) = success,
 *                   negative return value is error.
 */
EXTERN int ooGetLocalIPAddress(char * pIPAddrs);


EXTERN int ooSocketGetSockName(OOSOCKET socket, struct sockaddr_in *name,
                                                      socklen_t *size);


EXTERN long ooSocketHTONL(long val);

EXTERN short ooSocketHTONS(short val);

/**
 * This function is used to retrieve the ip and port number used by the socket
 * passed as parameter. It internally uses getsockname system call for this
 * purpose.
 * @param socket  Socket for which ip and port has to be determined.
 * @param ip      Buffer in which ip address will be returned.
 * @param len     Length of the ip address buffer.
 * @param port    Pointer to integer in which port number will be returned.
 * @param family  Pointer to integer in which IP family (4 or 6) will be returned
 *
 * @return        ASN_OK, on success; -ve on failed.
 */
EXTERN int ooSocketGetIpAndPort(OOSOCKET socket, char *ip, int len, int *port, int *family);


EXTERN int ooSocketGetInterfaceList(OOCTXT *pctxt, OOInterface **ifList);
/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif /* _OOSOCKET_H_ */
