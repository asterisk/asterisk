/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Viagénie <asteriskv6@viagenie.ca>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief Network socket handling
 */

#ifndef _ASTERISK_NETSOCK2_H
#define _ASTERISK_NETSOCK2_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <sys/socket.h>

#include <netinet/in.h>

/*!
 * Values for address families that we support. This is reproduced from socket.h
 * because we do not want users to include that file. Only netsock2.c should
 * ever include socket.h.
 */
enum {
	AST_AF_UNSPEC	= 0,
	AST_AF_INET	= 2,
	AST_AF_INET6	= 10,
};

/*!
 * \brief Socket address structure.
 *
 * \details
 * The first member is big enough to contain addresses of any
 * family. The second member contains the length (in bytes) used
 * in the first member.
 *
 * \note
 * Some BSDs have the length embedded in sockaddr structs. We
 * ignore them. (This is the right thing to do.)
 *
 * \note
 * It is important to always initialize ast_sockaddr before use
 * -- even if they are passed to ast_sockaddr_copy() as the
 * underlying storage could be bigger than what ends up being
 * copied -- leaving part of the data unitialized.
 */
struct ast_sockaddr {
	struct sockaddr_storage	 ss;
	socklen_t len;
};

/*!
 * \brief
 * Convert an IPv4-mapped IPv6 address into an IPv4 address.
 *
 * \warning You should rarely need this function. Only call this
 * if you know what you're doing.
 *
 * \param addr The IPv4-mapped address to convert
 * \param mapped_addr The resulting IPv4 address
 * \retval 0 Unable to make the conversion
 * \retval 1 Successful conversion
 */
int ast_sockaddr_ipv4_mapped(const struct ast_sockaddr *addr, struct ast_sockaddr *ast_mapped);

/*!
 * \since 1.8
 *
 * \brief
 * Checks if the ast_sockaddr is null. "null" in this sense essentially
 * means uninitialized, or having a 0 length.
 *
 * \param addr Pointer to the ast_sockaddr we wish to check
 * \retval 1 \a addr is null
 * \retval 0 \a addr is non-null.
 */
static inline int ast_sockaddr_isnull(const struct ast_sockaddr *addr)
{
	return !addr || addr->len == 0;
}

/*!
 * \since 1.8
 *
 * \brief
 * Sets address \a addr to null.
 *
 * \retval void
 */
static inline void ast_sockaddr_setnull(struct ast_sockaddr *addr)
{
	addr->len = 0;
}

/*!
 * \since 1.8
 *
 * \brief
 * Copies the data from one ast_sockaddr to another
 *
 * \param dst The destination ast_sockaddr
 * \param src The source ast_sockaddr
 * \retval void
 */
static inline void ast_sockaddr_copy(struct ast_sockaddr *dst,
		const struct ast_sockaddr *src)
{
	memcpy(dst, src, src->len);
	dst->len = src->len;
};

/*!
 * \since 1.8
 *
 * \brief
 * Compares two ast_sockaddr structures
 *
 * \retval -1 \a a is lexicographically smaller than \a b
 * \retval 0 \a a is equal to \a b
 * \retval 1 \a b is lexicographically smaller than \a a
 */
int ast_sockaddr_cmp(const struct ast_sockaddr *a, const struct ast_sockaddr *b);

/*!
 * \since 1.8
 *
 * \brief
 * Compares the addresses of two ast_sockaddr structures.
 *
 * \retval -1 \a a is lexicographically smaller than \a b
 * \retval 0 \a a is equal to \a b
 * \retval 1 \a b is lexicographically smaller than \a a
 */
int ast_sockaddr_cmp_addr(const struct ast_sockaddr *a, const struct ast_sockaddr *b);

#define AST_SOCKADDR_STR_ADDR		(1 << 0)
#define AST_SOCKADDR_STR_PORT		(1 << 1)
#define AST_SOCKADDR_STR_BRACKETS	(1 << 2)
#define AST_SOCKADDR_STR_HOST		AST_SOCKADDR_STR_ADDR | AST_SOCKADDR_STR_BRACKETS
#define AST_SOCKADDR_STR_DEFAULT	AST_SOCKADDR_STR_ADDR | AST_SOCKADDR_STR_PORT

