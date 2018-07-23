#!/usr/bin/env bash
CIDIR=$(dirname $(readlink -fn $0))
source $CIDIR/ci.functions
ASTETCDIR=$DESTDIR/etc/asterisk

cat <<-EOF > "$ASTETCDIR/logger.conf"
	[logfiles]
	full => notice,warning,error,debug,verbose
EOF

echo "[default]" > "$ASTETCDIR/extensions.conf"

cat <<-EOF > "$ASTETCDIR/manager.conf"
	[general]
	enabled=yes
	bindaddr=127.0.0.1
	port=5038

	[test]
	secret=test
	read = system,call,log,verbose,agent,user,config,dtmf,reporting,cdr,dialplan
	write = system,call,agent,user,config,command,reporting,originate
EOF

cat <<-EOF > "$ASTETCDIR/http.conf"
	[general]
	enabled=yes
	bindaddr=127.0.0.1
	port=8088
EOF

cat <<-EOF > "$ASTETCDIR/modules.conf"
	[modules]
	autoload=yes
	noload=res_mwi_external.so
	noload=res_mwi_external_ami.so
	noload=res_ari_mailboxes.so
	noload=res_stasis_mailbox.so
EOF

cat <<-EOF >> "$ASTETCDIR/sorcery.conf"
	[res_pjsip_pubsub]
	resource_list=memory
EOF

ASTERISK="$DESTDIR/usr/sbin/asterisk"
CONFFILE=$ASTETCDIR/asterisk.conf
OUTPUTDIR=${OUTPUT_DIR:-tests/CI/output/}
OUTPUTFILE=${OUTPUT_XML:-${OUTPUTDIR}/unittests-results.xml}

[ ! -d ${OUTPUTDIR} ] && mkdir -p $OUTPUTDIR
[ x"$USER_GROUP" != x ] && sudo chown -R $USER_GROUP $OUTPUTDIR

rm -rf $ASTETCDIR/extensions.{ael,lua} || :

set -x
sudo $ASTERISK ${USER_GROUP:+-U ${USER_GROUP%%:*} -G ${USER_GROUP##*:}} -gn -C $CONFFILE
for n in `seq 1 5` ; do
	sleep 3
	$ASTERISK -rx "core waitfullybooted" -C $CONFFILE && break
done
sleep 1
$ASTERISK -rx "${UNITTEST_COMMAND:-test execute all}" -C $CONFFILE
$ASTERISK -rx "test show results failed" -C $CONFFILE
$ASTERISK -rx "test generate results xml $OUTPUTFILE" -C $CONFFILE
$ASTERISK -rx "core stop now" -C $CONFFILE

runner rsync -vaH $DESTDIR/var/log/asterisk/. $OUTPUTDIR
set +x

[ x"$USER_GROUP" != x ] && sudo chown -R $USER_GROUP $OUTPUTDIR
if [ -f core* ] ; then
	echo "*** Found a core file after running unit tests ***"
	$DESTDIR/var/lib/asterisk/scripts/ast_coredumper --no-default-search core*
	exit 1
fi
