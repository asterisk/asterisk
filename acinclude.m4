# AST_GCC_ATTRIBUTE([attribute name])

AC_DEFUN([AST_GCC_ATTRIBUTE],
[
AC_MSG_CHECKING(for compiler 'attribute $1' support)
AC_COMPILE_IFELSE(
	AC_LANG_PROGRAM([static int __attribute__(($1)) test(void) {}],
			[]),
	AC_MSG_RESULT(yes)
	AC_DEFINE_UNQUOTED([HAVE_ATTRIBUTE_$1], 1, [Define to 1 if your GCC C compiler supports the '$1' attribute.]),
	AC_MSG_RESULT(no))
])

# AST_EXT_LIB_SETUP([package symbol name], [package friendly name], [package option name], [additional help text])

AC_DEFUN([AST_EXT_LIB_SETUP],
[
$1_DESCRIP="$2"
$1_OPTION="$3"
AC_ARG_WITH([$3], AC_HELP_STRING([--with-$3=PATH],[use $2 files in PATH $4]),[
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
AC_SUBST([PBX_$1])
])

# Check whether any of the mandatory modules are not present, and
# print error messages in case.

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
		AC_MSG_NOTICE(***)
		AC_MSG_NOTICE(*** The $i installation appears to be missing or broken.)
		AC_MSG_NOTICE(*** Either correct the installation, or run configure)
		AC_MSG_NOTICE(*** including --without-${a}.)
		err=1
	done
	if test $err = 1 ; then exit 1; fi
	AC_MSG_RESULT(ok)
])

#-- The following two tests are only performed if PBX_$1 != 1,
#   so you can use multiple tests and stop at the first matching one.
#   On success, set PBX_$1 = 1, and also #define HAVE_$1 1
#   and #define HAVE_$1_VERSION ${last_argument} so you can tell which
#   test succeeded.
#   They should be called after AST_EXT_LIB_SETUP($1, ...)

# Check if a given macro is defined in a certain header.

