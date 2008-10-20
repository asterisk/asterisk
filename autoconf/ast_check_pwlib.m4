AC_DEFUN([AST_CHECK_PWLIB], [
PWLIB_INCDIR=
PWLIB_LIBDIR=
AC_LANG_PUSH([C++])
if test "${PWLIBDIR:-unset}" != "unset" ; then
  AC_CHECK_HEADER(${PWLIBDIR}/version.h, HAS_PWLIB=1, )
fi
if test "${HAS_PWLIB:-unset}" = "unset" ; then
  if test "${OPENH323DIR:-unset}" != "unset"; then
    AC_CHECK_HEADER(${OPENH323DIR}/../pwlib/version.h, HAS_PWLIB=1, )
  fi
  if test "${HAS_PWLIB:-unset}" != "unset" ; then
    PWLIBDIR="${OPENH323DIR}/../pwlib"
  else
    AC_CHECK_HEADER(${HOME}/pwlib/include/ptlib.h, HAS_PWLIB=1, )
    if test "${HAS_PWLIB:-unset}" != "unset" ; then
      PWLIBDIR="${HOME}/pwlib"
    else
      AC_CHECK_HEADER(/usr/local/include/ptlib.h, HAS_PWLIB=1, )
      if test "${HAS_PWLIB:-unset}" != "unset" ; then
        AC_PATH_PROG(PTLIB_CONFIG, ptlib-config, , /usr/local/bin)
        if test "${PTLIB_CONFIG:-unset}" = "unset" ; then
          AC_PATH_PROG(PTLIB_CONFIG, ptlib-config, , /usr/local/share/pwlib/make)
        fi
        PWLIB_INCDIR="/usr/local/include"
        PWLIB_LIBDIR=`${PTLIB_CONFIG} --pwlibdir`
        if test "${PWLIB_LIBDIR:-unset}" = "unset"; then
          if test "x$LIB64" != "x"; then
            PWLIB_LIBDIR="/usr/local/lib64"
          else
            PWLIB_LIBDIR="/usr/local/lib"
          fi
        fi
        PWLIB_LIB=`${PTLIB_CONFIG} --ldflags --libs`
        PWLIB_LIB="-L${PWLIB_LIBDIR} `echo ${PWLIB_LIB}`"
      else
        AC_CHECK_HEADER(/usr/include/ptlib.h, HAS_PWLIB=1, )
        if test "${HAS_PWLIB:-unset}" != "unset" ; then
          AC_PATH_PROG(PTLIB_CONFIG, ptlib-config, , /usr/share/pwlib/make)
          PWLIB_INCDIR="/usr/include"
          PWLIB_LIBDIR=`${PTLIB_CONFIG} --pwlibdir`
          if test "${PWLIB_LIBDIR:-unset}" = "unset"; then
            if test "x$LIB64" != "x"; then
              PWLIB_LIBDIR="/usr/lib64"
            else
              PWLIB_LIBDIR="/usr/lib"
            fi
          fi
          PWLIB_LIB=`${PTLIB_CONFIG} --ldflags --libs`
          PWLIB_LIB="-L${PWLIB_LIBDIR} `echo ${PWLIB_LIB}`"
        fi
      fi
    fi
  fi
fi

#if test "${HAS_PWLIB:-unset}" = "unset" ; then
#  echo "Cannot find pwlib - please install or set PWLIBDIR and try again"
#  exit
#fi

if test "${HAS_PWLIB:-unset}" != "unset" ; then
  if test "${PWLIBDIR:-unset}" = "unset" ; then
    if test "${PTLIB_CONFIG:-unset}" != "unset" ; then
      PWLIBDIR=`$PTLIB_CONFIG --prefix`
    else
      echo "Cannot find ptlib-config - please install and try again"
      exit
    fi
  fi

  if test "x$PWLIBDIR" = "x/usr" -o "x$PWLIBDIR" = "x/usr/"; then
    PWLIBDIR="/usr/share/pwlib"
    PWLIB_INCDIR="/usr/include"
    if test "x$LIB64" != "x"; then
      PWLIB_LIBDIR="/usr/lib64"
    else
      PWLIB_LIBDIR="/usr/lib"
    fi
  fi
  if test "x$PWLIBDIR" = "x/usr/local" -o "x$PWLIBDIR" = "x/usr/"; then
    PWLIBDIR="/usr/local/share/pwlib"
    PWLIB_INCDIR="/usr/local/include"
    if test "x$LIB64" != "x"; then
      PWLIB_LIBDIR="/usr/local/lib64"
    else
      PWLIB_LIBDIR="/usr/local/lib"
    fi
  fi

  if test "${PWLIB_INCDIR:-unset}" = "unset"; then
    PWLIB_INCDIR="${PWLIBDIR}/include"
  fi
  if test "${PWLIB_LIBDIR:-unset}" = "unset"; then
    PWLIB_LIBDIR="${PWLIBDIR}/lib"
  fi

  AC_SUBST([PWLIBDIR])
  AC_SUBST([PWLIB_INCDIR])
  AC_SUBST([PWLIB_LIBDIR])
fi
  AC_LANG_POP([C++])
])

