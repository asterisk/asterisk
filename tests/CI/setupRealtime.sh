#!/usr/bin/env bash
CIDIR=$(dirname $(readlink -fn $0))
INITIALIZE_DB=0
source $CIDIR/ci.functions
ASTTOP=$(readlink -fn $CIDIR/../../)

set -e

POSTGRES_PID=`pidof postgres || : `

if [ -z "$POSTGRES_PID" ] ; then
	if [ -x /usr/local/bin/postgresql-start ] ; then
		/usr/local/bin/postgresql-start
	fi
fi

POSTGRES_PID=`pidof postgres || : `
if [ -z "$POSTGRES_PID" ] ; then
	echo "Postgres isn't running. It must be started manually before this test can continue."
	exit 1
fi

if [ $INITIALIZE_DB -gt 0 ] ; then
	echo "(re)Initializing Database"

	sudo -u postgres dropdb -e asterisk_test >/dev/null 2>&1 || :
	sudo -u postgres dropuser -e asterisk_test >/dev/null  2>&1 || :
	sudo -u postgres createuser --username=postgres -RDIElS asterisk_test
	sudo -u postgres createdb --username=postgres -E UTF-8 -O asterisk_test asterisk_test

	echo "Configuring ODBC"

	sudo odbcinst -u -d -n "PostgreSQL-Asterisk-Test"

	sudo odbcinst -i -d -n "PostgreSQL-Asterisk-Test" -f /dev/stdin <<-EOF
		[PostgreSQL-Asterisk-Test]
		Description=PostgreSQL ODBC driver (Unicode version)
		Driver=psqlodbcw.so
		Setup=libodbcpsqlS.so
		Debug=0
		CommLog=1
		UsageCount=1
	EOF

	sudo odbcinst -u -s -l -n asterisk-connector-test
	sudo odbcinst -i -s -l -n asterisk-connector-test -f /dev/stdin <<-EOF
		[asterisk-connector-test]
		Description        = PostgreSQL connection to 'asterisk' database
		Driver             = PostgreSQL-Asterisk-Test
		Database           = asterisk_test
		Servername         = 127.0.0.1
		UserName           = asterisk_test
		Port               = 5432
		Protocol           = 9.1
		ReadOnly           = No
		RowVersioning      = No
		ShowSystemTables   = No
		ShowOldColumn      = No
		FakeOldIndex       = No
		ConnSettings       =
	EOF
fi

cat >/tmp/config.ini <<-EOF
	[alembic]
	script_location = config
	sqlalchemy.url = postgresql://asterisk_test@localhost/asterisk_test

	[loggers]
	keys = root,sqlalchemy,alembic

	[handlers]
	keys = console

	[formatters]
	keys = generic

	[logger_root]
	level = WARN
	handlers = console
	qualname =

	[logger_sqlalchemy]
	level = WARN
	handlers =
	qualname = sqlalchemy.engine

	[logger_alembic]
	level = INFO
	handlers =
	qualname = alembic

	[handler_console]
	class = StreamHandler
	args = (sys.stderr,)
	level = NOTSET
	formatter = generic

	[formatter_generic]
	format = %(levelname)-5.5s [%(name)s] %(message)s
	datefmt = %H:%M:%S
EOF

pushd $ASTTOP/contrib/ast-db-manage

psql --username=asterisk_test --host=localhost --db=asterisk_test --command='DROP OWNED BY asterisk_test CASCADE'
alembic -c /tmp/config.ini upgrade head
rm -rf /tmp/config.ini || :

popd

cp test-config.yaml test-config.orig.yaml

cat >test-config.yaml <<-EOF
	global-settings:
	    test-configuration: config-realtime

	    condition-definitions:
	        -
	            name: 'threads'
	            pre:
	                typename: 'thread_test_condition.ThreadPreTestCondition'
	            post:
	                typename: 'thread_test_condition.ThreadPostTestCondition'
	                related-type: 'thread_test_condition.ThreadPreTestCondition'
	        -
	            name: 'sip-dialogs'
	            pre:
	                typename: 'sip_dialog_test_condition.SipDialogPreTestCondition'
	            post:
	                typename: 'sip_dialog_test_condition.SipDialogPostTestCondition'
	        -
	            name: 'locks'
	            pre:
	                typename: 'lock_test_condition.LockTestCondition'
	            post:
	                typename: 'lock_test_condition.LockTestCondition'
	        -
	            name: 'file-descriptors'
	            pre:
	                typename: 'fd_test_condition.FdPreTestCondition'
	            post:
	                typename: 'fd_test_condition.FdPostTestCondition'
	                related-type: 'fd_test_condition.FdPreTestCondition'
	        -
	            name: 'channels'
	            pre:
	                typename: 'channel_test_condition.ChannelTestCondition'
	            post:
	                typename: 'channel_test_condition.ChannelTestCondition'
	        -
	            name: 'sip-channels'
	            pre:
	                typename: 'sip_channel_test_condition.SipChannelTestCondition'
	            post:
	                typename: 'sip_channel_test_condition.SipChannelTestCondition'
	        -
	            name: 'memory'
	            pre:
	                typename: 'memory_test_condition.MemoryPreTestCondition'
	            post:
	                typename: 'memory_test_condition.MemoryPostTestCondition'
	                related-type: 'memory_test_condition.MemoryPreTestCondition'

	config-realtime:
	    test-modules:
	        modules:
	            -
	                typename: realtime_converter.RealtimeConverter
	                config-section: realtime-config
		
	    realtime-config:
	        username: "asterisk_test"
	        password: "asterisk_test"
	        host: "localhost"
	        db: "asterisk_test"
	        dsn: "asterisk-connector-test"
EOF