# AST_C_DEFINE_CHECK([package symbol name], [macro name], [header file], [version])
AC_DEFUN([AST_C_DEFINE_CHECK],
[
    if test "x${PBX_$1}" != "x1" -a "${USE_$1}" != "no"; then
	AC_MSG_CHECKING([for $2 in $3])
	saved_cppflags="${CPPFLAGS}"
	if test "x${$1_DIR}" != "x"; then
	    $1_INCLUDE= "-I${$1_DIR}/include"
	fi
	CPPFLAGS="${CPPFLAGS} ${$1_INCLUDE}"

	AC_COMPILE_IFELSE(
	    [ AC_LANG_PROGRAM( [#include <$3>], [int foo = $2;]) ],
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

# AST_EXT_LIB_CHECK([package symbol name], [package library name], [function to check], [package header], [additional LIB data], [version])
AC_DEFUN([AST_EXT_LIB_CHECK],
[
if test "x${PBX_$1}" != "x1" -a "${USE_$1}" != "no"; then
   pbxlibdir=""
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

   if test "${AST_$1_FOUND}" = "yes"; then
      $1_LIB="-l$2 $5"
      $1_HEADER_FOUND="1"
      if test "x${$1_DIR}" != "x"; then
         $1_LIB="${pbxlibdir} ${$1_LIB}"
	 $1_INCLUDE="-I${$1_DIR}/include"
	 if test "x$4" != "x" ; then
	    AC_CHECK_HEADER([${$1_DIR}/include/$4], [$1_HEADER_FOUND=1], [$1_HEADER_FOUND=0] )
	 fi
      else
	 if test "x$4" != "x" ; then
            AC_CHECK_HEADER([$4], [$1_HEADER_FOUND=1], [$1_HEADER_FOUND=0] )
	 fi
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
	 AC_DEFINE_UNQUOTED([HAVE_$1_VERSION], [$6], [Define to indicate the ${$1_DESCRIP} library version])
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
if test "${PWLIBDIR:-unset}" != "unset" ; then
  AC_CHECK_FILE(${PWLIBDIR}/version.h, HAS_PWLIB=1, )
fi
if test "${HAS_PWLIB:-unset}" = "unset" ; then
  if test "${OPENH323DIR:-unset}" != "unset"; then
    AC_CHECK_FILE(${OPENH323DIR}/../pwlib/version.h, HAS_PWLIB=1, )
  fi
  if test "${HAS_PWLIB:-unset}" != "unset" ; then
    PWLIBDIR="${OPENH323DIR}/../pwlib"
  else
    AC_CHECK_FILE(${HOME}/pwlib/include/ptlib.h, HAS_PWLIB=1, )
    if test "${HAS_PWLIB:-unset}" != "unset" ; then
      PWLIBDIR="${HOME}/pwlib"
    else
      AC_CHECK_FILE(/usr/local/include/ptlib.h, HAS_PWLIB=1, )
      if test "${HAS_PWLIB:-unset}" != "unset" ; then
        AC_PATH_PROG(PTLIB_CONFIG, ptlib-config, , /usr/local/bin)
        if test "${PTLIB_CONFIG:-unset}" = "unset" ; then
          AC_PATH_PROG(PTLIB_CONFIG, ptlib-config, , /usr/local/share/pwlib/make)
        fi
        PWLIB_INCDIR="/usr/local/include"
        if test "x$LIB64" != "x"; then
          PWLIB_LIBDIR="/usr/local/lib64"
        else
          PWLIB_LIBDIR="/usr/local/lib"
        fi
      else
        AC_CHECK_FILE(/usr/include/ptlib.h, HAS_PWLIB=1, )
        if test "${HAS_PWLIB:-unset}" != "unset" ; then
          AC_PATH_PROG(PTLIB_CONFIG, ptlib-config, , /usr/share/pwlib/make)
          PWLIB_INCDIR="/usr/include"
          if test "x$LIB64" != "x"; then
          	PWLIB_LIBDIR="/usr/lib64"
          else
	        PWLIB_LIBDIR="/usr/lib"
	      fi
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
if test "${OPENH323DIR:-unset}" != "unset" ; then
  AC_CHECK_FILE(${OPENH323DIR}/version.h, HAS_OPENH323=1, )
fi
if test "${HAS_OPENH323:-unset}" = "unset" ; then
  AC_CHECK_FILE(${PWLIBDIR}/../openh323/version.h, OPENH323DIR="${PWLIBDIR}/../openh323"; HAS_OPENH323=1, )
  if test "${HAS_OPENH323:-unset}" != "unset" ; then
    OPENH323DIR="${PWLIBDIR}/../openh323"
    AC_CHECK_FILE(${OPENH323DIR}/include/h323.h, , OPENH323_INCDIR="${PWLIB_INCDIR}/openh323"; OPENH323_LIBDIR="${PWLIB_LIBDIR}")
  else
    AC_CHECK_FILE(${HOME}/openh323/include/h323.h, HAS_OPENH323=1, )
    if test "${HAS_OPENH323:-unset}" != "unset" ; then
      OPENH323DIR="${HOME}/openh323"
    else
      AC_CHECK_FILE(/usr/local/include/openh323/h323.h, HAS_OPENH323=1, )
      if test "${HAS_OPENH323:-unset}" != "unset" ; then
        OPENH323DIR="/usr/local/share/openh323"
        OPENH323_INCDIR="/usr/local/include/openh323"
        if test "x$LIB64" != "x"; then
          OPENH323_LIBDIR="/usr/local/lib64"
        else
          OPENH323_LIBDIR="/usr/local/lib"
        fi
      else
        AC_CHECK_FILE(/usr/include/openh323/h323.h, HAS_OPENH323=1, )
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

  AC_SUBST([OPENH323DIR])
  AC_SUBST([OPENH323_INCDIR])
  AC_SUBST([OPENH323_LIBDIR])
fi
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
	   LIBS="${LIBS} -L${$2_LIBDIR} -l${PLATFORM_$2} $7"
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
	      if test "${$2_LIBDIR}" != "" -a "${$2_LIBDIR}" != "/usr/lib"; then
	         $2_LIB="-L${$2_LIBDIR} -l${PLATFORM_$2}"
	      else
	         $2_LIB="-l${PLATFORM_$2}"
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
		files=`ls -l ${OPENH323_LIBDIR}/libh323_${PWLIB_PLATFORM}_*.so*`
		libfile=
		if test -n "$files"; then
			for f in $files; do
				if test -f $f -a ! -L $f; then
					libfile=`basename $f`
					break;
				fi
			done
		fi
		if test "${libfile:-unset}" != "unset"; then
			OPENH323_SUFFIX=`eval "echo ${libfile} | sed -e 's/libh323_${PWLIB_PLATFORM}_\(@<:@^.@:>@*\)\..*/\1/'"`
		fi
		case "${OPENH323_SUFFIX}" in
			n)
				OPENH323_BUILD="notrace";;
			r)
				OPENH323_BUILD="opt";;
			d)
				OPENH323_BUILD="debug";;
			*)
				OPENH323_BUILD="notrace";;
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
    *-*-amigaos* | *-*-msdosdjgpp* | *-*-uclinux* )
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

