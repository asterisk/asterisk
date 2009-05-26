dnl 
dnl @synopsis AST_CHECK_OSPTK([REQ_VER_MAJOR],[REQ_VER_MINOR],[REQ_VER_BUGFIX])
dnl
dnl @summary check for existence of OSP Toolkit package
dnl
dnl This macro check for existence of OSP Toolkit package by checking osp/osp.h
dnl header file, OSPPInit function and OSP Toolkit version.
dnl
AC_DEFUN([AST_CHECK_OSPTK],
[
	# if OSPTK has not been checked and is not excluded
	if test "x${PBX_OSPTK}" != "x1" -a "${USE_OSPTK}" != "no"; then
		# if --with-osptk=DIR has been specified, use it.
		if test "x${OSPTK_DIR}" != "x"; then
			osptk_cflags="-I${OSPTK_DIR}/include"
			osptk_ldflags="-L${OSPTK_DIR}/lib"
		else
			osptk_cflags=""
			osptk_ldflags=""
		fi

		# check for the header
		osptk_saved_cppflags="${CPPFLAGS}"
		CPPFLAGS="${CPPFLAGS} ${osptk_cflags}"
		AC_CHECK_HEADER([osp/osp.h], [osptk_header_found=yes], [osptk_header_found=no])
		CPPFLAGS="${osptk_saved_cppflags}"

		# check for the library
		if test "${osptk_header_found}" = "yes"; then
			osptk_extralibs="-lssl -lcrypto"

			AC_CHECK_LIB([osptk], [OSPPInit], [osptk_library_found=yes], [osptk_library_found=no], ${osptk_ldflags} ${osptk_extralibs})

			# check OSP Toolkit version
			if test "${osptk_library_found}" = "yes"; then
				AC_MSG_CHECKING(if OSP Toolkit version is compatible with app_osplookup)

				osptk_saved_cppflags="${CPPFLAGS}"
				CPPFLAGS="${CPPFLAGS} ${osptk_cflags}"
				AC_RUN_IFELSE(
					[AC_LANG_SOURCE([[
						#include <osp/osp.h>
						int main(void) {
							int ver = OSP_CLIENT_TOOLKIT_VERSION_MAJOR * 10000 + OSP_CLIENT_TOOLKIT_VERSION_MINOR * 100 + OSP_CLIENT_TOOLKIT_VERSION_BUGFIX;
							int req = $1 * 10000 + $2 * 100 + $3;
							return (ver < req) ? 1 : 0;
						}
					]])],
					[osptk_compatible=yes],
					[osptk_compatible=no]
				)
				CPPFLAGS="${osptk_saved_cppflags}"

				if test "${osptk_compatible}" = "yes"; then
					AC_MSG_RESULT(yes)
					PBX_OSPTK=1
					OSPTK_INCLUDE="${osptk_cflags}"
					OSPTK_LIB="${osptk_ldflags} -losptk ${osptk_extralibs}"
					AC_DEFINE_UNQUOTED([HAVE_OSPTK], 1, [Define this to indicate the ${OSPTK_DESCRIP} library])
				else
					AC_MSG_RESULT(no)
				fi
			fi
		fi
	fi
])

