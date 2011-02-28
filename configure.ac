# Process this file with autoconf to produce a configure script.
#
# Make sure we use autoconf 2.60 to generate the "configure" script,
# in case we want to commit it. Other than that, version 2.59 is
# perfectly fine for our purposes, so people who want to modify
# this file just have to remember to set the AC_PREREQ argument
# to something that suits their needs.

AC_PREREQ(2.60)

AC_INIT([asterisk], [trunk], [https://issues.asterisk.org])

# cross-compile macros
AC_CANONICAL_BUILD
AC_CANONICAL_HOST

# check existence of the package
AC_CONFIG_SRCDIR([main/asterisk.c])

AC_CONFIG_AUX_DIR(`pwd`)

AC_COPYRIGHT("Asterisk")
AC_REVISION($Revision$)

# preserve any CFLAGS or LDFLAGS that may be set
# NOTE: This must be done before calling any macros that end up
# calling AC_PROG_CC or the like, since they will set a default
# set of CFLAGS ("-g -O2") if the user did not supply any, and
# we don't want those default flags to be carried over into the
# rest of the build system since we have other means of controlling
# debugging symbol generation and optimization.
CONFIG_CFLAGS="${CFLAGS}"
CONFIG_LDFLAGS="${LDFLAGS}"
AC_SUBST(CONFIG_CFLAGS)
AC_SUBST(CONFIG_LDFLAGS)

# specify output header file
AC_CONFIG_HEADER(include/asterisk/autoconfig.h)

AC_USE_SYSTEM_EXTENSIONS	dnl note- does not work on FreeBSD

# System default paths
AC_SUBST([astsbindir],        ['${sbindir}'])dnl
AC_SUBST([astetcdir],         ['${sysconfdir}/asterisk'])dnl
AC_SUBST([astheaderdir],      ['${includedir}/asterisk'])dnl
AC_SUBST([astlibdir],         ['${libdir}/asterisk'])dnl
AC_SUBST([astmandir],         ['${mandir}'])dnl
AC_SUBST([astvarlibdir],      ['${localstatedir}/lib/asterisk'])dnl
AC_SUBST([astdatadir],        ['${astvarlibdir}'])dnl
AC_SUBST([astdbdir],          ['${astvarlibdir}'])dnl
AC_SUBST([astkeydir],         ['${astvarlibdir}'])dnl
AC_SUBST([astspooldir],       ['${localstatedir}/spool/asterisk'])dnl
AC_SUBST([astlogdir],         ['${localstatedir}/log/asterisk'])dnl
AC_SUBST([astvarrundir],      ['${localstatedir}/run/asterisk'])dnl

case "${host_os}" in
     *bsd*)
     if test ${prefix} = 'NONE'; then
        astvarlibdir='${prefix}/share/asterisk'
        astdbdir='${localstatedir}/db/asterisk'
     fi
     ;;
     darwin*)
     if test ${prefix} = 'NONE'; then
        astvarrundir='/Library/Application Support/Asterisk/Run'
     fi
     ;;
esac

case "${host_os}" in
     freebsd*)
     ac_default_prefix=/usr/local
     CPPFLAGS=-I/usr/local/include
     LDFLAGS=-L/usr/local/lib
     ;;
     openbsd*)
     ac_default_prefix=/usr/local
     if test ${prefix} = '/usr/local' || test ${prefix} = 'NONE'; then
        if test ${sysconfdir} = '${prefix}/etc'; then
           astetcdir=/etc/asterisk
        fi
        if test ${mandir} = '${prefix}/man'; then
           astmandir=/usr/share/man
        fi
     fi
     CPPFLAGS=-I/usr/local/include
     LDFLAGS=-L/usr/local/lib
     ;;
     darwin*)
     ac_default_prefix=/usr/local
     if test ${prefix} = 'NONE'; then
        astlibdir='/Library/Application Support/Asterisk/Modules'
        astvarlibdir='/Library/Application Support/Asterisk'
        astlogdir=/Library/Logs/Asterisk
        astvarrundir='/Library/Application Support/Asterisk/Run'
     fi
     AC_DEFINE([AST_POLL_COMPAT], 1, [Define to 1 if internal poll should be used.])
     AC_DEFINE([_DARWIN_UNLIMITED_SELECT], 1, [Define to 1 if running on Darwin.])
     ;;
     solaris*)
     if test ${prefix} = 'NONE'; then
        astetcdir=/var/etc/asterisk
        astsbindir=/opt/asterisk/sbin
        astlibdir=/opt/asterisk/lib
        astheaderdir=/opt/asterisk/include
        astmandir=/opt/asterisk/man
        astvarlibdir=/var/opt/asterisk
        astspooldir=/var/spool/asterisk
        astlogdir=/var/log/asterisk
        astvarrundir=/var/run/asterisk
     fi
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

PBX_WINARCH=0

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
     mingw32)
     OSARCH=mingw32
     PBX_WINARCH=1
     ;;
     cygwin)
     OSARCH=cygwin
     PBX_WINARCH=1
     ;;
     linux-gnueabi)
     OSARCH=linux-gnu
     ;;
     *)
     OSARCH=${host_os}
     ;;
esac

AC_SUBST(OSARCH)
AC_SUBST(PBX_WINARCH)

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
AST_PROG_LD	# note, does not work on FreeBSD
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_RANLIB
AST_CHECK_GNU_MAKE
AC_PROG_EGREP

AC_CHECK_TOOLS([STRIP], [strip gstrip], :)
AC_CHECK_TOOLS([AR], [ar gar], :)
AC_CHECK_TOOLS([SHA1SUM], [sha1sum], $ac_aux_dir/build_tools/sha1sum-sh)
AC_CHECK_TOOLS([OPENSSL], [openssl], :)

GNU_LD=0
if test "x$with_gnu_ld" = "xyes" ; then
   GNU_LD=1
fi
AC_SUBST(GNU_LD)

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
AC_PATH_PROG([CURL], [curl], :)
AC_PATH_PROG([RUBBER], [rubber], :)
AC_PATH_PROG([CATDVI], [catdvi], :)
AC_PATH_PROG([KPATHSEA], [kpsewhich], :)
AC_PATH_PROG([XMLLINT], [xmllint], :)
AC_PATH_PROG([XMLSTARLET], [xmlstarlet], :)
if test "${WGET}" != ":" ; then
  DOWNLOAD=${WGET}
else if test "${CURL}" != ":" ; then
  DOWNLOAD="${CURL} -O --progress-bar -w \"%{url_effective}\n\""
else
  AC_PATH_PROG([FETCH], [fetch], [:])
  DOWNLOAD=${FETCH}
