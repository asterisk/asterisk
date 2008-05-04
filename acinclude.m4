# Various support functions for configure.ac in asterisk
#

# Helper function to check for gcc attributes.
# AST_GCC_ATTRIBUTE([attribute name])

AC_DEFUN([AST_GCC_ATTRIBUTE],
[
AC_MSG_CHECKING(for compiler 'attribute $1' support)
saved_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Werror"
AC_COMPILE_IFELSE(
	AC_LANG_PROGRAM([static int __attribute__(($1)) test(void) {}],
			[]),
	AC_MSG_RESULT(yes)
	AC_DEFINE_UNQUOTED([HAVE_ATTRIBUTE_$1], 1, [Define to 1 if your GCC C compiler supports the '$1' attribute.]),
	AC_MSG_RESULT(no))
]
CFLAGS="$saved_CFLAGS"
)

# Helper function to setup variables for a package.
# $1 -> the package name. Used in configure.ac and also as a prefix
#	for the variables ($1_DIR, $1_INCLUDE, $1_LIB) in makeopts
# $3 ->	option name, used in --with-$3 or --without-$3 when calling configure.
# $2 and $4 are just text describing the package (short and long form)

# AST_EXT_LIB_SETUP([package], [short description], [configure option name], [long description])

AC_DEFUN([AST_EXT_LIB_SETUP],
[
    $1_DESCRIP="$2"
    $1_OPTION="$3"
    AC_ARG_WITH([$3], AC_HELP_STRING([--with-$3=PATH],[use $2 files in PATH $4]),
    [
	case ${withval} in
	n|no)
	USE_$1=no
	;;
	y|ye|yes)
	ac_mandatory_list="${ac_mandatory_list} $1"
	;;
	*)
	$1_DIR="${withval}"
	ac_mandatory_list="${ac_mandatory_list} $1"
	;;
	esac
    ])
    PBX_$1=0
    AC_SUBST([$1_LIB])
    AC_SUBST([$1_INCLUDE])
    AC_SUBST([$1_DIR])
    AC_SUBST([PBX_$1])
])

# Check whether any of the mandatory modules are not present, and
# print error messages in case. The mandatory list is built using
# --with-* arguments when invoking configure.

AC_DEFUN([AST_CHECK_MANDATORY],
[
	AC_MSG_CHECKING([for mandatory modules: ${ac_mandatory_list}])
	err=0;
	for i in ${ac_mandatory_list}; do
		eval "a=\${PBX_$i}"
		if test "x${a}" = "x1" ; then continue; fi
		if test ${err} = "0" ; then AC_MSG_RESULT(fail) ; fi
		AC_MSG_RESULT()
		eval "a=\${${i}_OPTION}"
		AC_MSG_NOTICE([***])
		AC_MSG_NOTICE([*** The $i installation appears to be missing or broken.])
		AC_MSG_NOTICE([*** Either correct the installation, or run configure])
		AC_MSG_NOTICE([*** including --without-${a}.])
		err=1
	done
	if test $err = 1 ; then exit 1; fi
	AC_MSG_RESULT(ok)
])

# The next three functions check for the availability of a given package.
# AST_C_DEFINE_CHECK looks for the presence of a #define in a header file,
# AST_C_COMPILE_CHECK can be used for testing for various items in header files,
# AST_EXT_LIB_CHECK looks for a symbol in a given library, or at least
#	for the presence of a header file.
# AST_EXT_TOOL_CHECK looks for a symbol in using $1-config to determine CFLAGS and LIBS
#
# They are only run if PBX_$1 != 1 (where $1 is the package),
# so you can call them multiple times and stop at the first matching one.
# On success, they both set PBX_$1 = 1, set $1_INCLUDE and $1_LIB as applicable,
# and also #define HAVE_$1 1 and #define HAVE_$1_VERSION ${last_argument}
# in autoconfig.h so you can tell which test succeeded.
# They should be called after AST_EXT_LIB_SETUP($1, ...)

# Check if a given macro is defined in a certain header.

