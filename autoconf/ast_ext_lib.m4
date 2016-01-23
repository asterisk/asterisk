# Helper function to setup variables for a package.
# $1 -> the package name. Used in configure.ac and also as a prefix
#	for the variables ($1_DIR, $1_INCLUDE, $1_LIB) in makeopts
# $3 ->	option name, used in --with-$3 or --without-$3 when calling configure.
# $2 and $4 are just text describing the package (short and long form)
# $5 is the capability to fulfill (defaults to package)
# $6 is the action to take if not specified

# AST_EXT_LIB_SETUP([package], [short description], [configure option name], [long description], [capability], [action if not specified])

AC_DEFUN([AST_EXT_LIB_SETUP],
[
    $1_DESCRIP="$2"
    $1_OPTION="$3"
    PBX_$1=0
    AC_ARG_WITH([$3], AC_HELP_STRING([--with-$3=PATH],[use $2 files in PATH$4]),
    [
	case ${withval} in
	n|no)
	USE_$1=no
	# -1 is a magic value used by menuselect to know that the package
	# was disabled, other than 'not found'
	PBX_$1=-1
	;;
	y|ye|yes)
	if test -n "$5" -a "${ac_mandatory_list#* $5 *}" != "$ac_mandatory_list" ; then
		AC_MSG_ERROR(An option with capability $5 has already been specified)
	fi
	USE_$1=yes
	ac_mandatory_list="$ac_mandatory_list m4_ifval([$5], [$5], [$1]) "
	;;
	*)
	if test -n "$5" -a "${ac_mandatory_list#* $5 *}" != "$ac_mandatory_list" ; then
		AC_MSG_ERROR(An option with capability $5 has already been specified)
	fi
	USE_$1=yes
	$1_DIR="${withval}"
	ac_mandatory_list="$ac_mandatory_list m4_ifval([$5], [$5], [$5]) "
	;;
	esac
    ], [ USE_$1=notspecified ; $6 ])
    AH_TEMPLATE(m4_bpatsubst([[HAVE_$1]], [(.*)]), [Define to 1 if you have the $2 library.])
    AC_SUBST([$1_LIB])
    AC_SUBST([$1_INCLUDE])
    AC_SUBST([$1_DIR])
    AC_SUBST([PBX_$1])
])

# AST_OPTION_ONLY([option name], [option variable], [option description], [default value])
AC_DEFUN([AST_OPTION_ONLY],
[
AC_ARG_WITH([$1], AC_HELP_STRING([--with-$1=PATH], [use $3 in PATH]),
	[
	case ${withval} in
	n|no)
		unset $2
		;;
	*)
		if test "x${withval}" = "x"; then
			m4_ifval($4, [$2="$4"], [:])
		else
			$2="${withval}"
		fi
		;;
	esac
	],
	[m4_ifval($4, [$2="$4"], [:])])
AC_SUBST($2)
])

# Setup required dependent package
# AST_EXT_LIB_SETUP_DEPENDENT([dependent package symbol name], [dependent package friendly name], [master package symbol name], [master package name])

AC_DEFUN([AST_EXT_LIB_SETUP_DEPENDENT],
[
$1_DESCRIP="$2"
m4_ifval([$4], [$1_OPTION=$4])
m4_ifval([$3], [
for i in ${ac_mandatory_list}; do
   if test "x$3" = "x$i"; then
      ac_mandatory_list="${ac_mandatory_list} $1 "
      break
   fi
done
$1_DIR=${$3_DIR}
])
PBX_$1=0
AH_TEMPLATE(m4_bpatsubst([[HAVE_$1]], [(.*)]), [Define to 1 if you have the $2 library.])
AC_SUBST([$1_LIB])
AC_SUBST([$1_INCLUDE])
AC_SUBST([$1_DIR])
AC_SUBST([PBX_$1])
])

# Setup optional dependent package
# AST_EXT_LIB_SETUP_OPTIONAL([optional package symbol name], [optional package friendly name], [master package symbol name], [master package name])

AC_DEFUN([AST_EXT_LIB_SETUP_OPTIONAL],
[
$1_DESCRIP="$2"
m4_ifval([$4], [$1_OPTION=$4])
m4_ifval([$3], [$1_DIR=${$3_DIR}
])
PBX_$1=0
AH_TEMPLATE(m4_bpatsubst([[HAVE_$1]], [(.*)]), [Define to 1 if $3 has the $2 feature.])
AC_SUBST([$1_LIB])
AC_SUBST([$1_INCLUDE])
AC_SUBST([$1_DIR])
AC_SUBST([PBX_$1])
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
      ast_ext_lib_check_save_CFLAGS="${CFLAGS}"
      CFLAGS="${CFLAGS} $6"
      AC_CHECK_LIB([$2], [${pbxfuncname}], [AST_$1_FOUND=yes], [AST_$1_FOUND=no], [${pbxlibdir} $5])
      CFLAGS="${ast_ext_lib_check_save_CFLAGS}"
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
         ast_ext_lib_check_saved_CPPFLAGS="${CPPFLAGS}"
         CPPFLAGS="${CPPFLAGS} ${$1_INCLUDE}"
         AC_CHECK_HEADER([$4], [$1_HEADER_FOUND=1], [$1_HEADER_FOUND=0])
         CPPFLAGS="${ast_ext_lib_check_saved_CPPFLAGS}"
      fi
      if test "x${$1_HEADER_FOUND}" = "x0" ; then
         $1_LIB=""
         $1_INCLUDE=""
      else
         if test "x${pbxfuncname}" = "x" ; then		# only checking headers -> no library
            $1_LIB=""
         fi
         PBX_$1=1
         cat >>confdefs.h <<_ACEOF
[@%:@define] HAVE_$1 1
_ACEOF
         m4_ifval([$7], [
         cat >>confdefs.h <<_ACEOF
[@%:@define] HAVE_$1_VERSION $7
_ACEOF
            ])
      fi
   fi
fi
m4_ifval([$7], [AH_TEMPLATE(m4_bpatsubst([[HAVE_$1_VERSION]], [(.*)]), [Define to the version of the $2 library.])])
])