fi
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
AC_CACHE_CHECK([for flex that ignores fwrite return value], [ac_cv_FLEX_WORKAROUND], [
	if test "x$FLEX" != "x:" ; then
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
		cat >$tmp/test.fl <<__EOL__
%{
#include <unistd.h>
#include <stdio.h>
static void yyunput (int c,char *buf_ptr  ) __attribute__((unused));
static int input(void) __attribute__((unused));
%}

%%
username    printf( "%s", getlogin() );
__EOL__
		${FLEX} -o ${tmp}/test.c ${tmp}/test.fl
		${CC} -o ${tmp}/test.o -c ${tmp}/test.c -Wall -Werror >/dev/null 2>&1
		if test -e "${tmp}/test.o"; then
			ac_cv_FLEX_WORKAROUND=no
		else
			ac_cv_FLEX_WORKAROUND=yes
		fi
		rm -rf ${tmp}
	fi
	])

if test "x${ac_cv_FLEX_WORKAROUND}" = "xyes"; then
	AC_DEFINE([NEED_FLEX_FWRITE_WORKAROUND], 1, [Define to 1 if your system has a version of flex that does not check the return value of fwrite.])
fi

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

AC_CHECK_PROGS([MD5], [md5 md5sum gmd5sum digest])

if test "${MD5}" = "digest" ; then
   MD5="${MD5} -a md5"   
fi

ACX_PTHREAD

AC_LANG(C)

AC_ARG_ENABLE([dev-mode],
	[AS_HELP_STRING([--enable-dev-mode],
		[Turn on developer mode])],
	[case "${enableval}" in
	      y|ye|yes) AST_DEVMODE=yes ;;
	      n|no)  AST_DEVMODE=no ;;
	      noisy)
	      AST_DEVMODE=yes
	      NOISY_BUILD=yes
	      ;;
	      *) AC_MSG_ERROR(bad value ${enableval} for --enable-dev-mode)  ;;
	esac])
AC_SUBST(NOISY_BUILD)
AC_SUBST(AST_DEVMODE)

AST_CODE_COVERAGE=no
AC_ARG_ENABLE([coverage],
	[AS_HELP_STRING([--enable-coverage],
		[Turn on code coverage tracking (for gcov)])],
	[case "${enableval}" in
	      y|ye|yes) AST_CODE_COVERAGE=yes ;;
	      n|no) AST_CODE_COVERAGE=no ;;
	      *) AC_MSG_ERROR(bad value ${enableval} for --enable-coverage)  ;;
	esac])
AC_SUBST(AST_CODE_COVERAGE)

# AST_EXT_LIB_SETUP is used to tell configure to handle variables for
# various packages.
# $1 is the prefix for the variables in makeopts and autoconfig.h
# $2 is the short comment, $4 is the long comment
# $3 is the name used in --with- or --without- flags for configure.
#
# Package option names should be in alphabetical order
# by the --with option name (the third field),
# to make things easier for the users.

AST_EXT_LIB_SETUP([ALSA], [Advanced Linux Sound Architecture], [asound])
AST_EXT_LIB_SETUP([BFD], [Debug symbol decoding], [bfd])

# BKTR is used for backtrace support on platforms that do not
# have it natively.
AST_EXT_LIB_SETUP([BKTR], [Stack Backtrace], [execinfo])
AST_EXT_LIB_SETUP([BLUETOOTH], [Bluetooth], [bluetooth])
AST_EXT_LIB_SETUP([CAP], [POSIX 1.e capabilities], [cap])
AST_EXT_LIB_SETUP([CURSES], [curses], [curses])
AST_EXT_LIB_SETUP([CRYPTO], [OpenSSL Cryptography], [crypto])
AST_EXT_LIB_SETUP([DAHDI], [DAHDI], [dahdi])
AST_EXT_LIB_SETUP([FFMPEG], [Ffmpeg and avcodec], [avcodec])
AST_EXT_LIB_SETUP([GSM], [External GSM], [gsm], [, use 'internal' GSM otherwise])
AST_EXT_LIB_SETUP([GTK2], [gtk2], [gtk2])
AST_EXT_LIB_SETUP([GMIME], [GMime], [gmime])
AST_EXT_LIB_SETUP([OPENH323], [OpenH323], [h323])
AST_EXT_LIB_SETUP([HOARD], [Hoard Memory Allocator], [hoard])
AST_EXT_LIB_SETUP([ICAL], [iCal], [ical])
AST_EXT_LIB_SETUP([ICONV], [Iconv], [iconv])
AST_EXT_LIB_SETUP([IKSEMEL], [Iksemel Jabber], [iksemel])
AST_EXT_LIB_SETUP([IMAP_TK], [UW IMAP Toolkit], [imap])
AST_EXT_LIB_SETUP([INOTIFY], [inotify support], [inotify])
AST_EXT_LIB_SETUP([IODBC], [iODBC], [iodbc])
AST_EXT_LIB_SETUP([ISDNNET], [ISDN4Linux], [isdnnet])
AST_EXT_LIB_SETUP([JACK], [Jack Audio Connection Kit], [jack])
AST_EXT_LIB_SETUP([KQUEUE], [kqueue support], [kqueue])
AST_EXT_LIB_SETUP([LDAP], [OpenLDAP], [ldap])
AST_LIBCURL_CHECK_CONFIG([], [7.10.1])
AST_EXT_LIB_SETUP([LIBXML2], [LibXML2], [libxml2])
AST_EXT_LIB_SETUP([LTDL], [libtool], [ltdl])
AST_EXT_LIB_SETUP([LUA], [Lua], [lua])
AST_EXT_LIB_SETUP([MISDN], [mISDN user], [misdn])
AST_EXT_LIB_SETUP([MYSQLCLIENT], [MySQL client], [mysqlclient])
AST_EXT_LIB_SETUP([NBS], [Network Broadcast Sound], [nbs])
AST_EXT_LIB_SETUP([NCURSES], [ncurses], [ncurses])
AST_EXT_LIB_SETUP([NEON], [neon], [neon])
AST_EXT_LIB_SETUP([NEON29], [neon29], [neon29])
AST_EXT_LIB_SETUP([NETSNMP], [Net-SNMP], [netsnmp])
AST_EXT_LIB_SETUP([NEWT], [newt], [newt])
AST_EXT_LIB_SETUP([OGG], [OGG], [ogg])
AST_EXT_LIB_SETUP([OPENAIS], [OpenAIS], [openais])
AST_EXT_LIB_SETUP([OPENR2], [MFR2], [openr2])
AST_EXT_LIB_SETUP([OSPTK], [OSP Toolkit], [osptk])
AST_EXT_LIB_SETUP([OSS], [Open Sound System], [oss])
AST_EXT_LIB_SETUP([PGSQL], [PostgreSQL], [postgres])
AST_EXT_LIB_SETUP([POPT], [popt], [popt])
AST_EXT_LIB_SETUP([PORTAUDIO], [PortAudio], [portaudio])
AST_EXT_LIB_SETUP([PRI], [ISDN PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_MWI], [ISDN PRI Message Waiting Indication], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_MCID], [ISDN PRI Malicious Call ID], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_CALL_WAITING], [ISDN PRI call waiting supplementary service], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_AOC_EVENTS], [ISDN PRI advice of charge supplementary service events], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_TRANSFER], [ISDN PRI call transfer supplementary service], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_CCSS], [ISDN PRI call completion supplementary service], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_HANGUP_FIX], [ISDN PRI hangup fix], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_SUBADDR], [ISDN PRI subaddressing], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_CALL_HOLD], [ISDN PRI call hold], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_CALL_REROUTING], [ISDN PRI call rerouting and call deflection], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_SETUP_KEYPAD], [ISDN PRI keypad facility in SETUP], [PRI], [pri])
# ------------------------------------v
# TODO: The code can be changed to always include these features now.
# These features will always be present if pri_connected_line_update is available.
AST_EXT_LIB_SETUP_DEPENDENT([PRI_INBANDDISCONNECT], [ISDN PRI set_inbanddisconnect], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_PROG_W_CAUSE], [ISDN progress with cause], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_SERVICE_MESSAGES], [ISDN service messages], [PRI], [pri])
AST_EXT_LIB_SETUP_DEPENDENT([PRI_REVERSE_CHARGE], [ISDN reverse charge], [PRI], [pri])
# ------------------------------------^
AST_EXT_LIB_SETUP([PWLIB], [PWlib], [pwlib])
AST_EXT_LIB_SETUP([RADIUS], [Radius Client], [radius])
AST_EXT_LIB_SETUP([RESAMPLE], [LIBRESAMPLE], [resample])
AST_EXT_LIB_SETUP([SDL], [Sdl], [sdl])
AST_EXT_LIB_SETUP([SDL_IMAGE], [Sdl Image], [SDL_image])
AST_OPTION_ONLY([sounds-cache], [SOUNDS_CACHE_DIR], [cached sound tarfiles], [])
AST_EXT_LIB_SETUP([SPANDSP], [SPANDSP], [spandsp])
AST_EXT_LIB_SETUP([SS7], [ISDN SS7], [ss7])
AST_EXT_LIB_SETUP([SPEEX], [Speex], [speex])
AST_EXT_LIB_SETUP([SPEEX_PREPROCESS], [Speex preprocess routines], [speex])
AST_EXT_LIB_SETUP([SPEEXDSP], [SpeexDSP], [speexdsp])
AST_EXT_LIB_SETUP_DEPENDENT([SPEEX_PREPROCESS], [speex_preprocess_ctl], [], [speex])
AST_EXT_LIB_SETUP([SQLITE], [SQLite], [sqlite])
AST_EXT_LIB_SETUP([SQLITE3], [SQLite], [sqlite3])
AST_EXT_LIB_SETUP([SRTP], [Secure RTP], [srtp])
AST_EXT_LIB_SETUP([OPENSSL], [OpenSSL Secure Sockets Layer], [ssl])
AST_EXT_LIB_SETUP([SUPPSERV], [mISDN Supplemental Services], [suppserv])
AST_EXT_LIB_SETUP([FREETDS], [FreeTDS], [tds])
AST_EXT_LIB_SETUP([TERMCAP], [Termcap], [termcap])
AST_EXT_LIB_SETUP([TIMERFD], [timerfd], [timerfd])
AST_EXT_LIB_SETUP([TINFO], [Term Info], [tinfo])
AST_EXT_LIB_SETUP([TONEZONE], [tonezone], [tonezone])
AST_EXT_LIB_SETUP([UNIXODBC], [unixODBC], [unixodbc])
AST_EXT_LIB_SETUP([USB], [usb], [usb])
AST_EXT_LIB_SETUP([VORBIS], [Vorbis], [vorbis])
AST_EXT_LIB_SETUP([VPB], [Voicetronix API], [vpb])
AST_EXT_LIB_SETUP([X11], [X11], [x11])
AST_EXT_LIB_SETUP([ZLIB], [zlib compression], [z])

