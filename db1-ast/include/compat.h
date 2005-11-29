/* Values for building 4.4 BSD db routines in the GNU C library.  */

#ifndef _compat_h_
#define _compat_h_

#include <fcntl.h>

/*
 * If you can't provide lock values in the open(2) call.  Note, this
 * allows races to happen.
 */
#ifndef O_EXLOCK			/* 4.4BSD extension. */
#define	O_EXLOCK	0
#endif

#ifndef O_SHLOCK			/* 4.4BSD extension. */
#define	O_SHLOCK	0
#endif

#include <errno.h>

#ifndef EFTYPE
#define	EFTYPE		EINVAL		/* POSIX 1003.1 format errno. */
#endif

#include <unistd.h>
#include <limits.h>

#ifndef _POSIX_VDISABLE			/* POSIX 1003.1 disabling char. */
#define	_POSIX_VDISABLE	0		/* Some systems used 0. */
#endif

#include <termios.h>

#ifndef	TCSASOFT			/* 4.4BSD extension. */
#define	TCSASOFT	0
#endif

#include <sys/param.h>

#ifndef	MAX				/* Usually found in <sys/param.h>. */
#define	MAX(_a,_b)	((_a)<(_b)?(_b):(_a))
#endif
#ifndef	MIN				/* Usually found in <sys/param.h>. */
#define	MIN(_a,_b)	((_a)<(_b)?(_a):(_b))
#endif


#endif /* compat.h */
