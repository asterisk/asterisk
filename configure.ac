AC_PREREQ(2.60a)

AC_INIT([asterisk], [master], [https://github.com/asterisk/asterisk/issues])

# cross-compile macros
AC_CANONICAL_BUILD
AC_CANONICAL_HOST

# check existence of the package
AC_CONFIG_SRCDIR([main/asterisk.c])

AC_COPYRIGHT("Asterisk")

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
AC_CONFIG_HEADERS([include/asterisk/autoconfig.h])

# Note: AC_PROG_CC *must* be specified before AC_USE_SYSTEM_EXTENSIONS or any
# other macro that uses the C compiler, or the default order will be used.
AC_PROG_CC([gcc cc])

AC_USE_SYSTEM_EXTENSIONS	dnl note- does not work on FreeBSD

# System default paths
AC_SUBST([astsbindir],        ['${sbindir}'])dnl
AC_SUBST([astcachedir],       ['${localstatedir}/cache/asterisk'])dnl
AC_SUBST([astetcdir],         ['${sysconfdir}/asterisk'])dnl
AC_SUBST([astheaderdir],      ['${includedir}/asterisk'])dnl
AC_SUBST([astlibdir],         ['${libdir}'])dnl
AC_SUBST([astmoddir],         ['${libdir}/asterisk/modules'])dnl
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
     if test ${prefix} != 'NONE'; then
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
     netbsd*)
     ac_default_prefix=/usr/pkg
     CPPFLAGS=-I/usr/pkg/include
     LDFLAGS=-L/usr/pkg/lib
     ;;
     dragonfly*|freebsd*)
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
        astmoddir='/Library/Application Support/Asterisk/Modules'
        astvarlibdir='/Library/Application Support/Asterisk'
        astlogdir=/Library/Logs/Asterisk
        astvarrundir='/Library/Application Support/Asterisk/Run'
     fi
     AC_DEFINE([AST_POLL_COMPAT], 1, [Define to 1 if internal poll should be used.])
     AC_DEFINE([_DARWIN_UNLIMITED_SELECT], 1, [Define to 1 if running on Darwin.])
     ;;
     solaris*)
     if test ${prefix} = 'NONE'; then
        astcachedir=/var/cache/asterisk
        astetcdir=/var/etc/asterisk
        astsbindir=/opt/asterisk/sbin
        astlibdir=/opt/asterisk/lib
        astmoddir=/opt/asterisk/lib/modules
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
     dragonfly*|freebsd*)
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
     linux-gnu*)
     OSARCH=linux-gnu
     ;;
     linux-musl*)
     OSARCH=linux-musl
     ;;
     kfreebsd*-gnu)
     OSARCH=kfreebsd-gnu
     ;;
     *)
     OSARCH=${host_os}
     ;;
esac

AC_SUBST(OSARCH)
AC_SUBST(PBX_WINARCH)

AC_MSG_CHECKING(whether char is unsigned)
AC_COMPILE_IFELSE(
	[
		AC_LANG_SOURCE(
#include <limits.h>
#if CHAR_MIN == 0
#error
#endif
		)
	],[
		AC_MSG_RESULT(no)
		CONFIG_SIGNED_CHAR=""
	],[
		AC_MSG_RESULT(yes)
		CONFIG_SIGNED_CHAR="-fsigned-char"
	]
)
AC_SUBST(CONFIG_SIGNED_CHAR)

#  check for uname
AC_PATH_TOOL([UNAME], [uname], :)
if test "x${UNAME}" != "x:"; then
   PBX_OSREV=$(${UNAME} -r)
fi
AC_SUBST(PBX_OSREV)

AH_TOP([#ifndef ASTERISK_AUTOCONFIG_H
#define ASTERISK_AUTOCONFIG_H
/* IWYU pragma: private, include "asterisk.h" */

#include "asterisk/buildopts.h" /* IWYU pragma: export */])

AH_BOTTOM([#endif	/* ASTERISK_AUTOCONFIG_H */])

# cross-compile checks
if test "${cross_compiling}" = "yes";
then
   AC_CHECK_TOOL(CC, gcc, :)
   AC_CHECK_TOOL(CXX, g++, :)
   AC_CHECK_TOOL(LD, ld, :)
   AC_CHECK_TOOL(RANLIB, ranlib, :)
   CROSS_COMPILING=1
   AC_SUBST(CROSS_COMPILING)
fi

# Checks for programs.
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

GNU_LD=0
if test "x$with_gnu_ld" = "xyes" ; then
   GNU_LD=1
fi
AC_SUBST(GNU_LD)

AC_PATH_PROG([BISON], [bison], :)
AC_PATH_PROG([CMP], [cmp], :)
AC_PATH_PROG([CAT], [cat], :)
AC_PATH_PROG([CUT], [cut], :)
AC_PATH_PROG([FLEX], [flex], :)
AC_PATH_PROG([GREP], [grep], :)
AC_PATH_PROGS([PYTHON], [python2.7 python2 python python3], :)
AC_PATH_PROG([FIND], [find], :)
AC_PATH_PROG([BASENAME], [basename], :)
AC_PATH_PROG([DIRNAME], [dirname], :)
AC_PATH_PROG([SHELL], [sh], :)
AC_PATH_PROG([LN], [ln], :)
AC_PATH_PROG([DOXYGEN], [doxygen], :)
AC_PATH_PROG([DOT], [dot], :)
AC_PATH_PROG([WGET], [wget], :)
AC_PATH_PROG([CURL], [curl], :)
AC_PATH_PROG([XMLLINT], [xmllint], :)
AC_PATH_PROGS([XMLSTARLET], [xmlstarlet xml], :)
AC_PATH_PROG([BASH], [bash], :)
AC_PATH_PROG([GIT], [git], :)
AC_PATH_PROG([ALEMBIC], [alembic], :)
AC_PATH_PROG([BZIP2], [bzip2], :)
AC_PATH_PROG([TAR], [tar], :)
AC_PATH_PROG([PATCH], [patch], :)
AC_PATH_PROG([SED], [sed], :)
AC_PATH_PROG([NM], [nm], :)
AC_PATH_PROG([REALPATH], [realpath], :)


DOWNLOAD=":"
DOWNLOAD_TO_STDOUT=
DOWNLOAD_TIMEOUT=
if test "x${WGET}" != "x:"; then
  DOWNLOAD=${WGET}
  DOWNLOAD_TO_STDOUT="${WGET} -q -O-"
  DOWNLOAD_TIMEOUT='--timeout=$1'
elif test "x${CURL}" != "x:"; then
  DOWNLOAD="${CURL} -L -O --progress-bar -w \"%{url_effective}\n\""
  DOWNLOAD_TO_STDOUT="${CURL} -Ls"
  DOWNLOAD_TIMEOUT='--max-time $(or $2,$1)'
else
  AC_PATH_PROG([FETCH], [fetch], :)
  if test "x${FETCH}" != "x:"; then
    DOWNLOAD=${FETCH}
    DOWNLOAD_TO_STDOUT="${FETCH} -o-"
    DOWNLOAD_TIMEOUT='--timeout=$(or $2,$1)'
  fi
fi
AC_SUBST(DOWNLOAD)
AC_SUBST(DOWNLOAD_TO_STDOUT)
AC_SUBST(DOWNLOAD_TIMEOUT)

AC_PATH_PROG([LDCONFIG], [ldconfig], :)
AC_PATH_PROG([SHA1SUM], [sha1sum], $ac_aux_dir/build_tools/sha1sum-sh)
AC_PATH_PROG([OPENSSL], [openssl], :)

PKG_PROG_PKG_CONFIG()

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

if test "${FLEX}" = ":"; then
	PBX_FLEX=0
else
	PBX_FLEX=1
fi
AC_SUBST(PBX_FLEX)

if test "${XMLSTARLET}" = ":"; then
	PBX_XMLSTARLET=0
else
	PBX_XMLSTARLET=1
fi
AC_SUBST(PBX_XMLSTARLET)

if test "${BASH}" = ":"; then
	PBX_BASH=0
else
	PBX_BASH=1
fi
AC_SUBST(PBX_BASH)

AC_CHECK_TOOL([SOXMIX], [soxmix], :)
if test "x${SOXMIX}" != "x:"; then
	AC_DEFINE([HAVE_SOXMIX], 1, [Define to 1 if your system has soxmix application.])
fi

AC_CHECK_PROGS([MD5], [md5 md5sum gmd5sum digest])

if test "${MD5}" = "digest" ; then
   MD5="${MD5} -a md5"
fi

AX_PTHREAD

AC_LANG(C)

PBX_NO_BINARY_MODULES=0
AC_ARG_ENABLE([binary-modules],
	[AS_HELP_STRING([--disable-binary-modules],
		[Block installation of binary modules.])],
	[case "${enableval}" in
	      y|ye|yes) PBX_NO_BINARY_MODULES=0 ;;
	      n|no)  PBX_NO_BINARY_MODULES=1 ;;
	      *) AC_MSG_ERROR(bad value ${enableval} for --disable-binary-modules)  ;;
	esac])
AC_SUBST(PBX_NO_BINARY_MODULES)

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
	      strict)
	      AST_DEVMODE=yes
	      AST_DEVMODE_STRICT=yes
	      ;;
	      *) AC_MSG_ERROR(bad value ${enableval} for --enable-dev-mode)  ;;
	esac])
AC_SUBST(NOISY_BUILD)
AC_SUBST(AST_DEVMODE)
AC_SUBST(AST_DEVMODE_STRICT)

AST_OPTION_ONLY([download-cache], [AST_DOWNLOAD_CACHE], [cached sound AND external module tarfiles], [])
AST_OPTION_ONLY([sounds-cache], [SOUNDS_CACHE_DIR], [cached sound tarfiles], [])
AST_OPTION_ONLY([externals-cache], [EXTERNALS_CACHE_DIR], [cached external module tarfiles], [])

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

AST_CHECK_RAII()
AST_CHECK_STRSEP_ARRAY_BOUNDS()

JANSSON_BUNDLED=no
AC_ARG_WITH([jansson-bundled],
	[AS_HELP_STRING([--with-jansson-bundled],
		[Use bundled jansson library])],
	[case "${withval}" in
		y|ye|yes) JANSSON_BUNDLED=yes ;;
		*) JANSSON_BUNDLED=no ;;
	esac])

PJPROJECT_BUNDLED=yes
AH_TEMPLATE(m4_bpatsubst([[HAVE_PJPROJECT_BUNDLED]], [(.*)]), [Define to 1 when using the bundled pjproject.])

AC_ARG_WITH([pjproject-bundled],
	[AS_HELP_STRING([--with-pjproject-bundled],
		[Use bundled pjproject libraries (default)])],
	[case "${withval}" in
	      n|no) PJPROJECT_BUNDLED=no ;;
	      *) PJPROJECT_BUNDLED=yes ;;
	esac])

if test "${with_pjproject}" = "no" || test "${with_pjproject}" = "n" ; then
	PJPROJECT_BUNDLED=no
fi

LIBJWT_BUNDLED=no
AC_ARG_WITH([libjwt-bundled],
	[AS_HELP_STRING([--with-libjwt-bundled],
		[Use bundled libjwt library])],
	[case "${withval}" in
		y|ye|yes) LIBJWT_BUNDLED=yes ;;
		*) LIBJWT_BUNDLED=no ;;
	esac])

