# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

m4_define([PBX_VERSION],
          m4_bpatsubst(m4_esyscmd([build_tools/make_version .]),
                       [\([0-9.]*\)\(\w\|\W\)*],
                       [\1]))
AC_INIT(asterisk, PBX_VERSION, www.asterisk.org)

# cross-compile macros
AC_CANONICAL_BUILD
AC_CANONICAL_HOST

# check existence of the package
AC_CONFIG_SRCDIR([main/asterisk.c])

# specify output header file
AC_CONFIG_HEADER(include/asterisk/autoconfig.h)

AC_COPYRIGHT("Asterisk")
AC_REVISION($Revision$)

case "${host_os}" in
     freebsd)
     ac_default_prefix=/usr/local
     ;;
     *)
     ac_default_prefix=/usr
     if test ${sysconfdir} = '${prefix}/etc'; then
        sysconfdir=/etc
     fi
     if test ${mandir} = '${prefix}/man'; then
        mandir=/usr/share/man
     fi
     ;;
esac

if test ${localstatedir} = '${prefix}/var'; then
     localstatedir=/var
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

#  check for uname
AC_PATH_TOOL([UNAME], [uname], No)
if test ! x"${UNAME}" = xNo; then
   PBX_OSREV=$(${UNAME} -r)
fi
AC_SUBST(PBX_OSREV)

# This needs to be before any macros that use the C compiler
AC_GNU_SOURCE

AH_TOP(
#ifndef ASTERISK_AUTOCONFIG_H
#define ASTERISK_AUTOCONFIG_H

#ifndef MENUSELECT
#include "asterisk/buildopts.h"
#endif

#ifndef _REENTRANT
#define _REENTRANT
#endif
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
   AC_CHECK_TOOL(AR, ar, :)
fi

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_CPP
AC_PROG_CXXCPP
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_RANLIB
AST_CHECK_GNU_MAKE

AC_PATH_PROG([GREP], [grep], :)
AC_PATH_PROG([AR], [ar], :)
AC_PATH_PROG([FIND], [find], :)
AC_PATH_PROG([COMPRESS], [compress], :)
AC_PATH_PROG([BASENAME], [basename], :)
AC_PATH_PROG([DIRNAME], [dirname], :)
AC_PATH_PROG([SHELL], [sh], :)
AC_PATH_PROG([LN], [ln], :)
AC_PATH_PROG([DOT], [dot], :)

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
AST_EXT_LIB_SETUP([CURSES], [curses], [curses])
AST_EXT_LIB_SETUP([GNUTLS], [GNU TLS support (used for iksemel only)], [gnutls])
AST_EXT_LIB_SETUP([GSM], [GSM], [gsm], [, or 'internal'])
AST_EXT_LIB_SETUP([IKSEMEL], [Iksemel Jabber Library], [iksemel])
AST_EXT_LIB_SETUP([IMAP_TK], [UW IMAP Toolkit], [imap])
AST_EXT_LIB_SETUP([ISDNNET], [ISDN4Linux Library], [isdnnet])
AST_EXT_LIB_SETUP([KDE], [KDE], [kde])
AST_EXT_LIB_SETUP([MISDN], [mISDN User Library], [misdn])
AST_EXT_LIB_SETUP([NBS], [Network Broadcast Sound], [nbs])
AST_EXT_LIB_SETUP([NCURSES], [ncurses], [ncurses])
AST_EXT_LIB_SETUP([NETSNMP], [Net-SNMP], [netsnmp])
AST_EXT_LIB_SETUP([NEWT], [newt], [newt])
AST_EXT_LIB_SETUP([UNIXODBC], [unixODBC], [odbc])
AST_EXT_LIB_SETUP([OGG], [OGG], [ogg])
AST_EXT_LIB_SETUP([OSPTK], [OSP Toolkit], [osptk])
AST_EXT_LIB_SETUP([OSS], [Open Sound System], [oss])
AST_EXT_LIB_SETUP([POPT], [popt], [popt])
AST_EXT_LIB_SETUP([PGSQL], [PostgreSQL], [postgres])
AST_EXT_LIB_SETUP([PRI], [ISDN PRI], [pri])
AST_EXT_LIB_SETUP([PWLIB], [PWlib], [pwlib])
AST_EXT_LIB_SETUP([QT], [Qt], [qt])
AST_EXT_LIB_SETUP([RADIUS], [Radius Client], [radius])
AST_EXT_LIB_SETUP([SPEEX], [Speex], [speex])
AST_EXT_LIB_SETUP([SQLITE], [SQLite], [sqlite])
AST_EXT_LIB_SETUP([SUPPSERV], [mISDN Supplemental Services], [suppserv])
AST_EXT_LIB_SETUP([OPENSSL], [OpenSSL], [ssl])
AST_EXT_LIB_SETUP([FREETDS], [FreeTDS], [tds])
AST_EXT_LIB_SETUP([TERMCAP], [Termcap], [termcap])
AST_EXT_LIB_SETUP([TINFO], [Term Info], [tinfo])
AST_EXT_LIB_SETUP([TONEZONE], [tonezone], [tonezone])
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
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h libintl.h limits.h locale.h malloc.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/file.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h syslog.h termios.h unistd.h utime.h])

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
AC_FUNC_MKTIME
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
AC_CHECK_FUNCS([atexit bzero dup2 endpwent floor ftruncate getcwd gethostbyname gethostname gettimeofday inet_ntoa isascii localtime_r memchr memmove memset mkdir munmap pow putenv re_comp regcomp rint select setenv socket sqrt strcasecmp strchr strcspn strdup strerror strncasecmp strndup strnlen strrchr strsep strspn strstr strtol unsetenv utime strtoq strcasestr asprintf vasprintf])