# check for basic system features and functionality before
# checking for package libraries

AC_FUNC_ALLOCA
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h libintl.h limits.h locale.h malloc.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/event.h sys/file.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h syslog.h termios.h unistd.h utime.h arpa/nameser.h sys/io.h])

# some embedded systems omit internationalization (locale) support
AC_CHECK_HEADERS([xlocale.h])

AC_CHECK_HEADERS([winsock.h winsock2.h])

AC_CHECK_HEADER([sys/poll.h],
	 [],
     AC_DEFINE([AST_POLL_COMPAT], 1, [Define to 1 if internal poll should be used.]))

AC_SYS_LARGEFILE

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_LONG_DOUBLE_WIDER
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_CHECK_MEMBERS([struct ucred.uid, struct ucred.cr_uid], [], [], [#include <sys/socket.h>])
AC_CHECK_MEMBERS([struct ifreq.ifr_ifru.ifru_hwaddr], [], [], [#include <net/if.h>])
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
AC_CHECK_FUNCS([asprintf atexit closefrom dup2 eaccess endpwent euidaccess ffsll ftruncate getcwd gethostbyname gethostname getloadavg gettimeofday glob htonll ioperm inet_ntoa isascii memchr memmove memset mkdir mkdtemp munmap ntohll newlocale ppoll putenv re_comp regcomp select setenv socket strcasecmp strcasestr strchr strcspn strdup strerror strlcat strlcpy strncasecmp strndup strnlen strrchr strsep strspn strstr strtod strtol strtold strtoq unsetenv utime vasprintf getpeereid sysctl swapctl])

# NOTE: we use AC_CHECK_LIB to get -lm into the arguments for later checks,
# so that AC_CHECK_FUNCS can detect functions in that library.
AC_CHECK_LIB([m], [sqrt])
# BSD might not have exp2, and/or log2
AC_CHECK_FUNCS([exp2 log2 exp10 log10 sin cos tan asin acos atan atan2 pow rint exp log remainder fmod round trunc floor ceil])

# Certain architectures don't really have long double, even though
# AC_CHECK_FUNCS would otherwise find the following functions.
if test "x${ac_cv_type_long_double_wider}" = "xyes" ; then
	AC_CHECK_FUNCS([exp2l log2l exp10l log10l sinl cosl tanl asinl acosl atanl atan2l powl sqrtl rintl expl logl remainderl fmodl roundl truncl floorl ceill])
fi

AC_MSG_CHECKING(for LLONG_MAX in limits.h)
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([#include <limits.h>],
		[long long foo = LLONG_MAX]),
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_LLONG_MAX], 1, [Define to 1 if limits.h includes a LLONG_MAX definition.]),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for timersub in time.h)
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([#include <sys/time.h>],
		[struct timeval *a; timersub(a, a, a);]),
	AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_TIMERSUB], 1, [Define to 1 if your system defines timersub.]),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for a version of GNU ld that supports the --dynamic-list flag)
old_LDFLAGS=${LDFLAGS}
cat >conftest.dynamics <<_ACEOF
{
	*ast_*;
};
_ACEOF
LDFLAGS="${LDFLAGS} -Wl,--dynamic-list,conftest.dynamics"
PBX_DYNAMIC_LIST=0
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([], []),
	PBX_DYNAMIC_LIST=1
	AC_MSG_RESULT(yes),
	AC_MSG_RESULT(no)
)
AC_SUBST(PBX_DYNAMIC_LIST)
LDFLAGS=${old_LDFLAGS}
rm -f conftest.dynamics

