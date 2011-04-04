# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.60)

AC_INIT([asterisk], [1.4], [https://issues.asterisk.org])

# cross-compile macros
AC_CANONICAL_BUILD
AC_CANONICAL_HOST

# check existence of the package
AC_CONFIG_SRCDIR([main/asterisk.c])

AC_CONFIG_AUX_DIR(`pwd`)

# specify output header file
AC_CONFIG_HEADER(include/asterisk/autoconfig.h)

AC_COPYRIGHT("Asterisk")
AC_REVISION($Revision$)

AC_USE_SYSTEM_EXTENSIONS	# note- does not work on FreeBSD

case "${host_os}" in
     freebsd*)
     ac_default_prefix=/usr/local
     CPPFLAGS=-I/usr/local/include
     LDFLAGS=-L/usr/local/lib
     ;;
     darwin*)
     ac_default_prefix=/usr/local
     AC_DEFINE([AST_POLL_COMPAT], 1, [Define to 1 if internal poll should be used.])
     AC_DEFINE([_DARWIN_UNLIMITED_SELECT], 1, [Define to 1 if running on Darwin.])
     ;;
     *)
     ac_default_prefix=/usr
     if test ${prefix} = '/usr' || test ${prefix} = 'NONE'; then
        if test ${sysconfdir} = '${prefix}/etc'; then
           sysconfdir=/etc
        fi
        if test ${mandir} = '${prefix}/man'; then
           mandir=/usr/share/man
        fi
     fi
     ;;
esac

if test ${prefix} = ${ac_default_prefix} || test ${prefix} = 'NONE'; then
     if test ${localstatedir} = '${prefix}/var'; then
        localstatedir=/var
     fi
fi

BUILD_PLATFORM=${build}
BUILD_CPU=${build_cpu}
BUILD_VENDOR=${build_vendor}
BUILD_OS=${build_os}

AC_SUBST(BUILD_PLATFORM)
AC_SUBST(BUILD_CPU)
AC_SUBST(BUILD_VENDOR)
AC_SUBST(BUILD_OS)

HOST_PLATFORM=${host}
HOST_CPU=${host_cpu}
HOST_VENDOR=${host_vendor}
HOST_OS=${host_os}

AC_SUBST(HOST_PLATFORM)
AC_SUBST(HOST_CPU)
AC_SUBST(HOST_VENDOR)
AC_SUBST(HOST_OS)

case "${host_os}" in
     freebsd*)
     OSARCH=FreeBSD
     ;;
     netbsd*)
     OSARCH=NetBSD
     ;;
     openbsd*)
     OSARCH=OpenBSD
     ;;
     solaris*)
     OSARCH=SunOS
     ;;
     linux-gnueabi)
     OSARCH=linux-gnu
     ;;
     *)
     OSARCH=${HOST_OS}
     ;;
esac

AC_SUBST(OSARCH)

#  check for uname
AC_PATH_TOOL([UNAME], [uname], No)
if test ! x"${UNAME}" = xNo; then
   PBX_OSREV=$(${UNAME} -r)
fi
AC_SUBST(PBX_OSREV)

AH_TOP(
#ifndef ASTERISK_AUTOCONFIG_H
#define ASTERISK_AUTOCONFIG_H

#include "asterisk/buildopts.h"

)

AH_BOTTOM(
#endif
)

# cross-compile checks
if test "${cross_compiling}" = "yes"; 
then
   AC_CHECK_TOOL(CC, gcc, :)
   AC_CHECK_TOOL(CXX, g++, :)
   AC_CHECK_TOOL(LD, ld, :)
   AC_CHECK_TOOL(RANLIB, ranlib, :)
fi

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_CPP
AC_PROG_CXXCPP
# This macro is just copied into our local acinclude.m4 from libtool.m4 so that
# the developers regenerating the configure script don't have to install libtool.
AST_PROG_LD 	# note - does not work on freebsd
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_RANLIB
AST_CHECK_GNU_MAKE
AC_PROG_EGREP

AC_CHECK_TOOLS([STRIP], [strip gstrip], :)
AC_CHECK_TOOLS([AR], [ar gar], :)

GNU_LD=0
if test "x$with_gnu_ld" = "xyes" ; then
   GNU_LD=1
fi
AC_SUBST(GNU_LD)

AC_PATH_PROG([AWK], [awk], :)
AC_PATH_PROG([BISON], [bison], :)
AC_PATH_PROG([CMP], [cmp], :)
AC_PATH_PROG([FLEX], [flex], :)
AC_PATH_PROG([GREP], [grep], :)
AC_PATH_PROG([FIND], [find], :)
AC_PATH_PROG([COMPRESS], [compress], :)
AC_PATH_PROG([BASENAME], [basename], :)
AC_PATH_PROG([DIRNAME], [dirname], :)
AC_PATH_PROG([SHELL], [sh], :)
AC_PATH_PROG([LN], [ln], :)
AC_PATH_PROG([DOT], [dot], :)
AC_PATH_PROG([WGET], [wget], :)
if test "${WGET}" != ":" ; then
  DOWNLOAD=${WGET}
else
  AC_PATH_PROG([FETCH], [fetch], [:])
  DOWNLOAD=${FETCH}
fi
AC_SUBST(DOWNLOAD)

AC_CACHE_CHECK([for bison that supports parse-param], [ac_cv_path_BISON2], [
	if test "x$BISON" != "x:" ; then
		# Create a temporary directory $tmp in $TMPDIR (default /tmp).
		# Use mktemp if possible; otherwise fall back on mkdir,
		# with $RANDOM to make collisions less likely.
		: ${TMPDIR=/tmp}
		{
		  tmp=`
		    (umask 077 && mktemp -d "$TMPDIR/fooXXXXXX") 2>/dev/null
		  ` &&
		  test -n "$tmp" && test -d "$tmp"
		} || {
		  tmp=$TMPDIR/foo$$-$RANDOM
		  (umask 077 && mkdir "$tmp")
		} || exit $?
		cat >$tmp/test.y <<__EOL__
%parse-param {struct parse_io *parseio}
%%
file : { \$\$ = parseio->pval = 1; }
	;
%%
__EOL__
		${BISON} -o ${tmp}/test.tab.c ${tmp}/test.y >/dev/null 2>&1
		if test -e "${tmp}/test.tab.c"; then
			ac_cv_path_BISON2=${BISON}
		fi
		rm -rf ${tmp}
	fi
	])
if test "x${ac_cv_path_BISON2}" = "x" ; then
	BISON=:
	PBX_BISON=0
else
	PBX_BISON=1
fi
AC_SUBST(PBX_BISON)
if test "x${FLEX}" = "x:" ; then
	PBX_FLEX=0
else
	PBX_FLEX=1
fi
AC_SUBST(PBX_FLEX)

AC_CHECK_TOOL([SOXMIX], [soxmix], [:])
if test "${SOXMIX}" != ":" ; then
	AC_DEFINE([HAVE_SOXMIX], 1, [Define to 1 if your system has soxmix application.])
fi

ACX_PTHREAD

AC_LANG(C)

AC_ARG_ENABLE(dev-mode,
	[  --enable-dev-mode    Turn on developer mode],
	[case "${enableval}" in
	      y|ye|yes) AST_DEVMODE=yes ;;
	      n|no)  AST_DEVMODE=no ;;
	      *) AC_MSG_ERROR(bad value ${enableval} for --enable-dev-mode)  ;;
	esac])
AC_SUBST(AST_DEVMODE)

# package option names should be in alphabetical order
# by the --with option name, to make things easier for the users :-)