AC_MSG_CHECKING(checking for compiler atomic operations)
AC_LINK_IFELSE(
AC_LANG_PROGRAM([], [int foo1; int foo2 = __sync_fetch_and_add(&foo1, 1);]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_GCC_ATOMICS], 1, [Define to 1 if your GCC C compiler provides atomic operations.]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(checking for res_ninit)
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([#include <resolv.h>],
			[int foo = res_ninit(NULL);]),
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_RES_NINIT], 1, [Define to 1 if your system has the re-entrant resolver functions.]),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(checking for RTLD_NOLOAD)
AC_LINK_IFELSE(
	AC_LANG_PROGRAM([#include <dlfcn.h>],
			[int foo = RTLD_NOLOAD;]),
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_RTLD_NOLOAD], 1, [Define to 1 if your system has a dynamic linker that supports RTLD_NOLOAD.]),
	AC_MSG_RESULT(no)
)

AST_GCC_ATTRIBUTE(pure)
AST_GCC_ATTRIBUTE(malloc)
AST_GCC_ATTRIBUTE(const)
AST_GCC_ATTRIBUTE(unused)
AST_GCC_ATTRIBUTE(always_inline)

AC_CHECK_HEADER([libkern/OSAtomic.h],
                [AC_DEFINE_UNQUOTED([HAVE_OSX_ATOMICS], 1, [Define to 1 if OSX atomic operations are supported.])])

AC_CHECK_SIZEOF(int)

# do the package library checks now

AST_EXT_LIB_CHECK([ALSA], [asound], [snd_spcm_init], [alsa/asoundlib.h], [-lm -ldl])

AST_EXT_LIB_CHECK([CURSES], [curses], [initscr], [curses.h])

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
	 GSM_LIB="-lgsm"
	 if test "x${GSM_DIR}" != "x"; then
	    GSM_LIB="${gsmlibdir} ${GSM_LIB}"
	    GSM_INCLUDE="-I${GSM_DIR}/include"
	 fi
	 PBX_GSM=1
	 GSM_INTERNAL="no"
      fi
   fi
   if test "${GSM_INTERNAL}" = "yes"; then
      PBX_GSM=1
   fi
fi

AST_EXT_LIB_CHECK([IKSEMEL], [iksemel], [iks_start_sasl], [iksemel.h])

if test "${PBX_IKSEMEL}" = 1; then
   AST_EXT_LIB_CHECK([GNUTLS], [gnutls], [gnutls_bye])
   if test "${PBX_GNUTLS}" = 1; then
      IKSEMEL_LIB="${IKSEMEL_LIB} -lgnutls -lz -lgcrypt -lnsl -lgpg-error"
   fi
fi