AC_CHECK_HEADER([sys/poll.h], 
   [HAS_POLL=1]
   AC_DEFINE([HAVE_SYS_POLL_H], 1, [Define to 1 if your system has working sys/poll.h]),
   )

AC_ARG_ENABLE([internal-poll],
	[AS_HELP_STRING([--enable-internal-poll],
		[Use Asterisk's poll implementation])],
	[case "${enableval}" in
		y|ye|yes) HAS_POLL="";;
		n|no) HAS_POLL="${HAS_POLL}" ;;
		*) AC_MSG_ERROR(bad value ${enableval} for --enable-internal-poll) ;;
	esac])
AC_SUBST(HAS_POLL)



# https support (in main/http.c) uses funopen on BSD systems,
# fopencookie on linux
AC_CHECK_FUNCS([funopen fopencookie])

AC_CHECK_FUNCS([inet_aton])

# check if we have IP_PKTINFO constant defined
AC_MSG_CHECKING(for IP_PKTINFO)
AC_LINK_IFELSE(
		AC_LANG_PROGRAM([#include <netinet/in.h>],
						[int pi = IP_PKTINFO;]),
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_PKTINFO], 1, [Define to 1 if your system defines IP_PKTINFO.]),
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

AC_CHECK_HEADER([byteswap.h], [AC_DEFINE_UNQUOTED([HAVE_BYTESWAP_H], 1, [Define to 1 if byteswap.h macros are available.])])