#
# OpenSSL stuff has to be done here because we want to pass
# any resulting CFLAGS and LDFLAGS to the bundled pjproject
# configure done in THIRD_PARTY_CONFIGURE()
#
AST_EXT_LIB_SETUP([CRYPTO], [OpenSSL Cryptography], [crypto])
AST_EXT_LIB_SETUP([OPENSSL], [OpenSSL Secure Sockets Layer], [ssl])
AST_EXT_LIB_SETUP_OPTIONAL([OPENSSL_BIO_METHOD], [OpenSSL BIO Method Support], [CRYPTO], [crypto])

# If neither --without-ssl (PBX_OPENSSL=-1) nor --without-crypto (PBX_CRYPTO=-1)
# and there were no directories atdded to --with-ssl and --with-crypto, and
# pkg-config is installed (which is should be by install_prereq).
if test "x$PKG_CONFIG" != "x" -a $PBX_OPENSSL -ge 0 -a $PBX_CRYPTO -ge 0 -a "x$OPENSSL_DIR" = "x" ; then
	AC_MSG_NOTICE(checking OPENSSL with pkg-config)
	AC_MSG_NOTICE(checking whether system openssl > 1.1.0)
	AST_PKG_CONFIG_CHECK([OPENSSL], [openssl >= 1.1.0])
	if test $PBX_OPENSSL -eq 0 ; then
		AC_MSG_NOTICE(checking whether alternate openssl11 is installed)
		AST_PKG_CONFIG_CHECK([OPENSSL], [openssl11])
	fi
	if test $PBX_OPENSSL -eq 1 ; then
		AC_DEFINE([HAVE_OPENSSL_BIO_METHOD], 1)
		PBX_OPENSSL_BIO_METHOD=1
	else
		AC_MSG_NOTICE(checking fallback system openssl)
		AST_PKG_CONFIG_CHECK([OPENSSL], [openssl])
	fi

	if test $PBX_OPENSSL -eq 1; then
		AC_DEFINE([HAVE_CRYPTO], 1)
		PBX_CRYPTO=1
	fi
else
	AC_MSG_NOTICE(Checking OPENSSL without pkg-config)
	AST_EXT_LIB_CHECK([CRYPTO], [crypto], [AES_encrypt], [openssl/aes.h])
	if test "$PBX_CRYPTO" = "1";
	then
		AST_EXT_LIB_CHECK([OPENSSL], [ssl], [SSL_connect], [openssl/ssl.h], [-lcrypto])
		AST_EXT_LIB_CHECK([OPENSSL_BIO_METHOD], [ssl], [BIO_meth_new], [openssl/ssl.h], [-lcrypto])
	fi
fi
if test $PBX_OPENSSL -eq 0 ; then
	AC_MSG_WARN(OpenSSL does not seem to be installed)
fi


THIRD_PARTY_CONFIGURE()

# AST_EXT_LIB_SETUP is used to tell configure to handle variables for
# various packages.
# $1 is the prefix for the variables in makeopts and autoconfig.h
# $2 is the short comment, $4 is the long comment
# $3 is the name used in --with- or --without- flags for configure.
#
# Package option names should be in alphabetical order
# by the --with option name (the third field),
# to make things easier for the users.

AST_EXT_LIB_SETUP([BFD], [Debug symbol decoding], [bfd])

# BKTR is used for backtrace support on platforms that do not
# have it natively.
AST_EXT_LIB_SETUP([BKTR], [Stack Backtrace], [execinfo])
AST_EXT_LIB_SETUP([BLUETOOTH], [Bluetooth], [bluetooth])
AST_EXT_LIB_SETUP([CAP], [POSIX 1.e capabilities], [cap])
AST_EXT_LIB_SETUP([CODEC2], [Codec 2 Audio Decoder/Encoder], [codec2])
AST_EXT_LIB_SETUP([COROSYNC], [Corosync], [cpg])
AST_EXT_LIB_SETUP_OPTIONAL([COROSYNC_CFG_STATE_TRACK], [A callback only in corosync 1.x], [COROSYNC], [cfg])
AST_EXT_LIB_SETUP([CRYPT], [password and data encryption], [crypt])
AST_EXT_LIB_SETUP([DAHDI], [DAHDI], [dahdi])
AST_EXT_LIB_SETUP([FFMPEG], [Ffmpeg and avcodec], [avcodec])
AST_EXT_LIB_SETUP([GSM], [External GSM], [gsm], [, use 'internal' GSM otherwise])
AST_EXT_LIB_SETUP([ILBC], [System iLBC], [ilbc], [, use 'internal' iLBC otherwise])
AST_EXT_LIB_SETUP([GTK2], [gtk2], [gtk2])
AST_EXT_LIB_SETUP([GMIME], [GMime], [gmime])
AST_EXT_LIB_SETUP([HOARD], [Hoard Memory Allocator], [hoard])
AST_EXT_LIB_SETUP([ICAL], [iCal], [ical])
AST_EXT_LIB_SETUP([ICONV], [Iconv], [iconv])
AST_EXT_LIB_SETUP([IKSEMEL], [Iksemel Jabber], [iksemel])
AST_EXT_LIB_SETUP([IMAP_TK], [UW IMAP Toolkit], [imap])
AST_EXT_LIB_SETUP([INOTIFY], [inotify support], [inotify])
AST_EXT_LIB_SETUP([IODBC], [iODBC], [iodbc])
AST_EXT_LIB_SETUP([JACK], [Jack Audio Connection Kit], [jack])
AST_EXT_LIB_SETUP([JANSSON], [Jansson JSON library], [jansson])
AST_EXT_LIB_SETUP([URIPARSER], [uriparser library], [uriparser])
AST_EXT_LIB_SETUP([KQUEUE], [kqueue support], [kqueue])
AST_EXT_LIB_SETUP([LDAP], [OpenLDAP], [ldap])
AST_LIBCURL_CHECK_CONFIG([], [7.10.1])
AST_EXT_LIB_SETUP([LIBEDIT], [NetBSD Editline library], [libedit])
AST_EXT_LIB_SETUP_OPTIONAL([LIBEDIT_IS_UNICODE], [Libedit compiled for unicode], [LIBEDIT], [libedit])
AST_EXT_LIB_SETUP([LIBJWT], [LIBJWT], [libjwt])
AST_EXT_LIB_SETUP([LIBXML2], [LibXML2], [libxml2])
AST_EXT_LIB_SETUP([LIBXSLT], [LibXSLT], [libxslt])
AST_EXT_LIB_SETUP_OPTIONAL([LIBXSLT_CLEANUP], [LibXSLT Library Cleanup Function], [LIBXSLT], [libxslt])
AST_EXT_LIB_SETUP([LUA], [Lua], [lua])
AC_ARG_VAR([LUA_VERSIONS],[A space separated list of target lua versions to test.])
AST_EXT_LIB_SETUP([MYSQLCLIENT], [MySQL client], [mysqlclient])
AST_EXT_LIB_SETUP([NEON], [neon], [neon])
AST_EXT_LIB_SETUP([NEON29], [neon29], [neon29])
AST_EXT_LIB_SETUP([NETSNMP], [Net-SNMP], [netsnmp])
AST_EXT_LIB_SETUP([NEWT], [newt], [newt])
AST_EXT_LIB_SETUP([OGG], [OGG], [ogg])
AST_EXT_LIB_SETUP([OPENR2], [MFR2], [openr2])
AST_EXT_LIB_SETUP([OPUS], [Opus], [opus])
AST_EXT_LIB_SETUP([OPUSFILE], [Opusfile], [opusfile])
AST_EXT_LIB_SETUP([PGSQL], [PostgreSQL], [postgres])
AST_EXT_LIB_SETUP([BEANSTALK], [Beanstalk Job Queue], [beanstalk])

if test "x${PBX_PJPROJECT}" != "x1" ; then
AST_EXT_LIB_SETUP([PJPROJECT], [PJPROJECT], [pjproject])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_DLG_CREATE_UAS_AND_INC_LOCK], [PJSIP Dialog Create UAS with Incremented Lock], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJ_TRANSACTION_GRP_LOCK], [PJSIP Transaction Group Lock Support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_REPLACE_MEDIA_STREAM], [PJSIP Media Stream Replacement Support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_GET_DEST_INFO], [pjsip_get_dest_info support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJ_SSL_CERT_LOAD_FROM_FILES2], [pj_ssl_cert_load_from_files2 support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_EXTERNAL_RESOLVER], [PJSIP External Resolver Support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_TLS_TRANSPORT_PROTO], [PJSIP TLS Transport proto field support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_TLS_1_1], [PJSIP TLS 1.1 Support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_TLS_1_2], [PJSIP TLS 1.2 Support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_TLS_1_3], [PJSIP TLS 1.3 Support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_EVSUB_GRP_LOCK], [PJSIP EVSUB Group Lock support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_INV_SESSION_REF], [PJSIP INVITE Session Reference Count support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_AUTH_CLT_DEINIT], [pjsip_auth_clt_deinit support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_TSX_LAYER_FIND_TSX2], [pjsip_tsx_layer_find_tsx2 support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_INV_ACCEPT_MULTIPLE_SDP_ANSWERS], [PJSIP INVITE Accept Multiple SDP Answers], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_ENDPOINT_COMPACT_FORM], [PJSIP Compact Form Support on Endpoint], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_TRANSPORT_DISABLE_CONNECTION_REUSE], [PJSIP Transport Connection Reuse Disabling], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_OAUTH_AUTHENTICATION], [PJSIP OAuth Authentication Support], [PJPROJECT], [pjsip])
AST_EXT_LIB_SETUP_OPTIONAL([PJSIP_TLS_TRANSPORT_RESTART], [PJSIP TLS Transport Restart Support], [PJPROJECT], [pjsip])
fi