/*!
 * \since 1.8
 *
 * \brief
 * Convert a socket address to a string.
 *
 * \details
 * This will be of the form a.b.c.d:xyz
 * for IPv4 and [a:b:c:...:d]:xyz for IPv6.
 *
 * This function is thread-safe. The returned string is on static
 * thread-specific storage.
 *
 * \param addr The input to be stringified
 * \param format one of the following:
 * AST_SOCKADDR_STR_DEFAULT:
 *    a.b.c.d:xyz for IPv4
 *    [a:b:c:...:d]:xyz for IPv6.
 * AST_SOCKADDR_STR_ADDR: address only
 *    a.b.c.d for IPv4
 *    a:b:c:...:d for IPv6.
 * AST_SOCKADDR_STR_HOST: address only, suitable for a URL
 *    a.b.c.d for IPv4
 *    [a:b:c:...:d] for IPv6.
 * AST_SOCKADDR_STR_PORT: port only
 * \retval "(null)" \a addr is null
 * \retval "" An error occurred during processing
 * \retval string The stringified form of the address
 */
char *ast_sockaddr_stringify_fmt(const struct ast_sockaddr *addr, int format);

/*!
 * \since 1.8
 *
 * \brief
 * Wrapper around ast_sockaddr_stringify_fmt() with default format
 *
 * \return same as ast_sockaddr_stringify_fmt()
 */
static inline char *ast_sockaddr_stringify(const struct ast_sockaddr *addr)
{
	return ast_sockaddr_stringify_fmt(addr, AST_SOCKADDR_STR_DEFAULT);
}

/*!
 * \since 1.8
 *
 * \brief
 * Wrapper around ast_sockaddr_stringify_fmt() to return an address only
 *
 * \return same as ast_sockaddr_stringify_fmt()
 */
static inline char *ast_sockaddr_stringify_addr(const struct ast_sockaddr *addr)
{
	return ast_sockaddr_stringify_fmt(addr, AST_SOCKADDR_STR_ADDR);
}

/*!
 * \since 1.8
 *
 * \brief
 * Wrapper around ast_sockaddr_stringify_fmt() to return an address only,
 * suitable for a URL (with brackets for IPv6).
 *
 * \return same as ast_sockaddr_stringify_fmt()
 */
static inline char *ast_sockaddr_stringify_host(const struct ast_sockaddr *addr)
{
	return ast_sockaddr_stringify_fmt(addr, AST_SOCKADDR_STR_HOST);
}

/*!
 * \since 1.8
 *
 * \brief
 * Wrapper around ast_sockaddr_stringify_fmt() to return a port only
 *
 * \return same as ast_sockaddr_stringify_fmt()
 */
static inline char *ast_sockaddr_stringify_port(const struct ast_sockaddr *addr)
{
	return ast_sockaddr_stringify_fmt(addr, AST_SOCKADDR_STR_PORT);
}

/*!
 * \since 1.8
 *
 * \brief
 * Splits a string into its host and port components
 *
 * \param str[in]   The string to parse. May be modified by writing a NUL at the end of
 *                  the host part.
 * \param host[out] Pointer to the host component within \a str.
 * \param port[out] Pointer to the port component within \a str.
 * \param flags     If set to zero, a port MAY be present. If set to PARSE_PORT_IGNORE, a
 *                  port MAY be present but will be ignored. If set to PARSE_PORT_REQUIRE,
 *                  a port MUST be present. If set to PARSE_PORT_FORBID, a port MUST NOT
 *                  be present.
 *
 * \retval 1 Success
 * \retval 0 Failure
 */
int ast_sockaddr_split_hostport(char *str, char **host, char **port, int flags);

/*!
 * \since 1.8
 *
 * \brief
 * Parse an IPv4 or IPv6 address string.
 *
 * \details
 * Parses a string containing an IPv4 or IPv6 address followed by an optional
 * port (separated by a colon) into a struct ast_sockaddr. The allowed formats
 * are the following:
 *
 * a.b.c.d
 * a.b.c.d:port
 * a:b:c:...:d
 * [a:b:c:...:d]
 * [a:b:c:...:d]:port
 *
 * Host names are NOT allowed.
 *
 * \param[out] addr The resulting ast_sockaddr
 * \param str The string to parse
 * \param flags If set to zero, a port MAY be present. If set to
 * PARSE_PORT_IGNORE, a port MAY be present but will be ignored. If set to
 * PARSE_PORT_REQUIRE, a port MUST be present. If set to PARSE_PORT_FORBID, a
 * port MUST NOT be present.
 *
 * \retval 1 Success
 * \retval 0 Failure
 */
