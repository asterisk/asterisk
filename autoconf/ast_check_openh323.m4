AC_DEFUN([AST_CHECK_OPENH323], [
OPENH323_INCDIR=
OPENH323_LIBDIR=
AC_LANG_PUSH([C++])
if test "${OPENH323DIR:-unset}" != "unset" ; then
  AC_CHECK_HEADER(${OPENH323DIR}/version.h, HAS_OPENH323=1, )
fi
if test "${HAS_OPENH323:-unset}" = "unset" ; then
  AC_CHECK_HEADER(${PWLIBDIR}/../openh323/version.h, OPENH323DIR="${PWLIBDIR}/../openh323"; HAS_OPENH323=1, )
  if test "${HAS_OPENH323:-unset}" != "unset" ; then
    OPENH323DIR="${PWLIBDIR}/../openh323"
    saved_cppflags="${CPPFLAGS}"
    CPPFLAGS="${CPPFLAGS} -I${PWLIB_INCDIR}/openh323 -I${PWLIB_INCDIR}"
    AC_CHECK_HEADER(${OPENH323DIR}/include/h323.h, , OPENH323_INCDIR="${PWLIB_INCDIR}/openh323"; OPENH323_LIBDIR="${PWLIB_LIBDIR}", [#include <ptlib.h>])
    CPPFLAGS="${saved_cppflags}"
  else
    saved_cppflags="${CPPFLAGS}"
    CPPFLAGS="${CPPFLAGS} -I${HOME}/openh323/include -I${PWLIB_INCDIR}"
    AC_CHECK_HEADER(${HOME}/openh323/include/h323.h, HAS_OPENH323=1, , [#include <ptlib.h>])
    CPPFLAGS="${saved_cppflags}"
    if test "${HAS_OPENH323:-unset}" != "unset" ; then
      OPENH323DIR="${HOME}/openh323"
    else
      saved_cppflags="${CPPFLAGS}"
      CPPFLAGS="${CPPFLAGS} -I/usr/local/include/openh323 -I${PWLIB_INCDIR}"
      AC_CHECK_HEADER(/usr/local/include/openh323/h323.h, HAS_OPENH323=1, , [#include <ptlib.h>])
      CPPFLAGS="${saved_cppflags}"
      if test "${HAS_OPENH323:-unset}" != "unset" ; then
        OPENH323DIR="/usr/local/share/openh323"
        OPENH323_INCDIR="/usr/local/include/openh323"
        if test "x$LIB64" != "x" && test -d "/usr/local/lib64"; then
          OPENH323_LIBDIR="/usr/local/lib64"
        else
          OPENH323_LIBDIR="/usr/local/lib"
        fi
      else
        saved_cppflags="${CPPFLAGS}"
        CPPFLAGS="${CPPFLAGS} -I/usr/include/openh323 -I${PWLIB_INCDIR}"
        AC_CHECK_HEADER(/usr/include/openh323/h323.h, HAS_OPENH323=1, , [#include <ptlib.h>])
        CPPFLAGS="${saved_cppflags}"
        if test "${HAS_OPENH323:-unset}" != "unset" ; then
          OPENH323DIR="/usr/share/openh323"
          OPENH323_INCDIR="/usr/include/openh323"
          if test "x$LIB64" != "x" && test -d "/usr/local/lib64"; then
            OPENH323_LIBDIR="/usr/lib64"
          else
            OPENH323_LIBDIR="/usr/lib"
          fi
        fi
      fi
    fi
  fi
fi

if test "${HAS_OPENH323:-unset}" != "unset" ; then
  if test "${OPENH323_INCDIR:-unset}" = "unset"; then
    OPENH323_INCDIR="${OPENH323DIR}/include"
  fi
  if test "${OPENH323_LIBDIR:-unset}" = "unset"; then
    OPENH323_LIBDIR="${OPENH323DIR}/lib"
  fi

  OPENH323_LIBDIR="`cd ${OPENH323_LIBDIR}; pwd`"
  OPENH323_INCDIR="`cd ${OPENH323_INCDIR}; pwd`"
  OPENH323DIR="`cd ${OPENH323DIR}; pwd`"

  AC_SUBST([OPENH323DIR])
  AC_SUBST([OPENH323_INCDIR])
  AC_SUBST([OPENH323_LIBDIR])
fi
  AC_LANG_POP([C++])
])

AC_DEFUN([AST_CHECK_OPENH323_BUILD], [
	if test "${HAS_OPENH323:-unset}" != "unset"; then
		AC_MSG_CHECKING(OpenH323 build option)
		OPENH323_SUFFIX=
		prefixes="h323_${PWLIB_PLATFORM}_ h323_ openh323"
		for pfx in $prefixes; do
			#files=`ls -l /usr/local/lib/lib${pfx}*.so* 2>/dev/null`
			files=`ls -l ${OPENH323_LIBDIR}/lib${pfx}*.so* 2>/dev/null`
			if test -z "$files"; then
				# check the default location
				files=`ls -l /usr/local/lib/lib${pfx}*.so* 2>/dev/null`
			fi
			libfile=
			if test -n "$files"; then
				for f in $files; do
					if test -f $f -a ! -L $f; then
						libfile=`basename $f`
						break;
					fi
				done
			fi
			if test -n "$libfile"; then
				OPENH323_PREFIX=$pfx
				break;
			fi
		done
		if test "${libfile:-unset}" != "unset"; then
			OPENH323_SUFFIX=`eval "echo ${libfile} | sed -e 's/lib${OPENH323_PREFIX}\(@<:@^.@:>@*\)\..*/\1/'"`
		fi
		case "${OPENH323_SUFFIX}" in
			n)
				OPENH323_BUILD="notrace";;
			r)
				OPENH323_BUILD="opt";;
			d)
				OPENH323_BUILD="debug";;
			*)
				if test "${OPENH323_PREFIX:-undef}" = "openh323"; then
					notrace=`eval "grep NOTRACE ${OPENH323DIR}/openh323u.mak | grep = | sed -e 's/@<:@A-Z0-9_@:>@*@<:@ 	@:>@*=@<:@ 	@:>@*//'"`
					if test "x$notrace" = "x"; then
						notrace="0"
					fi
					if test "$notrace" -ne 0; then
						OPENH323_BUILD="notrace"
					else
						OPENH323_BUILD="opt"
					fi
					OPENH323_LIB="-l${OPENH323_PREFIX}"
				else
					OPENH323_BUILD="notrace"
				fi
				;;
		esac
		AC_MSG_RESULT(${OPENH323_BUILD})

		AC_SUBST([OPENH323_SUFFIX])
		AC_SUBST([OPENH323_BUILD])
	fi
])