AST_EXT_LIB_SETUP([ALSA], [Advanced Linux Sound Architecture], [asound])
AST_EXT_LIB_SETUP([BFD], [Debug symbol decoding], [bfd])
AST_EXT_LIB_SETUP([CAP], [POSIX 1.e capabilities], [cap])
AST_EXT_LIB_SETUP([CURSES], [curses], [curses])
AST_EXT_LIB_SETUP([DAHDI], [DAHDI], [dahdi])
AST_EXT_LIB_SETUP([GNUTLS], [GNU TLS support (used for iksemel only)], [gnutls])
AST_EXT_LIB_SETUP([GSM], [GSM], [gsm], [, or 'internal'])
AST_EXT_LIB_SETUP([OPENH323], [OpenH323], [h323])
AST_EXT_LIB_SETUP([IKSEMEL], [Iksemel Jabber Library], [iksemel])
AST_EXT_LIB_SETUP([IMAP_TK], [UW IMAP Toolkit], [imap])
AST_EXT_LIB_SETUP([ISDNNET], [ISDN4Linux Library], [isdnnet])
AST_EXT_LIB_SETUP([KDE], [KDE], [kde])
AST_LIBCURL_CHECK_CONFIG([], [7.10.1])
AST_EXT_LIB_SETUP([LTDL], [libtool], [ltdl])
AST_EXT_LIB_SETUP([MISDN], [mISDN User Library], [misdn])
AST_EXT_LIB_SETUP([NBS], [Network Broadcast Sound], [nbs])
AST_EXT_LIB_SETUP([NCURSES], [ncurses], [ncurses])
AST_EXT_LIB_SETUP([NETSNMP], [Net-SNMP], [netsnmp])
AST_EXT_LIB_SETUP([NEWT], [newt], [newt])
AST_EXT_LIB_SETUP([UNIXODBC], [unixODBC], [odbc])
AST_EXT_LIB_SETUP([OGG], [OGG], [ogg])
AST_EXT_LIB_SETUP([OPENAIS], [OpenAIS], [openais])
AST_EXT_LIB_SETUP([OPENR2], [MFR2], [openr2])
AST_EXT_LIB_SETUP([OSPTK], [OSP Toolkit], [osptk])
AST_EXT_LIB_SETUP([OSS], [Open Sound System], [oss])
AST_EXT_LIB_SETUP([POPT], [popt], [popt])
AST_EXT_LIB_SETUP([PGSQL], [PostgreSQL], [postgres])
AST_EXT_LIB_SETUP([PRI], [ISDN PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_CALL_HOLD], [NOTIFY with call ptr], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_VERSION], [ISDN PRI get_version], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_INBANDDISCONNECT], [ISDN PRI set_inbanddisconnect], [PRI], [pri])
AST_EXT_LIB_SETUP([PWLIB], [PWlib], [pwlib])
AST_EXT_LIB_SETUP([RADIUS], [Radius Client], [radius])
AST_EXT_LIB_SETUP([SPEEX], [Speex], [speex])
AST_EXT_LIB_SETUP([SPEEXDSP], [Speexdsp], [speexdsp])
AST_EXT_LIB_SETUP_DEPENDENT([SPEEX_PREPROCESS], [speex_preprocess_ctl], [], [speex])
AST_EXT_LIB_SETUP([SQLITE], [SQLite], [sqlite])
AST_EXT_LIB_SETUP([OPENSSL], [OpenSSL], [ssl])
AST_EXT_LIB_SETUP([SUPPSERV], [mISDN Supplemental Services], [suppserv])
AST_EXT_LIB_SETUP([FREETDS], [FreeTDS], [tds])
AST_EXT_LIB_SETUP([TERMCAP], [Termcap], [termcap])
AST_EXT_LIB_SETUP([TINFO], [Term Info], [tinfo])
AST_EXT_LIB_SETUP([TONEZONE], [tonezone], [tonezone])
AST_EXT_LIB_SETUP([USB], [usb], [usb])
AST_EXT_LIB_SETUP([VORBIS], [Vorbis], [vorbis])
AST_EXT_LIB_SETUP([VPB], [Voicetronix API], [vpb])
AST_EXT_LIB_SETUP([ZLIB], [zlib], [z])
AST_EXT_LIB_SETUP([ZAPTEL], [Zaptel], [zaptel])

# check for basic system features and functionality before
# checking for package libraries

AC_FUNC_ALLOCA
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h libintl.h limits.h locale.h malloc.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/file.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h syslog.h termios.h unistd.h utime.h arpa/nameser.h sys/io.h])

AC_CHECK_HEADER([sys/poll.h],
	 [],
     AC_DEFINE([AST_POLL_COMPAT], 1, [Define to 1 if internal poll should be used.]))

AC_SYS_LARGEFILE

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_HEADER_TIME
AC_STRUCT_TM
AC_C_VOLATILE
AC_CHECK_TYPES([ptrdiff_t])

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_ERROR_AT_LINE
AST_FUNC_FORK
AC_FUNC_FSEEKO
AC_PROG_GCC_TRADITIONAL
# XXX: these are commented out until we determine whether it matters if our malloc()
# acts exactly like glibc's or not
# AC_FUNC_MALLOC
# AC_FUNC_REALLOC
AC_FUNC_MEMCMP
AC_FUNC_MMAP
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_SETVBUF_REVERSED
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_STRCOLL
AC_FUNC_STRFTIME
AC_FUNC_STRNLEN
AC_FUNC_STRTOD
AC_FUNC_UTIME_NULL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([asprintf atexit bzero dup2 endpwent floor ftruncate getcwd gethostbyname gethostname getloadavg gettimeofday inet_ntoa isascii localtime_r memchr memmove memset mkdir munmap pow ppoll putenv re_comp regcomp rint select setenv socket sqrt strcasecmp strcasestr strchr strcspn strdup strerror strlcat strlcpy strncasecmp strndup strnlen strrchr strsep strspn strstr strtol strtoq unsetenv utime vasprintf ioperm])

AC_MSG_CHECKING(for timersub in time.h)
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([#include <sys/time.h>],
			[struct timeval *zombies; timersub(zombies, zombies, zombies);]),
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_TIMERSUB], 1, [Define to 1 if your system has timersub in time.h]),
	AC_MSG_RESULT(no)
)

# some systems already have gethostbyname_r so we don't need to build ours in main/utils.c
AC_SEARCH_LIBS(gethostbyname_r, [socket nsl])

AC_MSG_CHECKING(for gethostbyname_r with 6 arguments)
AC_LINK_IFELSE(
        AC_LANG_PROGRAM([#include <stdlib.h>
                         #include <netdb.h>],
                        [struct hostent *he = gethostbyname_r((const char *)NULL, (struct hostent *)NULL, (char *)NULL, (int)0, (struct hostent **)NULL, (int *)NULL);]),
        AC_MSG_RESULT(yes)
        AC_DEFINE([HAVE_GETHOSTBYNAME_R_6], 1, [Define to 1 if your system has gethostbyname_r with 6 arguments.]),
        AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for gethostbyname_r with 5 arguments)
AC_LINK_IFELSE(
        AC_LANG_PROGRAM([#include <stdlib.h>
	                 #include <netdb.h>],
                        [struct hostent *he = gethostbyname_r((const char *)NULL, (struct hostent *)NULL, (char *)NULL, (int)0, (int *)NULL);]),
        AC_MSG_RESULT(yes)
        AC_DEFINE([HAVE_GETHOSTBYNAME_R_5], 1, [Define to 1 if your system has gethostbyname_r with 5 arguments.]),
        AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for PTHREAD_RWLOCK_INITIALIZER)
AC_LINK_IFELSE(
        AC_LANG_PROGRAM([#include <pthread.h>],
                        [int foo = PTHREAD_RWLOCK_INITIALIZER;]),
        AC_MSG_RESULT(yes)
        AC_DEFINE([HAVE_PTHREAD_RWLOCK_INITIALIZER], 1, [Define to 1 if your system has PTHREAD_RWLOCK_INITIALIZER.]),
        AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for PTHREAD_RWLOCK_PREFER_WRITER_NP)
AC_LINK_IFELSE(
        AC_LANG_PROGRAM([#include <pthread.h>],
                        [int foo = PTHREAD_RWLOCK_PREFER_WRITER_NP;]),
        AC_MSG_RESULT(yes)
        AC_DEFINE([HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NP], 1, [Define to 1 if your system has PTHREAD_RWLOCK_PREFER_WRITER_NP.]),
        AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for pthread_rwlock_timedwrlock() in pthread.h)
saved_LDFLAGS="${LDFLAGS}"
LDFLAGS="${LDFLAGS} -lpthread"
AC_LINK_IFELSE(
  [AC_LANG_PROGRAM(
    [#include <pthread.h>
     #include <time.h>],
    [pthread_rwlock_t foo; struct timespec bar; pthread_rwlock_timedwrlock(&foo, &bar)])
  ],[
    AC_MSG_RESULT(yes)
    ac_cv_pthread_rwlock_timedwrlock="yes"
  ],[
    AC_MSG_RESULT(no)
    ac_cv_pthread_rwlock_timedwrlock="no"
  ]
)
LDFLAGS="${saved_LDFLAGS}"
if test "${ac_cv_pthread_rwlock_timedwrlock}" = "yes"; then
  AC_DEFINE([HAVE_PTHREAD_RWLOCK_TIMEDWRLOCK], 1, [Define if your system has pthread_rwlock_timedwrlock()])
fi

AC_MSG_CHECKING(if PTHREAD_ONCE_INIT needs braces)
saved_CFLAGS="${CFLAGS}"
CFLAGS="${CFLAGS} -Werror -Wmissing-braces"
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM(
    [#include <pthread.h>
     void empty(){}],
	[pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, empty);])
  ],[
    AC_MSG_RESULT(no)
    ac_cv_pthread_once_needsbraces="no"
  ],[
    AC_MSG_RESULT(yes)
    ac_cv_pthread_once_needsbraces="yes"
  ]
)
CFLAGS="${saved_CFLAGS}"
if test "${ac_cv_pthread_once_needsbraces}" = "yes"; then
  AC_DEFINE([PTHREAD_ONCE_INIT_NEEDS_BRACES], 1, [Define if your system needs braces around PTHREAD_ONCE_INIT])
fi

# Can we compare a mutex to its initial value?
# Generally yes on OpenBSD/FreeBSD and no on Mac OS X.
AC_MSG_CHECKING(whether we can compare a mutex to its initial value)
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([#include <pthread.h>], [pthread_mutex_t lock;
	if ((lock) != ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		return 0;
	}
	return 0]),
	AC_MSG_RESULT(yes)
	AC_DEFINE([CAN_COMPARE_MUTEX_TO_INIT_VALUE], 1, [Define to 1 if the implementation of mutexes supports comparison of a mutex to its initializer.]),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for compiler atomic operations)
AC_LINK_IFELSE(
AC_LANG_PROGRAM([], [int foo1; int foo2 = __sync_fetch_and_add(&foo1, 1);]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_GCC_ATOMICS], 1, [Define to 1 if your GCC C compiler provides atomic operations.]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(if we can increase the maximum select-able file descriptor)
AC_RUN_IFELSE(
AC_LANG_PROGRAM([
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
], [[
	struct rlimit rlim = { FD_SETSIZE * 2, FD_SETSIZE * 2 };
	int fd0, fd1;
	struct timeval tv = { 0, };
	struct ast_fdset { long fds_bits[[1024]]; } fds = { { 0, } };
	if (setrlimit(RLIMIT_NOFILE, &rlim)) { exit(1); }
	if ((fd0 = open("/dev/null", O_RDONLY)) < 0) { exit(1); }
	if (dup2(fd0, (fd1 = FD_SETSIZE + 1)) < 0) { exit(1); }
	FD_SET(fd0, (fd_set *) &fds);
	FD_SET(fd1, (fd_set *) &fds);
	if (select(FD_SETSIZE + 2, (fd_set *) &fds, NULL, NULL, &tv) < 0) { exit(1); }
	exit(0)]]),
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_VARIABLE_FDSET], 1, [Define to 1 if your system can support larger than default select bitmasks.]),
	AC_MSG_RESULT(no),
	AC_MSG_RESULT(cross-compile)
)