AC_DEFUN([AST_CHECK_PWLIB_VERSION], [
	if test "${HAS_$2:-unset}" != "unset"; then
		$2_VERSION=`grep "$2_VERSION" ${$2_INCDIR}/$3 | sed -e 's/[[[:space:]]]\{1,\}/ /g' | cut -f3 -d ' ' | sed -e 's/"//g'`
		$2_MAJOR_VERSION=`echo ${$2_VERSION} | cut -f1 -d.`
		$2_MINOR_VERSION=`echo ${$2_VERSION} | cut -f2 -d.`
		$2_BUILD_NUMBER=`echo ${$2_VERSION} | cut -f3 -d.`
		let $2_VER=${$2_MAJOR_VERSION}*10000+${$2_MINOR_VERSION}*100+${$2_BUILD_NUMBER}
		let $2_REQ=$4*10000+$5*100+$6

		AC_MSG_CHECKING(if $1 version ${$2_VERSION} is compatible with chan_h323)
		if test ${$2_VER} -lt ${$2_REQ}; then
			AC_MSG_RESULT(no)
			unset HAS_$2
		else
			AC_MSG_RESULT(yes)
		fi
	fi
])


AC_DEFUN([AST_CHECK_PWLIB_BUILD], [
	if test "${HAS_$2:-unset}" != "unset"; then
	   AC_MSG_CHECKING($1 installation validity)

	   saved_cppflags="${CPPFLAGS}"
	   saved_libs="${LIBS}"
	   if test "${$2_LIB:-unset}" != "unset"; then
	      LIBS="${LIBS} ${$2_LIB} $7"
	   else
    	      LIBS="${LIBS} -L${$2_LIBDIR} -l${PLATFORM_$2} $7"
	   fi
	   CPPFLAGS="${CPPFLAGS} -I${$2_INCDIR} $6"

	   AC_LANG_PUSH([C++])

	   AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([$4],[$5])],
		[	AC_MSG_RESULT(yes) 
			ac_cv_lib_$2="yes" 
		],
		[	AC_MSG_RESULT(no) 
			ac_cv_lib_$2="no" 
		]
		)

	   AC_LANG_POP([C++])

	   LIBS="${saved_libs}"
	   CPPFLAGS="${saved_cppflags}"

	   if test "${ac_cv_lib_$2}" = "yes"; then
	      if test "${$2_LIB:-undef}" = "undef"; then
	         if test "${$2_LIBDIR}" != "" -a "${$2_LIBDIR}" != "/usr/lib"; then
	            $2_LIB="-L${$2_LIBDIR} -l${PLATFORM_$2}"
	         else
	            $2_LIB="-l${PLATFORM_$2}"
	         fi
	      fi
	      if test "${$2_INCDIR}" != "" -a "${$2_INCDIR}" != "/usr/include"; then
	         $2_INCLUDE="-I${$2_INCDIR}"
	      fi
	   	  PBX_$2=1
	   	  AC_DEFINE([HAVE_$2], 1, [$3])
	   fi
	fi
])