AST_EXT_LIB_SETUP([POPT], [popt], [popt])
AST_EXT_LIB_SETUP([PORTAUDIO], [PortAudio], [portaudio])
AST_EXT_LIB_SETUP([PRI], [ISDN PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_SETUP_ACK_INBAND], [ISDN PRI progress inband ie in SETUP ACK], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_L2_PERSISTENCE], [ISDN Layer 2 persistence option], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_DATETIME_SEND], [ISDN PRI Date/time ie send policy], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_MWI_V2], [ISDN PRI Message Waiting Indication (Fixed)], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_DISPLAY_TEXT], [ISDN PRI user display text IE contents during call], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_MWI], [ISDN PRI Message Waiting Indication], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_MCID], [ISDN PRI Malicious Call ID], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_CALL_WAITING], [ISDN PRI call waiting supplementary service], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_AOC_EVENTS], [ISDN PRI advice of charge supplementary service events], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_TRANSFER], [ISDN PRI call transfer supplementary service], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_CCSS], [ISDN PRI call completion supplementary service], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_HANGUP_FIX], [ISDN PRI hangup fix], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_SUBADDR], [ISDN PRI subaddressing], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_CALL_HOLD], [ISDN PRI call hold], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_CALL_REROUTING], [ISDN PRI call rerouting and call deflection], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_SETUP_KEYPAD], [ISDN PRI keypad facility in SETUP], [PRI], [pri])
# ------------------------------------v
# TODO: The code can be changed to always include these features now.
# These features will always be present if pri_connected_line_update is available.
AST_EXT_LIB_SETUP_OPTIONAL([PRI_INBANDDISCONNECT], [ISDN PRI set_inbanddisconnect], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_PROG_W_CAUSE], [ISDN progress with cause], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_SERVICE_MESSAGES], [ISDN service messages], [PRI], [pri])
AST_EXT_LIB_SETUP_OPTIONAL([PRI_REVERSE_CHARGE], [ISDN reverse charge], [PRI], [pri])
# ------------------------------------^
AST_EXT_LIB_SETUP([RADIUS], [Radius Client], [radius])
AST_EXT_LIB_SETUP([FFTW3], [LIBFFTW3], [fftw3])
AST_EXT_LIB_SETUP([RESAMPLE], [LIBRESAMPLE], [resample])
AST_EXT_LIB_SETUP([SNDFILE], [libsndfile], [sndfile])
AST_EXT_LIB_SETUP([SDL], [Sdl], [sdl])
AST_EXT_LIB_SETUP([SDL_IMAGE], [Sdl Image], [SDL_image])
AST_EXT_LIB_SETUP([SPANDSP], [SPANDSP], [spandsp])
AST_EXT_LIB_SETUP([SS7], [ISDN SS7], [ss7])
AST_EXT_LIB_SETUP([SPEEX], [Speex], [speex])
AST_EXT_LIB_SETUP([SPEEX_PREPROCESS], [Speex preprocess routines], [speex])
AST_EXT_LIB_SETUP([SPEEXDSP], [SpeexDSP], [speexdsp])
AST_EXT_LIB_SETUP_DEPENDENT([SPEEX_PREPROCESS], [speex_preprocess_ctl], [], [speex])
AST_EXT_LIB_SETUP([SQLITE3], [SQLite], [sqlite3])
AST_EXT_LIB_SETUP([SRTP], [Secure RTP], [srtp])
AST_EXT_LIB_SETUP_OPTIONAL([SRTP_256], [SRTP Library AES-256 (ICM)], [SRTP], [srtp])
AST_EXT_LIB_SETUP_OPTIONAL([SRTP_192], [SRTP Library AES-192 (ICM)], [SRTP], [srtp])
AST_EXT_LIB_SETUP_OPTIONAL([SRTP_GCM], [SRTP Library AES-128 (GCM) and AES-256 (GCM)], [SRTP], [srtp])
AST_EXT_LIB_SETUP_OPTIONAL([SRTP_SHUTDOWN], [SRTP Library Shutdown Function], [SRTP], [srtp])
AST_EXT_LIB_SETUP_OPTIONAL([SRTP_GET_VERSION], [SRTP Library Version Function], [SRTP], [srtp])
AST_EXT_LIB_SETUP_OPTIONAL([RT], [Realtime functions], [rt])
AST_EXT_LIB_SETUP([FREETDS], [FreeTDS], [tds])
AST_EXT_LIB_SETUP([TIMERFD], [timerfd], [timerfd])
AST_EXT_LIB_SETUP([TONEZONE], [tonezone], [tonezone])
AST_EXT_LIB_SETUP([UNBOUND], [unbound], [unbound])
AST_EXT_LIB_SETUP([UNIXODBC], [unixODBC], [unixodbc])
AST_EXT_LIB_SETUP([VORBIS], [Vorbis], [vorbis])
AST_EXT_LIB_SETUP([X11], [X11], [x11])
AST_EXT_LIB_SETUP([ZLIB], [zlib compression], [z])

# check for basic system features and functionality before
# checking for package libraries

AC_FUNC_ALLOCA
AC_HEADER_DIRENT
if test -z $ac_header_dirent -o "$ac_header_dirent" = "no"; then
  AC_MSG_ERROR([*** Could not find dirent header that defines 'DIR'.])
fi

AC_HEADER_SYS_WAIT
if test "$ac_cv_header_sys_wait_h" != "yes"; then
  AC_MSG_ERROR([*** POSIX.1 compatible sys/wait.h is required.])
fi

AC_HEADER_RESOLV
if test "$ac_cv_header_resolv_h" != "yes"; then
  AC_MSG_ERROR([*** resolv.h is required.])
fi

# Check for headers that are unconditionally required on all platforms.  Do not use
# this to check for headers that are only needed by modules.
AC_CHECK_HEADERS([ \
  arpa/nameser.h assert.h ctype.h dlfcn.h errno.h fcntl.h float.h grp.h \
  inttypes.h limits.h locale.h math.h pwd.h netinet/in.h regex.h \
  sched.h stdarg.h stdint.h stdio.h stdlib.h string.h syslog.h \
  sys/file.h sys/ioctl.h sys/param.h sys/resource.h sys/socket.h sys/stat.h \
  sys/time.h sys/types.h sys/un.h \
  termios.h time.h unistd.h \
], [], [
  AC_MSG_ERROR([*** A required header was not found.])
])

AC_CHECK_HEADERS([arpa/inet.h libintl.h malloc.h netdb.h stddef.h strings.h sys/event.h utime.h])

# Find required NetBSD Editline library (libedit).
AST_PKG_CONFIG_CHECK(LIBEDIT, libedit)
# some platforms do not list libedit via pkg-config, for example OpenBSD 6.2
AST_EXT_LIB_CHECK([LIBEDIT], [edit], [history_init], [histedit.h], [-ltermcap])
if test "${PBX_LIBEDIT}" != 1; then
	AC_MSG_ERROR(*** Please install the 'libedit' development package.)
	exit 1
fi
AST_C_COMPILE_CHECK([LIBEDIT_IS_UNICODE], [el_rfunc_t *callback;], [histedit.h], [], [Testing for libedit unicode support])

# Find required UUID support.
#  * -luuid on Linux
#  * -le2fs-uuid on OpenBSD
#  * in libsystem on OS X
AST_EXT_LIB_CHECK([LIBUUID], [uuid], [uuid_generate_random], [uuid/uuid.h])
if test "x$LIBUUID_LIB" != "x" ; then
  UUID_INCLUDE="$LIBUUID_INCLUDE"
  UUID_LIB="$LIBUUID_LIB"
else
  AST_EXT_LIB_CHECK([E2FSUUID], [e2fs-uuid], [uuid_generate_random], [uuid/uuid.h])
  if test "x$E2FSUUID_LIB" != "x" ; then
    UUID_INCLUDE="$E2FSUUID_INCLUDE"
    UUID_LIB="$E2FSUUID_LIB"
  else
    AC_CHECK_FUNCS([uuid_generate_random], [SYSUUID=true], [SYSUUID=""])
    if test "x$SYSUUID" != "x" ; then
      UUID_INCLUDE=""
      UUID_LIB=""
    else
      AC_MSG_ERROR([*** uuid support not found (this typically means the uuid development package is missing)])
    fi
  fi
fi
AC_SUBST(UUID_INCLUDE)
AC_SUBST(UUID_LIB)

# Find required JSON support if bundled is not enabled.
if test "$JANSSON_BUNDLED" = "no" ; then
	AST_PKG_CONFIG_CHECK([JANSSON], [jansson >= 2.11])
	# json_sprintf is available in 2.11+
	AST_EXT_LIB_CHECK([JANSSON], [jansson], [json_sprintf], [jansson.h])

	if test "${PBX_JANSSON}" != 1; then
		AC_MSG_NOTICE(*** Asterisk requires libjansson >= 2.11 and no system copy was found.)
		AC_MSG_NOTICE(*** Please install the 'libjansson' development package or)
		AC_MSG_NOTICE(*** use './configure --with-jansson-bundled')
		exit 1
	fi
	JANSSON_DEFINE_JSON_INT()
else
	PBX_JANSSON=1
fi

source ./third-party/versions.mak
# Find required JWT support if bundled is not enabled.
if test "$LIBJWT_BUNDLED" = "no" ; then
	AST_PKG_CONFIG_CHECK([LIBJWT], [libjwt >= $LIBJWT_VERSION])
else
	PBX_LIBJWT=1
fi

# See if clock_gettime is in librt
AST_EXT_LIB_CHECK([RT], [rt], [clock_gettime], [])

AST_PKG_CONFIG_CHECK([LIBXML2], [libxml-2.0])
AST_EXT_TOOL_CHECK([LIBXML2], [xml2-config], , ,
        [#include <libxml/tree.h>
        #include <libxml/parser.h>],
        [LIBXML_TEST_VERSION])

if test "${PBX_LIBXML2}" != 1; then
	AC_MSG_NOTICE(*** The Asterisk menuselect tool requires the 'libxml2' development package.)
	AC_MSG_NOTICE(*** Please install the 'libxml2' development package.)
	exit 1
fi

AST_EXT_LIB_CHECK([URIPARSER], [uriparser], [uriParseUriA], [uriparser/Uri.h])

# Another mandatory item (unless it's explicitly disabled)
AC_ARG_ENABLE([xmldoc],
	[AS_HELP_STRING([--disable-xmldoc],
		[Explicitly disable XML documentation])],
	[case "${enableval}" in
		y|ye|yes) DISABLE_XMLDOC=no ;;
		n|no)  DISABLE_XMLDOC=yes ;;
		*) AC_MSG_ERROR(bad value ${enableval} for --disable-xmldoc)  ;;
	esac], [DISABLE_XMLDOC=no])

AC_SUBST([DISABLE_XMLDOC])
if test "${DISABLE_XMLDOC}" != "yes"; then
        AC_DEFINE([AST_XML_DOCS], 1, [Define to enable XML documentation.])
fi

AST_EXT_LIB_CHECK([LIBXSLT], [xslt], [xsltLoadStylesheetPI], [libxslt/xsltInternals.h], [${LIBXML2_LIB}], [${LIBXML2_INCLUDE}])
AST_EXT_LIB_CHECK([LIBXSLT_CLEANUP], [xslt], [xsltCleanupGlobals], [libxslt/xsltInternals.h], [${LIBXML2_LIB}], [${LIBXML2_INCLUDE}])

AC_ARG_ENABLE([permanent-dlopen],
	[AS_HELP_STRING([--enable-permanent-dlopen],
		[Enable when your libc has a permanent dlopen like musl])],
	[case "${enableval}" in
		y|ye|yes) PERMANENT_DLOPEN=yes ;;
		n|no)  PERMANENT_DLOPEN=no ;;
		*) AC_MSG_ERROR(bad value ${enableval} for --enable-permanent-dlopen)  ;;
	esac], [PERMANENT_DLOPEN=no])

AC_SUBST([PERMANENT_DLOPEN])
if test "${PERMANENT_DLOPEN}" = "yes"; then
	AC_DEFINE([HAVE_PERMANENT_DLOPEN], 1, [Define to support libc with permanent dlopen.])