if test "${ac_cv_have_variable_fdset}x" = "0x"; then
	AC_RUN_IFELSE(
		AC_LANG_PROGRAM([
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
], [if (getuid() != 0) { exit(1); }]),
		AC_DEFINE([CONFIGURE_RAN_AS_ROOT], 1, [Some configure tests will unexpectedly fail if configure is run by a non-root user.  These may be able to be tested at runtime.]))
fi

AST_GCC_ATTRIBUTE(pure)
AST_GCC_ATTRIBUTE(malloc)
AST_GCC_ATTRIBUTE(const)
AST_GCC_ATTRIBUTE(unused)
AST_GCC_ATTRIBUTE(always_inline)
AST_GCC_ATTRIBUTE(deprecated)
AST_GCC_ATTRIBUTE(warn_unused_result)

AC_MSG_CHECKING(for -ffunction-sections support)
saved_CFLAGS="${CFLAGS}"
CFLAGS="${CFLAGS} -ffunction-sections"
AC_COMPILE_IFELSE(
	AC_LANG_PROGRAM([], [int x = 1;]),
	AC_MSG_RESULT(yes)
	[saved_LDFLAGS="${LDFLAGS}"]
	[LDFLAGS="${LDFLAGS} -Wl,--gc-sections"]
	AC_MSG_CHECKING(for --gc-sections support)
	AC_LINK_IFELSE(
		AC_LANG_PROGRAM([], [int x = 1;]),
		AC_MSG_RESULT(yes)
		[GC_CFLAGS="-ffunction-sections"]
		[[GC_LDFLAGS="-Wl,--gc-sections"]],
		AC_MSG_RESULT(no)
	)
	[LDFLAGS="${saved_LDFLAGS}"],
	AC_MSG_RESULT(no)
)
CFLAGS="${saved_CFLAGS}"
AC_SUBST(GC_CFLAGS)
AC_SUBST(GC_LDFLAGS)

AC_MSG_CHECKING(for -Wdeclaration-after-statement support)
if $(${CC} -Wdeclaration-after-statement -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
   AC_MSG_RESULT(yes)
   AST_DECLARATION_AFTER_STATEMENT=-Wdeclaration-after-statement
else
   AC_MSG_RESULT(no)
   AST_DECLARATION_AFTER_STATEMENT=
fi
AC_SUBST(AST_DECLARATION_AFTER_STATEMENT)

AC_MSG_CHECKING(for _FORTIFY_SOURCE support)
if $(${CC} -D_FORTIFY_SOURCE=2 -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
   AC_MSG_RESULT(yes)
   AST_FORTIFY_SOURCE=-D_FORTIFY_SOURCE=2
else
   AC_MSG_RESULT(no)
   AST_FORTIFY_SOURCE=
fi
AC_SUBST(AST_FORTIFY_SOURCE)

AC_MSG_CHECKING(for -fno-strict-overflow)
if $(${CC} -O2 -fno-strict-overflow -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
   AC_MSG_RESULT(yes)
   AST_NO_STRICT_OVERFLOW=-fno-strict-overflow
else
   AC_MSG_RESULT(no)
   AST_NO_STRICT_OVERFLOW=
fi
AC_SUBST(AST_NO_STRICT_OVERFLOW)

AC_SEARCH_LIBS(res_9_ninit, resolv)
AC_MSG_CHECKING(for res_ninit)
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([
			#ifdef HAVE_SYS_SOCKET_H
			#include <sys/socket.h>
			#endif
			#ifdef HAVE_NETINET_IN_H
			#include <netinet/in.h>
			#endif
			#ifdef HAVE_ARPA_NAMESER_H
			#include <arpa/nameser.h>
			#endif
			#include <resolv.h>],
			[int foo = res_ninit(NULL);]),
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_RES_NINIT], 1, [Define to 1 if your system has the re-entrant resolver functions.])
	AC_SEARCH_LIBS(res_9_ndestroy, resolv)
	AC_MSG_CHECKING(for res_ndestroy)
	AC_LINK_IFELSE(
		AC_LANG_PROGRAM([
				#ifdef HAVE_SYS_SOCKET_H
				#include <sys/socket.h>
				#endif
				#ifdef HAVE_NETINET_IN_H
				#include <netinet/in.h>
				#endif
				#ifdef HAVE_ARPA_NAMESER_H
				#include <arpa/nameser.h>
				#endif
				#include <resolv.h>],
				[res_ndestroy(NULL);]),
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_RES_NDESTROY], 1, [Define to 1 if your system has the ndestroy resolver function.]),
		AC_MSG_RESULT(no)
	),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for GLOB_NOMAGIC)
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([#include <glob.h>],
			[int foo = GLOB_NOMAGIC;]),
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_GLOB_NOMAGIC], 1, [Define to 1 if your glob function supports GLOB_NOMAGIC option.]),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for GLOB_BRACE)
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([#include <glob.h>],
			[int foo = GLOB_BRACE;]),
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_GLOB_BRACE], 1, [Define to 1 if your glob function supports GLOB_BRACE option.]),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for IP_MTU_DISCOVER)
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([#include <netinet/in.h>],
			[int foo = IP_MTU_DISCOVER;]),
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_IP_MTU_DISCOVER], 1, [Define to 1 if your system has PMTU discovery on UDP sockets.]),
	AC_MSG_RESULT(no)
)

AC_CHECK_HEADER([libkern/OSAtomic.h],
                [AC_DEFINE_UNQUOTED([HAVE_OSX_ATOMICS], 1, [Define to 1 if OSX atomic operations are supported.])])

AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(long long)
AC_COMPUTE_INT([ac_cv_sizeof_fd_set_fds_bits], [sizeof(foo.fds_bits[[0]])], [$ac_includes_default
fd_set foo;])
# This doesn't actually work; what it does is to use the variable set in the
# previous test as a cached value to set the right output variables.
AC_CHECK_SIZEOF(fd_set.fds_bits)

# Set a type compatible with the previous.  We cannot just use a generic type
# for these bits, because on big-endian systems, the bits won't match up
# correctly if the size is wrong.
if test $ac_cv_sizeof_int = $ac_cv_sizeof_fd_set_fds_bits; then
  AC_DEFINE([TYPEOF_FD_SET_FDS_BITS], [int], [Define to a type of the same size as fd_set.fds_bits[[0]]])
else if test $ac_cv_sizeof_long = $ac_cv_sizeof_fd_set_fds_bits; then
  AC_DEFINE([TYPEOF_FD_SET_FDS_BITS], [long], [Define to a type of the same size as fd_set.fds_bits[[0]]])
else if test $ac_cv_sizeof_long_long = $ac_cv_sizeof_fd_set_fds_bits; then
  AC_DEFINE([TYPEOF_FD_SET_FDS_BITS], [long long], [Define to a type of the same size as fd_set.fds_bits[[0]]])
fi ; fi ; fi

AC_MSG_CHECKING(for dladdr in dlfcn.h)
PBX_DLADDR=0
old_LIBS=${LIBS}
LIBS="${LIBS} -ldl"
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([#define _GNU_SOURCE 1
#include <dlfcn.h>],
		[dladdr((void *)0, (void *)0)]
	),
	AC_MSG_RESULT(yes)
	PBX_DLADDR=1
	AC_SUBST([PBX_DLADDR])
	AC_DEFINE([HAVE_DLADDR], 1, [Define to 1 if your system has the dladdr() GNU extension]),
	AC_MSG_RESULT(no)
)
LIBS=${old_LIBS}


# do the package library checks now

AST_EXT_LIB_CHECK([ALSA], [asound], [snd_spcm_init], [alsa/asoundlib.h], [-lm -ldl])

AST_EXT_LIB_CHECK([BFD], [bfd], [bfd_openr], [bfd.h])

AST_EXT_LIB_CHECK([CURSES], [curses], [initscr], [curses.h])

if test "x${OSARCH}" = "xlinux-gnu" ; then
  AST_EXT_LIB_CHECK([CAP], [cap], [cap_from_text], [sys/capability.h])
fi

PBX_DAHDI_TRANSCODE=0
if test "${USE_DAHDI}" != "no" ; then
	AST_C_DEFINE_CHECK([DAHDI], [DAHDI_RESET_COUNTERS], [dahdi/user.h], [230])
	AST_C_DEFINE_CHECK([DAHDI], [DAHDI_DEFAULT_MTU_MRU], [dahdi/user.h], [220])
	AST_C_DEFINE_CHECK([DAHDI], [DAHDI_CODE], [dahdi/user.h], [200])
	if test "x${PBX_DAHDI}" = "x1" ; then
		PBX_DAHDI_TRANSCODE=1
		AC_SUBST(PBX_DAHDI_TRANSCODE)
	fi
fi

AST_C_COMPILE_CHECK([GETIFADDRS], [struct ifaddrs *p; getifaddrs(&p)], [ifaddrs.h], , [getifaddrs() support])

