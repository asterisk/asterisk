# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

m4_define([MENUSELECT_VERSION],
          m4_bpatsubst(m4_esyscmd([./make_version .]),
                       [\([0-9.]*\)\(\w\|\W\)*],
                       [\1]))
AC_INIT(menuselect, MENUSELECT_VERSION, www.asterisk.org)

# check existence of the package
AC_CONFIG_SRCDIR([menuselect.c])

# specify output header file
AC_CONFIG_HEADER(autoconfig.h)

AC_COPYRIGHT("Menuselect")
AC_REVISION($Revision$)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

HOST_OS=${host_os}
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

AH_TOP(
#ifndef MENUSELECT_AUTOCONFIG_H
#define MENUSELECT_AUTOCONFIG_H

#ifndef _REENTRANT
#define _REENTRANT
#endif
)

AH_BOTTOM(
#endif
)

# Checks for programs.
AC_PROG_CC
AST_CHECK_GNU_MAKE


AC_LANG(C)
AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug], [Turn on debug mode])],
	[case "${enableval}" in
		y|ye|yes) MENUSELECT_DEBUG=yes ;;
		n|no)  MENUSELECT_DEBUG=no ;;
		*) AC_MSG_ERROR(bad value ${enableval} for --enable-debug)  ;;
    esac])
AC_SUBST(MENUSELECT_DEBUG)

# Support weak symbols on a platform specific basis.  The Mac OS X
# (Darwin) support must be isolated from the other platforms because
# it has caused other platforms to fail.
#
case "${OSARCH}" in
	darwin*)
	# Allow weak symbol support on Darwin platforms only because there
	# is active community support for it.
	# However, Darwin seems to break weak symbols for each new version.
	#
	AST_GCC_ATTRIBUTE(weak_import, [], [], PBX_WEAKREF)

	# Several other platforms including Linux have GCC versions that
	# define the weak attribute.  However, this attribute is only
	# setup for use in the code by Darwin.
	AST_GCC_ATTRIBUTE(weak, [], [], PBX_WEAKREF)
	;;
	linux-gnu)
	# Primarily support weak symbols on Linux platforms.
	#
	AST_GCC_ATTRIBUTE(weakref, [weakref("foo")], static, PBX_WEAKREF)
	;;
	*)
	# Allow weak symbols on other platforms.  However, any problems
	# with this feature on other platforms must be fixed by the
	# community.
	#
	AST_GCC_ATTRIBUTE(weakref, [weakref("foo")], static, PBX_WEAKREF)
	;;
esac

AC_FUNC_ALLOCA
AC_CHECK_FUNCS([asprintf getloadavg setenv strcasestr strndup strnlen strsep unsetenv vasprintf])

# The frontend can use curses, ncurses, newt or GTK2 so check for all of them

AST_EXT_LIB_SETUP([NEWT], [newt], [newt])
AST_EXT_LIB_SETUP([CURSES], [curses], [curses])
AST_EXT_LIB_SETUP([NCURSES], [ncurses], [ncurses])
AST_EXT_LIB_SETUP([LIBXML2], [LibXML2], [libxml2])

AST_EXT_LIB_CHECK([NEWT], [newt], [newtBell], [newt.h])
AST_EXT_LIB_CHECK([CURSES], [curses], [initscr], [curses.h])
AST_EXT_LIB_CHECK([NCURSES], [ncurses], [initscr], [curses.h])
AST_EXT_TOOL_CHECK([LIBXML2], [xml2-config], , ,
        [#include <libxml/tree.h>
        #include <libxml/parser.h>],
        [LIBXML_TEST_VERSION])

if test "${PBX_LIBXML2}" != 1; then
  AC_MSG_ERROR([Could not find required 'Libxml2' development package])
fi

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

test "$silent"=yes && ac_sub_configure_args="${ac_sub_configure_args} --silent"

AC_CONFIG_FILES([makeopts])
AC_OUTPUT

AC_MSG_NOTICE(Menuselect build configuration successfully completed)
