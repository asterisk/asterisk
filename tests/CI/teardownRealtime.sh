#!/usr/bin/env bash
CIDIR=$(dirname $(readlink -fn $0))
source $CIDIR/ci.functions

cp test-config.orig.yaml test-config.yaml
psql --username=asterisk --host=localhost --db=asterisk --command='DROP OWNED BY asterisk CASCADE'