int ast_sockaddr_parse(struct ast_sockaddr *addr, const char *str, int flags);

/*!
 * \since 1.8
 *
 * \brief
 * Parses a string with an IPv4 or IPv6 address and place results into an array
 *
 * \details
 * Parses a string containing a host name or an IPv4 or IPv6 address followed
 * by an optional port (separated by a colon).  The result is returned into a
 * array of struct ast_sockaddr. Allowed formats for str are the following:
 *
 * hostname:port
 * host.example.com:port
 * a.b.c.d
 * a.b.c.d:port
 * a:b:c:...:d
 * [a:b:c:...:d]
 * [a:b:c:...:d]:port
 *
 * \param[out] addrs The resulting array of ast_sockaddrs
 * \param str The string to parse
 * \param flags If set to zero, a port MAY be present. If set to
 * PARSE_PORT_IGNORE, a port MAY be present but will be ignored. If set to
 * PARSE_PORT_REQUIRE, a port MUST be present. If set to PARSE_PORT_FORBID, a
 * port MUST NOT be present.
 *
 * \param family Only addresses of the given family will be returned. Use 0 or
 * AST_SOCKADDR_UNSPEC to get addresses of all families.
 *
 * \retval 0 Failure
 * \retval non-zero The number of elements in addrs array.
 */
int ast_sockaddr_resolve(struct ast_sockaddr **addrs, const char *str,
			 int flags, int family);

/*!
 * \since 1.8
 *
 * \brief
 * Get the port number of a socket address.
 *
 * \warning Do not use this function unless you really know what you are doing.
 * And "I want the port number" is not knowing what you are doing.
 *
 * \retval 0 Address is null
 * \retval non-zero The port number of the ast_sockaddr
 */
#define ast_sockaddr_port(addr)	_ast_sockaddr_port(addr, __FILE__, __LINE__, __PRETTY_FUNCTION__)
uint16_t _ast_sockaddr_port(const struct ast_sockaddr *addr, const char *file, int line, const char *func);

/*!
 * \since 1.8
 *
 * \brief
 * Sets the port number of a socket address.
 *
 * \warning Do not use this function unless you really know what you are doing.
 * And "I want the port number" is not knowing what you are doing.
 *
 * \param addr Address on which to set the port
 * \param port The port you wish to set the address to use
 * \retval void
 */
#define ast_sockaddr_set_port(addr,port)	_ast_sockaddr_set_port(addr,port,__FILE__,__LINE__,__PRETTY_FUNCTION__)
void _ast_sockaddr_set_port(struct ast_sockaddr *addr, uint16_t port, const char *file, int line, const char *func);

/*!
 * \since 1.8
 *
 * \brief
 * Get an IPv4 address of an ast_sockaddr
 *
 * \warning You should rarely need this function. Only use if you know what
 * you're doing.
 * \return IPv4 address in network byte order
 */
uint32_t ast_sockaddr_ipv4(const struct ast_sockaddr *addr);

/*!
 * \since 1.8
 *
 * \brief
 * Determine if the address is an IPv4 address
 *
 * \warning You should rarely need this function. Only use if you know what
 * you're doing.
 * \retval 1 This is an IPv4 address
 * \retval 0 This is an IPv6 or IPv4-mapped IPv6 address
 */
int ast_sockaddr_is_ipv4(const struct ast_sockaddr *addr);

/*!
 * \since 1.8
 *
 * \brief
 * Determine if this is an IPv4-mapped IPv6 address
 *
 * \warning You should rarely need this function. Only use if you know what
 * you're doing.
 *
 * \retval 1 This is an IPv4-mapped IPv6 address.
 * \retval 0 This is not an IPv4-mapped IPv6 address.
 */
int ast_sockaddr_is_ipv4_mapped(const struct ast_sockaddr *addr);

/*!
 * \since 1.8
 *
 * \brief
 * Determine if this is an IPv6 address
 *
 * \warning You should rarely need this function. Only use if you know what
 * you're doing.
 *
 * \retval 1 This is an IPv6 or IPv4-mapped IPv6 address.
 * \retval 0 This is an IPv4 address.
 */
int ast_sockaddr_is_ipv6(const struct ast_sockaddr *addr);

/*!
 * \since 1.8
 *
 * \brief
 * Determine if the address type is unspecified, or "any" address.
 *
 * \details
 * For IPv4, this would be the address 0.0.0.0, and for IPv6,
 * this would be the address ::. The port number is ignored.
 *
 * \retval 1 This is an "any" address
 * \retval 0 This is not an "any" address
 */
