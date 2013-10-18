#!/bin/sh

#
# Downloads Swagger-UI to put in static-http.
#
# Swagger-UI is a Swagger compliant HTML+JavaScript web app, which can be used
# to browse ARI (Asterisk REST Interface).
#

PROGNAME=$(basename $0)

: ${GIT:=git}
: ${REPO:=https://github.com/leedm777/swagger-ui.git}
: ${BRANCH:=asterisk}

if ! test -d static-http; then
    echo "${PROGNAME}: Must run from Asterisk source directory" >&2
    exit 1
fi

set -ex

CLONE_DIR=$(mktemp -d /tmp/swagger-ui.XXXXXX) || exit 1
trap "rm -rf ${CLONE_DIR}" EXIT

${GIT} clone -q -b ${BRANCH} ${REPO} ${CLONE_DIR}

rm -rf static-http/swagger-ui
cp -a ${CLONE_DIR}/dist static-http/swagger-ui

cat <<EOF
Swagger-UI downloaded. Install using 'make install'.

To use, enable  ARI (ari.conf), the HTTP server (http.conf) and static
content (also http.conf).
EOF
