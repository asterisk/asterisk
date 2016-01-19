/*
 * Asterisk config_site.h
 */

#include <sys/select.h>

#define PJ_HAS_IPV6 1
#define NDEBUG 1
#define PJ_MAX_HOSTNAME (256)
#define PJSIP_MAX_URL_SIZE (512)

/* These are mostly from PJ_CONFIG_MAXIMUM_SPEED */
#define PJ_SCANNER_USE_BITWISE	0
#define PJ_OS_HAS_CHECK_STACK	0
#define PJ_LOG_MAX_LEVEL		3
#define PJ_ENABLE_EXTRA_CHECK	0

#define PJ_IOQUEUE_MAX_HANDLES	(FD_SETSIZE)

#define PJSIP_MAX_TSX_COUNT		((640*1024)-1)
#define PJSIP_MAX_DIALOG_COUNT	((640*1024)-1)
#define PJSIP_UDP_SO_SNDBUF_SIZE	(24*1024*1024)
#define PJSIP_UDP_SO_RCVBUF_SIZE	(24*1024*1024)
#define PJ_DEBUG			0
#define PJSIP_SAFE_MODULE		0
#define PJ_HAS_STRICMP_ALNUM		0
#define PJ_HASH_USE_OWN_TOLOWER		1
#define PJSIP_UNESCAPE_IN_PLACE		1

#undef PJ_TODO
#define PJ_TODO(x)