fi

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
AC_CHECK_MEMBERS([struct ucred.uid, struct ucred.cr_uid, struct sockpeercred.uid], [], [], [#include <sys/types.h>
#include <sys/socket.h> ])
AC_CHECK_MEMBERS([struct ifreq.ifr_ifru.ifru_hwaddr], [], [], [#include <net/if.h>])
AC_STRUCT_TM
AC_C_VOLATILE
AC_CHECK_TYPES([ptrdiff_t])
stat_nsec_found=no
AC_CHECK_MEMBERS([struct stat.st_mtim, struct stat.st_mtimensec, struct stat.st_mtimespec], [stat_nsec_found=yes], [], [[#include <sys/stat.h>]])

if test "${stat_nsec_found}" != yes; then
	AC_MSG_WARN(Cannot determine nanosecond field of struct stat)
fi

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
AC_FUNC_STAT
AC_FUNC_STRCOLL
AC_FUNC_STRFTIME
AC_FUNC_STRNLEN
AC_FUNC_STRTOD
AC_FUNC_UTIME_NULL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([asprintf atexit closefrom dup2 eaccess endpwent euidaccess ffsll ftruncate getcwd gethostbyname gethostname getloadavg gettimeofday glob ioperm inet_ntoa isascii memchr memmove memset mkdir mkdtemp munmap newlocale pipe2 ppoll putenv re_comp regcomp select setenv socket strcasecmp strcasestr strchr strcspn strdup strerror strlcat strlcpy strncasecmp strndup strnlen strrchr strsep strspn strstr strtod strtol strtold strtoq unsetenv uselocale utime vasprintf getpeereid sysctl swapctl malloc_trim])

AC_MSG_CHECKING(for htonll)
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([#include <arpa/inet.h>],
		[return htonll(0);])],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_HTONLL, 1, [Define to 1 if arpa/inet.h includes a htonll definition.]),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for ntohll)
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([#include <arpa/inet.h>],
		[return ntohll(0);])],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_NTOHLL, 1, [Define to 1 if arpa/inet.h includes a ntohll definition.]),
	AC_MSG_RESULT(no)
)

# NOTE: we use AC_CHECK_LIB to get -lm into the arguments for later checks,
# so that AC_CHECK_FUNCS can detect functions in that library.
AC_CHECK_LIB([m], [sqrt])
# BSD might not have exp2, and/or log2
AC_CHECK_FUNCS([exp2 log2 exp10 log10 sin cos tan asin acos atan atan2 pow rint exp log remainder fmod round roundf trunc floor ceil])

# Certain architectures don't really have long double, even though
# AC_CHECK_FUNCS would otherwise find the following functions.
if test "x${ac_cv_type_long_double_wider}" = "xyes" ; then
	AC_CHECK_FUNCS([exp2l log2l exp10l log10l sinl cosl tanl asinl acosl atanl atan2l powl sqrtl rintl expl logl remainderl fmodl roundl truncl floorl ceill])
fi

AC_MSG_CHECKING(for LLONG_MAX in limits.h)
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([#include <limits.h>],
		[long long foo = LLONG_MAX])],
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_LLONG_MAX], 1, [Define to 1 if limits.h includes a LLONG_MAX definition.]),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for timersub in time.h)
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([#include <sys/time.h>],
		[struct timeval *a; timersub(a, a, a);])],
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
	[AC_LANG_PROGRAM([], [])],
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

AC_ARG_ENABLE([asteriskssl],
	[AS_HELP_STRING([--disable-asteriskssl],
		[Disable Asterisk's SSL wrapper library])],
	[case "${enableval}" in
		y|ye|yes) AST_ASTERISKSSL=yes ;;
		n|no)  AST_ASTERISKSSL=no ;;
		*) AC_MSG_ERROR(bad value ${enableval} for --disable-asteriskssl)  ;;
	esac], [AST_ASTERISKSSL=yes])
AC_SUBST(AST_ASTERISKSSL)

AC_CHECK_FUNCS([inet_aton])

# check if we have IP_PKTINFO constant defined
AC_MSG_CHECKING(for IP_PKTINFO)
AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([#include <netinet/in.h>],
						[int pi = IP_PKTINFO;])],
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_PKTINFO], 1, [Define to 1 if your system defines IP_PKTINFO.]),
		AC_MSG_RESULT(no)
)

# NetBSD libc ships with an internal-only incompatible symbol gethostbyname_r
AC_CHECK_DECLS([gethostbyname_r],
        [have_gethostbyname_r_public_declaration=yes],
        [have_gethostbyname_r_public_declaration=no],
        [#include <stdlib.h>
         #include <netdb.h>]
)

if test "x$have_gethostbyname_r_public_declaration" = "xyes"; then
        # some systems already have gethostbyname_r so we don't need to build ours in main/utils.c
        AC_SEARCH_LIBS(gethostbyname_r, [socket nsl])

        AC_MSG_CHECKING(for gethostbyname_r with 6 arguments)
        AC_LINK_IFELSE(
                [AC_LANG_PROGRAM([#include <stdlib.h>
                                 #include <netdb.h>],
                                [int r = gethostbyname_r((const char *)NULL, (struct hostent *)NULL, (char *)NULL, (int)0, (struct hostent **)NULL, (int *)NULL);])],
                AC_MSG_RESULT(yes)
                AC_DEFINE([HAVE_GETHOSTBYNAME_R_6], 1, [Define to 1 if your system has gethostbyname_r with 6 arguments.]),
                AC_MSG_RESULT(no)
        )

        AC_MSG_CHECKING(for gethostbyname_r with 5 arguments)
        AC_LINK_IFELSE(
                [AC_LANG_PROGRAM([#include <stdlib.h>
	                         #include <netdb.h>],
                                [int r = gethostbyname_r((const char *)NULL, (struct hostent *)NULL, (char *)NULL, (int)0, (int *)NULL);])],
                AC_MSG_RESULT(yes)
                AC_DEFINE([HAVE_GETHOSTBYNAME_R_5], 1, [Define to 1 if your system has gethostbyname_r with 5 arguments.]),
                AC_MSG_RESULT(no)
        )
fi

AC_CHECK_HEADER([byteswap.h], [AC_DEFINE_UNQUOTED([HAVE_BYTESWAP_H], 1, [Define to 1 if byteswap.h macros are available.])])

AC_MSG_CHECKING(for __swap16 variant of <sys/endian.h> byteswapping macros)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([#include <sys/endian.h>], [int a = 1; int b = __swap16(a);])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_SYS_ENDIAN_SWAP16], 1, [Define to 1 if your sys/endian.h header file provides the __swap16 macro.]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for bswap16 variant of <sys/endian.h> byteswapping macros)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([#include <sys/endian.h>], [int a = 1; int b = bswap16(a);])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_SYS_ENDIAN_BSWAP16], 1, [Define to 1 if your sys/endian.h header file provides the bswap16 macro.]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for locale_t in locale.h)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([#include <locale.h>], [locale_t lt = NULL])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_LOCALE_T_IN_LOCALE_H], 1, [Define to 1 if your system defines the locale_t type in locale.h]),
AC_MSG_RESULT(no)
  AC_MSG_CHECKING(for locale_t in xlocale.h)
  AC_LINK_IFELSE(
  [AC_LANG_PROGRAM([#include <xlocale.h>], [locale_t lt = NULL])],
  AC_MSG_RESULT(yes)
  AC_DEFINE([HAVE_LOCALE_T_IN_XLOCALE_H], 1, [Define to 1 if your system defines the locale_t type in xlocale.h]),
  AC_MSG_RESULT(no)
  )
)

AC_MSG_CHECKING(for O_EVTONLY in fcntl.h)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([#include <fcntl.h>], [int a = O_EVTONLY;])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_O_EVTONLY], 1, [Define to 1 if your system defines the file flag O_EVTONLY in fcntl.h]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for O_SYMLINK in fcntl.h)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([#include <fcntl.h>], [int a = O_SYMLINK;])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_O_SYMLINK], 1, [Define to 1 if your system defines the file flag O_SYMLINK in fcntl.h]),
AC_MSG_RESULT(no)
)

AST_C_DEFINE_CHECK([PTHREAD_RWLOCK_INITIALIZER], [PTHREAD_RWLOCK_INITIALIZER], [pthread.h])

AC_MSG_CHECKING(for PTHREAD_RWLOCK_PREFER_WRITER_NP in pthread.h)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([#include <pthread.h>], [int a = PTHREAD_RWLOCK_PREFER_WRITER_NP;])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NP], 1, [Define to 1 if your system defines PTHREAD_RWLOCK_PREFER_WRITER_NP in pthread.h]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for PTHREAD_MUTEX_RECURSIVE_NP in pthread.h)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([#include <pthread.h>], [int a = PTHREAD_MUTEX_RECURSIVE_NP;])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_PTHREAD_MUTEX_RECURSIVE_NP], 1, [Define to 1 if your system defines PTHREAD_MUTEX_RECURSIVE_NP in pthread.h]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for PTHREAD_MUTEX_ADAPTIVE_NP in pthread.h)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([#include <pthread.h>], [int a = PTHREAD_MUTEX_ADAPTIVE_NP;])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_PTHREAD_MUTEX_ADAPTIVE_NP], 1, [Define to 1 if your system defines PTHREAD_MUTEX_ADAPTIVE_NP in pthread.h]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for pthread_spinlock_t in pthread.h)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([#include <pthread.h>], [pthread_spinlock_t spin;])],
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_PTHREAD_SPINLOCK], 1, [Define to 1 if your system has pthread_spinlock_t in pthread.h]),
AC_MSG_RESULT(no)
)

save_LIBS="$LIBS"
save_CFLAGS="$CFLAGS"
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"

AC_MSG_CHECKING(for pthread_rwlock_timedwrlock() in pthread.h)
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

# Some platforms define sem_init(), but only support sem_open(). joyous.
AC_MSG_CHECKING(for working unnamed semaphores)
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([#include <semaphore.h>],
		[sem_t sem; return sem_init(&sem, 0, 0);])],
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAS_WORKING_SEMAPHORE], 1, [Define to 1 if anonymous semaphores work.]),
	AC_MSG_RESULT(no),
	AC_MSG_RESULT(cross-compile)
	AC_MSG_NOTICE([WARNING: result yes guessed because of cross compilation])
	AC_DEFINE([HAS_WORKING_SEMAPHORE], 1, [Define to 1 if anonymous semaphores work.])
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
	[AC_LANG_PROGRAM([#include <pthread.h>], [pthread_mutex_t lock;
	if ((lock) != ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		return 0;
	}
	return 0])],
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

# for FreeBSD thr_self
AC_CHECK_HEADERS([sys/thr.h])

AC_MSG_CHECKING(for compiler sync operations)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([], [int foo1; int foo2 = __sync_fetch_and_add(&foo1, 1);])],
AC_MSG_RESULT(yes)
ax_cv_have_gcc_atomics=1
AC_DEFINE([HAVE_GCC_ATOMICS], 1, [Define to 1 if your GCC C compiler provides __sync atomic operations.]),
AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for compiler atomic operations)
AC_LINK_IFELSE(
[AC_LANG_PROGRAM([], [int foo1; int foo2 = __atomic_fetch_add(&foo1, 1, __ATOMIC_RELAXED);])],
AC_MSG_RESULT(yes)
ax_cv_have_c_atomics=1
AC_DEFINE([HAVE_C_ATOMICS], 1, [Define to 1 if your C compiler provides __atomic operations.]),
AC_MSG_RESULT(no)
)

if test -z $ax_cv_have_c_atomics$ax_cv_have_gcc_atomics; then
	AC_MSG_ERROR([*** Atomic operations are not supported by your compiler.])
fi

# glibc, AFAIK, is the only C library that makes printing a NULL to a string safe.
AC_MSG_CHECKING([if your system printf is NULL-safe.])
AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([#include <stdio.h>],
		[printf("%s", NULL)])],
	AC_DEFINE([HAVE_NULLSAFE_PRINTF], 1, [Define to 1 if your C library can safely print NULL to string formats.])
	AC_MSG_RESULT(yes),
	AC_MSG_RESULT(no),
	# It's unlikely an embedded system will have this.
	AC_MSG_RESULT(unknown)
)

AC_MSG_CHECKING([if socket() accepts SOCK_NONBLOCK])
AC_RUN_IFELSE(
[AC_LANG_PROGRAM([
	#ifdef HAVE_SYS_TYPES_H
	# include <sys/types.h>
	#endif
	#ifdef HAVE_SYS_SOCKET_H
	# include <sys/socket.h>
	#endif
	], [
	   if (socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0) < 0) {
	      return 1;
	   }
	   return 0;
	])],
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_SOCK_NONBLOCK], 1, [Define to 1 if your socket() implementation can accept SOCK_NONBLOCK.]),
	AC_MSG_RESULT(no),
	AC_MSG_RESULT(cross-compile)
)