GSM_INTERNAL="yes"
AC_SUBST(GSM_INTERNAL)
GSM_SYSTEM="yes"
if test "${USE_GSM}" != "no"; then
   if test "${GSM_DIR}" = "internal"; then
      GSM_SYSTEM="no"
   elif test "${GSM_DIR}" != ""; then
      GSM_INTERNAL="no"
   fi
   if test "${GSM_SYSTEM}" = "yes"; then
      gsmlibdir=""
      if test "x${GSM_DIR}" != "x"; then
         if test -d ${GSM_DIR}/lib; then
            gsmlibdir="-L${GSM_DIR}/lib"
         else
            gsmlibdir="-L${GSM_DIR}"
         fi
      fi
      AC_CHECK_LIB([gsm], [gsm_create], AC_DEFINE_UNQUOTED([HAVE_GSM], 1,
      [Define to indicate the GSM library]), [], ${gsmlibdir})
      if test "${ac_cv_lib_gsm_gsm_create}" = "yes"; then
         if test "x${GSM_DIR}" != "x" ; then
            AC_CHECK_HEADER([${GSM_DIR}/include/gsm.h], [GSM_HEADER_FOUND=1], [GSM_HEADER_FOUND=0])
            AC_CHECK_HEADER([${GSM_DIR}/include/gsm/gsm.h], [GSM_GSM_HEADER_FOUND=1], [GSM_GSM_HEADER_FOUND=0])
         else
            AC_CHECK_HEADER([gsm.h], [GSM_HEADER_FOUND=1], [GSM_HEADER_FOUND=0])
            AC_CHECK_HEADER([gsm/gsm.h], [GSM_GSM_HEADER_FOUND=1], [GSM_GSM_HEADER_FOUND=0])
         fi
         if test "${GSM_HEADER_FOUND}" = "0" ; then
            if test "{GSM_GSM_HEADER_FOUND}" = "0" ; then
               if test "x${GSM_MANDATORY}" = "xyes" ; then
                  AC_MSG_NOTICE([***])
                  AC_MSG_NOTICE([*** It appears that you do not have the gsm development package installed.])
                  AC_MSG_NOTICE([*** Please install it to include ${GSM_DESCRIP} support, or re-run configure])
                  AC_MSG_NOTICE([*** without explicitly specifying --with-${GSM_OPTION}])
                  exit 1
               fi
            fi
         fi
         GSM_OK=0
         if test "${GSM_HEADER_FOUND}" = "1" ; then
            AC_DEFINE_UNQUOTED([HAVE_GSM_HEADER], 1, [Define to indicate that gsm.h has no prefix for its location])
            GSM_OK=1
         else
            if test "${GSM_GSM_HEADER_FOUND}" = "1" ; then
               AC_DEFINE_UNQUOTED([HAVE_GSM_GSM_HEADER], 1, [Define to indicate that gsm.h is in gsm/gsm.h])
               GSM_OK=1
            fi
         fi
         if test "${GSM_OK}" = "1" ; then
            GSM_LIB="-lgsm"
            if test "x${GSM_DIR}" != "x"; then
               GSM_LIB="${gsmlibdir} ${GSM_LIB}"
               GSM_INCLUDE="-I${GSM_DIR}/include"
            fi
            PBX_GSM=1
            GSM_INTERNAL="no"
         fi
      fi
   fi
   if test "${GSM_INTERNAL}" = "yes"; then
      PBX_GSM=1
      AC_DEFINE_UNQUOTED([HAVE_GSM_HEADER], 1, [Define to indicate that gsm.h has no prefix for its location])
   fi
fi

AST_EXT_LIB_CHECK([IKSEMEL], [iksemel], [iks_start_sasl], [iksemel.h])

if test "${PBX_IKSEMEL}" = 1; then
   AST_EXT_LIB_CHECK([GNUTLS], [gnutls], [gnutls_bye], [gnutls/gnutls.h], [-lz -lgcrypt -lgpg-error])
fi

