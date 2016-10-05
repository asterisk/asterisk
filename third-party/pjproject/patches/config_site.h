/*
 * Asterisk config_site.h
 */

#include <sys/select.h>

/*
 * Since both pjproject and asterisk source files will include config_site.h,
 * we need to make sure that only pjproject source files include asterisk_malloc_debug.h.
 */
#if defined(MALLOC_DEBUG) && !defined(_ASTERISK_ASTMM_H)
#include "asterisk_malloc_debug.h"
#endif

/*
 * Defining PJMEDIA_HAS_SRTP to 0 does NOT disable Asterisk's ability to use srtp.
 * It only disables the pjmedia srtp transport which Asterisk doesn't use.
 * The reason for the disable is that while Asterisk works fine with older libsrtp
 * versions, newer versions of pjproject won't compile with them.
 */
#define PJMEDIA_HAS_SRTP 0

#define PJ_HAS_IPV6 1
#define NDEBUG 1
#define PJ_MAX_HOSTNAME (256)
#define PJSIP_MAX_URL_SIZE (512)
#ifdef PJ_HAS_LINUX_EPOLL
#define PJ_IOQUEUE_MAX_HANDLES	(5000)
#else
#define PJ_IOQUEUE_MAX_HANDLES	(FD_SETSIZE)
#endif
#define PJ_IOQUEUE_HAS_SAFE_UNREG 1
#define PJ_IOQUEUE_MAX_EVENTS_IN_SINGLE_POLL (16)

#define PJ_SCANNER_USE_BITWISE	0
#define PJ_OS_HAS_CHECK_STACK	0
#define PJ_LOG_MAX_LEVEL		3
#define PJ_ENABLE_EXTRA_CHECK	1
#define PJSIP_MAX_TSX_COUNT		((64*1024)-1)
#define PJSIP_MAX_DIALOG_COUNT	((64*1024)-1)
#define PJSIP_UDP_SO_SNDBUF_SIZE	(512*1024)
#define PJSIP_UDP_SO_RCVBUF_SIZE	(512*1024)
#define PJ_DEBUG			0
#define PJSIP_SAFE_MODULE		0
#define PJ_HAS_STRICMP_ALNUM		0
#define PJ_HASH_USE_OWN_TOLOWER		1
/*
  It is imperative that PJSIP_UNESCAPE_IN_PLACE remain 0 or undefined.
  Enabling it will result in SEGFAULTS when URIs containing escape sequences are encountered.
*/
#undef PJSIP_UNESCAPE_IN_PLACE
#define PJSIP_MAX_PKT_LEN			6000

#undef PJ_TODO
#define PJ_TODO(x)

/* Defaults too low for WebRTC */
#define PJ_ICE_MAX_CAND 32
#define PJ_ICE_MAX_CHECKS (PJ_ICE_MAX_CAND * 2)