if test "${USE_IMAP_TK}" != "no"; then
   if test "${IMAP_TK_DIR}" = ""; then
      IMAP_TK_DIR=`pwd`"/../imap-2004g"
   fi
   AC_MSG_CHECKING(checking for UW IMAP Toolkit c-client library)
   saved_cppflags="${CPPFLAGS}"
   saved_libs="${LIBS}"
   if test -f ${IMAP_TK_DIR}/c-client/LDFLAGS ; then
      imap_ldflags=`cat ${IMAP_TK_DIR}/c-client/LDFLAGS`
   fi
   CPPFLAGS="${CPPFLAGS} -I${IMAP_TK_DIR}/c-client"
   LIBS="${LIBS} ${IMAP_TK_DIR}/c-client/c-client.a "`echo ${imap_ldflags}`
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
   if test "${ac_cv_imap_tk}" = "yes"; then
      AC_MSG_RESULT(yes) 
      IMAP_TK_LIB="${IMAP_TK_DIR}/c-client/c-client.a "`echo ${imap_ldflags}`
      IMAP_TK_INCLUDE="-I${IMAP_TK_DIR}/c-client"
      PBX_IMAP_TK=1
      AC_DEFINE([HAVE_IMAP_TK], 1, [Define if your system has the UW IMAP Toolkit c-client library.])
      if test "${ac_cv_imap_tk2006}" = "yes"; then
         AC_DEFINE([HAVE_IMAP_TK2006], 1, [Define if your system has the UW IMAP Toolkit c-client library version 2006 or greater.])
      fi
   elif test ! -z "${IMAP_TK_MANDATORY}"; then
      AC_MSG_RESULT(no) 
      AC_MSG_NOTICE(***)
      AC_MSG_NOTICE(*** The UW IMAP Toolkit installation on this system appears to be broken.)
      AC_MSG_NOTICE(*** Either correct the installation, or run configure)
      AC_MSG_NOTICE(*** including --without-imap.)
      exit 1
   else
      AC_MSG_RESULT(no) 
   fi
fi

AC_LANG_PUSH(C++)

if test "${USE_KDE}" != "no"; then
   AC_MSG_CHECKING(checking for crashHandler in -lkdecore)
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
   elif test ! -z "${KDE_MANDATORY}"; then
      AC_MSG_RESULT(no) 
      AC_MSG_NOTICE(***)
      AC_MSG_NOTICE(*** The KDE installation on this system appears to be broken.)
      AC_MSG_NOTICE(*** Either correct the installation, or run configure)
      AC_MSG_NOTICE(*** including --without-kde.)
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
fi

AST_EXT_LIB_CHECK([NBS], [nbs], [nbs_connect], [nbs.h])

AST_EXT_LIB_CHECK([NCURSES], [ncurses], [initscr], [curses.h])

NETSNMP_CONFIG=No
if test "${USE_NETSNMP}" != "no"; then	
   if test "x${NETSNMP_DIR}" != "x"; then
      AC_PATH_TOOL([NETSNMP_CONFIG], [net-snmp-config], No, [${NETSNMP_DIR}/bin])
      if test x"${NETSNMP_CONFIG}" = xNo; then
         AC_MSG_NOTICE(***)
         AC_MSG_NOTICE(*** net-snmp-config was not found in the path you specified:)
         AC_MSG_NOTICE(*** ${NETSNMP_DIR}/bin)
         AC_MSG_NOTICE(*** Either correct the installation, or run configure)
         AC_MSG_NOTICE(*** including --without-netsnmp)
         exit 1
      fi
   else
      AC_PATH_TOOL([NETSNMP_CONFIG], [net-snmp-config], No)
   fi
fi
if test x"${NETSNMP_CONFIG}" != xNo; then
   NETSNMP_libs=`net-snmp-config --agent-libs`
   
   AC_CHECK_LIB([netsnmp], [snmp_register_callback], AC_DEFINE_UNQUOTED([HAVE_NETSNMP], 1,
   [Define to indicate the Net-SNMP library]), [], ${NETSNMP_libs})

   if test "${ac_cv_lib_netsnmp_snmp_register_callback}" = "yes"; then
      NETSNMP_LIB="${NETSNMP_libs}"
      PBX_NETSNMP=1
   elif test ! -z "${NETSNMP_MANDATORY}";
   then
      AC_MSG_NOTICE(***)
      AC_MSG_NOTICE(*** The Net-SNMP installation on this system appears to be broken.)
      AC_MSG_NOTICE(*** Either correct the installation, or run configure)
      AC_MSG_NOTICE(*** including --without-netsnmp)
      exit 1
   fi
