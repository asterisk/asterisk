#
# If this file is changed, be sure to run ASTTOPDIR/bootstrap.sh
# before committing.
#

AC_DEFUN([THIRD_PARTY_CONFIGURE],
[
	JANSSON_CONFIGURE()
	PJPROJECT_CONFIGURE()
	LIBJWT_CONFIGURE()
])