AC_MSG_CHECKING(if we can increase the maximum select-able file descriptor)
AC_RUN_IFELSE(
[AC_LANG_PROGRAM([
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
	exit(0)]])],
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_VARIABLE_FDSET], 1, [Define to 1 if your system can support larger than default select bitmasks.]),
	AC_MSG_RESULT(no),
	AC_MSG_RESULT(cross-compile)
)

if test "${ac_cv_have_variable_fdset}x" = "0x"; then
	AC_RUN_IFELSE(
		[AC_LANG_PROGRAM([
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
], [if (getuid() != 0) { exit(1); }])],
		AC_DEFINE([CONFIGURE_RAN_AS_ROOT], 1, [Some configure tests will unexpectedly fail if configure is run by a non-root user.  These may be able to be tested at runtime.]))
fi

AC_MSG_CHECKING([if we have usable eventfd support])
AC_RUN_IFELSE(
  [AC_LANG_PROGRAM([#include <sys/eventfd.h>],
      [return eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE) == -1;])],
  AC_MSG_RESULT(yes)
  AC_DEFINE([HAVE_EVENTFD], 1, [Define to 1 if your system supports eventfd and the EFD_NONBLOCK and EFD_SEMAPHORE flags.]),
  AC_MSG_RESULT(no),
  AC_MSG_RESULT(cross-compile)
)

AST_GCC_ATTRIBUTE(pure)
AST_GCC_ATTRIBUTE(malloc)
AST_GCC_ATTRIBUTE(const)
AST_GCC_ATTRIBUTE(unused)
AST_GCC_ATTRIBUTE(always_inline)
AST_GCC_ATTRIBUTE(deprecated)
AST_GCC_ATTRIBUTE(sentinel)
AST_GCC_ATTRIBUTE(warn_unused_result)
AST_GCC_ATTRIBUTE(may_alias)
AST_GCC_ATTRIBUTE(constructor)
if test "$ax_cv_have_func_attribute_constructor" != "1"; then
	AC_MSG_ERROR([*** Function constructor attribute is not supported by your compiler.])
fi

AST_GCC_ATTRIBUTE(destructor)
if test "$ax_cv_have_func_attribute_destructor" != "1"; then
	AC_MSG_ERROR([*** Function destructor attribute is not supported by your compiler.])
fi
AST_GCC_ATTRIBUTE(noreturn,noreturn)

AC_MSG_CHECKING(for -fsanitize=address support)
saved_sanitize_CFLAGS="${CFLAGS}"
saved_sanitize_LDFLAGS="${LDFLAGS}"
CFLAGS="-fsanitize=address -fno-omit-frame-pointer"
LDFLAGS="-fsanitize=address"
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([], [int x = 1;])],
	AC_MSG_RESULT(yes)
	[AST_ADDRESS_SANITIZER=1],
	[AST_ADDRESS_SANITIZER=0]
	AC_MSG_RESULT(no)
)
CFLAGS="${saved_sanitize_CFLAGS}"
LDFLAGS="${saved_sanitize_LDFLAGS}"
AC_SUBST(AST_ADDRESS_SANITIZER)

AC_MSG_CHECKING(for -fsanitize=thread support)
saved_sanitize_CFLAGS="${CFLAGS}"
saved_sanitize_LDFLAGS="${LDFLAGS}"
CFLAGS="-fno-omit-frame-pointer -pie -fPIE -fsanitize=thread"
LDFLAGS="-fsanitize=thread -pie -fPIE"
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([], [int x = 1;])],
	AC_MSG_RESULT(yes)
	[AST_THREAD_SANITIZER=1],
	[AST_THREAD_SANITIZER=0]
	AC_MSG_RESULT(no)
)
CFLAGS="${saved_sanitize_CFLAGS}"
LDFLAGS="${saved_sanitize_LDFLAGS}"
AC_SUBST(AST_THREAD_SANITIZER)

AC_MSG_CHECKING(for -fsanitize=leak support)
saved_sanitize_CFLAGS="${CFLAGS}"
saved_sanitize_LDFLAGS="${LDFLAGS}"
CFLAGS="-fno-omit-frame-pointer -fsanitize=leak"
LDFLAGS="-fsanitize=leak"
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([], [int x = 1;])],
	AC_MSG_RESULT(yes)
	[AST_LEAK_SANITIZER=1],
	[AST_LEAK_SANITIZER=0]
	AC_MSG_RESULT(no)
)
CFLAGS="${saved_sanitize_CFLAGS}"
LDFLAGS="${saved_sanitize_LDFLAGS}"
AC_SUBST(AST_LEAK_SANITIZER)

AC_MSG_CHECKING(for -fsanitize=undefined support)
saved_sanitize_CFLAGS="${CFLAGS}"
saved_sanitize_LDFLAGS="${LDFLAGS}"
CFLAGS="-fno-omit-frame-pointer -fsanitize=undefined"
LDFLAGS="-fsanitize=undefined"
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([], [int x = 1;])],
	AC_MSG_RESULT(yes)
	[AST_UNDEFINED_SANITIZER=1],
	[AST_UNDEFINED_SANITIZER=0]
	AC_MSG_RESULT(no)
)
CFLAGS="${saved_sanitize_CFLAGS}"
LDFLAGS="${saved_sanitize_LDFLAGS}"
AC_SUBST(AST_UNDEFINED_SANITIZER)

AC_MSG_CHECKING(for -Wdeclaration-after-statement support)
if $(${CC} -Wdeclaration-after-statement -Werror -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
	AC_MSG_RESULT(yes)
	AST_DECLARATION_AFTER_STATEMENT=-Wdeclaration-after-statement
else
	AC_MSG_RESULT(no)
	AST_DECLARATION_AFTER_STATEMENT=
fi
AC_SUBST(AST_DECLARATION_AFTER_STATEMENT)

AC_MSG_CHECKING(for -Wtrampolines support)
if $(${CC} -Wtrampolines -Werror -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
	AC_MSG_RESULT(yes)
	AST_TRAMPOLINES=-Wtrampolines
else
	AC_MSG_RESULT(no)
	AST_TRAMPOLINES=
fi
AC_SUBST(AST_TRAMPOLINES)

AC_MSG_CHECKING(for _FORTIFY_SOURCE support)
if $(${CC} -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
	AC_MSG_RESULT(yes)
	AST_FORTIFY_SOURCE="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2"
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

AC_MSG_CHECKING(for -Wno-format-truncation)
if $(${CC} -Wno-format-truncation -Werror -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
	AC_MSG_RESULT(yes)
	AST_NO_FORMAT_TRUNCATION=-Wno-format-truncation
else
	AC_MSG_RESULT(no)
	AST_NO_FORMAT_TRUNCATION=
fi
AC_SUBST(AST_NO_FORMAT_TRUNCATION)

AC_MSG_CHECKING(for -Wno-format-y2k)
if $(${CC} -Wno-format-y2k -Werror -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
	AC_MSG_RESULT(yes)
	AST_NO_FORMAT_Y2K=-Wno-format-y2k
else
	AC_MSG_RESULT(no)
	AST_NO_FORMAT_Y2K=
fi
AC_SUBST(AST_NO_FORMAT_Y2K)

AC_MSG_CHECKING(for -Wno-stringop-truncation)
if $(${CC} -Wno-stringop-truncation -Werror -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
	AC_MSG_RESULT(yes)
	AST_NO_STRINGOP_TRUNCATION=-Wno-stringop-truncation
else
	AC_MSG_RESULT(no)
	AST_NO_STRINGOP_TRUNCATION=
fi
AC_SUBST(AST_NO_STRINGOP_TRUNCATION)

AC_MSG_CHECKING(for -Wshadow)
if $(${CC} -Wshadow -Werror -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
	AC_MSG_RESULT(yes)
	AST_SHADOW_WARNINGS=-Wshadow
else
	AC_MSG_RESULT(no)
	AST_SHADOW_WARNINGS=
fi
AC_SUBST(AST_SHADOW_WARNINGS)

AC_MSG_CHECKING(for -march=native support)
if $(${CC} -march=native -S -o /dev/null -xc /dev/null > /dev/null 2>&1); then
	AC_MSG_RESULT(yes)
	AST_NATIVE_ARCH=1
else
	AC_MSG_RESULT(no)
	AST_NATIVE_ARCH=0
fi
AC_SUBST(AST_NATIVE_ARCH)


dnl Check to see if rpath should be set in LDFLAGS
AC_ARG_ENABLE(rpath,
	[AS_HELP_STRING([--disable-rpath],
			[Disables rpath linker option checking])],
	[case "${enableval}" in
		y|ye|yes) check_rpath=yes ;;
		n|no) check_rpath=no ;;
                *) AC_MSG_ERROR(bad value ${enableval} for --disable-rpath) ;;
	esac], [check_rpath=yes])

AC_MSG_CHECKING(whether to use rpath)
AST_RPATH=
if test "${OSARCH}" = "NetBSD"; then
	AST_RPATH="-Wl,-rpath,/usr/pkg/lib"
fi
if test "${check_rpath}" != yes; then
	AC_MSG_RESULT(skipped)
elif test "${prefix}" = /usr || test "${prefix}" = NONE; then
	AC_MSG_RESULT(not needed)
else
	AST_RPATH="-Wl,-rpath,${libdir}"
fi
AC_SUBST(AST_RPATH)

AC_MSG_CHECKING(for sysinfo)
AC_LINK_IFELSE(
        [AC_LANG_PROGRAM([#include <sys/sysinfo.h>],
                        [struct sysinfo sys_info; int uptime = sys_info.uptime])],
        AC_MSG_RESULT(yes)
        AC_DEFINE([HAVE_SYSINFO], 1, [Define to 1 if your system has sysinfo support]),
        AC_MSG_RESULT(no)
)

AC_SEARCH_LIBS(res_9_ninit, resolv)
AC_MSG_CHECKING(for res_ninit)
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([
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
			[
				int foo;
				foo = res_ninit(NULL);
				foo = res_nsearch(NULL, NULL, 0, 0, NULL, 0);
			])],
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_RES_NINIT], 1, [Define to 1 if your system has the re-entrant resolver functions.])
	AC_SEARCH_LIBS(res_9_ndestroy, resolv)
	AC_MSG_CHECKING(for res_ndestroy)
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([
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
				[res_ndestroy(NULL);])],
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_RES_NDESTROY], 1, [Define to 1 if your system has the ndestroy resolver function.]),
		AC_MSG_RESULT(no)
	)
	AC_SEARCH_LIBS(res_9_close, resolv)
	AC_MSG_CHECKING(for res_close)
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([
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
				[res_close();])],
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_RES_CLOSE], 1, [Define to 1 if your system has the close resolver function.]),
		AC_MSG_RESULT(no)
	)
	AC_CHECK_MEMBERS([struct __res_state._u._ext.nsaddrs], [], [], [[#include <resolv.h>]]),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for BIND_8_COMPAT required)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[
#undef BIND_8_COMPAT
#include <arpa/nameser.h>
]],
[[int x = NXDOMAIN]])],
AC_MSG_RESULT(no),
AC_MSG_RESULT(yes)
[BIND8_CFLAGS=-DBIND_8_COMPAT])
AC_SUBST(BIND8_CFLAGS)

