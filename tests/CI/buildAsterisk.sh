#!/bin/bash

CIDIR=$(dirname $(readlink -fn $0))
source $CIDIR/ci.functions

gen_cats() {
	set +x
	action=$1
	shift
	cats=$@

	for x in $cats ; do
		echo " --${action}-category ${x}"
	done
}

gen_mods() {
	set +x
	action=$1
	shift
	mods=$@

	for x in $mods ; do
		echo " --${action} ${x}"
	done
}

sudo mkdir -p /srv/cache/externals /srv/cache/sounds || :
sudo chown -R jenkins:users /srv/cache
[ ! -d tests/CI/output ] && mkdir tests/CI/output
sudo chown -R jenkins:users tests/CI/output

MAKE=`which make`
printenv | sort

common_config_args="--sysconfdir=/etc --with-pjproject-bundled"
common_config_args+=" --with-sounds-cache=/srv/cache/sounds --with-externals-cache=/srv/cache/externals"
common_config_args+=" --enable-dev-mode"
export WGET_EXTRA_ARGS="--quiet"

runner ./configure ${common_config_args} CCACHE_DISABLE=1 >tests/CI/output/configure.txt

runner ${MAKE} menuselect.makeopts

runner menuselect/menuselect `gen_mods enable DONT_OPTIMIZE BETTER_BACKTRACES MALLOC_DEBUG DO_CRASH TEST_FRAMEWORK` menuselect.makeopts
runner menuselect/menuselect `gen_mods disable COMPILE_DOUBLE BUILD_NATIVE` menuselect.makeopts

cat_enables="MENUSELECT_BRIDGES MENUSELECT_CEL MENUSELECT_CDR"
cat_enables+=" MENUSELECT_CHANNELS MENUSELECT_CODECS MENUSELECT_FORMATS MENUSELECT_FUNCS"
cat_enables+=" MENUSELECT_PBX MENUSELECT_RES MENUSELECT_UTILS MENUSELECT_TESTS"
runner menuselect/menuselect `gen_cats enable $cat_enables` menuselect.makeopts

mod_disables="res_digium_phone chan_vpb"
[ "$BRANCH_NAME" == "master" ] && mod_disables+=" codec_opus codec_silk codec_g729a codec_siren7 codec_siren14"
runner menuselect/menuselect `gen_mods disable $mod_disables` menuselect.makeopts

mod_enables="app_voicemail app_directory FILE_STORAGE"
mod_enables+=" res_mwi_external res_ari_mailboxes res_mwi_external_ami res_stasis_mailbox"
mod_enables+=" CORE-SOUNDS-EN-GSM MOH-OPSOUND-GSM EXTRA-SOUNDS-EN-GSM"
runner menuselect/menuselect `gen_mods enable $mod_enables` menuselect.makeopts

runner ${MAKE} -j8 || runner ${MAKE} -j1 NOISY_BUILD=yes

ALEMBIC=$(which alembic 2>/dev/null || : )
if [ x"$ALEMBIC" = x ] ; then
	echo "Alembic not installed"
	exit 1
fi

cd contrib/ast-db-manage
find -name *.pyc -delete
out=$(alembic -c config.ini.sample branches)
if [ "x$out" != "x" ] ; then
	>&2 echo "Alembic branches were found for config"
	>&2 echo $out
	exit 1
else
	>&2 echo "Alembic for 'config' OK"
fi

out=$(alembic -c cdr.ini.sample branches)
if [ "x$out" != "x" ] ; then
	>&2 echo "Alembic branches were found for cdr"
	>&2 echo $out
	exit 1
else
	>&2 echo "Alembic for 'cdr' OK"
fi

out=$(alembic -c voicemail.ini.sample branches)
if [ "x$out" != "x" ] ; then
	>&2 echo "Alembic branches were found for voicemail"
	>&2 echo $out
	exit 1
else
	>&2 echo "Alembic for 'voicemail' OK"
fi

if [ -f "doc/core-en_US.xml" ] ; then
	${MAKE} validate-docs || ${MAKE} NOISY_BUILD=yes validate-docs
fi