# AST_C_DEFINE_CHECK([package], [macro name], [header file], [version])
AC_DEFUN([AST_C_DEFINE_CHECK],
[
    if test "x${PBX_$1}" != "x1"; then
	AC_MSG_CHECKING([for $2 in $3])
	saved_cppflags="${CPPFLAGS}"
	if test "x${$1_DIR}" != "x"; then
	    $1_INCLUDE="-I${$1_DIR}/include"
	fi
	CPPFLAGS="${CPPFLAGS} ${$1_INCLUDE}"

	AC_COMPILE_IFELSE(
	    [ AC_LANG_PROGRAM( [#include <$3>],
			       [#if defined($2)
				int foo = 0;
			        #else
			        int foo = bar;
			        #endif
				0
			       ])],
	    [   AC_MSG_RESULT(yes)
		PBX_$1=1
		AC_DEFINE([HAVE_$1], 1, [Define if your system has the $1 headers.])
		AC_DEFINE([HAVE_$1_VERSION], $4, [Define $1 headers version])
	    ],
	    [   AC_MSG_RESULT(no) ] 
	)
	CPPFLAGS="${saved_cppflags}"
    fi
    AC_SUBST(PBX_$1)
])


# Check if a given expression will compile using a certain header.

# AST_C_COMPILE_CHECK([package], [expression], [header file], [version])
AC_DEFUN([AST_C_COMPILE_CHECK],
[
    if test "x${PBX_$1}" != "x1" -a "${USE_$1}" != "no"; then
	AC_MSG_CHECKING([if "$2" compiles using $3])
	saved_cppflags="${CPPFLAGS}"
	if test "x${$1_DIR}" != "x"; then
	    $1_INCLUDE="-I${$1_DIR}/include"
	fi
	CPPFLAGS="${CPPFLAGS} ${$1_INCLUDE}"

	AC_COMPILE_IFELSE(
	    [ AC_LANG_PROGRAM( [#include <$3>],
			       [ $2; ]
			       )],
	    [   AC_MSG_RESULT(yes)
		PBX_$1=1
		AC_DEFINE([HAVE_$1], 1, [Define if your system has the $1 headers.])
		AC_DEFINE([HAVE_$1_VERSION], $4, [Define $1 headers version])
	    ],
	    [       AC_MSG_RESULT(no) ] 
	)
	CPPFLAGS="${saved_cppflags}"
    fi
])


# Check for existence of a given package ($1), either looking up a function
# in a library, or, if no function is supplied, only check for the
# existence of the header files.

# AST_EXT_LIB_CHECK([package], [library], [function], [header],
#	 [extra libs], [extra cflags], [version])
AC_DEFUN([AST_EXT_LIB_CHECK],
[
if test "x${PBX_$1}" != "x1" -a "${USE_$1}" != "no"; then
   pbxlibdir=""
   # if --with-$1=DIR has been specified, use it.
   if test "x${$1_DIR}" != "x"; then
      if test -d ${$1_DIR}/lib; then
      	 pbxlibdir="-L${$1_DIR}/lib"
      else
      	 pbxlibdir="-L${$1_DIR}"
      fi
   fi
   pbxfuncname="$3"
   if test "x${pbxfuncname}" = "x" ; then   # empty lib, assume only headers
      AST_$1_FOUND=yes
   else
      AC_CHECK_LIB([$2], [${pbxfuncname}], [AST_$1_FOUND=yes], [AST_$1_FOUND=no], ${pbxlibdir} $5)
   fi

   # now check for the header.
   if test "${AST_$1_FOUND}" = "yes"; then
      $1_LIB="${pbxlibdir} -l$2 $5"
      # if --with-$1=DIR has been specified, use it.
      if test "x${$1_DIR}" != "x"; then
	 $1_INCLUDE="-I${$1_DIR}/include"
      fi
      $1_INCLUDE="${$1_INCLUDE} $6"
      if test "x$4" = "x" ; then	# no header, assume found
         $1_HEADER_FOUND="1"
      else				# check for the header
         saved_cppflags="${CPPFLAGS}"
         CPPFLAGS="${CPPFLAGS} ${$1_INCLUDE} $6"
	 AC_CHECK_HEADER([$4], [$1_HEADER_FOUND=1], [$1_HEADER_FOUND=0])
         CPPFLAGS="${saved_cppflags}"
      fi
      if test "x${$1_HEADER_FOUND}" = "x0" ; then
         $1_LIB=""
         $1_INCLUDE=""
      else
         if test "x${pbxfuncname}" = "x" ; then		# only checking headers -> no library
	    $1_LIB=""
	 fi
         PBX_$1=1
         # XXX don't know how to evaluate the description (third argument) in AC_DEFINE_UNQUOTED
         AC_DEFINE_UNQUOTED([HAVE_$1], 1, [Define this to indicate the ${$1_DESCRIP} library])
	 AC_DEFINE_UNQUOTED([HAVE_$1_VERSION], [$7], [Define to indicate the ${$1_DESCRIP} library version])
      fi
   fi
fi
])


# Check for a package using $2-config. Similar to AST_EXT_LIB_CHECK,
# but use $2-config to determine cflags and libraries to use.
# $3 and $4 can be used to replace --cflags and --libs in the request

# AST_EXT_TOOL_CHECK([package], [tool name], [--cflags], [--libs], [includes], [expression])
AC_DEFUN([AST_EXT_TOOL_CHECK],
[
    if test "x${PBX_$1}" != "x1" -a "${USE_$1}" != "no"; then
	PBX_$1=0
	AC_CHECK_TOOL(CONFIG_$1, $2-config, No)
	if test ! "x${CONFIG_$1}" = xNo; then
	    if test x"$3" = x ; then A=--cflags ; else A="$3" ; fi
	    $1_INCLUDE=$(${CONFIG_$1} $A)
	    if test x"$4" = x ; then A=--libs ; else A="$4" ; fi
	    $1_LIB=$(${CONFIG_$1} $A)
	    if test x"$5" != x ; then
		saved_cppflags="${CPPFLAGS}"
		if test "x${$1_DIR}" != "x"; then
		    $1_INCLUDE="-I${$1_DIR}/include"
		fi
		CPPFLAGS="${CPPFLAGS} ${$1_INCLUDE}"

		saved_ldflags="${LDFLAGS}"
		LDFLAGS="${$1_LIB}"

		AC_LINK_IFELSE(
		    [ AC_LANG_PROGRAM( [ $5 ],
				       [ $6; ]
				       )],
		    [   PBX_$1=1
			AC_DEFINE([HAVE_$1], 1, [Define if your system has the $1 headers.])
		    ],
		    []
		)
		CPPFLAGS="${saved_cppflags}"
		LDFLAGS="${saved_ldflags}"
	    else
		PBX_$1=1
		AC_DEFINE([HAVE_$1], 1, [Define if your system has the $1 libraries.])
	    fi
	fi
    fi
])

AC_DEFUN(
[AST_CHECK_GNU_MAKE], [AC_CACHE_CHECK(for GNU make, GNU_MAKE,
   GNU_MAKE='Not Found' ;
   GNU_MAKE_VERSION_MAJOR=0 ;
   GNU_MAKE_VERSION_MINOR=0 ;
   for a in make gmake gnumake ; do
      if test -z "$a" ; then continue ; fi ;
      if ( sh -c "$a --version" 2> /dev/null | grep GNU  2>&1 > /dev/null ) ;  then
         GNU_MAKE=$a ;
         GNU_MAKE_VERSION_MAJOR=`$GNU_MAKE --version | grep "GNU Make" | cut -f3 -d' ' | cut -f1 -d'.'`
         GNU_MAKE_VERSION_MINOR=`$GNU_MAKE --version | grep "GNU Make" | cut -f2 -d'.' | cut -c1-2`
         break;
      fi
   done ;
) ;
if test  "x$GNU_MAKE" = "xNot Found"  ; then
   AC_MSG_ERROR( *** Please install GNU make.  It is required to build Asterisk!)
   exit 1
fi
AC_SUBST([GNU_MAKE])
])


AC_DEFUN(
[AST_CHECK_PWLIB], [
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


AC_DEFUN(
[AST_CHECK_OPENH323_PLATFORM], [
PWLIB_OSTYPE=
case "$host_os" in
  linux*)          PWLIB_OSTYPE=linux ;
  		;;
  freebsd* )       PWLIB_OSTYPE=FreeBSD ;
  		;;
  openbsd* )       PWLIB_OSTYPE=OpenBSD ;
				   ENDLDLIBS="-lossaudio" ;
		;;
  netbsd* )        PWLIB_OSTYPE=NetBSD ;
				   ENDLDLIBS="-lossaudio" ;
		;;
  solaris* | sunos* ) PWLIB_OSTYPE=solaris ;
		;;
  darwin* )	       PWLIB_OSTYPE=Darwin ;
		;;
  beos*)           PWLIB_OSTYPE=beos ;
                   STDCCFLAGS="$STDCCFLAGS -D__BEOS__"
		;;
  cygwin*)         PWLIB_OSTYPE=cygwin ;
		;;
  mingw*)	       PWLIB_OSTYPE=mingw ;
		           STDCCFLAGS="$STDCCFLAGS -mms-bitfields" ;
		           ENDLDLIBS="-lwinmm -lwsock32 -lsnmpapi -lmpr -lcomdlg32 -lgdi32 -lavicap32" ;
		;;
  * )		       PWLIB_OSTYPE="$host_os" ;
		           AC_MSG_WARN("OS $PWLIB_OSTYPE not recognized - proceed with caution!") ;
		;;