AST_C_DEFINE_CHECK([GLOB_NOMAGIC], [GLOB_NOMAGIC], [glob.h])

AST_C_DEFINE_CHECK([GLOB_BRACE], [GLOB_BRACE], [glob.h])

AST_C_DEFINE_CHECK([RTLD_NOLOAD], [RTLD_NOLOAD], [dlfcn.h])

AST_C_DEFINE_CHECK([IP_MTU_DISCOVER], [IP_MTU_DISCOVER], [netinet/in.h])

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
elif test $ac_cv_sizeof_long = $ac_cv_sizeof_fd_set_fds_bits; then
  AC_DEFINE([TYPEOF_FD_SET_FDS_BITS], [long], [Define to a type of the same size as fd_set.fds_bits[[0]]])
elif test $ac_cv_sizeof_long_long = $ac_cv_sizeof_fd_set_fds_bits; then
  AC_DEFINE([TYPEOF_FD_SET_FDS_BITS], [long long], [Define to a type of the same size as fd_set.fds_bits[[0]]])
fi

AC_MSG_CHECKING(for dladdr in dlfcn.h)
PBX_DLADDR=0
old_LIBS=${LIBS}
LIBS="${LIBS} -ldl"
AC_LINK_IFELSE(
	[AC_LANG_PROGRAM([#define _GNU_SOURCE 1
#include <dlfcn.h>],
		[dladdr((void *)0, (void *)0)]
	)],
	AC_MSG_RESULT(yes)
	PBX_DLADDR=1
	AC_SUBST([PBX_DLADDR])
	AC_DEFINE([HAVE_DLADDR], 1, [Define to 1 if your system has the dladdr() GNU extension]),
	AC_MSG_RESULT(no)
)
LIBS=${old_LIBS}

# re-check without -ldl
# Non-Linux platforms like FreeBSD and NetBSD do not need a library libdl.so.
if test "${PBX_DLADDR}" = "0"; then
	AC_MSG_CHECKING(for dladdr in dlfcn.h without -ldl)
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([#define _GNU_SOURCE 1
#include <dlfcn.h>],
			[dladdr((void *)0, (void *)0)]
		)],
		AC_MSG_RESULT(yes)
		PBX_DLADDR=1
		AC_SUBST([PBX_DLADDR])
		AC_DEFINE([HAVE_DLADDR], 1, [Define to 1 if your system has the dladdr() GNU extension]),
		AC_MSG_RESULT(no)
	)
fi


# do the package library checks now

AST_EXT_LIB_CHECK([BFD], [bfd], [bfd_openr], [bfd.h])
# Fedora/RedHat/CentOS require extra libraries
AST_EXT_LIB_CHECK([BFD], [bfd], [bfd_openr], [bfd.h], [-ldl -liberty])
# openSUSE requires -lz
AST_EXT_LIB_CHECK([BFD], [bfd], [bfd_openr], [bfd.h], [-ldl -liberty -lz])

case "${OSARCH}" in
linux*)
  AST_EXT_LIB_CHECK([CAP], [cap], [cap_from_text], [sys/capability.h])
  ;;
esac

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
         elif test "${GSM_GSM_HEADER_FOUND}" = "1" ; then
            AC_DEFINE_UNQUOTED([HAVE_GSM_GSM_HEADER], 1, [Define to indicate that gsm.h is in gsm/gsm.h])
            GSM_OK=1
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

ILBC_INTERNAL="yes"
AC_SUBST(ILBC_INTERNAL)
ILBC_SYSTEM="yes"
if test "${USE_ILBC}" != "no"; then
   if test "${ILBC_DIR}" = "internal"; then
      ILBC_SYSTEM="no"
   elif test "${ILBC_DIR}" != ""; then
      ILBC_INTERNAL="no"
   fi
   if test "${ILBC_SYSTEM}" = "yes"; then
      AST_PKG_CONFIG_CHECK(ILBC, libilbc)
      if test "$PBX_ILBC" = "1"; then
	 ILBC_INTERNAL="no"
      fi
   fi
   if test "${ILBC_INTERNAL}" = "yes"; then
      PBX_ILBC=1
   fi
fi