elif test ! -z "${NETSNMP_MANDATORY}";
then
   AC_MSG_NOTICE(***)
   AC_MSG_NOTICE(*** The Net-SNMP installation on this system appears to be broken.)
   AC_MSG_NOTICE(*** Either correct the installation, or run configure)
   AC_MSG_NOTICE(*** including --without-netsnmp)
   exit 1
fi

AST_EXT_LIB_CHECK([NEWT], [newt], [newtBell], [newt.h])

AST_EXT_LIB_CHECK([UNIXODBC], [odbc], [SQLConnect], [sql.h], [-lltdl])

AST_EXT_LIB_CHECK([OGG], [ogg], [ogg_sync_init], [])

AST_EXT_LIB_CHECK([OSPTK], [osptk], [OSPPCryptoDecrypt], [osp/osp.h], [-lcrypto -lssl])

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

PG_CONFIG=No
if test "${USE_PGSQL}" != "no"; then	
   if test "x${PGSQL_DIR}" != "x"; then
      AC_PATH_TOOL([PG_CONFIG], [pg_config], No, [${PGSQL_DIR}/bin])
      if test x"${PG_CONFIG}" = xNo; then
         AC_MSG_NOTICE(***)
         AC_MSG_NOTICE(*** pg_config was not found in the path you specified:)
         AC_MSG_NOTICE(*** ${PGSQL_DIR}/bin)
         AC_MSG_NOTICE(*** Either correct the installation, or run configure)
         AC_MSG_NOTICE(*** including --without-postgres)
         exit 1
      fi
   else
      AC_PATH_TOOL([PG_CONFIG], [pg_config], No)
   fi
fi
if test "${PG_CONFIG}" != No; then
   PGSQL_libdir=`pg_config --libdir`
   PGSQL_includedir=`pg_config --includedir`
   
   AC_CHECK_LIB([pq], [PQexec], AC_DEFINE_UNQUOTED([HAVE_PGSQL], 1,
   [Define to indicate the PostgreSQL library]), [], -L${PGSQL_libdir} -lz)

   if test "${ac_cv_lib_pq_PQexec}" = "yes"; then
      PGSQL_LIB="-L${PGSQL_libdir} -lpq -lz -lcrypt"
      PGSQL_INCLUDE="-I${PGSQL_includedir}"
      PBX_PGSQL=1
   elif test ! -z "${PGSQL_MANDATORY}";
   then
      AC_MSG_NOTICE(***)
      AC_MSG_NOTICE(*** The PostgreSQL installation on this system appears to be broken.)
      AC_MSG_NOTICE(*** Either correct the installation, or run configure)
      AC_MSG_NOTICE(*** including --without-postgres)
      exit 1
   fi
elif test ! -z "${PGSQL_MANDATORY}";
then
   AC_MSG_NOTICE(***)
   AC_MSG_NOTICE(*** The PostgreSQL installation on this system appears to be broken.)
   AC_MSG_NOTICE(*** Either correct the installation, or run configure)
   AC_MSG_NOTICE(*** including --without-postgres)
   exit 1
fi

AST_EXT_LIB_CHECK([POPT], [popt], [poptStrerror], [popt.h])

AST_EXT_LIB_CHECK([PRI], [pri], [pri_call], [libpri.h])

