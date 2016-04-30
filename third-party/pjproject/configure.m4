AC_DEFUN([PJPROJECT_SYMBOL_CHECK],
[
	$1_INCLUDE="$PJPROJECT_INCLUDE"
    AC_MSG_CHECKING([for $2 declared in $3])

	saved_cpp="$CPPFLAGS"
	CPPFLAGS="$PJPROJECT_INCLUDE"
	AC_EGREP_HEADER($2, $3, [
		AC_MSG_RESULT(yes)
		PBX_$1=1
		AC_DEFINE([HAVE_$1], 1, [Define if your system has $2 declared.])
	], [
		AC_MSG_RESULT(no)
	])

	CPPGLAGS="$saved_cpp"
	$1_INCLUDE="$PJPROJECT_INCLUDE"
])

AC_DEFUN([PJPROJECT_CONFIGURE],
[
	AC_MSG_CHECKING(for embedded pjproject (may have to download))
	AC_MSG_RESULT(configuring)
	${GNU_MAKE} --quiet --no-print-directory -C $1 configure
	if test $? -ne 0 ; then
		AC_MSG_RESULT(failed)
		AC_MSG_NOTICE(Unable to configure $1)
		AC_MSG_ERROR(Run "${GNU_MAKE} -C $1 NOISY_BUILD=yes configure" to see error details.)
	fi

	PJPROJECT_INCLUDE=$(${GNU_MAKE} --quiet --no-print-directory -C $1 echo_cflags)
	PJPROJECT_CFLAGS="$PJPROJECT_INCLUDE"
	PBX_PJPROJECT=1
	PJPROJECT_BUNDLED=yes
	AC_DEFINE([HAVE_PJPROJECT], 1, [Define if your system has PJPROJECT])
	AC_DEFINE([HAVE_PJPROJECT_BUNDLED], 1, [Define if your system has PJPROJECT_BUNDLED])
	AC_MSG_CHECKING(for embedded pjproject)
	AC_MSG_RESULT(yes)

	PJPROJECT_SYMBOL_CHECK([PJSIP_DLG_CREATE_UAS_AND_INC_LOCK], [pjsip_dlg_create_uas_and_inc_lock], [pjsip.h])
	PJPROJECT_SYMBOL_CHECK([PJ_TRANSACTION_GRP_LOCK], [pjsip_tsx_create_uac2], [pjsip.h])
	PJPROJECT_SYMBOL_CHECK([PJSIP_REPLACE_MEDIA_STREAM], [PJMEDIA_SDP_NEG_ALLOW_MEDIA_CHANGE], [pjmedia.h])
	PJPROJECT_SYMBOL_CHECK([PJSIP_GET_DEST_INFO], [pjsip_get_dest_info], [pjsip.h])
	PJPROJECT_SYMBOL_CHECK([PJ_SSL_CERT_LOAD_FROM_FILES2], [pj_ssl_cert_load_from_files2], [pjlib.h])
	PJPROJECT_SYMBOL_CHECK([PJSIP_EXTERNAL_RESOLVER], [pjsip_endpt_set_ext_resolver], [pjsip.h])
	AC_DEFINE([HAVE_PJSIP_TLS_TRANSPORT_PROTO], 1, [Define if your system has PJSIP_TLS_TRANSPORT_PROTO])
])
