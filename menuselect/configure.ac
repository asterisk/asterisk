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

AC_CANONICAL_HOST

HOST_OS=${host_os}
AC_SUBST(HOST_OS)

case "${host_os}" in
     *dragonfly*)
     CPPFLAGS="${CPPFLAGS} -I/usr/local/include"
     LDFLAGS="${LDFLAGS} -L/usr/local/lib"
     ;;
     *netbsd*)
     CPPFLAGS="${CPPFLAGS} -I/usr/pkg/include"
     LDFLAGS="${LDFLAGS} -L/usr/pkg/lib"
     ;;
     *bsd*)
     CPPFLAGS="${CPPFLAGS} -I/usr/local/include"
     LDFLAGS="${LDFLAGS} -L/usr/local/lib"
     ;;
esac

AH_TOP(
#ifndef MENUSELECT_AUTOCONFIG_H
#define MENUSELECT_AUTOCONFIG_H
)

AH_BOTTOM([#endif])

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

AC_FUNC_ALLOCA
AC_CHECK_FUNCS([asprintf getloadavg setenv strcasestr strndup strnlen strsep unsetenv vasprintf])

# The frontend can use curses, ncurses, newt or GTK2 so check for all of them

AST_EXT_LIB_SETUP([NEWT], [newt], [newt])
AST_EXT_LIB_SETUP([CURSES], [curses], [curses])
AST_EXT_LIB_SETUP([NCURSES], [ncurses], [ncurses])
AST_EXT_LIB_SETUP([LIBXML2], [LibXML2], [libxml2])
AST_EXT_LIB_SETUP([TINFO], [terminfo], [libtinfo])

AST_EXT_LIB_CHECK([NEWT], [newt], [newtBell], [newt.h])
AST_EXT_LIB_CHECK([NCURSES], [ncurses], [initscr], [ncurses.h])
if test "${PBX_NCURSES}" != 1; then
  # some platforms have the header in a sub-directory, for example Solaris
  AST_EXT_LIB_CHECK([NCURSES], [ncurses], [initscr], [ncurses/ncurses.h])
  if test "${PBX_NCURSES}" != 1; then
    AST_EXT_LIB_CHECK([CURSES], [curses], [initscr], [curses.h])
  else
    HAVE_NCURSES_SUBDIR=yes
  fi
fi
AC_SUBST(HAVE_NCURSES_SUBDIR)

if test "${PBX_NCURSES}" = 1; then
  if test "${HAVE_NCURSES_SUBDIR}" = "yes"; then
    AST_EXT_LIB_CHECK([TINFO], [tinfo], [keypad], [ncurses/ncurses.h])
  else
    AST_EXT_LIB_CHECK([TINFO], [tinfo], [keypad], [ncurses.h])
  fi
else
  AST_EXT_LIB_CHECK([TINFO], [tinfo], [keypad], [curses.h])
fi

AST_EXT_TOOL_CHECK([LIBXML2], [xml2-config], , ,
        [#include <libxml/tree.h>
        #include <libxml/parser.h>],
        [LIBXML_TEST_VERSION])

if test "${PBX_LIBXML2}" != 1; then
  AC_MSG_ERROR([Could not find required 'Libxml2' development package])
fi

AST_PKG_CONFIG_CHECK([GTK2], [gtk+-2.0])
AC_SUBST(PBX_GTK2)
AC_SUBST(GTK2_INCLUDE)
AC_SUBST(GTK2_LIB)

test "$silent"=yes && ac_sub_configure_args="${ac_sub_configure_args} --silent"

AC_CONFIG_FILES([makeopts])
AC_OUTPUT

AC_MSG_NOTICE(Menuselect build configuration successfully completed)