PLATFORM_PTLIB="ptlib_${OSTYPE}_${MACHTYPE}_r"
if test "${USE_PWLIB}" != "no"; then
   AC_MSG_CHECKING(checking for existence of pwlib)
	
   saved_cppflags="${CPPFLAGS}"
   saved_libs="${LIBS}"
   LIBS="${LIBS} -L${PWLIB_DIR} -l${PLATFORM_PTLIB}"
   CPPFLAGS="${CPPFLAGS} -I${PWLIB_DIR}/include"
	
   AC_LINK_IFELSE(
	[
	AC_LANG_PROGRAM(
	[#include "ptime.h"],
	[int q = PTime::IsDaylightSaving();])
	],
	[	AC_MSG_RESULT(yes) 
		ac_cv_lib_pwlib="yes" 
	],
	[	AC_MSG_RESULT(no) 
		ac_cv_lib_pwlib="no" 
	]
	)

   LIBS="${saved_libs}"
   CPPFLAGS="${saved_cppflags}"

   if test "${ac_cv_lib_pwlib}" = "yes"; then
      PWLIB_LIB="-l{PLATFORM_PWLIB}"
      if test "${PWLIB_DIR}" != ""; then
         PWLIB_LIB="-L${PWLIB_DIR}/lib ${PWLIB_LIB}"
         PWLIB_INCLUDE="-I${PWLIB_DIR}/include"
      fi
   	PBX_PWLIB=1
   	AC_DEFINE([HAVE_PWLIB], 1, [Define if your system has the pwlib libraries.])
   elif test ! -z "${PWLIB_MANDATORY}"; then
      AC_MSG_NOTICE(***)
      AC_MSG_NOTICE(*** The PWLIB installation on this system appears to be broken.)
      AC_MSG_NOTICE(*** Either correct the installation, or run configure)
      AC_MSG_NOTICE(*** including --without-pwlib)
      exit 1
   fi
fi

AC_LANG_PUSH(C++)

if test "${USE_QT}" != "no"; then
   AC_MSG_CHECKING(checking for QDate in -lqt)
   saved_libs="${LIBS}"
   saved_cppflags="${CPPFLAGS}"
   if test "x${QT_DIR}" != "x"; then
      LIBS="${LIBS} -L${QT_DIR}/lib"
      CPPFLAGS="${CPPFLAGS} -I${QT_DIR}/include"
   fi
   LIBS="${LIBS} -lqt"
   qtlib="qt"
   AC_LINK_IFELSE(
	[
		AC_LANG_PROGRAM(
		[#include <qt3/qdatetime.h>],
		[QDate date();])
	],
	[ac_cv_lib_qt_qt_date="yes"],
	[ac_cv_lib_qt_qt_date="no"])
   LIBS="${saved_libs}"
   CPPFLAGS="${saved_cppflags}"
	
   if test "${ac_cv_lib_qt_qt_date}" = "no"; then
      saved_libs="${LIBS}"
      saved_cppflags="${CPPFLAGS}"
      if test "x${QT_DIR}" != "x"; then
      	 LIBS="${LIBS} -L${QT_DIR}/lib"
	 CPPFLAGS="${CPPFLAGS} -I${QT_DIR}/include"
      fi
      LIBS="${LIBS} -lqt-mt"
      qtlib="qt-mt"	
      AC_LINK_IFELSE(
		[
			AC_LANG_PROGRAM(
			[#include <qt3/qdatetime.h>],
			[QDate date();])
		],
		[ac_cv_lib_qt_qt_date="yes"],
		[ac_cv_lib_qt_qt_date="no"])
      LIBS="${saved_libs}"
      CPPFLAGS="${saved_cppflags}"
   fi	

   if test "${ac_cv_lib_qt_qt_date}" = "yes"; then
      AC_MSG_RESULT(yes) 
   else
      AC_MSG_RESULT(no) 
   fi
	
   if test "${ac_cv_lib_qt_qt_date}" = "yes"; then
      QT_LIB="-l${qtlib}"
      if test "${QT_DIR}" != ""; then
      	 QT_LIB="-L${QT_DIR}/lib ${QT_LIB}"
	 QT_INCLUDE="-I${QT_DIR}/include"
      fi
      PBX_QT=1
      AC_DEFINE([HAVE_QT], 1, [Define if your system has the Qt library])
      AC_PATH_TOOL(QTMOC, moc, No)
   elif test ! -z "${QT_MANDATORY}"; 
   then
      AC_MSG_NOTICE(***)
      AC_MSG_NOTICE(*** The Qt installation on this system appears to be broken.)
      AC_MSG_NOTICE(*** Either correct the installation, or run configure)
      AC_MSG_NOTICE(*** including --without-qt.)
      exit 1
   fi
fi

AC_LANG_POP

AST_EXT_LIB_CHECK([RADIUS], [radiusclient-ng], [rc_read_config], [radiusclient-ng.h])

AST_EXT_LIB_CHECK([SPEEX], [speex], [speex_encode], [speex/speex.h], [-lm])

AST_EXT_LIB_CHECK([SQLITE], [sqlite], [sqlite_exec], [sqlite.h])

AST_EXT_LIB_CHECK([OPENSSL], [ssl], [ssl2_connect], [openssl/ssl.h], [-lcrypto])

AST_EXT_LIB_CHECK([FREETDS], [tds], [tds_version], [tds.h])
if test "${PBX_FREETDS}" != "0";
then
    case `grep TDS_VERSION_NO ${FREETDS_DIR:-/usr/include}/tdsver.h` in
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
  tonezone_extra="-lm"
fi

AST_EXT_LIB_CHECK([TONEZONE], [tonezone], [tone_zone_find], [zaptel/tonezone.h], [${tonezone_extra}])

AST_EXT_LIB_CHECK([VORBIS], [vorbis], [vorbis_info_init], [vorbis/codec.h], [-lm -lvorbisenc])

AC_LANG_PUSH(C++)

if test "${USE_VPB}" != "no"; then
   AC_MSG_CHECKING(checking for vpb_open in -lvpb)
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
   elif test ! -z "${VPB_MANDATORY}"; then
      AC_MSG_NOTICE(***)
      AC_MSG_NOTICE(*** The VoiceTronix (vpb) installation on this system appears to be broken.)
      AC_MSG_NOTICE(*** Either correct the installation, or run configure)
      AC_MSG_NOTICE(*** including --without-vpb.)
      exit 1
   fi
fi

AC_LANG_POP

AST_EXT_LIB_CHECK([ZLIB], [z], [compress], [zlib.h])

if test "${USE_ZAPTEL}" != "no"; then
   AC_MSG_CHECKING(checking for zt_transcode_header in zaptel.h)
   saved_cppflags="${CPPFLAGS}"
   if test "x${ZAPTEL_DIR}" != "x"; then
      CPPFLAGS="${CPPFLAGS} -I${ZAPTEL_DIR}/include"
   fi
   AC_COMPILE_IFELSE(
	[
	AC_LANG_PROGRAM(
	[#include <zaptel/zaptel.h>],
	[struct zt_transcode_header test;])
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
   elif test ! -z "${ZAPTEL_MANDATORY}"; 
   then
      AC_MSG_NOTICE(***)
      AC_MSG_NOTICE(*** The Zaptel installation on this system appears to be broken.)
      AC_MSG_NOTICE(*** Either correct the installation, or run configure)
      AC_MSG_NOTICE(*** including --without-zaptel.)
      exit 1
   fi
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
  AC_MSG_ERROR(*** termcap support not found)
fi
AC_SUBST(EDITLINE_LIB)

AC_CHECK_HEADER([h323.h], [PBX_H323=1], [PBX_H323=0])
AC_SUBST(PBX_H323)

AC_CHECK_HEADER([linux/ixjuser.h], [PBX_IXJUSER=1], [PBX_IXJUSER=0], [
				   #include <linux/version.h>
				   #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
				   #include <linux/compiler.h>
				   #endif
				   ])
AC_SUBST(PBX_IXJUSER)

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

PBX_CURL=0
AC_PATH_TOOL([CURL], [curl-config], No)
if test ! x"${CURL}" = xNo; then
# check for version
   if test "${host_os}" = "SunOS"; then
      if [[ 0x`curl-config --vernum` -ge 0x70907 ]]; then
         CURLLIB=$(${CURL} --libs)
         PBX_CURL=1
         AC_DEFINE([HAVE_CURL], 1, [Define if your system has the curl libraries.])
      fi
   else
      if [[[ 0x`curl-config --vernum` -ge 0x70907 ]]]; then
         CURLLIB=$(${CURL} --libs)
         PBX_CURL=1
         AC_DEFINE([HAVE_CURL], 1, [Define if your system has the curl libraries.])
      fi
   fi
fi
AC_SUBST(PBX_CURL)
AC_SUBST(CURLLIB)

AC_CONFIG_FILES([build_tools/menuselect-deps makeopts])
AC_OUTPUT

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

if test ${GNU_MAKE_VERSION_MAJOR} -lt 3 || test ${GNU_MAKE_VERSION_MINOR} -lt 81 ; then
   AC_MSG_NOTICE()
   AC_MSG_NOTICE(***** WARNING ******)
   AC_MSG_NOTICE(You are currently using GNU Make version ${GNU_MAKE_VERSION_MAJOR}.${GNU_MAKE_VERSION_MINOR}.)
   AC_MSG_NOTICE(The Asterisk build system uses features that may not work correctly prior to 3.81.)
   AC_MSG_NOTICE(Please consider upgrading GNU Make or you may experience problems.)
   AC_MSG_NOTICE(********************)
   AC_MSG_NOTICE()
fi