esac

PWLIB_MACHTYPE=
case "$host_cpu" in
   x86 | i686 | i586 | i486 | i386 ) PWLIB_MACHTYPE=x86
                   ;;

   x86_64)	   PWLIB_MACHTYPE=x86_64 ;
		   P_64BIT=1 ;
                   LIB64=1 ;
		   ;;

   alpha | alphaev56 | alphaev6 | alphaev67 | alphaev7) PWLIB_MACHTYPE=alpha ;
		   P_64BIT=1 ;
		   ;;

   sparc )         PWLIB_MACHTYPE=sparc ;
		   ;;

   powerpc )       PWLIB_MACHTYPE=ppc ;
		   ;;

   ppc )           PWLIB_MACHTYPE=ppc ;
		   ;;

   powerpc64 )     PWLIB_MACHTYPE=ppc64 ;
		   P_64BIT=1 ;
                   LIB64=1 ;
		   ;;

   ppc64 )         PWLIB_MACHTYPE=ppc64 ;
		   P_64BIT=1 ;
                   LIB64=1 ;
		   ;;

   ia64)	   PWLIB_MACHTYPE=ia64 ;
		   P_64BIT=1 ;
	  	   ;;

   s390x)	   PWLIB_MACHTYPE=s390x ;
		   P_64BIT=1 ;
                   LIB64=1 ;
		   ;;

   s390)	   PWLIB_MACHTYPE=s390 ;
		   ;;

   * )		   PWLIB_MACHTYPE="$host_cpu";
		   AC_MSG_WARN("CPU $PWLIB_MACHTYPE not recognized - proceed with caution!") ;;