if test "${USE_IMAP_TK}" != "no"; then
	saved_cppflags="${CPPFLAGS}"
	saved_libs="${LIBS}"
	switch_to_system_on_failure="no"
	if test "${IMAP_TK_DIR}" = ""; then
		IMAP_TK_DIR=`pwd`"/../imap-2004g"
		switch_to_system_on_failure="yes"
	fi
	if test "${IMAP_TK_DIR}" != "system"; then
		AC_MSG_CHECKING(for UW IMAP Toolkit c-client library)
		if test -f "${IMAP_TK_DIR}/c-client/LDFLAGS"; then
      		imap_ldflags=`cat ${IMAP_TK_DIR}/c-client/LDFLAGS`
		fi
		imap_libs="${IMAP_TK_DIR}/c-client/c-client.a"
	  	imap_include="-I${IMAP_TK_DIR}/c-client"
      	CPPFLAGS="${CPPFLAGS} ${imap_include}"
	  	LIBS="${LIBS} ${imap_libs} "`echo ${imap_ldflags}`
	  	AC_LINK_IFELSE(
	    	AC_LANG_PROGRAM(
				[#include "c-client.h"
				void mm_searched (MAILSTREAM *stream,unsigned long number)
				{
				}
				void mm_exists (MAILSTREAM *stream,unsigned long number)
				{
				}
				void mm_expunged (MAILSTREAM *stream,unsigned long number)
				{
				}
				void mm_flags (MAILSTREAM *stream,unsigned long number)
				{
				}
				void mm_notify (MAILSTREAM *stream,char *string,long errflg)
				{
				}
				void mm_list (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
				{
				}
				void mm_lsub (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
				{
				}
				void mm_status (MAILSTREAM *stream,char *mailbox,MAILSTATUS *status)
				{
				}
				void mm_log (char *string,long errflg)
				{
				}
				void mm_dlog (char *string)
				{
				}
				void mm_login (NETMBX *mb,char *user,char *pwd,long trial)
				{
				}
				void mm_critical (MAILSTREAM *stream)
				{
				}
				void mm_nocritical (MAILSTREAM *stream)
				{
				}
				long mm_diskerror (MAILSTREAM *stream,long errcode,long serious)
				{
				}
				void mm_fatal (char *string)
				{
				}],
				[
				MAILSTREAM *foo = mail_open(NULL, "", 0);
				]
			),
			[ac_cv_imap_tk="yes"],
			[ac_cv_imap_tk="no"]
	   	)
		if test "${ac_cv_imap_tk}" = "yes"; then
			AC_LINK_IFELSE(
				AC_LANG_PROGRAM(
					[#include "c-client.h"
					void mm_searched (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_exists (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_expunged (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_flags (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_notify (MAILSTREAM *stream,char *string,long errflg)
					{
					}
					void mm_list (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
					{
					}
					void mm_lsub (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
					{
					}
					void mm_status (MAILSTREAM *stream,char *mailbox,MAILSTATUS *status)
					{
					}
					void mm_log (char *string,long errflg)
					{
					}
					void mm_dlog (char *string)
					{
					}
					void mm_login (NETMBX *mb,char *user,char *pwd,long trial)
					{
					}
					void mm_critical (MAILSTREAM *stream)
					{
					}
					void mm_nocritical (MAILSTREAM *stream)
					{
					}
					long mm_diskerror (MAILSTREAM *stream,long errcode,long serious)
					{
					}
					void mm_fatal (char *string)
					{
					}],
					[
					long check = mail_expunge_full(NULL, "", 0);
					]
				),
				[ac_cv_imap_tk2006="yes"],
				[ac_cv_imap_tk2006="no"]
			)
		fi
		CPPFLAGS="${saved_cppflags}"
		LIBS="${saved_libs}"
		if test "${ac_cv_imap_tk}" = "no"; then
			AC_MSG_RESULT(no)
			if test "${switch_to_system_on_failure}" = "yes"; then 
				IMAP_TK_DIR="system"
			else #This means they specified a directory. Search for a package installation there too
				AC_MSG_CHECKING([for system c-client library...])
				CPPFLAGS="${saved_cppflags}"
				LIBS="${saved_libs}"
				imap_include="-I${IMAP_TK_DIR}/include"
				imap_ldflags="-L${IMAP_TK_DIR}/lib"
				imap_libs="-lc-client"
				CPPFLAGS="${CPPFLAGS} ${imap_include}"
				LIBS="${LIBS} ${imap_libs} ${imap_ldflags}"
				AC_LINK_IFELSE(
	    			AC_LANG_PROGRAM(
						[#include "c-client.h"
						void mm_searched (MAILSTREAM *stream,unsigned long number)
						{
						}
						void mm_exists (MAILSTREAM *stream,unsigned long number)
						{
						}
						void mm_expunged (MAILSTREAM *stream,unsigned long number)
						{
						}
						void mm_flags (MAILSTREAM *stream,unsigned long number)
						{
						}
						void mm_notify (MAILSTREAM *stream,char *string,long errflg)
						{
						}
						void mm_list (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
						{
						}
						void mm_lsub (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
						{
						}
						void mm_status (MAILSTREAM *stream,char *mailbox,MAILSTATUS *status)
						{
						}
						void mm_log (char *string,long errflg)
						{
						}
						void mm_dlog (char *string)
						{
						}
						void mm_login (NETMBX *mb,char *user,char *pwd,long trial)
						{
						}
						void mm_critical (MAILSTREAM *stream)
						{
						}
						void mm_nocritical (MAILSTREAM *stream)
						{
						}
						long mm_diskerror (MAILSTREAM *stream,long errcode,long serious)
						{
						}
						void mm_fatal (char *string)
						{
						}],
						[
						MAILSTREAM *foo = mail_open(NULL, "", 0);
						]
					),
					[ac_cv_imap_tk="yes"],
					[ac_cv_imap_tk="no"]
	   			)
				if test "${ac_cv_imap_tk}" = "yes"; then
					AC_LINK_IFELSE(
						AC_LANG_PROGRAM(
							[#include "c-client.h"
							void mm_searched (MAILSTREAM *stream,unsigned long number)
							{
							}
							void mm_exists (MAILSTREAM *stream,unsigned long number)
							{
							}
							void mm_expunged (MAILSTREAM *stream,unsigned long number)
							{
							}
							void mm_flags (MAILSTREAM *stream,unsigned long number)
							{
							}
							void mm_notify (MAILSTREAM *stream,char *string,long errflg)
							{
							}
							void mm_list (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
							{
							}
							void mm_lsub (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
							{
							}
							void mm_status (MAILSTREAM *stream,char *mailbox,MAILSTATUS *status)
							{
							}
							void mm_log (char *string,long errflg)
							{
							}
							void mm_dlog (char *string)
							{
							}
							void mm_login (NETMBX *mb,char *user,char *pwd,long trial)
							{
							}
							void mm_critical (MAILSTREAM *stream)
							{
							}
							void mm_nocritical (MAILSTREAM *stream)
							{
							}
							long mm_diskerror (MAILSTREAM *stream,long errcode,long serious)
							{
							}
							void mm_fatal (char *string)
							{
							}],
							[
							long check = mail_expunge_full(NULL, "", 0);
							]
						),
						[ac_cv_imap_tk2006="yes"],
						[ac_cv_imap_tk2006="no"]
					)
				fi
			fi
		fi
	fi 
	if test "${IMAP_TK_DIR}" = "system"; then
		#We will enter here if user specified "system" or if any of above checks failed
		AC_MSG_CHECKING([for system c-client library...])
		CPPFLAGS="${saved_cppflags}"
		LIBS="${saved_libs}"
		imap_ldflags=""
		imap_libs="-lc-client"
		imap_include="-DUSE_SYSTEM_IMAP" #Try the imap directory first
		CPPFLAGS="${CPPFLAGS} ${imap_include}"
		LIBS="${LIBS} ${imap_libs} "`echo ${imap_ldflags}`
		AC_LINK_IFELSE(
	    	AC_LANG_PROGRAM(
				[#include <stdio.h>
				#include <imap/c-client.h>
				void mm_searched (MAILSTREAM *stream,unsigned long number)
				{
				}
				void mm_exists (MAILSTREAM *stream,unsigned long number)
				{
				}
				void mm_expunged (MAILSTREAM *stream,unsigned long number)
				{
				}
				void mm_flags (MAILSTREAM *stream,unsigned long number)
				{
				}
				void mm_notify (MAILSTREAM *stream,char *string,long errflg)
				{
				}
				void mm_list (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
				{
				}
				void mm_lsub (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
				{
				}
				void mm_status (MAILSTREAM *stream,char *mailbox,MAILSTATUS *status)
				{
				}
				void mm_log (char *string,long errflg)
				{
				}
				void mm_dlog (char *string)
				{
				}
				void mm_login (NETMBX *mb,char *user,char *pwd,long trial)
				{
				}
				void mm_critical (MAILSTREAM *stream)
				{
				}
				void mm_nocritical (MAILSTREAM *stream)
				{
				}
				long mm_diskerror (MAILSTREAM *stream,long errcode,long serious)
				{
				}
				void mm_fatal (char *string)
				{
				}],
				[
				MAILSTREAM *foo = mail_open(NULL, "", 0);
				]
			),
			[ac_cv_imap_tk="yes"],
			[ac_cv_imap_tk="no"]
	   	)
		if test "${ac_cv_imap_tk}" = "yes"; then
			AC_LINK_IFELSE(
				AC_LANG_PROGRAM(
					[#include <stdio.h>
					#include <imap/c-client.h>
					void mm_searched (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_exists (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_expunged (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_flags (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_notify (MAILSTREAM *stream,char *string,long errflg)
					{
					}
					void mm_list (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
					{
					}
					void mm_lsub (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
					{
					}
					void mm_status (MAILSTREAM *stream,char *mailbox,MAILSTATUS *status)
					{
					}
					void mm_log (char *string,long errflg)
					{
					}
					void mm_dlog (char *string)
					{
					}
					void mm_login (NETMBX *mb,char *user,char *pwd,long trial)
					{
					}
					void mm_critical (MAILSTREAM *stream)
					{
					}
					void mm_nocritical (MAILSTREAM *stream)
					{
					}
					long mm_diskerror (MAILSTREAM *stream,long errcode,long serious)
					{
					}
					void mm_fatal (char *string)
					{
					}],
					[
					long check = mail_expunge_full(NULL, "", 0);
					]
				),
				[ac_cv_imap_tk2006="yes"],
				[ac_cv_imap_tk2006="no"]
			)
		else #looking in imap directory didn't work, try c-client
			imap_ldflags=""
			imap_libs="-lc-client"
			imap_include="-DUSE_SYSTEM_CCLIENT"
			CPPFLAGS="${saved_cppflags}"
			LIBS="${saved_libs}"
			CPPFLAGS="${CPPFLAGS} ${imap_include}"
			LIBS="${LIBS} ${imap_libs} "`echo ${imap_ldflags}`
			AC_LINK_IFELSE(
	    		AC_LANG_PROGRAM(
					[#include <stdio.h>
					#include <c-client/c-client.h>
					void mm_searched (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_exists (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_expunged (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_flags (MAILSTREAM *stream,unsigned long number)
					{
					}
					void mm_notify (MAILSTREAM *stream,char *string,long errflg)
					{
					}
					void mm_list (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
					{
					}
					void mm_lsub (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
					{
					}
					void mm_status (MAILSTREAM *stream,char *mailbox,MAILSTATUS *status)
					{
					}
					void mm_log (char *string,long errflg)
					{
					}
					void mm_dlog (char *string)
					{
					}
					void mm_login (NETMBX *mb,char *user,char *pwd,long trial)
					{
					}
					void mm_critical (MAILSTREAM *stream)
					{
					}
					void mm_nocritical (MAILSTREAM *stream)
					{
					}
					long mm_diskerror (MAILSTREAM *stream,long errcode,long serious)
					{
					}
					void mm_fatal (char *string)
					{
					}],
					[
					MAILSTREAM *foo = mail_open(NULL, "", 0);
					]
				),
				[ac_cv_imap_tk="yes"],
				[ac_cv_imap_tk="no"]
	   		)
			if test "${ac_cv_imap_tk}" = "yes"; then
				AC_LINK_IFELSE(
					AC_LANG_PROGRAM(
						[#include <stdio.h>
						#include <c-client/c-client.h>
						void mm_searched (MAILSTREAM *stream,unsigned long number)
						{
						}
						void mm_exists (MAILSTREAM *stream,unsigned long number)
						{
						}
						void mm_expunged (MAILSTREAM *stream,unsigned long number)
						{
						}
						void mm_flags (MAILSTREAM *stream,unsigned long number)
						{
						}
						void mm_notify (MAILSTREAM *stream,char *string,long errflg)
						{
						}
						void mm_list (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
						{
						}
						void mm_lsub (MAILSTREAM *stream,int delimiter,char *mailbox,long attributes)
						{
						}
						void mm_status (MAILSTREAM *stream,char *mailbox,MAILSTATUS *status)
						{
						}
						void mm_log (char *string,long errflg)
						{
						}
						void mm_dlog (char *string)
						{
						}
						void mm_login (NETMBX *mb,char *user,char *pwd,long trial)
						{
						}
						void mm_critical (MAILSTREAM *stream)
						{
						}
						void mm_nocritical (MAILSTREAM *stream)
						{
						}
						long mm_diskerror (MAILSTREAM *stream,long errcode,long serious)
						{
						}
						void mm_fatal (char *string)
						{
						}],
						[
						long check = mail_expunge_full(NULL, "", 0);
						]
					),
					[ac_cv_imap_tk2006="yes"],
					[ac_cv_imap_tk2006="no"]
				)
			fi
		fi
	fi
	if test "${ac_cv_imap_tk}" = "yes"; then
		AC_MSG_RESULT(yes)
		IMAP_TK_LIB="${imap_libs} "`echo ${imap_ldflags}`
		IMAP_TK_INCLUDE="${imap_include}"
		PBX_IMAP_TK=1
		AC_DEFINE([HAVE_IMAP_TK], 1, [Define if your system has the UW IMAP Toolkit c-client library.])
		if test "${ac_cv_imap_tk2006}" = "yes"; then
			AC_DEFINE([HAVE_IMAP_TK2006], 1, [Define if your system has the UW IMAP Toolkit c-client library version 2006 or greater.])
		fi
	elif test -n "${IMAP_TK_MANDATORY}"; then
		AC_MSG_RESULT(no) 
		AC_MSG_NOTICE([***])
		AC_MSG_NOTICE([*** The UW IMAP Toolkit installation on this system appears to be broken.])
		AC_MSG_NOTICE([*** Either correct the installation, or run configure])
		AC_MSG_NOTICE([*** including --without-imap.])
		exit 1
	else
		AC_MSG_RESULT(no)
	fi
	CPPFLAGS="${saved_cppflags}"
	LIBS="${saved_libs}"
fi

# Needed by unixodbc
AST_EXT_LIB_CHECK([LTDL], [ltdl], [lt_dlinit], [ltdl.h], [])

AC_LANG_PUSH(C++)

if test "${USE_KDE}" != "no"; then
   AC_MSG_CHECKING(for crashHandler in -lkdecore)
   saved_libs="${LIBS}"
   saved_cppflags="${CPPFLAGS}"
   CPPFLAGS="${CPPFLAGS} -I${KDE_DIR}/include"
   if test -d ${KDE_DIR}/lib; then
      kdelibdir="${KDE_DIR}/lib"
   else
      kdelibdir="${KDE_DIR}"
   fi
   LIBS="${LIBS} -L${kdelibdir} -lkdecore"

   AC_LINK_IFELSE(
	[AC_LANG_PROGRAM(
			[#include "kcrash.h"],
			[KCrash::defaultCrashHandler(1);])
	],
	[ac_cv_lib_kde_crash="yes"],
	[ac_cv_lib_kde_crash="no"])
		
   LIBS="${saved_libs}"
   CPPFLAGS="${saved_cppflags}"
	
   if test "${ac_cv_lib_kde_crash}" = "yes"; then
      AC_MSG_RESULT(yes) 
      KDE_LIB="-lkdecore -lkdeui"
      if test "${KDE_DIR}" != ""; then
      	 KDE_LIB="-L${kdelibdir} ${KDE_LIB}"
	 KDE_INCLUDE="-I${KDE_DIR}/include"
      fi
      PBX_KDE=1
      AC_DEFINE([HAVE_LIBKDE], 1, [Define if your system has the KDE libraries.])
   elif test -n "${KDE_MANDATORY}"; then
      AC_MSG_RESULT(no) 
      AC_MSG_NOTICE([***])
      AC_MSG_NOTICE([*** The KDE installation on this system appears to be broken.])
      AC_MSG_NOTICE([*** Either correct the installation, or run configure])
      AC_MSG_NOTICE([*** including --without-kde.])
      exit 1
   else
      AC_MSG_RESULT(no) 
   fi
fi
if test "${PBX_KDE}" = 1; then
   AC_PATH_TOOL(KDEINIT, kdeinit, No)
   if test ! x"${KDEINIT}" = xNo; then
      KDEDIR=$(${DIRNAME} ${KDEINIT})
      KDEDIR=$(${DIRNAME} ${KDEDIR})
   fi
   AC_SUBST([KDEDIR])
fi

AC_LANG_POP

AST_EXT_LIB_CHECK([MISDN], [mISDN], [mISDN_open], [mISDNuser/mISDNlib.h])

if test "${PBX_MISDN}" = 1; then
   AST_EXT_LIB_CHECK([ISDNNET], [isdnnet], [init_manager], [mISDNuser/isdn_net.h], [-lmISDN -lpthread])
   AST_EXT_LIB_CHECK([SUPPSERV], [suppserv], [encodeFac], [mISDNuser/suppserv.h])
   AC_CHECK_HEADER([linux/mISDNdsp.h], [AC_DEFINE_UNQUOTED([MISDN_1_2], 1, [Build chan_misdn for mISDN 1.2 or later.])])
fi

AST_EXT_LIB_CHECK([NBS], [nbs], [nbs_connect], [nbs.h])

AST_EXT_LIB_CHECK([NCURSES], [ncurses], [initscr], [curses.h])

NETSNMP_CONFIG=No
if test "${USE_NETSNMP}" != "no"; then	
   if test "x${NETSNMP_DIR}" != "x"; then
      AC_PATH_TOOL([NETSNMP_CONFIG], [net-snmp-config], No, [${NETSNMP_DIR}/bin])
      if test x"${NETSNMP_CONFIG}" = xNo; then
         AC_MSG_NOTICE([***])
         AC_MSG_NOTICE([*** net-snmp-config was not found in the path you specified:])
         AC_MSG_NOTICE([*** ${NETSNMP_DIR}/bin])
         AC_MSG_NOTICE([*** Either correct the installation, or run configure])
         AC_MSG_NOTICE([*** including --without-netsnmp])
         exit 1
      fi
   else
      AC_PATH_TOOL([NETSNMP_CONFIG], [net-snmp-config], No)
   fi
fi
if test x"${NETSNMP_CONFIG}" != xNo; then
   NETSNMP_libs=`${NETSNMP_CONFIG} --agent-libs`
   
   AC_CHECK_LIB([netsnmp], [snmp_register_callback], AC_DEFINE_UNQUOTED([HAVE_NETSNMP], 1,
   [Define to indicate the Net-SNMP library]), [], ${NETSNMP_libs})

   if test "${ac_cv_lib_netsnmp_snmp_register_callback}" = "yes"; then
      NETSNMP_LIB="${NETSNMP_libs}"
      PBX_NETSNMP=1
   elif test -n "${NETSNMP_MANDATORY}";
   then
      AC_MSG_NOTICE([***])
      AC_MSG_NOTICE([*** The Net-SNMP installation on this system appears to be broken.])
      AC_MSG_NOTICE([*** Either correct the installation, or run configure])
      AC_MSG_NOTICE([*** including --without-netsnmp])
      exit 1
   fi
elif test -n "${NETSNMP_MANDATORY}";
then
   AC_MSG_NOTICE([***])
   AC_MSG_NOTICE([*** The Net-SNMP installation on this system appears to be broken.])
   AC_MSG_NOTICE([*** Either correct the installation, or run configure])
   AC_MSG_NOTICE([*** including --without-netsnmp])
   exit 1
fi

AST_EXT_LIB_CHECK([NEWT], [newt], [newtBell], [newt.h])

AST_EXT_LIB_CHECK([UNIXODBC], [odbc], [SQLConnect], [sql.h], [])

AST_EXT_LIB_CHECK([OGG], [ogg], [ogg_sync_init], [])

if test "${USE_OSS}" != "no"; then
PBX_OSS=0
AC_CHECK_HEADER([linux/soundcard.h],
	        [
		PBX_OSS=1
                AC_DEFINE_UNQUOTED([HAVE_OSS], 1, [Define to indicate the Open Sound System library])
		])
if test "$PBX_OSS" = "0"; then
   AC_CHECK_HEADER([sys/soundcard.h],
                   [
		   PBX_OSS=1
		   AC_DEFINE_UNQUOTED([HAVE_OSS], 1, [Define to indicate the Open Sound System library])
		   ])
fi
if test "$PBX_OSS" = "0"; then
   AST_EXT_LIB_CHECK([OSS], [ossaudio], [oss_ioctl_mixer], [soundcard.h])
fi
fi

PG_CONFIG=No
if test "${USE_PGSQL}" != "no"; then	
   if test "x${PGSQL_DIR}" != "x"; then
      AC_PATH_TOOL([PG_CONFIG], [pg_config], No, [${PGSQL_DIR}/bin])
      if test x"${PG_CONFIG}" = xNo; then
         AC_MSG_NOTICE([***])
         AC_MSG_NOTICE([*** pg_config was not found in the path you specified:])
         AC_MSG_NOTICE([*** ${PGSQL_DIR}/bin])
         AC_MSG_NOTICE([*** Either correct the installation, or run configure])
         AC_MSG_NOTICE([*** including --without-postgres])
         exit 1
      fi
   else
      AC_PATH_TOOL([PG_CONFIG], [pg_config], No)
   fi
fi
if test "${PG_CONFIG}" != No; then
   PGSQL_libdir=`${PG_CONFIG} --libdir`
   PGSQL_includedir=`${PG_CONFIG} --includedir`

   if test "x$?" != "x0" ; then
      if test -n "${PGSQL_MANDATORY}" ; then
         AC_MSG_NOTICE([***])
         AC_MSG_NOTICE([*** The PostgreSQL installation on this system appears to be broken.])
         AC_MSG_NOTICE([*** Either correct the installation, or run configure])
         AC_MSG_NOTICE([*** including --without-postgres])
         exit 1
	  fi
   else 
      AC_CHECK_LIB([pq], [PQescapeStringConn], AC_DEFINE_UNQUOTED([HAVE_PGSQL], 1,
      [Define to indicate the PostgreSQL library]), [], -L${PGSQL_libdir} -lz)

      if test "${ac_cv_lib_pq_PQescapeStringConn}" = "yes"; then
         PGSQL_LIB="-L${PGSQL_libdir} -lpq -lz"
         PGSQL_INCLUDE="-I${PGSQL_includedir}"
         PBX_PGSQL=1
      elif test -n "${PGSQL_MANDATORY}";
      then
         AC_MSG_NOTICE([***])
         AC_MSG_NOTICE([*** The PostgreSQL installation on this system appears to be broken.])
         AC_MSG_NOTICE([*** Either correct the installation, or run configure])
         AC_MSG_NOTICE([*** including --without-postgres])
         exit 1
      fi
   fi
elif test -n "${PGSQL_MANDATORY}";
then
   AC_MSG_NOTICE([***])
   AC_MSG_NOTICE([*** The PostgreSQL installation on this system appears to be broken.])
   AC_MSG_NOTICE([*** Either correct the installation, or run configure])
   AC_MSG_NOTICE([*** including --without-postgres])
   exit 1
fi

AST_EXT_LIB_CHECK([POPT], [popt], [poptStrerror], [popt.h])

AST_EXT_LIB_CHECK([PRI], [pri], [pri_keypad_facility], [libpri.h])
AST_EXT_LIB_CHECK([PRI_CALL_HOLD], [pri], [pri_hold_enable], [libpri.h])
AST_EXT_LIB_CHECK([PRI_VERSION], [pri], [pri_get_version], [libpri.h])
AST_EXT_LIB_CHECK([PRI_INBANDDISCONNECT], [pri], [pri_set_inbanddisconnect], [libpri.h])

if test "${USE_PWLIB}" != "no"; then
	if test -n "${PWLIB_DIR}"; then
		PWLIBDIR="${PWLIB_DIR}"
	fi
	AST_CHECK_PWLIB()
	AST_CHECK_PWLIB_VERSION([PWLib], [PWLIB], [ptbuildopts.h], [1], [9], [2], [P[[WT]]LIB_VERSION])
		
	if test "${HAS_PWLIB:-unset}" != "unset"; then
		AST_CHECK_PWLIB_PLATFORM()

		PLATFORM_PWLIB="pt_${PWLIB_PLATFORM}_r"

		AST_CHECK_PWLIB_BUILD([PWLib], [PWLIB],
			[Define if your system has the PWLib libraries.],
			[#include "ptlib.h"],
			[int q = (int) PTime::IsDaylightSavings();])
	fi
fi

if test "${USE_PWLIB}" != "no" -a "x${ac_cv_lib_PWLIB}" != "xyes" -a -n "${PWLIB_MANDATORY}"; then
   AC_MSG_NOTICE([***])
   AC_MSG_NOTICE([*** The PWLIB installation on this system appears to be broken.])
   AC_MSG_NOTICE([*** Either correct the installation, or run configure])
   AC_MSG_NOTICE([*** including --without-pwlib])
   exit 1
fi

if test "${PBX_PWLIB}" = "1" -a "${USE_OPENH323}" != "no" ; then
	if test -n "${OPENH323_DIR}"; then
		OPENH323DIR="${OPENH323_DIR}"
	fi
	AST_CHECK_OPENH323()
	AST_CHECK_PWLIB_VERSION([OpenH323], [OPENH323], [openh323buildopts.h], [1], [17], [3], [], [1], [19], [0])
	AST_CHECK_OPENH323_BUILD()
	PLATFORM_OPENH323="h323_${PWLIB_PLATFORM}_${OPENH323_SUFFIX}"
	AST_CHECK_PWLIB_BUILD([OpenH323], [OPENH323],
		[Define if your system has the OpenH323 libraries.],
		[#include "ptlib.h"
		#include "h323.h"
		#include "h323ep.h"],
		[H323EndPoint ep = H323EndPoint();],
		[${PWLIB_INCLUDE}], [${PWLIB_LIB}])
fi
if test "${USE_OPENH323}" != "no" -a "x${ac_cv_lib_OPENH323}" != "xyes" -a -n "${OPENH323_MANDATORY}"; then
   AC_MSG_NOTICE([***])
   AC_MSG_NOTICE([*** The OPENH323 installation on this system appears to be broken.])
   AC_MSG_NOTICE([*** Either correct the installation, or run configure])
   AC_MSG_NOTICE([*** including --without-h323])
   exit 1
fi

AST_EXT_LIB_CHECK([RADIUS], [radiusclient-ng], [rc_read_config], [radiusclient-ng.h])

AST_EXT_LIB_CHECK([SPEEX], [speex], [speex_encode], [speex/speex.h], [-lm])

# See if the main speex library contains the preprocess functions
AST_EXT_LIB_CHECK([SPEEX_PREPROCESS], [speex], [speex_preprocess_ctl], [speex/speex.h], [-lm])
if test "${PBX_SPEEX_PREPROCESS}" = 1; then
   PBX_SPEEX_PREPROCESS=1
fi

AST_EXT_LIB_CHECK([SPEEXDSP], [speexdsp], [speex_preprocess_ctl], [speex/speex.h], [-lm])
if test "${PBX_SPEEXDSP}" = 1; then
   PBX_SPEEX_PREPROCESS=1
fi

AC_SUBST(PBX_SPEEX_PREPROCESS)

AST_EXT_LIB_CHECK([SQLITE], [sqlite], [sqlite_exec], [sqlite.h])

AST_EXT_LIB_CHECK([OPENSSL], [ssl], [ssl2_connect], [openssl/ssl.h], [-lcrypto])
if test "$PBX_OPENSSL" = "1";
then
    AST_EXT_LIB_CHECK([OSPTK], [osptk], [OSPPCryptoDecrypt], [osp/osp.h], [-lcrypto -lssl])
fi

AST_EXT_LIB_CHECK([FREETDS], [tds], [tds_version], [tds.h])
if test "${PBX_FREETDS}" != "0";
then
    if test "${FREETDS_DIR}x" = "x";
    then
        for tds_dir in /usr /usr/local;
        do
            if test -f "${tds_dir}/include/tdsver.h";
            then
                FREETDS_DIR="${tds_dir}"
            fi
        done
    fi
    case `${GREP} TDS_VERSION_NO ${FREETDS_DIR:-/usr}/include/tdsver.h` in
    *0.64*)
        FREETDS_INCLUDE="${FREETDS_INCLUDE} -DFREETDS_0_64"
	;;
    *0.63*)
        FREETDS_INCLUDE="${FREETDS_INCLUDE} -DFREETDS_0_63"
	;;
    *0.62*)
        FREETDS_INCLUDE="${FREETDS_INCLUDE} -DFREETDS_0_62"
	;;
    *)
        FREETDS_INCLUDE="${FREETDS_INCLUDE} -DFREETDS_PRE_0_62"
	;;
    esac
fi

AST_EXT_LIB_CHECK([TERMCAP], [termcap], [tgetent], [])

AST_EXT_LIB_CHECK([TINFO], [tinfo], [tgetent], [])

if test "${host_os}" != "linux-gnu" ; then
  tonezone_extra_lib="-lm"
fi

if test "${PBX_DAHDI}" = "1" || test "${USE_ZAPTEL}" = "no"; then
  tonezone_dir="dahdi"
   if test "x${DAHDI_DIR}" != "x"; then
      tonezone_extra_inc="${DAHDI_INCLUDE}"
   fi
else
  tonezone_dir="zaptel"
   if test "x${ZAPTEL_DIR}" != "x"; then
      tonezone_extra_inc="${ZAPTEL_INCLUDE}"
   fi
fi

AST_EXT_LIB_CHECK([TONEZONE], [tonezone], [tone_zone_find], [${tonezone_dir}/tonezone.h], [${tonezone_extra_lib}], [${tonezone_extra_inc}])

AST_EXT_LIB_CHECK([USB], [usb], [usb_init], [usb.h], [])

AST_EXT_LIB_CHECK([VORBIS], [vorbis], [vorbis_info_init], [vorbis/codec.h], [-lm -lvorbisenc])

AC_LANG_PUSH(C++)

if test "${USE_VPB}" != "no"; then
   AC_MSG_CHECKING(for vpb_open in -lvpb)
   saved_libs="${LIBS}"
   saved_cppflags="${CPPFLAGS}"
   if test "x${VPB_DIR}" != "x"; then
      if test -d ${VPB_DIR}/lib; then
         vpblibdir=${VPB_DIR}/lib
      else
         vpblibdir=${VPB_DIR}
      fi
      LIBS="${LIBS} -L${vpblibdir}"
      CPPFLAGS="${CPPFLAGS} -I${VPB_DIR}/include"
   fi
   LIBS="${LIBS} -lvpb -lpthread"
   AC_LINK_IFELSE(
	[
	AC_LANG_PROGRAM(
	[#include <vpbapi.h>],
	[int q = vpb_open(0,0);])
	],
	[	AC_MSG_RESULT(yes) 
		ac_cv_lib_vpb_vpb_open="yes" 
	],
	[	AC_MSG_RESULT(no) 
		ac_cv_lib_vpb_vpb_open="no" 
	]
	)
   LIBS="${saved_libs}"
   CPPFLAGS="${saved_cppflags}"
   if test "${ac_cv_lib_vpb_vpb_open}" = "yes"; then
	VPB_LIB="-lvpb"
	if test "${VPB_DIR}" != ""; then
	   VPB_LIB="-L${vpblibdir}  ${VPB_LIB}"
	   VPB_INCLUDE="-I${VPB_DIR}/include"
	fi
	PBX_VPB=1
	AC_DEFINE([HAVE_VPB], 1, [Define if your system has the VoiceTronix API libraries.])
   elif test -n "${VPB_MANDATORY}"; then
      AC_MSG_NOTICE([***])
      AC_MSG_NOTICE([*** The VoiceTronix (vpb) installation on this system appears to be broken.])
      AC_MSG_NOTICE([*** Either correct the installation, or run configure])
      AC_MSG_NOTICE([*** including --without-vpb.])
      exit 1
   fi
fi

AC_LANG_POP

AST_EXT_LIB_CHECK([ZLIB], [z], [compress], [zlib.h])

if test "${USE_ZAPTEL}" != "no" && test "x${PBX_DAHDI}" != "x1"; then
   AC_MSG_CHECKING(for ZT_DIAL_OP_CANCEL in zaptel/zaptel.h)
   saved_cppflags="${CPPFLAGS}"
   if test "x${ZAPTEL_DIR}" != "x"; then
      CPPFLAGS="${CPPFLAGS} -I${ZAPTEL_DIR}/include"
   fi
   AC_COMPILE_IFELSE(
	[
	AC_LANG_PROGRAM(
	[#include <zaptel/zaptel.h>],
	[int foo = ZT_DIAL_OP_CANCEL;])
	],
	[	AC_MSG_RESULT(yes) 
		ac_cv_zaptel_h="yes" 
	],
	[	AC_MSG_RESULT(no) 
		ac_cv_zaptel_h="no" 
	]
	)
   CPPFLAGS="${saved_cppflags}"
   if test "${ac_cv_zaptel_h}" = "yes"; then
	if test "${ZAPTEL_DIR}" != ""; then
	   ZAPTEL_INCLUDE="-I${ZAPTEL_DIR}/include"
	fi
	PBX_ZAPTEL=1
	AC_DEFINE([HAVE_ZAPTEL], 1, [Define if your system has the Zaptel headers.])
	saved_cppflags="${CPPFLAGS}"
	CPPFLAGS="${CPPFLAGS} ${ZAPTEL_INCLUDE}"
	AC_CHECK_MEMBER([ZT_PARAMS.chan_alarms],,,[#include <zaptel/zaptel.h>])
	CPPFLAGS="${saved_cppflags}"
	if test "${ac_cv_member_ZT_PARAMS_chan_alarms}" = "yes"; then
	     AC_DEFINE([HAVE_ZAPTEL_CHANALARMS], 1, [Define if your Zaptel drivers have chan_alarms.])
	fi
   elif test -n "${ZAPTEL_MANDATORY}"; 
   then
      AC_MSG_NOTICE([***])
      AC_MSG_NOTICE([*** The Zaptel installation on this system appears to be broken.])
      AC_MSG_NOTICE([*** Either correct the installation, or run configure])
      AC_MSG_NOTICE([*** including --without-zaptel.])
      exit 1
   fi
fi

if test "${PBX_ZAPTEL}" = 1 && test "x${PBX_DAHDI}" != "x1"; then
   AC_MSG_CHECKING(for ZT_EVENT_REMOVED in zaptel/zaptel.h)
   saved_cppflags="${CPPFLAGS}"
   if test "x${ZAPTEL_DIR}" != "x"; then
      CPPFLAGS="${CPPFLAGS} -I${ZAPTEL_DIR}/include"
   fi
   AC_COMPILE_IFELSE(
	[
	AC_LANG_PROGRAM(
	[#include <zaptel/zaptel.h>],
	[int foo = ZT_EVENT_REMOVED;])
	],
	[	AC_MSG_RESULT(yes) 
		ac_cv_zaptel_vldtmf="yes" 
	],
	[	AC_MSG_RESULT(no) 
		ac_cv_zaptel_vldtmf="no" 
	]
	)
   CPPFLAGS="${saved_cppflags}"
   if test "${ac_cv_zaptel_vldtmf}" = "yes"; then
	PBX_ZAPTEL_VLDTMF=1
   fi
   AC_MSG_CHECKING(for zt_transcoder_info in zaptel/zaptel.h)
   saved_cppflags="${CPPFLAGS}"
   if test "x${ZAPTEL_DIR}" != "x"; then
      CPPFLAGS="${CPPFLAGS} -I${ZAPTEL_DIR}/include"
   fi
   AC_COMPILE_IFELSE(
	[
	AC_LANG_PROGRAM(
	[#include <zaptel/zaptel.h>],
	[int foo = sizeof(struct zt_transcoder_info);])
	],
	[	AC_MSG_RESULT(yes) 
		ac_cv_zaptel_transcode="yes" 
	],
	[	AC_MSG_RESULT(no) 
		ac_cv_zaptel_transcode="no" 
	]
	)
   CPPFLAGS="${saved_cppflags}"
   if test "${ac_cv_zaptel_transcode}" = "yes"; then
	PBX_DAHDI_TRANSCODE=1
   fi
fi
AC_SUBST(PBX_ZAPTEL_VLDTMF)
AC_SUBST(PBX_DAHDI_TRANSCODE)

if test "x${PBX_ZAPTEL}" = "x1" ; then
	PBX_DAHDI=1
	DAHDI_INCLUDE=${ZAPTEL_INCLUDE}
	DAHDI_LIB=${ZAPTEL_LIB}
	AC_SUBST(PBX_DAHDI)
	AC_SUBST(DAHDI_INCLUDE)
	AC_SUBST(DAHDI_LIB)
fi

EDITLINE_LIB=""
if test "x$TERMCAP_LIB" != "x" ; then
  EDITLINE_LIB="$TERMCAP_LIB"
elif test "x$TINFO_LIB" != "x" ; then
  EDITLINE_LIB="$TINFO_LIB"
elif test "x$CURSES_LIB" != "x" ; then
  EDITLINE_LIB="$CURSES_LIB"
elif test "x$NCURSES_LIB" != "x" ; then
  EDITLINE_LIB="$NCURSES_LIB"
else
  AC_MSG_ERROR([*** termcap support not found (on modern systems, this typically means the ncurses development package is missing)])
fi
AC_SUBST(EDITLINE_LIB)

AC_CHECK_HEADER([h323.h], [PBX_H323=1], [PBX_H323=0])
AC_SUBST(PBX_H323)

AC_CHECK_HEADER([linux/compiler.h],
                [AC_DEFINE_UNQUOTED([HAVE_LINUX_COMPILER_H], 1, [Define to 1 if your system has linux/compiler.h.])])

AC_CHECK_HEADER([linux/ixjuser.h], [PBX_IXJUSER=1], [PBX_IXJUSER=0], [
				   #include <linux/version.h>
				   #ifdef HAVE_LINUX_COMPILER_H
				   #include <linux/compiler.h>
				   #endif
				   ])
AC_SUBST(PBX_IXJUSER)

if test "${cross_compiling}" = "no";
then
  AC_CHECK_FILE(/sbin/launchd, AC_DEFINE([HAVE_SBIN_LAUNCHD], 1, [Define to 1 if your system has /sbin/launchd.]))
fi

PBX_GTK=0
AC_CHECK_TOOL(GTKCONFIG, gtk-config, No)
if test ! "x${GTKCONFIG}" = xNo; then
   GTK_INCLUDE=$(${GTKCONFIG} --cflags gthread)
   GTK_LIB=$(${GTKCONFIG} --libs gthread)
   PBX_GTK=1
   AC_DEFINE([HAVE_GTK], 1, [Define if your system has the GTK libraries.])
fi
AC_SUBST(PBX_GTK)
AC_SUBST(GTK_INCLUDE)
AC_SUBST(GTK_LIB)

PBX_GTK2=0
AC_CHECK_TOOL(PKGCONFIG, pkg-config, No)
if test ! "x${PKGCONFIG}" = xNo; then
   GTK2_INCLUDE=$(${PKGCONFIG} gtk+-2.0 --cflags 2>/dev/null)
   GTK2_LIB=$(${PKGCONFIG} gtk+-2.0 --libs)
   PBX_GTK2=1
   AC_DEFINE([HAVE_GTK2], 1, [Define if your system has the GTK2 libraries.])
fi
AC_SUBST(PBX_GTK2)
AC_SUBST(GTK2_INCLUDE)
AC_SUBST(GTK2_LIB)

if test -f makeopts; then
	${ac_cv_path_EGREP} 'CURSES|GTK2|OSARCH|NEWT' makeopts > makeopts.acbak
else
	touch makeopts.acbak
fi

AC_CONFIG_FILES([build_tools/menuselect-deps makeopts channels/h323/Makefile])
AC_OUTPUT

${ac_cv_path_EGREP} 'CURSES|GTK2|OSARCH|NEWT' makeopts > makeopts.acbak2
if test "x${ac_cv_path_CMP}" = "x:"; then
	( cd `pwd`/menuselect && ./configure )
else if ${ac_cv_path_CMP} -s makeopts.acbak makeopts.acbak2; then : ; else
	( cd `pwd`/menuselect && ./configure )
fi ; fi

rm makeopts.acbak makeopts.acbak2


if test "x${silent}" != "xyes" ; then
echo
echo "               .\$\$\$\$\$\$\$\$\$\$\$\$\$\$\$=..      "
echo "            .\$7\$7..          .7\$\$7:.    "
echo "          .\$\$:.                 ,\$7.7   "
echo "        .\$7.     7\$\$\$\$           .\$\$77  "
echo "     ..\$\$.       \$\$\$\$\$            .\$\$\$7 "
echo "    ..7\$   .?.   \$\$\$\$\$   .?.       7\$\$\$."
echo "   \$.\$.   .\$\$\$7. \$\$\$\$7 .7\$\$\$.      .\$\$\$."
echo " .777.   .\$\$\$\$\$\$77\$\$\$77\$\$\$\$\$7.      \$\$\$,"
echo " \$\$\$~      .7\$\$\$\$\$\$\$\$\$\$\$\$\$7.       .\$\$\$."
echo ".\$\$7          .7\$\$\$\$\$\$\$7:          ?\$\$\$."
echo "\$\$\$          ?7\$\$\$\$\$\$\$\$\$\$I        .\$\$\$7 "
echo "\$\$\$       .7\$\$\$\$\$\$\$\$\$\$\$\$\$\$\$\$      :\$\$\$. "
echo "\$\$\$       \$\$\$\$\$\$7\$\$\$\$\$\$\$\$\$\$\$\$    .\$\$\$.  "
echo "\$\$\$        \$\$\$   7\$\$\$7  .\$\$\$    .\$\$\$.   "
echo "\$\$\$\$             \$\$\$\$7         .\$\$\$.    "
echo "7\$\$\$7            7\$\$\$\$        7\$\$\$      "
echo " \$\$\$\$\$                        \$\$\$       "
echo "  \$\$\$\$7.                       \$\$  (TM)     "
echo "   \$\$\$\$\$\$\$.           .7\$\$\$\$\$\$  \$\$      "
echo "     \$\$\$\$\$\$\$\$\$\$\$\$7\$\$\$\$\$\$\$\$\$.\$\$\$\$\$\$      "
echo "       \$\$\$\$\$\$\$\$\$\$\$\$\$\$\$\$.                "
echo
fi

AC_MSG_NOTICE(Package configured for: )
AC_MSG_NOTICE( OS type  : $host_os)
AC_MSG_NOTICE( Host CPU : $host_cpu)
if test "${cross_compiling}" = "yes"; then
   AC_MSG_NOTICE( Cross Compilation = YES)
fi