int ast_sockaddr_is_any(const struct ast_sockaddr *addr);

/*!
 * \since 1.8
 *
 * \brief
 * Computes a hash value from the address. The port is ignored.
 *
 * \retval 0 Unknown address family
 * \retval other A 32-bit hash derived from the address
 */
int ast_sockaddr_hash(const struct ast_sockaddr *addr);

/*!
 * \since 1.8
 *
 * \brief
 * Wrapper around accept(2) that uses struct ast_sockaddr.
 *
 * \details
 * For parameter and return information, see the man page for
 * accept(2).
 */
int ast_accept(int sockfd, struct ast_sockaddr *addr);

/*!
 * \since 1.8
 *
 * \brief
 * Wrapper around bind(2) that uses struct ast_sockaddr.
 *
 * \details
 * For parameter and return information, see the man page for
 * bind(2).
 */
int ast_bind(int sockfd, const struct ast_sockaddr *addr);

/*!
 * \since 1.8
 *
 * \brief
 * Wrapper around connect(2) that uses struct ast_sockaddr.
 *
 * \details
 * For parameter and return information, see the man page for
 * connect(2).
 */
int ast_connect(int sockfd, const struct ast_sockaddr *addr);

/*!
 * \since 1.8
 *
 * \brief
 * Wrapper around getsockname(2) that uses struct ast_sockaddr.
 *
 * \details
 * For parameter and return information, see the man page for
 * getsockname(2).
 */
int ast_getsockname(int sockfd, struct ast_sockaddr *addr);

/*!
 * \since 1.8
 *
 * \brief
 * Wrapper around recvfrom(2) that uses struct ast_sockaddr.
 *
 * \details
 * For parameter and return information, see the man page for
 * recvfrom(2).
 */
ssize_t ast_recvfrom(int sockfd, void *buf, size_t len, int flags,
		     struct ast_sockaddr *src_addr);

/*!
 * \since 1.8
 *
 * \brief
 * Wrapper around sendto(2) that uses ast_sockaddr.
 *
 * \details
 * For parameter and
 * return information, see the man page for sendto(2)
 */
ssize_t ast_sendto(int sockfd, const void *buf, size_t len, int flags,
		   const struct ast_sockaddr *dest_addr);

/*!
 * \since 1.8
 *
 * \brief
 * Set type of service
 *
 * \details
 * Set ToS ("Type of Service for IPv4 and "Traffic Class for IPv6) and
 * CoS (Linux's SO_PRIORITY)
 *
 * \param sockfd File descriptor for socket on which to set the parameters
 * \param tos The type of service for the socket
 * \param cos The cost of service for the socket
 * \param desc A text description of the socket in question.
 * \retval 0 Success
 * \retval -1 Error, with errno set to an appropriate value
 */
int ast_set_qos(int sockfd, int tos, int cos, const char *desc);

/*!
 * These are backward compatibility functions that may be used by subsystems
 * that have not yet been converted to IPv6. They will be removed when all
 * subsystems are IPv6-ready.
 */
/*@{*/

/*!
 * \since 1.8
 *
 * \brief
 * Converts a struct ast_sockaddr to a struct sockaddr_in.
 *
 * \param addr The ast_sockaddr to convert
 * \param[out] sin The resulting sockaddr_in struct
 * \retval nonzero Success
 * \retval zero Failure
 */
#define ast_sockaddr_to_sin(addr,sin)	_ast_sockaddr_to_sin(addr,sin, __FILE__, __LINE__, __PRETTY_FUNCTION__)
int _ast_sockaddr_to_sin(const struct ast_sockaddr *addr,
			struct sockaddr_in *sin, const char *file, int line, const char *func);

/*!
 * \since 1.8
 *
 * \brief
 * Converts a struct sockaddr_in to a struct ast_sockaddr.
 *
 * \param sin The sockaddr_in to convert
 * \return an ast_sockaddr structure
 */
#define ast_sockaddr_from_sin(addr,sin)	_ast_sockaddr_from_sin(addr,sin, __FILE__, __LINE__, __PRETTY_FUNCTION__)
void _ast_sockaddr_from_sin(struct ast_sockaddr *addr, const struct sockaddr_in *sin,
		const char *file, int line, const char *func);

/*@}*/

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_NETSOCK2_H */