esac

PWLIB_PLATFORM="${PWLIB_OSTYPE}_${PWLIB_MACHTYPE}"

AC_SUBST([PWLIB_PLATFORM])
])


AC_DEFUN(
[AST_CHECK_OPENH323], [
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
    AC_CHECK_HEADER(${HOME}/openh323/include/h323.h, HAS_OPENH323=1, )
    CPPFLAGS="${saved_cppflags}"
    if test "${HAS_OPENH323:-unset}" != "unset" ; then
      OPENH323DIR="${HOME}/openh323"
    else
      saved_cppflags="${CPPFLAGS}"
      CPPFLAGS="${CPPFLAGS} -I/usr/local/include/openh323 -I${PWLIB_INCDIR}"
      AC_CHECK_HEADER(/usr/local/include/openh323/h323.h, HAS_OPENH323=1, )
      CPPFLAGS="${saved_cppflags}"
      if test "${HAS_OPENH323:-unset}" != "unset" ; then
        OPENH323DIR="/usr/local/share/openh323"
        OPENH323_INCDIR="/usr/local/include/openh323"
        if test "x$LIB64" != "x"; then
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
          if test "x$LIB64" != "x"; then
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


AC_DEFUN(
[AST_CHECK_PWLIB_VERSION], [
	if test "${HAS_$2:-unset}" != "unset"; then
		$2_VERSION=`grep "$2_VERSION" ${$2_INCDIR}/$3 | cut -f2 -d ' ' | sed -e 's/"//g'`
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


AC_DEFUN(
[AST_CHECK_PWLIB_BUILD], [
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

AC_DEFUN(
[AST_CHECK_OPENH323_BUILD], [
	if test "${HAS_OPENH323:-unset}" != "unset"; then
		AC_MSG_CHECKING(OpenH323 build option)
		OPENH323_SUFFIX=
		prefixes="h323_${PWLIB_PLATFORM}_ h323_ openh323"
		for pfx in $prefixes; do
			files=`ls -l ${OPENH323_LIBDIR}/lib${pfx}*.so* 2>/dev/null`
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


# AST_FUNC_FORK
# -------------
AN_FUNCTION([fork],  [AST_FUNC_FORK])
AN_FUNCTION([vfork], [AST_FUNC_FORK])
AC_DEFUN([AST_FUNC_FORK],
[AC_REQUIRE([AC_TYPE_PID_T])dnl
AC_CHECK_HEADERS(vfork.h)
AC_CHECK_FUNCS(fork vfork)
if test "x$ac_cv_func_fork" = xyes; then
  _AST_FUNC_FORK
else
  ac_cv_func_fork_works=$ac_cv_func_fork
fi
if test "x$ac_cv_func_fork_works" = xcross; then
  case $host in
    *-*-amigaos* | *-*-msdosdjgpp* | *-*-uclinux* | *-*-linux-uclibc* )
      # Override, as these systems have only a dummy fork() stub
      ac_cv_func_fork_works=no
      ;;
    *)
      ac_cv_func_fork_works=yes
      ;;
  esac
  AC_MSG_WARN([result $ac_cv_func_fork_works guessed because of cross compilation])
fi
ac_cv_func_vfork_works=$ac_cv_func_vfork
if test "x$ac_cv_func_vfork" = xyes; then
  _AC_FUNC_VFORK
fi;
if test "x$ac_cv_func_fork_works" = xcross; then
  ac_cv_func_vfork_works=$ac_cv_func_vfork
  AC_MSG_WARN([result $ac_cv_func_vfork_works guessed because of cross compilation])
fi

if test "x$ac_cv_func_vfork_works" = xyes; then
  AC_DEFINE(HAVE_WORKING_VFORK, 1, [Define to 1 if `vfork' works.])
else
  AC_DEFINE(vfork, fork, [Define as `fork' if `vfork' does not work.])
fi
if test "x$ac_cv_func_fork_works" = xyes; then
  AC_DEFINE(HAVE_WORKING_FORK, 1, [Define to 1 if `fork' works.])
fi
])# AST_FUNC_FORK


# _AST_FUNC_FORK
# -------------
AC_DEFUN([_AST_FUNC_FORK],
  [AC_CACHE_CHECK(for working fork, ac_cv_func_fork_works,
    [AC_RUN_IFELSE(
      [AC_LANG_PROGRAM([AC_INCLUDES_DEFAULT],
	[
	  /* By Ruediger Kuhlmann. */
	  return fork () < 0;
	])],
      [ac_cv_func_fork_works=yes],
      [ac_cv_func_fork_works=no],
      [ac_cv_func_fork_works=cross])])]
)# _AST_FUNC_FORK

# AST_PROG_LD
# ----------
# find the pathname to the GNU or non-GNU linker
AC_DEFUN([AST_PROG_LD],
[AC_ARG_WITH([gnu-ld],
    [AC_HELP_STRING([--with-gnu-ld],
	[assume the C compiler uses GNU ld @<:@default=no@:>@])],
    [test "$withval" = no || with_gnu_ld=yes],
    [with_gnu_ld=no])
AC_REQUIRE([AST_PROG_SED])dnl
AC_REQUIRE([AC_PROG_CC])dnl
AC_REQUIRE([AC_CANONICAL_HOST])dnl
AC_REQUIRE([AC_CANONICAL_BUILD])dnl
ac_prog=ld
if test "$GCC" = yes; then
  # Check if gcc -print-prog-name=ld gives a path.
  AC_MSG_CHECKING([for ld used by $CC])
  case $host in
  *-*-mingw*)
    # gcc leaves a trailing carriage return which upsets mingw
    ac_prog=`($CC -print-prog-name=ld) 2>&5 | tr -d '\015'` ;;
  *)
    ac_prog=`($CC -print-prog-name=ld) 2>&5` ;;
  esac
  case $ac_prog in
    # Accept absolute paths.
    [[\\/]]* | ?:[[\\/]]*)
      re_direlt='/[[^/]][[^/]]*/\.\./'
      # Canonicalize the pathname of ld
      ac_prog=`echo $ac_prog| $SED 's%\\\\%/%g'`
      while echo $ac_prog | grep "$re_direlt" > /dev/null 2>&1; do
	ac_prog=`echo $ac_prog| $SED "s%$re_direlt%/%"`
      done
      test -z "$LD" && LD="$ac_prog"
      ;;
  "")
    # If it fails, then pretend we aren't using GCC.
    ac_prog=ld
    ;;
  *)
    # If it is relative, then search for the first ld in PATH.
    with_gnu_ld=unknown
    ;;
  esac
elif test "$with_gnu_ld" = yes; then
  AC_MSG_CHECKING([for GNU ld])
else
  AC_MSG_CHECKING([for non-GNU ld])
fi
AC_CACHE_VAL(lt_cv_path_LD,
[if test -z "$LD"; then
  lt_save_ifs="$IFS"; IFS=$PATH_SEPARATOR
  for ac_dir in $PATH; do
    IFS="$lt_save_ifs"
    test -z "$ac_dir" && ac_dir=.
    if test -f "$ac_dir/$ac_prog" || test -f "$ac_dir/$ac_prog$ac_exeext"; then
      lt_cv_path_LD="$ac_dir/$ac_prog"
      # Check to see if the program is GNU ld.  I'd rather use --version,
      # but apparently some variants of GNU ld only accept -v.
      # Break only if it was the GNU/non-GNU ld that we prefer.
      case `"$lt_cv_path_LD" -v 2>&1 </dev/null` in
      *GNU* | *'with BFD'*)
	test "$with_gnu_ld" != no && break
	;;
      *)
	test "$with_gnu_ld" != yes && break
	;;
      esac
    fi
  done
  IFS="$lt_save_ifs"
else
  lt_cv_path_LD="$LD" # Let the user override the test with a path.
fi])
LD="$lt_cv_path_LD"
if test -n "$LD"; then
  AC_MSG_RESULT($LD)
else
  AC_MSG_RESULT(no)
fi
test -z "$LD" && AC_MSG_ERROR([no acceptable ld found in \$PATH])
AST_PROG_LD_GNU
])# AST_PROG_LD


# AST_PROG_LD_GNU
# --------------
AC_DEFUN([AST_PROG_LD_GNU],
[AC_REQUIRE([AST_PROG_EGREP])dnl
AC_CACHE_CHECK([if the linker ($LD) is GNU ld], lt_cv_prog_gnu_ld,
[# I'd rather use --version here, but apparently some GNU lds only accept -v.
case `$LD -v 2>&1 </dev/null` in
*GNU* | *'with BFD'*)
  lt_cv_prog_gnu_ld=yes
  ;;
*)
  lt_cv_prog_gnu_ld=no
  ;;
esac])
with_gnu_ld=$lt_cv_prog_gnu_ld
])# AST_PROG_LD_GNU

# AST_PROG_EGREP
# -------------
m4_ifndef([AST_PROG_EGREP], [AC_DEFUN([AST_PROG_EGREP],
[AC_CACHE_CHECK([for egrep], [ac_cv_prog_egrep],
   [if echo a | (grep -E '(a|b)') >/dev/null 2>&1
    then ac_cv_prog_egrep='grep -E'
    else ac_cv_prog_egrep='egrep'
    fi])
 EGREP=$ac_cv_prog_egrep
 AC_SUBST([EGREP])
])]) # AST_PROG_EGREP

# AST_PROG_SED
# -----------
# Check for a fully functional sed program that truncates
# as few characters as possible.  Prefer GNU sed if found.
AC_DEFUN([AST_PROG_SED],
[AC_CACHE_CHECK([for a sed that does not truncate output], ac_cv_path_SED,
    [dnl ac_script should not contain more than 99 commands (for HP-UX sed),
     dnl but more than about 7000 bytes, to catch a limit in Solaris 8 /usr/ucb/sed.
     ac_script=s/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/
     for ac_i in 1 2 3 4 5 6 7; do
       ac_script="$ac_script$as_nl$ac_script"
     done
     echo "$ac_script" | sed 99q >conftest.sed
     $as_unset ac_script || ac_script=
     _AC_PATH_PROG_FEATURE_CHECK(SED, [sed gsed],
	[_AC_FEATURE_CHECK_LENGTH([ac_path_SED], [ac_cv_path_SED],
		["$ac_path_SED" -f conftest.sed])])])
 SED="$ac_cv_path_SED"
 AC_SUBST([SED])dnl
 rm -f conftest.sed
])# AST_PROG_SED

dnl @synopsis ACX_PTHREAD([ACTION-IF-FOUND[, ACTION-IF-NOT-FOUND]])
dnl
dnl @summary figure out how to build C programs using POSIX threads
dnl
dnl This macro figures out how to build C programs using POSIX threads.
dnl It sets the PTHREAD_LIBS output variable to the threads library and
dnl linker flags, and the PTHREAD_CFLAGS output variable to any special
dnl C compiler flags that are needed. (The user can also force certain
dnl compiler flags/libs to be tested by setting these environment
dnl variables.)
dnl
dnl Also sets PTHREAD_CC to any special C compiler that is needed for
dnl multi-threaded programs (defaults to the value of CC otherwise).
dnl (This is necessary on AIX to use the special cc_r compiler alias.)
dnl
dnl NOTE: You are assumed to not only compile your program with these
dnl flags, but also link it with them as well. e.g. you should link
dnl with $PTHREAD_CC $CFLAGS $PTHREAD_CFLAGS $LDFLAGS ... $PTHREAD_LIBS
dnl $LIBS
dnl
dnl If you are only building threads programs, you may wish to use
dnl these variables in your default LIBS, CFLAGS, and CC:
dnl
dnl        LIBS="$PTHREAD_LIBS $LIBS"
dnl        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
dnl        CC="$PTHREAD_CC"
dnl
dnl In addition, if the PTHREAD_CREATE_JOINABLE thread-attribute
dnl constant has a nonstandard name, defines PTHREAD_CREATE_JOINABLE to
dnl that name (e.g. PTHREAD_CREATE_UNDETACHED on AIX).
dnl
dnl ACTION-IF-FOUND is a list of shell commands to run if a threads
dnl library is found, and ACTION-IF-NOT-FOUND is a list of commands to
dnl run it if it is not found. If ACTION-IF-FOUND is not specified, the
dnl default action will define HAVE_PTHREAD.
dnl
dnl Please let the authors know if this macro fails on any platform, or
dnl if you have any other suggestions or comments. This macro was based
dnl on work by SGJ on autoconf scripts for FFTW (www.fftw.org) (with
dnl help from M. Frigo), as well as ac_pthread and hb_pthread macros
dnl posted by Alejandro Forero Cuervo to the autoconf macro repository.
dnl We are also grateful for the helpful feedback of numerous users.
dnl
dnl @category InstalledPackages
dnl @author Steven G. Johnson <stevenj@alum.mit.edu>
dnl @version 2006-05-29
dnl @license GPLWithACException

AC_DEFUN([ACX_PTHREAD],
[
AC_REQUIRE([AC_CANONICAL_HOST])
AC_LANG_SAVE
AC_LANG_C
acx_pthread_ok=no

# We used to check for pthread.h first, but this fails if pthread.h
# requires special compiler flags (e.g. on True64 or Sequent).
# It gets checked for in the link test anyway.

# First of all, check if the user has set any of the PTHREAD_LIBS,
# etcetera environment variables, and if threads linking works using
# them:
if test x"$PTHREAD_LIBS$PTHREAD_CFLAGS" != x; then
        save_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
        save_LIBS="$LIBS"
        LIBS="$PTHREAD_LIBS $LIBS"
        AC_MSG_CHECKING([for pthread_join in LIBS=$PTHREAD_LIBS with CFLAGS=$PTHREAD_CFLAGS])
        AC_TRY_LINK_FUNC(pthread_join, acx_pthread_ok=yes)
        AC_MSG_RESULT($acx_pthread_ok)
        if test x"$acx_pthread_ok" = xno; then
                PTHREAD_LIBS=""
                PTHREAD_CFLAGS=""
        fi
        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"
fi

# We must check for the threads library under a number of different
# names; the ordering is very important because some systems
# (e.g. DEC) have both -lpthread and -lpthreads, where one of the
# libraries is broken (non-POSIX).

# Create a list of thread flags to try.  Items starting with a "-" are
# C compiler flags, and other items are library names, except for "none"
# which indicates that we try without any flags at all, and "pthread-config"
# which is a program returning the flags for the Pth emulation library.

acx_pthread_flags="pthreads none -Kthread -kthread lthread -pthread -pthreads -mthreads pthread --thread-safe -mt pthread-config"

# The ordering *is* (sometimes) important.  Some notes on the
# individual items follow:

# pthreads: AIX (must check this before -lpthread)
# none: in case threads are in libc; should be tried before -Kthread and
#       other compiler flags to prevent continual compiler warnings
# -Kthread: Sequent (threads in libc, but -Kthread needed for pthread.h)
# -kthread: FreeBSD kernel threads (preferred to -pthread since SMP-able)
# lthread: LinuxThreads port on FreeBSD (also preferred to -pthread)
# -pthread: Linux/gcc (kernel threads), BSD/gcc (userland threads)
# -pthreads: Solaris/gcc
# -mthreads: Mingw32/gcc, Lynx/gcc
# -mt: Sun Workshop C (may only link SunOS threads [-lthread], but it
#      doesn't hurt to check since this sometimes defines pthreads too;
#      also defines -D_REENTRANT)
#      ... -mt is also the pthreads flag for HP/aCC
# pthread: Linux, etcetera
# --thread-safe: KAI C++
# pthread-config: use pthread-config program (for GNU Pth library)

case "${host_cpu}-${host_os}" in
        *solaris*)

        # On Solaris (at least, for some versions), libc contains stubbed
        # (non-functional) versions of the pthreads routines, so link-based
        # tests will erroneously succeed.  (We need to link with -pthreads/-mt/
        # -lpthread.)  (The stubs are missing pthread_cleanup_push, or rather
        # a function called by this macro, so we could check for that, but
        # who knows whether they'll stub that too in a future libc.)  So,
        # we'll just look for -pthreads and -lpthread first:

        acx_pthread_flags="-pthreads pthread -mt -pthread $acx_pthread_flags"
        ;;
esac

if test x"$acx_pthread_ok" = xno; then
for flag in $acx_pthread_flags; do

        case $flag in
                none)
                AC_MSG_CHECKING([whether pthreads work without any flags])
                ;;

                -*)
                AC_MSG_CHECKING([whether pthreads work with $flag])
                PTHREAD_CFLAGS="$flag"
                ;;

		pthread-config)
		AC_CHECK_PROG(acx_pthread_config, pthread-config, yes, no)
		if test x"$acx_pthread_config" = xno; then continue; fi
		PTHREAD_CFLAGS="`pthread-config --cflags`"
		PTHREAD_LIBS="`pthread-config --ldflags` `pthread-config --libs`"
		;;

                *)
                AC_MSG_CHECKING([for the pthreads library -l$flag])
                PTHREAD_LIBS="-l$flag"
                ;;
        esac

        save_LIBS="$LIBS"
        save_CFLAGS="$CFLAGS"
        LIBS="$PTHREAD_LIBS $LIBS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"

        # Check for various functions.  We must include pthread.h,
        # since some functions may be macros.  (On the Sequent, we
        # need a special flag -Kthread to make this header compile.)
        # We check for pthread_join because it is in -lpthread on IRIX
        # while pthread_create is in libc.  We check for pthread_attr_init
        # due to DEC craziness with -lpthreads.  We check for
        # pthread_cleanup_push because it is one of the few pthread
        # functions on Solaris that doesn't have a non-functional libc stub.
        # We try pthread_create on general principles.
        AC_TRY_LINK([#include <pthread.h>],
                    [pthread_t th; pthread_join(th, 0);
                     pthread_attr_init(0); pthread_cleanup_push(0, 0);
                     pthread_create(0,0,0,0); pthread_cleanup_pop(0); ],
                    [acx_pthread_ok=yes])

        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"

        AC_MSG_RESULT($acx_pthread_ok)
        if test "x$acx_pthread_ok" = xyes; then
                break;
        fi

        PTHREAD_LIBS=""
        PTHREAD_CFLAGS=""
done
fi

# Various other checks:
if test "x$acx_pthread_ok" = xyes; then
        save_LIBS="$LIBS"
        LIBS="$PTHREAD_LIBS $LIBS"
        save_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"

        # Detect AIX lossage: JOINABLE attribute is called UNDETACHED.
	AC_MSG_CHECKING([for joinable pthread attribute])
	attr_name=unknown
	for attr in PTHREAD_CREATE_JOINABLE PTHREAD_CREATE_UNDETACHED; do
	    AC_TRY_LINK([#include <pthread.h>], [int attr=$attr; return attr;],
                        [attr_name=$attr; break])
	done
        AC_MSG_RESULT($attr_name)
        if test "$attr_name" != PTHREAD_CREATE_JOINABLE; then
            AC_DEFINE_UNQUOTED(PTHREAD_CREATE_JOINABLE, $attr_name,
                               [Define to necessary symbol if this constant
                                uses a non-standard name on your system.])
        fi

        AC_MSG_CHECKING([if more special flags are required for pthreads])
        flag=no
        case "${host_cpu}-${host_os}" in
            *-aix* | *-freebsd* | *-darwin*) flag="-D_THREAD_SAFE";;
            *solaris* | *-osf* | *-hpux*) flag="-D_REENTRANT";;
        esac
        AC_MSG_RESULT(${flag})
        if test "x$flag" != xno; then
            PTHREAD_CFLAGS="$flag $PTHREAD_CFLAGS"
        fi

        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"

        # More AIX lossage: must compile with xlc_r or cc_r
	if test x"$GCC" != xyes; then
          AC_CHECK_PROGS(PTHREAD_CC, xlc_r cc_r, ${CC})
        else
          PTHREAD_CC=$CC
	fi
else
        PTHREAD_CC="$CC"
fi

AC_SUBST(PTHREAD_LIBS)
AC_SUBST(PTHREAD_CFLAGS)
AC_SUBST(PTHREAD_CC)

# Finally, execute ACTION-IF-FOUND/ACTION-IF-NOT-FOUND:
if test x"$acx_pthread_ok" = xyes; then
        ifelse([$1],,AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.]),[$1])
        :
else
        acx_pthread_ok=no
        $2
fi
AC_LANG_RESTORE
])dnl ACX_PTHREAD