AC_MSG_CHECKING(for __swap16 variant of <sys/endian.h> byteswapping macros)
AC_LINK_IFELSE(
AC_LANG_PROGRAM([#include <sys/endian.h>], [int a = 1; int b = __swap16(a);]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_SYS_ENDIAN_SWAP16], 1, [Define to 1 if your sys/endian.h header file provides the __swap16 macro.]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for bswap16 variant of <sys/endian.h> byteswapping macros)
AC_LINK_IFELSE(
AC_LANG_PROGRAM([#include <sys/endian.h>], [int a = 1; int b = bswap16(a);]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_SYS_ENDIAN_BSWAP16], 1, [Define to 1 if your sys/endian.h header file provides the bswap16 macro.]),
AC_MSG_RESULT(no)
)

if test "${cross_compiling}" = "no";
then
  AC_CHECK_FILE(/dev/urandom, AC_DEFINE([HAVE_DEV_URANDOM], 1, [Define to 1 if your system has /dev/urandom.]))
fi

AC_MSG_CHECKING(for locale_t in locale.h)
AC_LINK_IFELSE(
AC_LANG_PROGRAM([#include <locale.h>], [locale_t lt = NULL]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_LOCALE_T_IN_LOCALE_H], 1, [Define to 1 if your system defines the locale_t type in locale.h]),
AC_MSG_RESULT(no)
  AC_MSG_CHECKING(for locale_t in xlocale.h)
  AC_LINK_IFELSE(
  AC_LANG_PROGRAM([#include <xlocale.h>], [locale_t lt = NULL]),
  AC_MSG_RESULT(yes)
  AC_DEFINE([HAVE_LOCALE_T_IN_XLOCALE_H], 1, [Define to 1 if your system defines the locale_t type in xlocale.h]),
  AC_MSG_RESULT(no)
  )
)

AC_MSG_CHECKING(for O_EVTONLY in fcntl.h)
AC_LINK_IFELSE(
AC_LANG_PROGRAM([#include <fcntl.h>], [int a = O_EVTONLY;]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_O_EVTONLY], 1, [Define to 1 if your system defines the file flag O_EVTONLY in fcntl.h]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for O_SYMLINK in fcntl.h)
AC_LINK_IFELSE(
AC_LANG_PROGRAM([#include <fcntl.h>], [int a = O_SYMLINK;]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_O_SYMLINK], 1, [Define to 1 if your system defines the file flag O_SYMLINK in fcntl.h]),
AC_MSG_RESULT(no)
)

AST_C_DEFINE_CHECK([PTHREAD_RWLOCK_INITIALIZER], [PTHREAD_RWLOCK_INITIALIZER], [pthread.h])

AC_MSG_CHECKING(for PTHREAD_RWLOCK_PREFER_WRITER_NP in pthread.h)
AC_LINK_IFELSE(
AC_LANG_PROGRAM([#include <pthread.h>], [int a = PTHREAD_RWLOCK_PREFER_WRITER_NP;]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NP], 1, [Define to 1 if your system defines PTHREAD_RWLOCK_PREFER_WRITER_NP in pthread.h]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for PTHREAD_MUTEX_RECURSIVE_NP in pthread.h)
AC_LINK_IFELSE(
AC_LANG_PROGRAM([#include <pthread.h>], [int a = PTHREAD_MUTEX_RECURSIVE_NP;]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_PTHREAD_MUTEX_RECURSIVE_NP], 1, [Define to 1 if your system defines PTHREAD_MUTEX_RECURSIVE_NP in pthread.h]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for pthread_rwlock_timedwrlock() in pthread.h)
save_LIBS="$LIBS"
save_CFLAGS="$CFLAGS"
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
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
LIBS="$save_LIBS"
CFLAGS="$save_CFLAGS"
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

AST_C_DEFINE_CHECK([PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP], [PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP], [pthread.h])

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
	AC_DEFINE([CAN_COMPARE_MUTEX_TO_INIT_VALUE], 1, [Define to 1 if your system's implementation of mutexes supports comparison of a mutex to its initializer.]),
	AC_MSG_RESULT(no)
)

#if test "${cross_compiling}" = "no";
#then
#AC_MSG_CHECKING(for working epoll support)
#AC_LINK_IFELSE(
#AC_LANG_PROGRAM([#include <sys/epoll.h>], [int res = epoll_create(10);
#					  if (res < 0)
#					     return 1;
#					  close (res);
#					  return 0;]),
#AC_MSG_RESULT(yes)
#AC_DEFINE([HAVE_EPOLL], 1, [Define to 1 if your system has working epoll support.]),
#AC_MSG_RESULT(no)
#)
#fi

AC_MSG_CHECKING(for compiler atomic operations)
AC_LINK_IFELSE(
AC_LANG_PROGRAM([], [int foo1; int foo2 = __sync_fetch_and_add(&foo1, 1);]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_GCC_ATOMICS], 1, [Define to 1 if your GCC C compiler provides atomic operations.]),
AC_MSG_RESULT(no)
)

# glibc, AFAIK, is the only C library that makes printing a NULL to a string safe.
AC_MSG_CHECKING([if your system printf is NULL-safe.])
AC_RUN_IFELSE(
	AC_LANG_PROGRAM([#include <stdio.h>],
		[printf("%s", NULL)]),
	AC_DEFINE([HAVE_NULLSAFE_PRINTF], 1, [Define to 1 if your C library can safely print NULL to string formats.])
	AC_MSG_RESULT(yes),
	AC_MSG_RESULT(no),
	# It's unlikely an embedded system will have this.
	AC_MSG_RESULT(unknown)
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
AST_GCC_ATTRIBUTE(sentinel)
AST_GCC_ATTRIBUTE(warn_unused_result)
AST_GCC_ATTRIBUTE(weak_import, [], [], PBX_WEAKREF)
AST_GCC_ATTRIBUTE(weakref, [weakref("foo")], static, PBX_WEAKREF)

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

AC_MSG_CHECKING(for -Wshadow)
if $(${CC} -Wshadow -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
	AC_MSG_RESULT(yes)
	AST_SHADOW_WARNINGS=-Wshadow
else
	AC_MSG_RESULT(no)
	AST_SHADOW_WARNINGS=
fi
AC_SUBST(AST_SHADOW_WARNINGS)

AC_MSG_CHECKING(for sysinfo)
AC_LINK_IFELSE(
        AC_LANG_PROGRAM([#include <sys/sysinfo.h>],
                        [struct sysinfo sys_info; int uptime = sys_info.uptime]),
        AC_MSG_RESULT(yes)
        AC_DEFINE([HAVE_SYSINFO], 1, [Define to 1 if your system has sysinfo support]),
        AC_MSG_RESULT(no)
)

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
	)
	AC_SEARCH_LIBS(res_9_close, resolv)
	AC_MSG_CHECKING(for res_close)
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
				[res_close();]),
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_RES_CLOSE], 1, [Define to 1 if your system has the close resolver function.]),
		AC_MSG_RESULT(no)
	),
	AC_MSG_RESULT(no)
)

AST_C_DEFINE_CHECK([GLOB_NOMAGIC], [GLOB_NOMAGIC], [glob.h])

AST_C_DEFINE_CHECK([GLOB_BRACE], [GLOB_BRACE], [glob.h])

AST_C_DEFINE_CHECK([IP_MTU_DISCOVER], [IP_MTU_DISCOVER], [netinet/in.h])

AC_CHECK_HEADER([libkern/OSAtomic.h],
                [AC_DEFINE_UNQUOTED([HAVE_OSX_ATOMICS], 1, [Define to 1 if OSX atomic operations are supported.])])

AC_CHECK_SIZEOF([int])
AC_CHECK_SIZEOF([long])
AC_CHECK_SIZEOF([long long])
AC_CHECK_SIZEOF([char *])
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

# PKGCONFIG is used in later tests
AC_CHECK_TOOL(PKGCONFIG, pkg-config, No)


# do the package library checks now

AST_EXT_LIB_CHECK([ALSA], [asound], [snd_spcm_init], [alsa/asoundlib.h], [-lm -ldl])

AST_EXT_LIB_CHECK([BFD], [bfd], [bfd_openr], [bfd.h])

AST_EXT_LIB_CHECK([CURSES], [curses], [initscr], [curses.h])

if test "x${OSARCH}" = "xlinux-gnu" ; then
  AST_EXT_LIB_CHECK([CAP], [cap], [cap_from_text], [sys/capability.h])
fi

AST_C_DEFINE_CHECK([DAHDI], [DAHDI_RESET_COUNTERS], [dahdi/user.h], [230])
AST_C_DEFINE_CHECK([DAHDI], [DAHDI_DEFAULT_MTU_MRU], [dahdi/user.h], [220])
AST_C_DEFINE_CHECK([DAHDI], [DAHDI_CODE], [dahdi/user.h], [200])

AST_C_DEFINE_CHECK([DAHDI_HALF_FULL], [DAHDI_POLICY_HALF_FULL], [dahdi/user.h])

AST_C_COMPILE_CHECK([DAHDI_LINEREVERSE_VMWI], [struct dahdi_vmwi_info booger], [dahdi/user.h], , [enhanced dahdi vmwi support])

AST_C_COMPILE_CHECK([DAHDI_ECHOCANCEL_FAX_MODE], [int foo = DAHDI_ECHOCANCEL_FAX_MODE], [dahdi/user.h])

AST_C_COMPILE_CHECK([GETIFADDRS], [struct ifaddrs *p; getifaddrs(&p)], [ifaddrs.h], , [getifaddrs() support])
AST_C_COMPILE_CHECK([TIMERFD], [timerfd_create(0,0); timerfd_settime(0,0,NULL,NULL);], [sys/timerfd.h], , [timerfd support])

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

AST_EXT_LIB_CHECK([ICONV], [iconv], [iconv_open], [iconv.h])
# Some versions of Linux package iconv in glibc
AST_EXT_LIB_CHECK([ICONV], [c], [iconv_close], [iconv.h])

# If ical.h is NOT in the libical directory, then it is of a version insufficient for us.
AST_EXT_LIB_CHECK([ICAL], [ical], [icaltimezone_get_utc_timezone], [libical/ical.h], [${PTHREAD_LIBS}], [${PTHREAD_CFLAGS}])

AST_EXT_LIB_CHECK([IKSEMEL], [iksemel], [iks_start_sasl], [iksemel.h])

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
	else
		AC_MSG_RESULT(no) 
	fi
	CPPFLAGS="${saved_cppflags}"
	LIBS="${saved_libs}"
fi

AST_EXT_LIB_CHECK([IODBC], [iodbc], [SQLConnect], [sql.h], [${PTHREAD_LIBS}], [${PTHREAD_CFLAGS}])

AST_EXT_LIB_CHECK([INOTIFY], [c], [inotify_init], [sys/inotify.h])

AST_EXT_LIB_CHECK([JACK], [jack], [jack_activate], [jack/jack.h])

# BSD (and OS X) equivalent of inotify
AST_EXT_LIB_CHECK([KQUEUE], [c], [kqueue], [sys/event.h])

# 64-bit version of kevent (from kqueue) on OS X
AC_CHECK_FUNCS([kevent64])

# Needed by unixodbc
AST_EXT_LIB_CHECK([LTDL], [ltdl], [lt_dlinit], [ltdl.h], [])

AST_EXT_LIB_CHECK([LDAP], [ldap], [ldap_initialize], [ldap.h])

AC_ARG_ENABLE([xmldoc],
	[AS_HELP_STRING([--disable-xmldoc],
		[Explicity disable XML documentation])],
	[case "${enableval}" in
		y|ye|yes) disable_xmldoc=no ;;
		n|no)  disable_xmldoc=yes ;;
		*) AC_MSG_ERROR(bad value ${enableval} for --disable-xmldoc)  ;;
	esac], [disable_xmldoc=no])

if test "${disable_xmldoc}" != "yes"; then
	AST_EXT_TOOL_CHECK([LIBXML2], [xml2-config], , ,
	[#include <libxml/tree.h>
	#include <libxml/parser.h>],
	[LIBXML_TEST_VERSION])
	if test "${PBX_LIBXML2}" != 1; then
		AC_MSG_NOTICE(*** XML documentation will not be available because the 'libxml2' development package is missing.)
		AC_MSG_NOTICE(*** Please run the 'configure' script with the '--disable-xmldoc' parameter option)
		AC_MSG_NOTICE(*** or install the 'libxml2' development package.)
		exit 1
	fi
fi

AST_EXT_LIB_CHECK([MISDN], [mISDN], [mISDN_open], [mISDNuser/mISDNlib.h])

if test "${PBX_MISDN}" = 1; then
   AST_EXT_LIB_CHECK([ISDNNET], [isdnnet], [init_manager], [mISDNuser/isdn_net.h], [-lmISDN -lpthread])
   AST_EXT_LIB_CHECK([SUPPSERV], [suppserv], [encodeFac], [mISDNuser/suppserv.h])
   AST_C_DEFINE_CHECK([MISDN_FAC_RESULT], [Fac_RESULT], [mISDNuser/suppserv.h])
   AST_C_DEFINE_CHECK([MISDN_FAC_ERROR], [Fac_ERROR], [mISDNuser/suppserv.h])
   AC_CHECK_HEADER([linux/mISDNdsp.h], [AC_DEFINE_UNQUOTED([MISDN_1_2], 1, [Build chan_misdn for mISDN 1.2 or later.])])
   AC_CHECK_MEMBER([Q931_info_t.redirect_dn], [], [PBX_MISDN=0], [#include <mISDNuser/mISDNlib.h>])
fi

AST_EXT_TOOL_CHECK([MYSQLCLIENT], [mysql_config])

AST_EXT_LIB_CHECK([NBS], [nbs], [nbs_connect], [nbs.h])

AST_EXT_LIB_CHECK([NCURSES], [ncurses], [initscr], [curses.h])

AST_EXT_TOOL_CHECK([NEON], [neon-config])

AST_EXT_TOOL_CHECK([NEON29], [neon-config], , [--libs],
[#include <ne_auth.h>],
[#ifndef NE_AUTH_NTLM
#error Need libneon >= 0.29.0
#endif])

AST_EXT_TOOL_CHECK([NETSNMP], [net-snmp-config], , [--agent-libs],
[#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>],
[int callback = snmp_register_callback(0, 0, NULL, NULL)])

AST_EXT_LIB_CHECK([NEWT], [newt], [newtBell], [newt.h])

AST_EXT_LIB_CHECK([UNIXODBC], [odbc], [SQLConnect], [sql.h], [])

AST_EXT_LIB_CHECK([OGG], [ogg], [ogg_sync_init], [])

# Non-glibc platforms require libexecinfo for backtrace support
AST_EXT_LIB_CHECK([BKTR], [execinfo], [backtrace], [execinfo.h])
# Linux, however, has backtrace directly in glibc
AST_EXT_LIB_CHECK([BKTR], [c], [backtrace], [execinfo.h])

AST_EXT_LIB_CHECK([BLUETOOTH], [bluetooth], [ba2str], [bluetooth/bluetooth.h])

# possible places for oss definitions
AST_EXT_LIB_CHECK([OSS], [ossaudio], [], [linux/soundcard.h])
AST_EXT_LIB_CHECK([OSS], [ossaudio], [], [sys/soundcard.h])
AST_EXT_LIB_CHECK([OSS], [ossaudio], [oss_ioctl_mixer], [soundcard.h])

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

      AC_MSG_CHECKING(for pg_encoding_to_char within Postgres headers)
      old_CFLAGS=${CFLAGS}
      CFLAGS="${CFLAGS} -I${PGSQL_includedir} -Werror"
      old_LDFLAGS=${LDFLAGS}
      LDFLAGS="${LDFLAGS} -L${PGSQL_libdir} -lpq -lz"
      AC_LINK_IFELSE([AC_LANG_PROGRAM([#include <libpq-fe.h>],
          [const char *foo = pg_encoding_to_char(1)])],
        [AC_MSG_RESULT(yes)
         AC_DEFINE_UNQUOTED([HAVE_PGSQL_pg_encoding_to_char], 1, [Define to indicate presence of the pg_encoding_to_char API.])],
        [AC_MSG_RESULT(no)])
      CFLAGS=${old_CFLAGS}
      LDFLAGS=${old_LDFLAGS}

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
fi

AST_EXT_LIB_CHECK([POPT], [popt], [poptStrerror], [popt.h])

AST_EXT_LIB_CHECK([PORTAUDIO], [portaudio], [Pa_GetDeviceCount], [portaudio.h])

AST_EXT_LIB_CHECK([PRI], [pri], [pri_connected_line_update], [libpri.h])
AST_EXT_LIB_CHECK([PRI_MWI], [pri], [pri_mwi_indicate], [libpri.h])
AST_EXT_LIB_CHECK([PRI_MCID], [pri], [pri_mcid_enable], [libpri.h])
AST_EXT_LIB_CHECK([PRI_CALL_WAITING], [pri], [pri_connect_ack_enable], [libpri.h])
AST_EXT_LIB_CHECK([PRI_AOC_EVENTS], [pri], [pri_aoc_events_enable], [libpri.h])
AST_EXT_LIB_CHECK([PRI_TRANSFER], [pri], [pri_transfer_enable], [libpri.h])
AST_EXT_LIB_CHECK([PRI_CCSS], [pri], [pri_cc_enable], [libpri.h])
AST_EXT_LIB_CHECK([PRI_HANGUP_FIX], [pri], [pri_hangup_fix_enable], [libpri.h])
AST_EXT_LIB_CHECK([PRI_SUBADDR], [pri], [pri_sr_set_called_subaddress], [libpri.h])
AST_EXT_LIB_CHECK([PRI_CALL_HOLD], [pri], [pri_hold_enable], [libpri.h])
AST_EXT_LIB_CHECK([PRI_CALL_REROUTING], [pri], [pri_reroute_enable], [libpri.h])
AST_EXT_LIB_CHECK([PRI_SETUP_KEYPAD], [pri], [pri_sr_set_keypad_digits], [libpri.h])

# ------------------------------------v
# TODO: The code can be changed to always include these features now.
# These features will always be present if pri_connected_line_update is available.
AST_EXT_LIB_CHECK([PRI_PROG_W_CAUSE], [pri], [pri_progress_with_cause], [libpri.h])
AST_EXT_LIB_CHECK([PRI_INBANDDISCONNECT], [pri], [pri_set_inbanddisconnect], [libpri.h])
AST_EXT_LIB_CHECK([PRI_SERVICE_MESSAGES], [pri], [pri_maintenance_service], [libpri.h])
AST_EXT_LIB_CHECK([PRI_REVERSE_CHARGE], [pri], [pri_sr_set_reversecharge], [libpri.h])
# ------------------------------------^

AST_EXT_LIB_CHECK([RESAMPLE], [resample], [resample_open], [libresample.h], [-lm])

AST_C_COMPILE_CHECK([SPANDSP], [
		#if SPANDSP_RELEASE_DATE < 20080516
		#error "spandsp 0.0.5 or greater is required"
		#endif
	], [spandsp/version.h], , [minimum version of SpanDSP])

if test "x${PBX_SPANDSP}" = "x1" ; then
	# We found the correct version in the header, now let's make sure it links
	# properly, and that libtiff is available
	PBX_SPANDSP=0
	AST_EXT_LIB_CHECK([SPANDSP], [spandsp], [span_set_message_handler], [spandsp.h], [-ltiff])
fi

if test "x${PBX_SPANDSP}" = "x1" ; then
	# We also need t38_terminal_init()
	PBX_SPANDSP=0
	AST_EXT_LIB_CHECK([SPANDSP], [spandsp], [t38_terminal_init], [spandsp.h], [-ltiff])
fi

AST_EXT_LIB_CHECK([SS7], [ss7], [ss7_pollflags], [libss7.h])

AST_EXT_LIB_CHECK([OPENR2], [openr2], [openr2_chan_new], [openr2.h])

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

AST_EXT_LIB_CHECK([LUA], [lua5.1], [luaL_newstate], [lua5.1/lua.h], [-lm]) 
if test "x${PBX_LUA}" = "x1" ; then
	if test x"${LUA_DIR}" = x; then
		LUA_INCLUDE="${LUA_INCLUDE} -I/usr/include/lua5.1"
	else
		LUA_INCLUDE="${LUA_INCLUDE} -I${LUA_DIR}/lua5.1"
	fi
fi
	
# Some distributions (like SuSE) remove the 5.1 suffix.
AST_EXT_LIB_CHECK([LUA], [lua], [luaL_register], [lua.h], [-lm]) 

AST_EXT_LIB_CHECK([RADIUS], [radiusclient-ng], [rc_read_config], [radiusclient-ng.h])

# This is a bit complex... in reality, Asterisk's AIS support is dependent on finding
# *any* implementation of AIS, not just OpenAIS. However, the configure script needs
# to know the specifics of each possible implementation, and then represent the one
# that was found as 'AIS'.

PBX_AIS=0

# OpenAIS installs its libraries into /usr/lib/openais by default, so check there

AST_EXT_LIB_CHECK([OPENAIS], [SaClm], [saClmInitialize], [openais/saClm.h], [-L/usr/lib/openais -L/usr/lib64/openais])

if test "${PBX_OPENAIS}" = 1; then
   PBX_AIS=1
   if test -n "${OPENAIS_DIR}"; then
      AIS_INCLUDE="${OPENAIS_INCLUDE}/openais"
      AIS_LIB="-lSaEvt ${OPENAIS_LIB}"
   else
      AIS_INCLUDE="-I/usr/include/openais"
      AIS_LIB="-lSaClm -lSaEvt -L/usr/lib/openais -L/usr/lib64/openais"
   fi
fi

AC_SUBST(PBX_AIS)
AC_SUBST(AIS_INCLUDE)
AC_SUBST(AIS_LIB)

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

AST_EXT_LIB_CHECK([SQLITE3], [sqlite3], [sqlite3_open], [sqlite3.h], [${PTHREAD_LIBS}], [${PTHREAD_CFLAGS}])

AST_EXT_LIB_CHECK([CRYPTO], [crypto], [AES_encrypt], [openssl/aes.h])

if test "$PBX_CRYPTO" = "1";
then
    AST_EXT_LIB_CHECK([OPENSSL], [ssl], [ssl2_connect], [openssl/ssl.h], [-lcrypto])
fi

if test "$PBX_OPENSSL" = "1";
then
    AST_CHECK_OSPTK([3], [6], [0])
fi

AST_EXT_LIB_CHECK([SRTP], [srtp], [srtp_init], [srtp/srtp.h])

if test "$PBX_SRTP" = "1";
then
    saved_libs="${LIBS}"
    saved_ldflags="${LDFLAGS}"
    saved_cflags="${CFLAGS}"
    LIBS="${LIBS} ${SRTP_LIB}"
    LDFLAGS="${LDFLAGS} -shared -fPIC"
    CFLAGS="${CFLAGS} ${SRTP_INCLUDE}"
    AC_MSG_CHECKING(for the ability of -lsrtp to be linked in a shared object)
    AC_LINK_IFELSE(
    [
        AC_LANG_PROGRAM(
            [#include <srtp/srtp.h>],
            [srtp_init();]
        )
    ],
    [ AC_MSG_RESULT(yes) ],
    [
        AC_MSG_RESULT(no)
        AC_MSG_NOTICE(***)
        AC_MSG_NOTICE(*** libsrtp could not be linked as a shared object)
        AC_MSG_NOTICE(*** try compiling libsrtp manually and configuring with)
        AC_MSG_NOTICE(*** ./configure CFLAGS=-fPIC --prefix=/usr)
        AC_MSG_NOTICE(*** replacing /usr with the prefix of your choice)
        AC_MSG_NOTICE(***)
        AC_MSG_NOTICE(*** If you do not need SRTP support re-run configure)
        AC_MSG_NOTICE(*** with the --without-srtp option.)
        exit 1
    ]
    )
    LIBS="${saved_libs}"
    LDFLAGS="${saved_ldflags}"
    CFLAGS="${saved_cflags}"
fi

AST_EXT_TOOL_CHECK([GMIME], [gmime-config], [], [], [#include <gmime/gmime.h>], [gboolean q = g_mime_check_version(0,0,0);])
if test "x${PBX_GMIME}" = "x0"; then
	# Later versions of GMime use pkg-config
	if test "x${PKGCONFIG}" = xNo; then :; else
		GMIME_INCLUDE=$(${PKGCONFIG} gmime-2.0 --cflags 2>/dev/null)
		GMIME_LIB=$(${PKGCONFIG} gmime-2.0 --libs)
		if test "x${GMIME_INCLUDE}${GMIME_LIB}" = "x"; then 
			GMIME_INCLUDE=$(${PKGCONFIG} gmime-2.2 --cflags 2>/dev/null)
			GMIME_LIB=$(${PKGCONFIG} gmime-2.2 --libs)
		fi
		if test "x${GMIME_INCLUDE}${GMIME_LIB}" != "x"; then
			PBX_GMIME=1
			AC_DEFINE([HAVE_GMIME], 1, [Define if your system has the GMIME libraries.])
		fi
	fi
fi

AST_EXT_LIB_CHECK([HOARD], [hoard], [malloc], [])

AST_EXT_LIB_CHECK([FREETDS], [sybdb], [dbinit], [sybdb.h])

AST_EXT_LIB_CHECK([TERMCAP], [termcap], [tgetent], [])

AST_EXT_LIB_CHECK([TINFO], [tinfo], [tgetent], [])

AC_CHECK_LIB([tonezone], [tone_zone_find_by_num], tonezone_does_not_need_lm=yes, tonezone_does_not_need_lm=no)

if test "${tonezone_does_not_need_lm}" = "no" ; then
  tonezone_extra="-lm"
fi

AST_EXT_LIB_CHECK([TONEZONE], [tonezone], [tone_zone_find], [dahdi/tonezone.h], [${tonezone_extra} ${DAHDI_INCLUDE}])

AST_EXT_LIB_CHECK([USB], [usb], [usb_init], [usb.h], [])

if test "${OSARCH}" = "OpenBSD";
then
	AST_EXT_LIB_CHECK([VORBIS], [vorbis], [vorbis_info_init], [vorbis/codec.h], [-lm -lvorbisenc -logg])
else
	AST_EXT_LIB_CHECK([VORBIS], [vorbis], [vorbis_info_init], [vorbis/codec.h], [-lm -lvorbisenc])
fi

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
   LIBS="${PTHREAD_LIBS} ${LIBS} -lvpb"
   CPPFLAGS="${CPPFLAGS} ${PTHREAD_CFLAGS}"
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
   fi
fi

AC_LANG_POP

AST_EXT_LIB_CHECK([ZLIB], [z], [compress], [zlib.h])

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

if test "x${PBX_UNIXODBC}" = "x1" -o "x${PBX_IODBC}" = "x1"; then
	# Does ODBC support wide characters?
	AC_MSG_CHECKING(whether ODBC has support for Unicode types)
	AC_LINK_IFELSE(
		AC_LANG_PROGRAM(
			[#include <sql.h>
#include <sqlext.h>],
			[int foo = SQL_WCHAR]),
		AC_MSG_RESULT(yes)
			AC_DEFINE([HAVE_ODBC_WCHAR], [1], [Define to 1 if your ODBC library has wide (Unicode) types.]),
		AC_MSG_RESULT(no)
	)
fi

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

# Used in res/res_pktccops
AST_C_DEFINE_CHECK([MSG_NOSIGNAL], [MSG_NOSIGNAL], [sys/socket.h])
AST_C_DEFINE_CHECK([SO_NOSIGPIPE], [SO_NOSIGPIPE], [sys/socket.h])

AST_EXT_TOOL_CHECK([SDL], [sdl-config])
AST_EXT_LIB_CHECK([SDL_IMAGE], [SDL_image], [IMG_Load], [SDL_image.h], [${SDL_LIB}], [${SDL_INCLUDE}])
AST_EXT_LIB_CHECK([FFMPEG], [avcodec], [sws_getContext], [ffmpeg/avcodec.h], [${PTHREAD_LIBS} -lz -lm], [${PTHREAD_CFLAGS}])

# possible places for video4linux version 1
AC_CHECK_HEADER([linux/videodev.h],
	[AC_DEFINE_UNQUOTED([HAVE_VIDEODEV_H], 1, [Define to 1 if your system has linux/videodev.h.])])

# possible places for X11
AST_EXT_LIB_CHECK([X11], [X11], [XOpenDisplay], [X11/Xlib.h],,, [standard_path])
AST_EXT_LIB_CHECK([X11], [X11], [XOpenDisplay], [X11/Xlib.h],, [-I/usr/X11R6/include], [X11R6])

PBX_LAUNCHD=0
if test "${cross_compiling}" = "no";
then
  AC_CHECK_FILE(/sbin/launchd, AC_DEFINE([HAVE_SBIN_LAUNCHD], 1, [Define to 1 if your system has /sbin/launchd.]))
  PBX_LAUNCHD=1
fi
AC_SUBST(PBX_LAUNCHD)

PBX_GTK2=0
if test ! "x${PKGCONFIG}" = xNo; then
   GTK2_INCLUDE=$(${PKGCONFIG} gtk+-2.0 --cflags 2>/dev/null)
   GTK2_LIB=$(${PKGCONFIG} gtk+-2.0 --libs)
   PBX_GTK2=1
   AC_DEFINE([HAVE_GTK2], 1, [Define if your system has the GTK2 libraries.])
fi
AC_SUBST(PBX_GTK2)
AC_SUBST(GTK2_INCLUDE)
AC_SUBST(GTK2_LIB)

# build a GENERIC_ODBC result based on the presence of either UnixODBC (preferred)
# or iODBC

PBX_GENERIC_ODBC=0

if test "${PBX_UNIXODBC}" = 1; then
   PBX_GENERIC_ODBC=1
   GENERIC_ODBC_LIB="${UNIXODBC_LIB}"
   GENERIC_ODBC_INCLUDE="${UNIXODBC_INCLUDE}"
elif test "${PBX_IODBC}" = 1; then
   PBX_GENERIC_ODBC=1
   GENERIC_ODBC_LIB="${IODBC_LIB}"
   GENERIC_ODBC_INCLUDE="${IODBC_INCLUDE}"
fi

AC_SUBST([GENERIC_ODBC_LIB])
AC_SUBST([GENERIC_ODBC_INCLUDE])
AC_SUBST([PBX_GENERIC_ODBC])

PBX_SYSLOG=0

if test "${ac_cv_header_syslog_h}" = "yes"; then
   # syslog facilities
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_AUTH],     [LOG_AUTH],     [syslog.h])
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_AUTHPRIV], [LOG_AUTHPRIV], [syslog.h])
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_CRON],     [LOG_CRON],     [syslog.h])
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_DAEMON],   [LOG_DAEMON],   [syslog.h])
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_FTP],      [LOG_FTP],      [syslog.h])
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_KERN],     [LOG_KERN],     [syslog.h])
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_LPR],      [LOG_LPR],      [syslog.h])
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_MAIL],     [LOG_MAIL],     [syslog.h])
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_NEWS],     [LOG_NEWS],     [syslog.h])
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_SYSLOG],   [LOG_SYSLOG],   [syslog.h])
   AST_C_DEFINE_CHECK([SYSLOG_FACILITY_LOG_UUCP],     [LOG_UUCP],     [syslog.h])
   PBX_SYSLOG=1
fi

AC_SUBST([PBX_SYSLOG])

if test -f makeopts; then
	${ac_cv_path_EGREP} 'CURSES|GTK2|OSARCH|NEWT' makeopts > makeopts.acbak
else
	touch makeopts.acbak
fi

AC_CONFIG_FILES([build_tools/menuselect-deps makeopts channels/h323/Makefile])
AST_CHECK_MANDATORY

if test -f build_tools/menuselect-deps; then
    # extract old values of all PBX_ variables from menuselect-deps
    # and preserve them so that menuselect can determine whether
    # any previously-met dependencies are no longer met and warn
    # the user appropriately
    while IFS="=:" read var val old_val; do
        eval "PBX_${var}=\${PBX_${var}}:${val}"
    done < build_tools/menuselect-deps
fi

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
AC_MSG_NOTICE( build-cpu:vendor:os: $build_cpu : $build_vendor : $build_os :) 
AC_MSG_NOTICE( host-cpu:vendor:os: $host_cpu : $host_vendor : $host_os :) 
if test "${cross_compiling}" = "yes"; then
   AC_MSG_NOTICE( Cross Compilation = YES)
fi