AST_EXT_LIB_CHECK([ICONV], [iconv], [iconv_open], [iconv.h])
# GNU libiconv #define's iconv_open to libiconv_open, so we need to search for that symbol
AST_EXT_LIB_CHECK([ICONV], [iconv], [libiconv_open], [iconv.h])
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
		IMAP_TK_DIR="`pwd`/../imap-2004g"
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
	    	[AC_LANG_PROGRAM(
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
			)],
			[ac_cv_imap_tk="yes"],
			[ac_cv_imap_tk="no"]
	   	)
		if test "${ac_cv_imap_tk}" = "yes"; then
			AC_LINK_IFELSE(
				[AC_LANG_PROGRAM(
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
				)],
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
	    			[AC_LANG_PROGRAM(
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
					)],
					[ac_cv_imap_tk="yes"],
					[ac_cv_imap_tk="no"]
	   			)
				if test "${ac_cv_imap_tk}" = "yes"; then
					AC_LINK_IFELSE(
						[AC_LANG_PROGRAM(
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
						)],
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
		imap_libs="-lcrypto -lssl -lc-client"
		imap_include="-DUSE_SYSTEM_IMAP" #Try the imap directory first
		CPPFLAGS="${CPPFLAGS} ${imap_include}"
		LIBS="${LIBS} ${imap_libs} "`echo ${imap_ldflags}`
		AC_LINK_IFELSE(
	    	[AC_LANG_PROGRAM(
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
			)],
			[ac_cv_imap_tk="yes"],
			[ac_cv_imap_tk="no"]
	   	)
		if test "${ac_cv_imap_tk}" = "yes"; then
			AC_LINK_IFELSE(
				[AC_LANG_PROGRAM(
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
				)],
				[ac_cv_imap_tk2006="yes"],
				[ac_cv_imap_tk2006="no"]
			)
		else #looking in imap directory didn't work, try c-client
			imap_ldflags=""
			imap_libs="-lcrypto -lssl -lc-client"
			imap_include="-DUSE_SYSTEM_CCLIENT"
			CPPFLAGS="${saved_cppflags}"
			LIBS="${saved_libs}"
			CPPFLAGS="${CPPFLAGS} ${imap_include}"
			LIBS="${LIBS} ${imap_libs} "`echo ${imap_ldflags}`
			AC_LINK_IFELSE(
	    		[AC_LANG_PROGRAM(
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
				)],
				[ac_cv_imap_tk="yes"],
				[ac_cv_imap_tk="no"]
	   		)
			if test "${ac_cv_imap_tk}" = "no"; then
				imap_libs="-lcrypto -lssl -lc-client4"
				LIBS="${saved_libs} ${imap_libs} "`echo ${imap_ldflags}`
				AC_LINK_IFELSE(
					[AC_LANG_PROGRAM(
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
					)],
					[ac_cv_imap_tk="yes"],
					[ac_cv_imap_tk="no"]
				)
			fi
			if test "${ac_cv_imap_tk}" = "yes"; then
				AC_LINK_IFELSE(
					[AC_LANG_PROGRAM(
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
					)],
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

AST_EXT_LIB_CHECK([LDAP], [ldap], [ldap_initialize], [ldap.h])

AST_EXT_TOOL_CHECK([MYSQLCLIENT], [mysql_config])

if test "${PBX_MYSQLCLIENT}" = 1; then
   AC_MSG_CHECKING(for MySQL client bool support)
   AC_LINK_IFELSE(
           [AC_LANG_PROGRAM([#include <mysql/mysql.h>],
                   [bool test = 1;])],
           AC_MSG_RESULT(yes)
           AC_DEFINE(HAVE_MYSQLCLIENT_BOOL, 1, [Define to 1 if mysql/mysql.h has bool defined.]),
           AC_MSG_RESULT(no)
   )
   AC_MSG_CHECKING(for MySQL client my_bool support)
   AC_LINK_IFELSE(
           [AC_LANG_PROGRAM([#include <mysql/mysql.h>],
                   [my_bool test = 1;])],
           AC_MSG_RESULT(yes)
           AC_DEFINE(HAVE_MYSQLCLIENT_MY_BOOL, 1, [Define to 1 if mysql/mysql.h has my_bool defined.]),
           AC_MSG_RESULT(no)
   )
fi

AST_EXT_TOOL_CHECK([NEON], [neon-config])

AST_EXT_TOOL_CHECK([NEON29], [neon-config], , [--libs],
[#include <ne_auth.h>],
[#ifndef NE_AUTH_NTLM
#error Need libneon >= 0.29.0
#endif])

AST_PKG_CONFIG_CHECK(NETSNMP, netsnmp-agent)
AST_EXT_TOOL_CHECK([NETSNMP], [net-snmp-config], , [--agent-libs],
[#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>],
[int callback = snmp_register_callback(0, 0, NULL, NULL)])

AST_EXT_LIB_CHECK([NEWT], [newt], [newtBell], [newt.h])

# Check that unbound is installed and the version code fragment compiles
AST_EXT_LIB_EXTRA_CHECK([UNBOUND], [unbound], [ub_ctx_delete], [unbound.h],
   [], [], [
      AC_LANG_PROGRAM( [#include <unbound.h>],
         [
            #if (UNBOUND_VERSION_MAJOR < 1 || (UNBOUND_VERSION_MAJOR == 1 && UNBOUND_VERSION_MINOR < 5 ))
            #error "Unbound version must be >= 1.5"
            #endif
         ]
      )
   ], [unbound version >= 1.5])

AST_EXT_LIB_CHECK([UNIXODBC], [odbc], [SQLConnect], [sql.h])

AST_EXT_LIB_CHECK([OGG], [ogg], [ogg_stream_init], [ogg/ogg.h])

# Non-glibc platforms require libexecinfo for backtrace support
AST_EXT_LIB_CHECK([BKTR], [execinfo], [backtrace], [execinfo.h])
# Linux, however, has backtrace directly in glibc
AST_EXT_LIB_CHECK([BKTR], [c], [backtrace], [execinfo.h])

AST_EXT_LIB_CHECK([BLUETOOTH], [bluetooth], [ba2str], [bluetooth/bluetooth.h])

AST_EXT_LIB_CHECK([BEANSTALK], [beanstalk], [bs_version], [beanstalk.h])

PG_CONFIG=":"
if test "${USE_PGSQL}" != "no"; then
   if test "x${PGSQL_DIR}" != "x"; then
      AC_PATH_TOOL([PG_CONFIG], [pg_config], :, [${PGSQL_DIR}/bin])
      if test "${PG_CONFIG}" = ":"; then
         AC_MSG_NOTICE([***])
         AC_MSG_NOTICE([*** pg_config was not found in the path you specified:])
         AC_MSG_NOTICE([*** ${PGSQL_DIR}/bin])
         AC_MSG_NOTICE([*** Either correct the installation, or run configure])
         AC_MSG_NOTICE([*** including --without-postgres])
         exit 1
      fi
   else
      AC_PATH_TOOL([PG_CONFIG], [pg_config], :)
   fi
fi
if test "x${PG_CONFIG}" != "x:"; then
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

if test "$USE_PJPROJECT" != "no" ; then
   if test "$PJPROJECT_BUNDLED" = "no" ; then
      AST_PKG_CONFIG_CHECK([PJPROJECT], [libpjproject])
      if test "x$PBX_PJPROJECT" = "x1" ; then
         AST_EXT_LIB_CHECK([PJSIP_DLG_CREATE_UAS_AND_INC_LOCK], [pjsip], [pjsip_dlg_create_uas_and_inc_lock], [pjsip.h], [$PJPROJECT_LIB], [$PJPROJECT_CFLAGS])
         AST_EXT_LIB_CHECK([PJ_TRANSACTION_GRP_LOCK], [pjsip], [pjsip_tsx_create_uac2], [pjsip.h], [$PJPROJECT_LIB], [$PJPROJECT_CFLAGS])

         saved_cppflags="${CPPFLAGS}"
         saved_libs="${LIBS}"
         CPPFLAGS="${CPPFLAGS} ${PJPROJECT_CFLAGS}"
         LIBS="${LIBS} ${PJPROJECT_LIB}"
         AST_C_COMPILE_CHECK([PJSIP_REPLACE_MEDIA_STREAM], [pjmedia_mod_offer_flag flag = PJMEDIA_SDP_NEG_ALLOW_MEDIA_CHANGE], [pjmedia.h])
         LIBS="${saved_libs}"
         CPPFLAGS="${saved_cppflags}"

         AST_EXT_LIB_CHECK([PJSIP_GET_DEST_INFO], [pjsip], [pjsip_get_dest_info], [pjsip.h], [$PJPROJECT_LIB], [$PJPROJECT_CFLAGS])
         AST_EXT_LIB_CHECK([PJ_SSL_CERT_LOAD_FROM_FILES2], [pj], [pj_ssl_cert_load_from_files2], [pjlib.h], [$PJPROJECT_LIB], [$PJPROJECT_CFLAGS])
         AST_EXT_LIB_CHECK([PJSIP_EXTERNAL_RESOLVER], [pjsip], [pjsip_endpt_set_ext_resolver], [pjsip.h], [$PJPROJECT_LIB], [$PJPROJECT_CFLAGS])

         saved_cppflags="${CPPFLAGS}"
         saved_libs="${LIBS}"
         CPPFLAGS="${CPPFLAGS} ${PJPROJECT_CFLAGS}"
         LIBS="${LIBS} ${PJPROJECT_LIB}"
         AST_C_COMPILE_CHECK([PJSIP_TLS_TRANSPORT_PROTO], [struct pjsip_tls_setting setting; int proto; proto = setting.proto;], [pjsip.h])
         AST_C_COMPILE_CHECK([PJSIP_TLS_1_1], [enum pjsip_ssl_method method = PJSIP_TLSV1_1_METHOD;], [pjsip.h])
         AST_C_COMPILE_CHECK([PJSIP_TLS_1_2], [enum pjsip_ssl_method method = PJSIP_TLSV1_2_METHOD;], [pjsip.h])
         AST_C_COMPILE_CHECK([PJSIP_TLS_1_3], [enum pjsip_ssl_method method = PJSIP_TLSV1_3_METHOD;], [pjsip.h])
         AST_C_COMPILE_CHECK([PJSIP_INV_ACCEPT_MULTIPLE_SDP_ANSWERS], [pjsip_cfg()->endpt.accept_multiple_sdp_answers = 0;], [pjsip.h])
         AST_C_COMPILE_CHECK([PJSIP_ENDPOINT_COMPACT_FORM], [pjsip_cfg()->endpt.use_compact_form = PJ_TRUE;], [pjsip.h])
         AST_C_COMPILE_CHECK([PJSIP_TRANSPORT_DISABLE_CONNECTION_REUSE], [struct pjsip_tpselector sel; sel.disable_connection_reuse = PJ_TRUE;], [pjsip.h])
         AST_C_COMPILE_CHECK([PJSIP_OAUTH_AUTHENTICATION], [struct pjsip_oauth_credential credential;], [pjsip.h])
         AC_MSG_CHECKING(for pjproject on_valid_pair callback)
         AC_LINK_IFELSE(
            [AC_LANG_PROGRAM(
               [#include <pjsip.h>
               #include <pjsip_ua.h>
               #include <pjnath.h>
               void on_valid_pair(pj_ice_sess *ice) {}
               void on_ice_complete(pj_ice_sess *ice, pj_status_t status) {}
               void on_rx_data(pj_ice_sess *ice, unsigned comp_id, unsigned transport_id, void *pkt, pj_size_t size, const pj_sockaddr_t *src_addr, unsigned src_addr_len) {}
               pj_status_t on_tx_pkt(pj_ice_sess *ice, unsigned comp_id, unsigned transport_id, const void *pkt, pj_size_t size, const pj_sockaddr_t *dst_addr, unsigned dst_addr_len) {}],
               [pj_ice_sess_cb ice_sess_cb = {
                  .on_valid_pair = on_valid_pair,
                  .on_ice_complete = on_ice_complete,
                  .on_rx_data = on_rx_data,
                  .on_tx_pkt = on_tx_pkt,
               };])],
            AC_MSG_RESULT(yes)
            AC_DEFINE(HAVE_PJPROJECT_ON_VALID_ICE_PAIR_CALLBACK, 1, [Define to 1 if on_valid_pair callback is present.]),
            AC_MSG_RESULT(no)
         )
         LIBS="${saved_libs}"
         CPPFLAGS="${saved_cppflags}"

         AST_EXT_LIB_CHECK([PJSIP_EVSUB_GRP_LOCK], [pjsip], [pjsip_evsub_add_ref], [pjsip.h], [$PJPROJECT_LIB], [$PJPROJECT_CFLAGS])
         AST_EXT_LIB_CHECK([PJSIP_INV_SESSION_REF], [pjsip], [pjsip_inv_add_ref], [pjsip.h], [$PJPROJECT_LIB], [$PJPROJECT_CFLAGS])
         AST_EXT_LIB_CHECK([PJSIP_AUTH_CLT_DEINIT], [pjsip], [pjsip_auth_clt_deinit], [pjsip.h], [$PJPROJECT_LIB], [$PJPROJECT_CFLAGS])
         AST_EXT_LIB_CHECK([PJSIP_TSX_LAYER_FIND_TSX2], [pjsip], [pjsip_tsx_layer_find_tsx2], [pjsip.h], [$PJPROJECT_LIB], [$PJPROJECT_CFLAGS])
         AST_EXT_LIB_CHECK([PJSIP_TLS_TRANSPORT_RESTART], [pjsip], [pjsip_tls_transport_restart], [pjsip.h], [$PJPROJECT_LIB], [$PJPROJECT_CFLAGS])
      fi
   fi

   if test "x$PBX_PJPROJECT" = "x1" ; then
      if test "x${AST_DEVMODE}" = "xyes" ; then
         AC_SUBST([PYTHONDEV_LIB])
         AC_SUBST([PYTHONDEV_INCLUDE])
         AST_PKG_CONFIG_CHECK([PYTHONDEV], [python-2.7])
         AST_PKG_CONFIG_CHECK([PYTHONDEV], [python2])
         AST_PKG_CONFIG_CHECK([PYTHONDEV], [python])
         # The PJProject offers a C extension to Python. That wrapper is used by the
         # Asterisk Test Framework, which requires Asterisk to be in Developer Mode.
         # However, the used wrapper (called 'PJSUA Python') is deprecated and was not
         # ported from Python 2.7 to Python 3.x. This is confirmed; it simply does not
         # build. For more see <http://www.pjsip.org/trac/wiki/Python_SIP_Tutorial>.
         # Therefore, the C libraries for Python 3 are of no help in Asterisk:
         #AST_PKG_CONFIG_CHECK([PYTHONDEV], [python3])
      fi
   fi
fi

AST_EXT_LIB_CHECK([POPT], [popt], [poptStrerror], [popt.h])

AST_PKG_CONFIG_CHECK([PORTAUDIO], [portaudio-2.0])
AST_EXT_LIB_CHECK([PORTAUDIO], [portaudio], [Pa_GetDeviceCount], [portaudio.h])

AST_EXT_LIB_CHECK([PRI], [pri], [pri_connected_line_update], [libpri.h])
if test "x$PBX_PRI" = "x1" ; then
   AST_EXT_LIB_CHECK([PRI_SETUP_ACK_INBAND], [pri], [pri_setup_ack], [libpri.h])
   AST_EXT_LIB_CHECK([PRI_L2_PERSISTENCE], [pri], [pri_persistent_layer2_option], [libpri.h])
   AST_EXT_LIB_CHECK([PRI_DATETIME_SEND], [pri], [pri_date_time_send_option], [libpri.h])
   AST_EXT_LIB_CHECK([PRI_MWI_V2], [pri], [pri_mwi_indicate_v2], [libpri.h])
   AST_EXT_LIB_CHECK([PRI_DISPLAY_TEXT], [pri], [pri_display_text], [libpri.h])
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
fi

AST_EXT_LIB_CHECK([RESAMPLE], [resample], [resample_open], [libresample.h], [-lm])
AST_EXT_LIB_CHECK([FFTW3], [fftw3], [fftw_malloc], [fftw3.h])
AST_EXT_LIB_CHECK([SNDFILE], [sndfile], [sf_open], [sndfile.h])

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

# Check for libss7 v2.0 branch compatible version.
AST_EXT_LIB_CHECK([SS7], [ss7], [ss7_set_isup_timer], [libss7.h])

AST_EXT_LIB_CHECK([OPENR2], [openr2], [openr2_chan_new], [openr2.h])

AST_EXT_LIB_CHECK([OPUS], [opus], [opus_encoder_create], [opus/opus.h])
# opusfile.h includes <opus_multistream.h> so we need to make sure that
# either $OPUS_INCLUDE or /usr/include/opus is added to the search path.
__opus_include=${OPUS_INCLUDE}
if test -z "$__opus_include" -o x"$__opus_include" = x" " ; then
	__opus_include=-I/usr/include/opus
fi
AST_EXT_LIB_CHECK([OPUSFILE], [opusfile], [op_open_callbacks], [opus/opusfile.h], [], [$__opus_include])

for ver in ${LUA_VERSIONS:-5.4 5.3 5.2 5.1}; do
	AST_EXT_LIB_CHECK([LUA], lua${ver}, [luaL_newstate], lua${ver}/lua.h, [-lm])
	if test "x${PBX_LUA}" = "x1" ; then
		if test x"${LUA_DIR}" = x; then
			LUA_INCLUDE="${LUA_INCLUDE} -I/usr/include/lua${ver}"
		else
			LUA_INCLUDE="${LUA_INCLUDE} -I${LUA_DIR}/lua${ver}"
		fi
		break;
	fi
done

# Some distributions (like openSUSE and NetBSD) remove the 5.x suffix.
if test "x${LUA_VERSIONS}" = "x"; then
	AST_EXT_LIB_CHECK([LUA], [lua], [luaL_newstate], [lua.h], [-lm])
fi

# Accept either RADIUS client library, their APIs are fully compatible,
# just different header filenames and different SONAMEs
AST_EXT_LIB_CHECK([RADIUS], [freeradius-client], [rc_read_config], [freeradius-client.h])
if test "x${PBX_RADIUS}" = "x1"; then
	AC_DEFINE(RADIUS_HEADER_STR, [<freeradius-client.h>], [Name of RADIUS library include header])
else
	AST_EXT_LIB_CHECK([RADIUS], [radiusclient-ng], [rc_read_config], [radiusclient-ng.h])
	if test "x${PBX_RADIUS}" = "x1"; then
		AC_DEFINE(RADIUS_HEADER_STR, [<radiusclient-ng.h>], [Name of RADIUS library include header])
	else
		AST_EXT_LIB_CHECK([RADIUS], [radcli], [rc_read_config], [radcli/radcli.h])
		if test "x${PBX_RADIUS}" = "x1"; then
			AC_DEFINE(RADIUS_HEADER_STR, [<radcli/radcli.h>], [Name of RADIUS library include header])
		fi
	fi
fi

AST_EXT_LIB_CHECK([CODEC2], [codec2], [codec2_create], [codec2/codec2.h])

AST_EXT_LIB_CHECK([COROSYNC], [cpg], [cpg_join], [corosync/cpg.h], [-lcpg -lcfg])
AST_EXT_LIB_CHECK([COROSYNC_CFG_STATE_TRACK], [cfg], [corosync_cfg_state_track], [corosync/cfg.h], [-lcfg])

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

AST_EXT_LIB_CHECK([SQLITE3], [sqlite3], [sqlite3_open], [sqlite3.h], [${PTHREAD_LIBS}], [${PTHREAD_CFLAGS}])

if test "${PBX_SQLITE3}" != 1; then
	AC_MSG_ERROR(*** Asterisk now uses SQLite3 for the internal Asterisk database.)
	AC_MSG_ERROR(*** Please install the SQLite3 development package.)
	exit 1
fi

# Find crypt support
# * -lcrypt on *NIX
# * in libsystem on OS X
AST_EXT_LIB_CHECK([LIBCRYPT], [crypt], [crypt], [crypt.h])
AC_CHECK_FUNC([crypt], [SYSCRYPT=true], [SYSCRYPT=""])

if test "x$LIBCRYPT_LIB" != "x" ; then
    CRYPT_LIB="$LIBCRYPT_LIB"
    CRYPT_INCLUDE="$LIBCRYPT_INCLUDE"
    AC_DEFINE([HAVE_CRYPT], [1], [Define to 1 if you have the 'crypt' function.])
elif test "x$SYSCRYPT" != "x" ; then
    CRYPT_LIB=""
    CRYPT_INCLUDE=""
    AC_DEFINE([HAVE_CRYPT], [1], [Define to 1 if you have the 'crypt' function.])
fi

AC_SUBST(CRYPT_LIB)
AC_SUBST(CRYPT_INCLUDE)

# Find crypt_r support
AC_CHECK_LIB([crypt], [crypt_r],
    [AC_DEFINE([HAVE_CRYPT_R], [1], [Define to 1 if you have the 'crypt_r' function.])])

AST_EXT_LIB_CHECK([SRTP], [srtp2], [srtp_init], [srtp2/srtp.h], [], [], [2])
AST_EXT_LIB_CHECK_SHARED([SRTP], [srtp2], [srtp_init], [srtp2/srtp.h], [], [], [], [
    AC_MSG_WARN([***])
    AC_MSG_WARN([*** libsrtp2 could not be linked as a shared object.])
    AC_MSG_WARN([*** Try compiling libsrtp2 manually. Configure libsrtp2])
    AC_MSG_WARN([*** with ./configure --prefix=/usr replacing /usr with])
    AC_MSG_WARN([*** the prefix of your choice, and then make with])
    AC_MSG_WARN([*** 'make libsrtp2.so'])
    AC_MSG_WARN([***])
    AC_MSG_WARN([*** After re-installing libsrtp2, re-run the Asterisk])
    AC_MSG_WARN([*** configure script.])
    AC_MSG_WARN([***])
    AC_MSG_WARN([*** If you do not need SRTP support re-run configure])
    AC_MSG_WARN([*** with the --without-srtp option.])
    exit 1
])

if test "x$PBX_SRTP" = x1;
then
    AST_EXT_LIB_CHECK([SRTP_256], [srtp2], [srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80])
    AST_EXT_LIB_CHECK([SRTP_192], [srtp2], [srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80])
    AST_EXT_LIB_CHECK([SRTP_GCM], [srtp2], [srtp_crypto_policy_set_aes_gcm_128_8_auth])
    AST_EXT_LIB_CHECK([SRTP_SHUTDOWN], [srtp2], [srtp_shutdown], [srtp2/srtp.h])
    AST_EXT_LIB_CHECK([SRTP_GET_VERSION], [srtp2], [srtp_get_version_string], [srtp2/srtp.h])

    # libsrtp2 removed support for PRNG, so we require OpenSSL
    if test "x$PBX_OPENSSL" != x1;
    then
        AC_MSG_WARN([***])
        AC_MSG_WARN([*** OpenSSL required when using libsrtp2, checking for libsrtp instead.])
        AC_MSG_WARN([***])
	PBX_SRTP=0
    fi
fi

if test "x$PBX_SRTP" != x1;
then
    AST_EXT_LIB_CHECK([SRTP], [srtp], [srtp_init], [srtp/srtp.h], [], [], [1])
    AST_EXT_LIB_CHECK_SHARED([SRTP], [srtp], [srtp_init], [srtp/srtp.h], [], [], [], [
        AC_MSG_WARN([***])
        AC_MSG_WARN([*** libsrtp could not be linked as a shared object.])
        AC_MSG_WARN([*** Try compiling libsrtp manually. Configure libsrtp])
        AC_MSG_WARN([*** with ./configure CFLAGS=-fPIC --prefix=/usr])
        AC_MSG_WARN([*** replacing /usr with the prefix of your choice.])
        AC_MSG_WARN([*** After re-installing libsrtp, re-run the Asterisk])
        AC_MSG_WARN([*** configure script.])
        AC_MSG_WARN([***])
        AC_MSG_WARN([*** If you do not need SRTP support re-run configure])
        AC_MSG_WARN([*** with the --without-srtp option.])
        exit 1
    ])

    if test "x$PBX_SRTP" = x1;
    then
        AST_EXT_LIB_CHECK([SRTP_256], [srtp], [crypto_policy_set_aes_cm_256_hmac_sha1_80])
        AST_EXT_LIB_CHECK([SRTP_192], [srtp], [crypto_policy_set_aes_cm_192_hmac_sha1_80])
        AST_EXT_LIB_CHECK([SRTP_GCM], [srtp], [crypto_policy_set_aes_gcm_128_8_auth])
        AST_EXT_LIB_CHECK([SRTP_SHUTDOWN], [srtp], [srtp_shutdown], [srtp/srtp.h])
        AST_EXT_LIB_CHECK([SRTP_GET_VERSION], [srtp], [srtp_get_version_string], [srtp/srtp.h])
    fi
fi

for ver in 3.0 2.6 2.4 2.2 2.0; do
	AST_PKG_CONFIG_CHECK([GMIME], gmime-$ver)
	if test "$PBX_GMIME" = 1; then
		break;
	fi
done

AST_EXT_LIB_CHECK([HOARD], [hoard], [malloc], [])

AST_EXT_LIB_CHECK([FREETDS], [sybdb], [dbinit], [sybdb.h])

AC_CHECK_LIB([tonezone], [tone_zone_find_by_num], tonezone_does_not_need_lm=yes, tonezone_does_not_need_lm=no)

if test "${tonezone_does_not_need_lm}" = "no" ; then
  tonezone_extra="-lm"
fi

AST_EXT_LIB_CHECK([TONEZONE], [tonezone], [tone_zone_find], [dahdi/tonezone.h], [${tonezone_extra} ${DAHDI_INCLUDE}])

AST_EXT_LIB_CHECK([VORBIS], [vorbis], [vorbis_info_init], [vorbis/codec.h], [-lm -lvorbisenc -lvorbisfile])
AST_C_DECLARE_CHECK([VORBIS_OPEN_CALLBACKS], [OV_CALLBACKS_NOCLOSE], [vorbis/vorbisfile.h])

AST_EXT_LIB_CHECK([ZLIB], [z], [compress], [zlib.h])

if test "x${PBX_UNIXODBC}" = "x1" -o "x${PBX_IODBC}" = "x1"; then
	# Does ODBC support wide characters?
	AC_MSG_CHECKING(whether ODBC has support for Unicode types)
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM(
			[#include <sql.h>
#include <sqlext.h>],
			[int foo = SQL_WCHAR])],
		AC_MSG_RESULT(yes)
			AC_DEFINE([HAVE_ODBC_WCHAR], [1], [Define to 1 if your ODBC library has wide (Unicode) types.]),
		AC_MSG_RESULT(no)
	)
fi

AC_CHECK_HEADER([linux/compiler.h],
                [AC_DEFINE_UNQUOTED([HAVE_LINUX_COMPILER_H], 1, [Define to 1 if your system has linux/compiler.h.])])


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
  AC_CHECK_FILE(/sbin/launchd,
    [PBX_LAUNCHD=1]
    AC_DEFINE([HAVE_SBIN_LAUNCHD], 1, [Define to 1 if your system has /sbin/launchd.]))
fi
AC_SUBST(PBX_LAUNCHD)

AST_PKG_CONFIG_CHECK([GTK2], [gtk+-2.0])

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

AC_SUBST([PBX_SYSTEMD])
AC_SUBST([SYSTEMD_LIB])
AC_SUBST([SYSTEMD_INCLUDE])
AST_PKG_CONFIG_CHECK([SYSTEMD], [libsystemd])

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

# conf_bridge (binaural rendering): check if HRIRs are available
# this check does not work when cross-compiling
if test "${cross_compiling}" = "no"; then
    AC_CHECK_FILES([bridges/bridge_softmix/include/hrirs.h])
fi

AC_CONFIG_FILES([build_tools/menuselect-deps makeopts])
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
if test "${ac_cv_path_CMP}" = ":"; then
	( cd "`pwd`/menuselect" && ./configure )
elif ${ac_cv_path_CMP} -s makeopts.acbak makeopts.acbak2; then : ; else
	( cd "`pwd`/menuselect" && ./configure )
fi

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

if test "x${AST_IODBC_FOUND}" = "xyes"; then
	echo "WARNING: iodbc libs/headers found!"
	echo
	echo "  If you plan to use ODBC functionality, you should consider"
	echo "  switching to unixodbc instead."
	echo "  See: https://issues.asterisk.org/jira/browse/ASTERISK-22459"
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
